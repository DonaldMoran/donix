# Open issues

**Last updated: 2026-11-04 (fifth session, corrected).**
**The fourth-session claim that the `time true` fault is
`static_init_tls` computing `base = 0` is refuted.**  The build of
record has no `PT_TLS`, so `base` is never read; and the live fault
is `CR2 0x34` / `RIP 0x448C6A` (write), not a TLS-image copy.  Read
"The live `time true` fault" before doing anything with `time`.
This file lists **only open issues**.  Closed items are removed, not
struck through; their history is in `git log` and
`docs/session-log.md`.  An item appears here only while it is a
defect, a missing feature, or an unverified claim about the tree.

Read top to bottom.  The numbered items are the long-standing list;
the un-numbered entries below them are smaller and were filed later.
Numbers are stable â€” a closed number is retired, not reused.

---

## Open items

**9.  No privilege model: uid/gid are 0 for every process.**
No per-process uid/euid split, no setuid bit (FAT has no mode bits to
hold one), no `chown`, no way to become root.  In that world 0 is the
honest answer: everything runs as root.

    getuid   (102) -> 0
    getgid   (104) -> 0
    geteuid  (107) -> 0
    getegid  (108) -> 0
    getgroups(115) -> 0 groups

`/etc/passwd` and `/etc/group` each carry one `root` entry so the
applets that resolve a number to a name (`id -un`, `whoami`, `ps`'s
USER column, `groups`) print `root` instead of a bare number or an
error.

This is temporary and it is the value that changes when a privilege
model lands.  That session adds `setuid`/`seteuid` (105/117), a
setuid-root marker on a FAT-hostile filesystem (the same
magic-marker shape as item 8), a `sudoers` equivalent, and then
changes these five to return the unprivileged default (1000) with
root as an escalation.

**Status note (2026-11-02).**  Nothing currently fails because this
is not implemented, and there is no user-facing feature it unlocks:
every binary on the image is built here, there is no network service,
no multi-user login, and no applet in `test.sh` behaves differently
as non-root.  The cost of implementing it is real -- every filesystem
syscall grows a permission check, the five identity syscalls flip
from 0 to 1000 together (changing `tar -x`, `ps`, and `whoami`
output), `chmod`/`chown` become real (which needs a FAT mode store or
a side-channel), and the setuid marker is security-relevant in a way
item 8's symlink marker is not.  **Before starting item 9, ask what
it unlocks that will be used soon.**  If the answer is "nothing in
the near term," item 10 or a visible-payoff item from the un-numbered
list is a better use of the session.

**10.  `CR4.OSFXSR` is set, but the kernel saves no SSE state.**
Userland compiled by `toolchain/musl-gcc.sh` can emit SSE; the kernel
is built with `-mno-sse -mno-sse2 -mno-avx -mno-mmx` and contains no
`fxsave`/`fxrstor`/`xsave` anywhere; `context_switch.asm` saves no
XMM registers.  But `kmain.c` sets CR4.OSFXSR (bit 16), which tells
the CPU that the kernel *does* save SSE state.  So a user process
that uses an XMM register can have it clobbered across a context
switch or an interrupt.

The session-52 stopgap added `-mno-sse -mno-sse2 -mno-avx -mno-mmx`
to the userland CFLAGS: the userland ABI now matches what the kernel
preserves, so no userland binary emits SSE and the corruption cannot
happen.  **It is not a fix.**  The real fix is for the kernel to save
and restore XMM state on context switch and on the interrupt path,
then to remove the `-mno-sse*` flags from the userland build.

No test currently fails because of this; the corruption is latent,
and the surface is *any* 64-bit computation that gcc chooses to
vectorize.

**The fix has two separable halves and they should be two
commits.**  (a) Kernel save/restore: `fxsave`/`fxrstor` (or
`xsave`/`xrstor`) in `context_switch.asm`'s save and resume sides,
and in the interrupt path (`interrupts.c`'s timer and ISR
entry/exit).  The save region is 512 bytes (or 576 with AVX); it must
be 16-byte aligned, so it is a `kmalloc`'d buffer per PCB or an
`__attribute__((aligned(16)))` field in `pcb_t`.  `CR0.MP` and
`CR0.NE` should be set; `CR4.OSXSAVE` may need setting if `xsave` is
chosen.  (b) Userland flag removal: drop `-mno-sse -mno-sse2
-mno-avx -mno-mmx` from `userland/musl/Makefile`'s `CFLAGS`.  **Do
not drop the kernel's `-mno-sse*` flags in the same commit** --
kernel SSE and userland SSE are two separate decisions, and the
kernel is written expecting no XMM state to survive across calls.

The test: a program that writes `%xmm0`, forks a child that does not
touch `%xmm0`, and has the child verify its value survived a tick
(and the reverse: parent clobbers `%xmm0`, child still sees its own
value).  One new applet, or an extension of an existing one.

The 2026-11-02/03 hot paths (`78216bc` fork rep movsb, `905bc70` ATA
rep-pio, `d69c2ab` `f_write`) are more vectorizable than what came
before, so the latent surface is larger than when this item was
filed.  That raises priority; it does not change the fix.

## Un-numbered open items

**`chmod`/`chown` accept and drop, and their applets are not
compiled in.**  Syscalls 90-93 (path and fd forms) return 0 and store
nothing: FAT has no mode bits and no ownership, everything is uid 0,
and `stat` reports the synthesized 0644/0755.  This is so `tar -x`,
`cp -a`, and `install` complete instead of printing "Unknown syscall"
for every file.  It is not a privilege model -- items 9 and 10 own
the real answer.

`CONFIG_CHMOD` and `CONFIG_CHOWN` are **unset** in
`configs/busybox.config`, so no command on the image calls 90-93 and
`chmod`/`chown` say "not found".  Enabling them would make the
commands run and report success while changing nothing (FAT has no
mode store).  Honoring FAT's read-only attribute via FatFs's
`f_chmod` would be a real but two-state mode change, and a separate,
larger decision.

**`tar` is enabled, and link restoration is verified.**  The v0.6.16
note named a real symptom but the wrong cause.  busybox's
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
round-trip silently with `rc=0`, including directory members (`src/`
lists as `drwxr-xr-x`), and `tar -tvf` records a symlink as
`lrwxrwxrwx ... -> target`.  One limit remains: two empty files with
the same name in different directories collide in `st_ino`, because
`FILINFO` carries no parent -- see `fat_ino`.

**`umask` (95) is implemented.**  `sys_umask` stores the low nine
mode bits in a `pcb_t.umask` field (appended after
`clear_child_tid`, no asserted offset moves) and returns the previous
mask, defaulting to `022` on first use because
`process_initialize_pcb` zeroes the field.  FatFs has no mode store,
so the mask is not persisted; `sys_mkdir` applies it to the mode it
hands the driver, which is the honest extent.  Verified on the
2026-11-04 second session: `umask` prints `0022`, `umask 077` then
`umask` prints `0077`, `umask 022` restores `0022`.  **This is a
syscall-completion fix, not a mode store** -- `chmod`/`chown` and
item 9 remain the places a real mode store would land.

**`sys_mkdir` returns `-EEXIST` for an existing directory, and
`mkdir -p` now works.**  `fat_mkdir` translates `FR_EXIST` to
`-(long)17` before falling through to `fatfs_errno`, which still maps
`FR_EXIST` to `-EPERM_` for every other FatFs caller; the
translation is scoped to `mkdir` because `f_mkdir` is the only call
in that function.  Separately, `sys_mkdir` returns `-EEXIST` for a
resolved path of `"/"`: **the check must run before
`strip_dot_prefix`**, which turns `"/"` into `"."` -- `f_mkdir`
rejects `"."` as `FR_INVALID_NAME`, and `fatfs_errno` maps that to
`-ENOENT`.  A first attempt placed the check after the strip and was
dead code; a one-run `serial_print` named it (`path='/' rr=0
resolved='/' post-strip "."`).  With both, `mkdir -p /b/c/d`
succeeds silently, `ls /b/c/d` is empty, `mkdir /b` prints `File
exists`, and `mkdir ""` still returns `-ENOENT`.  Correct POSIX for
the file case (`-ENOTDIR` when the target exists as a file) is **not**
implemented and is not covered by the `FR_EXIST` translation --
`f_mkdir` does not distinguish "exists as file" from "exists as
directory" in the error it returns.  That refinement is open.

**`set_tid_address` (218) `clear_child_tid` is implemented.**
`sys_set_tid_address` keeps its return value (the current pid, which
musl reads in `__libc_start_main`) and stores the user VA in
`pcb_t.clear_child_tid`; `process_exit` (scheduler.c) writes `0` to
`*clear_child_tid` after `close_all_files` and before the state
change, while the exiting process's own CR3 is still loaded.  The
store rejects `0` and non-canonical-kernel VAs but does **not** walk
the page table, so a canonical-but-unmapped VA would fault there.
No current caller does that; making it a safe `copy_to_user`-style
store is a separate hardening.  This is a real correctness fix and it
is **not** the `time true` fault (see that heading).

**`tkill` (200) is implemented as `kill(tid, sig)`.**  Not an open
issue; noted so it is not re-added.  busybox's `kill` applet,
signalling itself, issues raw syscall 200; without a handler it
printed "Unknown syscall: 200" and SIGINT's default action could not
be exercised.  `tkill(tid, sig)` is `kill(tid, sig)` in a
single-threaded process, implemented as a thin wrapper over
`sys_kill` so the raise path stays single-sourced (`b976b40`).

**The `g_syscall_stack_top - 120` frame base is derived and
correct.**  `user_syscall_entry.asm` pushes, in order, six
callee-saved GPRs (48 bytes), user RIP and RFLAGS (16), user RSP (8),
r8 and r9 (16), and rdi/rsi/rdx/r10 (32) â€” 120 bytes total, from
`g_syscall_stack_top` down.  The frame is:

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
`g_syscall_stack_top - 120` as `kf`, and `kf[0]` is the r10 slot at
-120, `kf[4]` is r9 at -88, `kf[6]` is user RSP at -72, `kf[7]` is
user RFLAGS at -64, `kf[8]` is user RIP at -56, and `kf[14]` is rbx
at -8.  Every offset lines up with the push order.

The number that confused an earlier note â€” 128 â€” is the value of
`rsp` during the `call syscall_dispatch`, because the call's return
address adds 8 bytes below the frame (and `add rsp, 8` after the call
discards the stacked arg5).  It is not the frame base.  The frame
base is 120, and both functions are right to use it.

Fragility worth naming: the constant is coupled to the stub's push
sequence.  If a push is added, removed, or reordered, `kf[]` must be
re-derived and the two C functions updated in the same commit.  A
change to one side without the other is a bug.

**The pipe lost-wakeup equality is a standing question, not a live
bug.**  `wake_no_waiter == writers_nonzero` was observed on two
2026-10-23 captures (`P12`: 8 and 8, and 2 and 2) and not on the
`20261023-sigign` boot (`P12`: 0 and 0).  The reader's self-clear of
`pipe->reader_waiting` was removed at `7aaaafd`, which closed the
lost-wakeup mechanism; the equality itself was never explained.  Do
not chase it as a signal bug and do not call it closed on one clean
run: it is a standing question about the pipe wake counts, separate
from signals.  If it reproduces as a hang, the ring's `P13` events
are the place to read.

**The 2026-11-04 second session observed the equality live.**  The
`RACE PROBE DUMP` on the runs that faulted `time true` reported `P12
calls: 714 refcount_gt1: 492 pipe_case: 222 wake_fired: 86
wake_no_waiter: 111 writers_nonzero: 111`, with `total records`
around 16.4M and 18.7M on the two boots.  **`wake_no_waiter ==
writers_nonzero` at 111 == 111.**  It was seen during the same boot
as the `time true` fault but that does not prove the two are related;
it is recorded as a live observation of the standing question, not as
a mechanism and not as a signal bug.  Do not chase it on this
evidence alone.  (The 2026-11-04 fifth-session boot that produced the
live `CR2 0x34` fault had `P12 calls: 0` -- no pipe activity on that
path.)

**Item 7a â€” the boot-time `#PF` at `0x400000`.**  Unobserved, not
fixed.  An intermittent fault that stops reproducing is not closed.
The `20261003-vmm-map-diag` instrumentation (the three silent `return
-1;` sites in `vmm_map_page_in_cr3` each print `VMM: map failed
site=<NAME> virt=... cr3=... free=NNN`) is in the tree and did not
fire on the boots that followed.  When it returns, the `free` field
separates the two candidates: a healthy count with `site=PDPT` or
`site=PD` is a missing clone table; a near-zero count is PMM
exhaustion in a shape the session-48 wrap does not cover.

**Item 7b â€” a user-mode `#GP` at `0x42F1A7` in busybox during
`find`.**  Unobserved, not fixed.  Different vector, different
location, different phase from 7a.  A user `#GP` with error `0` is
raised for a privileged instruction, a non-canonical address in a
base register, or a segment violation.  It needs its own instrument â€”
a first-party reproduction of the `find` sequence, or a kernel-side
`#GP` handler trace â€” before it is guessed at.  It is not the same
fault as 7a and must not be folded into 7a.

**Observed again, 2026-10-23, and not reproduced.**  A user-mode
`#PF` fired during 5000 iterations of `echo hi | wc` in a boot that
had first run `sigchld_probe`, `kill_self`, `sigsegv_probe`, and
`sigterm_ignore`.  CR2 `0x876CA6`, RIP `0x413962`, error `0x5`
(present + user + read), the CR2 inside the 2 MB page at phys base
`0x800000` â€” a user-mode read of a kernel physical address, which is
the 7b/7e signature.  The boot's capture contained exactly one `#PF`;
the nine signal probes were clean.  The kernel killed the faulting
busybox and returned to the fallback `musl_sh` prompt; the system
survived.

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

Not reproduced: 2, 10, 100, 500, and 2000 iterations in a fresh boot
all completed cleanly; a probe-then-5000 run on a later boot reached
5000.  So the loop is not the cause and 5000 is not a threshold; the
fault is intermittent.  Do not chase it as a signal bug (the probes
are not implicated) and do not call it closed on one clean run.  It
is the same family as the `0x42F1A7` `#GP` (7b) and the ash
stale-`jmp_buf` (7e), and the next session that touches the VFS,
fork, or exec paths should read this first.

**Disassembly note.**  `0x42F1A7` is inside an instruction, not at
one; the nearest real instructions that can raise a user `#GP(0)` are
`0x42F1A3` (non-canonical `%r12`) and `0x42F1AB` (non-canonical
`%rbp`).  Not SSE (no XMM in the window).  Suspected (not proven)
downstream of a pre-`v0.6.13` corruption race; unobserved since those
closed.  Standing test: the 100 Hz soak.  If it recurs, inspect
`%r12`/`%rbp` at those two addresses.

**Item 7e â€” ash's stale `jmp_buf`.**  The kernel-side faces of 7e are
closed (the pipe-wake face by `c3e5c97`; the heap-corruption face by
`2fc7fde`; the pipe-reader lost-wakeup face by `7aaaafd`; the
`test.sh` row-30 `C` by `d3c7f20`; the four dispatch guards by
`8dc47cf`, `85da375`, `e7e3e6b`, `d7d8c40`; the `C` itself by
`ef85962`).  What remains is `third_party`: `evaltree` stores a
pointer to its own stack frame's `jmp_buf` into `globals+0x38` and
restores it only on the `longjmp`-return path, not on the normal
return.  `raise_exception` then `_longjmp`s to that global with no
liveness check.  First-party reproduction: `pipe7e_sjlj_stale`
(`/usr/bin/PIPE7E_SJLJ_STALE`).  **The mechanism is `third_party/`'s
and is not editable.**

**`ls` long options are unavailable in the pinned BusyBox 1.36.1.**
`CONFIG_LONG_OPTS=y` is set, but `ls --color=auto`, `ls --help`,
`ls --full-time`, and `ls --group-directories-first` all fail with
`ls: unrecognized option: -`.  The `ls` applet registers no
long-option table in this source snapshot: the applet's long-option
registration is gated behind `FEATURE_LS_COLOR` rather than
`LONG_OPTS` (the bug upstream fixed in 2017 by changing
`IF_FEATURE_LS_COLOR(applet_long_options = ls_longopts;)` to
`IF_LONG_OPTS(applet_long_options = ls_longopts;)`).  `third_party/`
is read-only, so this cannot be fixed in-tree.  Workaround: use short
options (`-l`, `-F`, `-R`, `-h`), which work and are verified by
`test.sh` and the batch-1 smoke run.  If long `ls` options are ever
wanted, the pinned source needs the 2017 fix.  Not a kernel bug and
not a batch-1 regression.

**A nonexistent pid stats as a directory.**  `stat("/proc/999999/")`
reports `S_IFDIR` rather than `ENOENT`, because the check is on the
path shape, not on `process_find_by_pid`.  `ps` only stats pids
`readdir` gave it, so it does not affect the consumer.  Worth one
line when `/proc` is next touched.

**`gettimeofday` (96) and `time` (201) are unimplemented.**
Neither is reached by anything on the image today.  `ps -l` reaches
**99**, which is `sysinfo`, not `gettimeofday` -- that number was
recorded wrong in earlier notes.  `sys_sysinfo` now fills `struct
sysinfo` from `g_ticks` and the PMM (uptime, procs, totalram,
freeram; the rest zero).  Implement 96 and 201 when a caller appears
-- e.g. `date` (needs 201) or a `CONFIG_FEATURE_PS_TIME` path that
actually uses `time()`.

**A live pipe slot's `end` field is 0; no writer is in the source.**
Latent, not active.  A `file_slot_t` of kind `FILE_KIND_PIPE` was
captured with `refcount = 2` and `slot->end = 0`, at a fixed
kernel-heap address.  `PIPE_END_READ` is 1 and `PIPE_END_WRITE` is 2;
`0` is neither.  The three writes to a pipe slot's `end` in
`user_syscall.c` all write 1 or 2, never 0.  The writer is not in the
source.

The observable consequence is closed: `put_file_slot` refuses to free
a pipe when a slot's `end` is neither READ nor WRITE â€” it records the
event, frees the slot, and leaves the pipe alive.  A leak is
survivable; a lost wakeup is not.  No reproduction since the
2026-10-14 work.  If it recurs, the next step is a guard-band: write
a magic value to `slot->end` at slot creation and check it at every
`put_file_slot`; the window between the last good read and the first
bad one names where the write landed.

**`exec_free_and_unmap_user_pages` walks a fixed 1 MB brk window.**
`user_syscall.c:4358` lists the brk region as
`[0x8000200000, 0x8000300000)`.  But `sys_brk` has no upper bound,
and `sys_fork`'s copy walks `[0x8000200000, parent->brk_virt)`, the
real bound.  A parent whose `brk_virt` has grown past `0x8000300000`
forks a child that has pages the exec cleanup will neither unmap nor
free.  Leak, not corruption.  Extend the cleanup row to the live
bound, or walk `elf_page_list` and unmap by VA from the list itself.

---

## Also open -- small defects and missing features

- `kill(0, sig)` ("my group") still returns `-EINVAL`.
  `kill(-pgid, sig)` works (`9b1a4ed`).
- `sys_brk`'s fixed `heap_base` and the 4 MB mmap window are latent
  collisions.
- Real FatFs timestamp storage: the three timestamp syscalls return 0
  without storing.
- `prctl` is minimal (`PR_SET_NAME` accepted and dropped).
- Syscall-table audit script.
- `musl_wait`'s WNOHANG loop spins.
- `sys_mmap` rejects all non-anonymous mappings (a file-backed `mmap`
  caller gets `-ENOMEM` and must fall back to `read`; a Wayland
  prerequisite).
- Pipes support one concurrent reader and one concurrent writer (see
  `pipe_t`'s comment in `user_syscall.c` â€” a second blocked reader on
  the same pipe end has nowhere to record itself and will only wake
  on a keyboard IRQ).
- `sys_mkdir` does not distinguish "exists as file" from "exists as
  directory": both come back from `f_mkdir` as `FR_EXIST`, which
  `fat_mkdir` translates to `-EEXIST`.  POSIX wants `-ENOTDIR` for
  the file case.  Cosmetic today; no caller in the tree depends on
  the distinction.

## Also open -- verify

- `setpgid`/`setsid`-as-pgrp: can a process join or create a process
  group beyond `TIOCSPGRP`'s foreground assignment?  State
  unverified.
- The optional shebang interpreter argument reaching the interpreter
  as a real argv is unproven.  A test with an interpreter that can
  observe argv is the proof.

## Also open -- hazards (read before touching)

- `put_file_slot`'s pipe wake is coupled to `sys_close`'s wake (read
  the `put_file_slot` comment before touching either).
- `process_dump_all`'s `DETACHED` state: a process READY with `prev
  == NULL && next == NULL` is labeled DETACHED, which may mask a
  stranded-process bug.
- The `g_syscall_stack_top - 120` frame base is coupled to the stub's
  push sequence in `user_syscall_entry.asm`; a push added, removed,
  or reordered means `kf[]` must be re-derived and
  `signal_deliver_frame` + `sys_rt_sigreturn` updated in the same
  commit.
- `sys_mkdir`'s `"/"` check must stay **before** `strip_dot_prefix`.
  `strip_dot_prefix` turns `"/"` into `"."`, `f_mkdir(".")` returns
  `FR_INVALID_NAME`, and `fatfs_errno` maps that to `-ENOENT`; a
  check placed after the strip never fires.  Moving the check, or
  changing `strip_dot_prefix`'s handling of `"/"`, needs a behavioral
  re-run of `mkdir -p`.
- `fat_mkdir`'s `FR_EXIST -> -(long)17` translation is local to
  `mkdir`.  `fatfs_errno`'s `FR_EXIST -> -EPERM_` is shared by every
  other FatFs caller (`open(O_CREAT|O_EXCL)`, `rename`, `link`).  Do
  not move the translation into `fatfs_errno` without checking every
  caller; that is a wider behavior change than `mkdir`.

---

## The live `time true` fault (2026-11-04, fifth session, 5-boot count)

**The fourth-session "static_init_tls root cause" is refuted, twice.**
This section replaces the fourth-session "Why `time true` faults"
section.  The fault is real and reproducible; the mechanism recorded
there is not the mechanism.

### Refutation 1 -- three fixes were measured and failed

`third_party/busybox/scripts/trylink`'s final link was given
`-Wl,--dynamic-linker=/lib/ld-musl-x86_64.so.1`, and busybox was
rebuilt with `toolchain/musl-gcc.sh`.  **The flag had no effect.**
The result is byte-identical in shape to the unmodified binary:
516232 bytes, entry `0x413882`, `__init_tls`/`static_init_tls` at
`0x40fa40`, and **no `PHDR`, no `INTERP`, no `DYNAMIC`**.  bfd drops
the flag on a fully static link.

`userland/musl/Makefile`'s busybox rule was changed to pass
`CC="$(abspath $(MUSL_GCC)) -fuse-ld=lld"`.  LLD **did** emit
`PHDR`, but the binary was wrong in four ways: the load base moved
from `0x400000` to `0x200000` (entry `0x214140`); `PT_INTERP` was
emitted requesting `/lib/ld-musl-x86_64.so.1`; the
`date`/`time`/`uptime` applets were lost; and the boot faulted at
`CR2 0x400040` (auxv `AT_PHDR`).  **`PT_PHDR` presence is necessary
but not sufficient.**

`trylink` has a first-class hook: a file named
`third_party/busybox/busybox_ldscript` causes
`-Wl,-T,busybox_ldscript`.  A from-scratch script with `PHDRS` and a
hand-built `SECTIONS` block was written.  `trylink` **did** use it,
and `PHDR` appeared with **no `INTERP`** -- the right shape.  But the
hand-written `SECTIONS` discarded the real section contents: `LOAD 0`
was `0x2ca` bytes (the real `.text` is `0x541e5`), `LOAD 1` was
`0x38` (the real `.rodata` is `0x12bd3`), entry was `0x400210`
(should be `0x413882`), and `--list` had no applets -- a
714-byte-code skeleton.  **The mechanism is proven; the content was
wrong.**  A correct script must be the **default `ld` script
byte-for-byte plus a `PHDRS` block and `:seg` suffixes**; the default
script is 276 lines (`ld --verbose`, sha256
`3e46a9c1bd09be84e237e57779efd9bc8ea9c3e7807b0c4a7ce45c9da87d60d3`).
Gate before any boot: `readelf -SW` must show `.text` ~`0x541e5`.

### Refutation 2 -- the build of record has no `PT_TLS`

The shipped busybox is `userland/musl/build/busybox.elf`, which is
byte-identical to `third_party/busybox/busybox` (sha
`2d62a9914362a85c574bc2a9fef836dbb91ec1e567d69ce2980fbb10b3717300`,
428032 bytes).  Its program headers are:

    LOAD x3 (0x400000 / 0x455000 / 0x468fc8), NOTE, GNU_PROPERTY,
    GNU_STACK, GNU_RELRO -- and NO PT_TLS, NO PT_PHDR,
    NO PT_DYNAMIC, NO PT_INTERP.

In `static_init_tls`, `tls_phdr` stays `0`, the `if (tls_phdr)` block
is skipped, `base` is never read, and there is no TLS image to copy.
**Patching `static_init_tls`'s `base` computation is dead code for
this binary.**

**`third_party/busybox/busybox_unstripped` is stale and is a
different link** (516232 bytes, sha
`6bb18f9279c11d6dd71d945d948b2e9e507ac55482fdd05c98a8e0038f7272b4`).
Every `readelf`/`nm`/`objdump` diagnostic must use `busybox.elf`,
not `busybox_unstripped`.  Several earlier `readelf` conclusions
were drawn against the stale file.

### The live fault (5-boot count)

A 5-boot count with kernel probes (pid, fs_base, MSR) on
`sys_execve`, `sys_arch_prctl`, and `context_switch_resume` measured
the live fault.  The `fs_base` hypothesis is dead: the base is
correct at every point.

**The symptom is identical on every boot.  The last syscall is not.**

    boot 1: CR2=0 RIP=0 err=0x15  num=13  rcx=0x44CC0D  fs=0x46A138
    boot 2: CR2=0 RIP=0 err=0x15  num=218 rcx=0x40FBC0  fs=0x46A138
    boot 3: CR2=0 RIP=0 err=0x15  num=218 rcx=0x40FBC0  fs=0x46A138
    boot 4: CR2=0 RIP=0 err=0x15  num=218 rcx=0x40FBC0  fs=0x46A138
    boot 5: CR2=0 RIP=0 err=0x15  num=218 rcx=0x40FBC0  fs=0x46A138

**The fault always ends in an instruction fetch from address 0**:
`CR2 = 0`, `RIP = 0`, error `0x15` (present + user + fetch), and
`fs_base` is the correct `0x46A138`.  This is a `ret` (or a jump
through a register) to a zeroed target.  The corruption is a control
target going to 0, not a data address.

**The landing point moves through startup.**  In 4 of 5 boots the
last syscall was `218` (`set_tid_address`, return `0x40FBC0`, early
`__init_tls`).  In 1 of 5 it was `13` (`rt_sigaction`, return
`0x44CC0D`, later in `__libc_start_main`).  The corruption window is
timing-dependent and can land on either return path; a single
breakpoint at `0x40FBC0` would miss boot 1.

**`CR2 = 0x34` / `RIP = 0x448C6A` did not appear in any of the 5
boots.**  That trace is not the current fault and is retired.

**The `num 13` / `0x44CC0D` trace is live, not stale.**  It was
previously listed as a stale trace from the reverted
`153dda9`/`939e3ef` state; boot 1 of this count produced it on an
unmodified tree.  Do not dismiss it.

**`context_switch.asm` is not the cause.**  The `wrmsr` installs the
correct `fs_base`.  The saved return addresses (`0x40FBC0`,
`0x44CC0D`) are correct.  The corruption is in the user stack or a
user register, in the window around whichever syscall the child is in
when the race lands.

### The next step is an instrument, not a breakpoint

A kernel-side check on the user `RIP` about to be returned to, on
both return paths, that prints `pid`, the target `RIP`, and the user
stack top when the target is zero.  That catches the corruption
before the fetch faults, on whichever syscall it follows.  A
breakpoint at a fixed landing point (`0x40FBC0`) would miss boot 1.

### The vfork stash is superseded, not pending

`stash@{0}` ("vfork 3b (wake arm + sys_vfork) + diagnostics; time
true intermittent, not a complete fix") is **already in the tree**.
Its two halves were committed in `001a756` ("vfork: exec gives the
child its own cr3; wake the parent on exec"):

- `process.c` `process_wake_parent_if_waiting` has the `_is_vfork`
  arm (`block_kind == BLOCK_KIND_VFORK && wait_pid == child->pid`).
- `sys_execve` has the `process_wake_parent_if_waiting(self)` call
  after `self->entry_point = entry;`.

**The fix landed and `time true` still faults** (the 5-boot data
above is from a tree that contains it).  So the stash is a
superseded diagnostic record, not a pending change.  Do not
re-inspect or re-apply it; the remaining fault is downstream of the
exec-side wake and the fresh-address-space block, and it is not
fixed by either.

The stash's own "KNOWN" comment -- `vmm_clone_page_table`
deep-copies the low half, so the clone keeps the parent's user
mappings outside the new image -- remains the named open issue, but
it is a diagnosis, not the fix, and the tree already unmaps the old
pages after the fresh-address-space block.

### The options, and their reproducibility

The constraint that matters for a shippable fix: **a fix must be
tracked and built by the clone script, or a cloner does not get
it.**

**1. Own `time` as a tracked userland binary.**
`userland/musl/apps/time.c` plus an image-Makefile staging line.
Tracked, built by the clone script, runs like every other musl ELF
on the image.  **Gives a working `time`; does not fix the busybox
`time true` fault** (busybox's own startup still faults).  Inherits
the `getrusage` accounting bug.

**2. Re-open the `fs_base` / context-switch restore path for
re-exec.**  `1bf2ca2` fixed the general case; the re-exec path still
faults.  Narrowest fix is likely a `%fs`-base reload-on-exec
correction.  Blast radius: every process, so the six-behavior gate
applies.

**3. Dynamic linking.**  Fixes a startup fault by construction
(`PT_PHDR` appears for a dynamic link), but **not a small option**:
the kernel has no `PT_INTERP` handling (`elf.c:91` skips non-`PT_LOAD`;
`elf.c:232` returns the binary's `e_entry`), no `/lib` staging in the
image, `userland/musl/Makefile` builds `-static -no-pie` only, and
`third_party/musl-install/lib/` has no `libc.so`.  A multi-session
kernel + build-system project.

**4. Linker script.**  Proven mechanism, but `busybox_ldscript` is
gitignored and the result depends on the host binutils version
(`-z separate-code` is a binutils default that has changed across
versions).  **Not reproducible for a cloner** without a tracked
build-system change, and then binutils-version-dependent.  Recorded
as a local experiment, not a shippable fix.

### Busybox build path and the stamp

`./run` and the top-level `Makefile` do not build busybox.  The musl
build runs through `userland/musl/Makefile`'s `musl-userland` target
(reached by `hdd-single.img`), which invokes
`make -C third_party/busybox CC=$(abspath toolchain/musl-gcc.sh)`.
A bare `make -C third_party/busybox` falls through to host gcc and
produces a **glibc** binary.  The rule's stamp,
`userland/musl/build/.busybox.stamp`, depends only on
`third_party/busybox/.config` -- **not** on the `Makefile` or on the
binary -- so any change to the busybox build flags or binary
requires `rm -f userland/musl/build/.busybox.stamp
userland/musl/build/busybox.elf` before the next build, or the old
binary is silently copied.

**`toolchain/install_musl.sh` reuses `third_party/musl-src` in
place** if it is already at `v1.2.5`; it does not re-materialize it.
A patch applied to `musl-src` therefore persists across runs, and
reverting the tracked script does **not** undo it -- the revert is
`git -C third_party/musl-src checkout -- <file>` followed by a
rebuild.

---

## Measurement corrections and history notes

**`time /bin/true` never worked, and "no fault" was not an
improvement.**  `/bin/true` does not exist.  `sys_execve` reported
`f_open(...) -> 4` (`FR_NO_FILE`) and the shell printed
`can't execute '/bin/true': No such file or directory` and exited 127 --
**before any child existed**.  Valid real-file controls are
`/usr/bin/HELLO`, `/usr/bin/ECHO`, `/usr/bin/CAT`; all three fault,
at `0x400221`, `0x4002EC`, `0x40039D` respectively, with the
corrected `exception_frame_t`.

**The one gdb run where `time true` succeeded was never reproduced.**
It happened in a single run with an `hbreak` at `0x40FBC0` armed;
later runs with the same breakpoint armed faulted.  The cause is
unknown.  It is **not** an explanation of the fault, **not** a fix,
and **must not** be cited as evidence that the fault is a race or
that gdb masks it.

**The `exception_frame_t` layout was the reason every earlier "fault
site" was wrong.**  The old struct began at `error_code`; the stubs
push 15 GPRs first.  `frame->rip` read `r14`, `frame->cs` read
`r13`, `frame->rsp` read `r11`.  Fixed by `fb188f0`; every dump
before it must be read with that in mind.

**`fs_base` was a separate, real bug, fixed by `1bf2ca2` -- but not
for the `time true` re-exec path.**  The `CR2=0x34` / `RIP=0x448C6A`
(`__errno_location`) fault was the per-process TLS base not being
restored on context switch.  `1bf2ca2` fixed the general case; the
fifth-session boot shows it is **back for `time true`**.  The
fourth-session note "It was **not** the `time true` fault, which
survived it" is **corrected**: it is the live `time true` fault now.

**`$fs_base` is not a per-process marker.**  It is the same VA
(`0x46a138`) in every busybox process; `%rsp` is the same VA across
processes.  **Only `pid` or `CR3` distinguishes processes.**  Every
gdb printf on a `time true` fault must print `pid` or `CR3`, or the
lines are cross-process and the inference is invalid.

**The `argv_scratch` item is mis-stated and is closed.**
`objdump -d kernel.elf` shows `sys_execve`'s frame is `sub
$0x1a68,%rsp` = **6760 bytes** of `PROC_STACK_SIZE` = 16384;
`EXEC_MAX_ARGC` is 16 and `EXEC_MAX_ARG_LEN` is 256, so
`argv_scratch` is **16 * 256 = 4096 bytes**, not 16 KB and not
`[64][256]`; and the 16 KB array (`EXEC_MAX_ENVC *
EXEC_MAX_ARG_LEN`) is `envp_scratch`, which is **already
`kmalloc`'d** (`user_syscall.c:5845-5849`).  There is no 16 KB stack
overflow in `sys_execve` to fix.

**The `mkdir("/")` check must run before `strip_dot_prefix`.**
`strip_dot_prefix("/")` leaves `"."`, `f_mkdir(".")` returns
`FR_INVALID_NAME`, `fatfs_errno` maps that to `-ENOENT`, and busybox
`mkdir -p` treats `-ENOENT` as "parent missing" and stops.  The fix
is the check before the strip, on `'/'` followed by `'\0'`.  **The
general lesson: a diagnostic on the failure path named the failing
call in one run, after three guesses had failed.**  `set -e` in the
runner also closed the terminal on the abort; the abort itself was
the useful signal and must be visible.

---

## vfork child faults in musl startup (partially fixed)

**Status: partially fixed.  `time true` is not reliable; do not claim
vfork works.**  Work done in the 2026-11-04 session, on top of
`c652c8f`.

**Fixed (keep):**
- `c652c8f` -- `vmm_create_address_space` clone-then-unmap.  This
  fixed the **triple fault** (the machine no longer resets on `time
  true`).
- vfork block/wake: `sys_vfork` blocks the parent on
  `BLOCK_KIND_VFORK` (excluded from the tick wake in
  `process_wake_all_blocked`), and the parent was observed blocked
  (kind=6) and woken.

**Measured correct (nine checks, each against actual bytes):**
cr3 clone; exec frame slots (`ktop[-7]`/`[-9]` sane); sysret target
(valid busybox RIPs, sane `rbx`); exec argv/stack layout (correct
SysV); `.data`/relocation copy (`[0x469000]=0x5`, `[0x469018]=0x44D4E6`);
`elf_load_into_process` entry (`0x413882` = ELF `e_entry`);
kernel-stack slot allocator; brk unmap (disabling it changed
nothing); `vmm_map_page_in_cr3` / `vmm_unmap_page_in_cr3` bodies
(both correct).

**The busybox applet-dispatch faults.**  This is the `time true`
fault, narrowed through eight commits and one revert.  The shell's
`time` vforks a child; the child execs busybox `time`; `time_main`
vforks again to run `true`; that second child execs `true`.  The
**first** child is fine: it runs `_start` -> `__init_libc` ->
`__init_tls` -> `main` -> `time_main`, and `time_main` prints a wall
time when the second child dies.  **The second child is the one that
faults**, at the live `CR2 0x34` / `0x448C6A` signature.

**REFUTED by that session's disassembly -- do not re-walk:**

- **The V3/V3b register zeroing was the fault.**  With
  `c1bd883`/`f57299b` the fault was `0x453DC5`; with them reverted
  (`325e85b`) it is back at the live signature.  The zeroing was
  **masking** the fault, not fixing anything.  The revert is the
  Linux vfork contract and is kept.
- **The missing auxv is the fault.**  `a10e585` writes the auxv; the
  stack dump shows `AT_PHDR = 0x400040`, `AT_PHENT = 0x38`,
  `AT_ENTRY = 0x413882`, `AT_RANDOM`, `AT_NULL` in place.
- **`sys_set_tid_address` is the fault.**  `__init_tls` (`0x40FA40`,
  **not** `static_init_tls`) calls `set_tid_address(&0x46AA20)` at
  `0x40FBB4` and returns at `0x40FBC3`; `__init_libc` (`0x40B060`)
  takes the "not setuid" path and returns at `0x40B215`;
  `libc_start_main_stage2` (`0x44860B`) calls `main` through
  `%r12 = 0x413DA6`.  None of that is a call through 0.  The
  `tidptr` gap was real and is implemented (`4312aab`).
- **`static_init_tls` at `0x40FBC0` is a `call *%rax`.**  The symbol
  at `0x40FA40` is `__init_tls`; `0x40FBC0` is the instruction after
  `__init_tls`'s `set_tid_address` `syscall` -- its `ret`.
- **`kmain.c`'s self-tests map over the ELF text page.**
  `kmain.c:550` maps `0x40000000` and unmaps it; `kmain.c:707` maps
  `0xFFFFFFFF82000000`.  Neither touches `0x40FBC0`.

**FALSE POSITIVE -- do not re-chase.**  The `PTAUDIT` "22 anomalies
at `0x455000..0x45C000`" were **not** corruption.  `phys =
0x8000_0000_07D6_xxxx` in a donix PTE is the **normal encoding**
(the HHDM address is stored in the PTE phys field; `pte_phys()` /
`phys_to_virt()` account for it).  The `MAPAUDIT` probe never fired
because **no caller passed a bad phys** -- together those meant
"ordinary PTEs", which was misread as corruption for several turns.
The pages are busybox's own image.

**Stashed, not committed:** the exec-side vfork work (`process.c`
wake arm, `user_syscall.c` cr3 swap + wake) is in `stash@{0}`,
message "vfork 3b ... not a complete fix".
