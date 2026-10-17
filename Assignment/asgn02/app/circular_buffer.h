/*
 * circular_buffer.h - thread-safe circular buffer (mutex + condition vars).
 * Author: Vu Tung Minh (SE201043) - LOS201
 */
#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include "sms_types.h"

typedef struct {
    SensorData items[CB_SIZE];
    int head, tail, size;
    unsigned long total_produced;
    unsigned long total_consumed;
    unsigned long overflow_count;
    int shutdown;
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} CircularBuffer;

void cb_init(CircularBuffer *cb);
void cb_destroy(CircularBuffer *cb);
int  cb_push(CircularBuffer *cb, const SensorData *d, int wait_ms);
int  cb_pop(CircularBuffer *cb, SensorData *out);
void cb_shutdown(CircularBuffer *cb);
void cb_stats(CircularBuffer *cb, int *size, unsigned long *produced,
              unsigned long *consumed, unsigned long *overflows);

#endif /* CIRCULAR_BUFFER_H */
