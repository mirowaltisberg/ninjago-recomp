#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "nj/guest.h"

void (*nj_fatal_hook)(const char *msg);
_Thread_local NjCtx *nj_fatal_ctx;

static void dump_ctx(const NjCtx *c) {
    for (int i = 0; i < 16; i++)
        fprintf(stderr, "  r%-2d %08x%s", i, c->r[i], (i % 4 == 3) ? "\n" : "");
    fprintf(stderr, "  NZCV %u%u%u%u  fpscr %08x\n", c->n, c->z, c->c, c->v, c->fpscr);
}

_Noreturn void nj_fatal(const char *fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (nj_fatal_hook) nj_fatal_hook(msg); /* must not return */
    fprintf(stderr, "nj_fatal: %s\n", msg);
    if (nj_fatal_ctx) dump_ctx(nj_fatal_ctx);
    abort();
}
