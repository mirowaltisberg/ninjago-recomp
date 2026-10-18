#include <fcntl.h>
#include <math.h>
#include <unistd.h>

#include "nj_test.h"

#include "nj/audio.h"
#include "nj/backend.h"
#include "nj/guest.h"
#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/obb.h"

#define PAGE 0x00F00000u
#define FN_COUNT (PAGE + 0x00) /* callback(caller, ctx): (*ctx)++ */

enum { OBJ_Realize = 0, OBJ_GetState = 2, OBJ_GetInterface = 3, OBJ_Destroy = 6 };
enum { ENG_CreateAudioPlayer = 2, ENG_CreateOutputMix = 7 };
enum { PLAY_SetPlayState = 0, PLAY_GetPlayState = 1, PLAY_GetDuration = 2, PLAY_GetPosition = 3 };
enum { BQ_Enqueue = 0, BQ_Clear = 1, BQ_GetState = 2, BQ_RegisterCallback = 3 };
enum { VOL_SetVolumeLevel = 0, VOL_GetVolumeLevel = 1 };

static void setup(void) {
    static int done;
    if (done) return;
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_mem_map_fixed_regions(), 0);
    NJ_EQ_U(nj_heap_init(), 0);
    NJ_EQ_U(nj_mem_map(PAGE, 0x4000, NJ_PROT_R | NJ_PROT_W | NJ_PROT_X), 0);
    const uint32_t code[] = {0xE5912000, 0xE2822001, 0xE5812000, 0xE12FFF1E};
    for (int i = 0; i < 4; i++) nj_w32(FN_COUNT + 4u * (uint32_t)i, code[i]);
    hle_opensl_register();
    NJ_EQ_U(nj_backend_init(), 0);
    done = 1;
}

/* Call method `index` of the interface `self` with up to 7 argument words after self. */
static uint32_t sl(uint32_t self, int index, int n, const uint32_t *args) {
    NjCtx *c = nj_ctx();
    uint32_t sp = c->r[13], nsp = (sp - 256) & ~7u;
    uint32_t w[8] = {self};
    for (int i = 0; i < n; i++) w[i + 1] = args[i];
    for (int i = 0; i < n + 1; i++) {
        if (i < 4) c->r[i] = w[i];
        else nj_w32(nsp + 4u * (uint32_t)(i - 4), w[i]);
    }
    c->r[13] = nsp;
    hle_dispatch(c, nj_r32(nj_r32(self) + 4u * (uint32_t)index));
    c->r[13] = sp;
    return c->r[0];
}

static uint32_t iid(const char *name) { return nj_r32(hle_data_addr(name)); }

static uint32_t engine_itf(void) {
    static uint32_t eng;
    if (eng) return eng;
    uint32_t out = nj_malloc(4);
    NjCtx *c = nj_ctx();
    uint32_t sp = c->r[13], nsp = (sp - 64) & ~7u;
    nj_w32(nsp, 0);
    nj_w32(nsp + 4, 0);
    c->r[0] = out; c->r[1] = 0; c->r[2] = 0; c->r[3] = 0;
    c->r[13] = nsp;
    hle_dispatch(c, hle_thunk("slCreateEngine"));
    c->r[13] = sp;
    NJ_EQ_U(c->r[0], 0);
    uint32_t obj = nj_r32(out);
    uint32_t a1[] = {0};
    NJ_EQ_U(sl(obj, OBJ_Realize, 1, a1), 0);
    uint32_t a2[] = {iid("SL_IID_ENGINE"), out};
    NJ_EQ_U(sl(obj, OBJ_GetInterface, 2, a2), 0);
    eng = nj_r32(out);
    return eng;
}

static uint32_t output_mix(void) {
    static uint32_t mix;
    if (mix) return mix;
    uint32_t out = nj_malloc(4);
    uint32_t a[] = {out, 0, 0, 0};
    NJ_EQ_U(sl(engine_itf(), ENG_CreateOutputMix, 4, a), 0);
    mix = nj_r32(out);
    uint32_t r[] = {0};
    NJ_EQ_U(sl(mix, OBJ_Realize, 1, r), 0);
    return mix;
}

/* Creates a realized player; locator/format are guest structs; returns the player object. */
static uint32_t make_player(uint32_t locator, uint32_t format, uint32_t ids, uint32_t req, uint32_t nids) {
    uint32_t src = nj_malloc(8), snk = nj_malloc(8), out_loc = nj_malloc(8), out = nj_malloc(4);
    nj_w32(src, locator);
    nj_w32(src + 4, format);
    nj_w32(out_loc, 4 /* SL_DATALOCATOR_OUTPUTMIX */);
    nj_w32(out_loc + 4, output_mix());
    nj_w32(snk, out_loc);
    nj_w32(snk + 4, 0);
    uint32_t a[] = {out, src, snk, nids, ids, req};
    NJ_EQ_U(sl(engine_itf(), ENG_CreateAudioPlayer, 6, a), 0);
    uint32_t p = nj_r32(out);
    uint32_t r[] = {0};
    NJ_EQ_U(sl(p, OBJ_Realize, 1, r), 0);
    return p;
}

static uint32_t get_itf(uint32_t obj, const char *iid_name) {
    uint32_t out = nj_malloc(4);
    uint32_t a[] = {iid(iid_name), out};
    NJ_EQ_U(sl(obj, OBJ_GetInterface, 2, a), 0);
    return nj_r32(out);
}

static uint32_t pcm_player(void) {
    uint32_t loc = nj_calloc(1, 8), fmt = nj_calloc(1, 28), ids = nj_malloc(12), req = nj_malloc(12);
    nj_w32(loc, 0x800007BDu); /* SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE */
    nj_w32(loc + 4, 4);
    nj_w32(fmt + 0, 2);       /* SL_DATAFORMAT_PCM */
    nj_w32(fmt + 4, 2);       /* channels */
    nj_w32(fmt + 8, 44100000);/* milliHz */
    nj_w32(fmt + 12, 16);
    nj_w32(fmt + 16, 16);
    nj_w32(fmt + 20, 3);
    nj_w32(fmt + 24, 2);      /* little endian */
    nj_w32(ids, iid("SL_IID_ANDROIDSIMPLEBUFFERQUEUE"));
    nj_w32(ids + 4, iid("SL_IID_PLAY"));
    nj_w32(ids + 8, iid("SL_IID_VOLUME"));
    for (int i = 0; i < 3; i++) nj_w32(req + 4u * (uint32_t)i, 1);
    return make_player(loc, fmt, ids, req, 3);
}

NJ_TEST(sl_iid_values) {
    setup();
    const uint8_t want[16] = {0x60, 0xc2, 0x97, 0x8d, 0xd4, 0xdd, 0xdb, 0x11, 0x8f, 0x95, 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b};
    NJ_CHECK(memcmp(nj_host(iid("SL_IID_ENGINE")), want, 16) == 0);
    NJ_CHECK(iid("SL_IID_PLAY") && iid("SL_IID_VOLUME") && iid("SL_IID_SEEK") && iid("SL_IID_PLAYBACKRATE"));
}

NJ_TEST(sl_engine_flow) {
    setup();
    uint32_t p = pcm_player();
    NJ_CHECK(get_itf(p, "SL_IID_ANDROIDSIMPLEBUFFERQUEUE") != 0);
    NJ_CHECK(get_itf(p, "SL_IID_PLAY") != 0);
    uint32_t st = nj_malloc(4);
    uint32_t a[] = {st};
    NJ_EQ_U(sl(p, OBJ_GetState, 1, a), 0);
    NJ_EQ_U(nj_r32(st), 2); /* SL_OBJECT_STATE_REALIZED */
    sl(p, OBJ_Destroy, 0, NULL);
}

NJ_TEST(sl_bufferqueue_callback) {
    setup();
    uint32_t p = pcm_player();
    uint32_t bq = get_itf(p, "SL_IID_ANDROIDSIMPLEBUFFERQUEUE"), play = get_itf(p, "SL_IID_PLAY");
    uint32_t counter = nj_calloc(1, 4);
    uint32_t a1[] = {FN_COUNT, counter};
    NJ_EQ_U(sl(bq, BQ_RegisterCallback, 2, a1), 0);
    uint32_t bytes = 441 * 4; /* 10 ms of 16-bit stereo */
    for (int i = 0; i < 3; i++) {
        uint32_t buf = nj_calloc(1, bytes);
        uint32_t a2[] = {buf, bytes};
        NJ_EQ_U(sl(bq, BQ_Enqueue, 2, a2), 0);
    }
    uint32_t a3[] = {3 /* SL_PLAYSTATE_PLAYING */};
    NJ_EQ_U(sl(play, PLAY_SetPlayState, 1, a3), 0);
    for (int i = 0; i < 100 && nj_r32(counter) < 3; i++) usleep(10000);
    NJ_EQ_U(nj_r32(counter), 3);
    uint32_t state = nj_calloc(1, 8);
    uint32_t a4[] = {state};
    sl(bq, BQ_GetState, 1, a4);
    NJ_EQ_U(nj_r32(state), 0);
    sl(p, OBJ_Destroy, 0, NULL);
}

NJ_TEST(sl_volume_roundtrip) {
    setup();
    uint32_t p = pcm_player();
    uint32_t vol = get_itf(p, "SL_IID_VOLUME");
    uint32_t a[] = {(uint32_t)(int32_t)-600};
    NJ_EQ_U(sl(vol, VOL_SetVolumeLevel, 1, a), 0);
    uint32_t out = nj_calloc(1, 4);
    uint32_t b[] = {out};
    sl(vol, VOL_GetVolumeLevel, 1, b);
    NJ_EQ_U((uint16_t)nj_r16(out), (uint16_t)-600);
    sl(p, OBJ_Destroy, 0, NULL);
}

NJ_TEST(audio_mix_pcm_volume) {
    int16_t pcm[2 * 256];
    for (int i = 0; i < 512; i++) pcm[i] = 16384;
    int id = nj_audio_create_pcm(44100, 2, 16);
    NJ_CHECK(id >= 0);
    nj_audio_queue(id, pcm, sizeof pcm, NULL, NULL);
    nj_audio_set_playing(id, true);
    float out[2 * 64];
    nj_audio_mix(out, 64);
    NJ_CHECK(fabsf(out[10] - 0.5f) < 0.01f);
    nj_audio_set_volume(id, 0.5f);
    nj_audio_mix(out, 64);
    NJ_CHECK(fabsf(out[10] - 0.25f) < 0.01f);
    nj_audio_destroy(id);
}

static int open_obb_mp3(uint64_t *off, uint64_t *len) {
    if (access(NJ_GUEST_OBB_PATH, R_OK) != 0) return -1;
    NjObbEntry *e = NULL;
    uint32_t n = 0;
    if (nj_obb_index(NJ_GUEST_OBB_PATH, &e, &n)) return -1;
    int fd = -1;
    for (uint32_t i = 0; i < n; i++)
        if (!strcmp(e[i].name, "music/nj_gauntlet_of_humility_loop.mp3")) {
            *off = e[i].data_offset;
            *len = e[i].size;
            fd = open(NJ_GUEST_OBB_PATH, O_RDONLY);
        }
    free(e);
    return fd;
}

NJ_TEST(audio_mp3_from_obb) {
    uint64_t off, len;
    int fd = open_obb_mp3(&off, &len);
    if (fd < 0) NJ_SKIP("OBB not present");
    int id = nj_audio_create_mp3_fd(fd, off, len);
    close(fd);
    NJ_CHECK(id >= 0);
    NJ_CHECK(nj_audio_duration_ms(id) > 1000 && nj_audio_duration_ms(id) != NJ_AUDIO_TIME_UNKNOWN);
    nj_audio_set_playing(id, true);
    static float out[2 * 44100];
    float peak = 0;
    for (int k = 0; k < 4; k++) {
        nj_audio_mix(out, 11025);
        for (int i = 0; i < 2 * 11025; i++) if (fabsf(out[i]) > peak) peak = fabsf(out[i]);
    }
    NJ_CHECK(peak > 0.01f);
    NJ_CHECK(nj_audio_position_ms(id) >= 900);
    nj_audio_destroy(id);
}

NJ_TEST(sl_mp3_player_fd) {
    setup();
    uint64_t off, len;
    int fd = open_obb_mp3(&off, &len);
    if (fd < 0) NJ_SKIP("OBB not present");
    uint32_t loc = nj_calloc(1, 24), fmt = nj_calloc(1, 12), ids = nj_malloc(8), req = nj_malloc(8);
    nj_w32(loc, 0x800007BCu); /* SL_DATALOCATOR_ANDROIDFD */
    nj_w32(loc + 4, (uint32_t)fd);
    nj_w64(loc + 8, off);
    nj_w64(loc + 16, len);
    nj_w32(fmt, 3); /* SL_DATAFORMAT_MIME */
    nj_w32(ids, iid("SL_IID_PLAY"));
    nj_w32(ids + 4, iid("SL_IID_SEEK"));
    nj_w32(req, 1);
    nj_w32(req + 4, 1);
    uint32_t p = make_player(loc, fmt, ids, req, 2);
    uint32_t play = get_itf(p, "SL_IID_PLAY");
    uint32_t d = nj_malloc(4);
    uint32_t a1[] = {d};
    sl(play, PLAY_GetDuration, 1, a1);
    NJ_CHECK(nj_r32(d) > 1000 && nj_r32(d) != NJ_AUDIO_TIME_UNKNOWN);
    uint32_t a2[] = {3};
    sl(play, PLAY_SetPlayState, 1, a2);
    usleep(300000);
    uint32_t pos = nj_malloc(4);
    uint32_t a3[] = {pos};
    sl(play, PLAY_GetPosition, 1, a3);
    NJ_CHECK(nj_r32(pos) >= 100);
    NJ_CHECK(nj_audio_null_frames() > 0);
    sl(p, OBJ_Destroy, 0, NULL);
    close(fd);
}
