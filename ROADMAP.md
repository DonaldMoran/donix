# ROADMAP

### donix — what's next

donix is at **v0.6.0**: it speaks the Linux x86_64 syscall ABI, runs
static musl-linked binaries, and builds its userland from a tracked
source tree at `userland/musl/`.

This file is **future work only**. For the current state of the
project, see [`handoff.md`](handoff.md). For how donix got here, see
[`docs/migration-history.md`](docs/migration-history.md) (the musl
migration) and [`docs/dons-os-history.md`](docs/dons-os-history.md)
(the pre-fork dons-os story).

---

## Next: Phase B — busybox against musl

**Status:** planned, not started.

The kernel speaks the Linux ABI and runs static musl binaries. The
next milestone is running a real Unix userland: a static **busybox**
linked against the project-local musl 1.2.5.

### Approach

1. Build busybox against the project-local musl:

   ```
   cd third_party
   git clone https://git.busybox.net/busybox
   cd busybox
   git checkout 1_36_stable        # or whatever the current stable is
   make defconfig
   # Set CONFIG_STATIC=y, CONFIG_PREFIX=/tmp/busybox-install
   # Use toolchain/musl-gcc.sh as CC
   make -j$(nproc)
   make install
   ```

   The result is `_install/bin/busybox`, a static musl-linked ELF.

2. Stage it: copy to `userland/musl/build/busybox.elf`, add that path
   to `USERLAND_ELFS` in `05_boot_kernel64/Makefile`, and add a
   matching `mcopy_one` line so it lands on the FAT as
   `BUSYBOX.ELF`.

3. From `musl_sh`: `busybox echo hello` (or the correct argv layout —
   busybox expects `argv[0]` to be the applet name).

4. Watch the `Unknown syscall: N` output. Every unimplemented syscall
   busybox hits is a candidate for the next kernel commit.

### What to expect

Busybox at startup does much more than the current canary tests: it
installs signal handlers, reads `/proc` or sysfs, checks terminal
settings, and uses `fcntl`, `getuid`, `getgid`, `geteuid`, `getegid`,
`umask`, `rt_sigreturn`, `prctl`, `setrlimit`/`getrlimit`, `uname`, and
possibly `access`/`faccessat`. Each of those is a small implementation.

Plan Phase B as a sequence of small commits:

1. Get busybox to link (build-script change only, probably).
2. Get it to reach `main` (may need new syscalls).
3. Get one applet (`echo` is easiest) to work.
4. Get a real applet (`ls`, `cat`) working.

Each is its own commit with the existing canary re-run.

---

## After Phase B

Small, independent follow-ups, roughly in priority order.

### Syscalls busybox will need

- **`fcntl` (72)** — `F_SETFD`, `F_GETFD`, `F_DUPFD`. A minimal stub
  returns 0 for flag-set operations and the fd for `F_DUPFD`.
- **`sys_newfstatat` (262)** — `fstatat(fd, path, st, flags)`.
  Delegates to `sys_stat` when `dirfd == AT_FDCWD` or the path is
  absolute; returns `-ENOSYS` otherwise until there is a per-process
  cwd.
- **`sys_open` `O_DIRECTORY` fix** — check `fattrib & AM_DIR` in the
  fallback path; return `-ENOTDIR` if the target is a file.
- **`isr14_handler` user-mode fault handling** — terminate the
  faulting process instead of halting the console. Busybox's first
  segfault will bite this.

### Shell features

- **Pipes and redirection** — `cat file > out.txt`,
  `cat file | grep foo`. Needs `pipe(2)` and `dup2(2)`.
- **`cd` / relative paths** — `chdir` + per-process cwd. FatFs already
  supports `f_chdir`.
- **Environment variables** — extend the argv mechanism with an `envp`
  array; `getenv`/`setenv` on the userland side.

### Kernel hardening

- **ELF loader `PT_NX` follow-up** — with `EFER.NXE` enabled, mark
  data/BSS/stack segments non-executable.
- **Page-table teardown on process exit** — walk and free the
  user-space portion in `process_reclaim`.
- **Kernel log routing** — route `sys_execve` and `sys_open`
  diagnostics to serial only, or add a `SYS_KLOG(level)` syscall.

### Testing infrastructure

- **Boot-time self-test mode** (`-DSELFTEST`) — run the existing 17
  tests at boot and halt.
- **`make test` target** — boot QEMU headless, run the self-test, grep
  the serial log.
- **Spawn regression test** — a kernel-mode child that spawns
  `HELLO.ELF`, waits, and asserts exit status 0.
- **argv / REPL regression tests** — scripted `echo`/`cat`/`ls`/
  `fstest --verify` sequences.

### Longer term

- **Per-process tty / console focus** — prerequisite for multiple
  concurrent shells. Also the natural point to build the ring-buffer
  console (see `docs/MAINTENANCE.md` §4e).
- **Serial console debug access** — kernel shell over COM1, physically
  separate from the user keyboard.
- **Framebuffer graphics** — move off VGA text mode.
- **VFS layer** — virtual filesystem above FatFs, with mount points
  and path resolution.
- **Device drivers** — PCI enumeration, AHCI, PS/2 mouse.

---

## Notes

Feature work goes here. The current state of the project and the
session-by-session history are in [`handoff.md`](handoff.md). The
pre-fork dons-os history and the musl migration are in
[`docs/`](docs/).

MIT licensed — contributions welcome.
