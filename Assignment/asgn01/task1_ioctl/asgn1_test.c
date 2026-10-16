/*
 * asgn1_test.c - userspace ioctl exerciser for ASSIGNMENT-01 Task 1.
 *
 * Author: Vu Tung Minh (SE201043)
 *
 * Opens /dev/asgn1 and calls all four ioctl commands, printing each field of
 * the statistics structure.  It also validates RESET_BUFFER and both modes.
 * Exit code 0 = all checks passed, 1 = a check failed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "asgn1_ioctl.h"

#define DEV_PATH "/dev/asgn1"

static int failures = 0;

static void check(const char *name, int cond)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond)
        failures++;
}

int main(void)
{
    int fd, m, ret;
    char ver[ASGN1_VERSION_LEN];
    char buf[256];
    struct asgn1_stats st;
    ssize_t n;

    fd = open(DEV_PATH, O_RDWR);
    if (fd < 0) {
        perror("open " DEV_PATH);
        return 1;
    }
    printf("Opened %s (fd=%d)\n", DEV_PATH, fd);

    /* 1) GET_VERSION ---------------------------------------------------- */
    memset(ver, 0, sizeof(ver));
    ret = ioctl(fd, ASGN1_GET_VERSION, ver);
    printf("[ioctl] ASGN1_GET_VERSION -> ret=%d version=\"%s\"\n", ret, ver);
    check("GET_VERSION returns non-empty string",
          ret == 0 && strlen(ver) > 0);

    /* 2) SET_MODE KERNEL + write --------------------------------------- */
    m = ASGN1_MODE_KERNEL;
    ret = ioctl(fd, ASGN1_SET_MODE, m);
    printf("[ioctl] ASGN1_SET_MODE(%d=KERNEL) -> ret=%d\n", m, ret);
    check("SET_MODE KERNEL accepted", ret == 0);

    n = pwrite(fd, "hello-kernel", 12, 0);
    (void)n;
    memset(buf, 0, sizeof(buf));
    n = pread(fd, buf, sizeof(buf) - 1, 0);
    printf("  KERNEL mode read back: \"%s\"\n", buf);
    check("KERNEL mode prepends [KERNEL]",
          n > 0 && strncmp(buf, "[KERNEL] ", 9) == 0);

    /* 3) SET_MODE ECHO + write ----------------------------------------- */
    m = ASGN1_MODE_ECHO;
    ret = ioctl(fd, ASGN1_SET_MODE, m);
    printf("[ioctl] ASGN1_SET_MODE(%d=ECHO) -> ret=%d\n", m, ret);
    check("SET_MODE ECHO accepted", ret == 0);

    n = pwrite(fd, "hello-echo", 10, 0);
    (void)n;
    memset(buf, 0, sizeof(buf));
    n = pread(fd, buf, sizeof(buf) - 1, 0);
    printf("  ECHO mode read back: \"%s\"\n", buf);
    check("ECHO mode stores data verbatim", strcmp(buf, "hello-echo") == 0);

    /* invalid mode must be rejected with EINVAL */
    ret = ioctl(fd, ASGN1_SET_MODE, 7);
    printf("[ioctl] ASGN1_SET_MODE(7=invalid) -> ret=%d errno=%d\n",
           ret, errno);
    check("invalid mode rejected with EINVAL", ret == -1 && errno == EINVAL);

    /* 4) GET_STATS ------------------------------------------------------ */
    memset(&st, 0, sizeof(st));
    ret = ioctl(fd, ASGN1_GET_STATS, &st);
    printf("[ioctl] ASGN1_GET_STATS -> ret=%d\n", ret);
    printf("        open_count      = %d\n", st.open_count);
    printf("        write_count     = %d\n", st.write_count);
    printf("        read_count      = %d\n", st.read_count);
    printf("        buffer_len      = %d\n", st.buffer_len);
    printf("        last_write_size = %d\n", st.last_write_size);
    check("GET_STATS buffer_len == 10 (hello-echo)", st.buffer_len == 10);
    check("GET_STATS last_write_size == 10", st.last_write_size == 10);
    check("GET_STATS write_count == 2", st.write_count == 2);

    /* 5) RESET_BUFFER --------------------------------------------------- */
    ret = ioctl(fd, ASGN1_RESET_BUFFER);
    printf("[ioctl] ASGN1_RESET_BUFFER -> ret=%d\n", ret);
    memset(buf, 0, sizeof(buf));
    n = pread(fd, buf, sizeof(buf) - 1, 0);
    printf("  after reset, read returns %zd bytes: \"%s\"\n", n, buf);
    check("after RESET buffer is empty", n == 0);

    memset(&st, 0, sizeof(st));
    ioctl(fd, ASGN1_GET_STATS, &st);
    printf("  after reset stats: open=%d write=%d read=%d len=%d\n",
           st.open_count, st.write_count, st.read_count, st.buffer_len);
    check("RESET zeroed write_count", st.write_count == 0);
    check("RESET zeroed read_count", st.read_count == 0);
    check("RESET zeroed buffer_len", st.buffer_len == 0);

    /* 6) unknown command must give ENOTTY ------------------------------- */
    errno = 0;
    ret = ioctl(fd, _IO(ASGN1_IOC_MAGIC, 9));
    printf("[ioctl] unknown cmd -> ret=%d errno=%d (%s)\n",
           ret, errno, strerror(errno));
    check("unknown ioctl returns ENOTTY", ret == -1 && errno == ENOTTY);

    close(fd);
    printf("\n%s (%d failure(s))\n", failures ? "RESULT: FAIL" : "RESULT: ALL PASS",
           failures);
    return failures ? 1 : 0;
}
