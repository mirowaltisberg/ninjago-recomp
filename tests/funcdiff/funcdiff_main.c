/* Function-level differential test: picked self-contained guest functions (tools/recomp/funcdiff_pick.py) run
 * with random arguments under Unicorn (entry to the return trap) and as translated C (tools/recomp/emit.py);
 * every core register, flag, VFP register, the scratch and stack memory and the library's globals must agree. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <unicorn/unicorn.h>

#include "nj/guest.h"
#include "nj/loader.h"
#include "recomp_rt.h"

#define SCRATCH 0x00E00000u /* argument data */
#define STACK 0x00E10000u   /* guest stack, sp near the top */
#define REGION 0x10000u
#define SEEDS 32
#define MAX_INSNS 2000000

void nj_aot_set_image(const NjImage *img);

static uint64_t rng_state;
static uint64_t rnd(void) {
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * 0x2545F4914F6CDD1Dull;
}

static uint32_t pointer(void) { return SCRATCH + 0x1000 + ((uint32_t)rnd() % 0xC000 & ~7u); }

static uint32_t some_float(void) {
    float f = (float)((int)(rnd() % 20001) - 10000) / 64.0f;
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}

/* Arguments vary by seed: all pointers, pointer + small ints, small ints, floats (softfp), mixed. */
static void random_state(NjCtx *c, unsigned seed) {
    memset(c, 0, sizeof *c);
    for (int i = 0; i < 13; i++) c->r[i] = (uint32_t)rnd();
    for (int i = 0; i < 4; i++) {
        switch (seed % 5) {
        case 0: c->r[i] = pointer(); break;
        case 1: c->r[i] = i == 0 ? pointer() : (uint32_t)(rnd() % 64); break;
        case 2: c->r[i] = (uint32_t)(rnd() % 256); break;
        case 3: c->r[i] = some_float(); break;
        default: c->r[i] = (rnd() & 1) ? pointer() : some_float(); break;
        }
    }
    c->r[13] = STACK + REGION - 0x100 - ((uint32_t)rnd() % 0x100 & ~7u);
    c->r[14] = NJ_RETURN_TRAP;
    uint32_t f = (uint32_t)rnd();
    c->n = f & 1;
    c->z = f >> 1 & 1;
    c->c = f >> 2 & 1;
    c->v = f >> 3 & 1;
    for (int i = 0; i < 32; i++) c->d[i] = rnd();
}

static const int core[15] = {UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4,
                             UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9,
                             UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR};
static uint32_t cpsr_mode;

static void to_uc(uc_engine *uc, const NjCtx *c) {
    uint32_t cpsr = cpsr_mode | c->n << 31 | c->z << 30 | c->c << 29 | c->v << 28 | c->q << 27 | c->ge << 16;
    uc_reg_write(uc, UC_ARM_REG_CPSR, &cpsr);
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

static int differ(uint32_t fn, const char *name, const char *field, uint64_t u, uint64_t a) {
    printf("MISMATCH %08x %-40.40s %s uc=%llx aot=%llx\n", fn, name, field, (unsigned long long)u, (unsigned long long)a);
    return 1;
}

static uint32_t rw_ga, rw_size; /* the library's writable segment (globals) */

static int compare(uint32_t fn, const char *name, const NjCtx *u, const NjCtx *a, const uint8_t *umem, const uint8_t *amem,
                   const uint8_t *udata) {
    int bad = 0;
    char f[16];
    for (int i = 0; i < 15; i++)
        if (u->r[i] != a->r[i]) { snprintf(f, sizeof f, "r%d", i); bad += differ(fn, name, f, u->r[i], a->r[i]); }
    if (u->n != a->n || u->z != a->z || u->c != a->c || u->v != a->v || u->q != a->q || u->ge != a->ge)
        bad += differ(fn, name, "flags", u->n << 3 | u->z << 2 | u->c << 1 | u->v, a->n << 3 | a->z << 2 | a->c << 1 | a->v);
    for (int i = 0; i < 32; i++)
        if (u->d[i] != a->d[i]) { snprintf(f, sizeof f, "d%d", i); bad += differ(fn, name, f, u->d[i], a->d[i]); }
    if ((u->fpscr ^ a->fpscr) & 0xF0000000u) bad += differ(fn, name, "fpscr.nzcv", u->fpscr >> 28, a->fpscr >> 28);
    for (uint32_t i = 0; i < 2 * REGION && bad < 8; i += 4)
        if (memcmp(umem + i, amem + i, 4)) {
            uint32_t x, y;
            memcpy(&x, umem + i, 4);
            memcpy(&y, amem + i, 4);
            snprintf(f, sizeof f, "mem %08x", SCRATCH + i);
            bad += differ(fn, name, f, x, y);
        }
    const uint8_t *adata = nj_host(rw_ga);
    for (uint32_t i = 0; i + 4 <= rw_size && bad < 8; i += 4)
        if (memcmp(udata + i, adata + i, 4)) {
            uint32_t x, y;
            memcpy(&x, udata + i, 4);
            memcpy(&y, adata + i, 4);
            snprintf(f, sizeof f, "mem %08x", rw_ga + i);
            bad += differ(fn, name, f, x, y);
        }
    return bad;
}

int main(int argc, char **argv) {
    const char *only = argc > 1 ? argv[1] : NULL; /* optional: hex link address of one pick */
    FILE *picks = fopen(NJ_FUNCDIFF_PICKS, "r");
    if (!picks || access(NJ_GUEST_SO_PATH, R_OK) != 0) {
        printf("funcdiff: no picks (guest library not present)\n");
        return 77;
    }
    static NjImage img;
    if (nj_mem_init() || nj_image_load(NJ_GUEST_SO_PATH, NJ_IMAGE_BASE, &img) || nj_mem_map(SCRATCH, 2 * REGION, NJ_PROT_R | NJ_PROT_W))
        return 1;
    nj_aot_set_image(&img);
    rw_ga = img.rw_ga;
    rw_size = img.rw_size;
    uint8_t *data0 = malloc(rw_size), *udata = malloc(rw_size), *data = nj_host(rw_ga);
    uc_engine *uc;
    if (uc_open(UC_ARCH_ARM, UC_MODE_ARM, &uc) != UC_ERR_OK) return 1;
    uc_ctl_set_cpu_model(uc, UC_CPU_ARM_CORTEX_A15);
    if (uc_mem_map_ptr(uc, NJ_IMAGE_BASE, (img.size + 0xFFFu) & ~0xFFFu, UC_PROT_ALL, nj_host(NJ_IMAGE_BASE)) ||
        uc_mem_map_ptr(uc, SCRATCH, 2 * REGION, UC_PROT_ALL, nj_host(SCRATCH)))
        return 1;
    uint32_t cpacr = 0xF00000, fpexc = 0x40000000, cpsr;
    uc_reg_write(uc, UC_ARM_REG_C1_C0_2, &cpacr);
    uc_reg_write(uc, UC_ARM_REG_FPEXC, &fpexc);
    uc_reg_read(uc, UC_ARM_REG_CPSR, &cpsr);
    cpsr_mode = cpsr & 0x1F;

    static uint8_t mem0[2 * REGION], umem[2 * REGION];
    uint8_t *mem = nj_host(SCRATCH);
    unsigned funcs = 0, failed = 0, runs = 0, skipped = 0, missing = 0, untested = 0;
    char line[512];
    while (fgets(line, sizeof line, picks)) {
        char name[400] = "";
        unsigned long link = 0;
        if (sscanf(line, "%lx %399s", &link, name) < 1) continue;
        if (only && strtoul(only, NULL, 16) != link) continue;
        uint32_t ga = NJ_IMAGE_BASE + (uint32_t)link;
        NjAotFn fn = nj_aot_lookup(ga);
        funcs++;
        if (!fn) {
            printf("MISSING  %08x %s (no translated function)\n", ga, name);
            missing++;
            continue;
        }
        int bad = 0;
        unsigned fn_runs = runs;
        for (unsigned seed = 0; seed < SEEDS && !bad; seed++) {
            rng_state = 0x9E3779B97F4A7C15ull ^ (link << 16) ^ seed;
            rnd();
            NjCtx init, u, a;
            random_state(&init, seed);
            for (uint32_t j = 0; j < 2 * REGION; j += 8) { /* memory holds pointers and floats as well as noise */
                uint64_t r = rnd();
                uint32_t w[2] = {(uint32_t)r, (uint32_t)(r >> 32)};
                if (r % 3 == 1) { w[0] = pointer(); w[1] = pointer(); }
                if (r % 3 == 2) { w[0] = some_float(); w[1] = some_float(); }
                memcpy(mem + j, w, 8);
            }
            for (uint32_t j = 0; j < 4; j++) nj_w32(init.r[13] + 4 * j, (seed & 1) ? pointer() : some_float()); /* stack args */
            memcpy(mem0, mem, sizeof mem0);
            memcpy(data0, data, rw_size);
            to_uc(uc, &init);
            uc_err e = uc_emu_start(uc, ga, NJ_RETURN_TRAP, 0, MAX_INSNS);
            uint32_t pc;
            uc_reg_read(uc, UC_ARM_REG_PC, &pc);
            if (e != UC_ERR_OK || pc != NJ_RETURN_TRAP) { /* faulted or ran too long: inputs not meaningful */
                memcpy(data, data0, rw_size);
                skipped++;
                continue;
            }
            from_uc(uc, &u);
            memcpy(umem, mem, sizeof umem);
            memcpy(udata, data, rw_size);
            memcpy(mem, mem0, sizeof mem0);
            memcpy(data, data0, rw_size);
            a = init;
            fn(&a);
            bad = compare(ga, name, &u, &a, umem, mem, udata);
            runs++;
        }
        failed += bad != 0;
        if (runs == fn_runs) {
            printf("UNTESTED %08x %s (every seed faulted or ran too long)\n", ga, name);
            untested++;
        }
    }
    printf("funcdiff: %u functions, %u runs, %u skipped seeds, %u failing, %u missing, %u untested\n", funcs, runs,
           skipped, failed, missing, untested);
    uc_close(uc);
    return failed || missing || !runs ? 1 : 0;
}
