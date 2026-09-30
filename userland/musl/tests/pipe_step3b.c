/*
 * pipe_step3b.c -- test for the process-exit wake path.
 *
 * Step 3 added EOF: a reader on an empty pipe whose writer closed
 * returns 0.  But Step 3's EOF depends on sys_close waking the
 * reader, and sys_close is not the only way a writer goes away.
 *
 * When a process exits, process_exit -> close_all_files ->
 * put_file_slot decrements writers_open, but (before this step)
 * put_file_slot woke nobody.  So a reader blocked on a writer
 * that EXITED -- as opposed to one that called close() -- would
 * stay in hlt until the next keyboard IRQ woke it via
 * process_wake_all_blocked.  On a headless system, or in a
 * pipeline where the user is not typing, that is a hang.
 *
 * This test proves the fix: the writer child writes nothing,
 * then calls _exit(0) WITHOUT closing its write fd.  The
 * parent's read() must return 0 (EOF) on the child's exit
 * alone, with no keystroke, no close, and nothing else to wake
 * it.
 *
 * If the fix is absent, this test HANGS -- the parent sleeps in
 * hlt, the child is gone, and the only thing that could wake
 * the parent is a keypress, which the automated test does not
 * provide.  A hang here is the failure mode; there is no error
 * message for it.  If you see the test stop after "waiting for
 * child to exit", the wake is missing.
 *
 * Build: picked up by userland/musl/Makefile's tests/*.c wildcard.
 * Stage: 05_boot_kernel64/Makefile must copy build/pipe_step3b.elf
 *        to PIPE_STEP3B.ELF (USERLAND_ELFS + mcopy chain).
 * Run:   from ash, `pipe_step3b`.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

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
    printf("pipe_step3b: starting\n");

    int fds[2];
    if (pipe(fds) != 0) {
        printf("  FAIL - pipe() failed\n");
        return 1;
    }

    pid_t child = fork();
    if (child < 0) {
        printf("  FAIL - fork() failed\n");
        return 1;
    }

    if (child == 0) {
        /*
         * Child: hold the write end open for a moment so the
         * parent has time to block, then EXIT WITHOUT CLOSING.
         *
         * The key point: no close(fds[1]).  _exit() runs the
         * child's exit path, which closes all fds via
         * close_all_files -> put_file_slot, and that is the path
         * under test.  An explicit close(fds[1]) first would
         * exercise sys_close instead, which is Step 3's already-
         * proven path, and would not test this fix.
         */
        close(fds[0]);
        for (volatile int i = 0; i < 5000000; i++) { /* let parent block */ }
        _exit(0);
        /* not reached */
    }

    close(fds[1]);   /* parent doesn't write */

    printf("  .. waiting for child to exit (reader blocked in hlt)\n");

    char buf[16];
    errno = 0;
    ssize_t nr = read(fds[0], buf, sizeof(buf));

    check(nr == 0, "read() returned 0 (EOF) after writer EXITED without closing");
    check(errno != EAGAIN, "read() did not return EAGAIN");

    int status = 0;
    waitpid(child, &status, 0);
    close(fds[0]);

    if (failures == 0) {
        printf("STEP3B OK\n");
        return 0;
    }
    printf("STEP3B FAILED: %d check(s) failed\n", failures);
    return 1;
}
