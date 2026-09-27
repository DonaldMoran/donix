#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

static void puts_raw(const char* s, unsigned long n) {
    __asm__ volatile("syscall"
                     :
                     : "a"(1L), "D"(1L), "S"(s), "d"(n)
                     : "rcx", "r11", "memory");
}

int main(void) {
    char line[256];

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
             * Resolve argv[0] to a full path, matching the newlib
             * shell's convention (user_shell.c:run_external):
             *
             *     snprintf(path, sizeof(path), "0:/%s.ELF", argv[0]);
             *
             * The user types a bare command name (`cat`, `hello`);
             * the shell turns it into `0:/NAME.ELF`.  If the token
             * already contains `:/` (the user typed a full path
             * like `0:/musl_r10probe.elf`), leave it alone.
             *
             * argv[1..n] are passed verbatim, exactly as the newlib
             * shell does.  The newlib cat/echo expect bare
             * filenames and prepend `0:/` themselves.
             */
            char path[128];
            {
                const char* tok = argv[0];
                int has_prefix = 0;
                for (const char* q = tok; *q; q++) {
                    if (q[0] == ':' && q[1] == '/') { has_prefix = 1; break; }
                }
                if (has_prefix) {
                    int i = 0;
                    while (tok[i] && i < (int)sizeof(path) - 1) {
                        path[i] = tok[i];
                        i++;
                    }
                    path[i] = 0;
                } else {
                    int i = 0;
                    path[i++] = '0';
                    path[i++] = ':';
                    path[i++] = '/';
                    for (const char* q = tok; *q && i < (int)sizeof(path) - 1; q++) {
                        path[i++] = *q;
                    }
                    if (i < (int)sizeof(path) - 5) {
                        path[i++] = '.';
                        path[i++] = 'E';
                        path[i++] = 'L';
                        path[i++] = 'F';
                    }
                    path[i] = 0;
                }
            }

            execve(path, argv, (char**)0);
            puts_raw("EXEC-FAILED\n", 12);
            _exit(127);
        }

        int status = 0;
        wait4(pid, &status, 0, (void*)0);
    }
}
