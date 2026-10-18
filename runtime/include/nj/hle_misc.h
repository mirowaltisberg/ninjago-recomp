/* libm and libdl emulation. */
#ifndef NJ_HLE_MISC_H
#define NJ_HLE_MISC_H

#include "nj/loader.h"

/* acosf asin atan2f atanf ceil ceilf cos cosf floor floorf fmod log10 log10f pow powf sin sinf sqrt sqrtf __isfinitef */
void hle_libm_register(void);
/* dlopen/dlsym/dlclose; dlsym on the guest library's own handle resolves its exports (img may be NULL). */
void hle_dl_register(const NjImage *img);

#endif
