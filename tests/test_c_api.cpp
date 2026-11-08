#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    for (double v : shift) REQUIRE (v == 0.0);
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
