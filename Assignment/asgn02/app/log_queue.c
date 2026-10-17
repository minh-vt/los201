/*
 * log_queue.c - bounded blocking queue of log lines.
 * Author: Vu Tung Minh (SE201043) - LOS201
 */
#define _GNU_SOURCE
#include "log_queue.h"
#include <errno.h>
#include <string.h>

void lq_init(LogQueue *q)
{
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

void lq_destroy(LogQueue *q)
{
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}

int lq_push(LogQueue *q, const char *line)
{
    pthread_mutex_lock(&q->mutex);
    if (q->size == LOGQ_SIZE) {      /* never block producers: drop oldest */
        q->dropped++;
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }
    strncpy(q->items[q->head].line, line, LOG_LINE_MAX - 1);
    q->items[q->head].line[LOG_LINE_MAX - 1] = '\0';
    q->head = (q->head + 1) % LOGQ_SIZE;
    q->size++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

int lq_pop(LogQueue *q, LogMsg *out)
{
    pthread_mutex_lock(&q->mutex);
    while (q->size == 0 && !q->shutdown)
        pthread_cond_wait(&q->not_empty, &q->mutex);

    if (q->size == 0) {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    *out = q->items[q->tail];
    q->tail = (q->tail + 1) % LOGQ_SIZE;
    q->size--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

void lq_shutdown(LogQueue *q)
{
    pthread_mutex_lock(&q->mutex);
    q->shutdown = 1;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
}
