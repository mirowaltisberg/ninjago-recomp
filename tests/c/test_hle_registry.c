#include <unistd.h>

#include "nj_test.h"

#include "nj/guest.h"
#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/loader.h"

static void setup(void) {
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_mem_map_fixed_regions(), 0);
    NJ_EQ_U(nj_heap_init(), 0);
}

static void set42(NjCtx *c) { hle_ret(c, 42); }
static void set7(NjCtx *c) { hle_ret(c, 7); }

NJ_TEST(thunk_stable) {
    setup();
    uint32_t a = hle_thunk("memcpy_t1");
    NJ_EQ_U(hle_thunk("memcpy_t1"), a);
    NJ_CHECK(hle_is_thunk(a));
    NJ_EQ_U((a - NJ_THUNK_BASE) % 8, 0);
    uint32_t idx = (a - NJ_THUNK_BASE) / 8;
    NJ_EQ_U(nj_r32(a), 0xEF000000u | idx);
    NJ_EQ_U(nj_r32(a + 4), 0xE12FFF1Eu);
    NJ_EQ_STR(hle_thunk_name(idx), "memcpy_t1");
    NJ_CHECK(hle_thunk("other_t1") != a);
}

NJ_TEST(unimplemented_is_fatal) {
    setup();
    NjCtx c = {0};
    uint32_t t = hle_thunk("nope_not_implemented");
    const char *name = NULL;
    NJ_CHECK(hle_fn_at(t, &name) == NULL);
    NJ_EQ_STR(name, "nope_not_implemented");
    NJ_EXPECT_FATAL(hle_dispatch(&c, t), "nope_not_implemented");
}

NJ_TEST(dispatch_registered_and_late) {
    setup();
    NjCtx c = {0};
    hle_register("early_t2", set42);
    hle_dispatch(&c, hle_thunk("early_t2"));
    NJ_EQ_U(c.r[0], 42);
    uint32_t late = hle_thunk("late_t2"); /* thunk handed out before the implementation exists */
    hle_register("late_t2", set7);
    hle_dispatch(&c, late);
    NJ_EQ_U(c.r[0], 7);
}

NJ_TEST(args_softfp) {
    setup();
    NjCtx c = {0};
    double d = 2.5;
    uint64_t bits;
    memcpy(&bits, &d, 8);
    c.r[2] = (uint32_t)bits;
    c.r[3] = (uint32_t)(bits >> 32);
    NJ_CHECK(hle_arg_d(&c, 2) == 2.5);
    c.r[13] = NJ_STACK_BASE + 0x1000;
    nj_w32(c.r[13] + 0, 0x11111111);
    nj_w32(c.r[13] + 4, 0x22222222);
    NJ_EQ_U(hle_arg(&c, 4), 0x11111111);
    NJ_EQ_U(hle_arg(&c, 5), 0x22222222);
    NJ_EQ_U(hle_arg64(&c, 4), 0x2222222211111111ull);
    hle_ret_d(&c, -1.0);
    NJ_EQ_U(c.r[0], 0);
    NJ_EQ_U(c.r[1], 0xBFF00000u);
    hle_ret_f(&c, 1.0f);
    NJ_EQ_U(c.r[0], 0x3F800000u);
}

NJ_TEST(data_alloc_region_alignment) {
    setup();
    uint32_t a = hle_data_alloc(3, 1);
    uint32_t b = hle_data_alloc(16, 64);
    NJ_CHECK(a >= NJ_HLEDATA_BASE && a < NJ_HLEDATA_BASE + NJ_HLEDATA_SIZE);
    NJ_EQ_U(b % 64, 0);
    NJ_CHECK(b >= a + 3);
    uint32_t s = hle_guest_strdup("hello");
    NJ_EQ_STR(hle_cstr(s), "hello");
    NJ_CHECK(hle_cstr(0) == NULL);
}

NJ_TEST(bind_unregistered_data_fatal) {
    setup();
    NjImport imp = {"some_unregistered_data_t3", NJ_HEAP_BASE + 0x100, NJ_IMPORT_DATA};
    NjImage fake = {0};
    fake.imports = &imp;
    fake.import_count = 1;
    NJ_EXPECT_FATAL(hle_bind_imports(&fake), "some_unregistered_data_t3");
}

static NjImage img;
static int img_loaded;

static void load_real(void) {
    if (access(NJ_GUEST_SO_PATH, R_OK) != 0) NJ_SKIP("guest library not present");
    setup();
    if (!img_loaded) { NJ_EQ_U(nj_image_load(NJ_GUEST_SO_PATH, NJ_IMAGE_BASE, &img), 0); img_loaded = 1; }
}

NJ_TEST(bind_imports_real) {
    load_real();
    uint32_t dummy = hle_data_alloc(64, 4);
    for (uint32_t i = 0; i < img.import_count; i++)
        if (img.imports[i].type == NJ_IMPORT_DATA) hle_register_data(img.imports[i].name, dummy);
    hle_bind_imports(&img);
    for (uint32_t i = 0; i < img.import_count; i++) {
        uint32_t v = nj_r32(img.imports[i].slot_ga);
        if (img.imports[i].type == NJ_IMPORT_FUNC) {
            NJ_CHECK(hle_is_thunk(v));
            const char *name = NULL;
            hle_fn_at(v, &name);
            NJ_EQ_STR(name, img.imports[i].name);
        } else {
            NJ_EQ_U(v, dummy);
        }
    }
}

NJ_TEST(override_resolve) {
    load_real();
    const char *sym = "Java_com_wbgames_LEGOgame_GameGLSurfaceView_nativeRender";
    hle_register_override(sym, set42);
    hle_resolve_overrides(&img);
    NJ_CHECK(hle_override_at(nj_image_sym(&img, sym)) == set42);
    NJ_CHECK(hle_override_at(nj_image_sym(&img, sym) + 4) == NULL);
    hle_register_override("no_such_symbol_t4", set7);
    NJ_EXPECT_FATAL(hle_resolve_overrides(&img), "no_such_symbol_t4");
}

NJ_TEST(override_by_code_checks_bytes) {
    load_real();
    hle_register_override("no_such_symbol_t4", NULL); /* left behind by override_resolve */
    static const uint8_t acquire[] = {0x70, 0xb5, 0x04, 0x46}; /* push {r4-r6,lr}; mov r4,r0 (Thumb) */
    hle_register_override_code(0x3ef470, acquire, sizeof acquire, "guard_acquire_t", set42);
    hle_resolve_overrides(&img);
    NJ_CHECK(hle_override_at(NJ_IMAGE_BASE + 0x3ef470) == set42);
    static const uint8_t wrong[] = {0x00, 0x00, 0x00, 0x00};
    hle_register_override_code(0x3ef4d4, wrong, sizeof wrong, "guard_wrong_t", set7);
    NJ_EXPECT_FATAL(hle_resolve_overrides(&img), "guard_wrong_t");
    hle_register_override_code(0x3ef4d4, wrong, sizeof wrong, "guard_wrong_t", NULL);
    hle_register_override_code(0x3ef470, acquire, sizeof acquire, "guard_acquire_t", NULL);
}
