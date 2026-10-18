/* Guest heap on a dlmalloc mspace that spans the whole heap range. */
#include <pthread.h>
#include <stddef.h>

#include "nj/guest.h"
#include "nj/heap.h"

/* dlmalloc mspace API (third_party/dlmalloc/malloc.c, built with ONLY_MSPACES). */
typedef void *mspace;
mspace create_mspace_with_base(void *base, size_t capacity, int locked);
void *mspace_malloc(mspace msp, size_t bytes);
void mspace_free(mspace msp, void *mem);
void *mspace_calloc(mspace msp, size_t n_elements, size_t elem_size);
void *mspace_realloc(mspace msp, void *mem, size_t newsize);
void *mspace_memalign(mspace msp, size_t alignment, size_t bytes);
size_t mspace_usable_size(const void *mem);

static mspace heap;
static pthread_once_t heap_once = PTHREAD_ONCE_INIT;
static int heap_rc = -1;

static void heap_create(void) {
    if (nj_mem_map(NJ_HEAP_BASE, NJ_HEAP_SIZE, NJ_PROT_R | NJ_PROT_W) != 0) return;
    heap = create_mspace_with_base(nj_host(NJ_HEAP_BASE), NJ_HEAP_SIZE, 1);
    heap_rc = heap ? 0 : -1;
}

int nj_heap_init(void) {
    if (!nj_mem) return -1;
    pthread_once(&heap_once, heap_create);
    return heap_rc;
}

static uint32_t to_guest(void *p) { return p ? nj_guest(p) : 0; }
static void *to_host(uint32_t ga) { return ga ? nj_host(ga) : NULL; }

uint32_t nj_malloc(uint32_t n) { return to_guest(mspace_malloc(heap, n)); }
void nj_free(uint32_t ga) { if (ga) mspace_free(heap, nj_host(ga)); }
uint32_t nj_calloc(uint32_t n, uint32_t sz) { return to_guest(mspace_calloc(heap, n, sz)); }
uint32_t nj_realloc(uint32_t ga, uint32_t n) { return to_guest(mspace_realloc(heap, to_host(ga), n)); }
uint32_t nj_memalign(uint32_t align, uint32_t n) { return to_guest(mspace_memalign(heap, align, n)); }
uint32_t nj_malloc_usable_size(uint32_t ga) { return ga ? (uint32_t)mspace_usable_size(nj_host(ga)) : 0; }
