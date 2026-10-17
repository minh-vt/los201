/*
 * alert_thread.c - blocks on the POSIX message queue, prints alarms on stderr,
 * counts them per type and forwards them to the Logger Thread.
 *
 * Author: Vu Tung Minh (SE201043) - LOS201
 */
#define _GNU_SOURCE
#include "alert_thread.h"
#include "sms_context.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>

static const char *type_name(int t)
{
    switch (t) {
    case ALARM_TEMP_HIGH:  return "TEMP_HIGH";
    case ALARM_HUMID_HIGH: return "HUMID_HIGH";
    case ALARM_TEMP_SPIKE: return "TEMP_SPIKE";
    default:               return "UNKNOWN";
    }
}

void *alert_thread(void *arg)
{
    SmsCtx *ctx = (SmsCtx *)arg;
    struct timespec ts;

    pthread_setname_np(pthread_self(), "alert");
    printf("[ALERT] started\n");

    while (!ctx->stop) {
        AlarmMsg msg;
        char raw[ALARM_MQ_MSGSIZE];
        ssize_t n;
        char line[LOG_LINE_MAX];

        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 0;                 /* 200 ms timeout */
        ts.tv_nsec += 200000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_nsec -= 1000000000L; ts.tv_sec++; }

        n = mq_timedreceive(ctx->alarm_mq, raw, sizeof(raw), NULL, &ts);
        if (n < 0) {
            if (errno == EAGAIN) {
                /* O_NONBLOCK queue is shared with the sender, so the receive
                 * end returns EAGAIN immediately when empty.  Sleep instead of
                 * spinning: this removes the alert-thread CPU hotspot. */
                struct timespec nap = {0, 20 * 1000000L};
                nanosleep(&nap, NULL);
                continue;
            }
            if (errno == ETIMEDOUT || errno == EINTR)
                continue;
            perror("mq_timedreceive");
            break;
        }
        if (n < (ssize_t)sizeof(AlarmMsg))
            continue;
        memcpy(&msg, raw, sizeof(msg));

        pthread_mutex_lock(&ctx->stats_mutex);
        if (msg.type >= 1 && msg.type <= ALARM_TYPE_COUNT)
            ctx->total_alerts[msg.type]++;
        pthread_mutex_unlock(&ctx->stats_mutex);

        fprintf(stderr, "[ALERT][%lld] %s: VALUE %.3f THRESHOLD %.3f\n",
                (long long)msg.timestamp_ms, type_name(msg.type),
                msg.value, msg.threshold);

        snprintf(line, sizeof(line),
                 "ALERT ts=%lld type=%s value=%.3f threshold=%.3f",
                 (long long)msg.timestamp_ms, type_name(msg.type),
                 msg.value, msg.threshold);
        lq_push(&ctx->lq, line);
    }

    /* drain remaining alarms so none is lost at shutdown */
    for (;;) {
        AlarmMsg msg;
        char raw[ALARM_MQ_MSGSIZE];
        ssize_t n;
        n = mq_timedreceive(ctx->alarm_mq, raw, sizeof(raw),
                            NULL, &(struct timespec){0, 0});
        if (n < (ssize_t)sizeof(AlarmMsg))
            break;
        memcpy(&msg, raw, sizeof(msg));
        pthread_mutex_lock(&ctx->stats_mutex);
        if (msg.type >= 1 && msg.type <= ALARM_TYPE_COUNT)
            ctx->total_alerts[msg.type]++;
        pthread_mutex_unlock(&ctx->stats_mutex);
        fprintf(stderr, "[ALERT][%lld] %s: VALUE %.3f THRESHOLD %.3f\n",
                (long long)msg.timestamp_ms, type_name(msg.type),
                msg.value, msg.threshold);
    }

    printf("[ALERT] stopped\n");
    return NULL;
}
