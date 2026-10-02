/*
 * musl_exec2 -- assert that sys_execve's bare-name shim is GONE.
 *
 * HISTORY.  This file used to test the opposite: sys_execve used
 * to try three spellings of a path when the raw one failed -- as
 * given, then (if it had no ':') uppercased + ".ELF" at the FAT
 * root, then the same under /BIN.  This file exercised those
 * fallbacks with execve("HELLO"), execve("/HELLO.ELF"), and
 * execve("echo").
 *
 * All three of those forms are dead now, and this file asserts
 * that.  Session 42 (tag 20261001-noshim) removed the bare-name
 * attempt from sys_execve entirely, and the earlier commit
 * 20261001-nosuffix moved the donix-native binaries from the FAT
 * root and /BIN into /usr/bin and dropped the ".ELF" suffix.  A
 * bare name no longer resolves, and nothing is named "*.ELF"
 * any more.
 *
 * WHY KEEP THE FILE INSTEAD OF DELETING IT.  A test that asserts
 * the shim is gone is more valuable than no test: if a future
 * session reintroduces bare-name resolution (or stages a binary
 * with an .ELF suffix), checks 2 and 3 below go red.  Deleting
 * the file would let that happen silently.
 *
 * The checks:
 *
 *   1. execve("/usr/bin/HELLO", NULL, NULL) -- the form callers
 *      use now.  Leading slash, staged bare under /usr/bin.
 *      sys_execve attempt (a) tries the path literally, which
 *      FatFs rejects (leading '/'), then attempt (b) rewrites it
 *      to "0:/usr/bin/HELLO" and opens it.  HELLO runs and exits
 *      0.  POSITIVE: the parent expects exit 0.
 *
 *   2. execve("HELLO", NULL, NULL) -- bare name, no slash.  This
 *      is the form the old shim resolved.  It must FAIL now:
 *      attempt (a) f_open("HELLO") looks at the FAT root, where
 *      nothing is named HELLO; attempt (b) requires a leading
 *      '/', so it is skipped.  -ENOENT.  NEGATIVE: the parent
 *      expects a nonzero child exit.
 *
 *   3. execve("/HELLO.ELF", NULL, NULL) -- the old suffixed
 *      form.  Attempt (b) rewrites it to "0:/HELLO.ELF"; nothing
 *      is named that (the suffix is gone and the binary moved to
 *      /usr/bin/HELLO).  Must FAIL.  NEGATIVE: nonzero exit.
 *
 * Checks 2 and 3 do NOT check which errno came back.  The only
 * channel from child to parent is the exit code, and plumbing
 * "-ret" through it would add code for a distinction the test
 * does not care about: the question is "did the name resolve at
 * all", not "did it fail for exactly the right reason".  The
 * child exits 1 on any execve failure; the parent checks "not
 * zero".
 *
 * The syscalls are issued as raw inline asm, matching
 * musl_exec.c.  Every syscall asm here declares %rax as an
 * output ("=a"(ret)) -- see the ASM CONSTRAINT NOTE in
 * envp_step1.c and the docs/gotchas.md entry: an input-only
 * syscall asm block lies to the compiler about %rax, and two
 * such blocks back to back issue the second `syscall` with the
 * first's return value as its number.
 *
 * puts_raw takes ONLY the string and computes its length; see
 * the STRING LENGTH NOTE in envp_step1.c.  Do not add a length
 * parameter back.
 */
#include <unistd.h>

static void puts_raw(const char* s) {
    unsigned long n = 0;
    while (s[n]) n++;

    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(1L), "D"(1L), "S"(s), "d"(n)
                     : "rcx", "r11", "memory");
    (void)ret;
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
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(60L), "D"((long)code)
                     : "rcx", "r11", "memory");
    (void)ret;
    __builtin_unreachable();
}

/*
 * Fork and execve `path`.  Returns the child's exit code
 * (0..255), or -1 if fork or wait failed.
 *
 * The child execve's with a NULL argv and envp (this test does
 * not care what the program sees, only whether it loads and
 * exits 0).  If execve returns, it failed: the child prints a
 * diagnostic and exits 1.
 */
static int run_exec(const char* path) {
    long pid = raw_fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        raw_execve(path, (char**)0, (char**)0);

        /* execve only returns on failure. */
        puts_raw("MUSL_EXEC2-CHILD-EXEC-FAIL\n");
        raw_exit(1);
    }

    int status = -1;
    long reaped = raw_wait4(pid, &status, 0);
    if (reaped != pid) return -1;

    return (status >> 8) & 0xff;
}

int main(void) {
    /* --- 1. POSITIVE: the new path form resolves --- */
    {
        int code = run_exec("/usr/bin/HELLO");
        if (code == 0) {
            puts_raw("ok 1: /usr/bin/HELLO resolves and exits 0\n");
        } else if (code < 0) {
            puts_raw("FAIL 1: fork or wait failed\n");
            return 1;
        } else {
            puts_raw("FAIL 1: /usr/bin/HELLO did not exit 0\n");
            return 1;
        }
    }

    /* --- 2. NEGATIVE: a bare name must NOT resolve --- */
    {
        int code = run_exec("HELLO");
        if (code == 0) {
            puts_raw("FAIL 2: bare HELLO resolved (shim is back?)\n");
            return 1;
        } else if (code < 0) {
            puts_raw("FAIL 2: fork or wait failed\n");
            return 1;
        } else {
            puts_raw("ok 2: bare HELLO does not resolve\n");
        }
    }

    /* --- 3. NEGATIVE: the old .ELF suffix form must NOT resolve --- */
    {
        int code = run_exec("/HELLO.ELF");
        if (code == 0) {
            puts_raw("FAIL 3: /HELLO.ELF resolved (.ELF is back?)\n");
            return 1;
        } else if (code < 0) {
            puts_raw("FAIL 3: fork or wait failed\n");
            return 1;
        } else {
            puts_raw("ok 3: /HELLO.ELF does not resolve\n");
        }
    }

    puts_raw("MUSL_EXEC2-ALL-PASS\n");
    raw_exit(0);
}
