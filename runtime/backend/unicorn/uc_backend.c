/* Unicorn CPU backend: development host and differential-testing oracle (never shipped).
 *
 * Each host thread owns one NjThread with a guest stack slot. Every guest call nesting level has its
 * own uc_engine and NjCtx, so an HLE function that calls back into guest code (qsort comparators,
 * callbacks) never disturbs the registers of the outer call. All engines map the same host memory. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unicorn/unicorn.h>

#include "nj/backend.h"
#include "nj/hle.h"

#define MAX_DEPTH 16
#define MAX_INTERVALS 1024
#define MAX_OVERRIDES 256

typedef struct NjThread NjThread;

typedef struct {
    NjThread *t;
    int level;
} EngineInfo;

struct NjThread {
    uint32_t slot;
    uint32_t stack_top;
    int depth; /* active nj_call levels on this thread */
    uc_engine *eng[MAX_DEPTH];
    EngineInfo info[MAX_DEPTH];
    NjCtx ctx[MAX_DEPTH];
    /* last invalid memory access seen by a hook */
    int fault_type;
    uint64_t fault_addr;
};

static _Thread_local NjThread *self;
static int inited;
static const NjImage *diag_img;
static uint64_t iv_start[MAX_INTERVALS], iv_end[MAX_INTERVALS];
static int iv_count;
static uint32_t ov_addr[MAX_OVERRIDES];
static uint32_t ov_count;
static pthread_mutex_t slot_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t slot_used[NJ_STACK_COUNT];

static const int core_regs[17] = {
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,  UC_ARM_REG_R3,  UC_ARM_REG_R4,  UC_ARM_REG_R5,
    UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8,  UC_ARM_REG_R9,  UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,  UC_ARM_REG_CPSR};

void nj_backend_set_image(const NjImage *img) { diag_img = img; }

static void sync_in(uc_engine *uc, NjCtx *c) {
    uint32_t v[17];
    void *ptrs[17];
    for (int i = 0; i < 17; i++) ptrs[i] = &v[i];
    uc_reg_read_batch(uc, (int *)core_regs, ptrs, 17);
    memcpy(c->r, v, sizeof c->r);
    uint32_t cpsr = v[16];
    c->n = cpsr >> 31 & 1;
    c->z = cpsr >> 30 & 1;
    c->c = cpsr >> 29 & 1;
    c->v = cpsr >> 28 & 1;
    c->q = cpsr >> 27 & 1;
    c->ge = cpsr >> 16 & 0xf;
}

static void write_results(uc_engine *uc, const NjCtx *c) {
    uc_reg_write(uc, UC_ARM_REG_R0, &c->r[0]);
    uc_reg_write(uc, UC_ARM_REG_R1, &c->r[1]);
}

static void on_intr(uc_engine *uc, uint32_t intno, void *user) {
    EngineInfo *ei = user;
    NjCtx *c = &ei->t->ctx[ei->level];
    uint32_t pc;
    uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    uint32_t insn = nj_r32(pc - 4);
    if (intno != 2 || (insn & 0x0F000000u) != 0x0F000000u || !hle_is_thunk(pc - 4)) {
        sync_in(uc, c);
        nj_fatal_ctx = c;
        nj_fatal("unexpected guest exception %u at 0x%08x (insn %08x)", intno, pc - 4, insn);
    }
    uint32_t thunk_ga = NJ_THUNK_BASE + 8u * (insn & 0xFFFFFFu);
    sync_in(uc, c);
    nj_fatal_ctx = c;
    hle_dispatch(c, thunk_ga);
    write_results(uc, c);
}

static void on_override(uc_engine *uc, uint64_t address, uint32_t size, void *user) {
    (void)size;
    EngineInfo *ei = user;
    NjCtx *c = &ei->t->ctx[ei->level];
    NjHleFn fn = hle_override_at((uint32_t)address);
    if (!fn) return;
    sync_in(uc, c);
    nj_fatal_ctx = c;
    fn(c);
    write_results(uc, c);
    uint32_t lr = c->r[14];
    uc_reg_write(uc, UC_ARM_REG_PC, &lr); /* bit 0 selects Thumb, like bx lr */
}

static bool on_invalid(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, void *user) {
    (void)uc; (void)size; (void)value;
    EngineInfo *ei = user;
    ei->t->fault_type = type;
    ei->t->fault_addr = address;
    return false;
}

static const char *fault_name(int type) {
    switch (type) {
    case UC_MEM_READ_UNMAPPED: return "read unmapped";
    case UC_MEM_WRITE_UNMAPPED: return "write unmapped";
    case UC_MEM_FETCH_UNMAPPED: return "fetch unmapped";
    case UC_MEM_READ_PROT: return "read protected";
    case UC_MEM_WRITE_PROT: return "write protected";
    case UC_MEM_FETCH_PROT: return "fetch protected";
    default: return "none";
    }
}

static uc_engine *new_engine(NjThread *t, int level) {
    uc_engine *uc;
    if (uc_open(UC_ARCH_ARM, UC_MODE_ARM, &uc) != UC_ERR_OK) nj_fatal("uc_open failed");
    uc_ctl_set_cpu_model(uc, UC_CPU_ARM_CORTEX_A15);
    for (int i = 0; i < iv_count; i++) {
        uc_err e = uc_mem_map_ptr(uc, iv_start[i], (size_t)(iv_end[i] - iv_start[i]), UC_PROT_ALL, nj_mem + iv_start[i]);
        if (e != UC_ERR_OK)
            nj_fatal("uc_mem_map_ptr 0x%llx-0x%llx: %s", (unsigned long long)iv_start[i],
                     (unsigned long long)iv_end[i], uc_strerror(e));
    }
    uint32_t cpacr = 0x00F00000u, fpexc = 0x40000000u;
    uc_reg_write(uc, UC_ARM_REG_C1_C0_2, &cpacr);
    uc_reg_write(uc, UC_ARM_REG_FPEXC, &fpexc);
    EngineInfo *ei = &t->info[level];
    ei->t = t;
    ei->level = level;
    uc_hook h;
    uc_hook_add(uc, &h, UC_HOOK_INTR, (void *)on_intr, ei, 1, 0);
    uc_hook_add(uc, &h, UC_HOOK_MEM_INVALID, (void *)on_invalid, ei, 1, 0);
    for (uint32_t i = 0; i < ov_count; i++) uc_hook_add(uc, &h, UC_HOOK_CODE, (void *)on_override, ei, ov_addr[i], ov_addr[i]);
    return uc;
}

static NjThread *thread_self(void) {
    if (self) return self;
    if (!inited) nj_fatal("nj_backend_init() must run before guest code");
    pthread_mutex_lock(&slot_lock);
    uint32_t slot = 0;
    while (slot < NJ_STACK_COUNT && slot_used[slot]) slot++;
    if (slot == NJ_STACK_COUNT) { pthread_mutex_unlock(&slot_lock); nj_fatal("out of guest thread stacks"); }
    slot_used[slot] = 1;
    pthread_mutex_unlock(&slot_lock);
    NjThread *t = calloc(1, sizeof *t);
    t->slot = slot;
    t->stack_top = NJ_STACK_BASE + (slot + 1) * NJ_STACK_SIZE - 16;
    for (int i = 0; i < MAX_DEPTH; i++) {
        t->ctx[i].r[13] = t->stack_top;
        t->ctx[i].thread = (struct NjThread *)t;
    }
    self = t;
    return t;
}

static void thread_release(void) {
    NjThread *t = self;
    if (!t) return;
    for (int i = 0; i < MAX_DEPTH; i++) if (t->eng[i]) uc_close(t->eng[i]);
    pthread_mutex_lock(&slot_lock);
    slot_used[t->slot] = 0;
    pthread_mutex_unlock(&slot_lock);
    free(t);
    self = NULL;
}

int nj_backend_init(void) {
    if (inited) return 0;
    if (!nj_mem || !nj_mem_is_mapped(NJ_HLEDATA_BASE, NJ_HLEDATA_SIZE) ||
        !nj_mem_is_mapped(NJ_STACK_BASE, NJ_STACK_SIZE * NJ_STACK_COUNT) ||
        !nj_mem_is_mapped(NJ_THUNK_BASE, NJ_THUNK_SIZE) || !nj_mem_is_mapped(NJ_RETURN_TRAP, 4) ||
        !nj_mem_is_mapped(NJ_HEAP_BASE, 4096))
        return -1;
    nj_w32(NJ_RETURN_TRAP, 0xE1200070u); /* bkpt: never executed, emulation stops at this address */
    iv_count = nj_mem_intervals(iv_start, iv_end, MAX_INTERVALS);
    ov_count = hle_override_list(ov_addr, MAX_OVERRIDES);
    nj_mem_set_frozen(true);
    inited = 1;
    return 0;
}

NjCtx *nj_ctx(void) {
    NjThread *t = thread_self();
    return &t->ctx[t->depth > 0 ? t->depth - 1 : 0];
}

uint32_t nj_call(NjCtx *c, uint32_t fn, int argc, const uint32_t *argv) {
    NjThread *t = thread_self();
    int level = t->depth;
    if (level >= MAX_DEPTH) nj_fatal("guest call nesting deeper than %d", MAX_DEPTH);
    if (!t->eng[level]) t->eng[level] = new_engine(t, level);
    uc_engine *uc = t->eng[level];
    NjCtx *inner = &t->ctx[level];

    /* HLE calls made by the guest during this call sync their registers into ctx[level], which is the
     * caller's own context at depth 0; snapshot it so the contract (only r0/r1 change) holds. */
    NjCtx saved = *c;
    uint32_t sp = c->r[13];
    if (level > 0) sp -= 64; /* stay clear of the interrupted frame */
    sp &= ~7u;
    int nstack = argc > 4 ? argc - 4 : 0;
    sp = (sp - 4u * (uint32_t)nstack) & ~7u;
    for (int i = 4; i < argc; i++) nj_w32(sp + 4u * (uint32_t)(i - 4), argv[i]);
    for (int i = 0; i < 4; i++) {
        uint32_t v = i < argc ? argv[i] : 0;
        uc_reg_write(uc, core_regs[i], &v);
    }
    uint32_t lr = NJ_RETURN_TRAP;
    uc_reg_write(uc, UC_ARM_REG_SP, &sp);
    uc_reg_write(uc, UC_ARM_REG_LR, &lr);

    NjCtx *outer_fatal = nj_fatal_ctx;
    t->depth = level + 1;
    t->fault_type = 0;
    nj_fatal_ctx = inner;
    uc_err err = uc_emu_start(uc, fn, NJ_RETURN_TRAP, 0, 0);
    t->depth = level;
    if (err != UC_ERR_OK) {
        sync_in(uc, inner);
        uint32_t pc = inner->r[15], off = 0;
        const char *sym = diag_img ? nj_image_sym_near(diag_img, pc, &off) : NULL;
        nj_fatal_ctx = inner;
        nj_fatal("uc_emu_start: guest call to 0x%08x failed: %s; pc=0x%08x (%s+0x%x); fault: %s at 0x%08llx", fn,
                 uc_strerror(err), pc, sym ? sym : "?", off, fault_name(t->fault_type),
                 (unsigned long long)t->fault_addr);
    }
    uint32_t r0, r1;
    uc_reg_read(uc, UC_ARM_REG_R0, &r0);
    uc_reg_read(uc, UC_ARM_REG_R1, &r1);
    *c = saved;
    c->r[0] = r0;
    c->r[1] = r1;
    nj_fatal_ctx = outer_fatal;
    return r0;
}

typedef struct {
    uint32_t start, arg;
    void (*on_exit)(uint32_t, void *);
    void *ud;
} SpawnArgs;

static void *thread_main(void *p) {
    SpawnArgs a = *(SpawnArgs *)p;
    free(p);
    uint32_t ret = nj_call(nj_ctx(), a.start, 1, &a.arg);
    if (a.on_exit) a.on_exit(ret, a.ud);
    thread_release();
    return NULL;
}

int nj_spawn_thread(uint32_t start_fn, uint32_t arg, void (*on_exit)(uint32_t ret, void *ud), void *ud) {
    if (!inited) return -1;
    SpawnArgs *a = malloc(sizeof *a);
    *a = (SpawnArgs){start_fn, arg, on_exit, ud};
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 8u << 20); /* host stack for unicorn + HLE */
    pthread_t th;
    int rc = pthread_create(&th, &attr, thread_main, a);
    pthread_attr_destroy(&attr);
    if (rc) { free(a); return -1; }
    return 0;
}
