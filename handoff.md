# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-30 (session close).  **`v0.6.18` is
released; work is pre-`v0.6.19`.**  This session fixed the boot hang
that blocked the tty side: **the shell now reaches a prompt and takes
input.**  Baseline on `HEAD`: `canary --full` 28/28, `test.sh` 57/57,
prompt + `echo hi` works, and `HEAD` builds alone.  `dev` is ahead of
`origin/dev` by 35 commits, nothing pushed.

**Read "The concurrency contract" in `docs/contract.md` before
touching shared state, then "Where we are".**  The 2026-10-30 boot
hang and how it was found are in `docs/2026-10-30-ash-boot-loop.md`;
this file carries only the conclusions.

**THE TWO NEW COMMITS THIS SESSION** (on `dev`, tagged, nothing
pushed):

- `4d7f09e` `tty: open(/dev/tty) yields a console slot; tcgetpgrp
  works` -- `dev_open` builds a `FILE_KIND_CONSOLE` slot for the tty
  major (5), so an opened `/dev/tty` (fd 10) is a tty to
  `fd_is_console`, `sys_read`'s fd-0 guard, and `sys_write`'s
  fd-1/2 guard.  Before it, `tcgetpgrp` was `-ENOTTY` and ash printed
  `can't access tty; job control turned off`.
- `3e8bfc5` `tty: TIOCGPGRP answers with the caller's pgrp until
  TIOCSPGRP; the wire type is pid_t` (tag
  `20261030-tty-pgrp-boot-fix`) -- the ash startup loop
  `tcgetpgrp(0) != getpgid(0)` is closed by the lazy foreground-pgrp
  set and the 4-byte `pid_t` wire type; also adds `getpgid` (121),
  `getpgrp` (111), `setpgid` (109) over `pcb_t.pgrp`, `setpgid`'s
  write under `cli`.  **The pgrp syscalls and the ioctl fix are a
  pair** -- either alone leaves ash looping.
- `abccc8c` `scheduler: the item-7d scan requeues a stranded RUNNING
  process` (tag `20261030-scan-stranded-running`) -- both copies of
  the scan carried a comment saying "or stranded-RUNNING" and a
  predicate testing only `READY`.  A real code/comment divergence,
  **not** this hang; the commit says so.  Insertions only.

Plus the previous session's restore tags (`20261029-jobctl-*`,
`20261029-slice6-*`, `20261029-nanosleep-blocks-verified`,
`20261029-idle-dispatch-fixed`).  **Nothing is pushed.**

**WHAT THE SHELL IS, in one paragraph.**  The boot shell is
**`musl_sh`** (`userland/musl/apps/musl_sh.c`), which **forks and
execs `/bin/busybox sh`** and waits for it.  The PCB name of the
exec'd process stays `musl_sh`; the banner is busybox's.  So the boot
path is `musl_sh` -> fork/exec `busybox sh` -> ash.  The old
handoff's "NEXT is the busybox ash tty side" assumed ash is the boot
shell; it is the exec'd shell.  **This matters for job control:**
ash's `tcgetpgrp`/`setpgid` startup runs as the exec'd child, and the
foreground pgrp is the console's, set lazily by the first
`TIOCGPGRP` and overridden by `TIOCSPGRP`.

**WHAT THE VFS IS, in one paragraph.**  `g_mounts` maps `/` (FAT),
`/dev`, and `/proc` to `fs_driver_t` vtables.  `vfs_resolve`
canonicalizes, selects the mount by longest prefix, and follows a
`DONIX_LINK:` marker when `follow_final`.  Devices are rows in
`g_dev_table`; `/dev/null` is 1,3; `console` 5,1; `tty` 5,0; `zero`
1,5; `urandom` 1,9.  **An opened console/tty is now a
`FILE_KIND_CONSOLE` slot** (major 5), not a generic DEV slot --
`4d7f09e`.

**WHAT IS NOT DONE.**  **Job control's tty side** -- `Ctrl-Z` does
not raise SIGTSTP, the foreground pgrp is not used by the keyboard
IRQ, and `console_read` does not check `sig_pending` (it blocks with
`BLOCK_KIND_NONE`).  The kernel state machine (`PROC_STATE_STOPPED`,
stop/resume, SIGCONT) is in from 2026-10-30's predecessor.  A real
`mount(2)` (the table is `static const`); file-backed `mmap`;
`link(2)`/`mknod(2)`; **shebang (`#!`) is not parsed by
`sys_execve`** (`./script.sh` works only via busybox ash's `ENOEXEC`
fallback); `chmod`/`chown` accept and drop.  SSE state is not saved
(item 10).  No privilege model (item 9).

**WHERE WE ARE.**  `v0.6.18` (signals slices 1-5) is the last
release.  This session added the tty boot fix and the pgrp syscalls,
and the shell now reaches a prompt and takes input -- the prerequisite
for all job-control work.  The working tree is `dev`, ahead of
`origin/dev` by **35 commits**, nothing pushed.
`configs/busybox.config` (`CONFIG_ASH_JOB_CONTROL=y`) is modified,
uncommitted, waiting for the busybox rebuild (item 04).  `handoff.md`
is this file.

**NEXT.**  Job control's tty side, now unblocked:

1. **`Ctrl-Z` -> SIGTSTP to the foreground pgrp.**  `console_read` /
   `keyboard` maps `Ctrl-Z` (0x1A) to SIGTSTP and sends it to
   `g_console_fg_pgrp` (`user_syscall.c`), and the other control
   chars to their signals.  **This is the new async writer** -- the
   contract's Signals section applies: name the safe point (the
   existing syscall-return / iretq delivery points), keep the
   `sig_pending` set and any wake under `cli`, route any dispatch
   through `scheduler_switch_to`.  The foreground pgrp is the one
   `TIOCSPGRP` wrote; `pcb->pgrp` is the process group.
2. **`console_read` interruption.**  It blocks with
   `BLOCK_KIND_NONE` and has no `sig_pending` check; a blocked
   `read(0)` must return `-EINTR` on a caught signal, like the pipe
   read/write paths.
3. **The busybox rebuild** from `configs/busybox.config`
   (`CONFIG_ASH_JOB_CONTROL=y`).  Commit the config flip with the
   rebuild; the flip alone is not meaningful.
4. **Shebang + `/bin/sh` kernel half** (open-issues item 14).
5. **SSE** (item 10): save/restore XMM, then remove `-mno-sse*`.
6. **Privilege model** (item 9).
7. busybox batch 2; `test.sh`'s md5.
8. **A completed PIPE7E soak** at 100 Hz.

### Working style -- how this project gets changed

Read this before proposing any command block.

**One change at a time.**  A "change" is a commit, verified before the
next one starts.  **Verify by RUNNING, not by committing** -- build,
run, see the behavior, THEN commit.

**The state check comes before the command block.**  Every command
block starts with `git status -sb` (or `git log`).

**A command block is one paste -- write it to a file first.**
`git add`, `git status`, `git commit`, and `git tag` belong in
**one** block, with the message inline via heredoc.  **Do not paste a
block with `cd ... || exit 1` into an interactive shell** -- the
`exit` closes the terminal.  Write to `/tmp/<name>.sh` with
`cat > ... <<'SCRIPT_EOF'`, then `bash /tmp/<name>.sh`.

**Python edits with `count != 1` abort checks are the preferred edit
style.**  `set -e` at the top.  Idempotent.  **Do the count check IN
PYTHON (`str.count`)**, not with `grep`.  **Before asserting a count,
count the occurrences in the file and write the expected number from
that count, not from memory.**

**ANCHOR ON STRUCTURE UNIQUE TO THE TARGET FUNCTION.**  Grep the file,
confirm the count is 1 **and** the match is in the intended function.
**A post-write check string must be copied from the `new` literal,
not retyped.**  **And an anchor must be read from the file's current
bytes (`cat -A`), not from memory of what was written a few turns
ago.**  This session's three failed edit scripts (`assert n == 4`,
`did not land on comment opener`, a mis-indented predicate) were all
memory-based; the ones that worked were line-number or `cat -A`
based.

**Ask for source you do not have.**  Before patching a file it has
not seen **in this session**, ask for it.

**The build is TWO steps.**  `make -C 04_kernel_64bit
FAT_CONFIG=single`; `make -C 05_boot_kernel64 hdd-single.img`.
Running only the second copies a STALE `kernel.bin`.  Verify
`kernel.elf` is newer than the source you edited.

**`EXTRA_CFLAGS` is NOT forwarded by `04_kernel_64bit/Makefile`.**
`SW2_TRACE` is a one-line `#define` in `scheduler.c`.  `make
EXTRA_CFLAGS=-DSW2_TRACE=1` silently does nothing.

**`nm` output moves between builds.**  Re-run `nm` before using any
address.  `current_process` has **two** symbols (process.c and
scheduler.c).  `pcb_pool` stride is `0x840`.

**GDB over `-s` has no types** (`kernel.elf` is built WITHOUT `-g`).
Use raw offsets: `pid` 0x000, `name` 0x008, `state` 0x028, `cr3`
0x030, `entry_point` 0x038, `kernel_stack_virt` 0x048,
`kernel_stack_top` 0x050, `rsp` 0x110, `rip` 0x118, `next` 0x120,
`prev` 0x128, `block_kind` 0x158, `user_rsp` 0x610, `sig_pending`
0x618, `sig_blocked` 0x620, `signal_handler[N]` 0x628 + 8*N,
`pgrp`/`stop_pending`/`in_stop_safe_point` after `signal_restorer[]`.
State values: UNUSED=0, READY=1, RUNNING=2, BLOCKED=3, ZOMBIE=4,
TERMINATED=5, STOPPED=6.  Block kinds: NONE=0, WAITPID=1,
PIPE_READ=2, PIPE_WRITE=3, PAUSE=4, NANOSLEEP=5.

**A commit message is not a diff.**  `git show <sha> --stat` is the
check.  This session's `879c50c` had the `case SYS_*` lines and not
the `#define`s; the stat line and `syscall.h` still `M` named it;
the amend `3e8bfc5` fixed it.  **After any amend, build `HEAD` alone
(stash the working tree) to prove it compiles.**

**Do not run `git reset --hard` without stashing first.  Do not edit
`third_party/`.  Do not commit `kmain.c`'s `pit_init` knob -- the
committed value is 100.  Do not call a race closed on one clean run.**

---

## Releases

**`v0.6.18` is released.**  Work is pre-**`v0.6.19`**.  The release
tags `v0.6.15` through `v0.6.18` carry their summaries.  The
`20261030-*` tags, the five `20261029-jobctl-*`, the six
`20261029-slice6-*`, and the two 2026-10-29 tags are the current
restore points; the release procedure draws the `v0.6.19` summary
from them before dropping them.  **Nothing is pushed** until
`v0.6.19` is ready and verified.

---

## One-line summary

**`v0.6.18` is the last release.  This session fixed the ash boot
hang: the shell reaches a prompt and takes input.**  The boot shell
is `musl_sh` -> fork/exec `busybox sh`; ash loops until
`tcgetpgrp(0) == getpgid(0)`, which required both the pgrp syscalls
(`getpgid` 121, `getpgrp` 111, `setpgid` 109) and `TIOCGPGRP`
answering with the caller's pgrp (lazily, under `cli`) with a 4-byte
`pid_t` wire type.  `open(/dev/tty)` now yields a console slot.  The
two item-7d scans were fixed as a real code/comment divergence (not
this hang).  Gate 28/28 + 57/57 + prompt + input on `HEAD`, and
`HEAD` builds alone.  **Next is job control's tty side** -- `Ctrl-Z`
-> SIGTSTP to the foreground pgrp (the contract's safe-point
statement applies), then `console_read` interruption, then the
busybox rebuild (`configs/busybox.config` still uncommitted),
shebang, SSE, privilege.  Read `docs/contract.md` and "NEXT" in this
file before any code; the boot-hang detail is in
`docs/2026-10-30-ash-boot-loop.md`.
