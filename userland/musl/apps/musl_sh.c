#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <stdio.h>
#include <fcntl.h>

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
 * These builtins therefore run in the PARENT, before any fork.
 * They are checked by name, after tokenization, and if matched,
 * handled inline and the command is done -- no fork, no exec.
 *
 * The set is deliberately small.  Anything that can be an external
 * program (ls, cat, hello) is left to exec.  Only the commands
 * whose whole purpose is to affect the shell are builtins.
 *
 * Each builtin now returns its own exit status (0 = success), so
 * that `&&` can short-circuit on a failed builtin the same way it
 * does on a failed program.
 */

/* Print the current working directory, like /bin/pwd. */
static int builtin_pwd(void) {
    char buf[256];
    char* r = getcwd(buf, sizeof(buf));
    if (!r) {
        puts_z("pwd: cannot determine cwd\n");
        return 1;
    }
    puts_z(buf);
    printf("\n");
    return 0;
}

/*
 * Change directory, like the cd builtin.
 *
 *   cd           -> "/" (donix has no $HOME)
 *   cd <path>    -> chdir(path)
 *
 * On failure, print a short message and leave cwd unchanged
 * (chdir does not move it on failure).  Returns chdir's result
 * as the command status, so `cd /nope && foo` skips foo.
 */
static int builtin_cd(int argc, char** argv) {
    const char* target = "/";
    if (argc >= 2) target = argv[1];

    if (chdir(target) != 0) {
        puts_z("cd: cannot cd to ");
        puts_z(target);
        puts_raw("\n", 1);
        return 1;
    }
    return 0;
}

/*
 * Run a builtin in the parent if argv[0] names one.
 *
 * On a match, runs it and stores its exit status in *status_out,
 * returning 1.  On no match, returns 0 and leaves *status_out
 * untouched.
 *
 * Matching is by exact argv[0]; no path search, no aliases.
 * "cd" is the builtin; "/bin/cd" or "busybox cd" are not (there
 * is no such file, and busybox has no cd applet).
 *
 * `exit` is handled by the sequence runner, not here, because it
 * must terminate the shell rather than just return a status.
 */
static int run_builtin(int argc, char** argv, int* status_out) {
    if (argc == 0 || argv[0] == 0) return 0;

    if (strcmp(argv[0], "cd") == 0) {
        *status_out = builtin_cd(argc, argv);
        return 1;
    }
    if (strcmp(argv[0], "pwd") == 0) {
        *status_out = builtin_pwd();
        return 1;
    }
    return 0;
}

/*
 * ---- Tokenizer, phase 1 ----
 *
 * Split `line` (NUL-terminated, modified in place) into argv.
 *
 * This pass handles only:
 *
 *   - unquoted space/tab separates words
 *   - '...' and "..." group, contents taken literally; the quote
 *     characters themselves are dropped
 *
 * Operators (< > >> | && ;) are NOT handled here.  They are left
 * as literal characters inside words, so `echo hi>out` yields the
 * single word `hi>out`.  split_operators() below is phase 2 and
 * splits operator-containing words after this pass has run.
 *
 * In-place, two pointers over the same buffer:
 *   r = read  cursor, walks the raw input
 *   w = write cursor, walks the cooked output
 * w never gets ahead of r, so writing *w while reading *r is safe.
 *
 * The one hazard of an in-place pass is the token terminator.  On
 * a word with no quotes, every byte is copied 1:1, so at the end
 * of the word w has caught up to r -- and writing the terminating
 * NUL at w would land on the blank that r is about to inspect,
 * making the outer loop see a NUL and stop after one token.  So
 * the blank is consumed (r++) BEFORE the NUL is written; that is
 * what keeps the two cursors' work from colliding.  A word WITH
 * quotes shrinks, so w < r and the terminator is safe anyway --
 * but the blank must still be stepped over for the next token.
 *
 * Returns argc.  argv is NUL-terminated.  SH_MAX_ARGS caps the
 * token count; extra tokens are silently dropped.
 */
#define SH_MAX_ARGS 16

static int tokenize(char* line, char** argv) {
    char* r = line;
    char* w = line;
    int argc = 0;

    while (*r && argc < SH_MAX_ARGS - 1) {
        /* Skip unquoted blanks between tokens. */
        while (*r == ' ' || *r == '\t') r++;
        if (!*r) break;

        argv[argc++] = w;

        while (*r && *r != ' ' && *r != '\t') {
            if (*r == '\'' || *r == '"') {
                char q = *r++;
                while (*r && *r != q) *w++ = *r++;
                if (*r == q) r++;   /* consume closing quote */
                /* Unclosed quote: run to end of line. */
            } else {
                *w++ = *r++;
            }
        }

        /* Consume one trailing blank BEFORE writing the NUL, so
         * the terminator never lands on the byte r is inspecting.
         * (See the hazard note above.) */
        if (*r == ' ' || *r == '\t') r++;
        *w++ = 0;
    }

    argv[argc] = (char*)0;
    return argc;
}

/*
 * ---- Tokenizer, phase 2 ----
 *
 * Split operator characters out of the words phase 1 produced.
 *
 * Phase 1 gives words like `hi>out`, `a&&b`, `x|y`.  Phase 2
 * rewrites each such word as the sequence of tokens
 * `hi`, `>`, `out` / `a`, `&&`, `b` / `x`, `|`, `y`, so the
 * parser can look at argv and see the operators.
 *
 * Operators, longest match first:
 *   >>  &&  then  >  <  |  &  ;
 *
 * `&` and `;` are included because `&&` and `;` are in scope and
 * a lone `&`/`;` must not glue to a word.  A lone `&` is not
 * valid shell; it is tokenized and left for the parser to reject.
 *
 * This pass does NOT run in place.  Phase 1 wrote its tokens into
 * `line`; phase 2 copies them into `out`, inserting a NUL at each
 * operator boundary, and rewrites argv to point into `out`.  Two
 * separate buffers means no byte that is still to be read is ever
 * overwritten -- the in-place hazard phase 1 had to dance around
 * does not exist here.  `out` must be large enough for the worst
 * case; see the caller.
 *
 * Returns the new argc.  argv is NUL-terminated.
 */
static int split_operators(char* out, char** argv, int argc) {
    char* w = out;
    int nargc = 0;

    for (int i = 0; i < argc; i++) {
        if (nargc >= SH_MAX_ARGS - 1) break;

        const char* p = argv[i];
        char* start = w;

        while (*p) {
            char c = *p;
            if (c == '>' || c == '<' || c == '|' ||
                c == '&' || c == ';') {

                /* Close the word before the operator. */
                if (w > start) {
                    *w++ = 0;
                    argv[nargc++] = start;
                    if (nargc >= SH_MAX_ARGS - 1) break;
                    start = w;
                }

                /* The operator itself, longest match first. */
                char* op = w;
                *w++ = c;
                p++;
                if ((c == '>' || c == '&') && *p == c) {
                    *w++ = *p++;
                }
                *w++ = 0;
                argv[nargc++] = op;
                if (nargc >= SH_MAX_ARGS - 1) break;
                start = w;
            } else {
                *w++ = *p++;
            }
        }

        /* Close the word after the last operator (or the whole
         * word if it had none). */
        if (w > start && nargc < SH_MAX_ARGS - 1) {
            *w++ = 0;
            argv[nargc++] = start;
        }
    }

    argv[nargc] = (char*)0;
    return nargc;
}

/*
 * ---- Redirection ----
 *
 * Look for the first <, >, or >> in argv.  If found, and there is
 * a word after it, treat that word as the filename and return the
 * operator kind via *kind_out and the filename via *file_out.
 *
 *   *redir_idx_out  = index of the operator in argv
 *   *kind_out       = REDIR_IN / REDIR_OUT / REDIR_APPEND
 *   *file_out       = argv[redir_idx + 1]
 *
 * The command to run is argv[0 .. redir_idx).  The caller forks
 * and, in the child, opens *file_out and dup2()s it onto fd 0 or
 * 1 before exec.
 *
 * Returns 1 if a redirection was found and is well-formed, 0
 * otherwise.  On 0 the caller runs argv unchanged.
 *
 * Limits, deliberately:
 *   - only the FIRST redirection is honored; a second < or > is
 *     left in the command's args and reaches the program literally
 *   - redirection of a builtin is not supported (builtins run in
 *     the parent, which has no place to put a redirected fd)
 */
enum { REDIR_NONE = 0, REDIR_IN, REDIR_OUT, REDIR_APPEND };

static int parse_redir(char** argv, int argc,
                       int* redir_idx_out,
                       int* kind_out,
                       const char** file_out) {
    for (int i = 0; i < argc; i++) {
        const char* a = argv[i];
        int kind = REDIR_NONE;
        if (strcmp(a, "<") == 0) kind = REDIR_IN;
        else if (strcmp(a, ">") == 0) kind = REDIR_OUT;
        else if (strcmp(a, ">>") == 0) kind = REDIR_APPEND;
        if (kind == REDIR_NONE) continue;

        /* A redirection with nothing after it is a syntax error.
         * A redirection with nothing before it likewise. */
        if (i == 0) return 0;
        if (i + 1 >= argc) return 0;

        *redir_idx_out = i;
        *kind_out = kind;
        *file_out = argv[i + 1];
        return 1;
    }
    return 0;
}

/*
 * ---- Command runner ----
 *
 * Run ONE command: argv[0..argc), with any redirection already
 * present in argv.
 *
 * A builtin runs in the parent (run_builtin) and its own status is
 * returned.  Anything else forks; the child applies the
 * redirection and execs; the parent waits and returns the child's
 * exit status.
 *
 * Returns the command's exit status (0 = success, 1 = failure).
 * Callers use this for `&&`.
 *
 * `exit` is NOT handled here -- the sequence runner checks for it
 * before calling this, because it must terminate the shell.
 */
static int run_one(char** argv, int argc) {
    int bstatus = 0;
    if (run_builtin(argc, argv, &bstatus)) {
        /*
         * Builtin.  NOTE: a redirection attached to a builtin is
         * ignored -- the operator and filename reach the builtin
         * as ordinary argv and are silently dropped.  This is a
         * known step-1/step-2 limitation, to be revisited.
         */
        return bstatus;
    }

    int redir_idx = 0, redir_kind = REDIR_NONE;
    const char* redir_file = 0;
    int has_redir = parse_redir(argv, argc,
                                &redir_idx, &redir_kind, &redir_file);
    int cmd_argc = has_redir ? redir_idx : argc;

    if (cmd_argc == 0) {
        puts_raw("sh: syntax error\n", 17);
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        puts_raw("FORK-FAILED\n", 12);
        return 1;
    }

    if (pid == 0) {
        /*
         * Apply the redirection in the child, before exec.
         * On any failure, print and exit -- do NOT exec, so
         * the program does not run without its redirect.
         */
        if (has_redir) {
            int flags;
            int target_fd;
            if (redir_kind == REDIR_IN) {
                flags = O_RDONLY;
                target_fd = 0;
            } else if (redir_kind == REDIR_OUT) {
                flags = O_WRONLY | O_CREAT | O_TRUNC;
                target_fd = 1;
            } else { /* REDIR_APPEND */
                flags = O_WRONLY | O_CREAT | O_APPEND;
                target_fd = 1;
            }

            int fd = open(redir_file, flags, 0644);
            if (fd < 0) {
                puts_z("sh: cannot open ");
                puts_z(redir_file);
                puts_raw("\n", 1);
                _exit(1);
            }
            if (dup2(fd, target_fd) < 0) {
                puts_raw("sh: dup2 failed\n", 16);
                _exit(1);
            }
            if (fd != target_fd) close(fd);
        }

        /*
         * argv is NUL-terminated at cmd_argc.  execve reads argv
         * to the NUL, so write one at argv[cmd_argc] to terminate
         * the command there.  argv[cmd_argc] is either the
         * operator or the existing NUL, so this is safe; the
         * child execs (or _exits) immediately after, so the
         * mutation never escapes.
         */
        argv[cmd_argc] = (char*)0;

        execve(argv[0], argv, (char**)0);
        puts_raw("EXEC-FAILED\n", 12);
        _exit(127);
    }

    int status = 0;
    wait4(pid, &status, 0, (void*)0);

    /*
     * Extract the child's exit code for `&&`.  WIFEXITED /
     * WEXITSTATUS from <sys/wait.h>.  A child killed by a signal
     * (not possible here -- no signal delivery) would fall
     * through as 1.
     */
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

/*
 * ---- Sequence runner ----
 *
 * Walk the tokenized line, splitting it into commands separated by
 * `;` or `&&`, and run each one with run_one().
 *
 *   cmd1 ; cmd2          run cmd1, then cmd2 regardless
 *   cmd1 && cmd2         run cmd1, then cmd2 only if cmd1 was 0
 *
 * `&&` short-circuits on a non-zero status; `;` does not.  A
 * command with no separator is just a one-element sequence.
 *
 * `exit` is honored at the start of any command: it terminates
 * the shell immediately, skipping the rest of the line.  Sets
 * *should_exit and returns.
 *
 * `|` is NOT a separator here -- a pipeline is still a literal
 * word, so `a | b` runs `a` with `|` and `b` as args.  Pipelines
 * are a later step.
 *
 * Returns the exit status of the last command run.
 */
static int run_line(char** argv, int argc, int* should_exit) {
    int i = 0;
    int last_status = 0;

    *should_exit = 0;
    while (i < argc) {
        /* Find the end of this command: the next ; or && at or
         * after i. */
        int j = i;
        while (j < argc && strcmp(argv[j], ";") != 0 &&
               strcmp(argv[j], "&&") != 0) {
            j++;
        }
        int sep = (j < argc) ? (argv[j][0] == ';' ? ';' : '&') : 0;
        int cmd_argc = j - i;

        if (cmd_argc > 0) {
            /* `exit` must terminate the shell, not fork. */
            if (strcmp(argv[i], "exit") == 0) {
                *should_exit = 1;
                return 0;
            }

            last_status = run_one(&argv[i], cmd_argc);
        }

        if (sep == 0) break;            /* end of line */

        if (sep == '&') {
            /* &&: skip the next command if this one failed. */
            if (last_status != 0) {
                /* Skip past the operator and the next command. */
                i = j + 1;
                while (i < argc && strcmp(argv[i], ";") != 0 &&
                       strcmp(argv[i], "&&") != 0) {
                    i++;
                }
                if (i < argc && strcmp(argv[i], ";") == 0) {
                    i++;    /* ; after a skipped && operand: continue */
                    continue;
                }
                break;      /* nothing left */
            }
        }
        /* `;` or a satisfied `&&`: advance to the next command. */
        i = j + 1;
    }

    return last_status;
}

int main(void) {
    char line[256];

    /*
     * Phase 2 output buffer.  Phase 2 only ever adds one NUL per
     * operator run, and an operator run is at least one character,
     * so the worst case is 2x the input length.  line is 256, so
     * 512 is safe.  static so it does not sit on the stack next to
     * line[].
     */
    static char out[512];

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

        char* argv[SH_MAX_ARGS];
        int argc = tokenize(line, argv);
        argc = split_operators(out, argv, argc);

        /* TEMPORARY debug: the whole line is built into one buffer
         * and written with a single sys_write, so the tokenization
         * cannot be interleaved or torn.  Remove once the parser
         * step consumes argv. */
        {
            char dbg[512];
            unsigned long d = 0;
            int i;
            dbg[d++] = '[';
            for (i = 0; i < argc; i++) {
                if (i) dbg[d++] = '|';
                const char* s = argv[i];
                while (*s && d < sizeof(dbg) - 3) dbg[d++] = *s++;
            }
            dbg[d++] = ']';
            dbg[d++] = '\n';
            puts_raw(dbg, d);
        }

        if (argc == 0) continue;

        int should_exit = 0;
        run_line(argv, argc, &should_exit);

        if (should_exit) {
            puts_raw("bye\n", 4);
            return 0;
        }
    }
}
