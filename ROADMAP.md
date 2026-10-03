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

## v0.6.12 — item 7 closed; the process failure paths

**A correctness milestone.**  Five commits, and every one of them is
about a kernel path that could not report a failure, or a leak on a
path that does.  No new subsystem, no new applet, no new syscall.

**Item 7: the silent `vmm_map_page*` returns.**  `vmm_map_page` and
`vmm_map_page_in_cr3` had six sites — the PDPT, PD, and PT
allocation paths in each — where a failed page-table allocation
meant no mapping was made and the caller could not tell.  The change
was the one item 7 named as the honest fix, in two commits:

- The **signature change** — both functions return `int` (0 =
  mapped, -1 = allocation failed) — with the six `if (!phys)
  return;` sites becoming `return -1;`.  No caller handled the
  return in this commit; the point was to isolate the signature
  change from the caller changes, and to ship the `isr14_handler`
  diagnostic item 7 required be in place first.  **The session-45
  virtual-1 fault did not reproduce.**
- The **nine callers** handle the `-1`, in the shape each caller
  dictated: `ensure_hhdm_mapped` and `vmm_init`'s identity map
  **halt** (boot paths, no caller to report to); `elf_load_into_process`,
  `process_create`'s stack loop, `heap_extend`, `sys_mmap`,
  `sys_brk`, `exec_alloc_user_stack`, and `sys_fork`'s
  `EAGER_COPY_REGION` macro **recover**.  `heap_extend` is the
  nontrivial one: it unmaps and frees its partial region, because
  nothing else tracks those pages.

**`process_create`'s failure exits, and the unchecked clone.**
Reading the nine call sites turned up `pcb->cr3 =
vmm_clone_page_table(current_cr3);` with no check — a `0` return
became `pcb->cr3 = 0`, and the next `vmm_map_page_in_cr3` walked
page tables at `HHDM_START + 0`.  Worse than a leak.  The same
function's failure exits leaked the cloned page-table hierarchy.
Both are fixed by a new `process_free_clone(cr3)` and full cleanup
on every failure exit.

**The exit-path page-table leak.**  `process_reclaim` and
`process_destroy` freed a process's ELF pages and its kernel stack
slot but abandoned the cloned page-table hierarchy.  Both now call
`process_free_clone`.  This is what first *exercised* the helper
commit 3 added — `exec_churn` ran it 24 times, and the `k`-shell
selftest's three exception children 3 more, with no fault and no
double-free.

**Dead code.**  `f_stat_with_retry`'s `has_drive` computed a
variable, cast it to `(void)`, and did nothing with it; the whole
`if` block was dead.  Deleted.

**The milestone's own lesson**, and the reason it is recorded here:
a helper written for failure paths is **correct by inspection
only** until something on a hot path calls it.  Commit 3's
`process_free_clone` and commit 4's use of it on the exit path are
the two states; only the second was verified, and `exec_churn` is
what made it so.  See `docs/gotchas.md`, "A function that has never
run is correct by inspection only."

**Verification.**  Both boot paths, every commit: `canary` 15/15,
`canary --full` 28/28, `selftest` 17/17, `exec_churn` 24 rounds all
children exit 0, `mmap_stress` 16 rounds.  No `#PF`, no `#DF`, no
`#GP`, no `Unknown syscall:`, no `PMM: WARNING - Double free`.

---

## v0.6.11 — the seam, `/proc`, and the PMM fix

**The pathname dispatch seam (session 44).**  The first feature the
previous architecture could not express at all: `resolve_at` returns
a backend tag from a path's first component, and FAT / DEV / PROC
are selected by it.  No VFS — no inode, no vnode, no mount table.
Its first three consumers: `/dev/null`, `/proc/self/status`, and
`/dev/console` + `/proc/self/fd/N` (which is what makes `tty` print
`/dev/console`).  `/dev/null`'s old exact-path predicate was deleted
in the process.

**`/proc` per-pid, and `ps` (session 47).**  `/proc` became a
listable directory: `readdir("/proc")` returns `self` and the live
pids, and `/proc/<pid>/stat`, `status`, and `cmdline` each read back
real per-process fields.  `ps` and `pstree` were enabled and work.
The bug worth remembering: `ps` printed a header and no rows because
libbb's `procps_scan` does `stat("/proc/<pid>/")` — **with a
trailing slash** — and skipped the entry when it failed.  donix
served the per-pid *files* but not the per-pid *directory*.

**The PMM zone-scan fix (session 48).**  The intermittent boot-time
`#PF` at `0x400000` — first boot after adding userland ELFs, cleared
by the second or third — was **not** the silent `vmm_map_page*`
returns.  It was the PMM zone scan: `pmm_alloc_page` started at a
cursor and moved one direction, so a page free on the far side was
never found.  Adding ELFs pushed the cursor past such pages, and the
boot ELF load's page-table allocation returned 0 with
`pmm_free_pages` healthy.  `pmm_scan_zone` now wraps.  Ten-plus
consecutive boots on the trigger image, no fault.

**What `v0.6.11` did not close** (the list as it stood at the
`v0.6.11` bump; item 7 was closed in `v0.6.12`):

- `open("/proc/<pid>", O_DIRECTORY)` — `ps` stats the directory, it
  does not open it.
- `readdir("/dev")` — `/dev` is not a directory; `ls /dev` fails.
- Symlinks (item 8) — design recorded, buildable as the seam's next
  consumer.
- `sys_gettimeofday` (99) — so `ps -l` is off.
- `/etc/passwd` — so `ps`'s USER column shows the numeric uid.

---

## v0.6.10 — `realpath`, the `readlink` errno, `/dev/null` (shipped)

A tail on `v0.6.9` with no new subsystem.  `CONFIG_REALPATH=y`
(config-only — the applet routes through musl's `realpath()` plus
`libbb`'s `xmalloc_readlink` and `getcwd`, all present).  The
`readlink` errno split (`-ENOENT` for a missing path, `-EINVAL` for
an existing non-symlink) was already in the tree and was closed with
a run behind it, `readlink_errno.c`.  `/dev/null` works across
`open`/`stat`/`access` (`2>/dev/null` discards stderr), which was
the seam's first consumer.

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

Revisit when the next major direction opens.

---

## Notes

Feature work goes here.  The current state of the project and the
session-by-session history are in [`handoff.md`](handoff.md).  The
pre-fork dons-os history and the musl migration are in
[`docs/`](docs/).

MIT licensed — contributions welcome.
