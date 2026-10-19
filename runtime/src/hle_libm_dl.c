/* libm (softfp: floats in r0/r1, doubles in r0:r1 / r2:r3) and libdl emulation. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "nj/hle.h"
#include "nj/hle_misc.h"

#define F(i) hle_arg_f(c, i)
#define D(i) hle_arg_d(c, i)

#define UNARY_F(name) static void hle_##name(NjCtx *c) { hle_ret_f(c, name(F(0))); }
#define UNARY_D(name) static void hle_##name(NjCtx *c) { hle_ret_d(c, name(D(0))); }
#define BINARY_F(name) static void hle_##name(NjCtx *c) { hle_ret_f(c, name(F(0), F(1))); }
#define BINARY_D(name) static void hle_##name(NjCtx *c) { hle_ret_d(c, name(D(0), D(2))); }

UNARY_F(acosf) UNARY_F(atanf) UNARY_F(ceilf) UNARY_F(cosf) UNARY_F(floorf) UNARY_F(log10f)
UNARY_F(sinf) UNARY_F(sqrtf)
UNARY_D(asin) UNARY_D(ceil) UNARY_D(cos) UNARY_D(floor) UNARY_D(log10) UNARY_D(sin) UNARY_D(sqrt)
BINARY_F(atan2f) BINARY_F(powf)
BINARY_D(fmod) BINARY_D(pow)

static void hle___isfinitef(NjCtx *c) { hle_ret(c, isfinite(F(0)) ? 1 : 0); }

void hle_libm_register(void) {
    static const struct { const char *name; NjHleFn fn; } fns[] = {
        {"acosf", hle_acosf}, {"asin", hle_asin}, {"atan2f", hle_atan2f}, {"atanf", hle_atanf},
        {"ceil", hle_ceil}, {"ceilf", hle_ceilf}, {"cos", hle_cos}, {"cosf", hle_cosf},
        {"floor", hle_floor}, {"floorf", hle_floorf}, {"fmod", hle_fmod}, {"log10", hle_log10},
        {"log10f", hle_log10f}, {"pow", hle_pow}, {"powf", hle_powf}, {"sin", hle_sin},
        {"sinf", hle_sinf}, {"sqrt", hle_sqrt}, {"sqrtf", hle_sqrtf}, {"__isfinitef", hle___isfinitef},
    };
    for (size_t i = 0; i < sizeof fns / sizeof fns[0]; i++) hle_register(fns[i].name, fns[i].fn);
}

/* ---- libdl ---- */

static const NjImage *image;
static uint32_t self_handle;

/* Symbols the engine probes but that must look absent (no program binaries on iOS). */
static const char *const absent[] = {"glGetProgramBinaryOES", "glProgramBinaryOES"};

static void hle_dlopen(NjCtx *c) {
    const char *name = hle_cstr(hle_arg(c, 0));
    if (name && strstr(name, "libLEGO_M1")) {
        if (!self_handle) self_handle = hle_data_alloc(4, 4);
        hle_ret(c, self_handle);
        return;
    }
    hle_ret(c, hle_data_alloc(4, 4)); /* any distinct non-zero handle */
}

static void hle_dlsym(NjCtx *c) {
    uint32_t handle = hle_arg(c, 0);
    const char *name = hle_cstr(hle_arg(c, 1));
    if (!name) { hle_ret(c, 0); return; }
    if (handle && handle == self_handle && image) { hle_ret(c, nj_image_sym(image, name)); return; }
    for (size_t i = 0; i < sizeof absent / sizeof absent[0]; i++)
        if (strcmp(name, absent[i]) == 0) { hle_ret(c, 0); return; }
    if (hle_lookup(name)) { hle_ret(c, hle_thunk(name)); return; }
    fprintf(stderr, "[dl] dlsym(%s): not available\n", name);
    hle_ret(c, 0);
}

static void hle_dlclose(NjCtx *c) { hle_ret(c, 0); }

void hle_dl_register(const NjImage *img) {
    image = img;
    hle_register("dlopen", hle_dlopen);
    hle_register("dlsym", hle_dlsym);
    hle_register("dlclose", hle_dlclose);
}
