/*
 * sigsuspend_probe.c -- signals slice 6, rt_sigsuspend acceptance.
 *
 * The distinction from pause: the parent BLOCKS SIGUSR1 via
 * rt_sigprocmask, forks a signaler child that sends it, then
 * calls rt_sigsuspend with an empty mask.  The mask swap is
 * atomic with the wait, so the signal is delivered inside the
 * call.  On return the parent's mask must be restored to the
 * original (SIGUSR1 blocked) and the handler must have run.
 *
 * A naive "unblock then pause" would have a window where the
 * child's signal arrives while SIGUSR1 is still blocked and is
 * only observed at the next syscall boundary, not inside the
 * wait.  sigsuspend closes that window.
 */
#include <stdint.h>

static long sc1(long n, long a) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a) : "rcx", "r11", "memory");
    return r;
}
static long sc2(long n, long a, long b) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b) : "rcx", "r11", "memory");
    return r;
}
static long sc3(long n, long a, long b, long c) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
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

static void handler(long sig) {
    (void)sig;
    g_handler_ran = 1;
    puts_raw("SIGSUSPEND-PROBE: handler ran\n");
}
static void restorer(void) {
    __asm__ volatile("mov $15, %%eax; syscall" ::: "memory");
}

struct ksigaction {
    uint64_t handler; uint64_t flags; uint64_t restorer; uint64_t mask[2];
};
struct timespec { int64_t tv_sec; int64_t tv_nsec; };

#define SIGUSR1 10
#define SIGKILL 9
#define SIG_BLOCK 0

int main(void) {
    puts_raw("SIGSUSPEND-PROBE: installing SIGUSR1 handler\n");

    struct ksigaction sa;
    sa.handler  = (uint64_t)handler;
    sa.flags    = 0;
    sa.restorer = (uint64_t)restorer;
    sa.mask[0]  = 0;
    sa.mask[1]  = 0;

    if (sc3(13, SIGUSR1, (long)&sa, 0) != 0) {
        puts_raw("SIGSUSPEND-PROBE: rt_sigaction failed\n");
        sc1(60, 1);
    }

    /*
     * Block SIGUSR1.  The bit for signal 10 is 1 << 10, minus
     * one because Linux delivers signal N via bit (N-1).  So
     * 1ULL << 9.
     */
    uint64_t block_sigusr1 = 1ULL << 9;
    long bret = sc3(14, SIG_BLOCK, (long)&block_sigusr1, 0);
    if (bret != 0) {
        puts_raw("SIGSUSPEND-PROBE: rt_sigprocmask BLOCK failed\n");
        sc1(60, 1);
    }
    puts_raw("SIGSUSPEND-PROBE: SIGUSR1 blocked\n");

    long parent = sc1(39, 0);
    long sig_pid = sc1(57, 0);
    if (sig_pid < 0) {
        puts_raw("SIGSUSPEND-PROBE: fork failed\n");
        sc1(60, 1);
    }
    if (sig_pid == 0) {
        puts_raw("SIGSUSPEND-PROBE: [child] sleeping 200ms\n");
        struct timespec ts = { 0, 200000000L };
        sc2(35, (long)&ts, 0);
        puts_raw("SIGSUSPEND-PROBE: [child] sending signal\n");
        sc2(62, parent, SIGUSR1);
        sc1(60, 0);
        for (;;) { }
    }

    puts_raw("SIGSUSPEND-PROBE: calling sigsuspend with empty mask\n");
    uint64_t empty_mask = 0;
    long r = sc3(130, (long)&empty_mask, 8, 0);

    puts_raw("SIGSUSPEND-PROBE: sigsuspend returned ");
    putdec(r);
    puts_raw("\n");

    int pass = (r == -4) && g_handler_ran;

    /* Check the mask was restored: SIGUSR1 should be blocked again. */
    uint64_t cur = 0;
    long gret = sc3(14, 0, 0, (long)&cur);   /* how=0 (BLOCK), set=NULL, oldset=cur */
    if (gret == 0) {
        puts_raw("SIGSUSPEND-PROBE: current mask bit for SIGUSR1 = ");
        putdec((long)((cur & block_sigusr1) != 0));
        puts_raw("\n");
        if ((cur & block_sigusr1) == 0) {
            puts_raw("SIGSUSPEND-PROBE: FAIL (mask not restored)\n");
            pass = 0;
        }
    }

    puts_raw("SIGSUSPEND-PROBE: cleaning up\n");
    sc2(62, sig_pid, SIGKILL);
    sc3(61, sig_pid, 0, 0);

    if (pass) {
        puts_raw("SIGSUSPEND-PROBE: PASS\n");
        sc1(60, 0);
    }
    puts_raw("SIGSUSPEND-PROBE: FAIL\n");
    sc1(60, 1);
}
