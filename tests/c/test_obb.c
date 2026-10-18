#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "nj_test.h"

#include "nj/obb.h"

/* Builds a one-entry zip whose local extra field (9 bytes) differs from the central one (0 bytes). */
static void put16(FILE *f, uint16_t v) { fputc(v & 0xff, f); fputc(v >> 8, f); }
static void put32(FILE *f, uint32_t v) { put16(f, (uint16_t)v); put16(f, (uint16_t)(v >> 16)); }

static void make_zip(const char *path, const char *name, uint16_t method, const char *data, uint16_t local_extra) {
    FILE *f = fopen(path, "wb");
    uint32_t n = (uint32_t)strlen(name), len = (uint32_t)strlen(data);
    put32(f, 0x04034b50); put16(f, 10); put16(f, 0); put16(f, method); put16(f, 0); put16(f, 0);
    put32(f, 0); put32(f, len); put32(f, len); put16(f, (uint16_t)n); put16(f, local_extra);
    fwrite(name, 1, n, f);
    for (int i = 0; i < local_extra; i++) fputc(0xEE, f);
    fwrite(data, 1, len, f);
    long cd = ftell(f);
    put32(f, 0x02014b50); put16(f, 20); put16(f, 10); put16(f, 0); put16(f, method); put16(f, 0); put16(f, 0);
    put32(f, 0); put32(f, len); put32(f, len); put16(f, (uint16_t)n); put16(f, 0); put16(f, 0);
    put16(f, 0); put16(f, 0); put32(f, 0); put32(f, 0 /* local header offset */);
    fwrite(name, 1, n, f);
    long cd_end = ftell(f);
    put32(f, 0x06054b50); put16(f, 0); put16(f, 0); put16(f, 1); put16(f, 1);
    put32(f, (uint32_t)(cd_end - cd)); put32(f, (uint32_t)cd); put16(f, 0);
    fclose(f);
}

NJ_TEST(obb_local_extra) {
    char path[] = "/tmp/claude-501-njzipXXXXXX";
    close(mkstemp(path));
    make_zip(path, "music\\a.mp3", 0, "ID3 rest-of-file", 9);
    NjObbEntry *e = NULL;
    uint32_t n = 0;
    NJ_EQ_U(nj_obb_index(path, &e, &n), 0);
    NJ_EQ_U(n, 1);
    NJ_EQ_STR(e[0].name, "music/a.mp3");
    NJ_EQ_U(e[0].size, 16);
    FILE *f = fopen(path, "rb");
    fseek(f, (long)e[0].data_offset, SEEK_SET);
    char got[4] = {0};
    fread(got, 1, 3, f);
    fclose(f);
    NJ_EQ_STR(got, "ID3");
    free(e);
    unlink(path);
}

NJ_TEST(obb_rejects_deflate) {
    char path[] = "/tmp/claude-501-njzipXXXXXX";
    close(mkstemp(path));
    make_zip(path, "a.bin", 8, "xxxx", 0);
    NjObbEntry *e = NULL;
    uint32_t n = 0;
    NJ_CHECK(nj_obb_index(path, &e, &n) != 0);
    unlink(path);
}

NJ_TEST(obb_real_fib) {
    if (access(NJ_GUEST_OBB_PATH, R_OK) != 0) NJ_SKIP("OBB not present");
    NjObbEntry *e = NULL;
    uint32_t n = 0;
    NJ_EQ_U(nj_obb_index(NJ_GUEST_OBB_PATH, &e, &n), 0);
    NJ_EQ_U(n, 33);
    int found = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (strcmp(e[i].name, "lego_m1_android.fib") != 0) continue;
        found = 1;
        FILE *f = fopen(NJ_GUEST_OBB_PATH, "rb");
        fseeko(f, (off_t)e[i].data_offset, SEEK_SET);
        char magic[9] = {0};
        fread(magic, 1, 8, f);
        fclose(f);
        NJ_EQ_STR(magic, "FUSE1.00");
        NJ_EQ_U(e[i].size, 104937991);
    }
    NJ_CHECK(found);
    free(e);
}

#include "nj/guest.h"
#include "nj/hle.h"

NJ_TEST(asset_manager_handle_stable) {
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_mem_map_fixed_regions(), 0);
    hle_android_register();
    NjCtx c = {0};
    hle_dispatch(&c, hle_thunk("AAssetManager_fromJava"));
    uint32_t a = c.r[0];
    NJ_CHECK(a >= NJ_HLEDATA_BASE && a < NJ_HLEDATA_BASE + NJ_HLEDATA_SIZE);
    hle_dispatch(&c, hle_thunk("AAssetManager_fromJava"));
    NJ_EQ_U(c.r[0], a);
}
