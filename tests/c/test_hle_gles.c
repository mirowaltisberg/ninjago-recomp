#include "nj_test.h"

#include "nj/backend.h"
#include "nj/gles.h"
#include "nj/guest.h"
#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/hle_misc.h"

#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_EXTENSIONS 0x1F03
#define GL_RENDERER 0x1F01
#define GL_ETC1_RGB8_OES 0x8D64

static void setup(void) {
    static int done;
    if (done) return;
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_mem_map_fixed_regions(), 0);
    NJ_EQ_U(nj_heap_init(), 0);
    hle_gles_register();
    hle_egl_register();
    NJ_EQ_U(nj_backend_init(), 0);
    nj_egl_set_current_main();
    done = 1;
}

static uint32_t call(const char *name, const uint32_t *w, int n) {
    NjCtx *c = nj_ctx();
    uint32_t sp = c->r[13], nsp = (sp - 128) & ~7u;
    for (int i = 0; i < n; i++) {
        if (i < 4) c->r[i] = w[i];
        else nj_w32(nsp + 4u * (uint32_t)(i - 4), w[i]);
    }
    c->r[13] = nsp;
    hle_dispatch(c, hle_thunk(name));
    c->r[13] = sp;
    return c->r[0];
}

NJ_TEST(gl_shader_source_arrays) {
    setup();
    uint32_t strs = nj_malloc(8), lens = nj_malloc(8);
    nj_w32(strs, hle_guest_strdup("void main(){XXXX"));
    nj_w32(strs + 4, hle_guest_strdup("}"));
    nj_w32(lens, 12); /* only "void main(){" of the first string */
    nj_w32(lens + 4, (uint32_t)-1); /* negative: NUL-terminated */
    uint32_t w[] = {7, 2, strs, lens};
    call("glShaderSource", w, 4);
    NJ_EQ_STR(gles_stub_last_shader_source(), "void main(){}");
}

NJ_TEST(gl_attrib_client_pointer) {
    setup();
    uint32_t data = nj_malloc(64);
    uint32_t wb0[] = {GL_ARRAY_BUFFER, 0};
    call("glBindBuffer", wb0, 2);
    uint32_t w[] = {0, 3, 0x1406 /* GL_FLOAT */, 0, 12, data};
    call("glVertexAttribPointer", w, 6);
    NJ_CHECK(gles_stub_last_attrib_pointer() == nj_host(data));
    uint32_t wb5[] = {GL_ARRAY_BUFFER, 5};
    call("glBindBuffer", wb5, 2);
    uint32_t w2[] = {0, 3, 0x1406, 0, 12, 0x20};
    call("glVertexAttribPointer", w2, 6);
    NJ_CHECK(gles_stub_last_attrib_pointer() == (const void *)0x20);
}

NJ_TEST(gl_draw_elements_offset_or_pointer) {
    setup();
    uint32_t idx = nj_malloc(12);
    uint32_t wb[] = {GL_ELEMENT_ARRAY_BUFFER, 0};
    call("glBindBuffer", wb, 2);
    uint32_t w[] = {4 /* GL_TRIANGLES */, 6, 0x1403 /* GL_UNSIGNED_SHORT */, idx};
    call("glDrawElements", w, 4);
    NJ_CHECK(gles_stub_last_draw_indices() == nj_host(idx));
    uint32_t wb9[] = {GL_ELEMENT_ARRAY_BUFFER, 9};
    call("glBindBuffer", wb9, 2);
    uint32_t w2[] = {4, 6, 0x1403, 0x40};
    call("glDrawElements", w2, 4);
    NJ_CHECK(gles_stub_last_draw_indices() == (const void *)0x40);
}

NJ_TEST(gl_getstring_guest) {
    setup();
    uint32_t w[] = {GL_EXTENSIONS};
    uint32_t s = call("glGetString", w, 1);
    NJ_CHECK(s >= NJ_HEAP_BASE);
    NJ_CHECK(strstr(hle_cstr(s), "GL_OES_compressed_ETC1_RGB8_texture") != NULL);
    NJ_EQ_U(call("glGetString", w, 1), s); /* cached */
    uint32_t w2[] = {GL_RENDERER};
    NJ_CHECK(call("glGetString", w2, 1) != s);
}

NJ_TEST(gl_float_args_and_stack_args) {
    setup();
    float r = 0.25f;
    uint32_t rb;
    memcpy(&rb, &r, 4);
    uint32_t w[] = {rb, rb, rb, rb};
    call("glClearColor", w, 4); /* must not crash; floats arrive in r0-r3 */
    uint32_t data = nj_calloc(1, 8);
    uint32_t w2[] = {0x0DE1, 0, GL_ETC1_RGB8_OES, 4, 4, 0, 8, data};
    call("glCompressedTexImage2D", w2, 8);
    NJ_EQ_U(gles_stub_compressed_format_count(GL_ETC1_RGB8_OES), 1);
}

NJ_TEST(gl_frames_and_draw_counts) {
    setup();
    unsigned f0 = gles_stub_frames();
    uint32_t w[] = {4, 0, 3};
    call("glDrawArrays", w, 3);
    call("glDrawArrays", w, 3);
    gles_stub_frame();
    NJ_EQ_U(gles_stub_frames(), f0 + 1);
    NJ_CHECK(gles_stub_draws_last_frame() >= 2);
}

NJ_TEST(egl_flow) {
    setup();
    uint32_t dpy = call("eglGetCurrentDisplay", NULL, 0);
    NJ_CHECK(dpy != 0);
    uint32_t main_ctx = call("eglGetCurrentContext", NULL, 0);
    NJ_CHECK(main_ctx != 0);
    uint32_t cfgs = nj_malloc(4), num = nj_malloc(4);
    uint32_t w1[] = {dpy, 0, cfgs, 1, num};
    NJ_EQ_U(call("eglChooseConfig", w1, 5), 1);
    NJ_EQ_U(nj_r32(num), 1);
    NJ_EQ_U(nj_r32(cfgs), nj_egl_config_handle());
    uint32_t w2[] = {dpy, nj_r32(cfgs), main_ctx, 0};
    uint32_t ctx2 = call("eglCreateContext", w2, 4);
    NJ_CHECK(ctx2 != 0 && ctx2 != main_ctx);
    uint32_t w3[] = {dpy, nj_r32(cfgs), 0};
    uint32_t pb = call("eglCreatePbufferSurface", w3, 3);
    NJ_CHECK(pb != 0);
    uint32_t w4[] = {dpy, pb, pb, ctx2};
    NJ_EQ_U(call("eglMakeCurrent", w4, 4), 1);
    NJ_EQ_U(call("eglGetCurrentContext", NULL, 0), ctx2);
    uint32_t w5[] = {dpy, 0, 0, main_ctx};
    call("eglMakeCurrent", w5, 4);
    NJ_EQ_U(call("eglGetError", NULL, 0), 0x3000);
}

NJ_TEST(egl_get_proc_address) {
    setup();
    uint32_t w[] = {hle_guest_strdup("glBindVertexArrayOES")};
    NJ_CHECK(hle_is_thunk(call("eglGetProcAddress", w, 1)));
    uint32_t w2[] = {hle_guest_strdup("glProgramBinaryOES")};
    NJ_EQ_U(call("eglGetProcAddress", w2, 1), 0);
}

NJ_TEST(gl_atc_decoded_to_rgba) {
    setup();
    nj_gles_decode_atc = true;
    uint32_t data = nj_calloc(1, 8); /* one 4x4 ATC_RGB block */
    unsigned before_c = gles_stub_compressed_format_count(0x8C92);
    unsigned before_t = gles_stub_teximage_count(0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */);
    uint32_t w[] = {0x0DE1, 0, 0x8C92, 4, 4, 0, 8, data};
    call("glCompressedTexImage2D", w, 8);
    nj_gles_decode_atc = false;
    NJ_EQ_U(gles_stub_compressed_format_count(0x8C92), before_c);
    NJ_EQ_U(gles_stub_teximage_count(0x1908, 0x1401), before_t + 1);
}
