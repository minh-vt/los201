/*
 * sms_context.h - process-wide shared context passed to every thread.
 * Author: Vu Tung Minh (SE201043) - LOS201
 */
#ifndef SMS_CONTEXT_H
#define SMS_CONTEXT_H

#include "sms_types.h"
#include "circular_buffer.h"
#include "log_queue.h"
#include <mqueue.h>
#include <signal.h>

typedef struct {
    /* configuration */
    int    interval_ms;
    double temp_threshold;
    double humid_threshold;
    int    duration_s;
    int    verbose;
    int    sim_mode;
    int    sched_other;   /* 1 = run sensor with SCHED_OTHER (comparison) */
    const char *dev_path;
    const char *log_path;

    /* runtime */
    volatile sig_atomic_t stop;
    CircularBuffer cb;
    LogQueue       lq;
    mqd_t          alarm_mq;
    int            dev_fd;
    struct timespec start_ts;

    pthread_mutex_t stats_mutex;
    unsigned long   total_samples;
    unsigned long   total_alerts[ALARM_TYPE_COUNT + 1];
} SmsCtx;

/* monotonic helper used by several threads */
static inline int64_t ms_since(const struct timespec *start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)(now.tv_sec - start->tv_sec) * 1000
         + (now.tv_nsec - start->tv_nsec) / 1000000;
}

#endif /* SMS_CONTEXT_H */
