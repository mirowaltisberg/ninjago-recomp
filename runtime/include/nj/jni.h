/* JNI facade: guest JavaVM/JNIEnv tables backed by host-side emulated Java objects and methods. */
#ifndef NJ_JNI_H
#define NJ_JNI_H

#include <stdint.h>

#include "nj/loader.h"

#define NJ_JNI_FUNCS 233 /* JNINativeInterface slots incl. 4 reserved (JNI 1.6) */
#define NJ_JNI_INVOKE_FUNCS 8

/* Build the guest JavaVM and JNIEnv (idempotent) and register every JNI slot thunk ("JNI.<Name>"). */
void nj_jni_init(void);
uint32_t nj_jni_env(void);
uint32_t nj_jni_vm(void);
/* Official JNI function name of a slot (NULL for reserved slots). */
const char *nj_jni_slot_name(int slot);

/* Host-side object construction (handles are opaque 32-bit values). */
uint32_t nj_jni_class(const char *class_name);           /* known or new class; never 0 */
uint32_t nj_jni_object(const char *class_name);          /* new instance */
uint32_t nj_jni_new_string(const char *utf8);
const char *nj_jni_string_chars(uint32_t jstring);       /* host view, NULL if not a string */
uint32_t nj_jni_new_object_array(const char *elem_class, uint32_t n);
void nj_jni_array_set(uint32_t array, uint32_t index, uint32_t obj);
/* Fields: sig is a JNI type ("I", "J", "Ljava/lang/String;"...); value holds the bits. */
void nj_jni_set_field(uint32_t obj, const char *name, const char *sig, uint64_t value);
/* Set this thread's pending Java exception to a new instance of exc_class. */
void nj_jni_throw(const char *exc_class);
/* Mark a class as known to FindClass/loadClass. */
void nj_jni_declare_class(const char *class_name);

/* Local reference frames: objects created by the guest (or by emulated Java methods) while a frame is
 * open are freed when it is popped unless globally referenced. nj_jni_call_* push/pop automatically. */
void nj_jni_push_frame(void);
void nj_jni_pop_frame(void);
/* Keep an object alive forever (cached singletons returned to the guest). */
void nj_jni_pin(uint32_t handle);
/* Number of live (non-class) objects, for diagnostics and tests. */
uint32_t nj_jni_live_objects(void);

/* Emulated Java methods. */
typedef union { uint32_t i; float f; double d; uint64_t j; uint32_t l; } NjJValue;
typedef uint64_t (*NjJavaFn)(uint32_t self, const NjJValue *args, int argc);
/* Register a Java method; float results are returned as their bit pattern in the low 32 bits. */
void nj_java_register(const char *class_name, const char *name, const char *sig, NjJavaFn fn);
/* Methods from docs/java-contract.md section 3 (language, movies, Flurry, browser, class loader). */
void nj_java_classes_register(void);

/* Call a guest JNI native by symbol: (env, thiz, args...) with host varargs typed by the JNI
 * descriptor (I Z B C S -> int, J -> long long, F/D -> double, L/[ -> uint32_t handle).
 * Returns r0 (or r0:r1 for J/D results). */
uint64_t nj_jni_call_native(const NjImage *img, const char *java_symbol, uint32_t thiz, const char *descriptor, ...);
uint64_t nj_jni_call_addr(uint32_t fn, uint32_t thiz, const char *descriptor, ...);

#endif
