/*
 * pipe7e_helper -- the execve'd side of the pipe7e reproducer.
 *
 * Its contract is: write a few bytes to stdout (which is the pipe
 * write end, wired by the parent), then exit 0.  It is the
 * smallest program that exercises the full pipe + exec + exit
 * path that the x=$(cmd) shape uses.
 *
 * It is deliberately separate from churn_helper.c, which only
 * returns 0 and moves no bytes through a pipe.  The shell rows
 * that crash (base64 hello, rev, fold, hexdump) all move data on
 * the pipe, so this helper does too.
 */
#include <unistd.h>

int main(void) {
    /* Write, ignore the return: if the pipe is gone, the parent's
     * read-to-EOF will see an empty pipe and report that as a
     * failure.  We do not want the helper itself to fail for a
     * reason that is not the bug. */
    write(1, "x\n", 2);
    return 0;
}
