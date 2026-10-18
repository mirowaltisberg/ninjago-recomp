/* bionic pthreads on host pthreads. Guest sync objects (4-byte mutex/cond/sem) map to host objects
 * created lazily and keyed by guest address, so static initialisers (0 / 0x4000) just work. */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "nj/backend.h"
#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/hle_libc.h"
#include "nj/hle_pthread.h"

#define A(i) hle_arg(c, i)

/* ---- guest-address keyed object map ---- */

enum { OBJ_MUTEX = 1, OBJ_COND, OBJ_SEM, OBJ_THREAD };

typedef struct Sem { pthread_mutex_t m; pthread_cond_t cv; uint32_t count; } Sem;

typedef struct Thread {
    uint32_t start, arg, ret;
    int done, detached, joined;
    pthread_mutex_t m;
    pthread_cond_t cv;
} Thread;

typedef struct Obj { uint32_t ga; int kind; void *host; struct Obj *next; } Obj;

#define BUCKETS 4096
static Obj *buckets[BUCKETS];
static pthread_mutex_t map_lock = PTHREAD_MUTEX_INITIALIZER;

static Obj *find(uint32_t ga, int kind) {
    for (Obj *o = buckets[(ga >> 2) % BUCKETS]; o; o = o->next) if (o->ga == ga && o->kind == kind) return o;
    return NULL;
}

static void *insert(uint32_t ga, int kind, void *host) {
    Obj *o = malloc(sizeof *o);
    *o = (Obj){ga, kind, host, buckets[(ga >> 2) % BUCKETS]};
    buckets[(ga >> 2) % BUCKETS] = o;
    return host;
}

static void *take(uint32_t ga, int kind) {
    Obj **pp = &buckets[(ga >> 2) % BUCKETS];
    for (; *pp; pp = &(*pp)->next)
        if ((*pp)->ga == ga && (*pp)->kind == kind) {
            Obj *o = *pp;
            void *h = o->host;
            *pp = o->next;
            free(o);
            return h;
        }
    return NULL;
}

static int err(int host_rc) { return host_rc ? nj_errno_from_host(host_rc) : 0; }

/* ---- mutexes ---- */

static pthread_mutex_t *new_mutex(int type) {
    pthread_mutexattr_t at;
    pthread_mutexattr_init(&at);
    pthread_mutexattr_settype(&at, type == 1 ? PTHREAD_MUTEX_RECURSIVE
                                             : type == 2 ? PTHREAD_MUTEX_ERRORCHECK : PTHREAD_MUTEX_NORMAL);
    pthread_mutex_t *m = malloc(sizeof *m);
    pthread_mutex_init(m, &at);
    pthread_mutexattr_destroy(&at);
    return m;
}

static int type_from_guest_value(uint32_t v) { return (v & 0xC000) == 0x4000 ? 1 : (v & 0xC000) == 0x8000 ? 2 : 0; }

static pthread_mutex_t *get_mutex(uint32_t ga) {
    pthread_mutex_lock(&map_lock);
    Obj *o = find(ga, OBJ_MUTEX);
    pthread_mutex_t *m = o ? o->host : insert(ga, OBJ_MUTEX, new_mutex(type_from_guest_value(nj_r32(ga))));
    pthread_mutex_unlock(&map_lock);
    return m;
}

static void hle_mutexattr_init(NjCtx *c) { nj_w32(A(0), 0); hle_ret(c, 0); }
static void hle_mutexattr_settype(NjCtx *c) {
    if (A(1) > 2) { hle_ret(c, NJ_EINVAL); return; }
    nj_w32(A(0), A(1));
    hle_ret(c, 0);
}

static void hle_mutex_init(NjCtx *c) {
    uint32_t ga = A(0);
    int type = A(1) ? (int)nj_r32(A(1)) : 0;
    nj_w32(ga, type == 1 ? 0x4000u : type == 2 ? 0x8000u : 0);
    pthread_mutex_lock(&map_lock);
    pthread_mutex_t *old = take(ga, OBJ_MUTEX);
    if (old) { pthread_mutex_destroy(old); free(old); }
    insert(ga, OBJ_MUTEX, new_mutex(type));
    pthread_mutex_unlock(&map_lock);
    hle_ret(c, 0);
}

static void hle_mutex_destroy(NjCtx *c) {
    pthread_mutex_lock(&map_lock);
    pthread_mutex_t *m = take(A(0), OBJ_MUTEX);
    pthread_mutex_unlock(&map_lock);
    if (m) { pthread_mutex_destroy(m); free(m); }
    hle_ret(c, 0);
}

static void hle_mutex_lock(NjCtx *c) { hle_ret(c, (uint32_t)err(pthread_mutex_lock(get_mutex(A(0))))); }
static void hle_mutex_trylock(NjCtx *c) { hle_ret(c, (uint32_t)err(pthread_mutex_trylock(get_mutex(A(0))))); }
static void hle_mutex_unlock(NjCtx *c) { hle_ret(c, (uint32_t)err(pthread_mutex_unlock(get_mutex(A(0))))); }

/* ---- condition variables ---- */

static pthread_cond_t *get_cond(uint32_t ga) {
    pthread_mutex_lock(&map_lock);
    Obj *o = find(ga, OBJ_COND);
    pthread_cond_t *cv = o ? o->host : NULL;
    if (!cv) {
        cv = malloc(sizeof *cv);
        pthread_cond_init(cv, NULL);
        insert(ga, OBJ_COND, cv);
    }
    pthread_mutex_unlock(&map_lock);
    return cv;
}

static void hle_cond_init(NjCtx *c) { nj_w32(A(0), 0); get_cond(A(0)); hle_ret(c, 0); }
static void hle_cond_destroy(NjCtx *c) {
    pthread_mutex_lock(&map_lock);
    pthread_cond_t *cv = take(A(0), OBJ_COND);
    pthread_mutex_unlock(&map_lock);
    if (cv) { pthread_cond_destroy(cv); free(cv); }
    hle_ret(c, 0);
}
static void hle_cond_wait(NjCtx *c) { hle_ret(c, (uint32_t)err(pthread_cond_wait(get_cond(A(0)), get_mutex(A(1))))); }
static void hle_cond_signal(NjCtx *c) { hle_ret(c, (uint32_t)err(pthread_cond_signal(get_cond(A(0))))); }
static void hle_cond_broadcast(NjCtx *c) { hle_ret(c, (uint32_t)err(pthread_cond_broadcast(get_cond(A(0))))); }

/* ---- semaphores (Darwin has no unnamed POSIX semaphores) ---- */

static Sem *get_sem(uint32_t ga) {
    pthread_mutex_lock(&map_lock);
    Obj *o = find(ga, OBJ_SEM);
    pthread_mutex_unlock(&map_lock);
    if (!o) nj_fatal("guest used an uninitialised semaphore at 0x%08x", ga);
    return o->host;
}

static void hle_sem_init(NjCtx *c) {
    Sem *s = malloc(sizeof *s);
    pthread_mutex_init(&s->m, NULL);
    pthread_cond_init(&s->cv, NULL);
    s->count = A(2);
    pthread_mutex_lock(&map_lock);
    Sem *old = take(A(0), OBJ_SEM);
    insert(A(0), OBJ_SEM, s);
    pthread_mutex_unlock(&map_lock);
    if (old) free(old);
    hle_ret(c, 0);
}

static void hle_sem_destroy(NjCtx *c) {
    pthread_mutex_lock(&map_lock);
    Sem *s = take(A(0), OBJ_SEM);
    pthread_mutex_unlock(&map_lock);
    if (s) { pthread_mutex_destroy(&s->m); pthread_cond_destroy(&s->cv); free(s); }
    hle_ret(c, 0);
}

static void hle_sem_wait(NjCtx *c) {
    Sem *s = get_sem(A(0));
    pthread_mutex_lock(&s->m);
    while (s->count == 0) pthread_cond_wait(&s->cv, &s->m);
    s->count--;
    pthread_mutex_unlock(&s->m);
    hle_ret(c, 0);
}

static void hle_sem_post(NjCtx *c) {
    Sem *s = get_sem(A(0));
    pthread_mutex_lock(&s->m);
    s->count++;
    pthread_cond_signal(&s->cv);
    pthread_mutex_unlock(&s->m);
    hle_ret(c, 0);
}

/* ---- threads ---- */

static _Thread_local uint32_t self_ga;

static Thread *new_thread_rec(uint32_t *ga_out) {
    Thread *t = calloc(1, sizeof *t);
    pthread_mutex_init(&t->m, NULL);
    pthread_cond_init(&t->cv, NULL);
    uint32_t ga = nj_calloc(1, 16); /* unique guest pthread_t value */
    pthread_mutex_lock(&map_lock);
    insert(ga, OBJ_THREAD, t);
    pthread_mutex_unlock(&map_lock);
    *ga_out = ga;
    return t;
}

static Thread *thread_rec(uint32_t ga) {
    pthread_mutex_lock(&map_lock);
    Obj *o = find(ga, OBJ_THREAD);
    pthread_mutex_unlock(&map_lock);
    return o ? o->host : NULL;
}

static void free_thread_rec(uint32_t ga) {
    pthread_mutex_lock(&map_lock);
    Thread *t = take(ga, OBJ_THREAD);
    pthread_mutex_unlock(&map_lock);
    if (!t) return;
    pthread_mutex_destroy(&t->m);
    pthread_cond_destroy(&t->cv);
    free(t);
    nj_free(ga);
}

/* Guest-visible start routine of every pthread_create'd thread: records identity, then runs the
 * real start function. Reached through a thunk so both CPU backends use the same path. */
static void hle_thread_entry(NjCtx *c) {
    uint32_t ga = A(0);
    Thread *t = thread_rec(ga);
    self_ga = ga;
    uint32_t arg = t->arg;
    hle_ret(c, nj_call(c, t->start, 1, &arg));
}

static void thread_exit_cb(uint32_t ret, void *ud) {
    uint32_t ga = (uint32_t)(uintptr_t)ud;
    Thread *t = thread_rec(ga);
    if (!t) return;
    pthread_mutex_lock(&t->m);
    t->ret = ret;
    t->done = 1;
    int detached = t->detached;
    pthread_cond_broadcast(&t->cv);
    pthread_mutex_unlock(&t->m);
    if (detached) free_thread_rec(ga);
}

static void hle_pthread_create(NjCtx *c) {
    uint32_t out = A(0), attr = A(1);
    uint32_t ga;
    Thread *t = new_thread_rec(&ga);
    t->start = A(2);
    t->arg = A(3);
    t->detached = attr ? (int)(nj_r32(attr) & 1) : 0;
    nj_w32(out, ga);
    if (nj_spawn_thread(hle_thunk("__nj_thread_entry"), ga, thread_exit_cb, (void *)(uintptr_t)ga)) {
        free_thread_rec(ga);
        hle_ret(c, NJ_EAGAIN);
        return;
    }
    hle_ret(c, 0);
}

static void hle_pthread_join(NjCtx *c) {
    uint32_t ga = A(0), retp = A(1);
    Thread *t = thread_rec(ga);
    if (!t || t->detached) { hle_ret(c, 3 /* ESRCH */); return; }
    pthread_mutex_lock(&t->m);
    while (!t->done) pthread_cond_wait(&t->cv, &t->m);
    uint32_t ret = t->ret;
    pthread_mutex_unlock(&t->m);
    if (retp) nj_w32(retp, ret);
    free_thread_rec(ga);
    hle_ret(c, 0);
}

static void hle_pthread_self(NjCtx *c) {
    if (!self_ga) new_thread_rec(&self_ga);
    hle_ret(c, self_ga);
}

static void hle_pthread_equal(NjCtx *c) { hle_ret(c, A(0) == A(1)); }

static void hle_attr_init(NjCtx *c) {
    uint32_t a = A(0);
    memset(nj_host(a), 0, NJ_BIONIC_PTHREAD_ATTR_SIZE);
    nj_w32(a + 8, NJ_STACK_SIZE); /* stack_size */
    nj_w32(a + 12, 4096);         /* guard_size */
    hle_ret(c, 0);
}
static void hle_attr_destroy(NjCtx *c) { hle_ret(c, 0); }
static void hle_attr_setdetachstate(NjCtx *c) {
    uint32_t flags = nj_r32(A(0));
    nj_w32(A(0), A(1) == 1 ? (flags | 1) : (flags & ~1u));
    hle_ret(c, 0);
}

static void hle_getschedparam(NjCtx *c) {
    if (A(1)) nj_w32(A(1), 0); /* SCHED_OTHER */
    if (A(2)) nj_w32(A(2), 0);
    hle_ret(c, 0);
}
static void hle_ok(NjCtx *c) { hle_ret(c, 0); }

static void hle_sched_max(NjCtx *c) { hle_ret(c, (A(0) == 1 || A(0) == 2) ? 99 : 0); }
static void hle_sched_min(NjCtx *c) { hle_ret(c, (A(0) == 1 || A(0) == 2) ? 1 : 0); }

/* ---- thread-specific data ---- */

#define MAX_KEYS 128
static uint8_t key_used[MAX_KEYS];
static _Thread_local uint32_t key_vals[MAX_KEYS];

static void hle_key_create(NjCtx *c) {
    pthread_mutex_lock(&map_lock);
    int k = 0;
    while (k < MAX_KEYS && key_used[k]) k++;
    if (k < MAX_KEYS) key_used[k] = 1;
    pthread_mutex_unlock(&map_lock);
    if (k == MAX_KEYS) { hle_ret(c, NJ_EAGAIN); return; }
    if (A(1)) fprintf(stderr, "[pthread] key %d has a destructor; destructors are not run\n", k);
    nj_w32(A(0), (uint32_t)k);
    hle_ret(c, 0);
}
static void hle_key_delete(NjCtx *c) {
    if (A(0) < MAX_KEYS) key_used[A(0)] = 0;
    hle_ret(c, 0);
}
static void hle_getspecific(NjCtx *c) { hle_ret(c, A(0) < MAX_KEYS ? key_vals[A(0)] : 0); }
static void hle_setspecific(NjCtx *c) {
    if (A(0) >= MAX_KEYS) { hle_ret(c, NJ_EINVAL); return; }
    key_vals[A(0)] = A(1);
    hle_ret(c, 0);
}

void hle_pthread_register(void) {
    static const struct { const char *name; NjHleFn fn; } fns[] = {
        {"pthread_attr_destroy", hle_attr_destroy}, {"pthread_attr_init", hle_attr_init},
        {"pthread_attr_setdetachstate", hle_attr_setdetachstate}, {"pthread_cond_broadcast", hle_cond_broadcast},
        {"pthread_cond_destroy", hle_cond_destroy}, {"pthread_cond_init", hle_cond_init},
        {"pthread_cond_signal", hle_cond_signal}, {"pthread_cond_wait", hle_cond_wait},
        {"pthread_create", hle_pthread_create}, {"pthread_equal", hle_pthread_equal},
        {"pthread_getschedparam", hle_getschedparam}, {"pthread_getspecific", hle_getspecific},
        {"pthread_join", hle_pthread_join}, {"pthread_key_create", hle_key_create},
        {"pthread_key_delete", hle_key_delete}, {"pthread_mutex_destroy", hle_mutex_destroy},
        {"pthread_mutex_init", hle_mutex_init}, {"pthread_mutex_lock", hle_mutex_lock},
        {"pthread_mutex_trylock", hle_mutex_trylock}, {"pthread_mutex_unlock", hle_mutex_unlock},
        {"pthread_mutexattr_init", hle_mutexattr_init}, {"pthread_mutexattr_settype", hle_mutexattr_settype},
        {"pthread_self", hle_pthread_self}, {"pthread_setname_np", hle_ok},
        {"pthread_setschedparam", hle_ok}, {"pthread_setspecific", hle_setspecific},
        {"sem_init", hle_sem_init}, {"sem_destroy", hle_sem_destroy}, {"sem_post", hle_sem_post},
        {"sem_wait", hle_sem_wait}, {"sched_get_priority_max", hle_sched_max},
        {"sched_get_priority_min", hle_sched_min}, {"__nj_thread_entry", hle_thread_entry},
    };
    for (size_t i = 0; i < sizeof fns / sizeof fns[0]; i++) hle_register(fns[i].name, fns[i].fn);
}
