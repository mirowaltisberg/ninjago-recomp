/* Name -> host function registry, thunk slots, data imports and guest-function overrides. */
#include <pthread.h>
#include <stdlib.h>

#include "nj/heap.h"
#include "nj/hle.h"

#define MAX_THUNKS (NJ_THUNK_SIZE / 8)
#define MAP_CAP 16384u /* power of two, > MAX names in each map */

typedef struct {
    const char *name; /* owned */
    NjHleFn fn;
    uint32_t value;   /* thunk index (fn map), data address (data map) or guest addr (override map) */
    int used;
} Entry;

typedef struct { Entry e[MAP_CAP]; } Map;

static Map fns, datas, overrides;
static const char *thunk_names[MAX_THUNKS];
static NjHleFn thunk_fns[MAX_THUNKS];
static uint32_t thunk_count;
static uint32_t data_top = NJ_HLEDATA_BASE;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct { uint32_t addr; NjHleFn fn; } OverrideAddr;
static OverrideAddr *override_addrs;
static uint32_t override_addr_count;

static uint32_t hash(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

/* Find or insert name; caller holds lock. */
static Entry *map_get(Map *m, const char *name, int insert) {
    uint32_t h = hash(name) & (MAP_CAP - 1);
    for (uint32_t probe = 0; probe < MAP_CAP; probe++, h = (h + 1) & (MAP_CAP - 1)) {
        Entry *e = &m->e[h];
        if (!e->used) {
            if (!insert) return NULL;
            e->used = 1;
            e->name = strdup(name);
            e->fn = NULL;
            e->value = UINT32_MAX;
            return e;
        }
        if (strcmp(e->name, name) == 0) return e;
    }
    nj_fatal("hle registry full while adding %s", name);
}

void hle_register(const char *name, NjHleFn fn) {
    pthread_mutex_lock(&lock);
    Entry *e = map_get(&fns, name, 1);
    e->fn = fn;
    if (e->value != UINT32_MAX) thunk_fns[e->value] = fn;
    pthread_mutex_unlock(&lock);
}

uint32_t hle_thunk(const char *name) {
    pthread_mutex_lock(&lock);
    Entry *e = map_get(&fns, name, 1);
    if (e->value == UINT32_MAX) {
        if (thunk_count == MAX_THUNKS) nj_fatal("out of thunk slots at %s", name);
        uint32_t i = thunk_count++;
        e->value = i;
        thunk_names[i] = e->name;
        thunk_fns[i] = e->fn;
        uint32_t ga = NJ_THUNK_BASE + 8 * i;
        nj_w32(ga, 0xEF000000u | i); /* svc #i */
        nj_w32(ga + 4, 0xE12FFF1Eu); /* bx lr */
    }
    uint32_t ga = NJ_THUNK_BASE + 8 * e->value;
    pthread_mutex_unlock(&lock);
    return ga;
}

static int thunk_index(uint32_t ga, uint32_t *idx) {
    if (!hle_is_thunk(ga) || (ga - NJ_THUNK_BASE) % 8) return 0;
    *idx = (ga - NJ_THUNK_BASE) / 8;
    return *idx < thunk_count;
}

NjHleFn hle_fn_at(uint32_t thunk_ga, const char **name_out) {
    uint32_t i;
    if (!thunk_index(thunk_ga, &i)) { if (name_out) *name_out = NULL; return NULL; }
    if (name_out) *name_out = thunk_names[i];
    return thunk_fns[i];
}

const char *hle_thunk_name(uint32_t index) { return index < thunk_count ? thunk_names[index] : NULL; }

void hle_dispatch(NjCtx *c, uint32_t thunk_ga) {
    const char *name;
    NjHleFn fn = hle_fn_at(thunk_ga, &name);
    if (!fn) {
        if (name) nj_fatal("unimplemented import: %s", name);
        nj_fatal("call to invalid thunk address 0x%08x", thunk_ga);
    }
    fn(c);
}

void hle_register_data(const char *name, uint32_t ga) {
    pthread_mutex_lock(&lock);
    map_get(&datas, name, 1)->value = ga;
    pthread_mutex_unlock(&lock);
}

uint32_t hle_data_addr(const char *name) {
    pthread_mutex_lock(&lock);
    Entry *e = map_get(&datas, name, 0);
    uint32_t v = (e && e->value != UINT32_MAX) ? e->value : 0;
    pthread_mutex_unlock(&lock);
    return v;
}

void hle_bind_imports(const NjImage *img) {
    for (uint32_t i = 0; i < img->import_count; i++) {
        const NjImport *imp = &img->imports[i];
        if (imp->type == NJ_IMPORT_FUNC) {
            nj_w32(imp->slot_ga, hle_thunk(imp->name));
        } else {
            uint32_t ga = hle_data_addr(imp->name);
            if (!ga) nj_fatal("unregistered data import: %s", imp->name);
            nj_w32(imp->slot_ga, ga);
        }
    }
}

void hle_register_override(const char *symbol, NjHleFn fn) {
    pthread_mutex_lock(&lock);
    map_get(&overrides, symbol, 1)->fn = fn;
    pthread_mutex_unlock(&lock);
}

static int oa_cmp(const void *a, const void *b) {
    uint32_t x = ((const OverrideAddr *)a)->addr, y = ((const OverrideAddr *)b)->addr;
    return (x > y) - (x < y);
}

void hle_resolve_overrides(const NjImage *img) {
    pthread_mutex_lock(&lock);
    free(override_addrs);
    override_addrs = calloc(MAP_CAP, sizeof(OverrideAddr));
    override_addr_count = 0;
    for (uint32_t i = 0; i < MAP_CAP; i++) {
        Entry *e = &overrides.e[i];
        if (!e->used || !e->fn) continue;
        uint32_t ga = nj_image_sym(img, e->name);
        if (!ga) {
            pthread_mutex_unlock(&lock);
            nj_fatal("override for unknown symbol: %s", e->name);
        }
        override_addrs[override_addr_count].addr = ga & ~1u;
        override_addrs[override_addr_count++].fn = e->fn;
    }
    qsort(override_addrs, override_addr_count, sizeof(OverrideAddr), oa_cmp);
    pthread_mutex_unlock(&lock);
}

NjHleFn hle_override_at(uint32_t guest_fn_addr) {
    uint32_t key = guest_fn_addr & ~1u, lo = 0, hi = override_addr_count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (override_addrs[mid].addr < key) lo = mid + 1;
        else hi = mid;
    }
    return (lo < override_addr_count && override_addrs[lo].addr == key) ? override_addrs[lo].fn : NULL;
}

uint32_t hle_data_alloc(uint32_t size, uint32_t align) {
    if (align == 0) align = 1;
    pthread_mutex_lock(&lock);
    uint32_t ga = (data_top + align - 1) & ~(align - 1);
    if ((uint64_t)ga + size > (uint64_t)NJ_HLEDATA_BASE + NJ_HLEDATA_SIZE) {
        pthread_mutex_unlock(&lock);
        nj_fatal("HLE data region exhausted (request %u bytes)", size);
    }
    data_top = ga + size;
    pthread_mutex_unlock(&lock);
    memset(nj_host(ga), 0, size);
    return ga;
}

uint32_t hle_guest_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    uint32_t ga = nj_malloc((uint32_t)n);
    if (!ga) nj_fatal("guest heap exhausted copying a string of %zu bytes", n);
    memcpy(nj_host(ga), s, n);
    return ga;
}
