#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "SynthVoice.h"
#include "align/Align.h"

namespace
{
struct Err { double meanOn, maxOn, meanEnd, maxEnd; };

// Erreur (en ms) du chemin trouvé. Les débuts de notes sont nets (attaque, consonne) ; les fins sont un fondu de 50 ms vers le silence,
// dont la position est naturellement floue : on les juge moins sévèrement.
Err pathError (const synth::Pair& p, const std::vector<double>& pathSeconds, double hop)
{
    Err r { 0, 0, 0, 0 };
    int n = 0;
    for (size_t k = 0; k < p.truth.points.size(); ++k)
    {
        const auto& pt = p.truth.points[k];
        const size_t i = std::min ((size_t) (pt.first / hop), pathSeconds.size() - 1);
        const double e = std::fabs (pathSeconds[i] - pt.second) * 1000.0;
        if (k % 2 == 0) { r.meanOn += e; r.maxOn = std::max (r.maxOn, e); ++n; }
        else { r.meanEnd += e; r.maxEnd = std::max (r.maxEnd, e); }
    }
    r.meanOn /= n; r.meanEnd /= n;
    return r;
}
} // namespace

TEST_CASE ("alignement : prises identiques = chemin droit")
{
    const auto p = synth::makePair (3, 0.0);
    const auto path = align::alignSignals (p.guide.audio.data(), p.guide.audio.size(), p.guide.audio.data(), p.guide.audio.size(), 44100.0);
    double worst = 0;
    for (size_t i = 0; i < path.size(); ++i) worst = std::max (worst, std::fabs (path[i] - (double) i * 0.005));
    CHECK (worst < 0.01);
}

TEST_CASE ("alignement : retrouve les décalages d'une doublure synthétique")
{
    for (uint32_t seed : { 1u, 2u, 3u, 4u, 5u, 6u })
    {
        const auto p = synth::makePair (seed, 0.10);
        align::AlignSettings a;
        const auto path = align::alignSignals (p.guide.audio.data(), p.guide.audio.size(), p.dub.audio.data(), p.dub.audio.size(), 44100.0, a);
        const Err e = pathError (p, path, 0.005);
        std::printf ("graine %u : debuts %.1f ms (max %.0f), fins %.1f ms (max %.0f)\n", seed, e.meanOn, e.maxOn, e.meanEnd, e.maxEnd);
        CHECK (e.meanOn < 8.0);
        CHECK (e.maxOn < 35.0);
        CHECK (e.meanEnd < 30.0);
    }
}

TEST_CASE ("alignement : le chemin ne recule jamais et reste dans les limites")
{
    const auto p = synth::makePair (9, 0.15);
    const auto path = align::alignSignals (p.guide.audio.data(), p.guide.audio.size(), p.dub.audio.data(), p.dub.audio.size(), 44100.0);
    for (size_t i = 1; i < path.size(); ++i) CHECK (path[i] >= path[i - 1]);
    CHECK (path.front() >= 0.0);
    CHECK (path.back() <= (double) p.dub.audio.size() / 44100.0 + 0.05);
}

TEST_CASE ("carte de correspondance : interpolation et prolongement")
{
    std::vector<double> path { 0.0, 0.1, 0.2, 0.3 }; // double en retard de 0 puis dérive : pente 20 / 1 trait de 5 ms
    const auto m = align::makeWarpMap (path, 0.005, 1000.0, 20, 5);
    CHECK (m.sourceAt (0) == Catch::Approx (0.0));
    CHECK (m.sourceAt (5) == Catch::Approx (100.0));   // trait 1 -> 0,1 s -> 100 échantillons
    CHECK (m.sourceAt (7.5) == Catch::Approx (150.0));
}

namespace
{
double overlap (const align::Segment& s, double a, double b) { return std::max (0.0, std::min (s.end, b) - std::max (s.start, a)); }

// Consonnes de la voix synthétique : 50 ms de bruit juste avant l'attaque de chaque note qui en a une.
std::vector<std::pair<double, double>> truthConsonants (const synth::Take& t)
{
    std::vector<std::pair<double, double>> v;
    for (const auto& n : t.notes) if (n.consonant) v.push_back ({ n.start - 0.05, n.start });
    return v;
}
} // namespace

TEST_CASE ("consonnes : détectées dans les voix synthétiques, peu de fausses alertes")
{
    int found = 0, total = 0, falseSeg = 0, segs = 0;
    for (uint32_t seed : { 1u, 2u, 3u, 4u })
    {
        const auto p = synth::makePair (seed, 0.10);
        const auto seg = align::findConsonants (p.dub.audio.data(), p.dub.audio.size(), 44100.0);
        const auto truth = truthConsonants (p.dub);
        for (const auto& t : truth)
        {
            ++total;
            double best = 0;
            for (const auto& s : seg) best = std::max (best, overlap (s, t.first, t.second));
            if (best > 0.025) ++found;
        }
        for (const auto& s : seg)
        {
            ++segs;
            bool any = false;
            for (const auto& t : truth) any = any || overlap (s, t.first, t.second) > 0.0;
            if (! any) ++falseSeg;
        }
    }
    std::printf ("consonnes : %d trouvées sur %d, %d fausses zones sur %d\n", found, total, falseSeg, segs);
    CHECK (found >= total * 0.9);
    CHECK (falseSeg <= segs / 10);
}

TEST_CASE ("carte protégée : la consonne garde sa durée, protect = 0 ne change rien")
{
    int ok = 0, checked = 0;
    for (uint32_t seed : { 1u, 2u, 3u, 4u })
    {
        const auto p = synth::makePair (seed, 0.10);
        const auto path = align::alignSignals (p.guide.audio.data(), p.guide.audio.size(), p.dub.audio.data(), p.dub.audio.size(), 44100.0);
        const auto seg = align::findConsonants (p.dub.audio.data(), p.dub.audio.size(), 44100.0);
        const auto plain = align::makeWarpMap (path, 0.005, 44100.0, p.guide.audio.size());
        const auto zero = align::makeWarpMap (path, 0.005, 44100.0, p.guide.audio.size(), seg, 0.0);
        const auto full = align::makeWarpMap (path, 0.005, 44100.0, p.guide.audio.size(), seg, 1.0);
        for (size_t k = 0; k < plain.sourceAtOutput.size(); k += 50) CHECK (zero.sourceAtOutput[k] == plain.sourceAtOutput[k]);
        for (size_t k = 1; k < full.sourceAtOutput.size(); ++k) REQUIRE (full.sourceAtOutput[k] >= full.sourceAtOutput[k - 1]);
        // Durée de sortie de chaque consonne : temps de sortie où la source atteint la fin moins celui où elle atteint le début.
        auto outTimeOf = [&] (const align::WarpMap& m, double srcSec) {
            const double target = srcSec * 44100.0;
            const auto it = std::lower_bound (m.sourceAtOutput.begin(), m.sourceAtOutput.end(), target);
            if (it == m.sourceAtOutput.begin() || it == m.sourceAtOutput.end()) return -1.0;
            const size_t i = (size_t) (it - m.sourceAtOutput.begin());
            const double f = (target - m.sourceAtOutput[i - 1]) / std::max (1e-9, m.sourceAtOutput[i] - m.sourceAtOutput[i - 1]);
            return ((double) (i - 1) + f) * (double) m.stepSamples / 44100.0;
        };
        for (const auto& s : seg)
        {
            const double a = outTimeOf (full, s.start), b = outTimeOf (full, s.end);
            if (a < 0 || b < 0) continue;
            ++checked;
            if (std::fabs ((b - a) - (s.end - s.start)) < 0.004) ++ok;
        }
    }
    std::printf ("consonnes protégées : %d durées conservées sur %d\n", ok, checked);
    CHECK (checked > 20);
    CHECK (ok >= checked * 0.85);
}

TEST_CASE ("force du recalage : 0 laisse le double, 1 garde le chemin, 0,5 est au milieu")
{
    const std::vector<double> path { 0.02, 0.07, 0.14, 0.17 }; // temps du guide : 0, 5, 10, 15 ms
    const auto none = align::applyStrength (path, 0.005, 0.0), full = align::applyStrength (path, 0.005, 1.0), half = align::applyStrength (path, 0.005, 0.5);
    for (size_t i = 0; i < path.size(); ++i)
    {
        CHECK (none[i] == Catch::Approx ((double) i * 0.005));
        CHECK (full[i] == Catch::Approx (path[i]));
        CHECK (half[i] == Catch::Approx (0.5 * ((double) i * 0.005 + path[i])));
    }
    CHECK (align::applyStrength (path, 0.005, 7.0)[2] == Catch::Approx (path[2])); // borné à 1
}

TEST_CASE ("force variable : phrase calée à fond, le reste presque tel quel")
{
    std::vector<double> path (400);
    for (size_t i = 0; i < path.size(); ++i) path[i] = (double) i * 0.005 + 0.1; // double en retard de 100 ms partout
    const std::vector<align::StrengthPoint> curve { { 0.0, 0.0 }, { 0.5, 0.0 }, { 0.6, 1.0 }, { 1.4, 1.0 }, { 1.5, 0.0 }, { 2.0, 0.0 } };
    const auto r = align::applyStrength (path, 0.005, curve);
    CHECK (r[10] == Catch::Approx (10 * 0.005));          // t = 0,05 s : force 0, aucun décalage corrigé
    CHECK (r[200] == Catch::Approx (200 * 0.005 + 0.1));  // t = 1,0 s : force 1, décalage complet
    CHECK (r[110] == Catch::Approx (110 * 0.005 + 0.05)); // t = 0,55 s : milieu de la montée
    CHECK (r[350] == Catch::Approx (350 * 0.005));        // t = 1,75 s : force 0
    for (size_t i = 1; i < r.size(); ++i) CHECK (r[i] >= r[i - 1]); // la courbe reste croissante
    CHECK (align::applyStrength (path, 0.005, std::vector<align::StrengthPoint> {}) == path);
}
