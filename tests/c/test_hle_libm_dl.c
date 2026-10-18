#include <math.h>
#include <unistd.h>

#include "nj_test.h"

#include "nj/guest.h"
#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/hle_misc.h"

static NjImage img;
static int have_img;

static void placeholder(NjCtx *c) { (void)c; }

static void setup(void) {
    static int done;
    if (done) return;
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_mem_map_fixed_regions(), 0);
    NJ_EQ_U(nj_heap_init(), 0);
    if (access(NJ_GUEST_SO_PATH, R_OK) == 0 && nj_image_load(NJ_GUEST_SO_PATH, NJ_IMAGE_BASE, &img) == 0) have_img = 1;
    hle_libm_register();
    hle_dl_register(have_img ? &img : NULL);
    hle_register("eglGetProcAddress", placeholder); /* stands in for the EGL module */
    done = 1;
}

static uint32_t fb(float f) { uint32_t b; memcpy(&b, &f, 4); return b; }
static float bf(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }
static uint64_t db(double d) { uint64_t b; memcpy(&b, &d, 8); return b; }
static double bd(uint32_t lo, uint32_t hi) { uint64_t b = (uint64_t)lo | (uint64_t)hi << 32; double d; memcpy(&d, &b, 8); return d; }

static NjCtx ctx;

static void call(const char *name) { hle_dispatch(&ctx, hle_thunk(name)); }

NJ_TEST(pow_softfp) {
    setup();
    uint64_t a = db(2.0), b = db(10.0);
    ctx.r[0] = (uint32_t)a; ctx.r[1] = (uint32_t)(a >> 32);
    ctx.r[2] = (uint32_t)b; ctx.r[3] = (uint32_t)(b >> 32);
    call("pow");
    NJ_CHECK(bd(ctx.r[0], ctx.r[1]) == 1024.0);
}

NJ_TEST(powf_softfp) {
    setup();
    ctx.r[0] = fb(2.0f);
    ctx.r[1] = fb(0.5f);
    call("powf");
    NJ_CHECK(fabsf(bf(ctx.r[0]) - 1.41421356f) < 1e-6f);
}

NJ_TEST(float_and_double_unary) {
    setup();
    ctx.r[0] = fb(2.7f);
    call("floorf");
    NJ_CHECK(bf(ctx.r[0]) == 2.0f);
    uint64_t d = db(-2.5);
    ctx.r[0] = (uint32_t)d; ctx.r[1] = (uint32_t)(d >> 32);
    call("ceil");
    NJ_CHECK(bd(ctx.r[0], ctx.r[1]) == -2.0);
    ctx.r[0] = fb(1.0f);
    ctx.r[1] = fb(1.0f);
    call("atan2f");
    NJ_CHECK(fabsf(bf(ctx.r[0]) - 0.78539816f) < 1e-6f);
    uint64_t x = db(7.5), y = db(2.0);
    ctx.r[0] = (uint32_t)x; ctx.r[1] = (uint32_t)(x >> 32); ctx.r[2] = (uint32_t)y; ctx.r[3] = (uint32_t)(y >> 32);
    call("fmod");
    NJ_CHECK(bd(ctx.r[0], ctx.r[1]) == 1.5);
}

NJ_TEST(isfinitef_nan_inf) {
    setup();
    ctx.r[0] = 0x7FC00000u; /* NaN */
    call("__isfinitef");
    NJ_EQ_U(ctx.r[0], 0);
    ctx.r[0] = 0x7F800000u; /* +inf */
    call("__isfinitef");
    NJ_EQ_U(ctx.r[0], 0);
    ctx.r[0] = fb(3.0f);
    call("__isfinitef");
    NJ_EQ_U(ctx.r[0], 1);
}

NJ_TEST(dlsym_known_unknown) {
    setup();
    ctx.r[0] = hle_guest_strdup("libEGL.so");
    ctx.r[1] = 0;
    call("dlopen");
    uint32_t h = ctx.r[0];
    NJ_CHECK(h != 0);
    ctx.r[0] = h; ctx.r[1] = hle_guest_strdup("eglGetProcAddress");
    call("dlsym");
    NJ_CHECK(hle_is_thunk(ctx.r[0]));
    ctx.r[0] = h; ctx.r[1] = hle_guest_strdup("glProgramBinaryOES");
    call("dlsym");
    NJ_EQ_U(ctx.r[0], 0);
    ctx.r[0] = h; ctx.r[1] = hle_guest_strdup("totally_unknown_symbol");
    call("dlsym");
    NJ_EQ_U(ctx.r[0], 0);
    ctx.r[0] = h;
    call("dlclose");
    NJ_EQ_U(ctx.r[0], 0);
}

NJ_TEST(dlsym_own_library) {
    setup();
    if (!have_img) NJ_SKIP("guest library not present");
    ctx.r[0] = hle_guest_strdup("libLEGO_M1.so");
    ctx.r[1] = 0;
    call("dlopen");
    uint32_t h = ctx.r[0];
    ctx.r[0] = h; ctx.r[1] = hle_guest_strdup("Java_com_wbgames_LEGOgame_GameGLSurfaceView_nativeRender");
    call("dlsym");
    NJ_EQ_U(ctx.r[0], nj_image_sym(&img, "Java_com_wbgames_LEGOgame_GameGLSurfaceView_nativeRender"));
}
