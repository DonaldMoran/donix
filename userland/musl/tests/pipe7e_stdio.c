/*
 * pipe7e_stdio.c -- the pipe7e shape, with stdio and a SIGSEGV
 * reporter.  A probe for open-issues item 7e, the control-transfer
 * family.
 *
 * pipe7e does the x=$(cmd) syscall shape with RAW fd I/O and no
 * signals, and passes 100000 iterations without a fault.  The 7e
 * faults fire in busybox ash, which does its I/O through FILE *
 * and installs SIGCHLD handlers.  The last sysret rcx values in
 * every 7e capture are musl text addresses in the stdio/lock
 * machinery (__stdio_write, __lockfile/__unlock).  So the
 * untested combination is: the same syscall shape, but with the
 * loop's I/O going through FILE *, and with a fault reporter
 * installed so that a fault prints the process's own state.
 *
 * This program is that combination.
 *
 *   - The handler is sigsegv_probe's: one-shot.  It prints rsp,
 *     rbp, and a stack window, then calls exit_group.  It does
 *     NOT return.  It is not item 12 -- see open-issues.md.
 *   - The loop reads the child's pipe through a FILE * opened
 *     with fdopen, and writes its own markers with fprintf /
 *     fflush, so every iteration goes through __stdio_write and
 *     the FILE lock.
 *   - The child's exit status is checked, same as pipe7e.
 *   - The marker prints BEFORE the iteration runs, per session
 *     51's gotcha.
 *
 * If a fault fires here, it is not ash-specific in its trigger,
 * and the handler prints the faulting process's own registers and
 * stack -- the thing kernel-side captures cannot give.
 *
 * Read-only on the disk.  Not a canary row; run by hand.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/wait.h>

#define ITERS 20000
#define MARKER_EVERY 500

static int fails = 0;

/* ---- raw syscall helpers, for the handler ---- */

static long sc1(long n, long a) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a)
                     : "rcx", "r11", "memory");
    return r;
}
static long sc3(long n, long a, long b, long c) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return r;
}

static void hputs(const char *s) {
    long len = 0;
    while (s[len]) len++;
    sc3(1, 1, (long)s, len);
}
static void hputhex(uint64_t v) {
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        int nib = (v >> ((15 - i) * 4)) & 0xF;
        buf[2 + i] = nib < 10 ? '0' + nib : 'a' + nib - 10;
    }
    buf[18] = '\n';
    sc3(1, 1, (long)buf, 19);
}

/*
 * The reporter.  Installed for SIGSEGV.  Prints its own rsp, rbp
 * and a stack window, then exit_group.  It must not return --
 * there is no rt_sigreturn.  No stdio here: the process's FILE
 * state at fault time is not to be trusted, so raw write only.
 */
static void handler(long sig) {
    uint64_t rsp, rbp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));

    hputs("\n=== PIPE7E-STDIO: SIGSEGV HANDLER ===\n");
    hputs("  sig  "); hputhex((uint64_t)sig);
    hputs("  rsp  "); hputhex(rsp);
    hputs("  rbp  "); hputhex(rbp);
    hputs("  stack window from rsp:\n");
    uint64_t *p = (uint64_t *)rsp;
    for (int i = 0; i < 16; i++) {
        hputhex(p[i]);
    }
    hputs("=== PIPE7E-STDIO: handler done, exiting ===\n");

    sc1(231, 0);   /* exit_group */
    for (;;) { }
}

struct ksigaction {
    uint64_t handler;
    uint64_t flags;
};

static int install_handler(void) {
    struct ksigaction sa;
    sa.handler = (uint64_t)handler;
    sa.flags   = 0;
    /* rt_sigaction(13): sig, act, oldact, sigsetsize */
    return (int)sc3(13, 11, (long)&sa, 0);
}

int main(void) {
    printf("=== pipe7e_stdio ===\n");
    printf("iters=%d marker_every=%d\n", ITERS, MARKER_EVERY);
    fflush(stdout);

    if (install_handler() != 0) {
        printf("PIPE7E-STDIO: rt_sigaction failed\n");
        fflush(stdout);
        return 2;
    }
    printf("PIPE7E-STDIO: SIGSEGV handler installed\n");
    fflush(stdout);

    /*
     * Also install a SIGCHLD handler, the way ash does.
     *
     * ash installs one and expects it to run when a child exits.
     * On this kernel the signal is never delivered -- item 12 is
     * a stub -- so this only sets the disposition; the handler
     * never runs.  The question this probe asks is whether the 7e
     * trigger depends on the process *having* a non-default
     * SIGCHLD disposition, not on the handler actually running.
     *
     * The same reporter is used for both signals: if a SIGCHLD
     * were ever delivered, printing and exiting is the right
     * behavior for a probe.
     */
    {
        struct ksigaction sc;
        sc.handler = (uint64_t)handler;
        sc.flags   = 0;
        sc3(13, 17, (long)&sc, 0);   /* SIGCHLD = 17 */
        printf("PIPE7E-STDIO: SIGCHLD handler installed\n");
        fflush(stdout);
    }

    char* const argv[] = { (char*)"/usr/bin/PIPE7E_HELPER", NULL };
    char* const envp[] = { NULL };

    for (int i = 0; i < ITERS; i++) {
        if (i % MARKER_EVERY == 0) {
            /* Through FILE *, so this goes via __stdio_write. */
            fprintf(stdout, "[pipe7e_stdio] iter %d\n", i);
            fflush(stdout);
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
            /* Child: wire the write end to stdout, then exec. */
            if (dup2(p[1], 1) < 0) _exit(2);
            close(p[0]);
            close(p[1]);
            execve("/usr/bin/PIPE7E_HELPER", argv, envp);
            _exit(3);
        }

        /* Parent: close the write end.  Read the child's output
         * through a FILE * -- this is the difference from
         * pipe7e.c, and the whole point of this probe. */
        close(p[1]);

        FILE *rf = fdopen(p[0], "r");
        if (!rf) {
            printf("FAIL iter %d: fdopen failed: %s\n", i, strerror(errno));
            fails++;
            close(p[0]);
            break;
        }

        char buf[64];
        long total = 0;
        for (;;) {
            size_t n = fread(buf, 1, sizeof buf, rf);
            total += (long)n;
            if (n < sizeof buf) {
                if (ferror(rf)) {
                    printf("FAIL iter %d: fread error\n", i);
                    fails++;
                }
                break;
            }
        }
        fclose(rf);   /* also closes p[0] */

        if (fails) break;

        int status = 0;
        errno = 0;
        pid_t w = wait4(pid, &status, 0, NULL);
        if (w < 0) {
            printf("FAIL iter %d: wait4 failed: %s\n", i, strerror(errno));
            fails++;
            break;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            printf("FAIL iter %d: child status=0x%x (exited=%d code=%d sig=%d)\n",
                   i, status, WIFEXITED(status),
                   WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                   WIFSIGNALED(status) ? WTERMSIG(status) : 0);
            fails++;
            break;
        }
        if (total == 0) {
            printf("FAIL iter %d: child wrote nothing\n", i);
            fails++;
            break;
        }
    }

    if (fails == 0) {
        printf("ok   %d iterations of pipe/fork/dup2/execve/fdopen/fread/wait4\n",
               ITERS);
        printf("PIPE7E-STDIO-ALL-PASS\n");
        return 0;
    }
    printf("PIPE7E-STDIO: %d failed\n", fails);
    return 1;
}
