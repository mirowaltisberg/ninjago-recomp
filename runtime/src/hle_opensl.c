/* OpenSL ES 1.0.1 (Android flavour) subset over the host mixer. Objects and interfaces live in guest
 * memory as {vtable*, object id}; vtable entries are thunks named "SL.<Interface>.<Method>".
 * Guest callbacks are delivered by one HLE-owned guest thread (the "pump"), never on the mixer thread. */
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nj/audio.h"
#include "nj/backend.h"
#include "nj/fs.h"
#include "nj/heap.h"
#include "nj/hle.h"

#define A(i) hle_arg(c, i)

enum {
    SL_RESULT_SUCCESS = 0, SL_RESULT_PARAMETER_INVALID = 2, SL_RESULT_MEMORY_FAILURE = 3,
    SL_RESULT_CONTENT_UNSUPPORTED = 7, SL_RESULT_FEATURE_UNSUPPORTED = 0x0C
};
enum { SL_OBJECT_STATE_UNREALIZED = 1, SL_OBJECT_STATE_REALIZED = 2 };
enum { SL_PLAYSTATE_STOPPED = 1, SL_PLAYSTATE_PAUSED = 2, SL_PLAYSTATE_PLAYING = 3 };
enum { SL_PLAYEVENT_HEADATEND = 1 };
#define SL_DATALOCATOR_URI 1u
#define SL_DATALOCATOR_BUFFERQUEUE 6u
#define SL_DATALOCATOR_ANDROIDFD 0x800007BCu
#define SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE 0x800007BDu
#define SL_DATAFORMAT_PCM 2u

/* ---- interface IDs (OpenSLES_IID.c) ---- */

typedef struct { const char *name; uint32_t time_low; uint16_t mid, hi, seq; uint8_t node[6]; } Iid;
enum { I_ENGINE, I_PLAY, I_VOLUME, I_SEEK, I_RATE, I_BQ, I_COUNT };
static const Iid iids[I_COUNT] = {
    {"SL_IID_ENGINE", 0x8d97c260, 0xddd4, 0x11db, 0x958f, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}},
    {"SL_IID_PLAY", 0xef0bd9c0, 0xddd7, 0x11db, 0xbf49, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}},
    {"SL_IID_VOLUME", 0x09e8ede0, 0xddde, 0x11db, 0xb4f6, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}},
    {"SL_IID_SEEK", 0xd43135a0, 0xdddc, 0x11db, 0xb458, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}},
    {"SL_IID_PLAYBACKRATE", 0x2e3b2a40, 0xddda, 0x11db, 0xa349, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}},
    {"SL_IID_ANDROIDSIMPLEBUFFERQUEUE", 0x198e4940, 0xc5d7, 0x11df, 0xa2a6, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}},
};
static uint32_t iid_ga[I_COUNT];

static int iid_index(uint32_t ga) {
    if (!ga) return -1;
    for (int i = 0; i < I_COUNT; i++)
        if (ga == iid_ga[i] || memcmp(nj_host(ga), nj_host(iid_ga[i]), 16) == 0) return i;
    return -1;
}

/* ---- objects ---- */

enum { T_ENGINE = 1, T_MIX, T_PLAYER };

typedef struct {
    int used, type, state;
    uint32_t self;               /* SLObjectItf */
    uint32_t itf[I_COUNT];       /* interface addresses (0 = not offered) */
    int stream, is_bq;
    uint32_t bq_cb, bq_ctx, play_cb, play_ctx, play_mask, play_state;
    int16_t volume_mb, rate_permille, stereo_permille;
    int mute, stereo_enabled, loop;
} Obj;

#define MAX_OBJS 256
static Obj objs[MAX_OBJS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t vt_object, vt_itf[I_COUNT];

static Obj *obj_of(uint32_t itf_ga) {
    uint32_t id = nj_r32(itf_ga + 4);
    if (id >= MAX_OBJS || !objs[id].used) nj_fatal("OpenSL call on a destroyed or invalid object 0x%08x", itf_ga);
    return &objs[id];
}

static uint32_t make_itf(uint32_t vtable, uint32_t id) {
    uint32_t ga = hle_data_alloc(8, 4);
    nj_w32(ga, vtable);
    nj_w32(ga + 4, id);
    return ga;
}

static Obj *new_obj(int type) {
    pthread_mutex_lock(&lock);
    uint32_t id = 0;
    while (id < MAX_OBJS && objs[id].used) id++;
    if (id == MAX_OBJS) { pthread_mutex_unlock(&lock); nj_fatal("too many OpenSL objects"); }
    Obj *o = &objs[id];
    memset(o, 0, sizeof *o);
    o->used = 1;
    o->type = type;
    o->state = SL_OBJECT_STATE_UNREALIZED;
    o->stream = -1;
    o->play_state = SL_PLAYSTATE_STOPPED;
    o->rate_permille = 1000;
    pthread_mutex_unlock(&lock);
    o->self = make_itf(vt_object, id);
    return o;
}

/* ---- callback pump ---- */

typedef struct Ev { uint32_t fn, argc, args[3]; struct Ev *next; } Ev;
static Ev *ev_head, *ev_tail;
static pthread_mutex_t ev_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ev_cv = PTHREAD_COND_INITIALIZER;
static pthread_once_t pump_once = PTHREAD_ONCE_INIT;

static void post(uint32_t fn, uint32_t argc, uint32_t a0, uint32_t a1, uint32_t a2) {
    Ev *e = malloc(sizeof *e);
    *e = (Ev){fn, argc, {a0, a1, a2}, NULL};
    pthread_mutex_lock(&ev_lock);
    if (ev_tail) ev_tail->next = e;
    else ev_head = e;
    ev_tail = e;
    pthread_cond_signal(&ev_cv);
    pthread_mutex_unlock(&ev_lock);
}

static void hle_pump(NjCtx *c) {
    for (;;) {
        pthread_mutex_lock(&ev_lock);
        while (!ev_head) pthread_cond_wait(&ev_cv, &ev_lock);
        Ev *e = ev_head;
        ev_head = e->next;
        if (!ev_head) ev_tail = NULL;
        pthread_mutex_unlock(&ev_lock);
        nj_call(c, e->fn, (int)e->argc, e->args);
        free(e);
    }
}

static void start_pump(void) {
    if (nj_spawn_thread(hle_thunk("__nj_sl_pump"), 0, NULL, NULL)) nj_fatal("cannot start the OpenSL callback thread");
}

static void bq_consumed(void *ud) {
    Obj *o = ud;
    if (o->used && o->bq_cb) post(o->bq_cb, 2, o->itf[I_BQ], o->bq_ctx, 0);
}

static void at_end(void *ud) {
    Obj *o = ud;
    if (o->used && o->play_cb && (o->play_mask & SL_PLAYEVENT_HEADATEND))
        post(o->play_cb, 3, o->itf[I_PLAY], o->play_ctx, SL_PLAYEVENT_HEADATEND);
}

/* ---- SLObjectItf ---- */

static void o_Realize(NjCtx *c) {
    Obj *o = obj_of(A(0));
    o->state = SL_OBJECT_STATE_REALIZED;
    if (o->type == T_MIX) nj_audio_output_start();
    hle_ret(c, SL_RESULT_SUCCESS);
}
static void o_Resume(NjCtx *c) { hle_ret(c, SL_RESULT_SUCCESS); }
static void o_GetState(NjCtx *c) { nj_w32(A(1), (uint32_t)obj_of(A(0))->state); hle_ret(c, SL_RESULT_SUCCESS); }

static void o_GetInterface(NjCtx *c) {
    Obj *o = obj_of(A(0));
    int i = iid_index(A(1));
    if (i < 0 || !o->itf[i]) { hle_ret(c, SL_RESULT_FEATURE_UNSUPPORTED); return; }
    nj_w32(A(2), o->itf[i]);
    hle_ret(c, SL_RESULT_SUCCESS);
}

static void o_RegisterCallback(NjCtx *c) { hle_ret(c, SL_RESULT_SUCCESS); }
static void o_AbortAsyncOperation(NjCtx *c) { (void)c; }

static void o_Destroy(NjCtx *c) {
    Obj *o = obj_of(A(0));
    if (o->stream >= 0) nj_audio_destroy(o->stream);
    pthread_mutex_lock(&lock);
    o->used = 0;
    pthread_mutex_unlock(&lock);
}
static void o_SetPriority(NjCtx *c) { hle_ret(c, SL_RESULT_SUCCESS); }
static void o_GetPriority(NjCtx *c) {
    if (A(1)) nj_w32(A(1), 0);
    if (A(2)) nj_w32(A(2), 1);
    hle_ret(c, SL_RESULT_SUCCESS);
}
static void o_SetLossOfControlInterfaces(NjCtx *c) { hle_ret(c, SL_RESULT_SUCCESS); }

/* ---- engine ---- */

static void e_CreateOutputMix(NjCtx *c) {
    Obj *m = new_obj(T_MIX);
    nj_w32(A(1), m->self);
    hle_ret(c, SL_RESULT_SUCCESS);
}

static int create_stream(Obj *p, uint32_t src) {
    uint32_t loc = nj_r32(src), fmt = nj_r32(src + 4);
    uint32_t type = nj_r32(loc);
    if (type == SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE || type == SL_DATALOCATOR_BUFFERQUEUE) {
        if (nj_r32(fmt) != SL_DATAFORMAT_PCM) nj_fatal("OpenSL buffer queue with data format %u", nj_r32(fmt));
        p->is_bq = 1;
        return nj_audio_create_pcm(nj_r32(fmt + 8) / 1000, (int)nj_r32(fmt + 4), (int)nj_r32(fmt + 12));
    }
    if (type == SL_DATALOCATOR_ANDROIDFD) {
        int fd = (int)nj_r32(loc + 4);
        int64_t off = (int64_t)nj_r64(loc + 8), len = (int64_t)nj_r64(loc + 16);
        if (len < 0) len = 0x7FFFFFFFFFFFFFFFLL; /* SL_DATALOCATOR_ANDROIDFD_USE_FILE_SIZE */
        return nj_audio_create_mp3_fd(fd, (uint64_t)off, (uint64_t)len);
    }
    if (type == SL_DATALOCATOR_URI) {
        const char *uri = hle_cstr(nj_r32(loc + 4));
        const char *path = uri && !strncmp(uri, "file://", 7) ? uri + 7 : uri;
        char buf[1024];
        const char *host = path ? nj_fs_host_path(path, buf, sizeof buf) : NULL;
        if (!host) { fprintf(stderr, "[opensl] URI not mapped: %s\n", uri ? uri : "(null)"); return -1; }
        return nj_audio_create_mp3_path(host);
    }
    nj_fatal("unsupported OpenSL data locator 0x%08x", type);
}

static void e_CreateAudioPlayer(NjCtx *c) {
    uint32_t out = A(1), src = A(2), n = hle_arg(c, 4), ids = hle_arg(c, 5), req = hle_arg(c, 6);
    for (uint32_t k = 0; k < n; k++) {
        int i = iid_index(nj_r32(ids + 4 * k));
        if (i < 0 && req && nj_r32(req + 4 * k)) {
            fprintf(stderr, "[opensl] player requires an unsupported interface\n");
            hle_ret(c, SL_RESULT_FEATURE_UNSUPPORTED);
            return;
        }
    }
    Obj *p = new_obj(T_PLAYER);
    p->stream = create_stream(p, src);
    if (p->stream < 0) {
        pthread_mutex_lock(&lock);
        p->used = 0;
        pthread_mutex_unlock(&lock);
        hle_ret(c, SL_RESULT_CONTENT_UNSUPPORTED);
        return;
    }
    uint32_t id = (uint32_t)(p - objs);
    for (int i = I_PLAY; i < I_COUNT; i++)
        if (i != I_BQ || p->is_bq) p->itf[i] = make_itf(vt_itf[i], id);
    nj_audio_set_end_callback(p->stream, at_end, p);
    nj_w32(out, p->self);
    hle_ret(c, SL_RESULT_SUCCESS);
}

/* ---- play ---- */

static void p_SetPlayState(NjCtx *c) {
    Obj *o = obj_of(A(0));
    uint32_t st = A(1);
    o->play_state = st;
    nj_audio_set_playing(o->stream, st == SL_PLAYSTATE_PLAYING);
    if (st == SL_PLAYSTATE_STOPPED && !o->is_bq) nj_audio_seek_ms(o->stream, 0);
    hle_ret(c, SL_RESULT_SUCCESS);
}
static void p_GetPlayState(NjCtx *c) { nj_w32(A(1), obj_of(A(0))->play_state); hle_ret(c, SL_RESULT_SUCCESS); }
static void p_GetDuration(NjCtx *c) { nj_w32(A(1), nj_audio_duration_ms(obj_of(A(0))->stream)); hle_ret(c, SL_RESULT_SUCCESS); }
static void p_GetPosition(NjCtx *c) { nj_w32(A(1), nj_audio_position_ms(obj_of(A(0))->stream)); hle_ret(c, SL_RESULT_SUCCESS); }
static void p_RegisterCallback(NjCtx *c) {
    Obj *o = obj_of(A(0));
    o->play_cb = A(1);
    o->play_ctx = A(2);
    hle_ret(c, SL_RESULT_SUCCESS);
}
static void p_SetCallbackEventsMask(NjCtx *c) { obj_of(A(0))->play_mask = A(1); hle_ret(c, SL_RESULT_SUCCESS); }
static void p_GetCallbackEventsMask(NjCtx *c) { nj_w32(A(1), obj_of(A(0))->play_mask); hle_ret(c, SL_RESULT_SUCCESS); }
static void p_Ok(NjCtx *c) { hle_ret(c, SL_RESULT_SUCCESS); }
static void p_GetZero(NjCtx *c) { nj_w32(A(1), 0); hle_ret(c, SL_RESULT_SUCCESS); }

/* ---- buffer queue ---- */

static void b_Enqueue(NjCtx *c) {
    Obj *o = obj_of(A(0));
    nj_audio_queue(o->stream, nj_host(A(1)), A(2), bq_consumed, o);
    hle_ret(c, SL_RESULT_SUCCESS);
}
static void b_Clear(NjCtx *c) { nj_audio_clear(obj_of(A(0))->stream); hle_ret(c, SL_RESULT_SUCCESS); }
static void b_GetState(NjCtx *c) {
    Obj *o = obj_of(A(0));
    nj_w32(A(1), nj_audio_queued(o->stream));
    nj_w32(A(1) + 4, nj_audio_consumed_total(o->stream));
    hle_ret(c, SL_RESULT_SUCCESS);
}
static void b_RegisterCallback(NjCtx *c) {
    Obj *o = obj_of(A(0));
    o->bq_cb = A(1);
    o->bq_ctx = A(2);
    hle_ret(c, SL_RESULT_SUCCESS);
}

/* ---- volume (millibels, int16) ---- */

static void apply_volume(Obj *o) {
    float g = o->mute ? 0.0f : powf(10.0f, o->volume_mb / 2000.0f);
    nj_audio_set_volume(o->stream, g);
    nj_audio_set_pan(o->stream, o->stereo_enabled ? o->stereo_permille / 1000.0f : 0.0f);
}
static void v_SetVolumeLevel(NjCtx *c) { Obj *o = obj_of(A(0)); o->volume_mb = (int16_t)A(1); apply_volume(o); hle_ret(c, 0); }
static void v_GetVolumeLevel(NjCtx *c) { nj_w16(A(1), (uint16_t)obj_of(A(0))->volume_mb); hle_ret(c, 0); }
static void v_GetMaxVolumeLevel(NjCtx *c) { nj_w16(A(1), 0); hle_ret(c, 0); }
static void v_SetMute(NjCtx *c) { Obj *o = obj_of(A(0)); o->mute = A(1) != 0; apply_volume(o); hle_ret(c, 0); }
static void v_GetMute(NjCtx *c) { nj_w32(A(1), (uint32_t)obj_of(A(0))->mute); hle_ret(c, 0); }
static void v_EnableStereoPosition(NjCtx *c) { Obj *o = obj_of(A(0)); o->stereo_enabled = A(1) != 0; apply_volume(o); hle_ret(c, 0); }
static void v_IsEnabledStereoPosition(NjCtx *c) { nj_w32(A(1), (uint32_t)obj_of(A(0))->stereo_enabled); hle_ret(c, 0); }
static void v_SetStereoPosition(NjCtx *c) { Obj *o = obj_of(A(0)); o->stereo_permille = (int16_t)A(1); apply_volume(o); hle_ret(c, 0); }
static void v_GetStereoPosition(NjCtx *c) { nj_w16(A(1), (uint16_t)obj_of(A(0))->stereo_permille); hle_ret(c, 0); }

/* ---- seek ---- */

static void s_SetPosition(NjCtx *c) { nj_audio_seek_ms(obj_of(A(0))->stream, A(1)); hle_ret(c, 0); }
static void s_SetLoop(NjCtx *c) { Obj *o = obj_of(A(0)); o->loop = A(1) != 0; nj_audio_set_loop(o->stream, o->loop); hle_ret(c, 0); }
static void s_GetLoop(NjCtx *c) {
    Obj *o = obj_of(A(0));
    if (A(1)) nj_w32(A(1), (uint32_t)o->loop);
    if (A(2)) nj_w32(A(2), 0);
    if (A(3)) nj_w32(A(3), NJ_AUDIO_TIME_UNKNOWN);
    hle_ret(c, 0);
}

/* ---- playback rate (permille, int16) ---- */

static void r_SetRate(NjCtx *c) {
    Obj *o = obj_of(A(0));
    o->rate_permille = (int16_t)A(1);
    nj_audio_set_rate(o->stream, o->rate_permille / 1000.0f);
    hle_ret(c, 0);
}
static void r_GetRate(NjCtx *c) { nj_w16(A(1), (uint16_t)obj_of(A(0))->rate_permille); hle_ret(c, 0); }
static void r_GetCapabilitiesOfRate(NjCtx *c) { nj_w32(A(2), 0); hle_ret(c, 0); }
static void r_GetRateRange(NjCtx *c) {
    if (A(1) != 0) { hle_ret(c, SL_RESULT_PARAMETER_INVALID); return; } /* one range; callers enumerate */
    nj_w16(A(2), 500);
    nj_w16(A(3), 2000);
    if (hle_arg(c, 4)) nj_w16(hle_arg(c, 4), 0);
    if (hle_arg(c, 5)) nj_w32(hle_arg(c, 5), 0);
    hle_ret(c, 0);
}

/* ---- slCreateEngine ---- */

static void hle_slCreateEngine(NjCtx *c) {
    pthread_once(&pump_once, start_pump);
    Obj *e = new_obj(T_ENGINE);
    e->itf[I_ENGINE] = make_itf(vt_itf[I_ENGINE], (uint32_t)(e - objs));
    nj_w32(A(0), e->self);
    hle_ret(c, SL_RESULT_SUCCESS);
}

typedef struct { const char *name; NjHleFn fn; } Method;

/* Build a vtable whose slot i is the thunk "SL.<itf>.<names[i]>"; fns[i] may be NULL (fatal if called). */
static uint32_t build_vtable(const char *itf, const Method *m, int n) {
    uint32_t vt = hle_data_alloc(4u * (uint32_t)n, 4);
    char name[96];
    for (int i = 0; i < n; i++) {
        snprintf(name, sizeof name, "SL.%s.%s", itf, m[i].name);
        if (m[i].fn) hle_register(name, m[i].fn);
        nj_w32(vt + 4u * (uint32_t)i, hle_thunk(name));
    }
    return vt;
}

void hle_opensl_register(void) {
    if (vt_object) return;
    for (int i = 0; i < I_COUNT; i++) {
        uint32_t s = hle_data_alloc(16, 4);
        nj_w32(s, iids[i].time_low);
        nj_w16(s + 4, iids[i].mid);
        nj_w16(s + 6, iids[i].hi);
        nj_w16(s + 8, iids[i].seq);
        memcpy(nj_host(s + 10), iids[i].node, 6);
        iid_ga[i] = s;
        uint32_t var = hle_data_alloc(4, 4); /* SL_IID_x is a pointer variable */
        nj_w32(var, s);
        hle_register_data(iids[i].name, var);
    }
    static const Method object[] = {
        {"Realize", o_Realize}, {"Resume", o_Resume}, {"GetState", o_GetState}, {"GetInterface", o_GetInterface},
        {"RegisterCallback", o_RegisterCallback}, {"AbortAsyncOperation", o_AbortAsyncOperation},
        {"Destroy", o_Destroy}, {"SetPriority", o_SetPriority}, {"GetPriority", o_GetPriority},
        {"SetLossOfControlInterfaces", o_SetLossOfControlInterfaces}};
    static const Method engine[] = {
        {"CreateLEDDevice", NULL}, {"CreateVibraDevice", NULL}, {"CreateAudioPlayer", e_CreateAudioPlayer},
        {"CreateAudioRecorder", NULL}, {"CreateMidiPlayer", NULL}, {"CreateListener", NULL},
        {"Create3DGroup", NULL}, {"CreateOutputMix", e_CreateOutputMix}, {"CreateMetadataExtractor", NULL},
        {"CreateExtensionObject", NULL}, {"QueryNumSupportedInterfaces", NULL}, {"QuerySupportedInterfaces", NULL},
        {"QueryNumSupportedExtensions", NULL}, {"QuerySupportedExtension", NULL}, {"IsExtensionSupported", NULL}};
    static const Method play[] = {
        {"SetPlayState", p_SetPlayState}, {"GetPlayState", p_GetPlayState}, {"GetDuration", p_GetDuration},
        {"GetPosition", p_GetPosition}, {"RegisterCallback", p_RegisterCallback},
        {"SetCallbackEventsMask", p_SetCallbackEventsMask}, {"GetCallbackEventsMask", p_GetCallbackEventsMask},
        {"SetMarkerPosition", p_Ok}, {"ClearMarkerPosition", p_Ok}, {"GetMarkerPosition", p_GetZero},
        {"SetPositionUpdatePeriod", p_Ok}, {"GetPositionUpdatePeriod", p_GetZero}};
    static const Method volume[] = {
        {"SetVolumeLevel", v_SetVolumeLevel}, {"GetVolumeLevel", v_GetVolumeLevel},
        {"GetMaxVolumeLevel", v_GetMaxVolumeLevel}, {"SetMute", v_SetMute}, {"GetMute", v_GetMute},
        {"EnableStereoPosition", v_EnableStereoPosition}, {"IsEnabledStereoPosition", v_IsEnabledStereoPosition},
        {"SetStereoPosition", v_SetStereoPosition}, {"GetStereoPosition", v_GetStereoPosition}};
    static const Method seek[] = {{"SetPosition", s_SetPosition}, {"SetLoop", s_SetLoop}, {"GetLoop", s_GetLoop}};
    static const Method rate[] = {
        {"SetRate", r_SetRate}, {"GetRate", r_GetRate}, {"SetPropertyConstraints", p_Ok},
        {"GetProperties", p_GetZero}, {"GetCapabilitiesOfRate", r_GetCapabilitiesOfRate},
        {"GetRateRange", r_GetRateRange}};
    static const Method bq[] = {
        {"Enqueue", b_Enqueue}, {"Clear", b_Clear}, {"GetState", b_GetState}, {"RegisterCallback", b_RegisterCallback}};
    vt_object = build_vtable("Object", object, 10);
    vt_itf[I_ENGINE] = build_vtable("Engine", engine, 15);
    vt_itf[I_PLAY] = build_vtable("Play", play, 12);
    vt_itf[I_VOLUME] = build_vtable("Volume", volume, 9);
    vt_itf[I_SEEK] = build_vtable("Seek", seek, 3);
    vt_itf[I_RATE] = build_vtable("PlaybackRate", rate, 6);
    vt_itf[I_BQ] = build_vtable("AndroidSimpleBufferQueue", bq, 4);
    hle_register("slCreateEngine", hle_slCreateEngine);
    hle_register("__nj_sl_pump", hle_pump);
}
