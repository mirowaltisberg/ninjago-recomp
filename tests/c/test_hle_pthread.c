#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include "nj_test.h"

#include "nj/backend.h"
#include "nj/guest.h"
#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/hle_libc.h"
#include "nj/hle_pthread.h"

#define PAGE 0x00F00000u
#define FN_INC (PAGE + 0x00) /* add r0,r0,#1; bx lr */

static void setup(void) {
    static int done;
    if (done) return;
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_mem_map_fixed_regions(), 0);
    NJ_EQ_U(nj_heap_init(), 0);
    NJ_EQ_U(nj_mem_map(PAGE, 0x4000, NJ_PROT_R | NJ_PROT_W | NJ_PROT_X), 0);
    nj_w32(FN_INC, 0xE2800001);
    nj_w32(FN_INC + 4, 0xE12FFF1E);
    hle_libc_core_register(NULL);
    hle_pthread_register();
    hle_time_register();
    NJ_EQ_U(nj_backend_init(), 0);
    done = 1;
}

static uint32_t call(const char *name, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    NjCtx *c = nj_ctx();
    c->r[0] = a0; c->r[1] = a1; c->r[2] = a2; c->r[3] = a3;
    hle_dispatch(c, hle_thunk(name));
    return c->r[0];
}

NJ_TEST(pt_create_join) {
    setup();
    uint32_t t = nj_malloc(4), ret = nj_malloc(4);
    NJ_EQ_U(call("pthread_create", t, 0, FN_INC, 41), 0);
    NJ_CHECK(nj_r32(t) != 0);
    NJ_EQ_U(call("pthread_join", nj_r32(t), ret, 0, 0), 0);
    NJ_EQ_U(nj_r32(ret), 42);
}

NJ_TEST(pt_self_equal) {
    setup();
    uint32_t a = call("pthread_self", 0, 0, 0, 0);
    NJ_CHECK(a != 0);
    NJ_EQ_U(call("pthread_self", 0, 0, 0, 0), a);
    NJ_CHECK(call("pthread_equal", a, a, 0, 0) != 0);
    NJ_EQ_U(call("pthread_equal", a, a + 4, 0, 0), 0);
}

NJ_TEST(pt_detached_attr) {
    setup();
    uint32_t attr = nj_calloc(1, NJ_BIONIC_PTHREAD_ATTR_SIZE), t = nj_malloc(4);
    NJ_EQ_U(call("pthread_attr_init", attr, 0, 0, 0), 0);
    NJ_EQ_U(call("pthread_attr_setdetachstate", attr, 1 /* PTHREAD_CREATE_DETACHED */, 0, 0), 0);
    NJ_EQ_U(call("pthread_create", t, attr, FN_INC, 1), 0);
    NJ_EQ_U(call("pthread_attr_destroy", attr, 0, 0, 0), 0);
    usleep(20000);
}

NJ_TEST(pt_recursive) {
    setup();
    uint32_t attr = nj_calloc(1, 4), m = nj_calloc(1, 4);
    call("pthread_mutexattr_init", attr, 0, 0, 0);
    NJ_EQ_U(call("pthread_mutexattr_settype", attr, 1 /* RECURSIVE */, 0, 0), 0);
    NJ_EQ_U(call("pthread_mutex_init", m, attr, 0, 0), 0);
    NJ_EQ_U(call("pthread_mutex_lock", m, 0, 0, 0), 0);
    NJ_EQ_U(call("pthread_mutex_lock", m, 0, 0, 0), 0);
    NJ_EQ_U(call("pthread_mutex_unlock", m, 0, 0, 0), 0);
    NJ_EQ_U(call("pthread_mutex_unlock", m, 0, 0, 0), 0);
    NJ_EQ_U(call("pthread_mutex_destroy", m, 0, 0, 0), 0);
}

NJ_TEST(pt_static_recursive_0x4000) {
    setup();
    uint32_t m = nj_calloc(1, 4);
    nj_w32(m, 0x4000); /* PTHREAD_RECURSIVE_MUTEX_INITIALIZER */
    NJ_EQ_U(call("pthread_mutex_lock", m, 0, 0, 0), 0);
    NJ_EQ_U(call("pthread_mutex_trylock", m, 0, 0, 0), 0);
    call("pthread_mutex_unlock", m, 0, 0, 0);
    call("pthread_mutex_unlock", m, 0, 0, 0);
}

NJ_TEST(pt_trylock_busy) {
    setup();
    uint32_t m = nj_calloc(1, 4); /* static normal initializer */
    NJ_EQ_U(call("pthread_mutex_lock", m, 0, 0, 0), 0);
    NJ_EQ_U(call("pthread_mutex_trylock", m, 0, 0, 0), NJ_EBUSY);
    call("pthread_mutex_unlock", m, 0, 0, 0);
}

static uint32_t g_m, g_cv, g_count;

static void *producer(void *arg) {
    (void)arg;
    for (int i = 0; i < 1000; i++) {
        call("pthread_mutex_lock", g_m, 0, 0, 0);
        nj_w32(g_count, nj_r32(g_count) + 1);
        call("pthread_cond_signal", g_cv, 0, 0, 0);
        call("pthread_mutex_unlock", g_m, 0, 0, 0);
    }
    return NULL;
}

NJ_TEST(pt_cond_handshake) {
    setup();
    g_m = nj_calloc(1, 4);
    g_cv = nj_calloc(1, 4);
    g_count = nj_calloc(1, 4);
    NJ_EQ_U(call("pthread_cond_init", g_cv, 0, 0, 0), 0);
    pthread_t th;
    pthread_create(&th, NULL, producer, NULL);
    uint32_t consumed = 0;
    call("pthread_mutex_lock", g_m, 0, 0, 0);
    while (consumed < 1000) {
        while (nj_r32(g_count) == 0) call("pthread_cond_wait", g_cv, g_m, 0, 0);
        consumed += nj_r32(g_count);
        nj_w32(g_count, 0);
    }
    call("pthread_mutex_unlock", g_m, 0, 0, 0);
    pthread_join(th, NULL);
    NJ_EQ_U(consumed, 1000);
    call("pthread_cond_broadcast", g_cv, 0, 0, 0);
    NJ_EQ_U(call("pthread_cond_destroy", g_cv, 0, 0, 0), 0);
}

static uint32_t g_key, g_seen[2];

static void *key_thread(void *arg) {
    int i = (int)(intptr_t)arg;
    call("pthread_setspecific", nj_r32(g_key), 100 + (uint32_t)i, 0, 0);
    usleep(5000);
    g_seen[i] = call("pthread_getspecific", nj_r32(g_key), 0, 0, 0);
    return NULL;
}

NJ_TEST(pt_keys) {
    setup();
    g_key = nj_malloc(4);
    NJ_EQ_U(call("pthread_key_create", g_key, 0, 0, 0), 0);
    pthread_t a, b;
    pthread_create(&a, NULL, key_thread, (void *)0);
    pthread_create(&b, NULL, key_thread, (void *)1);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    NJ_EQ_U(g_seen[0], 100);
    NJ_EQ_U(g_seen[1], 101);
    NJ_EQ_U(call("pthread_getspecific", nj_r32(g_key), 0, 0, 0), 0);
    NJ_EQ_U(call("pthread_key_delete", nj_r32(g_key), 0, 0, 0), 0);
}

NJ_TEST(sem_post_wait) {
    setup();
    uint32_t s = nj_calloc(1, 4);
    NJ_EQ_U(call("sem_init", s, 0, 2, 0), 0);
    NJ_EQ_U(call("sem_wait", s, 0, 0, 0), 0);
    NJ_EQ_U(call("sem_wait", s, 0, 0, 0), 0);
    NJ_EQ_U(call("sem_post", s, 0, 0, 0), 0);
    NJ_EQ_U(call("sem_wait", s, 0, 0, 0), 0);
    NJ_EQ_U(call("sem_destroy", s, 0, 0, 0), 0);
}

NJ_TEST(sched_priorities) {
    setup();
    NJ_EQ_U(call("sched_get_priority_max", 0, 0, 0, 0), 0);
    NJ_EQ_U(call("sched_get_priority_max", 1 /* SCHED_FIFO */, 0, 0, 0), 99);
    NJ_EQ_U(call("sched_get_priority_min", 1, 0, 0, 0), 1);
    uint32_t pol = nj_malloc(4), param = nj_malloc(4);
    NJ_EQ_U(call("pthread_getschedparam", call("pthread_self", 0, 0, 0, 0), pol, param, 0), 0);
    NJ_EQ_U(nj_r32(pol), 0);
    NJ_EQ_U(call("pthread_setname_np", call("pthread_self", 0, 0, 0, 0), hle_guest_strdup("x"), 0, 0), 0);
}

NJ_TEST(tm_layout) {
    setup();
    setenv("TZ", "UTC", 1);
    uint32_t t = nj_malloc(4);
    nj_w32(t, 1422489600u);
    uint32_t tm = call("localtime", t, 0, 0, 0);
    NJ_CHECK(tm != 0);
    NJ_EQ_U(nj_r32(tm + 20), 115); /* tm_year */
    NJ_EQ_U(nj_r32(tm + 16), 0);   /* tm_mon */
    NJ_EQ_U(nj_r32(tm + 12), 29);  /* tm_mday */
    NJ_EQ_U(nj_r32(tm + 24), 4);   /* Thursday */
}

NJ_TEST(fixed_clock) {
    setup();
    nj_time_set_fixed(true, 1000000000ull * 1000000ull, 16667);
    uint32_t tv = nj_malloc(8);
    call("gettimeofday", tv, 0, 0, 0);
    uint64_t a = (uint64_t)nj_r32(tv) * 1000000 + nj_r32(tv + 4);
    call("gettimeofday", tv, 0, 0, 0);
    uint64_t b = (uint64_t)nj_r32(tv) * 1000000 + nj_r32(tv + 4);
    NJ_EQ_U(b - a, 16667);
    nj_time_set_fixed(false, 0, 0);
    NJ_CHECK(call("time", 0, 0, 0, 0) > 1700000000u);
}
