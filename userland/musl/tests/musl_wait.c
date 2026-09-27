#include <unistd.h>
#include <sys/wait.h>

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

static long raw_fork(void) {
    long r;
    __asm__ volatile("syscall" : "=a"(r)
                     : "a"(57L), "D"(0L), "S"(0L), "d"(0L)
                     : "rcx", "r11", "memory");
    return r;
}

static void raw_exit(int code) {
    __asm__ volatile("syscall"
                     :
                     : "a"(60L), "D"((long)code)
                     : "rcx", "r11", "memory");
    __builtin_unreachable();
}

static long raw_wait4(long pid, int* status, int options) {
    long r;
    __asm__ volatile("syscall" : "=a"(r), "+m"(*status)
                     : "a"(61L), "D"(pid), "S"(status), "d"((long)options)
                     : "rcx", "r11", "memory");
    return r;
}

int main(void) {
    /* ---- Part 1: exit status propagation ---- */
    long pid = raw_fork();
    if (pid == 0) {
        puts_raw("WAIT-CHILD-42\n", 14);
        raw_exit(42);
    }

    int status = -1;
    long reaped = raw_wait4(pid, &status, 0);
    if (reaped != pid) {
        puts_raw("WAIT-REAP-FAIL\n", 15);
        return 1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 42) {
        puts_raw("WAIT-STATUS-FAIL raw=", 22);
        put_dec(status);
        return 1;
    }
    puts_raw("WAIT-STATUS-OK ", 15);
    put_dec(WEXITSTATUS(status));

    /* ---- Part 2: WNOHANG on a running child returns 0 ---- */
    pid = raw_fork();
    if (pid == 0) {
        for (volatile long i = 0; i < 100000; i++) {
            puts_raw(".", 1);
        }
        raw_exit(0);
    }

    status = -1;
    long wn = raw_wait4(pid, &status, WNOHANG);
    if (wn != 0) {
        puts_raw("WNOHANG-FAIL ret=", 17);
        put_dec(wn);
    } else {
        puts_raw("WAIT-WNOHANG-OK\n", 16);
    }
    reaped = raw_wait4(pid, &status, 0);
    if (reaped != pid) {
        puts_raw("WNOHANG-REAP-FAIL\n", 18);
        return 1;
    }

    /* ---- Part 3: wait4(-1, ...) reaps any child ---- */
    long p1 = raw_fork();
    if (p1 == 0) { raw_exit(11); }

    long p2 = raw_fork();
    if (p2 == 0) { raw_exit(22); }

    status = -1;
    long got1 = raw_wait4(-1, &status, 0);
    long r1 = status;
    long s1 = WEXITSTATUS(status);

    status = -1;
    long got2 = raw_wait4(-1, &status, 0);
    long r2 = status;
    long s2 = WEXITSTATUS(status);

    if (got1 <= 0 || got2 <= 0 || got1 == got2) {
        puts_raw("WAIT-ANY-FAIL\n", 14);
        return 1;
    }
    puts_raw("WAIT-ANY-1 got=", 15); put_dec(got1);
    puts_raw("WAIT-ANY-1 r=",   13); put_dec(r1);
    puts_raw("WAIT-ANY-1 s=",   13); put_dec(s1);
    puts_raw("WAIT-ANY-2 got=", 15); put_dec(got2);
    puts_raw("WAIT-ANY-2 r=",   13); put_dec(r2);
    puts_raw("WAIT-ANY-2 s=",   13); put_dec(s2);

    puts_raw("WAIT-ALL-OK\n", 12);
    return 0;
}
