/* Headless audio output: pulls the mixer in real time (10 ms blocks) and discards the samples. */
#include <math.h>
#include <pthread.h>
#include <time.h>

#include "nj/audio.h"

#define BLOCK 441

static pthread_once_t once = PTHREAD_ONCE_INIT;
static volatile uint64_t frames;
static volatile float peak;

static void *pump(void *arg) {
    (void)arg;
    static float buf[2 * BLOCK];
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    for (;;) {
        nj_audio_mix(buf, BLOCK);
        float p = 0;
        for (int i = 0; i < 2 * BLOCK; i++) if (fabsf(buf[i]) > p) p = fabsf(buf[i]);
        peak = p;
        frames += BLOCK;
        next.tv_nsec += 10000000;
        if (next.tv_nsec >= 1000000000) { next.tv_sec++; next.tv_nsec -= 1000000000; }
        struct timespec now, wait;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long long ns = (long long)(next.tv_sec - now.tv_sec) * 1000000000LL + (next.tv_nsec - now.tv_nsec);
        if (ns > 0) {
            wait.tv_sec = (time_t)(ns / 1000000000LL);
            wait.tv_nsec = (long)(ns % 1000000000LL);
            nanosleep(&wait, NULL);
        }
    }
    return NULL;
}

static void start(void) {
    pthread_t t;
    pthread_create(&t, NULL, pump, NULL);
    pthread_detach(t);
}

int nj_audio_output_start(void) { pthread_once(&once, start); return 0; }
uint64_t nj_audio_null_frames(void) { return frames; }
float nj_audio_null_peak(void) { return peak; }
