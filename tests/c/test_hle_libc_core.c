#include <pthread.h>
#include <unistd.h>

#include "nj_test.h"

#include "nj/backend.h"
#include "nj/guest.h"
#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/hle_libc.h"

#define PAGE 0x00F00000u
#define FN_CMP_INT (PAGE + 0x00) /* ldr r0,[r0]; ldr r1,[r1]; sub r0,r0,r1; bx lr */

static NjImage img;
static int have_img;

static void setup(void) {
    static int done;
    if (done) return;
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_mem_map_fixed_regions(), 0);
    NJ_EQ_U(nj_heap_init(), 0);
    NJ_EQ_U(nj_mem_map(PAGE, 0x4000, NJ_PROT_R | NJ_PROT_W | NJ_PROT_X), 0);
    const uint32_t cmp[] = {0xE5900000, 0xE5911000, 0xE0400001, 0xE12FFF1E};
    for (int i = 0; i < 4; i++) nj_w32(FN_CMP_INT + 4u * i, cmp[i]);
    if (access(NJ_GUEST_SO_PATH, R_OK) == 0 && nj_image_load(NJ_GUEST_SO_PATH, NJ_IMAGE_BASE, &img) == 0) have_img = 1;
    hle_libc_core_register(have_img ? &img : NULL);
    NJ_EQ_U(nj_backend_init(), 0);
    done = 1;
}

/* Call an HLE import by name with up to 4 word args; returns r0 (r1 in *hi if given). */
static uint32_t call4(const char *name, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t *hi) {
    NjCtx *c = nj_ctx();
    c->r[0] = a0; c->r[1] = a1; c->r[2] = a2; c->r[3] = a3;
    hle_dispatch(c, hle_thunk(name));
    if (hi) *hi = c->r[1];
    return c->r[0];
}

static uint32_t gstr(const char *s) { return hle_guest_strdup(s); }

NJ_TEST(ctype_pointer_var) {
    setup();
    uint32_t var = hle_data_addr("_ctype_");
    NJ_CHECK(var != 0);
    uint32_t tbl = nj_r32(var); /* _ctype_ is a pointer variable */
    NJ_EQ_U(nj_r8(tbl + 0), 0);                  /* EOF */
    NJ_CHECK(nj_r8(tbl + 1 + 'A') & 0x01);       /* _U */
    NJ_CHECK(nj_r8(tbl + 1 + 'A') & 0x40);       /* _X */
    NJ_CHECK(nj_r8(tbl + 1 + 'z') & 0x02);       /* _L */
    NJ_CHECK(nj_r8(tbl + 1 + '7') & 0x04);       /* _N */
    NJ_CHECK(!(nj_r8(tbl + 1 + '7') & 0x40));    /* digits are _N only */
    NJ_EQ_U(nj_r8(tbl + 1 + ' '), 0x08 | 0x80);  /* _S|_B */
    NJ_EQ_U(nj_r8(tbl + 1 + '\t'), 0x20 | 0x08); /* _C|_S, not printable */
    NJ_CHECK(nj_r8(tbl + 1 + '!') & 0x10);       /* _P */
    NJ_EQ_U(nj_r8(tbl + 1 + 200), 0);
}

NJ_TEST(tolower_toupper_tab) {
    setup();
    uint32_t lo = nj_r32(hle_data_addr("_tolower_tab_"));
    uint32_t up = nj_r32(hle_data_addr("_toupper_tab_"));
    NJ_EQ_U((int16_t)nj_r16(lo + 2 * (1 + 'Q')), 'q');
    NJ_EQ_U((int16_t)nj_r16(lo + 2 * (1 + 'q')), 'q');
    NJ_EQ_U((int16_t)nj_r16(up + 2 * (1 + 'q')), 'Q');
    NJ_EQ_U(nj_r16(lo + 0), 0xFFFFu); /* EOF entry is -1 */
}

NJ_TEST(stack_chk_guard_var) {
    setup();
    uint32_t g = hle_data_addr("__stack_chk_guard");
    NJ_CHECK(g != 0);
    NJ_CHECK(nj_r32(g) != 0);
}

NJ_TEST(strtod_double_ret) {
    setup();
    uint32_t end = nj_malloc(4);
    uint32_t s = gstr("2.5e3xyz"), hi;
    uint32_t lo = call4("strtod", s, end, 0, 0, &hi);
    uint64_t bits = (uint64_t)lo | (uint64_t)hi << 32;
    double d;
    memcpy(&d, &bits, 8);
    NJ_CHECK(d == 2500.0);
    NJ_EQ_U(nj_r32(end), s + 5);
}

NJ_TEST(strtol_strtoul_32bit) {
    setup();
    NJ_EQ_U(call4("strtol", gstr("99999999999"), 0, 10, 0, NULL), 0x7fffffffu); /* LONG_MAX on ARM32 */
    NJ_EQ_U(nj_r32(hle_errno_ga()), NJ_ERANGE);
    NJ_EQ_U(call4("strtol", gstr("-0x10"), 0, 16, 0, NULL), (uint32_t)-16);
    NJ_EQ_U(call4("strtoul", gstr("ffffffff"), 0, 16, 0, NULL), 0xffffffffu);
    NJ_EQ_U(call4("atoi", gstr("  -42abc"), 0, 0, 0, NULL), (uint32_t)-42);
}

NJ_TEST(string_pointers_are_guest) {
    setup();
    uint32_t s = gstr("hello world");
    NJ_EQ_U(call4("strchr", s, 'o', 0, 0, NULL), s + 4);
    NJ_EQ_U(call4("strrchr", s, 'o', 0, 0, NULL), s + 7);
    NJ_EQ_U(call4("strstr", s, gstr("wor"), 0, 0, NULL), s + 6);
    NJ_EQ_U(call4("strchr", s, 'z', 0, 0, NULL), 0);
    uint32_t d = nj_malloc(32);
    NJ_EQ_U(call4("memcpy", d, s, 12, 0, NULL), d);
    NJ_EQ_STR(hle_cstr(d), "hello world");
    NJ_EQ_U(call4("strlen", d, 0, 0, 0, NULL), 11);
    NJ_EQ_U(call4("strcasecmp", gstr("ABC"), gstr("abc"), 0, 0, NULL), 0);
}

NJ_TEST(qsort_guest_cmp) {
    setup();
    int vals[5] = {5, -3, 9, 0, 2};
    uint32_t base = nj_malloc(sizeof vals);
    memcpy(nj_host(base), vals, sizeof vals);
    call4("qsort", base, 5, 4, FN_CMP_INT, NULL);
    int want[5] = {-3, 0, 2, 5, 9};
    for (int i = 0; i < 5; i++) NJ_EQ_U(nj_r32(base + 4u * i), (uint32_t)want[i]);
}

NJ_TEST(bsearch_stack_arg) {
    setup();
    int vals[5] = {-3, 0, 2, 5, 9};
    uint32_t base = nj_malloc(sizeof vals);
    memcpy(nj_host(base), vals, sizeof vals);
    uint32_t key = nj_malloc(4);
    nj_w32(key, 5);
    NjCtx *c = nj_ctx();
    uint32_t sp = c->r[13];
    c->r[13] = sp - 8;
    nj_w32(c->r[13], FN_CMP_INT); /* 5th argument */
    c->r[0] = key; c->r[1] = base; c->r[2] = 5; c->r[3] = 4;
    hle_dispatch(c, hle_thunk("bsearch"));
    c->r[13] = sp;
    NJ_EQ_U(c->r[0], base + 12);
}

static uint32_t thread_errno_ga[2];

static void *errno_thread(void *arg) {
    thread_errno_ga[(intptr_t)arg] = hle_errno_ga();
    return NULL;
}

NJ_TEST(errno_per_thread) {
    setup();
    pthread_t a, b;
    pthread_create(&a, NULL, errno_thread, (void *)0);
    pthread_create(&b, NULL, errno_thread, (void *)1);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    NJ_CHECK(thread_errno_ga[0] && thread_errno_ga[1]);
    NJ_CHECK(thread_errno_ga[0] != thread_errno_ga[1]);
    NJ_EQ_U(call4("__errno", 0, 0, 0, 0, NULL), hle_errno_ga());
}

NJ_TEST(strtok_sequence) {
    setup();
    uint32_t s = gstr("a,b,,c"), delim = gstr(",");
    NJ_EQ_STR(hle_cstr(call4("strtok", s, delim, 0, 0, NULL)), "a");
    NJ_EQ_STR(hle_cstr(call4("strtok", 0, delim, 0, 0, NULL)), "b");
    NJ_EQ_STR(hle_cstr(call4("strtok", 0, delim, 0, 0, NULL)), "c");
    NJ_EQ_U(call4("strtok", 0, delim, 0, 0, NULL), 0);
}

NJ_TEST(abort_and_assert_fatal) {
    setup();
    NJ_EXPECT_FATAL(call4("abort", 0, 0, 0, 0, NULL), "abort");
    NJ_EXPECT_FATAL(call4("__assert2", gstr("x.c"), 12, gstr("fn"), gstr("p != 0"), NULL), "p != 0");
}

NJ_TEST(unwind_find_exidx) {
    setup();
    if (!have_img) NJ_SKIP("guest library not present");
    uint32_t count = nj_malloc(4);
    NJ_EQ_U(call4("__gnu_Unwind_Find_exidx", img.base + 0x200000, count, 0, 0, NULL), img.exidx_ga);
    NJ_EQ_U(nj_r32(count), img.exidx_count);
}

NJ_TEST(lrand48_range_and_determinism) {
    setup();
    for (int i = 0; i < 100; i++) NJ_CHECK(call4("lrand48", 0, 0, 0, 0, NULL) < 0x80000000u);
}
