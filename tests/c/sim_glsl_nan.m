/* Runs in the iOS Simulator (tools/sim/run_sim_test.sh sim_glsl_nan): renders the shape of the game's point-light
 * term with a zero light vector, which the Simulator's renderer turns into NaN (black) unless the shader is patched. */
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES2/glext.h>

#include <dirent.h>
#include <stdlib.h>

#include "nj_test.h"

#include "nj/gles.h"

/* Like the game's dlit shaders: tangent-space light vector, zero when the mesh has no tangents and the light lies
 * in the surface plane; the fragment shader lights by normalize(v).z. */
static const char *VS = "attribute highp vec4 pos;\nuniform highp vec3 lightVec;\nvarying highp vec4 v_l;\n"
                        "void main() { gl_Position = pos; v_l = vec4(lightVec, 1.0); }\n";
static const char *FS = "varying highp vec4 v_l;\n"
                        "void main() { highp float k = normalize(v_l.xyz).z * 0.5 + 0.5;\n"
                        "  gl_FragColor = vec4(vec3(0.25) + k * vec3(0.5), 1.0); }\n";

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "compile failed: %s\n", log);
    }
    return s;
}

/* Draws a full-screen quad with a zero light vector and returns the red channel of the centre pixel. */
static int render(int patched) {
    static EAGLContext *ctx;
    if (!ctx) {
        ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
        [EAGLContext setCurrentContext:ctx];
        GLuint fb, rb;
        glGenFramebuffers(1, &fb);
        glBindFramebuffer(GL_FRAMEBUFFER, fb);
        glGenRenderbuffers(1, &rb);
        glBindRenderbuffer(GL_RENDERBUFFER, rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, 16, 16);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
        glViewport(0, 0, 16, 16);
    }
    char *vs = patched ? nj_glsl_patch(VS) : strdup(VS), *fs = patched ? nj_glsl_patch(FS) : strdup(FS);
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "pos");
    glLinkProgram(p);
    glUseProgram(p);
    glUniform3f(glGetUniformLocation(p, "lightVec"), 0, 0, 0);
    static const GLfloat quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(0);
    glClearColor(1, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    uint8_t px[4];
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    free(vs);
    free(fs);
    return px[0];
}

NJ_TEST(sim_unpatched_zero_normalize_is_black) {
    NJ_EQ_U(render(0), 0); /* documents the Simulator renderer: NaN lighting comes out black */
}

NJ_TEST(sim_patched_zero_normalize_is_lit) {
    int r = render(1); /* k = 0.5 → 0.25 + 0.25 = 0.5 */
    NJ_CHECK(r > 120 && r < 136);
}

/* With NJ_SHADER_DUMP_DIR pointing at shaders dumped from a game run, every one must still compile once patched. */
NJ_TEST(sim_patched_game_shaders_compile) {
    const char *dir = getenv("NJ_SHADER_DUMP_DIR");
    if (!dir) NJ_SKIP("NJ_SHADER_DUMP_DIR not set");
    render(0); /* context */
    DIR *d = opendir(dir);
    NJ_CHECK(d != NULL);
    int total = 0, failed = 0;
    for (struct dirent *e; (e = readdir(d));) {
        if (!strstr(e->d_name, ".glsl")) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE *f = fopen(path, "r");
        static char src[65536];
        size_t n = fread(src, 1, sizeof src - 1, f);
        fclose(f);
        src[n] = 0;
        char *patched = nj_glsl_patch(src);
        GLuint s = compile(strstr(src, "gl_Position") ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER, patched);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { failed++; fprintf(stderr, "  %s\n", e->d_name); }
        glDeleteShader(s);
        free(patched);
        total++;
    }
    closedir(d);
    fprintf(stderr, "  %d shaders compiled, %d failed\n", total, failed);
    NJ_CHECK(total > 0);
    NJ_EQ_U(failed, 0);
}
