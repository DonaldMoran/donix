/*
 * kill_self.c -- install a handler for SIGUSR1, raise it with
 * kill(getpid(), SIGUSR1), and exit.
 *
 * Slice 1 of docs/signal-design.md.  The acceptance check is
 * external: after this runs, process_dump_all must show
 * sig_raise=1 sig_deliver=0.  sig_raise=1 says kill() set the
 * pending bit; sig_deliver=0 says nothing observed it on the way
 * out to user mode, which is correct -- slice 1 has no delivery.
 *
 * The handler is installed so this probe's shape matches
 * sigsegv_probe.c; slice 1 does not run it.  The probe prints
 * markers so a missing raise is distinguishable from a missing
 * counter.
 *
 * Not a POSIX signal test: raw syscalls, the same sc1/sc3 helpers
 * sigsegv_probe.c uses.
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
    puts_raw("KILL-SELF: handler running, sig=");
    puthex((uint64_t)sig);
    sc1(231, 0);
    for (;;) { }
}

struct ksigaction {
    uint64_t handler;
    uint64_t flags;
};

#define SIGUSR1 10

int main(void) {
    puts_raw("KILL-SELF: installing handler\n");

    struct ksigaction sa;
    sa.handler = (uint64_t)handler;
    sa.flags   = 0;

    long r = sc3(13, SIGUSR1, (long)&sa, 0);
    if (r != 0) {
        puts_raw("KILL-SELF: rt_sigaction failed: ");
        puthex((uint64_t)r);
        sc1(60, 1);
    }

    long pid = sc1(39, 0);   /* getpid */
    puts_raw("KILL-SELF: kill(getpid(), SIGUSR1), pid=");
    puthex((uint64_t)pid);

    r = sc3(62, pid, SIGUSR1, 0);
    if (r != 0) {
        puts_raw("KILL-SELF: kill failed: ");
        puthex((uint64_t)r);
        sc1(60, 1);
    }

    puts_raw("KILL-SELF: kill returned 0; slice 1 has no delivery, so we continue\n");
    puts_raw("KILL-SELF: done\n");
    sc1(60, 0);
}
