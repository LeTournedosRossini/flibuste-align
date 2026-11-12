// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "SynthVoice.h"
#include "align/align_c.h"

extern "C" int c_header_check (void);

namespace
{
std::vector<float> interleave (const std::vector<float>& m, int channels)
{
    std::vector<float> v (m.size() * (size_t) channels);
    for (size_t i = 0; i < m.size(); ++i)
        for (int c = 0; c < channels; ++c) v[i * (size_t) channels + (size_t) c] = m[i] * (c == 0 ? 1.0f : 0.8f);
    return v;
}

struct Fixture
{
    synth::Pair pair = synth::makePair (3, 0.10);
    align_guide* guide = nullptr;
    Fixture()
    {
        REQUIRE (align_guide_create (pair.guide.audio.data(), pair.guide.audio.size(), 1, 44100.0, nullptr, nullptr, nullptr, &guide) == ALIGN_OK);
    }
    ~Fixture() { align_guide_free (guide); }
};
} // namespace

TEST_CASE ("interface C : l'en-tête se compile en C")
{
    CHECK (c_header_check() == 1);
    CHECK (std::string (align_version()).size() > 0);
}

TEST_CASE ("interface C : réglages JSON")
{
    CHECK (align_settings_validate (align_settings_default_json()) == ALIGN_OK);
    CHECK (align_settings_validate ("{\"format\":\"flibuste-align-settings\",\"version\":1,\"futur\":{\"x\":[1,2]},\"general\":{\"strength\":0.5}}") == ALIGN_OK); // champ inconnu ignoré
    CHECK (align_settings_validate ("{ pas du json") == ALIGN_ERR_FORMAT);
    CHECK (std::string (align_last_error()).size() > 0);
    CHECK (align_settings_validate ("{\"general\":{\"strength\":2}}") == ALIGN_ERR_FORMAT);
    CHECK (align_settings_validate ("{\"general\":{\"engine\":\"inconnu\"}}") == ALIGN_ERR_FORMAT);
    CHECK (align_settings_validate ("{\"general\":{\"analysis_mode\":\"x\"}}") == ALIGN_ERR_FORMAT);
    CHECK (align_settings_validate ("{\"version\":99}") == ALIGN_ERR_FORMAT);
    CHECK (align_settings_validate (nullptr) == ALIGN_ERR_ARGUMENT);

    // Copier-coller : le « general » de la source remplace celui de la destination ; le « local » de la destination reste.
    const char* dst = "{\"general\":{\"strength\":1,\"engine\":\"signalsmith\"},\"local\":{\"strength_curve\":[[1,0],[2,1]]}}";
    const char* src = "{\"general\":{\"strength\":0.3,\"engine\":\"rubberband_r2\",\"protect\":0.5,\"analysis_mode\":\"harmony\"},\"local\":{\"strength_curve\":[[9,0.9]]}}";
    char* merged = nullptr;
    REQUIRE (align_settings_merge_general (dst, src, &merged) == ALIGN_OK);
    const std::string m (merged);
    align_free (merged);
    CHECK (m.find ("\"rubberband_r2\"") != std::string::npos);
    CHECK (m.find ("\"harmony\"") != std::string::npos);
    CHECK (m.find ("[1, 0], [2, 1]") != std::string::npos);        // courbe de la destination conservée
    CHECK (m.find ("[9, 0.9]") == std::string::npos);              // courbe de la source jamais copiée
    CHECK (align_settings_merge_general ("nope", src, &merged) == ALIGN_ERR_FORMAT);
}

TEST_CASE ("interface C : analyse, rendu mono et stéréo, durée et voies")
{
    Fixture fx;
    for (int channels : { 1, 2 })
    {
        const auto dub = interleave (fx.pair.dub.audio, channels);
        align_analysis* an = nullptr;
        REQUIRE (align_analyse (fx.guide, dub.data(), fx.pair.dub.audio.size(), channels, 44100.0, nullptr, nullptr, nullptr, &an) == ALIGN_OK);
        CHECK (align_analysis_length_frames (an) == fx.pair.guide.audio.size());
        CHECK (align_analysis_hop_seconds (an) == Catch::Approx (0.005));
        float* out = nullptr;
        size_t frames = 0;
        align_render_info info {};
        REQUIRE (align_render (an, dub.data(), fx.pair.dub.audio.size(), channels, 44100.0, nullptr, nullptr, nullptr, &out, &frames, &info) == ALIGN_OK);
        CHECK (frames == fx.pair.guide.audio.size());
        CHECK (info.engine_used == 0);
        CHECK (info.fell_back == 0);
        for (size_t i = 0; i < frames * (size_t) channels; ++i) REQUIRE (std::isfinite (out[i]));
        align_free (out);
        align_analysis_free (an);
    }
}

TEST_CASE ("interface C : progression croissante, annulation propre, plusieurs fils")
{
    Fixture fx;
    std::vector<double> seen;
    auto cb = [] (double f, void* u) { static_cast<std::vector<double>*> (u)->push_back (f); return 0; };
    align_analysis* an = nullptr;
    REQUIRE (align_analyse (fx.guide, fx.pair.dub.audio.data(), fx.pair.dub.audio.size(), 1, 44100.0, nullptr, cb, &seen, &an) == ALIGN_OK);
    REQUIRE (seen.size() > 5);
    for (size_t i = 1; i < seen.size(); ++i) REQUIRE (seen[i] >= seen[i - 1] - 1e-12);
    CHECK (seen.front() >= 0.0);
    CHECK (seen.back() > 0.85);
    CHECK (seen.back() <= 1.0);

    // Annulation à mi-chemin.
    struct Cancel { int calls = 0; } c;
    auto cancelAtThird = [] (double f, void* u) { static_cast<Cancel*> (u)->calls++; return f > 0.33 ? 1 : 0; };
    align_analysis* an2 = reinterpret_cast<align_analysis*> (0x1);
    CHECK (align_analyse (fx.guide, fx.pair.dub.audio.data(), fx.pair.dub.audio.size(), 1, 44100.0, nullptr, cancelAtThird, &c, &an2) == ALIGN_ERR_CANCELLED);
    CHECK (an2 == nullptr);
    CHECK (std::string (align_last_error()).size() > 0);
    // Rendu annulé aussi.
    float* out = reinterpret_cast<float*> (0x1);
    size_t frames = 7;
    CHECK (align_render (an, fx.pair.dub.audio.data(), fx.pair.dub.audio.size(), 1, 44100.0, nullptr, cancelAtThird, &c, &out, &frames, nullptr) == ALIGN_ERR_CANCELLED);
    CHECK (out == nullptr);
    CHECK (frames == 0);
    // Rien ne reste de l'annulation : l'appel suivant réussit.
    CHECK (align_render (an, fx.pair.dub.audio.data(), fx.pair.dub.audio.size(), 1, 44100.0, nullptr, nullptr, nullptr, &out, &frames, nullptr) == ALIGN_OK);
    align_free (out);

    // Le guide peut être lu par plusieurs fils en même temps : mêmes résultats.
    std::vector<double> p1, p2;
    auto work = [&] (std::vector<double>* dst) {
        align_analysis* a = nullptr;
        if (align_analyse (fx.guide, fx.pair.dub.audio.data(), fx.pair.dub.audio.size(), 1, 44100.0, nullptr, nullptr, nullptr, &a) != ALIGN_OK) return;
        size_t n = 0;
        align_analysis_get_path (a, nullptr, 0, &n);
        dst->resize (n);
        align_analysis_get_path (a, dst->data(), n, &n);
        align_analysis_free (a);
    };
    std::thread t1 (work, &p1), t2 (work, &p2);
    t1.join(); t2.join();
    CHECK_FALSE (p1.empty());
    CHECK (p1 == p2);
    align_analysis_free (an);
}

TEST_CASE ("interface C : octets de l'analyse, refaire le rendu à l'identique")
{
    Fixture fx;
    align_analysis* an = nullptr;
    REQUIRE (align_analyse (fx.guide, fx.pair.dub.audio.data(), fx.pair.dub.audio.size(), 1, 44100.0, nullptr, nullptr, nullptr, &an) == ALIGN_OK);
    unsigned char* bytes = nullptr;
    size_t size = 0;
    REQUIRE (align_analysis_serialize (an, &bytes, &size) == ALIGN_OK);
    align_analysis* back = nullptr;
    REQUIRE (align_analysis_deserialize (bytes, size, &back) == ALIGN_OK);
    float *a1 = nullptr, *a2 = nullptr;
    size_t f1 = 0, f2 = 0;
    REQUIRE (align_render (an, fx.pair.dub.audio.data(), fx.pair.dub.audio.size(), 1, 44100.0, nullptr, nullptr, nullptr, &a1, &f1, nullptr) == ALIGN_OK);
    REQUIRE (align_render (back, fx.pair.dub.audio.data(), fx.pair.dub.audio.size(), 1, 44100.0, nullptr, nullptr, nullptr, &a2, &f2, nullptr) == ALIGN_OK);
    REQUIRE (f1 == f2);
    CHECK (std::equal (a1, a1 + f1, a2)); // rendu identique au bit près (graine fixe, mêmes entrées)
    align_free (a1); align_free (a2);
    // Octets abîmés ou tronqués : erreur propre.
    align_analysis* bad = reinterpret_cast<align_analysis*> (0x1);
    CHECK (align_analysis_deserialize (bytes, size / 2, &bad) == ALIGN_ERR_FORMAT);
    CHECK (bad == nullptr);
    CHECK (align_analysis_deserialize (bytes + 4, size - 4, &bad) == ALIGN_ERR_FORMAT);
    align_free (bytes);
    align_analysis_free (back);
    align_analysis_free (an);
}

TEST_CASE ("interface C : tableaux pour l'interface, consonnes modifiables, force suggérée")
{
    Fixture fx;
    align_analysis* an = nullptr;
    REQUIRE (align_analyse (fx.guide, fx.pair.dub.audio.data(), fx.pair.dub.audio.size(), 1, 44100.0, nullptr, nullptr, nullptr, &an) == ALIGN_OK);
    size_t n = 0;
    REQUIRE (align_analysis_get_path (an, nullptr, 0, &n) == ALIGN_OK);
    CHECK (n > 100);
    std::vector<double> small (3);
    CHECK (align_analysis_get_path (an, small.data(), small.size(), &n) == ALIGN_ERR_ARGUMENT); // trop petit : n donne la taille nécessaire
    CHECK (n > 3);

    size_t nc = 0;
    REQUIRE (align_analysis_get_consonants (an, nullptr, 0, &nc) == ALIGN_OK);
    CHECK (nc > 5);
    const double mine[] = { 1.0, 1.05, 2.0, 2.04 };
    CHECK (align_analysis_set_consonants (an, mine, 2) == ALIGN_OK);
    std::vector<double> got (4);
    REQUIRE (align_analysis_get_consonants (an, got.data(), 2, &nc) == ALIGN_OK);
    CHECK (nc == 2);
    CHECK (got == std::vector<double> (mine, mine + 4));
    const double bad[] = { 2.0, 1.0 }; // fin avant début
    CHECK (align_analysis_set_consonants (an, bad, 1) == ALIGN_ERR_ARGUMENT);

    // Décalage appliqué : force 0 -> zéro partout ; force 1 -> le chemin.
    std::vector<double> shift (n);
    REQUIRE (align_applied_shift (an, "{\"general\":{\"strength\":0}}", shift.data(), n, &n) == ALIGN_OK);
    for (double v : shift) REQUIRE (std::fabs (v) < 1e-12); // zéro, aux arrondis près
    REQUIRE (align_applied_shift (an, nullptr, shift.data(), n, &n) == ALIGN_OK);
    double mx = 0;
    for (double v : shift) mx = std::max (mx, std::fabs (v));
    CHECK (mx > 0.02);

    size_t ns = 0;
    REQUIRE (align_suggest_strength (an, 30.0, nullptr, 0, &ns) == ALIGN_OK);
    CHECK (ns >= 2);
    std::vector<double> pairs (ns * 2);
    REQUIRE (align_suggest_strength (an, 30.0, pairs.data(), ns, &ns) == ALIGN_OK);
    for (size_t i = 1; i < ns; ++i) CHECK (pairs[2 * i] > pairs[2 * (i - 1)]);
    CHECK (align_suggest_strength (an, -5.0, nullptr, 0, &ns) == ALIGN_ERR_ARGUMENT);
    align_analysis_free (an);
}

TEST_CASE ("interface C : erreurs d'arguments claires")
{
    Fixture fx;
    align_analysis* an = nullptr;
    const auto& d = fx.pair.dub.audio;
    CHECK (align_analyse (nullptr, d.data(), d.size(), 1, 44100.0, nullptr, nullptr, nullptr, &an) == ALIGN_ERR_ARGUMENT);
    CHECK (align_analyse (fx.guide, nullptr, d.size(), 1, 44100.0, nullptr, nullptr, nullptr, &an) == ALIGN_ERR_ARGUMENT);
    CHECK (align_analyse (fx.guide, d.data(), 100, 1, 44100.0, nullptr, nullptr, nullptr, &an) == ALIGN_ERR_ARGUMENT);       // trop court
    CHECK (align_analyse (fx.guide, d.data(), d.size(), 0, 44100.0, nullptr, nullptr, nullptr, &an) == ALIGN_ERR_ARGUMENT);   // voies
    CHECK (align_analyse (fx.guide, d.data(), d.size(), 1, 48000.0, nullptr, nullptr, nullptr, &an) == ALIGN_ERR_ARGUMENT);   // autre fréquence que le guide
    CHECK (std::string (align_last_error()).find ("fréquence") != std::string::npos);
    CHECK (align_analyse (fx.guide, d.data(), d.size(), 1, 44100.0, "{\"general\":{\"analysis_mode\":\"harmony\"}}", nullptr, nullptr, &an) == ALIGN_ERR_ARGUMENT); // mode différent du guide
    CHECK (align_analyse (fx.guide, d.data(), d.size(), 1, 44100.0, "n'importe quoi", nullptr, nullptr, &an) == ALIGN_ERR_FORMAT);
    CHECK (an == nullptr);
    align_guide* g = reinterpret_cast<align_guide*> (0x1);
    CHECK (align_guide_create (nullptr, 1000, 1, 44100.0, nullptr, nullptr, nullptr, &g) == ALIGN_ERR_ARGUMENT);
    CHECK (g == nullptr);
    CHECK (align_guide_create (d.data(), d.size(), 1, 1000.0, nullptr, nullptr, nullptr, &g) == ALIGN_ERR_ARGUMENT);
    align_guide_free (nullptr);
    align_analysis_free (nullptr);
    align_free (nullptr);

    // Un guide « harmonies » avec des doubles « harmonies » : accepté.
    const char* harm = "{\"general\":{\"analysis_mode\":\"harmony\"}}";
    align_guide* hg = nullptr;
    REQUIRE (align_guide_create (fx.pair.guide.audio.data(), fx.pair.guide.audio.size(), 1, 44100.0, harm, nullptr, nullptr, &hg) == ALIGN_OK);
    CHECK (align_analyse (hg, d.data(), d.size(), 1, 44100.0, harm, nullptr, nullptr, &an) == ALIGN_OK);
    align_analysis_free (an);
    align_guide_free (hg);
}

// ------------------------------------------------------------------------------------------------ align_warp_render (étirement par marqueurs)
namespace
{
constexpr double kRate = 44100.0;
const char* const kEngines[3] = { "{\"general\":{\"engine\":\"signalsmith\"}}", "{\"general\":{\"engine\":\"rubberband_r2\"}}", "{\"general\":{\"engine\":\"rubberband_r3\"}}" };

std::vector<float> sine (double seconds, double hz, int channels = 1)
{
    std::vector<float> v ((size_t) (seconds * kRate) * (size_t) channels);
    for (size_t i = 0; i < v.size() / (size_t) channels; ++i)
        for (int c = 0; c < channels; ++c) v[i * (size_t) channels + (size_t) c] = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * hz * (double) i / kRate) * (c == 0 ? 1.0f : 0.8f);
    return v;
}

// Salves de bruit de 8 ms (fenêtre carrée, graine fixe) aux instants donnés (s) dans un silence.
std::vector<float> bursts (double seconds, const std::vector<double>& at)
{
    std::vector<float> v ((size_t) (seconds * kRate), 0.f);
    unsigned s = 12345;
    for (double t : at)
        for (size_t i = (size_t) (t * kRate); i < (size_t) (t * kRate) + (size_t) (0.008 * kRate); ++i)
        {
            s = s * 1664525u + 1013904223u;
            v[i] = 0.8f * ((float) (s >> 8) / (float) (1u << 23) - 1.0f);
        }
    return v;
}

double firstAbove (const std::vector<float>& x, size_t from, size_t to, float thr)
{
    for (size_t i = from; i < to && i < x.size(); ++i) if (std::fabs (x[i]) > thr) return (double) i / kRate;
    return -1.0;
}

int zeroCrossingsHz (const std::vector<float>& x, size_t from, size_t to)
{
    int n = 0;
    for (size_t i = from + 1; i < to; ++i) if (x[i - 1] < 0.f && x[i] >= 0.f) ++n;
    return (int) std::lround ((double) n / ((double) (to - from) / kRate));
}

struct Out
{
    float* p = nullptr;
    size_t n = 0;
    ~Out() { align_free (p); }
    std::vector<float> v() const { return std::vector<float> (p, p + n); }
};
} // namespace

TEST_CASE ("étirement par marqueurs : longueur exacte, hauteur conservée, trois moteurs")
{
    CHECK (align_c_api_version() >= 2);
    const auto in = sine (3.0, 440.0);
    const double keys[] = { 0, 0, 3.0, 4.5 };
    for (const char* settings : kEngines)
    {
        Out o;
        align_render_info info {};
        REQUIRE (align_warp_render (in.data(), in.size(), 1, kRate, keys, 2, settings, nullptr, nullptr, &o.p, &o.n, &info) == ALIGN_OK);
        CHECK (o.n == (size_t) std::llround (4.5 * kRate));
        CHECK (info.fell_back == 0);
        const auto y = o.v();
        CHECK (std::abs (zeroCrossingsHz (y, (size_t) (1.0 * kRate), (size_t) (3.5 * kRate)) - 440) <= 5);
        float peak = 0.f;
        for (float v : y) peak = std::max (peak, std::fabs (v));
        CHECK (peak > 0.3f);
        CHECK (peak < 0.9f); // Rubber Band R2 rend 0,8 pour un sinus de 0,5 étiré (battements entre fenêtres) : mesuré, pas un défaut du rappel
    }
}

TEST_CASE ("étirement par marqueurs : stéréo, voies conservées")
{
    const auto in = sine (2.0, 330.0, 2);
    const double keys[] = { 0, 0, 2.0, 1.5 };
    Out o;
    REQUIRE (align_warp_render (in.data(), in.size() / 2, 2, kRate, keys, 2, nullptr, nullptr, nullptr, &o.p, &o.n, nullptr) == ALIGN_OK);
    CHECK (o.n == (size_t) std::llround (1.5 * kRate));
    double l = 0, r = 0;
    for (size_t i = (size_t) (0.5 * kRate); i < (size_t) (1.0 * kRate); ++i) { l += std::fabs (o.p[2 * i]); r += std::fabs (o.p[2 * i + 1]); }
    CHECK (r / l == Catch::Approx (0.8).margin (0.05));
}

TEST_CASE ("étirement par marqueurs : les salves tombent aux instants de sortie prévus")
{
    const std::vector<double> src = { 0.5, 1.5, 2.5 };
    const auto in = bursts (3.0, src);
    const double keys[] = { 0, 0, 1.0, 0.8, 2.0, 2.2, 3.0, 3.0 };
    const double expected[] = { 0.4, 1.5, 2.6 }; // début : 0,5 -> 0,4 ; 1,5 -> 0,8 + 0,5 x 1,4 ; 2,5 -> 2,2 + 0,5 x 0,8
    for (int e = 0; e < 3; ++e)
    {
        Out o;
        REQUIRE (align_warp_render (in.data(), in.size(), 1, kRate, keys, 4, kEngines[e], nullptr, nullptr, &o.p, &o.n, nullptr) == ALIGN_OK);
        REQUIRE (o.n == (size_t) std::llround (3.0 * kRate));
        const auto y = o.v();
        for (int b = 0; b < 3; ++b)
        {
            // Signalsmith : début de la salve (mesuré : 1,9 à 2,8 ms de retard). Rubber Band : centre de gravité de l'énergie (R2 : jusqu'à 9 ms, R3 : jusqu'à 17 ms d'écart,
            // il étale les attaques) ; le début d'une salve y est précédé d'un pré-écho.
            const size_t from = (size_t) ((expected[b] - 0.1) * kRate), to = (size_t) ((expected[b] + 0.15) * kRate);
            if (e == 0)
            {
                const double t = firstAbove (y, from, to, 0.1f);
                INFO ("signalsmith salve " << b << " mesurée " << t << " s, prévue " << expected[b] << " s");
                REQUIRE (t > 0.0);
                CHECK (std::fabs (t - expected[b]) < 0.004);
            }
            else
            {
                double sw = 0, sx = 0;
                for (size_t i = from; i < to; ++i) { const double w = (double) y[i] * (double) y[i]; sw += w; sx += w * (double) i / kRate; }
                const double centre = expected[b] + 0.004 * (b == 1 ? 1.4 : 0.8); // le centre de la salve, étirée avec son tronçon
                INFO ("rubber band " << e << " salve " << b << " centre mesuré " << sx / sw << " s, prévu " << centre << " s");
                REQUIRE (sw > 0.0);
                CHECK (std::fabs (sx / sw - centre) < 0.020);
            }
        }
    }
}

TEST_CASE ("étirement par marqueurs : mêmes entrées, mêmes échantillons")
{
    const auto in = bursts (2.0, { 0.3, 0.9, 1.5 });
    const double keys[] = { 0, 0, 1.0, 1.3, 2.0, 2.0 };
    for (const char* settings : kEngines)
    {
        Out a, b;
        REQUIRE (align_warp_render (in.data(), in.size(), 1, kRate, keys, 3, settings, nullptr, nullptr, &a.p, &a.n, nullptr) == ALIGN_OK);
        REQUIRE (align_warp_render (in.data(), in.size(), 1, kRate, keys, 3, settings, nullptr, nullptr, &b.p, &b.n, nullptr) == ALIGN_OK);
        REQUIRE (a.n == b.n);
        CHECK (std::memcmp (a.p, b.p, a.n * sizeof (float)) == 0);
    }
}

TEST_CASE ("étirement par marqueurs : refus nets")
{
    const auto in = sine (2.0, 440.0);
    Out o;
    auto run = [&] (std::vector<double> k) { return align_warp_render (in.data(), in.size(), 1, kRate, k.data(), k.size() / 2, nullptr, nullptr, nullptr, &o.p, &o.n, nullptr); };
    CHECK (run ({ 0, 0, 2.0, 9.0 }) == ALIGN_ERR_ARGUMENT);          // rapport 4,5 > 4
    CHECK (std::string (align_last_error()).find ("rapport") != std::string::npos);
    CHECK (run ({ 0, 0, 2.0, 0.4 }) == ALIGN_ERR_ARGUMENT);          // rapport 0,2 < 0,25
    CHECK (run ({ 0, 0, 1.0, 1.0, 2.0, 2.0 }) == ALIGN_OK);
    CHECK (run ({ 0, 0, 1.0, 1.0, 1.005, 1.1, 2.0, 2.0 }) == ALIGN_ERR_ARGUMENT); // tronçon de 5 ms
    CHECK (run ({ 0, 0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0 }) == ALIGN_ERR_ARGUMENT);   // temps source répété
    CHECK (run ({ 0, 0, 1.0, 1.5, 1.5, 1.2, 2.0, 2.0 }) == ALIGN_ERR_ARGUMENT);   // temps de sortie qui recule
    CHECK (run ({ 0.1, 0, 2.0, 2.0 }) == ALIGN_ERR_ARGUMENT);        // pas (0, 0)
    CHECK (run ({ 0, 0, 1.5, 1.5 }) == ALIGN_ERR_ARGUMENT);          // la fin n'est pas la durée de la source
    CHECK (run ({ 0, 0 }) == ALIGN_ERR_ARGUMENT);                    // un seul couple
    CHECK (run ({ 0, 0, 2.0, std::nan ("") }) == ALIGN_ERR_ARGUMENT);
    CHECK (o.p == nullptr);
}

TEST_CASE ("étirement par marqueurs : annulation propre")
{
    const auto in = sine (3.0, 440.0);
    const double keys[] = { 0, 0, 3.0, 4.0 };
    for (const char* settings : kEngines)
    {
        Out o;
        int calls = 0;
        auto cancel = [] (double, void* u) { return ++*static_cast<int*> (u) > 2 ? 1 : 0; };
        CHECK (align_warp_render (in.data(), in.size(), 1, kRate, keys, 2, settings, cancel, &calls, &o.p, &o.n, nullptr) == ALIGN_ERR_CANCELLED);
        CHECK (o.p == nullptr);
        CHECK (o.n == 0);
    }
}

TEST_CASE ("étirement par marqueurs : tronçon à rapport 1 (mesure)")
{
    const auto in = sine (3.0, 220.0);
    const double keys[] = { 0, 0, 1.0, 1.0, 2.0, 2.4, 3.0, 3.4 }; // le premier tronçon (0 à 1 s) est à rapport 1
    for (const char* settings : kEngines)
    {
        Out o;
        REQUIRE (align_warp_render (in.data(), in.size(), 1, kRate, keys, 4, settings, nullptr, nullptr, &o.p, &o.n, nullptr) == ALIGN_OK);
        double worst = 0.0;
        for (size_t i = (size_t) (0.2 * kRate); i < (size_t) (0.8 * kRate); ++i) worst = std::max (worst, (double) std::fabs (o.p[i] - in[i]));
        std::printf ("rapport 1, %s : écart maximal %.4f pour un sinus de crête 0,5\n", settings, worst);
        CHECK (worst < 0.05); // garde large : le chiffre mesuré est rendu à DAW
    }
}
