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
#include <math.h>
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

THUNK(glBindBuffer, U(0), U(1))
THUNK(glBindRenderbuffer, U(0), U(1))
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
static _Thread_local GLuint t_fb, t_unit, t_tex0; /* guest framebuffer (0 = screen), active unit, unit 0's 2D texture */
/* Client-side vertex arrays per attribute, for the overlay's full-screen test. */
typedef struct { GLint size; GLenum type; GLsizei stride; const uint8_t *client; } AttribArray;
static _Thread_local AttribArray t_attr[16];

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
        GLint ab = 0, eb = 0;
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &ab);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &eb);
        fprintf(stderr, "[gl] draw vbo %d ibo %d prog %u mode %u n %d fb %d tex %u depth %d/0x%x/%d blend %d 0x%x/0x%x cull %d ff 0x%x stencil %d%s %s\n",
                ab, eb, cur_program, mode, count, fb, t_tex0, glIsEnabled(GL_DEPTH_TEST), df, dm, glIsEnabled(GL_BLEND), bs, bd,
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

static void hud_before_draw(GLenum mode, GLint first, GLsizei count, GLenum itype, const void *indices); /* below */

static void h_glDrawArrays(NjCtx *c) {
    hud_before_draw(U(0), S(1), S(2), 0, NULL);
    if (dbg_drop(U(0), S(2))) return;
    glDrawArrays(U(0), S(1), S(2));
}

/* ---- overlay inset ----
 * In gameplay the engine renders the 3D scene into a texture, draws that texture full screen, then draws the
 * 2D overlay (controls, HUD) on top. Draws to the screen after the scene texture has been drawn this frame get
 * a viewport (and scissor) scaled towards the screen centre. Bindings are tracked per thread: the loader
 * thread has its own context. */
static float hud_s = 1.0f;
static int screen_w, screen_h;
static int scene_this_frame, scene_last_frame;
static GLuint rt_tex[64]; /* textures attached to framebuffers (render targets) */
static int nrt;
static pthread_mutex_t rt_lock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local GLint t_vp[4], t_sc[4];
static _Thread_local int t_sc_set, t_scaled;

void nj_gles_set_hud_scale(float s, int w, int h) {
    hud_s = s > 0 && s < 1 ? s : 1.0f;
    screen_w = w;
    screen_h = h;
}
void nj_gles_frame_begin(void) {
    scene_last_frame = scene_this_frame;
    scene_this_frame = 0;
}
bool nj_gles_hud_scaled(void) { return hud_s < 1.0f && scene_last_frame; }
float nj_gles_hud_scale_factor(void) { return hud_s; }

static void scaled_box(const GLint in[4], GLint out[4]) {
    float cx = screen_w / 2.0f, cy = screen_h / 2.0f;
    out[0] = (GLint)lroundf(cx + (in[0] - cx) * hud_s);
    out[1] = (GLint)lroundf(cy + (in[1] - cy) * hud_s);
    out[2] = (GLint)lroundf(in[2] * hud_s);
    out[3] = (GLint)lroundf(in[3] * hud_s);
}

static void apply_viewport(void) {
    GLint v[4];
    if (t_scaled) scaled_box(t_vp, v);
    else memcpy(v, t_vp, sizeof v);
    glViewport(v[0], v[1], v[2], v[3]);
    if (!t_sc_set) return;
    if (t_scaled) scaled_box(t_sc, v);
    else memcpy(v, t_sc, sizeof v);
    glScissor(v[0], v[1], v[2], v[3]);
}

static int is_render_target(GLuint tex) {
    pthread_mutex_lock(&rt_lock);
    int found = 0;
    for (int i = 0; i < nrt && !found; i++) found = rt_tex[i] == tex;
    pthread_mutex_unlock(&rt_lock);
    return found;
}

/* Per program: the engine's position attribute and ViewComposite matrix (gl_Position = ViewComposite * pos). */
typedef struct { GLuint prog; GLint pos, vc; float m[16]; int m_set; } ProgView;
static ProgView prog_views[256];
static int nprog_views;

static ProgView *prog_view(GLuint prog) {
    for (int i = 0; i < nprog_views; i++)
        if (prog_views[i].prog == prog) return &prog_views[i];
    if (nprog_views == 256) return NULL;
    ProgView *pv = &prog_views[nprog_views++];
    *pv = (ProgView){prog, glGetAttribLocation(prog, "VERTEX_INPUT_PREANIM_Position"),
                     glGetUniformLocation(prog, "ViewComposite"), {0}, 0};
    return pv;
}

static void h_glUniformMatrix4fv(NjCtx *c) {
    const GLfloat *v = P(3);
    if (hud_s < 1.0f && v && S(1) >= 1 && !B(2)) {
        ProgView *pv = prog_view(cur_program);
        if (pv && pv->vc >= 0 && pv->vc == S(0)) {
            memcpy(pv->m, v, sizeof pv->m);
            pv->m_set = 1;
        }
    }
    glUniformMatrix4fv(S(0), S(1), B(2), v);
}

/* Whether a draw's vertices (client arrays only) cover the whole screen: backgrounds and fades. */
static int covers_screen(GLint first, GLsizei count, GLenum itype, const void *indices) {
    ProgView *pv = prog_view(cur_program);
    if (!pv || pv->pos < 0 || pv->pos >= 16 || count <= 0) return 0;
    const AttribArray *a = &t_attr[pv->pos];
    if (!a->client || a->type != GL_FLOAT || a->size < 2) return 0;
    GLsizei stride = a->stride ? a->stride : a->size * 4;
    float lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f;
    for (GLsizei k = 0; k < count; k++) {
        uint32_t i = (uint32_t)(first + k);
        if (indices) i = itype == GL_UNSIGNED_SHORT ? ((const uint16_t *)indices)[k] : ((const uint8_t *)indices)[k];
        float v[4] = {0, 0, 0, 1};
        memcpy(v, a->client + (size_t)i * (size_t)stride, sizeof(float) * (size_t)(a->size < 4 ? a->size : 4));
        float x = v[0], y = v[1], w = v[3];
        if (pv->m_set) {
            const float *m = pv->m; /* column-major */
            x = m[0] * v[0] + m[4] * v[1] + m[8] * v[2] + m[12] * v[3];
            y = m[1] * v[0] + m[5] * v[1] + m[9] * v[2] + m[13] * v[3];
            w = m[3] * v[0] + m[7] * v[1] + m[11] * v[2] + m[15] * v[3];
        }
        if (w <= 0) return 0;
        x /= w;
        y /= w;
        lo_x = x < lo_x ? x : lo_x;
        hi_x = x > hi_x ? x : hi_x;
        lo_y = y < lo_y ? y : lo_y;
        hi_y = y > hi_y ? y : hi_y;
    }
    return lo_x <= -0.95f && hi_x >= 0.95f && lo_y <= -0.95f && hi_y >= 0.95f;
}

static void hud_before_draw(GLenum mode, GLint first, GLsizei count, GLenum itype, const void *indices) {
    (void)mode;
    int want = 0;
    if (hud_s < 1.0f && t_fb == 0 && screen_w > 0) {
        if (is_render_target(t_tex0)) scene_this_frame = 1; /* the composited scene: full screen */
        else want = scene_this_frame && !covers_screen(first, count, itype, indices);
    }
    if (want != t_scaled) {
        t_scaled = want;
        apply_viewport();
    }
}

static void h_glViewport(NjCtx *c) {
    for (int i = 0; i < 4; i++) t_vp[i] = S(i);
    apply_viewport();
}

static void h_glScissor(NjCtx *c) {
    for (int i = 0; i < 4; i++) t_sc[i] = S(i);
    t_sc_set = 1;
    apply_viewport();
}

static void h_glActiveTexture(NjCtx *c) {
    t_unit = U(0) - GL_TEXTURE0;
    glActiveTexture(U(0));
}

static void h_glBindTexture(NjCtx *c) {
    if (U(0) == GL_TEXTURE_2D && t_unit == 0) t_tex0 = U(1);
    glBindTexture(U(0), U(1));
}

static void h_glFramebufferTexture2D(NjCtx *c) {
    GLuint tex = U(3);
    if (tex && !is_render_target(tex)) {
        pthread_mutex_lock(&rt_lock);
        if (nrt < 64) rt_tex[nrt++] = tex;
        pthread_mutex_unlock(&rt_lock);
    }
    glFramebufferTexture2D(U(0), U(1), U(2), tex, S(4));
}

static void h_glDeleteTextures(NjCtx *c) {
    const GLuint *ids = P(1);
    pthread_mutex_lock(&rt_lock);
    for (GLsizei k = 0; ids && k < S(0); k++)
        for (int i = 0; i < nrt; i++)
            if (rt_tex[i] == ids[k]) rt_tex[i--] = rt_tex[--nrt];
    pthread_mutex_unlock(&rt_lock);
    glDeleteTextures(S(0), ids);
}

/* Android's window surface is framebuffer 0; on iOS the platform supplies an FBO in its place. */
static void h_glBindFramebuffer(NjCtx *c) {
    GLuint fb = U(1);
    t_fb = fb;
    if (fb != 0 && t_scaled) { /* offscreen rendering never uses the overlay inset */
        t_scaled = 0;
        apply_viewport();
    }
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
    GLint bound = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &bound);
    const void *p = offset_or_pointer(c, 5, GL_ARRAY_BUFFER_BINDING);
    if (U(0) < 16) t_attr[U(0)] = (AttribArray){S(1), U(2), S(4), bound ? NULL : p};
    glVertexAttribPointer(U(0), S(1), U(2), B(3), S(4), p);
}

static void h_glDrawElements(NjCtx *c) {
    const void *idx = offset_or_pointer(c, 3, GL_ELEMENT_ARRAY_BUFFER_BINDING);
    GLint ibo = 0;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ibo);
    hud_before_draw(U(0), 0, S(1), U(2), ibo ? NULL : idx);
    if (dbg_drop(U(0), S(1))) return;
    glDrawElements(U(0), S(1), U(2), idx);
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
