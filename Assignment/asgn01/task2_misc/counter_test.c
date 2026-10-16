/*
 * counter_test.c - userspace exerciser for ASSIGNMENT-01 Task 2.
 *
 * Author: Vu Tung Minh (SE201043)
 *
 * Verifies auto-increment on read, set/reset through write(), and rejection of
 * invalid input.  Exit code 0 = all checks passed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define DEV_PATH "/dev/counter"

static int failures = 0;

static void check(const char *name, int cond)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond)
        failures++;
}

/* read the current value as an integer using a fresh open() */
static int read_value(void)
{
    int fd = open(DEV_PATH, O_RDONLY);
    char buf[32];
    ssize_t n;

    if (fd < 0) {
        perror("open " DEV_PATH);
        return -1;
    }
    memset(buf, 0, sizeof(buf));
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return -1;
    return atoi(buf);
}

static int write_str(const char *s)
{
    int fd = open(DEV_PATH, O_WRONLY);
    ssize_t n;

    if (fd < 0) {
        perror("open " DEV_PATH);
        return -1;
    }
    n = write(fd, s, strlen(s));
    close(fd);
    return (int)n;
}

int main(void)
{
    int v;

    printf("=== counter_driver userspace test ===\n");

    /* start from a known state */
    write_str("0");
    v = read_value();
    printf("after write '0', first read = %d (expect 0)\n", v);
    check("write '0' resets to 0", v == 0);

    v = read_value();
    printf("second read = %d (expect 1, auto-increment)\n", v);
    check("read auto-increments (0 -> 1)", v == 1);

    v = read_value();
    printf("third read = %d (expect 2)\n", v);
    check("read auto-increments (1 -> 2)", v == 2);

    if (write_str("100") < 0) { /* set */ }
    v = read_value();
    printf("after write '100', read = %d (expect 100)\n", v);
    check("write '100' sets counter", v == 100);

    v = read_value();
    printf("next read = %d (expect 101)\n", v);
    check("counter continues after set", v == 101);

    if (write_str("0") < 0) { /* reset */ }
    v = read_value();
    printf("after reset, read = %d (expect 0)\n", v);
    check("reset to 0 works", v == 0);

    errno = 0;
    v = write_str("-5");
    printf("write '-5' -> %d errno=%d (%s)\n", v, errno, strerror(errno));
    check("negative value rejected (EINVAL)", v < 0 && errno == EINVAL);

    errno = 0;
    v = write_str("abc");
    printf("write 'abc' -> %d errno=%d (%s)\n", v, errno, strerror(errno));
    check("non-numeric value rejected (EINVAL)", v < 0 && errno == EINVAL);

    printf("\n%s (%d failure(s))\n",
           failures ? "RESULT: FAIL" : "RESULT: ALL PASS", failures);
    return failures ? 1 : 0;
}
