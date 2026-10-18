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

void hle_overrides_register(void) {
    static const uint8_t acquire_code[] = {0x70, 0xb5, 0x04, 0x46, 0x13, 0x4e, 0x7e, 0x44}; /* push {r4-r6,lr}; … */
    static const uint8_t release_code[] = {0x0a, 0x4b, 0x10, 0xb5, 0x7b, 0x44, 0x04, 0x46}; /* ldr r3,[pc,#40]; … */
    hle_register_override_code(0x3ef470, acquire_code, sizeof acquire_code, "__cxa_guard_acquire", guard_acquire);
    hle_register_override_code(0x3ef4d4, release_code, sizeof release_code, "__cxa_guard_release", guard_release);
    hle_register_override("_ZNSt12__node_alloc11_M_allocateERj", node_allocate);
    hle_register_override("_ZNSt12__node_alloc13_M_deallocateEPvj", node_deallocate);
    hle_register_override("__cxa_begin_cleanup", exception_path);
    hle_register_override("__cxa_type_match", exception_path);
}
