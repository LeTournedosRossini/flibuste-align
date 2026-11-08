// flibuste-align : cale un double sur un guide.
//   align_cli guide.wav double.wav sortie.wav [--engine signalsmith|r2|r3] [--protect 0..1] [--tightness 0..1]
//                                             [--max-shift secondes] [--chroma poids] [--strength 0..1] [--open-ends] [--write-consonants fichier.txt]
// Le résultat a la durée et la fréquence d'échantillonnage du guide (les deux fichiers doivent avoir la même fréquence).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define DR_WAV_IMPLEMENTATION
#include "dr_wav/dr_wav.h"

#include "align/Render.h"

namespace
{
struct Audio { std::vector<std::vector<float>> ch; unsigned rate = 0; };

bool readWav (const char* path, Audio& a)
{
    unsigned ch, rate;
    drwav_uint64 frames;
    float* d = drwav_open_file_and_read_pcm_frames_f32 (path, &ch, &rate, &frames, nullptr);
    if (! d) return false;
    a.rate = rate;
    a.ch.assign (ch, std::vector<float> ((size_t) frames));
    for (drwav_uint64 i = 0; i < frames; ++i)
        for (unsigned c = 0; c < ch; ++c) a.ch[c][(size_t) i] = d[i * ch + c];
    drwav_free (d, nullptr);
    return true;
}

bool writeWav (const char* path, const Audio& a)
{
    drwav_data_format f;
    f.container = drwav_container_riff; f.format = DR_WAVE_FORMAT_IEEE_FLOAT; f.channels = (drwav_uint32) a.ch.size();
    f.sampleRate = a.rate; f.bitsPerSample = 32;
    drwav w;
    if (! drwav_init_file_write (&w, path, &f, nullptr)) return false;
    const size_t n = a.ch.empty() ? 0 : a.ch[0].size();
    std::vector<float> inter (n * a.ch.size());
    for (size_t i = 0; i < n; ++i)
        for (size_t c = 0; c < a.ch.size(); ++c) inter[i * a.ch.size() + c] = a.ch[c][i];
    drwav_write_pcm_frames (&w, n, inter.data());
    drwav_uninit (&w);
    return true;
}

std::vector<float> mono (const Audio& a)
{
    std::vector<float> m (a.ch[0].size(), 0.f);
    for (const auto& c : a.ch) for (size_t i = 0; i < m.size() && i < c.size(); ++i) m[i] += c[i] / (float) a.ch.size();
    return m;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 4) { std::fprintf (stderr, "usage : align_cli guide.wav double.wav sortie.wav [--engine signalsmith|r2|r3] [--protect 0..1] [--tightness 0..1] [--max-shift s] [--open-ends] [--write-consonants f.txt]\n"); return 2; }
    align::Engine engine = align::Engine::Signalsmith;
    align::AlignSettings as;
    double protect = 0.0, strength = 1.0;
    align::AnalysisSettings an;
    const char* consFile = nullptr;
    for (int i = 4; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? argv[++i] : (std::fprintf (stderr, "valeur manquante après %s\n", a.c_str()), std::exit (2), (char*) nullptr); };
        if (a == "--engine")
        {
            const std::string v = next();
            engine = v == "r2" ? align::Engine::RubberBandR2 : v == "r3" ? align::Engine::RubberBandR3 : align::Engine::Signalsmith;
        }
        else if (a == "--protect") protect = std::atof (next());
        else if (a == "--tightness") as.tightness = std::atof (next());
        else if (a == "--max-shift") as.maxShiftSeconds = std::atof (next());
        else if (a == "--strength") strength = std::atof (next());
        else if (a == "--chroma") an.chromaWeight = std::atof (next());
        else if (a == "--open-ends") as.openEnds = true;
        else if (a == "--write-consonants") consFile = next();
        else { std::fprintf (stderr, "option inconnue : %s\n", a.c_str()); return 2; }
    }
    Audio guide, dub;
    if (! readWav (argv[1], guide)) { std::fprintf (stderr, "impossible de lire %s\n", argv[1]); return 1; }
    if (! readWav (argv[2], dub)) { std::fprintf (stderr, "impossible de lire %s\n", argv[2]); return 1; }
    if (guide.rate != dub.rate) { std::fprintf (stderr, "fréquences différentes (%u et %u Hz) : convertissez l'un des deux\n", guide.rate, dub.rate); return 1; }
    const double sr = guide.rate;
    const auto t0 = std::chrono::steady_clock::now();
    const auto g = mono (guide), d = mono (dub);
    const auto found = align::alignSignals (g.data(), g.size(), d.data(), d.size(), sr, as, an);
    const auto path = align::applyStrength (found, 0.005, strength);
    const auto cons = align::findConsonants (d.data(), d.size(), sr);
    const auto map = align::makeWarpMap (path, 0.005, sr, g.size(), cons, protect);
    const auto r = align::render (dub.ch, map, sr, engine);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();

    // Compte rendu : décalage et vitesse du double.
    double minShift = 1e9, maxShift = -1e9, minRate = 1e9, maxRate = -1e9;
    for (size_t i = 0; i < path.size(); ++i)
    {
        const double sh = path[i] - (double) i * 0.005;
        minShift = std::min (minShift, sh); maxShift = std::max (maxShift, sh);
    }
    for (size_t i = 40; i + 40 < path.size(); i += 20) // vitesse sur 200 ms
    {
        const double rate = (path[i + 20] - path[i - 20]) / (40 * 0.005);
        minRate = std::min (minRate, rate); maxRate = std::max (maxRate, rate);
    }
    std::printf ("force %.0f %% | moteur %s%s | décalage du double de %+.0f à %+.0f ms | vitesse de %.2f à %.2f | %zu consonnes protégées à %.0f %% | %.1f s de calcul\n",
                 strength * 100, align::engineName (r.engineUsed), r.fellBack ? " (repli)" : "", minShift * 1000, maxShift * 1000, minRate, maxRate, cons.size(), protect * 100, secs);
    if (consFile)
        if (FILE* f = std::fopen (consFile, "w")) { for (const auto& s : cons) std::fprintf (f, "%.4f\t%.4f\n", s.start, s.end); std::fclose (f); }
    Audio out;
    out.rate = guide.rate;
    out.ch = r.channels;
    if (! writeWav (argv[3], out)) { std::fprintf (stderr, "impossible d'écrire %s\n", argv[3]); return 1; }
    return 0;
}
