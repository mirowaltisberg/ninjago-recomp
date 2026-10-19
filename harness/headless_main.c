/* Headless boot of the guest game on macOS: nj_headless_uc --so P --obb P --frames N [--trace F]
 * [--fixed-clock] [--fps N] [--tap FRAME,X,Y] [--sandbox DIR] [--size WxH] [--shaders DIR] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "boot.h"
#include "nj/audio.h"
#include "nj/fs.h"
#include "nj/gles.h"
#include "nj/jni.h"

void nj_headless_set_size(int w, int h);

int main(int argc, char **argv) {
    NjBootConfig cfg = {0};
    cfg.width = 1920;
    cfg.height = 1080;
    cfg.sandbox_dir = "build/sandbox";
    int frames = 600, fps = 0;
    int taps[16][3], ntaps = 0; /* frame, x, y */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--so") && i + 1 < argc) cfg.so_path = argv[++i];
        else if (!strcmp(argv[i], "--obb") && i + 1 < argc) cfg.obb_path = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) cfg.trace_path = argv[++i];
        else if (!strcmp(argv[i], "--sandbox") && i + 1 < argc) cfg.sandbox_dir = argv[++i];
        else if (!strcmp(argv[i], "--fixed-clock")) cfg.fixed_clock = true;
        else if (!strcmp(argv[i], "--fps") && i + 1 < argc) fps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tap") && i + 1 < argc && ntaps < 16) { sscanf(argv[++i], "%d,%d,%d", &taps[ntaps][0], &taps[ntaps][1], &taps[ntaps][2]); ntaps++; }
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &cfg.width, &cfg.height);
        else if (!strcmp(argv[i], "--shaders") && i + 1 < argc) setenv("NJ_SHADER_DUMP_DIR", argv[++i], 1);
        else { fprintf(stderr, "unknown argument %s\n", argv[i]); return 2; }
    }
    if (!cfg.so_path || !cfg.obb_path) {
        fprintf(stderr, "usage: %s --so libLEGO_M1.so --obb main.obb [--frames N] [--trace file] [--fixed-clock]\n", argv[0]);
        return 2;
    }
    nj_headless_set_size(cfg.width, cfg.height);
    NjImage img;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (nj_boot(&cfg, &img)) return 1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    fprintf(stderr, "== boot done in %.2f s\n", (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9);
    struct timespec next = t1;
    for (int f = 0; f < frames; f++) {
        if (fps > 0) { /* pace like a display at fps Hz */
            next.tv_nsec += 1000000000L / fps;
            if (next.tv_nsec >= 1000000000L) { next.tv_sec++; next.tv_nsec -= 1000000000L; }
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long long ns = (long long)(next.tv_sec - now.tv_sec) * 1000000000LL + (next.tv_nsec - now.tv_nsec);
            if (ns > 0) { struct timespec w = {(time_t)(ns / 1000000000LL), (long)(ns % 1000000000LL)}; nanosleep(&w, NULL); }
        }
        for (int t = 0; t < ntaps; t++) {
            if (f == taps[t][0]) nj_touch(0, 0, (float)taps[t][1], (float)taps[t][2]);
            if (f == taps[t][0] + 4) nj_touch(2, 0, (float)taps[t][1], (float)taps[t][2]);
        }
        nj_frame();
        gles_stub_frame();
        if ((f + 1) % 60 == 0) fprintf(stderr, "== frame %d: %u draws\n", f + 1, gles_stub_draws_last_frame());
    }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    fprintf(stderr, "== %d frames in %.2f s\n", frames, (t0.tv_sec - t1.tv_sec) + (t0.tv_nsec - t1.tv_nsec) / 1e9);
    fprintf(stderr, "== audio: %llu frames mixed, last block peak %.4f, max peak %.4f\n",
            (unsigned long long)nj_audio_null_frames(), (double)nj_audio_null_peak(), (double)nj_audio_null_max_peak());
    fprintf(stderr, "== jni: %u live objects\n", nj_jni_live_objects());
    gles_stub_report(stdout);
    nj_fs_report(stdout);
    return 0;
}
