/* Guest file system: path prefixes -> host directories, bionic stdio over host FILE*, fds, mmap. */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "nj/format.h"
#include "nj/fs.h"
#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/hle_libc.h"

#define A(i) hle_arg(c, i)

static pthread_mutex_t fs_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---- path prefixes ---- */

#define MAX_PREFIXES 32
static struct { char *guest, *host; } prefixes[MAX_PREFIXES];
static int prefix_count;

void nj_fs_map_prefix(const char *guest_prefix, const char *host_dir) {
    pthread_mutex_lock(&fs_lock);
    for (int i = 0; i < prefix_count; i++)
        if (strcmp(prefixes[i].guest, guest_prefix) == 0) {
            free(prefixes[i].host);
            prefixes[i].host = strdup(host_dir);
            pthread_mutex_unlock(&fs_lock);
            return;
        }
    if (prefix_count < MAX_PREFIXES) {
        prefixes[prefix_count].guest = strdup(guest_prefix);
        prefixes[prefix_count].host = strdup(host_dir);
        prefix_count++;
    }
    pthread_mutex_unlock(&fs_lock);
}

const char *nj_fs_host_path(const char *gp, char *buf, size_t cap) {
    int best = -1;
    size_t best_len = 0;
    pthread_mutex_lock(&fs_lock);
    for (int i = 0; i < prefix_count; i++) {
        size_t n = strlen(prefixes[i].guest);
        if (strncmp(gp, prefixes[i].guest, n) == 0 && (gp[n] == 0 || gp[n] == '/') && n >= best_len) {
            best = i;
            best_len = n;
        }
    }
    const char *r = NULL;
    if (best >= 0) {
        snprintf(buf, cap, "%s%s", prefixes[best].host, gp + best_len);
        r = buf;
    }
    pthread_mutex_unlock(&fs_lock);
    return r;
}

static void warn_unmapped(const char *gp) {
    static char *seen[256];
    static int nseen;
    pthread_mutex_lock(&fs_lock);
    for (int i = 0; i < nseen; i++) if (strcmp(seen[i], gp) == 0) { pthread_mutex_unlock(&fs_lock); return; }
    if (nseen < 256) seen[nseen++] = strdup(gp);
    pthread_mutex_unlock(&fs_lock);
    fprintf(stderr, "[fs] guest path outside mapped prefixes: %s\n", gp);
}

/* Resolve guest path arg; on failure sets ENOENT and returns NULL. */
static const char *host_path_or_enoent(uint32_t ga, char *buf, size_t cap) {
    const char *gp = hle_cstr(ga);
    if (!gp) { hle_set_errno(NJ_EINVAL); return NULL; }
    const char *h = nj_fs_host_path(gp, buf, cap);
    if (!h) { warn_unmapped(gp); hle_set_errno(NJ_ENOENT); }
    return h;
}

static void set_host_errno(void) { hle_set_errno(nj_errno_from_host(errno)); }

/* ---- guest FILE objects ---- */

#define MAX_FILES 1024
static struct { uint32_t ga; FILE *f; } files[MAX_FILES];
static uint32_t sf_ga; /* __sF[3] */

FILE *nj_fs_host_file(uint32_t gf) {
    if (sf_ga && gf >= sf_ga && gf < sf_ga + 3 * NJ_BIONIC_FILE_SIZE) {
        uint32_t idx = (gf - sf_ga) / NJ_BIONIC_FILE_SIZE;
        return idx == 0 ? stdin : idx == 1 ? stdout : stderr;
    }
    FILE *r = NULL;
    pthread_mutex_lock(&fs_lock);
    for (int i = 0; i < MAX_FILES; i++) if (files[i].ga == gf && gf) { r = files[i].f; break; }
    pthread_mutex_unlock(&fs_lock);
    return r;
}

static FILE *file_arg(uint32_t gf) {
    FILE *f = nj_fs_host_file(gf);
    if (!f) nj_fatal("guest used an unknown FILE* 0x%08x", gf);
    return f;
}

static void hle_fopen(NjCtx *c) {
    char buf[1024];
    const char *h = host_path_or_enoent(A(0), buf, sizeof buf);
    if (!h) { hle_ret(c, 0); return; }
    FILE *f = fopen(h, hle_cstr(A(1)));
    if (!f) { set_host_errno(); hle_ret(c, 0); return; }
    uint32_t gf = nj_calloc(1, NJ_BIONIC_FILE_SIZE);
    pthread_mutex_lock(&fs_lock);
    int slot = -1;
    for (int i = 0; i < MAX_FILES; i++) if (!files[i].ga) { slot = i; break; }
    if (slot >= 0) { files[slot].ga = gf; files[slot].f = f; }
    pthread_mutex_unlock(&fs_lock);
    if (slot < 0) nj_fatal("too many open guest files");
    hle_ret(c, gf);
}

static void hle_fclose(NjCtx *c) {
    uint32_t gf = A(0);
    FILE *f = NULL;
    pthread_mutex_lock(&fs_lock);
    for (int i = 0; i < MAX_FILES; i++)
        if (files[i].ga == gf && gf) { f = files[i].f; files[i].ga = 0; files[i].f = NULL; break; }
    pthread_mutex_unlock(&fs_lock);
    if (!f) { hle_ret(c, 0); return; } /* closing stdio streams is ignored */
    int rc = fclose(f);
    nj_free(gf);
    hle_ret(c, (uint32_t)rc);
}

static void hle_fread(NjCtx *c) {
    size_t total = (size_t)A(1) * A(2);
    hle_ret(c, total ? (uint32_t)fread(nj_host(A(0)), A(1), A(2), file_arg(A(3))) : 0);
}
static void hle_fwrite(NjCtx *c) {
    size_t total = (size_t)A(1) * A(2);
    hle_ret(c, total ? (uint32_t)fwrite(nj_host(A(0)), A(1), A(2), file_arg(A(3))) : 0);
}
static void hle_fseek(NjCtx *c) {
    int rc = fseeko(file_arg(A(0)), (off_t)(int32_t)A(1), (int)A(2));
    if (rc) set_host_errno();
    hle_ret(c, (uint32_t)rc);
}
static void hle_ftell(NjCtx *c) { hle_ret(c, (uint32_t)(int32_t)ftello(file_arg(A(0)))); }
static void hle_feof(NjCtx *c) { hle_ret(c, (uint32_t)feof(file_arg(A(0)))); }
static void hle_fgetc(NjCtx *c) { hle_ret(c, (uint32_t)fgetc(file_arg(A(0)))); }
static void hle_ungetc(NjCtx *c) { hle_ret(c, (uint32_t)ungetc((int)A(0), file_arg(A(1)))); }
static void hle_rewind(NjCtx *c) { rewind(file_arg(A(0))); }
static void hle_fflush(NjCtx *c) { hle_ret(c, (uint32_t)fflush(A(0) ? file_arg(A(0)) : NULL)); }
static void hle_fputc(NjCtx *c) { hle_ret(c, (uint32_t)fputc((int)A(0), file_arg(A(1)))); }
static void hle_fputs(NjCtx *c) { hle_ret(c, (uint32_t)fputs(hle_cstr(A(0)), file_arg(A(1)))); }

static void hle_fprintf(NjCtx *c) {
    NjVaCursor va = nj_va_from_args(c, 2);
    char *s = nj_format_alloc(hle_cstr(A(1)), &va);
    FILE *f = file_arg(A(0));
    int n = fputs(s, f) < 0 ? -1 : (int)strlen(s);
    free(s);
    hle_ret(c, (uint32_t)n);
}

/* ---- paths ---- */

static void hle_remove(NjCtx *c) {
    char buf[1024];
    const char *h = host_path_or_enoent(A(0), buf, sizeof buf);
    int rc = h ? remove(h) : -1;
    if (h && rc) set_host_errno();
    hle_ret(c, (uint32_t)rc);
}

static void hle_mkdir(NjCtx *c) {
    char buf[1024];
    const char *h = host_path_or_enoent(A(0), buf, sizeof buf);
    int rc = h ? mkdir(h, (mode_t)(A(1) & 07777)) : -1;
    if (h && rc) set_host_errno();
    hle_ret(c, (uint32_t)rc);
}

/* bionic 32-bit ARM struct stat (104 bytes). */
static void write_stat(uint32_t st, const struct stat *s) {
    memset(nj_host(st), 0, NJ_BIONIC_STAT_SIZE);
    nj_w64(st + 0, (uint64_t)s->st_dev);
    nj_w32(st + 12, (uint32_t)s->st_ino);
    nj_w32(st + 16, (uint32_t)s->st_mode);
    nj_w32(st + 20, (uint32_t)s->st_nlink);
    nj_w32(st + 24, (uint32_t)s->st_uid);
    nj_w32(st + 28, (uint32_t)s->st_gid);
    nj_w64(st + 32, (uint64_t)s->st_rdev);
    nj_w64(st + 48, (uint64_t)s->st_size);
    nj_w32(st + 56, (uint32_t)s->st_blksize);
    nj_w64(st + 64, (uint64_t)s->st_blocks);
    nj_w32(st + 72, (uint32_t)s->st_atimespec.tv_sec);
    nj_w32(st + 76, (uint32_t)s->st_atimespec.tv_nsec);
    nj_w32(st + 80, (uint32_t)s->st_mtimespec.tv_sec);
    nj_w32(st + 84, (uint32_t)s->st_mtimespec.tv_nsec);
    nj_w32(st + 88, (uint32_t)s->st_ctimespec.tv_sec);
    nj_w32(st + 92, (uint32_t)s->st_ctimespec.tv_nsec);
    nj_w64(st + 96, (uint64_t)s->st_ino);
}

static void hle_stat(NjCtx *c) {
    char buf[1024];
    const char *h = host_path_or_enoent(A(0), buf, sizeof buf);
    struct stat s;
    if (!h) { hle_ret(c, (uint32_t)-1); return; }
    if (stat(h, &s)) { set_host_errno(); hle_ret(c, (uint32_t)-1); return; }
    write_stat(A(1), &s);
    hle_ret(c, 0);
}

/* ---- fds: Linux open flags differ from Darwin's ---- */

static int host_open_flags(uint32_t lf) {
    int f = (int)(lf & 3); /* O_RDONLY/O_WRONLY/O_RDWR are equal */
    if (lf & 0100) f |= O_CREAT;
    if (lf & 0200) f |= O_EXCL;
    if (lf & 01000) f |= O_TRUNC;
    if (lf & 02000) f |= O_APPEND;
    if (lf & 04000) f |= O_NONBLOCK;
    if (lf & 02000000) f |= O_CLOEXEC;
    return f; /* O_LARGEFILE (0400000) and O_DIRECTORY-style hints are ignored */
}

static void hle_open(NjCtx *c) {
    char buf[1024];
    const char *h = host_path_or_enoent(A(0), buf, sizeof buf);
    if (!h) { hle_ret(c, (uint32_t)-1); return; }
    int fd = open(h, host_open_flags(A(1)), (mode_t)(A(2) & 07777));
    if (fd < 0) set_host_errno();
    hle_ret(c, (uint32_t)fd);
}

static void hle_close(NjCtx *c) {
    int fd = (int)A(0);
    if (fd <= 2) { hle_ret(c, 0); return; }
    int rc = close(fd);
    if (rc) set_host_errno();
    hle_ret(c, (uint32_t)rc);
}

/* ---- mmap: page-aligned guest heap blocks ---- */

#define MAX_MAPS 1024
static struct { uint32_t ga, len; } maps[MAX_MAPS];

static void hle_mmap(NjCtx *c) {
    uint32_t addr = A(0), len = A(1), flags = A(3), off = A(5);
    int fd = (int)A(4);
    if ((flags & 0x10) && addr) nj_fatal("guest mmap MAP_FIXED at 0x%08x is not supported", addr);
    if (len == 0) { hle_set_errno(NJ_EINVAL); hle_ret(c, 0xFFFFFFFFu); return; }
    uint32_t ga = nj_memalign(4096, len);
    if (!ga) { hle_set_errno(NJ_ENOMEM); hle_ret(c, 0xFFFFFFFFu); return; }
    uint8_t *p = nj_host(ga);
    size_t got = 0;
    if (!(flags & 0x20)) { /* file-backed */
        if ((flags & 0x01) && (A(2) & 2))
            fprintf(stderr, "[fs] MAP_SHARED writable file mapping: writes will not reach the file\n");
        while (got < len) {
            ssize_t n = pread(fd, p + got, len - got, (off_t)off + (off_t)got);
            if (n <= 0) break;
            got += (size_t)n;
        }
    }
    memset(p + got, 0, len - got);
    pthread_mutex_lock(&fs_lock);
    int slot = -1;
    for (int i = 0; i < MAX_MAPS; i++) if (!maps[i].ga) { slot = i; break; }
    if (slot >= 0) { maps[slot].ga = ga; maps[slot].len = len; }
    pthread_mutex_unlock(&fs_lock);
    if (slot < 0) nj_fatal("too many guest mmaps");
    hle_ret(c, ga);
}

static void hle_munmap(NjCtx *c) {
    uint32_t ga = A(0);
    int found = 0;
    pthread_mutex_lock(&fs_lock);
    for (int i = 0; i < MAX_MAPS; i++)
        if (maps[i].ga == ga && ga) { maps[i].ga = 0; found = 1; break; }
    pthread_mutex_unlock(&fs_lock);
    if (!found) { hle_set_errno(NJ_EINVAL); hle_ret(c, (uint32_t)-1); return; }
    nj_free(ga);
    hle_ret(c, 0);
}

void hle_fs_register(void) {
    if (!sf_ga) {
        sf_ga = hle_data_alloc(3 * NJ_BIONIC_FILE_SIZE, 4);
        hle_register_data("__sF", sf_ga);
    }
    static const struct { const char *name; NjHleFn fn; } fns[] = {
        {"fopen", hle_fopen}, {"fclose", hle_fclose}, {"fread", hle_fread}, {"fwrite", hle_fwrite},
        {"fseek", hle_fseek}, {"ftell", hle_ftell}, {"feof", hle_feof}, {"fgetc", hle_fgetc},
        {"ungetc", hle_ungetc}, {"rewind", hle_rewind}, {"fflush", hle_fflush}, {"fputc", hle_fputc},
        {"fputs", hle_fputs}, {"fprintf", hle_fprintf}, {"remove", hle_remove}, {"mkdir", hle_mkdir},
        {"stat", hle_stat}, {"open", hle_open}, {"close", hle_close}, {"mmap", hle_mmap},
        {"munmap", hle_munmap},
    };
    for (size_t i = 0; i < sizeof fns / sizeof fns[0]; i++) hle_register(fns[i].name, fns[i].fn);
}
