## A consumer inferred from behavior is not a consumer

*Session 40 (`unlinkat`), commit `20261001-unlinkat`. Not a kernel
bug -- a wrong claim in a commit message, caught before the commit
was made.*

`unlinkat(2)` was added because busybox `rm -r` was believed to
need it.  The reasoning was: `rm -r` walks a directory and removes
entries by name; on Linux that is `unlinkat(dirfd, name, ...)`; the
`openat`/`newfstatat` work in session 39 was for `find`, and this
would be the same shape for `rm`.  It is a plausible story.

It is also false.  A tree-wide grep settles it:

    grep -rn "unlinkat" third_party/busybox/ \
        --include='*.c' --include='*.h' | grep -v testsuite

returns nothing.  `rm -r` is `libbb/remove_file.c`, and what it
actually does is:

    lstat(path, &st);              // syscall 6
    if (S_ISDIR(st.st_mode)) {
        dp = opendir(path);
        while ((d = readdir(dp))) {
            new_path = concat_subpath_file(path, d->d_name);
            remove_file(new_path, flags);   // RECURSIVE, by string
        }
        rmdir(path);               // syscall 84
    } else {
        unlink(path);              // syscall 87
    }

`concat_subpath_file` builds a path string; the recursion carries
the full path down.  No dirfd is opened, no `*at` syscall is made.
`find` uses `openat` + `newfstatat` because it *recurses*; `rm -r`
uses neither because it does not need a dirfd to remove a file it
can name.

**The rule.**  A commit message that names what a feature is *for*
is making a factual claim about another program's source.  Read
that source before writing the sentence.  "The syscall busybox
`rm -r` reaches" is checkable in one grep and was checked only
after the sentence was drafted, at which point it was wrong.

**The tell, and why it was easy to miss.**  `rm -r` *worked*.
Running it on a tree succeeded, which felt like confirmation.  But
success was consistent with both stories -- `rm -r` via `unlinkat`,
and `rm -r` via `unlink`+`rmdir` -- and only one of them was true.
Behavior that two mechanisms both explain is not evidence for
either.  The confirmation was in the source, not in the run.

**What made `rm -r` work, for the record.**  Not `unlinkat`.  It
was the `unlink`/`rmdir` type check in the same commit: before it,
`f_unlink` accepted files and directories alike, so `remove_file`'s
`lstat`-then-branch was advisory -- either branch produced the same
result.  After the check, the branch is load-bearing: `unlink` on a
directory is `-EISDIR`, `rmdir` on a file is `-ENOTDIR`.
`remove_file` was already correct; the kernel started enforcing
what it assumed.  The feature that mattered and the feature that
was *believed* to matter were different.

**A companion failure, same session.**  The first commit block for
this work was written against an assumed repository state and
included `git commit` and `git tag` in the same paste as the build,
with the test *after*.  The order was wrong: the commit should not
exist until the test is green.  The second block was an `--amend`
for a commit that had never been made -- the earlier `git commit`
had not run, so there was nothing to amend, and the amend would
have folded the feature into the *refactor* commit.  Both were
caught by asking for `git status` and `git log` before the block
rather than after.  The fix, now a rule: **the state check comes
before the command block, not after.**

**Where this shape recurs.**  Same family as "A wrong constant
propagated because it was consistent with itself" (session 39):
one assertion, copied or inferred, never checked against the thing
that defines truth.  The PIT case was a wrong *number*; this is a
wrong *causal claim*.  Both were caught by going to the source --
`grep` for the number, `grep` for the symbol -- and both had a
plausible story that made the check feel unnecessary.

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

## A wrong constant propagated because it was consistent with itself

*Session 39 (framebuffer cursor).  Four instances of the same
wrong number in one session, none of them verified against the
source of truth.*

The PIT rate was asserted as **500 Hz** in a comment on `sys_poll`
in `user_syscall.c`:

    * deadline (g_ticks is available; PIT frequency is 500 Hz so
    * 1 tick == 2 ms) and loop on hlt until the deadline or

The actual rate is **100 Hz** (`pit_init(100)` in `kmain.c`), so
1 tick is 10 ms.  The 500 Hz figure in that comment was inferred,
at some earlier date, from the divisor math in `pit_init`:

    uint32_t divisor = 1193180 / freq;

`1193180 / 500 = 2386`, a clean divisor; `1193180 / 100 = 11931`,
also a clean divisor.  Both are plausible.  Nobody checked the
argument at the call site.

Then, in this session, three more comments were written that
asserted 500 Hz:

- `vga.c`, above the cursor state: "the PIT runs at 500 Hz (see
  pit_init).  250 ticks = 500 ms".
- `vga.c`, above `vga_cursor_tick`: "Called on every PIT tick
  (500 Hz)".
- a `#define CURSOR_BLINK_TICKS 250 /* 500 ms at 500 Hz */`.

The pattern is visible in how the second and third appeared: the
first was read, it was consistent with the divisor math the author
had also looked at, and the number was copied forward.  Nothing
about the number was *verified* -- nothing ran a `grep` for
`pit_init` and read the argument.  All three were wrong, and the
`CURSOR_BLINK_TICKS` value had to be retuned by eye (to 75, then
to 50) before anyone asked why the computed rate did not match
what was on screen.

The correction came from one command:

    grep -rn "pit_init" 04_kernel_64bit/

which printed, among other lines:

    04_kernel_64bit/kmain.c:1300:    pit_init(100);

That is the source of truth.  It is the only place the argument
appears.  Everything else in the tree was an assertion of a rate,
and every assertion was wrong.

**The rule.** A fact asserted in several places is not evidence of
anything; it is one assertion, copied.  When a comment, a define,
or a doc says a hardware or timing constant -- a frequency, a
resolution, a rate, a bit width, a pin number -- go to the call
site, the definition, or the hardware, and read it there.  Do not
verify it against another comment; the other comment may be the
same inference made twice.

**A related tell.** In this session the wrong number survived
review because it matched *the math that was used to derive it*.
The divisor formula is correct; the argument to it was guessed.
When a value has a derivation, the derivation is not the check.
The check is the input to the derivation.

**Where this shape recurs.** This is the same family as "Assumed
byte-order conventions are the same shape" (session 38): a
decision that was plausible, matched something else nearby, and
was never tested against the thing that defines truth.  The
byte-order case was caught by drawing a test pattern and looking
at it; this case was caught by a `grep`.  Both are cheap and both
are the only thing that actually settles the question.
