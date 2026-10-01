/*
 * envp_helper -- the execve'd side of the envp regression test.
 *
 * This is a normal musl binary, not a raw-syscall program.  That
 * is the point: it proves that the C runtime's `environ` was
 * populated from the environment block the kernel wrote onto the
 * new stack.  getenv() walks the array __libc_start_main built
 * from the envp execve passed it; if sys_execve dropped envp
 * (pre-session-42 behavior), getenv returns NULL and this exits 2.
 *
 * Contract with envp_step1 (the driver, which forks and execve's
 * this program):
 *
 *   envp contains DONIX_ENVP_TEST=hello42
 *     -> exit 0    (getenv matched)
 *   envp contains DONIX_ENVP_TEST=hello42 and SECOND=two,
 *   and BOTH match
 *     -> exit 0
 *   envp contains DONIX_ENVP_TEST but the value is wrong
 *     -> exit 1
 *   getenv("DONIX_ENVP_TEST") returned NULL
 *     -> exit 2    (empty or missing environment)
 *
 * The driver distinguishes these; exit 2 is the "envp did not
 * survive execve" signal, which is the failure this test exists
 * to catch.
 *
 * Output goes to the console via printf.  The driver does NOT
 * parse it -- the driver reads the child's exit status, because
 * there is no way for the parent to capture the child's stdout
 * on donix (no pipe-capture helper in the tree yet).  The lines
 * below are for a human watching the run, not for the driver.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const char* v = getenv("DONIX_ENVP_TEST");
    if (!v) {
        printf("envp_helper: getenv(\"DONIX_ENVP_TEST\") -> (null)\n");
        return 2;
    }

    printf("envp_helper: DONIX_ENVP_TEST=%s\n", v);
    if (strcmp(v, "hello42") != 0) {
        printf("envp_helper: value mismatch, want hello42\n");
        return 1;
    }

    /*
     * The two-variable case.  SECOND is absent in the single-var
     * run and in the empty run, so its absence is not an error --
     * only a present-but-wrong value is.  The driver that sets
     * SECOND expects it to be here; the driver that does not,
     * does not care.
     */
    const char* s = getenv("SECOND");
    if (s) {
        printf("envp_helper: SECOND=%s\n", s);
        if (strcmp(s, "two") != 0) {
            printf("envp_helper: SECOND mismatch, want two\n");
            return 1;
        }
    }

    return 0;
}
