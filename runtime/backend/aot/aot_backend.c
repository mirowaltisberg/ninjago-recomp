/* AOT CPU backend: runs guest code recompiled to C (tools/recomp, gen/) on the host CPU.
 *
 * Translated functions take the NjCtx they run on and pass it down their call chain, so an HLE function
 * receives the live context of its caller. Each host thread owns one NjThread with a guest stack slot and
 * one NjCtx per nj_call nesting level: a call from host code into the guest runs on that level's context and
 * the caller's context is restored afterwards (only r0/r1 change). Guest call depth uses the host C stack. */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "nj/backend.h"
#include "nj/hle.h"
#include "recomp_rt.h"

#define MAX_DEPTH 16
#define HOST_STACK (64u << 20) /* translated guest recursion runs on the host stack */

void nj_aot_set_image(const NjImage *img);

typedef struct NjThread {
    uint32_t slot;
    uint32_t stack_top;
    int depth; /* active nj_call levels on this thread */
    NjCtx ctx[MAX_DEPTH];
} NjThread;

static _Thread_local NjThread *self;
static int inited;
static pthread_mutex_t slot_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t slot_used[NJ_STACK_COUNT];

void nj_backend_set_image(const NjImage *img) { nj_aot_set_image(img); }

static NjThread *thread_self(void) {
    if (self) return self;
    if (!inited) nj_fatal("nj_backend_init() must run before guest code");
    pthread_mutex_lock(&slot_lock);
    uint32_t slot = 0;
    while (slot < NJ_STACK_COUNT && slot_used[slot]) slot++;
    if (slot == NJ_STACK_COUNT) { pthread_mutex_unlock(&slot_lock); nj_fatal("out of guest thread stacks"); }
    slot_used[slot] = 1;
    pthread_mutex_unlock(&slot_lock);
    NjThread *t = calloc(1, sizeof *t);
    t->slot = slot;
    t->stack_top = NJ_STACK_BASE + (slot + 1) * NJ_STACK_SIZE - 16;
    for (int i = 0; i < MAX_DEPTH; i++) {
        t->ctx[i].r[13] = t->stack_top;
        t->ctx[i].thread = (struct NjThread *)t;
    }
    self = t;
    return t;
}

static void thread_release(void) {
    NjThread *t = self;
    if (!t) return;
    pthread_mutex_lock(&slot_lock);
    slot_used[t->slot] = 0;
    pthread_mutex_unlock(&slot_lock);
    free(t);
    self = NULL;
}

int nj_backend_init(void) {
    if (inited) return 0;
    if (!nj_mem || !nj_mem_is_mapped(NJ_HLEDATA_BASE, NJ_HLEDATA_SIZE) ||
        !nj_mem_is_mapped(NJ_STACK_BASE, NJ_STACK_SIZE * NJ_STACK_COUNT) ||
        !nj_mem_is_mapped(NJ_THUNK_BASE, NJ_THUNK_SIZE) || !nj_mem_is_mapped(NJ_HEAP_BASE, 4096))
        return -1;
    nj_mem_set_frozen(true);
    inited = 1;
    return 0;
}

NjCtx *nj_ctx(void) {
    NjThread *t = thread_self();
    return &t->ctx[t->depth > 0 ? t->depth - 1 : 0];
}

uint32_t nj_call(NjCtx *c, uint32_t fn, int argc, const uint32_t *argv) {
    NjThread *t = thread_self();
    int level = t->depth;
    if (level >= MAX_DEPTH) nj_fatal("guest call nesting deeper than %d", MAX_DEPTH);
    NjCtx *inner = &t->ctx[level];
    NjCtx saved = *c; /* inner is c itself for a top-level call from the host: restore it afterwards */
    *inner = saved;   /* flags and VFP state carry over */
    uint32_t sp = c->r[13];
    if (level > 0) sp -= 64; /* stay clear of the interrupted frame */
    sp &= ~7u;
    int nstack = argc > 4 ? argc - 4 : 0;
    sp = (sp - 4u * (uint32_t)nstack) & ~7u;
    for (int i = 4; i < argc; i++) nj_w32(sp + 4u * (uint32_t)(i - 4), argv[i]);
    for (int i = 0; i < 4; i++) inner->r[i] = i < argc ? argv[i] : 0;
    inner->r[13] = sp;
    inner->r[14] = NJ_RETURN_TRAP;
    inner->thread = (struct NjThread *)t;

    NjCtx *outer_fatal = nj_fatal_ctx;
    t->depth = level + 1;
    nj_fatal_ctx = inner;
    nj_aot_call_indirect(inner, fn);
    t->depth = level;
    nj_fatal_ctx = outer_fatal;
    uint32_t r0 = inner->r[0], r1 = inner->r[1];
    *c = saved;
    c->r[0] = r0;
    c->r[1] = r1;
    return r0;
}

typedef struct {
    uint32_t start, arg;
    void (*on_exit)(uint32_t, void *);
    void *ud;
} SpawnArgs;

static void *thread_main(void *p) {
    SpawnArgs a = *(SpawnArgs *)p;
    free(p);
    uint32_t ret = nj_call(nj_ctx(), a.start, 1, &a.arg);
    if (a.on_exit) a.on_exit(ret, a.ud);
    thread_release();
    return NULL;
}

int nj_spawn_thread(uint32_t start_fn, uint32_t arg, void (*on_exit)(uint32_t ret, void *ud), void *ud) {
    if (!inited) return -1;
    SpawnArgs *a = malloc(sizeof *a);
    *a = (SpawnArgs){start_fn, arg, on_exit, ud};
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, HOST_STACK);
    pthread_t th;
    int rc = pthread_create(&th, &attr, thread_main, a);
    pthread_attr_destroy(&attr);
    if (rc) { free(a); return -1; }
    return 0;
}
