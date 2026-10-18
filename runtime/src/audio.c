/* Software mixer: PCM queue streams (by reference to guest buffers) and streaming MP3 decoders,
 * linear resampling to NJ_AUDIO_RATE stereo float. Callbacks run under the mixer lock and must only
 * post work elsewhere (they must not call back into nj_audio_*). */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "minimp3/minimp3_ex.h" /* implementation compiled in third_party/minimp3/minimp3_impl.c */

#include "nj/audio.h"

#define MAX_STREAMS 64
#define MP3_CHUNK 2304 /* frames decoded per refill */

typedef struct Buf {
    const uint8_t *p;
    uint32_t bytes, pos;
    void (*cb)(void *);
    void *ud;
    struct Buf *next;
} Buf;

typedef struct {
    int fd;
    uint64_t base, length, pos;
} Io;

typedef struct {
    int used, is_mp3, playing, loop, ended;
    uint32_t rate;
    int channels, bits;
    float gain, pan, ratio;
    double phase;
    float prev[2], cur[2];
    uint64_t played; /* source frames */
    Buf *head, *tail;
    uint32_t queued, consumed;
    mp3dec_ex_t dec;
    mp3dec_io_t mio;
    Io io;
    int16_t pcm[MP3_CHUNK * 2];
    size_t pcm_frames, pcm_pos;
    void (*at_end)(void *);
    void *end_ud;
} Stream;

static Stream streams[MAX_STREAMS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static Stream *get(int id) { return (id >= 0 && id < MAX_STREAMS && streams[id].used) ? &streams[id] : NULL; }

static int alloc_stream(void) {
    for (int i = 0; i < MAX_STREAMS; i++)
        if (!streams[i].used) {
            memset(&streams[i], 0, sizeof streams[i]);
            streams[i].used = 1;
            streams[i].gain = 1.0f;
            streams[i].ratio = 1.0f;
            return i;
        }
    return -1;
}

int nj_audio_create_pcm(uint32_t rate_hz, int channels, int bits) {
    if (!rate_hz || channels < 1 || channels > 2 || (bits != 8 && bits != 16)) return -1;
    pthread_mutex_lock(&lock);
    int id = alloc_stream();
    if (id >= 0) {
        streams[id].rate = rate_hz;
        streams[id].channels = channels;
        streams[id].bits = bits;
    }
    pthread_mutex_unlock(&lock);
    return id;
}

static size_t io_read(void *buf, size_t size, void *ud) {
    Io *io = ud;
    if (io->pos >= io->length) return 0;
    if (size > io->length - io->pos) size = (size_t)(io->length - io->pos);
    ssize_t n = pread(io->fd, buf, size, (off_t)(io->base + io->pos));
    if (n <= 0) return 0;
    io->pos += (uint64_t)n;
    return (size_t)n;
}

static int io_seek(uint64_t position, void *ud) {
    Io *io = ud;
    io->pos = position > io->length ? io->length : position;
    return 0;
}

static int create_mp3(int fd, uint64_t offset, uint64_t length) {
    pthread_mutex_lock(&lock);
    int id = alloc_stream();
    if (id < 0) { pthread_mutex_unlock(&lock); close(fd); return -1; }
    Stream *s = &streams[id];
    s->is_mp3 = 1;
    s->io = (Io){fd, offset, length, 0};
    s->mio = (mp3dec_io_t){io_read, &s->io, io_seek, &s->io};
    if (mp3dec_ex_open_cb(&s->dec, &s->mio, MP3D_SEEK_TO_SAMPLE) || !s->dec.info.hz || !s->dec.info.channels) {
        s->used = 0;
        pthread_mutex_unlock(&lock);
        close(fd);
        return -1;
    }
    s->rate = (uint32_t)s->dec.info.hz;
    s->channels = s->dec.info.channels;
    s->bits = 16;
    pthread_mutex_unlock(&lock);
    return id;
}

int nj_audio_create_mp3_fd(int fd, uint64_t offset, uint64_t length) {
    int own = dup(fd);
    return own < 0 ? -1 : create_mp3(own, offset, length);
}

int nj_audio_create_mp3_path(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    off_t len = lseek(fd, 0, SEEK_END);
    return create_mp3(fd, 0, (uint64_t)len);
}

void nj_audio_destroy(int id) {
    pthread_mutex_lock(&lock);
    Stream *s = get(id);
    if (s) {
        for (Buf *b = s->head; b;) { Buf *n = b->next; free(b); b = n; }
        if (s->is_mp3) { mp3dec_ex_close(&s->dec); close(s->io.fd); }
        s->used = 0;
    }
    pthread_mutex_unlock(&lock);
}

void nj_audio_queue(int id, const void *data, uint32_t bytes, void (*consumed)(void *), void *ud) {
    Buf *b = calloc(1, sizeof *b);
    b->p = data;
    b->bytes = bytes;
    b->cb = consumed;
    b->ud = ud;
    pthread_mutex_lock(&lock);
    Stream *s = get(id);
    if (!s || s->is_mp3) { pthread_mutex_unlock(&lock); free(b); return; }
    if (s->tail) s->tail->next = b;
    else s->head = b;
    s->tail = b;
    s->queued++;
    pthread_mutex_unlock(&lock);
}

void nj_audio_clear(int id) {
    pthread_mutex_lock(&lock);
    Stream *s = get(id);
    if (s) {
        for (Buf *b = s->head; b;) { Buf *n = b->next; free(b); b = n; }
        s->head = s->tail = NULL;
        s->queued = 0;
    }
    pthread_mutex_unlock(&lock);
}

#define ACCESSOR(name, type, field, dflt)                       \
    type name(int id) {                                         \
        pthread_mutex_lock(&lock);                              \
        Stream *s = get(id);                                    \
        type v = s ? (type)(field) : (dflt);                    \
        pthread_mutex_unlock(&lock);                            \
        return v;                                               \
    }
ACCESSOR(nj_audio_queued, uint32_t, s->queued, 0)
ACCESSOR(nj_audio_consumed_total, uint32_t, s->consumed, 0)
ACCESSOR(nj_audio_position_ms, uint32_t, s->rate ? s->played * 1000 / s->rate : 0, 0)
ACCESSOR(nj_audio_duration_ms, uint32_t,
         s->is_mp3 ? s->dec.samples / (uint64_t)s->channels * 1000 / s->rate : NJ_AUDIO_TIME_UNKNOWN,
         NJ_AUDIO_TIME_UNKNOWN)

#define SETTER(name, type, stmt)                                \
    void name(int id, type v) {                                 \
        pthread_mutex_lock(&lock);                              \
        Stream *s = get(id);                                    \
        if (s) { stmt; }                                        \
        pthread_mutex_unlock(&lock);                            \
    }
SETTER(nj_audio_set_playing, bool, s->playing = v; if (v) s->ended = 0)
SETTER(nj_audio_set_volume, float, s->gain = v)
SETTER(nj_audio_set_pan, float, s->pan = v < -1 ? -1 : v > 1 ? 1 : v)
SETTER(nj_audio_set_rate, float, s->ratio = v > 0.05f ? v : 0.05f)
SETTER(nj_audio_set_loop, bool, s->loop = v)

void nj_audio_seek_ms(int id, uint32_t ms) {
    pthread_mutex_lock(&lock);
    Stream *s = get(id);
    if (s && s->is_mp3) {
        uint64_t frame = (uint64_t)ms * s->rate / 1000;
        mp3dec_ex_seek(&s->dec, frame * (uint64_t)s->channels);
        s->played = frame;
        s->pcm_frames = s->pcm_pos = 0;
        s->ended = 0;
    }
    pthread_mutex_unlock(&lock);
}

void nj_audio_set_end_callback(int id, void (*at_end)(void *), void *ud) {
    pthread_mutex_lock(&lock);
    Stream *s = get(id);
    if (s) { s->at_end = at_end; s->end_ud = ud; }
    pthread_mutex_unlock(&lock);
}

/* Next source frame into f[2]; returns 0 if nothing is available (underrun or end). */
static int next_frame(Stream *s, float f[2]) {
    if (s->is_mp3) {
        if (s->pcm_pos == s->pcm_frames) {
            size_t got = mp3dec_ex_read(&s->dec, s->pcm, MP3_CHUNK * (size_t)s->channels);
            if (got == 0 && s->loop) {
                mp3dec_ex_seek(&s->dec, 0);
                s->played = 0;
                got = mp3dec_ex_read(&s->dec, s->pcm, MP3_CHUNK * (size_t)s->channels);
            }
            if (got == 0) {
                if (!s->ended) {
                    s->ended = 1;
                    s->playing = 0;
                    if (s->at_end) s->at_end(s->end_ud);
                }
                return 0;
            }
            s->pcm_frames = got / (size_t)s->channels;
            s->pcm_pos = 0;
        }
        const int16_t *p = &s->pcm[s->pcm_pos * (size_t)s->channels];
        f[0] = p[0] / 32768.0f;
        f[1] = s->channels == 2 ? p[1] / 32768.0f : f[0];
        s->pcm_pos++;
        s->played++;
        return 1;
    }
    uint32_t fbytes = (uint32_t)(s->channels * (s->bits / 8));
    while (s->head && s->head->pos + fbytes > s->head->bytes) {
        Buf *b = s->head;
        s->head = b->next;
        if (!s->head) s->tail = NULL;
        s->queued--;
        s->consumed++;
        if (b->cb) b->cb(b->ud);
        free(b);
    }
    if (!s->head) return 0;
    const uint8_t *p = s->head->p + s->head->pos;
    if (s->bits == 16) {
        int16_t l, r;
        memcpy(&l, p, 2);
        r = l;
        if (s->channels == 2) memcpy(&r, p + 2, 2);
        f[0] = l / 32768.0f;
        f[1] = r / 32768.0f;
    } else {
        f[0] = (p[0] - 128) / 128.0f;
        f[1] = s->channels == 2 ? (p[1] - 128) / 128.0f : f[0];
    }
    s->head->pos += fbytes;
    s->played++;
    /* report a fully played buffer immediately so callbacks are not delayed by one frame */
    if (s->head->pos + fbytes > s->head->bytes) {
        Buf *b = s->head;
        s->head = b->next;
        if (!s->head) s->tail = NULL;
        s->queued--;
        s->consumed++;
        if (b->cb) b->cb(b->ud);
        free(b);
    }
    return 1;
}

void nj_audio_mix(float *out, uint32_t frames) {
    memset(out, 0, sizeof(float) * 2 * frames);
    pthread_mutex_lock(&lock);
    for (int i = 0; i < MAX_STREAMS; i++) {
        Stream *s = &streams[i];
        if (!s->used || !s->playing) continue;
        double step = (double)s->rate * s->ratio / NJ_AUDIO_RATE;
        float gl = s->gain * (s->pan > 0 ? 1 - s->pan : 1), gr = s->gain * (s->pan < 0 ? 1 + s->pan : 1);
        for (uint32_t k = 0; k < frames && s->playing; k++) {
            s->phase += step;
            while (s->phase >= 1.0) {
                s->prev[0] = s->cur[0];
                s->prev[1] = s->cur[1];
                float f[2] = {0, 0};
                if (!next_frame(s, f)) { f[0] = f[1] = 0; }
                s->cur[0] = f[0];
                s->cur[1] = f[1];
                s->phase -= 1.0;
            }
            float t = (float)s->phase;
            out[2 * k] += gl * (s->prev[0] + (s->cur[0] - s->prev[0]) * t);
            out[2 * k + 1] += gr * (s->prev[1] + (s->cur[1] - s->prev[1]) * t);
        }
    }
    pthread_mutex_unlock(&lock);
}
