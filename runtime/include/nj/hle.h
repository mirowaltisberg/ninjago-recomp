/* HLE registry: host implementations of guest imports, thunks, data imports and function overrides. */
#ifndef NJ_HLE_H
#define NJ_HLE_H

#include <stdint.h>
#include <string.h>

#include "nj/guest.h"
#include "nj/loader.h"

typedef void (*NjHleFn)(NjCtx *c);

/* Register (or replace) the host implementation for an import/thunk name. */
void hle_register(const char *name, NjHleFn fn);
/* Stable guest address of the thunk for name (created on first use). Slot i at NJ_THUNK_BASE + 8*i
 * holds "svc #i; bx lr". A thunk without a registered fn is fatal when called. */
uint32_t hle_thunk(const char *name);
/* fn for a thunk address (NULL if not a thunk or not implemented); name_out receives the name. */
NjHleFn hle_fn_at(uint32_t thunk_ga, const char **name_out);
/* Thunk index -> name (NULL if out of range). */
const char *hle_thunk_name(uint32_t index);
/* Call the function behind a thunk address; nj_fatal("unimplemented import: <name>") if none. */
void hle_dispatch(NjCtx *c, uint32_t thunk_ga);
static inline int hle_is_thunk(uint32_t ga) { return ga - NJ_THUNK_BASE < NJ_THUNK_SIZE; }

/* Data imports: name -> guest address written into GLOB_DAT slots. */
void hle_register_data(const char *name, uint32_t ga);
uint32_t hle_data_addr(const char *name);
/* FUNC slots <- hle_thunk(name); DATA slots <- registered address (fatal if unregistered). */
void hle_bind_imports(const NjImage *img);

/* Replace a guest function (by exported symbol name) with a host implementation. */
void hle_register_override(const char *symbol, NjHleFn fn);
/* Resolve registered overrides against the image (fatal if a symbol is missing). */
void hle_resolve_overrides(const NjImage *img);
/* Override for a guest function address (Thumb bit ignored), NULL if none. */
NjHleFn hle_override_at(uint32_t guest_fn_addr);

/* Bump allocation in the HLE data region (never freed). Fatal when exhausted. */
uint32_t hle_data_alloc(uint32_t size, uint32_t align);
/* Host view of a guest C string (NULL for 0). */
static inline const char *hle_cstr(uint32_t ga) { return ga ? (const char *)nj_host(ga) : NULL; }
/* Copy a host string into the guest heap. */
uint32_t hle_guest_strdup(const char *s);

/* Argument access, AAPCS softfp: words 0-3 in r0-r3, then [sp + 4*(word-4)]. */
static inline uint32_t hle_arg(const NjCtx *c, int word) {
    return word < 4 ? c->r[word] : nj_r32(c->r[13] + 4u * (uint32_t)(word - 4));
}
/* 64-bit argument starting at an even word index. */
static inline uint64_t hle_arg64(const NjCtx *c, int word) {
    return (uint64_t)hle_arg(c, word) | ((uint64_t)hle_arg(c, word + 1) << 32);
}
static inline float hle_arg_f(const NjCtx *c, int word) { uint32_t b = hle_arg(c, word); float f; memcpy(&f, &b, 4); return f; }
static inline double hle_arg_d(const NjCtx *c, int word) { uint64_t b = hle_arg64(c, word); double d; memcpy(&d, &b, 8); return d; }
static inline void hle_ret(NjCtx *c, uint32_t v) { c->r[0] = v; }
static inline void hle_ret64(NjCtx *c, uint64_t v) { c->r[0] = (uint32_t)v; c->r[1] = (uint32_t)(v >> 32); }
static inline void hle_ret_f(NjCtx *c, float f) { uint32_t b; memcpy(&b, &f, 4); c->r[0] = b; }
static inline void hle_ret_d(NjCtx *c, double d) { uint64_t b; memcpy(&b, &d, 8); hle_ret64(c, b); }

#endif
