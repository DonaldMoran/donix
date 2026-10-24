# Open issues

**Last updated: 2026-10-23** (HEAD `20261023-sigign`).  This file
lists **only open issues**.  Closed items are removed, not struck
through; their history is in `git log` and `docs/session-log.md`.
An item appears here only while it is a defect, a missing
feature, or an unverified claim about the tree.

Read top to bottom.  The numbered items are the long-standing
list; the un-numbered entries below them are smaller and were
filed later.  Numbers are stable — a closed number is retired,
not reused.

---

## Open items

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

**13.  `CONFIG_ASH_JOB_CONTROL` gap.**  busybox ash's
job-control code is gated behind `CONFIG_ASH_JOB_CONTROL`, which
this build does not set.  When a child faults and `sys_wait4`
returns a signal-kill status, ash's default message is printed,
but the shell does not do job-control bookkeeping (no `[1]+
Done` lines, no `fg`/`bg`, no `kill %1`).

Signals are done (see handoff): raise, delivery at three points,
dispositions including `SIG_IGN`, the default-action table, and
`SIGCHLD` reaching `wait4` all work.  What is not built is the
job-control surface *on top of* that machinery: there is no
stopped process state (`signal_default_action` maps SIGSTOP and
SIGTSTP to TERMINATE, the documented placeholder), no SIGCONT
resume path, no process groups (`sys_kill` refuses `pid <= 0`),
no controlling terminal, no `tcsetpgrp`/`setpgid`, and
`CONFIG_ASH_JOB_CONTROL` is unset so ash has no job table.  Not
small, but the hard half -- the signal substrate -- is in.

**14.  Shebang (`#!`) and `/bin/sh` are missing.**  `./script.sh`
works only because busybox ash falls back on `ENOEXEC` and runs
`sh script.sh`.  The kernel's `sys_execve` opens the file, checks
for ELF magic, finds `#!`, prints `sys_execve: not an ELF file`,
and returns `-ENOEXEC`; there is no shebang handling.
**The image half is now done**: `/bin/sh` exists as a soft
link to busybox (after the one-time
`/bin/busybox --install -s /bin`; see handoff).  What remains
is the kernel half -- `#!` parsing in `sys_execve`.

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

**`chmod`/`chown` accept and drop, and their applets are not
compiled in.**  Syscalls 90-93 (path and fd forms) return 0 and
store nothing: FAT has no mode bits and no ownership, everything
is uid 0, and `stat` reports the synthesized 0644/0755.  This is
so `tar -x`, `cp -a`, and `install` complete instead of printing
"Unknown syscall" for every file.  It is not a privilege model --
items 9 and 10 own the real answer.

`CONFIG_CHMOD` and `CONFIG_CHOWN` are **unset** in
`configs/busybox.config`, so no command on the image calls 90-93
and `chmod`/`chown` say "not found".  Enabling them would make
the commands run and report success while changing nothing (FAT
has no mode store).  Honoring FAT's read-only attribute via
FatFs's `f_chmod` would be a real but two-state mode change, and
a separate, larger decision.

**`tar` is enabled, and link restoration is verified.**  The
v0.6.16 note that called its creation path a busybox applet
issue was **wrong**: busybox's self-archive check compares the
archive's `(st_dev, st_ino)` against each member's, and every
FAT file reported `Inode: 0`, so every member matched and was
skipped.  `76d66db` gives FAT a real `st_ino` from `(sclust,
objsize)`; `tar -cf`/`-tf`/`-xf` now work, and `tar -tvf`
records a symlink as `lrwxrwxrwx ... -> target`.  One limit
remains: two empty files with the same name in different
directories collide in `st_ino`, because `FILINFO` carries no
parent -- see `fat_ino`.

**`tkill` (200) is implemented as `kill(tid, sig)`.**  Not an
open issue; noted so it is not re-added.  busybox's `kill`
applet, signalling itself, issues raw syscall 200; without a
handler it printed "Unknown syscall: 200" and SIGINT's default
action could not be exercised.  `tkill(tid, sig)` is
`kill(tid, sig)` in a single-threaded process, implemented as a
thin wrapper over `sys_kill` so the raise path stays
single-sourced (`b976b40`).

**The `-120` frame-base constant is consistent with what is
tested, not proven.**  `signal_deliver_frame` and
`sys_rt_sigreturn` both compute the frame base as
`g_syscall_stack_top - 120`, while `user_syscall_entry.asm`
pushes 128 bytes (48 callee-saved + 16 RIP/RFLAGS + 8 user RSP
+ 16 r8/r9 + 32 rdi/rsi/rdx/r10, plus a return address and a
discarded arg5 slot).  The signal gate passing means the
constant agrees with every tested delivery point; it does not
prove the derivation.  Any change to the stub's pushes or to a
delivery point must re-derive it, and a change to one without
the other is a bug.

**The pipe lost-wakeup equality is a standing question, not a
live bug.**  `wake_no_waiter == writers_nonzero` was observed on
two 2026-10-23 captures (`P12`: 8 and 8, and 2 and 2) and not on
the `20261023-sigign` boot (`P12`: 0 and 0).  The reader's
self-clear of `pipe->reader_waiting` was removed at `7aaaafd`,
which closed the lost-wakeup mechanism; the equality itself was
never explained.  Do not chase it as a signal bug and do not
call it closed on one clean run: it is a standing question about
the pipe wake counts, separate from signals.  If it reproduces
as a hang, the ring's `P13` events are the place to read.

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

**Observed again, 2026-10-23, and not reproduced.**  A user-mode
`#PF` fired during 5000 iterations of `echo hi | wc` in a boot
that had first run `sigchld_probe`, `kill_self`, `sigsegv_probe`,
and `sigterm_ignore`.  CR2 `0x876CA6`, RIP `0x413962`, error
`0x5` (present + user + read), the CR2 inside the 2 MB page at
phys base `0x800000` — a user-mode read of a kernel physical
address, which is the 7b/7e signature.  The boot's capture
contained exactly one `#PF`; the nine signal probes were clean.
The kernel killed the faulting busybox and returned to the
fallback `musl_sh` prompt; the system survived.

Not reproduced: 2, 10, 100, 500, and 2000 iterations in a fresh
boot all completed cleanly; a probe-then-5000 run on a later
boot reached 5000.  So the loop is not the cause and 5000 is not
a threshold; the fault is intermittent.  Do not chase it as a
signal bug (the probes are not implicated) and do not call it
closed on one clean run.  It is the same family as the
`0x42F1A7` `#GP` (7b) and the ash stale-`jmp_buf` (7e), and the
next session that touches the VFS, fork, or exec paths should
read this first.

**Disassembly of the current build (2026-10-18).**  `third_party/busybox/busybox` is non-PIE (`Type: EXEC`, load base `0x400000`); `0x42F1A7` is inside the executable PT_LOAD but is **not an instruction boundary** -- it falls inside `mov 0xa0(%r12),%rdi` at `0x42F1A3`.  Starting a disassembly at `0x42F1A7` yields a spurious `movabs`, which is the tell.  The nearest real instructions that can raise a user `#GP(0)` are `0x42F1A3` (`mov 0xa0(%r12),%rdi`, a non-canonical `%r12`) and `0x42F1AB` (`cmpb $0x0,0x1(%rbp)`, a non-canonical `%rbp` set by `xchg %rax,%rbp` at `0x42F197` from a returned pointer).  No SSE/XMM instruction appears in the window, so 7b is **not** a face of item 10.  The recorded address is therefore either a fault/operand address, a transcription slip, or from an older build.  Suspected (not proven) to be a downstream symptom of a pre-v0.6.13 corruption race (heap cycle / pipe wake / `C`); unobserved since those closed.  The standing test is a 100 Hz soak (PIPE7E-ALL-PASS, 250000 iterations @ 100 Hz over >3.5 hours, 2026-10-18); if it recurs, inspect `%r12`/`%rbp` at `0x42F1A3`/`0x42F1AB` and trace the corruption upstream of `find`.

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

**`ls` long options are unavailable in the pinned BusyBox 1.36.1.**  `CONFIG_LONG_OPTS=y` is set, but `ls --color=auto`, `ls --help`, `ls --full-time`, and `ls --group-directories-first` all fail with `ls: unrecognized option: -`.  The `ls` applet registers no long-option table in this source snapshot: the applet's long-option registration is gated behind `FEATURE_LS_COLOR` rather than `LONG_OPTS` (the bug upstream fixed in 2017 by changing `IF_FEATURE_LS_COLOR(applet_long_options = ls_longopts;)` to `IF_LONG_OPTS(applet_long_options = ls_longopts;)`).  `third_party/` is read-only, so this cannot be fixed in-tree.  Workaround: use short options (`-l`, `-F`, `-R`, `-h`), which work and are verified by `test.sh` and the batch-1 smoke run.  If long `ls` options are ever wanted, the pinned source needs the 2017 fix.  Not a kernel bug and not a batch-1 regression.

**A nonexistent pid stats as a directory.**  `stat("/proc/999999/")`
reports `S_IFDIR` rather than `ENOENT`, because the check is on
the path shape, not on `process_find_by_pid`.  `ps` only stats
pids `readdir` gave it, so it does not affect the consumer.
Worth one line when `/proc` is next touched.

**`gettimeofday` (96) and `time` (201) are unimplemented.**
Neither is reached by anything on the image today.  `ps -l`
reaches **99**, which is `sysinfo`, not `gettimeofday` -- that
number was recorded wrong in earlier notes.  `sys_sysinfo` now
fills `struct sysinfo` from `g_ticks` and the PMM (uptime,
procs, totalram, freeram; the rest zero).  Implement 96 and 201
when a caller appears -- e.g. `date` (needs 201) or a
`CONFIG_FEATURE_PS_TIME` path that actually uses `time()`.

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
- **`sigsegv_probe`** installs a `SIGSEGV` handler, faults, and
  edits `ucontext.gregs[16]` so `rt_sigreturn` resumes past the
  fault.  It is the acceptance check for slice 4.
- **`sigterm_ignore`** installs `SIG_IGN` for SIGTERM, raises
  SIGTERM, and prints whether it survived.  It is the acceptance
  check for the `SIG_IGN` disposition (`20261023-sigign`).
- **`at_step1` sections**, as of session 41: sections 1–7
  `resolve_at`; 8–9 `faccessat`; 11 `utimensat`.  (There is no
  section 10.)
- **The canary is now `canary`, a program.**  `canary` (read-only)
  and `canary --full` (also the mutating rows).  15/15 and 28/28.
