/* Index of the OBB (a zip with stored entries) as the engine expects it from addOBBEntriesToFusion. */
#ifndef NJ_OBB_H
#define NJ_OBB_H

#include <stdint.h>

typedef struct {
    char name[256];       /* '/' separators */
    uint64_t data_offset; /* first byte of the entry's data (past its local header) */
    uint64_t size;        /* stored size (= uncompressed size) */
} NjObbEntry;

/* Parse the zip's central directory. Fails (-1, message on stderr) on compressed entries.
 * *out is malloc'd; caller frees. Directory entries are skipped. */
int nj_obb_index(const char *host_path, NjObbEntry **out, uint32_t *count);

/* AAssetManager_fromJava. */
void hle_android_register(void);

#endif
