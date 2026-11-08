// Essai sans interface du processeur Sillage : faux hôte (transport + entrée latérale), capture, calcul en arrière-plan, lecture, état sauvegardé.
#include <cmath>
#include <cstdio>

#include "SillageProcessor.h"
#include "SynthVoice.h"

namespace
{
struct FakeHost : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setIsPlaying (playing);
        i.setTimeInSamples (pos);
        return i;
    }
    bool playing = false;
    juce::int64 pos = 0;
};

int failures = 0;
void check (bool ok, const char* what)
{
    std::printf ("%s %s\n", ok ? "  ok  " : "ÉCHEC ", what);
    if (! ok) ++failures;
}

bool waitFor (SillageProcessor& p, SillageProcessor::Phase ph, int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        if (p.phase() == ph && ! p.busy()) return true;
    }
    return false;
}

// Joue [from, to) du morceau par blocs ; rend la sortie (voie gauche) et recopie l'entrée donnée.
std::vector<float> run (SillageProcessor& p, FakeHost& host, const std::vector<float>& dub, const std::vector<float>& guide, juce::int64 songStart, std::size_t n, bool playing = true)
{
    std::vector<float> out (n);
    juce::AudioBuffer<float> buf (4, 512);
    juce::MidiBuffer midi;
    host.playing = playing;
    for (std::size_t i = 0; i < n; i += 512)
    {
        const int len = (int) std::min<std::size_t> (512, n - i);
        buf.setSize (4, len, false, false, true);
        for (int k = 0; k < len; ++k)
        {
            const float d = (i + (std::size_t) k) < dub.size() ? dub[i + (std::size_t) k] : 0.f, g = (i + (std::size_t) k) < guide.size() ? guide[i + (std::size_t) k] : 0.f;
            buf.setSample (0, k, d); buf.setSample (1, k, d); buf.setSample (2, k, g); buf.setSample (3, k, g);
        }
        host.pos = songStart + (juce::int64) i;
        p.processBlock (buf, midi);
        for (int k = 0; k < len; ++k) out[i + (std::size_t) k] = buf.getSample (0, k);
    }
    return out;
}

// Un étireur de phase ne rend pas les mêmes échantillons que l'entrée, même à vitesse 1 : on compare les enveloppes d'énergie (fenêtres de 10 ms).
double envelopeCorrelation (const std::vector<float>& a, const std::vector<float>& b, std::size_t from, std::size_t to)
{
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
    int cnt = 0;
    for (std::size_t i = from; i + 441 < to; i += 441)
    {
        double ea = 0, eb = 0;
        for (std::size_t k = 0; k < 441; ++k) { ea += (double) a[i + k] * a[i + k]; eb += (double) b[i + k] * b[i + k]; }
        sa += ea; sb += eb; saa += ea * ea; sbb += eb * eb; sab += ea * eb; ++cnt;
    }
    const double cov = sab / cnt - (sa / cnt) * (sb / cnt), va = saa / cnt - std::pow (sa / cnt, 2), vb = sbb / cnt - std::pow (sb / cnt, 2);
    return cov / std::sqrt (std::max (1e-30, va * vb));
}

double residualMs (const synth::Pair& pr, const std::vector<float>& out, std::size_t offset)
{
    std::vector<float> seg (out.begin() + (std::ptrdiff_t) offset, out.end());
    const auto re = align::alignSignals (pr.guide.audio.data(), pr.guide.audio.size(), seg.data(), seg.size(), 44100.0);
    double s = 0;
    for (const auto& n : pr.guide.notes) s += std::fabs (re[std::min ((std::size_t) (n.start / 0.005), re.size() - 1)] - n.start) * 1000.0;
    return s / (double) pr.guide.notes.size();
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto pr = synth::makePair (3, 0.10);
    const std::size_t N = pr.guide.audio.size();
    const juce::int64 songStart = 44100; // la phrase commence 1 s après le début du morceau

    SillageProcessor p;
    p.setRateAndBufferSizeDetails (44100.0, 512);
    p.prepareToPlay (44100.0, 512);
    FakeHost host;
    p.setPlayHead (&host);

    // 0. Sans capture : le double passe tel quel.
    auto pass = run (p, host, pr.dub.audio, pr.guide.audio, 0, 20000);
    double d0 = 0;
    for (std::size_t i = 0; i < pass.size(); ++i) d0 = std::max (d0, (double) std::fabs (pass[i] - pr.dub.audio[i]));
    check (d0 == 0.0, "sans capture, le double passe sans changement");

    // 1. Capture : armer, jouer toute la phrase à partir de songStart, arrêter.
    p.armCapture();
    check (p.phase() == SillageProcessor::Phase::Armed, "armé après « Capturer »");
    auto during = run (p, host, pr.dub.audio, pr.guide.audio, songStart, N);
    double d1 = 0;
    for (std::size_t i = 0; i < during.size(); ++i) d1 = std::max (d1, (double) std::fabs (during[i] - pr.dub.audio[i]));
    check (d1 == 0.0, "pendant la capture, le double passe sans changement");
    check (p.phase() == SillageProcessor::Phase::Capturing, "en capture");
    run (p, host, {}, {}, songStart + (juce::int64) N, 512, false); // un bloc à l'arrêt : la capture se termine
    const auto t0 = juce::Time::getMillisecondCounter();
    const bool ready = waitFor (p, SillageProcessor::Phase::Ready, 120000);
    std::printf ("       phase %d, attente %u ms, message : %s\n", (int) p.phase(), juce::Time::getMillisecondCounter() - t0, p.statusText().toRawUTF8());
    check (ready, "calcul terminé, prêt");
    check (p.capturedSeconds() > 5.0, "durée capturée plausible");
    check (p.display() != nullptr && p.display()->shiftMs.size() > 100, "affichage prêt");

    // 2. Lecture : avant la zone le double est inchangé, dans la zone il est recalé sur le guide.
    const std::size_t total = (std::size_t) songStart + N + 20000;
    std::vector<float> dubOnTimeline (total, 0.f), guideOnTimeline (total, 0.f);
    for (std::size_t i = 0; i < 30000 && i < total; ++i) dubOnTimeline[i] = (float) std::sin (0.05 * (double) i) * 0.1f; // du matériel avant la zone
    for (std::size_t i = 0; i < N; ++i) { dubOnTimeline[(std::size_t) songStart + i] = pr.dub.audio[i]; guideOnTimeline[(std::size_t) songStart + i] = pr.guide.audio[i]; }
    auto played = run (p, host, dubOnTimeline, guideOnTimeline, 0, total);
    double before = 0;
    for (std::size_t i = 0; i < (std::size_t) songStart - 10; ++i) before = std::max (before, (double) std::fabs (played[i] - dubOnTimeline[i]));
    check (before == 0.0, "avant la zone capturée, le double est inchangé");
    const double res = residualMs (pr, played, (std::size_t) songStart);
    std::printf ("       calage résiduel après lecture : %.1f ms\n", res);
    check (res < 8.0, "dans la zone, le double est calé sur le guide (moins de 8 ms)");
    double after = 0;
    for (std::size_t i = (std::size_t) songStart + N + 10; i < total; ++i) after = std::max (after, (double) std::fabs (played[i] - dubOnTimeline[i]));
    check (after == 0.0, "après la zone capturée, le double est inchangé");

    // 2a. Export : le WAV contient le double recalé, à la position de la capture.
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sillage_export_test.wav");
        juce::String err;
        check (p.canExport(), "export possible");
        check (p.exportAligned (tmp, err), "export écrit");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> rd (wav.createReaderFor (tmp.createInputStream().release(), true));
        check (rd != nullptr && (std::size_t) rd->lengthInSamples == N, "le WAV exporté a la durée du guide");
        if (rd)
        {
            check (rd->metadataValues[juce::WavAudioFormat::bwavTimeReference].getLargeIntValue() == songStart, "la position de la capture est écrite dans le WAV (BWF)");
            std::vector<float> ex ((std::size_t) rd->lengthInSamples);
            float* dst[1] = { ex.data() };
            rd->read (dst, 1, 0, (int) rd->lengthInSamples);
            double mx = 0;
            for (std::size_t i = 400; i + 400 < N; ++i) mx = std::max (mx, (double) std::fabs (ex[i] - played[(std::size_t) songStart + i]));
            std::printf ("       écart WAV exporté / lecture : %.2e\n", mx);
            check (mx < 1e-5, "le WAV exporté est ce qu'on entend dans la zone");
        }
        check (p.suggestedExportName().endsWith (".wav"), "nom de fichier proposé");
        tmp.deleteFile();
    }

    // 2b. Image de l'interface (si un chemin est donné) : avec une courbe de force variable posée.
    if (argc > 1)
    {
        p.setStrengthCurve ({ { 0.0, 0.0 }, { 3.0, 0.0 }, { 3.6, 1.0 }, { 7.0, 1.0 }, { 7.6, 0.2 } });
        juce::MessageManager::getInstance()->runDispatchLoopUntil (800);
        waitFor (p, SillageProcessor::Phase::Ready, 60000);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (980, 640);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f (argv[1]);
        f.deleteFile();
        juce::FileOutputStream fos (f);
        juce::PNGImageFormat png;
        png.writeImageToStream (img, fos);
        std::printf ("       image de l'interface : %s\n", argv[1]);
        p.setStrengthCurve ({});
        juce::MessageManager::getInstance()->runDispatchLoopUntil (800);
        waitFor (p, SillageProcessor::Phase::Ready, 60000);
    }

    // 3. Réglage « Recalé » décoché : l'original.
    p.parameters().getParameter ("aligned")->setValueNotifyingHost (0.f);
    auto orig = run (p, host, dubOnTimeline, guideOnTimeline, 0, total);
    double dOrig = 0;
    for (std::size_t i = 0; i < total; ++i) dOrig = std::max (dOrig, (double) std::fabs (orig[i] - dubOnTimeline[i]));
    check (dOrig == 0.0, "« Recalé » décoché : l'original passe");
    p.parameters().getParameter ("aligned")->setValueNotifyingHost (1.f);

    // 4. Force 0 : le rendu se refait tout seul et ne déforme plus.
    p.parameters().getParameter ("strength")->setValueNotifyingHost (0.f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (800);
    check (waitFor (p, SillageProcessor::Phase::Ready, 60000), "nouveau rendu terminé après changement de force");
    auto flat = run (p, host, dubOnTimeline, guideOnTimeline, 0, total);
    double dFlat = 0;
    for (std::size_t i = (std::size_t) songStart + 2000; i < (std::size_t) songStart + N - 2000; ++i) dFlat = std::max (dFlat, (double) std::fabs (flat[i] - dubOnTimeline[i]));
    const double cFlat = envelopeCorrelation (flat, dubOnTimeline, (std::size_t) songStart + 2000, (std::size_t) songStart + N - 2000);
    std::printf ("       force 0 : corrélation d'enveloppe avec l'original %.3f\n", cFlat);
    check (cFlat > 0.97, "force 0 : le double reste (à l'enveloppe près) tel quel dans la zone");
    p.parameters().getParameter ("strength")->setValueNotifyingHost (1.f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (800);
    waitFor (p, SillageProcessor::Phase::Ready, 60000);

    // 5. État sauvegardé puis restauré dans un autre exemplaire : même résultat.
    juce::MemoryBlock state;
    p.getStateInformation (state);
    std::printf ("       taille de l'état : %.1f Mo\n", (double) state.getSize() / 1048576.0);
    auto again = run (p, host, dubOnTimeline, guideOnTimeline, 0, total);
    SillageProcessor q;
    q.setRateAndBufferSizeDetails (44100.0, 512);
    q.prepareToPlay (44100.0, 512);
    q.setPlayHead (&host);
    q.setStateInformation (state.getData(), (int) state.getSize());
    check (waitFor (q, SillageProcessor::Phase::Ready, 120000), "état restauré : prêt");
    auto restored = run (q, host, dubOnTimeline, guideOnTimeline, 0, total);
    double dRest = 0;
    for (std::size_t i = 0; i < total; ++i) dRest = std::max (dRest, (double) std::fabs (restored[i] - again[i]));
    std::printf ("       écart maximal restauré / original : %.2e\n", dRest);
    std::printf ("       calage résiduel : original %.1f ms, restauré %.1f ms\n", residualMs (pr, again, (std::size_t) songStart), residualMs (pr, restored, (std::size_t) songStart));
    check (residualMs (pr, restored, (std::size_t) songStart) < 8.0, "après restauration, le double est toujours calé sur le guide");
    const double cRest = envelopeCorrelation (restored, again, (std::size_t) songStart + 2000, (std::size_t) songStart + N - 2000);
    std::printf ("       corrélation d'enveloppe restauré / original : %.4f\n", cRest);
    check (cRest > 0.99, "après restauration, la lecture a la même enveloppe que l'original");

    std::printf ("%s\n", failures == 0 ? "TOUT EST BON" : "DES ÉCHECS");
    return failures == 0 ? 0 : 1;
}
