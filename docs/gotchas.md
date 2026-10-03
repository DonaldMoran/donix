## A case in a switch is not reached if an earlier guard refuses the input

*Session 44 (`fstat(0)` on a console sentinel), commit
`20261002-dev-console-tty`.  A pre-existing bug the new test found
-- a correct case, one line below the guard that made it
unreachable.*

`sys_fstat_body` has a `FILE_KIND_CONSOLE` case that fills a
`kernel_stat_t` for a console sentinel -- the slot on fds 0, 1, and
2.  The case is correct and has been there since console sentinels
were introduced.  **It had never run.**

The function begins:

    static long sys_fstat_body(int fd, void* user_stat) {
        ...
        file_slot_t* slot = get_file_slot(fd, 0);
        if (!slot) return -(long)EBADF_;
        ...
        if (slot->kind == FILE_KIND_CONSOLE) { ... }

`get_file_slot` refuses fds below 3:

    static file_slot_t* get_file_slot(int fd, uint32_t kind) {
        if (!self || fd < 3 || fd >= MAX_PROCESS_FILES) return NULL;

So `fstat(0)` returned `-EBADF` at the second line, **before the
`FILE_KIND_CONSOLE` case could be reached.**  The case was not
wrong; it was dead.  `fstat(0)` -- a thing every Unix does --
failed for the entire life of console sentinels, and nothing
noticed because nothing called `fstat` on fd 0.

**What made it matter in session 44.**  `ttyname_r(3)`'s gate 3b
compares `stat("/dev/console")`'s `(st_dev, st_ino)` against
`fstat(0)`'s.  `stat` was right; `fstat(0)` was `-EBADF`.  So the
comparison failed, `ttyname_r` returned an error, and `tty` printed
`not a tty` -- even after `/dev/console` and the readlink were both
in place.  The new `proc_fd` test reported exactly one failure:

    ok   stat(/dev/console) succeeds
    ok   stat(/dev/console) reports S_IFCHR
    FAIL stat(/dev/console) matches fstat(0) on (st_dev, st_ino)

**The fix is one line** -- `sys_fstat_body` uses `get_file_slot_any`
instead of `get_file_slot`, the same relaxation `sys_close`,
`sys_read`, `sys_write`, `sys_dup2`, and `sys_fcntl` already have,
for the same reason: fds 0/1/2 are real open files.  With it,
`fstat(0)` reaches the console case and reports `(1, 1)`, the pair
`stat("/dev/console")` reports, and `ttyname_r`'s gate 3b passes.

**The rule.**  A `switch` on a value, or a chain of `if`s, does not
run in isolation -- it runs *after everything above it in the
function*.  When a case "should" fire and does not, the first thing
to read is not the case; it is what runs before it.  A guard, an
early return, or a lookup that refuses the input will make every
case below it unreachable for that input, and the case will look
perfectly correct while never executing.

**The tell.**  A case in a function that handles several kinds, for
a kind the function is *supposed* to handle, with no evidence it
has ever run.  In this instance the evidence was a test that
failed on a comparison the case existed to satisfy.  In general:
trace the input from the top of the function and find where it
stops.

**Where this shape recurs.**  Same family as "A shim's dead code is
only dead if you watch it not run" (session 42): code that is
present, correct, and never reached.  The shim case was a *branch*
whose condition could not be true; this is a *case* whose input was
filtered out one line earlier.  Both produce a function that looks
right and behaves as if the code were absent, and both were found
by running the path and watching it fail to do what the code said.

## Placing a function near its conceptual neighbors does not place it after its callees

*Session 44 (`proc_readlink`), build error, caught before any test
ran.  A new function put next to its theme rather than checked
against its call graph.*

`proc_readlink` was written for the `/proc/self/fd/N` readlink
target.  Its natural home, by theme, was next to `proc_lookup` --
both are the PROC backend's functions, and `proc_lookup` is at line
570.  So `proc_readlink` went at line 649, right after it.

`proc_readlink` calls `safe_copy_to_user`.  `safe_copy_to_user` is
**defined at line 1371**, later in the file, and there was no
forward declaration for it above 649 -- because until this commit,
nothing above 1371 called it directly.

The compiler:

    user_syscall.c:684:9: error: call to undeclared function
        'safe_copy_to_user'; ISO C99 and later do not support
        implicit function declarations

    user_syscall.c:1371:12: error: static declaration of
        'safe_copy_to_user' follows non-static declaration

Two errors, one cause: a caller placed above its callee with no
forward declaration.  **The compiler caught it, no test ran, no
commit was made** -- which is the good kind of failure.  But it
would have been avoided by checking what the new function calls
before choosing where to put it.

**The fix.**  Add `safe_copy_to_user` to the forward-declaration
block near the top of the file, where `f_stat_with_retry`,
`sys_fstat_body`, `get_file_slot_any`, and `access_resolved`
already live for exactly this reason:

    /* Defined below, in the safe-copy section.  proc_readlink
     * (above that definition) calls it. */
    static int safe_copy_to_user(void* user_dest,
                                 const void* kernel_src,
                                 size_t count);

**The rule.**  Where a function goes is a question about its
**call graph**, not its **theme**.  Before placing a new function,
list what it calls and confirm each is declared above the chosen
line.  A function's neighbors in the file are the functions it
*relates to*; they are not necessarily the functions it *depends
on*.

**The tell.**  A new function placed next to its subsystem's other
functions, in a file large enough that "later" is thousands of
lines away.  `user_syscall.c` is ~6500 lines; anything placed in
its first third calls only what is declared in the first third, or
needs a forward declaration.  The forward-declaration block exists
because this has happened before -- read it, and add to it.

**Where this shape recurs.**  Same family as "The kernel syscall
name and the libc name differ" (session 39): a compile-time
question (does this name resolve here?) answered by convention
(put it with its friends) rather than by the rule that decides it
(is it declared above?).  The fix in both is a one-line addition
to a declaration block, and the lesson in both is that the
convention is not the check.

## A fix can make an earlier branch unreachable

*Session 44 (`sys_execve`'s `"0:"` retry), commit
`20261002-execve-seam`.  Dead code that was not merely unused --
impossible to reach.*

`sys_execve` had two attempts to open the program file: (a) the
path as given, stripped for FatFs, and (b) `"0:" + path` if (a)
failed and the path had no drive prefix.  Attempt (b) was
documented as "the remaining shim" -- the one piece of path
handling the kernel still did on execve's behalf.

**Attempt (b) never ran.**  Its condition was:

    if (!has_drive && exec_path[0] == '/') {

but `exec_path` was produced by

    strip_dot_prefix(exec_path);

**one step earlier**, and `strip_dot_prefix` removes a leading `/`.
So for every real path, `exec_path[0]` was `'u'` or `'b'` or the
first letter of a bare name -- never `/`.  The condition was false
by construction, and the `"0:"` branch below it was unreachable.

**Attempt (a) is what always worked.**  FatFs has no cwd, so it
walks every path from the drive root, and `usr/bin/HELLO` -- the
stripped form -- is a valid path there.  The first `f_open`
succeeded, the retry was never entered, and `execve` worked for
every program on the image.

**So the retry was dead because a *later-added fix* solved the
problem it was written for.**  `strip_dot_prefix` was added to
normalize `./script.sh` and absolute paths; it happens to remove
the very leading `/` the retry checked for.  The retry was correct
when written and became unreachable when the normalization was
added above it -- and nothing noticed, because "unused by current
callers" and "impossible to reach" look identical from the
outside.

**The rule.**  Dead code is not one thing.  *Unused* code has no
caller; *unreachable* code has a caller whose path can never
satisfy the guard.  The second is worse, because a reader looking
for a consumer sees one -- the `sys_execve` function itself -- and
concludes the code is live.  When removing a branch, check not
whether it is *called* but whether its *condition* can be true
given everything that runs before it.

**The tell.**  A guard that tests a property of a variable, where
the variable was just transformed by something above it.  Read
what happened to the variable between its assignment and the
guard.  If the transformation makes the guard's condition
impossible, the branch below is dead, whatever its comment says.

**Where this shape recurs.**  Same family as "A test can encode an
earlier version's behavior" (session 43) and "A shim's dead code is
only dead if you watch it not run" (session 42).  In all three, an
artifact -- a branch, a test, a helper -- was correct when written
and false later, and the staleness was invisible because the
artifact was never *run* against the current state.  The
difference here is that the change that made it stale was a
*fix*, not a regression; that is the case people least expect,
because "I just fixed something" does not feel like "I may have
broken a branch."  It can.  Check the branches downstream of a
normalization.

## A test can encode an earlier version's behavior, and it will hang rather than fail

*Session 43 (`pipe_step1`), commit `20261002-pipe-step1`.  Not a
kernel bug -- a test that had been correct six sessions earlier,
and went stale when the behavior it asserted changed.*

`pipe_step1` asserted that a read on an empty pipe returns
`-EAGAIN` and that a write to a full pipe returns `-EAGAIN`.  That
was Step-1 semantics, from session 36, when pipes were
non-blocking.  Steps 2 and 3 made read and write **block**: a
reader with a live writer sleeps in `hlt` until data arrives, and
a writer with a live reader sleeps until space frees.

**Re-running the old assertions did not fail.  It hung.**  The
test wrote 5 bytes, read 5 bytes, then read again expecting
`-EAGAIN` -- but `writers_open` was still 1, so the kernel
blocked the reader, and nothing in a non-interactive test would
ever wake it.  A test that hangs names nothing: no assertion is
reported, no exit code is produced, and the suite stops at the
point of the hang with no indication of which check was wrong.

A stale test that *fails* is a message.  A stale test that *hangs*
is a dead end.

**How the staleness was found.**  `open-issues.md`'s test-design
notes carry the rule:

> **Pipe regression suite is not a canary.**  `pipe_step1` …
> `pipe_step3b` fork and take seconds; run them when changing
> `sys_read`/`sys_write`/`sys_close`/`put_file_slot`/`sys_fork`/
> `sys_pipe` or adding a `FILE_KIND_*`, but not as part of the boot
> canary.

Session 43 added a `FILE_KIND_*` (`FILE_KIND_DEV_NULL`) and
touched `sys_read` and `sys_write`, so the rule applied.  The
suite was run for the first time since session 36, and
`pipe_step1` hung on the third check.  **The rule was right; it
had simply not been executed for six sessions.**  A rule in a doc
that is not run is the same as no rule.

**The fix.**  The EAGAIN assertions are removed.  The empty/full
behavior is tested where it can be tested safely -- by tests that
fork a peer, so a blocked process has something to wake it:

- `pipe_step2` -- blocking + directed wake, forks a peer.
- `pipe_step3` -- EOF, `-EPIPE`, dup-aware closed-end counts.
- `pipe_step3b` -- wake on the exit path.

`pipe_step1` is now the fork-free smoke test: create a pipe, move
bytes through it, close it, re-close for `EBADF`.  Checks that
cannot block.

**The rule.**  When a syscall's behavior changes from non-blocking
to blocking, every test asserting the old non-blocking behavior
becomes a hang, not a failure.  A behavioral change that turns a
return value into a wait is the one kind of change a test suite
cannot report: the test does not disagree, it stops.  So:

> **A test suite that has not been run since a semantic change is
> an unknown, and a hang is how the unknown presents.**  Run the
> suite the change's own rule names, or the staleness is invisible
> until it blocks a session.

**Where this shape recurs.**  Same family as "A fix with no test
is indistinguishable from an unfixed defect" (session 43, the
`readlink` entry): a test or a doc asserting a state the code no
longer has.  The `readlink` case was a *fix* believed absent when
it was present; this is a *behavior* believed present when it had
changed.  Both produced wasted motion -- rounds spent looking for
a defect that was fixed, a suite run that deadlocked on an
assertion that no longer described the kernel.  The check is the
same: read the code before trusting the test or the doc.

**A related tell.**  A test file's header comment that says "Step
1" or "non-blocking" is describing the state at the time it was
written.  When the behavior the header describes has changed, the
header is a claim, and re-running the file is the check.  In this
case the header still said "This implementation ... does NOT
block: read on an empty pipe and write on a full pipe return
-EAGAIN" -- a sentence that was true when written and false six
sessions later, and was the exact reason the test hung.

## A fix with no test is indistinguishable from an unfixed defect

*Session 43 (`readlink` errno), commit `20261002-readlink-test`.
Not a kernel bug -- a stale claim in three docs, and a fix that
had been in the tree, untested, since session 42.*

`open-issues.md` item 8 described a defect: `sys_readlink` returns
`-EINVAL` for every input, including paths that do not exist.
Linux distinguishes `-ENOENT` (the path does not exist) from
`-EINVAL` (the path exists but is not a symlink).  The item
carried a described fix -- "resolve the path first; return
`-ENOENT` if it does not resolve, `-EINVAL` if it does" -- and a
required new regression test.

**The fix was already in the tree.**  `sys_readlink` had been
resolving the path since the session-42 readlink commit:

    long exists = access_resolved(resolved);
    if (exists != 0) return exists;   /* -ENOENT */
    ...
    return -(long)EINVAL_;

That is exactly the described fix, written and compiled.  The
item, the handoff, and the ROADMAP all still said it was open.

**Why nothing noticed.**  No applet in the current set calls
`readlink(2)`.  The only consumer is musl's `ttyname(3)`, which
tries `readlink("/proc/self/fd/N")` then walks `/dev` -- and
neither `/proc` nor `/dev` exists, so `ttyname` returns NULL
either way.  It can never distinguish the two errnos, so it could
never tell whether the kernel's answer was right.  A fix with no
consumer has no test, and a fix with no test is a fix nobody can
confirm.

**The session that found it went looking for the fix.**  Session
43 opened `open-issues.md`, read item 8, and started scheduling a
kernel change that had already been made.  The rounds spent
"verifying state" were spent looking for the source of a defect
that was not present.

**How the evidence was actually in hand, and read past.**  The
session-43 `realpath` trace had shown that libbb's
`xmalloc_readlink` collapses every `readlink` error to NULL before
any branch can distinguish `ENOENT` from `EINVAL`.  That collapse
is only *observable* if the kernel is returning the two errnos
differently -- if the kernel returned `-EINVAL` for everything, as
item 8 claimed, the collapse would have been a no-op and the
`/nonexistent` fallback branch in `xmalloc_realpath_coreutils`
would have taken the same path either way.  The trace was
evidence the fix was in.  It was written up as "item 8 does not
affect realpath's output," which is true, and stopped there.

**The rule.**  A doc entry that describes a defect and its fix is
a claim that the defect is *currently present*.  Before acting on
it, check the code.  If the fix is there, the entry is stale, and
the work is to write the test that proves it and delete the entry
-- not to write the fix again.

    grep -n "sys_readlink" 04_kernel_64bit/user_syscall.c

One command.  The function body answers whether the fix is in.

**The test that closed it.**  `readlink_errno.c` calls
`readlink(2)` directly, since no applet does, and asserts both
answers:

    readlink("/nonexistent")     -> -1, errno == ENOENT
    readlink("/usr/bin/HELLO")   -> -1, errno == EINVAL
    readlink("/")                -> -1, errno == EINVAL

All three pass.  Item 8 is closed with a run behind the claim,
not a reading.

**Where this shape recurs.**  Same family as "A consumer inferred
from behavior is not a consumer" (session 40), in the opposite
direction.  That entry is about a *feature* believed to have a
consumer it did not have.  This one is about a *defect* believed
to be present when the fix had already landed.  Both are claims
about the state of the source that were never checked against the
source, and both produced wasted motion -- a syscall written for a
caller that never made it, a fix looked for that was already
there.  The check is the same in both: read the source before
writing the sentence.

**A related tell.**  An open-issues entry that names its own fix
is one step away from being stale.  If the entry says "the fix is
X," someone may already have done X, and the entry may be the only
thing still saying otherwise.  Items that name a fix are worth a
`grep` before they are worth a work session.

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

## A syscall argument the caller did not set holds the previous syscall's return value

*Session 41 (`faccessat`), commit `20261001-faccessat`. Not a kernel
bug in the end -- a validation that should not have been there.*

`sys_faccessat` was rewritten to route through `resolve_at` and, by
analogy with `sys_newfstatat` and `sys_unlinkat`, to validate its
`flags` argument: unknown bits → `-EINVAL`.  `at_step1` section 8
then failed:

    [faccessat] dirfd=3 flags=0x00000000FFFFFFEA
    FAIL 8: faccessat(dfd, "busybox"): Invalid argument

`0xFFFFFFEA` is `-22`.  It is not random garbage and it is not
uninitialized memory.  It is the **return value of the previous
syscall** -- section 7's bad-flag `fstatat`, which returns `-EINVAL`
-- still sitting in `%r10`, the register the kernel reads as the
fourth syscall argument.  Section 9 showed the same thing with a
different leftover: `flags=0x1`, the return of whatever ran before.

**Why the register was stale.**  musl calls this syscall with
**three** arguments:

    third_party/musl-src/src/unistd/faccessat.c:
        return syscall(SYS_faccessat, fd, filename, amode);

The kernel's syscall entry reads `%rdi`, `%rsi`, `%rdx`, `%r10`,
`%r8`, `%r9` for arguments 1..6.  A three-argument call does not
write `%r10`, so it holds whatever the last thing to use it left
behind -- and on this path that is the previous syscall's return
value, because the syscall-return path puts the result in `%rax`
but nothing clears `%r10`.

So the failure was **deterministic**, not flaky: `faccessat` after
a call that returned `-EINVAL` always saw `0xFFFFFFEA`; after a call
that returned `1` always saw `0x1`.  That is why the trace showed a
clean `-22` rather than noise.

**The rule.**  Before validating a syscall argument, find out
whether the caller's wrapper actually sets it.  Read the libc
source (or the syscall's own kernel entry comment) and count the
arguments.  **Do not add a validation because a sibling syscall has
one** -- the sibling may be called differently.

**The specific asymmetry, for the record.**  `faccessat` and
`utimensat` are both `*at` syscalls, both now route through
`resolve_at`, and they differ on exactly this point:

- `sys_faccessat` does **not** validate flags.  musl calls it with
  three arguments.  Linux's `faccessat(2)` does not validate flags
  either; only `faccessat2(2)` (439) does, and donix does not
  implement 439.
- `sys_utimensat` **does** validate flags.  musl calls it with four
  (the `#else` branch of `third_party/musl-src/src/stat/utimensat.c`,
  which is what runs on x86_64 -- no 32-bit `time_t`, so the
  `_time64` path is compiled out).  The fourth register is set, so
  a mask is safe.

The difference is not a style choice.  It is **which one musl passes
four arguments to**, read from musl's source.

**Where this shape recurs.**  Same family as "A wrong constant
propagated because it was consistent with itself" (session 39) and
"A consumer inferred from behavior is not a consumer" (session 40):
a decision that was plausible, matched something nearby, and was
never tested against the thing that defines truth.  The PIT case
was a wrong *number*, the `unlinkat` case a wrong *causal claim*,
this one a wrong *assumption about the ABI*.  All three were caught
by going to the source -- `grep` for the number, `grep` for the
symbol, `cat` for the caller -- and all three had a plausible story
that made the check feel unnecessary.

**A related tell.**  A validation added "for consistency" is a
validation whose correctness depends on every caller, not on the
value being validated.  If the goal is consistency, the honest move
is to check whether the consistency holds -- here, whether both
syscalls are called the same way -- before copying the pattern.

## The incremental kernel build can silently skip

*Session 41, build hygiene. Not a kernel bug -- a make dependency
that does not do what it looks like it does.*

After editing `04_kernel_64bit/user_syscall.c`, `make -C
04_kernel_64bit` printed:

    make: Entering directory '/home/noneya/code/donix/04_kernel_64bit'
    make: Nothing to be done for 'all'.
    make: Leaving directory '/home/noneya/code/donix/04_kernel_64bit'

with the source already saved.  The build that followed staged an
image whose `kernel.bin` did **not** contain the edit, and the
result was a test failure that looked like a code bug:

    FAIL 8: faccessat(dfd, "busybox"): Invalid argument

The reason: `kernel.bin`'s mtime was **newer** than
`user_syscall.c`'s.  `make` compares timestamps, and a newer output
than input means "up to date."  The edit and a previous build had
landed close enough together that the output's timestamp was later,
so make skipped the rebuild -- and the image was built against a
stale `kernel.bin`.

**Why it is easy to miss.**  The build *succeeded*.  No error, no
warning, no "nothing to do" that looks wrong on its own.  The only
symptom is that the running kernel does not match the source, which
presents as a code bug -- and sends you debugging code that is not
on the machine.

**The reliable path.**  `./run` does `make clean` first, so the
rebuild always happens:

    make clean && make FAT_CONFIG=single && \
        make -C 05_boot_kernel64 hdd-single.img && \
        make -C 05_boot_kernel64 run-single

**When an incremental build is safe.**  After a `make clean` in the
same invocation.  A bare `make -C 04_kernel_64bit` is only reliable
if the change is known to be older than the last link -- which is
not something to rely on.

**The tell.**  If a test fails in a way that suggests the source was
not compiled in, and the source edit was recent, suspect the
incremental build before the code.  Compare mtimes:

    stat -c '%y %n' 04_kernel_64bit/user_syscall.c \
                    04_kernel_64bit/kernel.bin

If `kernel.bin` is newer than a source you just edited, the build
skipped and the image is stale.

**Where this shape recurs.**  Same family as the PIT-constant and
`unlinkat` entries: a check that *looks* like it is doing the right
thing -- "make says up to date" / "the number is consistent" / "the
feature works" -- but is not checking the thing that matters.
Timestamps say nothing about content; a constant matching a
derivation says nothing about the input; a feature working says
nothing about *why*.  In each case the fix is to check the source
of truth -- mtimes against the edit, the call site for the constant,
the caller's source for the feature.

## The kernel stack is 16 KB; do not put a large scratch buffer on it

*Session 42 (envp).  A sizing decision that would have become a
stack overflow.*

`sys_execve` had to snapshot the caller's `envp` array and strings
out of the old address space before tearing it down, the way it
already snapshots `argv`.  The obvious shape — mirror the argv
snapshot, a stack array `char envp_scratch[EXEC_MAX_ENVC]
[EXEC_MAX_ARG_LEN]` — would have been:

    EXEC_MAX_ENVC   = 64
    EXEC_MAX_ARG_LEN = 256
    sizeof(envp_scratch) = 64 * 256 = 16384 bytes

**16 KB, on a kernel stack that is 16 KB.**  From `process.h`:

    #define PROC_STACK_SIZE  16384   // 16KB: syscall entry +
                                     // nested timer frame +
                                     // sys_read blocking headroom

That comment is not decoration; the 16 KB is already committed to
three specific things, and `sys_execve` adds its own frame on top
(`path`, `exec_path`, `argv_scratch[16][256]` = 4 KB, `proc_name`,
the ELF-validation locals).  A 16 KB scratch array would put the
frame well past the top of the stack.  The failure mode is not a
clean fault — it corrupts whatever is below the stack, which is the
syscall-entry frame or the adjacent kernel stack slot.

**The fix, and the rule.**  The envp snapshot is `kmalloc`'d, like
`elf_buf`, and freed on every exit path.  The argv snapshot stayed
a stack array because 4 KB is affordable; the envp one did not,
because 16 KB is not.

> **Before adding a scratch buffer to a syscall, check
> `PROC_STACK_SIZE` in `process.h` and count the bytes.**  Anything
> over a couple of KB belongs in a `kmalloc`'d buffer, not on the
> stack.  The kernel stack is 16 KB and is shared with the
> syscall-entry frame and any nested interrupt frame.

**How to size it.**  `PROC_STACK_SIZE` is one number in one place.
Grep it:

    grep -rn "PROC_STACK_SIZE" 04_kernel_64bit/

and read the comment on it — the headroom it describes is for
specific existing consumers, not slack for new ones.

**Where this shape recurs.**  Same family as "A wrong constant
propagated because it was consistent with itself" (session 39): a
number that was plausible, matched something nearby (the argv
snapshot's 4 KB), and was never checked against the limit that
defines truth (the 16 KB stack).  Here the check was cheap — one
grep for `PROC_STACK_SIZE` — and it was done before the code was
written, not after a crash.  The rule is the same: go to the
definition and read it; do not infer a limit from a neighboring
example.

## A shim's dead code is only dead if you watch it not run

*Session 42 (the layout move and the shim removal).  Three times in
one session, "this code has no consumer" was wrong.*

The session removed a chunk of `sys_execve` that guessed where a
bare command name lived: uppercase it, append `.ELF`, try it at the
root and in `/bin`.  The reasoning for removing it was sound on its
face — the binaries had moved to `/usr/bin`, the suffix was gone,
so every sub-attempt of the guess would find nothing.  **The
reasoning was right.  The way it was reached was wrong, three
times, and each time the correction came from running the thing,
not from thinking about it.**

### One — the shim was not dead, and the canary could not see it

The first claim was "the bare-name attempt is dead code, the canary
passes without it."  The canary *did* pass.  But every canary row
goes through `canary.c`'s own full-path `execve` calls or through
ash's built-in applets — **neither of which uses the kernel's
bare-name guess.**  The canary was passing without exercising the
code the claim was about.

The counter-evidence came from a single experiment: copy a binary to
the root as `HELLO.ELF`, type `hello` at the ash prompt, and watch
the serial log.

    $ hello
    sys_execve: pid=11 entry=0x400221 argc=1 ... (hello)
    hello from donix (musl)

The log shows `(hello)` — the **bare name**, not a resolved path.
Ash's `execvp`, finding no `PATH` entry (ash does not export
`PATH`), fell through to `execve("hello")`, and the kernel's
bare-name guess is what found `/HELLO.ELF`.  **The guess had a
consumer the canary could not reach.**

### Two — the helper had a caller the deletion missed

The removal deleted the two helper functions.  The build failed:

    error: call to undeclared function 'exec_resolve_bare_name'

Line 2417, in `f_stat_with_retry` — the **stat** path, not the exec
path.  It used the same helper to retry a failed `f_stat` as
`0:/NAME.ELF`, and it was added in session 22 for ash's
`find_execable` probe.  The deletion was written against a
remembered version of the file and missed the caller.

The compiler caught it.  **That is the cheap case** — the failure
was loud and immediate.  The expensive version of the same mistake
is deleting code whose caller fails silently at runtime.

### Three — the guess was dead this time, and one run proved it

The retry *looked* load-bearing: ash probes `PATH` with
`access(name, X_OK)` before exec'ing, and that probe goes through
`f_stat_with_retry`.  If the retry were doing the work, deleting it
would break every `PATH` lookup and no command would run from ash.

The evidence it was dead: **ash's `PATH` includes `/usr/bin`, the
binaries are in `/usr/bin`, and FatFs resolves a multi-component
path.**  So `access("/usr/bin/ls", X_OK)` succeeds directly and the
retry never fires.  The proof was the canary's `ls` rows passing
**from ash** — they go through `find_execable` → `access` →
`f_stat_with_retry`, and they found every binary.

This time the reasoning was sound.  But it was the same shape as
the two wrong calls before it, and the thing that made it different
was that the run confirmed it.

### The rule

> **Before deleting code as "no consumer," make the consumer
> attempt to use it and watch.**  Passing tests are not evidence
> the code is unused — they are evidence the tests do not reach it.
> A dead-code claim needs a run where the code *fails to be
> needed*, not a run where it is simply not noticed.

**The specific tell.**  Three claims, three times, the same error:
the evidence was "it works without this" when the real question was
"does the path that uses this still work."  A canary that passes
tells you what *is* covered.  It says nothing about what is not.

### Where this shape recurs

Same family as "A consumer inferred from behavior is not a
consumer" (session 40) and "A syscall argument the caller did not
set holds the previous syscall's return value" (session 41).  Each
was a plausible claim that matched something nearby and was never
tested against the thing that defines truth.  The `unlinkat` case
was a wrong *claim about busybox's source*; the `faccessat` case a
wrong *assumption about the ABI*; this one a wrong *claim about
reachability*.  In every case the fix was to go to the source —
`grep` for the caller, `cat` for the wrapper, **run the path and
watch the log** — and in every case the plausible story made the
check feel unnecessary.

**A related note.**  Deleting code is the one edit a test cannot
guard.  An addition is caught by a test that exercises the new
path.  A deletion is caught only by a test that exercises the
*old* path — and if the old path is genuinely dead, no test
exercises it, which is exactly when the deletion is safe and
exactly when there is no test to say so.  That asymmetry is why
deletion wants a run, not a review.


## An input-only `syscall` asm block does not tell GCC that `%rax` is overwritten

*Session 42 (the envp regression test), commit `20261001-envtest`.
A bug in the test's own inline asm, found by a log line that made
no sense.*

`envp_step1.c` issues its syscalls as raw inline asm, matching
`musl_exec.c` and `musl_exec2.c`.  The write helper was copied from
those files verbatim:

    static void puts_raw(const char* s, unsigned long n) {
        __asm__ volatile("syscall"
                         :
                         : "a"(1L), "D"(1L), "S"(s), "d"(n)
                         : "rcx", "r11", "memory");
    }

`"a"(1L)` is an **input**.  The asm has **no outputs** and does not
list `"rax"` as clobbered.  It declares `rcx`, `r11`, `memory` —
which is correct, those are what `syscall` destroys — and stops
there.

But `syscall` **always overwrites `%rax` with the return value.**
The block never told the compiler so.  GCC was therefore free to
believe `%rax` still held `1` after the call, and to reuse that
belief for the next operation without reloading.

With two `puts_raw` calls back to back at the end of `main`:

    puts_raw("ENVP-ALL-PASS\n", 14);
    puts_raw("ENVP-RAW-EXIT\n", 14);
    raw_exit(0);

the second `syscall` ran **without reloading `%rax`**.  It executed
with `%rax` = the first syscall's return value.  The serial log
showed:

    ok 3: empty envp passes through as empty
    Unknown syscall: 41
    Unknown syscall: 18446744073709551578

**The second line is the tell.**  `18446744073709551578` is
`2^64 - 38`, the unsigned bit pattern of `-38` — which is
`-ENOSYS`, the value syscall 41 (`socket`, unrelated) had just
returned.  A syscall *number* that is the previous syscall's
*return value* is only possible if `%rax` was never reloaded.

**Why it hid in `musl_exec.c` and `musl_exec2.c`.**  They have the
identical `puts_raw`.  It never bit there because their callers
always followed a `puts_raw` with a `raw_fork`/`raw_wait4`/
`raw_exec`/`raw_exit` that sets `"a"(NNL)` as an input — a fresh
`%rax`, which masked the missing clobber.  The latent bug is still
in those two files' `puts_raw` (both now fixed in
`20261001-lenfix`).

**The rule.**  An inline-asm block that executes `syscall` must
declare `%rax` as an output (`"=a"(ret)`) or list `"rax"` as a
clobber.  Naming it only as an input is a lie to the compiler.  The
compiler does not know what the instruction does; it knows only
what the constraints say.

    static void puts_raw(const char* s) {
        unsigned long n = 0;
        while (s[n]) n++;

        long ret;
        __asm__ volatile("syscall"
                         : "=a"(ret)
                         : "a"(1L), "D"(1L), "S"(s), "d"(n)
                         : "rcx", "r11", "memory");
        (void)ret;
    }

**The tell.**  A syscall number in the kernel's "Unknown syscall:"
diagnostic that is the *previous* syscall's return value, as an
unsigned 64-bit number.  `18446744073709551578` for `-ENOSYS`,
`18446744073709551614` for `-2` (`-ENOENT`), and so on.  Real
syscall numbers are small and positive; a number near `2^64` is a
return value in disguise.

**Where this shape recurs.**  Same family as "A syscall argument the
caller did not set holds the previous syscall's return value"
(session 41).  That entry is about a *register the kernel reads*
holding a stale value because the caller never set it.  This one is
about a *register the compiler believes* holds a value because the
asm block never said otherwise.  Both are stale `%rax`-adjacent
state at a syscall boundary, both are deterministic rather than
flaky, and both were found by reading a trace that showed a value
that should not have been there.  The check is the same: when a
syscall sees an argument or a number that no caller could have
meant, suspect the boundary between the caller and the kernel, and
read the asm constraints or the libc wrapper.

## A redirection binds to the last command in an `&&` chain

*Session 45 (the `run` script's build-capture fix), commit
`6cfb0e6`.  Not a kernel bug -- a shell redirection that scoped to
one command instead of the whole chain, so every capture was
missing the front of the build.*

The `run` script's active line was:

    make clean && make FAT_CONFIG=single && \
        make -C 05_boot_kernel64 hdd-single.img && \
        make -C 05_boot_kernel64 run-single > capture.txt

The intent was "build everything and capture the whole build."
What it actually did was `> capture.txt` **binds to the last
command in the `&&` chain** -- `make -C 05_boot_kernel64
run-single`.  The `clean`, the kernel build, and the image build
wrote to the **terminal**, not to `capture.txt`.  So every capture
produced this way was missing the front of the build, including
the one line that says whether the kernel was even rebuilt.

**Why it cost half a session.**  Session 45 spent it comparing
`kernel.bin` binaries and reasoning about allocator state, when
the log it was reading did not contain the kernel build at all.
The evidence needed to answer "was the kernel rebuilt?" was never
in the file, and nothing about the command line said so.

**The fix.**  Wrap the whole chain in a brace group so the pipe
and the redirection apply to all of it:

    { make -C 04_kernel_64bit clean && \
      make -C 04_kernel_64bit FAT_CONFIG=single && \
      make -C 05_boot_kernel64 hdd-single.img && \
      make -C 05_boot_kernel64 run-single ; } 2>&1 | tee capture.txt

The brace group makes `2>&1 | tee capture.txt` apply to the whole
chain.  `tee` writes to the terminal **and** to the file, so the
terminal is not silenced.  The explicit `make -C 04_kernel_64bit`
stages make the kernel build appear in the log.  Committed as
`6cfb0e6`.

**The rule.**  A redirection at the end of an `&&` chain binds to
the **last** command, not the chain.  `a && b && c && d > file`
sends only `d`'s output to `file`; `a`, `b`, and `c` write to the
terminal.  This is easy to miss because the command line *reads*
as if the redirection covers everything, and because the file it
produces is non-empty -- it just does not contain what you think.

**The tell.**  A capture file whose first line is not the first
command you expected.  Or: you are comparing two artifacts whose
build logs you believe you have, and one of them does not contain
the build at all.  When the whole chain's output matters, wrap it
in `{ ... ; }` and pipe the group.

**Where this shape recurs.**  Same family as "The incremental
kernel build can silently skip" (session 41): a build step whose
output was believed captured and was not, which presents as a
mystery about the artifact rather than about the command.  In both
cases the fix is to make the command do what it reads as doing --
`make clean` first, or wrap the chain -- and the lesson is that a
command line is a claim about what ran, checked against the log,
not against the intent.

## A hand-counted string length in a syscall wrapper will be wrong

*Session 42 (the envp regression test), commit `20261001-lenfix`.
A bug in the test's own string literals, invisible on the console.*

`puts_raw` originally took `(const char* s, unsigned long n)` and
wrote exactly `n` bytes.  Every call site hand-counted the length:

    puts_raw("ok 1: single var survives execve\n", 34);
    puts_raw("FAIL 1: getenv returned NULL (envp dropped)\n", 46);
    puts_raw("FAIL 1: unexpected child exit\n", 30);
    ...

Of the **thirteen** such literals in `envp_step1.c`, **nine were
wrong** — off by one or two.  The runs *looked* clean, because of
which direction they were wrong:

- A length one **too long** writes the string's NUL terminator as a
  byte.  On the serial console a NUL prints as nothing, so the extra
  byte was invisible.
- A length two too long writes the NUL and the byte after it — also
  invisible unless that byte happens to be printable, and in
  `.rodata` it usually is not.
- A length **too short** drops the trailing `\n`.  That one *was*
  visible: the `MUSL_EXEC2-ALL-PASS` line ran together with the
  kernel's `EXIT:` diagnostic on the same line, because the final
  `puts_raw` was passed `19` for a `20`-byte string and never wrote
  the newline.

**Why "one too long" is not harmless.**  It is a one-byte read past
the end of the literal.  In `.rodata` with other constants nearby it
reads whatever is next, which is why the output still looked right.
But a literal that ends exactly at a page boundary makes the extra
byte a **fault** — and the day it faults is the day the test is
being used to debug something else.

**The rule.**  Do not pass a hand-counted length to a function that
writes a string.  There is no compile-time check on a counted
literal, and it *will* be wrong again.  The fix is to **remove the
parameter**, not to recount:

    static void puts_raw(const char* s) {
        unsigned long n = 0;
        while (s[n]) n++;
        ... /* write(1, s, n) via the asm block above */
    }

C gives you `sizeof` for an array and nothing for a bare pointer, so
the only reliable length for a string literal is one the callee
computes.  `strlen` is available in a normal musl binary; in a raw-
asm test, a two-line loop is enough and depends on nothing.

**The tell.**  Two strings on the same output line that should be on
separate lines.  That is a dropped `\n`, which means the length was
too short.  The inverse — a `\0` byte appearing in a `write` of a
literal — does not show on a console but shows in a hexdump of the
output stream.

**Where this shape recurs.**  Same family as "A wrong constant
propagated because it was consistent with itself" (session 39) and
"The incremental kernel build can silently skip" (session 41): a
derived value (`n = 34`) that was never checked against the thing it
derives from (the literal's actual length), whose wrongness was
masked because the wrong value was *close enough* to produce
plausible output.  In every case the fix is to check the source of
truth — here, to not have a derived value at all.
