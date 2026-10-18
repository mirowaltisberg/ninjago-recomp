/* Host replacements for the few Thumb-mode runtime helpers linked into the guest library
 * (STLport node allocator, C++ exception plumbing), so both CPU backends behave identically. */
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

void hle_overrides_register(void) {
    hle_register_override("_ZNSt12__node_alloc11_M_allocateERj", node_allocate);
    hle_register_override("_ZNSt12__node_alloc13_M_deallocateEPvj", node_deallocate);
    hle_register_override("__cxa_begin_cleanup", exception_path);
    hle_register_override("__cxa_type_match", exception_path);
}
