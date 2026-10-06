/*
 * sigsegv_probe.c -- catch our own SIGSEGV, print state, exit.
 *
 * Debug instrument for item 7e.  Not a POSIX signal test: it
 * uses raw syscalls and the kernel's minimal one-shot signal
 * redirect, so that it works without musl's sigaction machinery.
 *
 * The handler does NOT return.  If the kernel-side redirect is
 * missing, the process is killed at the NULL deref and the shell
 * prints "Segmentation fault" instead of the handler's output.
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
    buf[18] = '\n';
    sc3(1, 1, (long)buf, 19);
}

static void handler(long sig) {
    uint64_t rsp, rbp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));

    puts_raw("SIGSEGV-PROBE: handler running, sig=");
    puthex((uint64_t)sig);
    puts_raw("  rsp  "); puthex(rsp);
    puts_raw("  rbp  "); puthex(rbp);
    puts_raw("  stack window from rsp:\n");
    uint64_t *p = (uint64_t *)rsp;
    for (int i = 0; i < 12; i++) {
        puthex(p[i]);
    }
    puts_raw("SIGSEGV-PROBE: handler done\n");

    sc1(231, 0);
    for (;;) { }
}

struct ksigaction {
    uint64_t handler;
    uint64_t flags;
};

int main(void) {
    puts_raw("SIGSEGV-PROBE: installing handler\n");

    struct ksigaction sa;
    sa.handler = (uint64_t)handler;
    sa.flags   = 0;

    if (sc3(13, 11, (long)&sa, 0) != 0) {
        puts_raw("SIGSEGV-PROBE: rt_sigaction failed\n");
        sc1(60, 1);
    }

    puts_raw("SIGSEGV-PROBE: dereferencing NULL\n");

    volatile uint64_t *null = (uint64_t *)0;
    (void)*null;

    puts_raw("SIGSEGV-PROBE: NULL deref returned?! FAIL\n");
    sc1(60, 1);
}
