Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-09-28 (session 30)
**Current HEAD:** tag `v0.6.3`, branch `dev`
**Last milestone:** `v0.6.3` (published) — the shell is fully usable
**Next milestone:** undecided; candidates below.

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

## Where we are — v0.6.3

**The shell is fully usable.**  Boot drops into busybox ash; `exit`
returns to the donix shell; both shells have `cd`, `pwd`, `ls`,
`cat`, and they all respect the working directory.

The cwd story, end to end:

- `chdir(2)` (80) stores an absolute path in `pcb->cwd`.
- `getcwd(2)` (79) returns it; musl's `getcwd()` wrapper accepts it.
- `sys_open`, `sys_stat`, `sys_access` resolve relative paths (`.`,
  `..`, `./x`, `../x`, plain names) against `pcb->cwd` via
  `resolve_against_cwd`.
- `sys_fork` copies `cwd` to the child; `sys_execve` preserves it.
- `ls` and `cat` (donix-native) pass paths through unchanged; the
  kernel resolves them.  (They used to prepend `0:/`.)
- `musl_sh` has `cd`, `pwd`, `exit` builtins, run in the parent.

Also closed this session: `geteuid(2)` (107) and `prctl(2)` (157,
`PR_SET_NAME` accepted and dropped).  **The serial log is free of
`Unknown syscall:` lines.**

Session 30 commits (all on 2026-09-28; the scratch tags were dropped
before the `v0.6.3` push — see `docs/session-log.md` for the rows):

| Commit subject | What |
|---|---|
| kernel: implement geteuid(2) -- syscall 107 | returns a fixed uid |
| kernel: implement prctl(2) PR_SET_NAME -- syscall 157 | accept and drop |
| kernel: implement chdir(2) -- syscall 80 | stores absolute cwd |
| kernel: resolve relative paths against cwd | `resolve_against_cwd`; `sys_fork` copies cwd |
| userland: ls/cat pass paths through | stop prepending `0:/` |
| musl_sh: cd, pwd, exit builtins | the fallback prompt is usable |

### Known limitation

`cd ..` at `donix>` fails (`cd: cannot cd to ..`).  The `musl_sh`
`cd` builtin passes the raw `..` to `chdir`, and FatFs has no `..`
entry.  **`cd ..` inside ash works** (ash resolves `..` against its
own `$PWD` first).  See `docs/open-issues.md` item 2.

---

## Next step (do this first)

**Session 31.  Pick a direction, then one change at a time.**

Candidates, roughly in order of value:

1. **The user-mode `#PF` test binary.**  `fault_kill_current(0x0E)`
   in `isr14_handler` is in but unverified end-to-end.  Needs a
   test binary that dereferences a bad pointer without setting
   `g_expect_fault`.  Small, closes a real gap.

2. **`cd ..` at `donix>`.**  Make `builtin_cd` resolve `.`/`..`
   against `getcwd()` before calling `chdir`, or make `sys_chdir`
   resolve through `resolve_against_cwd` the way the path syscalls
   do.  Small.

3. **`newfstatat` (262).**  Number reserved, no dispatch case.
   musl routes `fstatat` through `stat`/`lstat` on x86_64 for the
   common case, so it is not hit yet, but a caller passing
   `AT_FDCWD` plus flags would reach it.

4. **The VFS layer (larger).**  `sys_execve`'s three-attempt path
   resolution is a shim.  When a VFS lands, delete it.  Do not add
   a fourth attempt; build the VFS.  See `docs/open-issues.md`.

5. **Busybox applet symlinks** on the FAT volume, for `/bin/NAME`
   as a real file.  Standalone mode side-steps this for applets.

Pick one, do it, test it, tag it.  Do not bundle.

---

## Canary state (focused canary green as of `v0.6.3`)

**Boot drops into ash.**  The focused canary reflects that:

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
    busybox pwd                 # /bin (after cd /bin)
    busybox ash
    # at the ash prompt: pwd, cd /bin, pwd, ls, exit
    # back at donix>: hello

**Do NOT add `cd ..` at `donix>` as a canary row** -- it fails (see
Known limitation).  `cd ..` inside ash is fine.

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`; use
`busybox sh` or `/bin/busybox sh`.

The **full canary** (milestone-only) adds: `echo`, `cat`,
`musl_stat`, `musl_min`, `musl_malloc`, `musl_printf`,
`musl_exec`, `musl_readdir`, `musl_r10probe`, `brk_verify`,
`brkraw`, `brkgrow`, `musl_dup2`, `musl_dupfd`, `musl_ids`,
`musl_getcwd`, `busybox echo`, `busybox wc hello-world.txt`.

**Canary rows must not mutate the disk.**  No standing `mkdir`
row; `rmdir`/`unlink` do not exist yet to clean up.

**Expected noise:** none.  The serial log has no `Unknown syscall:`
lines as of this milestone.  The `sys_execve: pid=... (name)`
trace lines are informational, not errors.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. The user-mode `#PF` kill path (`fault_kill_current(0x0E)` in
   `isr14_handler`) is in but unverified end-to-end.
2. `cd ..` at `donix>` fails (the `musl_sh` builtin passes raw
   `..` to FatFs).  ash is unaffected.
3. **VFS layer (eventual).**  `sys_execve`'s path resolution is a
   shim for a filesystem layer donix does not have.  When it
   lands, delete the shim; do not extend it.
4. `newfstatat` (262) has a reserved number but no dispatch case.
5. Busybox applet symlinks not installed; standalone mode
   side-steps this for applets.

Also open: syscall-table audit script (the table itself was
audited in session 30 and is correct; the script would keep it
correct); fork is O(~6 MB) per call; `sys_munmap` is a stub;
`sys_brk`'s fixed `heap_base` and the 4 MB mmap window are latent
collisions; `musl_wait` emits hundreds of progress dots.

---

## State on disk

- `configs/busybox.config` — tracked canonical busybox config.
  Has `FEATURE_PREFER_APPLETS=y`, `FEATURE_SH_STANDALONE=y`,
  `BUSYBOX_EXEC_PATH="/bin/busybox"`.
- `userland/musl/` — tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  — gitignored; rebuild with `./toolchain/install_musl.sh`.
- `toolchain/{install_musl.sh,musl-gcc.sh}` — tracked.
- `~/code/x/` — the exploring copy.  Fully ported; safe to delete
  whenever.  `~/code/y/` — a second copy used for a bisection
  experiment this session; also safe to delete.
- `docs/` — reference material, see below.

---

## Where things live

Not needed to start a session; ask for a file when the current task
needs it.

- `docs/strategy.md` — Phase A/B plan, rules, files-not-to-touch,
  tagging convention, git hygiene, recovery.
- `docs/gotchas.md` — every bug writeup, by subsystem.  Session 30
  candidates to add: the `sys_getcwd` absolute-cwd requirement
  (musl rejects a non-absolute cwd), and the `puts_raw`-vs-`printf`
  newline quirk.
- `docs/session-log.md` — commit tables and per-test canary notes.
  Rows are named by tag; scratch tags are dropped before a
  milestone push, so a row's tag may no longer resolve — the
  commit message is the record.
- `docs/open-issues.md` — full open-issues list.
- `docs/migration-history.md`, `docs/dons-os-history.md` —
  historical narrative (A1-A6, pre-fork).
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` — frozen at
  `v0.6.0`.

---

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  `v0.6.3`: the shell is fully usable -- boot into
busybox ash, `exit` to the donix shell, and `cd`/`pwd`/`ls`/`cat`
respect the working directory in both shells, across fork and exec.
The serial log is free of `Unknown syscall:` lines.  Session 31:
pick one item from Next step and do it.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
