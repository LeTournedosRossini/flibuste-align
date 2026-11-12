// SPDX-License-Identifier: GPL-3.0-or-later
// Compare Rubber Band (R2 « faster », R3 « finer ») et Signalsmith Stretch sur des doublures synthétiques à correspondance connue.
// Écrit les WAV dans le dossier donné en argument pour l'écoute, et affiche : erreur de calage, hauteur (cents), consonnes, temps de calcul.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "../tests/SynthVoice.h"
#include "align/Render.h"

namespace
{
float sampleOr0 (const std::vector<float>& x, long i) { return (i >= 0 && (size_t) i < x.size()) ? x[(size_t) i] : 0.f; }
void writeWav (const std::string& path, const std::vector<float>& x, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const uint32_t n = (uint32_t) (x.size() * 4), rate = (uint32_t) sr, br = rate * 4;
    const uint16_t fmt = 3, ch = 1, ba = 4, bits = 32;
    const uint32_t riff = 36 + n, sub = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&sub, 4, 1, f); std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f); std::fwrite (&rate, 4, 1, f);
    std::fwrite (&br, 4, 1, f); std::fwrite (&ba, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&n, 4, 1, f); std::fwrite (x.data(), 4, x.size(), f);
    std::fclose (f);
}

// Hauteur par autocorrélation sur une fenêtre de 50 ms centrée sur t (secondes). Rend 0 si rien de net.
double f0At (const std::vector<float>& x, double t, double sr)
{
    const long c = (long) (t * sr), w = (long) (0.05 * sr);
    const long lagMin = (long) (sr / 700.0), lagMax = (long) (sr / 150.0);
    double e0 = 0;
    std::vector<double> ac ((size_t) lagMax + 2, 0.0);
    for (long i = 0; i < w; ++i) { const double v = sampleOr0 (x, c - w / 2 + i); e0 += v * v; }
    if (e0 < 1e-6) return 0;
    for (long l = lagMin; l <= lagMax; ++l)
    {
        double s = 0;
        for (long i = 0; i < w; ++i) s += (double) sampleOr0 (x, c - w / 2 + i) * sampleOr0 (x, c - w / 2 + i + l);
        ac[(size_t) l] = s;
    }
    double best = 0;
    for (long l = lagMin; l <= lagMax; ++l) best = std::max (best, ac[(size_t) l]);
    if (best / e0 < 0.5) return 0;
    for (long l = lagMin + 1; l < lagMax; ++l) // premier sommet proche du maximum : évite les erreurs d'octave
        if (ac[(size_t) l] >= 0.9 * best && ac[(size_t) l] >= ac[(size_t) l - 1] && ac[(size_t) l] >= ac[(size_t) l + 1]) return sr / (double) l;
    return 0;
}

double energy (const std::vector<float>& x, double t0, double t1, double sr)
{
    double e = 0;
    for (long i = (long) (t0 * sr); i < (long) (t1 * sr); ++i) { const double v = sampleOr0 (x, i); e += v * v; }
    return e;
}

struct Score { double consLen = 0, bias = 0, timingMs = 0, pitchCents = 0, burstDb = 0, preEchoDb = 0, seconds = 0; int n = 0, np = 0, nb = 0, nl = 0; };
} // namespace

int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : "";
    const double sr = 44100.0;
    const char* names[] = { "RubberBand-R3", "RubberBand-R2", "Signalsmith", "RubberBand-R3+C", "RubberBand-R2+C", "Signalsmith+C" };
    Score tot[6];
    for (unsigned seed = 1; seed <= 8; ++seed)
    {
        const auto p = synth::makePair (seed, 0.10);
        align::AlignSettings as;
        const auto path = align::alignSignals (p.guide.audio.data(), p.guide.audio.size(), p.dub.audio.data(), p.dub.audio.size(), sr, as);
        const auto map = align::makeWarpMap (path, 0.005, sr, p.guide.audio.size());
        for (int e = 0; e < 6; ++e)
        {
            const auto t0 = std::chrono::steady_clock::now();
            const align::Engine eng[3] = { align::Engine::RubberBandR3, align::Engine::RubberBandR2, align::Engine::Signalsmith };
            const auto mapUsed = e < 3 ? map : align::makeWarpMap (path, 0.005, sr, p.guide.audio.size(), align::findConsonants (p.dub.audio.data(), p.dub.audio.size(), sr), 1.0);
            const auto rr = align::render ({ p.dub.audio }, mapUsed, sr, eng[e % 3]);
            if (rr.fellBack) std::printf ("(repli sur Signalsmith, graine %u)\n", seed);
            const std::vector<float>& out = rr.channels[0];
            tot[e].seconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            if (! outDir.empty() && seed == 1)
            {
                writeWav (outDir + "/guide.wav", p.guide.audio, (int) sr);
                writeWav (outDir + "/double-original.wav", p.dub.audio, (int) sr);
                writeWav (outDir + "/double-" + names[e] + ".wav", out, (int) sr);
            }
            const auto segsOut = align::findConsonants (out.data(), out.size(), sr);
            // 1. Calage : on réaligne la sortie sur le guide ; le chemin doit être droit.
            const auto re = align::alignSignals (p.guide.audio.data(), p.guide.audio.size(), out.data(), out.size(), sr, as);
            for (const auto& n : p.guide.notes)
            {
                const size_t i = std::min ((size_t) (n.start / 0.005), re.size() - 1);
                tot[e].bias += (re[i] - n.start) * 1000.0; tot[e].timingMs += std::fabs (re[i] - n.start) * 1000.0; ++tot[e].n;
                // 2. Hauteur : au milieu de la note, la sortie doit avoir la hauteur du double original à l'endroit correspondant.
                const double mid = n.start + n.length * 0.5;
                const double fo = f0At (out, mid, sr), fd = f0At (p.dub.audio, p.truth.at (mid), sr);
                if (fo > 0 && fd > 0) { tot[e].pitchCents += std::fabs (1200.0 * std::log2 (fo / fd)); ++tot[e].np; }
                // 3. Consonne : énergie des 50 ms avant l'attaque (doit rester celle du double) et des 50 ms d'avant (pré-écho).
                if (n.consonant)
                {
                    for (const auto& sg : segsOut) if (sg.end > n.start - 0.08 && sg.start < n.start + 0.01) { tot[e].consLen += (sg.end - sg.start) * 1000.0; ++tot[e].nl; break; }
                    const double on = p.truth.at (n.start);
                    const double eb = energy (out, n.start - 0.05, n.start, sr), ed = energy (p.dub.audio, on - 0.05, on, sr);
                    const double ep = energy (out, n.start - 0.12, n.start - 0.07, sr), edp = energy (p.dub.audio, on - 0.12, on - 0.07, sr);
                    if (ed > 1e-9) { tot[e].burstDb += 10 * std::log10 ((eb + 1e-12) / ed); ++tot[e].nb; }
                    tot[e].preEchoDb += 10 * std::log10 ((ep + 1e-12) / (edp + 1e-9)); 
                }
            }
        }
    }
    std::printf ("%-16s %10s %10s %10s %12s %12s %12s %8s\n", "étireur", "biais ms", "calage ms", "hauteur ct", "consonne dB", "pré-écho dB", "long. cons ms", "temps s");
    for (int e = 0; e < 6; ++e)
        std::printf ("%-16s %10.1f %10.1f %10.1f %12.1f %12.1f %12.1f %8.2f\n", names[e], tot[e].bias / tot[e].n, tot[e].timingMs / tot[e].n, tot[e].pitchCents / std::max (1, tot[e].np),
                     tot[e].burstDb / std::max (1, tot[e].nb), tot[e].preEchoDb / std::max (1, tot[e].nb), tot[e].consLen / std::max (1, tot[e].nl), tot[e].seconds);
    return 0;
}
