/* Guest file system: path mapping, stdio, file descriptors, mmap. */
#ifndef NJ_FS_H
#define NJ_FS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Map a guest path prefix to a host directory; the longest matching prefix wins. */
void nj_fs_map_prefix(const char *guest_prefix, const char *host_dir);
/* Host path for a guest path, or NULL if no prefix matches. */
const char *nj_fs_host_path(const char *guest_path, char *buf, size_t cap);
/* Host FILE* behind a guest FILE* (stdin/stdout/stderr for &__sF[0..2]); NULL if unknown. */
FILE *nj_fs_host_file(uint32_t guest_file);

/* fopen..fputs, fprintf, remove, mkdir, stat, open, close, mmap, munmap and data __sF. */
void hle_fs_register(void);

#define NJ_BIONIC_FILE_SIZE 84
#define NJ_BIONIC_STAT_SIZE 104

#endif
