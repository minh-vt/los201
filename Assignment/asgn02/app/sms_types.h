/*
 * sms_types.h - shared data types for the Sensor Monitoring System.
 * Author: Vu Tung Minh (SE201043) - LOS201
 */
#ifndef SMS_TYPES_H
#define SMS_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <time.h>

#define CB_SIZE          64
#define LOGQ_SIZE        128
#define LOG_LINE_MAX     256
#define SENSOR_WINDOW    10
#define ALARM_MQ_NAME    "/sms_alarm"
#define ALARM_MQ_MAX     10    /* <= /proc/sys/fs/mqueue/msg_max default */
#define ALARM_MQ_MSGSIZE 64

/* One sensor sample as produced by the driver / simulator. */
typedef struct {
    int64_t timestamp_ms;
    double  temperature;
    double  humidity;
} SensorData;

/* Alarm categories. */
enum {
    ALARM_TEMP_HIGH = 1,
    ALARM_HUMID_HIGH,
    ALARM_TEMP_SPIKE,
    ALARM_TYPE_COUNT
};

/* Message pushed onto the POSIX message queue. */
typedef struct {
    int      type;        /* one of the ALARM_* values            */
    double   value;       /* observed value                       */
    double   threshold;   /* configured threshold                 */
    int64_t  timestamp_ms;
} AlarmMsg;

#endif /* SMS_TYPES_H */
