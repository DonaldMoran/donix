/*
 * pipe7e_sjlj_stale.c -- reproduce ash's missing exception-handler
 * restore, in two phases.
 *
 * The mechanism, read from busybox_unstripped this session:
 *
 *   - `evaltree` (0x41C403) does
 *         lea  0x38(%rsp),%rax
 *         mov  %rax,0x38(%rbx)      ; globals->exception_handler = &its own jmp_buf
 *     -- it stores a pointer to a jmp_buf INSIDE ITS OWN STACK FRAME
 *     into the global exception handler.
 *
 *   - `evaltree`'s NORMAL return path (0x41C43C..0x41C45F) does NOT
 *     restore globals+0x38.  Neither does `popstackmark`
 *     (0x415A86), `dotrap` (0x41C782), nor `int_on` (0x41561F).
 *
 *   - `raise_exception` (0x415521) does
 *         mov  0x38(%rax),%rdi
 *         call _longjmp             ; no liveness check
 *
 *   - `_longjmp` (0x43A53D) does
 *         mov  0x30(%rdi),%rsp      ; restore %rsp from the target jmp_buf
 *         jmp  *0x38(%rdi)          ; jump to the saved rip
 *
 * So after an `evaltree` returns normally, the global handler still
 * points into its RETURNED frame.  A later `raise_exception` jumps
 * there, `_longjmp` restores a STALE %rsp from the dead frame, and
 * the first `ret` after the resumed setjmp-return pops whatever now
 * occupies that slot -- in the capture, 0x1, the _longjmp return
 * value.
 *
 * PHASE 1 mirrors ash exactly: inner function setjmps into a
 * stack-local jmp_buf, stores &that into a global, returns without
 * restoring.  Then longjmp to the global.  Whether this faults
 * depends on what occupies the stack at the restored %rsp -- it is
 * a real bug either way, and this phase REPORTS rather than
 * asserting.
 *
 * PHASE 2 forces a stale %rsp: same shape, but a stack-clobbering
 * call overwrites the region the inner frame occupied before the
 * longjmp.  The restored %rsp then reads the garbage, and the ret
 * after the resumed setjmp pops it.
 *
 * The sigsegv_probe reporter is installed, so a fault prints the
 * process's own state instead of dying silently.
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

/* ---- raw syscall helpers, for the reporter ---- */

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

/* The reporter.  One-shot; prints rsp/rbp and a stack window, then
 * exit_group.  Raw write only. */
static void handler(long sig) {
    uint64_t rsp, rbp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));

    hputs("\n=== PIPE7E-SJLJ-STALE: SIGSEGV HANDLER ===\n");
    hputs("  sig  "); hputhex((uint64_t)sig);
    hputs("  rsp  "); hputhex(rsp);
    hputs("  rbp  "); hputhex(rbp);
    hputs("  stack window from rsp:\n");
    uint64_t *p = (uint64_t *)rsp;
    for (int i = 0; i < 16; i++) {
        hputhex(p[i]);
    }
    hputs("=== PIPE7E-SJLJ-STALE: handler done, exiting ===\n");

    sc1(231, 0);
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
    return (int)sc3(13, 11, (long)&sa, 0);
}

/* The global handler pointer -- ash's globals->exception_handler at
 * globals+0x38.  It is a POINTER, so it can point into a frame that
 * has gone away -- which is the whole bug. */
static jmp_buf *g_handler = NULL;

/* ---- Phase 1: mirror ash ---- */

/*
 * The inner function.  It setjmps into a STACK-LOCAL jmp_buf and
 * stores &that into the global, then RETURNS WITHOUT RESTORING the
 * global.  This is evaltree at 0x41C403 with no restore on the
 * normal return path.
 */
static int inner_mirror(void) {
    jmp_buf local;               /* lives in inner_mirror's frame */
    g_handler = &local;          /* -> the global */
    if (setjmp(local) == 0) {
        /* Direct call: return normally, WITHOUT restoring g_handler.
         * This leaves g_handler pointing into this returned frame. */
        return 0;
    }
    /* longjmp return path -- in ash, this is where the restore
     * happens.  Reached only if the longjmp targets THIS frame. */
    hputs("[phase1] longjmp returned into inner_mirror's frame\n");
    return 1;
}

/* raise_exception: longjmp to whatever the global points at, no
 * liveness check. */
static void raise_one(void) {
    hputs("[raise] longjmp to global handler\n");
    longjmp(*g_handler, 1);
}

/* ---- Phase 2: force a stale %rsp ---- */

/* A function that dirties a chunk of stack and returns.  Called
 * AFTER the inner frame has returned, so the region the inner
 * jmp_buf occupied is overwritten with a known pattern. */
__attribute__((noinline))
static void clobber(void) {
    volatile uint64_t buf[64];
    for (int i = 0; i < 64; i++) {
        buf[i] = 0xDEADBEEF00000000ULL | (uint64_t)i;
    }
    /* prevent the compiler removing the writes */
    __asm__ volatile("" : : "r"(buf) : "memory");
}

static int inner_forced(void) {
    jmp_buf local;
    g_handler = &local;
    if (setjmp(local) == 0) {
        return 0;
    }
    hputs("[phase2] longjmp returned into inner_forced's frame\n");
    return 1;
}

int main(void) {
    printf("=== pipe7e_sjlj_stale ===\n");
    fflush(stdout);

    if (install_handler() != 0) {
        printf("PIPE7E-SJLJ-STALE: rt_sigaction failed\n");
        fflush(stdout);
        return 2;
    }
    printf("PIPE7E-SJLJ-STALE: SIGSEGV handler installed\n");
    fflush(stdout);

    /* ---------------- Phase 1 ---------------- */
    printf("PIPE7E-SJLJ-STALE: phase1 begin (mirror ash, no restore)\n");
    fflush(stdout);

    /* The outer frame sets the global to its OWN buffer first, the
     * way a prior handler would be installed.  Then inner_mirror
     * overwrites it with &local and does not restore it. */
    jmp_buf outer;
    if (setjmp(outer) == 0) {
        g_handler = &outer;
        printf("[phase1] outer setjmp done; calling inner_mirror\n");
        fflush(stdout);
        int r = inner_mirror();
        printf("[phase1] inner_mirror returned %d; g_handler points into a returned frame\n", r);
        printf("[phase1] now raising -- longjmp to the stale handler\n");
        fflush(stdout);
        raise_one();                 /* -> longjmp into inner_mirror's dead frame */
        printf("[phase1] UNREACHED: raise_one returned\n");
        fflush(stdout);
    } else {
        printf("[phase1] outer longjmp-return: the longjmp came back to OUTER\n");
        fflush(stdout);
    }
    printf("[phase1] complete without fault -- stale rsp landed in valid stack\n");
    fflush(stdout);

    /* ---------------- Phase 2 ---------------- */
    printf("PIPE7E-SJLJ-STALE: phase2 begin (force stale rsp)\n");
    fflush(stdout);

    jmp_buf outer2;
    if (setjmp(outer2) == 0) {
        g_handler = &outer2;
        printf("[phase2] outer setjmp done; calling inner_forced\n");
        fflush(stdout);
        int r = inner_forced();
        printf("[phase2] inner_forced returned %d; clobbering the stack it used\n", r);
        fflush(stdout);
        clobber();                   /* overwrite inner_forced's dead frame */
        printf("[phase2] now raising -- longjmp to the stale, clobbered handler\n");
        fflush(stdout);
        raise_one();
        printf("[phase2] UNREACHED: raise_one returned\n");
        fflush(stdout);
    } else {
        printf("[phase2] outer2 longjmp-return: the longjmp came back to OUTER2\n");
        fflush(stdout);
    }
    printf("[phase2] complete without fault\n");
    fflush(stdout);

    printf("PIPE7E-SJLJ-STALE: DONE (neither phase faulted)\n");
    return 0;
}
