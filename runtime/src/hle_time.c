/* Time functions with 32-bit guest time_t, plus a deterministic clock for trace comparison. */
#include <pthread.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "nj/heap.h"
#include "nj/hle.h"
#include "nj/hle_pthread.h"

#define A(i) hle_arg(c, i)

static bool fixed;
static uint64_t fixed_now, fixed_step;
static pthread_mutex_t clock_lock = PTHREAD_MUTEX_INITIALIZER;

void nj_time_set_fixed(bool on, uint64_t start_us, uint32_t step_us_per_query) {
    pthread_mutex_lock(&clock_lock);
    fixed = on;
    fixed_now = start_us;
    fixed_step = step_us_per_query;
    pthread_mutex_unlock(&clock_lock);
}

static uint64_t now_us(void) {
    pthread_mutex_lock(&clock_lock);
    if (fixed) {
        uint64_t t = fixed_now;
        fixed_now += fixed_step;
        pthread_mutex_unlock(&clock_lock);
        return t;
    }
    pthread_mutex_unlock(&clock_lock);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000u + (uint64_t)tv.tv_usec;
}

static void hle_gettimeofday(NjCtx *c) {
    uint64_t t = now_us();
    if (A(0)) { nj_w32(A(0), (uint32_t)(t / 1000000u)); nj_w32(A(0) + 4, (uint32_t)(t % 1000000u)); }
    if (A(1)) { nj_w32(A(1), 0); nj_w32(A(1) + 4, 0); }
    hle_ret(c, 0);
}

static void hle_time(NjCtx *c) {
    uint32_t s = (uint32_t)(now_us() / 1000000u);
    if (A(0)) nj_w32(A(0), s);
    hle_ret(c, s);
}

static _Thread_local uint32_t tm_buf, zone_buf;

static void hle_localtime(NjCtx *c) {
    time_t t = (time_t)(int32_t)nj_r32(A(0));
    struct tm tm;
    if (!localtime_r(&t, &tm)) { hle_ret(c, 0); return; }
    if (!tm_buf) { tm_buf = nj_calloc(1, NJ_BIONIC_TM_SIZE); zone_buf = nj_calloc(1, 16); }
    const int32_t f[9] = {tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon,
                          tm.tm_year, tm.tm_wday, tm.tm_yday, tm.tm_isdst};
    for (int i = 0; i < 9; i++) nj_w32(tm_buf + 4u * (uint32_t)i, (uint32_t)f[i]);
    nj_w32(tm_buf + 36, (uint32_t)(int32_t)tm.tm_gmtoff);
    strncpy(nj_host(zone_buf), tm.tm_zone ? tm.tm_zone : "UTC", 15);
    nj_w32(tm_buf + 40, zone_buf);
    hle_ret(c, tm_buf);
}

static void hle_usleep(NjCtx *c) { usleep(A(0)); hle_ret(c, 0); }
static void hle_sleep(NjCtx *c) { sleep(A(0)); hle_ret(c, 0); }

void hle_time_register(void) {
    hle_register("gettimeofday", hle_gettimeofday);
    hle_register("time", hle_time);
    hle_register("localtime", hle_localtime);
    hle_register("usleep", hle_usleep);
    hle_register("sleep", hle_sleep);
}
