/*
 * pipe7e_stdio_sjlj.c -- pipe7e_stdio with a setjmp/longjmp in the
 * loop.  A probe for open-issues item 7e, the control-transfer
 * family.
 *
 * The evidence this probe follows: session 58 captured a #GP in
 * busybox ash whose faulting RIP was a user STACK address
 * (0x80000FB700), with the return address from `call __setjmp`
 * (redirectsafe+0x2f) sitting right there on the stack.  A `ret`
 * popped a stack address.  That points at the jmp_buf path.
 *
 * pipe7e_stdio already covers the x=$(cmd) syscall shape with
 * FILE * I/O and a SIGSEGV reporter, and passes 20000 iterations.
 * This test adds the one thing ash does that pipe7e_stdio does
 * not: a jmp_buf save/restore inside the loop.
 *
 * Shape:
 *   - setjmp is called once per iteration, BEFORE the fork, so a
 *     longjmp cannot orphan a child or leak a pipe fd.
 *   - the longjmp is taken on the iteration chosen by
 *     LONGJMP_EVERY, and lands back at that same setjmp.  The
 *     iteration then continues normally.
 *   - every `break` in the loop is still structurally inside the
 *     loop, so a real failure still breaks and still reports.
 *   - the handler is sigsegv_probe's: one-shot, prints rsp/rbp
 *     and a stack window, then exit_group.  It does NOT return --
 *     there is no rt_sigreturn.  The longjmp is a deliberate
 *     in-process jump, not a recovery from the signal.
 *
 * Read-only on the disk.  Not a canary row; run by hand.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <setjmp.h>
#include <sys/wait.h>

#define ITERS        20000
#define MARKER_EVERY 500

/* Take the longjmp once every N iterations.  1 = every iteration,
 * which puts the jmp_buf save/restore on the hot path, where the
 * #GP lead says the corruption is.  Set higher to sample. */
#define LONGJMP_EVERY 1

static jmp_buf jb;

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

/* The reporter.  Installed for SIGSEGV.  Raw write only -- the
 * process's FILE state at fault time is not to be trusted. */
static void handler(long sig) {
    uint64_t rsp, rbp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));

    hputs("\n=== PIPE7E-STDIO-SJLJ: SIGSEGV HANDLER ===\n");
    hputs("  sig  "); hputhex((uint64_t)sig);
    hputs("  rsp  "); hputhex(rsp);
    hputs("  rbp  "); hputhex(rbp);
    hputs("  stack window from rsp:\n");
    uint64_t *p = (uint64_t *)rsp;
    for (int i = 0; i < 16; i++) {
        hputhex(p[i]);
    }
    hputs("=== PIPE7E-STDIO-SJLJ: handler done, exiting ===\n");

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
    printf("=== pipe7e_stdio_sjlj ===\n");
    printf("iters=%d marker_every=%d longjmp_every=%d\n",
           ITERS, MARKER_EVERY, LONGJMP_EVERY);
    fflush(stdout);

    if (install_handler() != 0) {
        printf("PIPE7E-STDIO-SJLJ: rt_sigaction failed\n");
        fflush(stdout);
        return 2;
    }
    printf("PIPE7E-STDIO-SJLJ: SIGSEGV handler installed\n");
    fflush(stdout);

    {
        struct ksigaction sc;
        sc.handler = (uint64_t)handler;
        sc.flags   = 0;
        sc3(13, 17, (long)&sc, 0);   /* SIGCHLD = 17 */
        printf("PIPE7E-STDIO-SJLJ: SIGCHLD handler installed\n");
        fflush(stdout);
    }

    char* const argv[] = { (char*)"/usr/bin/PIPE7E_HELPER", NULL };
    char* const envp[] = { NULL };

    for (int i = 0; i < ITERS; i++) {
        if (i % MARKER_EVERY == 0) {
            fprintf(stdout, "[pipe7e_stdio_sjlj] iter %d\n", i);
            fflush(stdout);
        }

        /*
         * The jmp_buf save/restore, before the fork so a longjmp
         * cannot orphan a child.  setjmp returns 0 on the direct
         * call and nonzero on the longjmp return; on the nonzero
         * path we fall through and run the iteration.
         */
        if (setjmp(jb) == 0) {
            if (LONGJMP_EVERY > 0 && (i % LONGJMP_EVERY) == 0) {
                longjmp(jb, 1);
            }
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
            if (dup2(p[1], 1) < 0) _exit(2);
            close(p[0]);
            close(p[1]);
            execve("/usr/bin/PIPE7E_HELPER", argv, envp);
            _exit(3);
        }

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
        fclose(rf);

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
        printf("ok   %d iterations of pipe/fork/dup2/execve/fdopen/fread/wait4 with setjmp/longjmp\n",
               ITERS);
        printf("PIPE7E-STDIO-SJLJ-ALL-PASS\n");
        return 0;
    }
    printf("PIPE7E-STDIO-SJLJ: %d failed\n", fails);
    return 1;
}
