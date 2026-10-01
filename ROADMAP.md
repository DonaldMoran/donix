### donix — what's next

donix speaks the Linux x86_64 syscall ABI, runs static musl-linked
binaries, and boots straight into **busybox `ash`** on top of a
from-scratch kernel. `cd`, `pwd`, `ls`, and `cat` respect a real
per-process working directory, in both shells, across `fork` and
`execve`. The console is a VT100 emulator on a **1024×768 linear
framebuffer** with Terminus 10×18 text, so full-screen software
runs: `vi` fills the screen, edits, and saves. Pipelines work:
`cat file | head`, `echo hi | wc` — from `ash` and from donix's own
`donix>` shell. Files and directories can be created and removed; a
faulting process is killed cleanly.

This file is **future work only**. For the current state of the
project, see [`handoff.md`](handoff.md). For how donix got here, see
[`docs/migration-history.md`](docs/migration-history.md) (the musl
migration) and [`docs/dons-os-history.md`](docs/dons-os-history.md)
(the pre-fork dons-os story).

---

## Done — Phases A through B, v0.6.6, sessions 37 and 38

For the record, so this file does not re-plan finished work:

- **Phase A** — boot chain, long mode, paging, heap, scheduler, ELF
  loader, ATA, FAT16. Carried forward from dons-os.
- **Phase B** — Linux x86_64 syscall ABI; static musl binaries;
  busybox 1.36.1 running as the boot shell (`ash` interactive, applets
  in-process via standalone mode, `/bin/busybox` on the image).
- **v0.6.3** — a real working directory.
- **v0.6.4 — the basics.** Full VT100/ANSI emulation in `vga.c`;
  `keyboard.c` delivers ESC/DEL/CR; correct Linux `open(2)` flags
  plus `ftruncate`, `utimes`, `futimesat`, `utimensat`; `rm`/`rmdir`;
  user-mode `#PF`/`#GP` kills the process.
- **v0.6.5 — scripts, redirection, a real fd layer.** Scripts run;
  redirection works end to end; four syscalls (`uname`, `lseek`,
  `rename`, `readv`); more busybox applets; fds 0/1/2 first-class.
- **v0.6.6 — pipes.** `pipe(2)` (22), 4 KB ring, directed wake, EOF,
  `-EPIPE`, exit-path wake, stdio-guard inversion. `dup(2)` (32).
  Keyboard fix (Shift+backslash).
- **Session 37 — `musl_sh` is a real shell; `cat` has stdin.**
  `donix>` strips quotes and parses `<`, `>`, `>>`, `|`, `&&`, `;`,
  and pipelines. `cat < file` works without busybox. Userland only.
- **Session 38 — framebuffer console.** VBE mode 0x118
  (1024×768×24), framebuffer mapped into the kernel, Terminus 10×18
  glyph blitter, the console routed through a framebuffer-aware cell
  primitive with a shadow grid (scroll / insert-delete / alt-screen
  all work on the framebuffer), and `vi` filling the screen. The
  VGA text console is the fallback; the VT100 parser is unchanged
  and drives either backend.

The narratives are in the annotated scratch tags,
`docs/session-log.md`, and `handoff.md`.

---

## v0.6.8 — in progress, the `*at()` family

Opened in session 39 (`20260930-at`).  The theme is the `*at()`
family: one path resolver shared by every syscall that takes a
dirfd.  `resolve_at`, `newfstatat` (262), and `openat` (257) are
done; the stat family is inverted to wrappers as on Linux; busybox
`find` (with `-type`) is enabled and works.  The remaining
syscalls (`unlinkat`, `mkdirat`, `renameat`, …) are wrappers plus
dispatch cases.  `unlinkat` (263) is the smallest honest next step.

The live state, the canary rows, and the NEXT SESSION list are in
[`handoff.md`](handoff.md).  This file records *future* work; the
`v0.6.8` items still outstanding are listed there.

### Still open in this milestone (from `handoff.md`)

- **`unlinkat` (263)** — `unlink`/`rmdir` in one syscall;
  `AT_REMOVEDIR` selects.  Real consumer: `rm -r`.
- **`mkdirat` (258)** — `mkdir` with a dirfd.
- **`renameat` (264)** — two-path; `resolve_at` twice.
- **`linkat` / `readlinkat` / `symlinkat`** — FAT has no links or
  symlinks; honest `-EPERM`/`-ENOSYS` until a VFS exists.  Skip.
- **`faccessat` / `fchmodat` / `fchownat`** — donix ignores
  permissions and ownership.  Skip.

---

## Kernel hardening and infrastructure

Independent of the shell/framebuffer work.  Roughly in order of
value.

- **Real copy-on-write for `fork`.**  The eager copy is O(~6 MB) per
  fork.  Mark shared PTEs read-only and copy on write in a `#PF`
  handler.
- **Page-table teardown on process exit.**
- **ELF loader `PT_NX` follow-up.**  Mark data/BSS/stack
  non-executable.
- **`sys_munmap`.**  Currently a stub returning 0.
- **`sys_brk` heap base and the mmap window.**  Fixed addresses;
  latent collisions.
- **Kernel log routing.**  Route diagnostics to serial only, or add
  a `SYS_KLOG(level)` syscall.
- **Real FatFs timestamp storage.**  The timestamp syscalls return 0
  without storing.
- **Pipe buffer growth.**  `pipe_t.capacity` is a field precisely so
  the buffer can grow from 4 KB toward 64 KB later.

### Testing infrastructure

- **Boot-time self-test mode** (`-DSELFTEST`).
- **`make test` target** — headless boot + serial grep.
- **Scripted canary** — diff `capture.txt` against a known-good log.

### Longer term

- **Per-process tty / console focus** — prerequisite for multiple
  concurrent shells.
- **Serial console debug access** — kernel shell over COM1.
- **Device drivers** — PCI enumeration, AHCI, PS/2 mouse (see the
  Wayland section below for what a mouse is and is not needed for).
- **Scalable GUI font / richer graphics** — beyond the current
  fixed-size text console; a Wayland compositor is the far end of
  this arc.

---

## Wayland (long horizon)

Direction of travel: donix should eventually run a Wayland client —
ideally a small `wl_shm` client written against the wire protocol,
statically linked against musl, speaking to a compositor that is
either `weston` (heavy: EGL, DRM, libinput, xkbcommon, mesa) or a
minimal custom one.  The custom-compositor-plus-hand-written-client
route is the honest first probe; Weston is a mountain of
dependencies and should not be the opening move.

Not a `v0.6.x` target.  Recorded here so the milestones above stay
honest about what is and is not on that path.

**Prerequisites, rough order:**

- **A compositor, or a hand-written `wl_shm` client.**  The first
  real decision and the biggest unknown.
- **`AF_UNIX` sockets + `SCM_RIGHTS` fd passing.**  Wayland's
  transport.  Not yet present.  Larger than mouse support.
- **`mmap` of non-anonymous mappings** — a shared buffer from a
  `memfd` or file.  Currently refused; see `open-issues.md`.
- **`memfd_create` + `ftruncate`** for `wl_shm` pools.
- **`poll` (or `epoll`)** — the client's main loop.
- **`SIGBUS` on buffer overrun** — needs real signal delivery, the
  same subsystem `SIGPIPE` needs (see `open-issues.md` item 6).
- **`futex`** — verify it exists; musl threads need it anyway.

**Explicitly not Wayland prerequisites, despite being commonly
assumed:**

- **Mouse support.**  Needed for an *interactive compositor*, not
  for a `wl_shm` client driven by a canned event stream.  Its own
  future milestone (already listed under "Longer term" above).
- **A VFS.**  Wayland does not require one.  A `wl_shm` client needs
  no device nodes at all; a DRM-backed compositor later can reach
  `/dev/...` through the existing path layer.  The VFS is on the
  critical path for *donix generally*, not for Wayland.

Revisit when `v0.6.8` closes.

---

## Notes

Feature work goes here.  The current state of the project and the
session-by-session history are in [`handoff.md`](handoff.md).  The
pre-fork dons-os history and the musl migration are in
[`docs/`](docs/).

MIT licensed — contributions welcome.
