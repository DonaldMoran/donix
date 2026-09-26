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
   `musl_min` green; `musl_printf` red until `20260924L`, see
   "Resolved bugs"
9. `mmap` = 9, `munmap` = 11 — **complete at `20260924B`** (minimal
   anonymous-private implementation)
9.5. `mprotect` = 10 — **stub added at `20260924B`**, returns 0.
   Not on the original list; musl calls it after `mmap` and its
   absence caused a `#GP`.  Real permission changes not implemented.
10. `getrandom` = 318, `rseq` = 334 (stubs returning `-ENOSYS`) — complete
11. `fork` = 57 — **complete at `20260924D`**
12. `execve` = 59 — **complete at `20260924I`** (both steps: spawn
    moved to 507, real in-place execve at 59).  Exercised by the new
    `musl_exec` test.
13. `wait4` = 61 — **complete at `20260924J`** (blocking wait, reap,
    parent-pid preserved across execve — all exercised by
    `musl_exec`; status propagation, `WNOHANG`, and `wait4(-1)` now
    exercised by `musl_wait`).  Required the `"+m"(*status)` asm
    fix in the test and Linux status-word encoding in `sys_wait4`.
14. `getdents64` = 217 — **complete at `20260924K`**.  Exercised by
    the new `musl_readdir` test (21 entries listed, count matches
    the image build).  Required a companion change to `sys_open`:
    musl's `opendir` goes through Linux `open(2)` with
    `O_DIRECTORY`, not the donix-private `SYS_OPENDIR` (500), so
    `sys_open` now falls back to `f_opendir` when `f_open` fails on
    a directory.  See "Syscall ABI" gotcha below.

**A2 complete at `20260924K`.**  Each item was its own commit.  After
each, newlib's `hello` / `memtest` / `ls` / `cat` / `echo` /
`printf` / `malloc` still worked.

**A2 is reopened at `20260926P` for `stat`/`fstat`/`newfstatat`.**
A4's `ls` port needs file sizes, and no earlier musl test called the
`stat` family, so A2's "complete" declaration at `20260924K` was
premature in the sense that "the syscalls the current tests need are
done."  See A4 below.

### A3 — musl shell

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
exists, bootstrapped through `build_musl_tests.sh` and wired into
the image build (see "musl test binaries" under Testing harness).  It
is launched manually from the newlib shell (`musl_sh.elf` at the `] `
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

**Complete at `20260926J`.**  All three A3 items are done:
- Tokenize `musl_sh`'s command line — `20260926F`.
- Path normalization (`0:/` + `.ELF` for bare names) — `20260926I`.
- `musl_sh` is the default boot shell, loaded from
  `0:/MUSL_SH.ELF` from the FAT.  The embedded newlib shell
  (`build_user_shell_elf`) remains as a fallback if the FAT read
  fails or the file is missing — `20260926J`.

Two kernel fixes were required along the way:
- `sys_execve` must pass `argc`/`argv` in `%rdi`/`%rsi` so that
  newlib's `crt0.S` (which reads those registers on entry) sees the
  execve arguments — `20260926G`.
- `sys_execve`'s argv layout wrote the envp NULL terminator on top
  of `argv[0]`'s string, and for short `argv[0]` values that also
  clobbered `argv[1]`'s string — `20260926H`.

### A4 — migrate userland apps to musl

`hello`, `ls`, `cat`, `echo`, `memtest` — rebuild each against musl.
**One at a time.** Test each.

Target list and current status:

| App | Status | Tag | Notes |
|-----|--------|-----|-------|
| `hello` | **done (parallel)** | `20260926L` | `HELLO_MUSL.ELF` on FAT; newlib `HELLO.ELF` intact. Straight port; only `printf`. |
| `echo` | **done (parallel)** | `20260926M` | `ECHO_MUSL.ELF` on FAT; newlib `ECHO.ELF` intact. Straight port; only `write` + argv. |
| `cat` | **done (parallel)** | `20260926N` | `CAT_MUSL.ELF` on FAT; newlib `CAT.ELF` intact. Straight port; `open`/`read`/`close`/`write` + argv. |
| `ls` | **unblocked, next** | — | Not a straight port. `sys_fstat` (5) and `sys_stat` (4) are both implemented and tested (`20260926R`, `20260926T`). `readdir`/`getdents64` have been green since `20260924K`. The port needs: `opendir("0:/")` + `readdir` for the entry list; `d_type` from `struct dirent` for `DT_DIR` vs `DT_REG` (already correctly set by `sys_getdents64` from `fno.fattrib & AM_DIR`); `stat("0:/NAME", &st)` per entry for `st_size`. No further kernel work is needed for `ls_musl` — `sys_newfstatat` (262) is not on its path. |
| `memtest` | **not started** | — | Read the source first. If it is pure `malloc`/`free`/`write`, it is a straight port. If it calls donix-private heap stats, the test's meaning changes and it becomes a decision, not a port. |

**Parallel-then-cut-over pattern.**  Each app is first built as a
parallel `*_MUSL.ELF` alongside the newlib binary, tested in
isolation, committed.  Only then does a separate commit replace the
FAT name (`HELLO.ELF` → musl build) and retire the newlib binary.
The parallel commit and the cut-over commit are always two distinct
changes.

### A5 — retire newlib

Remove the newlib userland, build rules, and libraries. Safe because
nothing uses them.

## musl build

**donix builds against a project-local musl 1.2.5, built from source.**

The musl source and install trees live under `third_party/`, which is
gitignored:

    third_party/musl-src/       — git clone of upstream musl at v1.2.5
    third_party/musl-install/   — the install tree (headers, libc.a,
                                  crt*.o, musl-gcc.specs)

Two tracked scripts build and use it:

- **`toolchain/install_musl.sh`** — clones musl v1.2.5 (network needed
  for the initial clone), configures with
  `--prefix=$REPO/third_party/musl-install --target=x86_64-linux-musl
  --disable-shared`, builds, installs, and verifies the specs file
  references the local prefix.  Idempotent: safe to rerun, but if
  `third_party/musl-src/` is not exactly at `v1.2.5` it bails rather
  than overwriting.

  **Critical:** the `--target=x86_64-linux-musl` flag makes musl's
  build system search for cross-prefixed host tools
  (`x86_64-linux-musl-gcc`, `-ar`, `-ranlib`, `-nm`).  Fedora's
  `musl-gcc` package does not ship those.  The install script
  therefore sets `CC=gcc AR=ar RANLIB=ranlib NM=nm` explicitly on the
  `./configure` line.  Without those overrides, `./configure` fails
  with "cannot find a C compiler," and if you get past that, `make`
  fails at `x86_64-linux-musl-ar: No such file or directory`.  Do
  not remove them.

- **`toolchain/musl-gcc.sh`** — a two-line wrapper that execs
  `gcc -specs <repo>/third_party/musl-install/lib/musl-gcc.specs`.
  It computes the prefix from its own location, so the repo can be
  cloned anywhere.  Note **`lib/`**, not `lib64/` — upstream musl's
  install layout uses `lib/`; Fedora's package uses `lib64/`.

`build_musl_tests.sh` calls `"$MUSL_GCC"` everywhere.  The default is
`./toolchain/musl-gcc.sh`.  To fall back to Fedora's system toolchain
for a comparison run:

    MUSL_GCC=/usr/bin/musl-gcc ./build_musl_tests.sh

**Fedora's system musl remains installed and working** at
`/usr/bin/musl-gcc` and `/usr/x86_64-linux-musl/`, as a reference and
fallback.  Its install layout is `lib64/`; the wrapper for the local
tree uses `lib/`.  Do not confuse the two.

**`struct stat` layout (x86_64, musl 1.2.5, 144 bytes):**

    offset  size  field
    ------  ----  -----
      0      8    st_dev
      8      8    st_ino
     16      8    st_nlink
     24      4    st_mode
     28      4    st_uid
     32      4    st_gid
     36      4    __pad0
     40      8    st_rdev
     48      8    st_size
     56      8    st_blksize
     64      8    st_blocks
     72     16    st_atim  (struct timespec: int64 tv_sec, int64 tv_nsec)
     88     16    st_mtim
    104     16    st_ctim
    120     24    __unused[3]
    144          total

This was verified by compiling and running a `offsetof`-based probe
against the project-local musl headers (the output is recorded in
session 7's log).  The kernel's `sys_fstat` / `sys_stat` /
`sys_newfstatat` must write this layout **byte-for-byte**.  Any field
out of place means `ls` prints garbage or the caller faults.
`sys_fstat` (`20260926R`) and `sys_stat` (`20260926T`) both write it
correctly — `musl_stat` proves `st_size = 180` and `st_mode = 0x81a4`
for `0:/HELLO-WORLD.TXT` via both `fstat(fd, &st)` and
`stat(path, &st)`.

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

- **`process_create` bakes `entry_point` into the resume frame.**
  `elf_load_into_process` returns `e_entry` but does not update the
  frame; any caller that passes a different `entry_point` to
  `process_create` will resume at the wrong RIP.  The helper
  `load_elf_into_user_process` in `kmain.c` (added at `20260926J`)
  rewrites the frame's RIP slot at `[pcb->rsp + 0x78]` after a
  successful load.  All three ELF-to-user-process call sites
  (`kmain`'s boot path, `usershell`, `elfload`) go through it.
  The latent bug was invisible while the only such binary was the
  embedded newlib shell, which is linked at exactly
  `0x8000000000` — the same value `kmain` passed as `entry_point`,
  so the stale frame slot was already correct by coincidence.
  `musl_sh` is linked at `0x400000`; the coincidence no longer
  holds.

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
  `%r10` clobber — the kernel's ABI did not match Linux's, and
  newlib's usage never exercised the difference.  Musl did.

- **`SYS_EXIT = 60` in `user_syscall_entry.asm`** — the only numeric
  syscall reference in assembly.
- **`-mcmodel=large` is required for userland** (linked above 4 GB).
  `-mcmodel=small`/`medium` don't work. `-no-pie` is not needed.
- **The syscall entry/return frame saves 16 slots, not 12.**  In
  addition to the callee-saved registers and the user RIP/RFLAGS/RSP,
  `user_syscall_entry.asm` pushes `%rdi`, `%rsi`, `%rdx`, `%r10`
  before the argument shuffle.  On the return path, **all four are
  restored** — `%r10` was previously discarded, which was wrong and
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

- **`sys_execve` argv layout: `envp` NULL needs its own slot.**  The
  frame layout is (low to high) `argv[0]..argv[argc-1]`, `argv NULL`,
  `envp NULL`, then the argument strings.  `array_bytes = (argc+1)*8`
  covers argv slots + argv NULL; the envp NULL goes at
  `argv_region_bottom + array_bytes`, and strings start at
  `argv_region_bottom + array_bytes + 8`.  The original code put
  `strings_start` at `argv_region_bottom + array_bytes` — the same
  address as the envp NULL write — so the envp NULL zeroed the first
  8 bytes of `argv[0]`'s string.  For a long `argv[0]`
  (`"0:/cat.elf"`) the collateral damage stopped before `argv[1]`'s
  string; for a short one (`"cat"`, `"echo"`) it clobbered the first
  bytes of `argv[1]` and the child saw an empty argument.  Fixed in
  `sys_spawn` at an earlier session and in `sys_execve` at
  `20260926H`.

- **`sys_execve` must also pass `argc`/`argv` in `%rdi`/`%rsi`.**
  musl's `_start` reads the SysV stack layout and ignores `%rdi`/
  `%rsi` on entry.  donix's newlib `crt0.S` (`arc2/crt0.S`) does the
  opposite: its first two instructions are
  `mov [rip + argc_saved], rdi` and `mov [rip + argv_saved], rsi`,
  and it never reads the SysV layout.  Both conventions must be
  satisfied for `sys_execve` to work with either kind of binary.  The
  frame slots are `-96` (`%rdi`) and `-104` (`%rsi`) from
  `kernel_stack_top`, i.e. `ktop[-12]` and `ktop[-13]` in C.  Fixed
  at `20260926G`; `sys_spawn` already did this via `frame[9]`/
  `frame[10]` of the child's initial resume frame.

- **`SYS_DONIX_SPAWN` (507) is the newlib spawn number.**  The newlib
  userland calls 507 for spawn semantics (`arc2/syscalls.c:spawn`).
  Number 59 is the real Linux `execve`, implemented at `20260924I`.
  `sys_spawn` (507) still creates a new process; `sys_execve` (59)
  replaces the caller's address space in place.  Do not point newlib
  at 59.

- **A raw-syscall test that reads a kernel-written buffer must declare
  the buffer as an asm memory output — `"+m"(*ptr)`, not just
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
  the donix-private `SYS_OPENDIR` (500) — that is a newlib-only
  convenience.  `sys_open` had to grow a directory fallback (at
  `20260924K`) because otherwise musl's `opendir` failed with
  `FR_INVALID_NAME`.  When you add a syscall that has a donix-private
  equivalent, check which one musl actually uses before assuming they
  map 1:1.

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
  - `sys_fstat` (5) is what `fstat` calls.  **Implemented at
    `20260926R`.**  Tested by `musl_stat` — green.
  - `sys_stat` (4) is what `stat` calls when the path is absolute
    or `AT_FDCWD` is the fd.  **Implemented at `20260926T`.**
    Tested by `musl_stat`'s second half (`stat("0:/HELLO-WORLD.TXT",
    &st)`) — green.  This was the last kernel blocker for `ls_musl`,
    which builds `0:/NAME` paths and calls `stat()` on them.
  - `sys_newfstatat` (262) is what `fstatat(fd, path, st, flags)`
    calls when the fd is not `AT_FDCWD` and the path is not
    absolute.  **Not yet implemented.**  Not needed by `ls_musl`
    (which uses absolute `0:/` paths), but needed for general
    correctness.  Deferred commit; nothing in A4 depends on it.
  - `sys_statx` (332) is not reached on x86_64 because the
    `sizeof(st_atime_sec) < sizeof(time_t)` guard is false.  No
    implementation needed.
  - `SYS_lstat` (6) is only reached via `lstat()` with
    `AT_SYMLINK_NOFOLLOW`.  Nothing in A4 needs it.  Not
    implemented; if something calls it, it will print
    `Unknown syscall: 6`.

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

- **`musl_sh`'s argv[0] normalization matches the newlib shell's
  convention.**  `musl_sh` normalizes `argv[0]` to `0:/NAME.ELF` if
  the token has no `:/`, and passes `argv[1..n]` through verbatim.
  That mirrors `user_shell.c:run_external`'s
  `snprintf(path, sizeof(path), "0:/%s.ELF", argv[0])`, and it is
  what the newlib `cat`/`echo` expect (bare filenames; they prepend
  `0:/` themselves).  Added at `20260926I`.  This convention also
  shapes the A4 ports: a musl `cat`/`echo` built for A4 must take a
  bare filename on `argv[1]` and prepend `0:/` itself, exactly as
  the newlib versions do, or the `musl_sh`-to-binary interface
  breaks.  `cat_musl` (`20260926N`) does this.  Any future A4 port
  that reads a file argument must do the same.

- **A4 ports of newlib apps must take a bare filename on `argv[1]`
  and prepend `0:/` themselves.**  See the `musl_sh` convention
  entry above.  The newlib `cat.c` does
  `snprintf(path, sizeof(path), "0:/%s", argv[1])`; the musl port
  does the equivalent with a manual copy loop.  Do **not** expect
  the shell to pass a full path for a file argument — `musl_sh`
  normalizes only `argv[0]`.  This is the single most likely source
  of silent failure in an A4 port that takes a filename.

- **A4 ports that enumerate a directory should take a bare directory
  name on `argv[1]` and prepend `0:/`, same as `cat`/`echo`.**  A
  future `ls_musl` will need this.  The newlib `ls` hardcodes
  `opendir("0:/")` and takes no argument; the musl port should
  accept an optional `argv[1]`, normalize it to `0:/NAME` if it has
  no `:/`, and pass it to `opendir`.  That keeps `musl_sh`'s
  bare-filename convention intact for both files and directories.

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
  ~1000× slower.**  It's fine for one-off instruction traces, but if
  left on, a loop of a few hundred thousand iterations looks like a
  hang.  Turn it off for normal testing; turn it back on only for a
  specific investigation.  The normal `./run` path does not set this
  flag; only the `run-debug-log` target does, and that is not on the
  default path.

### Musl build specifics (added 2026-09-26, tag `20260926P`)

- **`*_MUSL.ELF` binaries got dramatically smaller** when the switch
  from Fedora's musl to the project-local musl happened at
  `20260926P`.  `HELLO_MUSL.ELF` went 79 KB → 18 KB;
  `MUSL_SH.ELF` went 94 KB → 20 KB; `MUSL_READDIR.ELF` 117 KB →
  23 KB.  Cause: upstream musl 1.2.5's `libc.a` is 2.75 MB;
  Fedora's packaged `libc.a` is 11.4 MB.  The Fedora package appears
  to include extra objects and/or debug info.  **This size change is
  expected and harmless.**  Do not treat a small musl binary as a
  sign something is broken.
- **The local install layout is `lib/`, not `lib64/`.**  Fedora's
  package uses `lib64/`; upstream musl's `make install` puts
  everything under `lib/`.  `toolchain/musl-gcc.sh` points at
  `lib/musl-gcc.specs`.  If you ever invoke Fedora's `musl-gcc`
  directly, the paths it uses are `lib64/`-relative — they are
  different trees and cannot be mixed.
- **`--target=x86_64-linux-musl` triggers a search for cross-prefixed
  host tools.**  musl's Makefile will look for
  `x86_64-linux-musl-gcc`, `x86_64-linux-musl-ar`, and so on, which
  do not exist on a Fedora machine.  `toolchain/install_musl.sh`
  overrides them via `CC=gcc AR=ar RANLIB=ranlib NM=nm` on the
  configure line.  If a future edit to the install script strips
  those, the build will fail with `./configure: cannot find a C
  compiler` or `make: x86_64-linux-musl-ar: No such file or
  directory`.  Both are the same root cause.
- **The initial clone requires network access.**  `install_musl.sh`
  does `git clone https://git.musl-libc.org/git/musl`.  No offline
  path exists today; if that becomes a requirement, add a source
  tarball to the repo (the *source*, not the install tree).

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

- **`musl_printf` / `printnum` dump garbage — kernel bug, fixed at
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

  Fix: `char b[24]` → `char b[32]` at the `musl_r10probe` `puthex64`
  in `build_musl_tests.sh`.  One line.

  **NOTE: this fix did not, on its own, make the test reliable.**
  The page fault that first exposed this overflow turned out to be
  a *separate* kernel bug — the `MSR_FS_BASE` issue fixed at
  `20260926E`.  The `b[32]` fix only removed one of the ways the
  symptom could manifest.

- **Second `execve` from the same `musl_sh` faulted fatally**
  (resolved 2026-09-26, tag `20260926E`).  The previous session's
  diagnosis — page-table corruption in `vmm_clone_page_table` or
  in `sys_execve`'s teardown — was **wrong**.  Reading
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
  child's first instruction after `_Fork` returns — before any
  syscall — and faulted at `CR2 = 0`, `RIP = 0x4014D5`.  Disassembly
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

- **`sys_execve` clobbered `argv[1]` for short `argv[0]` values**
  (resolved 2026-09-26, tag `20260926H`).  Symptom: newlib
  `cat hello-world.txt` under `musl_sh` printed nothing and exited
  cleanly; the kernel trace showed `argc=2` reaching `sys_execve`,
  so the argument array was delivered, but the child saw an empty
  `argv[1]`.

  Root cause: `sys_execve`'s argv layout wrote the `envp` NULL
  terminator at `argv_region_bottom + array_bytes`, the same
  address as `strings_start`.  For `argv[0] = "cat"` (4 bytes), the
  `argv[1]` string began inside the 8-byte zeroing window and was
  clobbered.  For `argv[0] = "0:/cat.elf"` (11 bytes), the
  collateral damage stopped before `argv[1]`, so the bug was
  invisible until `musl_sh`'s normalization started passing short
  bare names.

  Fix: `strings_start = argv_region_bottom + array_bytes + 8`.
  The `envp` NULL write address is unchanged.  `sys_spawn` already
  had this fix from an earlier session.

- **`sys_execve` did not pass `argc`/`argv` in `%rdi`/`%rsi`**
  (resolved 2026-09-26, tag `20260926G`).  Symptom: newlib `cat`
  under `musl_sh` reached `main` but printed `(null)` for `argv[1]`.
  Root cause: `crt0.S` reads `argc`/`argv` from `%rdi`/`%rsi` on
  entry; `sys_execve` set the SysV stack layout but not those
  registers, so `crt0.S` stashed the execve call's own arguments —
  pointers into the old, torn-down address space — into
  `argc_saved`/`argv_saved`.  Fix: write `ktop[-12] = argc` and
  `ktop[-13] = array_base`.  `sys_spawn` already did the
  equivalent via `frame[9]`/`frame[10]`.

- **`musl_sh`'s `argv[0]` was not normalized** (resolved
  2026-09-26, tag `20260926I`).  Symptom: `cat hello-world.txt`
  under `musl_sh` called `f_open("cat")` and failed.  Root cause:
  `musl_sh` passed the raw typed token as the path; the newlib
  shell's convention is to prepend `0:/` and append `.ELF`.  Fix:
  `musl_sh` now builds `path = "0:/" + argv[0] + ".ELF"` unless
  `argv[0]` already contains `:/`, matching
  `user_shell.c:run_external`.

### Open issues

- **`sys_newfstatat` (262) is not implemented.**  `sys_fstat` (5)
  and `sys_stat` (4) are both done and tested.  See "musl `fstatat`
  routing" under "Syscall ABI" for the exact paths musl takes.
  `sys_newfstatat` is what `fstatat(fd, path, st, flags)` calls when
  the fd is not `AT_FDCWD` and the path is not absolute — nothing in
  A4 needs it, so it is a "for general correctness" commit that can
  wait until either a test exercises it or the A4 backlog is being
  cleared.  Implementing it is a straight port of `sys_stat` with
  the fd and flags handled: for the common case (`AT_FDCWD`, empty
  flags, or absolute path), it can delegate to `sys_stat`; for the
  general case, it needs to resolve `fd` + relative `path` to a
  full `0:/...` path before calling `f_stat`.
- **Newlib shell's line editor mishandles backspaces; a malformed
  line can leave FatFs in a bad state.**  Reproduced at
  `20260926J`.  The clean sequence works:

      ] cat hello-world.txt   → prints the file (pid=4)
      ] ls                    → prints 23 entries (pid=5)

  The failing sequence inserts a line with backspaces between them:

      ] cat     ls            → the `ls` output shows a garbage
                                entry "FILE   ls-diale.ct=  (0 bytes)"
      ] ls                    → f_open(0:/ls.ELF) -> 4 (FR_NO_FILE)
      ] ls                    → f_open(0:/ls.ELF) -> 4

  The newlib shell's line editor has a known backspace echo bug
  (see "Cosmetic / housekeeping" in Part 1).  A line containing
  backspaces can produce a command line whose stored buffer differs
  from what was echoed, and the resulting malformed command appears
  to corrupt the shared FatFs volume state or the kernel-side file
  table, so the *next* command cannot find its ELF.  The clean
  sequence in the *same* capture as the failure (a fresh boot,
  `] cat` then `] ls` with no backspaces in between) works, which
  rules out "any `cat` poisons the volume".

  Not reachable from `musl_sh`, which has no line editor and reads
  input byte-at-a-time.  Will be deleted with the newlib shell at
  A5.  If a FatFs-level fix is wanted before then, investigate
  whether `sys_spawn`/`sys_open` leave a volume-side lock or window
  in a bad state when they are called with an invalid path; but the
  motivating caller is going away, so this is low priority.

- **`musl_sh` echoes garbage when the typed line contains
  backspaces.**  Observed at `20260926M`, `20260926N`, and again at
  `20260926P`.  The kernel trace shows the argv that actually
  reached `sys_execve` is correct — `argc` matches the number of
  tokens typed, and the program output is right — but the echoed
  input line is scrambled.  Example from `20260926P`:

      donix> hello_mi usl
      sys_execve: ... argc=1 ... (hello_musl.ELF)

  The user typed `hello_musl`; the echo inserted a space.  `musl_sh`'s
  read loop handles `\b`/`0x7f` by emitting `"\b \b"` (three bytes)
  and decrementing the buffer index, so the *stored* line is
  correct; the garble is in what the terminal displays.  Most likely
  cause: the `"\b \b"` sequence interacts badly with QEMU's
  `-serial stdio` capture, which does not interpret backspace for
  display.  This is **not** the same bug as the newlib shell's line
  editor — it does not corrupt the stored line or FatFs.  Cosmetic.
  Fix, if wanted: emit `"\b \b"` only when stdout is a real tty, or
  drop the erase-on-backspace entirely and just decrement `n` (the
  character stays on screen but the stored line is still correct).

- **`musl_readdir` under `musl_sh` prints `Unknown syscall: N`
  between entries.**  Numbers seen at `20260926J` and unchanged
  after the toolchain switch at `20260926P`: 6, 7, 8, 15, 17, 72.
  The `readdir` loop still returns the correct count (27), so the
  test is green, but the noise is real and we do not yet know which
  musl libc call is generating it.  Some of these may now be
  answered by the `fstatat` routing read: `SYS_lstat` (6) is
  reachable via `lstat()` with `AT_SYMLINK_NOFOLLOW`; nothing in
  A4 needs it.  The others (`7` mkdir, `8` creat, `15` rt_sigreturn,
  `17` pread64, `72` fcntl) have not been traced to a caller yet.
  Next step if investigated: add a one-line serial print in
  `syscall_dispatch`'s `default:` case that includes the caller's
  PID and the saved user RIP
  (`*(uint64_t*)(self->kernel_stack_top - 56)`), to see which
  process and which user instruction is calling the unimplemented
  number.  Diagnose before deciding whether to implement the
  syscalls or just gate the print.

- **`isr14_handler` halts on user-mode faults.**  The `#PF` handler
  checks only `g_expect_fault`; it does not look at `error_code & 4`
  to distinguish a user-mode fault from a kernel-mode one.  Any
  unexpected user-mode fault kills the console instead of
  terminating the faulting process.  Not blocking A4, but it will
  bite the next time a user program faults unexpectedly.  Fix: in
  `isr14_handler`, if `(error_code & 4)` and
  `g_expect_fault != 0x0E`, call `sys_exit(-1)` for the faulting
  process instead of halting.

- If the "prompt returns, keyboard dead" symptom reappears, check
  whether the cause is the `20260924F` idle-fallback race.  The
  second-`execve` bug that previously shared this symptom space is
  now resolved (see `20260926E` above).

### Cosmetic / housekeeping

- `syscall.c:29` has an old stub `sys_brk` that shadows the real one in
  `user_syscall.c`. `syscall.o` is **not** in `OBJS` in the kernel
  Makefile, so `syscall.c` is not linked at all. Not fatal, but
  confusing. Clean up later (delete the file in its own commit).
- `build_musl_tests.sh` has duplicate `# Test 4:` and `# Test 7:`
  comments (copy-paste artifacts from `musl_twommap` and the
  `brkraw`/`brkgrow` tests).  Cosmetic.
- `musl_min` is built without `-no-pie` while every other musl test
  uses it.  `musl_min` works, but for consistency at some point it
  should match.  Cosmetic.
- Shell line-editing has a backspace echo bug. Not on critical path.
  (This is the root cause of the FatFs corruption open issue
  above, but the fix is A5's problem, not a standalone cleanup.)
- `sys_spawn` still prints its serial trace with the prefix
  `sys_execve:` (a string literal, not a symbol).  Cosmetic; rename
  the literal to `sys_spawn:` at the next convenient edit.
- `vmm_clone_kernel_half` and `vmm_free_user_page_tables` in `vmm.c`
  / `vmm.h` are unused (written for the abandoned copy-then-swap
  `execve`).  Kept for future copy-on-write work.  If still unused
  after A4, delete in a cleanup commit.
- `DEBUG_WRITE_BOUNCE` in `user_syscall.c` is currently `0` (gated,
  inert).  It was added at `20260924L` to test a shared-bounce-buffer
  theory for the `printnum` NUL bytes; the theory was wrong, the flag
  never fired.  Keep it gated for now as a diagnostic in case the
  symptom reappears; delete in a cleanup commit if still unused.
- `kmain.c`'s `handle_command` `elfload` case still does a manual
  `mov %cr3, %proc->cr3` around `load_elf_into_user_process`.  That
  switch is a leftover from an older `elf_load_into_process` that
  resolved PTEs against the active CR3; the current one takes the
  target CR3 as a parameter, so the switch is dead code.  Keeping
  it as-is for now; delete in a cleanup commit.
- The `syscall.h` header's `SYS_EXECVE` doc comment still says
  "NOT YET IMPLEMENTED" and describes the dispatcher routing 59 to
  `sys_spawn` as a placeholder.  That was replaced at `20260924I`.
  Doc-only, stale; fix in a cleanup commit.

## Testing harness

- **Kernel shell:** `k` at boot prompt
  (`proclist`, `schstat`, `heapstat`, `selftest`, `fatls`,
  `usershell`, `elfload`).
- **User shell:** `musl_sh`, launched automatically from the FAT
  as `0:/MUSL_SH.ELF` at boot (since `20260926J`).  Do not press
  `k` to get it.  Falls back to the embedded newlib shell if
  `MUSL_SH.ELF` cannot be read from the FAT.
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
- **musl test binaries** are built by `build_musl_tests.sh` (tracked,
  committed at `57a3f9e`, extended at `20260924I` with `musl_exec`,
  `20260924J` with `musl_wait`, `20260924K` with `musl_readdir`,
  `20260924L` with `musl_r10probe`, `20260926B`/`20260926C` with
  `musl_sh`, `20260926D` with the `musl_r10probe` buffer fix,
  `20260926F` with `musl_sh` tokenization, `20260926I` with
  `musl_sh` argv[0] normalization, `20260926L` with `hello_musl`,
  `20260926M` with `echo_musl`, `20260926N` with `cat_musl`,
  `20260926P` with the `MUSL_GCC` variable and switch to
  `./toolchain/musl-gcc.sh`, `20260926R` with `musl_stat` (fstat
  half), `20260926T` with the `stat` half of `musl_stat`).
  Sources are heredoc'd into `/tmp/` and linked with
  `"$MUSL_GCC" -static -no-pie -O2 -mcmodel=large`, where `$MUSL_GCC`
  defaults to `./toolchain/musl-gcc.sh`.  The image build
  (`05_boot_kernel64/Makefile`) copies `/tmp/musl_*` and
  `/tmp/*_musl` to `::/*.ELF` on the FAT via `mcopy_one`.
  **`/tmp` is not persistent across reboots** on Fedora.  If the
  `MUSL_*.ELF` files are missing from a boot, run
  `./build_musl_tests.sh` first.  A missing `/tmp/musl_*` produces
  a `WARN: ... not found` line during `hdd-single.img` build but
  does **not** fail the build — the image is silently missing those
  ELFs.
- **Canary run pattern.**  Every A4 port follows the same manual
  test: boot, run the new `*_MUSL.ELF` from `musl_sh`, run the
  corresponding newlib binary from `musl_sh` in the same boot, run
  a few error cases (no argument, missing file), then run the full
  canary suite.  This is tedious but is what "one change at a time"
  costs.  Automating it (a `musl_sh` script-mode that reads commands
  from a file, plus a QEMU wrapper that boots and greps `capture.txt`)
  was considered at `20260926N` and deferred — the script-mode patch
  was larger than the test harness it replaced, and `ls` + `stat` is
  the next blocker regardless.  Revisit when the manual cost grows
  past the patch cost.

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

**Last updated:** 2026-09-26 (session 7, A4 in progress)
**Current HEAD:** `20260926T` (the `sys_stat` commit); this handoff
commit will be `20260926U`
**Last known-good tag:** `20260926T`
**Disaster preserved at:** branch `disaster-20260923A` (commit `47262a9`)

## Current milestone

**A4 in progress.**  Three of the five A4 apps are done as parallel
`*_MUSL.ELF` binaries (`hello`, `echo`, `cat`).  `ls` is now
**fully unblocked**: both `sys_fstat` (5) and `sys_stat` (4) are
implemented and tested; `readdir`/`getdents64` have been green since
`20260924K`.  `ls_musl` is next.  `sys_newfstatat` (262) remains
unimplemented and is not needed by `ls_musl`.  `memtest` not yet
assessed.

## Session 7 commits, in order

| Tag | Commit | What |
|-----|--------|------|
| `20260926A` | `sys_read: fd 0 returns on first byte, not on count (POSIX short read)` | Kernel fix. `continue` → `break` in `sys_read`'s fd-0 branch. Unblocks musl's `read(0, buf, N)` for N > 1. |
| `20260926B` | `A3: add minimal musl shell, wired into image build` | `musl_sh` heredoc + build block in `build_musl_tests.sh`; `MUSL_SH := /tmp/musl_sh` + `mcopy_one` in `05_boot_kernel64/Makefile`. |
| `20260926C` | `A3: musl_sh reads a line byte-at-a-time, echoes as typed` | `musl_sh`'s read loop changed from one big `read` to a nested byte-at-a-time loop that echoes and breaks on `\n`. |
| `20260926D` | `musl_r10probe: fix stack buffer overflow in puthex64 (b[24] -> b[32])` | One-line test fix. Did *not* resolve the fault on its own. |
| `20260926E` | `Save/restore MSR_FS_BASE per process across context switches` | Adds `fs_base` to `pcb_t`, records it at `arch_prctl`, inherits it in `sys_fork`, save/restore at every context-switch site. Fixes the second-`execve` fatal `#PF`. |
| `20260926F` | `A3: musl_sh tokenizes its command line into argv` | `musl_sh` splits its input on whitespace and passes the token array to `execve`. |
| `20260926G` | `execve: pass argc/argv in %rdi/%rsi for newlib crt0.S` | `sys_execve` writes `ktop[-12]`/`ktop[-13]` so `crt0.S` sees the execve arguments. |
| `20260926H` | `execve: envp NULL terminator needs its own slot (fix argv[1] clobber)` | `sys_execve`'s `strings_start` moves to `argv_region_bottom + array_bytes + 8`. |
| `20260926I` | `A3: musl_sh normalizes argv[0] to 0:/NAME.ELF` | `musl_sh` builds `0:/NAME.ELF` for bare `argv[0]`, matching the newlib shell's convention. |
| `20260926J` | `boot: load MUSL_SH.ELF from FAT as default shell, embedded newlib as fallback` | Adds `load_file_to_buffer` and `load_elf_into_user_process` helpers to `kmain.c`; routes the boot path, `usershell`, and `elfload` through the latter.  Boot now tries `0:/MUSL_SH.ELF` first. |
| `20260926K` | `handoff: A3 complete; newlib line-editor/FatFs issue diagnosed` | Handoff update.  No code change. |
| `20260926L` | `A4: add musl hello as HELLO_MUSL.ELF (parallel, not replacing newlib HELLO.ELF)` | `build_musl_tests.sh` gains `/tmp/hello_musl`; `05_boot_kernel64/Makefile` gains `HELLO_MUSL :=` and the `mcopy_one` line.  Tested from `musl_sh`: `hello_musl` prints `hello from donix (musl)`.  Newlib `HELLO.ELF` untouched and green. |
| `20260926M` | `A4: add musl echo as ECHO_MUSL.ELF (parallel, not replacing newlib ECHO.ELF)` | Same pattern.  Tested with 0, 1, 3, 5 arguments.  Newlib `ECHO.ELF` untouched and green (`echo Donald loves Marie` verified). |
| `20260926N` | `A4: add musl cat as CAT_MUSL.ELF (parallel, not replacing newlib CAT.ELF)` | Same pattern.  `cat_musl hello-world.txt` prints the three lines; `cat_musl` with no argument prints `usage: cat FILE`; `cat_musl no-such-file.txt` prints `cat: cannot open` with kernel log `sys_open: f_open FAIL path=0:/no-such-file.txt r=4`.  Newlib `CAT.ELF` untouched and green. |
| `20260926O` | `handoff: A4 hello/echo/cat done; ls blocked on kernel stat` | Handoff update.  No code change. |
| `20260926P` | `build: use project-local musl 1.2.5 from source (toolchain/musl-gcc.sh)` | Adds `toolchain/install_musl.sh` and `toolchain/musl-gcc.sh`; adds `third_party/` to `.gitignore`; switches `build_musl_tests.sh` from bare `musl-gcc` to `"$MUSL_GCC"` (defaulting to `./toolchain/musl-gcc.sh`).  Rebuilds and reruns the full canary: all green.  `*_MUSL.ELF` shrink noticeably (upstream musl `libc.a` is 2.75 MB vs Fedora's 11.4 MB).  Fedora's `/usr/bin/musl-gcc` remains as a reference via `MUSL_GCC=/usr/bin/musl-gcc`. |
| `20260926Q` | `handoff: A4 progress; musl is project-local; stat is next` | Handoff update.  No code change. |
| `20260926R` | `sys_fstat: implement Linux fstat(2) for A4 ls` | Adds `SYS_STAT` (4), `SYS_FSTAT` (5), `SYS_NEWFSTATAT` (262) defines and `long sys_fstat(int, void*)` prototype to `syscall.h`.  Adds `kernel_stat_t` (144-byte layout matching musl 1.2.5 x86_64), `KSTAT_IFREG`/`KSTAT_IFDIR` constants, `sys_fstat` implementation, and `case SYS_FSTAT:` to `user_syscall.c`.  Adds `musl_stat` test to `build_musl_tests.sh` (opens `0:/HELLO-WORLD.TXT`, `fstat`s, asserts `st_size == 180` and `S_IFREG`, prints `STAT-OK`).  Adds `MUSL_STAT := /tmp/musl_stat` and an `mcopy_one` line to `05_boot_kernel64/Makefile`.  Tested: `musl_stat` prints `STAT-FR 0`, `STAT-SIZE 180`, `STAT-MODE 0x81a4`, `STAT-OK`.  Read the musl source while debugging and confirmed the routing: `fstat` → `__fstatat(fd, "", st, AT_EMPTY_PATH)` → `SYS_fstat = 5`.  `stat`/`lstat` route through the `fstatat_kstat` fallback to `SYS_stat = 4` and `SYS_lstat = 6`.  Full routing recorded in Part 1. |
| `20260926S` | `handoff: sys_fstat green; sys_stat and fstatat routing recorded` | Handoff update.  No code change. |
| `20260926T` | `sys_stat: implement Linux stat(2) for A4 ls` | Adds `long sys_stat(const char*, void*)` prototype to `syscall.h`.  Factors the kstat fill into `static void fill_kstat_from_filinfo(kernel_stat_t*, const FILINFO*)`, refactors `sys_fstat` to use it, adds `sys_stat` (path-based, uses `f_stat`) and `case SYS_STAT:` to `user_syscall.c`.  Extends the `musl_stat` test in `build_musl_tests.sh` with a second half that calls `stat("0:/HELLO-WORLD.TXT", &st)` and prints `STAT2-*` markers.  Tested: `musl_stat` prints `STAT-FR 0`, `STAT-SIZE 180`, `STAT-MODE 0x81a4`, `STAT-OK`, `STAT2-SR 0`, `STAT2-SIZE 180`, `STAT2-MODE 0x81a4`, `STAT2-OK`.  Full canary re-run green.  `MUSL_STAT.ELF` grew from 32728 to 32776 bytes (the second half links a bit more of libc). |
| `20260926U` | `handoff: sys_stat green; ls_musl is next` | This commit.  Handoff update.  No code change. |

## Canary state (all green as of `20260926T`)

Boot-time shell is `musl_sh`; the canaries below were run from its
`donix> ` prompt in a single boot, in this order.  The FAT contains
**27** entries.

| Test | State | Notes |
|------|-------|-------|
| hello | green | newlib banner block printed |
| hello_musl | green | `hello from donix (musl)` |
| memtest (newlib) | green | `[memtest] PASS` |
| ls | green | 27 files |
| echo hi | green | `hi` |
| echo_musl hi | green | `hi` |
| cat hello-world.txt | green | file contents printed |
| cat_musl hello-world.txt | green | file contents printed; identical output to newlib |
| musl_stat | green | `STAT-FR 0`, `STAT-SIZE 180`, `STAT-MODE 0x81a4`, `STAT-OK`, `STAT2-SR 0`, `STAT2-SIZE 180`, `STAT2-MODE 0x81a4`, `STAT2-OK` |
| printnum | green | `x=42` |
| musl_min | green | `MUSL-START` |
| musl_malloc | green | `MALLOC-OK`, `SMALL-OK` |
| musl_printf | green | `MUSL-PRINTF` |
| musl_fork | green | `A`, `P`, then child `C` |
| musl_fork_raw | green | `A`, `P`, `C` (order varies run-to-run, expected) |
| musl_exec | green | `EXEC-PARENT-START`, `MUSL-START`, `EXEC-PARENT-DONE` |
| musl_wait | green | `WAIT-STATUS-OK 42`, `WAIT-WNOHANG-OK`, `WAIT-ANY-1 got=23 s=11`, `WAIT-ANY-2 got=24 s=22`, `WAIT-ALL-OK` |
| musl_readdir | green | 27 entries, `READDIR-DONE count=27` — but see open issue: `Unknown syscall: N` interleaved |
| musl_r10probe | green | `R10-AFTER=0xdeadbeefcafebabe` |
| brk_verify | green | `p=0x8000200000`, `VERIFY-OK` |
| brkraw | green | `FS=`, `BRK0=`, `BRKN=`, `WANT=` correct |
| brkgrow | green | `start=`, `64K got=`, `1M got=` correct |
| musl_sh (boot) | green | appears automatically at `donix> ` after `Shell: booting musl_sh from FAT` |

Current `*_MUSL.ELF` sizes on the FAT (project-local musl):
`HELLO_MUSL.ELF` 18752, `ECHO_MUSL.ELF` 12864, `CAT_MUSL.ELF` 13032,
`MUSL_STAT.ELF` 32776 (larger because it links `printf`),
`MUSL_MIN.ELF` 12792, `MUSL_PRINTF.ELF` 18752, `MUSL_MALLOC.ELF` 28280,
`MUSL_FORK.ELF` 20016, `MUSL_FORK_RAW.ELF` 12408, `PRINTNUM.ELF` 27816,
`BRK_VERIFY.ELF` 17544, `BRKRAW.ELF` 12824, `BRKGROW.ELF` 12824,
`MUSL_EXEC.ELF` 12496, `MUSL_WAIT.ELF` 12440, `MUSL_READDIR.ELF` 23008,
`MUSL_R10PROBE.ELF` 12800, `MUSL_SH.ELF` 20144.  Newlib binaries
unchanged.

## Next step (exactly this, then stop)

**A4 item 4: port `ls` to musl as a parallel `LS_MUSL.ELF`.**

Now that `sys_fstat`, `sys_stat`, and `getdents64`/`readdir` are all
green, `ls_musl` is a straight userland port.  No kernel work
needed.  Follow the parallel-then-cut-over pattern used for
`hello`/`echo`/`cat`.

The `ls_musl` source:

- `opendir(path)` where `path = "0:/" + argv[1]` if `argv[1]` has no
  `:/`, else `argv[1]` verbatim, else `"0:/"` if `argc < 2`.  This
  matches `musl_sh`'s bare-filename convention (see the "A4 ports
  that enumerate a directory" entry in Part 1).
- `readdir(d)` loop.  For each entry `e`:
  - Build `entry_path = path + e->d_name` (careful with the trailing
    slash — FatFs accepts both `0:/NAME` and `0:/NAME`, so a
    straightforward concatenation is fine).
  - `struct stat st; stat(entry_path, &st);`
  - If `S_ISDIR(st.st_mode)`: print `<DIR>  NAME`.
  - Else: print `FILE   NAME  (SIZE bytes)` where `SIZE` is
    `st.st_size`.
  - Note: `e->d_type` from `dirent` also carries the DIR/REG bit and
    is set correctly by `sys_getdents64`.  Either `d_type` or the
    `stat` call can distinguish directories.  Using `stat` gives us
    the size in the same call, so prefer `stat`; `d_type` is a
    fallback if a later `stat` ever fails on an entry.
- `closedir(d)`.
- Print the summary `N file(s), M directory(ies)` matching the
  newlib `ls` format.
- Exit 0 on success, 1 on `opendir` failure, 2 on `readdir` error.

Add `/tmp/ls_musl` to `build_musl_tests.sh` (a `[BUILD] ls_musl`
block after `cat_musl`).  Add `LS_MUSL := /tmp/ls_musl` and an
`mcopy_one "$(LS_MUSL)" LS_MUSL.ELF` line to
`05_boot_kernel64/Makefile`, parallel to `LS.ELF`, not overwriting
it.

Test from `musl_sh`:
- `ls_musl` — expect the 27-entry listing, same names as newlib
  `ls`.
- `ls` (newlib) — must still print 27 entries.  Canary.
- Compare the two listings.  File sizes should match; the `<DIR>` /
  `FILE` distinction should match for every entry (all entries on
  the FAT are files, so no `<DIR>` lines expected).

Then run the full canary.  Commit as `20260926V`.  Handoff update
as `20260926W`.

`memtest` comes after `ls`.

## State on disk

- `/tmp/musl_*` and `/tmp/*_musl` — musl test binaries, rebuilt by
  `build_musl_tests.sh`.  **`/tmp` is not persistent across reboots
  on Fedora.**  If the `MUSL_*.ELF` files are missing from a boot,
  run `./build_musl_tests.sh` first.
- `third_party/musl-src/` and `third_party/musl-install/` — the
  musl source and install trees.  **Gitignored.**  Rebuild with
  `./toolchain/install_musl.sh`.  Requires network access for the
  initial clone.
- `toolchain/install_musl.sh` and `toolchain/musl-gcc.sh` — tracked.
- `/tmp/20260923A-working-tree.patch` (614 lines) — plain-text backup
  of abandoned work from the disaster commit.  Can be deleted.
- `/tmp/memtest2.c.bak` (524 bytes) — backup of an untracked test.
- `~/code/x` — snapshot of the pre-cleanup tree, kept for diffing.
  Can be deleted; the halt it was being kept for is now resolved.
- `notes_musl.txt` was moved out of the tree at `20260926P` (now at
  `/tmp/notes_musl.txt`).
- `vmm_clone_kernel_half` and `vmm_free_user_page_tables` in
  `vmm.c` / `vmm.h` are currently unused (written for the abandoned
  copy-then-swap `execve`).  Kept for future copy-on-write work.
- `DEBUG_WRITE_BOUNCE` in `user_syscall.c` is `0` (gated, inert).
- `04_kernel_64bit/kmain.c` has the two helpers,
  `load_file_to_buffer` and `load_elf_into_user_process`, above
  `kmain`.

## Open items

- **A4 continues.  Next: `ls_musl`.**  See "Next step" above.
  `sys_fstat` (5) and `sys_stat` (4) are both done.  `ls_musl`
  needs no further kernel work.  `sys_newfstatat` (262) remains
  unimplemented but is not on `ls_musl`'s path; it is a "for
  general correctness" commit that can wait.  `memtest` follows
  `ls`.
- **Cut-over commits pending.**  Each `*_MUSL.ELF` binary currently
  exists *alongside* the newlib version.  After all five A4 apps
  are green as parallel files, a separate cut-over commit per app
  replaces the FAT name (`HELLO.ELF` → musl build, etc.) and retires
  the newlib binary.  Do not do a cut-over before all five are
  green.
- **Open issues to chase, not blocking A4:**
  - Newlib shell's line editor mishandles backspaces; a malformed
    line can leave FatFs unable to open subsequent ELFs.  Not
    reachable from `musl_sh`; will be deleted with the newlib shell
    at A5.
  - `musl_sh` echoes garbage on lines containing backspaces
    (cosmetic; the stored line and the argv are correct).
  - `Unknown syscall: N` fires during `musl_readdir` (numbers 6,
    7, 8, 15, 17, 72; unchanged after the toolchain switch).  The
    readdir loop is still correct; the noise is real.  None of
    these are on the `ls_musl` path (which uses `opendir`,
    `getdents64`, `stat`, and `write`).
  - `isr14_handler` still halts on user-mode faults; should
    terminate the faulting process instead.
- **Deferred cleanups** (one commit each, when convenient):
  - Delete `syscall.c` (dead — `syscall.o` is not in `OBJS`).
  - Rename `sys_spawn`'s serial trace prefix from `sys_execve:`
    to `sys_spawn:`.
  - Delete `DEBUG_WRITE_BOUNCE` from `user_syscall.c`.
  - Remove the dead CR3 switch in `kmain.c`'s `elfload` case.
  - Fix the stale `SYS_EXECVE` doc comment in `syscall.h`.
  - Audit the other `puthex`/`put_dec` helpers in
    `build_musl_tests.sh` for tight margins.
  - Duplicate `# Test N:` comments and `musl_min`'s missing
    `-no-pie` in `build_musl_tests.sh`.
- Cosmetic items remain (see "Cosmetic / housekeeping" in Part 1).

## How to use this file

At the start of a new session, paste the entire file and say:

> "Continue from here. What's the next step?"

At the end of a session, update **Part 2** (session status) with the
current tag, canary state, and next step. Part 1 (strategy) only
changes if the plan itself changes.
