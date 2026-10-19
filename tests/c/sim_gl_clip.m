/* Characterizes the Simulator's OpenGL ES renderer (tools/sim/run_sim_test.sh sim_gl_clip). It drops triangles that
 * reach behind the camera instead of clipping them at the near plane: in the dojo, large floor triangles vanish
 * (the background shows as black) once the camera follows the fighter towards the front. GPUs, including the
 * iPhone's, clip such triangles; if this test starts failing, Apple has fixed the renderer. */
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES2/glext.h>

#include "nj_test.h"

static const char *VS = "attribute highp vec4 pos;\nvoid main() { gl_Position = pos; }\n";
static const char *FS = "void main() { gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); }\n";

static GLuint program(void) {
    GLuint p = glCreateProgram();
    GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(v, 1, &VS, NULL);
    glShaderSource(f, 1, &FS, NULL);
    glCompileShader(v);
    glCompileShader(f);
    glAttachShader(p, v);
    glAttachShader(p, f);
    glBindAttribLocation(p, 0, "pos");
    glLinkProgram(p);
    return p;
}

static float edge(float ax, float az, float bx, float bz, float px, float pz) { return (bx - ax) * (pz - az) - (bz - az) * (px - ax); }

/* Projects view-space points of a floor triangle (Y = -1) with a 90-degree perspective (near 0.1, far 100), draws
 * it into a 64x64 target and returns how many pixels differ from exact ray-traced coverage. */
static int draw_and_compare(const float view[3][3]) {
    static EAGLContext *ctx;
    if (!ctx) {
        ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
        [EAGLContext setCurrentContext:ctx];
        GLuint fb, rb;
        glGenFramebuffers(1, &fb);
        glBindFramebuffer(GL_FRAMEBUFFER, fb);
        glGenRenderbuffers(1, &rb);
        glBindRenderbuffer(GL_RENDERBUFFER, rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, 64, 64);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
        glViewport(0, 0, 64, 64);
        glUseProgram(program());
    }
    const float n = 0.1f, f = 100.0f;
    GLfloat clip[12];
    for (int i = 0; i < 3; i++) {
        float X = view[i][0], Y = view[i][1], Z = view[i][2]; /* camera looks down -Z */
        clip[4 * i + 0] = X;
        clip[4 * i + 1] = Y;
        clip[4 * i + 2] = (f + n) / (n - f) * Z + 2 * f * n / (n - f);
        clip[4 * i + 3] = -Z;
    }
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, clip);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    static uint8_t px[64 * 64 * 4];
    glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, px);
    int wrong = 0;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            float nx = (x + 0.5f) / 32 - 1, ny = (y + 0.5f) / 32 - 1;
            int want = 0;
            if (ny < 0) { /* ray (nx, ny, -1) meets the floor at distance t */
                float t = -1 / ny, hx = nx * t, hz = -t;
                float e0 = edge(view[0][0], view[0][2], view[1][0], view[1][2], hx, hz);
                float e1 = edge(view[1][0], view[1][2], view[2][0], view[2][2], hx, hz);
                float e2 = edge(view[2][0], view[2][2], view[0][0], view[0][2], hx, hz);
                want = t > 0.1f && t < 100 && ((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0));
            }
            wrong += want != (px[4 * (y * 64 + x) + 1] == 255);
        }
    return wrong;
}

NJ_TEST(sim_floor_triangle_in_front_is_drawn_exactly) {
    const float tri[3][3] = {{-4, -1, -2}, {4, -1, -2}, {0, -1, -20}}; /* floor 1 below the eye, all in front */
    int wrong = draw_and_compare(tri);
    fprintf(stderr, "  %d of 4096 pixels wrong\n", wrong);
    NJ_CHECK(wrong < 64); /* edge pixels only */
}

NJ_TEST(sim_renderer_misclips_floor_triangle_reaching_behind_camera) {
    const float tri[3][3] = {{-40, -1, 10}, {40, -1, 10}, {0, -1, -20}}; /* two corners behind the eye */
    int wrong = draw_and_compare(tri);
    fprintf(stderr, "  %d of 4096 pixels wrong\n", wrong);
    NJ_CHECK(wrong > 500);
}
