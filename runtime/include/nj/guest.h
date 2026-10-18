/* Guest address space and CPU context shared by the HLE layer and both CPU backends. */
#ifndef NJ_GUEST_H
#define NJ_GUEST_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Guest memory map (32-bit guest addresses). The recompiler uses NJ_IMAGE_BASE via --base. */
#define NJ_IMAGE_BASE   0x00100000u
#define NJ_HLEDATA_BASE 0x01000000u
#define NJ_HLEDATA_SIZE 0x01000000u /* 16 MiB */
#define NJ_HEAP_BASE    0x02000000u
#define NJ_HEAP_SIZE    0xDE000000u /* up to NJ_STACK_BASE */
#define NJ_STACK_BASE   0xE0000000u
#define NJ_STACK_SIZE   0x00100000u /* 1 MiB per guest thread */
#define NJ_STACK_COUNT  256u
#define NJ_THUNK_BASE   0xF0000000u
#define NJ_THUNK_SIZE   0x00080000u /* 8 bytes per thunk -> 65536 thunks */
#define NJ_RETURN_TRAP  0xFFFF0000u

#define NJ_PROT_R 1
#define NJ_PROT_W 2
#define NJ_PROT_X 4

struct NjThread;

typedef struct NjCtx {
    uint32_t r[16];              /* r13 = sp, r14 = lr, r15 = pc */
    uint32_t n, z, c, v, q, ge;  /* flags, each 0/1; ge holds GE[3:0] */
    uint64_t d[32];              /* VFP: s[2k] = low 32 bits of d[k], s[2k+1] = high 32 bits */
    uint32_t fpscr;
    uint32_t excl_addr, excl_val, excl_valid;
    uint32_t tpidruro;
    struct NjThread *thread;
} NjCtx;

/* Host pointer of guest address 0; valid after nj_mem_init(). */
extern uint8_t *nj_mem;

static inline void *nj_host(uint32_t ga) { return nj_mem + ga; }
static inline uint32_t nj_guest(const void *hp) { return (uint32_t)((const uint8_t *)hp - nj_mem); }

static inline uint8_t nj_r8(uint32_t ga) { return nj_mem[ga]; }
static inline uint16_t nj_r16(uint32_t ga) { uint16_t v; memcpy(&v, nj_mem + ga, 2); return v; }
static inline uint32_t nj_r32(uint32_t ga) { uint32_t v; memcpy(&v, nj_mem + ga, 4); return v; }
static inline uint64_t nj_r64(uint32_t ga) { uint64_t v; memcpy(&v, nj_mem + ga, 8); return v; }
static inline void nj_w8(uint32_t ga, uint8_t v) { nj_mem[ga] = v; }
static inline void nj_w16(uint32_t ga, uint16_t v) { memcpy(nj_mem + ga, &v, 2); }
static inline void nj_w32(uint32_t ga, uint32_t v) { memcpy(nj_mem + ga, &v, 4); }
static inline void nj_w64(uint32_t ga, uint64_t v) { memcpy(nj_mem + ga, &v, 8); }

static inline uint32_t nj_get_s(const NjCtx *c, int n) {
    uint64_t d = c->d[n >> 1];
    return (n & 1) ? (uint32_t)(d >> 32) : (uint32_t)d;
}
static inline void nj_set_s(NjCtx *c, int n, uint32_t bits) {
    uint64_t *d = &c->d[n >> 1];
    if (n & 1) *d = (*d & 0x00000000FFFFFFFFull) | ((uint64_t)bits << 32);
    else       *d = (*d & 0xFFFFFFFF00000000ull) | bits;
}
static inline uint64_t nj_get_d(const NjCtx *c, int n) { return c->d[n]; }
static inline void nj_set_d(NjCtx *c, int n, uint64_t bits) { c->d[n] = bits; }

/* Reserve the 4 GiB guest region (PROT_NONE). Idempotent. 0 on success. */
int nj_mem_init(void);
/* Make [ga, ga+size) accessible; prot is NJ_PROT_* (X is bookkeeping only). 0 on success. */
int nj_mem_map(uint32_t ga, uint32_t size, int prot);
/* Map the fixed regions every backend expects before start: HLE data, all stacks, thunks. */
int nj_mem_map_fixed_regions(void);
bool nj_mem_is_mapped(uint32_t ga, uint32_t size);
/* Copy the mapped intervals [start, end) (host-page rounded, sorted). Returns the count. */
int nj_mem_intervals(uint64_t *starts, uint64_t *ends, int max);
/* While frozen, nj_mem_map() is fatal: CPU backends snapshot the mapping at start. */
void nj_mem_set_frozen(bool frozen);

/* Print message (+ current guest context if known) and abort. */
_Noreturn void nj_fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* If set, called with the formatted message instead of aborting; must not return (longjmp). */
extern void (*nj_fatal_hook)(const char *msg);
/* Context printed by nj_fatal; backends set it per thread. */
extern _Thread_local NjCtx *nj_fatal_ctx;

#endif
