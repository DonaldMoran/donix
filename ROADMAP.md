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

## v0.6.7 — pick a direction

The framebuffer was the big item and it is done.  What's left is a
set of small, independent items and several larger subsystem
questions.  Pick one, do it, test it, tag it.

### Small, close gaps

- **`newfstatat` (262).**  Number reserved, no dispatch case.
  Delegates to `sys_stat` when `dirfd == AT_FDCWD` or the path is
  absolute.  Unblocks `find`.
- **Ctrl-`[` as ESC.**  `scancode_to_ascii` has no Ctrl parameter
  yet.  Low urgency (the literal ESC key works).
- **`sys_utimensat` cwd resolution.**  Calls `strip_dot_prefix` but
  not `resolve_against_cwd`; one-line fix.
- **`sys_fcntl` fd < 3 for the other subcommands.**  `F_GETFL`,
  `F_SETFL`, `F_GETFD`, `F_SETFD` still refuse `fd < 3`.  Not on any
  current path.
- **`sys_open` `O_DIRECTORY`** — done in session 38 (returns
  `-ENOTDIR` on a file).
- **Redirection of a builtin is silently ignored.**  `docs/open-issues.md`
  item 2.
- **A builtin in a pipeline is refused.**  `docs/open-issues.md`
  item 3.

### Broaden busybox coverage

The file/text utility set is broad.  Still untried: `awk`, `tar`,
and `find` (needs `newfstatat`).  Work through them one at a time,
watching for `Unknown syscall: N`.  `ps`, `top`, `kill`, and job
control need subsystems the kernel does not have yet.

### Framebuffer follow-ups (optional, not urgent)

The console is 10×18 at 1024×768.  If it strains:
- **Bigger font** — swap `ter-u18n.psf` for `ter-u24n.psf` (12×24)
  or `ter-u32n.psf` (16×32) and update `FB_FONT_*` in `fb.c`.  Data
  change.
- **Bigger mode** — a different VBE mode number in `stage2.asm`; the
  kernel adapts (it reads the captured descriptor).  The grid becomes
  `fb_width/10 × fb_height/18`.
- **A scalable / anti-aliased GUI font later** — **Hack** (MIT) is
  the code-oriented choice.  Out of scope for a console.

### Larger subsystem questions

- **Signal delivery.**  `sys_rt_sigaction`/`sys_rt_sigprocmask` are
  stubs.  Prerequisite for `SIGPIPE`, job control, and `kill(2)`.
  Larger than it sounds: per-process pending/blocked mask, a
  delivery point on syscall return or interrupt, a user-mode handler
  trampoline.  Its own milestone-scale effort.
- **VFS layer.**  `sys_execve`'s three-attempt path resolution and
  `resolve_against_cwd` are shims.  Delete them when a VFS lands; do
  not extend them.

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
- **Device drivers** — PCI enumeration, AHCI, PS/2 mouse.
- **Scalable GUI font / richer graphics** — beyond the current
  fixed-size text console.

---

## Notes

Feature work goes here.  The current state of the project and the
session-by-session history are in [`handoff.md`](handoff.md).  The
pre-fork dons-os history and the musl migration are in
[`docs/`](docs/).

MIT licensed — contributions welcome.
