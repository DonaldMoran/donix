# Open issues

**Last updated: 2026-11-06 (ninth session), at `1a737e2`.**
This file lists **only open issues**. Closed items are removed, not
struck through; their history is in `git log`. An item appears here
only while it is a defect, a missing feature, or an unverified claim
about the tree.

Read top to bottom. The numbered items are the long-standing list;
the un-numbered entries below them are smaller and were filed later.
Numbers are stable -- a closed number is retired, not reused.

**Recently closed, do not re-open without new evidence:**
- **`logname` and `hostid` are stubs.** Fixed and documented in `f4dd5ee`.
  Neither is a missing kernel syscall: Linux x86_64 has no `getlogin` or
  `gethostid` syscall (syscall 25 is `mremap`). Musl libc's `gethostid()`
  hardcodes `return 0;` so `hostid` printing `00000000` is the standard
  upstream Musl behavior on Linux. Musl's `getlogin()` reads `getenv("LOGNAME")`.
  `musl_sh.c` previously invoked `execve` with an empty environment; providing
  `default_envp` with `LOGNAME=root`, `USER=root`, `HOME=/root`, and `TERM=linux`
  allows `logname` to print `root` and exit 0. `coreutils_sweep.exp` asserts
  `root` on `logname` and passes.
- **`sync` / `fsync` syscalls and BusyBox commands.**
  Fixed by `f49c392` (kernel syscalls) and `6d14dc9` (BusyBox config).
  Kernel side implements `SYS_FSYNC(74)` (flushes `FIL` data/metadata
  via `f_sync`, calls `f_sync_volume` on the underlying `FATFS` instance
  to flush window and drive cache; flushes directories via `f_sync_volume`;
  returns `-EBADF` on invalid fds and `-EINVAL` on pipes/special fds)
  and `SYS_SYNC(162)` (iterates all mounted volumes via `f_syncfs` and
  calls `ata_flush_cache`). BusyBox config enabled `CONFIG_SYNC=y` and
  `CONFIG_FSYNC=y`. Verified with `test_sync` in guest and
  `tools/coreutils_sweep.exp` PASS=100 (both `sync` and `fsync` pass).
- **`touch -d '<date>'` on a nonexistent path kept `get_fattime`'s
  value; `f_utime` on a just-created file returned `FR_NO_FILE`.**
  Fixed by `1a737e2`. The proven cause was **not** a missing flush in
  `f_open` and **not** a wrong `dir_sect`: `f_settime` (the
  `futimens` path) wrote `DIR_ModTime` correctly, and then
  `f_close` -> `f_sync` re-stamped it to `GET_FATTIME()` because
  `f_open` sets `FA_MODIFIED` on create. The fix is `FIL.timeset`
  (`ff.h`), set by `f_settime` on success, initialised to 0 in
  `f_open`, skipped and cleared by `f_sync`. `f_utime` (path form)
  needed no flag. The `coreutils_sweep` `touch` row is now a real
  `auto` assertion and passes. Do not re-file "touch -d on a fresh
  path"; it is closed and gated.
- **The `time true` fault** -- fixed by `6406106`. A stale `%cr3` at
  the `sysret` after vfork+exec: `sys_execve` switched `self->cr3`
  but never loaded it into `%cr3`, and the sysret stub does not
  reload `%cr3`, so the vfork child sysret'd into the parent's
  address space and faulted at `CR2 = 0` / `RIP = 0` /
  `error = 0x15` on the first fetch. All the earlier routes
  (`static_init_tls`, `fs_base`, the linker routes, `939e3ef`,
  `153dda9`) are refuted and recorded in `handoff.md`'s
  "WHAT WAS REFUTED". The full pre-fix record is in `git log`
  around `6406106`.

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

**`getrusage` / `times` accounting returns garbage.** `time true`
runs and its `real` is correct, but `user` / `sys` print about
`152710235h`. The vfork+exec path is fixed (`6406106`); this is a
**separate kernel accounting bug** and it is what makes `time`'s
`user`/`sys` columns nonsense. Do **not** fix it in a session that
touches `sys_execve` -- give it its own session and its own commit.
The investigation starts at `sys_times`/`sys_getrusage` and the tick
accounting in `scheduler.c` (whether per-process `utime`/`stime` are
accumulated at all, and in what unit).

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

**`f_utime` (path form) does not protect an open `FIL`'s timestamp.**
`1a737e2` guards the timestamp a `futimens`/`f_settime` caller set on
an **open** `FIL` via `FIL.timeset`. The **path** form, `f_utime`,
writes the directory entry directly and has no `FIL` to mark, so if
the same file is **also** open with `FA_MODIFIED` set, that open
`FIL`'s `f_close` -> `f_sync` will still re-stamp `ModTime` with
`GET_FATTIME()`, overwriting the path-form value. No caller in the
tree does this today -- busybox's `touch -d` goes through
`futimens` -> `f_settime`, not the path form, once the file exists --
so it is a latent corner, not a live bug. If a future caller needs
it, the fix is a timestamp generation counter or a per-volume "last
timestamp write" epoch, not another `FIL` flag.

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
hands the driver, which is the honest extent.  Verified: `umask`
prints `0022`, `umask 077` then `umask` prints `0077`, `umask 022`
restores `0022`.  **This is a syscall-completion fix, not a mode
store** -- `chmod`/`chown` and item 9 remain the places a real mode
store would land.

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
dead code; a one-run `serial_print` named it.  With both, `mkdir -p
/b/c/d` succeeds silently, `ls /b/c/d` is empty, `mkdir /b` prints
`File exists`, and `mkdir ""` still returns `-ENOENT`.  Correct POSIX
for the file case (`-ENOTDIR` when the target exists as a file) is
**not** implemented and is not covered by the `FR_EXIST`
translation -- `f_mkdir` does not distinguish "exists as file" from
"exists as directory" in the error it returns.  That refinement is
open.

**`set_tid_address` (218) `clear_child_tid` is implemented.**
`sys_set_tid_address` keeps its return value (the current pid, which
musl reads in `__libc_start_main`) and stores the user VA in
`pcb_t.clear_child_tid`; `process_exit` (scheduler.c) writes `0` to
`*clear_child_tid` after `close_all_files` and before the state
change, while the exiting process's own CR3 is still loaded.  The
store rejects `0` and non-canonical-kernel VAs but does **not** walk
the page table, so a canonical-but-unmapped VA would fault there.
No current caller does that; making it a safe `copy_to_user`-style
store is a separate hardening.

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
r8 and r9 (16), and rdi/rsi/rdx/r10 (32) -- 120 bytes total, from
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

The number that confused an earlier note -- 128 -- is the value of
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

**Item 7a -- the boot-time `#PF` at `0x400000`.**  Unobserved, not
fixed.  An intermittent fault that stops reproducing is not closed.
The `20261003-vmm-map-diag` instrumentation (the three silent `return
-1;` sites in `vmm_map_page_in_cr3` each print `VMM: map failed
site=<NAME> virt=... cr3=... free=NNN`) is in the tree and did not
fire on the boots that followed.  When it returns, the `free` field
separates the two candidates: a healthy count with `site=PDPT` or
`site=PD` is a missing clone table; a near-zero count is PMM
exhaustion in a shape the session-48 wrap does not cover.

**Item 7b -- a user-mode `#GP` at `0x42F1A7` in busybox during
`find`.**  Unobserved, not fixed.  Different vector, different
location, different phase from 7a.  A user `#GP` with error `0` is
raised for a privileged instruction, a non-canonical address in a
base register, or a segment violation.  It needs its own instrument --
a first-party reproduction of the `find` sequence, or a kernel-side
`#GP` handler trace -- before it is guessed at.  It is not the same
fault as 7a and must not be folded into 7a.

**Observed again, 2026-10-23, and not reproduced.**  A user-mode
`#PF` fired during 5000 iterations of `echo hi | wc` in a boot that
had first run `sigchld_probe`, `kill_self`, `sigsegv_probe`, and
`sigterm_ignore`.  CR2 `0x876CA6`, RIP `0x413962`, error `0x5`
(present + user + read), the CR2 inside the 2 MB page at phys base
`0x800000` -- a user-mode read of a kernel physical address, which is
the 7b/7e signature.  The boot's capture contained exactly one `#PF`;
the nine signal probes were clean.  The kernel killed the faulting
busybox and returned to the fallback `musl_sh` prompt; the system
survived.

> **Cross-reference.**  The canary `.text` `#PF` (tracked in
> `handoff.md`) faulted at `RIP=CR2=0x413966` before 2026-11-02, and
> at `0x450630` on an unlucky post-`7cb26fb` run -- `error 0x14`
> (present, user, fetch), `pte=0`, canary's own cr3.  The `0x413962`
> `#PF` above is on the *same page* as the `0x413966` canary fault
> (offset 4 bytes).  Different `error` code (`0x5` read vs `0x14`
> fetch) and different process (`echo hi | wc` vs `canary --full`),
> so they are not proven to be the same fault -- but close enough to
> flag.  The canary `.text` `#PF` did not reproduce after the
> 2026-11-02 IF fix (`162a1d3`).  **Do not merge them without
> evidence.**

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

**Item 7e -- ash's stale `jmp_buf`.**  The kernel-side faces of 7e are
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
`test.sh`.  Not a kernel bug.

**A nonexistent pid stats as a directory.**  `stat("/proc/999999/")`
reports `S_IFDIR` rather than `ENOENT`, because the check is on the
path shape, not on `process_find_by_pid`.  `ps` only stats pids
`readdir` gave it, so it does not affect the consumer.  Worth one
line when `/proc` is next touched.

**`gettimeofday` (96) and `time` (201) are implemented.**  Both read
the existing 100 Hz `g_ticks` counter plus the boot RTC offset, so
`tv_sec` is wall-clock time (not seconds since boot, as it was
before `1479723`/`9d7b5d3`), and `tv_usec` has 10 ms PIT
granularity.  `date` and `uptime` use them.  **`time`'s accounting
is the open bug above** (`user`/`sys` garbage), not these two
syscalls.  Noted so they are not re-filed as "unimplemented".

**A live pipe slot's `end` field is 0; no writer is in the source.**
Latent, not active.  A `file_slot_t` of kind `FILE_KIND_PIPE` was
captured with `refcount = 2` and `slot->end = 0`, at a fixed
kernel-heap address.  `PIPE_END_READ` is 1 and `PIPE_END_WRITE` is 2;
`0` is neither.  The three writes to a pipe slot's `end` in
`user_syscall.c` all write 1 or 2, never 0.  The writer is not in the
source.

The observable consequence is closed: `put_file_slot` refuses to free
a pipe when a slot's `end` is neither READ nor WRITE -- it records the
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

- **Control bytes need a socket serial in `.exp` harnesses.**
  `./run` pipes QEMU through `tee` (Pattern A), so an expect pty byte
  never reaches the guest; `-serial stdio` (Pattern B) makes `^C` the
  tty INTR char and SIGINTs QEMU. Any harness that sends `^C`/`^Z`
  must use `-serial unix:...,server,nowait` + `socat -,raw,echo=0`.
  See `docs/expect-harness.md` and `tools/coreutils_sweep.exp`.
  **Plain-text commands through `headless` also proved unreliable
  this session** -- several `.exp` writes never reached the guest.
  For anything that matters, prefer `./run telnet` or a UNIX socket.

- **No timezone: `date` prints the host's local time labeled
  `UTC`.** The RTC work reads the CMOS and reports the epoch
  second count; the C library prints that as `UTC` because
  donix has no `TZ` and no `/usr/share/zoneinfo`.  `run` now
  passes `-rtc base=localtime` (`20261106-rtc-localtime`), so
  the RTC carries the host's local time and the guest's `date`
  shows the same wall time the host does -- mislabeled
  `UTC` because the label is the absent TZ.  This matches a
  real PC's CMOS, which is localtime by default.  The proper
  fix is a TZ in the shell's envp: `load_elf_into_user_process`
  in `kmain.c` builds the initial `musl_sh` frame with
  `argc=0`, `argv=NULL`, `envp=NULL` -- the shell has no
  environment, and the envp propagates through `musl_sh`'s
  `execve` of busybox.  Setting `TZ=EST5EDT` (musl parses the
  POSIX TZ string without a zoneinfo file) needs the same
  argv/envp frame layout as `sys_execve`'s section 6 -- ~80
  lines.  Own session.  **The same frame is the `PATH` gap below.**

- **The shell does not export `PATH`.** `env | wc -l` is 2 and
  `printenv PATH` is empty, because the initial `musl_sh` frame has
  `envp = NULL`.  This is an environment gap, not a syscall gap, and
  it lands in the same `kmain.c` frame code as the TZ item.  One
  session can do both; it needs the `sys_execve` section-6 argv/envp
  layout.

- `kill(0, sig)` ("my group") still returns `-EINVAL`.
  `kill(-pgid, sig)` works (`9b1a4ed`).
- `sys_brk`'s fixed `heap_base` and the 4 MB mmap window are latent
  collisions.
- `prctl` is minimal (`PR_SET_NAME` accepted and dropped).
- Syscall-table audit script.
- `musl_wait`'s WNOHANG loop spins.
- `sys_mmap` rejects all non-anonymous mappings (a file-backed `mmap`
  caller gets `-ENOMEM` and must fall back to `read`; a Wayland
  prerequisite).
- Pipes support one concurrent reader and one concurrent writer (see
  `pipe_t`'s comment in `user_syscall.c` -- a second blocked reader on
  the same pipe end has nowhere to record itself and will only wake
  on a keyboard IRQ).
- `sys_mkdir` does not distinguish "exists as file" from "exists as
  directory": both come back from `f_mkdir` as `FR_EXIST`, which
  `fat_mkdir` translates to `-EEXIST`.  POSIX wants `-ENOTDIR` for
  the file case.  Cosmetic today; no caller in the tree depends on
  the distinction.
- `busybox sh` re-entry at `donix>` prints a cosmetic
  `sys_execve: f_open(usr/bin/BUSYBOX) -> 4` before the ash prompt
  returns.  It recovers; no behavior is lost.  Worth one line when
  the exec path is next touched.

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
- `sys_execve` now loads `%cr3` from `self->cr3` at the end of
  section 8 (`6406106`).  Any change to the exec stack, the `%cr3`
  switch, or the sysret stub must be tested with the two shell gate
  tests in `handoff.md`'s "THE HARNESS RULE", not just
  `canary`/`test.sh`.
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
- **`FIL.timeset` is cleared by `f_sync` after a `FA_MODIFIED`
  sync.** Any new code that sets `DIR_ModTime` on an **open** `FIL`
  must set `fp->timeset` too, or the close-time `f_sync` will
  overwrite it with `GET_FATTIME()`.  The path form `f_utime` is not
  covered (see the un-numbered item above).
- **`ff.c` and `ff.h` are CRLF; `user_syscall.c` is LF.** Any edit
  to those files must use binary-safe I/O (`open(p,"rb")` /
  `write_bytes`), and the patch script must assert the file's
  endings before and after.  A `write_text` on `ff.c` turns a
  one-line change into a whole-file diff.

---

## Measurement corrections and history notes

**The sector-128 theory was a red herring.** During the `touch -d`
investigation, `fp->dir_sect = 128` for a freshly created file
looked like a collision with the boot kernel's `dd … seek=128`. It
is not: `128` is the **volume-relative** sector of `::/`'s root
directory (`fs->dirbase` = `fatbase + fasize`), and `diskio.c`
adds `FAT_PARTITION_OFFSET` 2048 on every read/write, so the raw
disk sector is 2176. `etc/passwd` at `132` is the same root
directory four sectors on. `dir_sect` was correct the whole time;
the bug was the close-time `f_sync` re-stamp.

**`time /bin/true` never worked, and "no fault" was not an
improvement.**  `/bin/true` does not exist.  `sys_execve` reported
`f_open(...) -> 4` (`FR_NO_FILE`) and the shell printed
`can't execute '/bin/true': No such file or directory` and exited 127 --
**before any child existed**.  Valid real-file controls are
`/usr/bin/HELLO`, `/usr/bin/ECHO`, `/usr/bin/CAT`.

**The `exception_frame_t` layout was the reason every earlier "fault
site" was wrong.**  The old struct began at `error_code`; the stubs
push 15 GPRs first.  `frame->rip` read `r14`, `frame->cs` read
`r13`, `frame->rsp` read `r11`.  Fixed by `fb188f0`; every dump
before it must be read with that in mind.

**`$fs_base` is not a per-process marker.**  It is the same VA
(`0x46a138`) in every busybox process; `%rsp` is the same VA across
processes.  **Only `pid` or `CR3` distinguishes processes.**  Every
gdb printf on a fault must print `pid` or `CR3`, or the lines are
cross-process and the inference is invalid.

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
