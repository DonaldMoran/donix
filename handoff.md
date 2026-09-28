# donix handoff

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-09-27 (session 25)
**Current HEAD:** tag `20260927-20`, branch `dev`
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

Session 25 added `sys_mkdir` (syscall 7) -- the per-keystroke
`Unknown syscall: 7` noise from ash's line editor is gone -- and
enabled `FEATURE_EDITING_HISTORY=256`.

---

## Canary state (focused canary green as of `20260927-20`)

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
`busybox echo`.

All rows green as of `20260927-20`.  Full per-test notes:
`docs/session-log.md`.

---

## Next step (exactly this, then stop)

**Session 26.**

1. **Fix `isr14_handler` to kill the faulting process on a user-mode
   `#PF` instead of halting the console.**  Today the handler checks
   only `g_expect_fault`; it does not distinguish a user-mode fault
   from a kernel-mode one, so any unexpected user-mode `#PF` ends
   the boot.  Fix: if `(error_code & 4)` and
   `g_expect_fault != 0x0E`, call `sys_exit(-1)` for the faulting
   process instead of halting.  One change, one commit, one tag.
   Payoff: every future Phase B debugging session stops requiring
   a reboot after a user-mode crash.

2. **Re-test the focused canary.**  A user-mode fault that used to
   halt should now kill the process and return to `musl_sh`.

3. Stop.  Milestone decision and `FEATURE_TAB_COMPLETION` are
   next-next.

---

## Open issues (top 3; full list in `docs/open-issues.md`)

1. `isr14_handler` halts on user-mode `#PF` -- the next step above.
2. Fork is O(~6 MB) per call -- eager copy in `sys_fork`; the
   long-term fix is real copy-on-write.
3. `sys_newfstatat` (262) not implemented; `sys_munmap` is a stub;
   `sys_brk`'s fixed `heap_base` and the 4 MB mmap window are
   latent collisions.

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
  material; grep when you hit a specific problem.
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
`userland/musl/`.  Phase B: `busybox ash` is an interactive shell
and `ls <file>` works.  Session 25 implemented `sys_mkdir` and
enabled history.  Next: `isr14_handler`, so a user-mode `#PF`
kills the faulting process instead of halting the console.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
