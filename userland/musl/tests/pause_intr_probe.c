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
    puts_raw("PAUSE-PROBE: handler ran\n");
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

int main(void) {
    puts_raw("PAUSE-PROBE: installing SIGUSR1 handler\n");

    struct ksigaction sa;
    sa.handler  = (uint64_t)handler;
    sa.flags    = 0;
    sa.restorer = (uint64_t)restorer;
    sa.mask[0]  = 0;
    sa.mask[1]  = 0;

    if (sc3(13, SIGUSR1, (long)&sa, 0) != 0) {
        puts_raw("PAUSE-PROBE: rt_sigaction failed\n");
        sc1(60, 1);
    }

    long parent = sc1(39, 0);
    long sig_pid = sc1(57, 0);
    if (sig_pid < 0) {
        puts_raw("PAUSE-PROBE: fork (signaler) failed\n");
        sc1(60, 1);
    }
    if (sig_pid == 0) {
        puts_raw("PAUSE-PROBE: [child] sleeping 200ms\n");
        struct timespec ts = { 0, 200000000L };
        sc2(35, (long)&ts, 0);
        puts_raw("PAUSE-PROBE: [child] sending signal\n");
        sc2(62, parent, SIGUSR1);
        sc1(60, 0);
        for (;;) { }
    }

    puts_raw("PAUSE-PROBE: blocking in pause\n");
    long r = sc1(34, 0);

    puts_raw("PAUSE-PROBE: pause returned ");
    putdec(r);
    puts_raw("\n");

    int pass = (r == -4);

    puts_raw("PAUSE-PROBE: cleaning up\n");
    sc2(62, sig_pid, SIGKILL);
    sc3(61, sig_pid, 0, 0);

    if (pass) {
        puts_raw("PAUSE-PROBE: PASS\n");
        sc1(60, 0);
    }
    puts_raw("PAUSE-PROBE: FAIL\n");
    sc1(60, 1);
}
