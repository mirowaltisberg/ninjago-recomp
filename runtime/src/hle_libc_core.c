/* bionic libc core subset: memory, strings, number parsing, sorting, ctype tables, errno, process. */
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "nj/backend.h"
#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/hle_libc.h"

#define A(i) hle_arg(c, i)
#define HS(ga) ((char *)nj_host(ga))

static const NjImage *image;

/* ---- errno ---- */

static _Thread_local uint32_t errno_ga;

uint32_t hle_errno_ga(void) {
    if (!errno_ga) errno_ga = nj_calloc(1, 4);
    return errno_ga;
}

void hle_set_errno(int linux_errno) { nj_w32(hle_errno_ga(), (uint32_t)linux_errno); }

int nj_errno_from_host(int e) {
    switch (e) {
    case EAGAIN: return NJ_EAGAIN;
    case EDEADLK: return NJ_EDEADLK;
    case ENOSYS: return NJ_ENOSYS;
    case ENOTEMPTY: return NJ_ENOTEMPTY;
    case ETIMEDOUT: return NJ_ETIMEDOUT;
    default: return e; /* 1..34 share their numbers between Darwin and Linux */
    }
}

static void hle___errno(NjCtx *c) { hle_ret(c, hle_errno_ga()); }

/* ---- memory ---- */

static void hle_malloc(NjCtx *c) { hle_ret(c, nj_malloc(A(0))); }
static void hle_free(NjCtx *c) { nj_free(A(0)); }
static void hle_calloc(NjCtx *c) { hle_ret(c, nj_calloc(A(0), A(1))); }
static void hle_realloc(NjCtx *c) { hle_ret(c, nj_realloc(A(0), A(1))); }
static void hle_memcmp(NjCtx *c) { hle_ret(c, A(2) ? (uint32_t)memcmp(HS(A(0)), HS(A(1)), A(2)) : 0); }
static void hle_memcpy(NjCtx *c) { if (A(2)) memcpy(HS(A(0)), HS(A(1)), A(2)); hle_ret(c, A(0)); }
static void hle_memmove(NjCtx *c) { if (A(2)) memmove(HS(A(0)), HS(A(1)), A(2)); hle_ret(c, A(0)); }
static void hle_memset(NjCtx *c) { if (A(2)) memset(HS(A(0)), (int)A(1), A(2)); hle_ret(c, A(0)); }

/* ---- strings ---- */

static uint32_t g(const char *p) { return p ? nj_guest(p) : 0; }

static void hle_strlen(NjCtx *c) { hle_ret(c, (uint32_t)strlen(HS(A(0)))); }
static void hle_strcmp(NjCtx *c) { hle_ret(c, (uint32_t)strcmp(HS(A(0)), HS(A(1)))); }
static void hle_strncmp(NjCtx *c) { hle_ret(c, (uint32_t)strncmp(HS(A(0)), HS(A(1)), A(2))); }
static void hle_strcasecmp(NjCtx *c) { hle_ret(c, (uint32_t)strcasecmp(HS(A(0)), HS(A(1)))); }
static void hle_strncasecmp(NjCtx *c) { hle_ret(c, (uint32_t)strncasecmp(HS(A(0)), HS(A(1)), A(2))); }
static void hle_strcpy(NjCtx *c) { strcpy(HS(A(0)), HS(A(1))); hle_ret(c, A(0)); }
static void hle_strncpy(NjCtx *c) { strncpy(HS(A(0)), HS(A(1)), A(2)); hle_ret(c, A(0)); }
static void hle_strcat(NjCtx *c) { strcat(HS(A(0)), HS(A(1))); hle_ret(c, A(0)); }
static void hle_strncat(NjCtx *c) { strncat(HS(A(0)), HS(A(1)), A(2)); hle_ret(c, A(0)); }
static void hle_strchr(NjCtx *c) { hle_ret(c, g(strchr(HS(A(0)), (char)A(1)))); }
static void hle_strrchr(NjCtx *c) { hle_ret(c, g(strrchr(HS(A(0)), (char)A(1)))); }
static void hle_strstr(NjCtx *c) { hle_ret(c, g(strstr(HS(A(0)), HS(A(1))))); }

static _Thread_local char *strtok_save;

static void hle_strtok(NjCtx *c) {
    char *s = A(0) ? HS(A(0)) : NULL;
    if (!s && !strtok_save) { hle_ret(c, 0); return; }
    hle_ret(c, g(strtok_r(s, HS(A(1)), &strtok_save)));
}

static const char *linux_strerror(int e) {
    switch (e) {
    case 0: return "Success";
    case NJ_EPERM: return "Operation not permitted";
    case NJ_ENOENT: return "No such file or directory";
    case NJ_EINTR: return "Interrupted system call";
    case NJ_EIO: return "I/O error";
    case NJ_EBADF: return "Bad file descriptor";
    case NJ_EAGAIN: return "Try again";
    case NJ_ENOMEM: return "Out of memory";
    case NJ_EACCES: return "Permission denied";
    case NJ_EEXIST: return "File exists";
    case NJ_ENOTDIR: return "Not a directory";
    case NJ_EISDIR: return "Is a directory";
    case NJ_EINVAL: return "Invalid argument";
    case NJ_ENOSPC: return "No space left on device";
    case NJ_ERANGE: return "Math result not representable";
    default: return NULL;
    }
}

static _Thread_local uint32_t strerror_buf;

static void hle_strerror(NjCtx *c) {
    if (!strerror_buf) strerror_buf = nj_malloc(64);
    const char *m = linux_strerror((int)A(0));
    if (m) snprintf(HS(strerror_buf), 64, "%s", m);
    else snprintf(HS(strerror_buf), 64, "Unknown error %d", (int)A(0));
    hle_ret(c, strerror_buf);
}

/* ---- number parsing (long is 32-bit on the guest) ---- */

static void set_end(uint32_t endp_ga, const char *end) { if (endp_ga) nj_w32(endp_ga, nj_guest(end)); }

static void hle_atoi(NjCtx *c) { hle_ret(c, (uint32_t)(int32_t)strtoll(HS(A(0)), NULL, 10)); }

static void hle_strtol(NjCtx *c) {
    char *end;
    errno = 0;
    long long v = strtoll(HS(A(0)), &end, (int)A(2));
    int range = errno == ERANGE;
    if (v > INT32_MAX) { v = INT32_MAX; range = 1; }
    if (v < INT32_MIN) { v = INT32_MIN; range = 1; }
    if (range) hle_set_errno(NJ_ERANGE);
    set_end(A(1), end);
    hle_ret(c, (uint32_t)(int32_t)v);
}

static void hle_strtoul(NjCtx *c) {
    const char *s = HS(A(0));
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;
    int neg = *p == '-';
    char *end;
    errno = 0;
    unsigned long long v = strtoull(s, &end, (int)A(2));
    unsigned long long mag = neg ? (0ull - v) : v;
    uint32_t r;
    if (errno == ERANGE || mag > 0xFFFFFFFFull) {
        hle_set_errno(NJ_ERANGE);
        r = 0xFFFFFFFFu;
    } else {
        r = neg ? (uint32_t)(0u - (uint32_t)mag) : (uint32_t)mag;
    }
    set_end(A(1), end);
    hle_ret(c, r);
}

static void hle_strtod(NjCtx *c) {
    char *end;
    errno = 0;
    double d = strtod(HS(A(0)), &end);
    if (errno == ERANGE) hle_set_errno(NJ_ERANGE);
    set_end(A(1), end);
    hle_ret_d(c, d);
}

/* ---- sorting and searching with guest comparators ---- */

static int guest_cmp(NjCtx *c, uint32_t cmp, uint32_t a, uint32_t b) {
    uint32_t args[2] = {a, b};
    return (int32_t)nj_call(c, cmp, 2, args);
}

static void swap_bytes(uint32_t a, uint32_t b, uint32_t size) {
    uint8_t *x = nj_host(a), *y = nj_host(b);
    for (uint32_t i = 0; i < size; i++) { uint8_t t = x[i]; x[i] = y[i]; y[i] = t; }
}

/* In-place quicksort on guest memory: every comparison sees guest addresses of array elements. */
static void sort_range(NjCtx *c, uint32_t base, uint32_t lo, uint32_t hi, uint32_t size, uint32_t cmp) {
    while (hi - lo > 1) {
        if (hi - lo <= 8) { /* insertion sort */
            for (uint32_t i = lo + 1; i < hi; i++)
                for (uint32_t j = i; j > lo && guest_cmp(c, cmp, base + (j - 1) * size, base + j * size) > 0; j--)
                    swap_bytes(base + (j - 1) * size, base + j * size, size);
            return;
        }
        uint32_t mid = lo + (hi - lo) / 2, last = hi - 1;
        /* median of three into position last */
        if (guest_cmp(c, cmp, base + mid * size, base + lo * size) < 0) swap_bytes(base + mid * size, base + lo * size, size);
        if (guest_cmp(c, cmp, base + last * size, base + lo * size) < 0) swap_bytes(base + last * size, base + lo * size, size);
        if (guest_cmp(c, cmp, base + mid * size, base + last * size) < 0) swap_bytes(base + mid * size, base + last * size, size);
        uint32_t store = lo;
        for (uint32_t i = lo; i < last; i++)
            if (guest_cmp(c, cmp, base + i * size, base + last * size) < 0) {
                if (i != store) swap_bytes(base + i * size, base + store * size, size);
                store++;
            }
        if (store != last) swap_bytes(base + store * size, base + last * size, size);
        /* recurse into the smaller half, loop on the larger */
        if (store - lo < hi - store - 1) { sort_range(c, base, lo, store, size, cmp); lo = store + 1; }
        else { sort_range(c, base, store + 1, hi, size, cmp); hi = store; }
    }
}

static void hle_qsort(NjCtx *c) {
    uint32_t base = A(0), n = A(1), size = A(2), cmp = A(3);
    if (n > 1 && size) sort_range(c, base, 0, n, size, cmp);
}

static void hle_bsearch(NjCtx *c) {
    uint32_t key = A(0), base = A(1), n = A(2), size = A(3), cmp = A(4);
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int r = guest_cmp(c, cmp, key, base + mid * size);
        if (r == 0) { hle_ret(c, base + mid * size); return; }
        if (r < 0) hi = mid;
        else lo = mid + 1;
    }
    hle_ret(c, 0);
}

/* ---- random: drand48 family LCG with the standard default seed (as bionic) ---- */

static uint64_t rand48_state = 0x1234ABCD330Eull;
static pthread_mutex_t rand_lock = PTHREAD_MUTEX_INITIALIZER;

static void hle_lrand48(NjCtx *c) {
    pthread_mutex_lock(&rand_lock);
    rand48_state = (0x5DEECE66Dull * rand48_state + 0xB) & 0xFFFFFFFFFFFFull;
    uint32_t r = (uint32_t)(rand48_state >> 17);
    pthread_mutex_unlock(&rand_lock);
    hle_ret(c, r);
}

/* ---- process ---- */

static void hle_abort(NjCtx *c) { (void)c; nj_fatal("guest called abort()"); }

static void hle_raise(NjCtx *c) {
    int sig = (int)A(0);
    if (sig == 6 || sig == 11 || sig == 7 || sig == 4) nj_fatal("guest raise(%d)", sig);
    fprintf(stderr, "[hle] guest raise(%d) ignored\n", sig);
    hle_ret(c, 0);
}

static void hle___assert2(NjCtx *c) {
    nj_fatal("guest assertion failed: %s:%d (%s): %s", hle_cstr(A(0)), (int)A(1), hle_cstr(A(2)), hle_cstr(A(3)));
}

static void hle___stack_chk_fail(NjCtx *c) { (void)c; nj_fatal("guest __stack_chk_fail: stack corruption detected"); }
static void hle_atexit_ok(NjCtx *c) { hle_ret(c, 0); } /* the app never exits cleanly; handlers are not run */
static void hle_noop(NjCtx *c) { (void)c; }

static void hle___gnu_Unwind_Find_exidx(NjCtx *c) {
    if (!image) { hle_ret(c, 0); return; }
    if (A(1)) nj_w32(A(1), image->exidx_count);
    hle_ret(c, image->exidx_ga);
}

/* ---- ctype tables (C locale, bionic/OpenBSD layout) ---- */

enum { CT_U = 0x01, CT_L = 0x02, CT_N = 0x04, CT_S = 0x08, CT_P = 0x10, CT_C = 0x20, CT_X = 0x40, CT_B = 0x80 };

static uint32_t pointer_var_to(uint32_t table) {
    uint32_t var = hle_data_alloc(4, 4);
    nj_w32(var, table);
    return var;
}

static void build_ctype(void) {
    uint32_t ct = hle_data_alloc(257, 4);
    uint32_t lo = hle_data_alloc(257 * 2, 4), up = hle_data_alloc(257 * 2, 4);
    nj_w8(ct, 0);
    nj_w16(lo, 0xFFFF);
    nj_w16(up, 0xFFFF);
    for (int ch = 0; ch < 256; ch++) {
        uint8_t f = 0;
        if (ch < 128) {
            if (isupper(ch)) f |= CT_U;
            if (islower(ch)) f |= CT_L;
            if (isdigit(ch)) f |= CT_N;
            if (isspace(ch)) f |= CT_S;
            if (ispunct(ch)) f |= CT_P;
            if (iscntrl(ch)) f |= CT_C;
            if (isxdigit(ch) && !isdigit(ch)) f |= CT_X;
            if (ch == ' ') f |= CT_B;
        }
        nj_w8(ct + 1 + (uint32_t)ch, f);
        nj_w16(lo + 2 * (1 + (uint32_t)ch), (uint16_t)((ch >= 'A' && ch <= 'Z') ? ch + 32 : ch));
        nj_w16(up + 2 * (1 + (uint32_t)ch), (uint16_t)((ch >= 'a' && ch <= 'z') ? ch - 32 : ch));
    }
    hle_register_data("_ctype_", pointer_var_to(ct));
    hle_register_data("_tolower_tab_", pointer_var_to(lo));
    hle_register_data("_toupper_tab_", pointer_var_to(up));
}

void hle_libc_core_register(const NjImage *img) {
    image = img;
    if (!hle_data_addr("_ctype_")) {
        build_ctype();
        uint32_t guard = hle_data_alloc(4, 4);
        nj_w32(guard, 0x4E4A4743u); /* any non-zero canary */
        hle_register_data("__stack_chk_guard", guard);
    }
    static const struct { const char *name; NjHleFn fn; } fns[] = {
        {"malloc", hle_malloc}, {"free", hle_free}, {"calloc", hle_calloc}, {"realloc", hle_realloc},
        {"memcmp", hle_memcmp}, {"memcpy", hle_memcpy}, {"memmove", hle_memmove}, {"memset", hle_memset},
        {"strlen", hle_strlen}, {"strcmp", hle_strcmp}, {"strncmp", hle_strncmp}, {"strcasecmp", hle_strcasecmp},
        {"strncasecmp", hle_strncasecmp}, {"strcpy", hle_strcpy}, {"strncpy", hle_strncpy}, {"strcat", hle_strcat},
        {"strncat", hle_strncat}, {"strchr", hle_strchr}, {"strrchr", hle_strrchr}, {"strstr", hle_strstr},
        {"strtok", hle_strtok}, {"strerror", hle_strerror}, {"atoi", hle_atoi}, {"atol", hle_atoi},
        {"strtol", hle_strtol}, {"strtoul", hle_strtoul}, {"strtod", hle_strtod}, {"qsort", hle_qsort},
        {"bsearch", hle_bsearch}, {"lrand48", hle_lrand48}, {"abort", hle_abort}, {"raise", hle_raise},
        {"__assert2", hle___assert2}, {"__stack_chk_fail", hle___stack_chk_fail}, {"__cxa_atexit", hle_atexit_ok},
        {"__aeabi_atexit", hle_atexit_ok}, {"__cxa_finalize", hle_noop}, {"__errno", hle___errno},
        {"__gnu_Unwind_Find_exidx", hle___gnu_Unwind_Find_exidx},
    };
    for (size_t i = 0; i < sizeof fns / sizeof fns[0]; i++) hle_register(fns[i].name, fns[i].fn);
}
