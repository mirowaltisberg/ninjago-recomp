/* Host audio mixer used by the OpenSL ES layer, plus the platform output hook. */
#ifndef NJ_AUDIO_H
#define NJ_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#define NJ_AUDIO_RATE 44100
#define NJ_AUDIO_TIME_UNKNOWN 0xFFFFFFFFu

/* Streams: PCM queues (sound effects) or MP3 decoders (music). Ids are >= 0, -1 on failure. */
int nj_audio_create_pcm(uint32_t rate_hz, int channels, int bits);
int nj_audio_create_mp3_fd(int fd, uint64_t offset, uint64_t length); /* the fd is dup()ed */
int nj_audio_create_mp3_path(const char *host_path);
void nj_audio_destroy(int id);

/* Queue a PCM buffer by reference; consumed(ud) runs (on the mixing thread) once it has been played. */
void nj_audio_queue(int id, const void *data, uint32_t bytes, void (*consumed)(void *ud), void *ud);
void nj_audio_clear(int id);   /* drops queued buffers without callbacks */
uint32_t nj_audio_queued(int id);
uint32_t nj_audio_consumed_total(int id); /* buffers consumed since creation */

void nj_audio_set_playing(int id, bool on);
void nj_audio_set_volume(int id, float gain);
void nj_audio_set_pan(int id, float pan); /* -1 left .. +1 right */
void nj_audio_set_rate(int id, float ratio);
void nj_audio_set_loop(int id, bool loop);
void nj_audio_seek_ms(int id, uint32_t ms);
uint32_t nj_audio_position_ms(int id);
uint32_t nj_audio_duration_ms(int id);
/* at_end(ud) runs on the mixing thread when a non-looping decoder reaches its end. */
void nj_audio_set_end_callback(int id, void (*at_end)(void *ud), void *ud);

/* Pull interleaved stereo float frames at NJ_AUDIO_RATE (called by the platform output). */
void nj_audio_mix(float *out, uint32_t frames);

/* Platform: start pulling from the mixer (idempotent). Headless: a real-time null sink. */
int nj_audio_output_start(void);
uint64_t nj_audio_null_frames(void);
float nj_audio_null_peak(void);
float nj_audio_null_max_peak(void);

/* OpenSL ES: slCreateEngine + the six SL_IID_* data imports and all interface methods. */
void hle_opensl_register(void);

#endif
