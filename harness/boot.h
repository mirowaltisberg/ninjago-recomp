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
void nj_touch(int kind, int id, float x, float y); /* kind: 0 down, 1 move, 2 up, 3 cancel-all, 4 pointer-down, 5 pointer-up */
void nj_pause(void);
void nj_resume(void);

#endif
