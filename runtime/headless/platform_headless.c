/* Headless platform services for macOS boot tests. */
#include <pthread.h>

#include "nj/platform.h"

static int next_ctx = 2;
static int width = 1920, height = 1080;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned default_fbo;

void nj_headless_set_default_framebuffer(unsigned fbo) { default_fbo = fbo; }
unsigned nj_platform_default_framebuffer(void) { return default_fbo; }

void nj_headless_set_size(int w, int h) { width = w; height = h; }

int nj_platform_create_context(int share_id) {
    (void)share_id;
    pthread_mutex_lock(&lock);
    int id = next_ctx++;
    pthread_mutex_unlock(&lock);
    return id;
}

int nj_platform_make_current(int ctx_id) { (void)ctx_id; return 1; }

void nj_platform_surface_size(int *w, int *h) { *w = width; *h = height; }
