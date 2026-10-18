#include "boot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "nj/audio.h"
#include "nj/backend.h"
#include "nj/format.h"
#include "nj/fs.h"
#include "nj/gles.h"
#include "nj/guest.h"
#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/hle_libc.h"
#include "nj/hle_misc.h"
#include "nj/hle_overrides.h"
#include "nj/hle_pthread.h"
#include "nj/jni.h"
#include "nj/obb.h"

#define PKG "com.lego.ninjago.toe"
#define G_FILES "/data/data/" PKG "/files"
#define G_CACHE "/data/data/" PKG "/cache"
#define G_EXTERNAL "/storage/emulated/0/Android/data/" PKG "/files"
#define G_OBB_DIR "/storage/emulated/0/Android/obb/" PKG
#define FUSION "Java_com_wbgames_LEGOgame_Fusion_"
#define VIEW "Java_com_wbgames_LEGOgame_GameGLSurfaceView_"

static const NjImage *image;
static uint32_t cls_fusion, cls_view;

static void mkdirs(const char *path) {
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(buf, 0755); *p = '/'; }
    mkdir(buf, 0755);
}

static void map_dir(const char *guest, const char *sandbox, const char *sub) {
    char host[1024];
    snprintf(host, sizeof host, "%s/%s", sandbox, sub);
    mkdirs(host);
    nj_fs_map_prefix(guest, host);
}

static int dir_of(const char *path, char *out, size_t cap) {
    const char *slash = strrchr(path, '/');
    if (!slash) return -1;
    snprintf(out, cap, "%.*s", (int)(slash - path), path);
    return 0;
}


static uint32_t obb_entries(const char *obb_host, uint32_t *count) {
    NjObbEntry *e = NULL;
    uint32_t n = 0;
    if (nj_obb_index(obb_host, &e, &n)) return 0;
    uint32_t arr = nj_jni_new_object_array("com/wbgames/LEGOgame/Fusion$APKZipFileEntry", n);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t o = nj_jni_object("com/wbgames/LEGOgame/Fusion$APKZipFileEntry");
        nj_jni_set_field(o, "fileName", "Ljava/lang/String;", nj_jni_new_string(e[i].name));
        nj_jni_set_field(o, "offset", "J", e[i].data_offset);
        nj_jni_set_field(o, "length", "J", e[i].size);
        nj_jni_array_set(arr, i, o);
    }
    free(e);
    *count = n;
    return arr;
}

int nj_boot(const NjBootConfig *cfg, NjImage *img) {
    if (nj_mem_init() || nj_mem_map_fixed_regions() || nj_heap_init()) { fprintf(stderr, "boot: memory setup failed\n"); return -1; }
    if (nj_image_load(cfg->so_path, NJ_IMAGE_BASE, img)) { fprintf(stderr, "boot: cannot load %s\n", cfg->so_path); return -1; }
    image = img;
    if (cfg->fixed_clock) nj_time_set_fixed(true, 1500000000ull * 1000000ull, 1000);
    if (cfg->trace_path) hle_trace_open(cfg->trace_path);

    hle_libc_core_register(img);
    hle_format_register();
    hle_fs_register();
    hle_android_register();
    hle_pthread_register();
    hle_time_register();
    hle_libm_register();
    hle_dl_register(img);
    hle_gles_register();
    hle_egl_register();
    hle_opensl_register();
    nj_jni_init();
    nj_java_classes_register();
    hle_overrides_register();
    hle_resolve_overrides(img);
    hle_bind_imports(img);

    map_dir(G_FILES, cfg->sandbox_dir, "files");
    map_dir(G_CACHE, cfg->sandbox_dir, "cache");
    map_dir(G_EXTERNAL, cfg->sandbox_dir, "external");
    char obb_dir[1024];
    if (dir_of(cfg->obb_path, obb_dir, sizeof obb_dir)) return -1;
    nj_fs_map_prefix(G_OBB_DIR, obb_dir);

    nj_backend_set_image(img);
    if (nj_backend_init()) { fprintf(stderr, "boot: backend init failed\n"); return -1; }
    NjCtx *c = nj_ctx();

    /* System.loadLibrary: static constructors */
    for (uint32_t i = 0; i < img->init_count; i++) {
        uint32_t fn = nj_r32(img->init_array_ga + 4 * i);
        if (fn && fn != 0xFFFFFFFFu) nj_call(c, fn, 0, NULL);
    }

    cls_fusion = nj_jni_class("com/wbgames/LEGOgame/Fusion");
    cls_view = nj_jni_class("com/wbgames/LEGOgame/GameGLSurfaceView");

    /* GameActivity.onCreate */
    nj_jni_call_native(img, FUSION "nativeSetWritePath", cls_fusion, "(Ljava/lang/String;)V", nj_jni_new_string(G_EXTERNAL));
    nj_jni_call_native(img, FUSION "nativeSetSavePath", cls_fusion, "(Ljava/lang/String;)V", nj_jni_new_string(G_FILES));
    nj_jni_call_native(img, FUSION "nativeSetCachePath", cls_fusion, "(Ljava/lang/String;)V", nj_jni_new_string(G_CACHE));
    nj_jni_call_native(img, FUSION "nativeInitializeAssetManager", cls_fusion, "(Landroid/content/res/AssetManager;)V",
                       nj_jni_object("android/content/res/AssetManager"));
    nj_jni_call_native(img, FUSION "nativeSetCommandLine", cls_fusion, "(Ljava/lang/String;)V", nj_jni_new_string(""));
    const char *model = cfg->device_model ? cfg->device_model : "iPhone18,3";
    nj_jni_call_native(img, FUSION "nativeSetDeviceStrings", cls_fusion,
                       "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
                       nj_jni_new_string(model), nj_jni_new_string("iphone"), nj_jni_new_string("Apple"),
                       nj_jni_new_string("apple"));
    uint32_t n = 0, entries = obb_entries(cfg->obb_path, &n);
    if (!entries) { fprintf(stderr, "boot: cannot index %s\n", cfg->obb_path); return -1; }
    char obb_guest[1024];
    snprintf(obb_guest, sizeof obb_guest, G_OBB_DIR "/%s", strrchr(cfg->obb_path, '/') + 1);
    nj_jni_call_native(img, FUSION "addOBBEntriesToFusion", cls_fusion,
                       "(Ljava/lang/String;[Lcom/wbgames/LEGOgame/Fusion$APKZipFileEntry;)V",
                       nj_jni_new_string(obb_guest), entries);

    /* onResume, then the GL thread: onSurfaceCreated / onSurfaceChanged, then window focus */
    nj_jni_call_native(img, VIEW "nativeResume", cls_view, "()V");
    nj_egl_set_current_main();
    uint32_t config = nj_jni_object("javax/microedition/khronos/egl/EGLConfig");
    nj_jni_set_field(config, "mEGLConfig", "I", nj_egl_config_handle());
    nj_jni_call_native(img, VIEW "nativeInit", cls_view, "(Ljavax/microedition/khronos/egl/EGLConfig;Landroid/app/Activity;)V",
                       config, nj_jni_object("com/lego/ninjago/toe/GameActivity"));
    nj_jni_call_native(img, VIEW "nativeResize", cls_view, "(II)V", cfg->width, cfg->height);
    nj_jni_call_native(img, VIEW "nativeWindowFocusChanged", cls_view, "(Z)V", 1);
    return 0;
}

void nj_frame(void) { nj_jni_call_native(image, VIEW "nativeRender", cls_view, "()V"); }

void nj_touch(int kind, int id, float x, float y) {
    static const char *const names[] = {"nativeTouchEventDown", "nativeTouchEventMove", "nativeTouchEventUp", NULL,
                                        "nativeTouchEventGestureStart", "nativeTouchEventGestureEnd"};
    if (kind == 3) { nj_jni_call_native(image, FUSION "nativeTouchEventCancelAll", cls_fusion, "()V"); return; }
    if (kind < 0 || kind > 5 || !names[kind]) return;
    char sym[128];
    snprintf(sym, sizeof sym, FUSION "%s", names[kind]);
    nj_jni_call_native(image, sym, cls_fusion, "(IFFF)V", id, (double)x, (double)y, 1.0);
}

void nj_pause(void) { nj_jni_call_native(image, VIEW "nativePause", cls_view, "()V"); }
void nj_resume(void) { nj_jni_call_native(image, VIEW "nativeResume", cls_view, "()V"); }
