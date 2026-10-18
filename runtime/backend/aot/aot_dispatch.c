/* Dispatch for recompiled guest code: guest code pointers -> translated functions, HLE thunks or overrides. */
#include "nj/hle.h"
#include "nj/loader.h"
#include "recomp_rt.h"

static const NjImage *diag_img;
void nj_aot_set_image(const NjImage *img) { diag_img = img; }

static const char *where(uint32_t ga, uint32_t *off) {
    *off = 0;
    const char *s = diag_img ? nj_image_sym_near(diag_img, ga, off) : NULL;
    return s ? s : "?";
}

NjAotFn nj_aot_lookup(uint32_t ga) {
    uint32_t lo = 0, hi = nj_aot_count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (nj_aot_table[mid].addr < ga) lo = mid + 1;
        else hi = mid;
    }
    return lo < nj_aot_count && nj_aot_table[lo].addr == ga ? nj_aot_table[lo].fn : NULL;
}

void nj_aot_call_indirect(NjCtx *c, uint32_t target) {
    if (hle_is_thunk(target)) {
        hle_dispatch(c, target);
        return;
    }
    NjHleFn ov = hle_override_at(target);
    if (ov) {
        ov(c);
        return;
    }
    NjAotFn fn = (target & 1) ? NULL : nj_aot_lookup(target);
    if (!fn) {
        uint32_t off;
        const char *s = where(target & ~1u, &off);
        nj_fatal_ctx = c;
        nj_fatal("AOT dispatch miss: %s code at 0x%08x (%s+0x%x)", (target & 1) ? "Thumb" : "untranslated", target, s, off);
    }
    fn(c);
}

void (*nj_aot_import(const char *name))(NjCtx *) {
    NjHleFn fn = hle_fn_at(hle_thunk(name), NULL);
    if (!fn) nj_fatal("AOT: import %s has no host implementation", name);
    return fn;
}

void nj_aot_bad_return(NjCtx *c, uint32_t fn, uint32_t got, uint32_t want) {
    uint32_t off;
    const char *s = where(fn, &off);
    nj_fatal_ctx = c;
    nj_fatal("AOT: function 0x%08x (%s) returns to 0x%08x, entered with lr 0x%08x", fn, s, got, want);
}

void nj_aot_untranslated(uint32_t fn, const char *what) {
    uint32_t off;
    const char *s = where(fn, &off);
    nj_fatal("AOT: function 0x%08x (%s) was not translated: %s", fn, s, what);
}
