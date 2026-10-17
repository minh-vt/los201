/*
 * sensor_thread.c - reads one sample per period from /dev/sms_sensor.
 *
 * RT design:
 *   - SCHED_FIFO priority 50 so the sampling loop wins over normal threads.
 *   - clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME) so the wake-up time is
 *     absolute and the period does not drift when a cycle is late.
 *   - the difference between expected and actual wake-up time is recorded as
 *     jitter (us) and summarised as Min/Avg/Max/P99 + histogram.
 *
 * Author: Vu Tung Minh (SE201043) - LOS201
 */
#define _GNU_SOURCE
#include "sensor_thread.h"
#include "sms_context.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sched.h>
#include <time.h>

#define JITTER_MAX 200000
#define RT_PRIORITY 50

static long   g_jitter[JITTER_MAX];
static size_t g_jitter_n;

void sensor_jitter_reset(void)
{
    g_jitter_n = 0;
}

/* microseconds: a - b */
static long diff_us(const struct timespec *a, const struct timespec *b)
{
    return (long)(a->tv_sec - b->tv_sec) * 1000000L
         + (a->tv_nsec - b->tv_nsec) / 1000L;
}

/*
 * read_one - obtain one SensorData.
 * In simulation mode the value is generated locally (used for the x86
 * Valgrind/strace/perf runs where the ARM kernel module is not present).
 */
static int read_one(SmsCtx *ctx, SensorData *d)
{
    static unsigned int seed;
    int tries;

    if (ctx->sim_mode) {
        struct timespec now;
        unsigned int r;
        if (seed == 0)
            seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
        r = rand_r(&seed);
        d->temperature = 15.0 + (double)(r % 30001) / 1000.0;
        r = rand_r(&seed);
        d->humidity    = 30.0 + (double)(r % 60001) / 1000.0;
        clock_gettime(CLOCK_REALTIME, &now);
        d->timestamp_ms = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
        return 0;
    }

    for (tries = 0; tries < 50; tries++) {
        char buf[128];
        ssize_t n = read(ctx->dev_fd, buf, sizeof(buf) - 1);
        if (n < 0) {
            if (errno == EAGAIN) {            /* driver throttling */
                struct timespec ts = {0, 2 * 1000000L};
                nanosleep(&ts, NULL);
                continue;
            }
            if (errno == EINTR)               /* interrupted by signal */
                continue;
            perror("read /dev/sms_sensor");
            return -1;
        }
        if (n == 0) {                          /* EOF: device gone */
            fprintf(stderr, "[SENSOR] EOF on device\n");
            return -1;
        }
        buf[n] = '\0';
        if (sscanf(buf, "%lld,%lf,%lf",
                   (long long *)&d->timestamp_ms,
                   &d->temperature, &d->humidity) == 3)
            return 0;
    }
    fprintf(stderr, "[SENSOR] too many EAGAIN retries\n");
    return -1;
}

void *sensor_thread(void *arg)
{
    SmsCtx *ctx = (SmsCtx *)arg;
    struct sched_param sp = { .sched_priority = RT_PRIORITY };
    struct timespec next;
    long period_ns = (long)ctx->interval_ms * 1000000L;
    int rt_ok;

    pthread_setname_np(pthread_self(), "sensor");
    sensor_jitter_reset();

    if (ctx->sched_other) {
        rt_ok = -1;
        printf("[SENSOR] SCHED_OTHER (default CFS) - comparison run\n");
    } else {
        rt_ok = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
        printf("[SENSOR] SCHED_FIFO prio %d : %s\n", RT_PRIORITY,
               rt_ok == 0 ? "OK" : "FAILED (running SCHED_OTHER)");
    }

    clock_gettime(CLOCK_MONOTONIC, &next);

    while (!ctx->stop) {
        struct timespec actual;
        long jitter;
        SensorData d;
        char line[LOG_LINE_MAX];

        /* advance the absolute deadline before sleeping */
        next.tv_nsec += period_ns;
        while (next.tv_nsec >= 1000000000L) {
            next.tv_nsec -= 1000000000L;
            next.tv_sec++;
        }

        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        clock_gettime(CLOCK_MONOTONIC, &actual);
        jitter = diff_us(&actual, &next);

        if (g_jitter_n < JITTER_MAX)
            g_jitter[g_jitter_n++] = jitter;

        /* If we fell more than one full period behind, resynchronise the
         * absolute deadline so the backlog cannot grow without bound
         * (on real hardware this essentially never triggers). */
        if (jitter > (long)ctx->interval_ms * 1000L)
            next = actual;

        if (read_one(ctx, &d) == 0) {
            if (cb_push(&ctx->cb, &d, 50) == 0) {
                if (ctx->verbose)
                    printf("[SENSOR] t=%lld temp=%.3f hum=%.3f jitter=%ldus\n",
                           (long long)d.timestamp_ms, d.temperature,
                           d.humidity, jitter);
                snprintf(line, sizeof(line),
                         "SENSOR ts=%lld temp=%.3f hum=%.3f jitter_us=%ld",
                         (long long)d.timestamp_ms, d.temperature,
                         d.humidity, jitter);
                lq_push(&ctx->lq, line);
            } else {
                lq_push(&ctx->lq, "SENSOR buffer full - sample dropped");
            }
        }
    }
    printf("[SENSOR] stopped after %zu samples\n", g_jitter_n);
    return NULL;
}

static int cmp_long(const void *a, const void *b)
{
    long x = *(const long *)a, y = *(const long *)b;
    return (x > y) - (x < y);
}

void sensor_report_jitter(const char *label, FILE *out)
{
    size_t i;
    long min, max, sum;
    long *sorted;
    size_t b0 = 0, b1 = 0, b2 = 0, b3 = 0;

    if (g_jitter_n == 0) {
        fprintf(out, "No jitter samples collected.\n");
        return;
    }

    min = max = sum = 0;
    for (i = 0; i < g_jitter_n; i++) {
        if (i == 0 || g_jitter[i] < min) min = g_jitter[i];
        if (i == 0 || g_jitter[i] > max) max = g_jitter[i];
        sum += g_jitter[i];
        if (g_jitter[i] < 100) b0++;
        else if (g_jitter[i] < 500) b1++;
        else if (g_jitter[i] < 1000) b2++;
        else b3++;
    }

    sorted = malloc(g_jitter_n * sizeof(long));
    memcpy(sorted, g_jitter, g_jitter_n * sizeof(long));
    qsort(sorted, g_jitter_n, sizeof(long), cmp_long);

    fprintf(out, "===== Jitter report: %s =====\n", label);
    fprintf(out, "samples : %zu\n", g_jitter_n);
    fprintf(out, "Min     : %ld us\n", min);
    fprintf(out, "Avg     : %.1f us\n", (double)sum / (double)g_jitter_n);
    fprintf(out, "Max     : %ld us\n", max);
    fprintf(out, "P99     : %ld us\n", sorted[(size_t)(0.99 * g_jitter_n)]);
    fprintf(out, "Histogram:\n");
    fprintf(out, "  [    0,  100) us : %zu (%.1f%%)\n", b0, 100.0 * b0 / g_jitter_n);
    fprintf(out, "  [  100,  500) us : %zu (%.1f%%)\n", b1, 100.0 * b1 / g_jitter_n);
    fprintf(out, "  [  500, 1000) us : %zu (%.1f%%)\n", b2, 100.0 * b2 / g_jitter_n);
    fprintf(out, "  [ 1000,  inf) us : %zu (%.1f%%)\n", b3, 100.0 * b3 / g_jitter_n);
    free(sorted);
}
