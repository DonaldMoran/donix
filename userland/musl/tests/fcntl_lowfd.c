/*
 * fcntl_lowfd -- exercise fcntl's flag subcommands on fd 0/1/2.
 *
 * Before the change this tests, sys_fcntl used get_file_slot()
 * for every subcommand except F_DUPFD, and get_file_slot()
 * refuses fd < 3.  So fcntl(0, F_GETFL) returned -EBADF even
 * after fd 0 had been redirected to a real file.
 *
 * The checks:
 *
 *   1. fcntl(0, F_GETFL) on a fresh console fd 0 -> 0.
 *      (The console sentinel is a valid slot; the flag case
 *      returns 0.  This passed before the change too.)
 *
 *   2. open a file; dup2 it onto fd 0; fcntl(0, F_GETFL) -> >= 0.
 *      (This is the change: fd 0 now holds a real file, and the
 *      flag command must not refuse it.)
 *
 *   3. close fd 0; fcntl(0, F_GETFL) -> -EBADF.
 *      (A closed fd is still refused.)
 *
 * Prints ok/FAIL per check and exits nonzero on any failure.
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

int main(void) {
    int fails = 0;
    int r;

    /* 1. console fd 0: F_GETFL returns 0. */
    errno = 0;
    r = fcntl(0, F_GETFL);
    if (r == 0) {
        printf("ok 1: fcntl(console 0, F_GETFL) -> 0\n");
    } else {
        printf("FAIL 1: fcntl(0, F_GETFL) -> %d errno=%d\n", r, errno);
        fails++;
    }

    /* 2. redirected fd 0: F_GETFL succeeds, not EBADF. */
    int fd = open("/HELLO-WORLD.TXT", O_RDONLY);
    if (fd < 0) {
        printf("FAIL 2: open /HELLO-WORLD.TXT: errno=%d\n", errno);
        return 1;
    }
    if (dup2(fd, 0) < 0) {
        printf("FAIL 2: dup2(%d, 0): errno=%d\n", fd, errno);
        return 1;
    }
    close(fd);

    errno = 0;
    r = fcntl(0, F_GETFL);
    if (r >= 0) {
        printf("ok 2: fcntl(redirected 0, F_GETFL) -> %d\n", r);
    } else {
        printf("FAIL 2: fcntl(redirected 0, F_GETFL) -> %d errno=%d\n",
               r, errno);
        fails++;
    }

    /* 3. closed fd 0: still EBADF. */
    close(0);
    errno = 0;
    r = fcntl(0, F_GETFL);
    if (r == -1 && errno == EBADF) {
        printf("ok 3: fcntl(closed 0, F_GETFL) -> EBADF\n");
    } else {
        printf("FAIL 3: fcntl(closed 0, F_GETFL) -> %d errno=%d\n",
               r, errno);
        fails++;
    }

    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
