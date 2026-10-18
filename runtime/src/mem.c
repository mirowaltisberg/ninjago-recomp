/* 4 GiB guest address space reservation and mapping bookkeeping. */
#include <pthread.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#include "nj/guest.h"

uint8_t *nj_mem;

#define NJ_MAX_INTERVALS 1024

typedef struct { uint64_t start, end; } Interval; /* [start, end) in guest addresses, host-page rounded */

static Interval intervals[NJ_MAX_INTERVALS];
static int interval_count;
static pthread_mutex_t mem_lock = PTHREAD_MUTEX_INITIALIZER;

int nj_mem_init(void) {
    pthread_mutex_lock(&mem_lock);
    if (!nj_mem) {
        void *p = mmap(NULL, 1ull << 32, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p != MAP_FAILED) nj_mem = p;
    }
    pthread_mutex_unlock(&mem_lock);
    return nj_mem ? 0 : -1;
}

/* Insert [start, end) and merge overlapping or adjacent intervals. Caller holds mem_lock. */
static int add_interval(uint64_t start, uint64_t end) {
    int i = 0;
    while (i < interval_count && intervals[i].end < start) i++;
    int j = i;
    while (j < interval_count && intervals[j].start <= end) {
        if (intervals[j].start < start) start = intervals[j].start;
        if (intervals[j].end > end) end = intervals[j].end;
        j++;
    }
    int removed = j - i;
    if (removed == 0) {
        if (interval_count == NJ_MAX_INTERVALS) return -1;
        memmove(&intervals[i + 1], &intervals[i], (size_t)(interval_count - i) * sizeof(Interval));
        interval_count++;
    } else if (removed > 1) {
        memmove(&intervals[i + 1], &intervals[j], (size_t)(interval_count - j) * sizeof(Interval));
        interval_count -= removed - 1;
    }
    intervals[i].start = start;
    intervals[i].end = end;
    return 0;
}

int nj_mem_map(uint32_t ga, uint32_t size, int prot) {
    if (!nj_mem || size == 0) return -1;
    uint64_t page = (uint64_t)getpagesize();
    uint64_t start = (uint64_t)ga & ~(page - 1);
    uint64_t end = ((uint64_t)ga + size + page - 1) & ~(page - 1);
    if (end > (1ull << 32)) return -1;
    int hp = PROT_NONE;
    if (prot & NJ_PROT_R) hp |= PROT_READ;
    if (prot & NJ_PROT_W) hp |= PROT_WRITE | PROT_READ;
    if (mprotect(nj_mem + start, (size_t)(end - start), hp) != 0) return -1;
    pthread_mutex_lock(&mem_lock);
    int rc = add_interval(start, end);
    pthread_mutex_unlock(&mem_lock);
    return rc;
}

int nj_mem_map_fixed_regions(void) {
    const int rw = NJ_PROT_R | NJ_PROT_W;
    if (nj_mem_map(NJ_HLEDATA_BASE, NJ_HLEDATA_SIZE, rw)) return -1;
    if (nj_mem_map(NJ_STACK_BASE, NJ_STACK_SIZE * NJ_STACK_COUNT, rw)) return -1;
    if (nj_mem_map(NJ_THUNK_BASE, NJ_THUNK_SIZE, rw | NJ_PROT_X)) return -1;
    if (nj_mem_map(NJ_RETURN_TRAP, 0x10000, rw | NJ_PROT_X)) return -1;
    return 0;
}

bool nj_mem_is_mapped(uint32_t ga, uint32_t size) {
    uint64_t start = ga, end = (uint64_t)ga + size;
    bool ok = false;
    pthread_mutex_lock(&mem_lock);
    for (int i = 0; i < interval_count; i++) {
        if (intervals[i].start <= start && end <= intervals[i].end) { ok = true; break; }
        if (intervals[i].start > start) break;
    }
    pthread_mutex_unlock(&mem_lock);
    return ok;
}
