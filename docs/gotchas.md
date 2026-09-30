## Low fds (0/1/2) are first-class

**Symptom.** `busybox uniq FILE` hangs — with or without `-c`, on any
file.  `uniq -c < file` works.  The session-33 handoff recorded this
as "`uniq` hangs, cause unknown, trace-first investigation needed."
That diagnosis was wrong.  `uniq` was never the problem; the kernel's
fd allocation was.

**Root cause, part 1 — `open` did not return the lowest free fd.**

busybox `uniq.c` does:

    close(STDIN_FILENO);              /* frees fd 0 */
    xopen(input_filename, O_RDONLY);  /* expects fd 0 back */
    ...
    while ((cur_line = xmalloc_fgetline(stdin)) != NULL) { ... }

so it closes fd 0, reopens the input file expecting fd 0 back, and
then reads from `stdin` (fd 0).  This is the normal Unix idiom —
`open(2)` returns the lowest free fd, and after `close(0)` that is 0.

donix's `alloc_file_slot` started its search at fd 3:

    for (int i = 3; i < MAX_PROCESS_FILES; i++) { ... }

so the open landed on fd 3.  `stdin` (fd 0) stayed pointed at the
now-closed fd 0.  `read(0, ...)` then took the fd-0 keyboard path in
`sys_read` (because `get_file_slot_any(0)` returned NULL), and the
applet blocked forever waiting for a keystroke that would never come.
The file was open the whole time — as fd 3 — and never read.

`uniq -c < file` worked because shell redirection uses `dup2` to
install the file on fd 0, and `sys_dup2` accepts `newfd = 0`.  Only
the `close(0); open(file)` idiom broke.

**The fix, part 1 — console sentinels.**

fds 0/1/2 must be *occupied* at process start, not NULL, so a fresh
process's first `open()` returns fd 3 — matching Linux, where stdio
fds are always held by the tty or a pipe.

`process_create` now calls `user_syscall_init_console_fds`, which
installs a `FILE_KIND_CONSOLE` sentinel slot in each of fds 0/1/2.
The sentinel carries no obj.  `sys_read` and `sys_write` recognize it
by kind and take their existing keyboard/screen path (they check
`slot->kind != FILE_KIND_FILE`, and CONSOLE is not FILE, so they fall
through correctly with no code change).  `put_file_slot` frees a
console slot without calling `f_close`.

**The fix, part 2 — lowest-free-fd open.**

`alloc_file_slot` now starts at fd 0.  With the sentinels in place:

  - a fresh process's first `open()` returns fd 3 (0/1/2 held);
  - after `close(0)` — the `uniq` idiom — `open()` returns fd 0.

Both behaviors are now Linux-identical.

**Root cause, part 3 — `dup2` and `fcntl` refused low fds.**

With parts 1 and 2 in, `uniq FILE` worked, but a *second* redirect in
the same shell broke:

    $ echo hi > out.txt          # works
    $ cat out.txt                # works
    $ cat < hello-world.txt
    sh: 1: Bad file descriptor

The fd-lifecycle trace showed why.  By that point in the shell's life,
an earlier redirect's save/restore had left a low fd free, so:

    [fd] pid=10 open  hello-world.txt -> fd=1 kind=FILE
    [fd] pid=10 fcntl fd=0 cmd=1030 -> EBADF
    [fd] pid=10 dup2  old=1 new=0 -> EBADF
    sh: 1: Bad file descriptor

`open()` returning fd 1 is *correct* — fd 1 was free, and lowest-free-
fd says reuse it.  The failure was downstream:

  - `sys_dup2(1, 0)` called `get_file_slot(oldfd, 0)`, which refuses
    `fd < 3`, so `oldfd = 1` returned NULL and `sys_dup2` returned
    `EBADF`.  ash installs a redirect with `dup2(file_fd, 1)` (or
    `dup2(file_fd, 0)`), so a redirect whose scratch fd landed in
    0/1/2 failed.
  - `sys_fcntl` had the same `get_file_slot` at the top, so ash's
    save-stdout step — `fcntl(1, F_DUPFD_CLOEXEC, ...)` — returned
    `EBADF`.  ash tolerates *that* one (it proceeds to `dup2`
    anyway), but it is the same bug.

Fix: `sys_dup2` uses `get_file_slot_any(oldfd)` (accepts 0/1/2).
`sys_fcntl` uses `get_file_slot_any` for `F_DUPFD` and
`F_DUPFD_CLOEXEC` only; its other subcommands still refuse `fd < 3`,
and the new fd from `F_DUPFD` still lands on `fd >= 3`.

After the fix the trace reads:

    [fd] pid=3 open  out.txt -> fd=3 kind=FILE
    [fd] pid=3 fcntl DUPFD fd=1 min=10 -> 10
    [fd] pid=3 dup2  old=3 new=1 kind=FILE -> 1
    [fd] pid=3 close fd=3 kind=FILE -> 0
    [fd] pid=4 close fd=10 kind=CONSOLE -> 0
    EXIT: pid=4 ...
    [fd] pid=3 dup2  old=10 new=1 kind=CONSOLE -> 1
    [fd] pid=3 close fd=10 kind=CONSOLE -> 0

The `fcntl DUPFD ... -> 10` is the save; `dup2(10, 1)` is the restore.
Both now succeed, so repeated redirects in one shell work.

**Method note — the trace that found it.**

This was pinned with a temporary `DEBUG_FD_TRACE` in `user_syscall.c`
that logged every `open`/`close`/`dup2`/`fcntl` with pid, fd, slot
kind, and return value, in the `[read]` trace style used for the
session-33 redirection fix.  Two lessons:

  - The good-vs-bad comparison was **useless** until part 3 was fixed.
    With only parts 1 and 2 in, the "bad" trace (`i = 0`) and the
    "good" trace (`i = 3`) were byte-for-byte identical, because the
    failure was downstream of `open`: `open` returned fd 3 in both,
    and `uniq` was the only thing that differed.  The moment
    `fcntl DUPFD fd=1 -> 10` appeared instead of `-> EBADF`, the
    whole picture fell out.
  - The `sys_dup2`/`sys_fcntl` guards were invisible from reading the
    code alone — they look like intentional policy ("don't treat
    stdio as a file"), not a bug.  It took the trace of ash's actual
    redirect save/restore sequence to see that the guards were
    refusing a real file that had been installed on a low fd.

Lesson for next time: when a shell misbehaves on the second redirect
but not the first, suspect the redirect save/restore bookkeeping and
trace the `fcntl`/`dup2` sequence, not `open`/`read`.

## Negative fd-kind tests don't extend to new kinds

*Session 36 (v0.6.6). Same class as the session-34 `uniq` hang: a
low-fd dispatch that was correct for files and silently wrong when a
new fd kind appeared.*

`sys_read` and `sys_write` have a fast path that routes fd 0
(keyboard) and fds 1/2 (screen) to the console instead of the
general fd table. Before v0.6.6 the guard was:

    if (fd == 0 && (!slot || slot->kind != FILE_KIND_FILE)) {
        /* keyboard */
    }

which was written when a *file* was the only thing `dup2` could
put on fd 0. The guard means "fd 0 is the keyboard only when
nothing is redirected onto it," and `!= FILE_KIND_FILE` was a
fine proxy for "nothing is redirected onto it" at the time.

Then `pipe(2)` landed (v0.6.6) and a pipe end acquired a new
kind, `FILE_KIND_PIPE`. A shell pipeline does exactly what the
guard is meant to detect -- it `dup2`s a pipe end onto the
child's fd 0 (for `cmd | other`, the read end onto `other`'s
stdin) and fd 1 (the write end onto `cmd`'s stdout). But a pipe
slot has `kind == FILE_KIND_PIPE`, which is `!= FILE_KIND_FILE`,
so the guard was *true* and a piped stdin took the keyboard path.

The symptom is the worst kind: not a wrong answer, but a hang.
`cat file | head` forked `cat` with fd 1 = write end of a pipe,
`cat` wrote into the pipe correctly, and `head` forked with
fd 0 = read end of the pipe and blocked forever in the keyboard
path -- waiting for a keystroke that would never come, while the
bytes it wanted sat unread in the pipe's ring buffer. The only
way to unstick it was to press a key, at which point `head`
consumed a *keystroke* as its stdin and the pipeline produced
nonsense.

### The rule

**A negative test on a kind (`!= KIND_X`) silently changes meaning
every time a new kind is added.** The correct form is a positive
test on the set that should take the special path. The v0.6.6 fix
inverts both guards:

    if (fd == 0 && (!slot || slot->kind == FILE_KIND_CONSOLE)) {
        /* keyboard */
    }

which is what the guard always *meant* -- the keyboard/screen path
exists exactly for the console sentinels installed by
`user_syscall_init_console_fds`, so anything else on a low fd is a
real fd. New fd kinds now route correctly by default instead of
needing to be added to the negative test.

### How to notice this class early

When adding a new `FILE_KIND_*`, grep for every `FILE_KIND_` it
appears in the same condition as:

    grep -n "FILE_KIND_FILE" 04_kernel_64bit/user_syscall.c

Every site that says `!= FILE_KIND_FILE` or `== FILE_KIND_FILE`
is a place the new kind's behavior is implicitly decided. Read
each one and ask "should the new kind take this path?" -- do not
assume the answer is "no, because it isn't a file."

This is the second bug of this exact shape. The first (session 34,
`uniq` hang) was `alloc_file_slot` starting its search at fd 3,
which was correct when the only low fds were stdio and wrong once
a program `close(0)`'d and expected `open` to hand fd 0 back. Both
are "a decision encoded as a test on the current set of kinds /
fd numbers, invalidated by a new one."

A third instance of the same *shape* -- a decision correct only for
the set of cases known at the time -- appeared the very next
session, in userland rather than the kernel: see "Multi-write
output races the child's kernel prints" below. It is not a
`FILE_KIND_*` case, but it is the same lesson: an assumption baked
in when the set was smaller, invalidated when the set grew.

## Multi-write output races the child's kernel prints

*Session 37 (musl_sh work), userland. Three instances of one
pattern, all in `userland/musl/apps/musl_sh.c` or its debug
scaffolding.*

**Symptom.** A line of output is missing its tail -- a closing
`]`, a trailing `\n` -- or two unrelated lines run together with no
separator between them. It happens on *some* lines and not others,
and the discriminator is not obvious from the text alone.

**Root cause.** Output produced as several separate `write` calls
(`puts_raw` is one `write` per call) can be interleaved with output
the *kernel* prints during a child's `fork`/`exec`, or with the next
thing the shell prints. Each `puts_raw` is its own `sys_write`; the
scheduler can run the child -- which prints `sys_execve: ...`, or
`EXIT: ...` -- between two of them. The bytes are not lost, they are
*reordered*, and the ordering matters.

**The three instances.**

1. **The tokenizer debug print (fixed).** The line

       puts_raw("[", 1);
       for (...) { if (i) puts_raw("|", 1); puts_z(argv[i]); }
       puts_raw("]\n", 2);

   produced `[echo|x y` with no `]\n` on lines where the next thing
   was a child's `sys_execve: ...` print. Building the whole line
   into one buffer and issuing a single `puts_raw` fixed it. This is
   the instance that named the pattern.

2. **`sh: cannot open <file>` (open).** In the redirection failure
   path, `fork_child` does three writes:

       puts_z("sh: cannot open ");
       puts_z(redir_file);
       puts_raw("\n", 1);

   Same exposure. Cosmetic (it is an error path), but the same bug.

3. **`cd: cannot cd to <path>` (open).** `builtin_cd`'s failure
   path does three writes:

       puts_z("cd: cannot cd to ");
       puts_z(target);
       puts_raw("\n", 1);

   Observed in the step-2 run: `cd: cannot cd to /nopedonix>` --
   the newline was reordered past the next prompt.

**The rule.** When the *ordering* of your output relative to
anything else matters -- and it does whenever a child may print
between your writes, or the shell may print next -- build the whole
line in one buffer and issue **one** `write`. Several
`puts_raw`/`puts_z` calls in a row are several writes and are not
atomic with respect to each other.

**Note the connection.** This is the same lesson as "Negative
fd-kind tests don't extend to new kinds," in a different medium:
a choice (split the output into several writes) that was harmless
while the set of things that could print between them was small,
and stopped being harmless when that set grew (a forking pipeline,
a child that prints on exit). It is the third instance of that
shape. Expect more; the shape recurs across subsystems.

## `argv[cmd_argc] = 0` mutates argv in the child

*Session 37 (musl_sh work), userland.*

`fork_child` terminates the command's argv by writing a NUL at
`argv[cmd_argc]`, because `execve` reads argv to its NUL and the
command may be a prefix of a longer token list (`echo hi > f` --
the command is `echo hi`, the `>` and `f` are the redirection):

    argv[cmd_argc] = (char*)0;
    execve(argv[0], argv, (char**)0);

`argv[cmd_argc]` is either the operator token or the line's existing
NUL, so the write is in bounds. It is safe **because the child execs
or `_exit`s immediately afterward** -- no code re-reads argv after
this point.

**The hazard.** This is a decision that is correct only because of
what happens next. If `execve` fails and the child were to *continue*
rather than `_exit`, it would see a truncated argv: everything from
`cmd_argc` on is now unreachable (`argv[cmd_argc]` reads as the
terminator). Nothing does that today -- the child's next statement
is `_exit(127)` -- but it is the kind of coupling worth naming, in
the same family as the blunt-fd-close entry below.

**If you change the child's post-`execve` path**, check this
mutation first. If the child ever needs the full argv after a failed
`execve`, terminate the command some other way (a separate
`char* cmd_argv[SH_MAX_ARGS]` copy, or save/restore the byte).

## Blunt fd close in `fork_child` is correct only for the current shell

*Session 37 (musl_sh work), userland.*

`fork_child` closes **every** fd from 3 to 63 in the child, just
before `execve`:

    for (int fd = 3; fd < 64; fd++) close(fd);

**Why it is there.** In a pipeline `a | b | c`, the middle command
`b` inherits the parent's copies of *both* pipe ends as well as its
own. `in_fd`/`out_fd` become its fd 0 and fd 1, but the *other* ends
are still open on raw fds above 2. If `b` keeps the upstream write
end open, then when `a` exits the reader (`b` itself) never sees
EOF -- there is still a writer holding the pipe -- and the pipeline
deadlocks. Closing all inherited fds above 2 is the standard fix.

**Why it is a gotcha.** Closing 3..63 unconditionally is a
sledgehammer. It is correct **only because the shell holds no fd
above 2 at exec time** -- it opens nothing persistent. If the shell
ever does (a history file, a script fd, a directory fd held across a
command), this close would silently kill it in every child.

**If you give the shell any fd above 2**, replace the blanket close
with an explicit "close these fds" list threaded through
`fork_child`. Do not just widen or narrow the range; the range is
the wrong shape. This is the same family as the `argv[cmd_argc] = 0`
entry above: correct because of what the shell happens to hold
today, not because of anything structural.

## `capture.txt` shows backspace history, not the corrected line

*Session 37 (musl_sh work), diagnostic note -- not a bug.*

**Symptom.** A line in `capture.txt` reads as garbage -- the typed
text, an erase, more text -- and does not match what the shell
actually ran. Example:

    donix> echo "a b c"    " c
    sys_execve: pid=4 ... argc=3 (echo)
    a b c

which looks like it tokenized `echo "a b c" " c` into three tokens
and printed something that does not match. It did not.

**Root cause.** The shell's line editor erases a character by
emitting `\b \b` to the console:

    if (c == '\b' || c == 0x7f) {
        if (n > 0) { n--; puts_raw("\b \b", 3); }
        continue;
    }

On a real terminal (the VGA/SDL display), `\b \b` visually erases
the character, so the screen shows the corrected line. On **serial**
-- which is what `capture.txt` records -- the `\b \b` bytes are
captured *literally*, and a transcript does not retroactively edit
itself. So `capture.txt` shows the full typed history, backspaces
and all, while `line[]` (what the tokenizer saw) and the VGA both
show the corrected line.

**How to read a backspaced capture line.** Do not read the echoed
line as the shell's input. Read the `sys_execve: ... argc=N`
line -- that is the real argument count -- or read the VGA. A
transcript of an edit is not the edited text.

**Not a bug.** The shell is correct; the capture is a faithful
record of the byte stream including the erase sequences.
