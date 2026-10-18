/* Emulated Java side of the Android build (docs/java-contract.md section 3). */
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nj/jni.h"

static const char *iso3_for(const char *two) {
    static const char *const map[][2] = {
        {"de", "deu"}, {"en", "eng"}, {"fr", "fra"}, {"it", "ita"}, {"es", "spa"}, {"nl", "nld"}, {"pt", "por"},
        {"da", "dan"}, {"sv", "swe"}, {"nb", "nor"}, {"no", "nor"}, {"fi", "fin"}, {"pl", "pol"}, {"ru", "rus"},
        {"ja", "jpn"}, {"ko", "kor"}, {"zh", "zho"}, {"tr", "tur"}, {"cs", "ces"}, {"hu", "hun"}, {"el", "ell"}};
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (!strncmp(two, map[i][0], 2)) return map[i][1];
    return "eng";
}

/* ISO 639-2/T code of the preferred language (NJ_LANG overrides), like Locale.getISO3Language(). */
static const char *preferred_iso3(void) {
    const char *env = getenv("NJ_LANG");
    if (env && *env) return env;
    static char cached[8];
    if (cached[0]) return cached;
    const char *code = "eng";
    CFArrayRef langs = CFLocaleCopyPreferredLanguages();
    if (langs && CFArrayGetCount(langs) > 0) {
        char buf[32];
        if (CFStringGetCString(CFArrayGetValueAtIndex(langs, 0), buf, sizeof buf, kCFStringEncodingUTF8)) code = iso3_for(buf);
    }
    if (langs) CFRelease(langs);
    snprintf(cached, sizeof cached, "%s", code);
    return cached;
}

static uint64_t j_language(uint32_t self, const NjJValue *a, int n) {
    (void)self; (void)a; (void)n;
    return nj_jni_new_string(preferred_iso3());
}

static uint64_t j_log_string(const char *what, uint32_t s) {
    const char *t = nj_jni_string_chars(s);
    fprintf(stderr, "[java] %s(%s)\n", what, t ? t : "");
    return 0;
}

static uint64_t j_openBrowser(uint32_t self, const NjJValue *a, int n) { (void)self; (void)n; return j_log_string("openBrowser", a[0].l); }
static uint64_t j_flurry(uint32_t self, const NjJValue *a, int n) { (void)self; (void)n; return j_log_string("Flurry", a[0].l); }
static uint64_t j_shutdownApp(uint32_t self, const NjJValue *a, int n) {
    (void)self; (void)a; (void)n;
    fprintf(stderr, "[java] shutdownApp() requested by the game\n");
    return 0;
}
/* The OBB contains no videos, so movie playback always reports failure and the engine skips FMVs. */
static uint64_t j_startMovie(uint32_t self, const NjJValue *a, int n) { (void)self; (void)n; j_log_string("startMoviePlayback", a[0].l); return 0; }
static uint64_t j_noop(uint32_t self, const NjJValue *a, int n) { (void)self; (void)a; (void)n; return 0; }

static uint32_t loader;

static uint64_t j_getClassLoader(uint32_t self, const NjJValue *a, int n) {
    (void)self; (void)a; (void)n;
    if (!loader) loader = nj_jni_object("java/lang/ClassLoader");
    return loader;
}

static uint64_t j_loadClass(uint32_t self, const NjJValue *a, int n) {
    (void)self; (void)n;
    const char *dotted = nj_jni_string_chars(a[0].l);
    if (!dotted) { nj_jni_throw("java/lang/ClassNotFoundException"); return 0; }
    char name[256];
    snprintf(name, sizeof name, "%s", dotted);
    for (char *p = name; *p; p++) if (*p == '.') *p = '/';
    static const char *const known[] = {
        "com/wbgames/LEGOgame/FusionCallback", "com/wbgames/LEGOgame/GameFlurry",
        "com/wbgames/LEGOgame/GameGLSurfaceView$Renderer", "com/wbgames/LEGOgame/GameGLSurfaceView$GameVideoPlayer",
        "com/wbgames/LEGOgame/GameGLSurfaceView", "com/wbgames/LEGOgame/Fusion",
        "com/wbgames/LEGOgame/Fusion$APKZipFileEntry", "com/lego/ninjago/toe/GameActivity"};
    for (size_t i = 0; i < sizeof known / sizeof known[0]; i++)
        if (!strcmp(name, known[i])) return nj_jni_class(name);
    fprintf(stderr, "[java] loadClass(%s): not available\n", name);
    nj_jni_throw("java/lang/ClassNotFoundException");
    return 0;
}

void nj_java_classes_register(void) {
    static const char *const declared[] = {
        "com/wbgames/LEGOgame/Fusion", "com/wbgames/LEGOgame/Fusion$APKZipFileEntry",
        "com/wbgames/LEGOgame/GameGLSurfaceView", "com/lego/ninjago/toe/GameActivity", "android/app/Activity",
        "android/content/res/AssetManager", "javax/microedition/khronos/egl/EGLConfig", "java/io/FileDescriptor",
        "java/lang/Exception", "java/lang/ClassLoader"};
    for (size_t i = 0; i < sizeof declared / sizeof declared[0]; i++) nj_jni_declare_class(declared[i]);
    nj_java_register("com/wbgames/LEGOgame/FusionCallback", "GetCurrentLanguageAsISO3Letter", "()Ljava/lang/String;", j_language);
    nj_java_register("com/lego/ninjago/toe/GameActivity", "openBrowser", "(Ljava/lang/String;)V", j_openBrowser);
    const char *r = "com/wbgames/LEGOgame/GameGLSurfaceView$Renderer";
    nj_java_register(r, "shutdownApp", "()V", j_shutdownApp);
    nj_java_register(r, "startMoviePlayback", "(Ljava/lang/String;ZLjava/lang/String;)Z", j_startMovie);
    nj_java_register(r, "stopMoviePlayback", "()V", j_noop);
    nj_java_register(r, "setMoviePlaybackAttributes", "(F)V", j_noop);
    const char *vp = "com/wbgames/LEGOgame/GameGLSurfaceView$GameVideoPlayer";
    nj_java_register(vp, "setNewSubTitleInfo", "(IF)V", j_noop);
    nj_java_register(vp, "addSubtitle", "(Ljava/lang/String;FF)V", j_noop);
    nj_java_register("com/wbgames/LEGOgame/GameFlurry", "Flurry_LogEvent", "(Ljava/lang/String;)V", j_flurry);
    nj_java_register("com/wbgames/LEGOgame/GameFlurry", "Flurry_LogEventParam1",
                     "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", j_flurry);
    nj_java_register("android/app/Activity", "getClassLoader", "()Ljava/lang/ClassLoader;", j_getClassLoader);
    nj_java_register("java/lang/ClassLoader", "loadClass", "(Ljava/lang/String;Z)Ljava/lang/Class;", j_loadClass);
}
