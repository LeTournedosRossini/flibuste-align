#include <catch2/catch_test_macros.hpp>

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
