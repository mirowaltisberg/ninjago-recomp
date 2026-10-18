#include <stdlib.h>

#include "nj_test.h"

#include "nj/gles.h"

static const char *SRC = "// key = 1\nuniform highp vec4 L;\nvoid main() { gl_FragColor = vec4(normalize(L.xyz), 1.0); }\n";

NJ_TEST(glsl_patch_redirects_normalize_after_helpers) {
    char *out = nj_glsl_patch(SRC);
    const char *helper = strstr(out, "nj_normalize(highp vec3"), *def = strstr(out, "#define normalize nj_normalize");
    NJ_CHECK(helper != NULL);
    NJ_CHECK(def != NULL && def > helper); /* the helpers themselves must not see the macro */
    NJ_CHECK(strstr(out, SRC) != NULL);    /* the game's source follows unchanged */
    NJ_CHECK(strstr(out, SRC) > def);
    free(out);
}

NJ_TEST(glsl_patch_keeps_version_and_extension_first) {
    const char *src = "#version 100\n#extension GL_OES_standard_derivatives : enable\nvoid main() { normalize(vec2(1.0)); }\n";
    char *out = nj_glsl_patch(src);
    NJ_CHECK(strncmp(out, "#version 100\n#extension GL_OES_standard_derivatives : enable\n", 61) == 0);
    NJ_CHECK(strstr(out + 61, "nj_normalize") != NULL);
    free(out);
}

NJ_TEST(glsl_patch_leaves_shaders_without_normalize_alone) {
    const char *src = "void main() { gl_FragColor = vec4(1.0); }\n";
    char *out = nj_glsl_patch(src);
    NJ_EQ_STR(out, src);
    free(out);
}
