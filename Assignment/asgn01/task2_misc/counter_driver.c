/*
 * counter_driver.c - ASSIGNMENT-01, Task 2: misc device counter driver.
 *
 * Author: Vu Tung Minh (SE201043)
 * Course: LOS201 - Embedded Linux Systems
 * Target: ARMv7-A (Cortex-A9, QEMU vexpress-a9), Linux 5.15.0
 *
 * A tiny "hardware counter" driver registered with the miscdevice framework,
 * so the kernel hands out MISC_MAJOR (10) automatically and we only pick a
 * dynamically allocated minor number.  The device node is /dev/counter.
 *
 *   read()   : returns "<value>\n" and then increments the counter.
 *   write()  : parses a decimal integer and sets the counter (kstrtoint).
 *   /proc/counter_info : current value + total reads + total writes.
 *
 * The counter uses atomic_t so read/increment is safe on SMP without a lock.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/atomic.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/kstrtox.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vu Tung Minh - SE201043");
MODULE_DESCRIPTION("ASSIGNMENT-01 Task 2: misc counter device");
MODULE_VERSION("1.0");

#define DEVICE_NAME "counter"

/* shared state ------------------------------------------------------------ */
static atomic_t counter = ATOMIC_INIT(0);   /* current counter value          */
static int total_reads = 0;                 /* statistics, guarded by mutex   */
static int total_writes = 0;
static DEFINE_MUTEX(counter_mutex);
static struct proc_dir_entry *counter_proc;

/*
 * counter_read - return the current value then auto-increment.
 *
 * *ppos implements EOF: the value is only produced once per open(), so a
 * second call with *ppos != 0 returns 0 and cat terminates.
 */
static ssize_t counter_read(struct file *file, char __user *buf,
                            size_t len, loff_t *ppos)
{
    int val;
    char kbuf[32];
    int n;

    if (*ppos != 0)
        return 0;                     /* already delivered -> EOF */

    val = atomic_read(&counter);
    n = scnprintf(kbuf, sizeof(kbuf), "%d\n", val);

    if (len < (size_t)n)
        return -EINVAL;
    if (copy_to_user(buf, kbuf, n))
        return -EFAULT;

    *ppos += n;
    atomic_inc(&counter);             /* auto-increment after each read */

    mutex_lock(&counter_mutex);
    total_reads++;
    mutex_unlock(&counter_mutex);

    pr_info("counter_driver: read -> %d (next=%d)\n", val,
            atomic_read(&counter));
    return n;
}

/*
 * counter_write - set the counter to a decimal integer supplied by the user.
 * Rejects empty input, non-numeric text and negative values with -EINVAL.
 */
static ssize_t counter_write(struct file *file, const char __user *buf,
                             size_t len, loff_t *ppos)
{
    char kbuf[32];
    int val, ret;

    if (len == 0 || len >= sizeof(kbuf))
        return -EINVAL;
    if (copy_from_user(kbuf, buf, len))
        return -EFAULT;
    kbuf[len] = '\0';

    ret = kstrtoint(strstrip(kbuf), 10, &val);
    if (ret || val < 0) {
        pr_warn("counter_driver: invalid write \"%s\" -> -EINVAL\n", kbuf);
        return -EINVAL;
    }

    atomic_set(&counter, val);

    mutex_lock(&counter_mutex);
    total_writes++;
    mutex_unlock(&counter_mutex);

    pr_info("counter_driver: write -> counter=%d\n", val);
    return len;
}

/* ---- /proc/counter_info ------------------------------------------------- */
static int counter_proc_show(struct seq_file *m, void *v)
{
    int reads, writes;

    mutex_lock(&counter_mutex);
    reads = total_reads;
    writes = total_writes;
    mutex_unlock(&counter_mutex);

    seq_printf(m, "counter_driver statistics\n");
    seq_printf(m, "=========================\n");
    seq_printf(m, "Current value : %d\n", atomic_read(&counter));
    seq_printf(m, "Total reads   : %d\n", reads);
    seq_printf(m, "Total writes  : %d\n", writes);
    return 0;
}

static int counter_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, counter_proc_show, NULL);
}

static const struct proc_ops counter_proc_fops = {
    .proc_open    = counter_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static const struct file_operations counter_fops = {
    .owner          = THIS_MODULE,
    .read           = counter_read,
    .write          = counter_write,
    .llseek         = no_llseek,
};

static struct miscdevice counter_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DEVICE_NAME,
    .fops  = &counter_fops,
    .mode  = 0666,
};

/* ---- module init / exit ------------------------------------------------- */
static int __init counter_init(void)
{
    int ret;

    ret = misc_register(&counter_misc);
    if (ret) {
        pr_err("counter_driver: misc_register failed: %d\n", ret);
        return ret;
    }

    counter_proc = proc_create("counter_info", 0444, NULL, &counter_proc_fops);
    if (!counter_proc)
        pr_warn("counter_driver: cannot create /proc/counter_info\n");

    pr_info("counter_driver: registered as /dev/%s (misc minor=%d)\n",
            DEVICE_NAME, counter_misc.minor);
    return 0;
}

static void __exit counter_exit(void)
{
    if (counter_proc)
        proc_remove(counter_proc);
    misc_deregister(&counter_misc);
    pr_info("counter_driver: unloaded\n");
}

module_init(counter_init);
module_exit(counter_exit);
