// SPDX-License-Identifier: GPL-3.0-or-later
/* Démonstration et test de bout en bout de l'interface C, en C pur :
 *   align_demo guide.wav double.wav onsets.txt
 * Lit les deux fichiers, analyse, vérifie le chemin sur les débuts de notes connus (onsets.txt), rend, relit l'analyse sérialisée et vérifie que le rendu est
 * identique, teste l'annulation. Rend 0 si tout va bien. Un exemple d'usage de chaque famille de fonctions. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DR_WAV_IMPLEMENTATION
#include "dr_wav/dr_wav.h"
#include "align/align_c.h"

static int g_fail = 0;
#define CHECK(cond, msg) do { if (cond) printf ("  ok   %s\n", msg); else { printf ("ECHEC  %s\n", msg); ++g_fail; } } while (0)

static float* read_wav (const char* path, unsigned* channels, unsigned* rate, size_t* frames)
{
    drwav_uint64 n = 0;
    float* d = drwav_open_file_and_read_pcm_frames_f32 (path, channels, rate, &n, NULL);
    *frames = (size_t) n;
    return d; /* à libérer par drwav_free */
}

static int progress_print (double f, void* user)
{
    double* last = (double*) user;
    if (f - *last >= 0.25 || f >= 0.999) { printf ("       progression %3.0f %%\n", f * 100.0); *last = f; }
    return 0;
}

static int cancel_at_half (double f, void* user) { (void) user; return f > 0.5; }

int main (int argc, char** argv)
{
    if (argc < 4) { fprintf (stderr, "usage : align_demo guide.wav double.wav onsets.txt\n"); return 2; }
    unsigned gc = 0, gr = 0, dc = 0, dr = 0;
    size_t gn = 0, dn = 0;
    float* guide = read_wav (argv[1], &gc, &gr, &gn);
    float* dub = read_wav (argv[2], &dc, &dr, &dn);
    if (! guide || ! dub || gr != dr) { fprintf (stderr, "fichiers illisibles ou de fréquences différentes\n"); return 2; }
    printf ("bibliothèque %s, interface C version %d ; guide %.2f s, double %.2f s\n", align_version(), align_c_api_version(), (double) gn / gr, (double) dn / dr);

    align_guide* g = NULL;
    align_analysis* a = NULL;
    double last = -1.0;
    CHECK (align_guide_create (guide, gn, (int) gc, (double) gr, NULL, NULL, NULL, &g) == ALIGN_OK, "analyse du guide");
    CHECK (align_analyse (g, dub, dn, (int) dc, (double) dr, NULL, progress_print, &last, &a) == ALIGN_OK, "analyse du double (avec progression)");
    if (! a) { printf ("%s\n", align_last_error()); return 1; }

    /* Vérité connue : début de chaque note, temps du guide -> temps du double. */
    size_t np = 0;
    align_analysis_get_path (a, NULL, 0, &np);
    double* path = (double*) malloc (np * sizeof (double));
    align_analysis_get_path (a, path, np, &np);
    FILE* f = fopen (argv[3], "r");
    char line[256];
    double sum = 0, worst = 0;
    int n = 0;
    while (f && fgets (line, sizeof line, f))
    {
        double gt, dt;
        if (line[0] == '#' || sscanf (line, "%lf %lf", &gt, &dt) != 2) continue;
        size_t i = (size_t) (gt / align_analysis_hop_seconds (a));
        if (i >= np) i = np - 1;
        double e = fabs (path[i] - dt) * 1000.0;
        sum += e; if (e > worst) worst = e; ++n;
    }
    if (f) fclose (f);
    printf ("       %d débuts de notes : erreur moyenne %.1f ms, pire %.1f ms\n", n, n ? sum / n : -1.0, worst);
    CHECK (n > 5 && sum / n < 15.0 && worst < 45.0, "le chemin retrouve les débuts de notes connus");
    free (path);

    float* out = NULL;
    size_t of = 0;
    align_render_info info;
    CHECK (align_render (a, dub, dn, (int) dc, (double) dr, NULL, NULL, NULL, &out, &of, &info) == ALIGN_OK, "rendu (réglages par défaut)");
    CHECK (of == gn && info.fell_back == 0, "la sortie a la durée du guide");

    /* Octets de l'analyse : refaire le rendu plus tard, à l'identique. */
    unsigned char* bytes = NULL;
    size_t bsize = 0;
    align_analysis* back = NULL;
    float* out2 = NULL;
    size_t of2 = 0;
    CHECK (align_analysis_serialize (a, &bytes, &bsize) == ALIGN_OK && align_analysis_deserialize (bytes, bsize, &back) == ALIGN_OK, "analyse enregistrée puis relue");
    CHECK (align_render (back, dub, dn, (int) dc, (double) dr, NULL, NULL, NULL, &out2, &of2, NULL) == ALIGN_OK && of2 == of && memcmp (out, out2, of * sizeof (float) * dc) == 0,
           "le rendu refait depuis les octets est identique");
    printf ("       octets de l'analyse : %zu\n", bsize);

    /* Annulation. */
    align_analysis* c = (align_analysis*) 1;
    CHECK (align_analyse (g, dub, dn, (int) dc, (double) dr, NULL, cancel_at_half, NULL, &c) == ALIGN_ERR_CANCELLED && c == NULL, "annulation propre");

    /* Copier-coller des réglages généraux. */
    char* merged = NULL;
    CHECK (align_settings_merge_general (align_settings_default_json(), "{\"general\":{\"engine\":\"rubberband_r2\"}}", &merged) == ALIGN_OK && strstr (merged, "rubberband_r2") != NULL,
           "fusion des réglages généraux");

    /* Étirement par marqueurs : le double étiré par tronçons (x0,8 puis x1,25), longueur exacte, deux rendus identiques, rapport hors limites refusé. */
    {
        const double src = (double) dn / (double) dr;
        const double keys[6] = { 0.0, 0.0, src * 0.5, src * 0.5 * 0.8, src, src * 0.5 * 0.8 + src * 0.5 * 1.25 };
        float *w1 = NULL, *w2 = NULL;
        size_t n1 = 0, n2 = 0;
        const char* r2 = "{\"general\":{\"engine\":\"rubberband_r2\"}}";
        CHECK (align_warp_render (dub, dn, (int) dc, (double) dr, keys, 3, NULL, NULL, NULL, &w1, &n1, NULL) == ALIGN_OK
               && n1 == (size_t) (keys[5] * (double) dr + 0.5), "étirement par marqueurs : longueur exacte (Signalsmith)");
        CHECK (align_warp_render (dub, dn, (int) dc, (double) dr, keys, 3, NULL, NULL, NULL, &w2, &n2, NULL) == ALIGN_OK && n2 == n1 && memcmp (w1, w2, n1 * sizeof (float) * dc) == 0,
               "étirement par marqueurs : deux rendus identiques");
        align_free (w1); align_free (w2); w1 = w2 = NULL;
        CHECK (align_warp_render (dub, dn, (int) dc, (double) dr, keys, 3, r2, NULL, NULL, &w1, &n1, NULL) == ALIGN_OK
               && align_warp_render (dub, dn, (int) dc, (double) dr, keys, 3, r2, NULL, NULL, &w2, &n2, NULL) == ALIGN_OK && memcmp (w1, w2, n1 * sizeof (float) * dc) == 0,
               "étirement par marqueurs : deux rendus identiques (Rubber Band R2)");
        align_free (w1); align_free (w2); w1 = NULL;
        {
            const double bad[4] = { 0.0, 0.0, src, src * 5.0 };
            CHECK (align_warp_render (dub, dn, (int) dc, (double) dr, bad, 2, NULL, NULL, NULL, &w1, &n1, NULL) == ALIGN_ERR_ARGUMENT && w1 == NULL,
                   "étirement par marqueurs : rapport hors de [0,25 ; 4] refusé");
        }
    }

    align_free (merged); align_free (out); align_free (out2); align_free (bytes);
    align_analysis_free (a); align_analysis_free (back); align_guide_free (g);
    drwav_free (guide, NULL); drwav_free (dub, NULL);
    printf (g_fail ? "DES ECHECS\n" : "TOUT EST BON\n");
    return g_fail ? 1 : 0;
}
