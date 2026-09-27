/*
 * musl_getcwd — exercise getcwd(2) (syscall 79).
 *
 * busybox ash calls getcwd at startup and uses the result as $PWD.
 * The kernel returns "/" for any (buf, size) with size >= 2.
 *
 * Checks:
 *   1. getcwd(buf, sizeof buf) returns buf, and buf contains "/".
 *   2. getcwd(buf, 1) returns -ERANGE (34).
 *   3. getcwd(NULL, 0) returns -EINVAL (22).  (GNU extension not
 *      supported.)
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

static long raw_getcwd(char* buf, unsigned long size) {
    long r;
    __asm__ volatile("syscall" : "=a"(r)
                     : "a"(79L), "D"(buf), "S"(size)
                     : "rcx", "r11", "memory");
    return r;
}

int main(void) {
    char buf[64];

    /* Check 1: getcwd(buf, 64) returns buf and buf[0] == '/'. */
    long r = raw_getcwd(buf, sizeof(buf));
    if (r != (long)(unsigned long)buf) {
        puts_raw("GETCWD-RET-FAIL\n", 16);
        return 1;
    }
    if (buf[0] != '/' || buf[1] != '\0') {
        puts_raw("GETCWD-CONTENT-FAIL\n", 20);
        return 1;
    }

    /* Check 2: getcwd(buf, 1) -> -ERANGE (-34). */
    r = raw_getcwd(buf, 1);
    if (r != -34) {
        puts_raw("GETCWD-ERANGE-FAIL\n", 19);
        return 1;
    }

    /* Check 3: getcwd(NULL, 0) -> -EINVAL (-22). */
    r = raw_getcwd((char*)0, 0);
    if (r != -22) {
        puts_raw("GETCWD-EINVAL-FAIL\n", 19);
        return 1;
    }

    puts_raw("GETCWD-OK /\n", 12);
    return 0;
}
