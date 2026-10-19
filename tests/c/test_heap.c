#include <pthread.h>

#include "nj_test.h"

#include "nj/guest.h"
#include "nj/heap.h"

static void setup(void) {
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_heap_init(), 0);
}

static int in_heap(uint32_t ga, uint32_t n) {
    return ga >= NJ_HEAP_BASE && (uint64_t)ga + n <= (uint64_t)NJ_HEAP_BASE + NJ_HEAP_SIZE;
}

NJ_TEST(heap_in_range) {
    setup();
    uint32_t a = nj_malloc(100);
    NJ_CHECK(a != 0);
    NJ_CHECK(in_heap(a, 100));
    NJ_EQ_U(a % 8, 0);
    nj_free(a);
}

NJ_TEST(heap_large_400mb) {
    setup();
    uint32_t n = 400u << 20;
    uint32_t a = nj_malloc(n);
    NJ_CHECK(a != 0);
    NJ_CHECK(in_heap(a, n));
    nj_w8(a + n - 1, 0x7f);
    NJ_EQ_U(nj_r8(a + n - 1), 0x7f);
    nj_free(a);
}

NJ_TEST(heap_realloc_preserves) {
    setup();
    uint32_t a = nj_malloc(16);
    for (uint32_t i = 0; i < 16; i++) nj_w8(a + i, (uint8_t)(i * 7));
    uint32_t b = nj_realloc(a, 1u << 20);
    NJ_CHECK(b != 0);
    for (uint32_t i = 0; i < 16; i++) NJ_EQ_U(nj_r8(b + i), (uint8_t)(i * 7));
    NJ_CHECK(nj_malloc_usable_size(b) >= (1u << 20));
    nj_free(b);
}

NJ_TEST(heap_calloc_zeroes) {
    setup();
    uint32_t a = nj_malloc(4096);
    memset(nj_host(a), 0xAB, 4096);
    nj_free(a);
    uint32_t b = nj_calloc(1024, 4);
    for (uint32_t i = 0; i < 4096; i++) NJ_EQ_U(nj_r8(b + i), 0);
    nj_free(b);
}

NJ_TEST(heap_memalign_4k) {
    setup();
    uint32_t a = nj_memalign(4096, 10000);
    NJ_CHECK(a != 0);
    NJ_EQ_U(a % 4096, 0);
    nj_free(a);
}

NJ_TEST(heap_free_null_ok) {
    setup();
    nj_free(0);
    uint32_t a = nj_realloc(0, 0); /* C allows NULL or a unique block; it must not crash */
    nj_free(a);
    uint32_t b = nj_realloc(0, 32); /* realloc(NULL, n) behaves like malloc */
    NJ_CHECK(b != 0);
    nj_free(b);
}

#define THREADS 4
#define CYCLES 10000

static uint32_t live[THREADS][8];

static void *churn(void *arg) {
    int t = (int)(intptr_t)arg;
    for (int i = 0; i < CYCLES; i++) {
        int slot = i % 8;
        if (live[t][slot]) nj_free(live[t][slot]);
        uint32_t n = 16 + (uint32_t)((i * 37 + t * 101) % 2000);
        uint32_t a = nj_malloc(n);
        if (!a) return (void *)1;
        memset(nj_host(a), t + 1, n); /* overlap with another thread's block would corrupt its tag */
        live[t][slot] = a;
    }
    return NULL;
}

NJ_TEST(heap_threads) {
    setup();
    pthread_t th[THREADS];
    for (int t = 0; t < THREADS; t++) pthread_create(&th[t], NULL, churn, (void *)(intptr_t)t);
    for (int t = 0; t < THREADS; t++) {
        void *rv;
        pthread_join(th[t], &rv);
        NJ_CHECK(rv == NULL);
    }
    for (int t = 0; t < THREADS; t++)
        for (int s = 0; s < 8; s++) {
            uint32_t a = live[t][s];
            NJ_EQ_U(nj_r8(a), t + 1);
            NJ_EQ_U(nj_r8(a + 15), t + 1);
            nj_free(a);
            live[t][s] = 0;
        }
}
