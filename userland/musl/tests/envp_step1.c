/*
 * envp_step1 -- regression test for sys_execve passing envp
 * through to the new program (session 42, tag 20261001-envp).
 *
 * Before session 42, sys_execve ignored its third argument and
 * wrote a single NULL as the envp terminator, so every exec'd
 * program ran with an empty environment: getenv() returned NULL
 * for everything, `busybox env` printed nothing, and $VAR
 * expansion in ash was always empty.
 *
 * This test forks three times and execve's envp_helper with three
 * different environments:
 *
 *   1. {"DONIX_ENVP_TEST=hello42", NULL}
 *      helper getenv()s it, matches, exits 0.
 *   2. {"DONIX_ENVP_TEST=hello42", "SECOND=two", NULL}
 *      helper matches both, exits 0.
 *   3. {NULL}
 *      helper getenv()s NULL, exits 2.
 *
 * The driver checks the child's EXIT STATUS, not its stdout.
 * There is no way for the parent to capture the child's output
 * on donix yet, and the exit status is the honest carrier: a
 * child that could not see its environment exits 2, one that saw
 * a wrong value exits 1, one that matched exits 0.
 *
 * The syscalls under test (fork, execve, wait4, exit) are issued
 * via raw inline asm, matching musl_exec.c and musl_exec2.c.  The
 * driver deliberately does not use musl's fork()/execve()
 * wrappers: the wrapper's error handling would obscure whether a
 * failure came from the kernel or from musl, and this test is
 * about the kernel.
 *
 * ASM CONSTRAINT NOTE.  Every asm block below that executes
 * `syscall` declares %rax as an OUTPUT ("=a"(ret)), not merely
 * as an input.  The `syscall` instruction always overwrites %rax
 * with the return value, so an asm block that names %rax only as
 * an input and does not list it as clobbered is lying to the
 * compiler: GCC may assume %rax survives the block and reuse it
 * for the next operation without reloading.
 *
 * That is exactly what happened in the first cut of this file.
 * puts_raw was written with "a"(1L) as an input and no output
 * and no "rax" clobber.  With two puts_raw calls back to back at
 * the end of main, GCC kept its belief that %rax still held 1
 * from the first call's input and issued the second `syscall`
 * without reloading %rax.  The second syscall therefore ran with
 * %rax = the FIRST syscall's return value.  The run showed
 *
 *     Unknown syscall: 41                     (socket, unrelated)
 *     Unknown syscall: 18446744073709551578   (-38, i.e. -ENOSYS)
 *
 * The second line is the tell: 18446744073709551578 is 2^64-38,
 * the bit pattern of the -ENOSYS that syscall 41 returned.  A
 * syscall number that is the previous syscall's return value is
 * only possible if %rax was never reloaded.  musl_exec.c and
 * musl_exec2.c have the same puts_raw, but their callers always
 * followed it with a raw_fork/raw_wait4/raw_exit that sets
 * "a"(NNL) as an input, so the missing clobber never showed.
 * See docs/gotchas.md.
 *
 * STRING LENGTH NOTE.  puts_raw below takes ONLY the string; it
 * computes the length itself.  The first cut took (str, len) and
 * every call site hand-counted the length -- and several were
 * wrong (off by one or two), silently writing the string's NUL
 * terminator or one byte past it.  That is invisible on the
 * console (a NUL writes as nothing) until the day a string ends
 * exactly at a page boundary and the extra byte faults.  Do not
 * reintroduce a length parameter: there is no compile-time check
 * on a hand-counted length, and it WILL be wrong again.  See
 * docs/gotchas.md.
 *
 * The helper IS a normal musl binary (it uses getenv and printf),
 * which is correct -- the thing being tested is whether the C
 * runtime's `environ` got populated, and that can only be checked
 * from a program that uses the C runtime.
 *
 * Path convention: the driver passes "/usr/bin/ENVP_HELPER".
 * sys_execve attempt (a) tries it literally, which FatFs rejects
 * (leading '/'), then attempt (b) rewrites it to
 * "0:/usr/bin/ENVP_HELPER" and opens it.  That is the new
 * post-shim path; a bare name would not resolve, which is the
 * subject of musl_exec2's rewrite.
 */
#include <unistd.h>

/*
 * write(2) directly, no stdio buffering -- keeps the ordering
 * between parent and child bytes deterministic.
 *
 * Takes only the string; computes the length.  See the STRING
 * LENGTH NOTE above -- do not add a length parameter back.
 *
 * "=a"(ret) is load-bearing.  See the ASM CONSTRAINT NOTE above:
 * without it GCC assumes %rax is unchanged, and a second
 * puts_raw immediately after this one runs `syscall` with a
 * stale %rax.
 */
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

/*
 * exit(2).  Does not return.  "=a"(ret) is declared for the same
 * reason as puts_raw: the syscall overwrites %rax, and the
 * compiler must be told.
 */
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
 * Run envp_helper with the given envp.  Returns the child's exit
 * code (0..255), or -1 if fork or wait failed.
 *
 * argv is always {"ENVP_HELPER", NULL} -- the helper does not
 * read argv, but execve requires a non-NULL argv on Linux and a
 * future helper might.
 */
static int run_helper_with_envp(char** envp) {
    long pid = raw_fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        static char path[] = "/usr/bin/ENVP_HELPER";
        static char arg0[] = "ENVP_HELPER";
        char* argv[2];
        argv[0] = arg0;
        argv[1] = (char*)0;

        raw_execve(path, argv, envp);

        /* execve only returns on failure.  Exit 99 so the driver
         * can tell "execve failed" from any helper exit code. */
        puts_raw("ENVP-CHILD-EXEC-FAIL\n");
        raw_exit(99);
    }

    int status = -1;
    long reaped = raw_wait4(pid, &status, 0);
    if (reaped != pid) return -1;

    return (status >> 8) & 0xff;
}

int main(void) {
    /* --- 1. one variable --- */
    {
        char* envp[2];
        envp[0] = "DONIX_ENVP_TEST=hello42";
        envp[1] = (char*)0;

        int code = run_helper_with_envp(envp);
        if (code == 0) {
            puts_raw("ok 1: single var survives execve\n");
        } else if (code == 2) {
            puts_raw("FAIL 1: getenv returned NULL (envp dropped)\n");
            return 1;
        } else if (code == 99) {
            puts_raw("FAIL 1: execve failed\n");
            return 1;
        } else {
            puts_raw("FAIL 1: unexpected child exit\n");
            return 1;
        }
    }

    /* --- 2. two variables --- */
    {
        char* envp[3];
        envp[0] = "DONIX_ENVP_TEST=hello42";
        envp[1] = "SECOND=two";
        envp[2] = (char*)0;

        int code = run_helper_with_envp(envp);
        if (code == 0) {
            puts_raw("ok 2: two vars survive execve\n");
        } else if (code == 2) {
            puts_raw("FAIL 2: getenv returned NULL (envp dropped)\n");
            return 1;
        } else if (code == 99) {
            puts_raw("FAIL 2: execve failed\n");
            return 1;
        } else {
            puts_raw("FAIL 2: unexpected child exit\n");
            return 1;
        }
    }

    /* --- 3. empty envp: {NULL} ---
     *
     * getenv must return NULL for everything.  The helper exits 2
     * for that, so this check EXPECTS exit 2.  Getting exit 0 here
     * would mean the helper saw a stale environment -- i.e. the
     * kernel ignored the empty envp and passed something else
     * through, which is the bug this case exists to catch.
     */
    {
        char* envp[1];
        envp[0] = (char*)0;

        int code = run_helper_with_envp(envp);
        if (code == 2) {
            puts_raw("ok 3: empty envp passes through as empty\n");
        } else if (code == 0) {
            puts_raw("FAIL 3: empty envp got a stale environment\n");
            return 1;
        } else if (code == 99) {
            puts_raw("FAIL 3: execve failed\n");
            return 1;
        } else {
            puts_raw("FAIL 3: unexpected child exit\n");
            return 1;
        }
    }

    puts_raw("ENVP-ALL-PASS\n");
    raw_exit(0);
}
