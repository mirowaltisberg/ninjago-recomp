/* printf/scanf over guest variadic arguments (AAPCS softfp, bionic ARM va_list = pointer). */
#ifndef NJ_FORMAT_H
#define NJ_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#include "nj/guest.h"

typedef struct {
    NjCtx *c;
    int word;        /* next argument word (from_va == 0) */
    uint32_t va_ptr; /* next stack address (from_va == 1) */
    int from_va;
} NjVaCursor;

/* Variadic call: the first variadic argument is argument word first_word. */
NjVaCursor nj_va_from_args(NjCtx *c, int first_word);
/* va_list argument (guest pointer to the next stacked argument). */
NjVaCursor nj_va_from_list(uint32_t va_list_ga);
uint32_t nj_va_u32(NjVaCursor *va);
uint64_t nj_va_u64(NjVaCursor *va); /* 8-byte aligned slot */
double nj_va_f64(NjVaCursor *va);

/* Format like bionic printf with guest arguments. Writes at most cap-1 chars + NUL to out (host buffer).
 * Returns the full length the result would have. */
int nj_format(char *out, size_t cap, const char *fmt, NjVaCursor *va);
/* Format into a freshly malloc'd host string (caller frees). */
char *nj_format_alloc(const char *fmt, NjVaCursor *va);
/* sscanf into guest pointers taken from va. Returns the number of assignments (or -1 on input failure). */
int nj_scan(const char *input, const char *fmt, NjVaCursor *va);

/* snprintf sprintf vsnprintf vsprintf sscanf strftime __android_log_print/vprint/write. */
void hle_format_register(void);
/* Also append guest log lines to this file (NULL disables). */
void nj_log_set_file(const char *path);

#endif
