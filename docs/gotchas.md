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
