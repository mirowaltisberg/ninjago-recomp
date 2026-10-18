#include <pthread.h>
#include <unistd.h>

#include "nj_test.h"

#include "nj/backend.h"
#include "nj/guest.h"
#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/loader.h"

#define PAGE 0x00F00000u
#define FN_ADD (PAGE + 0x00)      /* add r0,r0,r1; bx lr */
#define FN_CALL_R3 (PAGE + 0x10)  /* push {lr}; blx r3; pop {pc} */
#define FN_VFP (PAGE + 0x20)      /* vmov s0,r0; vmov s1,r1; vadd.f32 s0,s0,s1; vmov r0,s0; bx lr */
#define FN_INC (PAGE + 0x40)      /* add r0,r0,#1; bx lr */
#define FN_STACKARGS (PAGE + 0x50)/* ldr r0,[sp]; ldr r1,[sp,#4]; add r0,r0,r1; bx lr */

static const char *RENDER = "Java_com_wbgames_LEGOgame_GameGLSurfaceView_nativeRender";
static NjImage img;
static int have_img;

static void put(uint32_t ga, const uint32_t *w, int n) { for (int i = 0; i < n; i++) nj_w32(ga + 4u * i, w[i]); }

static void t_double(NjCtx *c) { hle_ret(c, 2 * hle_arg(c, 0)); }
static void t_nested(NjCtx *c) {
    uint32_t args[2] = {hle_arg(c, 0), hle_arg(c, 1)};
    uint32_t sum = nj_call(c, FN_ADD, 2, args);
    hle_ret(c, sum + 1);
}
static void t_override42(NjCtx *c) { hle_ret(c, 42); }

static void setup(void) {
    static int done;
    if (done) return;
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_mem_map_fixed_regions(), 0);
    NJ_EQ_U(nj_heap_init(), 0);
    NJ_EQ_U(nj_mem_map(PAGE, 0x4000, NJ_PROT_R | NJ_PROT_W | NJ_PROT_X), 0);
    const uint32_t add[] = {0xE0800001, 0xE12FFF1E};
    const uint32_t callr3[] = {0xE92D4000, 0xE12FFF33, 0xE8BD8000};
    const uint32_t vfp[] = {0xEE000A10, 0xEE001A90, 0xEE300A20, 0xEE100A10, 0xE12FFF1E};
    const uint32_t inc[] = {0xE2800001, 0xE12FFF1E};
    const uint32_t stk[] = {0xE59D0000, 0xE59D1004, 0xE0800001, 0xE12FFF1E};
    put(FN_ADD, add, 2);
    put(FN_CALL_R3, callr3, 3);
    put(FN_VFP, vfp, 5);
    put(FN_INC, inc, 2);
    put(FN_STACKARGS, stk, 4);
    hle_register("t_double", t_double);
    hle_register("t_nested", t_nested);
    if (access(NJ_GUEST_SO_PATH, R_OK) == 0 && nj_image_load(NJ_GUEST_SO_PATH, NJ_IMAGE_BASE, &img) == 0) {
        have_img = 1;
        hle_register_override(RENDER, t_override42);
        hle_resolve_overrides(&img);
        nj_backend_set_image(&img);
    }
    NJ_EQ_U(nj_backend_init(), 0);
    done = 1;
}

NJ_TEST(uc_add) {
    setup();
    uint32_t a[2] = {40, 2};
    NjCtx *c = nj_ctx();
    NJ_EQ_U(nj_call(c, FN_ADD, 2, a), 42);
    NJ_EQ_U(c->r[0], 42);
}

NJ_TEST(uc_calls_import) {
    setup();
    uint32_t a[4] = {21, 0, 0, hle_thunk("t_double")};
    NJ_EQ_U(nj_call(nj_ctx(), FN_CALL_R3, 4, a), 42);
}

NJ_TEST(uc_nested) {
    setup();
    uint32_t a[4] = {40, 2, 0, hle_thunk("t_nested")};
    NJ_EQ_U(nj_call(nj_ctx(), FN_CALL_R3, 4, a), 43);
}

NJ_TEST(uc_vfp) {
    setup();
    float x = 1.5f, y = 2.0f, want = 3.5f;
    uint32_t a[2], w;
    memcpy(&a[0], &x, 4);
    memcpy(&a[1], &y, 4);
    memcpy(&w, &want, 4);
    NJ_EQ_U(nj_call(nj_ctx(), FN_VFP, 2, a), w);
}

NJ_TEST(uc_stack_args) {
    setup();
    uint32_t a[6] = {0, 0, 0, 0, 40, 2};
    NJ_EQ_U(nj_call(nj_ctx(), FN_STACKARGS, 6, a), 42);
}

NJ_TEST(uc_preserves_caller_ctx) {
    setup();
    NjCtx *c = nj_ctx();
    c->r[5] = 0x55555555;
    uint32_t sp = c->r[13];
    uint32_t a[2] = {1, 2};
    nj_call(c, FN_ADD, 2, a);
    NJ_EQ_U(c->r[5], 0x55555555);
    NJ_EQ_U(c->r[13], sp);
}

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static uint32_t thread_ret;
static int thread_done;

static void on_exit_cb(uint32_t ret, void *ud) {
    (void)ud;
    pthread_mutex_lock(&mu);
    thread_ret = ret;
    thread_done = 1;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mu);
}

NJ_TEST(uc_thread) {
    setup();
    NJ_EQ_U(nj_spawn_thread(FN_INC, 41, on_exit_cb, NULL), 0);
    pthread_mutex_lock(&mu);
    while (!thread_done) pthread_cond_wait(&cv, &mu);
    pthread_mutex_unlock(&mu);
    NJ_EQ_U(thread_ret, 42);
}

NJ_TEST(uc_fault_is_fatal) {
    setup();
    NJ_EXPECT_FATAL(nj_call(nj_ctx(), 0x00E00000u, 0, NULL), "0x00e00000");
}

NJ_TEST(uc_override_hook) {
    setup();
    if (!have_img) NJ_SKIP("guest library not present");
    NJ_EQ_U(nj_call(nj_ctx(), nj_image_sym(&img, RENDER), 0, NULL), 42);
}

NJ_TEST(uc_mem_frozen_after_init) {
    setup();
    NJ_EXPECT_FATAL(nj_mem_map(0x00E00000u, 0x4000, NJ_PROT_R), "frozen");
}
