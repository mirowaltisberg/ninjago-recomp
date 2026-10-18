/* iOS implementation of nj/platform.h and the audio output hook. */
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES2/gl.h>

/* Register the render-thread context (platform id 1), its layer-backed framebuffer and drawable size. */
void nj_ios_set_main_context(EAGLContext *ctx, GLuint framebuffer, int width, int height);
/* Reactivate the audio session and restart the output unit (after an interruption or when resuming). */
void nj_ios_audio_resume(void);
