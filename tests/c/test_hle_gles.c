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

NJ_TEST(gl_shader_source_is_patched_for_apple) {
    setup();
    uint32_t strs = nj_malloc(4);
    nj_w32(strs, hle_guest_strdup("void main(){ normalize(vec3(0.0)); }"));
    uint32_t w[] = {8, 1, strs, 0};
    call("glShaderSource", w, 4);
    const char *src = gles_stub_last_shader_source();
    NJ_CHECK(strstr(src, "#define normalize nj_normalize") != NULL);
    NJ_CHECK(strstr(src, "void main(){ normalize(vec3(0.0)); }") != NULL);
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

#include <GLES2/gl2.h>

void nj_headless_set_default_framebuffer(unsigned fbo);

NJ_TEST(gl_default_framebuffer_mapping) {
    setup();
    nj_headless_set_default_framebuffer(77);
    uint32_t w0[] = {GL_FRAMEBUFFER, 0};
    call("glBindFramebuffer", w0, 2);
    GLint host = -1;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &host); /* what the host GL really has bound */
    NJ_EQ_U(host, 77);
    uint32_t out = nj_malloc(4);
    uint32_t wq[] = {GL_FRAMEBUFFER_BINDING, out};
    call("glGetIntegerv", wq, 2);
    NJ_EQ_U(nj_r32(out), 0); /* the guest sees "the default framebuffer" */
    uint32_t w5[] = {GL_FRAMEBUFFER, 5};
    call("glBindFramebuffer", w5, 2);
    call("glGetIntegerv", wq, 2);
    NJ_EQ_U(nj_r32(out), 5);
    nj_headless_set_default_framebuffer(0);
}

#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0

static void viewport_is(int x, int y, int w, int h) {
    int v[4];
    gles_stub_last_viewport(v);
    NJ_EQ_U(v[0], x);
    NJ_EQ_U(v[1], y);
    NJ_EQ_U(v[2], w);
    NJ_EQ_U(v[3], h);
}

NJ_TEST(gl_hud_overlay_scaled_after_scene_composite) {
    setup();
    nj_gles_set_hud_scale(0.5f, 1000, 500);
    nj_gles_frame_begin();
    uint32_t fb2[] = {GL_FRAMEBUFFER, 2}, rt[] = {GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 7, 0};
    call("glBindFramebuffer", fb2, 2);
    call("glFramebufferTexture2D", rt, 5); /* texture 7 holds the rendered scene */
    uint32_t fb0[] = {GL_FRAMEBUFFER, 0}, vp[] = {0, 0, 1000, 500}, unit[] = {GL_TEXTURE0};
    call("glBindFramebuffer", fb0, 2);
    call("glViewport", vp, 4);
    call("glActiveTexture", unit, 1);
    uint32_t scene[] = {GL_TEXTURE_2D, 7}, icon[] = {GL_TEXTURE_2D, 9}, strip[] = {5, 0, 4}, tris[] = {4, 0, 6};
    call("glBindTexture", scene, 2);
    call("glDrawArrays", strip, 3);
    viewport_is(0, 0, 1000, 500); /* the composited scene stays full screen */
    call("glBindTexture", icon, 2);
    call("glDrawArrays", tris, 3);
    viewport_is(250, 125, 500, 250); /* the overlay drawn after it shrinks towards the centre */
    nj_gles_frame_begin();
    NJ_CHECK(nj_gles_hud_scaled());
    call("glDrawArrays", tris, 3); /* a frame without a scene (menu) is left alone */
    viewport_is(0, 0, 1000, 500);
    nj_gles_frame_begin();
    NJ_CHECK(!nj_gles_hud_scaled());
    nj_gles_set_hud_scale(1.0f, 1000, 500);
}

static uint32_t guest_floats(const float *v, int n) {
    uint32_t ga = nj_malloc(4u * (uint32_t)n);
    for (int i = 0; i < n; i++) {
        uint32_t b;
        memcpy(&b, &v[i], 4);
        nj_w32(ga + 4u * (uint32_t)i, b);
    }
    return ga;
}

/* Full-screen overlay draws (menu backgrounds, fades) keep the full viewport so no border appears. */
NJ_TEST(gl_hud_full_screen_overlay_stays_full_size) {
    setup();
    nj_gles_set_hud_scale(0.5f, 1000, 500);
    nj_gles_frame_begin();
    uint32_t fb2[] = {GL_FRAMEBUFFER, 2}, rt[] = {GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 7, 0};
    call("glBindFramebuffer", fb2, 2);
    call("glFramebufferTexture2D", rt, 5);
    uint32_t fb0[] = {GL_FRAMEBUFFER, 0}, vp[] = {0, 0, 1000, 500}, unit[] = {GL_TEXTURE0};
    call("glBindFramebuffer", fb0, 2);
    call("glViewport", vp, 4);
    call("glActiveTexture", unit, 1);
    const uint32_t prog = 41;
    uint32_t qa[] = {prog, hle_guest_strdup("VERTEX_INPUT_PREANIM_Position")};
    uint32_t qu[] = {prog, hle_guest_strdup("ViewComposite")};
    uint32_t pos = call("glGetAttribLocation", qa, 2), vc = call("glGetUniformLocation", qu, 2);
    uint32_t use[] = {prog};
    call("glUseProgram", use, 1);
    static const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    uint32_t um[] = {vc, 1, 0, guest_floats(ident, 16)};
    call("glUniformMatrix4fv", um, 4);
    uint32_t nobuf[] = {GL_ARRAY_BUFFER, 0};
    call("glBindBuffer", nobuf, 2);
    static const float full[16] = {-1, -1, 0, 1, 1, -1, 0, 1, -1, 1, 0, 1, 1, 1, 0, 1};
    static const float small[16] = {-.2f, -.2f, 0, 1, .2f, -.2f, 0, 1, -.2f, .2f, 0, 1, .2f, .2f, 0, 1};
    uint32_t ap_full[] = {pos, 4, 0x1406 /* GL_FLOAT */, 0, 0, guest_floats(full, 16)};
    uint32_t ap_small[] = {pos, 4, 0x1406, 0, 0, guest_floats(small, 16)};
    uint32_t scene[] = {GL_TEXTURE_2D, 7}, icon[] = {GL_TEXTURE_2D, 9}, strip[] = {5, 0, 4};
    call("glVertexAttribPointer", ap_full, 6);
    call("glBindTexture", scene, 2);
    call("glDrawArrays", strip, 3); /* composite */
    call("glBindTexture", icon, 2);
    call("glDrawArrays", strip, 3); /* a full-screen overlay quad */
    viewport_is(0, 0, 1000, 500);
    call("glVertexAttribPointer", ap_small, 6);
    call("glDrawArrays", strip, 3); /* a button */
    viewport_is(250, 125, 500, 250);
    nj_gles_set_hud_scale(1.0f, 1000, 500);
}
