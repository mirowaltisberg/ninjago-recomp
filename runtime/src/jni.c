/* JNI facade. Java objects live host-side behind opaque 32-bit handles; the guest sees standard
 * JavaVM/JNIEnv function tables whose slots are HLE thunks named "JNI.<Function>". */
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nj/backend.h"
#include "nj/format.h"
#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/jni.h"

#define A(i) hle_arg(c, i)

static const char *const slot_names[NJ_JNI_FUNCS] = {
    NULL, NULL, NULL, NULL, "GetVersion", "DefineClass", "FindClass", "FromReflectedMethod", "FromReflectedField",
    "ToReflectedMethod", "GetSuperclass", "IsAssignableFrom", "ToReflectedField", "Throw", "ThrowNew",
    "ExceptionOccurred", "ExceptionDescribe", "ExceptionClear", "FatalError", "PushLocalFrame", "PopLocalFrame",
    "NewGlobalRef", "DeleteGlobalRef", "DeleteLocalRef", "IsSameObject", "NewLocalRef", "EnsureLocalCapacity",
    "AllocObject", "NewObject", "NewObjectV", "NewObjectA", "GetObjectClass", "IsInstanceOf", "GetMethodID",
    "CallObjectMethod", "CallObjectMethodV", "CallObjectMethodA", "CallBooleanMethod", "CallBooleanMethodV",
    "CallBooleanMethodA", "CallByteMethod", "CallByteMethodV", "CallByteMethodA", "CallCharMethod",
    "CallCharMethodV", "CallCharMethodA", "CallShortMethod", "CallShortMethodV", "CallShortMethodA",
    "CallIntMethod", "CallIntMethodV", "CallIntMethodA", "CallLongMethod", "CallLongMethodV", "CallLongMethodA",
    "CallFloatMethod", "CallFloatMethodV", "CallFloatMethodA", "CallDoubleMethod", "CallDoubleMethodV",
    "CallDoubleMethodA", "CallVoidMethod", "CallVoidMethodV", "CallVoidMethodA", "CallNonvirtualObjectMethod",
    "CallNonvirtualObjectMethodV", "CallNonvirtualObjectMethodA", "CallNonvirtualBooleanMethod",
    "CallNonvirtualBooleanMethodV", "CallNonvirtualBooleanMethodA", "CallNonvirtualByteMethod",
    "CallNonvirtualByteMethodV", "CallNonvirtualByteMethodA", "CallNonvirtualCharMethod",
    "CallNonvirtualCharMethodV", "CallNonvirtualCharMethodA", "CallNonvirtualShortMethod",
    "CallNonvirtualShortMethodV", "CallNonvirtualShortMethodA", "CallNonvirtualIntMethod",
    "CallNonvirtualIntMethodV", "CallNonvirtualIntMethodA", "CallNonvirtualLongMethod",
    "CallNonvirtualLongMethodV", "CallNonvirtualLongMethodA", "CallNonvirtualFloatMethod",
    "CallNonvirtualFloatMethodV", "CallNonvirtualFloatMethodA", "CallNonvirtualDoubleMethod",
    "CallNonvirtualDoubleMethodV", "CallNonvirtualDoubleMethodA", "CallNonvirtualVoidMethod",
    "CallNonvirtualVoidMethodV", "CallNonvirtualVoidMethodA", "GetFieldID", "GetObjectField", "GetBooleanField",
    "GetByteField", "GetCharField", "GetShortField", "GetIntField", "GetLongField", "GetFloatField",
    "GetDoubleField", "SetObjectField", "SetBooleanField", "SetByteField", "SetCharField", "SetShortField",
    "SetIntField", "SetLongField", "SetFloatField", "SetDoubleField", "GetStaticMethodID",
    "CallStaticObjectMethod", "CallStaticObjectMethodV", "CallStaticObjectMethodA", "CallStaticBooleanMethod",
    "CallStaticBooleanMethodV", "CallStaticBooleanMethodA", "CallStaticByteMethod", "CallStaticByteMethodV",
    "CallStaticByteMethodA", "CallStaticCharMethod", "CallStaticCharMethodV", "CallStaticCharMethodA",
    "CallStaticShortMethod", "CallStaticShortMethodV", "CallStaticShortMethodA", "CallStaticIntMethod",
    "CallStaticIntMethodV", "CallStaticIntMethodA", "CallStaticLongMethod", "CallStaticLongMethodV",
    "CallStaticLongMethodA", "CallStaticFloatMethod", "CallStaticFloatMethodV", "CallStaticFloatMethodA",
    "CallStaticDoubleMethod", "CallStaticDoubleMethodV", "CallStaticDoubleMethodA", "CallStaticVoidMethod",
    "CallStaticVoidMethodV", "CallStaticVoidMethodA", "GetStaticFieldID", "GetStaticObjectField",
    "GetStaticBooleanField", "GetStaticByteField", "GetStaticCharField", "GetStaticShortField",
    "GetStaticIntField", "GetStaticLongField", "GetStaticFloatField", "GetStaticDoubleField",
    "SetStaticObjectField", "SetStaticBooleanField", "SetStaticByteField", "SetStaticCharField",
    "SetStaticShortField", "SetStaticIntField", "SetStaticLongField", "SetStaticFloatField",
    "SetStaticDoubleField", "NewString", "GetStringLength", "GetStringChars", "ReleaseStringChars",
    "NewStringUTF", "GetStringUTFLength", "GetStringUTFChars", "ReleaseStringUTFChars", "GetArrayLength",
    "NewObjectArray", "GetObjectArrayElement", "SetObjectArrayElement", "NewBooleanArray", "NewByteArray",
    "NewCharArray", "NewShortArray", "NewIntArray", "NewLongArray", "NewFloatArray", "NewDoubleArray",
    "GetBooleanArrayElements", "GetByteArrayElements", "GetCharArrayElements", "GetShortArrayElements",
    "GetIntArrayElements", "GetLongArrayElements", "GetFloatArrayElements", "GetDoubleArrayElements",
    "ReleaseBooleanArrayElements", "ReleaseByteArrayElements", "ReleaseCharArrayElements",
    "ReleaseShortArrayElements", "ReleaseIntArrayElements", "ReleaseLongArrayElements",
    "ReleaseFloatArrayElements", "ReleaseDoubleArrayElements", "GetBooleanArrayRegion", "GetByteArrayRegion",
    "GetCharArrayRegion", "GetShortArrayRegion", "GetIntArrayRegion", "GetLongArrayRegion",
    "GetFloatArrayRegion", "GetDoubleArrayRegion", "SetBooleanArrayRegion", "SetByteArrayRegion",
    "SetCharArrayRegion", "SetShortArrayRegion", "SetIntArrayRegion", "SetLongArrayRegion",
    "SetFloatArrayRegion", "SetDoubleArrayRegion", "RegisterNatives", "UnregisterNatives", "MonitorEnter",
    "MonitorExit", "GetJavaVM", "GetStringRegion", "GetStringUTFRegion", "GetPrimitiveArrayCritical",
    "ReleasePrimitiveArrayCritical", "GetStringCritical", "ReleaseStringCritical", "NewWeakGlobalRef",
    "DeleteWeakGlobalRef", "ExceptionCheck", "NewDirectByteBuffer", "GetDirectBufferAddress",
    "GetDirectBufferCapacity", "GetObjectRefType"};

static const char *const vm_names[NJ_JNI_INVOKE_FUNCS] = {
    NULL, NULL, NULL, "DestroyJavaVM", "AttachCurrentThread", "DetachCurrentThread", "GetEnv",
    "AttachCurrentThreadAsDaemon"};

const char *nj_jni_slot_name(int slot) { return slot >= 0 && slot < NJ_JNI_FUNCS ? slot_names[slot] : NULL; }

/* ---- host-side objects ---- */

typedef enum { K_CLASS = 1, K_OBJECT, K_STRING, K_ARRAY } Kind;

typedef struct Field { char *name, *sig; uint64_t value; struct Field *next; } Field;

typedef struct {
    Kind kind;
    char *cls;   /* class name (for K_CLASS: the class itself) */
    char *str;   /* K_STRING */
    uint32_t *elems, n; /* K_ARRAY */
    Field *fields;
    uint32_t local_refs, global_refs;
    uint8_t pinned;    /* classes and host-created objects outside native calls: never freed */
    uint8_t contained; /* stored in an array or object field: never freed (no tracing collector) */
} JObj;

#define OBJ_BASE 0x10000000u
#define MID_BASE 0x20000000u
#define FID_BASE 0x30000000u
#define MAX_OBJS 262144u

static JObj *objs[MAX_OBJS];
static uint32_t nobjs = 1; /* high-water mark of used indices */
static uint32_t free_idx[MAX_OBJS], nfree;
static uint32_t live; /* non-class objects alive */
static pthread_mutex_t jlock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;

/* Per-thread local references; frames mark where each native call's locals start. */
typedef struct {
    uint32_t *h;
    uint32_t n, cap;
    uint32_t frames[64];
    int depth;
} Locals;
static _Thread_local Locals locals;

static void locals_add(uint32_t h) {
    if (locals.n == locals.cap && locals.depth == 0) { /* outside native calls: drop deleted slots first */
        uint32_t k = 0;
        for (uint32_t i = 0; i < locals.n; i++) if (locals.h[i]) locals.h[k++] = locals.h[i];
        locals.n = k;
    }
    if (locals.n == locals.cap) {
        locals.cap = locals.cap ? locals.cap * 2 : 256;
        locals.h = realloc(locals.h, locals.cap * sizeof *locals.h);
    }
    locals.h[locals.n++] = h;
}

static uint32_t idx_of(uint32_t h) {
    if (h < OBJ_BASE + 4 || h >= OBJ_BASE + 4 * MAX_OBJS || (h & 3)) return 0;
    return (h - OBJ_BASE) / 4;
}

/* local != 0: the new object holds one local reference of the calling thread; otherwise it is pinned. */
static uint32_t new_obj(Kind k, const char *cls, int local) {
    JObj *o = calloc(1, sizeof *o);
    o->kind = k;
    o->cls = strdup(cls);
    if (local && k != K_CLASS) o->local_refs = 1;
    else o->pinned = 1;
    pthread_mutex_lock(&jlock);
    uint32_t idx;
    if (nfree) idx = free_idx[--nfree];
    else if (nobjs < MAX_OBJS) idx = nobjs++;
    else { pthread_mutex_unlock(&jlock); nj_fatal("JNI object table full (%u live)", live); }
    objs[idx] = o;
    if (k != K_CLASS) live++;
    pthread_mutex_unlock(&jlock);
    uint32_t h = OBJ_BASE + 4 * idx;
    if (o->local_refs) locals_add(h);
    return h;
}

static JObj *obj(uint32_t h) {
    uint32_t idx = idx_of(h);
    pthread_mutex_lock(&jlock);
    JObj *o = idx && idx < nobjs ? objs[idx] : NULL;
    pthread_mutex_unlock(&jlock);
    return o;
}

static JObj *obj_or_fatal(uint32_t h, const char *what) {
    JObj *o = obj(h);
    if (!o) nj_fatal("JNI %s: invalid or freed object handle 0x%08x", what, h);
    return o;
}

/* Caller holds jlock. */
static void maybe_free(uint32_t idx) {
    JObj *o = objs[idx];
    if (!o || o->pinned || o->contained || o->local_refs || o->global_refs) return;
    for (Field *f = o->fields; f;) { Field *n = f->next; free(f->name); free(f->sig); free(f); f = n; }
    free(o->cls);
    free(o->str);
    free(o->elems);
    free(o);
    objs[idx] = NULL;
    free_idx[nfree++] = idx;
    live--;
}

static void release_local(uint32_t h) {
    uint32_t idx = idx_of(h);
    pthread_mutex_lock(&jlock);
    JObj *o = idx && idx < nobjs ? objs[idx] : NULL;
    if (o && o->local_refs) { o->local_refs--; maybe_free(idx); }
    pthread_mutex_unlock(&jlock);
}

static void mark_contained(uint32_t h) {
    JObj *o = obj(h);
    if (o) o->contained = 1;
}

void nj_jni_push_frame(void) {
    if (locals.depth == 64) nj_fatal("JNI local frames nested too deeply");
    locals.frames[locals.depth++] = locals.n;
}

static _Thread_local uint32_t pending;

void nj_jni_pop_frame(void) {
    if (!locals.depth) return;
    uint32_t start = locals.frames[--locals.depth];
    for (uint32_t i = start; i < locals.n; i++) if (locals.h[i]) release_local(locals.h[i]);
    locals.n = start;
    if (!locals.depth) pending = 0; /* an exception does not outlive the native call that raised it */
}

uint32_t nj_jni_live_objects(void) {
    pthread_mutex_lock(&jlock);
    uint32_t n = live;
    pthread_mutex_unlock(&jlock);
    return n;
}

void nj_jni_pin(uint32_t h) {
    JObj *o = obj(h);
    if (o) o->pinned = 1;
}

/* Host-created objects are locals inside a native call (Java return values) and pinned otherwise. */
static int host_local(void) { return locals.depth > 0; }

/* classes: interned; "declared" ones are visible to FindClass */
#define MAX_CLASSES 256
static struct { char *name; uint32_t h; int declared; } classes[MAX_CLASSES];
static int nclasses;

static uint32_t class_handle(const char *name, int declare, int create) {
    pthread_mutex_lock(&jlock);
    uint32_t h = 0;
    int i = 0;
    for (; i < nclasses; i++)
        if (strcmp(classes[i].name, name) == 0) {
            if (declare) classes[i].declared = 1;
            h = (classes[i].declared || create) ? classes[i].h : 0;
            break;
        }
    if (i == nclasses && (create || declare)) {
        if (nclasses == MAX_CLASSES) { pthread_mutex_unlock(&jlock); nj_fatal("JNI class table full"); }
        h = new_obj(K_CLASS, name, 0);
        classes[nclasses].name = strdup(name);
        classes[nclasses].h = h;
        classes[nclasses].declared = declare;
        nclasses++;
    }
    pthread_mutex_unlock(&jlock);
    return h;
}

uint32_t nj_jni_class(const char *name) { return class_handle(name, 1, 1); }
void nj_jni_declare_class(const char *name) { class_handle(name, 1, 1); }
uint32_t nj_jni_object(const char *name) { nj_jni_declare_class(name); return new_obj(K_OBJECT, name, host_local()); }

static uint32_t new_string(const char *utf8, int local) {
    uint32_t h = new_obj(K_STRING, "java/lang/String", local);
    obj(h)->str = strdup(utf8 ? utf8 : "");
    return h;
}

uint32_t nj_jni_new_string(const char *utf8) { return new_string(utf8, host_local()); }

const char *nj_jni_string_chars(uint32_t s) {
    JObj *o = obj(s);
    return o && o->kind == K_STRING ? o->str : NULL;
}

static uint32_t new_object_array(const char *elem_class, uint32_t n, int local) {
    char name[300];
    snprintf(name, sizeof name, "[L%s;", elem_class);
    uint32_t h = new_obj(K_ARRAY, name, local);
    JObj *o = obj(h);
    o->elems = calloc(n ? n : 1, 4);
    o->n = n;
    return h;
}

uint32_t nj_jni_new_object_array(const char *elem_class, uint32_t n) { return new_object_array(elem_class, n, host_local()); }

void nj_jni_array_set(uint32_t array, uint32_t index, uint32_t value) {
    JObj *o = obj_or_fatal(array, "array set");
    if (o->kind != K_ARRAY || index >= o->n) nj_fatal("JNI array index %u out of range", index);
    o->elems[index] = value;
    if (value) mark_contained(value);
}

/* field descriptors (per class) give jfieldIDs; values live on the objects */
#define MAX_FIELDS 512
static struct { char *cls, *name, *sig; } fdesc[MAX_FIELDS];
static uint32_t nfdesc;

static uint32_t field_id(const char *cls, const char *name, const char *sig, int create) {
    pthread_mutex_lock(&jlock);
    uint32_t id = 0;
    for (uint32_t i = 0; i < nfdesc && !id; i++)
        if (!strcmp(fdesc[i].cls, cls) && !strcmp(fdesc[i].name, name) && !strcmp(fdesc[i].sig, sig))
            id = FID_BASE + 4 * i;
    if (!id && create) {
        if (nfdesc == MAX_FIELDS) { pthread_mutex_unlock(&jlock); nj_fatal("JNI field table full"); }
        fdesc[nfdesc].cls = strdup(cls);
        fdesc[nfdesc].name = strdup(name);
        fdesc[nfdesc].sig = strdup(sig);
        id = FID_BASE + 4 * nfdesc++;
    }
    pthread_mutex_unlock(&jlock);
    return id;
}

static Field *field_on(JObj *o, const char *name, int create) {
    pthread_mutex_lock(&jlock);
    Field *f = o->fields;
    while (f && strcmp(f->name, name)) f = f->next;
    if (!f && create) {
        f = calloc(1, sizeof *f);
        f->name = strdup(name);
        f->next = o->fields;
        o->fields = f;
    }
    pthread_mutex_unlock(&jlock);
    return f;
}

void nj_jni_set_field(uint32_t h, const char *name, const char *sig, uint64_t value) {
    JObj *o = obj_or_fatal(h, "set field");
    field_id(o->cls, name, sig, 1);
    Field *f = field_on(o, name, 1);
    free(f->sig);
    f->sig = strdup(sig);
    f->value = value;
    if ((sig[0] == 'L' || sig[0] == '[') && value) mark_contained((uint32_t)value);
}

/* ---- emulated methods ---- */

#define MAX_METHODS 256
static struct { char *cls, *name, *sig; NjJavaFn fn; } methods[MAX_METHODS];
static uint32_t nmethods;

void nj_java_register(const char *cls, const char *name, const char *sig, NjJavaFn fn) {
    nj_jni_declare_class(cls);
    pthread_mutex_lock(&jlock);
    for (uint32_t i = 0; i < nmethods; i++)
        if (!strcmp(methods[i].cls, cls) && !strcmp(methods[i].name, name) && !strcmp(methods[i].sig, sig)) {
            methods[i].fn = fn;
            pthread_mutex_unlock(&jlock);
            return;
        }
    if (nmethods == MAX_METHODS) { pthread_mutex_unlock(&jlock); nj_fatal("JNI method table full"); }
    methods[nmethods].cls = strdup(cls);
    methods[nmethods].name = strdup(name);
    methods[nmethods].sig = strdup(sig);
    methods[nmethods].fn = fn;
    nmethods++;
    pthread_mutex_unlock(&jlock);
}

static uint32_t method_id(const char *cls, const char *name, const char *sig) {
    for (uint32_t i = 0; i < nmethods; i++)
        if (!strcmp(methods[i].cls, cls) && !strcmp(methods[i].name, name) && !strcmp(methods[i].sig, sig))
            return MID_BASE + 4 * i;
    for (uint32_t i = 0; i < nmethods; i++) /* inherited methods: match by name and signature */
        if (!strcmp(methods[i].name, name) && !strcmp(methods[i].sig, sig)) return MID_BASE + 4 * i;
    return 0;
}

/* ---- exceptions ---- */

void nj_jni_throw(const char *exc_class) { pending = nj_jni_object(exc_class); }

/* ---- signature parsing ---- */

/* Fills types with one char per parameter (L for objects/arrays); returns the return type char. */
static char parse_sig(const char *sig, char *types, int *n) {
    *n = 0;
    const char *p = sig;
    if (*p == '(') p++;
    while (*p && *p != ')') {
        char t = *p;
        while (*p == '[') { t = 'L'; p++; }
        if (*p == 'L') { t = 'L'; p = strchr(p, ';'); if (!p) break; }
        else if (t != 'L') t = *p;
        types[(*n)++] = t;
        p++;
    }
    if (*p == ')') p++;
    return *p == '[' ? 'L' : *p;
}

/* ---- JNI functions ---- */

static void j_GetVersion(NjCtx *c) { hle_ret(c, 0x00010006); }

static void j_FindClass(NjCtx *c) {
    const char *name = hle_cstr(A(1));
    uint32_t h = name ? class_handle(name, 0, 0) : 0;
    if (!h) {
        fprintf(stderr, "[jni] FindClass(%s): not available\n", name ? name : "(null)");
        nj_jni_throw("java/lang/NoClassDefFoundError");
    }
    hle_ret(c, h);
}

static void j_Throw(NjCtx *c) { pending = A(1); hle_ret(c, 0); }
static void j_ThrowNew(NjCtx *c) {
    JObj *k = obj(A(1));
    fprintf(stderr, "[jni] ThrowNew(%s, %s)\n", k ? k->cls : "?", hle_cstr(A(2)) ? hle_cstr(A(2)) : "");
    pending = nj_jni_object(k ? k->cls : "java/lang/Throwable");
    hle_ret(c, 0);
}
static void j_ExceptionOccurred(NjCtx *c) { hle_ret(c, pending); }
static void j_ExceptionDescribe(NjCtx *c) {
    (void)c;
    JObj *o = obj(pending);
    if (o) fprintf(stderr, "[jni] pending exception: %s\n", o->cls);
}
static void j_ExceptionClear(NjCtx *c) { (void)c; pending = 0; }
static void j_ExceptionCheck(NjCtx *c) { hle_ret(c, pending ? 1 : 0); }
static void j_FatalError(NjCtx *c) { nj_fatal("JNI FatalError: %s", hle_cstr(A(1)) ? hle_cstr(A(1)) : ""); }
static void j_PushLocalFrame(NjCtx *c) { hle_ret(c, 0); }
static void j_PopLocalFrame(NjCtx *c) { hle_ret(c, A(1)); }
static void j_NewGlobalRef(NjCtx *c) { /* also NewWeakGlobalRef: weak references keep objects alive */
    JObj *o = obj(A(1));
    if (o) { pthread_mutex_lock(&jlock); o->global_refs++; pthread_mutex_unlock(&jlock); }
    hle_ret(c, o ? A(1) : 0);
}
static void j_DeleteGlobalRef(NjCtx *c) {
    uint32_t idx = idx_of(A(1));
    pthread_mutex_lock(&jlock);
    JObj *o = idx && idx < nobjs ? objs[idx] : NULL;
    if (o && o->global_refs) { o->global_refs--; maybe_free(idx); }
    pthread_mutex_unlock(&jlock);
}
static void j_NewLocalRef(NjCtx *c) {
    JObj *o = obj(A(1));
    if (o && !o->pinned) {
        pthread_mutex_lock(&jlock);
        o->local_refs++;
        pthread_mutex_unlock(&jlock);
        locals_add(A(1));
    }
    hle_ret(c, o ? A(1) : 0);
}
/* Deleting a reference that is not one of this thread's locals (e.g. an array element) is a no-op. */
static void j_DeleteLocalRef(NjCtx *c) {
    uint32_t h = A(1);
    for (uint32_t i = locals.n; i-- > 0;)
        if (locals.h[i] == h) { locals.h[i] = 0; release_local(h); return; }
}
static void j_IsSameObject(NjCtx *c) { hle_ret(c, A(1) == A(2)); }
static void j_EnsureLocalCapacity(NjCtx *c) { hle_ret(c, 0); }

static void j_AllocObject(NjCtx *c) {
    JObj *k = obj_or_fatal(A(1), "AllocObject");
    hle_ret(c, new_obj(K_OBJECT, k->cls, 1));
}

static void j_GetObjectClass(NjCtx *c) {
    JObj *o = obj_or_fatal(A(1), "GetObjectClass");
    hle_ret(c, o->kind == K_CLASS ? nj_jni_class("java/lang/Class") : nj_jni_class(o->cls));
}

static void j_IsInstanceOf(NjCtx *c) {
    JObj *o = obj(A(1)), *k = obj(A(2));
    if (!A(1)) { hle_ret(c, 1); return; }
    hle_ret(c, o && k && (!strcmp(o->cls, k->cls) || !strcmp(k->cls, "java/lang/Object")));
}

static void get_method(NjCtx *c) {
    JObj *k = obj_or_fatal(A(1), "GetMethodID");
    const char *name = hle_cstr(A(2)), *sig = hle_cstr(A(3));
    uint32_t mid = method_id(k->cls, name, sig);
    if (!mid) {
        fprintf(stderr, "[jni] method %s.%s%s not available\n", k->cls, name, sig);
        nj_jni_throw("java/lang/NoSuchMethodError");
    }
    hle_ret(c, mid);
}

static void get_field(NjCtx *c) {
    JObj *k = obj_or_fatal(A(1), "GetFieldID");
    uint32_t fid = field_id(k->cls, hle_cstr(A(2)), hle_cstr(A(3)), 0);
    if (!fid) nj_jni_throw("java/lang/NoSuchFieldError");
    hle_ret(c, fid);
}

static Field *field_for(uint32_t h, uint32_t fid, const char *what) {
    JObj *o = obj_or_fatal(h, what);
    uint32_t i = (fid - FID_BASE) / 4;
    if (fid < FID_BASE || i >= nfdesc) nj_fatal("JNI %s: invalid field id 0x%08x", what, fid);
    return field_on(o, fdesc[i].name, 1);
}

static void j_GetField32(NjCtx *c) { hle_ret(c, (uint32_t)field_for(A(1), A(2), "Get<T>Field")->value); }
static void j_GetField64(NjCtx *c) { hle_ret64(c, field_for(A(1), A(2), "Get<T>Field")->value); }
static void j_SetField32(NjCtx *c) {
    Field *f = field_for(A(1), A(2), "Set<T>Field");
    f->value = A(3);
    uint32_t i = (A(2) - FID_BASE) / 4;
    if ((fdesc[i].sig[0] == 'L' || fdesc[i].sig[0] == '[') && A(3)) mark_contained(A(3));
}
static void j_SetField64(NjCtx *c) { field_for(A(1), A(2), "Set<T>Field")->value = hle_arg64(c, 4); }

/* Call<Type>Method / CallNonvirtual / CallStatic, variants: 0 = varargs, 1 = va_list, 2 = jvalue[] */
static void do_call(NjCtx *c, int nonvirt, int variant, char ret) {
    uint32_t self = A(1), mid = nonvirt ? A(3) : A(2);
    int first = nonvirt ? 4 : 3;
    uint32_t i = (mid - MID_BASE) / 4;
    if (mid < MID_BASE || i >= nmethods) nj_fatal("JNI call with invalid method id 0x%08x", mid);
    char types[64];
    int n;
    parse_sig(methods[i].sig, types, &n);
    NjJValue args[64];
    NjVaCursor va = variant == 0 ? nj_va_from_args(c, first) : nj_va_from_list(variant == 1 ? A(first) : 0);
    uint32_t jv = variant == 2 ? A(first) : 0;
    for (int k = 0; k < n; k++) {
        NjJValue v = {0};
        if (variant == 2) {
            uint32_t e = jv + 8u * (uint32_t)k;
            switch (types[k]) {
            case 'Z': v.i = nj_r8(e); break;
            case 'B': v.i = (uint32_t)(int32_t)(int8_t)nj_r8(e); break;
            case 'C': v.i = nj_r16(e); break;
            case 'S': v.i = (uint32_t)(int32_t)(int16_t)nj_r16(e); break;
            case 'J': v.j = nj_r64(e); break;
            case 'D': { uint64_t b = nj_r64(e); memcpy(&v.d, &b, 8); break; }
            case 'F': { uint32_t b = nj_r32(e); memcpy(&v.f, &b, 4); break; }
            default: v.i = nj_r32(e); break;
            }
        } else {
            switch (types[k]) {
            case 'J': v.j = nj_va_u64(&va); break;
            case 'D': v.d = nj_va_f64(&va); break;
            case 'F': v.f = (float)nj_va_f64(&va); break; /* C default promotion float -> double */
            default: v.i = nj_va_u32(&va); break;
            }
        }
        args[k] = v;
    }
    uint64_t r = methods[i].fn(self, args, n);
    if (ret == 'J' || ret == 'D') hle_ret64(c, r);
    else if (ret != 'V') hle_ret(c, (uint32_t)r);
}

#define CALL_VARIANTS(T, ch)                                                                   \
    static void j_Call##T##Method(NjCtx *c) { do_call(c, 0, 0, ch); }                          \
    static void j_Call##T##MethodV(NjCtx *c) { do_call(c, 0, 1, ch); }                         \
    static void j_Call##T##MethodA(NjCtx *c) { do_call(c, 0, 2, ch); }                         \
    static void j_CallNonvirtual##T##Method(NjCtx *c) { do_call(c, 1, 0, ch); }                \
    static void j_CallNonvirtual##T##MethodV(NjCtx *c) { do_call(c, 1, 1, ch); }               \
    static void j_CallNonvirtual##T##MethodA(NjCtx *c) { do_call(c, 1, 2, ch); }               \
    static void j_CallStatic##T##Method(NjCtx *c) { do_call(c, 0, 0, ch); }                    \
    static void j_CallStatic##T##MethodV(NjCtx *c) { do_call(c, 0, 1, ch); }                   \
    static void j_CallStatic##T##MethodA(NjCtx *c) { do_call(c, 0, 2, ch); }

CALL_VARIANTS(Object, 'L')
CALL_VARIANTS(Boolean, 'Z')
CALL_VARIANTS(Byte, 'B')
CALL_VARIANTS(Char, 'C')
CALL_VARIANTS(Short, 'S')
CALL_VARIANTS(Int, 'I')
CALL_VARIANTS(Long, 'J')
CALL_VARIANTS(Float, 'F')
CALL_VARIANTS(Double, 'D')
CALL_VARIANTS(Void, 'V')

static void j_NewStringUTF(NjCtx *c) { hle_ret(c, A(1) ? new_string(hle_cstr(A(1)), 1) : 0); }

static JObj *string_or_fatal(uint32_t h, const char *what) {
    JObj *o = obj_or_fatal(h, what);
    if (o->kind != K_STRING) nj_fatal("JNI %s on a non-string", what);
    return o;
}

static void j_GetStringUTFLength(NjCtx *c) { hle_ret(c, (uint32_t)strlen(string_or_fatal(A(1), "GetStringUTFLength")->str)); }
static void j_GetStringLength(NjCtx *c) { hle_ret(c, (uint32_t)strlen(string_or_fatal(A(1), "GetStringLength")->str)); }

static void j_GetStringUTFChars(NjCtx *c) {
    JObj *o = string_or_fatal(A(1), "GetStringUTFChars");
    if (A(2)) nj_w8(A(2), 1); /* isCopy */
    hle_ret(c, hle_guest_strdup(o->str));
}

static void j_ReleaseStringUTFChars(NjCtx *c) { nj_free(A(2)); }

static void j_GetStringUTFRegion(NjCtx *c) {
    JObj *o = string_or_fatal(A(1), "GetStringUTFRegion");
    uint32_t start = A(2), len = A(3), buf = hle_arg(c, 4);
    size_t sl = strlen(o->str);
    if (start > sl) start = (uint32_t)sl;
    if (start + len > sl) len = (uint32_t)(sl - start);
    memcpy(nj_host(buf), o->str + start, len);
    nj_w8(buf + len, 0);
}

static JObj *array_or_fatal(uint32_t h, const char *what) {
    JObj *o = obj_or_fatal(h, what);
    if (o->kind != K_ARRAY) nj_fatal("JNI %s on a non-array", what);
    return o;
}

static void j_GetArrayLength(NjCtx *c) { hle_ret(c, array_or_fatal(A(1), "GetArrayLength")->n); }

static void j_NewObjectArray(NjCtx *c) {
    JObj *k = obj_or_fatal(A(2), "NewObjectArray");
    uint32_t a = new_object_array(k->cls, A(1), 1);
    JObj *o = obj(a);
    for (uint32_t i = 0; i < o->n; i++) o->elems[i] = A(3);
    if (A(3)) mark_contained(A(3));
    hle_ret(c, a);
}

static void j_GetObjectArrayElement(NjCtx *c) {
    JObj *o = array_or_fatal(A(1), "GetObjectArrayElement");
    if (A(2) >= o->n) { nj_jni_throw("java/lang/ArrayIndexOutOfBoundsException"); hle_ret(c, 0); return; }
    hle_ret(c, o->elems[A(2)]);
}

static void j_SetObjectArrayElement(NjCtx *c) { nj_jni_array_set(A(1), A(2), A(3)); }

static void j_RegisterNatives(NjCtx *c) {
    JObj *k = obj(A(1));
    fprintf(stderr, "[jni] RegisterNatives(%s, %u methods) ignored\n", k ? k->cls : "?", A(3));
    hle_ret(c, 0);
}
static void j_Monitor(NjCtx *c) { hle_ret(c, 0); }

static uint32_t vm_ga, env_ga;

static void j_GetJavaVM(NjCtx *c) { nj_w32(A(1), vm_ga); hle_ret(c, 0); }

static void vm_GetEnv(NjCtx *c) { nj_w32(A(1), env_ga); hle_ret(c, 0); }
static void vm_Attach(NjCtx *c) { if (A(1)) nj_w32(A(1), env_ga); hle_ret(c, 0); }
static void vm_Detach(NjCtx *c) { hle_ret(c, 0); }

uint32_t nj_jni_env(void) { return env_ga; }
uint32_t nj_jni_vm(void) { return vm_ga; }

static NjHleFn impl_for(const char *name) {
#define IMPL(n) if (!strcmp(name, #n)) return j_##n;
#define CALLS(T) IMPL(Call##T##Method) IMPL(Call##T##MethodV) IMPL(Call##T##MethodA) \
    IMPL(CallNonvirtual##T##Method) IMPL(CallNonvirtual##T##MethodV) IMPL(CallNonvirtual##T##MethodA) \
    IMPL(CallStatic##T##Method) IMPL(CallStatic##T##MethodV) IMPL(CallStatic##T##MethodA)
    IMPL(GetVersion) IMPL(FindClass) IMPL(Throw) IMPL(ThrowNew) IMPL(ExceptionOccurred) IMPL(ExceptionDescribe)
    IMPL(ExceptionClear) IMPL(ExceptionCheck) IMPL(FatalError) IMPL(PushLocalFrame) IMPL(PopLocalFrame)
    IMPL(IsSameObject) IMPL(EnsureLocalCapacity) IMPL(AllocObject) IMPL(GetObjectClass) IMPL(IsInstanceOf)
    IMPL(NewStringUTF) IMPL(GetStringUTFLength) IMPL(GetStringLength) IMPL(GetStringUTFChars)
    IMPL(ReleaseStringUTFChars) IMPL(GetStringUTFRegion) IMPL(GetArrayLength) IMPL(NewObjectArray)
    IMPL(GetObjectArrayElement) IMPL(SetObjectArrayElement) IMPL(RegisterNatives) IMPL(GetJavaVM)
    CALLS(Object) CALLS(Boolean) CALLS(Byte) CALLS(Char) CALLS(Short) CALLS(Int) CALLS(Long) CALLS(Float)
    CALLS(Double) CALLS(Void)
#undef CALLS
#undef IMPL
    if (!strcmp(name, "NewGlobalRef") || !strcmp(name, "NewWeakGlobalRef")) return j_NewGlobalRef;
    if (!strcmp(name, "DeleteGlobalRef") || !strcmp(name, "DeleteWeakGlobalRef")) return j_DeleteGlobalRef;
    if (!strcmp(name, "NewLocalRef")) return j_NewLocalRef;
    if (!strcmp(name, "DeleteLocalRef")) return j_DeleteLocalRef;
    if (!strcmp(name, "MonitorEnter") || !strcmp(name, "MonitorExit")) return j_Monitor;
    if (!strcmp(name, "GetMethodID") || !strcmp(name, "GetStaticMethodID")) return get_method;
    if (!strcmp(name, "GetFieldID") || !strcmp(name, "GetStaticFieldID")) return get_field;
    char plain[64]; /* static fields live on the class object: GetStaticIntField -> GetIntField */
    if ((!strncmp(name, "GetStatic", 9) || !strncmp(name, "SetStatic", 9)) && strstr(name, "Field") && !strstr(name, "ID")) {
        snprintf(plain, sizeof plain, "%.3s%s", name, name + 9);
        name = plain;
    }
    if (!strcmp(name, "GetLongField") || !strcmp(name, "GetDoubleField")) return j_GetField64;
    if (!strcmp(name, "SetLongField") || !strcmp(name, "SetDoubleField")) return j_SetField64;
    if (!strncmp(name, "Get", 3) && strstr(name, "Field") && !strstr(name, "ID")) return j_GetField32;
    if (!strncmp(name, "Set", 3) && strstr(name, "Field")) return j_SetField32;
    return NULL;
}

void nj_jni_init(void) {
    if (env_ga) return;
    uint32_t table = hle_data_alloc(4 * NJ_JNI_FUNCS, 4);
    char thunk_name[96];
    for (int i = 0; i < NJ_JNI_FUNCS; i++) {
        if (!slot_names[i]) continue;
        snprintf(thunk_name, sizeof thunk_name, "JNI.%s", slot_names[i]);
        NjHleFn fn = impl_for(slot_names[i]);
        if (fn) hle_register(thunk_name, fn);
        nj_w32(table + 4u * (uint32_t)i, hle_thunk(thunk_name));
    }
    env_ga = hle_data_alloc(4, 4);
    nj_w32(env_ga, table);
    uint32_t vtable = hle_data_alloc(4 * NJ_JNI_INVOKE_FUNCS, 4);
    hle_register("JNIVM.AttachCurrentThread", vm_Attach);
    hle_register("JNIVM.AttachCurrentThreadAsDaemon", vm_Attach);
    hle_register("JNIVM.DetachCurrentThread", vm_Detach);
    hle_register("JNIVM.GetEnv", vm_GetEnv);
    for (int i = 0; i < NJ_JNI_INVOKE_FUNCS; i++) {
        if (!vm_names[i]) continue;
        snprintf(thunk_name, sizeof thunk_name, "JNIVM.%s", vm_names[i]);
        nj_w32(vtable + 4u * (uint32_t)i, hle_thunk(thunk_name));
    }
    vm_ga = hle_data_alloc(4, 4);
    nj_w32(vm_ga, vtable);
    nj_jni_declare_class("java/lang/Class");
    nj_jni_declare_class("java/lang/String");
    nj_jni_declare_class("java/lang/Object");
}

/* ---- host -> guest native calls ---- */

static uint64_t call_addr_v(uint32_t fn, uint32_t thiz, const char *desc, va_list ap) {
    uint32_t w[64];
    int n = 0;
    w[n++] = env_ga;
    w[n++] = thiz;
    char types[48];
    int nt;
    char ret = parse_sig(desc, types, &nt);
    for (int k = 0; k < nt; k++) {
        switch (types[k]) {
        case 'J': {
            uint64_t v = (uint64_t)va_arg(ap, long long);
            n = (n + 1) & ~1;
            w[n++] = (uint32_t)v;
            w[n++] = (uint32_t)(v >> 32);
            break;
        }
        case 'D': {
            double d = va_arg(ap, double);
            uint64_t b;
            memcpy(&b, &d, 8);
            n = (n + 1) & ~1;
            w[n++] = (uint32_t)b;
            w[n++] = (uint32_t)(b >> 32);
            break;
        }
        case 'F': {
            float f = (float)va_arg(ap, double);
            uint32_t b;
            memcpy(&b, &f, 4);
            w[n++] = b;
            break;
        }
        case 'L': w[n++] = va_arg(ap, uint32_t); break;
        default: w[n++] = (uint32_t)va_arg(ap, int); break;
        }
    }
    NjCtx *c = nj_ctx();
    nj_jni_push_frame();
    uint32_t r0 = nj_call(c, fn, n, w);
    uint32_t r1 = c->r[1];
    nj_jni_pop_frame();
    return (ret == 'J' || ret == 'D') ? ((uint64_t)r0 | (uint64_t)r1 << 32) : r0;
}

uint64_t nj_jni_call_addr(uint32_t fn, uint32_t thiz, const char *desc, ...) {
    va_list ap;
    va_start(ap, desc);
    uint64_t r = call_addr_v(fn, thiz, desc, ap);
    va_end(ap);
    return r;
}

uint64_t nj_jni_call_native(const NjImage *img, const char *sym, uint32_t thiz, const char *desc, ...) {
    uint32_t fn = nj_image_sym(img, sym);
    if (!fn) nj_fatal("JNI native %s not found in the guest library", sym);
    va_list ap;
    va_start(ap, desc);
    uint64_t r = call_addr_v(fn, thiz, desc, ap);
    va_end(ap);
    return r;
}
