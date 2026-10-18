/* Host replacements for the Thumb-mode runtime helpers that ARM code calls (STLport node allocator,
 * gabi++ static-initialisation guards, C++ exception plumbing), so both CPU backends behave identically
 * and the ARM-only recompiler never has to translate Thumb code. */
#include <pthread.h>

#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/hle_overrides.h"

/* std::__node_alloc::_M_allocate(size_t& n): rounds n up to the 8-byte node granularity. */
static void node_allocate(NjCtx *c) {
    uint32_t np = hle_arg(c, 0);
    uint32_t n = (nj_r32(np) + 7u) & ~7u;
    nj_w32(np, n);
    uint32_t p = nj_malloc(n ? n : 8);
    if (!p) nj_fatal("guest heap exhausted in __node_alloc::_M_allocate(%u)", n);
    hle_ret(c, p);
}

/* std::__node_alloc::_M_deallocate(void* p, size_t n) */
static void node_deallocate(NjCtx *c) { nj_free(hle_arg(c, 0)); }

static void exception_path(NjCtx *c) {
    (void)c;
    nj_fatal("guest reached C++ exception handling (__cxa_begin_cleanup/__cxa_type_match), which is not emulated");
}

/* gabi++ __cxa_guard_acquire/release (unnamed in the stripped library): the guard word holds bit 0
 * "initialised", 0x100 "initialisation pending" and 0x200 "a thread is waiting"; one global mutex and
 * condition variable, exactly like gabi++. */
static pthread_mutex_t guard_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t guard_cv = PTHREAD_COND_INITIALIZER;

static void guard_acquire(NjCtx *c) {
    uint32_t g = hle_arg(c, 0);
    pthread_mutex_lock(&guard_lock);
    for (;;) {
        uint32_t v = nj_r32(g);
        if (v & 1) { hle_ret(c, 0); break; }
        if (!(v & 0x100)) { nj_w32(g, 0x100); hle_ret(c, 1); break; }
        nj_w32(g, v | 0x200);
        pthread_cond_wait(&guard_cv, &guard_lock);
    }
    pthread_mutex_unlock(&guard_lock);
}

static void guard_release(NjCtx *c) {
    uint32_t g = hle_arg(c, 0);
    pthread_mutex_lock(&guard_lock);
    uint32_t v = nj_r32(g);
    nj_w32(g, 1);
    if (v & 0x200) pthread_cond_broadcast(&guard_cv);
    pthread_mutex_unlock(&guard_lock);
}

/* Two gabi++ static constructors compiled as Thumb (init_array entries 397 and 398), replayed exactly:
 * same import calls, same stores. Addresses are link addresses; the image is loaded at NJ_IMAGE_BASE. */
static void call_import(NjCtx *c, const char *name) { hle_dispatch(c, hle_thunk(name)); }
#define G(link) (NJ_IMAGE_BASE + (link))

static void gabi_ctor_397(NjCtx *c) {
    c->r[0] = G(0x549afc); /* thread-local key */
    c->r[1] = G(0x3ee901); /* its destructor (Thumb; never run: thread-exit destructors are not emulated) */
    call_import(c, "pthread_key_create");
    if (c->r[0] != 0) nj_fatal("gabi++ static constructor: pthread_key_create failed");
    uint32_t s = G(0x549ae4);
    nj_w32(s, 12);
    nj_w32(s + 8, 12 + 0xff0);
    nj_w32(s + 4, 0x155);
    c->r[0] = s + 12;
    c->r[1] = 0;
    call_import(c, "pthread_mutex_init");
    nj_w32(s + 0x14, 0);
    nj_w32(s + 0x10, 0);
    c->r[0] = G(0x549ae0);
    c->r[1] = G(0x3ee92d); /* atexit handler (Thumb; never run) */
    c->r[2] = nj_r32(G(0x43dc60));
    call_import(c, "__cxa_atexit");
}

static void gabi_ctor_398(NjCtx *c) {
    c->r[0] = G(0x549b0c);
    c->r[1] = 0;
    call_import(c, "pthread_mutex_init");
    c->r[0] = G(0x549b0c);
    c->r[1] = G(0x3efb6d); /* atexit handler (Thumb; never run) */
    c->r[2] = nj_r32(G(0x43dc60));
    call_import(c, "__cxa_atexit");
}

void hle_overrides_register(void) {
    static const uint8_t ctor397_code[] = {0x14, 0x48, 0x15, 0x49, 0x38, 0xb5, 0x78, 0x44};
    static const uint8_t ctor398_code[] = {0x10, 0xb5, 0x00, 0x21, 0x07, 0x4c, 0x7c, 0x44};
    hle_register_override_code(0x1729cc, ctor397_code, sizeof ctor397_code, "gabi++ static ctor 397", gabi_ctor_397);
    hle_register_override_code(0x172a3c, ctor398_code, sizeof ctor398_code, "gabi++ static ctor 398", gabi_ctor_398);
    static const uint8_t acquire_code[] = {0x70, 0xb5, 0x04, 0x46, 0x13, 0x4e, 0x7e, 0x44}; /* push {r4-r6,lr}; … */
    static const uint8_t release_code[] = {0x0a, 0x4b, 0x10, 0xb5, 0x7b, 0x44, 0x04, 0x46}; /* ldr r3,[pc,#40]; … */
    hle_register_override_code(0x3ef470, acquire_code, sizeof acquire_code, "__cxa_guard_acquire", guard_acquire);
    hle_register_override_code(0x3ef4d4, release_code, sizeof release_code, "__cxa_guard_release", guard_release);
    hle_register_override("_ZNSt12__node_alloc11_M_allocateERj", node_allocate);
    hle_register_override("_ZNSt12__node_alloc13_M_deallocateEPvj", node_deallocate);
    hle_register_override("__cxa_begin_cleanup", exception_path);
    hle_register_override("__cxa_type_match", exception_path);
}
