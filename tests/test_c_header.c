/* Compilé en C (pas en C++) : l'en-tête doit être valide pour un compilateur C. Rend 1 si tout est cohérent. */
#include "align/align_c.h"

static int progress_cb (double fraction, void* user) { (void) fraction; (void) user; return 0; }

int c_header_check (void)
{
    align_guide* g = 0;
    align_analysis* a = 0;
    float* out = 0;
    size_t frames = 0, count = 0;
    unsigned char* bytes = 0;
    align_render_info info;
    align_progress_fn fn = progress_cb;
    /* les adresses des fonctions suffisent : on vérifie que tout se déclare et se compile en C */
    int (*f1)(const float*, size_t, int, double, const char*, align_progress_fn, void*, align_guide**) = align_guide_create;
    int (*f2)(const align_guide*, const float*, size_t, int, double, const char*, align_progress_fn, void*, align_analysis**) = align_analyse;
    int (*f3)(const align_analysis*, const float*, size_t, int, double, const char*, align_progress_fn, void*, float**, size_t*, align_render_info*) = align_render;
    (void) g; (void) a; (void) out; (void) frames; (void) count; (void) bytes; (void) info; (void) fn; (void) f1; (void) f2; (void) f3;
    return ALIGN_C_API_VERSION == align_c_api_version() && ALIGN_OK == 0 && ALIGN_ERR_CANCELLED == 2;
}
