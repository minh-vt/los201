/*
 * sms_sensor_driver.c - ASSIGNMENT-02: simulated industrial sensor driver.
 *
 * Author: Vu Tung Minh (SE201043)
 * Course: LOS201 - Embedded Linux Systems
 * Target: ARMv7-A (Cortex-A9, QEMU vexpress-a9), Linux 5.15.0
 *
 * Exposes /dev/sms_sensor.  Every read() returns one CSV sample:
 *     <timestamp_ms>,<temperature>,<humidity>\n
 * Temperature is in [15.000, 45.000] C and humidity in [30.000, 90.000] %,
 * generated with get_random_u32().  /proc/sms_stats reports read_count,
 * last_temperature, last_humidity and driver uptime.  ioctl SMS_SET_INTERVAL
 * configures the minimum delay (ms) between two consecutive reads.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>
#include <linux/delay.h>

#include "sms_sensor.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vu Tung Minh - SE201043");
MODULE_DESCRIPTION("ASSIGNMENT-02: simulated temperature/humidity sensor");
MODULE_VERSION("1.0");

#define SMS_LINE_MAX 96

static int major_number = SMS_DRIVER_MAJOR;
static struct class  *sms_class;
static struct device *sms_device;
static struct proc_dir_entry *sms_proc_entry;

static DEFINE_MUTEX(sms_mutex);
static unsigned long read_count;
static int last_temp_milli = 25000;    /* 25.000 C  */
static int last_hum_milli  = 55000;    /* 55.000 %  */
static unsigned int min_interval_ms = 100;
static unsigned long last_read_jiffies;
static unsigned long load_jiffies;

/*
 * sms_read - produce one CSV sample and copy it to userspace.
 *
 * A streaming sensor yields a *new* sample on every call, so the file offset
 * is intentionally not used to signal EOF.  If the caller reads faster than
 * min_interval_ms it receives -EAGAIN and is expected to retry.
 *
 * Return: number of bytes copied, or a negative errno.
 */
static ssize_t sms_read(struct file *file, char __user *buf,
                        size_t len, loff_t *offset)
{
    char kbuf[SMS_LINE_MAX];
    int n;
    u64 now_ms;
    u32 r;

    mutex_lock(&sms_mutex);

    if (read_count > 0 && min_interval_ms > 0) {
        unsigned long elapsed = jiffies_to_msecs(jiffies - last_read_jiffies);
        if (elapsed < min_interval_ms) {
            mutex_unlock(&sms_mutex);
            return -EAGAIN;
        }
    }

    r = get_random_u32();
    last_temp_milli = 15000 + (int)(r % 30001);      /* 15.000 .. 45.000 C */
    r = get_random_u32();
    last_hum_milli  = 30000 + (int)(r % 60001);      /* 30.000 .. 90.000 % */

    now_ms = ktime_to_ms(ktime_get());

    n = scnprintf(kbuf, sizeof(kbuf), "%llu,%d.%03d,%d.%03d\n",
                  (unsigned long long)now_ms,
                  last_temp_milli / 1000, last_temp_milli % 1000,
                  last_hum_milli  / 1000, last_hum_milli  % 1000);

    read_count++;
    last_read_jiffies = jiffies;

    mutex_unlock(&sms_mutex);

    if (copy_to_user(buf, kbuf, n))
        return -EFAULT;

    pr_debug("sms_sensor: sample %d.%03d / %d.%03d\n",
             last_temp_milli / 1000, last_temp_milli % 1000,
             last_hum_milli / 1000, last_hum_milli % 1000);
    return n;
}

/*
 * sms_ioctl - SMS_SET_INTERVAL handler.
 * The value is clamped to [0, 60000] ms; 0 disables throttling.
 */
static long sms_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
    case SMS_SET_INTERVAL: {
        int v = (int)arg;
        if (v < 0)
            return -EINVAL;
        if (v > 60000)
            v = 60000;
        mutex_lock(&sms_mutex);
        min_interval_ms = v;
        mutex_unlock(&sms_mutex);
        pr_info("sms_sensor: interval set to %d ms\n", v);
        return 0;
    }
    default:
        return -ENOTTY;
    }
}

static const struct file_operations sms_fops = {
    .owner          = THIS_MODULE,
    .read           = sms_read,
    .unlocked_ioctl = sms_ioctl,
    .llseek         = no_llseek,
};

/* ---- /proc/sms_stats ---------------------------------------------------- */
static int sms_proc_show(struct seq_file *m, void *v)
{
    unsigned long rc, up;

    mutex_lock(&sms_mutex);
    rc = read_count;
    up = (jiffies - load_jiffies) / HZ;
    seq_printf(m, "sms_sensor driver statistics\n");
    seq_printf(m, "============================\n");
    seq_printf(m, "read_count       : %lu\n", rc);
    seq_printf(m, "last_temperature : %d.%03d C\n",
               last_temp_milli / 1000, last_temp_milli % 1000);
    seq_printf(m, "last_humidity    : %d.%03d %%\n",
               last_hum_milli / 1000, last_hum_milli % 1000);
    seq_printf(m, "driver_uptime    : %lu s\n", up);
    seq_printf(m, "min_interval_ms  : %u\n", min_interval_ms);
    mutex_unlock(&sms_mutex);
    return 0;
}

static int sms_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, sms_proc_show, NULL);
}

static const struct proc_ops sms_proc_fops = {
    .proc_open    = sms_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* ---- module init / exit ------------------------------------------------- */
static int __init sms_init(void)
{
    int ret;

    load_jiffies = jiffies;

    ret = register_chrdev(major_number, SMS_DEV_NAME, &sms_fops);
    if (ret < 0) {
        pr_err("sms_sensor: register_chrdev failed: %d\n", ret);
        return ret;
    }

    sms_class = class_create(THIS_MODULE, SMS_CLASS_NAME);
    if (IS_ERR(sms_class)) {
        unregister_chrdev(major_number, SMS_DEV_NAME);
        return PTR_ERR(sms_class);
    }

    sms_device = device_create(sms_class, NULL,
                               MKDEV(major_number, 0), NULL, SMS_DEV_NAME);
    if (IS_ERR(sms_device)) {
        class_destroy(sms_class);
        unregister_chrdev(major_number, SMS_DEV_NAME);
        return PTR_ERR(sms_device);
    }

    sms_proc_entry = proc_create("sms_stats", 0444, NULL, &sms_proc_fops);
    if (!sms_proc_entry)
        pr_warn("sms_sensor: cannot create /proc/sms_stats\n");

    pr_info("sms_sensor: ready (major=%d, /dev/%s)\n",
            major_number, SMS_DEV_NAME);
    return 0;
}

static void __exit sms_exit(void)
{
    if (sms_proc_entry)
        proc_remove(sms_proc_entry);
    device_destroy(sms_class, MKDEV(major_number, 0));
    class_destroy(sms_class);
    unregister_chrdev(major_number, SMS_DEV_NAME);
    pr_info("sms_sensor: unloaded\n");
}

module_init(sms_init);
module_exit(sms_exit);
