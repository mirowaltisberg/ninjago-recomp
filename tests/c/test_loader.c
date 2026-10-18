#include <unistd.h>

#include "nj_test.h"

#include "nj/guest.h"
#include "nj/heap.h"
#include "nj/loader.h"

/* Reference values taken independently with pyelftools from libLEGO_M1.so 1.05.2
 * (sha256 of the XAPK in input/SHA256SUMS). */
#define REF_FIRST_RELATIVE_VADDR 0x42c438u
#define REF_FIRST_RELATIVE_FILEVAL 0x40dda0u
#define REF_NATIVE_RENDER_VADDR 0x17331cu

static NjImage img;
static int loaded;

static void load(void) {
    if (access(NJ_GUEST_SO_PATH, R_OK) != 0) NJ_SKIP("guest library not present");
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_heap_init(), 0);
    if (!loaded) {
        NJ_EQ_U(nj_image_load(NJ_GUEST_SO_PATH, NJ_IMAGE_BASE, &img), 0);
        loaded = 1;
    }
}

static const NjImport *find_import(const char *name) {
    for (uint32_t i = 0; i < img.import_count; i++)
        if (strcmp(img.imports[i].name, name) == 0) return &img.imports[i];
    return NULL;
}

NJ_TEST(loader_counts) {
    load();
    NJ_EQ_U(img.base, NJ_IMAGE_BASE);
    NJ_EQ_U(img.import_count, 220);
    NJ_EQ_U(img.init_count, 400);
    NJ_EQ_U(img.relative_count, 16724);
    NJ_CHECK(img.size >= 0x549b7c);
}

NJ_TEST(loader_symbol) {
    load();
    uint32_t a = nj_image_sym(&img, "Java_com_wbgames_LEGOgame_GameGLSurfaceView_nativeRender");
    NJ_EQ_U(a, NJ_IMAGE_BASE + REF_NATIVE_RENDER_VADDR);
    NJ_EQ_U(nj_image_sym(&img, "no_such_symbol_xyz"), 0);
    uint32_t off = 99;
    NJ_EQ_STR(nj_image_sym_near(&img, a + 8, &off), "Java_com_wbgames_LEGOgame_GameGLSurfaceView_nativeRender");
    NJ_EQ_U(off, 8);
}

NJ_TEST(loader_relative_applied) {
    load();
    NJ_EQ_U(nj_r32(NJ_IMAGE_BASE + REF_FIRST_RELATIVE_VADDR), REF_FIRST_RELATIVE_FILEVAL + NJ_IMAGE_BASE);
}

NJ_TEST(loader_imports_named) {
    load();
    const NjImport *sf = find_import("__sF");
    NJ_CHECK(sf && sf->type == NJ_IMPORT_DATA);
    const NjImport *gl = find_import("glDrawElements");
    NJ_CHECK(gl && gl->type == NJ_IMPORT_FUNC);
    const NjImport *ux = find_import("__gnu_Unwind_Find_exidx");
    NJ_CHECK(ux && ux->type == NJ_IMPORT_FUNC);
    NJ_CHECK(gl->slot_ga >= img.base && gl->slot_ga < img.base + img.size);
}

NJ_TEST(loader_bss_zeroed_and_mapped) {
    load();
    uint32_t bss_end = NJ_IMAGE_BASE + 0x42c438u + 0x11d744u;
    NJ_CHECK(nj_mem_is_mapped(NJ_IMAGE_BASE, bss_end - NJ_IMAGE_BASE));
    NJ_EQ_U(nj_r32(bss_end - 4), 0);
}

NJ_TEST(loader_missing_file) {
    NJ_EQ_U(nj_mem_init(), 0);
    NjImage other;
    NJ_CHECK(nj_image_load("/nonexistent/lib.so", NJ_IMAGE_BASE, &other) != 0);
}

NJ_TEST(loader_writable_segment) {
    load();
    NJ_EQ_U(img.rw_ga, NJ_IMAGE_BASE + 0x42c438u);
    NJ_EQ_U(img.rw_size, 0x11d744u);
}
