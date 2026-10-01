/*
 * canary.c -- the post-change smoke test.
 *
 * Run as `canary` (read-only rows) or `canary --full` (also the
 * mutating rows).  Exits 0 if every check passed, 1 otherwise.
 *
 * Runs from EITHER shell.  From `donix>`, musl_sh's run_external
 * finds /usr/bin/CANARY.ELF.  From ash, busybox's PATH search finds
 * the same file.  The program does not know or care which shell
 * launched it -- it does its own fork/execve/wait4 for each row.
 *
 * WHY THIS EXISTS.  The canary used to be a list of lines in
 * handoff.md that a human read and retyped.  That drifts: the list
 * said `find / -type d` prints `/` and `/bin`, and after the
 * /usr/bin layout move it prints five directories instead -- a
 * change nobody notices until it is mistaken for a regression.  A
 * program makes the canary executable and self-reporting, and the
 * expected values live next to the checks.
 *
 * WHAT IT COVERS.  Every canary row that can run non-interactively:
 * the shell spine, the file and directory ops, pipelines,
 * redirection, find, and the *at resolver exercised through a
 * two-level tree walk.
 *
 * WHAT IT DOES NOT COVER, and cannot:
 *   - `busybox ash` interactively (needs a human at a prompt)
 *   - `vi` (takes the screen; mutates the disk)
 *   - anything that needs a keystroke
 * Those stay manual.  This program prints a reminder at the end.
 *
 * HOW IT CHECKS.  Each row runs via fork/execve/wait4, its stdout is
 * captured through a pipe, and the check is "exit status 0 AND the
 * output contains these substrings".  Substring, not exact match:
 * `ls` ordering, `find` traversal order, and the musl_wait dot count
 * all vary, and none of them varying is a regression.  Matching is
 * CASE-INSENSITIVE, because busybox `ls` prints the FAT short name
 * (`HELLO-WORLD.TXT`) where a donix-native tool prints the long name.
 *
 * PATHS.  Every donix-native binary is named with its `.ELF` suffix
 * (`/usr/bin/LS.ELF`, not `/usr/bin/LS`), because execve is called
 * directly here and does NOT get musl_sh's suffix-adding search.
 * `busybox` is the one binary staged without a suffix, so it is
 * named `/bin/busybox`.
 *
 * ARGV HELPERS.  check4 and check5 cover four- and five-word command
 * lines.  Rows with more words than that build argv by hand -- see
 * `find / -type f -name busybox`, which is seven words.  A row that
 * uses the wrong helper does not fail loudly; it silently drops the
 * trailing words and tests something else.  The first run of this
 * program had exactly that bug.
 *
 * MUTATING ROWS.  `echo hi > /cout.txt` and `rm -r /ctree` change
 * the disk.  They run only with --full, and each row cleans up after
 * itself.  A read-only canary is the default so a run never leaves
 * the image dirty.  A run interrupted partway could leave /cfile,
 * /cdir, /ctree, or /cout.txt behind; remove them by hand if a later
 * run reports "file exists".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

/* ------------------------------------------------------------------
 * Output capture.
 *
 * Run argv via fork/execve/wait4 with stdout (and, optionally,
 * stderr) redirected into a pipe.  Write the captured bytes into out
 * (NUL-terminated, capped at out_cap), and return the child's exit
 * status, or -1 on a fork/pipe failure.
 *
 * The pipe is read to EOF before wait4, so a child that writes more
 * than a pipe buffer cannot deadlock.  The output of every row here
 * is far under 64 KB.
 * ------------------------------------------------------------------ */
static int run_capture(char* const argv[], char* out, size_t out_cap,
                       int merge_stderr) {
    int fds[2];
    if (pipe(fds) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]); close(fds[1]);
        return -1;
    }

    if (pid == 0) {
        /* child */
        close(fds[0]);
        dup2(fds[1], 1);
        if (merge_stderr) dup2(fds[1], 2);
        if (fds[1] != 1) close(fds[1]);
        execve(argv[0], argv, (char**)0);
        /* execve failed: caller sees exit 127 and empty output */
        _exit(127);
    }

    /* parent */
    close(fds[1]);
    size_t total = 0;
    for (;;) {
        if (total + 1 >= out_cap) break;
        ssize_t n = read(fds[0], out + total, out_cap - 1 - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    out[total] = '\0';
    close(fds[0]);

    int status = 0;
    wait4(pid, &status, 0, (void*)0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

/* ------------------------------------------------------------------
 * Check bookkeeping.
 * ------------------------------------------------------------------ */
static int g_pass = 0;
static int g_fail = 0;

/*
 * tolower_ascii(): the C library's tolower is locale-dependent; a
 * four-line version is clearer than a setlocale call.
 */
static char tolower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

/*
 * contains_ci(): case-insensitive substring search.
 *
 * Busybox's `ls` prints the FAT short name -- `HELLO-WORLD.TXT` --
 * while a donix-native tool prints the long name.  Both are the same
 * file, so the check should not care about case.
 */
static int contains_ci(const char* haystack, const char* needle) {
    if (*needle == '\0') return 1;
    for (const char* h = haystack; *h; h++) {
        const char* hp = h;
        const char* np = needle;
        while (*hp && *np &&
               tolower_ascii(*hp) == tolower_ascii(*np)) {
            hp++;
            np++;
        }
        if (*np == '\0') return 1;
    }
    return 0;
}

/*
 * check(): run argv, require exit status 0 and every one of the
 * substrings in `want` present in the output (case-insensitively).
 * `want` is an array of strings, NULL-terminated; pass NULL for
 * "no substring requirement, exit status only".
 */
static void check(const char* label, char* const argv[],
                  const char* const want[], int merge_stderr) {
    static char out[8192];
    out[0] = '\0';

    int rc = run_capture(argv, out, sizeof(out), merge_stderr);

    int ok = (rc == 0);
    const char* missing = NULL;

    if (ok && want) {
        for (int i = 0; want[i]; i++) {
            if (!contains_ci(out, want[i])) {
                ok = 0;
                missing = want[i];
                break;
            }
        }
    }

    if (ok) {
        printf("ok   %s\n", label);
        g_pass++;
    } else {
        printf("FAIL %s\n", label);
        if (rc != 0) {
            printf("       exit status %d (errno %d: %s)\n",
                   rc, errno, strerror(errno));
        }
        if (missing) {
            printf("       output missing: \"%s\"\n", missing);
        }
        printf("       captured output:\n");
        const char* p = out;
        while (*p) {
            const char* nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            printf("         | %.*s\n", (int)len, p);
            if (!nl) break;
            p = nl + 1;
        }
        g_fail++;
    }
}

/*
 * check4(): build argv from up to four string literals and call check.
 */
static void check4(const char* label,
                   const char* a0, const char* a1,
                   const char* a2, const char* a3,
                   const char* const want[], int merge_stderr) {
    char* argv[5];
    int n = 0;
    argv[n++] = (char*)a0;
    if (a1) argv[n++] = (char*)a1;
    if (a2) argv[n++] = (char*)a2;
    if (a3) argv[n++] = (char*)a3;
    argv[n] = NULL;
    check(label, argv, want, merge_stderr);
}

/*
 * check5(): five-word command line.
 */
static void check5(const char* label,
                   const char* a0, const char* a1,
                   const char* a2, const char* a3,
                   const char* a4,
                   const char* const want[], int merge_stderr) {
    char* argv[6];
    int n = 0;
    argv[n++] = (char*)a0;
    if (a1) argv[n++] = (char*)a1;
    if (a2) argv[n++] = (char*)a2;
    if (a3) argv[n++] = (char*)a3;
    if (a4) argv[n++] = (char*)a4;
    argv[n] = NULL;
    check(label, argv, want, merge_stderr);
}

/* ==================================================================
 * Read-only rows.
 * ================================================================== */

static void run_readonly(void) {
    /* --- file reads ------------------------------------------------- */
    {
        static const char* const w[] = { "Hello", "single-drive", NULL };
        check4("cat hello-world.txt", "/usr/bin/CAT.ELF",
               "hello-world.txt", NULL, NULL, w, 0);
    }
    {
        static const char* const w[] = { "Hello", NULL };
        check4("busybox cat hello-world.txt", "/bin/busybox", "cat",
               "hello-world.txt", NULL, w, 0);
    }

    /* --- listing ---------------------------------------------------- */
    {
        static const char* const w[] = { "bin", "usr", "tmp",
                                         "hello-world.txt", NULL };
        check4("ls /", "/usr/bin/LS.ELF", NULL, NULL, NULL, w, 0);
    }
    {
        static const char* const w[] = { "busybox", NULL };
        check4("ls /bin", "/usr/bin/LS.ELF", "/bin", NULL, NULL, w, 0);
    }
    {
        static const char* const w[] = { "LS.ELF", "CAT.ELF",
                                         "MUSL_SH.ELF", NULL };
        check4("ls /usr/bin", "/usr/bin/LS.ELF", "/usr/bin", NULL, NULL,
               w, 0);
    }
    {
        static const char* const w[] = { "bin", "usr", "tmp",
                                         "hello-world.txt", NULL };
        check4("busybox ls /", "/bin/busybox", "ls", "/", NULL, w, 0);
    }

    /* --- shell spine ------------------------------------------------ */
    {
        static const char* const w[] = { NULL };
        check4("busybox pwd", "/bin/busybox", "pwd", NULL, NULL, w, 0);
    }
    {
        static const char* const w[] = { "hello from donix", NULL };
        check4("hello", "/usr/bin/HELLO.ELF", NULL, NULL, NULL, w, 0);
    }

    /* --- find: the two-level walk ----------------------------------- */
    {
        static const char* const w[] = { "/bin", "/bin/busybox", NULL };
        check4("find /bin", "/bin/busybox", "find", "/bin", NULL, w, 0);
    }
    /*
     * find / -type d: the row that changed.  Before the layout move
     * it printed `/` and `/bin`; after, it walks /usr and /usr/bin.
     * Five directories now.  Each is checked as a substring, so
     * traversal order does not matter.
     */
    {
        static const char* const w[] = { "/bin", "/tmp", "/usr",
                                         "/usr/bin", NULL };
        check5("find / -type d", "/bin/busybox", "find", "/",
               "-type", "d", w, 0);
    }
    /*
     * find / -type f -name busybox: SEVEN words.  Neither check4 nor
     * check5 reaches that, so argv is built by hand.  An earlier
     * version used check4 here and silently ran `find / -type` with
     * no argument, which returned every entry rather than filtering.
     */
    {
        static const char* const w[] = { "/bin/busybox", NULL };
        char* argv[8];
        argv[0] = (char*)"/bin/busybox";
        argv[1] = (char*)"find";
        argv[2] = (char*)"/";
        argv[3] = (char*)"-type";
        argv[4] = (char*)"f";
        argv[5] = (char*)"-name";
        argv[6] = (char*)"busybox";
        argv[7] = NULL;
        check("find / -type f -name busybox", argv, w, 0);
    }

    /* --- applets through busybox ------------------------------------ */
    {
        static const char* const w[] = { "BusyBox", NULL };
        check4("busybox (no args)", "/bin/busybox", NULL, NULL, NULL,
               w, 0);
    }
    {
        static const char* const w[] = { "donix", NULL };
        check4("busybox uname -n", "/bin/busybox", "uname", "-n",
               NULL, w, 0);
    }

    /* --- $PATH is not exported by ash (recorded, not a bug) --------- */
    /*
     * busybox printenv PATH prints nothing because ash keeps PATH as
     * a shell variable and does not export it.  This row asserts that
     * THE OUTPUT IS EMPTY -- it is the one row where "no output" is
     * the pass.  printenv exits 1 when the variable is unset, so
     * "success" here means exit 0 OR 1 with empty output.
     *
     * If it ever prints a path, ash started exporting PATH and the
     * handoff's note should be updated.
     */
    {
        static char out[512];
        out[0] = '\0';
        char* argv[4];
        argv[0] = (char*)"/bin/busybox";
        argv[1] = (char*)"printenv";
        argv[2] = (char*)"PATH";
        argv[3] = NULL;
        int rc = run_capture(argv, out, sizeof(out), 0);
        size_t l = strlen(out);
        while (l > 0 && (out[l-1] == '\n' || out[l-1] == '\r'))
            out[--l] = '\0';
        int ok = (l == 0) && (rc == 0 || rc == 1);
        if (ok) {
            printf("ok   printenv PATH is empty (ash does not export PATH)\n");
            g_pass++;
        } else {
            printf("FAIL printenv PATH is empty (ash does not export PATH)\n");
            printf("       exit %d, output \"%s\"\n", rc, out);
            g_fail++;
        }
    }
}

/* ==================================================================
 * Mutating rows (--full only).
 *
 * Every row here creates something and removes it again, so a
 * completed --full run leaves the disk as it found it.
 * ================================================================== */

/*
 * expect_nonzero(): run argv and require a NONZERO exit status.  Used
 * for the row that checks `LS` refuses a file that is not there --
 * "it worked" is the pass, and exit 0 would be the failure.
 */
static void expect_nonzero(const char* label, char* const argv[]) {
    static char out[2048];
    out[0] = '\0';
    int rc = run_capture(argv, out, sizeof(out), 1);
    if (rc != 0) {
        printf("ok   %s (exit %d, as expected)\n", label, rc);
        g_pass++;
    } else {
        printf("FAIL %s: exited 0, expected nonzero\n", label);
        printf("       captured output:\n");
        const char* p = out;
        while (*p) {
            const char* nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            printf("         | %.*s\n", (int)len, p);
            if (!nl) break;
            p = nl + 1;
        }
        g_fail++;
    }
}

static void run_mutating(void) {
    /* --- file create / stat / remove -------------------------------- */
    {
        static const char* const w[] = { NULL };
        check4("touch /cfile", "/bin/busybox", "touch", "/cfile",
               NULL, w, 0);
    }
    {
        static const char* const w[] = { "cfile", NULL };
        check4("ls /cfile", "/usr/bin/LS.ELF", "/cfile", NULL, NULL,
               w, 0);
    }
    {
        static const char* const w[] = { NULL };
        check4("rm /cfile", "/bin/busybox", "rm", "/cfile", NULL, w, 0);
    }
    {
        /* LS on a file that is gone must fail. */
        char* argv[3];
        argv[0] = (char*)"/usr/bin/LS.ELF";
        argv[1] = (char*)"/cfile";
        argv[2] = NULL;
        expect_nonzero("ls /cfile refuses a missing file", argv);
    }

    /* --- directory create / remove ---------------------------------- */
    {
        static const char* const w[] = { NULL };
        check4("mkdir /cdir", "/bin/busybox", "mkdir", "/cdir",
               NULL, w, 0);
    }
    {
        static const char* const w[] = { NULL };
        check4("rmdir /cdir", "/bin/busybox", "rmdir", "/cdir",
               NULL, w, 0);
    }

    /* --- rm -r a tree ----------------------------------------------- */
    {
        static const char* const w[] = { NULL };
        check4("mkdir /ctree", "/bin/busybox", "mkdir", "/ctree",
               NULL, w, 0);
    }
    {
        static const char* const w[] = { NULL };
        check4("touch /ctree/a", "/bin/busybox", "touch", "/ctree/a",
               NULL, w, 0);
    }
    {
        static const char* const w[] = { NULL };
        check4("rm -r /ctree", "/bin/busybox", "rm", "-r", "/ctree",
               w, 0);
    }

    /* --- redirection ------------------------------------------------ */
    {
        static const char* const w[] = { NULL };
        check4("echo hi > /cout.txt", "/bin/busybox", "sh", "-c",
               "echo hi > /cout.txt", w, 0);
    }
    {
        static const char* const w[] = { "hi", NULL };
        check4("cat /cout.txt", "/usr/bin/CAT.ELF", "/cout.txt",
               NULL, NULL, w, 0);
    }
    {
        static const char* const w[] = { NULL };
        check4("rm /cout.txt", "/bin/busybox", "rm", "/cout.txt",
               NULL, w, 0);
    }

    /* --- pipeline --------------------------------------------------- */
    {
        static const char* const w[] = { "1", "3", NULL };
        check4("echo hi | wc", "/bin/busybox", "sh", "-c",
               "echo hi | wc", w, 0);
    }
}

/* ==================================================================
 * main.
 * ================================================================== */
int main(int argc, char** argv) {
    int full = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--full") == 0) full = 1;
    }

    printf("=== donix canary%s ===\n", full ? " (--full)" : "");

    run_readonly();

    if (full) {
        printf("--- mutating rows (--full) ---\n");
        run_mutating();
    } else {
        printf("--- mutating rows skipped (run `canary --full` to include) ---\n");
    }

    printf("---\n%d passed, %d failed\n", g_pass, g_fail);

    if (!full) {
        printf("\nManual rows, not covered here:\n");
        printf("  busybox ash      # interactive; then pwd, cd /bin, ls, exit\n");
        printf("  vi test          # fills screen; :wq; ./test\n");
    }

    return g_fail ? 1 : 0;
}
