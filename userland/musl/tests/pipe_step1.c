/*
 * pipe_step1.c -- pipe(2) smoke test: creation, round-trip, close.
 *
 * Verifies the basics of sys_pipe:
 *   1. pipe() returns two distinct, valid fds (>= 3, thanks to the
 *      console sentinels in fds 0/1/2).
 *   2. A small write-then-read round-trips bytes intact.
 *   3. Closing both ends does not crash, and a re-close of a closed
 *      fd is EBADF (proving the fd was released, not double-freed).
 *
 * WHAT THIS TEST DOES NOT COVER, AND WHY.
 *
 * An earlier version of this file asserted that a read on an empty
 * pipe returns -1/EAGAIN and that a write to a full pipe returns
 * -1/EAGAIN.  That was Step-1 semantics, when pipes were
 * non-blocking.  Steps 2 and 3 made read and write BLOCK: a reader
 * with a live writer and an empty ring now sleeps in hlt until the
 * writer produces, and a writer with a live reader and a full ring
 * sleeps until the reader drains.  Those EAGAIN assertions are no
 * longer true, and re-running them does not fail -- it HANGS,
 * because nothing wakes the blocked process.
 *
 * The empty/full behavior is now tested where it can be tested
 * safely:
 *   - pipe_step2: blocking + directed wake (forks a peer).
 *   - pipe_step3: EOF, -EPIPE, dup-aware closed-end counts.
 *   - pipe_step3b: wake on the exit path.
 *
 * This file is the smoke test that needs no fork: create a pipe,
 * move bytes through it, close it.  Keep it that way.
 *
 * Build: picked up by the same Makefile that builds the other
 * userland/musl/tests/*.c programs.  Links statically against
 * donix's musl.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
        printf("pipe_step1: cannot continue without a pipe\n");
        return 1;
    }

    /* 2. Round-trip.  A small write, then a read of exactly that
     *    many bytes.  Both ends have live peers, so neither call
     *    blocks: the write has room, the read has data. */
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

    /*
     * 3. Close.  No assertion for "the kernel freed the ring"; the
     *    serial log is the check for leaks.  The re-close EBADF IS
     *    an assertion, because it proves the fd was released and
     *    not silently double-freed.
     */
    errno = 0;
    check(close(fds[0]) == 0, "close(read end) returns 0");
    check(close(fds[1]) == 0, "close(write end) returns 0");

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
