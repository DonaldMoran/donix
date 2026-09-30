
/*
 * pipe_step2.c -- Step 2 test for donix pipe(2) blocking.
 *
 * Step 1 proved the pipe object and the non-blocking I/O paths.
 * Step 2 adds blocking and the directed wake.  This test proves
 * both:
 *
 *   1. A read on an empty pipe BLOCKS.  The parent creates a
 *      pipe, forks a child, and immediately calls read() on the
 *      empty pipe.  If Step 2 is broken (falls back to EAGAIN)
 *      the read returns -1/EAGAIN, which is a FAIL.  If Step 2
 *      blocks but the wake is broken, the read never returns and
 *      the test hangs -- also a FAIL, visible as a stuck prompt.
 *
 *   2. A write by the child WAKES the parent.  The child sleeps
 *      briefly (to give the parent time to actually block), then
 *      writes a known string and exits.  The parent's read()
 *      returns that string.  This proves the write-side wake
 *      (pipe_wake_waiter on reader_waiting) fires and delivers.
 *
 *   3. A write on a full pipe BLOCKS and is woken by the reader.
 *      The parent writes PIPE_CAPACITY bytes to a fresh pipe
 *      (fills it), then writes one more byte in a child?  No --
 *      simpler: the parent itself fills the pipe, then a forked
 *      child reads one byte, which frees space and wakes the
 *      parent's blocked write.  Verifies the read-side wake.
 *
 * Only one direction is tested per step, to keep the failure mode
 * unambiguous: if (1)/(2) fail, the reader wake is broken; if
 * (3) fails, the writer wake is broken.  Do not fold them.
 *
 * Build: picked up by userland/musl/Makefile's tests/*.c wildcard.
 * Stage: 05_boot_kernel64/Makefile must copy build/pipe_step2.elf
 *        to PIPE_STEP2.ELF (add to USERLAND_ELFS and the mcopy
 *        chain, like pipe_step1).
 * Run:   from ash, `pipe_step2`.  Do NOT run from donix>; the
 *        shell's job control interacts badly with a forked test.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

/* Must match PIPE_DEFAULT_CAPACITY in user_syscall.c. */
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

/* ---- Test 1 & 2: reader blocks, writer wakes it. ---- */

static void test_reader_blocks_writer_wakes(void) {
    printf("test 1/2: reader blocks, child write wakes it\n");

    int fds[2];
    if (pipe(fds) != 0) { check(0, "pipe() for test 1"); return; }

    pid_t child = fork();
    if (child < 0) { check(0, "fork() for test 1"); return; }

    if (child == 0) {
        /* Child: wait a moment so the parent has time to block,
         * then write and exit. */
        close(fds[0]);   /* child doesn't read */
        for (volatile int i = 0; i < 5000000; i++) { /* busy wait */ }
        const char* msg = "woken";
        ssize_t n = write(fds[1], msg, 5);
        if (n != 5) {
            /* Can't use check() cleanly in a forked child because
             * it would race the parent's stdout; write directly. */
            write(2, "child: write failed\n", 20);
            _exit(1);
        }
        close(fds[1]);
        _exit(0);
    }

    /* Parent: read the empty pipe.  This blocks (Step 2).  When
     * the child writes, it wakes us. */
    close(fds[1]);   /* parent doesn't write */
    char buf[16];
    memset(buf, 0, sizeof(buf));
    errno = 0;
    ssize_t nr = read(fds[0], buf, 5);

    check(nr == 5, "read() returned 5 bytes after blocking");
    check(memcmp(buf, "woken", 5) == 0, "read() returned the child's bytes");
    check(errno != EAGAIN, "read() did not return EAGAIN (proves it blocked)");

    int status = 0;
    waitpid(child, &status, 0);

    close(fds[0]);
    printf("test 1/2: done\n");
}

/* ---- Test 3: writer blocks on full, reader wakes it. ---- */

static void test_writer_blocks_reader_wakes(void) {
    printf("test 3: writer blocks on full pipe, child read wakes it\n");

    int fds[2];
    if (pipe(fds) != 0) { check(0, "pipe() for test 3"); return; }

    /* Parent fills the pipe completely, before forking. */
    static char fill[PIPE_CAPACITY];
    memset(fill, 'F', sizeof(fill));
    ssize_t nf = write(fds[1], fill, sizeof(fill));
    check(nf == PIPE_CAPACITY, "parent filled the pipe");

    pid_t child = fork();
    if (child < 0) { check(0, "fork() for test 3"); return; }

    if (child == 0) {
        /* Child: read one byte.  This frees space and wakes the
         * parent's blocked write.  Then read the rest and exit. */
        close(fds[1]);
        char c;
        ssize_t n = read(fds[0], &c, 1);
        if (n != 1) {
            write(2, "child: read failed\n", 19);
            _exit(1);
        }
        /* Drain the rest so the parent's blocked write succeeds
         * and the pipe does not stay full. */
        static char drain[PIPE_CAPACITY];
        size_t got = 1;
        while (got < PIPE_CAPACITY) {
            ssize_t g = read(fds[0], drain, sizeof(drain));
            if (g <= 0) break;
            got += (size_t)g;
        }
        _exit(0);
    }

    /* Parent: write one more byte.  The pipe is full, so this
     * blocks (Step 2) until the child reads. */
    errno = 0;
    ssize_t nw = write(fds[1], "Z", 1);
    check(nw == 1, "parent's write() to a full pipe completed after blocking");
    check(errno != EAGAIN, "parent's write() did not return EAGAIN (proves it blocked)");

    close(fds[1]);
    int status = 0;
    waitpid(child, &status, 0);
    close(fds[0]);
    printf("test 3: done\n");
}

int main(void) {
    printf("pipe_step2: starting\n");

    test_reader_blocks_writer_wakes();
    test_writer_blocks_reader_wakes();

    if (failures == 0) {
        printf("STEP2 OK\n");
        return 0;
    }
    printf("STEP2 FAILED: %d check(s) failed\n", failures);
    return 1;
}
