# donix — Handoff

This file is the project's **live state**: strategy, current milestone,
canary, open issues, and session log. Read it top to bottom when
starting a new session.

**Historical records live in `docs/`:**

- `docs/migration-history.md` — how donix got here (A1–A6, resolved
  bugs, musl build specifics).
- `docs/dons-os-history.md` — the pre-fork version-by-version story.
- `docs/CHECKLIST.md` — capability checklist, frozen at v0.6.0.
- `docs/MAINTENANCE.md` — known debt, frozen at v0.6.0.
- `docs/LLD_BUG_REPORT.md` — Clang/LLD toolchain bugs.

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

**Phase A is complete as of `v0.6.0`.**  The kernel speaks Linux
x86_64 syscalls, the shell is musl, the userland apps are musl,
newlib has been fully retired from the tree, and the musl userland
lives in a tracked source tree at `userland/musl/`.  Phase B is next.

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
- the prebuilt newlib `.a` files *(now removed — A5 step 7)*
- `arc2/crt0.S` *(now removed — A5 step 7)*
- `arc2/reent.c` *(now removed — A5 step 7)*
- `user_newlib_linker.ld` *(now removed — A5 step 7)*

All of these work in the baseline. The last four no longer exist;
they were removed with the newlib tree at A5 step 7.

## A6 — musl userland source tree (complete at `v0.6.0`)

The musl userland was moved out of the heredoc-based build script and
into a tracked source tree at `userland/musl/`. The C sources did not
change; only where they live and how they are built.

- `userland/musl/{apps,tests}/` hold the sources; `Makefile` builds
  each `.c` into `build/*.elf` with `-static -no-pie -O2 -mcmodel=large`.
- `05_boot_kernel64/Makefile` invokes `make -C ../userland/musl` before
  staging the FAT, and copies `../userland/musl/build/*.elf` onto it.
- No `/tmp` staging, no separate build script.
- `musl_min` built with `-no-pie` like every other binary.
- Residual newlib artifacts under `04_kernel_64bit/` removed in a
  separate cleanup commit.

For the full A1–A6 migration narrative, see
[`docs/migration-history.md`](docs/migration-history.md).

---

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

- **`fork` must inherit the parent's `fs_base`.**  `MSR_FS_BASE` is
  per-process, and musl's `__post_Fork` reads `%fs:0x0` on the child's
  first instruction after `_Fork` returns.  Without
  `child->fs_base = parent->fs_base;` in `sys_fork`, the child runs
  with `MSR_FS_BASE = 0` and faults at `CR2 = 0`.  See
  [`docs/migration-history.md`](docs/migration-history.md) Appendix C.

- **`execve` must update the syscall-entry frame's RIP and RSP**, not
  just the PCB.  The return path (`user_syscall_entry.asm`'s `sysret`)
  reads the frame at `[kernel_stack_top - 56]` (user RIP) and
  `[kernel_stack_top - 72]` (user RSP).  These are `ktop[-7]` and
  `ktop[-9]` in C.  The shipped `sys_execve` writes exactly these two
  slots.  If the assembly push order changes, these offsets must
  change with it.

- **`sys_execve` must be atomic** (`cli`/`sti`).  A previous session saw
  `#GP` when the timer fired mid-operation.

- **`process_create` bakes `entry_point` into the resume frame.**
  `elf_load_into_process` returns `e_entry` but does not update the
  frame; any caller that passes a different `entry_point` to
  `process_create` will resume at the wrong RIP.  The helper
  `load_elf_into_user_process` in `kmain.c` rewrites the frame's RIP
  slot at `[pcb->rsp + 0x78]` after a successful load.  All
  ELF-to-user-process call sites (`kmain`'s boot path and the
  debug-shell `elfload` command) go through it.  The latent bug was
  invisible while the only such binary was the embedded newlib shell,
  which is linked at exactly `0x8000000000` — the same value `kmain`
  passed as `entry_point`, so the stale frame slot was already correct
  by coincidence.  `musl_sh` is linked at `0x400000`; the coincidence
  no longer holds.

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
  or keyboard IRQ; the shell is then woken normally.  See
  [`docs/migration-history.md`](docs/migration-history.md) Appendix C.

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
  clobber case, `CR2 = 0` for the inheritance case).

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
  last, via `mov rsp, [rsp - 72]` after the other pops.**  See
  [`docs/migration-history.md`](docs/migration-history.md) Appendix C.

- **`sys_read` on fd 0 must return on the first available byte, not
  on `count`.**  POSIX `read(2)` on a terminal returns when at least
  one byte is available; it does not block until `count` bytes have
  accumulated.  The original donix `sys_read` looped until
  `bytes_read == count`.  That was invisible under newlib (whose
  shell reads 1 byte at a time, `count == 1`) but broke musl's
  `read(0, buf, 255)`: it waited for 255 keystrokes before returning.
  This is the same *class* of bug as the `%r10` clobber — the kernel's
  ABI did not match Linux's, and newlib's usage never exercised the
  difference.  Musl did.

- **`SYS_EXIT = 60` in `user_syscall_entry.asm`** — the only numeric
  syscall reference in assembly.
- **`-mcmodel=large` is required for userland** (linked above 4 GB).
  `-mcmodel=small`/`medium` don't work. `-no-pie` is not needed.
  *(Superseded by A6: the musl userland now links at `0x400000`
  (below 4 GB) and uses `-no-pie` uniformly.  `-mcmodel=large` is
  kept in the flags for consistency but is not load-bearing for
  musl binaries.)*
- **The syscall entry/return frame saves 16 slots, not 12.**  In
  addition to the callee-saved registers and the user RIP/RFLAGS/RSP,
  `user_syscall_entry.asm` pushes `%rdi`, `%rsi`, `%rdx`, `%r10`
  before the argument shuffle.  On the return path, **all four are
  restored** — `%r10` was previously discarded, which was wrong and
  broke musl's `printf`.  The user RSP is now loaded from the frame at
  the very end, using no GPR.

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

- **`sys_execve` argv layout: `envp` NULL needs its own slot.**  The
  frame layout is (low to high) `argv[0]..argv[argc-1]`, `argv NULL`,
  `envp NULL`, then the argument strings.  `array_bytes = (argc+1)*8`
  covers argv slots + argv NULL; the envp NULL goes at
  `argv_region_bottom + array_bytes`, and strings start at
  `argv_region_bottom + array_bytes + 8`.  The original code put
  `strings_start` at `argv_region_bottom + array_bytes` — the same
  address as the envp NULL write — so the envp NULL zeroed the first
  8 bytes of `argv[0]`'s string.

- **`sys_execve` must also pass `argc`/`argv` in `%rdi`/`%rsi`.**
  musl's `_start` reads the SysV stack layout and ignores `%rdi`/
  `%rsi` on entry.  donix's newlib `crt0.S` (removed at A5 step 7) did
  the opposite: it read them from registers.  Both conventions had to
  be satisfied while both kinds of binary existed.  With newlib gone,
  the `%rdi`/`%rsi` writes in `sys_execve` (`ktop[-12]`,
  `ktop[-13]`) are harmless but no longer necessary; they are kept for
  now.

- **A raw-syscall test that reads a kernel-written buffer must declare
  the buffer as an asm memory output — `"+m"(*ptr)`, not just
  `"memory"`.**  A `"memory"` clobber is an aliasing/scheduling
  barrier, not a per-location output; GCC does not treat it as "the
  asm wrote through this specific pointer."  `musl_wait`'s
  `raw_wait4` silently read a stale `status` until the constraint
  became `"+m"(*status)`; the kernel was writing the correct value.
  Same rule applies to any future test that passes `&local` to a raw
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
  the donix-private `SYS_OPENDIR` (500) — that was a newlib-only
  convenience, removed at A5 step 3.  `sys_open` grew a directory
  fallback because otherwise musl's `opendir` failed with
  `FR_INVALID_NAME`; that fallback stays.  When you add a syscall that
  has a donix-private equivalent, check which one musl actually uses
  before assuming they map 1:1.

- **musl `fstatat` routing — what `fstat`, `stat`, and `lstat`
  actually call.**  Read from the project-local musl source at
  `third_party/musl-src/src/stat/`:

  - `fstat(fd, st)` → `__fstatat(fd, "", st, AT_EMPTY_PATH)`.
  - `stat(path, st)` → `fstatat(AT_FDCWD, path, st, 0)`.
  - `lstat(path, st)` → `fstatat(AT_FDCWD, path, st, AT_SYMLINK_NOFOLLOW)`.

  `__fstatat` dispatches to `fstatat_statx` first if
  `sizeof(kstat.st_atime_sec) < sizeof(time_t)`.  On x86_64 both are
  8 bytes, so that condition is false and the code goes straight to
  `fstatat_kstat`.

  `fstatat_kstat` then picks the actual syscall:
  - `flag == AT_EMPTY_PATH && fd >= 0 && !*path` (the `fstat` case)
    → `__syscall(SYS_fstat, fd, &kst)` = **syscall 5**.
  - `(fd == AT_FDCWD || *path == '/') && flag == AT_SYMLINK_NOFOLLOW`
    (the `lstat` case) → `__syscall(SYS_lstat, path, &kst)`.
  - `(fd == AT_FDCWD || *path == '/') && !flag` (the `stat` case)
    → `__syscall(SYS_stat, path, &kst)` = **syscall 4**.
  - Otherwise → `__syscall(SYS_fstatat, fd, path, &kst, flag)` =
    **syscall 262** (newfstatat).

  Confirmed numeric values from
  `third_party/musl-src/arch/x86_64/bits/syscall.h.in`:
  `__NR_stat = 4`, `__NR_fstat = 5`, `__NR_newfstatat = 262`,
  `__NR_statx = 332`.

  Consequences for donix:
  - `sys_fstat` (5) is what `fstat` calls.  Implemented and tested by
    `musl_stat`.
  - `sys_stat` (4) is what `stat` calls when the path is absolute
    or `AT_FDCWD` is the fd.  Implemented and tested by
    `musl_stat`'s second half.
  - `sys_newfstatat` (262) is what `fstatat(fd, path, st, flags)`
    calls when the fd is not `AT_FDCWD` and the path is not
    absolute.  **Not yet implemented.**  Not on any current test's
    path.
  - `sys_statx` (332) is not reached on x86_64 because the
    `sizeof(st_atime_sec) < sizeof(time_t)` guard is false.  No
    implementation needed.
  - `SYS_lstat` (6) is only reached via `lstat()` with
    `AT_SYMLINK_NOFOLLOW`.  Not implemented; if something calls it,
    it will print `Unknown syscall: 6`.

- **`sys_stat` and `sys_fstat` share `fill_kstat_from_filinfo`.**
  Both construct a FatFs `FILINFO` (from an open `FIL` for `fstat`,
  from `f_stat(path, &fno)` for `stat`) and then call the shared
  helper to populate `kernel_stat_t` and copy it out via
  `safe_copy_to_user`.  If a third caller ever needs the same layout
  (e.g. `sys_newfstatat`), it should use the same helper, not
  duplicate the fill logic.

- **`getdents64` emits one record per call, deliberately.**  FatFs's
  `f_readdir` advances an irreversible cursor.  If `sys_getdents64`
  tried to pack multiple records into one call and one did not fit,
  the entry would be consumed but not returned and musl's `readdir`
  would skip it.  One record per call is obviously correct.  musl's
  `readdir` passes a buffer big enough for one `linux_dirent64`, so
  the fit check never fails in practice.

- **`musl_sh`'s argv[0] normalization.**  `musl_sh` normalizes
  `argv[0]` to `0:/NAME.ELF` if the token has no `:/`, and passes
  `argv[1..n]` through verbatim.  This convention shapes every port:
  a musl `cat`/`echo`/`ls` port must take a bare filename on `argv[1]`
  and prepend `0:/` itself, or the `musl_sh`-to-binary interface
  breaks.  `cat`, `echo`, and `ls` all do this.  Any future port that
  reads a filename argument must do the same.

- **Ports that enumerate a directory take a bare directory name on
  `argv[1]` and prepend `0:/`.**  The `ls` port does this.  It
  accepts an optional `argv[1]`, normalizes it to `0:/NAME` if it has
  no `:/`, and passes it to `opendir`.

### Build system

- **After every patch, verify the edit actually landed in the
  tree you are building.**  A session was lost to editing the wrong
  project directory: the kernel was built from a stale file, and
  every test produced the same "the fix didn't work" result for
  hours.  Before each build, run `git status` (shows the file as
  modified) and a targeted `grep` on the changed function.  If
  either is unexpected, stop and fix the tree before building.
- **QEMU `-d in_asm,cpu -D /tmp/qemu-log.txt` makes everything run
  ~1000× slower.**  It's fine for one-off instruction traces, but if
  left on, a loop of a few hundred thousand iterations looks like a
  hang.  Turn it off for normal testing; turn it back on only for a
  specific investigation.  The normal `./run` path does not set this
  flag; only the `run-debug-log` target does, and that is not on the
  default path.

### Musl userland tree (added 2026-09-27, A6)

- **The tree is `userland/musl/`, at the repo root, not under
  `04_kernel_64bit/`.**  Kernel and userland are distinct layers.
  The kernel build does not reach into the userland tree at all; the
  image build invokes `make -C ../userland/musl` as a prerequisite.
- **`userland/musl/build/` is gitignored.**  It holds the `.elf`
  outputs.  If it is ever accidentally committed, remove it with
  `git rm -r --cached userland/musl/build` and add the ignore rule.
- **One Makefile, two directories.**  `apps/` and `tests/` share the
  same uniform build rule; the split is documentation, not build
  mechanics.  Adding a program is: create the `.c` file, add its
  output path to `USERLAND_ELFS` in `05_boot_kernel64/Makefile`,
  and add a matching `mcopy_one` line.  Nothing else.
- **`USERLAND_ELFS` is an explicit list, not `$(wildcard)`.**  If
  a source is added but the corresponding `.elf` is left out of
  `USERLAND_ELFS`, `make` will fail with "No rule to make target"
  rather than silently omitting the file from the FAT.  That is the
  intended behavior: the failure mode from the old `/tmp` staging
  ("WARN: ... not found" and continue) is gone.

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

### Open issues

- **`sys_newfstatat` (262) is not implemented.**  `sys_fstat` (5)
  and `sys_stat` (4) are both done and tested.  See "musl `fstatat`
  routing" under "Syscall ABI".  Not on any current test's path.
  Phase B (busybox) may exercise it.
- **`sys_open` accepts non-directories when called with
  `O_DIRECTORY`.**  `ls 0:/hello-world.txt` prints
  `0 file(s), 0 directory(ies)` and exits 0, instead of failing with
  "not a directory."  FatFs's `f_opendir` accepts a file path and
  yields a `DIR` whose `f_readdir` immediately returns "no entries";
  `sys_open`'s `f_opendir` fallback path does not verify that the
  target is actually a directory.  Fix (deferred): after `f_opendir`
  succeeds, check the entry's `fattrib & AM_DIR`; if not set,
  close and return `-ENOTDIR`.
- **`fcntl` (72) is called by musl's `opendir`.**  musl's `opendir`
  does `open(path, O_RDONLY|O_DIRECTORY)` then
  `fcntl(fd, F_SETFD, FD_CLOEXEC)`.  `sys_fcntl` is unimplemented,
  so the kernel prints `Unknown syscall: 72` and returns `-1`;
  musl ignores the failure.  Implementing `fcntl` as a minimal
  stub (return 0 for `F_SETFD`/`F_GETFD`, `-1` for others) would
  remove the noise.  Deferred.
- **`Unknown syscall: N` fires during `musl_readdir`.**  Numbers
  seen: 6, 7, 8, 15, 17, 72.  `72` is traced to musl's `opendir`
  calling `fcntl`.  The others (`6` lstat, `7` mkdir, `8` creat,
  `15` rt_sigreturn, `17` pread64) have not been traced to a
  caller yet.  The `readdir` loop still returns the correct count,
  so the test is green, but the noise is real.
- **`isr14_handler` halts on user-mode faults.**  The `#PF` handler
  checks only `g_expect_fault`; it does not look at `error_code & 4`
  to distinguish a user-mode fault from a kernel-mode one.  Any
  unexpected user-mode fault kills the console instead of
  terminating the faulting process.  Fix: in `isr14_handler`, if
  `(error_code & 4)` and `g_expect_fault != 0x0E`, call
  `sys_exit(-1)` for the faulting process instead of halting.
- **`musl_sh` echoes garbage when the typed line contains
  backspaces.**  The kernel trace shows the argv that actually
  reached `sys_execve` is correct, but the echoed input line is
  scrambled.  Cosmetic.  Fix, if wanted: emit `"\b \b"` only when
  stdout is a real tty, or drop the erase-on-backspace entirely
  and just decrement `n`.

### Cosmetic / housekeeping

- Audit the other `puthex`/`put_dec` helpers in
  `userland/musl/tests/` for tight margins, same as the
  `musl_r10probe` `puthex64` bug.  Cosmetic unless a test starts
  faulting.
- `SYS_REBOOT` (503) is the only 500+ syscall in
  `syscall_dispatch`.  It is reachable only via raw `syscall(503)`
  from a musl program; there is no musl-side wrapper.  If a user
  program needs to reboot the machine, write a tiny `REBOOT.ELF`
  that issues the raw syscall, or implement Linux `reboot(2)`
  (syscall 169).  Not blocking anything.
- `PMM_ALLOC_DIAG` in `pmm.c` is gated diagnostic code from the
  `brk` investigation; harmless, can be deleted at leisure.

## Testing harness

- **Kernel shell:** `k` at boot prompt (`proclist`, `schstat`,
  `heapstat`, `selftest`, `fatls`, `elfload`).  (`usershell` was
  removed at A5 step 6.)
- **User shell:** `musl_sh`, launched automatically from the FAT
  as `0:/MUSL_SH.ELF` at boot.  Do not press `k` to get it.  There
  is no newlib fallback any more; a FAT read failure is a serial
  PANIC.
- **QEMU:** `make runkernel64-kvm-single` (fast),
  `make runkernel64-single` (TCG), `make logkernel64` (debug).
  The normal `./run` script uses
  `make clean && make FAT_CONFIG=single && make -C
  05_boot_kernel64 hdd-single.img && make -C 05_boot_kernel64
  run-single > capture.txt` — single-drive, TCG, clean rebuild
  every time, QEMU output teed to `capture.txt`.  It does **not**
  set the `-d in_asm,cpu` flag; that only appears in the
  `run-debug-log` target.
- **Serial:** `-serial stdio` for kernel log. VGA to the QEMU window.
- **musl userland build.**  The musl userland is built by
  `make -C userland/musl` (or implicitly, as a prerequisite of
  `hdd-single.img` / `hdd.img`).  Sources are ordinary `.c` files
  under `userland/musl/apps/` and `userland/musl/tests/`; the
  Makefile compiles each into `userland/musl/build/*.elf` with
  `../../toolchain/musl-gcc.sh -static -no-pie -O2 -mcmodel=large`.
  The image build (`05_boot_kernel64/Makefile`) copies those ELFs
  to `::/*.ELF` on the FAT via `mcopy_one`.  A missing ELF is a
  hard `make` failure, not a warning.
- **Canary run pattern.**  Boot `musl_sh`, run each test in the
  Part 2 canary table in order, compare against the expected
  output.  Tedious but is what "one change at a time" costs.
  Automating it (a `musl_sh` script-mode that reads commands from
  a file, plus a QEMU wrapper that boots and greps `capture.txt`)
  was considered and deferred.  Revisit when the manual cost grows
  past the patch cost.

## Recovery

dons-os `dev` is the recovery point. donix is a clone; if it goes bad,
`git restore .` or re-clone from dons-os. Commit after every successful
milestone. One change at a time so `git restore .` always works.

### Tagging convention

Tags through `20260926Z` use a single-letter suffix
(`20260922A`–`20260926Z`), incrementing through the alphabet within a
calendar day.  That scheme is exhausted; `Z` is the last letter and
was used by the commit that introduced the new scheme.

From the commit after that one onward, use:

    YYYYMMDD-NN

where `NN` is a two-digit sequence starting at `01` and incrementing
per commit within that day.  A new day restarts at `01` under its
own date.  Examples:

    20260926-01
    20260926-02
    ...
    20260927-01

The two-digit zero-padding is required so lexical sort order matches
chronological order.

Do not renumber or retag existing tags.  The single-letter history
stays as it is; the new scheme applies only to new tags.

**Working tags vs milestone tags.**  The `YYYYMMDD-NN` tags are
*working tags*: local-only, one per commit, deleted from the local
repo once the session's work is consolidated.  Their names and
commit SHAs are recorded in `migration-tags.txt` (for A1–A5) so the
mapping survives after the tags are gone.  The A6 working tags were
deleted without being recorded; the session-11 commit table in Part 2
is the record.

*Milestone tags* (`v0.5.5`, `v0.6.0`, …) are the only tags pushed
to the remote.  Do not push working tags.

## Summary for any session

1. Confirm donix baseline works: boot to `musl_sh`, run the canary
   (`hello`, `echo`, `cat`, `ls`, `memtest`, `musl_*` tests).
2. Apply one logical change at a time.  Test.  Commit.  Tag.
3. Revert with `git restore .` on any failure and diagnose before
   proceeding.
4. Commit with explicit `git add <file>...`, not `git add -A`.
   Verify `git diff --cached --stat` before committing.

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  Phase B is busybox.**

---

# Part 2 — Session Status

**Last updated:** 2026-09-27 (session 12, docs restructure + v0.6.0)
**Current HEAD:** the docs-restructure commit, on branch `dev`, ahead
of `origin/dev` by one commit (uncommitted at time of writing).  The
`v0.6.0` milestone tag is on the previous commit, already published.
**Last known-good code tag:** `v0.6.0` (published).
**Disaster preserved at:** branch `disaster-20260923A`
(commit `47262a9`, local only).

## Current milestone

**Session 12 complete: docs restructure.**

The five root-level docs have been consolidated to reduce maintenance
burden.  Historical material moved to `docs/`; the handoff is now the
only file that changes per session.

**Kernel and userland state is functionally identical to `v0.6.0`:**

- Only Linux x86_64 syscalls plus `SYS_REBOOT` (503) in
  `syscall_dispatch`.
- The FAT has 21 entries; every one is a musl build.
- The boot shell is `musl_sh`, loaded from `0:/MUSL_SH.ELF`.  No
  newlib fallback.
- `make` produces `kernel.bin` and `userland/musl/build/*.elf`
  (the latter invoked as a prerequisite of the image).

The only thing that changed this session is where the documentation
lives.  No code changed.

**Next milestone: Phase B (busybox).**  See Part 1's "Phase B"
section and the "Next step" below.

## Documentation layout (as of this session)

```
README.md                     project overview, build/run, getting started
ROADMAP.md                    future work only
handoff.md                    ← this file; live state, session log
docs/
  migration-history.md        A1–A6 narrative, musl build, resolved bugs
  dons-os-history.md          pre-fork version-by-version story (frozen)
  CHECKLIST.md                capability list (frozen at v0.6.0)
  MAINTENANCE.md              known debt (frozen at v0.6.0)
  LLD_BUG_REPORT.md           Clang/LLD toolchain bugs (live)
migration-tags.txt            A1–A5 working tag → commit map
```

The rule going forward: **current state goes in this file; history
goes in `docs/`.**  When a section of this file becomes historical
(the way A1–A5 and the resolved bugs just did), move it to the
appropriate `docs/` file and leave a pointer.

## Session 12 commits, in order

| Commit | What |
|--------|------|
| *(this commit)* | Docs restructure. README rewritten; ROADMAP trimmed to future-only; `docs/{migration-history,dons-os-history}.md` created; `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` moved from root; handoff trimmed. |

## Session 11 commits (retained for reference)

| Tag (deleted) | Commit | What |
|---------------|--------|------|
| `20260927-00` | `91f2fb4` | cleanup: remove residual newlib artifacts from `04_kernel_64bit`. |
| `20260927-01` | `f4769c7` | A6.1: `userland/musl` tree, Makefile, and all sources. |
| `20260927-02` | `d74bf97` | A6.23: build and stage musl userland from `userland/musl`. |
| `20260927-03` | `c346ba0` | A6.24: delete `build_musl_tests.sh`. |
| `v0.6.0` | `883c4ae` | v0.6.0: version bump and A6 documentation pass. |

Sessions 9 (A5) and 10 (doc pass, v0.5.5 publish) are documented in
[`docs/migration-history.md`](docs/migration-history.md).

## Canary state (all green as of `v0.6.0`)

Boot-time shell is `musl_sh`.  The canaries below were run from its
`donix> ` prompt in a single boot, in this order.  The FAT contains
**21** entries.  All binaries are built from `userland/musl/build/`
with nothing in `/tmp`.

| Test | State | Notes |
|------|-------|-------|
| hello | green | `hello from donix (musl)` |
| echo hi | green | `hi` |
| echo a b c d e | green | `a b c d e` |
| cat hello-world.txt | green | file contents printed |
| ls | green | 21 files; sizes match the FAT listing; `Unknown syscall: 72` once |
| memtest | green | `[memtest] PASS` (mmap heap, `0x8010000020`) |
| musl_stat | green | `STAT-OK` and `STAT2-OK` (both `fstat` and `stat`) |
| musl_min | green | `MUSL-START` |
| musl_malloc | green | `MALLOC-OK`, `SMALL-OK` |
| musl_printf | green | `MUSL-PRINTF` |
| musl_fork | green | `A`, `P`, `C` |
| musl_exec | green | `EXEC-PARENT-START`, `MUSL-START`, `EXEC-PARENT-DONE` |
| musl_wait | green | `WAIT-STATUS-OK 42`, `WAIT-WNOHANG-OK`, `WAIT-ANY-1 s=11`, `WAIT-ANY-2 s=22`, `WAIT-ALL-OK` |
| musl_readdir | green | 21 entries, `READDIR-DONE count=21` — `Unknown syscall: N` interleaved (see open issue) |
| musl_r10probe | green | `R10-AFTER=0xdeadbeefcafebabe` |
| brk_verify | green | `p=0x8000200000`, `VERIFY-OK` |
| brkraw | green | `FS=`, `BRK0=`, `BRKN=`, `WANT=` correct |
| brkgrow | green | `start=`, `64K got=`, `1M got=` correct |
| musl_sh (boot) | green | appears automatically at `donix> ` after `Shell: booting musl_sh from FAT` |

The pids assigned during the canary depend on the exact sequence of
commands, since each `sys_execve` in `musl_sh`'s `fork`ed child
consumes a pid.  The test logic is pid-independent; only the
`s=`/`r=` values in `musl_wait` are load-bearing, and they match.

## State on disk

- `userland/musl/` — the musl userland source tree (tracked).
  `Makefile`, `apps/*.c` (6), `tests/*.c` (14).  `build/` is
  gitignored.
- `third_party/musl-src/` and `third_party/musl-install/` — the
  musl source and install trees.  Gitignored.  Rebuild with
  `./toolchain/install_musl.sh`.  Requires network access for the
  initial clone.
- `toolchain/install_musl.sh` and `toolchain/musl-gcc.sh` — tracked.
- `docs/` — the historical record.  See the layout section above.
- `/tmp/20260923A-working-tree.patch`, `/tmp/memtest2.c.bak` — old
  backups, can be deleted.
- `~/code/x` — snapshot of the pre-cleanup tree, kept for diffing.
  Can be deleted.
- `notes_musl.txt` — at `/tmp/notes_musl.txt`.

## Next step (exactly this, then stop)

**Session 13 starts Phase B (busybox).**

The first Phase B commit is scoped as: **add busybox alongside the
existing apps, do not touch them.**  Concretely:

1. Clone busybox into `third_party/busybox/` (gitignored, like
   `musl-src/`).  Configure with `CONFIG_STATIC=y`, `CC` pointing
   at `toolchain/musl-gcc.sh`, and `CONFIG_PREFIX` in
   `third_party/busybox-install/` (also gitignored).
2. Build.  The result is a static musl-linked `busybox` ELF.
3. Stage it: copy to `userland/musl/build/busybox.elf`, add that
   path to `USERLAND_ELFS` in `05_boot_kernel64/Makefile`, and add
   a matching `mcopy_one` line producing `BUSYBOX.ELF` on the FAT.
4. `./run`, then from `musl_sh`: `busybox` (no args, to see the
   usage banner), then `busybox ls` and note the
   `Unknown syscall: N` output.

Do **not** rename or replace the existing `ls`/`echo`/`cat` yet.
They are the current canary, and the whole point of this first
commit is to see what busybox needs without disturbing anything
that already works.  Naming/dispatch decisions come after busybox
runs once.

**Open issues that will surface during Phase B, in priority order:**

1. `fcntl` (72) — busybox will call it for `F_SETFD`/`F_GETFD`/
   `F_DUPFD`.  A minimal stub is ~30 minutes.
2. `sys_newfstatat` (262) — busybox's `stat` may route through it.
3. `isr14_handler` — a user-mode `#PF` currently halts the console;
   busybox's first segfault will end the session instead of
   terminating the process.
4. `sys_open` accepting `O_DIRECTORY` on non-directories.
5. The `Unknown syscall: N` cluster in `musl_readdir`.

See the "Open issues" section in Part 1 for the full list.

**Do not push without a plan.**  `dev` currently carries the A6
work plus `v0.6.0` plus the docs restructure.  Whether Phase B lands
on `dev` only, gets merged to `main` at the next milestone, or is
pushed immediately is a separate decision.  Milestone tags go on
the published side; the same principle applies to Phase B.

## Open items

- **Phase B (busybox):** the next milestone.  See Part 1's
  "Phase B" section and the "Next step" above.
- **Open issues to chase, in priority order, before or during
  early Phase B:**
  - `fcntl` (72) — minimal stub.
  - `sys_newfstatat` (262) — three-way delegation.
  - `isr14_handler` user-mode fault handling.
  - `sys_open` `O_DIRECTORY` fix.
  - The `Unknown syscall: N` cluster in `musl_readdir`.
  - `musl_sh` backspace echo (cosmetic).
- **Deferred cleanups:**
  - Audit `puthex`/`put_dec` helpers in `userland/musl/tests/`.
  - `PMM_ALLOC_DIAG` removal from `pmm.c`.
  - A small `REBOOT.ELF` musl binary (raw `syscall(503)`) so
    userland can reboot the machine.

## How to use this file

At the start of a new session, paste the entire file and say:

> "Continue from here. What's the next step?"

At the end of a session, update **Part 2** (session status) with the
current tag, canary state, and next step.  Part 1 (strategy and
gotchas) only changes if the strategy or a gotcha changes.

**When a section of this file becomes historical, move it to
`docs/`.**  The appendix structure of `docs/migration-history.md` is
the model: the live narrative is one file, the detail is in an
appendix, the pointer from the live file is one line.
