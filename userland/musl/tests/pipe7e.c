/*
 * pipe7e.c -- reproducer probe for open-issues item 7e, the
 * control-flow-to-data family that fires in fork-heavy workloads.
 *
 * The bug: a busybox process makes a correct syscall, sysret
 * returns cleanly, and then the process transfers control to a
 * data value.  Session 54 closed 7e as a busybox bug, but the
 * evidence is indirect: every capture has last sysret rcx in
 * busybox .text, and every crash fires on a busybox ash that is
 * running test.sh.  No capture has ever shown the same crash in
 * a program that is NOT busybox ash.
 *
 * This program is the x=$(cmd) shape -- pipe, fork, dup2 the
 * write end to stdout, execve a helper, read the pipe to EOF,
 * wait4 -- translated to C, with no shell in the loop.  It is the
 * same shape pipe_wake_probe.sh exercises in ash, but the process
 * that would fault is this program, not ash.
 *
 * If 7e fires here, it is NOT a busybox bug: it is in the kernel,
 * in musl, or in the libc+kernel interaction.  If it does not
 * fire here, that is strong evidence the fault is ash-specific
 * bookkeeping, and the chase moves into ash.
 *
 * The iteration marker is printed BEFORE the iteration runs, per
 * session 51's gotcha ("a test that cannot say where it stopped
 * cannot say much -- print the row marker before the row runs").
 *
 * The child's exit status IS checked.  A faulted child must not
 * be silently absorbed; if wait4 reports a non-zero exit or a
 * signal kill, this program prints it and stops.
 *
 * Read-only on the disk.  Not a canary row; run by hand.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define ITERS 50000
#define MARKER_EVERY 1000

static int fails = 0;

int main(void) {
    printf("=== pipe7e ===\n");
    printf("iters=%d marker_every=%d\n", ITERS, MARKER_EVERY);

    char* const argv[] = { (char*)"/usr/bin/PIPE7E_HELPER", NULL };
    char* const envp[] = { NULL };

    for (int i = 0; i < ITERS; i++) {
        if (i % MARKER_EVERY == 0) {
            printf("[pipe7e] iter %d\n", i);
        }

        int p[2];
        errno = 0;
        if (pipe(p) != 0) {
            printf("FAIL iter %d: pipe failed: %s\n", i, strerror(errno));
            fails++;
            break;
        }

        errno = 0;
        pid_t pid = fork();
        if (pid < 0) {
            printf("FAIL iter %d: fork failed: %s\n", i, strerror(errno));
            fails++;
            break;
        }

        if (pid == 0) {
            /* Child.  Wire the write end to stdout, then exec. */
            if (dup2(p[1], 1) < 0) {
                _exit(2);
            }
            close(p[0]);
            close(p[1]);
            execve("/usr/bin/PIPE7E_HELPER", argv, envp);
            /* execve must not return. */
            _exit(3);
        }

        /* Parent.  Close the write end, then read to EOF. */
        close(p[1]);

        char buf[64];
        ssize_t total = 0;
        for (;;) {
            errno = 0;
            ssize_t n = read(p[0], buf, sizeof buf);
            if (n < 0) {
                printf("FAIL iter %d: read failed: %s\n",
                       i, strerror(errno));
                fails++;
                break;
            }
            if (n == 0) {
                break;
            }
            total += n;
        }
        close(p[0]);

        if (fails) {
            break;
        }

        int status = 0;
        errno = 0;
        pid_t w = wait4(pid, &status, 0, NULL);
        if (w < 0) {
            printf("FAIL iter %d: wait4 failed: %s\n",
                   i, strerror(errno));
            fails++;
            break;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            printf("FAIL iter %d: child status=0x%x (exited=%d code=%d "
                   "sig=%d)\n",
                   i, status, WIFEXITED(status),
                   WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                   WIFSIGNALED(status) ? WTERMSIG(status) : 0);
            fails++;
            break;
        }
        if (total == 0) {
            printf("FAIL iter %d: child wrote nothing (read returned 0 "
                   "immediately)\n", i);
            fails++;
            break;
        }
    }

    if (fails == 0) {
        printf("ok   %d iterations of pipe/fork/dup2/execve/read/wait4\n",
               ITERS);
        printf("PIPE7E-ALL-PASS\n");
        return 0;
    }
    printf("PIPE7E: %d failed\n", fails);
    return 1;
}
