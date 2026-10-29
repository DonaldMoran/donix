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

static void handler(long sig) {
    (void)sig;
    puts_raw("PIPE-WRITE-PROBE: handler ran\n");
}
static void restorer(void) {
    __asm__ volatile("mov $15, %%eax; syscall" ::: "memory");
}

struct ksigaction {
    uint64_t handler; uint64_t flags; uint64_t restorer; uint64_t mask[2];
};
struct timespec { long tv_sec; long tv_nsec; };

#define SIGUSR1 10
#define SIGKILL 9

int main(void) {
    puts_raw("PIPE-WRITE-PROBE: installing SIGUSR1 handler\n");

    struct ksigaction sa;
    sa.handler  = (uint64_t)handler;
    sa.flags    = 0;
    sa.restorer = (uint64_t)restorer;
    sa.mask[0]  = 0;
    sa.mask[1]  = 0;

    if (sc3(13, SIGUSR1, (long)&sa, 0) != 0) {
        puts_raw("PIPE-WRITE-PROBE: rt_sigaction failed\n");
        sc1(60, 1);
    }

    int fds[2];
    if (sc1(22, (long)fds) != 0) {
        puts_raw("PIPE-WRITE-PROBE: pipe failed\n");
        sc1(60, 1);
    }

    char buf[4096];
    for (int i=0; i<4096; i++) buf[i] = 'A';
    
    // Fill the pipe. The capacity is 4096, so this write fits exactly.
    long r0 = sc3(1, fds[1], (long)buf, 4096);
    if (r0 != 4096) {
        puts_raw("PIPE-WRITE-PROBE: initial fill failed, returned ");
        putdec(r0);
        puts_raw("\n");
        sc1(60, 1);
    }

    long parent = sc1(39, 0);
    long sig_pid = sc1(57, 0);
    if (sig_pid < 0) {
        puts_raw("PIPE-WRITE-PROBE: fork (signaler) failed\n");
        sc1(60, 1);
    }
    if (sig_pid == 0) {
        struct timespec ts = { 0, 200000000L };
        sc2(35, (long)&ts, 0);
        sc2(62, parent, SIGUSR1);
        sc1(60, 0);
        for (;;) { }
    }

    puts_raw("PIPE-WRITE-PROBE: blocking in write\n");
    // This should block, because the pipe is full.
    long r = sc3(1, fds[1], (long)buf, 1);

    puts_raw("PIPE-WRITE-PROBE: write returned ");
    putdec(r);
    puts_raw("\n");

    int pass = (r == -4);

    puts_raw("PIPE-WRITE-PROBE: cleaning up\n");
    sc2(62, sig_pid, SIGKILL);
    sc3(61, sig_pid, 0, 0);

    if (pass) {
        puts_raw("PIPE-WRITE-PROBE: PASS\n");
        sc1(60, 0);
    }
    puts_raw("PIPE-WRITE-PROBE: FAIL\n");
    sc1(60, 1);
}
