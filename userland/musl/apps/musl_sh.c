#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <stdio.h>

static void puts_raw(const char* s, unsigned long n) {
    __asm__ volatile("syscall"
                     :
                     : "a"(1L), "D"(1L), "S"(s), "d"(n)
                     : "rcx", "r11", "memory");
}

static void puts_z(const char* s) {
    unsigned long n = 0;
    while (s[n]) n++;
    puts_raw(s, n);
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
 * Runs ONCE.  After ash exits, the user is at `donix>`; typing
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

/*
 * ---- Shell builtins ----
 *
 * The donix shell is a minimal exec loop: it tokenizes a line and
 * calls execve(argv[0]).  That means anything not backed by a real
 * .ELF file at the root or in /bin fails with EXEC-FAILED.  For
 * commands that must change the *shell's own* state -- cd (cwd),
 * exit (termination) -- a forked child is useless, because the
 * child's state change is discarded when it exits.
 *
 * These builtins therefore run in the PARENT, before the fork.
 * They are checked by name, after tokenization, and if matched,
 * handled inline and the line is done -- no fork, no exec.
 *
 * The set is deliberately small.  Anything that can be an external
 * program (ls, cat, hello) is left to exec.  Only the commands
 * whose whole purpose is to affect the shell are builtins.
 *
 * `exit` was already a special case in main(); it is now here for
 * consistency, and `cd` / `pwd` join it.
 */

/* Print the current working directory, like /bin/pwd. */
static void builtin_pwd(void) {
    char buf[256];
    char* r = getcwd(buf, sizeof(buf));
    if (!r) {
        puts_z("pwd: cannot determine cwd\n");
        return;
    }
    puts_z(buf);
    printf("\n");
}

/*
 * Change directory, like the cd builtin.
 *
 *   cd           -> "/" (donix has no $HOME)
 *   cd <path>    -> chdir(path)
 *
 * On failure, print a short message and leave cwd unchanged
 * (chdir does not move it on failure).
 */
static void builtin_cd(int argc, char** argv) {
    const char* target = "/";
    if (argc >= 2) target = argv[1];

    if (chdir(target) != 0) {
        puts_z("cd: cannot cd to ");
        puts_z(target);
        puts_raw("\n", 1);
    }
}

/*
 * Return 1 if the builtin ran (and the caller should NOT fork),
 * 0 if this is not a builtin.
 *
 * Matching is by exact argv[0]; no path search, no aliases.
 * "cd" is the builtin; "/bin/cd" or "busybox cd" are not (there
 * is no such file, and busybox has no cd applet).
 */
static int run_builtin(int argc, char** argv) {
    if (argc == 0 || argv[0] == 0) return 0;

    if (strcmp(argv[0], "cd") == 0) {
        builtin_cd(argc, argv);
        return 1;
    }
    if (strcmp(argv[0], "pwd") == 0) {
        builtin_pwd();
        return 1;
    }
    /* `exit` is handled in main() before this point, because it
     * must return from main, not just fall through. */
    return 0;
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
         * sys_read on fd 0 returns on the first available byte
         * (POSIX short read), so a single read() is not a line.
         * Loop here, like the newlib shell does.  Byte-at-a-time
         * also lets us echo each character immediately.
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

        /*
         * Tokenize the line ONCE, in the parent, so builtins can
         * see argv.  The fork/exec path reuses the same argv.
         *
         * MAX_ARGS caps the token count; extra tokens are dropped.
         */
        enum { MAX_ARGS = 16 };
        char* argv[MAX_ARGS];
        int argc = 0;
        {
            char* p = line;
            while (*p && argc < MAX_ARGS - 1) {
                while (*p == ' ' || *p == '\t') p++;
                if (!*p) break;
                argv[argc++] = p;
                while (*p && *p != ' ' && *p != '\t') p++;
                if (*p) *p++ = 0;
            }
            argv[argc] = (char*)0;
        }

        if (argc == 0) continue;

        /* `exit` terminates the shell itself, so it is handled
         * before the builtin dispatch (which returns rather than
         * returning from main). */
        if (strcmp(argv[0], "exit") == 0) {
            puts_raw("bye\n", 4);
            return 0;
        }

        /*
         * Builtins run in the parent, before any fork.  If one
         * handled the line, loop back for the next prompt.
         */
        if (run_builtin(argc, argv)) {
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            puts_raw("FORK-FAILED\n", 12);
            continue;
        }

        if (pid == 0) {
            /*
             * Pass argv[0] through unchanged.  The kernel's
             * sys_execve handles the forms the user might type:
             *
             *   - a bare name like "ls"  -> "0:/LS.ELF" (attempt c1)
             *   - an absolute path like "/LS.ELF"
             *                            -> "0:/LS.ELF" (attempt b)
             *   - bare "busybox"         -> "0:/BIN/BUSYBOX" (c2)
             */
            execve(argv[0], argv, (char**)0);
            puts_raw("EXEC-FAILED\n", 12);
            _exit(127);
        }

        int status = 0;
        wait4(pid, &status, 0, (void*)0);
    }
}
