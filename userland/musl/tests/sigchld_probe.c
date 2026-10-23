/*
 * sigchld_probe.c -- slice 6 acceptance + wait4 diagnostic.
 *
 * Slice 6's shape: install a SIGCHLD handler (real restorer),
 * fork, child exits immediately, parent does one syscall
 * (getpid) and the handler must run on the way out of it, then
 * the parent wait4s and reaps.
 *
 * This build adds diagnostics: on the "wait4 failed" path the
 * raw wait4 return value, the child pid it was waiting for, the
 * status word's low bytes, and a second wait4 attempt are all
 * printed.  The 3b52bf3 run printed "wait4 failed" with no
 * value; this tells us what it returned -- -ECHILD (already
 * reaped), -EFAULT (bad copy), 0 (WNOHANG, shouldn't happen), or
 * something else.
 *
 * Raw syscalls, no libc.
 */
#include <unistd.h>
#include <stdint.h>

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
static void puts_raw(const char *s) {
    long len = 0;
    while (s[len]) len++;
    sc3(1, 1, (long)s, len);
}
static void puthex(uint64_t v) {
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        int nib = (v >> ((15 - i) * 4)) & 0xF;
        buf[2 + i] = nib < 10 ? '0' + nib : 'a' + nib - 10;
    }
    buf[18] = ' ';
    sc3(1, 1, (long)buf, 19);
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

static void handler(long sig) {
    (void)sig;
    puts_raw("SIGCHLD-PROBE: handler ran\n");
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

int main(void) {
    puts_raw("SIGCHLD-PROBE: installing handler\n");

    struct ksigaction sa;
    sa.handler  = (uint64_t)handler;
    sa.flags    = 0;
    sa.restorer = (uint64_t)restorer;
    sa.mask[0]  = 0;
    sa.mask[1]  = 0;

    if (sc3(13, 17 /* SIGCHLD */, (long)&sa, 0) != 0) {
        puts_raw("SIGCHLD-PROBE: rt_sigaction failed\n");
        sc1(60, 1);
    }

    long pid = sc1(57, 0);
    if (pid < 0) {
        puts_raw("SIGCHLD-PROBE: fork failed\n");
        sc1(60, 1);
    }
    if (pid == 0) {
        sc1(60, 0);
        for (;;) { }
    }

    /* One syscall; handler runs on the way out of getpid if the
     * child's process_exit raised SIGCHLD. */
    sc1(39, 0);   /* getpid */
    puts_raw("SIGCHLD-PROBE: returned from getpid\n");

    /* ---- first wait4 ---- */
    int status = 0;
    long reaped = sc3(61, pid, (long)&status, 0);
    puts_raw("SIGCHLD-PROBE: wait4 returned ");
    puthex((uint64_t)reaped);
    puts_raw("= ");
    putdec(reaped);
    puts_raw(" (child pid = ");
    putdec(pid);
    puts_raw("), status=0x");
    puthex((uint64_t)(uint32_t)status);
    puts_raw("\n");

    if (reaped == pid) {
        puts_raw("SIGCHLD-PROBE: reaped\n");
        sc1(60, 0);
    }

    /* ---- diagnostic: second wait4 ---- */
    puts_raw("SIGCHLD-PROBE: wait4 failed; retrying\n");
    int status2 = 0;
    long reaped2 = sc3(61, pid, (long)&status2, 0);
    puts_raw("SIGCHLD-PROBE: second wait4 returned ");
    puthex((uint64_t)reaped2);
    puts_raw("= ");
    putdec(reaped2);
    puts_raw(", status=0x");
    puthex((uint64_t)(uint32_t)status2);
    puts_raw("\n");

    sc1(60, 1);
}
