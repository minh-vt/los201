/*
 * LAB-02: Character Device Driver with procfs and sysfs interfaces
 * Course: Embedded Linux Systems (LOS201)
 * Author: Vu Tung Minh (SE201043)
 * Target Architecture: ARMv7-A (Cortex-A9, QEMU vexpress-a9)
 * Target Kernel: Linux 5.15.0 LTS
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/mutex.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vu Tung Minh - SE201043");
MODULE_DESCRIPTION("LAB-02: Character Device Driver with procfs and sysfs interfaces");
MODULE_VERSION("1.0");

#define DEVICE_NAME     "lab2"
#define CLASS_NAME      "lab2_class"
#define BUFFER_SIZE     1024
#define DRIVER_MAJOR    240  /* 240-254: reserved for local/experimental use */

/*
 * Global variables & internal state
 */
static int    major_number = DRIVER_MAJOR;
static char   device_buffer[BUFFER_SIZE];
static int    buffer_len = 0;
static int    open_count = 0;
static struct class  *lab2_class  = NULL;
static struct device *lab2_device = NULL;
static struct proc_dir_entry *proc_entry = NULL;

/*
 * Giai thich co che dong bo bang MUTEX:
 * Driver su dung DEFINE_MUTEX(lab2_mutex) de dong bo hoa truy cap den cac tai nguyen
 * dung chung gom: device_buffer, buffer_len, open_count.
 * Khi nhieu tien trinh userspace dong thoi goi open(), read(), write() hoac doc qua
 * procfs/sysfs, mutex ngan ngua Race Condition. Mutex dua cac process den sau vao
 * trang thai ngu (sleep) cho den khi lock duoc giai phong, khong gay lang phi chu ky CPU.
 */
static DEFINE_MUTEX(lab2_mutex);

/*
 * =========================================================================
 * PHAN A: FILE_OPERATIONS IMPLEMENTATION (open, release, read, write)
 * =========================================================================
 */

static int lab2_open(struct inode *inode, struct file *file) {
    mutex_lock(&lab2_mutex);
    open_count++;
    mutex_unlock(&lab2_mutex);
    pr_info("lab2_driver: device opened (count=%d)\n", open_count);
    return 0;
}

static int lab2_release(struct inode *inode, struct file *file) {
    pr_info("lab2_driver: device closed\n");
    return 0;
}

/*
 * Giai thich copy_to_user() vs memcpy():
 * Khong the su dung memcpy() truc tiep giua kernel buffer va user buffer vi:
 * 1. Khong gian dia chi ao (Virtual Address Space): Con tro buf thuoc user space
 *    (bang trang MMU cua tien trinh hien tai), con device_buffer nam o kernel space.
 * 2. Kiem tra tinh hop le: Con tro user space co the la con tro NULL, vung nho chua duoc cap phat,
 *    hoac vung chi doc. memcpy() khong kiem tra quyen va se gay ra Kernel Oops / Kernel Panic.
 * 3. Xu ly Page Fault: copy_to_user() tich hop san kha nang xu ly page fault trong truong hop
 *    trang nho user space bi swap hoac chua duoc nap vao RAM vat ly.
 */
static ssize_t lab2_read(struct file *file, char __user *buf,
                         size_t len, loff_t *offset) {
    int bytes_read = 0;

    mutex_lock(&lab2_mutex);

    if (*offset >= buffer_len) {
        mutex_unlock(&lab2_mutex);
        return 0;  /* EOF (End of File) */
    }

    if (len > buffer_len - *offset)
        len = buffer_len - *offset;

    if (copy_to_user(buf, device_buffer + *offset, len)) {
        mutex_unlock(&lab2_mutex);
        pr_err("lab2_driver: copy_to_user failed\n");
        return -EFAULT;
    }

    *offset += len;
    bytes_read = len;

    mutex_unlock(&lab2_mutex);

    pr_info("lab2_driver: sent %d bytes to user\n", bytes_read);
    return bytes_read;
}

/*
 * Giai thich copy_from_user() vs memcpy():
 * Tuong tu copy_to_user, copy_from_user() xac thuc con tro user buffer truoc khi doc.
 * Ham tra ve so byte CHUA duoc copy (0 la thanh cong hoan toan). Neu co loi truy cap,
 * tra ve ma loi -EFAULT (Bad Address) de bao ve tinh toan ven cua he thong kernel.
 */
static ssize_t lab2_write(struct file *file, const char __user *buf,
                          size_t len, loff_t *offset) {
    if (len > BUFFER_SIZE - 1) {
        pr_warn("lab2_driver: write too large (%zu bytes), truncating to %d\n",
                len, BUFFER_SIZE - 1);
        len = BUFFER_SIZE - 1;
    }

    mutex_lock(&lab2_mutex);

    if (copy_from_user(device_buffer, buf, len)) {
        mutex_unlock(&lab2_mutex);
        pr_err("lab2_driver: copy_from_user failed\n");
        return -EFAULT;
    }

    buffer_len = len;
    device_buffer[buffer_len] = '\0';  /* Dam bao chuoi duoc ket thuc bang null */

    mutex_unlock(&lab2_mutex);

    pr_info("lab2_driver: received %zu bytes: [%s]\n", len, device_buffer);
    return len;
}

static struct file_operations lab2_fops = {
    .owner   = THIS_MODULE,
    .open    = lab2_open,
    .release = lab2_release,
    .read    = lab2_read,
    .write   = lab2_write,
};

/*
 * =========================================================================
 * PHAN B.1: PROCFS IMPLEMENTATION (/proc/lab2_info)
 * =========================================================================
 * Giai thich seq_file API & single_open():
 * procfs duoc dung de xuat cac thong so trang thai, thong ke hoat dong cua driver.
 * Tuy nhien, neu du lieu vuot qua mot page (PAGE_SIZE = 4KB), giao dien doc thong thuong
 * de gay loi tran bo dem hoac can co che xu ly offset phuc tap.
 * seq_file API cung cap iterator an toan, tu dong quan ly buffer va offset.
 * single_open() la ham wrapper tien ich danh cho cac entry procfs nho, chi can goi
 * ham show mot lan de xuat toan bo thong tin mot cach gon gang va an toan.
 */
static int lab2_proc_show(struct seq_file *m, void *v) {
    mutex_lock(&lab2_mutex);
    seq_printf(m, "LAB-02 Driver Statistics\n");
    seq_printf(m, "======================\n");
    seq_printf(m, "Driver name   : %s\n", DEVICE_NAME);
    seq_printf(m, "Major number  : %d\n", major_number);
    seq_printf(m, "Buffer size   : %d bytes\n", BUFFER_SIZE);
    seq_printf(m, "Data length   : %d bytes\n", buffer_len);
    seq_printf(m, "Open count    : %d\n", open_count);
    if (buffer_len > 0)
        seq_printf(m, "Last data     : [%s]\n", device_buffer);
    else
        seq_printf(m, "Last data     : (empty)\n");
    mutex_unlock(&lab2_mutex);
    return 0;
}

static int lab2_proc_open(struct inode *inode, struct file *file) {
    return single_open(file, lab2_proc_show, NULL);
}

/*
 * Tu Linux 5.6 tro di, procfs su dung struct proc_ops thay cho struct file_operations
 */
static const struct proc_ops lab2_proc_fops = {
    .proc_open    = lab2_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/*
 * =========================================================================
 * PHAN B.2: SYSFS IMPLEMENTATION (/sys/class/lab2_class/lab2/)
 * =========================================================================
 * Giai thich DEVICE_ATTR_RO va co che kobject:
 * sysfs bieu dien mo hinh thiet bi huong doi tuong (Linux Device Model) thong qua
 * kobject. Moi device hoac driver la mot node kobject duoc anh xa thanh thu muc trong /sys.
 * Macro DEVICE_ATTR_RO(name) tu dong khai bao bien `dev_attr_##name` voi quyen chi doc
 * (0444) va gan callback doc la ham `name##_show`.
 * Thiet ke cua sysfs tuan thu nguyen ly "one value per file" (moi attribute chua 1 gia tri duy nhat),
 * giup scripts userspace de dang truy van va cau hinh ma khong can parse chuoi phuc tap.
 */
static ssize_t buffer_len_show(struct device *dev,
                               struct device_attribute *attr, char *buf) {
    ssize_t ret;
    mutex_lock(&lab2_mutex);
    ret = sprintf(buf, "%d\n", buffer_len);
    mutex_unlock(&lab2_mutex);
    return ret;
}

static ssize_t open_count_show(struct device *dev,
                               struct device_attribute *attr, char *buf) {
    ssize_t ret;
    mutex_lock(&lab2_mutex);
    ret = sprintf(buf, "%d\n", open_count);
    mutex_unlock(&lab2_mutex);
    return ret;
}

static ssize_t last_data_show(struct device *dev,
                              struct device_attribute *attr, char *buf) {
    ssize_t ret;
    mutex_lock(&lab2_mutex);
    if (buffer_len > 0)
        ret = sprintf(buf, "%s\n", device_buffer);
    else
        ret = sprintf(buf, "(empty)\n");
    mutex_unlock(&lab2_mutex);
    return ret;
}

static DEVICE_ATTR_RO(buffer_len);
static DEVICE_ATTR_RO(open_count);
static DEVICE_ATTR_RO(last_data);

static struct attribute *lab2_attrs[] = {
    &dev_attr_buffer_len.attr,
    &dev_attr_open_count.attr,
    &dev_attr_last_data.attr,
    NULL,
};

static struct attribute_group lab2_attr_group = {
    .attrs = lab2_attrs,
};

/*
 * =========================================================================
 * MODULE INITIALIZATION & CLEANUP
 * =========================================================================
 */
static int __init lab2_init(void) {
    int ret;
    pr_info("lab2_driver: initializing module\n");

    /* 1. Dang ky character device voi static major number 240 */
    ret = register_chrdev(major_number, DEVICE_NAME, &lab2_fops);
    if (ret < 0) {
        pr_err("lab2_driver: register_chrdev failed: %d\n", ret);
        return ret;
    }

    /* 2. Tao device class: /sys/class/lab2_class/ */
    lab2_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(lab2_class)) {
        pr_err("lab2_driver: class_create failed\n");
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(lab2_class);
    }

    /* 3. Tao device node: /sys/class/lab2_class/lab2/ */
    lab2_device = device_create(lab2_class, NULL,
                                MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(lab2_device)) {
        pr_err("lab2_driver: device_create failed\n");
        class_destroy(lab2_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(lab2_device);
    }

    /* 4. Dang ky sysfs attribute group */
    ret = sysfs_create_group(&lab2_device->kobj, &lab2_attr_group);
    if (ret) {
        pr_warn("lab2_driver: sysfs_create_group failed: %d\n", ret);
    }

    /* 5. Tao procfs entry: /proc/lab2_info */
    proc_entry = proc_create("lab2_info", 0444, NULL, &lab2_proc_fops);
    if (!proc_entry) {
        pr_warn("lab2_driver: failed to create /proc/lab2_info\n");
    } else {
        pr_info("lab2_driver: /proc/lab2_info created successfully\n");
    }

    pr_info("lab2_driver: loaded successfully, major=%d\n", major_number);
    return 0;
}

static void __exit lab2_exit(void) {
    /* 1. Go bo procfs entry */
    if (proc_entry) {
        proc_remove(proc_entry);
        pr_info("lab2_driver: removed /proc/lab2_info\n");
    }

    /* 2. Go bo sysfs attribute group */
    if (lab2_device) {
        sysfs_remove_group(&lab2_device->kobj, &lab2_attr_group);
    }

    /* 3. Huy device node va class */
    device_destroy(lab2_class, MKDEV(major_number, 0));
    class_destroy(lab2_class);

    /* 4. Huy dang ky character device */
    unregister_chrdev(major_number, DEVICE_NAME);

    pr_info("lab2_driver: module unloaded successfully\n");
}

module_init(lab2_init);
module_exit(lab2_exit);
