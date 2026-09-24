# donix — Handoff

This file is both the project strategy and the current session status.
Read it top to bottom when starting a new session. Update the "Session
Status" section at the end of every session.

---

# Part 1 — Project Strategy

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

### Phase A — Linux syscall ABI, then static musl binaries

Make the kernel speak Linux x86_64 syscalls and run a static musl
binary. Newlib's existing userland stays as a **passive regression
canary**: after every kernel change, `hello` / `memtest` / `ls` / `cat` /
`echo` / `printf` / `malloc` must still work.

Do **not** add new features to the newlib userland — no `fork`, no
`execve`, no `getdents64` wrappers. That work would be thrown away when
musl arrives.

### Phase B — busybox / coreutils against musl

Static-link busybox against musl and try running it.

## The one critical rule

**One change at a time. Test. Commit. Revert on failure.**

The previous session failed by changing six files at once; each symptom
had a different cause. Never apply two changes without testing between
them.

## Files that must not be touched

Unless a specific tested problem requires it:

- `process.c` — especially `process_copy_kernel_frame`
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

### A1 — pure syscall renumbering

Change syscall numbers to Linux x86_64. **No semantics change. No new
syscalls.**

Files: `include/syscall.h`, `user_syscall_entry.asm` (the single
`cmp rbx, 2 → cmp rbx, 60` reference), `user_syscall.c` (`case N:`
labels only), `arc2/syscalls.c` (`#define SYS_*` numbers only). Four
files, mechanical.

**Test:** `hello`, `memtest`, `ls`, `cat`, `echo`, `printf`, `malloc`
— all still work.

**Status: complete at tag `20260922H`.**

### A2 — add the syscalls musl needs, one at a time

Add each to the **kernel** and test with a **tiny static musl binary**.
Newlib's existing userland is the regression canary after each — but do
not add new wrappers to it.

Order (musl's `__libc_start_main` calls these in sequence; each blocks
the next):

1. `arch_prctl(ARCH_SET_FS)` = 158 — **complete**
2. `set_tid_address` = 218 — **complete**
3. `rt_sigaction` = 13 (stub, return 0) — **complete**
4. `rt_sigprocmask` = 14 (return 0) — **complete**
5. `set_robust_list` = 273 (return 0) — **complete**
6. `ioctl` = 16 (`TCGETS` for stdio, `-ENOTTY` otherwise) — **complete**
7. `brk` = 12 — **complete at `20260924B`**, Linux absolute-address ABI
8. **Test:** minimal musl program reaches `main` and `printf` works —
   `musl_min` and `musl_printf` green (printf still red as a
   musl-internal issue, see "Resolved bugs")
9. `mmap` = 9, `munmap` = 11 — **complete at `20260924B`** (minimal
   anonymous-private implementation)
9.5. `mprotect` = 10 — **stub added at `20260924B`**, returns 0.
   Not on the original list; musl calls it after `mmap` and its
   absence caused a `#GP`.  Real permission changes not implemented.
10. `getrandom` = 318, `rseq` = 334 (stubs returning `-ENOSYS`) — complete
11. `fork` = 57 — **complete at `20260924D`**
12. `execve` = 59 — **step 1 complete at `20260924F`/`20260924H`;
    step 2 (real in-place execve) next**
13. `wait4` = 61
14. `getdents64` = 217

Each of the remaining items is its own milestone. **Commit after each.**
After each, newlib's `hello` / `memtest` / `ls` / `cat` / `echo` /
`printf` / `malloc` must still work.

### A3 — musl shell

Minimal shell in musl: `fork` + `execve` + `wait4`. Replaces the newlib
shell as default. Newlib shell stays as a fallback until musl's is
proven.

### A4 — migrate userland apps to musl

`hello`, `ls`, `cat`, `echo`, `memtest` — rebuild each against musl.
**One at a time.** Test each.

### A5 — retire newlib

Remove the newlib userland, build rules, and libraries. Safe because
nothing uses them.

## musl build

Clone `git://git.musl-libc.org/musl`, configure for `x86_64-linux-musl`
with the cross toolchain, `make install` → `libc.a`, `crt1.o`,
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
  callee-saved set (`%rbx`, `%rbp`, `%r12`–`%r15`) **plus `%r8` and
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

- **`execve` must update the resume frame's RIP (+0x78) and RSP (+0x90)**,
  not just the PCB.  The resume path reads the frame.

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

### Syscall ABI

- **`SYS_EXIT = 60` in `user_syscall_entry.asm`** — the only numeric
  syscall reference in assembly.
- **`-mcmodel=large` is required for userland** (linked above 4 GB).
  `-mcmodel=small`/`medium` don't work. `-no-pie` is not needed.
- **The syscall entry/return frame saves 16 slots, not 12.**  In
  addition to the callee-saved registers and the user RIP/RFLAGS/RSP,
  `user_syscall_entry.asm` now pushes `%rdi`, `%rsi`, `%rdx`, `%r10`
  before the argument shuffle.  The parent's return path restores
  `%rdi`, `%rsi`, `%rdx` and discards `%r10` (the exit path uses `%r10`
  as the user-RSP scratch).  This matches the Linux syscall ABI more
  closely: only `%rax`, `%rcx`, `%r11` are architecturally clobbered.
  Compilers *do* rely on `%rdi`, `%rsi`, `%rdx` surviving a syscall;
  musl's fork wrapper caches the TLS base in `%rdx` before the raw
  fork syscall and writes through it after the child resumes, and was
  faulting at CR2=0x98 without the restore.

  Frame layout, offsets from `parent->kernel_stack_top` (first push =
  offset `-8`):
  `-8=rbx, -16=rbp, -24=r12, -32=r13, -40=r14, -48=r15, -56=user RIP,
  -64=user RFLAGS, -72=user RSP, -80=r8, -88=r9, -96=rdi, -104=rsi,
  -112=rdx, -120=r10, -128=arg5 (discarded)`.

  `process_fork_copy_frame` reads from those offsets to build a fork
  child's frame.  If the assembly push order changes, that function's
  offsets must change with it.

- **`SYS_DONIX_SPAWN` (507) is the newlib spawn number.**  The newlib
  userland calls 507 for spawn semantics (`arc2/syscalls.c:spawn`).
  Number 59 is reserved for the real Linux `execve` (A2.12 step 2).
  In the current tree, 59 still dispatches to `sys_spawn` as a
  placeholder — that is intentional and documented in
  `include/syscall.h`.  Do not point newlib at 59.

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

  A secondary symptom — "shell prompt returns but keyboard goes
  dead" — was reported but never reproduced.  It may be the same
  race with a different interleaving.  If it reappears, reopen.

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

- **`musl_printf` / `printnum` dump garbage — musl-internal, not
  kernel** (investigated 2026-09-24, tag `20260924C`).  `printf` of a
  literal or a `%d` value on musl/donix prints the correct bytes and
  then dumps large chunks of the binary's own `.rodata` / `.eh_frame`.
  The newline from `printf("MUSL-PRINTF\n")` is silently dropped.

  Diagnosis: a raw-byte dump of the user's iov array in `sys_writev`
  showed that musl itself writes a garbage `iov[1]`:

      [writev raw] 28 42 40 00 00 00 00 00   iov[0].base = 0x404228
                   0b 00 00 00 00 00 00 00   iov[0].len  = 11
                   57 ff 0f 00 80 00 00 00   iov[1].base = 0x80000FFF57
                   20 ff 0f 00 80 00 00 00   iov[1].len  = 0x80000FFF20

  The kernel faithfully copies what musl wrote; the corruption is in
  musl's `__stdio_write` stack frame on donix.  Two user-stack
  addresses appear where `iov[1].base` / `iov[1].len` should be.

  This is a userland porting problem (a musl assumption about the
  runtime or ABI that donix does not satisfy), not a syscall-layer
  problem.  Every kernel canary is green: `hello`, `memtest`, `ls`,
  `cat`, `echo`, `musl_min`, `musl_malloc`.

  **Must be resolved before Phase A3 (musl shell).**  Planned as the
  first task after `execve`/`wait4` are in, because a working `execve`
  will let us run more musl test programs and narrow the cause.  Do
  **not** attempt to fix this from the kernel side; the kernel is
  behaving correctly.

### Open issues

- **None blocking.**  The intermittent halt was resolved at
  `20260924F`.  See "Resolved bugs."  If the "prompt returns, keyboard
  dead" symptom reappears, reopen it here.

### Cosmetic / housekeeping

- `syscall.c:29` has an old stub `sys_brk` that shadows the real one in
  `user_syscall.c`. Not fatal, but confusing. Clean up later.
- Shell line-editing has a backspace echo bug. Not on critical path.
- `sys_spawn` still prints its serial trace with the prefix
  `sys_execve:` (a string literal, not a symbol).  Cosmetic; rename
  the literal to `sys_spawn:` at the next convenient edit.

## Testing harness

- **Kernel shell:** `k` at boot prompt
  (`proclist`, `schstat`, `heapstat`, `selftest`, `fatls`).
- **User shell:** default — don't press `k`. Runs `0:/NAME.ELF`.
- **QEMU:** `make runkernel64-kvm-single` (fast),
  `make runkernel64-single` (TCG), `make logkernel64` (debug).
- **Serial:** `-serial stdio` for kernel log. VGA to the QEMU window.

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
4. Then A2 items 1–14, one at a time, each tested with a tiny musl
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

# Part 2 — Session Status

**Last updated:** 2026-09-24 (evening, session 3)
**Current tag / HEAD:** `20260924H`
**Last known-good tag:** `20260924H`
**Disaster preserved at:** branch `disaster-20260923A` (commit `47262a9`)

## Current milestone

**A2 item 12 — `execve` = 59.  Step 1 complete; step 2 next.**

Step 1 moved spawn semantics off number 59 and onto a donix-private
number so that 59 is free for the real Linux `execve`.  The rename
landed in `20260924F` (bundled with the halt fix — see "Git hygiene"
in Part 1), and the header documentation for 59 was updated in
`20260924H`.  `SYS_DONIX_SPAWN` is 507; `arc2/syscalls.c:spawn` calls
507; the kernel dispatcher still routes 59 to `sys_spawn` as a
placeholder.

Step 2 replaces the placeholder with real in-place `execve`
semantics: the calling process's address space is replaced, no new
process is created, the resume frame's user RIP and RSP are rewritten
to point at the new program, and the syscall returns 0 into the new
program's entry.

### Plan for step 2

1. Paste `04_kernel_64bit/user_syscall_entry.asm` first — the resume
   frame offsets in `process_fork_copy_frame`'s comment (`-56` user
   RIP, `-72` user RSP) must be verified against the actual push order
   in the assembly before writing any code.
2. Design the in-place `execve`:
   - Free the current process's old ELF pages and old user stack.
   - Load the new ELF into the **same** CR3 (`current->cr3`).
   - Write argv into the current process's user stack.
   - Rewrite the current process's syscall-entry frame so the return
     path resumes at the new program's entry with the new stack.
3. The syscall must run with interrupts disabled (`cli`/`sti`), like
   `sys_fork` does.
4. Test with a musl `fork` + `execve` program; the newlib shell's
   `spawn` path is unaffected because it now calls 507.

## Canary state (green as of `20260924H`)

| Test | State | Notes |
|------|-------|-------|
| hello | green | |
| ls | green | lists 18 files |
| cat | green | reads HELLO-WORLD.TXT |
| echo | green | |
| memtest (newlib) | green | `[memtest] PASS` |
| musl_min | green | prints `MUSL-START` |
| musl_malloc | green | `MALLOC-OK` and `SMALL-OK` |
| musl_fork | green | prints `A`, `P`, `C` |
| musl_fork_raw | green | prints `A`, `P`, `C` |
| musl_printf | red | musl-internal.  `printf("literal\n")` drops the newline; a garbage `iov[1]` appears in musl's `__stdio_write`.  Not a syscall bug. |
| printnum | red | same root cause as `musl_printf`. |
| brkraw | red | test binary needs re-run against the current kernel; `sys_brk` is now Linux-ABI |
| brkgrow | red | same |
| brk_verify | red | same |

## Next step (exactly this, then stop)

**A2 item 12 step 2 — real in-place `execve`.**

1. Confirm working tree clean: `git status` shows nothing modified,
   and HEAD is `20260924H` (or later if the handoff commit has been
   made on top).
2. Paste `handoff.md` plus these files:
   - `04_kernel_64bit/user_syscall_entry.asm`
   - `04_kernel_64bit/process.c`
   - `04_kernel_64bit/include/process.h`
   - `04_kernel_64bit/user_syscall.c` (the current `sys_spawn` handler
     and dispatcher)
   - `04_kernel_64bit/elf.c`
3. Say: "Continue from here.  Implement A2 item 12 step 2: the real
   Linux `execve`.  The current `case SYS_EXECVE` in `user_syscall.c`
   routes to `sys_spawn`, which creates a new process.  Replace it
   with an in-place `execve` that replaces the calling process's
   address space: free the old ELF pages and user stack, load the new
   ELF into `current->cr3`, write argv onto the current user stack,
   rewrite the current process's resume frame (user RIP and user RSP)
   so the syscall return path resumes at the new program's entry, and
   return 0.  The frame offsets in `process_fork_copy_frame` claim
   `kernel_stack_top - 56` is user RIP and `- 72` is user RSP — verify
   those against `user_syscall_entry.asm` before writing code."

`musl_printf` / `printnum` remain deferred — see "Resolved bugs" in
Part 1.  Do not spend time on them in this session.

## State on disk

- `/tmp/20260923A-working-tree.patch` (614 lines) — plain-text backup
  of abandoned work from the disaster commit.  Can be deleted.
- `/tmp/memtest2.c.bak` (524 bytes) — backup of an untracked test.
- `/tmp/musl_*` — musl test binaries, rebuilt by
  `build_musl_tests.sh` (tracked in the repo, committed at `57a3f9e`).
- `~/code/x` — snapshot of the pre-cleanup tree, kept for diffing.
  Can be deleted; the halt it was being kept for is now resolved.

## Open items

- **None blocking.**  Step 2 of `execve` is the next milestone.
- Cosmetic items remain (see "Cosmetic / housekeeping" in Part 1).

## How to use this file

At the start of a new session, paste the entire file and say:

> "Continue from here. What's the next step?"

At the end of a session, update **Part 2** (session status) with the
current tag, canary state, and next step. Part 1 (strategy) only
changes if the plan itself changes.
