/* Zip central-directory reader for the OBB (stored entries only, as the engine requires). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nj/hle.h"
#include "nj/obb.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)rd16(p) | (uint32_t)rd16(p + 2) << 16; }

int nj_obb_index(const char *host_path, NjObbEntry **out, uint32_t *count) {
    *out = NULL;
    *count = 0;
    FILE *f = fopen(host_path, "rb");
    if (!f) { fprintf(stderr, "[obb] cannot open %s\n", host_path); return -1; }
    fseeko(f, 0, SEEK_END);
    off_t fsize = ftello(f);
    size_t tail_len = fsize < 65557 ? (size_t)fsize : 65557; /* EOCD (22) + max comment */
    uint8_t *tail = malloc(tail_len);
    fseeko(f, fsize - (off_t)tail_len, SEEK_SET);
    int rc = -1;
    uint8_t *cd = NULL;
    NjObbEntry *entries = NULL;
    if (fread(tail, 1, tail_len, f) != tail_len) goto out;
    long eocd = -1;
    for (long i = (long)tail_len - 22; i >= 0; i--)
        if (rd32(tail + i) == 0x06054b50u) { eocd = i; break; }
    if (eocd < 0) { fprintf(stderr, "[obb] no end-of-central-directory record\n"); goto out; }
    uint16_t n = rd16(tail + eocd + 10);
    uint32_t cd_size = rd32(tail + eocd + 12), cd_off = rd32(tail + eocd + 16);
    cd = malloc(cd_size);
    fseeko(f, cd_off, SEEK_SET);
    if (fread(cd, 1, cd_size, f) != cd_size) goto out;
    entries = calloc(n ? n : 1, sizeof(NjObbEntry));
    uint32_t pos = 0, k = 0;
    for (uint16_t i = 0; i < n; i++) {
        if (pos + 46 > cd_size || rd32(cd + pos) != 0x02014b50u) { fprintf(stderr, "[obb] bad central directory\n"); goto out; }
        uint16_t method = rd16(cd + pos + 10);
        uint32_t csize = rd32(cd + pos + 20), usize = rd32(cd + pos + 24);
        uint16_t nlen = rd16(cd + pos + 28), elen = rd16(cd + pos + 30), clen = rd16(cd + pos + 32);
        uint32_t lho = rd32(cd + pos + 42);
        char name[256];
        size_t copy = nlen < sizeof name - 1 ? nlen : sizeof name - 1;
        memcpy(name, cd + pos + 46, copy);
        name[copy] = 0;
        for (char *p = name; *p; p++) if (*p == '\\') *p = '/';
        pos += 46u + nlen + elen + clen;
        if (copy && name[copy - 1] == '/') continue;
        if (method != 0 || csize != usize) {
            fprintf(stderr, "[obb] entry %s is compressed (method %u); the engine can only read stored entries\n", name, method);
            goto out;
        }
        uint8_t lh[30];
        fseeko(f, lho, SEEK_SET);
        if (fread(lh, 1, 30, f) != 30 || rd32(lh) != 0x04034b50u) { fprintf(stderr, "[obb] bad local header for %s\n", name); goto out; }
        NjObbEntry *e = &entries[k++];
        memcpy(e->name, name, copy + 1);
        e->data_offset = (uint64_t)lho + 30 + rd16(lh + 26) + rd16(lh + 28);
        e->size = usize;
    }
    *out = entries;
    *count = k;
    entries = NULL;
    rc = 0;
out:
    free(entries);
    free(cd);
    free(tail);
    fclose(f);
    return rc;
}

static void hle_AAssetManager_fromJava(NjCtx *c) {
    static uint32_t mgr;
    if (!mgr) mgr = hle_data_alloc(16, 4);
    hle_ret(c, mgr);
}

void hle_android_register(void) { hle_register("AAssetManager_fromJava", hle_AAssetManager_fromJava); }
