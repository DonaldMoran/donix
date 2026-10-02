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

## Done — Phases A through B, v0.6.9

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
- **v0.6.8 — the `*at()` family.** One path resolver (`resolve_at`)
  shared by every syscall that takes a dirfd.  `newfstatat` (262),
  `openat` (257), `unlinkat` (263), `faccessat` (269), and
  `utimensat` (280) all route through it.  The stat family is
  inverted to wrappers as on Linux.  busybox `find` (with `-type`)
  is enabled and works.
- **v0.6.9 — envp, the `/usr/bin` layout, the shim removal — and
  what grew from them.**  See below.

The narratives are in the annotated scratch tags,
`docs/session-log.md`, and `handoff.md`.

---

## v0.6.9 — envp, `/usr/bin`, the shim removal (shipped)

Opened in session 42 (`20261001-envp`) as three subjects and grew,
in the same session, into a large milestone: a regression test, a
rewrite, a page-table bug fix, four syscalls, ten busybox applets,
three small gaps, and four gotchas.  The milestone's own scope
followed each thing the last one exposed.

**envp.**  `sys_execve` ignored its third argument, so every program
ran with an empty environment.  It now passes the caller's `envp`
through verbatim, Linux-style — the shell builds the environment,
the kernel carries it.  `export FOO=bar` in ash, then `echo $FOO`,
prints `bar`.  `env` and `printenv` are enabled and read it.
Regression-tested by `tests/envp_step1.c`.

**The `/usr/bin` layout.**  The donix-native ELFs moved from the FAT
root to `/usr/bin`, `busybox` stays in `/bin`, `/tmp` was created.
This is the split the two shells' search rules need: ash's applets
never consult `PATH`, so busybox always wins there; `musl_sh`
searches `/usr/bin` first, so a bare name resolves to the
donix-native tool.  Every binary is staged **bare** — no `.ELF`
suffix.

**The shim removal.**  `sys_execve`'s bare-name attempt — uppercase
the name, append `.ELF`, try the root and `/bin` — is gone, along
with the helpers it called and a `f_stat` retry that used the same
guesser.  What remains is (a) the path as given and (b) the `"0:"`
prefix translation for an absolute path, which is the smallest the
shim can be without a VFS.  `musl_exec2` now asserts that a bare
name does *not* resolve.

**The canary is a program.**  `tests/canary.c` runs every
non-interactive canary row and reports pass/fail.  It replaces the
hand-typed list, which had already drifted once.

**The bugs and the syscalls.**  A `puts_raw` with an input-only
`syscall` asm block let GCC issue a second syscall with the first's
return value as its number; a set of hand-counted string lengths
were wrong by one or two.  Both are gotchas now.  An intermittent
`#PF` at `0x400000` — the huge-page split in `vmm_map_page_in_cr3`
silently returning on allocation failure, leaving a supervisor page
where a user page was asked for — was found, diagnosed, and fixed.
Four syscalls landed: `readlink` (89, honest `-EINVAL` — no
symlinks), `clock_gettime` (228, from `g_ticks`), `nanosleep` (35, a
`g_ticks` deadline loop), and `munmap` (11, for real — the stub was
a leak that musl's mallocng actually calls).

**Ten busybox applets.**  `basename`, `dirname`, `unlink`, `ttysize`,
`tty`, `arch`, `mktemp`, `sleep`, `usleep`, `truncate`, plus `env`
and `printenv` — all config-only once the syscalls above existed.
(`truncate` needed no new syscall: it uses `ftruncate` (77), not
`truncate` (76), which the handoff's table had wrong.)

**Three small gaps.**  `munmap` (counted above), `fcntl` now accepts
fd 0/1/2 for all subcommands, and Ctrl-`[` produces ESC (0x1B).

The live state, the canary rows, and the NEXT SESSION list are in
[`handoff.md`](handoff.md).

---

## v0.6.8 — the `*at()` family (shipped)

Opened in session 39 (`20260930-at`), shipped and pushed in session
42.  The theme was the `*at()` family: one path resolver shared by
every syscall that takes a dirfd.  `resolve_at` is that resolver;
`newfstatat` (262), `openat` (257), `unlinkat` (263), `faccessat`
(269), and `utimensat` (280) all route through it.  The stat family
is inverted to wrappers as on Linux.  busybox `find` (with `-type`)
is enabled and works.

**Every `*at` syscall with a consumer is implemented.**  The
remaining ones have no caller in the current applet set, verified
by reading busybox's source:

- `mkdirat` (258) — no consumer; `tar`/`cpio`/`unzip` would, but
  each needs a VFS first.
- `renameat` (264) — no consumer; `rm`-style code uses `rename`
  (82).
- `linkat`/`symlinkat`/`readlinkat` (265/266/267) — FAT has no
  links; no consumer is possible.

See `open-issues.md` and `gotchas.md`, "A consumer inferred from
behavior is not a consumer," for the `unlinkat` finding: it was
added on the belief that `rm -r` needs it, and `rm -r` does not.

---

## Make `/proc` possible — the next milestone

**This is the next major milestone after the immediate `realpath`
enable and `readlink` errno fix** (see `handoff.md`).  It is not a
"VFS milestone."  It is the first feature that the current
architecture **cannot express at all**, and building it is what
forces the pathname dispatch seam into existence.

### Why this, and not symlinks

`resolve_against_cwd` plus `fat_lookup` cannot produce
`/proc/self/status`.  There is no FAT entry and never will be.
That is the test from `docs/strategy.md`, "When a feature may force
architecture": a feature forces change when the current
architecture cannot express it, not when a new architecture would
be cleaner.

Symlinks fail that test -- they are deferrable, and they can be
added later as a *consumer* of the seam.  `/proc` passes it.
`/proc` also delivers observability the kernel needs
(`/proc/self/status`, `/proc/self/maps`), which makes kernel
development itself easier.  It is the right first customer.

### Scope — one deliverable, two parts, sized to one file

The milestone is **"the minimal dispatch seam plus the smallest
open-file representation that one `/proc` file requires."**  Both
parts, together, sized to `/proc/self/status` and nothing larger.

**The dispatch seam.**  Today path lookup is conceptually
`path -> FAT -> result`.  `/proc` makes it
`path -> first component -> procfs | fat | devfs`.  That is
dispatch, and it is the VFS front door whether it is called that or
not.  Design it knowing `/dev` and mounts are coming; do not build
them now.

**The minimal open-file representation.**  `open("/proc/self/status")`
must return something `read`, `stat`, and `close` can act on, and
that something is not a FAT file.  This is forced by the *first*
`/proc` file -- it cannot be deferred past it.  The minimal form is
a small `file_ops`-style struct with `read`/`stat`/`close`, with FAT
and proc both implementing it.  **No inode layer, no vnode layer,
no superblocks, no reference counts, no mount framework.**

**Acceptance test:** one `/proc` file, probably `/proc/self/status`,
opens, reads its contents, stats as a regular file, and closes --
from `donix>` and from ash.  Add it to the canary.

### What it subsumes

`open-issues.md` item 1: `sys_execve`'s two remaining path attempts
and `resolve_against_cwd` are shims the seam subsumes.  **Delete
them as part of this milestone; do not extend them.**

### The two failure modes to avoid

- **Over-abstraction.**  Do not schedule "VFS."  Do not build an
  inode/vnode/mount/superblock stack.  Build the seam `/proc`
  needs and stop.
- **Under-abstraction.**  Do not special-case `/proc` inside
  `sys_open`, then `sys_stat`, then `sys_access`.  This is the
  failure mode donix is *more* at risk of, because its history is
  "implement the syscall when a feature needs it."
  `resolve_against_cwd` and `sys_execve`'s shims are already the
  existing instance of this pattern.

### After it lands — consumers, in order

Each of these is a consumer of the seam, ordered by what it
unlocks.  None is scheduled yet.

1. **`/dev`** — `/dev/null`, `/dev/tty`, `/dev/urandom`; lets
   `tty` name its terminal.
2. **FAT-backed symlinks** — a `DONIX_LINK:`-style marker in an
   ordinary file, hidden entirely inside the seam.  See
   `open-issues.md` item 9.
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
- **The remaining silent `vmm_map_page*` returns.**  Six sites
  (`vmm_map_page_in_cr3` and `vmm_map_page`, the PDPT/PD/PT
  allocation paths) still `return` without mapping when a
  page-table allocation fails, and the caller cannot tell.  The
  huge-page-split instance was fixed in session 42 (`20261001-splitfix`).
  The rest are a deliberate decision per site: change the signature
  and check every caller, or halt on failure as the split path now
  does.  See `open-issues.md` item 7.
- **Page-table teardown on process exit.**
- **ELF loader `PT_NX` follow-up.**  Mark data/BSS/stack
  non-executable.
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

Revisit when `v0.6.10` opens.

---

## Notes

Feature work goes here.  The current state of the project and the
session-by-session history are in [`handoff.md`](handoff.md).  The
pre-fork dons-os history and the musl migration are in
[`docs/`](docs/).

MIT licensed — contributions welcome.
