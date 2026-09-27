# The musl migration

### How donix went from dons-os (newlib) to musl

This is the story of A1–A6: the syscall renumbering, the syscalls added
for musl, the musl shell, the userland ports, the retirement of newlib,
and the extraction of the userland into a tracked source tree.

Each phase is complete. For donix's current state, see
[`../handoff.md`](../handoff.md). For the pre-fork dons-os story, see
[`dons-os-history.md`](dons-os-history.md).

---

## The migration (A1–A6)

donix was forked from dons-os at its `v0.5.4` tag. The goal was to
re-target the OS to **static musl-linked binaries** and speak the
**Linux x86_64 syscall ABI natively**, without disturbing the kernel
infrastructure that dons-os had built.

This is the work that produced donix, and it is captured by the completion tag **`v0.5.5`** (the musl migration) and **`v0.6.0`**
(the musl userland source tree, phase A6). It was done in six phases, all complete.

### A1 — pure syscall renumbering

Change syscall numbers to Linux x86_64 numbers. No semantics change, no
new syscalls. Newlib's existing userland stayed as a passive regression
canary after every subsequent change. Complete at `20260922H`.

### A2 — add the syscalls musl needs

Added, one at a time, each its own commit, tested with a tiny musl
binary and the newlib canary re-run after each:

- `arch_prctl(ARCH_SET_FS)` = 158
- `set_tid_address` = 218
- `rt_sigaction` = 13 (stub)
- `rt_sigprocmask` = 14
- `set_robust_list` = 273
- `ioctl` = 16 (`TCGETS`, `-ENOTTY` otherwise)
- `brk` = 12 (Linux absolute-address ABI)
- `mmap` = 9, `munmap` = 11, `mprotect` = 10
- `getrandom` = 318, `rseq` = 334 (stubs)
- `fork` = 57
- `execve` = 59 (in-place; spawn moved off 59 to a donix-private 507
  and later deleted at A5 step 2)
- `wait4` = 61
- `getdents64` = 217
- `stat` = 4, `fstat` = 5 (added at the A4 `ls` port)

Complete at `20260924K`, extended with `stat`/`fstat` at `20260926T`.

### A3 — musl shell

A minimal musl shell (`musl_sh`) using `fork` + `execve` + `wait4`
replaced the newlib shell as the default boot shell. Path normalization
and tokenization were added incrementally. Complete at `20260926J`.

### A4 — userland apps ported to musl

`hello`, `echo`, `cat`, `ls`, `memtest`. Each was built in parallel
alongside its newlib original first, tested individually, then cut over
in a single Makefile change. The parallel-then-cut-over pattern kept the
newlib canary green throughout. Complete at `20260926Y`.

### A5 — retire newlib

The newlib userland, build rules, and libraries were removed in eight
steps, each its own commit and canary run:

1. newlib `*-elf` targets and `user-elfs` aggregate
2. `SYS_DONIX_SPAWN` (507) and `sys_spawn`
3. donix-private directory syscalls (500–502)
4. `SYS_ARCH_SET_FS` (504)
5. `SYS_DONIX_SBRK` (505)
6. the embedded newlib shell and its fallback
7. the entire `userland/newlib/` tree
8. leftover dead code

Complete at `20260926-08`.

### A6 — musl userland source tree

The musl userland was moved out of the heredoc-based
`build_musl_tests.sh` and into a tracked source tree at
`userland/musl/`. The C sources do not change; only where they live
and how they are built. Complete at `20260927-03`, tagged `v0.6.0`
after the doc pass.

- `userland/musl/{apps,tests}/` hold the sources; `Makefile` builds
  each `.c` into `build/*.elf`.
- `05_boot_kernel64/Makefile` invokes `make -C ../userland/musl` and
  stages the resulting ELFs onto the FAT.
- `build_musl_tests.sh` deleted.
- `musl_min` now built with `-no-pie` like every other binary.
- Residual newlib artifacts under `04_kernel_64bit/`
  (`user_newlib_linker.ld`, `user_shell_data.c`, and the empty
  `userland/newlib/` directory) removed in a separate cleanup commit.

### What `v0.5.5` means

At `v0.5.5`, the kernel speaks Linux x86_64 syscalls, the shell is
`musl_sh`, the userland C library is musl 1.2.5 built from source into a
project-local tree, and the newlib userland has been fully retired.

**What remains in the tree after the migration:**

- `SYS_REBOOT` (503) is the only 500+ syscall number left in
  `syscall_dispatch`.
- The only userland is musl: the FAT has `HELLO.ELF`, `ECHO.ELF`,
  `CAT.ELF`, `LS.ELF`, `MEMTEST.ELF`, `MUSL_SH.ELF`, and the musl
  test binaries, all built against the project-local musl 1.2.5.
- The only boot shell is `musl_sh`, loaded from `0:/MUSL_SH.ELF`.
- `make` produces `kernel.bin` only. No recursive userland build.

The 56 working tags used during the migration (`20260922A` through
`20260926-09`) are recorded in
[`migration-tags.txt`](migration-tags.txt) with their commit SHAs. The
release tags inherited from dons-os (`v0.0.1` through `v0.5.4`) remain
as real git tags; `v0.5.5` and `v0.6.0` are the donix completion tags.

### Bugs found and fixed during the migration

The A1–A5 work surfaced a batch of Linux-ABI mismatches and one
ordering bug that would not have been visible on the dons-os-only
kernel. Each is documented in [`handoff.md`](handoff.md) under
"Resolved bugs":

- **Syscall return path clobbered `%r10`** — the Linux ABI says only
  `%rax`, `%rcx`, `%r11` are clobbered by `syscall`. musl's stdio keeps
  a live pointer in `%r10` across `writev`. Fixed at `20260924L`.
- **`sys_read` on fd 0 blocked until `count`** — POSIX returns on the
  first available byte. Fixed at `20260926A`.
- **`brk` used increment semantics** instead of Linux's absolute
  address. Fixed at `20260924B`; newlib's increment-based `sbrk` moved
  to a private syscall (later deleted at A5 step 5).
- **`MSR_FS_BASE` was per-CPU, not per-process** — musl reads `%fs:0`
  on the child's first instruction after `fork`. Fixed by saving,
  restoring, and inheriting it. Fixed at `20260926E`.
- **`execve` did not pass `argc`/`argv` in `%rdi`/`%rsi`** — newlib's
  `crt0.S` reads them from registers. Fixed at `20260926G`.
- **`execve`'s argv layout zeroed `argv[1]`** for short `argv[0]`
  values. Fixed at `20260926H`.

The migration also produced a working `fork`, a working `execve`, a
working `wait4`, and a working `getdents64` — each of which the A2
phase developed against a specific musl test binary, with the newlib
canary re-run after every change.

**Design notes.** The original design plan for the dons-os-era
`SYS_EXEC` feature — spawn vs. execve, parent/child tracking, the
reap-vs-zombify decision, the kmalloc-whole-file load approach — is
preserved in git history at `ecc8ade`:

```
git show ecc8ade:SYS_EXEC_PLAN.md
```

It describes the newlib-era implementation (syscall numbers 8 and 9,
the `arc2/syscalls.c` shims) and is **not** updated for donix's Linux
ABI (`execve` = 59, `wait4` = 61).

---


---

## Appendix A — Phase A milestones (A1–A5, from handoff.md)

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


---

## Appendix B — musl build

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


---

## Appendix C — Resolved bugs

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

