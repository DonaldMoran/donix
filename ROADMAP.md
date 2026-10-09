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
faulting process is killed cleanly. `ps` lists processes, reading a
real `/proc`.

This file is **future work only**. For the current state of the
project, see [`handoff.md`](handoff.md). For how donix got here, see
[`docs/migration-history.md`](docs/migration-history.md) (the musl
migration) and [`docs/dons-os-history.md`](docs/dons-os-history.md)
(the pre-fork dons-os story).

---

## Done — Phases A through B, v0.6.12

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
- **v0.6.7 — a real shell, and a framebuffer console.**  The
  milestone sessions 37 and 38 make up, tagged `11c2f16` and merged
  to `main` (`3e6f00b`).  The shell became real (`musl_sh` tokenizer,
  redirection, sequences, pipelines) and the console moved to the
  1024×768 linear framebuffer with Terminus 10×18 text and `vi`
  filling the screen.  Detail under sessions 37 and 38 below.
- **v0.6.8 — the `*at()` family.** One path resolver (`resolve_at`)
  shared by every syscall that takes a dirfd.  `newfstatat` (262),
  `openat` (257), `unlinkat` (263), `faccessat` (269), and
  `utimensat` (280) all route through it.  The stat family is
  inverted to wrappers as on Linux.  busybox `find` (with `-type`)
  is enabled and works.
- **v0.6.9 — envp, the `/usr/bin` layout, the shim removal — and
  what grew from them.**  See below.
- **v0.6.10 — a tail on `v0.6.9`.**  `realpath` (config-only),
  the `readlink` errno closed by test (`readlink_errno.c`), and
  `/dev/null` working across `open`/`stat`/`access`.  No new
  subsystem.
- **v0.6.11 — the pathname dispatch seam, `/proc` and `ps`, and the
  PMM zone-scan fix.**  Three sessions' work, and a large milestone:
  session 44's seam (see below), session 47's `/proc` per-pid
  support and working `ps`/`pstree`, and session 48's fix for the
  intermittent boot-time `#PF`.
- **v0.6.12 — item 7 closed; the process failure paths.**  A
  correctness milestone, not a feature one.  Session 49 made
  `vmm_map_page*` able to report a failed page-table allocation and
  taught its nine callers to act on it (item 7, the silent returns);
  fixed `process_create`'s failure exits and its unchecked
  `vmm_clone_page_table` return; closed the same page-table leak on
  the process-exit path; and deleted one piece of dead code.  No new
  subsystem, no new applet.  See below.

The narratives are in the annotated `v*` tags,
`docs/session-log.md`, and `handoff.md`.

---

For the per-version narrative from `v0.6.7` onward, see
[`docs/version-history.md`](docs/version-history.md).

---

## `/proc` — shipped, with edges

**This was the next major direction; it is now largely done.**  The
seam exists, `/proc` is a listable directory, and `ps` and `pstree`
work.  What follows is the shape it took and what remains — not a
plan, a record.

### Why it forced architecture, and did

`resolve_against_cwd` plus `fat_lookup` could not produce
`/proc/self/status`.  There is no FAT entry and never will be.  That
is the test from `docs/strategy.md`, "When a feature may force
architecture," and `/proc` passed it: it forced the pathname
dispatch seam into existence, and the seam now serves every path
that is not a FAT file.

### What it does not yet do

- **`open("/proc/<pid>", O_DIRECTORY)`** — `ps` *stats* the per-pid
  directory; nothing opens it.  `ls /proc/1` would need
  `open_resolved` to accept the same two paths `stat_resolved` does,
  producing a `FILE_KIND_DIR` slot with `PROC_DIR_SENTINEL` — the
  mechanism `/proc` itself already uses.
- **`/dev` is not a directory.**  `ls /dev` fails; adding it is the
  same directory shape `/proc` got, and is the prerequisite for
  `/dev/tty` and `/dev/urandom`.
- **`sys_gettimeofday` (99)** — `PS_LONG`/`PS_TIME` need it, so
  `ps -l` is off.
- **`/etc/passwd`** — so `ps`'s USER column shows a name instead of
  the numeric uid.
- **A nonexistent pid stats as a directory** — the check is on the
  path shape, not on `process_find_by_pid`.

### The seam's remaining consumers, in order

Each is a consumer of the seam, ordered by what it unlocks.  None
is scheduled.

1. **`/dev` as a listable directory** — `/dev/tty`, `/dev/urandom`,
  and `ls /dev`.  Small, patterned work.
2. **FAT-backed symlinks** — a `DONIX_LINK:`-style marker in an
  ordinary file, hidden entirely inside the seam.  See
  `open-issues.md` item 8.
3. **`ln`, `link`, `readlink` with real targets**, and archive
  symlink restoration (`tar`, `unzip`).
4. **A mount framework**, if and when a second filesystem exists.

---

## Kernel hardening and infrastructure

Independent of the shell/framebuffer work.  Roughly in order of
value.

- **Real copy-on-write for `fork`.**  The eager copy is O(~6 MB) per
  fork, and copies read-only pages (`.text`, `.rodata`)
  unconditionally.  Mark shared PTEs read-only and copy on write in
  a `#PF` handler; at minimum, skip the read-only regions.  **This
  is a standing cost, not a regression** — it is present at
  `v0.6.6` and earlier.  Session 41 bisected a perceived slowdown
  in `musl_wait` and found the change was in the console (VGA text
  → framebuffer), not in `fork`; see `session-log.md`, session 41.
- **Stack NX.**  The user stack is still mapped without `PT_NX`
  (`process_create`'s `USER_STACK_PAGES` block and
  `exec_alloc_user_stack`).  The ELF loader's per-segment `PF_X` and
  the page-0 guard shipped in session 58 (`elf.c` sets `PT_NX` for a
  non-`PF_X` segment); what remains is the stack.
- **`sys_brk` heap base and the mmap window.**  Fixed addresses;
  latent collisions.
- **Signal delivery (`SIGPIPE`, `SIGBUS`).**  `sys_rt_sigaction` is
  a stub; `-EPIPE` is delivered without `SIGPIPE`.  A subsystem, and
  the prerequisite for job control, `kill(2)`, and a Wayland
  `wl_shm` client's `SIGBUS`.  See `open-issues.md`.
- **Kernel log routing.**  Route diagnostics to serial only, or add
  a `SYS_KLOG(level)` syscall.
- **Real FatFs timestamp storage.**  The timestamp syscalls return 0
  without storing.
- **Pipe buffer growth.**  `pipe_t.capacity` is a field precisely so
  the buffer can grow from 4 KB toward 64 KB later.

### Testing infrastructure

- **Boot-time self-test mode** (`-DSELFTEST`).  A self-test suite
  already runs at boot (17 checks, GDT/TSS/PMM/VMM/heap/NX/ATA/
  FatFs/exceptions); making it a build-flag mode would let a normal
  boot skip it.
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
  same subsystem `SIGPIPE` needs (see `open-issues.md`).
- **`futex`** — verify it exists; musl threads need it anyway.

**Explicitly not Wayland prerequisites, despite being commonly
assumed:**

- **Mouse support.**  Needed for an *interactive compositor*, not
  for a `wl_shm` client driven by a canned event stream.  Its own
  future milestone (already listed under "Longer term" above).
- **A VFS.**  Wayland does not require one.  A `wl_shm` client needs
  no device nodes at all; a DRM-backed compositor later can reach
  `/dev/...` through the pathname dispatch seam.  The VFS is on the
  critical path for *donix generally*, not for Wayland.

Revisit when the next major direction opens.

---

## Notes

Feature work goes here.  The current state of the project and the
session-by-session history are in [`handoff.md`](handoff.md).  The
pre-fork dons-os history and the musl migration are in
[`docs/`](docs/).

MIT licensed — contributions welcome.
