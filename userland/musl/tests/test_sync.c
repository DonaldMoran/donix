#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("test_sync: starting\n");

    /* 1. sync() */
    sync();

    /* 2. fsync(-1) -> EBADF */
    errno = 0;
    if (fsync(-1) != -1 || errno != EBADF) {
        printf("FAIL: fsync(-1) did not return EBADF (errno=%d)\n", errno);
        return 1;
    }

    /* 3. fsync(99) -> EBADF */
    errno = 0;
    if (fsync(99) != -1 || errno != EBADF) {
        printf("FAIL: fsync(99) did not return EBADF (errno=%d)\n", errno);
        return 1;
    }

    /* 4. pipe -> fsync -> EINVAL */
    int p[2];
    if (pipe(p) == 0) {
        errno = 0;
        if (fsync(p[0]) != -1 || errno != EINVAL) {
            printf("FAIL: fsync(p[0]) did not return EINVAL (errno=%d)\n", errno);
            return 1;
        }
        errno = 0;
        if (fsync(p[1]) != -1 || errno != EINVAL) {
            printf("FAIL: fsync(p[1]) did not return EINVAL (errno=%d)\n", errno);
            return 1;
        }
        close(p[0]);
        close(p[1]);
    }

    /* 5. regular file fsync */
    int fd = open("/tmp/test_sync_file.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        printf("FAIL: open file failed (errno=%d)\n", errno);
        return 1;
    }
    if (write(fd, "hello sync\n", 11) != 11) {
        printf("FAIL: write failed\n");
        return 1;
    }
    if (fsync(fd) != 0) {
        printf("FAIL: fsync(fd) failed (errno=%d)\n", errno);
        return 1;
    }
    close(fd);

    /* 6. directory fsync */
    int dfd = open("/tmp", O_RDONLY);
    if (dfd >= 0) {
        if (fsync(dfd) != 0) {
            printf("FAIL: fsync(dfd) failed (errno=%d)\n", errno);
            return 1;
        }
        close(dfd);
    }

    printf("PASS: test_sync\n");
    return 0;
}
