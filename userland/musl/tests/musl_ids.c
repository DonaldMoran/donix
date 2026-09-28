/*
 * musl_ids — exercise setsid(2) and getppid(2).
 *
 * Both are called by busybox ash at startup and currently log
 * "Unknown syscall: 107" and "Unknown syscall: 110".
 *
 * Checks:
 *   1. setsid() returns a positive number (the calling pid).
 *   2. getppid() returns a non-negative number.
 *   3. setsid() returns the same value on a second call (it is a
 *      no-op that returns the pid both times; the pid does not
 *      change between calls).
 */
#include <unistd.h>

static void puts_raw(const char* s, unsigned long n) {
    __asm__ volatile("syscall"
                     :
                     : "a"(1L), "D"(1L), "S"(s), "d"(n)
                     : "rcx", "r11", "memory");
}

static void put_dec(long v) {
    char b[24]; int n = 0;
    if (v < 0) { b[n++] = '-'; v = -v; }
    char tmp[24]; int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v > 0) { tmp[t++] = '0' + (v % 10); v /= 10; }
    while (t > 0) b[n++] = tmp[--t];
    b[n++] = '\n';
    puts_raw(b, n);
}

static long raw_setsid(void) {
    long r;
    __asm__ volatile("syscall" : "=a"(r)
                     : "a"(107L)
                     : "rcx", "r11", "memory");
    return r;
}

static long raw_getppid(void) {
    long r;
    __asm__ volatile("syscall" : "=a"(r)
                     : "a"(110L)
                     : "rcx", "r11", "memory");
    return r;
}

int main(void) {
    long sid1 = raw_setsid();
    if (sid1 <= 0) { puts_raw("IDS-SETSID-FAIL\n", 16); return 1; }

    long ppid = raw_getppid();
    if (ppid < 0) { puts_raw("IDS-GETPPID-FAIL\n", 17); return 1; }

    long sid2 = raw_setsid();
    if (sid2 != sid1) {
        puts_raw("IDS-SETSID-NOT-IDEMPOTENT\n", 25);
        return 1;
    }

    puts_raw("IDS-OK sid=", 12); put_dec(sid1);
    puts_raw("IDS-OK ppid=", 13); put_dec(ppid);
    return 0;
}
