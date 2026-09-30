Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-09-30 (post-`v0.6.6`)
**Current HEAD:** tag `v0.6.6`, branch `dev`
**Last milestone:** `v0.6.6` (published) — **`pipe(2)` is done and
pipelines work**
**Next milestone:** `v0.6.7` candidate — `musl_sh` quote stripping
and redirection parsing are the headline items

Commits are named by tag only, never by SHA.  **Working tags
(`2026092x-*`) are local scratch restore points** — they exist while
a milestone is being developed and are **dropped before the
milestone is pushed**.  Only `v*` tags go to the remote and are
permanent.  The commit record is `docs/session-log.md`; the commit
message carries the narrative.  Once a scratch tag is dropped it
resolves to nothing; do not cite one as if it were a stable
reference.

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 syscall ABI, natively. Newlib is gone.
Userland is a tracked source tree at `userland/musl/`. Phase B
(busybox) is well underway.

Full strategy, rules, and the one-change-at-a-time discipline:
`docs/strategy.md`.

---

## Tree on disk

One working copy.

- **Real project:** `/home/noneya/code/donix/`.  This is the tracked
  repository, the source of truth, and where the `v*` tags live.

The kernel C sources live in `04_kernel_64bit/`.  Verify by file
name before editing — do not trust the number alone.
`05_boot_kernel64/` holds the boot chain assembly and the image
builder, not the kernel C sources.

Scratch workspaces (e.g. a copy at `/home/noneya/code/testme/`)
have been used in past sessions to experiment before porting a
change back.  They are **transient**: never the source of truth,
never where a `v*` tag lives, and never referenced by this file.
If one exists, it is safe to reset or delete.  Do not port from a
scratch workspace into the real tree without a build and test in
the real tree.

---

## Where we are — `v0.6.6` tagged, working tree clean

`v0.6.6` is published.  Session 36 is the milestone: **`pipe(2)` is
implemented and pipelines work.**  All scratch tags have been
dropped; `dev` is pushed.

### Session 36 — pipes, end to end

Five kernel steps plus a keyboard fix, a missing syscall, and a
test revert.  Each step was independently tagged and tested; each
found exactly one class of bug.  In order:

1. **`pipe(2)` object and non-blocking I/O.**  `FILE_KIND_PIPE`, a
   shared `pipe_t` (4 KB ring, `capacity` a *field* not an inlined
   macro so growth is additive later), two refcounts kept
   distinct — slot refcount counts fd references to one *end*, pipe
   refcount counts live *ends*.  `sys_pipe` allocates the object and
   both ends atomically and rolls back on failure.

2. **Blocking and directed wake.**  `BLOCK_KIND_PIPE_READ`/`WRITE`
   = 2/3 (new values for the existing `block_kind`, no PCB offset
   movement).  `reader_waiting`/`writer_waiting` pointers on the
   pipe; `pipe_wake_waiter()` checks the waiter is non-NULL, still
   `BLOCKED`, and blocked on *this* kind before waking — the
   block_kind check is what makes a recycled PCB slot safe.  This
   is a directed wake, not a broadcast: a write wakes the one
   reader that can make progress, not every blocked process.

3. **EOF, `-EPIPE`, dup-aware counts.**  `file_slot_t.end` flag
   (read/write); `pipe_t.readers_open`/`writers_open` *counts*.
   Counts, not flags, because `pipe(fds); dup2(fds[1], 1)` leaves
   two fds holding the write end, and closing one must not look
   like "the writer closed."  `read` on empty + `writers_open == 0`
   → 0 (EOF); `write` with `readers_open == 0` → `-EPIPE`, checked
   *before* the full-buffer check.

4. **The exit-path wake.**  `put_file_slot`'s pipe case now wakes
   the peer when a count reaches zero.  Without it, a writer that
   `_exit`s without closing leaves a reader stuck in `hlt` until a
   keystroke — a hang on a headless system.

5. **The stdio routing fix — this is what made pipelines work.**
   `sys_read` fd-0 and `sys_write` fd-1/2 guards changed from
   `kind != FILE_KIND_FILE` to `kind == FILE_KIND_CONSOLE`.  The
   old form was correct when a file was the only thing `dup2`
   could put on a low fd; a pipe end (`FILE_KIND_PIPE`) is
   `!= FILE_KIND_FILE`, so a piped stdin took the keyboard path and
   blocked forever.  Full writeup: `docs/gotchas.md`, "Negative
   fd-kind tests don't extend to new kinds."

**Also in the milestone:** `dup(2)` (syscall 32) — musl's `dup()`
reaches `SYS_dup` directly and donix had no handler, so any caller
got `-ENOSYS`.  Found by `pipe_step3`'s first run, not by reading
code.  Implemented as a one-line delegation to
`sys_fcntl(fd, F_DUPFD, 0)`.  And a keyboard fix: `keyboard.c`
assigned neither `scancode_ascii[0x2B]` nor `scancode_shift[0x2B]`,
so Shift+backslash produced nothing in either shell — the `|`
character was untypeable.  Both tables now assign 0x2B.

### What `v0.6.5` contributed (prior milestone)

Scripts run (`MAX_PROCESS_FILES` 8 → 64, `execve` returns `ENOEXEC`
for a short non-ELF so ash runs the file through `sh`,
`./script.sh` normalizes).  Shell redirection end to end (three
interlocking bugs: `sys_read`/`sys_write` hard-coded fds 0/1/2,
`sys_fork` never copied `file_table[]`, `close_all_files` started at
fd 3).  Four syscalls: `uname` (63), `lseek` (8), `rename` (82),
`readv` (19).  busybox applets enabled: `head`, `tail`, `cp`, `mv`,
`grep`, `sed`, `cut`, `sort`, `stat`, `tee`, `test`, `tr`, `cmp`,
`od`, `uniq`.  And fds 0/1/2 first-class: console sentinels,
lowest-free-fd `open`, `dup2`/`fcntl` accepting low fds.

### Known failures, deliberately off

- **`diff`** — deliberately off; larger surface area.
- **`chmod`, `ln`, `mount`/`umount`** — need `chmod(2)` (90),
  `link(2)`/`symlink(2)` (86/88), and `mount(2)` respectively.

### Known limitation: `musl_sh`

`donix>` (musl_sh) does not strip shell quotes and does not parse
redirection or pipes.  All tests involving `<`, `>`, `|`, `&&`,
`;`, or quoting must be run from ash (`busybox sh` from `donix>`,
or the auto-launched ash at boot).  See `docs/gotchas.md`.

This includes `uniq -c < file`: from `donix>` the `<` reaches
`uniq` as a literal argument, which is why the `donix>` run shows
`uniq: can't open '<'`.  That is the `musl_sh` gap, not a `uniq`
bug.  A pipeline typed at `donix>` behaves the same way — `|`
reaches the first applet as a literal argument.  This is the
headline item for `v0.6.7`.

---

## NEXT SESSION — `musl_sh` quote stripping and redirection

`v0.6.7` candidate.  The headline items are the two `musl_sh`
gaps: quote stripping and parsing of redirection (`<`, `>`, `|`),
plus `&&`/`;`.  Both are userland-only — a tokenizer fix in
`userland/musl/apps/musl_sh.c`, no kernel change.

**Why this is the right next item.**  Everything the *kernel*
needs to run a real shell is now in place: `pipe`, `dup`, `dup2`,
`fork`, `execve`, `wait4`, redirection via `dup2`, and a working
cwd.  The only thing standing between `donix>` and a usable
interactive shell is that its tokenizer doesn't understand the
syntax.  ash does understand it, which is why all the shell-level
verification runs there.  Making `musl_sh` understand it means the
project's own shell can demonstrate the features the kernel
supports, rather than delegating to busybox.

**Scope, roughly:** split the input line on unquoted whitespace;
recognize `<`, `>`, `>>`, `|`, `&&`, `;` as operators; honor single
and double quotes; build an argv for `execve` and a pipeline of
`fork`/`dup2`/`execve`.  It is a real piece of shell code, but it
is *only* code — no new syscalls, no kernel risk, and every
primitive it needs is already tested.

**Test:** the same commands the canary already runs from ash,
typed at `donix>` instead.  `cat hello-world.txt | head -n 2`,
`echo hi > out.txt`, `cat out.txt`, `echo hi2 > out2.txt`, and
`uniq -c < hello-world.txt`.  When those work from `donix>`, the
gap is closed.

### If you touch pipelines: read this first

`-EPIPE` is delivered without `SIGPIPE` (see
`docs/open-issues.md`).  On real Linux, when a reader exits, the
writer is killed by `SIGPIPE`; here it gets `-EPIPE` from `write`
and must handle it itself.  For `cat file | head`, `head` exits,
`cat` gets `-EPIPE`, and busybox's `cat` handles it — so the
common pipelines work.  But a program that *doesn't* handle
`-EPIPE` (busybox `yes` is the example) will keep writing, or
block on a full pipe whose reader is gone, and hang rather than
die.  If you enable `ASH_JOB_CONTROL`, add a `yes`-style test, or
try `yes | head -n 1`, expect this.  Fixing it means implementing
signal delivery — a larger change, tracked in `open-issues.md`.

### Small, close gaps (all independent, all one-change-at-a-time)

1. **`newfstatat` (262)** — reserved number, no dispatch case.
   Delegates to `sys_stat` for `AT_FDCWD` or an absolute path.
   Unblocks `find`.
2. **`sys_open` `O_DIRECTORY` fix** — return `-ENOTDIR` when the
   target is a file.
3. **Ctrl-`[` as ESC** — `scancode_to_ascii` has no Ctrl parameter
   yet.
4. **`sys_utimensat` cwd resolution** — it calls `strip_dot_prefix`
   but not `resolve_against_cwd`.
5. **`sys_fcntl` fd < 3 for the other subcommands** — `F_GETFL`,
   `F_SETFL`, `F_GETFD`, `F_SETFD` still refuse `fd < 3`; Linux
   allows them on a redirected fd.  Not on any current path.

**More applets, any time:** `awk`, `find` (needs `newfstatat`),
`tar`.  `chmod` (90), `ln` (86/88), and `mount` need their own
syscalls.

Pick **one**, do it, test it, tag it.  One change at a time.

---

## Canary state

**The focused canary is green as of `v0.6.6`.**  Full table in
`docs/session-log.md`.  Script execution, redirection, the
pipelines, and the applet one-offs are verification, not canary
rows — the canary must not mutate the disk.

    # on boot, ash is already running
    pwd                         # /
    cd /bin
    pwd                         # /bin
    ls                          # busybox
    cd ..
    pwd                         # /
    ls                          # full root listing
    exit                        # back to donix>
    pwd                         # /
    cd /bin
    pwd                         # /bin
    ls                          # busybox (donix-native ls)
    cat busybox                 # reads /bin/busybox
    cd /
    pwd                         # /
    ls hello-world.txt
    memtest
    musl_fork
    musl_exec2
    musl_wait
    busybox ls
    busybox pwd                 # / (after cd /)
    busybox ash
    # at the ash prompt: pwd, cd /bin, pwd, ls, exit
    # back at donix>: hello

**Read-only `uniq` rows (added v0.6.5), run from ash:**

    uniq hello-world.txt
    uniq -c < hello-world.txt

**Pipeline rows (added v0.6.6), run from ash:**

    cat hello-world.txt | head -n 2
    echo hi | wc
    echo hello | cat

These are the milestone's headline verification.  `cat | head` is
the one that exercises blocking on both ends; `echo hi | wc` is the
one that exercises EOF (the reader must see the writer close and
stop); `echo hello | cat` is the smallest end-to-end case.

**Pipe regression suite (`userland/musl/tests/`), run from ash:**

    pipe_step1    # object + non-blocking I/O
    pipe_step2    # blocking + directed wake
    pipe_step3    # EOF, EPIPE, dup-aware counts
    pipe_step3b   # exit-path wake

These four are the only regression suite the pipe code has.  Each
prints a `STEPn OK` (or `STEP3B OK`) line on success.  If you change
anything in `sys_read`/`sys_write`/`sys_close`/`put_file_slot`/
`sys_fork`/`sys_pipe`, or add a new `FILE_KIND_*`, run them.  They
are not canary rows: they fork and they take seconds, but they do
not mutate the disk.

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`; use
`busybox sh` or `/bin/busybox sh`.

**Do NOT add a `donix>` redirection or pipe row.**  `musl_sh` does
not parse `<`, `>`, or `|`.  Those are ash-only tests until
`v0.6.7` closes the gap.

**Do NOT run `uniq -c < file` from `donix>`.**  From `donix>` the
`<` reaches `uniq` as a literal argument and `uniq` reports
`can't open '<'`.

The **full canary** (milestone-only) adds: `echo`, `cat`,
`musl_stat`, `musl_min`, `musl_malloc`, `musl_printf`, `musl_exec`,
`musl_readdir`, `musl_r10probe`, `brk_verify`, `brkraw`,
`brkgrow`, `musl_dup2`, `musl_dupfd`, `musl_ids`, `musl_getcwd`,
`busybox echo`, `busybox wc hello-world.txt`.

**Canary rows must not mutate the disk.**  The read-only applets
(`head`, `tail`, `grep`, `sed`, `cut`, `sort`, `stat`, `cmp`, `wc`,
`od`, `uniq`) could be added as canary rows.  `tee`, `cp`, `mv`,
the redirection tests, and the pipelines mutate or fork, so they
stay one-offs.

**Expected noise:** none beyond the known lines.  The serial log
has no `Unknown syscall:` lines and no `[fd]` lines (the fd trace
is off).  Informational lines that are expected: `EXIT: pid=N
state=2 parent=P qhead=Q` for every forked child (a zombie exit);
`EXIT: pid=N state=1 parent=P qhead=Q` for a forked shell's own
exit; `EXIT-FALLBACK: switching to idle, ...` when a child is the
last runnable process.  The `sys_open: f_open FAIL path=etc/...`
lines from `busybox stat` are expected — those files do not exist
and `stat` falls back cleanly.  The `sys_rename: f_rename FAIL ...`
line from `busybox mv` on a failed rename is expected — same style
of diagnostic `sys_unlink` and `sys_rmdir` carry, and only prints
on failure.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **VFS layer (eventual).**  `sys_execve`'s three-attempt path
   resolution and `resolve_against_cwd` are both shims.  When a VFS
   lands, delete them; do not extend.
2. **`musl_sh` does not strip quotes or parse redirection.**
   Userland-only fix.  The headline item for `v0.6.7`.
3. **`sys_fcntl` refuses fd < 3** for subcommands other than
   `F_DUPFD`/`F_DUPFD_CLOEXEC`.  Deliberate; a small extension of
   the session-34 work rather than a new problem.
4. **`rename(2)` does not replace; `chmod`/`ln`/`mount` need
   their own syscalls.**  Deliberate FatFs-limitation first cuts.
5. **`-EPIPE` is delivered without `SIGPIPE`.**  A program that
   relies on dying from `SIGPIPE` gets the errno instead.  Read
   the "If you touch pipelines" note above before changing anything
   in the pipe or signal path.

Also open: `newfstatat` (262) reserved, no dispatch case;
`sys_open` accepts non-directories with `O_DIRECTORY`; Ctrl- `[`
not mapped to ESC; `sys_utimensat` lacks `resolve_against_cwd`;
`sys_munmap` is a stub returning 0; `sys_brk`'s fixed `heap_base`
and the 4 MB mmap window are latent collisions; real FatFs
timestamp storage (the three timestamp syscalls return 0 without
storing); `prctl` is minimal (`PR_SET_NAME` accepted and dropped);
busybox applet symlinks not installed; syscall-table audit script;
`musl_wait`'s WNOHANG loop spins; `sys_mmap` rejects all
non-anonymous mappings (a file-backed `mmap` caller will get
`-ENOMEM` and must fall back to `read`); pipes support one
concurrent reader and one concurrent writer; `put_file_slot`'s pipe
wake is coupled to `sys_close`'s wake.

---

## State on disk

**Real project root:** `/home/noneya/code/donix/`.

Config and source locations (relative to the root):

- `configs/busybox.config` — tracked canonical busybox config.
  Enabled applets: `cat`, `cp`, `cut`, `echo`, `head`, `ls`,
  `mkdir`, `mv`, `od`, `pwd`, `rm`, `rmdir`, `sort`, `stat`,
  `tail`, `tee`, `test`, `touch`, `tr`, `uname`, `uniq`, `wc`,
  `cmp`, `grep`, `sed`, `vi`, plus `ash`.  Off (with reasons):
  `diff` (deliberate), `chmod`/`ln`/`mount` (need kernel work).
- `userland/musl/` — tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.  The pipe regression binaries
  (`pipe_step1`…`pipe_step3b`) live in `tests/` and are staged to
  the FAT image by `05_boot_kernel64/Makefile` — the image
  Makefile lists every userland ELF explicitly in two places
  (`USERLAND_ELFS` and the `mcopy_one` chain), so a new test must
  be added to both or it builds and never reaches the disk.
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  — gitignored; rebuild with `./toolchain/install_musl.sh`.
- `toolchain/{install_musl.sh,musl-gcc.sh}` — tracked.

Kernel sources: `04_kernel_64bit/`.

Scratch workspaces: transient, if any exist.  Not referenced here.

---

## Where things live

Not needed to start a session; ask for a file when the current task
needs it.  Paths below are relative to the tree root
(`/home/noneya/code/donix/`).

- `docs/strategy.md` — Phase A/B plan, rules, files-not-to-touch,
  tagging convention, git hygiene, recovery.
- `docs/gotchas.md` — every bug writeup, by subsystem.  Two entries
  are a matched pair worth reading together: "Low fds (0/1/2) are
  first-class" (session 34) and "Negative fd-kind tests don't
  extend to new kinds" (session 36).  Both are the same class — a
  decision encoded as a test on the current set of kinds or fd
  numbers, invalidated when a new one appears.  Expect a third.
- `docs/session-log.md` — commit tables and per-test canary notes.
  Rows are named by tag; scratch tags are dropped before a
  milestone push, so a row's tag may no longer resolve — the
  commit message is the record.
- `docs/open-issues.md` — full open-issues list.
- `docs/migration-history.md`, `docs/dons-os-history.md` —
  historical narrative (A1-A6, pre-fork).
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` — the first two
  frozen at `v0.6.0`; `LLD_BUG_REPORT.md` current.

---

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  `v0.6.6` is published: `pipe(2)` is implemented
and pipelines work — `cat file | head`, `echo hi | wc` — with
blocking read/write, directed wake, EOF, and `-EPIPE`.  `dup(2)`
(32) added.  Next: `musl_sh` quote stripping and redirection, so
`donix>` itself can do what ash already can.  One change at a
time.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
