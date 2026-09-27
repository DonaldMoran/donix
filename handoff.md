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
A4's `ls` port needed file sizes, and no earlier musl test called the
`stat` family.  `sys_fstat` (5) landed at `20260926R`, `sys_stat` (4)
at `20260926T`.  `sys_newfstatat` (262) remains unimplemented but is
not needed by anything in A4.

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
exists, bootstrapped through the (now-deleted) heredoc build script and
wired into the image build.  It is launched manually from the newlib
shell (`musl_sh.elf` at the `] ` prompt) and takes over the console.
It does `fork` + `execve` + `wait4` and the `wait4` correctly
serializes parent and child output.  It reads a line byte-at-a-time
and echoes as typed.  Command paths must be fully qualified
(`0:/NAME.ELF`); it does not yet do the newlib shell's `0:/` + `.ELF`
normalization.  It does not tokenize its command line; the whole line
is passed as `argv[0]` and as the `execve` path.

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
  (`build_user_shell_elf`) remained as a fallback at `20260926J`;
  it was removed at A5 step 6.

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
| `ls` | **done (parallel)** | `20260926V` | `LS_MUSL.ELF` on FAT; newlib `LS.ELF` intact. Uses `opendir`/`readdir` + `stat` per entry. Output is byte-for-byte identical to newlib `ls`. |
| `memtest` | **done (parallel)** | `20260926W` | `MEMTEST_MUSL.ELF` on FAT; newlib `MEMTEST.ELF` intact. Straight port; only `malloc`/`free`/`printf`.  Both `PASS`; the addresses differ (`0x8010000020` for musl's mmap heap vs. `0x8000200008` for newlib's brk heap), which is the expected separation between the two allocators. |

**A4 complete at `20260926W`.**  All five apps have parallel musl
builds on the FAT, tested individually, with the newlib originals
intact and the full canary green.  The next milestone was the
**cut-over**, then **A5** (retire newlib).

**Parallel-then-cut-over pattern.**  Each app is first built as a
parallel `*_MUSL.ELF` alongside the newlib binary, tested in
isolation, committed.  Only then does a separate commit replace the
FAT name (`HELLO.ELF` → musl build) and retire the newlib binary.
The parallel commit and the cut-over commit are always two distinct
changes.  Both phases are complete.

### A4 cut-over (complete at `20260926Y`)

Each `*_MUSL.ELF` existed *alongside* the corresponding newlib
`*.ELF`.  The cut-over replaced the FAT name with the musl build and
retired the newlib binary.

**Complete at `20260926Y`.**  All five apps cut over in a single
commit (see Part 2's cut-over notes for why one commit rather than
five).  At that tag the newlib `.elf` files were still built but no
longer copied to the FAT.  They are now fully removed — A5 step 7.

### A5 — retire newlib (complete at `20260926-08`)

Remove the newlib userland, build rules, and libraries.  Done as
eight commits, each tested against the full canary before the next.

**A5 removes, by step:**

1. **Newlib `*-elf` phony targets and `user-elfs` aggregate**
   (`05_boot_kernel64/Makefile`).  Also removed `FSTEST`,
   `MULTITEST`, `BIGTEST` from the FAT: those were the only
   `mcopy_one` lines sourcing `$(USERLAND_DIR)` and have no musl
   port.  FAT went 24 → 21 entries.  Tag `20260926-01`.
2. **`SYS_DONIX_SPAWN` (507) and `sys_spawn`.**  Nothing in the
   musl path called 507; newlib's `arc2/syscalls.c:spawn` was the
   only caller.  Removed the `#define` and forward declaration too.
   Tag `20260926-02`.
3. **`SYS_OPENDIR` / `SYS_READDIR` / `SYS_CLOSEDIR` (500–502).**
   Newlib-only conveniences; musl's `opendir` goes through Linux
   `open(O_DIRECTORY)` + `getdents64`, which is a separate
   implementation (`sys_getdents64`) and stays.  Also removed the
   now-dead kernel-side `dons_dirent_t` typedef.  Tag `20260926-03`.
4. **`SYS_ARCH_SET_FS` (504).**  Newlib-only; musl uses
   `arch_prctl(ARCH_SET_FS)` = 158, a separate case that stays.
   Tag `20260926-04`.
5. **`SYS_DONIX_SBRK` (505).**  Newlib-only increment-based sbrk.
   musl uses `brk(12)` with the Linux absolute-address ABI, which
   is a separate function and stays.  Tag `20260926-05`.
6. **The embedded newlib shell and its fallback.**  Deleted the
   xxd-generated blob (`user_shell_data.c`, ~89 KB), its Makefile
   rule and object entry, and every reference in `kmain.c`.  The
   boot path now reads `0:/MUSL_SH.ELF` from the FAT as the only
   shell source; a failure to read or load it is a serial PANIC
   rather than a silent fallback.  Removed the `usershell` command
   from the debug kernel shell.  Kernel shrank by ~88 KB.  Tag
   `20260926-06`.
7. **`04_kernel_64bit/userland/newlib/`.**  The whole tree: the
   newlib Makefile, built `.a` files, `arc2/`, `apps/`, `include/`.
   Removed the `userland` phony target, the `all: userland
   kernel.bin` prerequisite, the `$(MAKE) -C $(USERLAND_DIR) clean`
   line, the `USERLAND_DIR` variable, and the now-unused
   `USER_CFLAGS`.  Tag `20260926-07`.
8. **Dead-code cleanup.**  Deleted `syscall.c` (unlinked stubs);
   removed `DEBUG_WRITE_BOUNCE` from `user_syscall.c`; removed the
   dead CR3 switch in `kmain.c`'s `elfload` case; deleted the
   unused `vmm_clone_kernel_half` / `vmm_free_user_page_tables`
   from `vmm.c` / `vmm.h`.  Tag `20260926-08`.

Each step has its own commit and canary run.  The full canary was
green after every step.

**What remains in the tree after A5:**
- `SYS_REBOOT` (503) is the only 500+ syscall number left in
  `syscall_dispatch`.
- The only userland is musl.  The FAT has `HELLO.ELF`, `ECHO.ELF`,
  `CAT.ELF`, `LS.ELF`, `MEMTEST.ELF`, `MUSL_SH.ELF`, and the musl
  test binaries, all built against the project-local musl 1.2.5.
- The only boot shell is `musl_sh`, loaded from `0:/MUSL_SH.ELF`.
- `make` produces `kernel.bin` only.  No recursive userland build.

### A6 — musl userland source tree (complete at `v0.6.0`)

Move the musl userland out of the heredoc-based build script and
into a tracked source tree at `userland/musl/`.  This is a pure
restructure: the C sources do not change, and the binaries on the
FAT behave identically to before.

**Why it was needed.**  Every musl ELF was previously a heredoc
inside a single ~1400-line shell script.  The script wrote each
source to `/tmp/*.c`, compiled it to `/tmp/*.elf`, and the image
Makefile copied the results onto the FAT.  That worked for the
migration but does not scale: adding a program meant another
50-line heredoc, and the sources were never tracked as real files.
Phase B (busybox) needs a real source tree.

**What changed.**  See Part 2's session 11 notes.  In summary:

- A new tracked tree at `userland/musl/` with `apps/`, `tests/`,
  `Makefile`, and a gitignored `build/` output directory.
- `userland/musl/Makefile` builds every `.c` into `build/*.elf`
  with `-static -no-pie -O2 -mcmodel=large`.
- `05_boot_kernel64/Makefile` invokes `make -C ../userland/musl`
  before staging the FAT, and copies `../userland/musl/build/*.elf`
  onto it.  No `/tmp` staging, no separate build script.
- The old heredoc build script is deleted.
- Residual newlib artifacts in `04_kernel_64bit/`
  (`user_newlib_linker.ld`, `user_shell_data.c`, the empty
  `userland/newlib/` directory) removed in a separate cleanup
  commit.

**Verified.**  The full canary runs green from a fresh boot with
nothing in `/tmp`, from the new pipeline.  The FAT still has 21
entries, all musl builds, all correct sizes.

**`musl_min` note.**  The old script built `musl_min` without the
explicit `-no-pie` flag (an inconsistency documented in the old
"Cosmetic" section).  The new Makefile builds it uniformly with
`-no-pie`, like every other binary.  `readelf -h` confirms
`Type: EXEC` and `Entry point 0x40022b` for the new build; the FAT
copy runs `MUSL-START` cleanly.  The inconsistency is resolved.

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

`userland/musl/Makefile` calls `../../toolchain/musl-gcc.sh`
everywhere.  To fall back to Fedora's system toolchain for a
comparison run, override `MUSL_GCC`:

    make -C userland/musl MUSL_GCC=/usr/bin/musl-gcc

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
  successful load.  All ELF-to-user-process call sites (`kmain`'s
  boot path and the debug-shell `elfload` command) go through it.
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
  *(Superseded by A6: the musl userland now links at `0x400000`
  (below 4 GB) and uses `-no-pie` uniformly.  `-mcmodel=large` is
  kept in the flags for consistency but is not load-bearing for
  musl binaries.  See A6 notes.)*
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
  and it never reads the SysV layout.  Both conventions had to be
  satisfied while both kinds of binary existed.  With newlib gone
  (A5 step 7), the `%rdi`/`%rsi` writes in `sys_execve`
  (`ktop[-12]`, `ktop[-13]`) are harmless but no longer necessary;
  they are kept for now.  Fixed at `20260926G`.

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
  the donix-private `SYS_OPENDIR` (500) — that was a newlib-only
  convenience, removed at A5 step 3.  `sys_open` grew a directory
  fallback (at `20260924K`) because otherwise musl's `opendir` failed
  with `FR_INVALID_NAME`; that fallback stays.  When you add a
  syscall that has a donix-private equivalent, check which one musl
  actually uses before assuming they map 1:1.

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
    &st)`) — green.
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

- **`musl_sh`'s argv[0] normalization matches the old newlib shell's
  convention.**  `musl_sh` normalizes `argv[0]` to `0:/NAME.ELF` if
  the token has no `:/`, and passes `argv[1..n]` through verbatim.
  Added at `20260926I`.  This convention shapes the A4 ports: a musl
  `cat`/`echo`/`ls` port must take a bare filename on `argv[1]` and
  prepend `0:/` itself, or the `musl_sh`-to-binary interface breaks.
  `cat` (`20260926N`), `echo` (`20260926M`), and `ls`
  (`20260926V`) all do this.  Any future port that reads a filename
  argument must do the same.

- **A4 ports that enumerate a directory take a bare directory name
  on `argv[1]` and prepend `0:/`.**  The `ls` port
  (`20260926V`) does this.  The musl port accepts an optional
  `argv[1]`, normalizes it to `0:/NAME` if it has no `:/`, and passes
  it to `opendir`.

### Build system

- **Always confirm the .elf is relinked** after touching a source:
  `ls -l apps/NAME.elf` should be newer than the `.o` files.  A stale
  link can hide a real fix for an entire session.  *(Superseded by
  A6: the musl userland build is one-shot `.c → .elf` per file; there
  are no `.o` intermediates.  A `make -C userland/musl` always
  reflects the current source.  If a stale binary is ever suspected,
  `make -C userland/musl clean && make -C userland/musl` rebuilds
  from scratch.)*
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

- **Musl binaries got dramatically smaller** when the switch
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

### Musl userland tree (added 2026-09-27, A6)

- **The tree is `userland/musl/`, at the repo root, not under
  `04_kernel_64bit/`.**  Kernel and userland are distinct layers.
  The kernel build no longer reaches into the userland tree at all;
  the image build invokes `make -C ../userland/musl` as a
  prerequisite.
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
- **`musl_min` is built with `-no-pie` like every other binary.**
  The old heredoc build script built it without the flag (a
  documented inconsistency).  The new Makefile unifies the flags.
  Verified by `readelf -h`: `Type: EXEC`, `Entry point 0x40022b`.
  Runs `MUSL-START` cleanly on the FAT.

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

  Fix: in `process_exit`'s fallback path, if `idle` is a valid PCB
  (pid 1 exists), switch to it via `context_switch(exiting, idle)`
  instead of halting.  Idle `hlt`s until the next timer tick; the
  next keyboard IRQ wakes the shell via `process_wake_all_blocked`,
  and `timer_preempt_handler` picks it up on the following tick.

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
  (`SYS_DONIX_SBRK`).  That private syscall was deleted at A5 step 5
  with the newlib tree.

- **`mprotect` missing** (resolved 2026-09-24, tag `20260924B`).
  musl calls `mprotect` right after `mmap` to set permissions on the
  new region.  The kernel had no case for it, so the dispatcher
  returned `-1` and musl faulted.  Added as a stub returning 0; real
  permission changes (guarding the `PROT_NONE` pages musl requests
  with `MAP_FIXED`) are not implemented.

- **Copy-then-swap `execve` (abandoned)** (investigated 2026-09-24,
  tag `20260924I`).  The first design for in-place `execve` built the
  new address space in a scratch CR3 that shared the caller's kernel
  half, then swapped.  The idea was to preserve the caller's old
  address space on failure (Linux contract).

  It failed.  The scratch CR3 had to share high-half page tables
  with the caller's CR3; `vmm_free_user_page_tables(old_cr3)` then
  freed page-table pages that the new CR3 still referenced, causing
  8 double-frees and a page-fault cascade.

  Fix: **abandoned copy-then-swap.**  The shipped `execve` does
  teardown-then-load directly in `self->cr3`: no scratch, no swap,
  no shared page tables.  Failure after teardown calls
  `sys_exit(-1)` instead of returning `-errno`; the Linux contract
  is not fully honored on those paths, but the paths are unreachable
  for a validated in-memory ELF.

  The two helper functions written for the abandoned design
  (`vmm_clone_kernel_half`, `vmm_free_user_page_tables` in `vmm.c`)
  were unused after `execve` shipped and were deleted at A5 step 8.
  Their source remains in git history if a future COW implementation
  wants to reference them.

- **`musl_printf` / `printnum` dump garbage — kernel bug, fixed at
  `20260924L`.**  This was previously misdiagnosed as a musl-internal
  problem.  It was not.  It was a bug in the kernel's syscall return
  path.

  Symptom: `printf` of a literal or a `%d` value on musl/donix
  printed the correct bytes and then dumped large chunks of the
  binary's own `.rodata` / `.eh_frame`.  The newline from
  `printf("MUSL-PRINTF\n")` was silently dropped.  `ls` looped
  forever printing `FILE     (0 bytes)`.

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

  Corrected fix: restore `%r10` with `pop r10`, do not touch `%rax`
  on the return path, and load the user RSP from the kernel stack
  frame last via `mov rsp, [rsp - 72]`, using no GPR.  Frame layout
  and `process_fork_copy_frame` offsets unchanged.

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
  shell (which reads 1 byte at a time), but a musl program doing
  `read(0, buf, 255)` would block until 255 keystrokes had been
  entered.

  Fix: in `sys_read`'s fd-0 branch, the `continue` after a successful
  `safe_copy_to_user` becomes `break`.  One token changed.  This is
  the same class of bug as the `%r10` clobber.

- **`musl_r10probe` `puthex64` stack buffer overflow** (resolved
  2026-09-26, tag `20260926D`).  `char b[24]` filled with a 29-byte
  string.  Fix: `char b[24]` → `char b[32]`.  One line.

- **Second `execve` from the same `musl_sh` faulted fatally**
  (resolved 2026-09-26, tag `20260926E`).  The actual bug:
  `MSR_FS_BASE` (0xC0000100) is set once by `arch_prctl(ARCH_SET_FS)`
  and was never saved or restored across context switches, making it
  effectively per-CPU instead of per-process.  After the first child
  ran `arch_prctl` with its own TLS base, the parent `musl_sh`'s TLS
  base was clobbered.  On the parent's next `fork`, musl's `fork`
  wrapper loaded `errno` through `%fs`, got a null/stale pointer, and
  faulted.

  Fix, part 1: add `uint64_t fs_base;` to `pcb_t` (appended after
  `file_table`, past `block_kind = 0x158`, so `context_switch.asm`'s
  hardcoded offsets are unchanged), record it in `sys_arch_set_fs`
  and `sys_arch_prctl(ARCH_SET_FS)`, and save/restore around every
  context switch.

  Fix, part 2: `sys_fork` did not inherit the parent's `fs_base`, so
  the child ran with `MSR_FS_BASE = 0`.  Adding
  `child->fs_base = parent->fs_base;` in `sys_fork` closed it.

  Both parts landed in the same commit.

- **`sys_execve` clobbered `argv[1]` for short `argv[0]` values**
  (resolved 2026-09-26, tag `20260926H`).  Root cause: `sys_execve`'s
  argv layout wrote the `envp` NULL terminator at
  `argv_region_bottom + array_bytes`, the same address as
  `strings_start`.  For `argv[0] = "cat"` (4 bytes), the `argv[1]`
  string began inside the 8-byte zeroing window and was clobbered.
  Fix: `strings_start = argv_region_bottom + array_bytes + 8`.

- **`sys_execve` did not pass `argc`/`argv` in `%rdi`/`%rsi`**
  (resolved 2026-09-26, tag `20260926G`).  Fix: write
  `ktop[-12] = argc` and `ktop[-13] = array_base`.

- **`musl_sh`'s `argv[0]` was not normalized** (resolved
  2026-09-26, tag `20260926I`).  Fix: `musl_sh` now builds
  `path = "0:/" + argv[0] + ".ELF"` unless `argv[0]` already
  contains `:/`.

### Open issues

- **`sys_newfstatat` (262) is not implemented.**  `sys_fstat` (5)
  and `sys_stat` (4) are both done and tested.  See "musl `fstatat`
  routing" under "Syscall ABI".  Not on any current test's path.
  Phase B (busybox) may exercise it.
- **`sys_open` accepts non-directories when called with
  `O_DIRECTORY`.**  Observed at `20260926V`: `ls
  0:/hello-world.txt` prints `0 file(s), 0 directory(ies)` and
  exits 0, instead of failing with "not a directory."  FatFs's
  `f_opendir` accepts a file path and yields a `DIR` whose
  `f_readdir` immediately returns "no entries"; `sys_open`'s
  `f_opendir` fallback path does not verify that the target is
  actually a directory.  Fix (deferred): after `f_opendir`
  succeeds, check the entry's `fattrib & AM_DIR`; if not set,
  close and return `-ENOTDIR`.
- **`fcntl` (72) is called by musl's `opendir`.**  Confirmed at
  `20260926V`: musl's `opendir` does
  `open(path, O_RDONLY|O_DIRECTORY)` then
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

- `musl_min` used to be built without `-no-pie` in the old heredoc
  build script.  **Resolved at A6**: the new Makefile builds it with
  `-no-pie` like every other binary.  Verified by `readelf -h`
  (`Type: EXEC`) and by running it in the canary.
- Audit the other `puthex`/`put_dec` helpers in
  `userland/musl/tests/` for tight margins, same as the
  `musl_r10probe` `puthex64` bug fixed at `20260926D`.  Cosmetic
  unless a test starts faulting.
- `SYS_REBOOT` (503) is the only 500+ syscall in
  `syscall_dispatch`.  It is reachable only via raw `syscall(503)`
  from a musl program; there is no musl-side wrapper.  If a user
  program needs to reboot the machine, write a tiny `REBOOT.ELF`
  that issues the raw syscall, or implement Linux `reboot(2)`
  (syscall 169).  Not blocking anything.
- `PMM_ALLOC_DIAG` in `pmm.c` is gated diagnostic code from the
  `20260924B` `brk` investigation; harmless, can be deleted at
  leisure.

## Testing harness

- **Kernel shell:** `k` at boot prompt  (`proclist`, `schstat`, `heapstat`, `selftest`, `fatls`,
  `elfload`).  (`usershell` was removed at A5 step 6.)
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
  to `::/*.ELF` on the FAT via `mcopy_one`.
  **No `/tmp` staging, no external build script.**  To add a
  program: create the source, add its ELF path to `USERLAND_ELFS`
  in `05_boot_kernel64/Makefile`, add the matching `mcopy_one`
  line.  A missing ELF is a hard `make` failure, not a warning.
- **Canary run pattern.**  Boot `musl_sh`, run each test in the
  Part 2 canary table in order, compare against the expected
  output.  Tedious but is what "one change at a time" costs.
  Automating it (a `musl_sh` script-mode that reads commands from
  a file, plus a QEMU wrapper that boots and greps `capture.txt`)
  was considered at `20260926N` and deferred.  Revisit when the
  manual cost grows past the patch cost; A5 and A6 were done
  manually and stayed manageable.

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

**Last updated:** 2026-09-27 (session 11, A6 musl userland tree)
**Current HEAD:** the `v0.6.0` version-bump commit, on branch `dev`.
**Last known-good code tag:** `v0.6.0` (published).
**Disaster preserved at:** branch `disaster-20260923A`
(commit `47262a9`, local only).

## Current milestone

**Session 11 complete: A6 — musl userland source tree.**

The musl userland has been moved out of the heredoc build script and
into a tracked `userland/musl/` tree.  The full canary runs green
from a fresh boot with nothing in `/tmp`.

**Kernel and userland state is functionally identical to
`v0.5.5`:**

- Only Linux x86_64 syscalls plus `SYS_REBOOT` (503) in
  `syscall_dispatch`.
- The FAT has 21 entries; every one is a musl build.
- The boot shell is `musl_sh`, loaded from `0:/MUSL_SH.ELF`.  No
  newlib fallback.
- `make` produces `kernel.bin` and `userland/musl/build/*.elf`
  (the latter invoked as a prerequisite of the image).
- File sizes on the FAT are unchanged from `v0.5.5` (18752 for
  `HELLO.ELF`, 20144 for `MUSL_SH.ELF`, etc.).

The only thing that changed is *where* the userland source lives
and *how* it is built.

**Next milestone: Phase B (busybox).**  See Part 1's "Phase B"
section.  Before starting Phase B, consider the open issues below;
`fcntl` and `isr14_handler` will bite busybox first.

## Session 11 commits, in order

| Tag (deleted) | Commit | What |
|-----|--------|------|
| `20260927-00` | `91f2fb4` | `cleanup: remove residual newlib artifacts from 04_kernel_64bit` — deleted `04_kernel_64bit/user_newlib_linker.ld` (git-tracked) and the untracked `user_shell_data.c`; removed the residual empty `04_kernel_64bit/userland/newlib/` directory. |
| `20260927-01` | `f4769c7` | `A6.1: userland/musl tree, Makefile, and all sources` — created `userland/musl/{Makefile,apps/*,tests/*}`. 21 files, 918 insertions, all new. |
| `20260927-02` | `d74bf97` | `A6.23: build and stage musl userland from userland/musl` — patched `05_boot_kernel64/Makefile` to invoke `make -C ../userland/musl` and copy from `../userland/musl/build/`. 70 insertions, 44 deletions. |
| `20260927-03` | `c346ba0` | `A6.24: delete build_musl_tests.sh (superseded by userland/musl)` — 1421 deletions. |
| *(no tag)* | *(this commit)* | `v0.6.0: bump version banners, update ROADMAP/checklist/handoff` — `kmain.c` version banners set to `donix v0.6.0`; ROADMAP, OSDev_Checklist, and handoff updated for the new tree. Tagged `v0.6.0`. |

## Session 10 commits (retained for history)

| Commit | What |
|--------|------|
| `ed9b024` | Remove `SYS_EXEC_PLAN.md` (design plan preserved in git history at `ecc8ade`). |
| `63f11f7` | `migration-tags.txt` — preserve the A1–A5 working tag map (56 tags). |
| `5fe61c6` | `README.md` — rewrite for donix (musl, Linux syscall ABI). |
| `8c161cd` | `ROADMAP.md` — promote v0.5.5 to its own milestone section. |
| `402d296` | `OSDev_Checklist.md` — add donix migration section, Linux ABI, musl. |
| `bfc8222` | `MAINTENANCE.md` — add donix migration items, close resolved, mark dons-os era. |
| `28b0d60` | `LLD_BUG_REPORT.md` — update for donix; drop stale local-path section. |
| `b08061c` | `handoff.md` — update Current HEAD to the doc-pass tip. |
| `2980852` | `kmain` — rename `DonsDOS` to `donix` in the three version banners.  Tagged `v0.5.5`. |
| `ef4b263` | `handoff.md` — session-10 status update (previous handoff rewrite). |

Publishing (session 10): `git push -u origin main` (1795 objects,
5.19 MiB) and `git push origin --tags` (35 tags) to
`git@github.com:DonaldMoran/donix.git`.

## Session 9 commits (A5 — retained for history)

| Tag | Commit | What |
|-----|--------|------|
| `20260926-01` | `A5 step 1: remove newlib *-elf targets and user-elfs aggregate` | Also dropped `FSTEST`/`MULTITEST`/`BIGTEST.ELF` from the FAT (no musl port).  FAT 24 → 21 entries. |
| `20260926-02` | `A5 step 2: remove SYS_DONIX_SPAWN (507) and sys_spawn` | Nothing in the musl canary called 507. |
| `20260926-03` | `A5 step 3: remove SYS_OPENDIR/SYS_READDIR/SYS_CLOSEDIR (500-502)` | Also removed the dead kernel-side `dons_dirent_t` typedef. |
| `20260926-04` | `A5 step 4: remove SYS_ARCH_SET_FS (504)` | Newlib-only; musl uses `arch_prctl` = 158. |
| `20260926-05` | `A5 step 5: remove SYS_DONIX_SBRK (505)` | Newlib-only; musl uses `brk` = 12. |
| `20260926-06` | `A5 step 6: remove embedded newlib shell and its fallback` | Deleted `user_shell_data.c` (~89 KB), its Makefile rule, and all references in `kmain.c`.  Boot path now panics on FAT read failure.  Kernel shrank by ~88 KB. |
| `20260926-07` | `A5 step 7: delete the newlib userland tree` | Removed `userland/newlib/` whole, the `userland` phony target, `USERLAND_DIR`, `USER_CFLAGS`, and the `all: userland kernel.bin` prerequisite. |
| `20260926-08` | `A5 step 8: remove dead code` | Deleted `syscall.c`, `DEBUG_WRITE_BOUNCE`, the dead CR3 switch in `kmain.c`'s `elfload`, and the unused `vmm_clone_kernel_half` / `vmm_free_user_page_tables`. |

## Canary state (all green as of `20260927-03`; unchanged by A6)

Boot-time shell is `musl_sh`.  The canaries below were run from its
`donix> ` prompt in a single boot, in this order.  The FAT contains
**21** entries.  **All binaries are now built from
`userland/musl/build/` with nothing in `/tmp`.**

| Test | State | Notes |
|------|-------|-------|
| hello | green | `hello from donix (musl)` |
| echo hi | green | `hi` |
| echo a b c d e | green | `a b c d e` |
| cat hello-world.txt | green | file contents printed |
| ls | green | 21 files; sizes match the FAT listing; `Unknown syscall: 72` once |
| memtest | green | `[memtest] PASS` (mmap heap, `0x8010000020`) |
| musl_stat | green | `STAT-OK` and `STAT2-OK` (both `fstat` and `stat`) |
| musl_min | green | `MUSL-START` (built with `-no-pie` for the first time; `readelf -h` confirms `Type: EXEC`, entry `0x40022b`) |
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

- `userland/musl/` — **the musl userland source tree (tracked)**.
  `Makefile`, `apps/*.c` (6), `tests/*.c` (14).  `build/` is
  gitignored and holds the `.elf` outputs.
- `third_party/musl-src/` and `third_party/musl-install/` — the
  musl source and install trees.  **Gitignored.**  Rebuild with
  `./toolchain/install_musl.sh`.  Requires network access for the
  initial clone.
- `toolchain/install_musl.sh` and `toolchain/musl-gcc.sh` — tracked.
- `/tmp/20260923A-working-tree.patch` (614 lines) — plain-text backup
  of abandoned work from the disaster commit.  Can be deleted.
- `/tmp/memtest2.c.bak` (524 bytes) — backup of an untracked test.
- `~/code/x` — snapshot of the pre-cleanup tree, kept for diffing.
  Can be deleted.
- `notes_musl.txt` was moved out of the tree at `20260926P` (now at
  `/tmp/notes_musl.txt`).
- `04_kernel_64bit/kmain.c` has the two helpers,
  `load_file_to_buffer` and `load_elf_into_user_process`.

**No longer on disk:**
- `build_musl_tests.sh` — deleted at A6.24.
- `04_kernel_64bit/user_newlib_linker.ld` — deleted in the A6
  cleanup commit.
- `04_kernel_64bit/user_shell_data.c` — deleted in the A6 cleanup
  commit (was untracked).
- `04_kernel_64bit/userland/` — the residual empty directory was
  removed in the A6 cleanup commit (was untracked).
- `/tmp/musl_*` and `/tmp/*_musl` — no longer produced or used by
  anything.  `/tmp` non-persistence is no longer a concern.

## Next step (exactly this, then stop)

**Session 12 starts Phase B (busybox).**

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

See `MAINTENANCE.md` §0 for the full list with effort estimates.

**Do not push without a plan.**  `dev` currently carries the A6
work plus the `v0.6.0` tag.  Whether Phase B lands on `dev` only,
gets merged to `main` at the next milestone, or is pushed
immediately is a separate decision.  The tagging convention says
milestone tags only on the published side; the same principle
applies to Phase B.

## Open items

- **Phase B (busybox):** the next milestone.  See Part 1's
  "Phase B" section and the "Next step" above.
- **Open issues to chase, in priority order, before or during
  early Phase B:**
  - `fcntl` (72) is unimplemented; musl's `opendir` calls it.
    Busybox will call `fcntl` for `F_SETFD`/`F_GETFD`/`F_DUPFD`.
    A minimal stub removes the noise and unblocks common paths.
  - `sys_newfstatat` (262) is unimplemented.  Busybox may hit
    it via `fstatat(fd, path, st, flags)`.
  - `isr14_handler` halts on user-mode faults; should terminate
    the faulting process instead.  This will bite the first time
    busybox segfaults.
  - `sys_open` accepts non-directories when called with
    `O_DIRECTORY`.  Fix in `sys_open`'s fallback path.
  - `Unknown syscall: N` fires during `musl_readdir` (numbers 6,
    7, 8, 15, 17, 72).  `72` is traced to `opendir`'s `fcntl`.
    The others are not on any current hot path.
  - `musl_sh` echoes garbage on lines containing backspaces
    (cosmetic).
- **Deferred cleanups:**
  - Audit `puthex`/`put_dec` helpers in `userland/musl/tests/`
    for tight margins, same as the `musl_r10probe` `puthex64`
    bug fixed at `20260926D`.  Cosmetic unless a test starts
    faulting.
  - `PMM_ALLOC_DIAG` in `pmm.c` is inert diagnostic code from the
    `20260924B` `brk` investigation; can be deleted at leisure.
  - Consider writing a small `REBOOT.ELF` musl binary (raw
    `syscall(503)`) so userland can reboot the machine, since the
    newlib shell's `reboot` command is gone with A5 step 6.

## How to use this file

At the start of a new session, paste the entire file and say:

> "Continue from here. What's the next step?"

At the end of a session, update **Part 2** (session status) with the
current tag, canary state, and next step. Part 1 (strategy) only
changes if the plan itself changes.
