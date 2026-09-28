### donix — what's next

donix is at **v0.6.3**: it speaks the Linux x86_64 syscall ABI, runs
static musl-linked binaries, and boots straight into **busybox `ash`**
on top of a from-scratch kernel. `cd`, `pwd`, `ls`, and `cat` respect
a real per-process working directory, in both shells, across `fork`
and `execve`.

This file is **future work only**. For the current state of the
project, see [`handoff.md`](handoff.md). For how donix got here, see
[`docs/migration-history.md`](docs/migration-history.md) (the musl
migration) and [`docs/dons-os-history.md`](docs/dons-os-history.md)
(the pre-fork dons-os story).

---

## Done — Phases A through B, and v0.6.3

For the record, so this file does not re-plan finished work:

- **Phase A** — boot chain, long mode, paging, heap, scheduler, ELF
  loader, ATA, FAT16. Carried forward from dons-os.
- **Phase B** — Linux x86_64 syscall ABI; static musl binaries;
  busybox 1.36.1 running as the boot shell (`ash` interactive, applets
  in-process via standalone mode, `/bin/busybox` on the image).
- **v0.6.3** — a real working directory. `chdir`/`getcwd`, relative
  path resolution in `sys_open`/`sys_stat`/`sys_access`, cwd across
  `fork` and `execve`, `ls`/`cat` path pass-through, and `cd`/`pwd`
  builtins in `musl_sh`. The serial log is free of `Unknown syscall:`
  lines.

The narratives are in the `v0.6.3` annotated tag, `docs/session-log.md`,
and `handoff.md`.

---

## Next: pick a direction

The shell is usable. What comes next is a set of small, independent
items and two larger subsystem questions. Pick one, do it, test it,
tag it — the project's one-change-at-a-time discipline applies.

### Small, close gaps

- **`cd ..` at `donix>`.**  The `musl_sh` `cd` builtin passes the raw
  `..` to `chdir`, and FatFs has no `..` entry, so it fails.  Two
  clean fixes: (a) `builtin_cd` resolves `.`/`..` against `getcwd()`
  before calling `chdir`; (b) `sys_chdir` resolves through
  `resolve_against_cwd` the way the other path syscalls now do.
  (b) is more Unix-shaped.  `cd ..` inside ash already works.
- **User-mode `#PF` test binary.**  `fault_kill_current(0x0E)` in
  `isr14_handler` is in but unverified end-to-end.  A test binary
  that dereferences a bad pointer without setting `g_expect_fault`
  closes the loop.  This is the item the ROADMAP used to call "the
  `isr14` fault path"; the handler exists, the test does not.
- **`newfstatat` (262).**  Number reserved, no dispatch case.  musl
  routes `fstatat` through `stat`/`lstat` on x86_64 for the common
  case, so it is not hit yet; a caller passing `AT_FDCWD` plus flags
  would reach it.  Delegates to `sys_stat` when `dirfd == AT_FDCWD`
  or the path is absolute; it can now resolve relative paths against
  cwd, so this is a small wrapper rather than a stub.
- **`sys_open` `O_DIRECTORY` fix.**  In the `wants_dir` branch, check
  `fattrib & AM_DIR` and return `-ENOTDIR` when the target is a file.
  Latent today (nothing triggers it), but correct to close.

### Broaden busybox coverage

Busybox is the boot shell now, but only a handful of applets have
been exercised: `ls`, `echo`, `cat`, `pwd`, `wc`, `mkdir`.  The
untried ones — `rm`, `rmdir`, `cp`, `mv`, `grep`, `sed`, `awk`,
`tar` — are where the syscall surface gets tested hardest.

Work through them one at a time, watching for `Unknown syscall: N` in
the serial log.  Each missing syscall is its own commit.  The write
path (`rm`, `rmdir`) is worth doing early: it would let `mkdir` become
a standing canary row (paired mkdir + rmdir), which today it cannot
be because nothing cleans up after it.

`ps`, `top`, `kill`, and job control need subsystems the kernel does
not have yet (process introspection, signal delivery, process groups)
and should wait for those.

### Shell features

- **Pipes and redirection** — `cat file > out.txt`,
  `cat file | grep foo`.  Needs `pipe(2)` and `dup2(2)` (the latter
  already works).
- **Environment variables** — extend the argv mechanism with an
  `envp` array; `getenv`/`setenv` on the userland side.
- **Job control** — busybox ash has `ASH_JOB_CONTROL` off in the
  current config.  Enabling it needs signal delivery, process groups,
  and a foreground/background distinction -- none of which the kernel
  has today.

### Larger subsystem questions

- **VFS layer.**  `sys_execve` resolves paths through a three-attempt
  block in the kernel (leading `/` -> `0:` + path; then `0:/NAME.ELF`,
  `0:/BIN/NAME`, `0:/BIN/NAME.ELF`), and `resolve_against_cwd` is a
  per-syscall helper.  Both are stand-ins for a virtual filesystem.
  When a VFS lands, delete the shim and resolve once.  Do not add a
  fourth exec attempt; build the VFS.  See `docs/open-issues.md`.

---

## Kernel hardening and infrastructure

Independent of the shell work.  Roughly in order of value.

- **Real copy-on-write for `fork`.**  The eager copy is O(~6 MB) per
  fork.  Mark shared PTEs read-only and copy on write in a `#PF`
  handler.  Every busybox applet that forks makes the current cost
  more visible.
- **Page-table teardown on process exit.**  Walk and free the
  user-space portion in `process_reclaim`.
- **ELF loader `PT_NX` follow-up.**  With `EFER.NXE` enabled, mark
  data/BSS/stack segments non-executable.
- **`sys_munmap`.**  Currently a stub returning 0.  Needed before any
  real memory-releasing workload.
- **`sys_brk` heap base and the mmap window.**  Both are fixed
  addresses (`0x8000200000` and a 4 MB window); latent collisions.
- **Kernel log routing.**  Route `sys_execve`/`sys_open` diagnostics
  to serial only, or add a `SYS_KLOG(level)` syscall.  The trace
  lines are informational today but will get noisy as more runs.

### Testing infrastructure

- **Boot-time self-test mode** (`-DSELFTEST`) — run the existing test
  binaries at boot and halt.
- **`make test` target** — boot QEMU headless, run the self-test,
  grep the serial log.
- **Spawn regression test** — a kernel-mode child that spawns
  `HELLO.ELF`, waits, and asserts exit status 0.
- **Scripted canary** — a `capture.txt` diff against a known-good
  boot log, so a regression is caught mechanically rather than by eye.

### Longer term

- **Per-process tty / console focus** — prerequisite for multiple
  concurrent shells.  Also the natural point to build the ring-buffer
  console (see `docs/MAINTENANCE.md` §4e).
- **Serial console debug access** — kernel shell over COM1,
  physically separate from the user keyboard.
- **Framebuffer graphics** — move off VGA text mode.
- **Device drivers** — PCI enumeration, AHCI, PS/2 mouse.

---

## Notes

Feature work goes here.  The current state of the project and the
session-by-session history are in [`handoff.md`](handoff.md).  The
pre-fork dons-os history and the musl migration are in
[`docs/`](docs/).

MIT licensed — contributions welcome.
