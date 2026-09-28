#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

static void puts_raw(const char* s, unsigned long n) {
    __asm__ volatile("syscall"
                     :
                     : "a"(1L), "D"(1L), "S"(s), "d"(n)
                     : "rcx", "r11", "memory");
}

/*
 * Fork and run `/bin/busybox sh`, waiting for it to exit.
 *
 * Called once at startup, before the donix prompt loop.  The
 * purpose is to drop the user straight into the busybox shell on
 * boot, with the donix shell one `exit` away.
 *
 * If the fork fails, or busybox is missing, or execve fails, the
 * child prints EXEC-FAILED and exits; wait4 returns in the parent
 * and we fall through to the donix prompt.  A broken busybox
 * therefore degrades to "you get the donix shell" rather than a
 * dead console.
 *
 * argv is the explicit `busybox sh` form rather than relying on
 * busybox's argv[0]-basename applet dispatch.  It is the form the
 * kernel's execve sees after path resolution, and it is what the
 * exploring copy used.
 *
 * Runs ONCE.  If it ran on every loop iteration, `exit` at ash
 * would immediately re-enter ash and the donix prompt would be
 * unreachable.  After ash exits, the user is at `donix>`; typing
 * `busybox sh` (or `/bin/busybox sh`) re-enters ash.
 */
static void run_busybox_sh(void) {
    pid_t pid = fork();
    if (pid < 0) {
        puts_raw("FORK-FAILED\n", 12);
        return;
    }
    if (pid == 0) {
        char* argv[3];
        argv[0] = (char*)"/bin/busybox";
        argv[1] = (char*)"sh";
        argv[2] = (char*)0;
        execve(argv[0], argv, (char**)0);
        puts_raw("EXEC-FAILED\n", 12);
        _exit(127);
    }
    int status = 0;
    wait4(pid, &status, 0, (void*)0);
}

int main(void) {
    char line[256];

    /*
     * Drop into busybox ash on boot.  When the user types `exit`
     * at the ash prompt, the child exits, wait4 returns, and we
     * fall through to the donix prompt below.
     */
    run_busybox_sh();

    for (;;) {
        puts_raw("donix> ", 7);

        /*
         * Read one byte at a time until newline.
         *
         * sys_read on fd 0 now returns on the first available byte
         * (POSIX short read), so a single read() is not a line.
         * Loop here, like the newlib shell does.  Byte-at-a-time
         * also lets us echo each character immediately, which is
         * what a terminal user expects.
         */
        int n = 0;
        for (;;) {
            char c;
            int r = read(0, &c, 1);
            if (r <= 0) continue;
            if (c == '\r' || c == '\n') {
                puts_raw("\n", 1);
                break;
            }
            if (c == '\b' || c == 0x7f) {
                if (n > 0) {
                    n--;
                    puts_raw("\b \b", 3);
                }
                continue;
            }
            if (c < 0x20 || c > 0x7e) continue;
            if (n >= (int)sizeof(line) - 1) continue;
            line[n++] = c;
            puts_raw(&c, 1);
        }
        line[n] = 0;

        if (n == 0) continue;

        if (n == 4 && memcmp(line, "exit", 4) == 0) {
            puts_raw("bye\n", 4);
            return 0;
        }

        pid_t pid = fork();
        if (pid < 0) {
            puts_raw("FORK-FAILED\n", 12);
            continue;
        }

        if (pid == 0) {
            /*
             * Tokenize `line` in place on whitespace.  `line` is a
             * local in the child's copy of the address space after
             * fork, so mutating it is safe.
             *
             * MAX_ARGS is a compile-time cap; a longer command line
             * is truncated to the first MAX_ARGS-1 tokens, with the
             * final slot left NULL-terminated.
             */
            enum { MAX_ARGS = 16 };
            char* argv[MAX_ARGS];
            int argc = 0;

            char* p = line;
            while (*p && argc < MAX_ARGS - 1) {
                while (*p == ' ' || *p == '\t') p++;
                if (!*p) break;
                argv[argc++] = p;
                while (*p && *p != ' ' && *p != '\t') p++;
                if (*p) *p++ = 0;
            }
            argv[argc] = (char*)0;

            if (argc == 0) _exit(0);   /* should not happen: n>0 checked above */

            /*
             * Pass argv[0] through unchanged.  The kernel's
             * sys_execve now handles both forms the user might
             * type:
             *
             *   - a bare name like "ls"  -> "0:/LS.ELF" (attempt c1)
             *   - an absolute path like "/LS.ELF"
             *                            -> "0:/LS.ELF" (attempt b)
             *
             * The old rewriting here turned "/LS.ELF" into
             * "0://LS.ELF.ELF" (double slash, doubled suffix),
             * which FatFs rejected before the kernel's retry could
             * see it.  Doing nothing is now correct.
             */
            execve(argv[0], argv, (char**)0);
            puts_raw("EXEC-FAILED\n", 12);
            _exit(127);
        }

        int status = 0;
        wait4(pid, &status, 0, (void*)0);
    }
}
