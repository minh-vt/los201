/*
 * logger_thread.c - drains the log queue and writes /tmp/sms.log.
 *
 * The file is flushed every 5 s (not per line) to avoid an I/O storm, and it
 * is rotated to <log>.1 when it grows beyond 1 MiB.
 *
 * Author: Vu Tung Minh (SE201043) - LOS201
 */
#define _GNU_SOURCE
#include "logger_thread.h"
#include "sms_context.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define LOG_ROTATE_BYTES (1024 * 1024)
#define LOG_FLUSH_SECONDS 5

void *logger_thread(void *arg)
{
    SmsCtx *ctx = (SmsCtx *)arg;
    FILE *fp;
    long size;
    time_t last_flush;

    pthread_setname_np(pthread_self(), "logger");

    fp = fopen(ctx->log_path, "a");
    if (!fp) {
        perror("fopen log");
        return NULL;
    }
    setvbuf(fp, NULL, _IOFBF, 8192);
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    if (size < 0)
        size = 0;
    last_flush = time(NULL);
    printf("[LOGGER] writing to %s\n", ctx->log_path);

    for (;;) {
        LogMsg m;
        struct timespec now;
        struct tm tmv;
        char ts[32];
        int n;
        time_t nowt;

        if (lq_pop(&ctx->lq, &m) != 0)
            break;                        /* shutdown and drained */

        clock_gettime(CLOCK_REALTIME, &now);
        localtime_r(&now.tv_sec, &tmv);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

        n = fprintf(fp, "[%s.%03ld] %s\n", ts, now.tv_nsec / 1000000, m.line);
        if (n > 0)
            size += n;

        nowt = time(NULL);
        if (nowt - last_flush >= LOG_FLUSH_SECONDS) {
            fflush(fp);
            last_flush = nowt;
        }

        if (size > LOG_ROTATE_BYTES) {
            char rotated[300];
            fclose(fp);
            snprintf(rotated, sizeof(rotated), "%s.1", ctx->log_path);
            rename(ctx->log_path, rotated);
            fp = fopen(ctx->log_path, "a");
            if (!fp) { perror("fopen log (rotate)"); return NULL; }
            setvbuf(fp, NULL, _IOFBF, 8192);
            size = 0;
            printf("[LOGGER] rotated log to %s\n", rotated);
        }
    }

    fflush(fp);
    fclose(fp);
    printf("[LOGGER] stopped\n");
    return NULL;
}
