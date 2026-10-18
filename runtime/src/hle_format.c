/* printf/scanf family over guest varargs, strftime, and Android logging. */
#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "nj/format.h"
#include "nj/hle.h"

#define A(i) hle_arg(c, i)

/* ---- argument cursors ---- */

NjVaCursor nj_va_from_args(NjCtx *c, int first_word) { return (NjVaCursor){c, first_word, 0, 0}; }
NjVaCursor nj_va_from_list(uint32_t va_list_ga) { return (NjVaCursor){NULL, 0, va_list_ga, 1}; }

uint32_t nj_va_u32(NjVaCursor *va) {
    if (va->from_va) { uint32_t v = nj_r32(va->va_ptr); va->va_ptr += 4; return v; }
    return hle_arg(va->c, va->word++);
}

uint64_t nj_va_u64(NjVaCursor *va) {
    if (va->from_va) {
        va->va_ptr = (va->va_ptr + 7) & ~7u;
        uint64_t v = nj_r64(va->va_ptr);
        va->va_ptr += 8;
        return v;
    }
    va->word = (va->word + 1) & ~1; /* even register pair or 8-aligned stack slot */
    uint64_t v = hle_arg64(va->c, va->word);
    va->word += 2;
    return v;
}

double nj_va_f64(NjVaCursor *va) {
    uint64_t b = nj_va_u64(va);
    double d;
    memcpy(&d, &b, 8);
    return d;
}

/* ---- growable host string ---- */

typedef struct { char *p; size_t len, cap; } Sb;

static void sb_put(Sb *sb, const char *s, size_t n) {
    if (sb->len + n + 1 > sb->cap) {
        size_t nc = sb->cap ? sb->cap * 2 : 128;
        while (nc < sb->len + n + 1) nc *= 2;
        sb->p = realloc(sb->p, nc);
        sb->cap = nc;
    }
    memcpy(sb->p + sb->len, s, n);
    sb->len += n;
    sb->p[sb->len] = 0;
}

static void sb_printf(Sb *sb, const char *spec, ...) __attribute__((format(printf, 2, 3)));
static void sb_printf(Sb *sb, const char *spec, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, spec);
    int n = vsnprintf(tmp, sizeof tmp, spec, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof tmp) { sb_put(sb, tmp, (size_t)n); return; }
    char *big = malloc((size_t)n + 1);
    va_start(ap, spec);
    vsnprintf(big, (size_t)n + 1, spec, ap);
    va_end(ap);
    sb_put(sb, big, (size_t)n);
    free(big);
}

enum { LEN_NONE, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_LD, LEN_Z };

static void format_into(Sb *sb, const char *fmt, NjVaCursor *va) {
    while (*fmt) {
        if (*fmt != '%') {
            const char *e = strchr(fmt, '%');
            size_t n = e ? (size_t)(e - fmt) : strlen(fmt);
            sb_put(sb, fmt, n);
            fmt += n;
            continue;
        }
        fmt++;
        if (*fmt == '%') { sb_put(sb, "%", 1); fmt++; continue; }
        char flags[8];
        int nf = 0;
        while (strchr("-+ #0'", *fmt) && *fmt && nf < 6) flags[nf++] = *fmt++;
        flags[nf] = 0;
        int width = -1, prec = -1;
        if (*fmt == '*') {
            width = (int32_t)nj_va_u32(va);
            fmt++;
            if (width < 0) { width = -width; if (nf < 7) { flags[nf++] = '-'; flags[nf] = 0; } }
        } else if (isdigit((unsigned char)*fmt)) {
            width = 0;
            while (isdigit((unsigned char)*fmt)) width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = (int32_t)nj_va_u32(va); fmt++; if (prec < 0) prec = -1; }
            else while (isdigit((unsigned char)*fmt)) prec = prec * 10 + (*fmt++ - '0');
        }
        int len = LEN_NONE;
        if (fmt[0] == 'h' && fmt[1] == 'h') { len = LEN_HH; fmt += 2; }
        else if (*fmt == 'h') { len = LEN_H; fmt++; }
        else if (fmt[0] == 'l' && fmt[1] == 'l') { len = LEN_LL; fmt += 2; }
        else if (*fmt == 'l') { len = LEN_L; fmt++; }
        else if (*fmt == 'q' || *fmt == 'j') { len = LEN_LL; fmt++; }
        else if (*fmt == 'L') { len = LEN_LD; fmt++; }
        else if (*fmt == 'z' || *fmt == 't') { len = LEN_Z; fmt++; }
        char conv = *fmt;
        if (!conv) break;
        fmt++;

        char spec[48];
        int sp = snprintf(spec, sizeof spec, "%%%s", flags);
        if (width >= 0) sp += snprintf(spec + sp, sizeof spec - (size_t)sp, "%d", width);
        if (prec >= 0) sp += snprintf(spec + sp, sizeof spec - (size_t)sp, ".%d", prec);

        switch (conv) {
        case 'd': case 'i': {
            long long v;
            if (len == LEN_LL) v = (long long)nj_va_u64(va);
            else {
                int32_t w = (int32_t)nj_va_u32(va);
                v = len == LEN_HH ? (signed char)w : len == LEN_H ? (short)w : w;
            }
            snprintf(spec + sp, sizeof spec - (size_t)sp, "lld");
            sb_printf(sb, spec, v);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            unsigned long long v;
            if (len == LEN_LL) v = nj_va_u64(va);
            else {
                uint32_t w = nj_va_u32(va);
                v = len == LEN_HH ? (unsigned char)w : len == LEN_H ? (unsigned short)w : w;
            }
            snprintf(spec + sp, sizeof spec - (size_t)sp, "ll%c", conv);
            sb_printf(sb, spec, v);
            break;
        }
        case 'c':
            snprintf(spec + sp, sizeof spec - (size_t)sp, "c");
            sb_printf(sb, spec, (int)(unsigned char)nj_va_u32(va));
            break;
        case 's': {
            uint32_t ga = nj_va_u32(va);
            const char *s = ga ? hle_cstr(ga) : "(null)";
            snprintf(spec + sp, sizeof spec - (size_t)sp, "s");
            sb_printf(sb, spec, s);
            break;
        }
        case 'p': {
            char hex[16];
            snprintf(hex, sizeof hex, "0x%x", nj_va_u32(va));
            snprintf(spec, sizeof spec, "%%%s", strchr(flags, '-') ? "-" : "");
            if (width >= 0) snprintf(spec + strlen(spec), sizeof spec - strlen(spec), "%d", width);
            strcat(spec, "s");
            sb_printf(sb, spec, hex);
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
            snprintf(spec + sp, sizeof spec - (size_t)sp, "%c", conv);
            sb_printf(sb, spec, nj_va_f64(va));
            break;
        case 'n': {
            uint32_t ga = nj_va_u32(va);
            if (len == LEN_LL) nj_w64(ga, sb->len);
            else if (len == LEN_HH) nj_w8(ga, (uint8_t)sb->len);
            else if (len == LEN_H) nj_w16(ga, (uint16_t)sb->len);
            else nj_w32(ga, (uint32_t)sb->len);
            break;
        }
        default: /* unknown conversion: print it verbatim, like bionic */
            sb_put(sb, "%", 1);
            sb_put(sb, &conv, 1);
            break;
        }
    }
}

int nj_format(char *out, size_t cap, const char *fmt, NjVaCursor *va) {
    Sb sb = {0};
    sb_put(&sb, "", 0);
    format_into(&sb, fmt, va);
    if (cap) {
        size_t n = sb.len < cap - 1 ? sb.len : cap - 1;
        memcpy(out, sb.p, n);
        out[n] = 0;
    }
    int len = (int)sb.len;
    free(sb.p);
    return len;
}

char *nj_format_alloc(const char *fmt, NjVaCursor *va) {
    Sb sb = {0};
    sb_put(&sb, "", 0);
    format_into(&sb, fmt, va);
    return sb.p;
}

/* ---- scanning ---- */

static void store_int(uint32_t ga, int len, unsigned long long v) {
    if (len == LEN_HH) nj_w8(ga, (uint8_t)v);
    else if (len == LEN_H) nj_w16(ga, (uint16_t)v);
    else if (len == LEN_LL) nj_w64(ga, v);
    else nj_w32(ga, (uint32_t)v);
}

int nj_scan(const char *input, const char *fmt, NjVaCursor *va) {
    const char *s = input;
    int assigned = 0, converted_any = 0;
    while (*fmt) {
        if (isspace((unsigned char)*fmt)) {
            while (isspace((unsigned char)*fmt)) fmt++;
            while (isspace((unsigned char)*s)) s++;
            continue;
        }
        if (*fmt != '%' || fmt[1] == '%') {
            if (*fmt == '%') { fmt++; while (isspace((unsigned char)*s)) s++; }
            if (*s != *fmt) break;
            s++;
            fmt++;
            continue;
        }
        fmt++;
        int suppress = 0, width = 0, len = LEN_NONE;
        if (*fmt == '*') { suppress = 1; fmt++; }
        while (isdigit((unsigned char)*fmt)) width = width * 10 + (*fmt++ - '0');
        if (fmt[0] == 'h' && fmt[1] == 'h') { len = LEN_HH; fmt += 2; }
        else if (*fmt == 'h') { len = LEN_H; fmt++; }
        else if (fmt[0] == 'l' && fmt[1] == 'l') { len = LEN_LL; fmt += 2; }
        else if (*fmt == 'l') { len = LEN_L; fmt++; }
        else if (*fmt == 'L' || *fmt == 'q' || *fmt == 'j') { len = (*fmt == 'L') ? LEN_LD : LEN_LL; fmt++; }
        else if (*fmt == 'z' || *fmt == 't') { len = LEN_Z; fmt++; }
        char conv = *fmt++;
        if (!conv) break;
        if (conv == 'n') {
            if (!suppress) store_int(nj_va_u32(va), len, (unsigned long long)(s - input));
            continue;
        }
        if (conv != 'c' && conv != '[') while (isspace((unsigned char)*s)) s++;
        if (!*s) return converted_any || assigned ? assigned : -1;
        char tmp[512];
        size_t maxw = width > 0 ? (size_t)width : sizeof tmp - 1;
        if (maxw > sizeof tmp - 1) maxw = sizeof tmp - 1;
        if (strchr("diuxXo", conv)) {
            size_t n = 0;
            while (s[n] && !isspace((unsigned char)s[n]) && n < maxw) { tmp[n] = s[n]; n++; }
            tmp[n] = 0;
            int base = conv == 'd' || conv == 'u' ? 10 : conv == 'i' ? 0 : conv == 'o' ? 8 : 16;
            char *end;
            unsigned long long v = (conv == 'd' || conv == 'i') ? (unsigned long long)strtoll(tmp, &end, base)
                                                                : strtoull(tmp, &end, base);
            if (end == tmp) break;
            s += end - tmp;
            if (!suppress) { store_int(nj_va_u32(va), len, v); assigned++; }
        } else if (strchr("feEgGaA", conv)) {
            size_t n = 0;
            while (s[n] && !isspace((unsigned char)s[n]) && n < maxw) { tmp[n] = s[n]; n++; }
            tmp[n] = 0;
            char *end;
            double d = strtod(tmp, &end);
            if (end == tmp) break;
            s += end - tmp;
            if (!suppress) {
                uint32_t ga = nj_va_u32(va);
                if (len == LEN_L || len == LEN_LD) { uint64_t b; memcpy(&b, &d, 8); nj_w64(ga, b); }
                else { float f = (float)d; uint32_t b; memcpy(&b, &f, 4); nj_w32(ga, b); }
                assigned++;
            }
        } else if (conv == 's') {
            size_t n = 0;
            while (s[n] && !isspace((unsigned char)s[n]) && n < maxw) n++;
            if (!suppress) {
                uint32_t ga = nj_va_u32(va);
                memcpy(nj_host(ga), s, n);
                nj_w8(ga + (uint32_t)n, 0);
                assigned++;
            }
            s += n;
        } else if (conv == 'c') {
            size_t n = width > 0 ? (size_t)width : 1, i = 0;
            while (i < n && s[i]) i++;
            if (i < n) break;
            if (!suppress) { memcpy(nj_host(nj_va_u32(va)), s, n); assigned++; }
            s += n;
        } else if (conv == '[') {
            int neg = 0;
            if (*fmt == '^') { neg = 1; fmt++; }
            unsigned char set[256] = {0};
            if (*fmt == ']') { set[']'] = 1; fmt++; }
            while (*fmt && *fmt != ']') {
                if (fmt[1] == '-' && fmt[2] && fmt[2] != ']') {
                    for (int ch = (unsigned char)fmt[0]; ch <= (unsigned char)fmt[2]; ch++) set[ch] = 1;
                    fmt += 3;
                } else set[(unsigned char)*fmt++] = 1;
            }
            if (*fmt == ']') fmt++;
            size_t n = 0;
            while (s[n] && n < maxw && (set[(unsigned char)s[n]] != 0) != neg) n++;
            if (n == 0) break;
            if (!suppress) {
                uint32_t ga = nj_va_u32(va);
                memcpy(nj_host(ga), s, n);
                nj_w8(ga + (uint32_t)n, 0);
                assigned++;
            }
            s += n;
        } else {
            break;
        }
        converted_any = 1;
    }
    return assigned;
}

/* ---- HLE entry points ---- */

static void out_guest(NjCtx *c, uint32_t buf, uint32_t cap, const char *fmt, NjVaCursor *va) {
    int n = nj_format(cap ? nj_host(buf) : NULL, cap, fmt, va);
    hle_ret(c, (uint32_t)n);
}

static void hle_snprintf(NjCtx *c) { NjVaCursor va = nj_va_from_args(c, 3); out_guest(c, A(0), A(1), hle_cstr(A(2)), &va); }
static void hle_vsnprintf(NjCtx *c) { NjVaCursor va = nj_va_from_list(A(3)); out_guest(c, A(0), A(1), hle_cstr(A(2)), &va); }
static void hle_sprintf(NjCtx *c) { NjVaCursor va = nj_va_from_args(c, 2); out_guest(c, A(0), 0x7FFFFFFFu, hle_cstr(A(1)), &va); }
static void hle_vsprintf(NjCtx *c) { NjVaCursor va = nj_va_from_list(A(2)); out_guest(c, A(0), 0x7FFFFFFFu, hle_cstr(A(1)), &va); }

static void hle_sscanf(NjCtx *c) {
    NjVaCursor va = nj_va_from_args(c, 2);
    hle_ret(c, (uint32_t)nj_scan(hle_cstr(A(0)), hle_cstr(A(1)), &va));
}

static void hle_strftime(NjCtx *c) {
    uint32_t buf = A(0), max = A(1), tmg = A(3);
    struct tm tm = {0};
    tm.tm_sec = (int)nj_r32(tmg + 0);
    tm.tm_min = (int)nj_r32(tmg + 4);
    tm.tm_hour = (int)nj_r32(tmg + 8);
    tm.tm_mday = (int)nj_r32(tmg + 12);
    tm.tm_mon = (int)nj_r32(tmg + 16);
    tm.tm_year = (int)nj_r32(tmg + 20);
    tm.tm_wday = (int)nj_r32(tmg + 24);
    tm.tm_yday = (int)nj_r32(tmg + 28);
    tm.tm_isdst = (int)nj_r32(tmg + 32);
    tm.tm_gmtoff = (int32_t)nj_r32(tmg + 36);
    tm.tm_zone = nj_r32(tmg + 40) ? (char *)hle_cstr(nj_r32(tmg + 40)) : "UTC";
    size_t n = max ? strftime(nj_host(buf), max, hle_cstr(A(2)), &tm) : 0;
    hle_ret(c, (uint32_t)n);
}

static FILE *log_file;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

void nj_log_set_file(const char *path) {
    pthread_mutex_lock(&log_lock);
    if (log_file) fclose(log_file);
    log_file = path ? fopen(path, "a") : NULL;
    pthread_mutex_unlock(&log_lock);
}

static void log_line(int prio, const char *tag, const char *msg) {
    static const char letters[] = "??VDIWEFS";
    char p = prio >= 0 && prio < 9 ? letters[prio] : '?';
    size_t n = strlen(msg);
    const char *nl = (n && msg[n - 1] == '\n') ? "" : "\n";
    pthread_mutex_lock(&log_lock);
    fprintf(stderr, "%c [%s] %s%s", p, tag ? tag : "", msg, nl);
    if (log_file) { fprintf(log_file, "%c [%s] %s%s", p, tag ? tag : "", msg, nl); fflush(log_file); }
    pthread_mutex_unlock(&log_lock);
}

static void log_formatted(NjCtx *c, NjVaCursor *va) {
    char *msg = nj_format_alloc(hle_cstr(A(2)), va);
    log_line((int)A(0), hle_cstr(A(1)), msg);
    free(msg);
    hle_ret(c, 1);
}

static void hle_log_print(NjCtx *c) { NjVaCursor va = nj_va_from_args(c, 3); log_formatted(c, &va); }
static void hle_log_vprint(NjCtx *c) { NjVaCursor va = nj_va_from_list(A(3)); log_formatted(c, &va); }
static void hle_log_write(NjCtx *c) { log_line((int)A(0), hle_cstr(A(1)), hle_cstr(A(2))); hle_ret(c, 1); }

void hle_format_register(void) {
    hle_register("snprintf", hle_snprintf);
    hle_register("vsnprintf", hle_vsnprintf);
    hle_register("sprintf", hle_sprintf);
    hle_register("vsprintf", hle_vsprintf);
    hle_register("sscanf", hle_sscanf);
    hle_register("strftime", hle_strftime);
    hle_register("__android_log_print", hle_log_print);
    hle_register("__android_log_vprint", hle_log_vprint);
    hle_register("__android_log_write", hle_log_write);
}
