/* Instruction-level differential test: every generated case (tools/recomp/difftest_gen.py) runs on random
 * register, flag, VFP and memory states once under Unicorn (the original word, single-stepped in the loaded
 * image) and once as lifted C; registers, flags, VFP registers, FPSCR NZCV and scratch memory must agree. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <unicorn/unicorn.h>

#include "cases.h"
#include "nj/guest.h"
#include "nj/loader.h"

#define SCRATCH 0x00E00000u
#define SCRATCH_SIZE 0x10000u
#define SEEDS 64

static uint64_t rng_state;
static uint64_t rnd(void) { /* xorshift64* */
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * 0x2545F4914F6CDD1Dull;
}

/* Interesting VFP bit patterns: zeros, ones, infinities, NaNs, denormals, large and small values. */
static const uint32_t special_s[] = {0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x7f800000, 0xff800000,
                                     0x7fc00000, 0x7f800001, 0x00000001, 0x007fffff, 0x4f000000, 0xcf000000,
                                     0x4f800000, 0x3f000000, 0x40490fdb, 0x5f000000};
static const uint64_t special_d[] = {0, 0x8000000000000000ull, 0x3ff0000000000000ull, 0xbff0000000000000ull,
                                     0x7ff0000000000000ull, 0x7ff8000000000000ull, 0x0000000000000001ull,
                                     0x41e0000000000000ull, 0xc1e0000000000000ull, 0x41f0000000000000ull};

static uint64_t random_d(void) {
    uint64_t r = rnd();
    switch (r & 7) {
    case 0: return special_d[(r >> 8) % (sizeof special_d / sizeof special_d[0])];
    case 1: return (uint64_t)special_s[(r >> 8) & 15] | (uint64_t)special_s[(r >> 16) & 15] << 32;
    case 2: { /* two ordinary floats in [-1000, 1000) */
        float a = (float)((int)(r >> 16 & 0xfffff) - 0x80000) / 512.0f, b = (float)((int)(r >> 40 & 0xfffff) - 0x80000) / 512.0f;
        uint32_t x, y;
        memcpy(&x, &a, 4);
        memcpy(&y, &b, 4);
        return (uint64_t)x | (uint64_t)y << 32;
    }
    case 3: { /* an ordinary double */
        double a = (double)(int64_t)(r >> 20) / 4096.0 - 1e9;
        uint64_t x;
        memcpy(&x, &a, 8);
        return x;
    }
    default: return rnd();
    }
}

static void random_state(NjCtx *c, const NjCase *k) {
    memset(c, 0, sizeof *c);
    for (int i = 0; i < 15; i++) c->r[i] = (uint32_t)rnd();
    for (int i = 0; i < 15; i++) {
        if (k->mem_regs >> i & 1) c->r[i] = SCRATCH + 0x4000 + ((uint32_t)rnd() % 0x8000 & ~7u);
        if (k->idx_regs >> i & 1) c->r[i] = (uint32_t)(rnd() & 0x1f) << 2; /* small: indexes may be shifted by up to 7 */
    }
    c->r[13] = SCRATCH + 0x8000 + ((uint32_t)rnd() % 0x2000 & ~7u);
    uint32_t f = (uint32_t)rnd();
    c->n = f & 1;
    c->z = f >> 1 & 1;
    c->c = f >> 2 & 1;
    c->v = f >> 3 & 1;
    c->q = f >> 4 & 1;
    c->ge = f >> 5 & 15;
    for (int i = 0; i < 32; i++) c->d[i] = random_d();
    c->fpscr = (uint32_t)(rnd() & 0xF) << 28;
}

static const int core[15] = {UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4,
                             UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9,
                             UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR};
static uint32_t cpsr_mode;

static void to_uc(uc_engine *uc, const NjCtx *c) {
    uint32_t cpsr = cpsr_mode | c->n << 31 | c->z << 30 | c->c << 29 | c->v << 28 | c->q << 27 | c->ge << 16;
    uc_reg_write(uc, UC_ARM_REG_CPSR, &cpsr); /* same mode: the banked sp/lr stay in place */
    for (int i = 0; i < 15; i++) uc_reg_write(uc, core[i], &c->r[i]);
    for (int i = 0; i < 32; i++) uc_reg_write(uc, UC_ARM_REG_D0 + i, &c->d[i]);
    uc_reg_write(uc, UC_ARM_REG_FPSCR, &c->fpscr);
}

static void from_uc(uc_engine *uc, NjCtx *c) {
    for (int i = 0; i < 15; i++) uc_reg_read(uc, core[i], &c->r[i]);
    uint32_t cpsr;
    uc_reg_read(uc, UC_ARM_REG_CPSR, &cpsr);
    c->n = cpsr >> 31 & 1;
    c->z = cpsr >> 30 & 1;
    c->c = cpsr >> 29 & 1;
    c->v = cpsr >> 28 & 1;
    c->q = cpsr >> 27 & 1;
    c->ge = cpsr >> 16 & 15;
    for (int i = 0; i < 32; i++) uc_reg_read(uc, UC_ARM_REG_D0 + i, &c->d[i]);
    uc_reg_read(uc, UC_ARM_REG_FPSCR, &c->fpscr);
}

static int report(const NjCase *k, const char *field, uint64_t u, uint64_t a) {
    printf("MISMATCH %08x %08x %-32s %s uc=%llx aot=%llx\n", NJ_IMAGE_BASE + k->addr, k->word, k->text, field,
           (unsigned long long)u, (unsigned long long)a);
    return 1;
}

/* Returns the number of differing fields (each reported). */
static int compare(const NjCase *k, const NjCtx *u, const NjCtx *a, const uint8_t *umem, const uint8_t *amem) {
    int bad = 0;
    char name[16];
    for (int i = 0; i < 15; i++)
        if (u->r[i] != a->r[i]) { snprintf(name, sizeof name, "r%d", i); bad += report(k, name, u->r[i], a->r[i]); }
    if (u->n != a->n) bad += report(k, "n", u->n, a->n);
    if (u->z != a->z) bad += report(k, "z", u->z, a->z);
    if (u->c != a->c) bad += report(k, "c", u->c, a->c);
    if (u->v != a->v) bad += report(k, "v", u->v, a->v);
    if (u->q != a->q) bad += report(k, "q", u->q, a->q);
    if (u->ge != a->ge) bad += report(k, "ge", u->ge, a->ge);
    for (int i = 0; i < 32; i++)
        if (u->d[i] != a->d[i]) { snprintf(name, sizeof name, "d%d", i); bad += report(k, name, u->d[i], a->d[i]); }
    if ((u->fpscr ^ a->fpscr) & 0xF0000000u) bad += report(k, "fpscr.nzcv", u->fpscr >> 28, a->fpscr >> 28);
    for (uint32_t i = 0; i < SCRATCH_SIZE; i += 4)
        if (memcmp(umem + i, amem + i, 4)) {
            uint32_t x, y;
            memcpy(&x, umem + i, 4);
            memcpy(&y, amem + i, 4);
            snprintf(name, sizeof name, "mem+%04x", i);
            bad += report(k, name, x, y);
        }
    return bad;
}

int main(int argc, char **argv) {
    const char *only = argc > 1 ? argv[1] : NULL; /* optional: hex address of one case */
    if (nj_case_count == 0 || access(NJ_GUEST_SO_PATH, R_OK) != 0) {
        printf("difftest: no cases (guest library not present)\n");
        return 77;
    }
    static NjImage img;
    if (nj_mem_init() || nj_image_load(NJ_GUEST_SO_PATH, NJ_IMAGE_BASE, &img) || nj_mem_map(SCRATCH, SCRATCH_SIZE, NJ_PROT_R | NJ_PROT_W)) {
        fprintf(stderr, "difftest: cannot set up guest memory\n");
        return 1;
    }
    uc_engine *uc;
    if (uc_open(UC_ARCH_ARM, UC_MODE_ARM, &uc) != UC_ERR_OK) return 1;
    uc_ctl_set_cpu_model(uc, UC_CPU_ARM_CORTEX_A15);
    uint32_t img_size = (img.size + 0xFFFu) & ~0xFFFu;
    if (uc_mem_map_ptr(uc, NJ_IMAGE_BASE, img_size, UC_PROT_ALL, nj_host(NJ_IMAGE_BASE)) ||
        uc_mem_map_ptr(uc, SCRATCH, SCRATCH_SIZE, UC_PROT_ALL, nj_host(SCRATCH))) {
        fprintf(stderr, "difftest: uc_mem_map_ptr failed\n");
        return 1;
    }
    uint32_t cpacr = 0xF00000, fpexc = 0x40000000, cpsr;
    uc_reg_write(uc, UC_ARM_REG_C1_C0_2, &cpacr);
    uc_reg_write(uc, UC_ARM_REG_FPEXC, &fpexc);
    uc_reg_read(uc, UC_ARM_REG_CPSR, &cpsr);
    cpsr_mode = cpsr & 0x1F;

    static uint8_t mem0[SCRATCH_SIZE], umem[SCRATCH_SIZE];
    uint8_t *scratch = nj_host(SCRATCH);
    unsigned failed_cases = 0, uc_errors = 0, runs = 0;
    for (unsigned i = 0; i < nj_case_count; i++) {
        const NjCase *k = &nj_cases[i];
        if (only && strtoul(only, NULL, 16) != NJ_IMAGE_BASE + k->addr) continue;
        int bad = 0;
        for (unsigned seed = 0; seed < SEEDS && !bad; seed++) {
            rng_state = 0x9E3779B97F4A7C15ull ^ ((uint64_t)i << 20) ^ seed;
            rnd();
            NjCtx init, u, a;
            random_state(&init, k);
            for (uint32_t j = 0; j < SCRATCH_SIZE; j += 8) {
                uint64_t r = rnd();
                memcpy(scratch + j, &r, 8);
            }
            memcpy(mem0, scratch, SCRATCH_SIZE);
            to_uc(uc, &init);
            uint64_t pc = NJ_IMAGE_BASE + k->addr;
            uc_err e = uc_emu_start(uc, pc, pc + 4, 0, 1);
            if (e != UC_ERR_OK) {
                printf("UCERR    %08x %08x %-32s %s\n", (uint32_t)pc, k->word, k->text, uc_strerror(e));
                uc_errors++;
                bad = 1;
                break;
            }
            from_uc(uc, &u);
            memcpy(umem, scratch, SCRATCH_SIZE);
            memcpy(scratch, mem0, SCRATCH_SIZE);
            a = init;
            k->fn(&a);
            bad = compare(k, &u, &a, umem, scratch);
            runs++;
        }
        failed_cases += bad != 0;
    }
    printf("difftest: %u cases, %u runs, %u failing cases (%u Unicorn errors)\n", nj_case_count, runs, failed_cases,
           uc_errors);
    uc_close(uc);
    return failed_cases ? 1 : 0;
}
