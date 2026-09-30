## The kernel syscall name and the libc name differ

*Session 39 (`*at` work), userspace test. Not a bug -- a naming
convention that cost ten minutes.*

`newfstatat` is the **kernel** name: the syscall number 262 is
`__NR_newfstatat` in Linux's table, and the kernel handler is
`sys_newfstatat`.  The **libc** name is `fstatat` -- there is no
`newfstatat` function in musl's `<sys/stat.h>`, and a userspace
program that calls `newfstatat(...)` gets:

    error: implicit declaration of function 'newfstatat';
    did you mean 'fstatat'?

The two are the same syscall.  musl's `fstatat.c` calls
`__syscall(SYS_fstatat, ...)`, and `SYS_fstatat` is `#define`d to
`SYS_newfstatat` on x86_64 in `musl-src/src/internal/syscall.h`.
The kernel-side name reflects the number; the libc-side name
reflects the POSIX function.

**The rule.**  When writing a userspace test for a syscall, use the
**libc** name, not the kernel name.  `openat`, `fstatat`,
`unlinkat` -- these are the functions.  The kernel's `case
SYS_NEWFSTATAT:` is reached from `fstatat`, not from a
`newfstatat` function that does not exist.

Watch for the same shape elsewhere: `newfstat`, `newlstat`,
`newuname` are kernel names with no libc equivalent (the libc
names are `fstat`, `lstat`, `uname`).  If a compile fails with an
implicit declaration, the first thing to check is whether you are
calling the kernel name from userspace.

## When the count disagrees with the lines, suspect the test

*Session 39 (`*at` work), test bookkeeping. Not a kernel bug.*

A test printed `ok 1` through `ok 7` -- every line said the check
passed -- and then a summary line reading `FAILED (1 failures)`.
The natural reading is "one check passed its print but the count
disagrees; something is wrong."  What was actually wrong was the
test code: the success branch of check 7 incremented the failure
counter by mistake:

    if (fstatat(AT_FDCWD, "/hello-world.txt", &st, 0x4000) == -1 &&
        errno == EINVAL) {
        printf("ok 7: bad flag -> EINVAL\n");
        fails++;              /* <-- wrong; this branch is success */
    } else {
        printf("FAIL 7: bad flag did not give EINVAL\n");
        fails++;
    }

The kernel was correct: `fstatat(..., 0x4000)` returned `-EINVAL`
and errno was `EINVAL`.  The test's bookkeeping was not.

**The rule.**  When a summary count disagrees with the per-line
output, the *test* is the first suspect, not the code under test.
The lines are the record of what actually happened; the count is
derived.  A mismatch is almost always a bookkeeping bug: an
increment in the wrong branch, a counter not reset, a branch that
prints `ok` but does not increment the success count, or the
inverse.

**Also worth fixing when you find one:** reset `errno` to 0 before
each call whose result you are about to compare against a specific
`errno` value.  A stale `errno` from an earlier call can make a
check pass or fail for the wrong reason, and the failure looks
like a kernel bug when it is a test bug.  The `errno = 0;` at the
top of each check is cheap insurance:

    errno = 0;
    int r = fstatat(...);
    if (r == -1 && errno == EINVAL) { /* real pass */ }

Capture the return value too -- if the check fails, printing
`r=%d errno=%d (%s)` tells you exactly what the kernel returned
rather than leaving you to guess.
