Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-09-29 (post-`v0.6.5`)
**Current HEAD:** tag `v0.6.5`, branch `dev`
**Last milestone:** `v0.6.5` (published) — **scripts, redirection,
and a real fd layer are done**
**Next milestone:** `v0.6.6` candidate — `pipe(2)` is the headline
item

Commits are named by tag only, never by SHA.  **Working tags
(`2026092x-*`) are local scratch restore points** — they exist while
a milestone is being developed and are **dropped before the
milestone is pushed**.  Only `v*` tags go to the remote and are
permanent.  The commit record is `docs/session-log.md`; the commit
message carries the narrative.

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 syscall ABI, natively. Newlib is gone.
Userland is a tracked source tree at `userland/musl/`. Phase B
(busybox) is well underway.

Full strategy, rules, and the one-change-at-a-time discipline:
`docs/strategy.md`.

---

## Trees on disk

Two working copies exist.  Know which is which before touching
anything.

- **Real project:** `/home/noneya/code/donix/`.  This is the
  tracked repository, the source of truth, and where the `v*`
  tags live.  All work in the milestone ends up here.
- **Experimental tree:** `/home/noneya/code/testme/`.  A scratch
  copy used for exploratory work and for testing changes before
  they go into the real project.  Safe to reset; nothing in it is
  permanent.

The two trees share the same top-level layout.  The C sources for
the kernel live under a numbered subdirectory in each:

- `/home/noneya/code/donix/04_kernel_64bit/` — real project
  kernel sources.
- `/home/noneya/code/testme/04_kernel_64bit/` — experimental tree
  kernel sources.

Both trees use `04_kernel_64bit/` for the kernel source.  Verify
by file name before porting — do not trust the number alone.
`05_boot_kernel64/` holds the boot chain assembly and the image
builder, not the kernel C sources.

---

## Where we are — `v0.6.5` tagged, working tree clean

`v0.6.5` is published.  Sessions 33 and 34 together are the
milestone: **scripts run; redirection works; four new syscalls; a
broad busybox command set; and fds 0/1/2 are first-class.**  All
scratch tags have been dropped; `dev` is pushed.

### Session 34 — low fds (0/1/2) are first-class, `uniq` works

Session 34 opened with "investigate `uniq`" and closed with `uniq`
working and a kernel fix that turned out to be the actual bug.

`uniq FILE` hangs because `alloc_file_slot` started at fd 3.  busybox
`uniq` does `close(0); open(file)` and expects `open` to return fd 0;
on donix it returned fd 3, so `stdin` stayed pointed at the closed
fd 0 and `read(0, ...)` fell through to the keyboard path in
`sys_read` and blocked forever.  Three interlocking changes:

1. **Console sentinels.**  `process_create` installs a
   `FILE_KIND_CONSOLE` slot in fds 0/1/2 so a fresh process's stdio
   fds are occupied and `open(2)` returns fd 3, matching Linux.
   `sys_read`/`sys_write` treat a console slot as keyboard/screen;
   `put_file_slot` frees it without `f_close`.

2. **Lowest-free-fd `open`.**  `alloc_file_slot` starts at fd 0.
   With sentinels in place this returns 3 for a fresh process and 0
   only after `close(0)` — the `uniq` idiom.

3. **`dup2`/`fcntl` accept low fds.**  `sys_dup2` and the `F_DUPFD` /
   `F_DUPFD_CLOEXEC` cases of `sys_fcntl` use `get_file_slot_any`
   (accepts 0/1/2).  Without this, ash's redirect save/restore failed
   with `EBADF` once an earlier redirect had freed a low fd, so the
   *second* redirect in a shell broke.  The other `fcntl` subcommands
   still refuse `fd < 3`.

Full writeup with the trace: `docs/gotchas.md`, "Low fds (0/1/2) are
first-class."

### What session 33 contributed

Script execution (`MAX_PROCESS_FILES` 8 → 64, `execve` returns
`ENOEXEC` for a short non-ELF so ash runs the file through `sh`,
`./script.sh` normalizes).  Shell redirection end to end (three
interlocking bugs: `sys_read`/`sys_write` hard-coded fds 0/1/2,
`sys_fork` never copied `file_table[]`, `close_all_files` started at
fd 3).  Four syscalls: `uname` (63), `lseek` (8), `rename` (82),
`readv` (19).  Busybox applets enabled: `head`, `tail`, `cp`, `mv`,
`grep`, `sed`, `cut`, `sort`, `stat`, `tee`, `test`, `tr`, `cmp`,
`od`.

### Known failures, deliberately off

- **`diff`** — deliberately off; larger surface area.
- **`chmod`, `ln`, `mount`/`umount`** — need `chmod(2)` (90),
  `link(2)`/`symlink(2)` (86/88), and `mount(2)` respectively.

### Known limitation: `musl_sh`

`donix>` (musl_sh) does not strip shell quotes and does not parse
redirection.  All tests involving `<`, `>`, `|`, `&&`, `;`, or
quoting must be run from ash (`busybox sh` from `donix>`, or the
auto-launched ash at boot).  See `docs/gotchas.md`.

Note this includes `uniq -c < file`: from `donix>` the `<` reaches
`uniq` as a literal argument, which is why the `donix>` run in the
canary shows `uniq: can't open '<'`.  That is the `musl_sh` gap, not
an `uniq` bug.

### Known limitation: no `pipe(2)`

`|` does not work in any shell — not ash, not `donix>`.  The shell
never creates the pipe; `|` reaches the applet as a literal argv
argument.  Not an applet bug; `pipe(2)` is simply absent.  This is
the headline item for `v0.6.6`.  See `docs/open-issues.md`.

### Not carried forward from `testme`

The extra `busybox.config` applets that need kernel work, plus
`FEATURE_ALLOW_EXEC` (proven unnecessary) and `env` (cosmetic —
`envp` is ignored by `sys_execve`).  `testme` still has these; they
are deliberately left out.

---

## NEXT SESSION — `pipe(2)`

`v0.6.6` candidate.  The headline item is `pipe(2)`.

**`pipe(2)` — the biggest remaining shell gap.**  Without it, `|`
reaches applets as a literal argument.  `pipe(2)` is syscall 22.  The
implementation needs:

- a pipe object (a ring buffer, probably kernel-heap-allocated);
- per-fd read/write ends, so `file_slot_t` gains a `FILE_KIND_PIPE`
  case with an end flag;
- scheduler integration for blocking: a reader on an empty pipe and
  a writer on a full pipe must both block and be woken when the
  other end acts;
- `sys_pipe` returning two fds into the caller's array;
- `sys_read`/`sys_write` handling the pipe kind;
- `sys_close` waking a blocked peer when an end closes.

Bigger than anything since the redirection fix.  Worth its own
session.  Test: `busybox cat hello-world.txt | busybox head -n 2`,
`echo hi | wc`, and the interaction with the `dup2` work already
done.

**Small, close gaps (all independent, all one-change-at-a-time):**

1. **`newfstatat` (262)** — reserved number, no dispatch case.
   Delegates to `sys_stat` for `AT_FDCWD` or an absolute path.
2. **`sys_open` `O_DIRECTORY` fix** — return `-ENOTDIR` when the
   target is a file.
3. **Ctrl-`[` as ESC** — `scancode_to_ascii` has no Ctrl parameter
   yet.
4. **`sys_utimensat` cwd resolution** — found in session 32; it
   calls `strip_dot_prefix` but not `resolve_against_cwd`.
5. **`musl_sh` quote stripping + redirection parsing** — the two
   userland gaps.  A tokenizer fix, userland-only.
6. **`sys_fcntl` fd < 3 for the other subcommands** — a small
   extension of the session-34 work.  `F_GETFL`, `F_SETFL`,
   `F_GETFD`, `F_SETFD` still refuse `fd < 3`; Linux allows them on
   a redirected fd.  Not on any current path.

**More applets, once `pipe(2)` lands:** `awk`, `find` (needs
`newfstatat`), `tar`, `diff`.  `chmod` (90), `ln` (86/88), and
`mount` still need their own syscalls.

Pick **one**, do it, test it, tag it.  One change at a time.

---

## Canary state

**The focused canary is green as of `v0.6.5`.**  Full table in
`docs/session-log.md`.  Script execution, redirection, and the applet
one-offs are verification, not canary rows — the canary must not
mutate the disk.

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

**Read-only `uniq` rows (added session 34), run from ash:**

    uniq hello-world.txt
    uniq -c < hello-world.txt

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`; use
`busybox sh` or `/bin/busybox sh`.

**Do NOT add a `donix>` redirection row.**  `musl_sh` does not
parse `<` or `>`.

**Do NOT run `uniq -c < file` from `donix>`.**  It is an ash-only
test; from `donix>` the `<` reaches `uniq` as a literal argument and
`uniq` reports `can't open '<'`.

**Do NOT add a pipe (`|`) row.**  `pipe(2)` is not implemented;
`|` reaches the applet as a literal argument in both shells.

The **full canary** (milestone-only) adds: `echo`, `cat`,
`musl_stat`, `musl_min`, `musl_malloc`, `musl_printf`, `musl_exec`,
`musl_readdir`, `musl_r10probe`, `brk_verify`, `brkraw`,
`brkgrow`, `musl_dup2`, `musl_dupfd`, `musl_ids`, `musl_getcwd`,
`busybox echo`, `busybox wc hello-world.txt`.

**Canary rows must not mutate the disk.**  The new read-only applets
(`head`, `tail`, `grep`, `sed`, `cut`, `sort`, `stat`, `cmp`, `wc`,
`od`, `uniq`) could be added as canary rows.  `tee`, `cp`, `mv`, and
the redirection tests mutate, so they stay one-offs.

**Expected noise:** none.  The serial log has no `Unknown syscall:`
lines and no `[fd]` lines (the fd trace is off).  Two informational
lines are expected: `EXIT: pid=N state=1 parent=2 qhead=N` (a forked
busybox shell's own exit) and `EXIT-FALLBACK: switching to idle, ...`
(in `musl_fork` when the child is the last runnable process).  The
`sys_open: f_open FAIL path=etc/...` lines from `busybox stat` are
expected — those files do not exist and `stat` falls back cleanly.
The `sys_rename: f_rename FAIL ...` line from `busybox mv` on a
failed rename is expected — same style of diagnostic `sys_unlink` and
`sys_rmdir` carry, and only prints on failure.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **VFS layer (eventual).**  `sys_execve`'s three-attempt path
   resolution and `resolve_against_cwd` are both shims.  When a VFS
   lands, delete them; do not extend.
2. **`pipe(2)` is absent** — `|` does not work in any shell.
   The headline item for `v0.6.6`.
3. **`sys_fcntl` refuses fd < 3** for subcommands other than
   `F_DUPFD`/`F_DUPFD_CLOEXEC`.  Deliberate; a small extension of
   the session-34 work rather than a new problem.
4. **`musl_sh` does not strip quotes or parse redirection.**
   Userland-only fix.
5. **`rename(2)` does not replace; `chmod`/`ln`/`mount` need
   their own syscalls.**  Deliberate FatFs-limitation first cuts.

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
`-ENOMEM` and must fall back to `read`).

---

## State on disk

Paths, with the correct roots.

- **Real project root:** `/home/noneya/code/donix/`.
- **Experimental tree root:** `/home/noneya/code/testme/`.

Config and source locations (relative to whichever root is being
edited; both trees have the same top-level layout):

- `configs/busybox.config` — tracked canonical busybox config.
  Enabled applets: `cat`, `cp`, `cut`, `echo`, `head`, `ls`,
  `mkdir`, `mv`, `od`, `pwd`, `rm`, `rmdir`, `sort`, `stat`,
  `tail`, `tee`, `test`, `touch`, `tr`, `uname`, `uniq`, `wc`,
  `cmp`, `grep`, `sed`, `vi`, plus `ash`.  Off (with reasons):
  `diff` (deliberate), `chmod`/`ln`/`mount` (need kernel work).
- `userland/musl/` — tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  — gitignored; rebuild with `./toolchain/install_musl.sh`.
- `toolchain/{install_musl.sh,musl-gcc.sh}` — tracked.

Kernel sources:

- Real project kernel: `/home/noneya/code/donix/04_kernel_64bit/`.
- Experimental kernel: `/home/noneya/code/testme/04_kernel_64bit/`.

Scratch copies from earlier exploring:

- `/home/noneya/code/x/` — an older scratch copy.  Fully ported;
  safe to delete whenever.
- `/home/noneya/code/y/` — a second scratch copy used for a
  bisection experiment.  Safe to delete.

**`testme` still has unported changes** from the session-33 work:
`configs/busybox.config` (the extra applets), and the deliberate
non-port of `FEATURE_ALLOW_EXEC` and `env`.  See "Not carried
forward" above.

Docs live under `docs/` in each tree; see below.

---

## Where things live

Not needed to start a session; ask for a file when the current task
needs it.  Paths below are relative to a tree root
(`/home/noneya/code/donix/` or `/home/noneya/code/testme/`).

- `docs/strategy.md` — Phase A/B plan, rules, files-not-to-touch,
  tagging convention, git hygiene, recovery.
- `docs/gotchas.md` — every bug writeup, by subsystem.
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
`userland/musl/`.  `v0.6.5` is published: shell scripts run,
redirection works, fds 0/1/2 are first-class, and busybox runs a
broad set of file and text utilities including `uniq`.  Next:
`pipe(2)` — the biggest remaining shell gap.  One change at a
time.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
