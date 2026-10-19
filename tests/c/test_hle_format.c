#include <stdio.h>
#include <unistd.h>

#include "nj_test.h"

#include "nj/backend.h"
#include "nj/format.h"
#include "nj/guest.h"
#include "nj/heap.h"
#include "nj/hle.h"

static void setup(void) {
    static int done;
    if (done) return;
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_mem_map_fixed_regions(), 0);
    NJ_EQ_U(nj_heap_init(), 0);
    hle_format_register();
    NJ_EQ_U(nj_backend_init(), 0);
    done = 1;
}

static uint32_t gstr(const char *s) { return hle_guest_strdup(s); }
static uint64_t dbits(double d) { uint64_t b; memcpy(&b, &d, 8); return b; }

/* Prepare a call: words[] are the argument words (0-3 in registers, rest on an 8-aligned stack). */
static NjCtx *prep(const uint32_t *words, int n) {
    NjCtx *c = nj_ctx();
    static uint32_t base_sp;
    if (!base_sp) base_sp = c->r[13];
    uint32_t sp = (base_sp - 256) & ~7u;
    for (int i = 0; i < n; i++) {
        if (i < 4) c->r[i] = words[i];
        else nj_w32(sp + 4u * (uint32_t)(i - 4), words[i]);
    }
    c->r[13] = sp;
    return c;
}

static uint32_t call(const char *name, const uint32_t *w, int n) {
    NjCtx *c = prep(w, n);
    uint32_t sp = c->r[13];
    hle_dispatch(c, hle_thunk(name));
    c->r[13] = sp + 0; /* unchanged */
    return c->r[0];
}

NJ_TEST(fmt_basic) {
    setup();
    uint32_t buf = nj_malloc(64);
    uint32_t w[] = {buf, 64, gstr("%d|%5s|%-3x|%%"), 42, gstr("ab"), 255};
    NJ_EQ_U(call("snprintf", w, 6), 14);
    NJ_EQ_STR(hle_cstr(buf), "42|   ab|ff |%");
}

NJ_TEST(fmt_double_on_stack_aligned) {
    setup();
    uint32_t buf = nj_malloc(64);
    uint64_t d = dbits(1.5);
    /* buf=r0 n=r1 fmt=r2 7=r3, double in the 8-aligned stack slot at sp+0 */
    uint32_t w[] = {buf, 64, gstr("%d %f"), 7, (uint32_t)d, (uint32_t)(d >> 32)};
    call("snprintf", w, 6);
    NJ_EQ_STR(hle_cstr(buf), "7 1.500000");
}

NJ_TEST(fmt_double_skips_odd_register) {
    setup();
    uint32_t buf = nj_malloc(64);
    uint64_t d = dbits(0.25);
    /* snprintf(buf, n, "%g", 0.25): first vararg word 3 is odd, so the double goes to sp+0 */
    uint32_t w[] = {buf, 64, gstr("%g"), 0xDEADBEEF, (uint32_t)d, (uint32_t)(d >> 32)};
    call("snprintf", w, 6);
    NJ_EQ_STR(hle_cstr(buf), "0.25");
}

NJ_TEST(fmt_double_in_r2_r3) {
    setup();
    uint32_t buf = nj_malloc(64);
    uint64_t d = dbits(2.25);
    uint32_t w[] = {buf, gstr("%.2f"), (uint32_t)d, (uint32_t)(d >> 32)};
    call("sprintf", w, 4);
    NJ_EQ_STR(hle_cstr(buf), "2.25");
}

NJ_TEST(fmt_ll_and_lengths) {
    setup();
    uint32_t buf = nj_malloc(64);
    uint64_t v = (uint64_t)-5LL;
    uint32_t w[] = {buf, gstr("%d %lld %ld %zu %hhd"), 1, 0xAAAAAAAA, (uint32_t)v, (uint32_t)(v >> 32), 0xFFFFFFFF, 7, 0x1FF};
    call("sprintf", w, 9);
    NJ_EQ_STR(hle_cstr(buf), "1 -5 -1 7 -1");
}

NJ_TEST(fmt_va_list) {
    setup();
    uint32_t buf = nj_malloc(64);
    uint32_t ap = nj_memalign(8, 32);
    uint64_t d = dbits(0.25);
    nj_w32(ap, 3);
    nj_w32(ap + 4, 0xCCCCCCCC); /* padding before the 8-aligned double */
    nj_w64(ap + 8, d);
    uint32_t w[] = {buf, 64, gstr("%d %g"), ap};
    call("vsnprintf", w, 4);
    NJ_EQ_STR(hle_cstr(buf), "3 0.25");
}

NJ_TEST(fmt_strings_pointers_chars) {
    setup();
    uint32_t buf = nj_malloc(64);
    uint32_t w[] = {buf, gstr("[%s][%p][%c][%X][%*d][%.3s]"), 0, 0x1234, 'Z', 0xBEEF, 4, 7, gstr("abcdef")};
    call("sprintf", w, 9);
    NJ_EQ_STR(hle_cstr(buf), "[(null)][0x1234][Z][BEEF][   7][abc]");
}

NJ_TEST(fmt_truncation_returns_full_length) {
    setup();
    uint32_t buf = nj_malloc(8);
    uint32_t w[] = {buf, 4, gstr("abcdefgh")};
    NJ_EQ_U(call("snprintf", w, 3), 8);
    NJ_EQ_STR(hle_cstr(buf), "abc");
}

NJ_TEST(sscanf_mix) {
    setup();
    uint32_t pi = nj_malloc(4), pf = nj_malloc(4), ps = nj_malloc(16);
    uint32_t w[] = {gstr("12,3.5 hi"), gstr("%d,%f %s"), pi, pf, ps};
    NJ_EQ_U(call("sscanf", w, 5), 3);
    NJ_EQ_U(nj_r32(pi), 12);
    float f;
    uint32_t fb = nj_r32(pf);
    memcpy(&f, &fb, 4);
    NJ_CHECK(f == 3.5f);
    NJ_EQ_STR(hle_cstr(ps), "hi");
}

NJ_TEST(sscanf_hex_suppress_set) {
    setup();
    uint32_t pa = nj_malloc(4), pb = nj_malloc(16), pn = nj_malloc(4);
    uint32_t w[] = {gstr("ff skip abc-def"), gstr("%x %*s %[a-c]%n"), pa, pb, pn};
    NJ_EQ_U(call("sscanf", w, 5), 2);
    NJ_EQ_U(nj_r32(pa), 255);
    NJ_EQ_STR(hle_cstr(pb), "abc");
    NJ_EQ_U(nj_r32(pn), 11);
}

NJ_TEST(strftime_tm) {
    setup();
    uint32_t tm = nj_calloc(1, 44), buf = nj_malloc(32);
    nj_w32(tm + 20, 115); /* tm_year */
    nj_w32(tm + 16, 0);   /* tm_mon */
    nj_w32(tm + 12, 29);  /* tm_mday */
    nj_w32(tm + 8, 13);   /* tm_hour */
    uint32_t w[] = {buf, 32, gstr("%Y-%m-%d %H"), tm};
    NJ_EQ_U(call("strftime", w, 4), 13);
    NJ_EQ_STR(hle_cstr(buf), "2015-01-29 13");
}

NJ_TEST(android_log_to_file) {
    setup();
    char path[] = "/tmp/nj-logXXXXXX";
    int fd = mkstemp(path);
    close(fd);
    nj_log_set_file(path);
    uint32_t w[] = {4, gstr("Fusion"), gstr("value=%d"), 42};
    call("__android_log_print", w, 4);
    nj_log_set_file(NULL);
    FILE *f = fopen(path, "r");
    char line[128] = {0};
    fgets(line, sizeof line, f);
    fclose(f);
    unlink(path);
    NJ_CHECK(strstr(line, "[Fusion] value=42") != NULL);
}
