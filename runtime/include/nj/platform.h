/* Services the host platform (headless stub, iOS app) provides to the EGL/GLES layer. */
#ifndef NJ_PLATFORM_H
#define NJ_PLATFORM_H

/* GL contexts: id 1 is the main (render-thread) context the host created before nativeInit.
 * create returns a new id sharing objects with share_id; make_current binds ctx_id (0 = none)
 * on the calling thread and returns 1 on success. */
int nj_platform_create_context(int share_id);
int nj_platform_make_current(int ctx_id);
/* Framebuffer that stands in for Android's default framebuffer 0 while the main context is current
 * (iOS renders into an FBO backed by the layer); 0 when there is none. */
unsigned nj_platform_default_framebuffer(void);
/* Drawable size in pixels. */
void nj_platform_surface_size(int *w, int *h);

#endif
