/* Guest shader fix-ups for Apple's renderers. The game's point-light shaders normalize a tangent-space light vector
 * that is exactly zero for a mesh without tangents when the light lies in the surface plane. GLSL leaves
 * normalize(0) undefined; Apple's IEEE renderers return NaN, which blacks out the whole triangle (sim_glsl_nan
 * shows it on the Simulator's renderer). A safeguard: the game hitting that case has not been observed yet; the
 * black floor triangles seen in the Simulator are its renderer mis-clipping (sim_gl_clip). */
#include <stdlib.h>
#include <string.h>

#include "nj/gles.h"

/* Explicit precision: the game's fragment shaders declare no default float precision. */
static const char PRELUDE[] =
    "highp float nj_normalize(highp float v) { return v == 0.0 ? 0.0 : sign(v); }\n"
    "highp vec2 nj_normalize(highp vec2 v) { highp float d = dot(v, v); return d > 0.0 ? v * inversesqrt(d) : vec2(0.0); }\n"
    "highp vec3 nj_normalize(highp vec3 v) { highp float d = dot(v, v); return d > 0.0 ? v * inversesqrt(d) : vec3(0.0); }\n"
    "highp vec4 nj_normalize(highp vec4 v) { highp float d = dot(v, v); return d > 0.0 ? v * inversesqrt(d) : vec4(0.0); }\n"
    "#define normalize nj_normalize\n";

/* #version and #extension must precede any other tokens, so the prelude goes after the last of them. */
static size_t insert_at(const char *src) {
    size_t at = 0;
    for (const char *l = src; *l;) {
        const char *e = strchr(l, '\n');
        const char *t = l + strspn(l, " \t");
        if (!strncmp(t, "#version", 8) || !strncmp(t, "#extension", 10)) at = e ? (size_t)(e + 1 - src) : strlen(src);
        if (!e) break;
        l = e + 1;
    }
    return at;
}

char *nj_glsl_patch(const char *src) {
    if (!strstr(src, "normalize")) return strdup(src);
    size_t n = strlen(src), at = insert_at(src), pn = sizeof PRELUDE - 1;
    char *out = malloc(n + pn + 2);
    memcpy(out, src, at);
    size_t o = at;
    if (at && src[at - 1] != '\n') out[o++] = '\n';
    memcpy(out + o, PRELUDE, pn);
    memcpy(out + o + pn, src + at, n - at + 1);
    return out;
}
