/*
 * sensor_thread.h - Sensor Thread interface.
 * Author: Vu Tung Minh (SE201043) - LOS201
 */
#ifndef SENSOR_THREAD_H
#define SENSOR_THREAD_H
#include <stdio.h>
void *sensor_thread(void *arg);
void sensor_jitter_reset(void);
void sensor_report_jitter(const char *label, FILE *out);
#endif
