/*
 * sigstop_probe.c -- job control, kernel side: acceptance for
 * "SIGSTOP/SIGTSTP stop, SIGCONT resumes" (commit 3d9ede8).
 *
 * Shape:
 *
 *   child:
 *     write 'R' to pipe A          (handshake: about to stop)
 *     kill(getpid(), SIGSTOP)      (this call returns ONLY after SIGCONT)
 *     write 'D' to pipe B          (handshake: resumed)
 *     _exit(0)
 *
 *   parent:
 *     read 'R' from pipe A          (returns before the child stops)
 *     nanosleep(100 ms)             (parks; timer wakes it; gives the
 *                                    child time to reach its own stop)
 *     poll /proc/<child>/stat       (up to 20 x 10 ms; observational)
 *     kill(child, SIGCONT)          (unconditional -- T_seen does not gate)
 *     read 'D' from pipe B          (returns only after the child's kill
 *                                    syscall returned, i.e. after SIGCONT)
 *     waitpid                       (assert WIFEXITED && WEXITSTATUS == 0)
 *
 * Assertions (the probe's exit status):
 *   PASS = ('R' received) AND ('D' received) AND (child exited 0).
 *   FAIL = anything else.
 *
 * The 'T' observation is printed as T_seen=N.  It does NOT gate PASS:
 * a T-poll race would turn a kernel bug into a probe bug.  The
 * authoritative assertion is that the child's kill(SIGSTOP) syscall
 * returned (that is what 'D' proves) and the child exited 0.  If the
 * kernel is correct, T will be seen in most runs; a PASS with
 * T_seen=0 is the interesting case to notice.
 *
 * The parent must not be BLOCKED when the child stops: a BLOCKED
 * parent + STOPPED child + idle leaves nothing runnable.  The
 * nanosleep(100ms) parks the parent with BLOCK_KIND_NANOSLEEP; the
 * timer wakes it, and the child is stopped by then.  That is the
 * deterministic wait (a fixed usleep before the child stops would be
 * the race the previous session paid for).
 *
 * SIGSTOP is uncatchable, so no sigaction is installed in the child.
 * The child's kill is the syscall whose return is deferred; that is
 * exactly what commit 3d9ede8 makes possible.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

/* Read byte from fd, return 1 on success, 0 on failure. */
static int read_byte(int fd, char* out) {
    ssize_t n;
    do { n = read(fd, out, 1); } while (n < 0 && errno == EINTR);
    return (n == 1);
}

/* Write byte to fd. */
static int write_byte(int fd, char c) {
    ssize_t n;
    do { n = write(fd, &c, 1); } while (n < 0 && errno == EINTR);
    return (n == 1);
}

/* Read /proc/<pid>/stat and return the state char (field 3), or 0 on
 * failure.  /proc/<pid>/stat's shape is "pid (comm) state ..."; the
 * comm may contain spaces or ')', so parse from the LAST ')'. */
static char read_proc_state(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    char* rp = strrchr(buf, ')');
    if (!rp) return 0;
    if (rp[1] != ' ') return 0;
    return rp[2];
}

int main(void) {
    int ready_pipe[2];   /* child -> parent: 'R' (child about to stop) */
    int done_pipe[2];    /* child -> parent: 'D' (child resumed)      */

    if (pipe(ready_pipe) != 0) { perror("pipe ready"); return 2; }
    if (pipe(done_pipe)  != 0) { perror("pipe done");  return 2; }

    pid_t child = fork();
    if (child < 0) { perror("fork"); return 2; }

    if (child == 0) {
        close(ready_pipe[0]);
        close(done_pipe[0]);

        /* Handshake: we are about to raise SIGSTOP on ourselves. */
        if (!write_byte(ready_pipe[1], 'R')) _exit(3);
        close(ready_pipe[1]);

        /* The syscall whose return is deferred by the stop.
         * SIGSTOP is uncatchable, so its default action runs
         * through signal_default_action -> process_stop_and_yield.
         * It returns ONLY after SIGCONT has requeued this process. */
        if (kill(getpid(), SIGSTOP) != 0) {
            perror("child kill(SIGSTOP)");
            _exit(4);
        }

        /* Reached => kill(SIGSTOP) returned => SIGCONT was delivered
         * while we were STOPPED.  Tell the parent. */
        if (!write_byte(done_pipe[1], 'D')) _exit(5);
        close(done_pipe[1]);

        _exit(0);
    }

    /* ---- parent ---- */
    close(ready_pipe[1]);
    close(done_pipe[1]);

    char c;
    if (!read_byte(ready_pipe[0], &c) || c != 'R') {
        printf("parent: child did not signal ready\n");
        return 2;
    }
    close(ready_pipe[0]);
    printf("parent: child signaled ready; sleeping 100ms before polling\n");
    fflush(stdout);

    struct timespec ts100 = { .tv_sec = 0, .tv_nsec = 100000000L };
    struct timespec ts10  = { .tv_sec = 0, .tv_nsec =  10000000L };
    struct timespec rem;
    nanosleep(&ts100, &rem);

    int t_seen = 0;
    for (int i = 0; i < 20 && !t_seen; i++) {
        char sc = read_proc_state(child);
        if (sc == 'T') { t_seen = 1; break; }
        nanosleep(&ts10, &rem);
    }
    printf("parent: T_seen=%d\n", t_seen);
    fflush(stdout);

    /* Unconditional: continue the child whether or not T was seen. */
    if (kill(child, SIGCONT) != 0) {
        perror("parent kill(SIGCONT)");
        /* Fall through: the child may still exit if it was not
         * actually stopped; we will learn that from waitpid. */
    }

    /* Wait for the child's 'D' handshake.  This returns only when
     * the child's kill(SIGSTOP) syscall has returned.
     *
     * BOUNDED, not blocking.  A blocking read here turned the
     * "sys_kill does not requeue a STOPPED target for SIGCONT"
     * defect into a machine-wide hang: the parent blocked, the
     * child stayed STOPPED, idle ran hlt, nothing woke either.
     * The capture showed "parent: T_seen=1" and then nothing --
     * the parent was in this read.  A bounded wait fails the
     * probe with a clean diagnostic instead, and the failure
     * signature ("T_seen=1, resumed=0") names exactly the shape
     * the 02d commit fixed in sys_kill.  The 02c probe's own
     * contract should never be "hang on the way to the assertion."
     *
     * 200 x 10ms = 2s; on the success path 'D' arrives in
     * microseconds, so this changes nothing when the kernel is
     * correct. */
    int flags = fcntl(done_pipe[0], F_GETFL, 0);
    if (flags >= 0) fcntl(done_pipe[0], F_SETFL, flags | O_NONBLOCK);

    char d = 0;
    int got_d = 0;
    for (int i = 0; i < 200 && !got_d; i++) {
        if (read_byte(done_pipe[0], &d) && d == 'D') {
            got_d = 1;
            break;
        }
        struct timespec wait10 = { .tv_sec = 0, .tv_nsec = 10000000L };
        struct timespec rem10;
        nanosleep(&wait10, &rem10);
    }
    close(done_pipe[0]);
    printf("parent: child signaled resumed=%d\n", got_d);
    fflush(stdout);

    int st = 0;
    if (waitpid(child, &st, 0) < 0) { perror("waitpid"); return 2; }

    printf("parent: status=0x%x WIFEXITED=%d WEXITSTATUS=%d WIFSIGNALED=%d WTERMSIG=%d\n",
           st, WIFEXITED(st),
           WIFEXITED(st) ? WEXITSTATUS(st) : -1,
           WIFSIGNALED(st), WIFSIGNALED(st) ? WTERMSIG(st) : 0);
    fflush(stdout);

    if (WIFSIGNALED(st)) {
        printf("SIGSTOP-PROBE: FAIL (child killed by signal %d)\n",
               WTERMSIG(st));
        return 1;
    }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("SIGSTOP-PROBE: FAIL (child did not exit 0)\n");
        return 1;
    }
    if (!got_d) {
        printf("SIGSTOP-PROBE: FAIL (no resumed handshake)\n");
        return 1;
    }
    printf("SIGSTOP-PROBE: PASS (T_seen=%d; child's kill(SIGSTOP) "
           "returned after SIGCONT; child exited 0)\n", t_seen);
    return 0;
}
