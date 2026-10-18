#include <stdlib.h>
#include <unistd.h>

#include "nj_test.h"

#include "nj/backend.h"
#include "nj/guest.h"
#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/jni.h"

#define PAGE 0x00F00000u
#define FN_RET_R0 (PAGE + 0x00)  /* bx lr */
#define FN_RET_R1 (PAGE + 0x10)  /* mov r0,r1; bx lr */
#define FN_RET_R2 (PAGE + 0x20)  /* mov r0,r2; bx lr */
#define FN_RET_R3 (PAGE + 0x30)  /* mov r0,r3; bx lr */
#define FN_RET_SP0 (PAGE + 0x40) /* ldr r0,[sp]; bx lr */

/* JNI slot indices (jni.h order) */
enum {
    S_FindClass = 6, S_ExceptionOccurred = 15, S_ExceptionClear = 17, S_GetObjectClass = 31, S_GetMethodID = 33,
    S_CallObjectMethod = 34, S_GetFieldID = 94, S_GetObjectField = 95, S_GetIntField = 100, S_GetLongField = 101,
    S_GetStaticMethodID = 113, S_CallStaticObjectMethod = 114, S_CallStaticVoidMethod = 141,
    S_CallStaticVoidMethodA = 143, S_NewStringUTF = 167, S_GetStringUTFChars = 169, S_ReleaseStringUTFChars = 170,
    S_GetArrayLength = 171, S_GetObjectArrayElement = 173, S_GetJavaVM = 219, S_ExceptionCheck = 228,
    S_GetObjectRefType = 232
};

static void setup(void) {
    static int done;
    if (done) return;
    setenv("NJ_LANG", "eng", 1);
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_mem_map_fixed_regions(), 0);
    NJ_EQ_U(nj_heap_init(), 0);
    NJ_EQ_U(nj_mem_map(PAGE, 0x4000, NJ_PROT_R | NJ_PROT_W | NJ_PROT_X), 0);
    const uint32_t code[][2] = {{0xE12FFF1E, 0}, {0xE1A00001, 0xE12FFF1E}, {0xE1A00002, 0xE12FFF1E},
                                {0xE1A00003, 0xE12FFF1E}, {0xE59D0000, 0xE12FFF1E}};
    for (int i = 0; i < 5; i++) { nj_w32(PAGE + 0x10u * (uint32_t)i, code[i][0]); nj_w32(PAGE + 0x10u * (uint32_t)i + 4, code[i][1]); }
    nj_jni_init();
    nj_java_classes_register();
    NJ_EQ_U(nj_backend_init(), 0);
    done = 1;
}

/* Invoke JNI slot with argument words (0-3 in registers, rest on the stack). */
static NjCtx *jcall(int slot, const uint32_t *w, int n) {
    NjCtx *c = nj_ctx();
    static uint32_t base_sp;
    if (!base_sp) base_sp = c->r[13];
    uint32_t sp = (base_sp - 512) & ~7u;
    for (int i = 0; i < n; i++) {
        if (i < 4) c->r[i] = w[i];
        else nj_w32(sp + 4u * (uint32_t)(i - 4), w[i]);
    }
    c->r[13] = sp;
    uint32_t table = nj_r32(nj_jni_env());
    hle_dispatch(c, nj_r32(table + 4u * (uint32_t)slot));
    return c;
}

static uint32_t env(void) { return nj_jni_env(); }
static uint32_t gs(const char *s) { return hle_guest_strdup(s); }

NJ_TEST(jni_table_shape) {
    setup();
    uint32_t table = nj_r32(env());
    NJ_EQ_U(nj_r32(table + 0), 0); /* reserved */
    NJ_CHECK(hle_is_thunk(nj_r32(table + 4 * S_FindClass)));
    NJ_EQ_STR(nj_jni_slot_name(S_FindClass), "FindClass");
    NJ_EQ_STR(nj_jni_slot_name(S_ExceptionCheck), "ExceptionCheck");
    NJ_EQ_STR(nj_jni_slot_name(S_GetObjectRefType), "GetObjectRefType");
    NJ_EQ_STR(nj_jni_slot_name(S_CallStaticVoidMethod), "CallStaticVoidMethod");
    NJ_EQ_STR(nj_jni_slot_name(S_GetStringUTFChars), "GetStringUTFChars");
}

NJ_TEST(jni_language) {
    setup();
    uint32_t w1[] = {env(), gs("com/wbgames/LEGOgame/FusionCallback")};
    uint32_t cls = jcall(S_FindClass, w1, 2)->r[0];
    NJ_CHECK(cls != 0);
    uint32_t w2[] = {env(), cls, gs("GetCurrentLanguageAsISO3Letter"), gs("()Ljava/lang/String;")};
    uint32_t mid = jcall(S_GetStaticMethodID, w2, 4)->r[0];
    NJ_CHECK(mid != 0);
    uint32_t w3[] = {env(), cls, mid};
    uint32_t str = jcall(S_CallStaticObjectMethod, w3, 3)->r[0];
    uint32_t w4[] = {env(), str, 0};
    uint32_t chars = jcall(S_GetStringUTFChars, w4, 3)->r[0];
    NJ_EQ_STR(hle_cstr(chars), "eng");
    uint32_t w5[] = {env(), str, chars};
    jcall(S_ReleaseStringUTFChars, w5, 3);
}

static float probe_float;
static uint32_t probe_int;
static uint64_t probe_take(uint32_t self, const NjJValue *a, int n) {
    (void)self;
    NJ_CHECK(n == 2);
    probe_int = a[0].i;
    probe_float = a[1].f;
    return 0;
}

NJ_TEST(jni_float_vararg) {
    setup();
    nj_java_register("test/Probe", "take", "(IF)V", probe_take);
    uint32_t cls = nj_jni_class("test/Probe");
    uint32_t w1[] = {env(), cls, gs("take"), gs("(IF)V")};
    uint32_t mid = jcall(S_GetStaticMethodID, w1, 4)->r[0];
    double half = 0.5;
    uint64_t b;
    memcpy(&b, &half, 8);
    /* CallStaticVoidMethod(env, cls, mid, 7, 0.5): 7 in r3, the float promoted to double in the 8-aligned stack slot */
    uint32_t w2[] = {env(), cls, mid, 7, (uint32_t)b, (uint32_t)(b >> 32)};
    jcall(S_CallStaticVoidMethod, w2, 6);
    NJ_EQ_U(probe_int, 7);
    NJ_CHECK(probe_float == 0.5f);
    /* A-variant: jvalue[] with 8-byte elements, float stored as float */
    uint32_t jv = nj_calloc(2, 8);
    nj_w32(jv, 9);
    float q = 0.25f;
    uint32_t qb;
    memcpy(&qb, &q, 4);
    nj_w32(jv + 8, qb);
    uint32_t w3[] = {env(), cls, mid, jv};
    jcall(S_CallStaticVoidMethodA, w3, 4);
    NJ_EQ_U(probe_int, 9);
    NJ_CHECK(probe_float == 0.25f);
}

NJ_TEST(jni_unimplemented_slot) {
    setup();
    uint32_t w[] = {env(), 0};
    NJ_EXPECT_FATAL(jcall(S_GetObjectRefType, w, 2), "GetObjectRefType");
}

NJ_TEST(jni_object_array_fields) {
    setup();
    uint32_t arr = nj_jni_new_object_array("com/wbgames/LEGOgame/Fusion$APKZipFileEntry", 2);
    for (uint32_t i = 0; i < 2; i++) {
        uint32_t e = nj_jni_object("com/wbgames/LEGOgame/Fusion$APKZipFileEntry");
        nj_jni_set_field(e, "fileName", "Ljava/lang/String;", nj_jni_new_string(i ? "b.fib" : "a.fib"));
        nj_jni_set_field(e, "offset", "J", 0x123456789ull + i);
        nj_jni_set_field(e, "length", "J", 1000 + i);
        nj_jni_array_set(arr, i, e);
    }
    uint32_t w1[] = {env(), arr};
    NJ_EQ_U(jcall(S_GetArrayLength, w1, 2)->r[0], 2);
    uint32_t w2[] = {env(), arr, 1};
    uint32_t e1 = jcall(S_GetObjectArrayElement, w2, 3)->r[0];
    uint32_t w3[] = {env(), e1};
    uint32_t cls = jcall(S_GetObjectClass, w3, 2)->r[0];
    uint32_t w4[] = {env(), cls, gs("offset"), gs("J")};
    uint32_t fid = jcall(S_GetFieldID, w4, 4)->r[0];
    uint32_t w5[] = {env(), e1, fid};
    NjCtx *c = jcall(S_GetLongField, w5, 3);
    NJ_EQ_U((uint64_t)c->r[0] | (uint64_t)c->r[1] << 32, 0x12345678Aull);
    uint32_t w6[] = {env(), cls, gs("fileName"), gs("Ljava/lang/String;")};
    uint32_t fname = jcall(S_GetFieldID, w6, 4)->r[0];
    uint32_t w7[] = {env(), e1, fname};
    uint32_t s = jcall(S_GetObjectField, w7, 3)->r[0];
    NJ_EQ_STR(nj_jni_string_chars(s), "b.fib");
}

NJ_TEST(jni_missing_field_exception) {
    setup();
    uint32_t cfg = nj_jni_object("javax/microedition/khronos/egl/EGLConfig");
    nj_jni_set_field(cfg, "mEGLConfig", "I", 77);
    uint32_t cls = nj_jni_class("javax/microedition/khronos/egl/EGLConfig");
    uint32_t w1[] = {env(), cls, gs("mEGLConfig"), gs("J")};
    NJ_EQ_U(jcall(S_GetFieldID, w1, 4)->r[0], 0);
    uint32_t we[] = {env()};
    NJ_EQ_U(jcall(S_ExceptionCheck, we, 1)->r[0], 1);
    NJ_CHECK(jcall(S_ExceptionOccurred, we, 1)->r[0] != 0);
    jcall(S_ExceptionClear, we, 1);
    NJ_EQ_U(jcall(S_ExceptionCheck, we, 1)->r[0], 0);
    uint32_t w2[] = {env(), cls, gs("mEGLConfig"), gs("I")};
    uint32_t fid = jcall(S_GetFieldID, w2, 4)->r[0];
    NJ_CHECK(fid != 0);
    uint32_t w3[] = {env(), cfg, fid};
    NJ_EQ_U(jcall(S_GetIntField, w3, 3)->r[0], 77);
}

NJ_TEST(jni_classloader) {
    setup();
    uint32_t act = nj_jni_object("com/lego/ninjago/toe/GameActivity");
    uint32_t w0[] = {env(), act};
    uint32_t acls = jcall(S_GetObjectClass, w0, 2)->r[0];
    uint32_t w1[] = {env(), acls, gs("getClassLoader"), gs("()Ljava/lang/ClassLoader;")};
    uint32_t mid = jcall(S_GetMethodID, w1, 4)->r[0];
    NJ_CHECK(mid != 0);
    uint32_t w2[] = {env(), act, mid};
    uint32_t loader = jcall(S_CallObjectMethod, w2, 3)->r[0];
    NJ_CHECK(loader != 0);
    uint32_t w3[] = {env(), loader};
    uint32_t lcls = jcall(S_GetObjectClass, w3, 2)->r[0];
    uint32_t w4[] = {env(), lcls, gs("loadClass"), gs("(Ljava/lang/String;Z)Ljava/lang/Class;")};
    uint32_t lmid = jcall(S_GetMethodID, w4, 4)->r[0];
    uint32_t name = nj_jni_new_string("com.wbgames.LEGOgame.FusionCallback");
    uint32_t w5[] = {env(), loader, lmid, name, 0};
    uint32_t k = jcall(S_CallObjectMethod, w5, 5)->r[0];
    uint32_t w6[] = {env(), gs("com/wbgames/LEGOgame/FusionCallback")};
    NJ_EQ_U(k, jcall(S_FindClass, w6, 2)->r[0]);
}

NJ_TEST(jni_unknown_class_null) {
    setup();
    uint32_t w1[] = {env(), gs("com/amazon/ags/api/AmazonGamesClient")};
    NJ_EQ_U(jcall(S_FindClass, w1, 2)->r[0], 0);
    uint32_t we[] = {env()};
    NJ_EQ_U(jcall(S_ExceptionCheck, we, 1)->r[0], 1);
    jcall(S_ExceptionClear, we, 1);
}

NJ_TEST(javavm_getenv) {
    setup();
    uint32_t out = nj_malloc(4);
    uint32_t w[] = {env(), out};
    NJ_EQ_U(jcall(S_GetJavaVM, w, 2)->r[0], 0);
    uint32_t vm = nj_r32(out);
    NJ_EQ_U(vm, nj_jni_vm());
    NjCtx *c = nj_ctx();
    c->r[0] = vm; c->r[1] = out; c->r[2] = 0x00010006;
    hle_dispatch(c, nj_r32(nj_r32(vm) + 4 * 6)); /* GetEnv */
    NJ_EQ_U(c->r[0], 0);
    NJ_EQ_U(nj_r32(out), env());
}

NJ_TEST(jni_call_native_marshal) {
    setup();
    uint32_t thiz = nj_jni_class("com/wbgames/LEGOgame/Fusion");
    NJ_EQ_U((uint32_t)nj_jni_call_addr(FN_RET_R0, thiz, "(IF)V", 5, 1.5), env());
    NJ_EQ_U((uint32_t)nj_jni_call_addr(FN_RET_R1, thiz, "(IF)V", 5, 1.5), thiz);
    NJ_EQ_U((uint32_t)nj_jni_call_addr(FN_RET_R2, thiz, "(IF)V", 5, 1.5), 5);
    float f = 1.5f;
    uint32_t fb;
    memcpy(&fb, &f, 4);
    NJ_EQ_U((uint32_t)nj_jni_call_addr(FN_RET_R3, thiz, "(IF)V", 5, 1.5), fb);
    /* "(IJ)V": the long skips r3 and lands in the 8-aligned stack slot */
    NJ_EQ_U((uint32_t)nj_jni_call_addr(FN_RET_SP0, thiz, "(IJ)V", 5, 0x1122334455667788ll), 0x55667788u);
}

enum { S_DeleteLocalRef = 23, S_NewGlobalRef = 21, S_DeleteGlobalRef = 22 };

NJ_TEST(jni_local_refs_are_reclaimed) {
    setup();
    uint32_t base = nj_jni_live_objects();
    uint32_t s = gs("x");
    for (int i = 0; i < 300000; i++) { /* more than the table holds: must not hit "table full" */
        uint32_t w1[] = {env(), s};
        uint32_t h = jcall(S_NewStringUTF, w1, 2)->r[0];
        uint32_t w2[] = {env(), h};
        jcall(S_DeleteLocalRef, w2, 2);
    }
    NJ_EQ_U(nj_jni_live_objects(), base);
}

NJ_TEST(jni_frame_releases_locals_keeps_globals) {
    setup();
    uint32_t base = nj_jni_live_objects();
    nj_jni_push_frame();
    uint32_t w1[] = {env(), gs("a")};
    jcall(S_NewStringUTF, w1, 2);
    uint32_t keep = jcall(S_NewStringUTF, w1, 2)->r[0];
    uint32_t w2[] = {env(), keep};
    uint32_t g = jcall(S_NewGlobalRef, w2, 2)->r[0];
    nj_jni_pop_frame();
    NJ_EQ_U(nj_jni_live_objects(), base + 1); /* only the globally referenced string survives */
    NJ_EQ_STR(nj_jni_string_chars(g), "a");
    uint32_t w3[] = {env(), g};
    jcall(S_DeleteGlobalRef, w3, 2);
    NJ_EQ_U(nj_jni_live_objects(), base);
}
