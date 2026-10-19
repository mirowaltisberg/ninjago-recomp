/* Boots the guest library the way Android's Java side did (docs/java-contract.md). */
#ifndef NJ_BOOT_H
#define NJ_BOOT_H

#include <stdbool.h>

#include "nj/loader.h"

typedef struct {
    const char *so_path, *obb_path, *sandbox_dir;
    int width, height;
    bool fixed_clock;
    const char *trace_path;
    const char *device_model; /* reported through nativeSetDeviceStrings */
} NjBootConfig;

/* Load, register HLE, bind imports, run constructors, then the onCreate/onResume/GL-thread sequence
 * up to and including nativeResize. Must be called on the thread that will render. 0 on success. */
int nj_boot(const NjBootConfig *cfg, NjImage *img);
/* One GameGLSurfaceView.Renderer.onDrawFrame: nativeRender. */
void nj_frame(void);
/* Touch input in surface pixels (pointer ids as Android). */
/* Touch events in Android's order (GameGLSurfaceView.onTouchEvent): the first finger down starts a gesture,
 * further fingers go down/up individually, and every movement reports all fingers. Coordinates in pixels. */
enum { NJ_TOUCH_FIRST_DOWN, NJ_TOUCH_MOVE, NJ_TOUCH_LAST_UP, NJ_TOUCH_CANCEL, NJ_TOUCH_POINTER_DOWN, NJ_TOUCH_POINTER_UP };
void nj_touch(int kind, int id, float x, float y);
/* Name of the Fusion native a touch kind is delivered to (NULL for an unknown kind). */
const char *nj_touch_native(int kind);
void nj_pause(void);
void nj_resume(void);

#endif
