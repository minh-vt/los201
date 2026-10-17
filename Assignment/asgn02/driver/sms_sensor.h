/*
 * sms_sensor.h - ioctl definitions shared by sms_sensor_driver.c and sms_app.
 *
 * Author: Vu Tung Minh (SE201043)
 * Course: LOS201 - Embedded Linux Systems
 */
#ifndef SMS_SENSOR_H
#define SMS_SENSOR_H

#include <linux/types.h>
#include <linux/ioctl.h>

/* Device exposed by the driver */
#define SMS_DEV_NAME  "sms_sensor"
#define SMS_CLASS_NAME "sms_class"
#define SMS_DRIVER_MAJOR 242

#define SMS_IOC_MAGIC 'S'

/*
 * SMS_SET_INTERVAL - set the minimum interval (ms) between two read()
 * calls.  The argument is a plain int (direction _IOW).
 */
#define SMS_SET_INTERVAL _IOW(SMS_IOC_MAGIC, 0, int)

#endif /* SMS_SENSOR_H */
