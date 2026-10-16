/*
 * asgn1_driver.c - ASSIGNMENT-01, Task 1: character device with ioctl interface.
 *
 * Author: Vu Tung Minh (SE201043)
 * Course: LOS201 - Embedded Linux Systems
 * Target: ARMv7-A (Cortex-A9, QEMU vexpress-a9), Linux 5.15.0
 *
 * The driver exposes /dev/asgn1 (major 241, minor 0) and supports read/write
 * plus four ioctl commands declared in asgn1_ioctl.h:
 *   ASGN1_RESET_BUFFER, ASGN1_GET_STATS, ASGN1_SET_MODE, ASGN1_GET_VERSION.
 *
 * Layers:
 *   data layer      -> device_buffer + counters, protected by asgn1_mutex
 *   operation layer -> lab2-style file_operations (open/read/write/release)
 *   interface layer -> VFS (chrdev) + ioctl command dispatcher
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/string.h>

#include "asgn1_ioctl.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vu Tung Minh - SE201043");
MODULE_DESCRIPTION("ASSIGNMENT-01 Task 1: ioctl character device driver");
MODULE_VERSION("1.0");

#define DEVICE_NAME     "asgn1"
#define CLASS_NAME      "asgn1_class"
#define BUFFER_SIZE     1024
#define DRIVER_MAJOR    241               /* 241-254 reserved for local use      */
#define ASGN1_VERSION_STR "asgn1_driver v1.0 - SE201043"
#define ASGN1_PREFIX      "[KERNEL] "      /* prepended in ASGN1_MODE_KERNEL      */

/* ---- data layer --------------------------------------------------------- */
static int  major_number = DRIVER_MAJOR;
static char device_buffer[BUFFER_SIZE];
static int  buffer_len = 0;
static int  open_count = 0;
static int  write_count = 0;
static int  read_count = 0;
static int  last_write_size = 0;
static int  mode = ASGN1_MODE_ECHO;       /* default = Echo mode             */

static struct class  *asgn1_class  = NULL;
static struct device *asgn1_device = NULL;

/*
 * asgn1_mutex serialises every access to the buffer and to the counters.
 * Without it two concurrent write()/read()/ioctl() calls could interleave
 * inside the read-modify-write sequences and corrupt buffer_len / counters.
 */
static DEFINE_MUTEX(asgn1_mutex);

/* ---- operation layer ---------------------------------------------------- */

/*
 * asgn1_open - called on every open("/dev/asgn1").
 * Increments the open counter and returns 0.
 */
static int asgn1_open(struct inode *inode, struct file *file)
{
    mutex_lock(&asgn1_mutex);
    open_count++;
    mutex_unlock(&asgn1_mutex);
    pr_info("asgn1_driver: open (open_count=%d)\n", open_count);
    return 0;
}

/*
 * asgn1_release - called on the last close() of the device.
 * Only logs; no per-open resource is allocated.
 */
static int asgn1_release(struct inode *inode, struct file *file)
{
    pr_info("asgn1_driver: release\n");
    return 0;
}

/*
 * asgn1_read - copy the internal buffer to userspace.
 *
 * Because user and kernel pointers live in different virtual address spaces,
 * copy_to_user() (not memcpy()) is used.  *offset implements the VFS file
 * position so that a sequence of read() calls gets successive chunks and
 * finally returns 0 (EOF).
 *
 * Return: number of bytes copied, or a negative errno.
 */
static ssize_t asgn1_read(struct file *file, char __user *buf,
                          size_t len, loff_t *offset)
{
    ssize_t ret;

    mutex_lock(&asgn1_mutex);

    if (*offset >= buffer_len) {          /* nothing left -> EOF */
        mutex_unlock(&asgn1_mutex);
        return 0;
    }
    if (len > buffer_len - *offset)
        len = buffer_len - *offset;

    if (copy_to_user(buf, device_buffer + *offset, len)) {
        mutex_unlock(&asgn1_mutex);
        pr_err("asgn1_driver: copy_to_user failed\n");
        return -EFAULT;
    }
    *offset += len;
    read_count++;
    ret = len;

    mutex_unlock(&asgn1_mutex);

    pr_info("asgn1_driver: read %zd bytes\n", ret);
    return ret;
}

/*
 * asgn1_write - store user data in the internal buffer.
 *
 * Mode is honoured here: in ASGN1_MODE_KERNEL the string "[KERNEL] " is
 * prepended before the data is stored.  copy_from_user() validates the user
 * pointer and reports partial copies, so it is used instead of memcpy().
 *
 * Return: number of bytes consumed, or a negative errno.
 */
static ssize_t asgn1_write(struct file *file, const char __user *buf,
                           size_t len, loff_t *offset)
{
    int prefix_len;
    size_t maxlen;

    if (len == 0)
        return 0;

    mutex_lock(&asgn1_mutex);

    /* build the (optional) prefix directly inside device_buffer so that no
     * large on-stack buffer is needed (-Wframe-larger-than friendly) */
    prefix_len = (mode == ASGN1_MODE_KERNEL) ? (int)strlen(ASGN1_PREFIX) : 0;
    if (prefix_len)
        memcpy(device_buffer, ASGN1_PREFIX, prefix_len);

    maxlen = BUFFER_SIZE - 1 - prefix_len;
    if (len > maxlen)
        len = maxlen;

    if (copy_from_user(device_buffer + prefix_len, buf, len)) {
        mutex_unlock(&asgn1_mutex);
        return -EFAULT;
    }

    buffer_len = prefix_len + (int)len;
    device_buffer[buffer_len] = '\0';
    write_count++;
    last_write_size = (int)len;

    mutex_unlock(&asgn1_mutex);

    pr_info("asgn1_driver: write %zu bytes (mode=%d, stored=%d)\n",
            len, mode, buffer_len);
    return len;
}

/*
 * asgn1_ioctl - command dispatcher (VFS calls this for ioctl()).
 *
 * Handles the four commands from asgn1_ioctl.h and returns -ENOTTY for any
 * unknown command, as required by the ioctl convention.
 *
 * Return: 0 (or number of bytes) on success, negative errno on failure.
 */
static long asgn1_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct asgn1_stats stats;

    switch (cmd) {

    case ASGN1_RESET_BUFFER: {
        mutex_lock(&asgn1_mutex);
        memset(device_buffer, 0, BUFFER_SIZE);
        buffer_len = 0;
        write_count = 0;
        read_count = 0;
        last_write_size = 0;
        mutex_unlock(&asgn1_mutex);
        pr_info("asgn1_driver: ioctl RESET_BUFFER (buffer=%d, counters=0)\n",
                buffer_len);
        return 0;
    }

    case ASGN1_GET_STATS: {
        mutex_lock(&asgn1_mutex);
        stats.open_count      = open_count;
        stats.write_count     = write_count;
        stats.read_count      = read_count;
        stats.buffer_len      = buffer_len;
        stats.last_write_size = last_write_size;
        mutex_unlock(&asgn1_mutex);

        if (copy_to_user((struct asgn1_stats __user *)arg,
                         &stats, sizeof(stats))) {
            pr_err("asgn1_driver: GET_STATS copy_to_user failed\n");
            return -EFAULT;
        }
        pr_info("asgn1_driver: ioctl GET_STATS (o=%d w=%d r=%d len=%d)\n",
                stats.open_count, stats.write_count,
                stats.read_count, stats.buffer_len);
        return 0;
    }

    case ASGN1_SET_MODE: {
        int new_mode = (int)arg;
        if (new_mode != ASGN1_MODE_KERNEL && new_mode != ASGN1_MODE_ECHO)
            return -EINVAL;
        mutex_lock(&asgn1_mutex);
        mode = new_mode;
        mutex_unlock(&asgn1_mutex);
        pr_info("asgn1_driver: ioctl SET_MODE -> %s\n",
                mode == ASGN1_MODE_KERNEL ? "KERNEL" : "ECHO");
        return 0;
    }

    case ASGN1_GET_VERSION: {
        char ver[ASGN1_VERSION_LEN];
        strscpy(ver, ASGN1_VERSION_STR, sizeof(ver));
        /* copy string including terminating NUL, bounded by 64 bytes */
        if (copy_to_user((char __user *)arg, ver, strlen(ver) + 1)) {
            pr_err("asgn1_driver: GET_VERSION copy_to_user failed\n");
            return -EFAULT;
        }
        pr_info("asgn1_driver: ioctl GET_VERSION\n");
        return 0;
    }

    default:
        pr_warn("asgn1_driver: unknown ioctl 0x%x -> -ENOTTY\n", cmd);
        return -ENOTTY;
    }
}

static struct file_operations asgn1_fops = {
    .owner          = THIS_MODULE,
    .open           = asgn1_open,
    .release        = asgn1_release,
    .read           = asgn1_read,
    .write          = asgn1_write,
    .unlocked_ioctl = asgn1_ioctl,
};

/* ---- module init / exit ------------------------------------------------- */

static int __init asgn1_init(void)
{
    int ret;

    pr_info("asgn1_driver: init, major=%d\n", major_number);

    ret = register_chrdev(major_number, DEVICE_NAME, &asgn1_fops);
    if (ret < 0) {
        pr_err("asgn1_driver: register_chrdev failed: %d\n", ret);
        return ret;
    }

    asgn1_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(asgn1_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(asgn1_class);
    }

    /* devtmpfs will create /dev/asgn1 automatically for this device */
    asgn1_device = device_create(asgn1_class, NULL,
                                 MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(asgn1_device)) {
        class_destroy(asgn1_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(asgn1_device);
    }

    pr_info("asgn1_driver: ready (mode=ECHO default)\n");
    return 0;
}

static void __exit asgn1_exit(void)
{
    device_destroy(asgn1_class, MKDEV(major_number, 0));
    class_destroy(asgn1_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    pr_info("asgn1_driver: unloaded\n");
}

module_init(asgn1_init);
module_exit(asgn1_exit);
