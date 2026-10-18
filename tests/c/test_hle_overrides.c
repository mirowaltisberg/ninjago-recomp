#include <pthread.h>
#include <unistd.h>

#include "nj_test.h"

#include "nj/guest.h"
#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/hle_overrides.h"
#include "nj/loader.h"

static NjImage img;
static NjHleFn acquire, release;

static void setup(void) {
    static int done;
    if (access(NJ_GUEST_SO_PATH, R_OK) != 0) NJ_SKIP("guest library not present");
    if (done) return;
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_mem_map_fixed_regions(), 0);
    NJ_EQ_U(nj_heap_init(), 0);
    NJ_EQ_U(nj_image_load(NJ_GUEST_SO_PATH, NJ_IMAGE_BASE, &img), 0);
    hle_overrides_register();
    hle_resolve_overrides(&img);
    acquire = hle_override_at(NJ_IMAGE_BASE + 0x3ef470);
    release = hle_override_at(NJ_IMAGE_BASE + 0x3ef4d4);
    done = 1;
}

static uint32_t call(NjHleFn fn, uint32_t guard) {
    NjCtx c = {0};
    c.r[0] = guard;
    fn(&c);
    return c.r[0];
}

static uint32_t guard_ga;
static volatile int waiter_done;
static uint32_t waiter_result;

static void *waiter(void *ud) {
    (void)ud;
    waiter_result = call(acquire, guard_ga);
    __atomic_store_n(&waiter_done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

NJ_TEST(cxa_guard_overrides_are_registered) {
    setup();
    NJ_CHECK(acquire != NULL);
    NJ_CHECK(release != NULL);
}

/* gabi++ __cxa_guard_*: bit 0 initialised, 0x100 initialisation pending, 0x200 another thread waiting. */
NJ_TEST(cxa_guard_one_initialiser_others_wait) {
    setup();
    guard_ga = nj_calloc(1, 4);
    NJ_EQ_U(call(acquire, guard_ga), 1); /* first caller initialises */
    NJ_EQ_U(nj_r32(guard_ga), 0x100);
    pthread_t t;
    pthread_create(&t, NULL, waiter, NULL);
    usleep(50000);
    NJ_EQ_U(waiter_done, 0); /* blocked while initialisation is pending */
    NJ_EQ_U(nj_r32(guard_ga), 0x300);
    call(release, guard_ga);
    pthread_join(t, NULL);
    NJ_EQ_U(waiter_result, 0); /* woken, finds it initialised */
    NJ_EQ_U(nj_r32(guard_ga), 1);
    NJ_EQ_U(call(acquire, guard_ga), 0);
}
