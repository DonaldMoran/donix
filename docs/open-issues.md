# Open issues

**Last updated: 2026-10-18** (HEAD `3b8d13c`).  This file lists
**only open issues**.  Closed items are removed, not struck
through; their history is in `git log` and `docs/session-log.md`.
An item appears here only while it is a defect, a missing
feature, or an unverified claim about the tree.

Read top to bottom.  The numbered items are the long-standing
list; the un-numbered entries below them are smaller and were
filed later.  Numbers are stable — a closed number is retired,
not reused.

---

## Open items

**5.  `-EPIPE` is delivered without `SIGPIPE`.**  `sys_write` on
a pipe with no reader returns `-EPIPE` (32), matching Linux's
errno.  Real Linux also raises `SIGPIPE` first, which by default
terminates the process before `write` returns.  donix's signal
path is a stub (item 12), so the signal is not delivered and the
process sees the errno instead of dying.  A program that checks
`write`'s return value sees the right answer; a program that
relies on dying from `SIGPIPE` does not.

The exposure is narrow: busybox apps generally check `write`'s
return, and `yes | head -n 1` does not hang.  The remaining
exposure is a program that expects to be *killed* by `SIGPIPE`
and does not check `write` — none has been found.

Fixing this means implementing signal delivery (item 12).  Same
subsystem a Wayland `wl_shm` client needs for `SIGBUS` on buffer
overrun.  Doing it once serves both.

**8.  Symlinks: recorded design, not scheduled — and buildable.**
FAT16 has no native symlink storage, and donix is committed to
FAT.  The correct frame is Unix semantics, not FAT storage: if
`ln -s`, `readlink`, `realpath`, and archive restoration all
behave correctly, the kernel is semantically correct.

The encoding is a magic marker in an ordinary file — e.g.
`DONIX_LINK:/usr/bin/busybox` — hidden entirely inside the
pathname dispatch seam.  Do not implement standalone symlink
handling in individual syscalls (`open`, `stat`, `lstat`,
`execve`, `chdir`, `access`); that is the technical debt the
seam exists to prevent.  The seam exists (session 44), so this
is buildable as the seam's next consumer.

Payoff when it lands: `ln`, `link`, `readlink` with real
targets, `realpath` correctness, `tar`/`unzip` link restoration,
and the quiet assumptions (`/bin/sh -> busybox`) that many
configure scripts make.

**9.  No privilege model: uid/gid are 0 for every process.**
No per-process uid/euid split, no setuid bit (FAT has no mode
bits to hold one), no `chown`, no way to become root.  In that
world 0 is the honest answer: everything runs as root.

    getuid   (102) -> 0
    getgid   (104) -> 0
    geteuid  (107) -> 0
    getegid  (108) -> 0
    getgroups(115) -> 0 groups

`/etc/passwd` and `/etc/group` each carry one `root` entry so
the applets that resolve a number to a name (`id -un`, `whoami`,
`ps`'s USER column, `groups`) print `root` instead of a bare
number or an error.

This is temporary and it is the value that changes when a
privilege model lands.  That session adds `setuid`/`seteuid`
(105/117), a setuid-root marker on a FAT-hostile filesystem
(the same magic-marker shape as item 8), a `sudoers` equivalent,
and then changes these five to return the unprivileged default
(1000) with root as an escalation.

**10.  `CR4.OSFXSR` is set, but the kernel saves no SSE state.**
Userland compiled by `toolchain/musl-gcc.sh` can emit SSE; the
kernel is built with `-mno-sse -mno-sse2 -mno-avx -mno-mmx` and
contains no `fxsave`/`fxrstor`/`xsave` anywhere;
`context_switch.asm` saves no XMM registers.  But `kmain.c` sets
CR4.OSFXSR (bit 16), which tells the CPU that the kernel *does*
save SSE state.  So a user process that uses an XMM register can
have it clobbered across a context switch or an interrupt.

The session-52 stopgap added `-mno-sse -mno-sse2 -mno-avx
-mno-mmx` to the userland CFLAGS: the userland ABI now matches
what the kernel preserves, so no userland binary emits SSE and
the corruption cannot happen.  **It is not a fix.**  The real fix
is for the kernel to save and restore XMM state on context
switch and on the interrupt path, then to remove the
`-mno-sse*` flags from the userland build.

No test currently fails because of this; the corruption is
latent, and the surface is *any* 64-bit computation that gcc
chooses to vectorize.

**12.  Signal delivery is a stub; a faulted process cannot catch
its own signal.**  `sys_rt_sigaction` returns 0 and installs
nothing; `sys_rt_sigprocmask` returns 0 and does nothing.  A
process that faults is killed; a process that receives `-EPIPE`
on a write with no reader (item 5) sees the errno and is not
killed by `SIGPIPE`.

The exit path is honest: a faulted child's wait status is a
signal-kill encoding, so the parent's shell prints `Segmentation
fault` and can tell a crash from a clean exit.  But the signal
is not delivered to the process itself.

A minimal instrument exists — a first-party process can install
a `SIGSEGV` handler via a minimal `sys_rt_sigaction`, fault, and
run it (`sigsegv_probe`, staged as `/usr/bin/SIGSEGV_PROBE`).
The handler does not return; there is no `rt_sigreturn`, no
masks, no `SA_*` flags, no `oldact`, no queued signals, no
cross-process delivery.  **That is an instrument, not this
subsystem.**

Fixing this means implementing signal delivery: a real
`sys_rt_sigaction`, per-process signal handlers, a raise on the
fault path that runs the handler if installed, an `rt_sigreturn`
that restores the frame after the handler returns, and a
`SIGPIPE` raise on the `-EPIPE` write path.  That is a
subsystem, not a small change.  Same subsystem a Wayland
`wl_shm` client needs for `SIGBUS` on buffer overrun.  Doing it
once serves both.

**13.  `CONFIG_ASH_JOB_CONTROL` gap.**  busybox ash's
job-control code is gated behind `CONFIG_ASH_JOB_CONTROL`, which
this build does not set.  When a child faults and `sys_wait4`
returns a signal-kill status, ash's default message is printed,
but the shell does not do job-control bookkeeping (no `[1]+
Done` lines, no `fg`/`bg`, no `kill %1`).  It needs the same
signal delivery as item 12 plus the job-control surface
(process groups, a controlling terminal, `tcsetpgrp`).  Not
small.

**14.  Shebang (`#!`) and `/bin/sh` are missing.**  `./script.sh`
works only because busybox ash falls back on `ENOEXEC` and runs
`sh script.sh`.  The kernel's `sys_execve` opens the file, checks
for ELF magic, finds `#!`, prints `sys_execve: not an ELF file`,
and returns `-ENOEXEC`; there is no shebang handling.  And
`/bin/sh` does not exist on the image — `/bin` contains only
`busybox`.

Both must land together: `#!` parsing in `sys_execve` (read the
first line, build argv `[interp, optional_arg, script, ...]`,
recurse with a depth guard) **and** an interpreter on the image
(a copy of busybox at `/bin/sh`, or a kernel alias — a decision,
not a one-liner).  Fixing only the kernel gives `ENOENT` instead
of `ENOEXEC`, still a fallback.  Fixing only `/bin/sh` does
nothing, because the kernel never looks at the shebang.

This is a **feature gap**, not a bug: `./test.sh` runs, and the
printed line is the kernel correctly reporting that the script is
not an ELF.  It becomes a defect when a script's interpreter is
not a shell (e.g. `#!/usr/bin/python`), where there is no
fallback.

---

## Un-numbered open items

**Item 7a — the boot-time `#PF` at `0x400000`.**  Unobserved, not
fixed.  An intermittent fault that stops reproducing is not
closed.  The `20261003-vmm-map-diag` instrumentation (the three
silent `return -1;` sites in `vmm_map_page_in_cr3` each print
`VMM: map failed site=<NAME> virt=... cr3=... free=NNN`) is in
the tree and did not fire on the boots that followed.  When it
returns, the `free` field separates the two candidates: a healthy
count with `site=PDPT` or `site=PD` is a missing clone table; a
near-zero count is PMM exhaustion in a shape the session-48 wrap
does not cover.

**Item 7b — a user-mode `#GP` at `0x42F1A7` in busybox during
`find`.**  Unobserved, not fixed.  Different vector, different
location, different phase from 7a.  A user `#GP` with error `0`
is raised for a privileged instruction, a non-canonical address
in a base register, or a segment violation.  It needs its own
instrument — a first-party reproduction of the `find` sequence,
or a kernel-side `#GP` handler trace — before it is guessed at.
It is not the same fault as 7a and must not be folded into 7a.

**Item 7e — ash's stale `jmp_buf`.**  The kernel-side faces of
7e are closed (the pipe-wake face by `c3e5c97`; the
heap-corruption face by `2fc7fde`; the pipe-reader lost-wakeup
face by `7aaaafd`; the `test.sh` row-30 `C` by `d3c7f20`; the
four dispatch guards by `8dc47cf`, `85da375`, `e7e3e6b`,
`d7d8c40`; the `C` itself by `ef85962`).  What remains is
`third_party`: `evaltree` stores a pointer to its own stack
frame's `jmp_buf` into `globals+0x38` and restores it only on
the `longjmp`-return path, not on the normal return.
`raise_exception` then `_longjmp`s to that global with no
liveness check.  First-party reproduction: `pipe7e_sjlj_stale`
(`/usr/bin/PIPE7E_SJLJ_STALE`).  **The mechanism is
`third_party/`'s and is not editable.**

**`readdir("/dev")` fails; `readdir("/proc")` works.**  `/proc`
is a listable directory; `/dev` is not.  `ls /dev`, `ls /dev/`,
and `ls dev` all fail with `ENOENT`.  Adding it is the same
directory shape `/proc` got: a synthesized entry list in
`sys_getdents64`, and an `open_resolved`/`stat_resolved` branch
for the prefix.  The mechanism is proven and the pattern is
established.  This is what would let `tty` and `null` appear in
`ls /dev`, and it is the prerequisite for `/dev/tty` and
`/dev/urandom`.

**`st_rdev` is 0 on device nodes.**  `ls -l /dev/console` prints
`0, 0` for the major/minor column; a real Unix prints `4, 0`.
`fill_kstat_as_chardev` sets `st_dev` and `st_ino` but not
`st_rdev`.  Cosmetic: `tty` does not read it.

**No `/dev` directory.**  `ttyname(3)` names the console —
`readlink("/proc/self/fd/0")` returns `/dev/console` and the
`(st_dev, st_ino)` match passes, so `tty` prints `/dev/console`.
But `/dev` is not a directory, so `readdir` on it fails;
`/dev/tty` and `/dev/urandom` do not exist; and
`/dev/console` is stat-able but not openable
(`open("/dev/console")` returns `-ENOENT`).

**`open("/proc/<pid>", O_DIRECTORY)` is not done.**  `ps`
*stats* the per-pid directory; it does not *open* it.
`ls /proc/1` and `opendir("/proc/1")` would need
`open_resolved` to accept the same two paths `stat_resolved`
now does, producing a `FILE_KIND_DIR` slot with
`PROC_DIR_SENTINEL` — the mechanism already built for `/proc`
itself.  Small.

**A nonexistent pid stats as a directory.**  `stat("/proc/999999/")`
reports `S_IFDIR` rather than `ENOENT`, because the check is on
the path shape, not on `process_find_by_pid`.  `ps` only stats
pids `readdir` gave it, so it does not affect the consumer.
Worth one line when `/proc` is next touched.

**`sys_gettimeofday` (99) is not implemented.**  `clock_gettime`
(228) is, and is what musl reaches for in most cases, but
`PS_LONG`/`PS_TIME` in `ps` call `time()`/`localtime()`, which
reach 99, and `ps -l` therefore hits `Unknown syscall: 99`.  A
small syscall from `g_ticks`, like `clock_gettime`.

**A live pipe slot's `end` field is 0; no writer is in the
source.**  Latent, not active.  A `file_slot_t` of kind
`FILE_KIND_PIPE` was captured with `refcount = 2` and
`slot->end = 0`, at a fixed kernel-heap address.  `PIPE_END_READ`
is 1 and `PIPE_END_WRITE` is 2; `0` is neither.  The three writes
to a pipe slot's `end` in `user_syscall.c` all write 1 or 2,
never 0.  The writer is not in the source.

The observable consequence is closed: `put_file_slot` refuses to
free a pipe when a slot's `end` is neither READ nor WRITE — it
records the event, frees the slot, and leaves the pipe alive.  A
leak is survivable; a lost wakeup is not.  No reproduction since
the 2026-10-14 work.  If it recurs, the next step is a
guard-band: write a magic value to `slot->end` at slot creation
and check it at every `put_file_slot`; the window between the
last good read and the first bad one names where the write
landed.

**`exec_free_and_unmap_user_pages` walks a fixed 1 MB brk
window.**  `user_syscall.c:4358` lists the brk region as
`[0x8000200000, 0x8000300000)`.  But `sys_brk` has no upper
bound, and `sys_fork`'s copy walks
`[0x8000200000, parent->brk_virt)`, the real bound.  A parent
whose `brk_virt` has grown past `0x8000300000` forks a child
that has pages the exec cleanup will neither unmap nor free.
Leak, not corruption.  Extend the cleanup row to the live bound,
or walk `elf_page_list` and unmap by VA from the list itself.

---

## Also open

- `sys_brk`'s fixed `heap_base` and the 4 MB mmap window are
  latent collisions.
- Real FatFs timestamp storage: the three timestamp syscalls
  return 0 without storing.
- `prctl` is minimal (`PR_SET_NAME` accepted and dropped).
- Busybox applet symlinks not installed.
- Syscall-table audit script.
- `musl_wait`'s WNOHANG loop spins.
- `sys_mmap` rejects all non-anonymous mappings (a file-backed
  `mmap` caller gets `-ENOMEM` and must fall back to `read`; a
  Wayland prerequisite).
- Pipes support one concurrent reader and one concurrent
  writer (see `pipe_t`'s comment in `user_syscall.c` — a second
  blocked reader on the same pipe end has nowhere to record
  itself and will only wake on a keyboard IRQ).
- `put_file_slot`'s pipe wake is coupled to `sys_close`'s wake
  (read the `put_file_slot` comment before touching either).
- `process_dump_all`'s `DETACHED` state: a process READY with
  `prev == NULL && next == NULL` is labeled DETACHED, which may
  mask a stranded-process bug.

---

## Test-design notes

> **This section does not belong in this file.**  It should move
> to `handoff.md`'s canary section in the next documentation
> round.  It is here because it was here.

- **The old `musl_sh` ash-only caveats are gone.**  Redirection
  and pipeline tests are valid from `donix>` as well as ash.
- **Framebuffer / `vi` tests are one-offs, not canary rows.**
  `vi` mutates the disk and takes over the screen.
- **Pipe regression suite is not a canary.**  `pipe_step1` …
  `pipe_step3b` fork and take seconds.
- **`at_step2` is not a canary either.**  It mutates the disk.
  `at_step1` is read-only.
- **`readlink_errno` is read-only and fast**, run by hand.
- **`proc_status` and `proc_fd` are read-only and fast**, run by
  hand.
- **`proc_dir`, `proc_stat`, `proc_walk`, `proc_walk_fds`,
  `mmap_stress`, and `exec_churn`** are run by hand.
  `exec_churn` needs its helper staged as
  `/usr/bin/CHURN_HELPER`, and is the test that exercises
  `process_free_clone` on the process-exit path.
- **`test.sh`** is the ~58-row harness at `/root/scripts/test.sh`.
  Run it by hand when changing the applet config or the
  pipe/wait paths.
- **`mmap_nx`** proves `sys_mmap`'s `PT_NX` gate is enforced.
  Run it when changing `sys_mmap`, the `PROT_*` handling, or the
  leaf-PTE construction.
- **`sigsegv_probe`** installs a `SIGSEGV` handler via a minimal
  `sys_rt_sigaction`.  The handler does not return.
- **`at_step1` sections**, as of session 41: sections 1–7
  `resolve_at`; 8–9 `faccessat`; 11 `utimensat`.  (There is no
  section 10.)
- **The canary is now `canary`, a program.**  `canary` (read-only)
  and `canary --full` (also the mutating rows).  15/15 and 28/28.
