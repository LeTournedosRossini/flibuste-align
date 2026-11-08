#pragma once
// Voix chantée synthétique pour les essais : voyelles à deux formants, vibrato, souffle, consonnes bruitées avant certaines notes.
// Deux prises « chantent » les mêmes notes à des moments différents : on connaît donc la vraie correspondance de temps.
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace synth
{
struct Note { double start, length; int vowel; double f0; bool consonant; };

struct Take
{
    std::vector<float> audio;
    std::vector<Note> notes;
    double sampleRate = 44100.0;
};

inline std::vector<Note> melody (std::mt19937& rng, int count = 18)
{
    static const double scale[] = { 220.0, 246.9, 277.2, 293.7, 329.6, 370.0, 415.3, 440.0 };
    std::vector<Note> v;
    double t = 0.4;
    for (int i = 0; i < count; ++i)
    {
        Note n;
        n.length = 0.18 + 0.4 * std::uniform_real_distribution<double> (0, 1) (rng);
        n.start = t;
        n.vowel = (int) (rng() % 5);
        n.f0 = scale[rng() % 8];
        n.consonant = (rng() % 3) != 0;
        v.push_back (n);
        t += n.length + 0.05 + 0.15 * std::uniform_real_distribution<double> (0, 1) (rng);
    }
    return v;
}

// `notes` : les notes TELLES QU'ELLES SONT CHANTÉES dans cette prise. `seed` : variations propres à la prise (hauteur, souffle).
inline Take render (const std::vector<Note>& notes, double sr, double totalSeconds, uint32_t seed, double centsJitter = 12.0)
{
    static const double formants[5][2] = { { 700, 1200 }, { 400, 2000 }, { 300, 2300 }, { 500, 900 }, { 350, 700 } }; // a, e, i, o, u
    Take tk;
    tk.sampleRate = sr;
    tk.notes = notes;
    tk.audio.assign ((size_t) (totalSeconds * sr), 0.f);
    std::mt19937 rng (seed);
    std::normal_distribution<double> gauss (0.0, 1.0);
    for (const auto& n : notes)
    {
        const double cents = gauss (rng) * centsJitter;
        const double f0 = n.f0 * std::pow (2.0, cents / 1200.0);
        const double vibRate = 5.0 + 0.5 * gauss (rng), vibPhase = 6.28 * (rng() % 1000) / 1000.0;
        const size_t a = (size_t) (n.start * sr), len = (size_t) (n.length * sr);
        // Consonne : 50 ms de bruit passe-haut juste avant la voyelle.
        if (n.consonant)
        {
            const size_t cl = (size_t) (0.05 * sr);
            double prev = 0;
            for (size_t k = 0; k < cl && a >= cl; ++k)
            {
                const double w = gauss (rng), hp = w - prev; prev = w;
                const double env = std::sin (3.14159 * (double) k / (double) cl);
                tk.audio[a - cl + k] += (float) (0.12 * hp * env);
            }
        }
        double phase = 0;
        for (size_t k = 0; k < len && a + k < tk.audio.size(); ++k)
        {
            const double t = (double) k / sr;
            const double f = f0 * (1.0 + 0.012 * std::sin (6.28318 * vibRate * t + vibPhase));
            phase += f / sr;
            const double att = std::min (1.0, t / 0.03), rel = std::min (1.0, (n.length - t) / 0.05);
            double s = 0;
            for (int h = 1; h * f < 6000; ++h)
            {
                const double hf = h * f;
                const double g = std::exp (-std::pow ((hf - formants[n.vowel][0]) / 250.0, 2)) + 0.6 * std::exp (-std::pow ((hf - formants[n.vowel][1]) / 350.0, 2)) + 0.03 / h;
                s += g * std::sin (6.28318 * h * phase) / h;
            }
            tk.audio[a + k] += (float) (0.5 * att * std::max (0.0, rel) * s + 0.004 * gauss (rng));
        }
    }
    return tk;
}

// Correspondance exacte guide -> double : début et fin de chaque note, interpolation linéaire entre.
struct Truth
{
    std::vector<std::pair<double, double>> points; // (temps guide, temps double), croissants
    double at (double g) const
    {
        if (points.empty()) return g;
        if (g <= points.front().first) return points.front().second + (g - points.front().first);
        for (size_t i = 1; i < points.size(); ++i)
            if (g <= points[i].first)
            {
                const double f = (g - points[i - 1].first) / std::max (1e-9, points[i].first - points[i - 1].first);
                return points[i - 1].second + f * (points[i].second - points[i - 1].second);
            }
        return points.back().second + (g - points.back().first);
    }
};

struct Pair { Take guide, dub; Truth truth; };

// Le double chante les mêmes notes avec des écarts : début décalé de ±maxOffset, durée changée de ±30 %.
inline Pair makePair (uint32_t seed, double maxOffset = 0.10, double totalSeconds = 0, int count = 18)
{
    std::mt19937 rng (seed);
    auto g = melody (rng, count);
    std::vector<Note> d = g;
    std::uniform_real_distribution<double> u (-1, 1);
    double carry = 0;
    Pair p;
    for (size_t i = 0; i < g.size(); ++i)
    {
        d[i].start = g[i].start + carry + maxOffset * u (rng);
        d[i].length = g[i].length * (1.0 + 0.3 * u (rng));
        if (i > 0) d[i].start = std::max (d[i].start, d[i - 1].start + d[i - 1].length + 0.04);
        carry = 0.5 * (d[i].start - g[i].start); // le décalage dérive lentement, comme un chanteur qui traîne
        d[i].f0 = g[i].f0;
        p.truth.points.push_back ({ g[i].start, d[i].start });
        p.truth.points.push_back ({ g[i].start + g[i].length, d[i].start + d[i].length });
    }
    const double total = totalSeconds > 0 ? totalSeconds : d.back().start + d.back().length + 0.6;
    p.guide = render (g, 44100.0, std::max (total, g.back().start + g.back().length + 0.6), seed * 7 + 1);
    p.dub = render (d, 44100.0, total, seed * 13 + 5, 18.0);
    // Pour comparer avec la vérité terrain : la même prise « du double » chantée exactement aux temps du guide (mêmes variations de hauteur).
    return p;
}
} // namespace synth
