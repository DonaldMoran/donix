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
12. `execve` = 59 — **next**
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

### Syscall ABI

- **`SYS_EXIT = 60` in `user_syscall_entry.asm`** — the only numeric
  syscall reference in assembly.
- **`-mcmodel=large` is required for userland** (linked above 4 GB).
  `-mcmodel=small`/`medium` don't work. `-no-pie` is not needed.

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

### Resolved bugs (kept for the record)

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
  first task after `fork`/`execve`/`wait4` are in, because a working
  `execve` will let us run more musl test programs and narrow the
  cause.  Do **not** attempt to fix this from the kernel side; the
  kernel is behaving correctly.

### Open issues

- **Intermittent halt after a long command sequence with forking
  programs** (found 2026-09-24, tag `20260924D`).  After running a
  sequence like `hello`, `memtest`, `ls`, `cat`, `echo`, `musl_min`,
  `musl_malloc`, `musl_fork_raw`, the kernel *sometimes* halts with
  `process_exit: no runnable process, halting`.  The same sequence on
  a fresh boot does not always halt — the bug is nondeterministic.

  The halt happens when a forked child exits while the shell is
  blocked in `sys_read` (keyboard block).  `process_exit`'s fallback
  path finds the ready queue empty (the shell is BLOCKED on read, not
  on the queue) and, because the exiting process is a user process,
  takes the halt path instead of idling on pid 1.

  The halt did not appear before we removed the diagnostic prints
  from `scheduler.c` and `interrupts.c`.  Those prints included
  `serial_lock` / `serial_unlock` (cli/sti) windows of ~5–15 ms each,
  which changed the interleaving of the child's exit and the shell's
  wake.  So this is a timing race that the prints were masking, not a
  semantic change from the print removal.

  Diagnostic prints have been left in `scheduler.c` at the top of
  `process_exit` (`EXIT: pid=... state=... parent=... qhead=...`) and
  before the halt message (`HALT: exiting pid=... name=...
  entry=... parent=...`).  The next time the halt fires, capture the
  log — those two lines will show which process exited and what the
  queue looked like.

  Likely fix: in `process_exit`'s fallback path, if the ready queue
  is empty and the exiting process is a user process, pick the idle
  process (pid 1) and switch to it instead of halting.  Idle is
  BLOCKED on `hlt` and will be woken by the next timer tick; the
  shell will be woken by the next keyboard IRQ.  This has not been
  tested — verify with a reproduction first.

### Cosmetic / housekeeping

- `syscall.c:29` has an old stub `sys_brk` that shadows the real one in
  `user_syscall.c`. Not fatal, but confusing. Clean up later.
- Shell line-editing has a backspace echo bug. Not on critical path.

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

## The one-line summary

**Use newlib only as a regression canary for existing userland binaries.
Add new syscalls to the kernel and test them with musl from the start.
Never write new libc wrappers in newlib that you'll throw away when
musl lands.**

---

# Part 2 — Session Status

**Last updated:** 2026-09-24 (evening, session 2)
**Current tag / HEAD:** `20260924D`
**Last known-good tag:** `20260924C`
**Disaster preserved at:** branch `disaster-20260923A` (commit `47262a9`)

## Current milestone

**A2 item 11 — `fork` = 57 — complete at `20260924D`.**  The fork work
is done: `sys_fork` creates a child that resumes at the parent's user
RIP with `%rax = 0`, and the child can run, print, and exit cleanly.
Verified with `musl_fork_raw` (prints `A`, `P`, `C`).

The fix that got it working was in `process_fork_copy_frame`:
preserve `%r8` and `%r9` from the parent's syscall-entry frame in
addition to the callee-saved set.  An earlier version zeroed `%r8`,
which broke the child because the compiler-generated code after the
`syscall` in `musl_fork_raw`'s `main` was `mov %r8, %rax` (setting up
the `write` fd argument), and `%rax = 0` made the child call `read`.

Three scheduler fixes were also needed and are now in:
- `scheduler_ready_queue_add` is idempotent.
- `process_wake_all_blocked` skips processes already on the queue.
- `process_yield` removes a BLOCKED process from the ready queue.

## Canary state (green as of `20260924D`)

| Test | State | Notes |
|------|-------|-------|
| hello | green | |
| ls | green | lists 18 files |
| cat | green | reads HELLO-WORLD.TXT |
| echo | green | |
| memtest (newlib) | green | `[memtest] PASS` |
| musl_min | green | prints `MUSL-START` |
| musl_malloc | green | `MALLOC-OK` and `SMALL-OK` |
| musl_fork_raw | green | prints `A`, `P`, `C` |
| musl_printf | red | musl-internal.  `printf("literal\n")` drops the newline; a garbage `iov[1]` appears in musl's `__stdio_write`.  Not a syscall bug. |
| printnum | red | same root cause as `musl_printf`. |
| brkraw | red | test binary needs re-run against the current kernel; `sys_brk` is now Linux-ABI |
| brkgrow | red | same |
| brk_verify | red | same |

## Next step (exactly this, then stop)

**Two paths, decide at the start of the session:**

### Path A — fix the intermittent halt first

1. Confirm the reproduction: boot fresh, run
   `hello; memtest; ls; cat hello-world.txt; echo hi; musl_min;
   musl_malloc; musl_fork_raw` in sequence.  Repeat the sequence a
   few times if the halt does not fire on the first try.  The two
   diagnostics in `process_exit` (`EXIT:` and `HALT:`) will show
   which process exited and the queue state if the halt fires.
2. If reproducible, the likely fix is in `process_exit`'s fallback
   path: if the ready queue is empty and the exiting process is a
   user process, switch to the idle process (pid 1) instead of
   halting.  Idle will be woken by the next timer tick; the shell
   will be woken by the next keyboard IRQ.
3. Test: re-run the full command sequence several times.  No halt.
4. Then move to Path B.

### Path B — start A2 item 12 (`execve`)

1. Confirm working tree clean: `git status` shows nothing modified.
2. Start **A2 item 12 (`execve`)**.  Paste `handoff.md` plus these
   files:
   - `04_kernel_64bit/process.c`
   - `04_kernel_64bit/include/process.h`
   - `04_kernel_64bit/user_syscall.c` (the current `sys_execve` handler)
   - `04_kernel_64bit/elf.c`

   Say: "Continue from here.  Implement A2 item 12 (execve).  The
   current `sys_execve` is a spawn — it creates a new process and
   returns its pid.  Linux `execve` replaces the calling process's
   address space in place, without creating a new process.  Design
   the replacement semantics with the fork work in mind: the caller
   is a forked child, `sys_execve` should replace its CR3, load the
   new ELF into it, reset the resume frame's RIP and RSP, and return
   `0` from `sys_execve` to the caller's `main`."

3. `musl_printf` / `printnum` are deferred to Phase A3/A4 — see
   "Resolved bugs" in Part 1.  Do not spend time on them in this
   session.

## State on disk

- `/tmp/20260923A-working-tree.patch` (614 lines) — plain-text backup
  of abandoned work from the disaster commit.  Can be deleted.
- `/tmp/memtest2.c.bak` (524 bytes) — backup of an untracked test.
- `/tmp/musl_*` — musl test binaries, rebuilt by
  `build_musl_tests.sh` (now tracked in the repo).
- `build_musl_tests.sh` — committed at `57a3f9e`.
- `~/code/x` — snapshot of the pre-cleanup tree, kept for diffing.
  Can be deleted once the halt is resolved.

## Open items

- **Intermittent halt after a long command sequence with forking
  programs.**  See "Open issues" in Part 1 for details.  Two
  diagnostics left in `scheduler.c` to capture the state if it fires
  again.

Cosmetic items remain (see "Cosmetic / housekeeping" in Part 1).

## How to use this file

At the start of a new session, paste the entire file and say:

> "Continue from here. What's the next step?"

At the end of a session, update **Part 2** (session status) with the
current tag, canary state, and next step. Part 1 (strategy) only
changes if the plan itself changes.
