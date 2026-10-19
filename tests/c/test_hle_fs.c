#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "nj_test.h"

#include "nj/backend.h"
#include "nj/fs.h"
#include "nj/guest.h"
#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/hle_libc.h"

#define FILES "/data/data/com.lego.ninjago.toe/files"

static char tmpdir[256];

static void setup(void) {
    static int done;
    if (done) return;
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_mem_map_fixed_regions(), 0);
    NJ_EQ_U(nj_heap_init(), 0);
    hle_libc_core_register(NULL);
    hle_fs_register();
    NJ_EQ_U(nj_backend_init(), 0);
    snprintf(tmpdir, sizeof tmpdir, "/tmp/nj-fsXXXXXX");
    NJ_CHECK(mkdtemp(tmpdir) != NULL);
    nj_fs_map_prefix(FILES, tmpdir);
    nj_fs_map_prefix("/data/data/com.lego.ninjago.toe", "/nonexistent-shorter-prefix");
    done = 1;
}

static uint32_t gstr(const char *s) { return hle_guest_strdup(s); }

static uint32_t call(const char *name, const uint32_t *w, int n) {
    NjCtx *c = nj_ctx();
    uint32_t sp = c->r[13], nsp = (sp - 128) & ~7u;
    for (int i = 0; i < n; i++) {
        if (i < 4) c->r[i] = w[i];
        else nj_w32(nsp + 4u * (uint32_t)(i - 4), w[i]);
    }
    c->r[13] = nsp;
    hle_dispatch(c, hle_thunk(name));
    c->r[13] = sp;
    return c->r[0];
}

NJ_TEST(fs_prefix_longest_wins) {
    setup();
    char buf[512];
    const char *h = nj_fs_host_path(FILES "/save/a.dat", buf, sizeof buf);
    NJ_CHECK(h != NULL);
    char want[512];
    snprintf(want, sizeof want, "%s/save/a.dat", tmpdir);
    NJ_EQ_STR(h, want);
    NJ_CHECK(nj_fs_host_path("/sdcard/x", buf, sizeof buf) == NULL);
}

NJ_TEST(fs_roundtrip) {
    setup();
    uint32_t path = gstr(FILES "/a.bin");
    uint32_t w1[] = {path, gstr("wb")};
    uint32_t f = call("fopen", w1, 2);
    NJ_CHECK(f != 0);
    uint32_t data = gstr("xyz");
    uint32_t w2[] = {data, 1, 3, f};
    NJ_EQ_U(call("fwrite", w2, 4), 3);
    uint32_t w3[] = {f};
    NJ_EQ_U(call("fclose", w3, 1), 0);
    uint32_t w4[] = {path, gstr("rb")};
    f = call("fopen", w4, 2);
    uint32_t buf = nj_calloc(1, 8);
    uint32_t w5[] = {buf, 1, 8, f};
    NJ_EQ_U(call("fread", w5, 4), 3);
    NJ_EQ_STR(hle_cstr(buf), "xyz");
    uint32_t w6[] = {f};
    NJ_CHECK(call("feof", w6, 1) != 0);
    call("rewind", w6, 1);
    NJ_EQ_U(call("fgetc", w6, 1), 'x');
    uint32_t w7[] = {'Q', f};
    NJ_EQ_U(call("ungetc", w7, 2), 'Q');
    NJ_EQ_U(call("fgetc", w6, 1), 'Q');
    call("fclose", w6, 1);
}

NJ_TEST(fs_seek_tell) {
    setup();
    uint32_t w1[] = {gstr(FILES "/b.bin"), gstr("w+b")};
    uint32_t f = call("fopen", w1, 2);
    uint32_t w2[] = {gstr("0123456789"), f};
    NJ_CHECK((int32_t)call("fputs", w2, 2) >= 0);
    uint32_t w3[] = {f, 0, 2 /* SEEK_END */};
    NJ_EQ_U(call("fseek", w3, 3), 0);
    uint32_t w4[] = {f};
    NJ_EQ_U(call("ftell", w4, 1), 10);
    uint32_t w5[] = {f, (uint32_t)-3, 1 /* SEEK_CUR */};
    call("fseek", w5, 3);
    NJ_EQ_U(call("fgetc", w4, 1), '7');
    call("fclose", w4, 1);
}

NJ_TEST(fs_stat_layout) {
    setup();
    char host[512];
    snprintf(host, sizeof host, "%s/c.bin", tmpdir);
    FILE *hf = fopen(host, "wb");
    for (int i = 0; i < 1234; i++) fputc('a', hf);
    fclose(hf);
    uint32_t st = nj_calloc(1, NJ_BIONIC_STAT_SIZE);
    uint32_t w[] = {gstr(FILES "/c.bin"), st};
    NJ_EQ_U(call("stat", w, 2), 0);
    NJ_EQ_U(nj_r64(st + 48), 1234);
    NJ_EQ_U(nj_r32(st + 16) & 0170000, 0100000);
    NJ_CHECK(nj_r32(st + 80) > 1400000000u); /* st_mtime */
    uint32_t w2[] = {gstr(FILES), st};
    NJ_EQ_U(call("stat", w2, 2), 0);
    NJ_EQ_U(nj_r32(st + 16) & 0170000, 0040000);
}

NJ_TEST(fs_mmap_large) {
    setup();
    char host[512];
    snprintf(host, sizeof host, "%s/big.bin", tmpdir);
    int fd = open(host, O_CREAT | O_RDWR | O_TRUNC, 0644);
    uint64_t size = 400ull << 20;
    NJ_EQ_U(ftruncate(fd, (off_t)size), 0);
    NJ_EQ_U(pwrite(fd, "ENDMARK", 7, (off_t)size - 7), 7);
    close(fd);
    uint32_t wo[] = {gstr(FILES "/big.bin"), 0 /* O_RDONLY */, 0};
    uint32_t gfd = call("open", wo, 3);
    NJ_CHECK((int32_t)gfd >= 0);
    uint32_t wm[] = {0, (uint32_t)size, 1 /* PROT_READ */, 2 /* MAP_PRIVATE */, gfd, 0};
    uint32_t p = call("mmap", wm, 6);
    NJ_CHECK(p != 0xFFFFFFFFu);
    NJ_EQ_U(p % 4096, 0);
    NJ_CHECK(memcmp(nj_host(p + (uint32_t)size - 7), "ENDMARK", 7) == 0);
    uint32_t wu[] = {p, (uint32_t)size};
    NJ_EQ_U(call("munmap", wu, 2), 0);
    uint32_t wc[] = {gfd};
    NJ_EQ_U(call("close", wc, 1), 0);
}

NJ_TEST(fs_mmap_anonymous_zeroed) {
    setup();
    uint32_t wm[] = {0, 8192, 3, 0x22 /* MAP_PRIVATE|MAP_ANONYMOUS */, (uint32_t)-1, 0};
    uint32_t p = call("mmap", wm, 6);
    NJ_CHECK(p != 0xFFFFFFFFu);
    for (uint32_t i = 0; i < 8192; i += 512) NJ_EQ_U(nj_r8(p + i), 0);
}

NJ_TEST(fs_open_linux_flags) {
    setup();
    /* O_WRONLY|O_CREAT|O_TRUNC with Linux values 01 | 0100 | 01000 */
    uint32_t wo[] = {gstr(FILES "/d.bin"), 01 | 0100 | 01000, 0644};
    uint32_t fd = call("open", wo, 3);
    NJ_CHECK((int32_t)fd >= 0);
    uint32_t wc[] = {fd};
    call("close", wc, 1);
    char host[512];
    snprintf(host, sizeof host, "%s/d.bin", tmpdir);
    NJ_CHECK(access(host, F_OK) == 0);
}

NJ_TEST(fs_stderr_fprintf) {
    setup();
    char path[] = "/tmp/nj-errXXXXXX";
    int cap = mkstemp(path);
    int saved = dup(2);
    dup2(cap, 2);
    uint32_t sf = hle_data_addr("__sF");
    uint32_t w[] = {sf + 2 * NJ_BIONIC_FILE_SIZE, gstr("x=%d"), 5};
    call("fprintf", w, 3);
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    char got[16] = {0};
    pread(cap, got, sizeof got - 1, 0);
    close(cap);
    unlink(path);
    NJ_EQ_STR(got, "x=5");
}

NJ_TEST(fs_unmapped_path_enoent) {
    setup();
    uint32_t w[] = {gstr("/sdcard/zzz"), gstr("rb")};
    NJ_EQ_U(call("fopen", w, 2), 0);
    NJ_EQ_U(nj_r32(hle_errno_ga()), NJ_ENOENT);
    uint32_t w2[] = {gstr(FILES "/missing.bin"), gstr("rb")};
    NJ_EQ_U(call("fopen", w2, 2), 0);
    NJ_EQ_U(nj_r32(hle_errno_ga()), NJ_ENOENT);
}

NJ_TEST(fs_mkdir_remove) {
    setup();
    uint32_t w[] = {gstr(FILES "/sub"), 0755};
    NJ_EQ_U(call("mkdir", w, 2), 0);
    NJ_EQ_U(call("mkdir", w, 2), (uint32_t)-1);
    NJ_EQ_U(nj_r32(hle_errno_ga()), NJ_EEXIST);
    uint32_t w2[] = {gstr(FILES "/sub")};
    NJ_EQ_U(call("remove", w2, 1), 0);
}

NJ_TEST(fs_fileno_field_inline) {
    setup();
    char host[512];
    snprintf(host, sizeof host, "%s/e.bin", tmpdir);
    FILE *hf = fopen(host, "wb");
    fputs("12345", hf);
    fclose(hf);
    uint32_t w[] = {gstr(FILES "/e.bin"), gstr("rb")};
    uint32_t f = call("fopen", w, 2);
    NJ_CHECK(f != 0);
    /* bionic's fileno() macro reads FILE::_file (short at offset 14) directly */
    int fd = (int16_t)nj_r16(f + 14);
    struct stat st;
    NJ_EQ_U(fstat(fd, &st), 0);
    NJ_EQ_U(st.st_size, 5);
    NJ_CHECK(nj_r16(f + 12) & 0x0004); /* __SRD */
    uint32_t wc[] = {f};
    call("fclose", wc, 1);
}
