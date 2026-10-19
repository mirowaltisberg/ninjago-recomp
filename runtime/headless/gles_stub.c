/* Headless OpenGL ES 2 implementation for macOS boot tests: plausible answers (iOS-like strings and
 * limits), state tracking needed by the thunks, and statistics about what the game renders. */
#define GL_GLEXT_PROTOTYPES 1
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nj/gles.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK pthread_mutex_lock(&lock)
#define UNLOCK pthread_mutex_unlock(&lock)

/* ---- statistics ---- */

#define MAX_FUNCS 96
static struct { const char *name; unsigned long count; } calls[MAX_FUNCS];
static int ncalls;

static void count(const char *name) {
    LOCK;
    for (int i = 0; i < ncalls; i++) if (calls[i].name == name) { calls[i].count++; UNLOCK; return; }
    if (ncalls < MAX_FUNCS) { calls[ncalls].name = name; calls[ncalls].count = 1; ncalls++; }
    UNLOCK;
}
#define COUNT() count(__func__)

static unsigned frames, draws_frame, draws_last;
static unsigned long draws_total;

#define MAX_FMT 64
static struct { unsigned key, n; } cfmt[MAX_FMT], tfmt[MAX_FMT];
static int ncfmt, ntfmt;

static void bump(unsigned key, int compressed) {
    LOCK;
    typeof(cfmt[0]) *t = compressed ? cfmt : tfmt;
    int *n = compressed ? &ncfmt : &ntfmt;
    for (int i = 0; i < *n; i++) if (t[i].key == key) { t[i].n++; UNLOCK; return; }
    if (*n < MAX_FMT) { t[*n].key = key; t[*n].n = 1; (*n)++; }
    UNLOCK;
}

void gles_stub_frame(void) {
    LOCK;
    frames++;
    draws_last = draws_frame;
    draws_frame = 0;
    UNLOCK;
}
unsigned gles_stub_frames(void) { return frames; }
unsigned gles_stub_draws_last_frame(void) { return draws_last; }
unsigned gles_stub_compressed_format_count(unsigned f) {
    for (int i = 0; i < ncfmt; i++) if (cfmt[i].key == f) return cfmt[i].n;
    return 0;
}

unsigned gles_stub_teximage_count(unsigned format, unsigned type) {
    unsigned key = format << 16 | (type & 0xffff);
    for (int i = 0; i < ntfmt; i++) if (tfmt[i].key == key) return tfmt[i].n;
    return 0;
}

static const char *format_name(unsigned f) {
    switch (f) {
    case 0x8D64: return "ETC1_RGB8_OES";
    case 0x8C00: return "PVRTC_RGB_4BPP";
    case 0x8C01: return "PVRTC_RGB_2BPP";
    case 0x8C02: return "PVRTC_RGBA_4BPP";
    case 0x8C03: return "PVRTC_RGBA_2BPP";
    case 0x8C92: return "ATC_RGB_AMD";
    case 0x8C93: return "ATC_RGBA_EXPLICIT_ALPHA_AMD";
    case 0x87EE: return "ATC_RGBA_INTERPOLATED_ALPHA_AMD";
    case 0x83F0: return "DXT1_RGB";
    case 0x83F1: return "DXT1_RGBA";
    case 0x83F2: return "DXT3";
    case 0x83F3: return "DXT5";
    case 0x9274: return "ETC2_RGB8";
    case 0x9278: return "ETC2_RGBA8_EAC";
    default: return "?";
    }
}

void gles_stub_report(FILE *out) {
    LOCK;
    fprintf(out, "frames: %u, draw calls: %lu total, %u in the last frame\n", frames, draws_total, draws_last);
    fprintf(out, "compressed texture uploads by format:\n");
    for (int i = 0; i < ncfmt; i++) fprintf(out, "  0x%04x %-32s %u\n", cfmt[i].key, format_name(cfmt[i].key), cfmt[i].n);
    fprintf(out, "uncompressed texture uploads (format<<16 | type):\n");
    for (int i = 0; i < ntfmt; i++) fprintf(out, "  0x%08x %u\n", tfmt[i].key, tfmt[i].n);
    fprintf(out, "GL calls:\n");
    for (int i = 0; i < ncalls; i++) fprintf(out, "  %-28s %lu\n", calls[i].name, calls[i].count);
    UNLOCK;
}

/* ---- object names and bindings ---- */

static GLuint next_name = 1;
static GLuint array_buffer, vao, fbo;
#define MAX_VAO 4096
static GLuint element_buffer[MAX_VAO]; /* element binding is per-VAO state (0 = default VAO) */

static void gen(GLsizei n, GLuint *out) {
    LOCK;
    for (GLsizei i = 0; i < n; i++) out[i] = next_name++;
    UNLOCK;
}

static const void *last_attrib, *last_indices;
static char last_source[65536];

const char *gles_stub_last_shader_source(void) { return last_source; }
const void *gles_stub_last_attrib_pointer(void) { return last_attrib; }
const void *gles_stub_last_draw_indices(void) { return last_indices; }

/* stable locations per (program, name) */
#define MAX_LOC 4096
static struct { GLuint prog; char name[64]; GLint loc; } locs[MAX_LOC];
static int nlocs;

static GLint location(GLuint prog, const char *name) {
    LOCK;
    GLint next = 0;
    for (int i = 0; i < nlocs; i++) {
        if (locs[i].prog != prog) continue;
        if (!strncmp(locs[i].name, name, 63)) { GLint l = locs[i].loc; UNLOCK; return l; }
        next++;
    }
    GLint l = next;
    if (nlocs < MAX_LOC) {
        locs[nlocs].prog = prog;
        snprintf(locs[nlocs].name, 64, "%s", name);
        locs[nlocs].loc = l;
        nlocs++;
    }
    UNLOCK;
    return l;
}

/* ---- GL functions ---- */

void glActiveTexture(GLenum t) { (void)t; COUNT(); }
void glAttachShader(GLuint p, GLuint s) { (void)p; (void)s; COUNT(); }
void glBindBuffer(GLenum target, GLuint b) {
    COUNT();
    if (target == GL_ARRAY_BUFFER) array_buffer = b;
    else if (target == GL_ELEMENT_ARRAY_BUFFER && vao < MAX_VAO) element_buffer[vao] = b;
}
void glBindFramebuffer(GLenum t, GLuint f) { (void)t; COUNT(); fbo = f; }
void glBindRenderbuffer(GLenum t, GLuint r) { (void)t; (void)r; COUNT(); }
void glBindTexture(GLenum t, GLuint x) { (void)t; (void)x; COUNT(); }
void glBlendEquation(GLenum m) { (void)m; COUNT(); }
void glBlendFunc(GLenum s, GLenum d) { (void)s; (void)d; COUNT(); }
void glBufferData(GLenum t, GLsizeiptr n, const void *d, GLenum u) { (void)t; (void)n; (void)d; (void)u; COUNT(); }
void glClear(GLbitfield m) { (void)m; COUNT(); }
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { (void)r; (void)g; (void)b; (void)a; COUNT(); }
void glClearDepthf(GLfloat d) { (void)d; COUNT(); }
void glClearStencil(GLint s) { (void)s; COUNT(); }
void glCompileShader(GLuint s) { (void)s; COUNT(); }
void glCompressedTexImage2D(GLenum t, GLint l, GLenum f, GLsizei w, GLsizei h, GLint b, GLsizei n, const void *d) {
    (void)t; (void)l; (void)w; (void)h; (void)b; (void)n; (void)d;
    COUNT();
    bump(f, 1);
}
GLuint glCreateProgram(void) { COUNT(); GLuint n; gen(1, &n); return n; }
GLuint glCreateShader(GLenum t) { (void)t; COUNT(); GLuint n; gen(1, &n); return n; }
void glDeleteBuffers(GLsizei n, const GLuint *b) {
    COUNT();
    for (GLsizei i = 0; i < n; i++) {
        if (b[i] == array_buffer) array_buffer = 0;
        for (int v = 0; v < MAX_VAO; v++) if (element_buffer[v] == b[i]) element_buffer[v] = 0;
    }
}
void glDeleteFramebuffers(GLsizei n, const GLuint *f) { (void)n; (void)f; COUNT(); }
void glDeleteProgram(GLuint p) { (void)p; COUNT(); }
void glDeleteRenderbuffers(GLsizei n, const GLuint *r) { (void)n; (void)r; COUNT(); }
void glDeleteShader(GLuint s) { (void)s; COUNT(); }
void glDeleteTextures(GLsizei n, const GLuint *t) { (void)n; (void)t; COUNT(); }
void glDepthFunc(GLenum f) { (void)f; COUNT(); }
void glDepthMask(GLboolean m) { (void)m; COUNT(); }
void glDepthRangef(GLfloat n, GLfloat f) { (void)n; (void)f; COUNT(); }
void glDisable(GLenum c) { (void)c; COUNT(); }
void glDisableVertexAttribArray(GLuint i) { (void)i; COUNT(); }
void glDrawArrays(GLenum m, GLint f, GLsizei n) {
    (void)m; (void)f; (void)n;
    COUNT();
    LOCK; draws_frame++; draws_total++; UNLOCK;
}
void glDrawElements(GLenum m, GLsizei n, GLenum t, const void *idx) {
    (void)m; (void)n; (void)t;
    COUNT();
    last_indices = idx;
    LOCK; draws_frame++; draws_total++; UNLOCK;
}
void glEnable(GLenum c) { (void)c; COUNT(); }
void glEnableVertexAttribArray(GLuint i) { (void)i; COUNT(); }
void glFinish(void) { COUNT(); }
void glFlush(void) { COUNT(); }
void glFramebufferRenderbuffer(GLenum t, GLenum a, GLenum rt, GLuint r) { (void)t; (void)a; (void)rt; (void)r; COUNT(); }
void glFramebufferTexture2D(GLenum t, GLenum a, GLenum tt, GLuint x, GLint l) { (void)t; (void)a; (void)tt; (void)x; (void)l; COUNT(); }
void glFrontFace(GLenum m) { (void)m; COUNT(); }
void glGenBuffers(GLsizei n, GLuint *b) { COUNT(); gen(n, b); }
void glGenFramebuffers(GLsizei n, GLuint *f) { COUNT(); gen(n, f); }
void glGenRenderbuffers(GLsizei n, GLuint *r) { COUNT(); gen(n, r); }
void glGenTextures(GLsizei n, GLuint *t) { COUNT(); gen(n, t); }
void glGenVertexArraysOES(GLsizei n, GLuint *v) { COUNT(); gen(n, v); }
void glBindVertexArrayOES(GLuint v) { COUNT(); vao = v; }
void glDeleteVertexArraysOES(GLsizei n, const GLuint *v) {
    COUNT();
    for (GLsizei i = 0; i < n; i++) {
        if (v[i] == vao) vao = 0;
        if (v[i] < MAX_VAO) element_buffer[v[i]] = 0;
    }
}
void glGetActiveAttrib(GLuint p, GLuint i, GLsizei bs, GLsizei *len, GLint *sz, GLenum *ty, GLchar *name) {
    (void)p; (void)i; COUNT();
    if (len) *len = 0;
    if (sz) *sz = 0;
    if (ty) *ty = 0;
    if (name && bs > 0) name[0] = 0;
}
void glGetActiveUniform(GLuint p, GLuint i, GLsizei bs, GLsizei *len, GLint *sz, GLenum *ty, GLchar *name) {
    glGetActiveAttrib(p, i, bs, len, sz, ty, name);
}
GLint glGetAttribLocation(GLuint p, const GLchar *n) { COUNT(); return location(p, n); }
GLint glGetUniformLocation(GLuint p, const GLchar *n) { COUNT(); return location(p, n); }
void glGetBufferParameteriv(GLenum t, GLenum p, GLint *v) { (void)t; (void)p; COUNT(); *v = 0; }
GLenum glGetError(void) { COUNT(); return GL_NO_ERROR; }

GLboolean glIsEnabled(GLenum cap) { (void)cap; COUNT(); return GL_FALSE; }
void glGetIntegerv(GLenum p, GLint *v) {
    COUNT();
    switch (p) {
    case GL_ARRAY_BUFFER_BINDING: *v = (GLint)array_buffer; break;
    case GL_ELEMENT_ARRAY_BUFFER_BINDING: *v = vao < MAX_VAO ? (GLint)element_buffer[vao] : 0; break;
    case GL_FRAMEBUFFER_BINDING: *v = (GLint)fbo; break;
    case GL_MAX_TEXTURE_SIZE: case GL_MAX_CUBE_MAP_TEXTURE_SIZE: case GL_MAX_RENDERBUFFER_SIZE: *v = 4096; break;
    case GL_MAX_VERTEX_ATTRIBS: case GL_MAX_TEXTURE_IMAGE_UNITS: case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
    case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS: *v = 16; break;
    case GL_MAX_VERTEX_UNIFORM_VECTORS: *v = 256; break;
    case GL_MAX_FRAGMENT_UNIFORM_VECTORS: *v = 224; break;
    case GL_MAX_VARYING_VECTORS: *v = 15; break;
    case GL_VIEWPORT: v[0] = 0; v[1] = 0; v[2] = 1920; v[3] = 1080; break;
    case GL_NUM_COMPRESSED_TEXTURE_FORMATS: *v = 5; break;
    case GL_COMPRESSED_TEXTURE_FORMATS: v[0] = 0x8D64; v[1] = 0x8C00; v[2] = 0x8C01; v[3] = 0x8C02; v[4] = 0x8C03; break;
    case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: case GL_ALPHA_BITS: case GL_STENCIL_BITS: *v = 8; break;
    case GL_DEPTH_BITS: *v = 24; break;
    default: *v = 0; break;
    }
}

void glGetProgramInfoLog(GLuint p, GLsizei bs, GLsizei *len, GLchar *log) { (void)p; COUNT(); if (len) *len = 0; if (log && bs > 0) log[0] = 0; }
void glGetShaderInfoLog(GLuint s, GLsizei bs, GLsizei *len, GLchar *log) { (void)s; COUNT(); if (len) *len = 0; if (log && bs > 0) log[0] = 0; }
void glGetProgramiv(GLuint p, GLenum n, GLint *v) {
    (void)p; COUNT();
    *v = (n == GL_LINK_STATUS || n == GL_VALIDATE_STATUS) ? 1 : 0;
}
void glGetShaderiv(GLuint s, GLenum n, GLint *v) {
    (void)s; COUNT();
    *v = n == GL_COMPILE_STATUS ? 1 : n == GL_SHADER_SOURCE_LENGTH ? (GLint)strlen(last_source) + 1 : 0;
}

const GLubyte *glGetString(GLenum n) {
    COUNT();
    switch (n) {
    case GL_VENDOR: return (const GLubyte *)"Apple Inc.";
    case GL_RENDERER: return (const GLubyte *)"Apple A19 GPU";
    case GL_VERSION: return (const GLubyte *)"OpenGL ES 2.0 Apple A19 GPU";
    case GL_SHADING_LANGUAGE_VERSION: return (const GLubyte *)"OpenGL ES GLSL ES 1.00";
    case GL_EXTENSIONS:
        return (const GLubyte *)"GL_OES_compressed_ETC1_RGB8_texture GL_IMG_texture_compression_pvrtc "
                                "GL_OES_vertex_array_object GL_OES_depth24 GL_OES_rgb8_rgba8 "
                                "GL_OES_packed_depth_stencil GL_EXT_texture_filter_anisotropic";
    default: return NULL;
    }
}

void glLinkProgram(GLuint p) { (void)p; COUNT(); }
void glRenderbufferStorage(GLenum t, GLenum f, GLsizei w, GLsizei h) { (void)t; (void)f; (void)w; (void)h; COUNT(); }
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) { (void)x; (void)y; (void)w; (void)h; COUNT(); }

void glShaderSource(GLuint s, GLsizei count_, const GLchar *const *str, const GLint *len) {
    COUNT();
    size_t pos = 0;
    for (GLsizei i = 0; i < count_; i++) {
        size_t n = (len && len[i] >= 0) ? (size_t)len[i] : strlen(str[i]);
        if (pos + n >= sizeof last_source) n = sizeof last_source - 1 - pos;
        memcpy(last_source + pos, str[i], n);
        pos += n;
    }
    last_source[pos] = 0;
    (void)s;
}

void glStencilFunc(GLenum f, GLint r, GLuint m) { (void)f; (void)r; (void)m; COUNT(); }
void glStencilMask(GLuint m) { (void)m; COUNT(); }
void glStencilOp(GLenum a, GLenum b, GLenum c) { (void)a; (void)b; (void)c; COUNT(); }
void glTexImage2D(GLenum t, GLint l, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void *d) {
    (void)t; (void)l; (void)ifmt; (void)w; (void)h; (void)b; (void)d;
    COUNT();
    bump((unsigned)f << 16 | (ty & 0xffff), 0);
}
void glTexParameterf(GLenum t, GLenum p, GLfloat v) { (void)t; (void)p; (void)v; COUNT(); }
void glTexParameteri(GLenum t, GLenum p, GLint v) { (void)t; (void)p; (void)v; COUNT(); }
void glUniform1fv(GLint l, GLsizei n, const GLfloat *v) { (void)l; (void)n; (void)v; COUNT(); }
void glUniform1i(GLint l, GLint v) { (void)l; (void)v; COUNT(); }
void glUniform3fv(GLint l, GLsizei n, const GLfloat *v) { (void)l; (void)n; (void)v; COUNT(); }
void glUniform4fv(GLint l, GLsizei n, const GLfloat *v) { (void)l; (void)n; (void)v; COUNT(); }
void glUniformMatrix4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v) { (void)l; (void)n; (void)t; (void)v; COUNT(); }
void glUseProgram(GLuint p) { (void)p; COUNT(); }
void glVertexAttribPointer(GLuint i, GLint s, GLenum t, GLboolean n, GLsizei st, const void *p) {
    (void)i; (void)s; (void)t; (void)n; (void)st;
    COUNT();
    last_attrib = p;
}
static GLint last_viewport[4];
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    COUNT();
    last_viewport[0] = x;
    last_viewport[1] = y;
    last_viewport[2] = w;
    last_viewport[3] = h;
}
void gles_stub_last_viewport(int out[4]) { for (int i = 0; i < 4; i++) out[i] = last_viewport[i]; }
