Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-09-28 (session 28)
**Current HEAD:** tag `20260928-09`, branch `dev`
**Last milestone:** `v0.6.2` (published)
**Next milestone:** undecided; candidate `v0.6.3` or `v0.7.0`

Commits are named by tag only, never by SHA.  Working tags are
local and permanent -- `git show <tag>` always resolves.  The
commit record is `docs/session-log.md`; the commit message carries
the narrative.

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 syscall ABI, natively. Newlib is gone.
Userland is a tracked source tree at `userland/musl/`. Phase B
(busybox) is underway.

Full strategy, rules, and the one-change-at-a-time discipline:
`docs/strategy.md`.

---

## Current milestone

**`v0.6.2` -- Phase B: `busybox ash` is interactive, and `ls <file>`
works in both shells.**  `sys_ioctl` answers `TIOCGWINSZ` (what ash
actually probes), busybox is built with `FEATURE_EDITING=y`, and the
musl `ls` port stats its argument before `opendir`.

Session 25 added `sys_mkdir` and enabled
`FEATURE_EDITING_HISTORY=256`.

Session 27 found that the kernel's syscall table had diverged from
the Linux x86_64 ABI in two places -- `mkdir` was at 7 (Linux:
`poll`) and `setsid` was at 107 (Linux: `geteuid`).  Both had been
guessed from the "Unknown syscall: N" diagnostic rather than read
from the canonical table, so each handler sat at a number no
correct caller uses and shadowed a different syscall.  Corrected
`mkdir` to 83 and `setsid` to 112 (tag `20260928-05`); busybox
`mkdir` now works end to end.  The previously masked gaps --
`poll(2)` at 7 and `geteuid(2)` at 107 -- were exposed.

**Session 28: `poll(2)` implemented (syscall 7).**  `busybox ash`
stays interactive across many commands with no `Unknown syscall: 7`
noise.  The handler blocks on fd 0 when `timeout < 0`, mirroring
`sys_read`'s fd-0 path (`cli` / `state = BLOCKED` with
`BLOCK_KIND_NONE` / `sti; hlt`), and `irq1_handler`'s
`process_wake_all_blocked` wakes it on the next keystroke.  A
non-blocking first version was rejected: ash interprets
`poll(fds, 1, -1)` returning 0 as end-of-input and exits, and on
real Linux that return combination is unreachable.  Also added
`kbd_buffer_has_data()` to `keyboard.c` -- a non-destructive
readability check that must not consume a byte, because the caller
is about to `read(0)` itself.  Tag `20260928-08` (kernel), then
`20260928-09` (docs).

---

## Canary state (focused canary green as of `20260928-09`)

The **focused canary** is the default. Run it on every change:

    hello
    ls
    ls hello-world.txt
    memtest
    musl_fork
    musl_exec2
    musl_wait
    busybox ls
    busybox ash
    # at the ash prompt: ls, echo hi, exit
    # back at donix>: hello

The **full canary** (milestone-only) adds: `echo`, `cat`,
`musl_stat`, `musl_min`, `musl_malloc`, `musl_printf`, `musl_exec`,
`musl_readdir`, `musl_r10probe`, `brk_verify`, `brkraw`, `brkgrow`,
`musl_dup2`, `musl_dupfd`, `musl_ids`, `musl_getcwd`,
`busybox echo`, `busybox pwd`, `busybox wc hello-world.txt`.

All rows green as of `20260928-09`.  Full per-test notes:
`docs/session-log.md`.

**Canary rows must not mutate the disk.**  `busybox mkdir` was
verified once, end to end, at tag `20260928-05` (directory created,
visible in `ls` as `<DIR>`, then manually removed before the next
image build).  It is deliberately NOT a standing canary row:
every run would leave a directory behind, and the image would fill
or a name would collide with `FR_EXIST` over time.  A write-path
canary row should be added only once `rmdir`/`unlink` exists to
clean up after it -- pair them (mkdir + rmdir) rather than adding
mkdir alone.

**Known expected noise (not canary failures):** running
`busybox ash` prints `Unknown syscall: 107` once at startup
(`geteuid`).  That is the last remaining gap from session 27's
audit, tracked in `docs/open-issues.md`, and is the next work
item.  `Unknown syscall: 7` no longer appears -- poll is
implemented and blocks correctly.

---

## Next step (exactly this, then stop)

**Session 29.**

1. **Implement `geteuid(2)` -- syscall 107.**  Trivial: return a
   fixed uid.  1000 is fine (matches the typical Fedora user, and
   nothing on donix checks it).  This stops the one-time
   `Unknown syscall: 107` at ash startup.  Add `#define SYS_GETEUID
   107` to `include/syscall.h`, a `long sys_geteuid(void)` in
   `user_syscall.c` next to `sys_setsid`, and one `case SYS_GETEUID:`
   in `syscall_dispatch`.  One change, one commit, one tag.

2. **If time permits, write the syscall-table audit script.**
   `docs/open-issues.md` item 4.  The two divergences session 27
   found were found by accident.  A script that parses
   `syscall.h`'s `#define SYS_* N` lines and diffs the numbers
   against the canonical Linux x86_64 table
   (`arch/x86/entry/syscalls/syscall_64.tbl`) would catch the next
   one before it burns a session.  Separate commit.

3. Stop.  The user-mode `#PF` test binary, the busybox applet
   symlinks, and `FEATURE_TAB_COMPLETION` are next-next.

---

## Open issues (top 3; full list in `docs/open-issues.md`)

1. `geteuid(2)` (syscall 107) not implemented -- ash calls it once
   at startup.  The next step above.
2. The user-mode `#PF` kill path (`fault_kill_current(0x0E)` in
   `isr14_handler`, tag `20260928-04`) is in but unverified
   end-to-end -- needs a test binary that dereferences a bad
   pointer without setting `g_expect_fault`.
3. Busybox applet symlinks not installed on the FAT volume (bare
   `mkdir` from ash fails with `mkdir: not found`, only
   `busybox mkdir` works).

Also open: syscall-table audit script (item 4 above); fork is
O(~6 MB) per call; `sys_newfstatat` (262) not implemented;
`sys_munmap` is a stub; `sys_brk`'s fixed `heap_base` and the
4 MB mmap window are latent collisions; `musl_wait` emits
hundreds of progress dots before its children exit.

---

## State on disk

- `configs/busybox.config` -- tracked canonical busybox config.
  Edit only this copy; `userland/musl/Makefile` installs it to
  `third_party/busybox/.config`.
- `userland/musl/` -- tracked musl userland (`apps/` 6, `tests/`
  19).  `build/` gitignored.
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  -- gitignored; rebuild with `./toolchain/install_musl.sh`.
- `toolchain/{install_musl.sh,musl-gcc.sh}` -- tracked.
- `docs/` -- reference material, see below.

---

## Where things live

Not needed to start a session; ask for a file when the current task
needs it.

- `docs/strategy.md` -- Phase A/B plan, rules, files-not-to-touch,
  tagging convention, git hygiene, recovery.
- `docs/gotchas.md` -- every bug writeup, by subsystem.  Lookup
  material; grep when you hit a specific problem.  **Session 28
  added: "poll(fds, 1, -1) never returns 0 on Linux; busybox ash
  treats 0 as end-of-input."**
- `docs/session-log.md` -- commit tables (tag-only) and per-test
  canary notes.  Append a row per commit.
- `docs/open-issues.md` -- full open-issues list and deferred
  cleanups.
- `docs/migration-history.md`, `docs/dons-os-history.md` --
  historical narrative (A1-A6, pre-fork).
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` -- frozen at
  `v0.6.0`.

---

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  Phase B: `busybox ash` is interactive across
many commands with no per-keystroke syscall noise -- session 28
implemented `poll(2)` (syscall 7), which blocks on fd 0 with
`timeout < 0` using the same `cli`/`BLOCKED`/`sti; hlt` pattern
`sys_read` uses.  Next: implement `geteuid(2)` (syscall 107) to
close the last gap from session 27's syscall-number audit.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
