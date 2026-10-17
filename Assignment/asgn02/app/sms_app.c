/*
 * sms_app.c - ASSIGNMENT-02: Sensor Monitoring System (main program).
 *
 * Author: Vu Tung Minh (SE201043)
 * Course: LOS201 - Embedded Linux Systems
 * Target: ARMv7-A (QEMU vexpress-a9) and x86_64 (host, --sim mode).
 *
 * Creates four POSIX threads:
 *   sensor    -> reads /dev/sms_sensor, pushes into the circular buffer
 *   processor -> rolling average + threshold detection -> POSIX mqueue
 *   alert     -> consumes the mqueue, prints and counts alarms
 *   logger    -> writes /tmp/sms.log, flush 5 s, rotate at 1 MiB
 *
 * On SIGINT/SIGTERM the program stops the threads, flushes the log and prints
 * a summary (samples, alerts, buffer overflow, jitter statistics).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <mqueue.h>

#define THREAD_STACK_BYTES (256 * 1024)

#include "sms_context.h"
#include "sensor_thread.h"
#include "processor_thread.h"
#include "alert_thread.h"
#include "logger_thread.h"
#include "../driver/sms_sensor.h"

static SmsCtx g_ctx;

static void on_signal(int sig)
{
    (void)sig;
    g_ctx.stop = 1;
}

static void usage(const char *prog)
{
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("  -i <ms>    Sensor sampling interval (default: 500)\n");
    printf("  -t <C>     Temperature alert threshold (default: 40.0)\n");
    printf("  -h <%%>     Humidity alert threshold (default: 80.0)\n");
    printf("  -d <sec>   Run duration in seconds, 0 = infinite (default: 60)\n");
    printf("  -v         Verbose: print every sample\n");
    printf("  -s, --sim  Simulation mode (no driver, used for x86 Valgrind/perf)\n");
    printf("  -o, --other Run the Sensor Thread with SCHED_OTHER (jitter comparison)\n");
    printf("  -j <file>  Write jitter report to <file> (default /tmp/jitter_report.txt)\n");
}

int main(int argc, char **argv)
{
    SmsCtx *ctx = &g_ctx;
    pthread_t tid_sensor, tid_proc, tid_alert, tid_logger;
    struct mq_attr mqattr;
    const char *jitter_path = "/tmp/jitter_report.txt";
    int c, i;
    int rc = 0;

    memset(ctx, 0, sizeof(*ctx));
    ctx->interval_ms = 500;
    ctx->temp_threshold = 40.0;
    ctx->humid_threshold = 80.0;
    ctx->duration_s = 60;
    ctx->verbose = 0;
    ctx->sim_mode = 0;
    ctx->dev_path = "/dev/sms_sensor";
    ctx->log_path = "/tmp/sms.log";
    ctx->dev_fd = -1;

    static struct option longopts[] = {
        {"sim", no_argument, 0, 's'},
        {"other", no_argument, 0, 'o'},
        {"help", no_argument, 0, '?'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "i:t:h:d:vj:so", longopts, NULL)) != -1) {
        switch (c) {
        case 'i': ctx->interval_ms = atoi(optarg); break;
        case 't': ctx->temp_threshold = atof(optarg); break;
        case 'h': ctx->humid_threshold = atof(optarg); break;
        case 'd': ctx->duration_s = atoi(optarg); break;
        case 'v': ctx->verbose = 1; break;
        case 'j': jitter_path = optarg; break;
        case 's': ctx->sim_mode = 1; break;
        case 'o': ctx->sched_other = 1; break;
        default:  usage(argv[0]); return 2;
        }
    }

    if (ctx->interval_ms < 1 || ctx->interval_ms > 60000) {
        fprintf(stderr, "invalid interval: %d ms\n", ctx->interval_ms);
        return 2;
    }

    printf("=== Sensor Monitoring System (SMS) - LAB/ASSIGNMENT-02 ===\n");
    printf("interval=%d ms  temp_thr=%.1f C  hum_thr=%.1f %%  duration=%d s  %s\n",
           ctx->interval_ms, ctx->temp_threshold, ctx->humid_threshold,
           ctx->duration_s, ctx->sim_mode ? "SIM mode" : "driver mode");

    /* 1) driver integration */
    if (!ctx->sim_mode) {
        ctx->dev_fd = open(ctx->dev_path, O_RDONLY);
        if (ctx->dev_fd < 0) {
            fprintf(stderr, "cannot open %s: %s\n", ctx->dev_path,
                    strerror(errno));
            fprintf(stderr, "hint: run with --sim on the host, or load "
                            "sms_sensor_driver.ko on the target\n");
            return 1;
        }
        if (ioctl(ctx->dev_fd, SMS_SET_INTERVAL, ctx->interval_ms) < 0)
            perror("ioctl SMS_SET_INTERVAL");
        else
            printf("[INIT] driver interval set to %d ms\n", ctx->interval_ms);
    } else {
        printf("[INIT] simulation mode: sensor values generated locally\n");
    }

    /* 2) IPC + shared structures */
    cb_init(&ctx->cb);
    lq_init(&ctx->lq);
    pthread_mutex_init(&ctx->stats_mutex, NULL);

    memset(&mqattr, 0, sizeof(mqattr));
    mqattr.mq_maxmsg = ALARM_MQ_MAX;
    mqattr.mq_msgsize = ALARM_MQ_MSGSIZE;
    mq_unlink(ALARM_MQ_NAME);             /* remove stale queue if any */
    ctx->alarm_mq = mq_open(ALARM_MQ_NAME, O_CREAT | O_RDWR | O_NONBLOCK,
                            0666, &mqattr);
    if (ctx->alarm_mq == (mqd_t)-1) {
        perror("mq_open");
        rc = 1;
        goto out_no_threads;
    }

    /* raise the locked-memory soft limit up to its hard limit so that
     * mlockall(MCL_FUTURE) can also cover the thread stacks */
    {
        struct rlimit rl;
        if (getrlimit(RLIMIT_MEMLOCK, &rl) == 0) {
            rl.rlim_cur = rl.rlim_max;
            setrlimit(RLIMIT_MEMLOCK, &rl);
        }
    }

    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
        perror("[INIT] mlockall (continuing)");
    else
        printf("[INIT] mlockall OK (pages locked, no page-fault jitter)\n");

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    clock_gettime(CLOCK_MONOTONIC, &ctx->start_ts);

    /* 3) threads - small stacks so that mlockall(MCL_FUTURE) covers them */
    {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, THREAD_STACK_BYTES);
        pthread_create(&tid_sensor, &attr, sensor_thread, ctx);
        pthread_create(&tid_proc,   &attr, processor_thread, ctx);
        pthread_create(&tid_alert,  &attr, alert_thread, ctx);
        pthread_create(&tid_logger, &attr, logger_thread, ctx);
        pthread_attr_destroy(&attr);
    }

    printf("[INIT] 4 threads running (sensor, processor, alert, logger)\n");

    /* 4) run for the requested duration (or until Ctrl+C) */
    for (i = 0; !ctx->stop; i++) {
        sleep(1);
        if (ctx->duration_s > 0 && i + 1 >= ctx->duration_s)
            break;
    }
    printf("\n[MAIN] shutting down...\n");

    /* 5) graceful shutdown: wake waiters, then join every thread */
    ctx->stop = 1;
    cb_shutdown(&ctx->cb);
    lq_shutdown(&ctx->lq);

    pthread_join(tid_sensor, NULL);
    pthread_join(tid_proc, NULL);
    pthread_join(tid_alert, NULL);
    pthread_join(tid_logger, NULL);

    /* 6) summary */
    {
        int size;
        unsigned long prod, cons, ovf;
        double uptime = ms_since(&ctx->start_ts) / 1000.0;
        cb_stats(&ctx->cb, &size, &prod, &cons, &ovf);

        printf("\n================ SUMMARY ================\n");
        printf("uptime            : %.1f s\n", uptime);
        printf("samples processed : %lu\n", ctx->total_samples);
        printf("buffer produced   : %lu\n", prod);
        printf("buffer consumed   : %lu\n", cons);
        printf("buffer overflow   : %lu\n", ovf);
        printf("alerts TEMP_HIGH  : %lu\n", ctx->total_alerts[ALARM_TEMP_HIGH]);
        printf("alerts HUMID_HIGH : %lu\n", ctx->total_alerts[ALARM_HUMID_HIGH]);
        printf("alerts TEMP_SPIKE : %lu\n", ctx->total_alerts[ALARM_TEMP_SPIKE]);
        printf("log file          : %s\n", ctx->log_path);
        printf("=========================================\n\n");
    }

    /* 7) jitter report -> stdout + file */
    sensor_report_jitter(ctx->sim_mode ? "SIM (SCHED_FIFO)" : "TARGET (SCHED_FIFO)",
                         stdout);
    {
        FILE *jf = fopen(jitter_path, "w");
        if (jf) {
            sensor_report_jitter(ctx->sim_mode ? "SIM (SCHED_FIFO)"
                                               : "TARGET (SCHED_FIFO)", jf);
            fclose(jf);
            printf("[MAIN] jitter report written to %s\n", jitter_path);
        }
    }

    mq_close(ctx->alarm_mq);
    mq_unlink(ALARM_MQ_NAME);
    if (ctx->dev_fd >= 0)
        close(ctx->dev_fd);
    cb_destroy(&ctx->cb);
    lq_destroy(&ctx->lq);
    pthread_mutex_destroy(&ctx->stats_mutex);
    printf("[MAIN] clean exit\n");
    return rc;

out_no_threads:
    if (ctx->alarm_mq != (mqd_t)-1)
        mq_close(ctx->alarm_mq);
    if (ctx->dev_fd >= 0)
        close(ctx->dev_fd);
    cb_destroy(&ctx->cb);
    lq_destroy(&ctx->lq);
    pthread_mutex_destroy(&ctx->stats_mutex);
    return rc;
}
