# Open issues

**Last updated: 2026-11-04 (third session, after `153dda9`)** (HEAD on
`dev` at `153dda9`, two `sys_execve` fixes landed this session).
**Read the section "The `time true` fault, after the third-session
fixes" below before doing anything else with `time`/`time true`.
It names what is left and what the next probe is.**
This file lists **only open issues**.  Closed items
are removed, not struck through; their history is in `git log` and
`docs/session-log.md`.  An item appears here only while it is a
defect, a missing feature, or an unverified claim about the tree.

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

**Status note (2026-11-02).**  Nothing currently fails because
this is not implemented, and there is no user-facing feature it
unlocks: every binary on the image is built here, there is no
network service, no multi-user login, and no applet in `test.sh`
behaves differently as non-root.  The cost of implementing it is
real -- every filesystem syscall grows a permission check, the
five identity syscalls flip from 0 to 1000 together (changing
`tar -x`, `ps`, and `whoami` output), `chmod`/`chown` become
real (which needs a FAT mode store or a side-channel), and the
setuid marker is security-relevant in a way item 8's symlink
marker is not.  **Before starting item 9, ask what it unlocks
that will be used soon.**  If the answer is "nothing in the near
term," item 10 or a visible-payoff item from the un-numbered
list (real FatFs timestamps, `gettimeofday`/`time`) is a better
use of the session.

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

**The fix has two separable halves and they should be two
commits.**  (a) Kernel save/restore: `fxsave`/`fxrstor` (or
`xsave`/`xrstor`) in `context_switch.asm`'s save and resume
sides, and in the interrupt path (`interrupts.c`'s timer and ISR
entry/exit).  The save region is 512 bytes (or 576 with AVX);
it must be 16-byte aligned, so it is a `kmalloc`'d buffer per
PCB or an `__attribute__((aligned(16)))` field in `pcb_t`.
`CR0.MP` and `CR0.NE` should be set; `CR4.OSXSAVE` may need
setting if `xsave` is chosen.  (b) Userland flag removal: drop
`-mno-sse -mno-sse2 -mno-avx -mno-mmx` from
`userland/musl/Makefile`'s `CFLAGS`.  **Do not drop the
kernel's `-mno-sse*` flags in the same commit** -- kernel SSE
and userland SSE are two separate decisions, and the kernel is
written expecting no XMM state to survive across calls.

The test: a program that writes `%xmm0`, forks a child that does
not touch `%xmm0`, and has the child verify its value survived a
tick (and the reverse: parent clobbers `%xmm0`, child still sees
its own value).  One new applet, or an extension of an existing
one.

The 2026-11-02/03 hot paths (`78216bc` fork rep movsb, `905bc70` ATA
rep-pio, `d69c2ab` `f_write`) are more vectorizable than what came
before, so the latent surface is larger than when this item was filed.
That raises priority; it does not change the fix.

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
v0.6.16 note named a real symptom but the wrong cause.  busybox's
self-archive check compares the archive's `(st_dev, st_ino)` against
each member's, and every FAT file reported `Inode: 0`, so every
member matched and was skipped; `76d66db` gives FAT a real `st_ino`
from `(sclust, objsize)`.  That was **necessary but not sufficient**:
the archive was still lost on create and short on list/extract
because of two vendored-FatFs bugs of the same class -- **state
staged in `fs->win` is not flushed before something else moves the
window.**  `a0664a8` flushes the staged directory entry after
`dir_register` in `f_open`; `d69c2ab` flushes the FAT link before a
direct data write in `f_write`.  With both, `tar -cvf`/`-tvf`/`-xvf`
round-trip silently with `rc=0`, including directory members
(`src/` lists as `drwxr-xr-x`), and `tar -tvf` records a symlink as
`lrwxrwxrwx ... -> target`.  One limit
remains: two empty files with the same name in different
directories collide in `st_ino`, because `FILINFO` carries no
parent -- see `fat_ino`.

**`umask` (95) is implemented.**  `sys_umask` stores the low nine
mode bits in a `pcb_t.umask` field (appended after
`clear_child_tid`, no asserted offset moves) and returns the
previous mask, defaulting to `022` on first use because
`process_initialize_pcb` zeroes the field.  FatFs has no mode
store, so the mask is not persisted; `sys_mkdir` applies it to the
mode it hands the driver, which is the honest extent.  Verified
on the 2026-11-04 second session: `umask` prints `0022`,
`umask 077` then `umask` prints `0077`, `umask 022` restores
`0022`.  **This is a syscall-completion fix, not a mode store** --
`chmod`/`chown` and item 9 remain the places a real mode store
would land.

**`sys_mkdir` returns `-EEXIST` for an existing directory, and
`mkdir -p` now works.**  `fat_mkdir` translates `FR_EXIST` to
`-(long)17` before falling through to `fatfs_errno`, which still
maps `FR_EXIST` to `-EPERM_` for every other FatFs caller; the
translation is scoped to `mkdir` because `f_mkdir` is the only
call in that function.  Separately, `sys_mkdir` returns
`-EEXIST` for a resolved path of `"/"`: **the check must run
before `strip_dot_prefix`**, which turns `"/"` into `"."` --
`f_mkdir` rejects `"."` as `FR_INVALID_NAME`, and `fatfs_errno`
maps that to `-ENOENT`.  A first attempt placed the check after
the strip and was dead code; a one-run `serial_print` named it
(`path='/' rr=0 resolved='/' post-strip "."`).  With both,
`mkdir -p /b/c/d` succeeds silently, `ls /b/c/d` is empty,
`mkdir /b` prints `File exists`, and `mkdir ""` still returns
`-ENOENT`.  Correct POSIX for the file case (`-ENOTDIR` when the
target exists as a file) is **not** implemented and is not
covered by the `FR_EXIST` translation -- `f_mkdir` does not
distinguish "exists as file" from "exists as directory" in the
error it returns.  That refinement is open.

**`set_tid_address` (218) `clear_child_tid` is implemented.**
`sys_set_tid_address` keeps its return value (the current pid,
which musl reads in `__libc_start_main`) and stores the user VA
in `pcb_t.clear_child_tid`; `process_exit` (scheduler.c) writes
`0` to `*clear_child_tid` after `close_all_files` and before the
state change, while the exiting process's own CR3 is still
loaded.  The store rejects `0` and non-canonical-kernel VAs but
does **not** walk the page table, so a canonical-but-unmapped VA
would fault there.  No current caller does that; making it a
safe `copy_to_user`-style store is a separate hardening.  This
is a real correctness fix and it is **not** the `time true`
fault (see the next heading).

**`tkill` (200) is implemented as `kill(tid, sig)`.**  Not an
open issue; noted so it is not re-added.  busybox's `kill`
applet, signalling itself, issues raw syscall 200; without a
handler it printed "Unknown syscall: 200" and SIGINT's default
action could not be exercised.  `tkill(tid, sig)` is
`kill(tid, sig)` in a single-threaded process, implemented as a
thin wrapper over `sys_kill` so the raise path stays
single-sourced (`b976b40`).

**The `g_syscall_stack_top - 120` frame base is derived and
correct.**  `user_syscall_entry.asm` pushes, in order, six
callee-saved GPRs (48 bytes), user RIP and RFLAGS (16), user RSP
(8), r8 and r9 (16), and rdi/rsi/rdx/r10 (32) — 120 bytes total,
from `g_syscall_stack_top` down.  The frame is:

    g_syscall_stack_top -  8   rbx
                         - 16   rbp
                         - 24   r12
                         - 32   r13
                         - 40   r14
                         - 48   r15
                         - 56   user RIP
                         - 64   user RFLAGS
                         - 72   user RSP
                         - 80   r8
                         - 88   r9
                         - 96   rdi
                         -104   rsi
                         -112   rdx
                         -120   r10    <- frame base

`signal_deliver_frame` and `sys_rt_sigreturn` both compute
`g_syscall_stack_top - 120` as `kf`, and `kf[0]` is the r10 slot
at -120, `kf[4]` is r9 at -88, `kf[6]` is user RSP at -72,
`kf[7]` is user RFLAGS at -64, `kf[8]` is user RIP at -56, and
`kf[14]` is rbx at -8.  Every offset lines up with the push
order.

The number that confused an earlier note — 128 — is the value of
`rsp` during the `call syscall_dispatch`, because the call's
return address adds 8 bytes below the frame (and `add rsp, 8`
after the call discards the stacked arg5).  It is not the frame
base.  The frame base is 120, and both functions are right to
use it.

Fragility worth naming: the constant is coupled to the stub's
push sequence.  If a push is added, removed, or reordered,
`kf[]` must be re-derived and the two C functions updated in the
same commit.  A change to one side without the other is a bug.

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

**The 2026-11-04 second session observed the equality live.**
The `RACE PROBE DUMP` on the runs that faulted `time true`
reported `P12 calls: 714 refcount_gt1: 492 pipe_case: 222
wake_fired: 86 wake_no_waiter: 111 writers_nonzero: 111`, with
`total records` around 16.4M and 18.7M on the two boots.
**`wake_no_waiter == writers_nonzero` at 111 == 111.**  It was
seen during the same boot as the `time true` fault but that does
not prove the two are related; it is recorded as a live
observation of the standing question, not as a mechanism and not
as a signal bug.  Do not chase it on this evidence alone.

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

> **2026-11-02 evening cross-reference.**  The canary `.text` `#PF`
> that was tracked in `handoff.md` (not in this file) faulted at
> `RIP=CR2=0x413966` before 2026-11-02, and at `0x450630` on an
> unlucky post-`7cb26fb` run -- `error 0x14` (present, user, fetch),
> `pte=0`, canary's own cr3.  The `0x413962` `#PF` described in the
> preceding paragraph is on the *same page* as the `0x413966`
> canary fault (offset 4 bytes).  Different `error` code (`0x5`
> read vs `0x14` fetch) and different process (`echo hi | wc` vs
> `canary --full`), so they are not proven to be the same fault --
> but they are close enough to flag.  The canary `.text` `#PF` did
> not reproduce after the 2026-11-02 IF fix (`162a1d3`).  The
> VA-gated probe that was to close it was in the discarded stash
> and was dropped on 2026-11-02; it is not in the tree.  If this
> `0x413962` `#PF` recurs, read it on its own.  **Do not merge
> them without evidence.**

Status (2026-11-03): not observed since the 2026-11-02 disk-path
fixes (`905bc70`, `a0664a8`, `d69c2ab`).  Not closed -- the
mechanism was never named; recurrence should be read on its own.

Not reproduced: 2, 10, 100, 500, and 2000 iterations in a fresh
boot all completed cleanly; a probe-then-5000 run on a later
boot reached 5000.  So the loop is not the cause and 5000 is not
a threshold; the fault is intermittent.  Do not chase it as a
signal bug (the probes are not implicated) and do not call it
closed on one clean run.  It is the same family as the
`0x42F1A7` `#GP` (7b) and the ash stale-`jmp_buf` (7e), and the
next session that touches the VFS, fork, or exec paths should
read this first.

**Disassembly note.**  `0x42F1A7` is inside an instruction, not at
one; the nearest real instructions that can raise a user `#GP(0)` are
`0x42F1A3` (non-canonical `%r12`) and `0x42F1AB` (non-canonical
`%rbp`).  Not SSE (no XMM in the window).  Suspected (not proven)
downstream of a pre-`v0.6.13` corruption race; unobserved since those
closed.  Standing test: the 100 Hz soak.  If it recurs, inspect
`%r12`/`%rbp` at those two addresses.

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

## Also open -- small defects and missing features

- `kill(0, sig)` ("my group") still returns `-EINVAL`.
  `kill(-pgid, sig)` works (`9b1a4ed`).
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
- A nonexistent pid stats as a directory: `stat("/proc/999999/")`
  reports `S_IFDIR` rather than `ENOENT`.  `ps` only stats pids
  `readdir` gave it, so no consumer is affected.  Worth one line
  when `/proc` is next touched.
- `ls` long options are unavailable in the pinned BusyBox 1.36.1
  (`--color`, `--help`, `--full-time`): the applet registers no
  long-option table in this snapshot.  `third_party/` is read-only;
  use short options (`-l`, `-F`, `-R`, `-h`).  Not a kernel bug, a
  source-version limit.
- `exec_free_and_unmap_user_pages` walks a fixed 1 MB brk window
  (`user_syscall.c:4358` lists `[0x8000200000, 0x8000300000)`), but
  `sys_brk` has no upper bound and `sys_fork`'s copy uses the real
  `parent->brk_virt`.  A parent whose `brk_virt` grew past the
  window forks a child with pages the exec cleanup will neither
  unmap nor free.  Leak, not corruption; extend the cleanup row to
  the live bound, or unmap by VA from `elf_page_list`.
- `sys_mkdir` does not distinguish "exists as file" from "exists
  as directory": both come back from `f_mkdir` as `FR_EXIST`,
  which `fat_mkdir` translates to `-EEXIST`.  POSIX wants
  `-ENOTDIR` for the file case.  Cosmetic today; no caller in
  the tree depends on the distinction.

## Also open -- verify

- `setpgid`/`setsid`-as-pgrp: can a process join or create a
  process group beyond `TIOCSPGRP`'s foreground assignment?
  State unverified.
- The optional shebang interpreter argument reaching the
  interpreter as a real argv is unproven.  A test with an
  interpreter that can observe argv is the proof.

## Also open -- hazards (read before touching)

- `put_file_slot`'s pipe wake is coupled to `sys_close`'s wake
  (read the `put_file_slot` comment before touching either).
- `process_dump_all`'s `DETACHED` state: a process READY with
  `prev == NULL && next == NULL` is labeled DETACHED, which may
  mask a stranded-process bug.
- The `g_syscall_stack_top - 120` frame base is coupled to the
  stub's push sequence in `user_syscall_entry.asm`; a push added,
  removed, or reordered means `kf[]` must be re-derived and
  `signal_deliver_frame` + `sys_rt_sigreturn` updated in the same
  commit.
- `sys_mkdir`'s `"/"` check must stay **before** `strip_dot_prefix`.
  `strip_dot_prefix` turns `"/"` into `"."`, `f_mkdir(".")` returns
  `FR_INVALID_NAME`, and `fatfs_errno` maps that to `-ENOENT`; a
  check placed after the strip never fires.  Moving the check, or
  changing `strip_dot_prefix`'s handling of `"/"`, needs a
  behavioral re-run of `mkdir -p`.
- `fat_mkdir`'s `FR_EXIST -> -(long)17` translation is local to
  `mkdir`.  `fatfs_errno`'s `FR_EXIST -> -EPERM_` is shared by every
  other FatFs caller (`open(O_CREAT|O_EXCL)`, `rename`, `link`).
  Do not move the translation into `fatfs_errno` without checking
  every caller; that is a wider behavior change than `mkdir`.

## vfork child faults in musl startup (open, not fixed)

**Status: partially fixed. `time true` is not reliable; do not claim vfork
works.** Work done in the 2026-11-04 session, on top of `c652c8f`.

**Fixed (keep):**
- `c652c8f` -- `vmm_create_address_space` clone-then-unmap.  This fixed
  the **triple fault** (the machine no longer resets on `time true`).
- vfork block/wake: `sys_vfork` blocks the parent on
  `BLOCK_KIND_VFORK` (excluded from the tick wake in
  `process_wake_all_blocked`), and the parent was observed blocked
  (kind=6) and woken.

**Measured correct (nine checks, each against actual bytes):**
cr3 clone; exec frame slots (`ktop[-7]`/`[-9]` sane); sysret target
(valid busybox RIPs, sane `rbx`); exec argv/stack layout (correct SysV);
`.data`/relocation copy (`[0x469000]=0x5`, `[0x469018]=0x44D4E6`);
`elf_load_into_process` entry (`0x413882` = ELF `e_entry`); kernel-stack
slot allocator; brk unmap (disabling it changed nothing);
`vmm_map_page_in_cr3` / `vmm_unmap_page_in_cr3` bodies (both correct).

**Open bug:** `time true` intermittently faults in the vfork child during
musl/busybox startup.  Sometimes it succeeds (`real 0m 0.18s`); often it
faults at a **real busybox RIP** with a **register holding 0/small where
a pointer belongs**:
- `RIP 0x4512E8`, `mov %r13,0x8(%r12)`, `CR2 0x8` -> `%r12 = 0`
  (a list insert through a null head).
- `RIP 0x422300`, `testb $0x1,0x21(%rbx)`, `CR2 0xEF` -> `%rbx = 0xCE`.
- Earlier: `RIP 0`/`CR2 0`, same class.

**Leading hypothesis (not proven):** a callee-saved register (`%r12`,
`%rbx`) is intermittently wrong on the syscall-return path for the vfork
child -- same family as the C halt / pipe7e intermittents.  The next
measurement would be a check in `user_syscall_entry` just before
`o64 sysret`: if `%r12` (or `%rbx`) is 0 or `< 0x1000`, print it and the
syscall number.  Not yet run.

**FALSE POSITIVE -- do not re-chase.**  The `PTAUDIT` "22 anomalies at
`0x455000..0x45C000`" were **not** corruption.  `phys = 0x8000_0000_07D6_xxxx`
in a donix PTE is the **normal encoding** (the HHDM address is stored in
the PTE phys field; `pte_phys()` / `phys_to_virt()` account for it).  The
`MAPAUDIT` probe never fired because **no caller passed a bad phys** --
together those meant "ordinary PTEs", which was misread as corruption
for several turns.  The pages are busybox's own image.

**Stashed, not committed:** the exec-side vfork work (`process.c` wake arm,
`user_syscall.c` cr3 swap + wake) is in `stash@{0}`, message
"vfork 3b ... not a complete fix".


## busybox applet dispatch faults after exec (open, next session's first work)

**Filed 2026-11-04, at `325e85b`.** This is the `time true`
fault, narrowed through eight commits and one revert.

**What `time true` does now.** The shell's `time` vforks a
child; the child execs busybox `time`; `time_main` vforks again
to run `true`; that second child execs `true`. The **first**
child is fine: it runs `_start` -> `__init_libc` ->
`__init_tls` -> `main` -> `time_main`, and `time_main` prints a
wall time when the second child dies. **The second child is the
one that faults**: it runs `_start` -> `__init_libc` ->
`__init_tls` -> `set_tid_address` -> `__init_libc`'s
not-setuid return path -> `libc_start_main_stage2` -> `call
*%r12` with `%r12 = 0x413DA6` (main, non-zero) -> `main` ->
`lbb_prepare` (`0x4139F6`) -> `bb_basename(argv[0])` ->
`run_applet_and_exit` (`0x413A93`), and **the `call *%rax`
with `rax = 0` is in one of those last three.** The fault is
`RIP 0`, `CR2 0`, `error 0x15` (present + user + fetch), and the
PTE walked is **VA 0's identity-map page** (`pte = 0x3`,
`phys = 0`, supervisor-only) -- the child transferred control to
VA 0.

**What is REFUTED by this session's disassembly -- do not
re-walk.**

- **The V3/V3b register zeroing was the fault.** With
  `c1bd883`/`f57299b` the fault was `0x453DC5`; with them
  reverted (`325e85b`) it is back at `RIP 0` after
  `__init_tls`. The zeroing was **masking** `__init_tls`, not
  fixing anything. The revert is the Linux vfork contract
  (child gets the parent's registers) and is kept.
- **The missing auxv is the fault.** `a10e585` writes the auxv;
  the stack dump shows `AT_PHDR = 0x400040`, `AT_PHENT = 0x38`,
  `AT_ENTRY = 0x413882`, `AT_RANDOM`, `AT_NULL` in place.
  `__init_libc` reads it and proceeds. The fault is unchanged.
- **`sys_set_tid_address` is the fault.** `__init_tls`
  (`0x40FA40`, **not** `static_init_tls`) calls
  `set_tid_address(&0x46AA20)` at `0x40FBB4` and returns at
  `0x40FBC3`; `__init_libc` (`0x40B060`) takes the "not setuid"
  path (`AT_UID == AT_EUID`, `AT_GID == AT_EGID`,
  `AT_SECURE == 0`) and returns at `0x40B215`;
  `libc_start_main_stage2` (`0x44860B`) calls `main` through
  `%r12 = 0x413DA6`. **None of that is a call through 0.**
  `sys_set_tid_address`'s `tidptr` gap was real and has since
  been implemented (`4312aab`, see the `set_tid_address` entry
  above); it is still **not** this fault.
- **`static_init_tls` at `0x40FBC0` is a `call *%rax`.** The
  symbol at `0x40FA40` is `__init_tls`; `0x40FBC0` is the
  instruction after `__init_tls`'s `set_tid_address` `syscall`
  -- its `ret`.
- **`kmain.c`'s self-tests map over the ELF text page.**
  `kmain.c:550` maps `0x40000000` and unmaps it; `kmain.c:707`
  maps `0xFFFFFFFF82000000`. Neither touches `0x40FBC0`.

**Next step: the `0x40FBC0` probe, with the corrected
`exception_frame_t`, scoped to the right frame.** The probe is
still the way to name the zeroed-return writer, but the
2026-11-04 second session's dumps showed **two distinct `time
true` signatures, both live in one boot**, and the handoff's
original description matches only the second:

- **the busybox-side signature** (`pcb pid/name: N busybox`):
  `RSP = 0x80000FBFA0 = g_user_rsp_save + 0x180`, and the
  `__init_tls` call frame is **intact** at `rsp-0x18 = 0x40FBA0`,
  `rsp-0x20 = 0x4659FE`, `rsp-0x28 = 0x80000FBFE8`,
  `rsp-0x30 = 0x80000FBFF8`.  The `ret` that popped 0 is **above**
  `__init_tls`'s return, in `__init_libc`'s call chain.  A
  breakpoint at `0x40FBC0` fires for this process, shows the slot
  correct, and the fault happens later -- one frame too low.
- **the true-side signature** (`pcb pid/name: N true`):
  `RSP = 0x80000FBE30 = g_user_rsp_save + 0x10`, the
  `__init_tls` call frame intact below the fault, matching the
  original "the `__init_tls` `ret` at `0x40FBF0` pops 0 from
  `0x80000FBE28`".  The `0x40FBC0` breakpoint is the right probe
  for **this** one.

Both signatures show `last syscall num 218` and `last sysret rcx
0x40FBC0`; they differ in fault RSP and in which `ret` popped 0.
A probe at `0x40FBC0` must print a per-process marker (`$fs_base`
or pid) so the two are not confused, and the **last** `PROBE`
line before the fault is the one to read.  A watchpoint on the
slot that was popped then names the writer, with `$rip` and
`rdi/rsi/rdx` at the hit.

**A silent breakpoint is itself a result.** If `0x40FBC0` does
not fire before a `true`-side fault, the writer is before the
sysret, in `__init_libc`/`_start`/`rsp_init`.

**Not vfork.** The vfork work (V2, V1, exec cr3 swap, wake arm,
auxv) is complete and correct; the parent-reap path works end to
end. See the vfork heading above for the closed record.

## The `time true` fault, after the third-session fixes (open)

**Status (2026-11-04, third session).**  Two `sys_execve` fixes landed
on `dev`:

- **`939e3ef`** -- `sys_execve`: set `pcb->user_rsp = rsp_init`.
  `sys_execve` rewrote the syscall-entry frame's user-RSP slot
  (`ktop[-9] = rsp_init`) but never updated `pcb->user_rsp`.  The
  exec'ing process returns via `sysret` and reads `ktop[-9]` (correct);
  the vfork child resumes through `context_switch_resume`, which loads
  the user-RSP slot from `pcb->user_rsp` (`pcb` offset `0x610`,
  asserted in `process.c`).  For the child, `pcb->user_rsp` was the
  stale pre-exec value, `0x180` above the new stack's `rsp_init`.  The
  child started inside the strings region of the new stack; that was
  the busybox-side signature (`RSP = g_user_rsp_save + 0x180`,
  `rcx 0x40FBC0`).  **After this fix the busybox-side `0x40FBC0` fault
  no longer occurs.**
- **`153dda9`** -- `sys_execve`: 16-byte-align `rsp_init` per the SysV
  AMD64 ABI.  `sys_execve` placed `rsp_init` at
  `argv_region_bottom - 8`, where `argv_region_bottom = user_stack_top
  - 16 KB`.  `user_stack_top` is 16-aligned (`process.c:408-409`), so
  `rsp_init` was 8 mod 16.  musl's `_start` does `pop %rsi` then
  `and -16, %rsp`; with `rsp_init` misaligned by 8 that `and` is a
  no-op and `__libc_start_main`'s frame is 8 bytes higher than on
  Linux, so every subsequent `call`'s return address lands 8 bytes off
  from where musl's layout expects it, and a later `ret` pops a slot
  that was never pushed (zeroed fresh stack).  **After this fix the
  argc/argv/envp/auxv layout IS at `rsp_init`** (visible in the fault
  dump at `rsp + 0x30 = rsp_init`: `argc`, `argv[0]`, `NULL`,
  `envp[0]`, `NULL`), and **the fault has moved forward** -- the child
  now gets past `__libc_start_main` into the busybox applet-dispatch
  region.

**What still faults, in two signatures, both `RIP 0`, `CR2 0`,
`error 0x15`, `CS 0x33`:**

- **`num 13` (`rt_sigaction`), `last sysret rcx 0x44CC0D`,**
  `RSP = g_user_rsp_save + 0x390` -- observed on `pid 264` (`time
  true`) and `pid 268` (`time ls /`) after `153dda9`.
- **`num 218` (`set_tid_address`), `last sysret rcx 0x44AC73`,**
  `RSP = g_user_rsp_save + 0x180` -- observed on `pid 266` (`time
  true`) after `153dda9`.

Both are `RIP 0` from a `ret` popping a slot that is `0` in the fault
dump; in both dumps, **the entire stack region between `fault_rsp`
and `rsp_init` is zero** -- the popped slot was never written after
startup.  That is the remaining bug.  It is **not** the
`pcb->user_rsp` slot, and it is **not** the 8-mod-16 entry alignment
(both are now fixed).  It is a `ret` in busybox's startup popping a
slot that is zero on a fresh stack.

**The next probe (one boot, one `time ls /`):**

1. `./run debug`, attach gdb.
2. `hbreak *0x44cc0d` and `hbreak *0x44ac73`, each with `commands`
   printing **`rsp=%lx fs=%lx`** (the discriminator is `$rsp`; the
   `0x44CC0D` fault has `rsp - rsp_init = 0x390`, the `0x44AC73`
   fault has `0x180`; `$fs_base` is the same VA across busybox
   processes and is **not** a per-process marker -- see the
   `$fs_base` correction below).
3. **And** one `serial_print` in `sys_execve` at the
   `self->user_rsp = rsp_init` line, printing `rsp_init`,
   `self->user_rsp`, and `self->kernel_stack_top[-9]`.
4. Run `time ls /` until it faults.
5. Read the `$rsp` at the last breakpoint hit, compare to `rsp_init`.

**What the result means, and what the fix is:**

- **If `$rsp == rsp_init` at the breakpoint**, the delivered `_start`
  `rsp` is exactly right and the remaining fault is a busybox/musl
  startup issue in `third_party/` (not editable in-tree).  The fix is
  a **workaround**: e.g. force-initialize the slot the `ret` pops, or
  shrink `__init_libc`'s frame, or carry a one-byte patch to
  `third_party/musl/` and document it as a local patch.
- **If `$rsp != rsp_init`** (it will be `rsp_init + 0x390` or `+0x180`
  at the fault), then either `process_fork`'s `frame_rsp = is_user ?
  pcb->user_stack_top : pcb->kernel_stack_top;` is the wrong source
  (it should be the **parent's** user `RSP` for fork/vfork, and
  `sys_execve`'s `pcb->user_rsp = rsp_init` is what a vfork-then-exec
  should see), or the resume path (`context_switch_resume` vs the
  syscall-return `sysret`) reads a slot that `sys_execve` does not
  write.  **The fix is in the delivery path, not in the ABI.**

**`$fs_base` is not a per-process marker.**  It is the same VA
(`0x46a138`) in every busybox process and in most musl binaries;
`%rsp` is the same VA across processes.  **Only `pid` or `CR3`
distinguishes processes.**  The 2026-11-04 third-session WATCH hits
confirmed this: a `WATCH` line with `fs=0x46a138` and `rip=0x40b061`
(a `/usr/bin/*` binary's `__init_libc`) was indistinguishable, by
`fs` alone, from a busybox process.  The handoff's working-style rule
must be read as: **print `pid` or `CR3`; `$fs_base` and `%rsp` are
not sufficient on their own.**

**`time` is not usable today; `true` alone works.**  In the
third-session boot:

    ~ # true              (silent, returns to prompt)   -- works
    ~ # time              (prints usage)                 -- works
    ~ # time true         (faults at RIP 0)              -- faults
    ~ # time ls /         (faults at RIP 0)              -- faults

`time`'s own startup faults; `true` alone starts fine; `time` with no
args prints usage without a fault.  So `time true` and `time ls /` are
**not** a test of the `true`/`ls` applets -- they are a test of
**`time`'s own `__init_libc` / `__libc_start_main` startup** on a
forked child of the shell.  **`/usr/bin/TIME` does not exist** (only
`/bin/time`, a busybox applet).  The shell's PATH lookup tries
`/usr/bin/TIME`, gets `FR_NO_FILE`, falls back to `/bin/time`; that is
expected, not a bug.  `/bin/true` does not exist either; `time
/bin/true` prints `can't execute '/bin/true': No such file or
directory` and exits 127 -- that line is correct behavior, not a
control.

**The `time` applet's accounting is broken and separate.**  `real`
prints `0m 0.34s` on some runs and `1191h 48m 18s` on others; `user`
and `sys` print about `152710235h` in every run.  This is
`getrusage`/`times` reading uninitialized data (or a wrong unit), not
the `time true` fault and not the alignment fix.  **Do not fix it in
the same session as the `time true` fault.**

**A third `time true` fault signature existed before `153dda9`:**
`num 61` (`wait4`), `rcx 0x44CC0D`, `RSP = g_user_rsp_save + 0xB0`.
It was observed on `pid 8` and `pid 10` before the alignment fix and
did not recur in the post-fix boot.  **Recorded, not chased.**

**A boot-time fault outside `time true` was observed:**
`pid 3` (`busybox`), `num 7` (`poll`), `rcx 0x44D6A3`, `RSP =
g_user_rsp_save + 0x10`, before any `time true` in the boot.  Its
frame dump contains kernel addresses in GPR slots (`0xFFFFFFFF802A2E23`,
`0xFFFF800007E52B8B`) but the user-stack window below is normal.
Not `time true`; recorded, not chased.  A boot-time `#PF`/`#GP` that
does not reproduce is not closed.

**`process_fork`'s `frame_rsp = is_user ? pcb->user_stack_top :
pcb->kernel_stack_top;`** (`process.c:453`) is a candidate, not yet
proven.  For a plain `fork` or `vfork`, the child should resume at the
**parent's user `RSP`** (from the syscall-entry frame), not at
`user_stack_top`.  For a `vfork`-then-`exec`, `sys_execve` sets
`pcb->user_rsp = rsp_init`, and the child should resume at
`rsp_init`; whether the resume path uses `pcb->user_rsp` or the frame
`process_fork` built is what the next probe's `$rsp` comparison
answers.  **Do not change `frame_rsp` without that answer.**

**Status of the old "busybox applet dispatch faults after exec"
heading above.**  Its `0x40FBC0` probe is **closed by measurement**:
the slot was `0x40B163` at `0x40FBC0` (correct at sysret) in every
run of the third session, so the writer of the busybox-side slot was
not between `0x40FBC0` and `0x40FBF0`; the writer of the
`0x40FBF0`-pop-0 was between those two addresses (true-side) or in
`__init_libc`'s chain (busybox-side).  The `user_rsp` fix and the
align fix moved both.  Read this section first; the heading above is
history.


## Three history corrections from the 2026-11-04 evening session

**`time /bin/true` never worked, and "no fault" was not an
improvement.** `/bin` contains only `busybox` and `sh`; there is no
`/bin/true` file. `sys_execve` reported `f_open(...) -> 4`
(`FR_NO_FILE`) and the shell printed `can't execute '/bin/true': No
such file or directory` and exited 127 -- **before any child
existed**. It is not a control, not a fix, and not evidence about the
`RIP 0` fault. The valid real-file controls are `/usr/bin/HELLO`,
`/usr/bin/ECHO`, `/usr/bin/CAT`; all three fault, at `0x400221`,
`0x4002EC`, `0x40039D` respectively, with the corrected
`exception_frame_t`.

**The one gdb run where `time true` succeeded was never reproduced.**
It happened in a single run with an `hbreak` at `0x40FBC0` armed;
later runs with the same breakpoint armed faulted. The cause is
unknown. It is **not** an explanation of the fault, **not** a fix,
and **must not** be cited as evidence that the fault is a race or
that gdb masks it.

**The `exception_frame_t` layout was the reason every earlier "fault
site" was wrong.** The old struct began at `error_code`; the stubs
push 15 GPRs first. `frame->rip` read `r14`, `frame->cs` read `r13`,
`frame->rsp` read `r11`. Fixed by `fb188f0`; every dump before it
must be read with that in mind.

**`fs_base` was a separate, real bug, fixed by `1bf2ca2`.** The
`CR2=0x34` / `RIP=0x448C6A` (`__errno_location`) fault was the
per-process TLS base not being restored on context switch. It is
gone. It was **not** the `time true` fault, which survived it.


## Two measurement corrections from the 2026-11-04 second session

**The handoff's `argv_scratch` item is mis-stated and is closed.**
The handoff's NEXT item 3 said
"`sys_execve`'s `argv_scratch[64][256]` = 16 KB on a 16 KB
`PROC_STACK_SIZE` stack."  Measured: `objdump -d kernel.elf`
shows `sys_execve`'s frame is `sub $0x1a68,%rsp` = **6760 bytes**
of `PROC_STACK_SIZE` = 16384; `EXEC_MAX_ARGC` is 16 and
`EXEC_MAX_ARG_LEN` is 256, so `argv_scratch` is **16 * 256 = 4096
bytes**, not 16 KB and not `[64][256]`; and the 16 KB array
(`EXEC_MAX_ENVC * EXEC_MAX_ARG_LEN`) is `envp_scratch`, which is
**already `kmalloc`'d** (`user_syscall.c:5845-5849`).  There is
no 16 KB stack overflow in `sys_execve` to fix.  Moving
`argv_scratch` to the heap would save 4 KB of a 9624-byte margin
and is defensible for symmetry with `envp_scratch`, but it is
**not** the fix the item named; if it is ever done, its commit
message must say so.

**The `mkdir("/")` check must run before `strip_dot_prefix`.**
The first attempt at the `mkdir -p` unblock checked
`local_path[0] == '\0'` **after** `strip_dot_prefix`, on the
theory that `strip_dot_prefix("/")` leaves `""`.  A one-run
`serial_print` showed it does not: `path='/' rr=0(+) resolved='/'
user_first=47` and then `post-strip local_path='.' user_first=47`.
`strip_dot_prefix` turns `"/"` into `"."`, `f_mkdir(".")` returns
`FR_INVALID_NAME`, `fatfs_errno` maps that to `-ENOENT`, and
busybox `mkdir -p` treats `-ENOENT` as "parent missing" and stops
with `can't create directory '/': No such file or directory`.
The fix is the check before the strip, on `'/'` followed by
`'\0'` -- see the `sys_mkdir` entry above.  **The general lesson,
which belongs next to the working-style rules, is that a
diagnostic on the failure path named the failing call in one run,
after three guesses had failed.**  `set -e` in the runner also
closed the terminal on the abort; the abort itself was the useful
signal and must be visible.
