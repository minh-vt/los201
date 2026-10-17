/*
 * circular_buffer.c - single-producer/single-consumer circular buffer.
 *
 * Synchronisation: one mutex protects head/tail/size; not_empty wakes the
 * consumer, not_full wakes a producer that is waiting for space.  The
 * producer prefers to drop the sample instead of blocking forever, so the
 * sensor thread never misses its deadline because of a slow consumer.
 *
 * Author: Vu Tung Minh (SE201043) - LOS201
 */
#define _GNU_SOURCE
#include "circular_buffer.h"
#include <errno.h>
#include <string.h>

void cb_init(CircularBuffer *cb)
{
    memset(cb, 0, sizeof(*cb));
    pthread_mutex_init(&cb->mutex, NULL);
    pthread_cond_init(&cb->not_empty, NULL);
    pthread_cond_init(&cb->not_full, NULL);
}

void cb_destroy(CircularBuffer *cb)
{
    pthread_mutex_destroy(&cb->mutex);
    pthread_cond_destroy(&cb->not_empty);
    pthread_cond_destroy(&cb->not_full);
}

/* push one element; wait up to wait_ms for space, then drop (count overflow) */
int cb_push(CircularBuffer *cb, const SensorData *d, int wait_ms)
{
    int rc = 0;

    pthread_mutex_lock(&cb->mutex);

    while (cb->size == CB_SIZE && !cb->shutdown) {
        if (wait_ms <= 0) {
            cb->overflow_count++;
            pthread_mutex_unlock(&cb->mutex);
            return -1;
        }
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += (long)(wait_ms % 1000) * 1000000L;
        ts.tv_sec  += wait_ms / 1000 + ts.tv_nsec / 1000000000L;
        ts.tv_nsec %= 1000000000L;
        if (pthread_cond_timedwait(&cb->not_full, &cb->mutex, &ts) == ETIMEDOUT) {
            if (cb->size == CB_SIZE) {
                cb->overflow_count++;
                pthread_mutex_unlock(&cb->mutex);
                return -1;
            }
        }
    }
    if (cb->shutdown) {
        pthread_mutex_unlock(&cb->mutex);
        return -1;
    }

    cb->items[cb->head] = *d;
    cb->head = (cb->head + 1) % CB_SIZE;
    cb->size++;
    cb->total_produced++;
    pthread_cond_signal(&cb->not_empty);
    pthread_mutex_unlock(&cb->mutex);
    return rc;
}

/* blocking pop; returns -1 when shutdown and empty */
int cb_pop(CircularBuffer *cb, SensorData *out)
{
    pthread_mutex_lock(&cb->mutex);
    while (cb->size == 0 && !cb->shutdown)
        pthread_cond_wait(&cb->not_empty, &cb->mutex);

    if (cb->size == 0) {          /* shutdown and drained */
        pthread_mutex_unlock(&cb->mutex);
        return -1;
    }

    *out = cb->items[cb->tail];
    cb->tail = (cb->tail + 1) % CB_SIZE;
    cb->size--;
    cb->total_consumed++;
    pthread_cond_signal(&cb->not_full);
    pthread_mutex_unlock(&cb->mutex);
    return 0;
}

/* wake every waiter so that producer/consumer can observe the stop flag */
void cb_shutdown(CircularBuffer *cb)
{
    pthread_mutex_lock(&cb->mutex);
    cb->shutdown = 1;
    pthread_cond_broadcast(&cb->not_empty);
    pthread_cond_broadcast(&cb->not_full);
    pthread_mutex_unlock(&cb->mutex);
}

void cb_stats(CircularBuffer *cb, int *size, unsigned long *produced,
              unsigned long *consumed, unsigned long *overflows)
{
    pthread_mutex_lock(&cb->mutex);
    if (size)      *size      = cb->size;
    if (produced)  *produced  = cb->total_produced;
    if (consumed)  *consumed  = cb->total_consumed;
    if (overflows) *overflows = cb->overflow_count;
    pthread_mutex_unlock(&cb->mutex);
}
