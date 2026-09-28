### donix — what's next

donix is at **v0.6.2**: it speaks the Linux x86_64 syscall ABI, runs
static musl-linked binaries, and runs **busybox** — including its
interactive `ash` shell — on top of a from-scratch kernel.

This file is **future work only**. For the current state of the
project, see [`handoff.md`](handoff.md). For how donix got here, see
[`docs/migration-history.md`](docs/migration-history.md) (the musl
migration) and [`docs/dons-os-history.md`](docs/dons-os-history.md)
(the pre-fork dons-os story).

---

## Phase B — busybox against musl (done at v0.6.2)

**Status:** complete.

The kernel speaks the Linux ABI, runs static musl binaries, and now
runs **busybox 1.36.1** against musl 1.2.5. `busybox ash` is an
interactive shell: prompt, echo, backspace, line editing. It forks
and execs external binaries via `PATH`; its own applets (`busybox
ls`, `busybox echo`) run in-process.

What landed, in order:

1. busybox integrated into the userland build; config tracked at
   `configs/busybox.config`.
2. `fcntl(2)` (with `F_DUPFD`), `dup2(2)` with refcounted slots,
   `setsid`, `getppid`, `getcwd`, `access`/`faccessat`, and a
   bare-name retry in `sys_execve`.
3. Proper negative errnos from file syscalls; `FR_NO_PATH` added to
   the stat retry so `ash`'s PATH probe works.
4. `sys_fork` copies the ELF image region so a fork child's
   pre-`execve` writes don't clobber the parent's `.data`.
5. `sys_ioctl` learned `TCGETS`/`TCSETS*`/`TIOCGWINSZ`; busybox ash
   probes `TIOCGWINSZ`, not `TCGETS`, to decide stdin is a tty.
6. `FEATURE_EDITING=y` in the busybox config; `lineedit.c` does the
   shell's own echo.
7. The musl `ls` port learned to stat its argument before calling
   `opendir`, so `ls <file>` works in both shells.

For the narrative, see the `v0.6.2` annotated tag message and the
session log in [`handoff.md`](handoff.md).

---

## Next: Phase C — broaden busybox coverage

**Status:** planned, not started.

Now that busybox runs as a shell, the next questions are:

1. **What happens when busybox prefers its own applets?** Currently
   `CONFIG_FEATURE_PREFER_APPLETS` is off, so `ash` execs external
   binaries from `PATH` — i.e. the project's own `LS.ELF`,
   `ECHO.ELF`, `CAT.ELF` — rather than busybox's internal applets.
   Flipping it on exercises busybox's own code much harder and
   reveals how much of the syscall surface is really covered. This
   is a *broad* change and would re-test the whole canary; treat it
   as a milestone candidate on its own.
2. **Which busybox applets work?** `ls`, `echo`, `cat` work. `mkdir`,
   `rm`, `cp`, `mv`, `grep`, `sed`, `awk`, `tar` have not been tried.
3. **Job control.** busybox ash has `ASH_JOB_CONTROL` off in the
   current config. Enabling it needs signal delivery, process
   groups, and a foreground/background distinction — none of which
   the kernel has today.

### Approach

Work through the applet list one at a time, watching for
`Unknown syscall: N` in the kernel log. Each unimplemented syscall is
a candidate for its own commit. Prefer broad, easy applets first
(`mkdir`, `rm`, `cp`) and save anything requiring new subsystems
(`ps`, `top`, `kill`) for after the kernel has those subsystems.

---

## After Phase C

Small, independent follow-ups, roughly in priority order.

- **`sys_mkdir` (7)** — busybox ash's line editor calls it once per
  keystroke. Cosmetic; ~15 lines with FatFs's `f_mkdir`. See the
  handoff's open issues.

### Syscalls busybox will need

- **`sys_newfstatat` (262)** — `fstatat(fd, path, st, flags)`.
  Delegates to `sys_stat` when `dirfd == AT_FDCWD` or the path is
  absolute; returns `-ENOSYS` otherwise until there is a per-process
  cwd.
- **`sys_open` `O_DIRECTORY` fix** — check `fattrib & AM_DIR` in the
  `wants_dir` branch; return `-ENOTDIR` if the target is a file.
  Kernel-side latent (the musl `ls` port no longer triggers it).
- **`isr14_handler` user-mode fault handling** — terminate the
  faulting process instead of halting the console. Busybox's first
  segfault will bite this.

### Shell features

- **Pipes and redirection** — `cat file > out.txt`,
  `cat file | grep foo`. Needs `pipe(2)` and `dup2(2)` (the latter
  already works).
- **`cd` / relative paths** — `chdir` + per-process cwd. FatFs
  already supports `f_chdir`.
- **Environment variables** — extend the argv mechanism with an
  `envp` array; `getenv`/`setenv` on the userland side.

### Kernel hardening

- **ELF loader `PT_NX` follow-up** — with `EFER.NXE` enabled, mark
  data/BSS/stack segments non-executable.
- **Page-table teardown on process exit** — walk and free the
  user-space portion in `process_reclaim`.
- **Real copy-on-write for `fork`** — the eager copy is O(6 MB) per
  fork. Long-term fix: mark shared PTEs read-only, install a `#PF`
  handler that copies on write. Every busybox applet that forks
  makes the current cost more visible.
- **Kernel log routing** — route `sys_execve` and `sys_open`
  diagnostics to serial only, or add a `SYS_KLOG(level)` syscall.

### Testing infrastructure

- **Boot-time self-test mode** (`-DSELFTEST`) — run the existing 17
  tests at boot and halt.
- **`make test` target** — boot QEMU headless, run the self-test,
  grep the serial log.
- **Spawn regression test** — a kernel-mode child that spawns
  `HELLO.ELF`, waits, and asserts exit status 0.
- **argv / REPL regression tests** — scripted `echo`/`cat`/`ls`/
  `fstest --verify` sequences.

### Longer term

- **Per-process tty / console focus** — prerequisite for multiple
  concurrent shells. Also the natural point to build the ring-buffer
  console (see `docs/MAINTENANCE.md` §4e).
- **Serial console debug access** — kernel shell over COM1,
  physically separate from the user keyboard.
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
