/* Guest heap: allocations inside [NJ_HEAP_BASE, NJ_HEAP_BASE + NJ_HEAP_SIZE), returned as guest addresses. */
#ifndef NJ_HEAP_H
#define NJ_HEAP_H

#include <stdint.h>

/* Maps the whole heap range RW and creates the allocator. Idempotent. 0 on success. */
int nj_heap_init(void);

/* All functions are thread-safe and return 0 on failure. */
uint32_t nj_malloc(uint32_t n);
void nj_free(uint32_t ga);
uint32_t nj_calloc(uint32_t n, uint32_t sz);
uint32_t nj_realloc(uint32_t ga, uint32_t n);
uint32_t nj_memalign(uint32_t align, uint32_t n);
/* Usable size of a live block (for realloc-style HLE functions and tests). */
uint32_t nj_malloc_usable_size(uint32_t ga);

#endif
