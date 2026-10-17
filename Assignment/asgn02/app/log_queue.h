/*
 * log_queue.h - simple thread-safe queue decoupling producers from the
 * Logger Thread (mutex + condition variable).
 * Author: Vu Tung Minh (SE201043) - LOS201
 */
#ifndef LOG_QUEUE_H
#define LOG_QUEUE_H

#include "sms_types.h"

typedef struct { char line[LOG_LINE_MAX]; } LogMsg;

typedef struct {
    LogMsg items[LOGQ_SIZE];
    int head, tail, size;
    int shutdown;
    unsigned long dropped;
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} LogQueue;

void lq_init(LogQueue *q);
void lq_destroy(LogQueue *q);
int  lq_push(LogQueue *q, const char *line);
int  lq_pop(LogQueue *q, LogMsg *out);
void lq_shutdown(LogQueue *q);

#endif /* LOG_QUEUE_H */
