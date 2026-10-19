/* ELF32 little-endian ARM shared-object loader (Darwin has no <elf.h>, so the structures are local). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nj/guest.h"
#include "nj/loader.h"

typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
} Elf32_Phdr;

typedef struct {
    uint32_t st_name, st_value, st_size;
    uint8_t st_info, st_other;
    uint16_t st_shndx;
} Elf32_Sym;

enum { PT_LOAD = 1, PT_DYNAMIC = 2, PT_ARM_EXIDX = 0x70000001 };
enum { PF_W = 2 };
enum {
    DT_NULL = 0, DT_PLTRELSZ = 2, DT_HASH = 4, DT_STRTAB = 5, DT_SYMTAB = 6, DT_STRSZ = 10,
    DT_REL = 17, DT_RELSZ = 18, DT_JMPREL = 23, DT_INIT_ARRAY = 25, DT_FINI_ARRAY = 26,
    DT_INIT_ARRAYSZ = 27, DT_FINI_ARRAYSZ = 28
};
enum { R_ARM_NONE = 0, R_ARM_ABS32 = 2, R_ARM_GLOB_DAT = 21, R_ARM_JUMP_SLOT = 22, R_ARM_RELATIVE = 23 };
enum { STT_OBJECT = 1, STT_FUNC = 2 };

struct NjSymbol {
    const char *name;
    uint32_t value; /* guest address, Thumb bit kept */
};

typedef struct {
    uint32_t cap;
    NjSymbol **slots;
} NameIndex;

static uint32_t fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

static int sym_addr_cmp(const void *a, const void *b) {
    uint32_t x = ((const NjSymbol *)a)->value & ~1u, y = ((const NjSymbol *)b)->value & ~1u;
    return (x > y) - (x < y);
}

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *len = buf ? (size_t)n : 0;
    return buf;
}

static int load_segments(const uint8_t *file, size_t len, const Elf32_Phdr *ph, int phnum, uint32_t base,
                         NjImage *img) {
    uint32_t lo = UINT32_MAX, hi = 0;
    for (int i = 0; i < phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_offset + ph[i].p_filesz > len || ph[i].p_filesz > ph[i].p_memsz) return -1;
        if (ph[i].p_vaddr < lo) lo = ph[i].p_vaddr;
        if (ph[i].p_vaddr + ph[i].p_memsz > hi) hi = ph[i].p_vaddr + ph[i].p_memsz;
    }
    if (lo != 0 || hi == 0) return -1;
    img->base = base;
    img->size = hi;
    for (int i = 0; i < phnum; i++)
        if (ph[i].p_type == PT_LOAD && (ph[i].p_flags & PF_W)) {
            img->rw_ga = base + ph[i].p_vaddr;
            img->rw_size = ph[i].p_memsz;
        }
    if (nj_mem_map(base, hi, NJ_PROT_R | NJ_PROT_W)) return -1;
    for (int i = 0; i < phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        uint8_t *dst = nj_host(base + ph[i].p_vaddr);
        memcpy(dst, file + ph[i].p_offset, ph[i].p_filesz);
        memset(dst + ph[i].p_filesz, 0, ph[i].p_memsz - ph[i].p_filesz);
    }
    return 0;
}

/* Text pages strictly below the first writable segment's page become read-only after relocation. */
static void protect_text(const Elf32_Phdr *ph, int phnum, uint32_t base) {
    uint32_t data_lo = UINT32_MAX;
    for (int i = 0; i < phnum; i++)
        if (ph[i].p_type == PT_LOAD && (ph[i].p_flags & PF_W) && ph[i].p_vaddr < data_lo) data_lo = ph[i].p_vaddr;
    for (int i = 0; i < phnum; i++) {
        if (ph[i].p_type != PT_LOAD || (ph[i].p_flags & PF_W)) continue;
        uint32_t end = ph[i].p_vaddr + ph[i].p_memsz;
        uint32_t page = 0x4000; /* host page on Apple silicon; read-only only where no data shares the page */
        uint32_t limit = data_lo == UINT32_MAX ? end : (data_lo & ~(page - 1));
        if (end > limit) end = limit;
        end &= ~(page - 1);
        if (end > ph[i].p_vaddr) nj_mem_map(base + ph[i].p_vaddr, end - ph[i].p_vaddr, NJ_PROT_R | NJ_PROT_X);
    }
}

static int build_symbols(NjImage *img, const Elf32_Sym *syms, uint32_t nsyms, const char *strtab, uint32_t strsz) {
    img->strtab = malloc(strsz);
    if (!img->strtab) return -1;
    memcpy(img->strtab, strtab, strsz);
    img->syms = calloc(nsyms ? nsyms : 1, sizeof(NjSymbol));
    img->sym_count = 0;
    for (uint32_t i = 1; i < nsyms; i++) {
        if (syms[i].st_shndx == 0 || syms[i].st_name >= strsz) continue;
        const char *name = img->strtab + syms[i].st_name;
        if (!*name) continue;
        img->syms[img->sym_count].name = name;
        img->syms[img->sym_count].value = img->base + syms[i].st_value;
        img->sym_count++;
    }
    qsort(img->syms, img->sym_count, sizeof(NjSymbol), sym_addr_cmp);
    NameIndex *idx = malloc(sizeof *idx);
    idx->cap = 1;
    while (idx->cap < img->sym_count * 2) idx->cap <<= 1;
    idx->slots = calloc(idx->cap, sizeof(NjSymbol *));
    for (uint32_t i = 0; i < img->sym_count; i++) {
        uint32_t h = fnv1a(img->syms[i].name) & (idx->cap - 1);
        while (idx->slots[h]) h = (h + 1) & (idx->cap - 1);
        idx->slots[h] = &img->syms[i];
    }
    img->by_name = idx;
    return 0;
}

static int apply_relocs(NjImage *img, uint32_t rel_va, uint32_t rel_size, const Elf32_Sym *syms, uint32_t nsyms,
                        uint32_t *nimports) {
    for (uint32_t off = 0; off + 8 <= rel_size; off += 8) {
        uint32_t r_offset = nj_r32(img->base + rel_va + off);
        uint32_t r_info = nj_r32(img->base + rel_va + off + 4);
        uint32_t type = r_info & 0xff, si = r_info >> 8;
        uint32_t slot = img->base + r_offset;
        if (type == R_ARM_NONE) continue;
        if (type == R_ARM_RELATIVE) {
            nj_w32(slot, nj_r32(slot) + img->base);
            img->relative_count++;
            continue;
        }
        if (si >= nsyms) return -1;
        const Elf32_Sym *s = &syms[si];
        if (type != R_ARM_ABS32 && type != R_ARM_GLOB_DAT && type != R_ARM_JUMP_SLOT) {
            fprintf(stderr, "nj_image_load: unsupported relocation type %u at 0x%x\n", type, r_offset);
            return -1;
        }
        if (s->st_shndx != 0) { /* defined in the image itself */
            uint32_t value = img->base + s->st_value;
            nj_w32(slot, type == R_ARM_ABS32 ? nj_r32(slot) + value : value);
            continue;
        }
        if (type == R_ARM_ABS32) {
            fprintf(stderr, "nj_image_load: ABS32 against undefined symbol at 0x%x\n", r_offset);
            return -1;
        }
        NjImport *imp = &img->imports[(*nimports)++];
        imp->name = img->strtab + s->st_name;
        imp->slot_ga = slot;
        imp->type = (type == R_ARM_GLOB_DAT && (s->st_info & 0xf) != STT_FUNC) ? NJ_IMPORT_DATA : NJ_IMPORT_FUNC;
    }
    return 0;
}

int nj_image_load(const char *path, uint32_t base, NjImage *img) {
    memset(img, 0, sizeof *img);
    size_t len;
    uint8_t *file = read_file(path, &len);
    if (!file) return -1;
    int rc = -1;
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)file;
    if (len < sizeof *eh || memcmp(eh->e_ident, "\x7f" "ELF", 4) || eh->e_ident[4] != 1 || eh->e_ident[5] != 1 ||
        eh->e_machine != 40 || eh->e_type != 3 || eh->e_phoff + (uint64_t)eh->e_phnum * sizeof(Elf32_Phdr) > len)
        goto out;
    const Elf32_Phdr *ph = (const Elf32_Phdr *)(file + eh->e_phoff);
    if (load_segments(file, len, ph, eh->e_phnum, base, img)) goto out;

    uint32_t dyn_va = 0, dyn_sz = 0;
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_DYNAMIC) { dyn_va = ph[i].p_vaddr; dyn_sz = ph[i].p_memsz; }
        if (ph[i].p_type == PT_ARM_EXIDX) { img->exidx_ga = base + ph[i].p_vaddr; img->exidx_count = ph[i].p_memsz / 8; }
    }
    if (!dyn_va) goto out;
    uint32_t d[32] = {0}, init_sz = 0, fini_sz = 0;
    for (uint32_t off = 0; off + 8 <= dyn_sz; off += 8) {
        uint32_t tag = nj_r32(base + dyn_va + off), val = nj_r32(base + dyn_va + off + 4);
        if (tag == DT_NULL) break;
        if (tag < 32) d[tag] = val;
        if (tag == DT_INIT_ARRAYSZ) init_sz = val;
        if (tag == DT_FINI_ARRAYSZ) fini_sz = val;
    }
    if (!d[DT_HASH] || !d[DT_SYMTAB] || !d[DT_STRTAB]) goto out;
    uint32_t nsyms = nj_r32(base + d[DT_HASH] + 4); /* nchain */
    const Elf32_Sym *syms = nj_host(base + d[DT_SYMTAB]);
    const char *strtab = nj_host(base + d[DT_STRTAB]);
    if (build_symbols(img, syms, nsyms, strtab, d[DT_STRSZ])) goto out;

    uint32_t max_imports = (d[DT_RELSZ] + d[DT_PLTRELSZ]) / 8;
    img->imports = calloc(max_imports ? max_imports : 1, sizeof(NjImport));
    uint32_t nimports = 0;
    if (d[DT_REL] && apply_relocs(img, d[DT_REL], d[DT_RELSZ], syms, nsyms, &nimports)) goto out;
    if (d[DT_JMPREL] && apply_relocs(img, d[DT_JMPREL], d[DT_PLTRELSZ], syms, nsyms, &nimports)) goto out;
    img->import_count = nimports;
    if (d[DT_INIT_ARRAY]) { img->init_array_ga = base + d[DT_INIT_ARRAY]; img->init_count = init_sz / 4; }
    if (d[DT_FINI_ARRAY]) { img->fini_array_ga = base + d[DT_FINI_ARRAY]; img->fini_count = fini_sz / 4; }
    protect_text(ph, eh->e_phnum, base);
    rc = 0;
out:
    free(file);
    return rc;
}

uint32_t nj_image_sym(const NjImage *img, const char *name) {
    const NameIndex *idx = img->by_name;
    if (!idx) return 0;
    uint32_t h = fnv1a(name) & (idx->cap - 1);
    while (idx->slots[h]) {
        if (strcmp(idx->slots[h]->name, name) == 0) return idx->slots[h]->value;
        h = (h + 1) & (idx->cap - 1);
    }
    return 0;
}

const char *nj_image_sym_near(const NjImage *img, uint32_t ga, uint32_t *offset) {
    uint32_t lo = 0, hi = img->sym_count;
    while (lo < hi) { /* first symbol with address > ga */
        uint32_t mid = (lo + hi) / 2;
        if ((img->syms[mid].value & ~1u) <= ga) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return NULL;
    const NjSymbol *s = &img->syms[lo - 1];
    if (offset) *offset = ga - (s->value & ~1u);
    return s->name;
}
