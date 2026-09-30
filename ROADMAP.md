### donix — what's next

donix speaks the Linux x86_64 syscall ABI, runs static musl-linked
binaries, and boots straight into **busybox `ash`** on top of a
from-scratch kernel. `cd`, `pwd`, `ls`, and `cat` respect a real
per-process working directory, in both shells, across `fork` and
`execve`. The console is a VT100 emulator, so full-screen software
runs: `vi` edits a file, `:wq` saves it, `cat` reads it back.
Files and directories can be created and removed; a faulting
process is killed cleanly.

This file is **future work only**. For the current state of the
project, see [`handoff.md`](handoff.md). For how donix got here, see
[`docs/migration-history.md`](docs/migration-history.md) (the musl
migration) and [`docs/dons-os-history.md`](docs/dons-os-history.md)
(the pre-fork dons-os story).

---

## Done — Phases A through B, and v0.6.5

For the record, so this file does not re-plan finished work:

- **Phase A** — boot chain, long mode, paging, heap, scheduler, ELF
  loader, ATA, FAT16. Carried forward from dons-os.
- **Phase B** — Linux x86_64 syscall ABI; static musl binaries;
  busybox 1.36.1 running as the boot shell (`ash` interactive, applets
  in-process via standalone mode, `/bin/busybox` on the image).
- **v0.6.3** — a real working directory. `chdir`/`getcwd`, relative
  path resolution in `sys_open`/`sys_stat`/`sys_access`, cwd across
  `fork` and `execve`, `ls`/`cat` path pass-through, and `cd`/`pwd`
  builtins in `musl_sh`.
- **v0.6.4 — the basics.** Full VT100/ANSI emulation in `vga.c`
  (CSI parsing, cursor addressing, SGR, erase/insert/delete, a
  software alternate screen); `keyboard.c` delivers ESC, DEL, and
  CR the way Unix software expects; correct Linux `open(2)` flag
  translation plus `ftruncate`, `utimes`, `futimesat`, and
  `utimensat`; `rm` and `rmdir`; `cd ..` at `donix>`; a user-mode
  `#PF` (and ring-3 `#GP`) kills the process rather than the kernel.
- **v0.6.5 — scripts, redirection, and a real fd layer.** Shell
  scripts run (`MAX_PROCESS_FILES` 8 → 64, `execve` returns
  `ENOEXEC` for a non-ELF so ash falls back to the interpreter, and
  `./script.sh` normalizes). Shell redirection works end to end:
  `cmd < file`, `cmd > file`, `2>`, and repeated redirects in one
  shell all behave. Four new syscalls — `uname` (63), `lseek` (8),
  `rename` (82), `readv` (19) — and the busybox file/text utilities
  are enabled: `head`, `tail`, `cp`, `mv`, `grep`, `sed`, `cut`,
  `sort`, `stat`, `tee`, `test`, `tr`, `cmp`, `od`, `uniq`. Finally,
  fds 0/1/2 are first-class: console sentinels so a fresh process's
  `open` returns fd 3, lowest-free-fd allocation so `close(0);
  open(file)` returns fd 0, and `dup2`/`fcntl(F_DUPFD)` accept low
  fds so a redirect whose scratch fd is 0/1/2 works.

The narratives are in the `v0.6.4` and `v0.6.5` annotated tags,
`docs/session-log.md`, and `handoff.md`.

---

## After v0.6.4: pick a direction

The basics work and full-screen software runs. What comes next is a
set of small, independent items and several larger subsystem
questions. Pick one, do it, test it, tag it — the project's
one-change-at-a-time discipline applies.

### Small, close gaps

- **`newfstatat` (262).**  Number reserved, no dispatch case.  musl
  routes `fstatat` through `stat`/`lstat` on x86_64 for the common
  case, so it is not hit yet; a caller passing `AT_FDCWD` plus flags
  would reach it.  Delegates to `sys_stat` when `dirfd == AT_FDCWD`
  or the path is absolute; with cwd resolution now in `sys_stat`,
  this is a small wrapper rather than a stub.
- **`sys_open` `O_DIRECTORY` fix.**  In the `wants_dir` branch, check
  `fattrib & AM_DIR` and return `-ENOTDIR` when the target is a file.
  Latent today (nothing triggers it), but correct to close.
- **`sys_utimensat` cwd resolution.**  Found in session 32: it calls
  `strip_dot_prefix` but not `resolve_against_cwd`, like `sys_unlink`
  and `sys_mkdir` did before session 32 fixed them.  Same one-line
  fix.
- **Ctrl-`[` as ESC.**  Deferred during the terminal work;
  `scancode_to_ascii` has no fourth parameter for Ctrl state yet.
  The literal ESC key is enough for vi, but terminal users expect the
  alias.

### Broaden busybox coverage

Busybox is the boot shell now, and the file/text utility set is
broad: `ls`, `echo`, `cat`, `pwd`, `wc`, `head`, `tail`, `cp`, `mv`,
`grep`, `sed`, `cut`, `sort`, `stat`, `tee`, `test`, `tr`, `cmp`,
`od`, `uniq`, `mkdir`, `touch`, `vi`, `rm`, `rmdir`.  Still untried:
`awk`, `tar`, and `find` (which needs `newfstatat`).  These are where
the syscall surface gets tested hardest.

Work through them one at a time, watching for `Unknown syscall: N` in
the serial log.  Each missing syscall is its own commit.

`ps`, `top`, `kill`, and job control need subsystems the kernel does
not have yet (process introspection, signal delivery, process groups)
and should wait for those.

### Shell features

- **Pipes** — `cat file | grep foo`.  Redirection (`<`, `>`, `2>`)
  works as of `v0.6.5`; the pipe is the remaining piece and needs
  `pipe(2)` (`dup2(2)` already works).  This is the biggest gap in
  the shell.
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
- **Real FatFs timestamp storage.**  The three timestamp syscalls
  (`utimes`, `futimesat`, `utimensat`) return 0 without storing
  anything.  Enough for `touch` and vi; not enough for a tool that
  reads timestamps back.

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
