This file is the project's **live state**: strategy, current milestone,
canary, open issues, and session log. Read it top to bottom when
starting a new session.

**Historical records live in `docs/`:**

- `docs/migration-history.md` -- how donix got here (A1-A6, resolved
  bugs, musl build specifics).
- `docs/dons-os-history.md` -- the pre-fork version-by-version story.
- `docs/CHECKLIST.md` -- capability checklist, frozen at v0.6.0.
- `docs/MAINTENANCE.md` -- known debt, frozen at v0.6.0.
- `docs/LLD_BUG_REPORT.md` -- Clang/LLD toolchain bugs.

---

# Part 1 -- Project Strategy

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

### Phase A -- Linux syscall ABI, then static musl binaries

Make the kernel speak Linux x86_64 syscalls and run a static musl
binary. Newlib's existing userland stays as a **passive regression
canary**: after every kernel change, `hello` / `memtest` / `ls` / `cat` /
`echo` / `printf` / `malloc` must still work.

Do **not** add new features to the newlib userland -- no `fork`, no
`execve`, no `getdents64` wrappers. That work would be thrown away when
musl arrives.

### Phase B -- busybox / coreutils against musl

Static-link busybox against musl and try running it.

**Phase A is complete as of `v0.6.0`.**  The kernel speaks Linux
x86_64 syscalls, the shell is musl, the userland apps are musl,
newlib has been fully retired from the tree, and the musl userland
lives in a tracked source tree at `userland/musl/`.  Phase B is
underway; see Part 2 for the current state.

## The one critical rule

**One change at a time. Test. Commit. Revert on failure.**

The previous session failed by changing six files at once; each symptom
had a different cause. Never apply two changes without testing between
them.

## Files that must not be touched

Unless a specific tested problem requires it:

- `process.c` -- especially `process_copy_kernel_frame`
- `scheduler.c`
- `context_switch.asm`
- `interrupts.c`
- `kmain.c`
- the prebuilt newlib `.a` files *(now removed -- A5 step 7)*
- `arc2/crt0.S` *(now removed -- A5 step 7)*
- `arc2/reent.c` *(now removed -- A5 step 7)*
- `user_newlib_linker.ld` *(now removed -- A5 step 7)*

All of these work in the baseline. The last four no longer exist;
they were removed with the newlib tree at A5 step 7.

## A6 -- musl userland source tree (complete at `v0.6.0`)

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

For the full A1-A6 migration narrative, see
[`docs/migration-history.md`](docs/migration-history.md).

---

## Known bugs and gotchas

Apply each only when a specific problem requires it.

### Process / scheduler

- **`process_fork_copy_frame` must preserve the registers that the
  parent's syscall-return path preserves.**  That is: the
  callee-saved set (`%rbx`, `%rbp`, `%r12`-`%r15`) **plus `%r8` and
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

- **`fork` must copy the ELF image region (added 2026-09-27, session
  23).**  `vmm_clone_page_table` shares leaf physical pages, so a
  fork child initially sees the parent's `.text`, `.rodata`, `.data`,
  and `.bss`.  In the window between `fork` returning in the child
  and `execve` running, the child runs busybox's `ash` code, which
  writes to busybox's globals in `.data`.  Those writes landed in the
  page the parent was still using.  The symptom was a `#PF` at
  `CR2 = 0x20` (musl's `errno` offset from a null thread pointer, in
  practice a NULL-deref in a `FILE`/job list walk) inside busybox's
  stdio teardown, immediately after `wait4` returned, in the *parent*.
  The fix is to extend `sys_fork`'s eager copy to include
  `[0x400000, 0x600000)`.

  The eager copy in `sys_fork` now covers all four user regions:
  ELF image (2 MB), user stack (64 KB), brk heap (1 MB), mmap window
  (4 MB).  See `sys_fork` in `user_syscall.c`.  **This is not real
  copy-on-write**: every page in every region is copied unconditionally,
  including read-only `.text` and `.rodata`.  A production fix would
  mark shared PTEs read-only and copy only on write.  See "fork is
  O(6 MB)" under Open Issues.

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
  which is linked at exactly `0x8000000000` -- the same value `kmain`
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
  This path prints `EXIT-FALLBACK: switching to idle, exiting pid=...`
  and is visible in normal `musl_fork` runs where the parent has
  already returned to the shell.

### Context switch

- **`MSR_FS_BASE` (0xC0000100) must be saved and restored per process.**
  It is set once by `arch_prctl(ARCH_SET_FS)` at musl startup and is
  read on the child's very first user instruction after `fork`
  (musl's `__post_Fork` -> `__get_tp` -> `mov %fs:0x0, %rdx`).  The
  kernel must therefore:
  - record it in `sys_arch_set_fs` and
    `sys_arch_prctl(ARCH_SET_FS)` (`self->fs_base = addr`),
  - inherit it in `sys_fork`
    (`child->fs_base = parent->fs_base;`),
  - save/restore around every context switch -- both the preemptive
    path in `timer_preempt_handler` (which switches CR3 directly,
    not through `context_switch`) and the three `context_switch`
    call sites in `scheduler.c`.

  Without save/restore, the MSR is effectively per-CPU and a second
  musl process clobbers the first one's TLS base.  Without
  inheritance, a fork child runs with `%fs = 0`.  Both failure modes
  are `#PF` in user mode at near-null addresses (`CR2 = 0x34` for the
  clobber case, `CR2 = 0` for the inheritance case).

  A third failure mode was seen in session 22 and resolved in session
  23: `CR2 = 0x20` in the *parent* after a forked child exited and
  `wait4` returned.  That turned out **not** to be an `fs_base`
  problem -- the trace showed `next->fs_base` correct at the
  `process_exit` switch.  The `0x20` was a NULL+offset dereference in
  busybox's own FILE/job list, whose head had been nulled by the
  child's pre-execve `.data` writes to a shared page.  See the "fork
  must copy the ELF image region" gotcha above.

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
  This is the same *class* of bug as the `%r10` clobber -- the kernel's
  ABI did not match Linux's, and newlib's usage never exercised the
  difference.  Musl did.

- **`SYS_EXIT = 60` in `user_syscall_entry.asm`** -- the only numeric
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
  restored** -- `%r10` was previously discarded, which was wrong and
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
  `strings_start` at `argv_region_bottom + array_bytes` -- the same
  address as the envp NULL write -- so the envp NULL zeroed the first
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
  the buffer as an asm memory output -- `"+m"(*ptr)`, not just
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
  the donix-private `SYS_OPENDIR` (500) -- that was a newlib-only
  convenience, removed at A5 step 3.  `sys_open` grew a directory
  fallback because otherwise musl's `opendir` failed with
  `FR_INVALID_NAME`; that fallback stays.  When you add a syscall that
  has a donix-private equivalent, check which one musl actually uses
  before assuming they map 1:1.

- **musl `fstatat` routing -- what `fstat`, `stat`, and `lstat`
  actually call.**  Read from the project-local musl source at
  `third_party/musl-src/src/stat/`:

  - `fstat(fd, st)` -> `__fstatat(fd, "", st, AT_EMPTY_PATH)`.
  - `stat(path, st)` -> `fstatat(AT_FDCWD, path, st, 0)`.
  - `lstat(path, st)` -> `fstatat(AT_FDCWD, path, st, AT_SYMLINK_NOFOLLOW)`.

  `__fstatat` dispatches to `fstatat_statx` first if
  `sizeof(kstat.st_atime_sec) < sizeof(time_t)`.  On x86_64 both are
  8 bytes, so that condition is false and the code goes straight to
  `fstatat_kstat`.

  `fstatat_kstat` then picks the actual syscall:
  - `flag == AT_EMPTY_PATH && fd >= 0 && !*path` (the `fstat` case)
    -> `__syscall(SYS_fstat, fd, &kst)` = **syscall 5**.
  - `(fd == AT_FDCWD || *path == '/') && flag == AT_SYMLINK_NOFOLLOW`
    (the `lstat` case) -> `__syscall(SYS_lstat, path, &kst)`.
  - `(fd == AT_FDCWD || *path == '/') && !flag` (the `stat` case)
    -> `__syscall(SYS_stat, path, &kst)` = **syscall 4**.
  - Otherwise -> `__syscall(SYS_fstatat, fd, path, &kst, flag)` =
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
    `AT_SYMLINK_NOFOLLOW`.  Now implemented as an alias of
    `sys_stat`; see below.

- **`sys_stat` / `sys_lstat` must synthesize a stat for the FAT
  root.**  `f_stat("0:/")`, `f_stat("/")`, and `f_stat(".")` all
  return `FR_INVALID_NAME`.  musl's `stat()` reaches them via paths
  of `"0:/"`, `"/"`, `"."`, and busybox's `ls` reaches them via
  `"."`.  Return a synthetic `S_IFDIR | 0755` stat via
  `fill_kstat_as_root`.  `sys_lstat` is an alias of `sys_stat`;
  FAT has no symlinks.

- **User paths need a leading `"./"` stripped before FatFs sees
  them.**  FatFs accepts `"0:/NAME"`, `"0:NAME"`, and bare
  `"NAME"`, but rejects `"./NAME"`.  busybox's `ls` builds
  `"./NAME"` for each directory entry.  `strip_dot_prefix()`
  peels leading `"./"` and `"/"` in `sys_open`, `sys_stat`, and
  `sys_unlink`.

- **`sys_open` must route directory requests through `f_opendir`
  up front.**  `f_open("0:/")` returns `FR_OK` with a `FIL`
  that is not usable for read or write.  The fix is to check
  `wants_dir || is_root` before calling `f_open`, not after.

- **`fcntl(2)` is required for `opendir` to succeed.**  musl's
  `opendir` calls `fcntl(fd, F_SETFD, FD_CLOEXEC)` and treats a
  failure as fatal.  Without `sys_fcntl`, busybox unwinds into
  musl's `a_crash()` (a user-mode `hlt`, which raises `#GP`).
  The minimum viable implementation returns 0 for
  `F_GETFD`/`F_SETFD`/`F_GETFL`/`F_SETFL` and `-EINVAL` for
  everything else.

- **`sys_mmap` must not return the same VA for two anonymous
  mappings.**  The old code returned `MMAP_BASE` unconditionally
  when `addr == 0`, so a second `mmap` overwrote the first.  The
  symptom is musl's stdio `FILE` table going inconsistent and
  `a_crash()` firing on the next lock attempt.  The fix scans
  the mmap window for a free run of pages.

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
  no `:/`, and passes it to `opendir`.  **If the argument names a
  regular file, `ls` must `stat` it and print the single entry
  rather than calling `opendir` on it** -- `opendir("file")` fails
  with `FR_NO_PATH` (added 2026-09-27, session 24; see the `ls.c`
  fix at `20260927-16`).

- **`file_slot_t` is refcounted (added 2026-09-27, session 15).**
  `sys_dup2` shares a `file_slot_t*` between two fds rather than
  copying it.  `file_slot_t.refcount` counts references; a new
  `put_file_slot` helper decrements and frees the slot's obj and
  the slot itself only at zero.  `alloc_file_slot` initializes to 1;
  `sys_close`, `close_all_files`, and `sys_dup2`'s newfd-closing
  path all go through `put_file_slot`; `sys_dup2` increments on
  share; `sys_fcntl`'s `F_DUPFD`/`F_DUPFD_CLOEXEC` case increments
  on share too.  Any future code that frees a `file_slot_t` directly
  is a bug -- route it through `put_file_slot`.  Any future code that
  aliases a slot must increment the refcount.

- **`sys_execve` gained a bare-name retry (added 2026-09-27, session
  19).**  `exec_resolve_bare_name()` builds `0:/NAME.ELF` (uppercase)
  from a bare name or the last component of a leading-`/` path.
  `sys_execve` calls it only when the raw `f_open` fails and the path
  contains no `:`.  This is what makes `musl_exec2`'s three test cases
  pass and what makes `busybox ash`'s `execve("ls", ...)` work.
  Any future caller that hands `sys_execve` a path containing a `:`
  skips the retry (that is how `musl_sh`'s pre-normalized paths stay
  on the fast path).

- **Syscalls must return a proper negative errno on failure, not a
  bare `-1` (added 2026-09-27, session 21).**  musl's
  `__syscall_ret` converts a return value to `-errno` only when it
  is in the range `-4095..-1`; a bare `-1` is passed through and the
  caller's `errno` is left at whatever value it had before, which is
  almost always stale and misleading.  Every failure path in
  `user_syscall.c` must return a value like `-ENOENT` or `-EIO`, not
  `-1`.  The `fatfs_errno()` helper maps a FatFs `FRESULT` to the
  appropriate negative Linux errno (see the definition in
  `user_syscall.c`).  This is what made `busybox sh`'s error message
  change from "Operation not permitted" to "not found".

- **`busybox ash`'s PATH probe needs `sys_access` (21), not just
  `sys_stat` (added 2026-09-27, session 22).**  `ash`'s command
  search uses busybox's `find_execable()`, which on Linux is
  `access(path, X_OK)`.  Without `sys_access` in the dispatch table,
  the probe returned `-ENOSYS` and `ash` concluded *every* external
  command was "not found" before it ever reached `stat` or `execve`.
  `sys_access` and `sys_faccessat` are now implemented as thin
  `f_stat`-with-retry wrappers that ignore the mode bits (FAT has
  no permissions).  See `sys_access` in `user_syscall.c`.

- **`busybox ash`'s PATH candidates are `sbin/ls`, `usr/bin/ls`,
  etc., not `ls` (added 2026-09-27, session 22).**  The default
  `PATH` is `/sbin:/usr/sbin:/bin:/usr/bin`, and `ash` builds
  `<dir>/<cmd>` for each entry.  `strip_dot_prefix` peels the
  leading `/`, leaving `sbin/ls`.  `f_stat("sbin/ls")` fails with
  **`FR_NO_PATH` (5)**, not `FR_NO_FILE` (4) or `FR_INVALID_NAME`
  (6) -- FatFs treats the path as having a directory component it
  cannot find.  The retry guard in `f_stat_with_retry` must
  therefore include `FR_NO_PATH` alongside `FR_INVALID_NAME` and
  `FR_NO_FILE`.  Missing that case was the entire reason `busybox
  sh`'s external commands did not run even after `sys_stat` grew a
  retry.  `sys_execve`'s inline retry already fires on *any* non-OK
  `FRESULT`, so it never had this bug.

- **The `f_stat_with_retry` helper is shared by `sys_stat` and
  `sys_access` (added 2026-09-27, session 22).**  Both call the same
  function to stat a path with the bare-name retry.  Any future
  syscall that needs a "does this path exist" check (e.g.
  `sys_newfstatat`, or a real `sys_execve` probe) should route
  through it rather than reimplementing the retry.  The helper is
  defined above `sys_stat` in `user_syscall.c`.

- **`sys_ioctl` returns termios and winsize; it does not echo
  (added 2026-09-27, session 24).**  `sys_ioctl` answers `TCGETS`
  (0x5401) with a cooked-mode `struct termios` on fds 0/1/2,
  `TCSETS`/`TCSETSW`/`TCSETSF` (0x5402-0x5404) as no-ops, and
  `TIOCGWINSZ` (0x5413) with a 24x80 `struct winsize`.  `TIOCGWINSZ`
  is what busybox ash actually probes to decide stdin is a tty --
  **not `TCGETS`**.  The `TCSETS*` and `TIOCSWINSZ` handlers accept
  and ignore.  Everything else returns `-ENOTTY`.
  **The kernel does not echo typed input.**  Earlier in session 24
  an echo path was added to `sys_read` behind a `g_stdin_wants_echo`
  flag set by `TCGETS`; it was reverted because busybox with
  `FEATURE_EDITING=y` echoes its own input via `lineedit.c` and the
  two produced a double-echo.  Any future kernel-side echo
  implementation must coordinate with the reader: either the kernel
  echoes and the reader does not (true cooked-mode tty), or the
  reader echoes and the kernel does not (donix today).  Do not do
  both.

- **busybox must be built with `FEATURE_EDITING=y` (added
  2026-09-27, session 24).**  `configs/busybox.config` sets
  `CONFIG_FEATURE_EDITING=y` and
  `CONFIG_FEATURE_EDITING_MAX_LEN=1024`.  Without `FEATURE_EDITING`,
  busybox ash does not echo or line-edit its own input and instead
  relies on the kernel to do it -- which donix's kernel does not.
  With it, `lineedit.c` handles echo, backspace, and line editing,
  and typed input is visible.  **`FEATURE_EDITING_ASK_TERMINAL`
  must stay off**: enabling it makes ash probe the terminal for
  cursor position at startup, and donix does not answer that probe.
  **`FEATURE_EDITING_MAX_LEN` must be non-zero** -- a value of 0
  yields a zero-length input buffer.

- **busybox ash prefers external binaries over its own applets
  (added 2026-09-27, session 24).**  `configs/busybox.config` has
  `CONFIG_FEATURE_PREFER_APPLETS` off, so `ash` resolves commands
  by walking `PATH` first and only falls back to its internal
  applet if no external binary is found.  Consequence for donix:
  `ls` inside `busybox sh` runs `0:/LS.ELF` (the musl port), not
  busybox's internal `ls` applet.  Same for `echo`, `cat`, `sh`,
  and so on.  The upside is that a fix to a musl port fixes the
  same command inside `busybox sh`.  To use busybox's own applets,
  set `CONFIG_FEATURE_PREFER_APPLETS=y` -- but that changes every
  command and re-tests the whole canary.  Leave it off for now.

### Build system

- **After every patch, verify the edit actually landed in the
  tree you are building.**  A session was lost to editing the wrong
  project directory: the kernel was built from a stale file, and
  every test produced the same "the fix didn't work" result for
  hours.  Before each build, run `git status` (shows the file as
  modified) and a targeted `grep` on the changed function.  If
  either is unexpected, stop and fix the tree before building.
- **QEMU `-d in_asm,cpu -D /tmp/qemu-log.txt` makes everything run
  ~1000x slower.**  It's fine for one-off instruction traces, but if
  left on, a loop of a few hundred thousand iterations looks like a
  hang.  Turn it off for normal testing; turn it back on only for a
  specific investigation.  The normal `./run` path does not set this
  flag; only the `run-debug-log` target does, and that is not on the
  default path.
- **Busybox is built from `third_party/busybox/` and staged by
  `userland/musl/Makefile`.**  `third_party/busybox/` is
  gitignored, like `musl-src/`.  The canonical busybox config is
  **`configs/busybox.config`** (tracked); `userland/musl/Makefile`
  copies it to `third_party/busybox/.config` before building, and
  the stamp file (`build/.busybox.stamp`) depends on the generated
  copy, so an edit to the tracked config forces a rebuild.  See
  "Musl userland tree" in this file.

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
- **The busybox config lives at `configs/busybox.config`, not
  inside `third_party/`.**  `third_party/` is wholly gitignored,
  and busybox's own nested `.gitignore` re-ignores dotfiles, so a
  root-level negation cannot reach `third_party/busybox/.config`.
  The canonical copy is therefore tracked at
  `configs/busybox.config`; `userland/musl/Makefile` copies it into
  `third_party/busybox/.config` before building.  **Edit only
  `configs/busybox.config`.**  `third_party/busybox/.config` is a
  build artifact that is overwritten on every config change and
  must never be hand-edited.  **Beware**: a mismatch between the
  tracked config and the generated one can trigger busybox's
  auto-`oldconfig`, which walks every new symbol and prompts
  interactively during `make`.  If that happens, answer with the
  defaults you want and then run
  `cp third_party/busybox/.config configs/busybox.config` to sync
  the tracked copy.  (Learned 2026-09-27, session 24.)

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
- **Commits made after writing Part 2's session table must be
  recorded in the table before the session ends.**  Session 14 made
  three commits (`2b93300`, `b2e6997`, `fd847ed`) but the handoff
  table listed only the first; the next session worked from a
  stale map.  An unrecorded commit on `dev` is worse than no entry,
  because the next session trusts the missing row.
- **A modified file with only a diagnostic print in it should be
  reverted, not committed.**  Session 23 ended with `scheduler.c`
  carrying a `[exit-switch]` trace that was added during the
  `#PF` investigation.  The correct action was `git restore
  04_kernel_64bit/scheduler.c`, not `git add`.  This is the same
  rule as above -- the working tree should contain only changes
  that are part of a logical commit.

### Open issues

- **`sys_mkdir` (7) is not implemented (added 2026-09-27, session
  24).**  busybox ash's line editor calls `mkdir(2)` once per
  keystroke, so every interactive `busybox sh` capture is peppered
  with `Unknown syscall: 7` between each character.  Cosmetic --
  the shell works fine -- but noisy and worth fixing.  Implement
  with FatFs's `f_mkdir`: copy the path, `strip_dot_prefix`, call
  `f_mkdir`, map the `FRESULT` through `fatfs_errno`.  Roughly 15
  lines.  **This is the next change.**

- **`fork` is O(~6 MB) per call (added 2026-09-27, session 23).**
  The eager copy in `sys_fork` walks and copies every mapped page
  in the ELF image (2 MB), user stack (64 KB), brk heap (1 MB), and
  mmap window (4 MB) on every fork.  Every page is copied
  unconditionally, including read-only `.text` and `.rodata`.
  Correct but slow.  `busybox ash` forks per external command, so
  each `ls` or `echo` copies ~6 MB.  Visible on TCG; less so on
  KVM.  The correct long-term fix is real copy-on-write: mark
  shared PTEs read-only in both parent and child, install a `#PF`
  handler that allocates a fresh page, copies the contents, remaps
  with write permission, and retries the faulting instruction.
  Not blocking anything today, but every new busybox applet that
  forks makes it worse.

- **`sys_newfstatat` (262) is not implemented.**  `sys_fstat` (5)
  and `sys_stat` (4) are both done and tested.  See "musl `fstatat`
  routing" under "Syscall ABI".  Not on any current test's path.
  Phase B (busybox) may exercise it.

- **`sys_munmap` is a stub returning 0.**  busybox will eventually
  call it and expect real unmapping.

- **`sys_brk` uses a fixed `heap_base = 0x8000200000`.**  Same
  class of latent bug as the old `sys_mmap` had; no per-process
  state, no awareness of other allocations.  It hasn't collided
  with anything yet.

- **The mmap window is a fixed 4 MB** (`0x8010000000`-
  `0x8010400000`).  If busybox fills it, the next step is a
  per-process bump pointer, not a fixed base.

- **`sys_open` accepts non-directories when called with
  `O_DIRECTORY` (kernel-side latent; the userland `ls` port no
  longer triggers it).**  FatFs's `f_opendir` accepts a file path
  and yields a `DIR` whose `f_readdir` immediately returns "no
  entries"; `sys_open`'s `wants_dir` branch does not verify that the
  target is actually a directory.  Before session 24 this made
  `ls 0:/hello-world.txt` print `0 file(s), 0 directory(ies)` and
  exit 0.  The musl `ls` port is now fixed (`20260927-16`) to stat
  its argument first, so this path is no longer hit by `ls`.  But
  the kernel's behavior is still wrong in principle: a caller that
  passes `O_DIRECTORY` on a file gets a bogus `DIR` instead of
  `-ENOTDIR`.  Fix (deferred, not blocking): after `f_opendir`
  succeeds, `f_stat` the path and check `fattrib & AM_DIR`; if not
  set, close and return `-ENOTDIR`.

- **`Unknown syscall: N` fires during `musl_readdir`.**  Numbers
  seen: 8, 15, 17.  `6` is now implemented as `sys_lstat`;
  `72` as `fcntl`; `21`/`269` as `access`/`faccessat`; `7` is now
  called once per keystroke by busybox ash (see the top bullet in
  this list).  The remaining three (`8` creat, `15` rt_sigreturn,
  `17` pread64) have not been traced to a caller yet.  The
  `readdir` loop still returns the correct count, so the test is
  green, but the noise is real.

- **`isr14_handler` halts on user-mode faults.**  The `#PF` handler
  checks only `g_expect_fault`; it does not look at `error_code & 4`
  to distinguish a user-mode fault from a kernel-mode one.  Any
  unexpected user-mode fault kills the console instead of
  terminating the faulting process.  This is what makes every
  user-mode `#PF` in this project end the boot instead of killing
  the process.  Fix: in `isr14_handler`, if `(error_code & 4)` and
  `g_expect_fault != 0x0E`, call `sys_exit(-1)` for the faulting
  process instead of halting.

- **`musl_sh` echoes garbage when the typed line contains
  backspaces.**  The kernel trace shows the argv that actually
  reached `sys_execve` is correct, but the echoed input line is
  scrambled.  This also appears to leave stale bytes in the kernel
  keyboard buffer, which the *next* program to read stdin can
  consume -- that is the likely cause of the transient
  `sh: syntax error: unexpected ";"` seen on the first
  `busybox sh` invocation in an earlier capture.  Cosmetic, but
  with a real side effect.  Fix, if wanted: emit `"\b \b"` only
  when stdout is a real tty, or drop the erase-on-backspace
  entirely and just decrement `n`.

- **`musl_wait`'s WNOHANG loop spins** (added 2026-09-27, session
  23).  Visible in the capture as hundreds of `.` characters.  The
  test's polling loop runs continuously until its last child exits,
  and timer ticks print a dot each time.  Semantics are correct --
  `WAIT-WNOHANG-OK` eventually prints and all assertions pass --
  but it's slow and noisy.  Fix if wanted: yield or sleep in the
  test loop instead of spinning.  Cosmetic.

- **`f_stat_with_retry` collapses path components for nonexistent
  directories (theoretical, added session 22).**  For a path like
  `SUB/missing`, `f_stat` returns `FR_NO_PATH`, the retry fires,
  and `exec_resolve_bare_name` resolves to `0:/MISSING.ELF` --
  dropping the `SUB/` component.  Today the FAT root is flat, so no
  path has a real directory component, and this cannot produce a
  wrong result.  If subdirectories ever appear on the FAT, revisit:
  the retry should only strip path components that do not exist as
  directories, not blindly take the last component.

### Cosmetic / housekeeping

- Audit the other `puthex`/`put_dec` helpers in
  `userland/musl/tests/` for tight margins, same as the
  `musl_r10probe` `puthex64` bug.  Cosmetic unless a test starts
  faulting.  Session 17's `musl_ids` `put_dec` prints a space
  between `sid=`/`ppid=` and the digits; test-only, cosmetic.
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
  run-single > capture.txt` -- single-drive, TCG, clean rebuild
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
- **Busybox config.**  The canonical copy is
  `configs/busybox.config` (tracked).  `make -C userland/musl`
  copies it to `third_party/busybox/.config` before building.
  **Edit only the tracked copy.**  The generated file is a build
  artifact.
- **Canary run pattern.**  Two variants:

  - **Focused canary (default, ~30 seconds).**  Run this on every
    change:

    ```
    hello
    ls
    memtest
    musl_fork
    musl_exec2
    musl_wait
    busybox ls
    busybox ash
    ```

    Then at the `ash` prompt: `ls`, `echo hi`, `exit`.  Then back
    at `donix>`: `hello`.

    This covers every syscall family that has ever broken
    (execve, stat, mmap, brk, fork, wait4, getdents64, readdir,
    fcntl, dup2, access, ioctl, arch_prctl) plus the busybox
    stack.  It catches 95% of regressions.

  - **Full canary (milestone only).**  Run this when cutting a
    milestone tag, before pushing to `origin/dev`, or when
    changing `sys_fork`, `sys_execve`, `sys_wait4`, `sys_mmap`,
    `sys_brk`, `vmm_clone_page_table`, or the scheduler.  See the
    Part 2 canary table for the full list; it includes all the
    focused-canary rows plus `echo`, `cat`, `musl_stat`,
    `musl_min`, `musl_malloc`, `musl_printf`, `musl_readdir`,
    `musl_r10probe`, `brk_verify`, `brkraw`, `brkgrow`,
    `musl_dup2`, `musl_dupfd`, `musl_ids`, `musl_getcwd`,
    `busybox echo`.

  Automating the canary (a `musl_sh` script-mode that reads
  commands from a file, plus a QEMU wrapper that boots and greps
  `capture.txt`) was considered and deferred.  Revisit when the
  manual cost grows past the patch cost.

## Recovery

dons-os `dev` is the recovery point. donix is a clone; if it goes bad,
`git restore .` or re-clone from dons-os. Commit after every successful
milestone. One change at a time so `git restore .` always works.

### Tagging convention

Tags through `20260926Z` use a single-letter suffix
(`20260922A`-`20260926Z`), incrementing through the alphabet within a
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
    20260927-02

The two-digit zero-padding is required so lexical sort order matches
chronological order.

Do not renumber or retag existing tags.  The single-letter history
stays as it is; the new scheme applies only to new tags.

**Working tags vs milestone tags.**  The `YYYYMMDD-NN` tags are
*working tags*: local-only, one per commit, deleted from the local
repo once the session's work is consolidated.  Their names and
commit SHAs are recorded in `migration-tags.txt` (for A1-A5) so the
mapping survives after the tags are gone.  The A6 working tags were
deleted without being recorded; the session-11 commit table in Part 2
is the record.  Sessions 13 and 14 used `20260927-01` through
`20260927-04`; their commit tables in Part 2 are the record.
Sessions 15, 16, and 17 used `20260927-05`, `-06`, and `-07`.
Sessions 18 through 22 used `20260927-08` through `-11`; their
commit tables in Part 2 are the record.  Session 23 used
`20260927-12`.  Session 24 used `20260927-13` through `-17`; the
session-24 commit table in Part 2 is the record.  Session 24's
working tags were deleted before `dev` was pushed for the `v0.6.2`
milestone.  Working tags are deleted after their session is
consolidated; the SHA in the table is what survives.

*Milestone tags* (`v0.5.5`, `v0.6.0`, `v0.6.1`, ...) are the only
tags pushed to the remote.  Do not push working tags.

## Summary for any session

1. Confirm donix baseline works: boot to `musl_sh`, run the
   focused canary (see "Testing harness").
2. Apply one logical change at a time.  Test.  Commit.  Tag.
3. Revert with `git restore .` on any failure and diagnose before
   proceeding.
4. Commit with explicit `git add <file>...`, not `git add -A`.
   Verify `git diff --cached --stat` before committing.

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  Phase B: busybox runs, its banner prints, and
`busybox ash` is now an interactive shell -- prompt, echo,
backspace, and line editing all work.  Three changes got us there:
`sys_ioctl` learned `TCGETS`/`TCSETS*`/`TIOCGWINSZ` (busybox ash
probes `TIOCGWINSZ`, not `TCGETS`, to decide stdin is a tty);
busybox was rebuilt with `FEATURE_EDITING=y` so `lineedit.c` does
the shell's own echo; and the musl `ls` port learned to `stat` its
argument before calling `opendir`, so `ls <file>` works in both
`musl_sh` and `busybox ash` (which prefers the external `LS.ELF`
over its own applet).  A kernel-side echo attempt
(`g_stdin_wants_echo` in `sys_read`) was tried and reverted:
with both the kernel and `lineedit.c` echoing, every keystroke
appeared twice.  `v0.6.2` is cut and published.  Next: implement
`sys_mkdir` (syscall 7) to silence the `Unknown syscall: 7` line
busybox's line editor produces on every keystroke.**

---

# Part 2 -- Session Status

**Last updated:** 2026-09-27 (session 24, ioctl TCGETS/TIOCGWINSZ +
busybox FEATURE_EDITING + musl ls single-file -- interactive busybox
ash and `ls <file>` in both shells; `v0.6.2` cut)
**Current HEAD:** `6c7b6a4` (tag `20260927-16`), on branch `dev`,
twenty-six commits ahead of `origin/dev`.
**Last known-good code tag:** `v0.6.2` (`<v0.6.2-merge-sha>`, published).
Working tags since `v0.6.0`: `20260927-01` (kernel: fcntl, mmap, path
handling), `20260927-02` (build: busybox integration), `20260927-03`
(handoff: session 13 status, published as `v0.6.1`), `20260927-04`
(busybox config tracking; deleted, no longer in `git show-ref`),
`20260927-05` (kernel: dup2 + file_slot_t refcounting; deleted),
`20260927-06` (kernel: F_DUPFD in fcntl),
`20260927-07` (kernel: setsid + getppid),
`20260927-08` (kernel: getcwd),
`20260927-09` (kernel: execve bare-name retry),
`20260927-10` (kernel: proper errnos from file syscalls),
`20260927-11` (kernel: sys_access/faccessat + FR_NO_PATH in stat
retry -- busybox sh runs external commands),
`20260927-12` (kernel: fork copies the ELF image region -- fixes
busybox ash .data corruption),
`20260927-13` (kernel: ioctl TCGETS/TCSETS*/TIOCGWINSZ --
interactive busybox ash),
`20260927-14` (busybox: enable FEATURE_EDITING -- ash does its own
line echo),
`20260927-15` (handoff: session 24 -- interactive busybox ash),
`20260927-16` (musl ls: stat argument before opendir -- list single
files).  All working tags were local-only and were deleted before the
`v0.6.2` push.
**Disaster preserved at:** branch `disaster-20260923A`
(commit `47262a9`, local only).

## Current milestone

**`v0.6.2` -- Phase B: `busybox ash` is interactive, and `ls <file>`
works in both shells.**  Three changes got us there:

1. `sys_ioctl` learned `TCGETS`/`TCSETS*`/`TIOCGWINSZ`.  The one
   that mattered for busybox was **`TIOCGWINSZ`**, not `TCGETS`:
   ash probes the window size on fd 0 to decide whether stdin is
   a tty.  Returning a 24x80 `struct winsize` is what made it
   print a prompt and enter its interactive command loop.
2. Busybox was rebuilt with `FEATURE_EDITING=y`.  That compiles
   in `lineedit.c`, which does the shell's own input echo.  Before
   this, ash had no way to echo (it expected the kernel to) and
   the kernel does not implement a tty line discipline, so typed
   input was invisible.
3. The musl `ls` port learned to `stat` its argument before calling
   `opendir`.  `ls hello-world.txt` was opening the file as a
   directory and failing with `FR_NO_PATH`.  Because busybox ash
   prefers the external `LS.ELF` over its own applet (PREFER_APPLETS
   is off), the same fix also fixes `ls <file>` inside `busybox sh`.

The kernel-side echo attempt (`g_stdin_wants_echo` in `sys_read`)
was tried and reverted: with both the kernel and `lineedit.c`
echoing, every keystroke appeared twice.

**One cosmetic issue remains:** busybox's line editor calls
`mkdir(2)` (syscall 7) once per keystroke, producing
`Unknown syscall: 7` between every character.  Harmless but noisy.
Implementing `sys_mkdir` via `f_mkdir` will silence it.  See
"Next step" below.

**State of the tree:**

- Kernel: Linux x86_64 syscalls plus `SYS_REBOOT` (503).
- The FAT has **27** entries: the 26 musl builds from A6 plus
  `BUSYBOX.ELF` (137192 bytes).
- The boot shell is still `musl_sh`, loaded from `0:/MUSL_SH.ELF`.
  busybox is invoked from it, not in place of it.
- The focused canary (see Part 1 "Testing harness") passes on the
  current tree with traces off.

## Session 24 commits, in order

| Tag | Commit | What |
|-----|--------|------|
| `20260927-13` | `b5e1180` | kernel: ioctl TCGETS/TCSETS*/TIOCGWINSZ -- interactive busybox ash.  Also reverts the kernel-side stdin echo machinery (`g_stdin_wants_echo`) added earlier in the session; busybox's own line editor does the echoing, and having both echoed produced a double-echo. |
| `20260927-14` | `a4854b3` | busybox: enable FEATURE_EDITING -- ash does its own line echo. |
| `20260927-15` | `0fba23b` | handoff: session 24 -- interactive busybox ash. |
| `20260927-16` | `6c7b6a4` | musl ls: stat argument before opendir -- list single files.  Fixes `ls <file>` in both `musl_sh` and `busybox sh` (which prefers the external LS.ELF over its own applet). |
| `20260927-17` | `<final-handoff-sha>` | handoff: session 24 -- interactive busybox ash + ls <file> (final handoff before `v0.6.2` cut). |

**Note:** the SHA for `20260927-17` above is the final handoff commit before the milestone merge.  The session-24 working tags were deleted before `dev` was pushed for `v0.6.2`; the SHAs above are the record.  The commit messages have the full narrative.

## Session 23 commits, in order

| Tag | Commit | What |
|-----|--------|------|
| `20260927-12` | `4e9c002` | kernel: fork copies the ELF image region -- fixes busybox ash .data corruption. |

The tag is a working tag (local-only).  The commit message has the
full narrative.

## Session 22 commits, in order

| Tag | Commit | What |
|-----|--------|------|
| `20260927-11` | `7b62c99` | kernel: sys_access/faccessat + FR_NO_PATH in stat retry -- busybox sh runs external commands. |

The tag is a working tag (local-only).  The commit message has the
full narrative.

## Session 21 commits, in order

| Tag | Commit | What |
|-----|--------|------|
| `20260927-10` | `e8ce6d8` | kernel: return proper errnos from file syscalls. |

The tag is a working tag (local-only).  The commit message has the
full narrative.

## Session 19 commits, in order

| Tag | Commit | What |
|-----|--------|------|
| `20260927-09` | `f68ab7c` | kernel: sys_execve bare-name retry -- `0:/NAME.ELF`. |

The tag is a working tag (local-only).  The commit message has the
full narrative.

## Session 18 commits, in order

| Tag | Commit | What |
|-----|--------|------|
| `20260927-08` | `47ca6d9` | kernel: implement getcwd(2) -- syscall 79. |

The tag is a working tag (local-only).  The commit message has the
full narrative.

## Session 17 commits, in order

| Tag | Commit | What |
|-----|--------|------|
| `20260927-07` | `3a34791` | kernel: implement setsid(2) and getppid(2) -- syscalls 107, 110. |

The tag is a working tag (local-only).  The commit message has the
full narrative.

## Session 16 commits, in order

| Tag | Commit | What |
|-----|--------|------|
| `20260927-06` | `7baca94` | kernel: implement F_DUPFD in fcntl(2). |

The tag is a working tag (local-only).  The commit message has the
full narrative.

## Session 15 commits, in order

| Tag | Commit | What |
|-----|--------|------|
| `20260927-05` | `8ab966d` | kernel: implement dup2(2) -- syscall 33, with file_slot_t refcounting.  (Tag deleted; SHA is the record.) |

The tag is a working tag (local-only).  The commit message has the
full narrative.

## Session 14 commits, in order

| Tag | Commit | What |
|-----|--------|------|
| `20260927-04` | `2b93300` | busybox: track config under `configs/`, install into the source tree.  (Tag deleted; SHA is the record.) |
| -- | `b2e6997` | handoff: session 14 -- busybox config tracked under `configs/`.  (Untagged; the handoff edit for session 14.) |
| -- | `fd847ed` | kmain: bump donix banner to 0.6.1.  (Untagged; closes the "bump banner" deferred-cleanup item from session 13's handoff.) |

Session 14's handoff commit (`b2e6997`) was made after the config-
tracking commit and was not re-tagged; the banner bump (`fd847ed`)
was made after that and was also not tagged.  Both are on `dev`.

## Session 13 commits, in order

| Tag | Commit | What |
|-----|--------|------|
| `20260927-01` | `3ec535f` | kernel: fcntl(2), distinct anonymous mmap VAs, root/`./` path handling. |
| `20260927-02` | `6ef2bea` | build: busybox integrated into the userland build. |
| `20260927-03` | `e7f418e` | handoff: session 13 status -- Phase B first contact.  Published as `v0.6.1`. |

All three tags are working tags (local-only).  The commit messages
have the full narrative.

## Session 12 commits (retained for reference)

| Commit | What |
|--------|------|
| `1e98ef6` | Docs restructure.  README rewritten; ROADMAP trimmed to future-only; `docs/{migration-history,dons-os-history}.md` created; `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` moved from root; handoff trimmed. |

## Session 11 commits (retained for reference)

| Tag (deleted) | Commit | What |
|---------------|--------|------|
| `20260927-00` | `91f2fb4` | cleanup: remove residual newlib artifacts from `04_kernel_64bit`. |
| `20260927-01` | `f4769c7` | A6.1: `userland/musl` tree, Makefile, and all sources. |
| `20260927-02` | `d74bf97` | A6.23: build and stage musl userland from `userland/musl`. |
| `20260927-03` | `c346ba0` | A6.24: delete `build_musl_tests.sh`. |
| `v0.6.0` | `883c4ae` | v0.6.0: version bump and A6 documentation pass. |

Note: the working-tag numbers `20260927-01`/`-02`/`-03` were used
in session 11 and then reset for sessions 13/14.  The names are
reused because the session-11 tags were deleted.  This is fine for
local working tags, but the session-11 commit SHAs above are the
only record of that mapping.

Sessions 9 (A5) and 10 (doc pass, v0.5.5 publish) are documented in
[`docs/migration-history.md`](docs/migration-history.md).

## Canary state (focused canary green as of `20260927-16`)

The **focused canary** is the default.  Run it on every change.
See Part 1 "Testing harness" for what it covers and when to run
the full list instead.

| Test | State | Notes |
|------|-------|-------|
| hello | green | `hello from donix (musl)` |
| ls | green | 27 files; sizes match the FAT listing |
| ls hello-world.txt | green | `FILE   hello-world.txt  (180 bytes)`.  Added to the focused canary in session 24 (the `ls.c` fix); it also works inside `busybox sh`. |
| memtest | green | `[memtest] PASS` (mmap heap, `0x8010000020`) |
| musl_fork | green | `A`, `P`, `C`.  The `EXIT-FALLBACK: switching to idle` line may appear interleaved with the next prompt -- that is `process_exit`'s empty-queue path working correctly, not a bug. |
| musl_exec2 | green | `EXEC2-OK` |
| musl_wait | green | `WAIT-STATUS-OK 42`, `WAIT-WNOHANG-OK`, `WAIT-ANY-1 s=11`, `WAIT-ANY-2 s=22`, `WAIT-ALL-OK`.  Slow and noisy (WNOHANG spin, prints hundreds of `.`); see Part 1 open issues. |
| busybox ls | green | 27 entries |
| busybox ash -> ls -> echo hi -> exit | green | **Fully interactive.**  Prints a prompt, echoes typed input, backspace and line editing work.  Runs `ls` (27 entries), `echo hi` (`hi`), `exit` (returns to `donix>`).  `Unknown syscall: 7` appears between keystrokes (see Part 1 open issues); it is cosmetic.  `ls hello-world.txt` inside `ash` also works now. |
| hello (after `ash` exits) | green | `hello from donix (musl)` -- shell survived the whole sequence |

The **full canary** is the milestone-only variant.  Add these rows
when cutting a milestone tag or before pushing to `origin/dev`:

| Test | State | Notes |
|------|-------|-------|
| echo hi | green | `hi` |
| echo a b c d e | green | `a b c d e` |
| cat hello-world.txt | green | file contents printed |
| musl_stat | green | `STAT-OK` and `STAT2-OK` (both `fstat` and `stat`) |
| musl_min | green | `MUSL-START` |
| musl_malloc | green | `MALLOC-OK`, `SMALL-OK` |
| musl_printf | green | `MUSL-PRINTF` |
| musl_exec | green | `EXEC-PARENT-START`, `MUSL-START`, `EXEC-PARENT-DONE` |
| musl_readdir | green | 27 entries, `READDIR-DONE count=27` -- `Unknown syscall: N` interleaved (see open issue) |
| musl_r10probe | green | `R10-AFTER=0xdeadbeefcafebabe` |
| brk_verify | green | `p=0x8000200000`, `VERIFY-OK` |
| brkraw | green | `FS=`, `BRK0=`, `BRKN=`, `WANT=` correct |
| brkgrow | green | `start=`, `64K got=`, `1M got=` correct |
| musl_dup2 | green | `DUP2-OK`.  No heap warnings. |
| musl_dupfd | green | `DUPFD-OK`.  No heap warnings. |
| musl_ids | green | `IDS-OK sid= 22`, `IDS-OK ppid= 2`.  (The space after `sid=`/`ppid=` is a cosmetic quirk of the test's `put_dec`.) |
| musl_getcwd | green | `GETCWD-OK /` |
| busybox echo hi / busybox echo Donald | green | `hi` / `Donald` |
| busybox (no args) | green | prints the multi-call banner and the applet list (`ash, cat, echo, ls, sh`) |

## State on disk

- `configs/busybox.config` -- the tracked canonical busybox config.
  `userland/musl/Makefile` copies it to
  `third_party/busybox/.config` before building.  **Edit only this
  copy.**  The generated file is a build artifact.
- `userland/musl/` -- the musl userland source tree (tracked).
  `Makefile`, `apps/*.c` (6), `tests/*.c` (19).  `build/` is
  gitignored.
- `third_party/busybox/` -- the busybox source tree and the
  generated `.config`.  Gitignored.  Built from source by
  `userland/musl/Makefile` on demand; the resulting binary is
  copied into `userland/musl/build/busybox.elf`.
- `third_party/busybox-install/` -- the busybox `make install`
  prefix.  Gitignored.  Not used by the build; `userland/musl`
  copies the binary directly from `third_party/busybox/busybox`.
- `third_party/musl-src/` and `third_party/musl-install/` -- the
  musl source and install trees.  Gitignored.  Rebuild with
  `./toolchain/install_musl.sh`.  Requires network access for the
  initial clone.
- `toolchain/install_musl.sh` and `toolchain/musl-gcc.sh` -- tracked.
- `docs/` -- the historical record.  See the layout section above.
- `/tmp/20260923A-working-tree.patch`, `/tmp/memtest2.c.bak` -- old
  backups, can be deleted.
- `~/code/x` -- snapshot of the pre-cleanup tree, kept for diffing.
  Can be deleted.
- `notes_musl.txt` -- at `/tmp/notes_musl.txt`.

## Next step (exactly this, then stop)

**Session 25 continues Phase B.**

In priority order:

1. **Implement `sys_mkdir` (syscall 7).**  busybox's line editor
   calls it once per keystroke, so every interactive `busybox sh`
   capture is peppered with `Unknown syscall: 7`.  Roughly 15
   lines: copy the path with `copy_user_string`, `strip_dot_prefix`,
   call FatFs's `f_mkdir`, map the `FRESULT` through `fatfs_errno`.
   Dispatch entry: `case SYS_MKDIR:`.  Define `SYS_MKDIR` in the
   header if it is not already there.  Do **not** combine this with
   anything else -- one change, one commit.

   Note: FatFs's `f_mkdir` on the flat FAT root works, but creating
   a directory named the same as an existing file returns
   `FR_EXIST`, which `fatfs_errno` maps to `-EPERM`.  That is
   close enough to `-EEXIST` for busybox's purposes; if it turns
   out to matter, `fatfs_errno`'s `FR_EXIST` case can be changed to
   `-EEXIST` (errno 17) at that time.

2. **Re-test `busybox sh`** and confirm `Unknown syscall: 7` no
   longer appears between characters.  The rest of the interactive
   behavior should be unchanged.

3. **The milestone tag is already cut.**  `v0.6.2` landed before
   this session's end; see the annotated tag message.  The next
   milestone candidate (`v0.6.3` for a small bump, `v0.7.0` for a
   `FEATURE_PREFER_APPLETS` flip or similarly broad change) is
   decided in a future session, not this one.

4. **Optional, next-next:** enable `FEATURE_EDITING_HISTORY`
   (value 256) and `FEATURE_TAB_COMPLETION` in
   `configs/busybox.config`.  Both are small and improve
   interactive use.  Not this session.

**Open issues that will surface during Phase B, in priority order:**

1. `sys_mkdir` (7) -- the next change.
2. Fork is O(6 MB) per call -- the long-term architectural fix is
   real copy-on-write.
3. `isr14_handler` -- a user-mode `#PF` currently halts the
   console instead of killing the faulting process.
4. `sys_newfstatat` (262) -- busybox may route through it once
   more applets are enabled.
5. `sys_munmap` -- a stub returning 0; busybox will eventually
   call it and expect real unmapping.
6. `sys_brk`'s fixed `heap_base` and the 4 MB mmap window -- both
   are latent collisions waiting to happen.
7. `sys_open` accepting `O_DIRECTORY` on non-directories (kernel-
   side latent; the musl `ls` port no longer triggers it).
8. The `Unknown syscall: N` cluster in `musl_readdir` (8, 15, 17).

See the "Open issues" section in Part 1 for the full list.

**`v0.6.2` is published.**  `dev` and `main` are in sync at the
milestone merge.  Future work continues on `dev`; the next
milestone merge to `main` happens when the next `vX.Y.Z` tag is
cut.  Working tags stay local and are deleted after each session
(before the push).

## Open items

- **Phase B (busybox):** the current milestone.  See Part 1's
  "Phase B" section and the "Next step" above.
- **Open issues to chase, in priority order, before or during
  early Phase B:**
  - `sys_mkdir` (7) -- the next change.
  - Fork O(6 MB) -- real COW is the long-term fix.
  - `isr14_handler` user-mode fault handling.
  - `sys_newfstatat` (262) -- three-way delegation.
  - `sys_munmap` real implementation.
  - `sys_brk` and the mmap window -- per-process state, not fixed
    bases.
  - `sys_open` `O_DIRECTORY` fix (kernel-side latent).
  - The `Unknown syscall: N` cluster in `musl_readdir` (8, 15, 17).
  - `musl_sh` backspace echo (cosmetic; also leaves stale bytes
    in the keyboard buffer).
  - `musl_ids` `put_dec` space (cosmetic).
  - `musl_wait` WNOHANG loop spin (cosmetic).
  - `f_stat_with_retry` path-component collapse (theoretical;
    matters only if the FAT gains subdirectories).
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

**If you make commits after updating Part 2's session table, update
the table again before the session ends.**  An unrecorded commit on
`dev` is worse than no handoff entry, because the next session will
trust the missing row.  (Learned in session 14: `b2e6997` and
`fd847ed` were made after the session-14 table was written, and the
table was not updated until session 15.)

**When a section of this file becomes historical, move it to
`docs/`.**  The appendix structure of `docs/migration-history.md` is
the model: the live narrative is one file, the detail is in an
appendix, the pointer from the live file is one line.
