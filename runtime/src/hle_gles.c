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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "nj/gles.h"
#include "nj/hle.h"
#include "nj/platform.h"
#include "nj/texture.h"

#if TARGET_OS_IPHONE
bool nj_gles_decode_atc = true;
#else
bool nj_gles_decode_atc = false;
#endif

#define U(i) hle_arg(c, i)
#define S(i) ((GLint)hle_arg(c, i))
#define F(i) hle_arg_f(c, i)
#define P(i) ((void *)(hle_arg(c, i) ? nj_host(hle_arg(c, i)) : NULL))
#define B(i) ((GLboolean)(hle_arg(c, i) & 0xff))

#define V0(fn) static void h_##fn(NjCtx *c) { (void)c; fn(); }
#define THUNK(fn, ...) static void h_##fn(NjCtx *c) { fn(__VA_ARGS__); }
#define RET(fn, ...) static void h_##fn(NjCtx *c) { hle_ret(c, (uint32_t)fn(__VA_ARGS__)); }

THUNK(glActiveTexture, U(0))
THUNK(glBindBuffer, U(0), U(1))
THUNK(glBindRenderbuffer, U(0), U(1))
THUNK(glBindTexture, U(0), U(1))
THUNK(glBlendEquation, U(0))
THUNK(glBlendFunc, U(0), U(1))
THUNK(glBufferData, U(0), (GLsizeiptr)U(1), P(2), U(3))
THUNK(glClear, U(0))
THUNK(glClearColor, F(0), F(1), F(2), F(3))
THUNK(glClearDepthf, F(0))
THUNK(glClearStencil, S(0))
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
THUNK(glGetProgramInfoLog, U(0), S(1), P(2), P(3))
THUNK(glGetProgramiv, U(0), U(1), P(2))
THUNK(glGetShaderInfoLog, U(0), S(1), P(2), P(3))
THUNK(glGetShaderiv, U(0), U(1), P(2))
RET(glGetUniformLocation, U(0), P(1))
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
THUNK(glViewport, S(0), S(1), S(2), S(3))
THUNK(glGenVertexArraysOES, S(0), P(1))
THUNK(glBindVertexArrayOES, U(0))
THUNK(glDeleteVertexArraysOES, S(0), P(1))

/* ---- developer switches (environment), for bisecting rendering problems ----
 * NJ_SHADER_DUMP_DIR  save every shader source (as the guest wrote it) as shader_<id>.glsl
 * NJ_GL_RAW_SHADERS   pass shaders to GL without nj_glsl_patch
 * NJ_GL_SKIP          comma-separated rules applied from the start (see below)
 * NJ_GL_CTL           control file, re-read while the game runs, one rule per line:
 *                       skip stencil      drop draws made with the stencil test on
 *                       skip <text>       drop draws whose program's shader-key comment contains <text>
 *                       skipprog <id>     drop draws of one program
 *                       log <n>           log the next <n> draws with their GL state
 * NJ_GL_STATS         log each shader key the first time it draws with the stencil test on
 * Shaders that fail to compile or link are always logged. */
#define MAX_KEYED 2048
#define MAX_RULES 32
static struct { GLuint id; char *key; } shader_keys[MAX_KEYED], program_keys[MAX_KEYED];
static int nshader_keys, nprogram_keys;
static pthread_mutex_t key_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t dbg_once = PTHREAD_ONCE_INIT;
static char *skip_text[MAX_RULES];
static GLuint skip_prog[MAX_RULES];
static int nskip_text, nskip_prog, skip_stencil, log_left, dbg_on, dbg_stats;
static const char *dump_dir, *ctl_path;
static struct timespec ctl_mtime;
static _Thread_local GLuint cur_program;

/* Caller holds key_lock. Rules are separated by commas or newlines. */
static void parse_rules(const char *text) {
    for (int i = 0; i < nskip_text; i++) free(skip_text[i]);
    nskip_text = nskip_prog = skip_stencil = 0;
    char *copy = strdup(text ? text : ""), *list = copy, *tok;
    while ((tok = strsep(&list, ",\n"))) {
        char arg[256] = "";
        if (!strcmp(tok, "skip stencil") || !strcmp(tok, "stencil")) skip_stencil = 1;
        else if (sscanf(tok, "skipprog %u", &skip_prog[nskip_prog]) == 1) nskip_prog += nskip_prog < MAX_RULES - 1;
        else if (sscanf(tok, "log %d", &log_left) == 1) {}
        else if (sscanf(tok, "skip %255[^\n]", arg) == 1 && nskip_text < MAX_RULES) skip_text[nskip_text++] = strdup(arg);
        else if (*tok && !strchr(tok, ' ') && nskip_text < MAX_RULES) skip_text[nskip_text++] = strdup(tok);
    }
    free(copy);
}

static void dbg_init(void) {
    dump_dir = getenv("NJ_SHADER_DUMP_DIR");
    ctl_path = getenv("NJ_GL_CTL");
    dbg_stats = getenv("NJ_GL_STATS") != NULL;
    parse_rules(getenv("NJ_GL_SKIP"));
    dbg_on = dbg_stats || ctl_path || skip_stencil || nskip_text || nskip_prog;
}

/* Caller holds key_lock. */
static void reload_ctl(void) {
    struct stat st;
    if (!ctl_path || stat(ctl_path, &st) != 0) return;
    if (st.st_mtimespec.tv_sec == ctl_mtime.tv_sec && st.st_mtimespec.tv_nsec == ctl_mtime.tv_nsec) return;
    ctl_mtime = st.st_mtimespec;
    FILE *f = fopen(ctl_path, "r");
    if (!f) return;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    parse_rules(buf);
    fprintf(stderr, "[gl] control: %d text, %d program rules, stencil %d, log %d\n", nskip_text, nskip_prog, skip_stencil,
            log_left);
}

/* Caller holds key_lock. */
static char **key_slot(void *table, int *n, GLuint id, int create) {
    struct { GLuint id; char *key; } *t = table;
    for (int i = 0; i < *n; i++) if (t[i].id == id) return &t[i].key;
    if (!create || *n == MAX_KEYED) return NULL;
    t[*n].id = id;
    t[*n].key = NULL;
    return &t[(*n)++].key;
}

/* The engine's shaders start with comment lines naming their features ("// key = ...", "// albedo=..."). */
static void remember_shader(GLuint shader, const char *src) {
    char key[512] = "";
    for (const char *l = src; l && *l == '/' && strlen(key) < sizeof key - 2; l = strchr(l, '\n'), l = l ? l + 1 : NULL) {
        const char *e = strchr(l, '\n');
        size_t n = e ? (size_t)(e - l) : strlen(l);
        strncat(key, l, n < sizeof key - strlen(key) - 2 ? n : sizeof key - strlen(key) - 2);
        strcat(key, " ");
    }
    pthread_mutex_lock(&key_lock);
    char **slot = key_slot(shader_keys, &nshader_keys, shader, 1);
    if (slot) { free(*slot); *slot = strdup(key); }
    pthread_mutex_unlock(&key_lock);
}

static const char *program_key(GLuint p) {
    char **pk = key_slot(program_keys, &nprogram_keys, p, 0);
    return pk && *pk ? *pk : "";
}

static void h_glAttachShader(NjCtx *c) {
    glAttachShader(U(0), U(1));
    pthread_once(&dbg_once, dbg_init);
    pthread_mutex_lock(&key_lock);
    char **sk = key_slot(shader_keys, &nshader_keys, U(1), 0), **pk = key_slot(program_keys, &nprogram_keys, U(0), 1);
    if (sk && *sk && pk) {
        size_t n = (*pk ? strlen(*pk) : 0) + strlen(*sk) + 2;
        char *k = calloc(1, n);
        snprintf(k, n, "%s%s", *pk ? *pk : "", *sk);
        free(*pk);
        *pk = k;
    }
    pthread_mutex_unlock(&key_lock);
}

static void h_glCompileShader(NjCtx *c) {
    glCompileShader(U(0));
    GLint ok = 1;
    glGetShaderiv(U(0), GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        glGetShaderInfoLog(U(0), sizeof log, NULL, log);
        fprintf(stderr, "[gl] shader %u failed to compile: %s\n", U(0), log);
    }
}

static void h_glLinkProgram(NjCtx *c) {
    glLinkProgram(U(0));
    GLint ok = 1;
    glGetProgramiv(U(0), GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        glGetProgramInfoLog(U(0), sizeof log, NULL, log);
        pthread_mutex_lock(&key_lock);
        fprintf(stderr, "[gl] program %u failed to link: %s (%s)\n", U(0), log, program_key(U(0)));
        pthread_mutex_unlock(&key_lock);
    }
}

static void h_glUseProgram(NjCtx *c) {
    glUseProgram(U(0));
    cur_program = U(0);
}

/* Short form of a shader key for draw logs: the feature list without the "albedo=" prefix. */
static void short_key(const char *key, char *out, size_t n) {
    const char *a = strstr(key, "albedo=");
    snprintf(out, n, "%s", a ? a + 7 : key);
    char *sp = strchr(out, ' ');
    if (sp) *sp = 0;
}

/* Returns 1 when the current draw must be dropped. */
static int dbg_drop(GLenum mode, GLsizei count) {
    pthread_once(&dbg_once, dbg_init);
    if (!dbg_on) return 0;
    static unsigned calls;
    pthread_mutex_lock(&key_lock);
    if ((calls++ & 255) == 0) reload_ctl();
    int stencil = glIsEnabled(GL_STENCIL_TEST);
    const char *key = program_key(cur_program);
    int drop = stencil && skip_stencil;
    for (int i = 0; i < nskip_text && !drop; i++) drop = strstr(key, skip_text[i]) != NULL;
    for (int i = 0; i < nskip_prog && !drop; i++) drop = skip_prog[i] == cur_program;
    if (log_left > 0) {
        log_left--;
        GLint fb = 0, df = 0, dm = 0, bs = 0, bd = 0, ff = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
        glGetIntegerv(GL_DEPTH_FUNC, &df);
        glGetIntegerv(GL_DEPTH_WRITEMASK, &dm);
        glGetIntegerv(GL_BLEND_SRC_RGB, &bs);
        glGetIntegerv(GL_BLEND_DST_RGB, &bd);
        glGetIntegerv(GL_FRONT_FACE, &ff);
        char sk[200];
        short_key(key, sk, sizeof sk);
        fprintf(stderr, "[gl] draw prog %u mode %u n %d fb %d depth %d/0x%x/%d blend %d 0x%x/0x%x cull %d ff 0x%x stencil %d%s %s\n",
                cur_program, mode, count, fb, glIsEnabled(GL_DEPTH_TEST), df, dm, glIsEnabled(GL_BLEND), bs, bd,
                glIsEnabled(GL_CULL_FACE), ff, stencil, drop ? " DROPPED" : "", sk);
    }
    if (dbg_stats && stencil) {
        static char *seen[256];
        static int nseen;
        int known = 0;
        for (int i = 0; i < nseen; i++) known |= !strcmp(seen[i], key);
        if (!known && nseen < 256) {
            seen[nseen++] = strdup(key);
            GLint func = 0, ref = 0, pass = 0;
            glGetIntegerv(GL_STENCIL_FUNC, &func);
            glGetIntegerv(GL_STENCIL_REF, &ref);
            glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &pass);
            fprintf(stderr, "[gl] stencil draw: func 0x%x ref %d pass-op 0x%x program %u: %s\n", func, ref, pass, cur_program, key);
        }
    }
    pthread_mutex_unlock(&key_lock);
    return drop;
}

static void h_glDrawArrays(NjCtx *c) {
    if (dbg_drop(U(0), S(2))) return;
    glDrawArrays(U(0), S(1), S(2));
}

/* Android's window surface is framebuffer 0; on iOS the platform supplies an FBO in its place. */
static void h_glBindFramebuffer(NjCtx *c) {
    GLuint fb = U(1);
    if (fb == 0) fb = nj_platform_default_framebuffer();
    glBindFramebuffer(U(0), fb);
}

static void h_glGetIntegerv(NjCtx *c) {
    GLenum pname = U(0);
    GLint *out = P(1);
    glGetIntegerv(pname, out);
    if (pname == GL_FRAMEBUFFER_BINDING && out) {
        GLuint def = nj_platform_default_framebuffer();
        if (def && (GLuint)*out == def) *out = 0;
    }
}

/* iOS GPUs have no ATC: decode those uploads to RGBA8. */
static void h_glCompressedTexImage2D(NjCtx *c) {
    GLenum fmt = U(2);
    GLsizei w = S(3), h = S(4);
    if (nj_gles_decode_atc && nj_atc_format(fmt) && w > 0 && h > 0) {
        size_t need = nj_atc_size(fmt, w, h);
        if ((size_t)S(6) < need) nj_fatal("ATC upload %dx%d: %d bytes, need %zu", w, h, S(6), need);
        uint8_t *rgba = malloc((size_t)w * (size_t)h * 4);
        nj_atc_decode(fmt, w, h, P(7), rgba);
        glTexImage2D(U(0), S(1), GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        free(rgba);
        return;
    }
    glCompressedTexImage2D(U(0), S(1), fmt, w, h, S(5), S(6), P(7));
}

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
    if (dbg_drop(U(0), S(1))) return;
    glDrawElements(U(0), S(1), U(2), offset_or_pointer(c, 3, GL_ELEMENT_ARRAY_BUFFER_BINDING));
}

/* The guest's strings are joined and passed through nj_glsl_patch (NJ_GL_RAW_SHADERS=1 disables that). */
static void h_glShaderSource(NjCtx *c) {
    GLsizei count = S(1);
    uint32_t strs = U(2), lens = U(3);
    size_t total = 1;
    for (GLsizei i = 0; i < count; i++) {
        int32_t len = lens ? (int32_t)nj_r32(lens + 4u * (uint32_t)i) : -1;
        total += len >= 0 ? (size_t)len : strlen(hle_cstr(nj_r32(strs + 4u * (uint32_t)i)));
    }
    char *src = calloc(1, total);
    for (GLsizei i = 0; i < count; i++) {
        int32_t len = lens ? (int32_t)nj_r32(lens + 4u * (uint32_t)i) : -1;
        const char *part = hle_cstr(nj_r32(strs + 4u * (uint32_t)i));
        strncat(src, part, len >= 0 ? (size_t)len : strlen(part));
    }
    pthread_once(&dbg_once, dbg_init);
    char *patched = getenv("NJ_GL_RAW_SHADERS") ? strdup(src) : nj_glsl_patch(src);
    const GLchar *one = patched;
    glShaderSource(U(0), 1, &one, NULL);
    remember_shader(U(0), src);
    if (dump_dir) {
        char path[1024];
        snprintf(path, sizeof path, "%s/shader_%04u.glsl", dump_dir, U(0));
        FILE *f = fopen(path, "w");
        if (f) { fputs(src, f); fclose(f); }
    }
    free(patched);
    free(src);
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
