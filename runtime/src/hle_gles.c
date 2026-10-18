/* OpenGL ES 2 thunks: unpack softfp guest arguments, translate guest pointers, call the host GL
 * (the OpenGLES framework on iOS, runtime/headless/gles_stub.c on macOS). */
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif
#include <pthread.h>
#include <stdlib.h>

#include "nj/gles.h"
#include "nj/hle.h"

#define U(i) hle_arg(c, i)
#define S(i) ((GLint)hle_arg(c, i))
#define F(i) hle_arg_f(c, i)
#define P(i) ((void *)(hle_arg(c, i) ? nj_host(hle_arg(c, i)) : NULL))
#define B(i) ((GLboolean)(hle_arg(c, i) & 0xff))

#define V0(fn) static void h_##fn(NjCtx *c) { (void)c; fn(); }
#define THUNK(fn, ...) static void h_##fn(NjCtx *c) { fn(__VA_ARGS__); }
#define RET(fn, ...) static void h_##fn(NjCtx *c) { hle_ret(c, (uint32_t)fn(__VA_ARGS__)); }

THUNK(glActiveTexture, U(0))
THUNK(glAttachShader, U(0), U(1))
THUNK(glBindBuffer, U(0), U(1))
THUNK(glBindFramebuffer, U(0), U(1))
THUNK(glBindRenderbuffer, U(0), U(1))
THUNK(glBindTexture, U(0), U(1))
THUNK(glBlendEquation, U(0))
THUNK(glBlendFunc, U(0), U(1))
THUNK(glBufferData, U(0), (GLsizeiptr)U(1), P(2), U(3))
THUNK(glClear, U(0))
THUNK(glClearColor, F(0), F(1), F(2), F(3))
THUNK(glClearDepthf, F(0))
THUNK(glClearStencil, S(0))
THUNK(glCompileShader, U(0))
THUNK(glCompressedTexImage2D, U(0), S(1), U(2), S(3), S(4), S(5), S(6), P(7))
RET(glCreateProgram)
RET(glCreateShader, U(0))
THUNK(glDeleteBuffers, S(0), P(1))
THUNK(glDeleteFramebuffers, S(0), P(1))
THUNK(glDeleteProgram, U(0))
THUNK(glDeleteRenderbuffers, S(0), P(1))
THUNK(glDeleteShader, U(0))
THUNK(glDeleteTextures, S(0), P(1))
THUNK(glDepthFunc, U(0))
THUNK(glDepthMask, B(0))
THUNK(glDepthRangef, F(0), F(1))
THUNK(glDisable, U(0))
THUNK(glDisableVertexAttribArray, U(0))
THUNK(glDrawArrays, U(0), S(1), S(2))
THUNK(glEnable, U(0))
THUNK(glEnableVertexAttribArray, U(0))
V0(glFinish)
V0(glFlush)
THUNK(glFramebufferRenderbuffer, U(0), U(1), U(2), U(3))
THUNK(glFramebufferTexture2D, U(0), U(1), U(2), U(3), S(4))
THUNK(glFrontFace, U(0))
THUNK(glGenBuffers, S(0), P(1))
THUNK(glGenFramebuffers, S(0), P(1))
THUNK(glGenRenderbuffers, S(0), P(1))
THUNK(glGenTextures, S(0), P(1))
THUNK(glGetActiveAttrib, U(0), U(1), S(2), P(3), P(4), P(5), P(6))
THUNK(glGetActiveUniform, U(0), U(1), S(2), P(3), P(4), P(5), P(6))
RET(glGetAttribLocation, U(0), P(1))
THUNK(glGetBufferParameteriv, U(0), U(1), P(2))
RET(glGetError)
THUNK(glGetIntegerv, U(0), P(1))
THUNK(glGetProgramInfoLog, U(0), S(1), P(2), P(3))
THUNK(glGetProgramiv, U(0), U(1), P(2))
THUNK(glGetShaderInfoLog, U(0), S(1), P(2), P(3))
THUNK(glGetShaderiv, U(0), U(1), P(2))
RET(glGetUniformLocation, U(0), P(1))
THUNK(glLinkProgram, U(0))
THUNK(glRenderbufferStorage, U(0), U(1), S(2), S(3))
THUNK(glScissor, S(0), S(1), S(2), S(3))
THUNK(glStencilFunc, U(0), S(1), U(2))
THUNK(glStencilMask, U(0))
THUNK(glStencilOp, U(0), U(1), U(2))
THUNK(glTexImage2D, U(0), S(1), S(2), S(3), S(4), S(5), U(6), U(7), P(8))
THUNK(glTexParameterf, U(0), U(1), F(2))
THUNK(glTexParameteri, U(0), U(1), S(2))
THUNK(glUniform1fv, S(0), S(1), P(2))
THUNK(glUniform1i, S(0), S(1))
THUNK(glUniform3fv, S(0), S(1), P(2))
THUNK(glUniform4fv, S(0), S(1), P(2))
THUNK(glUniformMatrix4fv, S(0), S(1), B(2), P(3))
THUNK(glUseProgram, U(0))
THUNK(glViewport, S(0), S(1), S(2), S(3))
THUNK(glGenVertexArraysOES, S(0), P(1))
THUNK(glBindVertexArrayOES, U(0))
THUNK(glDeleteVertexArraysOES, S(0), P(1))

/* Last argument is a buffer offset when a buffer is bound, otherwise a client-memory pointer. */
static const void *offset_or_pointer(NjCtx *c, int word, GLenum binding) {
    GLint bound = 0;
    glGetIntegerv(binding, &bound);
    uint32_t v = hle_arg(c, word);
    return bound ? (const void *)(uintptr_t)v : (v ? nj_host(v) : NULL);
}

static void h_glVertexAttribPointer(NjCtx *c) {
    glVertexAttribPointer(U(0), S(1), U(2), B(3), S(4), offset_or_pointer(c, 5, GL_ARRAY_BUFFER_BINDING));
}

static void h_glDrawElements(NjCtx *c) {
    glDrawElements(U(0), S(1), U(2), offset_or_pointer(c, 3, GL_ELEMENT_ARRAY_BUFFER_BINDING));
}

static void h_glShaderSource(NjCtx *c) {
    GLsizei count = S(1);
    uint32_t strs = U(2), lens = U(3);
    const GLchar **sp = calloc((size_t)(count > 0 ? count : 1), sizeof *sp);
    GLint *lp = lens ? calloc((size_t)(count > 0 ? count : 1), sizeof *lp) : NULL;
    for (GLsizei i = 0; i < count; i++) {
        sp[i] = hle_cstr(nj_r32(strs + 4u * (uint32_t)i));
        if (lp) lp[i] = (GLint)nj_r32(lens + 4u * (uint32_t)i);
    }
    glShaderSource(U(0), count, sp, lp);
    free(sp);
    free(lp);
}

/* glGetString results are copied once per enum into guest memory. */
static struct { GLenum e; uint32_t ga; } strings[8];
static pthread_mutex_t str_lock = PTHREAD_MUTEX_INITIALIZER;

static void h_glGetString(NjCtx *c) {
    GLenum e = U(0);
    pthread_mutex_lock(&str_lock);
    uint32_t ga = 0;
    int i = 0;
    for (; i < 8 && strings[i].ga; i++) if (strings[i].e == e) { ga = strings[i].ga; break; }
    if (!ga) {
        const GLubyte *s = glGetString(e);
        if (s && i < 8) {
            ga = hle_guest_strdup((const char *)s);
            strings[i].e = e;
            strings[i].ga = ga;
        }
    }
    pthread_mutex_unlock(&str_lock);
    hle_ret(c, ga);
}

void hle_gles_register(void) {
#define R(fn) hle_register(#fn, h_##fn);
    R(glActiveTexture) R(glAttachShader) R(glBindBuffer) R(glBindFramebuffer) R(glBindRenderbuffer)
    R(glBindTexture) R(glBlendEquation) R(glBlendFunc) R(glBufferData) R(glClear) R(glClearColor)
    R(glClearDepthf) R(glClearStencil) R(glCompileShader) R(glCompressedTexImage2D) R(glCreateProgram)
    R(glCreateShader) R(glDeleteBuffers) R(glDeleteFramebuffers) R(glDeleteProgram) R(glDeleteRenderbuffers)
    R(glDeleteShader) R(glDeleteTextures) R(glDepthFunc) R(glDepthMask) R(glDepthRangef) R(glDisable)
    R(glDisableVertexAttribArray) R(glDrawArrays) R(glDrawElements) R(glEnable) R(glEnableVertexAttribArray)
    R(glFinish) R(glFlush) R(glFramebufferRenderbuffer) R(glFramebufferTexture2D) R(glFrontFace) R(glGenBuffers)
    R(glGenFramebuffers) R(glGenRenderbuffers) R(glGenTextures) R(glGetActiveAttrib) R(glGetActiveUniform)
    R(glGetAttribLocation) R(glGetBufferParameteriv) R(glGetError) R(glGetIntegerv) R(glGetProgramInfoLog)
    R(glGetProgramiv) R(glGetShaderInfoLog) R(glGetShaderiv) R(glGetString) R(glGetUniformLocation)
    R(glLinkProgram) R(glRenderbufferStorage) R(glScissor) R(glShaderSource) R(glStencilFunc) R(glStencilMask)
    R(glStencilOp) R(glTexImage2D) R(glTexParameterf) R(glTexParameteri) R(glUniform1fv) R(glUniform1i)
    R(glUniform3fv) R(glUniform4fv) R(glUniformMatrix4fv) R(glUseProgram) R(glVertexAttribPointer)
    R(glViewport) R(glGenVertexArraysOES) R(glBindVertexArrayOES) R(glDeleteVertexArraysOES)
#undef R
}
