
/*
 * pipe_step1.c -- Step 1 smoke test for donix pipe(2).
 *
 * Verifies the non-blocking pipe created by sys_pipe:
 *   1. pipe() returns two distinct, valid fds (>= 3, thanks to the
 *      console sentinels in fds 0/1/2).
 *   2. A small write-then-read round-trips bytes intact.
 *   3. A read on an empty pipe returns -1/EAGAIN.
 *   4. A write that exactly fills the pipe succeeds, and the next
 *      byte returns -1/EAGAIN.
 *   5. Closing both ends does not crash and does not leak (checked
 *      by the absence of kernel diagnostics on the serial log).
 *
 * Step 1 does NOT test blocking, EOF, or EPIPE -- those are Step
 * 2/3.  In particular, a read on an empty pipe returns EAGAIN, not
 * 0; the 0-on-EOF case requires a writer to have closed, which is
 * not yet distinguishable from a writer that is merely idle.
 *
 * Build: this file should be picked up by the same Makefile that
 * builds the other userland/musl/tests/*.c programs.  It links
 * statically against donix's musl.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Must match PIPE_DEFAULT_CAPACITY in user_syscall.c.  If that
 * constant changes, change this too, or test_full will not
 * actually test the boundary it claims to. */
#define PIPE_CAPACITY 4096

static int failures = 0;

static void check(int cond, const char* what) {
    if (cond) {
        printf("  ok   - %s\n", what);
    } else {
        printf("  FAIL - %s (errno=%d)\n", what, errno);
        failures++;
    }
}

int main(void) {
    printf("pipe_step1: starting\n");

    int fds[2];
    memset(fds, 0, sizeof(fds));

    /* 1. Creation. */
    errno = 0;
    int r = pipe(fds);
    check(r == 0, "pipe() returns 0");
    check(fds[0] >= 3, "read end is a low-but-not-stdio fd");
    check(fds[1] >= 3, "write end is a low-but-not-stdio fd");
    check(fds[0] != fds[1], "read and write ends are distinct");
    if (r != 0) {
        /* Without fds, the rest is meaningless. */
        printf("pipe_step1: cannot continue without a pipe\n");
        return 1;
    }

    /* 2. Round-trip. */
    const char* msg = "hello";
    errno = 0;
    ssize_t nw = write(fds[1], msg, 5);
    check(nw == 5, "write() of 5 bytes returns 5");

    char buf[16];
    memset(buf, 0, sizeof(buf));
    errno = 0;
    ssize_t nr = read(fds[0], buf, 5);
    check(nr == 5, "read() of 5 bytes returns 5");
    check(memcmp(buf, msg, 5) == 0, "read() returns the bytes written");

    /* 3. Empty pipe -> EAGAIN.  This is the first real test of the
     *    non-blocking path: the ring is empty, sys_read's pipe
     *    branch must report EAGAIN rather than block or return 0. */
    errno = 0;
    nr = read(fds[0], buf, sizeof(buf));
    check(nr == -1, "read() on empty pipe returns -1");
    check(errno == EAGAIN, "read() on empty pipe sets errno=EAGAIN");

    /* 4. Full pipe -> EAGAIN on the next byte.  Fill the ring with
     *    exactly PIPE_CAPACITY bytes, then try one more.  The fill
     *    must be a single write so it lands as one atomic append
     *    into the empty ring; if sys_write's free-space math is
     *    off by one, either the fill will short-write (and nw !=
     *    PIPE_CAPACITY) or the extra byte will succeed (and the
     *    next check fails). */
    static char fill[PIPE_CAPACITY];
    memset(fill, 'x', sizeof(fill));

    errno = 0;
    nw = write(fds[1], fill, sizeof(fill));
    check(nw == PIPE_CAPACITY, "write() filling the pipe returns capacity");

    errno = 0;
    nw = write(fds[1], "z", 1);
    check(nw == -1, "write() on full pipe returns -1");
    check(errno == EAGAIN, "write() on full pipe sets errno=EAGAIN");

    /* Drain the pipe, to leave it empty before the close test. */
    {
        static char drain[PIPE_CAPACITY];
        size_t got = 0;
        while (got < PIPE_CAPACITY) {
            ssize_t g = read(fds[0], drain, sizeof(drain));
            if (g <= 0) break;
            got += (size_t)g;
        }
        check(got == PIPE_CAPACITY, "draining the pipe recovers every byte");
    }

    /* 5. Close.  After this, the kernel should have freed the two
     *    slots, the pipe object, and the 4 KB ring buffer.  There
     *    is no userspace-visible assertion for "freed"; the test
     *    is that the serial log shows no diagnostic and that the
     *    process exits cleanly. */
    errno = 0;
    check(close(fds[0]) == 0, "close(read end) returns 0");
    check(close(fds[1]) == 0, "close(write end) returns 0");

    /* A second close must fail with EBADF, proving the fd really
     * was released and not silently double-freed. */
    errno = 0;
    r = close(fds[1]);
    check(r == -1, "re-close of a closed fd returns -1");
    check(errno == EBADF, "re-close of a closed fd sets errno=EBADF");

    if (failures == 0) {
        printf("STEP1 OK\n");
        return 0;
    }
    printf("STEP1 FAILED: %d check(s) failed\n", failures);
    return 1;
}
