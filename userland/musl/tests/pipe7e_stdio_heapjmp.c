/*
 * pipe7e_stdio_heapjmp.c -- pipe7e_stdio with a heap-allocated
 * jmp_buf and a longjmp taken from a nested callee.  A probe for
 * open-issues item 7e, the control-transfer family.
 *
 * pipe7e_stdio_sjlj falsified the setjmp/longjmp INSTRUCTION PATH
 * in a first-party program: 20000 iterations with a setjmp and a
 * longjmp per iteration, and no fault.  What that probe did NOT
 * reproduce is ash's STRUCTURE around the jmp_buf:
 *
 *   - ash's redirectsafe/evaltree use a `struct jmploc` that lives
 *     in HEAP memory, not a static in main's frame.
 *   - the longjmp is taken from DEEPER in the call chain -- out of
 *     a subshell or a nested evaltree -- not from the frame that
 *     called setjmp.
 *
 * This probe moves both of those one step toward ash:
 *
 *   - the jmp_buf is inside a malloc'd `struct sjlj_ctx`, the way
 *     struct jmploc is.
 *   - setjmp is called in a NESTED function (run_one_iteration),
 *     and the longjmp is taken from a DEEPER callee
 *     (trigger_longjmp), so the jump unwinds UNWIND_DEPTH frames.
 *
 * Everything else is pipe7e_stdio's shape, unchanged: the x=$(cmd)
 * syscall chain, FILE * I/O through fdopen/fread/fclose, the
 * child's exit status checked, and sigsegv_probe's one-shot
 * SIGSEGV reporter installed.
 *
 * The setjmp/longjmp block runs BEFORE the fork, so a longjmp can
 * neither orphan a child nor leak a pipe fd.  Every `break` in the
 * loop is still structurally inside the loop, so a real failure
 * still breaks and still reports.
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

/* How many nested frames the longjmp unwinds.  2 is the minimum
 * that unwinds at all: run_one_iteration -> trigger_longjmp.  ash
 * goes deeper and variably; raise this to push the probe closer
 * to ash's shape. */
#define UNWIND_DEPTH 2

/* Take the longjmp once every N iterations.  1 = every iteration,
 * so the heap jmp_buf save/restore and the unwind are on the hot
 * path, where the #GP lead says the corruption is. */
#define LONGJMP_EVERY 1

static int fails = 0;

/* The heap-resident jmp_buf, the way ash's struct jmploc is. */
struct sjlj_ctx {
    jmp_buf jb;
};

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

    hputs("\n=== PIPE7E-STDIO-HEAPJMP: SIGSEGV HANDLER ===\n");
    hputs("  sig  "); hputhex((uint64_t)sig);
    hputs("  rsp  "); hputhex(rsp);
    hputs("  rbp  "); hputhex(rbp);
    hputs("  stack window from rsp:\n");
    uint64_t *p = (uint64_t *)rsp;
    for (int i = 0; i < 16; i++) {
        hputhex(p[i]);
    }
    hputs("=== PIPE7E-STDIO-HEAPJMP: handler done, exiting ===\n");

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

/*
 * The longjmp is taken HERE, UNWIND_DEPTH-1 frames below the
 * setjmp in run_one_iteration.  This is the piece pipe7e_stdio_sjlj
 * did not have: the jump does not originate in the frame that
 * called setjmp.
 */
static void trigger_longjmp(struct sjlj_ctx *ctx, int depth) {
    if (depth > 1) {
        trigger_longjmp(ctx, depth - 1);
        /* not reached: the recursion's deepest frame longjmps */
        return;
    }
    longjmp(ctx->jb, 1);
}

/*
 * One iteration.  The setjmp lives here, in a nested function, so
 * the frame it saves is a callee's, not main's.  On the direct
 * call setjmp returns 0 and we take the longjmp; on the longjmp
 * return it returns nonzero and we fall through to run the
 * iteration's real work.
 */
static int run_one_iteration(struct sjlj_ctx *ctx, int i) {
    if (setjmp(ctx->jb) == 0) {
        if (LONGJMP_EVERY > 0 && (i % LONGJMP_EVERY) == 0) {
            trigger_longjmp(ctx, UNWIND_DEPTH - 1);
        }
    }

    char* const argv[] = { (char*)"/usr/bin/PIPE7E_HELPER", NULL };
    char* const envp[] = { NULL };

    int p[2];
    errno = 0;
    if (pipe(p) != 0) {
        printf("FAIL iter %d: pipe failed: %s\n", i, strerror(errno));
        fails++;
        return -1;
    }

    errno = 0;
    pid_t pid = fork();
    if (pid < 0) {
        printf("FAIL iter %d: fork failed: %s\n", i, strerror(errno));
        fails++;
        return -1;
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
        return -1;
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

    if (fails) return -1;

    int status = 0;
    errno = 0;
    pid_t w = wait4(pid, &status, 0, NULL);
    if (w < 0) {
        printf("FAIL iter %d: wait4 failed: %s\n", i, strerror(errno));
        fails++;
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        printf("FAIL iter %d: child status=0x%x (exited=%d code=%d sig=%d)\n",
               i, status, WIFEXITED(status),
               WIFEXITED(status) ? WEXITSTATUS(status) : -1,
               WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        fails++;
        return -1;
    }
    if (total == 0) {
        printf("FAIL iter %d: child wrote nothing\n", i);
        fails++;
        return -1;
    }
    return 0;
}

int main(void) {
    printf("=== pipe7e_stdio_heapjmp ===\n");
    printf("iters=%d marker_every=%d longjmp_every=%d unwind_depth=%d\n",
           ITERS, MARKER_EVERY, LONGJMP_EVERY, UNWIND_DEPTH);
    fflush(stdout);

    if (install_handler() != 0) {
        printf("PIPE7E-STDIO-HEAPJMP: rt_sigaction failed\n");
        fflush(stdout);
        return 2;
    }
    printf("PIPE7E-STDIO-HEAPJMP: SIGSEGV handler installed\n");
    fflush(stdout);

    {
        struct ksigaction sc;
        sc.handler = (uint64_t)handler;
        sc.flags   = 0;
        sc3(13, 17, (long)&sc, 0);   /* SIGCHLD = 17 */
        printf("PIPE7E-STDIO-HEAPJMP: SIGCHLD handler installed\n");
        fflush(stdout);
    }

    /* The jmp_buf lives on the heap, the way ash's struct jmploc
     * does -- not in main's frame. */
    struct sjlj_ctx *ctx = malloc(sizeof *ctx);
    if (!ctx) {
        printf("PIPE7E-STDIO-HEAPJMP: malloc failed\n");
        fflush(stdout);
        return 2;
    }

    for (int i = 0; i < ITERS; i++) {
        if (i % MARKER_EVERY == 0) {
            fprintf(stdout, "[pipe7e_stdio_heapjmp] iter %d\n", i);
            fflush(stdout);
        }
        if (run_one_iteration(ctx, i) != 0) {
            break;
        }
    }

    free(ctx);

    if (fails == 0) {
        printf("ok   %d iterations of pipe/fork/dup2/execve/fdopen/fread/wait4 with heap jmp_buf and nested longjmp\n",
               ITERS);
        printf("PIPE7E-STDIO-HEAPJMP-ALL-PASS\n");
        return 0;
    }
    printf("PIPE7E-STDIO-HEAPJMP: %d failed\n", fails);
    return 1;
}
