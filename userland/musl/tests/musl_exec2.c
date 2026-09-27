/*
 * musl_exec2 — exercise sys_execve's bare-name retry.
 *
 * busybox sh calls execve("ls", ...) with a bare name.  musl_sh
 * normalizes to "0:/NAME.ELF" itself; busybox sh does not.  The
 * kernel's sys_execve now retries with "0:/NAME.ELF" (uppercased)
 * when the raw path fails to open and has no ':' in it.
 *
 * Checks:
 *   1. fork(); child execve("HELLO", NULL, NULL) — bare name, no
 *      prefix, no suffix.  The kernel should resolve this to
 *      "0:/HELLO.ELF" and run hello.  Parent waits, expects exit 0.
 *   2. fork(); child execve("/HELLO.ELF", NULL, NULL) — leading
 *      slash, .ELF already appended.  Kernel should strip to
 *      "HELLO.ELF" and resolve the same way.
 *   3. fork(); child execve("echo", argv, NULL) where argv is
 *      {"echo", "hi", NULL} — lowercase bare name.  Kernel should
 *      uppercase and append .ELF.
 *
 * Prints "EXEC2-OK" if all three succeed.
 */
#include <unistd.h>

static void puts_raw(const char* s, unsigned long n) {
    __asm__ volatile("syscall"
                     :
                     : "a"(1L), "D"(1L), "S"(s), "d"(n)
                     : "rcx", "r11", "memory");
}

static long raw_fork(void) {
    long r;
    __asm__ volatile("syscall" : "=a"(r)
                     : "a"(57L), "D"(0L), "S"(0L), "d"(0L)
                     : "rcx", "r11", "memory");
    return r;
}

static long raw_wait4(long pid, int* status, int options) {
    long r;
    __asm__ volatile("syscall" : "=a"(r), "+m"(*status)
                     : "a"(61L), "D"(pid), "S"(status), "d"((long)options)
                     : "rcx", "r11", "memory");
    return r;
}

static long raw_execve(const char* path, char** argv, char** envp) {
    long r;
    __asm__ volatile("syscall" : "=a"(r)
                     : "a"(59L), "D"(path), "S"(argv), "d"(envp)
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

int main(void) {
    int status = -1;
    long pid, reaped;

    /* --- 1. bare "HELLO" --- */
    pid = raw_fork();
    if (pid == 0) {
        raw_execve("HELLO", (char**)0, (char**)0);
        /* execve only returns on failure. */
        puts_raw("EXEC2-CHILD1-FAIL\n", 18);
        raw_exit(1);
    }
    status = -1;
    reaped = raw_wait4(pid, &status, 0);
    if (reaped != pid || (status & 0xff00) != 0) {
        puts_raw("EXEC2-1-FAIL\n", 12);
        return 1;
    }

    /* --- 2. leading slash, .ELF already there: "/HELLO.ELF" --- */
    pid = raw_fork();
    if (pid == 0) {
        raw_execve("/HELLO.ELF", (char**)0, (char**)0);
        puts_raw("EXEC2-CHILD2-FAIL\n", 18);
        raw_exit(1);
    }
    status = -1;
    reaped = raw_wait4(pid, &status, 0);
    if (reaped != pid || (status & 0xff00) != 0) {
        puts_raw("EXEC2-2-FAIL\n", 12);
        return 1;
    }

    /* --- 3. lowercase bare name with argv: "echo" "hi" --- */
    pid = raw_fork();
    if (pid == 0) {
        char* argv[3];
        argv[0] = "echo";
        argv[1] = "hi";
        argv[2] = (char*)0;
        raw_execve("echo", argv, (char**)0);
        puts_raw("EXEC2-CHILD3-FAIL\n", 18);
        raw_exit(1);
    }
    status = -1;
    reaped = raw_wait4(pid, &status, 0);
    if (reaped != pid || (status & 0xff00) != 0) {
        puts_raw("EXEC2-3-FAIL\n", 12);
        return 1;
    }

    puts_raw("EXEC2-OK\n", 9);
    return 0;
}
