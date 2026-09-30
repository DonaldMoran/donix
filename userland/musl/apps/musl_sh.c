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
 * Return 1 if argv[0] names a builtin, 0 otherwise.  Does NOT run
 * it -- used by the pipeline runner to detect a builtin before
 * deciding whether the command can be forked.
 */
static int is_builtin(const char* name) {
    return strcmp(name, "cd") == 0 || strcmp(name, "pwd") == 0;
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
 * ---- Child fork helper ----
 *
 * Fork a child that:
 *   1. dup2()s in_fd onto 0 if in_fd >= 0
 *   2. dup2()s out_fd onto 1 if out_fd >= 0
 *   3. applies a file redirection (if has_redir), which overrides
 *      the pipe fd for the fd it targets
 *   4. execs argv[0..cmd_argc)
 *
 * Returns the child pid to the parent, -1 on fork failure.  Does
 * NOT wait -- the caller decides when to wait (run_one waits
 * immediately; run_pipeline forks all children first, closes the
 * parent's pipe ends, then waits for each).
 *
 * The parent does not close in_fd/out_fd here; that is the
 * caller's job, because in a pipeline the same pipe end may be
 * handed to more than one child before the parent is done with it.
 *
 * Redirection precedence: pipe fds are dup2'd first, then file
 * redirection is applied on top.  So `a | b > f` sends b's stdout
 * to f, not into the (nonexistent) next pipe, and `a < f | b`
 * reads a's stdin from f.  This matches POSIX shell behavior.
 */
static pid_t fork_child(char** argv, int cmd_argc,
                        int in_fd, int out_fd,
                        int has_redir, int redir_kind,
                        const char* redir_file) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid != 0) return pid;

    /* --- child --- */

    if (in_fd >= 0) {
        if (dup2(in_fd, 0) < 0) {
            puts_raw("sh: dup2 failed\n", 16);
            _exit(1);
        }
    }
    if (out_fd >= 0) {
        if (dup2(out_fd, 1) < 0) {
            puts_raw("sh: dup2 failed\n", 16);
            _exit(1);
        }
    }

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
     * The child inherited the parent's copies of the pipe ends.
     * They are all above fd 2 and the child has no use for any of
     * them now that 0/1 are set, so close them.  The caller passes
     * -1 for ends this child should not have had, but a child in
     * the middle of a pipeline does inherit both the upstream and
     * downstream pipe ends as raw fds (they are not its in_fd or
     * out_fd).  Leaving them open would keep the pipe alive after
     * the child exits, and the peer would never see EOF.
     *
     * There is no per-child list of "which fds to close" here, so
     * this closes every fd from 3 up to the process file-table cap
     * that the shell might have opened.  MAX_PROCESS_FILES is 64
     * (raised in v0.6.5); closing 3..63 is cheap and safe -- the
     * only fds above 2 this child owns are pipe ends.
     */
    for (int fd = 3; fd < 64; fd++) close(fd);

    /*
     * argv is NUL-terminated at cmd_argc.  execve reads argv to
     * the NUL, so write one at argv[cmd_argc] to terminate the
     * command there.  argv[cmd_argc] is either an operator or the
     * existing NUL, so this is safe; the child execs (or _exits)
     * immediately after, so the mutation never escapes.
     */
    argv[cmd_argc] = (char*)0;

    execve(argv[0], argv, (char**)0);
    puts_raw("EXEC-FAILED\n", 12);
    _exit(127);
}

/*
 * ---- Command runner ----
 *
 * Run ONE command: argv[0..argc), with any redirection already
 * present in argv.  No pipeline -- a `|` in argv is just a word.
 *
 * A builtin runs in the parent (run_builtin) and its own status is
 * returned.  Anything else forks (fork_child), waits, and returns
 * the child's exit status.
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
         * as ordinary argv and are silently dropped.  Known
         * limitation, to be revisited.
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

    pid_t pid = fork_child(argv, cmd_argc,
                           -1, -1,
                           has_redir, redir_kind, redir_file);
    if (pid < 0) {
        puts_raw("FORK-FAILED\n", 12);
        return 1;
    }

    int status = 0;
    wait4(pid, &status, 0, (void*)0);

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

/*
 * ---- Pipeline runner ----
 *
 * Run a segment of the line as a pipeline: one or more commands
 * separated by `|`.
 *
 *   cmd                 -> run_one (builtins run in the parent)
 *   a | b               -> fork both, pipe a's stdout to b's stdin
 *   a | b | c           -> N-1 pipes, each command's stdin/out wired
 *
 * A one-command segment is NOT a pipeline; it goes straight to
 * run_one so that a bare `cd` still changes the shell's cwd.  A
 * multi-command segment with a builtin in it is refused: a
 * builtin cannot be forked without changing its meaning (`cd` in
 * a pipeline would not affect the parent), and donix's builtins
 * have no subshell form.  `sh: builtin in pipeline not supported`
 * is printed and nothing runs.
 *
 * Returns the LAST command's exit status, which is what `&&`
 * consults after a pipeline.  A single command returns that
 * command's status.
 *
 * Redirection on a command inside a pipeline is honored; see the
 * precedence note on fork_child.
 */
static int run_pipeline(char** argv, int argc) {
    /* Find how many commands (count of | + 1) and where they
     * start.  cmd_start[k] is the first argv index of command k. */
    int cmd_start[SH_MAX_ARGS];
    int ncmd = 0;
    cmd_start[ncmd++] = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "|") == 0) {
            cmd_start[ncmd++] = i + 1;
        }
    }

    /* No pipe: single command. */
    if (ncmd == 1) {
        return run_one(argv, argc);
    }

    /*
     * Multi-command pipeline.  Refuse if any command is a builtin
     * (see above).  Also refuse an empty command (leading,
     * trailing, or doubled `|`), which would otherwise fork a
     * child with argc 0.
     */
    for (int k = 0; k < ncmd; k++) {
        int start = cmd_start[k];
        int end = (k + 1 < ncmd) ? cmd_start[k + 1] - 1 : argc;
        /* end is the index of the `|` that ends this command, or
         * argc for the last.  The command is argv[start..end). */
        if (end <= start) {
            puts_raw("sh: syntax error near |\n", 24);
            return 1;
        }
        if (is_builtin(argv[start])) {
            puts_raw("sh: builtin in pipeline not supported\n", 38);
            return 1;
        }
    }

    /*
     * Fork the commands.  pipes[k] is the k-th pipe (between
     * command k and command k+1); ncmd-1 pipes total.
     *
     * Order matters: create all pipes first, then fork each
     * command with the right ends.  After forking all children,
     * the parent closes every pipe end it holds -- that is what
     * lets a reader see EOF when its writer exits.
     */
    int pipes[SH_MAX_ARGS][2];
    pid_t pids[SH_MAX_ARGS];

    for (int k = 0; k < ncmd - 1; k++) {
        if (pipe(pipes[k]) < 0) {
            puts_raw("sh: pipe failed\n", 16);
            /* Close pipes made so far. */
            for (int j = 0; j < k; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return 1;
        }
    }

    for (int k = 0; k < ncmd; k++) {
        int start = cmd_start[k];
        int end = (k + 1 < ncmd) ? cmd_start[k + 1] - 1 : argc;
        int cmd_argc = end - start;

        int in_fd  = (k > 0) ? pipes[k - 1][0] : -1;
        int out_fd = (k < ncmd - 1) ? pipes[k][1] : -1;

        int redir_idx = 0, redir_kind = REDIR_NONE;
        const char* redir_file = 0;
        int has_redir = parse_redir(&argv[start], cmd_argc,
                                    &redir_idx, &redir_kind, &redir_file);
        int exec_argc = has_redir ? redir_idx : cmd_argc;
        if (exec_argc == 0) {
            puts_raw("sh: syntax error near |\n", 24);
            /* Reap nothing yet; close all pipe ends and return. */
            for (int j = 0; j < ncmd - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return 1;
        }

        pid_t pid = fork_child(&argv[start], exec_argc,
                               in_fd, out_fd,
                               has_redir, redir_kind, redir_file);
        if (pid < 0) {
            puts_raw("FORK-FAILED\n", 12);
            for (int j = 0; j < ncmd - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return 1;
        }
        pids[k] = pid;
    }

    /* Parent: close every pipe end.  Children have their own
     * copies; this is what makes EOF propagate. */
    for (int k = 0; k < ncmd - 1; k++) {
        close(pipes[k][0]);
        close(pipes[k][1]);
    }

    /* Wait for all, remember the last one's status. */
    int last_status = 0;
    for (int k = 0; k < ncmd; k++) {
        int status = 0;
        wait4(pids[k], &status, 0, (void*)0);
        if (k == ncmd - 1) {
            last_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        }
    }
    return last_status;
}

/*
 * ---- Sequence runner ----
 *
 * Walk the tokenized line, splitting it into segments separated by
 * `;` or `&&`, and run each segment with run_pipeline().
 *
 *   cmd1 ; cmd2          run cmd1, then cmd2 regardless
 *   cmd1 && cmd2         run cmd1, then cmd2 only if cmd1 was 0
 *
 * `&&` short-circuits on a non-zero status; `;` does not.  A
 * segment with no separator is a one-element sequence; a segment
 * with `|` is a pipeline and is run_pipeline's concern.
 *
 * `exit` is honored at the start of any segment: it terminates
 * the shell immediately, skipping the rest of the line.  Sets
 * *should_exit and returns.
 *
 * Returns the exit status of the last segment run.
 */
static int run_line(char** argv, int argc, int* should_exit) {
    int i = 0;
    int last_status = 0;

    *should_exit = 0;
    while (i < argc) {
        /* Find the end of this segment: the next ; or && at or
         * after i.  A | is NOT a segment separator -- it stays
         * inside the segment for run_pipeline. */
        int j = i;
        while (j < argc && strcmp(argv[j], ";") != 0 &&
               strcmp(argv[j], "&&") != 0) {
            j++;
        }
        int sep = (j < argc) ? (argv[j][0] == ';' ? ';' : '&') : 0;
        int seg_argc = j - i;

        if (seg_argc > 0) {
            /* `exit` must terminate the shell, not fork. */
            if (strcmp(argv[i], "exit") == 0) {
                *should_exit = 1;
                return 0;
            }

            last_status = run_pipeline(&argv[i], seg_argc);
        }

        if (sep == 0) break;            /* end of line */

        if (sep == '&') {
            /* &&: skip the next segment if this one failed. */
            if (last_status != 0) {
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
        /* `;` or a satisfied `&&`: advance to the next segment. */
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
