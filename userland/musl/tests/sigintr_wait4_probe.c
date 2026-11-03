/*
 * sigintr_wait4_probe.c -- signals slice 6, item (a) acceptance.
 *
 * The gap: a process blocked in wait4 was woken by sys_kill (which
 * sets sig_pending and marks it READY under cli), but sys_wait4's
 * loop re-blocked without consulting sig_pending -- so the handler
 * did not run until the wait completed.  Unix: a deliverable signal
 * interrupts the wait with -EINTR, the handler runs, and the
 * caller sees -EINTR when rt_sigreturn returns.
 *
 * The shape:
 *   - install a SIGUSR1 handler (real restorer, like sigchld_probe)
 *   - fork a signaler that nanosleeps briefly, then kills the parent
 *   - fork a sleeper that loops forever (it will never become a
 *     zombie, so wait4 genuinely blocks -- no reap to mask the test)
 *   - parent wait4(sleeper): must return -4 (EINTR), handler ran
 *   - cleanup: SIGKILL both children, reap both, print PASS
 *
 * Pre-fix this hangs (the sleeper never exits).  Post-fix it
 * returns -4 and the handler fires.
 *
 * Raw syscalls, no libc.
 */
#include <stdint.h>

static long sc1(long n, long a) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a)
                     : "rcx", "r11", "memory");
    return r;
}
static long sc2(long n, long a, long b) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b)
                     : "rcx", "r11", "memory");
    return r;
}
static long sc3(long n, long a, long b, long c) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return r;
}
static void puts_raw(const char *s) {
    long len = 0;
    while (s[len]) len++;
    sc3(1, 1, (long)s, len);
}
static void putdec(long v) {
    char buf[24]; int n = 0; int neg = 0;
    unsigned long u = (unsigned long)v;
    if (v < 0) { neg = 1; u = (unsigned long)(-v); }
    if (u == 0) buf[n++] = '0';
    else { char tmp[24]; int t = 0;
           while (u > 0) { tmp[t++] = '0' + (u % 10); u /= 10; }
           while (t > 0) buf[n++] = tmp[--t]; }
    if (neg) { for (int i = n; i > 0; i--) buf[i] = buf[i-1]; buf[0] = '-'; n++; }
    sc3(1, 1, (long)buf, n);
}

static volatile long g_handler_ran = 0;

static void usr1_handler(long sig) {
    (void)sig;
    g_handler_ran = 1;
    puts_raw("SIGINTR-PROBE: handler ran\n");
}

static void restorer(void) {
    __asm__ volatile("mov $15, %%eax; syscall" ::: "memory");
}

struct ksigaction {
    uint64_t handler;
    uint64_t flags;
    uint64_t restorer;
    uint64_t mask[2];
};

struct timespec { long tv_sec; long tv_nsec; };

#define SIGUSR1 10
#define SIGKILL 9

int main(void) {
    puts_raw("SIGINTR-PROBE: installing SIGUSR1 handler\n");

    struct ksigaction sa;
    sa.handler  = (uint64_t)usr1_handler;
    sa.flags    = 0;
    sa.restorer = (uint64_t)restorer;
    sa.mask[0]  = 0;
    sa.mask[1]  = 0;

    if (sc3(13, SIGUSR1, (long)&sa, 0) != 0) {
        puts_raw("SIGINTR-PROBE: rt_sigaction failed\n");
        sc1(60, 1);
    }

    long parent = sc1(39, 0);

    /* Signaler child: sleep ~200ms, then SIGUSR1 the parent. */
    long sig_pid = sc1(57, 0);
    if (sig_pid < 0) {
        puts_raw("SIGINTR-PROBE: fork (signaler) failed\n");
        sc1(60, 1);
    }
    if (sig_pid == 0) {
        struct timespec ts = { 0, 200000000L };  /* 200 ms */
        sc2(35, (long)&ts, 0);
        sc2(62, parent, SIGUSR1);
        sc1(60, 0);
        for (;;) { }
    }

    /* Sleeper child: never exits, so wait4 genuinely blocks. */
    long sleep_pid = sc1(57, 0);
    if (sleep_pid < 0) {
        puts_raw("SIGINTR-PROBE: fork (sleeper) failed\n");
        sc1(60, 1);
    }
    if (sleep_pid == 0) {
        for (;;) { }
    }

    puts_raw("SIGINTR-PROBE: signaler=");
    putdec(sig_pid);
    puts_raw(" sleeper=");
    putdec(sleep_pid);
    puts_raw("\nSIGINTR-PROBE: blocking in wait4(sleeper)\n");

    int status = 0;
    long r = sc3(61, sleep_pid, (long)&status, 0);

    puts_raw("SIGINTR-PROBE: wait4 returned ");
    putdec(r);
    puts_raw("\n");

    int pass = 1;
    if (r != -4) {
        puts_raw("SIGINTR-PROBE: FAIL (expected -4 EINTR)\n");
        pass = 0;
    }
    if (!g_handler_ran) {
        puts_raw("SIGINTR-PROBE: FAIL (handler did not run)\n");
        pass = 0;
    }

    puts_raw("SIGINTR-PROBE: cleaning up\n");
    sc2(62, sleep_pid, SIGKILL);
    sc2(62, sig_pid,   SIGKILL);
    sc3(61, sleep_pid, (long)&status, 0);
    sc3(61, sig_pid,   (long)&status, 0);

    if (pass) {
        puts_raw("SIGINTR-PROBE: PASS\n");
        sc1(60, 0);
    }
    puts_raw("SIGINTR-PROBE: FAIL\n");
    sc1(60, 1);
}
