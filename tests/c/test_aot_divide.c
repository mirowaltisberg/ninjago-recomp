/* The libgcc division helpers inside the guest library, run as recompiled code (whole-library AOT build). */
#include <unistd.h>

#include "nj_test.h"

#include "nj/backend.h"
#include "nj/guest.h"
#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/loader.h"

static NjImage img;

static void setup(void) {
    static int done;
    if (access(NJ_GUEST_SO_PATH, R_OK) != 0) NJ_SKIP("guest library not present");
    if (done) return;
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_mem_map_fixed_regions(), 0);
    NJ_EQ_U(nj_heap_init(), 0);
    NJ_EQ_U(nj_image_load(NJ_GUEST_SO_PATH, NJ_IMAGE_BASE, &img), 0);
    nj_backend_set_image(&img);
    NJ_EQ_U(nj_backend_init(), 0);
    done = 1;
}

static uint64_t call(const char *fn, uint32_t a, uint32_t b, uint32_t c2, uint32_t d) {
    uint32_t args[4] = {a, b, c2, d};
    NjCtx *c = nj_ctx();
    uint32_t r0 = nj_call(c, nj_image_sym(&img, fn), 4, args);
    return (uint64_t)c->r[1] << 32 | r0;
}

static const uint32_t U[] = {0, 1, 2, 3, 7, 10, 100, 255, 1000, 65535, 0x12345678u, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu};
#define N (sizeof U / sizeof U[0])

NJ_TEST(aot_uidiv_and_uidivmod) {
    setup();
    for (unsigned i = 0; i < N; i++)
        for (unsigned j = 1; j < N; j++) {
            uint32_t a = U[i], b = U[j];
            NJ_EQ_U((uint32_t)call("__aeabi_uidiv", a, b, 0, 0), a / b);
            uint64_t qr = call("__aeabi_uidivmod", a, b, 0, 0);
            NJ_EQ_U((uint32_t)qr, a / b);
            NJ_EQ_U((uint32_t)(qr >> 32), a % b);
        }
}

NJ_TEST(aot_idiv_and_idivmod) {
    setup();
    for (unsigned i = 0; i < N; i++)
        for (unsigned j = 1; j < N; j++) {
            int32_t a = (int32_t)U[i], b = (int32_t)U[j];
            if (a == INT32_MIN && b == -1) continue;
            NJ_EQ_U((uint32_t)call("__aeabi_idiv", (uint32_t)a, (uint32_t)b, 0, 0), (uint32_t)(a / b));
            uint64_t qr = call("__aeabi_idivmod", (uint32_t)a, (uint32_t)b, 0, 0);
            NJ_EQ_U((uint32_t)qr, (uint32_t)(a / b));
            NJ_EQ_U((uint32_t)(qr >> 32), (uint32_t)(a % b));
        }
}

NJ_TEST(aot_uldivmod_and_ldivmod) {
    setup();
    static const uint64_t V[] = {0, 1, 7, 1000, 0xffffffffull, 0x100000000ull, 0x123456789abcdefull, 0x7fffffffffffffffull,
                                 0x8000000000000000ull, 0xffffffffffffffffull, 12345678901ull};
    for (unsigned i = 0; i < sizeof V / sizeof V[0]; i++)
        for (unsigned j = 1; j < sizeof V / sizeof V[0]; j++) {
            uint64_t a = V[i], b = V[j];
            NJ_EQ_U(call("__aeabi_uldivmod", (uint32_t)a, (uint32_t)(a >> 32), (uint32_t)b, (uint32_t)(b >> 32)), a / b);
            int64_t sa = (int64_t)a, sb = (int64_t)b;
            if (sa == INT64_MIN && sb == -1) continue;
            NJ_EQ_U(call("__aeabi_ldivmod", (uint32_t)a, (uint32_t)(a >> 32), (uint32_t)b, (uint32_t)(b >> 32)),
                    (uint64_t)(sa / sb));
        }
}
