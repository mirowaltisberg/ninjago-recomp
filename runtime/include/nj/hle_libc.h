/* Guest C library (bionic) emulation: registration and shared helpers. */
#ifndef NJ_HLE_LIBC_H
#define NJ_HLE_LIBC_H

#include <stdint.h>

#include "nj/loader.h"

/* Linux/bionic errno values (they differ from Darwin's). */
enum {
    NJ_EPERM = 1, NJ_ENOENT = 2, NJ_EINTR = 4, NJ_EIO = 5, NJ_EBADF = 9, NJ_EAGAIN = 11, NJ_ENOMEM = 12,
    NJ_EACCES = 13, NJ_EBUSY = 16, NJ_EEXIST = 17, NJ_ENOTDIR = 20, NJ_EISDIR = 21, NJ_EINVAL = 22,
    NJ_EMFILE = 24, NJ_ENOSPC = 28, NJ_ESPIPE = 29, NJ_ERANGE = 34, NJ_EDEADLK = 35, NJ_ENOSYS = 38,
    NJ_ENOTEMPTY = 39, NJ_ETIMEDOUT = 110
};

/* malloc..strtod, qsort/bsearch, ctype tables, errno, abort/assert, atexit, __gnu_Unwind_Find_exidx. */
void hle_libc_core_register(const NjImage *img);

/* Guest address of the calling thread's errno int. */
uint32_t hle_errno_ga(void);
void hle_set_errno(int linux_errno);
/* Map a Darwin errno value to its Linux number. */
int nj_errno_from_host(int host_errno);

#endif
