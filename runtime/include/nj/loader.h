/* ELF32 ARM shared-object loader for the guest library. */
#ifndef NJ_LOADER_H
#define NJ_LOADER_H

#include <stdint.h>

typedef enum { NJ_IMPORT_FUNC, NJ_IMPORT_DATA } NjImportType;

typedef struct {
    const char *name;
    uint32_t slot_ga;   /* guest address of the GOT slot to fill */
    NjImportType type;
} NjImport;

typedef struct NjSymbol NjSymbol;

typedef struct NjImage {
    uint32_t base, size;              /* guest load base and mapped size */
    uint32_t init_array_ga, init_count;
    uint32_t fini_array_ga, fini_count;
    uint32_t exidx_ga, exidx_count;   /* PT_ARM_EXIDX (8-byte entries) */
    NjImport *imports;
    uint32_t import_count;
    uint32_t relative_count;          /* R_ARM_RELATIVE relocations applied */
    /* private */
    NjSymbol *syms;                   /* defined dynamic symbols sorted by address */
    uint32_t sym_count;
    void *by_name;                    /* hash index */
    char *strtab;                     /* owned copy of the dynamic string table */
} NjImage;

/* Map PT_LOAD segments at base, zero bss, apply R_ARM_RELATIVE, record imports. 0 on success. */
int nj_image_load(const char *path, uint32_t base, NjImage *img);
/* Guest address of a defined dynamic symbol (Thumb bit kept), 0 if absent. */
uint32_t nj_image_sym(const NjImage *img, const char *name);
/* Nearest defined symbol at or below ga (for diagnostics); offset receives ga - symbol. NULL if none. */
const char *nj_image_sym_near(const NjImage *img, uint32_t ga, uint32_t *offset);

#endif
