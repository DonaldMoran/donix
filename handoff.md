# donix â€” Handoff

This file is both the project strategy and the current session status.
Read it top to bottom when starting a new session. Update the "Session
Status" section at the end of every session.

---

# Part 1 â€” Project Strategy

## What donix is

donix is a fork of **dons-os**, cloned at dons-os's known-good baseline
on branch `dev`. Goal: a Unix-like OS that runs static musl-linked
binaries and speaks the Linux x86_64 syscall ABI natively. dons-os stays
intact as the recovery point.

## Baseline facts

- dons-os `dev` works: custom syscall numbers, spawn-style process
  creation, working shell, and the `hello` / `memtest` / `ls` / `cat` /
  `echo` / `fstest` / `multitest` / `bigtest` programs all run. newlib
  `printf` and `malloc` work correctly.
- The previous migration session's stdio failure was **self-inflicted**
  by unnecessary changes to `arc2/crt0.S`, `user_newlib_linker.ld`, and
  `arc2/syscalls.c` semantics. Those files work as-is. Leave them alone.

## Strategy: two phases

### Phase A â€” Linux syscall ABI, then static musl binaries

Make the kernel speak Linux x86_64 syscalls and run a static musl
binary. Newlib's existing userland stays as a **passive regression
canary**: after every kernel change, `hello` / `memtest` / `ls` / `cat` /
`echo` / `printf` / `malloc` must still work.

Do **not** add new features to the newlib userland â€” no `fork`, no
`execve`, no `getdents64` wrappers. That work would be thrown away when
musl arrives.

### Phase B â€” busybox / coreutils against musl

Static-link busybox against musl and try running it.

## The one critical rule

**One change at a time. Test. Commit. Revert on failure.**

The previous session failed by changing six files at once; each symptom
had a different cause. Never apply two changes without testing between
them.

## Files that must not be touched

Unless a specific tested problem requires it:

- `process.c` â€” especially `process_copy_kernel_frame`
- `scheduler.c`
- `context_switch.asm`
- `interrupts.c`
- `kmain.c`
- the prebuilt newlib `.a` files
- `arc2/crt0.S`
- `arc2/reent.c`
- `user_newlib_linker.ld`

All of these work in the baseline. The last three are the ones the
previous session damaged.

## Phase A milestones

### A1 â€” pure syscall renumbering

Change syscall numbers to Linux x86_64. **No semantics change. No new
syscalls.**

Files: `include/syscall.h`, `user_syscall_entry.asm` (the single
`cmp rbx, 2 â†’ cmp rbx, 60` reference), `user_syscall.c` (`case N:`
labels only), `arc2/syscalls.c` (`#define SYS_*` numbers only). Four
files, mechanical.

**Test:** `hello`, `memtest`, `ls`, `cat`, `echo`, `printf`, `malloc`
â€” all still work.

**Status: complete at tag `20260922H`.**

### A2 â€” add the syscalls musl needs, one at a time

Add each to the **kernel** and test with a **tiny static musl binary**.
Newlib's existing userland is the regression canary after each â€” but do
not add new wrappers to it.

Order (musl's `__libc_start_main` calls these in sequence; each blocks
the next):

1. `arch_prctl(ARCH_SET_FS)` = 158 â€” **complete**
2. `set_tid_address` = 218 â€” **complete**
3. `rt_sigaction` = 13 (stub, return 0) â€” **complete**
4. `rt_sigprocmask` = 14 (return 0) â€” **complete**
5. `set_robust_list` = 273 (return 0) â€” **complete**
6. `ioctl` = 16 (`TCGETS` for stdio, `-ENOTTY` otherwise) â€” **complete**
7. `brk` = 12 â€” **complete at `20260924B`**, Linux absolute-address ABI
8. **Test:** minimal musl program reaches `main` and `printf` works â€”
   `musl_min` green; `musl_printf` red until `20260924L`, see
   "Resolved bugs"
9. `mmap` = 9, `munmap` = 11 â€” **complete at `20260924B`** (minimal
   anonymous-private implementation)
9.5. `mprotect` = 10 â€” **stub added at `20260924B`**, returns 0.
   Not on the original list; musl calls it after `mmap` and its
   absence caused a `#GP`.  Real permission changes not implemented.
10. `getrandom` = 318, `rseq` = 334 (stubs returning `-ENOSYS`) â€” complete
11. `fork` = 57 â€” **complete at `20260924D`**
12. `execve` = 59 â€” **complete at `20260924I`** (both steps: spawn
    moved to 507, real in-place execve at 59).  Exercised by the new
    `musl_exec` test.
13. `wait4` = 61 â€” **complete at `20260924J`** (blocking wait, reap,
    parent-pid preserved across execve â€” all exercised by
    `musl_exec`; status propagation, `WNOHANG`, and `wait4(-1)` now
    exercised by `musl_wait`).  Required the `"+m"(*status)` asm
    fix in the test and Linux status-word encoding in `sys_wait4`.
14. `getdents64` = 217 â€” **complete at `20260924K`**.  Exercised by
    the new `musl_readdir` test (21 entries listed, count matches
    the image build).  Required a companion change to `sys_open`:
    musl's `opendir` goes through Linux `open(2)` with
    `O_DIRECTORY`, not the donix-private `SYS_OPENDIR` (500), so
    `sys_open` now falls back to `f_opendir` when `f_open` fails on
    a directory.  See "Syscall ABI" gotcha below.

**A2 complete at `20260924K`.**  Each item was its own commit.  After
each, newlib's `hello` / `memtest` / `ls` / `cat` / `echo` /
`printf` / `malloc` still worked.

### A3 â€” musl shell

Minimal shell in musl: `fork` + `execve` + `wait4`. Replaces the newlib
shell as default. Newlib shell stays as a fallback until musl's is
proven.

**Unblocked at `20260924L`.**  The blocker was `musl_printf` /
`printnum`, which turned out to be a kernel bug in the syscall return
path, not a musl-internal problem.  See "Resolved bugs" below.  All
syscalls A3 needs (`fork`, `execve`, `wait4`, plus stdio) are green.

**Design note for A3.**  The newlib shell does **not** `wait4` after
spawning a child; the shell and the child run concurrently and their
console output interleaves byte-by-byte.  This is visible in the
`20260924L` capture as `] ABCDEF` on the spawn line.  A musl shell
that calls `wait4` after `fork`/`execve` will naturally serialize
parent and child output.  Adopt that pattern from the start.

**Progress at `20260926D`.**  A minimal musl shell (`musl_sh`) now
exists, bootstrapped through `build_musl_tests.sh` and wired into the
image build (see "musl test binaries" under Testing harness).  It is
launched manually from the newlib shell (`musl_sh.elf` at the `] `
prompt) and takes over the console.  It does `fork` + `execve` +
`wait4` and the `wait4` correctly serializes parent and child output.
It reads a line byte-at-a-time and echoes as typed.  Command paths
must be fully qualified (`0:/NAME.ELF`); it does not yet do the
newlib shell's `0:/` + `.ELF` normalization.  It does not tokenize
its command line; the whole line is passed as `argv[0]` and as the
`execve` path.

**Unblocked at `20260926E`.**  The second-`execve` fatal fault was
**not** page-table corruption, as the previous session suspected.
It was `MSR_FS_BASE` not being part of the process context.  See
"Resolved bugs" below.  `musl_sh` now runs commands repeatedly from
a fresh boot.

**Remaining A3 work.**
- Tokenize `musl_sh`'s command line so `cat.elf file.txt` passes
  `argv[1]` through to the child.  Currently the whole line is
  treated as one path.
- Add `0:/` + `.ELF` path normalization so bare `hello` resolves to
  `0:/HELLO.ELF`, matching the newlib shell's behavior.
- Make `musl_sh` the default boot shell, keeping the newlib shell
  reachable as a fallback binary.

### A4 â€” migrate userland apps to musl

`hello`, `ls`, `cat`, `echo`, `memtest` â€” rebuild each against musl.
**One at a time.** Test each.

### A5 â€” retire newlib

Remove the newlib userland, build rules, and libraries. Safe because
nothing uses them.

## musl build

Clone `git://git.musl-libc.org/musl`, configure for `x86_64-linux-musl`
with the cross toolchain, `make install` â†’ `libc.a`, `crt1.o`,
`crti.o`, `crtn.o`. Link static test programs against those. Keep musl's
build out of the main build until Phase A3.

**Minimal musl test:**

```c
#include <unistd.h>
#include <stdio.h>
int main(void) {
    write(1, "MUSL-START\n", 11);
    printf("MUSL-PRINTF\n");
    write(1, "MUSL-END\n", 9);
    return 0;
}
```

## Known bugs and gotchas

Apply each only when a specific problem requires it.

### Process / scheduler

- **`process_fork_copy_frame` must preserve the registers that the
  parent's syscall-return path preserves.**  That is: the
  callee-saved set (`%rbx`, `%rbp`, `%r12`â€“`%r15`) **plus `%r8` and
  `%r9`**.  The syscall entry code (`user_syscall_entry.asm`) pushes
  `%r8` and `%r9` before the argument shuffle and pops them on return,
  so the parent's `%r8`/`%r9` survive across a syscall, and the
  compiler may rely on them.  An earlier version of `fork` zeroed
  `%r8`, which made the child call `read` instead of `write` because
  the code after the `syscall` in `musl_fork_raw`'s `main` was
  `mov %r8, %rax` (setting up the `write` fd argument).

  Parent's saved GPRs are at `[parent->kernel_stack_top - 8*k]`:
  `-8=rbx, -16=rbp, -24=r12, -32=r13, -40=r14, -48=r15, -56=user RIP,
  -64=user RFLAGS, -72=user RSP, -80=r8, -88=r9`.

- **`fork` needs eager user-stack copy after CR3 clone.**
  `vmm_clone_page_table` shares leaf physical pages; the parent's stack
  writes clobber the child.  Walk
  `[user_stack_virt, user_stack_top)`, allocate fresh pages with
  `pmm_alloc_page_for_elf`, copy, remap child PTEs.

- **`fork` must inherit the parent's `fs_base`.**  `MSR_FS_BASE` is
  per-process, and musl's `__post_Fork` reads `%fs:0x0` on the child's
  first instruction after `_Fork` returns.  Without
  `child->fs_base = parent->fs_base;` in `sys_fork`, the child runs
  with `MSR_FS_BASE = 0` and faults at `CR2 = 0`.  Fixed at
  `20260926E`; see "Resolved bugs" below.

- **`execve` must update the syscall-entry frame's RIP and RSP**, not
  just the PCB.  The return path (`user_syscall_entry.asm`'s `sysret`)
  reads the frame at `[kernel_stack_top - 56]` (user RIP) and
  `[kernel_stack_top - 72]` (user RSP).  These are `ktop[-7]` and
  `ktop[-9]` in C.  The shipped `sys_execve` writes exactly these two
  slots.  If the assembly push order changes, these offsets must
  change with it.

- **`sys_execve` must be atomic** (`cli`/`sti`).  A previous session saw
  `#GP` when the timer fired mid-operation.

### Scheduler queue discipline

- **`scheduler_ready_queue_add` is idempotent.**  A process that is
  already on the queue must not be linked again; the second link
  overwrites `prev`/`next` and can produce a self-link or a cycle.
  `scheduler_ready_queue_contains` is the guard.

- **`process_wake_all_blocked` must skip processes already on the
  queue.**  `sys_read`'s blocking loop sets `state = BLOCKED` and
  `hlt`s; a timer tick can run `process_wake_all_blocked` before the
  `hlt` returns, and without the skip the process is added on every
  tick.  The symptom was a fork child being added behind the shell in
  the ready queue, so the child never ran before the parent's zombie
  wake resumed the shell.

- **`process_yield` removes a BLOCKED process from the ready queue.**
  The timer handler pops a RUNNING process from the queue and then
  re-adds it; a process that blocks between the pop and the yield can
  be left on the queue.  A process that is on the ready queue while
  BLOCKED will be picked ahead of runnable processes.

- **`process_exit`'s empty-queue fallback must switch to idle, not
  halt.**  When a user process exits and the ready queue is empty
  (e.g. a forked child exits while the shell is blocked in `sys_read`
  and off the queue), `process_exit` must switch to the idle process
  (pid 1) rather than halting.  Idle `hlt`s until the next timer tick
  or keyboard IRQ; the shell is then woken normally.  Fixed at
  `20260924F`.  See "Resolved bugs" below.

### Context switch

- **`MSR_FS_BASE` (0xC0000100) must be saved and restored per process.**
  It is set once by `arch_prctl(ARCH_SET_FS)` at musl startup and is
  read on the child's very first user instruction after `fork`
  (musl's `__post_Fork` → `__get_tp` → `mov %fs:0x0, %rdx`).  The
  kernel must therefore:
  - record it in `sys_arch_set_fs` and
    `sys_arch_prctl(ARCH_SET_FS)` (`self->fs_base = addr`),
  - inherit it in `sys_fork`
    (`child->fs_base = parent->fs_base;`),
  - save/restore around every context switch — both the preemptive
    path in `timer_preempt_handler` (which switches CR3 directly,
    not through `context_switch`) and the three `context_switch`
    call sites in `scheduler.c`.

  Without save/restore, the MSR is effectively per-CPU and a second
  musl process clobbers the first one's TLS base.  Without
  inheritance, a fork child runs with `%fs = 0`.  Both failure modes
  are `#PF` in user mode at near-null addresses (`CR2 = 0x34` for the
  clobber case, `CR2 = 0` for the inheritance case).  Fixed at
  `20260926E`.

  `fs_base` is appended after `file_table` in `pcb_t` so
  `context_switch.asm`'s hardcoded offsets (which stop at
  `block_kind = 0x158`) are unchanged.  Any future field added to
  `pcb_t` must also be appended after `block_kind` for the same
  reason.

### Syscall ABI

- **The syscall return path must preserve every GPR except `%rax`,
  `%rcx`, `%r11`.**  The Linux x86_64 syscall ABI clobbers exactly
  those three.  Compilers rely on the rest surviving; musl's
  `__stdio_write` keeps a live pointer in `%r10` across the `writev`
  syscall.  **If the epilogue needs a scratch register for the user
  RSP, do not sacrifice a GPR: load `%rsp` from the kernel stack frame
  last, via `mov rsp, [rsp - 72]` after the other pops.**  This was
  the fix at `20260924L`; see "Resolved bugs" below.

- **`sys_read` on fd 0 must return on the first available byte, not
  on `count`.**  POSIX `read(2)` on a terminal returns when at least
  one byte is available; it does not block until `count` bytes have
  accumulated.  The original donix `sys_read` looped until
  `bytes_read == count`.  That was invisible under newlib (whose
  shell reads 1 byte at a time, `count == 1`) but broke musl's
  `read(0, buf, 255)`: it waited for 255 keystrokes before returning.
  Fixed at `20260926A`.  This is the same *class* of bug as the
  `%r10` clobber â€” the kernel's ABI did not match Linux's, and
  newlib's usage never exercised the difference.  Musl did.

- **`SYS_EXIT = 60` in `user_syscall_entry.asm`** â€” the only numeric
  syscall reference in assembly.
- **`-mcmodel=large` is required for userland** (linked above 4 GB).
  `-mcmodel=small`/`medium` don't work. `-no-pie` is not needed.
- **The syscall entry/return frame saves 16 slots, not 12.**  In
  addition to the callee-saved registers and the user RIP/RFLAGS/RSP,
  `user_syscall_entry.asm` pushes `%rdi`, `%rsi`, `%rdx`, `%r10`
  before the argument shuffle.  On the return path, **all four are
  restored** â€” `%r10` was previously discarded, which was wrong and
  broke musl's `printf` until `20260924L`.  The user RSP is now loaded
  from the frame at the very end, using no GPR.

  Frame layout, offsets from `parent->kernel_stack_top` (first push =
  offset `-8`):
  `-8=rbx, -16=rbp, -24=r12, -32=r13, -40=r14, -48=r15, -56=user RIP,
  -64=user RFLAGS, -72=user RSP, -80=r8, -88=r9, -96=rdi, -104=rsi,
  -112=rdx, -120=r10, -128=arg5 (discarded)`.

  `process_fork_copy_frame` reads from those offsets to build a fork
  child's frame.  If the assembly push order changes, that function's
  offsets must change with it.

- **The return-path epilogue loads the user RSP last, from memory.**
  After popping `%r10`, `%rdx`, `%rsi`, `%rdi`, `%r9`, `%r8`, the
  stack pointer is at the user RSP slot.  The epilogue loads `%r11`
  (user RFLAGS) from `[rsp + 8]` and `%rcx` (user RIP) from
  `[rsp + 16]`, then `add rsp, 24` and pops `%r15`/`%r14`/`%r13`/
  `%r12`/`%rbp`/`%rbx`, then `mov rsp, [rsp - 72]` to load the user
  RSP with no intermediate GPR.  `%rax` is never touched on this
  path: it carries the syscall return value to `sysret`.

- **`SYS_DONIX_SPAWN` (507) is the newlib spawn number.**  The newlib
  userland calls 507 for spawn semantics (`arc2/syscalls.c:spawn`).
  Number 59 is the real Linux `execve`, implemented at `20260924I`.
  `sys_spawn` (507) still creates a new process; `sys_execve` (59)
  replaces the caller's address space in place.  Do not point newlib
  at 59.

- **A raw-syscall test that reads a kernel-written buffer must declare
  the buffer as an asm memory output â€” `"+m"(*ptr)`, not just
  `"memory"`.**  A `"memory"` clobber is an aliasing/scheduling
  barrier, not a per-location output; GCC does not treat it as "the
  asm wrote through this specific pointer."  `musl_wait`'s
  `raw_wait4` silently read a stale `status` (initialized to `-1`) on
  the first `wait4(-1)` of a two-reap sequence until the constraint
  became `"+m"(*status)`; the kernel was writing the correct value
  (confirmed with a temporary serial print in `sys_wait4`).  Same
  rule applies to any future test that passes `&local` to a raw
  syscall and then reads `local` back.

- **To read a specific GPR after a raw `syscall`, pin it with a
  register-bound local.**  The idiom that works:

  ```c
  register uint64_t r10_reg asm("r10") = SENTINEL;
  __asm__ volatile ("syscall"
      : "=a"(ret), "+r"(r10_reg)
      : "a"(20L), "D"(1L), "S"(iov), "d"(2L)
      : "rcx", "r11", "memory");
  ```

  The `asm("r10")` binding pins the value to `%r10`.  Using a plain
  `"r"` operand lets GCC pick any register and defeats the test.
  `musl_r10probe` uses this idiom to prove the syscall return path
  preserves `%r10`.

- **musl's POSIX wrappers do not always go through your donix-private
  syscalls.**  musl's `opendir()` is `open(path, O_RDONLY|O_DIRECTORY)`
  followed by `readdir()` which uses `getdents64(2)`.  It never calls
  the donix-private `SYS_OPENDIR` (500) â€” that is a newlib-only
  convenience.  `sys_open` had to grow a directory fallback (at
  `20260924K`) because otherwise musl's `opendir` failed with
  `FR_INVALID_NAME`.  When you add a syscall that has a donix-private
  equivalent, check which one musl actually uses before assuming they
  map 1:1.

- **`getdents64` emits one record per call, deliberately.**  FatFs's
  `f_readdir` advances an irreversible cursor.  If `sys_getdents64`
  tried to pack multiple records into one call and one did not fit,
  the entry would be consumed but not returned and musl's `readdir`
  would skip it.  One record per call is obviously correct.  musl's
  `readdir` passes a buffer big enough for one `linux_dirent64`, so
  the fit check never fails in practice.

### stdio / newlib

- **If newlib `printf` breaks after a change, suspect the change, not
  newlib.** In baseline it works. Check the four A1 files against the
  table; check that no case label or handler body changed; if `crt0.S`
  or the linker script were modified, revert them.

### Build system (fixed 2026-09-24, tag `20260924A`)

- **The newlib userland Makefile needs `mkdir -p` for build dirs and
  `lib/libc.a` as a prerequisite.** `make <app>` from a clean tree
  failed with "can't create build/apps/memtest.o: No such file or
  directory" because `prep` only ran via `all`. Worse, `.o` rebuilds
  did not force `.elf` relinks, so a stale `sbrk` was being linked and
  masking new code. Fixed with `| dirs` order-only prerequisites and
  `lib/libc.a` added to each `.elf` target.
- **Always confirm the .elf is relinked** after touching a source:
  `ls -l apps/NAME.elf` should be newer than the `.o` files. A stale
  link can hide a real fix for an entire session.
- **After every patch, verify the edit actually landed in the
  tree you are building.**  A session was lost to editing the wrong
  project directory: the kernel was built from a stale file, and
  every test produced the same "the fix didn't work" result for
  hours.  Before each build, run `git status` (shows the file as
  modified) and a targeted `grep` on the changed function.  If
  either is unexpected, stop and fix the tree before building.
- **QEMU `-d in_asm,cpu -D /tmp/qemu-log.txt` makes everything run
  ~1000Ă— slower.**  It's fine for one-off instruction traces, but if
  left on, a loop of a few hundred thousand iterations looks like a
  hang.  Turn it off for normal testing; turn it back on only for a
  specific investigation.  The normal `./run` path does not set this
  flag; only the `run-debug-log` target does, and that is not on the
  default path.

### Git hygiene (learned 2026-09-24, tag `20260924F`)

- **Do not use `git add -A` when committing a single logical change.**
  In this session, `git add -A` on the halt-fix commit staged *every*
  modified file in the tree, including a separate in-flight rename of
  the spawn syscall.  The commit message described only the halt fix,
  but the commit actually contains the rename too.  The result was
  tested green, so it was left in place rather than rewritten, but the
  commit message is now misleading and the history can't be bisected
  cleanly through that range.
- **Use explicit `git add <file>...` for each commit**, listing only
  the files that belong to that change.  Verify with `git diff --cached
  --stat` before `git commit`.  If a file you didn't intend appears in
  the staged set, `git restore --staged <file>` it before committing.

### Resolved bugs (kept for the record)

- **Intermittent halt after a sequence with forking programs**
  (resolved 2026-09-24, tag `20260924F`).  After running a sequence
  like `hello`, `memtest`, `ls`, `cat`, `echo`, `musl_min`,
  `musl_malloc`, `musl_fork`, the kernel *sometimes* halted with
  `process_exit: no runnable process, halting`.

  Root cause: a forked child exits while the shell is blocked in
  `sys_read` (keyboard block, off the ready queue).  If the queue is
  empty at that moment, `process_exit`'s fallback path took the halt
  branch even though idle (pid 1) is a valid runnable process.  The
  bug was nondeterministic because it depended on the interleaving of
  the shell's block and the child's exit.

  Diagnosis: two diagnostic prints (`EXIT:` at the top of
  `process_exit`, `WAKE:`/`IRQ1-pre`/`IRQ1-post` around
  `process_wake_all_blocked`) captured a run where the child exited
  with `qhead=(empty)` and took the halt.  The same sequence on other
  runs showed a non-empty queue, confirming the race.

  Fix: in `process_exit`'s fallback path, if `idle` is a valid PCB
  (pid 1 exists), switch to it via `context_switch(exiting, idle)`
  instead of halting.  Idle `hlt`s until the next timer tick; the
  next keyboard IRQ wakes the shell via `process_wake_all_blocked`,
  and `timer_preempt_handler` picks it up on the following tick.

  A secondary symptom â€” "shell prompt returns but keyboard goes
  dead" â€” was reported but never reproduced.  It may be the same
  race with a different interleaving.  If it reappears, reopen.
  (See also the `20260926` entry below, which turned out *not* to be
  this.)

- **Linux `brk` ABI vs. increment `sbrk`** (resolved 2026-09-24, tag
  `20260924B`).  The original symptom was misdiagnosed as a PMM
  accounting bug: during `MUSL_MALLOC.ELF`, `pmm_free_pages` dropped
  from ~31663 to ~7922 in one step, and the HIGH zone appeared
  exhausted after 65 successful allocations.  A per-call diagnostic in
  `pmm_alloc_page` (still in the tree, gated by `PMM_ALLOC_DIAG`)
  showed the truth: `sys_brk` was being called in an unbounded loop,
  because musl calls `brk(absolute_address)` while the kernel expected
  `brk(increment)`.  musl's `brk(0x26c41000)` was interpreted as "add
  0x26c41000 to the current break", which tried to map ~158,000 pages;
  when that failed and returned `-1`, musl retried, and the loop
  consumed the zone.

  Fix: `sys_brk` (12) now uses the Linux absolute-address ABI, and
  newlib's increment-based `sbrk` moved to a donix-private syscall 505
  (`SYS_DONIX_SBRK`).  The kernel's `sys_sbrk(505)` implements the old
  semantics; `arc2/syscalls.c:sbrk` was updated to call it.

  The PMM was never broken.  The "anomaly" was a correct counter
  reflecting a runaway caller.

- **`mprotect` missing** (resolved 2026-09-24, tag `20260924B`).
  musl calls `mprotect` right after `mmap` to set permissions on the
  new region.  The kernel had no case for it, so the dispatcher
  returned `-1` and musl faulted.  Added as a stub returning 0; real
  permission changes (guarding the `PROT_NONE` pages musl requests
  with `MAP_FIXED`) are not implemented.  If a later test needs real
  protection, add the page-table walk in `sys_mprotect` and update
  the PTE bits.

- **Copy-then-swap `execve` (abandoned)** (investigated 2026-09-24,
  tag `20260924I`).  The first design for in-place `execve` built the
  new address space in a scratch CR3 that shared the caller's kernel
  half, then swapped.  The idea was to preserve the caller's old
  address space on failure (Linux contract).

  It failed.  The scratch CR3 had to share high-half page tables
  with the caller's CR3; `vmm_free_user_page_tables(old_cr3)` then
  freed page-table pages that the new CR3 still referenced, causing
  8 double-frees and a page-fault cascade (recursive #PF, RSP
  marching down by the handler frame size).  Two separate attempts
  at narrowing the shared state both failed.

  Fix: **abandoned copy-then-swap.**  The shipped `execve` does
  teardown-then-load directly in `self->cr3`: no scratch, no swap,
  no shared page tables.  Failure after teardown calls
  `sys_exit(-1)` instead of returning `-errno`; the Linux contract
  is not fully honored on those paths, but the paths are unreachable
  for a validated in-memory ELF.  If a caller-recoverable failure
  is ever needed, revisit copy-then-swap with proper page-table
  ownership.

  The two helper functions written for the abandoned design
  (`vmm_clone_kernel_half`, `vmm_free_user_page_tables` in `vmm.c`)
  are currently unused but kept in the tree; they may be useful when
  `fork` needs real copy-on-write.

- **`musl_printf` / `printnum` dump garbage â€” kernel bug, fixed at
  `20260924L`.**  This was previously misdiagnosed as a musl-internal
  problem.  It was not.  It was a bug in the kernel's syscall return
  path.

  Symptom: `printf` of a literal or a `%d` value on musl/donix
  printed the correct bytes and then dumped large chunks of the
  binary's own `.rodata` / `.eh_frame`.  The newline from
  `printf("MUSL-PRINTF\n")` was silently dropped.  `ls` looped
  forever printing `FILE     (0 bytes)`.

  Raw-byte dump of the user's iov array in `sys_writev`:

      [writev raw] 28 42 40 00 00 00 00 00   iov[0].base = 0x404228
                   0b 00 00 00 00 00 00 00   iov[0].len  = 11
                   57 ff 0f 00 80 00 00 00   iov[1].base = 0x80000FFF57
                   20 ff 0f 00 80 00 00 00   iov[1].len  = 0x80000FFF20

  `iov[0]` is correct (`base = "MUSL-PRINTF\n"` in `.rodata`,
  `len = 11`).  `iov[1]` contains two user-stack addresses where a
  musl-internal buffer pointer and its length should be.  The kernel
  faithfully copied what musl wrote; musl wrote garbage because the
  kernel had corrupted a register musl was relying on.

  Root cause: the syscall return path in `user_syscall_entry.asm`
  discarded the saved `%r10` and reused `%r10` as a scratch for the
  user RSP.  The Linux x86_64 syscall ABI clobbers only `%rax`,
  `%rcx`, `%r11`; every other GPR survives a syscall.  musl's
  `__stdio_write` keeps a live pointer in `%r10` across the `writev`
  syscall; after the syscall, `%r10` held the user RSP instead, so
  musl stored a stack address into `iov[1]`.  `sys_write` then tried
  to write 4 GB from the user stack.

  Confirmation: `musl_r10probe` set `%r10` to `0xDEADBEEFCAFEBABE`,
  issued a raw `writev`, and printed `%r10` afterward.  Before the
  fix: `R10-AFTER=0x00000080000fff60` (a user RSP).  After the fix:
  `R10-AFTER=0xdeadbeefcafebabe`.

  First attempt at the fix (wrong): used `pop rax` to stash the user
  RSP.  That destroyed `%rax`, the syscall return value.  `ls`
  looped forever on a positive-but-meaningless `readdir` return;
  `printnum` printed `x=42` then NUL bytes.

  Corrected fix: restore `%r10` with `pop r10`, do not touch `%rax`
  on the return path, and load the user RSP from the kernel stack
  frame last via `mov rsp, [rsp - 72]`, using no GPR.  Frame layout
  and `process_fork_copy_frame` offsets unchanged.

  New test: `musl_r10probe` (`build_musl_tests.sh`, image Makefile).
  `musl_printf`, `printnum`, and `ls` are all green after the fix.

- **`musl_wait` read a stale status on the first `wait4(-1)`**
  (resolved 2026-09-24, tag `20260924J`).  The test printed
  `WAIT-ANY-1 255` while a temporary serial print in `sys_wait4`
  showed the kernel copied `2816` (= `11 << 8`) to `&status`.  Root
  cause: the test's `raw_wait4` inline asm declared only a
  `"memory"` clobber, which GCC does not treat as writing the
  location pointed to by `%rsi`.  GCC cached the pre-call value
  (`-1`) across the syscall for the first reap.  Fix: declare
  `"+m"(*status)` in the asm.  The kernel was correct; the test was
  miscompiled.  See "Syscall ABI" above for the general rule.

- **`sys_read` fd 0 blocked until `count` bytes accumulated**
  (resolved 2026-09-26, tag `20260926A`).  POSIX `read(2)` on a
  terminal returns on the first available byte; donix's `sys_read`
  looped until `bytes_read == count`.  Invisible under the newlib
  shell (which reads 1 byte at a time, so `count == 1` and the loop
  exited on the first byte anyway), but a musl program doing
  `read(0, buf, 255)` would block until 255 keystrokes had been
  entered.  Discovered when `musl_sh`'s prompt appeared and then
  ignored all input: the shell's `read(0, line, 255)` was consuming
  each keystroke into its buffer but never returning to `main`.
  Confirmed by typing 256 characters into the frozen shell and
  finally seeing `EXEC-FAILED`.

  Fix: in `sys_read`'s fd-0 branch, the `continue` after a successful
  `safe_copy_to_user` becomes `break`.  One token changed.  This is
  the same class of bug as the `%r10` clobber: kernel ABI did not
  match Linux's, and newlib's usage never exercised the difference.

- **`musl_r10probe` `puthex64` stack buffer overflow** (resolved
  2026-09-26, tag `20260926D`).  `char b[24]` filled with a 29-byte
  string (`"R10-AFTER=0x"` prefix + 16 hex digits + newline).  Latent
  since `20260924L`; the compiler warned about it at build time but
  the test passed anyway because the overflow landed in unused stack
  space under the newlib-spawn path.

  Exposed when the test was first run under `musl_sh` via `execve`:
  different stack layout, and the overflow clobbered a live pointer.
  Symptom was a fatal `#PF` at `CR2=0x28`, `RIP=0x40097B`, with
  `PTE PRESENT, phys 0x28`.

  Fix: `char b[24]` â†’ `char b[32]` at the `musl_r10probe` `puthex64`
  in `build_musl_tests.sh`.  One line.

  **NOTE: this fix did not, on its own, make the test reliable.**
  The page fault that first exposed this overflow turned out to be
  a *separate* kernel bug â€” the `MSR_FS_BASE` issue fixed at
  `20260926E`.  The `b[32]` fix only removed one of the ways the
  symptom could manifest.

- **Second `execve` from the same `musl_sh` faulted fatally**
  (resolved 2026-09-26, tag `20260926E`).  The previous session's
  diagnosis â€” page-table corruption in `vmm_clone_page_table` or
  in `sys_execve`'s teardown â€” was **wrong**.  Reading
  `vmm_clone_page_table`, `vmm_free_user_page_tables`, and
  `sys_execve`'s `exec_free_and_unmap_user_pages` side by side
  showed that the teardown frees only data pages listed in
  `pcb->elf_page_list`, never page-table structures, and the child's
  `elf_page_list` is populated only with its own fresh stack pages
  by `process_create`.  No shared page-table page is freed.  The
  "freed page-table page" reading of `PTE = 0x3` was also wrong:
  `0x3` is the identity-map PTE for the low 2 MB, present but
  supervisor-only, and `CR2 = 0x34` is a near-null user dereference.

  The actual bug: `MSR_FS_BASE` (0xC0000100) is set once by
  `arch_prctl(ARCH_SET_FS)` and was never saved or restored across
  context switches, making it effectively per-CPU instead of
  per-process.  After the first child ran `arch_prctl` with its own
  TLS base, the parent `musl_sh`'s TLS base was clobbered.  On the
  parent's next `fork`, musl's `fork` wrapper loaded `errno`
  through `%fs`, got a null/stale pointer, and faulted at
  `CR2 = 0x34`, `RIP = 0x4009BD` (the `mov (%rax), %eax` after
  `__errno_location`; disassembly confirmed `0x4009BD` is that
  exact instruction).

  Fix, part 1: add `uint64_t fs_base;` to `pcb_t` (appended after
  `file_table`, past `block_kind = 0x158`, so `context_switch.asm`'s
  hardcoded offsets are unchanged), record it in `sys_arch_set_fs`
  and `sys_arch_prctl(ARCH_SET_FS)`, and save/restore around every
  context switch: the preemptive CR3 switch in
  `timer_preempt_handler` (which does not go through
  `context_switch`) and all three `context_switch` call sites in
  `scheduler.c`.

  Fix, part 2: the first fix unmasked a second bug.  `sys_fork` did
  not inherit the parent's `fs_base`, so the child ran with
  `MSR_FS_BASE = 0`.  musl's `__post_Fork` reads `%fs:0x0` on the
  child's first instruction after `_Fork` returns â€” before any
  syscall â€” and faulted at `CR2 = 0`, `RIP = 0x4014D5`.  Disassembly
  confirmed `0x4014D5` is `mov %fs:0x0, %rdx` inside `__get_tp`,
  called from `__post_Fork`.  Adding
  `child->fs_base = parent->fs_base;` in `sys_fork` closed it.

  Both parts landed in the same commit because they were discovered
  together: part 1 is not correct on its own (it introduces a new
  failure mode on the child's first instruction), and part 2 is only
  meaningful once `fs_base` exists.

  Files changed: `04_kernel_64bit/include/process.h` (new field),
  `user_syscall.c` (record at `arch_prctl`, inherit in `sys_fork`),
  `interrupts.c` (save/restore in `timer_preempt_handler`),
  `scheduler.c` (save/restore at the three `context_switch` call
  sites).  `context_switch.asm` was not touched.  The newlib canary
  never calls `arch_prctl`, so its `fs_base` stays 0 and every
  `wrmsr(0xC0000100, 0)` on switch is a harmless no-op.

### Open issues

- **`isr14_handler` halts on user-mode faults.**  The `#PF` handler
  checks only `g_expect_fault`; it does not look at `error_code & 4`
  to distinguish a user-mode fault from a kernel-mode one.  Any
  unexpected user-mode fault kills the console instead of
  terminating the faulting process.  Not blocking A3, but it will
  bite the next time a user program faults unexpectedly.  Fix: in
  `isr14_handler`, if `(error_code & 4)` and
  `g_expect_fault != 0x0E`, call `sys_exit(-1)` for the faulting
  process instead of halting.  Deferred until after A3's
  default-shell work.

- **`Unknown syscall: N` at `user_syscall.c:2044` is present but
  currently does not fire.**  The earlier handoff text claimed it
  was "noisy in every musl test"; that was written against an
  earlier tree where `exit_group` (231), `getrandom` (318), `rseq`
  (334), `set_robust_list` (273), etc. were still unimplemented.
  They have since been added, and the `20260926` canary does not
  trip the print.  The print is reachable (any syscall number not
  in the dispatch table hits `default:`) but nothing current calls
  such a number.  Gate it if it ever starts firing; currently it
  does not.

- If the "prompt returns, keyboard dead" symptom reappears, check
  whether the cause is the `20260924F` idle-fallback race.  The
  second-`execve` bug that previously shared this symptom space is
  now resolved (see `20260926E` above).

### Cosmetic / housekeeping

- `syscall.c:29` has an old stub `sys_brk` that shadows the real one in
  `user_syscall.c`. `syscall.o` is **not** in `OBJS` in the kernel
  Makefile, so `syscall.c` is not linked at all. Not fatal, but
  confusing. Clean up later (delete the file in its own commit,
  after A3's default-shell work).
- `build_musl_tests.sh` has duplicate `# Test 4:` and `# Test 7:`
  comments (copy-paste artifacts from `musl_twommap` and the
  `brkraw`/`brkgrow` tests).  Cosmetic.
- `musl_min` is built without `-no-pie` while every other musl test
  uses it.  `musl_min` works, but for consistency at some point it
  should match.  Cosmetic.
- Shell line-editing has a backspace echo bug. Not on critical path.
- `sys_spawn` still prints its serial trace with the prefix
  `sys_execve:` (a string literal, not a symbol).  Cosmetic; rename
  the literal to `sys_spawn:` at the next convenient edit.
- `vmm_clone_kernel_half` and `vmm_free_user_page_tables` in `vmm.c`
  / `vmm.h` are unused (written for the abandoned copy-then-swap
  `execve`).  Kept for future copy-on-write work.  If still unused
  after A3, delete in a cleanup commit.
- `DEBUG_WRITE_BOUNCE` in `user_syscall.c` is currently `0` (gated,
  inert).  It was added at `20260924L` to test a shared-bounce-buffer
  theory for the `printnum` NUL bytes; the theory was wrong, the flag
  never fired.  Keep it gated for now as a diagnostic in case the
  symptom reappears; delete in a cleanup commit if still unused.
- `musl_sh` does not tokenize its command line (the whole line is
  passed as `argv[0]` and as the `execve` path) and does not do the
  newlib shell's `0:/` + `.ELF` normalization.  Both are A3 work
  items, not cleanup.

## Testing harness

- **Kernel shell:** `k` at boot prompt
  (`proclist`, `schstat`, `heapstat`, `selftest`, `fatls`).
- **User shell:** default â€” don't press `k`. Runs `0:/NAME.ELF`.
- **QEMU:** `make runkernel64-kvm-single` (fast),
  `make runkernel64-single` (TCG), `make logkernel64` (debug).
  The normal `./run` script uses
  `make clean && make FAT_CONFIG=single && make -C
  05_boot_kernel64 hdd-single.img && make -C 05_boot_kernel64
  run-single > capture.txt` â€” single-drive, TCG, clean rebuild
  every time, QEMU output teed to `capture.txt`.  It does **not**
  set the `-d in_asm,cpu` flag; that only appears in the
  `run-debug-log` target.
- **Serial:** `-serial stdio` for kernel log. VGA to the QEMU window.
- **musl test binaries** are built by `build_musl_tests.sh` (tracked,
  committed at `57a3f9e`, extended at `20260924I` with `musl_exec`,
  `20260924J` with `musl_wait`, `20260924K` with `musl_readdir`,
  `20260924L` with `musl_r10probe`, `20260926B`/`20260926C` with
  `musl_sh`, `20260926D` with the `musl_r10probe` buffer fix).
  Sources are heredoc'd into `/tmp/` and linked with `musl-gcc
  -static -no-pie -O2 -mcmodel=large`.  The image build
  (`05_boot_kernel64/Makefile`) copies `/tmp/musl_*` to
  `::/MUSL_*.ELF` on the FAT via `mcopy_one`.
  **`/tmp` is not persistent across reboots** on Fedora.  If the
  `MUSL_*.ELF` files are missing from a boot, run
  `./build_musl_tests.sh` first.  A missing `/tmp/musl_*` produces
  a `WARN: ... not found` line during `hdd-single.img` build but
  does **not** fail the build â€” the image is silently missing those
  ELFs.

## Recovery

dons-os `dev` is the recovery point. donix is a clone; if it goes bad,
`git restore .` or re-clone from dons-os. Commit after every successful
milestone. One change at a time so `git restore .` always works.

## Summary for any session

1. Confirm donix baseline works: boot, `hello`, `memtest`, `ls`, `cat`,
   `echo`, `printf`, `malloc`.
2. Apply A1 only: four files, numbers only. No `crt0.S`, no linker
   script, no semantics, no new syscalls.
3. Test. Commit if it works. Revert and diagnose if it doesn't.
4. Then A2 items 1â€“14, one at a time, each tested with a tiny musl
   program. Newlib's existing userland is the regression canary after
   each.
5. Do not add `fork`/`execve`/`getdents64` wrappers to the newlib
   userland. That work will be discarded when musl arrives.
6. Produce **small targeted patches, not full files.** Ask to see
   current file content before patching. Ask for test results before
   the next patch.
7. If the assistant suggests changes to `crt0.S`,
   `user_newlib_linker.ld`, or `reent.c` during A1, push back: out of
   scope.
8. If a test fails, `git restore .` and diagnose. **Do not proceed
   with a broken milestone.**
9. Commit with explicit `git add <file>...`, not `git add -A`.  Verify
   `git diff --cached --stat` before committing.

## The one-line summary

**Use newlib only as a regression canary for existing userland binaries.
Add new syscalls to the kernel and test them with musl from the start.
Never write new libc wrappers in newlib that you'll throw away when
musl lands.**

---

# Part 2 â€” Session Status

**Last updated:** 2026-09-26 (session 7)
**Current HEAD:** `20260926E`
**Last known-good tag:** `20260926E`
**Disaster preserved at:** branch `disaster-20260923A` (commit `47262a9`)

## Current milestone

**A3 is unblocked.  `musl_sh` runs commands repeatedly from a fresh
boot.**  The blocker was a kernel bug: `MSR_FS_BASE` was not part of
the process context.  Fixed at `20260926E`.

Today's commits, in order, one change each:

| Tag | Commit | What |
|-----|--------|------|
| `20260926A` | `sys_read: fd 0 returns on first byte, not on count (POSIX short read)` | Kernel fix. `continue` â†’ `break` in `sys_read`'s fd-0 branch. Unblocks musl's `read(0, buf, N)` for N > 1. |
| `20260926B` | `A3: add minimal musl shell, wired into image build` | `musl_sh` heredoc + build block in `build_musl_tests.sh`; `MUSL_SH := /tmp/musl_sh` + `mcopy_one` in `05_boot_kernel64/Makefile`. |
| `20260926C` | `A3: musl_sh reads a line byte-at-a-time, echoes as typed` | `musl_sh`'s read loop changed from one big `read` to a nested byte-at-a-time loop that echoes and breaks on `\n`. |
| `20260926D` | `musl_r10probe: fix stack buffer overflow in puthex64 (b[24] -> b[32])` | One-line test fix. Did *not* resolve the fault on its own. |
| `20260926E` | `Save/restore MSR_FS_BASE per process across context switches` | Adds `fs_base` to `pcb_t`, records it at `arch_prctl`, inherits it in `sys_fork`, save/restore at every context-switch site. Fixes the second-`execve` fatal `#PF`. |

The `sys_read` fix (`20260926A`) was the actual reason `musl_sh`
appeared to ignore input when first tested.  The `b[32]` fix
(`20260926D`) removed a genuine buffer overflow but did not
eliminate the page fault.  The `MSR_FS_BASE` fix (`20260926E`) is
what actually closed the A3 blocker; the previous session's
page-table-corruption diagnosis was wrong.

## Canary state (all green as of `20260926E`)

| Test | State | Notes |
|------|-------|-------|
| hello | green | |
| ls | green | lists 23 files |
| cat | green | reads HELLO-WORLD.TXT |
| echo | green | |
| memtest (newlib) | green | `[memtest] PASS` |
| musl_min | green | prints `MUSL-START` |
| musl_malloc | green | `MALLOC-OK` and `SMALL-OK` |
| musl_fork | green | prints `A`, `P`, `C` |
| musl_fork_raw | green | prints `A`, `P`, `C` |
| musl_exec | green | `EXEC-PARENT-START`, child execve's `MUSL_MIN.ELF` in place (pid preserved), `MUSL-START`, `EXEC-PARENT-DONE` |
| musl_wait | green | `WAIT-STATUS-OK 42`, `WAIT-WNOHANG-OK`, `WAIT-ANY-1 s=11`, `WAIT-ANY-2 s=22`, `WAIT-ALL-OK` |
| musl_readdir | green | 23 entries, `READDIR-DONE count=23` |
| brk_verify | green | `p=0x8000200000`, `VERIFY-OK` |
| brkraw | green | `FS=`, `BRK0=`, `BRKN=`, `WANT=` correct |
| brkgrow | green | `start=`, `64K got=`, `1M got=` correct |
| musl_printf | green | prints `MUSL-PRINTF` with newline, exits cleanly |
| printnum | green | prints `x=42` with newline, exits cleanly |
| musl_r10probe | green (multi-run) | `R10-AFTER=0xdeadbeefcafebabe`.  Multiple consecutive runs in the same boot are now safe. |
| **musl_sh** | **green (multi-command)** | Two consecutive `0:/musl_r10probe.elf` runs both clean, plus `0:/hello.elf`, `0:/ls.elf`, `0:/memtest.elf`.  Bare `hello` (no prefix) fails with `f_open FR_NO_FILE` â€” path normalization is the next A3 item, not a bug.  `0:/cat.elf file.txt` also fails because `musl_sh` does not tokenize; argument splitting is a separate follow-up. |

## Next step (exactly this, then stop)

**A3 continues: tokenize `musl_sh`'s command line and pass the
token array to `execve`.**

Currently `musl_sh` passes the entire typed line as `argv[0]` and
as the `execve` path:

```c
argv[0] = line;
argv[1] = (char*)0;
execve(line, argv, (char**)0);
```

So `0:/cat.elf 0:/HELLO-WORLD.TXT` is passed as a single path
string, `f_open` rejects it (`FR_NO_FILE` or `FR_INVALID_NAME`),
and the shell prints `EXEC-FAILED`.  The kernel's argv pipeline
is already correct â€” `sys_execve` snapshots argv from the old
address space and lays it out on the new stack, and `cat.elf`
already reads `argc`/`argv` from the frame.  The shell is the
missing piece.

The change is one file: `build_musl_tests.sh`'s `musl_sh` heredoc.
Add a whitespace tokenizer that fills `char* argv[MAX_ARGS]` with
pointers into `line`, then call `execve(argv[0], argv, (char**)0)`.
Test with `0:/cat.elf 0:/HELLO-WORLD.TXT` (expect the file
contents printed).

**Then, as a separate change:** path normalization.  For each
token that has no `:/`, prepend `0:/` and append `.ELF` if the
token has no `.` extension.  Test with bare `hello` (no prefix).

**Do not combine the two changes.**  Tokenization first, test,
commit.  Then normalization, test, commit.  If both land in one
commit and something breaks, you will not know which caused it.

**After both are green:** make `musl_sh` the default boot shell,
keeping the newlib shell reachable as a fallback binary.

## State on disk

- `/tmp/musl_*` â€” musl test binaries, rebuilt by
  `build_musl_tests.sh`.  Present as of this session.
  **`/tmp` is not persistent across reboots on Fedora.**  If the
  `MUSL_*.ELF` files are missing from a boot, run
  `./build_musl_tests.sh` first.  The image build does **not** fail
  if they are missing â€” it prints `WARN: ... not found` and
  proceeds, producing an image without those ELFs.  If a musl test
  reports "file not found," check `/tmp` before assuming a kernel
  regression.
- `/tmp/20260923A-working-tree.patch` (614 lines) â€” plain-text backup
  of abandoned work from the disaster commit.  Can be deleted.
- `/tmp/memtest2.c.bak` (524 bytes) â€” backup of an untracked test.
- `~/code/x` â€” snapshot of the pre-cleanup tree, kept for diffing.
  Can be deleted; the halt it was being kept for is now resolved.
- `vmm_clone_kernel_half` and `vmm_free_user_page_tables` in
  `vmm.c` / `vmm.h` are currently unused (written for the abandoned
  copy-then-swap `execve`).  Kept for future copy-on-write work.
- `DEBUG_WRITE_BOUNCE` in `user_syscall.c` is `0` (gated, inert).
  Safe to delete; kept for one more session in case the `printnum`
  NUL symptom reappears.

## Open items

- **A3 continues.**
  - Tokenize `musl_sh`'s command line so `cat.elf file.txt` passes
    `argv[1]` through to the child.  Currently the whole line is
    treated as one path.
  - Add `0:/` + `.ELF` normalization so bare `hello` resolves to
    `0:/HELLO.ELF`, matching the newlib shell's behavior.
  - Make `musl_sh` the default boot shell, keeping the newlib shell
    reachable as a fallback binary.
- **Deferred cleanups** (one commit each, after A3's default-shell
  work):
  - Fix `isr14_handler` to distinguish user-mode faults (see "Open
    issues" in Part 1): user faults should terminate the faulting
    process, not halt the kernel.
  - Delete `syscall.c` (dead â€” `syscall.o` is not in `OBJS`; it
    holds a stub `sys_brk` that shadows the real one).
  - Silence `syscall_dispatch`'s `Unknown syscall: N` print if it
    ever starts firing.
  - Rename `sys_spawn`'s serial trace prefix from `sys_execve:` to
    `sys_spawn:` (string literal, cosmetic).
  - Delete `DEBUG_WRITE_BOUNCE` from `user_syscall.c`.
  - Audit the other `puthex`/`put_dec` helpers in
    `build_musl_tests.sh` for similar tight margins.  They fit as
    of this session, but the margins are small.
  - Duplicate `# Test N:` comments and `musl_min`'s missing
    `-no-pie` in `build_musl_tests.sh`.
- Cosmetic items remain (see "Cosmetic / housekeeping" in Part 1).

## How to use this file

At the start of a new session, paste the entire file and say:

> "Continue from here. What's the next step?"

At the end of a session, update **Part 2** (session status) with the
current tag, canary state, and next step. Part 1 (strategy) only
changes if the plan itself changes.
