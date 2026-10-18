/* EGL subset the engine uses: current-context queries, a second shared context with a pbuffer
 * surface (background loading), and eglGetProcAddress. Contexts map to platform contexts. */
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "nj/gles.h"
#include "nj/hle.h"
#include "nj/platform.h"

#define A(i) hle_arg(c, i)
#define EGL_SUCCESS 0x3000
#define EGL_DRAW 0x3059

static uint32_t display, config, main_ctx, main_surface;

#define MAX_CTX 16
static struct { uint32_t ga; int id; } ctxs[MAX_CTX];
static int nctx;
static pthread_mutex_t egl_lock = PTHREAD_MUTEX_INITIALIZER;

static _Thread_local uint32_t cur_ctx, cur_draw, cur_read;

unsigned nj_egl_config_handle(void) { return config; }

void nj_egl_set_current_main(void) {
    cur_ctx = main_ctx;
    cur_draw = cur_read = main_surface;
    nj_platform_make_current(1);
}

static int ctx_id(uint32_t ga) {
    pthread_mutex_lock(&egl_lock);
    int id = 0;
    for (int i = 0; i < nctx; i++) if (ctxs[i].ga == ga) { id = ctxs[i].id; break; }
    pthread_mutex_unlock(&egl_lock);
    return id;
}

static void e_GetCurrentDisplay(NjCtx *c) { hle_ret(c, display); }
static void e_GetCurrentContext(NjCtx *c) { hle_ret(c, cur_ctx); }
static void e_GetCurrentSurface(NjCtx *c) { hle_ret(c, A(0) == EGL_DRAW ? cur_draw : cur_read); }
static void e_True(NjCtx *c) { hle_ret(c, 1); }
static void e_GetError(NjCtx *c) { hle_ret(c, EGL_SUCCESS); }

static void e_ChooseConfig(NjCtx *c) {
    uint32_t configs = A(2), size = A(3), num = hle_arg(c, 4);
    if (configs && size) nj_w32(configs, config);
    if (num) nj_w32(num, 1);
    hle_ret(c, 1);
}

static void e_CreateContext(NjCtx *c) {
    int share = A(2) ? ctx_id(A(2)) : 0;
    int id = nj_platform_create_context(share ? share : 1);
    if (id <= 0) { hle_ret(c, 0); return; }
    uint32_t ga = hle_data_alloc(16, 4);
    pthread_mutex_lock(&egl_lock);
    if (nctx < MAX_CTX) { ctxs[nctx].ga = ga; ctxs[nctx].id = id; nctx++; }
    pthread_mutex_unlock(&egl_lock);
    hle_ret(c, ga);
}

static void e_CreatePbufferSurface(NjCtx *c) { hle_ret(c, hle_data_alloc(16, 4)); }

static void e_MakeCurrent(NjCtx *c) {
    uint32_t draw = A(1), read = A(2), ctx = A(3);
    int id = ctx ? ctx_id(ctx) : 0;
    if (ctx && !id) { hle_ret(c, 0); return; }
    if (!nj_platform_make_current(id)) { hle_ret(c, 0); return; }
    cur_ctx = ctx;
    cur_draw = draw;
    cur_read = read;
    hle_ret(c, 1);
}

static void e_GetProcAddress(NjCtx *c) {
    const char *name = hle_cstr(A(0));
    if (!name || !strcmp(name, "glGetProgramBinaryOES") || !strcmp(name, "glProgramBinaryOES")) { hle_ret(c, 0); return; }
    if (hle_lookup(name)) { hle_ret(c, hle_thunk(name)); return; }
    fprintf(stderr, "[egl] eglGetProcAddress(%s): not available\n", name);
    hle_ret(c, 0);
}

void hle_egl_register(void) {
    if (!display) {
        display = hle_data_alloc(16, 4);
        config = hle_data_alloc(16, 4);
        main_surface = hle_data_alloc(16, 4);
        main_ctx = hle_data_alloc(16, 4);
        ctxs[0].ga = main_ctx;
        ctxs[0].id = 1;
        nctx = 1;
    }
    hle_register("eglGetCurrentDisplay", e_GetCurrentDisplay);
    hle_register("eglGetCurrentContext", e_GetCurrentContext);
    hle_register("eglGetCurrentSurface", e_GetCurrentSurface);
    hle_register("eglBindAPI", e_True);
    hle_register("eglSwapInterval", e_True);
    hle_register("eglGetError", e_GetError);
    hle_register("eglChooseConfig", e_ChooseConfig);
    hle_register("eglCreateContext", e_CreateContext);
    hle_register("eglCreatePbufferSurface", e_CreatePbufferSurface);
    hle_register("eglMakeCurrent", e_MakeCurrent);
    hle_register("eglGetProcAddress", e_GetProcAddress);
}
