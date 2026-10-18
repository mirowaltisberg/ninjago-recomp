/* EGL and OpenGL ES 2 thunks for the guest. */
#ifndef NJ_GLES_H
#define NJ_GLES_H

#include <stdio.h>

/* All GL imports + OES vertex array functions (reachable through eglGetProcAddress). */
void hle_gles_register(void);
/* eglBindAPI eglChooseConfig eglCreateContext eglCreatePbufferSurface eglGetCurrent* eglGetError
 * eglGetProcAddress eglMakeCurrent eglSwapInterval. */
void hle_egl_register(void);
/* Declare that the calling thread has the main context and window surface current (the host does
 * this on its render thread before nativeInit, like Android's GL thread). */
void nj_egl_set_current_main(void);
/* Guest EGLConfig handle (also stored in the EGLConfig Java object's mEGLConfig field). */
unsigned nj_egl_config_handle(void);

/* Headless stub (runtime/headless/gles_stub.c) diagnostics. */
void gles_stub_frame(void);           /* call once per presented frame */
void gles_stub_report(FILE *out);
unsigned gles_stub_frames(void);
unsigned gles_stub_draws_last_frame(void);
const char *gles_stub_last_shader_source(void);
const void *gles_stub_last_attrib_pointer(void);
const void *gles_stub_last_draw_indices(void);
unsigned gles_stub_compressed_format_count(unsigned internalformat);

#endif
