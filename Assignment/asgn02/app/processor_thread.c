/*
 * processor_thread.c - consumes samples, computes a rolling average and
 * raises alarms on threshold violations.  Alarms travel to the Alert Thread
 * through a POSIX message queue (ALARM_MQ_NAME).
 *
 * Author: Vu Tung Minh (SE201043) - LOS201
 */
#define _GNU_SOURCE
#include "processor_thread.h"
#include "sms_context.h"

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <errno.h>

/* send one alarm; never blocks the pipeline for long */
static void raise_alarm(SmsCtx *ctx, int type, double value,
                        double threshold, int64_t ts)
{
    AlarmMsg msg;

    memset(&msg, 0, sizeof(msg));   /* clear struct padding sent over mq */
    msg.type = type;
    msg.value = value;
    msg.threshold = threshold;
    msg.timestamp_ms = ts;

    if (mq_send(ctx->alarm_mq, (const char *)&msg, sizeof(msg), 1) == -1) {
        if (errno != EAGAIN)
            perror("mq_send");
    }
}

void *processor_thread(void *arg)
{
    SmsCtx *ctx = (SmsCtx *)arg;
    double win_t[SENSOR_WINDOW], win_h[SENSOR_WINDOW];
    int widx = 0, wcount = 0, have_prev = 0;
    double prev_temp = 0.0;

    pthread_setname_np(pthread_self(), "process");
    printf("[PROCESSOR] started\n");

    for (;;) {
        SensorData d;
        double avg_t, avg_h;
        char line[LOG_LINE_MAX];
        int i;

        if (cb_pop(&ctx->cb, &d) != 0)
            break;                        /* shutdown and drained */

        win_t[widx] = d.temperature;
        win_h[widx] = d.humidity;
        widx = (widx + 1) % SENSOR_WINDOW;
        if (wcount < SENSOR_WINDOW)
            wcount++;

        avg_t = avg_h = 0.0;
        for (i = 0; i < wcount; i++) {
            avg_t += win_t[i];
            avg_h += win_h[i];
        }
        avg_t /= wcount;
        avg_h /= wcount;

        pthread_mutex_lock(&ctx->stats_mutex);
        ctx->total_samples++;
        pthread_mutex_unlock(&ctx->stats_mutex);

        if (d.temperature > ctx->temp_threshold)
            raise_alarm(ctx, ALARM_TEMP_HIGH, d.temperature,
                        ctx->temp_threshold, d.timestamp_ms);
        if (d.humidity > ctx->humid_threshold)
            raise_alarm(ctx, ALARM_HUMID_HIGH, d.humidity,
                        ctx->humid_threshold, d.timestamp_ms);
        if (have_prev && fabs(d.temperature - prev_temp) > 5.0)
            raise_alarm(ctx, ALARM_TEMP_SPIKE, d.temperature - prev_temp,
                        5.0, d.timestamp_ms);
        prev_temp = d.temperature;
        have_prev = 1;

        snprintf(line, sizeof(line),
                 "PROCESS ts=%lld temp=%.3f hum=%.3f avg10_t=%.3f avg10_h=%.3f",
                 (long long)d.timestamp_ms, d.temperature, d.humidity,
                 avg_t, avg_h);
        lq_push(&ctx->lq, line);
    }

    printf("[PROCESSOR] stopped\n");
    return NULL;
}
