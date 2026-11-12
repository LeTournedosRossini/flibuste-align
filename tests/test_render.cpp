// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "SynthVoice.h"
#include "align/Render.h"

namespace
{
const align::Engine kEngines[] = { align::Engine::Signalsmith, align::Engine::RubberBandR2, align::Engine::RubberBandR3 };
// Écart moyen (ms) entre les débuts de notes du guide et ceux de la sortie réalignée sur le guide : doit être proche de zéro.
double residualMs (const synth::Pair& p, const std::vector<float>& out)
{
    const auto re = align::alignSignals (p.guide.audio.data(), p.guide.audio.size(), out.data(), out.size(), 44100.0);
    double s = 0;
    for (const auto& n : p.guide.notes) s += std::fabs (re[std::min ((size_t) (n.start / 0.005), re.size() - 1)] - n.start) * 1000.0;
    return s / (double) p.guide.notes.size();
}
} // namespace

TEST_CASE ("rendu : chaque moteur rend la bonne longueur, des valeurs finies, et cale le double")
{
    const double limit[] = { 8.0, 12.0, 20.0 }; // R3 suit un peu moins bien la courbe (voir README)
    for (int e = 0; e < 3; ++e)
        for (uint32_t seed : { 1u, 2u, 3u })
        {
            const auto p = synth::makePair (seed, 0.10);
            const auto path = align::alignSignals (p.guide.audio.data(), p.guide.audio.size(), p.dub.audio.data(), p.dub.audio.size(), 44100.0);
            const auto map = align::makeWarpMap (path, 0.005, 44100.0, p.guide.audio.size());
            const auto r = align::render ({ p.dub.audio }, map, 44100.0, kEngines[e]);
            REQUIRE (r.channels.size() == 1);
            CHECK (r.channels[0].size() == p.guide.audio.size());
            CHECK_FALSE (r.fellBack);
            for (float v : r.channels[0]) REQUIRE (std::isfinite (v));
            const double res = residualMs (p, r.channels[0]);
            std::printf ("%-14s graine %u : calage résiduel %.1f ms\n", align::engineName (kEngines[e]), seed, res);
            CHECK (res < limit[e]);
        }
}

TEST_CASE ("rendu : la stéréo garde son image (voie droite = voie gauche inversée)")
{
    const auto p = synth::makePair (4, 0.10);
    const auto path = align::alignSignals (p.guide.audio.data(), p.guide.audio.size(), p.dub.audio.data(), p.dub.audio.size(), 44100.0);
    const auto map = align::makeWarpMap (path, 0.005, 44100.0, p.guide.audio.size());
    std::vector<float> right = p.dub.audio;
    for (auto& v : right) v = -v;
    for (auto e : kEngines)
    {
        const auto r = align::render ({ p.dub.audio, right }, map, 44100.0, e);
        REQUIRE (r.channels.size() == 2);
        double num = 0, den = 0;
        for (size_t i = 0; i < r.channels[0].size(); ++i) { num += (double) r.channels[0][i] * r.channels[1][i]; den += (double) r.channels[0][i] * r.channels[0][i]; }
        INFO (align::engineName (e));
        CHECK (num / den < -0.98);
    }
}

TEST_CASE ("rendu : cas limites (silence, très court, vide) sans valeur invalide")
{
    const std::vector<float> silence (44100, 0.f), tiny (50, 0.3f);
    std::vector<double> path (100);
    for (size_t i = 0; i < path.size(); ++i) path[i] = (double) i * 0.005;
    const auto map = align::makeWarpMap (path, 0.005, 44100.0, 22050);
    for (auto e : kEngines)
    {
        for (const auto* in : { &silence, &tiny })
        {
            const auto r = align::render ({ *in }, map, 44100.0, e);
            REQUIRE (r.channels.size() == 1);
            CHECK (r.channels[0].size() == 22050);
            for (float v : r.channels[0]) REQUIRE (std::isfinite (v));
        }
        const auto r = align::render ({}, map, 44100.0, e);
        CHECK (r.channels[0].size() == 22050);
    }
}

#include "align/Pipeline.h"

TEST_CASE ("chaîne : analyser une fois, rendre plusieurs fois avec d'autres réglages")
{
    const auto p = synth::makePair (5, 0.10);
    const auto an = align::analysePair (p.guide.audio.data(), p.guide.audio.size(), p.dub.audio.data(), p.dub.audio.size(), 44100.0);
    CHECK (an.length == p.guide.audio.size());
    CHECK_FALSE (an.consonants.empty());

    align::RenderSettings full, none;
    none.strength = 0.0;
    const auto rFull = align::renderAnalysis ({ p.dub.audio }, an, full);
    const auto rNone = align::renderAnalysis ({ p.dub.audio }, an, none);
    CHECK (rFull.channels[0].size() == an.length);
    CHECK (rNone.channels[0].size() == an.length);
    // Force 0 : le double reste calé comme avant (pas de déformation) ; force 1 : il est déformé, donc différent.
    double diff = 0;
    for (size_t i = 0; i < an.length; ++i) diff += std::fabs (rFull.channels[0][i] - rNone.channels[0][i]);
    CHECK (diff > 1.0);
    // La courbe de décalage appliquée suit la force.
    const auto s1 = align::appliedShift (an, full), s0 = align::appliedShift (an, none);
    double m1 = 0, m0 = 0;
    for (size_t i = 0; i < s1.size(); ++i) { m1 = std::max (m1, std::fabs (s1[i])); m0 = std::max (m0, std::fabs (s0[i])); }
    CHECK (m0 < 1e-12); // zéro, aux arrondis près (macOS : multiplication-addition fusionnée)
    CHECK (m1 > 0.01);
}

TEST_CASE ("suggestion de force : seulement les passages très décalés")
{
    align::Analysis a;
    a.hopSeconds = 0.005;
    a.path.resize (2000); // 10 s
    for (size_t i = 0; i < a.path.size(); ++i)
    {
        const double t = (double) i * 0.005;
        double shift = 0.0;
        if (t >= 2.0 && t < 3.0) shift = 0.100;       // 100 ms : très décalé
        if (t >= 6.0 && t < 6.5) shift = 0.015;       // 15 ms : trop petit
        if (t >= 7.0 && t < 7.04) shift = 0.2;        // 40 ms de pic : trop court
        a.path[i] = t + shift;
    }
    const auto c = align::suggestStrengthCurve (a);
    REQUIRE_FALSE (c.empty());
    for (size_t i = 1; i < c.size(); ++i) CHECK (c[i].time > c[i - 1].time);
    auto at = [&] (double t) { // valeur de la courbe en t (constante avant le premier point et après le dernier, comme dans le plugin)
        if (t <= c.front().time) return c.front().strength;
        for (size_t i = 1; i < c.size(); ++i)
            if (t <= c[i].time) return c[i - 1].strength + (c[i].strength - c[i - 1].strength) * (t - c[i - 1].time) / (c[i].time - c[i - 1].time);
        return c.back().strength;
    };
    CHECK (at (2.5) == Catch::Approx (1.0));
    CHECK (at (0.5) == Catch::Approx (0.0));
    CHECK (at (6.2) == Catch::Approx (0.0));
    CHECK (at (7.02) == Catch::Approx (0.0));
    CHECK (at (9.5) == Catch::Approx (0.0));
    // Aucun passage au-dessus du seuil : pas de courbe. Tout le morceau décalé : pas de courbe non plus (force constante).
    align::Analysis flat = a;
    for (size_t i = 0; i < flat.path.size(); ++i) flat.path[i] = (double) i * 0.005;
    CHECK (align::suggestStrengthCurve (flat).empty());
    for (size_t i = 0; i < flat.path.size(); ++i) flat.path[i] = (double) i * 0.005 + 0.2;
    CHECK (align::suggestStrengthCurve (flat).empty());
}
