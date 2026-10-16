/*
 * asgn1_ioctl.h - ioctl interface definition for ASSIGNMENT-01 Task 1.
 *
 * Author: Vu Tung Minh (SE201043)
 * Course: LOS201 - Embedded Linux Systems
 *
 * Shared between the kernel module (asgn1_driver.c) and the userspace
 * test program.  The magic number and the command numbers must match on
 * both sides, otherwise the kernel will answer -ENOTTY.
 */
#ifndef ASGN1_IOCTL_H
#define ASGN1_IOCTL_H

#include <linux/types.h>
#include <linux/ioctl.h>

/* 'k' = 0x6b, arbitrary but unique to this driver */
#define ASGN1_IOC_MAGIC   'k'

#define ASGN1_VERSION_LEN 64

/* Mode values for ASGN1_SET_MODE */
#define ASGN1_MODE_KERNEL 0   /* write() prepends "[KERNEL] " */
#define ASGN1_MODE_ECHO   1   /* write() stores data verbatim  */

/* Statistics structure returned by ASGN1_GET_STATS (direction: user reads) */
struct asgn1_stats {
    int open_count;       /* number of open() calls                  */
    int write_count;      /* number of write() calls                 */
    int read_count;       /* number of read() calls                  */
    int buffer_len;       /* current number of bytes in the buffer   */
    int last_write_size;  /* size of the most recent write()         */
};

/*
 * ioctl commands.  The direction bits encode the data flow:
 *   _IO   : no argument
 *   _IOR  : kernel -> userspace (copy_to_user)
 *   _IOW  : userspace -> kernel (copy_from_user)
 *   _IOWR : both directions
 */
#define ASGN1_RESET_BUFFER  _IO (ASGN1_IOC_MAGIC, 0)
#define ASGN1_GET_STATS     _IOR(ASGN1_IOC_MAGIC, 1, struct asgn1_stats)
#define ASGN1_SET_MODE      _IOW(ASGN1_IOC_MAGIC, 2, int)
#define ASGN1_GET_VERSION   _IOR(ASGN1_IOC_MAGIC, 3, char[ASGN1_VERSION_LEN])

#define ASGN1_IOC_MAXNR     3

#endif /* ASGN1_IOCTL_H */
