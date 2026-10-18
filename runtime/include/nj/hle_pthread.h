/* bionic pthreads, semaphores, scheduling and time. */
#ifndef NJ_HLE_PTHREAD_H
#define NJ_HLE_PTHREAD_H

#include <stdbool.h>
#include <stdint.h>

/* 26 pthread imports + sem_* + sched_get_priority_* */
void hle_pthread_register(void);
/* gettimeofday time localtime usleep sleep */
void hle_time_register(void);

/* Deterministic clock for trace comparison: every time query returns start_us + n * step_us. */
void nj_time_set_fixed(bool on, uint64_t start_us, uint32_t step_us_per_query);

/* bionic layout sizes */
#define NJ_BIONIC_TM_SIZE 44
#define NJ_BIONIC_PTHREAD_ATTR_SIZE 24

#endif
