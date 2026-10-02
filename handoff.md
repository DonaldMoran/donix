Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-02 (session 43, closing)
**Current HEAD:** branch `dev`, **13 commits ahead of `origin/dev`**
(`origin/dev` is at `v0.6.9`, `441229e`).  Nothing pushed since
`v0.6.9`.
**Last milestone:** `v0.6.9` (published) — envp, the `/usr/bin`
layout, the `execve` shim removal, the huge-page-split `#PF` fix,
four syscalls (`readlink`, `clock_gettime`, `nanosleep`, `munmap`),
ten busybox applets
**Milestone status:** **`v0.6.9` shipped and pushed.**  Session 43
is a tail on it: `realpath` enabled (config-only), the `readlink`
errno closed by a regression test, and `/dev/null` working across
`open`/`stat`/`access`.  All committed and scratch-tagged; none
pushed.  No milestone has been opened for the post-ship work.

Commits are named by tag only, never by SHA.  **Working tags
(`YYYYMMDD-*`) are local scratch restore points** — they exist while
a milestone is being developed and are **dropped before the
milestone is pushed**.  Only `v*` tags go to the remote and are
permanent.  The commit record is `docs/session-log.md`; the commit
message carries the narrative.  Once a scratch tag is dropped it
resolves to nothing; do not cite one as if it were a stable
reference.

**Scratch tags are annotated.**  The annotation is the fuller
per-commit summary; at a milestone bump the annotations seed the
final milestone narrative.  This is why each step gets a tag even
when no `v*` tag is imminent.

**Note on commit messages:** a commit message is a claim, not a
fact.  `95c6337 handoff: rewrite fresh for the v0.6.9 bump` did not
rewrite the handoff body.  Read the diff, not the subject.

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 syscall ABI, natively. Newlib is gone.
Userland is a tracked source tree at `userland/musl/`. Phase B
(busybox) is well underway.

Full strategy, rules, and the one-change-at-a-time discipline:
`docs/strategy.md`.  Direction of travel beyond the current
milestone: `ROADMAP.md`.

---

## Tree on disk

One working copy.

- **Real project:** `/home/noneya/code/donix/`.  This is the tracked
  repository, the source of truth, and where the `v*` tags live.

The kernel C sources live in `04_kernel_64bit/`.  Verify by file
name before editing — do not trust the number alone.
`05_boot_kernel64/` holds the boot chain assembly and the image
builder, not the kernel C sources.

Scratch workspaces (e.g. a copy at `/home/noneya/code/testme/`)
are **transient**: never the source of truth, never where a `v*`
tag lives, and never referenced by this file.  If one exists, it is
safe to reset or delete.  Do not port from a scratch workspace into
the real tree without a build and test in the real tree.

---

I general our project tree view up to level 3 is:

noneya@fedora:~/code/donix$ tree -L 3
.
├── 01_boot_16bit
│   ├── boot.asm
│   ├── Makefile
│   ├── README.md
│   └── stage2.asm
├── 02_boot_32bit
│   ├── boot.asm
│   ├── build.sh
│   ├── Makefile
│   ├── README.md
│   └── stage2.asm
├── 03_boot_64bit
│   ├── boot.asm
│   ├── kernel.asm
│   ├── Makefile
│   ├── README.md
│   ├── run_debug.sh
│   └── stage2.asm
├── 04_kernel_64bit
│   ├── ata.c
│   ├── context_switch.asm
│   ├── elf.c
│   ├── entry.asm
│   ├── fatfs
│   │   ├── diskio.c
│   │   ├── diskio.h
│   │   ├── ff16.zip
│   │   ├── ff.c
│   │   ├── ffconf.h
│   │   ├── ff.h
│   │   └── ffunicode.c
│   ├── fb.c
│   ├── fonts
│   │   └── ter-u18n.psf
│   ├── gdt.c
│   ├── heap.c
│   ├── idt.c
│   ├── idt_load.asm
│   ├── include
│   │   ├── ata.h
│   │   ├── bootinfo.h
│   │   ├── debug.h
│   │   ├── elf.h
│   │   ├── fat_config.h
│   │   ├── fb.h
│   │   ├── gdt.h
│   │   ├── heap.h
│   │   ├── idt.h
│   │   ├── interrupts.h
│   │   ├── io.h
│   │   ├── keyboard.h
│   │   ├── libc.h
│   │   ├── pmm.h
│   │   ├── process.h
│   │   ├── scheduler.h
│   │   ├── serial.h
│   │   ├── syscall.h
│   │   ├── tss.h
│   │   ├── userlib.h
│   │   ├── user_msr.h
│   │   ├── user_space.h
│   │   ├── user_syscall.h
│   │   ├── vga.h
│   │   └── vmm.h
│   ├── interrupts.c
│   ├── isr.asm
│   ├── keyboard.c
│   ├── kmain.c
│   ├── linker.ld
│   ├── Makefile
│   ├── pmm.c
│   ├── process.c
│   ├── ring3.c
│   ├── ring3_entry.S
│   ├── scheduler.c
│   ├── serial.c
│   ├── string.c
│   ├── test_program.asm
│   ├── test_syscall.c
│   ├── tss.c
│   ├── user_linker.ld
│   ├── user_syscall.c
│   ├── user_syscall_entry.asm
│   ├── vga.c
│   └── vmm.c
├── 05_boot_kernel64
│   ├── boot.asm
│   ├── BOOTCHAIN.md
│   ├── Makefile
│   └── stage2.asm
├── capture.txt
├── configs
│   └── busybox.config
├── docs
│   ├── CHECKLIST.md
│   ├── dons-os-history.md
│   ├── gotchas.md
│   ├── LLD_BUG_REPORT.md
│   ├── MAINTENANCE.md
│   ├── migration-history.md
│   ├── open-issues.md
│   ├── session-log.md
│   └── strategy.md
├── handoff.md
├── hdd.img
├── LICENSE
├── Makefile
├── migration-tags.txt
├── README.md
├── ROADMAP.md
├── run
├── test-files
│   └── HELLO-WORLD.TXT
├── third_party
│   ├── busybox
│   │   ├── applets
│   │   ├── applets_sh
│   │   ├── arch
│   │   ├── archival
│   │   ├── AUTHORS
│   │   ├── busybox
│   │   ├── busybox_ldscript.README.txt
│   │   ├── busybox_unstripped
│   │   ├── busybox_unstripped.map
│   │   ├── busybox_unstripped.out
│   │   ├── Config.in
│   │   ├── configs
│   │   ├── console-tools
│   │   ├── coreutils
│   │   ├── debianutils
│   │   ├── docs
│   │   ├── e2fsprogs
│   │   ├── editors
│   │   ├── examples
│   │   ├── findutils
│   │   ├── include
│   │   ├── init
│   │   ├── INSTALL
│   │   ├── klibc-utils
│   │   ├── libbb
│   │   ├── libpwdgrp
│   │   ├── LICENSE
│   │   ├── loginutils
│   │   ├── mailutils
│   │   ├── Makefile
│   │   ├── Makefile.custom
│   │   ├── Makefile.flags
│   │   ├── Makefile.help
│   │   ├── make_single_applets.sh
│   │   ├── miscutils
│   │   ├── modutils
│   │   ├── networking
│   │   ├── NOFORK_NOEXEC.lst
│   │   ├── NOFORK_NOEXEC.sh
│   │   ├── printutils
│   │   ├── procps
│   │   ├── qemu_multiarch_testing
│   │   ├── README
│   │   ├── runit
│   │   ├── scripts
│   │   ├── selinux
│   │   ├── shell
│   │   ├── size_single_applets.sh
│   │   ├── sysklogd
│   │   ├── testsuite
│   │   ├── TODO
│   │   ├── TODO_unicode
│   │   └── util-linux
│   ├── musl-install
│   │   ├── bin
│   │   ├── include
│   │   └── lib
│   └── musl-src
│       ├── arch
│       ├── compat
│       ├── config.mak
│       ├── configure
│       ├── COPYRIGHT
│       ├── crt
│       ├── dist
│       ├── dynamic.list
│       ├── include
│       ├── INSTALL
│       ├── ldso
│       ├── lib
│       ├── Makefile
│       ├── obj
│       ├── README
│       ├── src
│       ├── tools
│       ├── VERSION
│       └── WHATSNEW
├── toolchain
│   ├── install_musl.sh
│   └── musl-gcc.sh
└── userland
    └── musl
        ├── apps
        ├── Makefile
        └── tests

---

## Where we are — `v0.6.9` shipped, four post-ship changes

`v0.6.8` shipped the `*at()` family.  `v0.6.9` opened as **envp**
and grew, in session 42, into the **`/usr/bin` layout**, the
**removal of `execve`'s bare-name guess**, a **page-table bug fix**,
**four new syscalls**, **ten busybox applets**, **three small
gaps**, and **four new gotchas**.  Tagged `v0.6.9`, pushed,
scratch tags dropped.

Session 43 added four things on top, all committed and
scratch-tagged, none pushed:

- **`realpath` enabled**, config-only (`20261002-realpath`).
- **`readlink` errno closed by test** (`20261002-readlink-test`).
- **`/dev/null` works for `open()`** (`20261002-dev-null`).
- **`/dev/null` works for `stat` and `access` too**
  (`20261002-dev-null-stat`) — the match extracted into
  `path_is_devnull`, one predicate, three call sites.

It also corrected a stale test found along the way: `pipe_step1`
(`20261002-pipe-step1`), and the gotcha it produced
(`20261002-pipe-gotcha`).

### Session 43, in full

| Tag | What |
|---|---|
| `20261002-realpath` | `CONFIG_REALPATH=y` — enables busybox `realpath`; config-only |
| `20261002-readlink-test` | `readlink_errno.c` — readlink(2) errno split, 3 checks; closes item 8 |
| `20261002-dev-null` | `open()` recognizes `/dev/null`; `FILE_KIND_DEV_NULL` |
| `20261002-dev-null-stat` | `path_is_devnull` shared by open/stat/access; `ls`, `stat`, `test -e` work |
| `20261002-pipe-step1` | `pipe_step1` EAGAIN assertions removed; they block now, not fail |
| `20261002-dev-null-docs`, `-dev-null-stat-docs`, `-readlink-docs`, `-readlink-gotcha`, `-pipe-gotcha`, `-arch-rule`, `-handoff`, `-handoff-repath`, `-handoff-close` | docs |

**`realpath` — the trace, and two wrong test expectations.**
`realpath_main` → `xmalloc_realpath_coreutils` → `xmalloc_realpath`
→ musl `realpath()`, plus `libbb`'s `xmalloc_readlink` (readlink
89, present) and `xrealloc_getcwd_or_warn` (getcwd, present).  No
`//config:depends on` for `CONFIG_REALPATH`.  The handoff said
`realpath /nonexistent` should fail; it does not — its parent `/`
exists, so `xmalloc_realpath_coreutils`'s `ENOENT` fallback
succeeds.  The failing case is a path whose parent does not exist.
The diagnostic goes to stderr.

**`readlink` errno — the fix was already in.**  `sys_readlink`
returned `-ENOENT` for a missing path and `-EINVAL` for an existing
non-symlink; the fix had been in the tree since session 42,
untested, and `open-issues.md` item 8 still described the defect as
open.  No applet calls `readlink(2)`; the only consumer is musl's
`ttyname(3)`, which cannot distinguish the errnos with no `/proc`
and no `/dev`.  `readlink_errno.c` calls `readlink(2)` directly and
asserts both answers — 3/3.  Item 8 is closed with a run behind it.
See `gotchas.md`, "A fix with no test is indistinguishable from an
unfixed defect."

**`/dev/null` — from open-only to stat and access.**
`20261002-dev-null` made `open_resolved` recognize the exact path
`dev/null` (the `strip_dot_prefix`'d form) and return a
`FILE_KIND_DEV_NULL` slot: read returns 0, write returns count,
close frees, `fstat` reports `S_IFCHR`.  `2>/dev/null` became
silent-and-non-zero.

`20261002-dev-null-stat` then found that only `open` knew the path:
`ls /dev/null`, `test -e /dev/null`, and `stat /dev/null` all still
failed with ENOENT.  It extracted the inline match into
`path_is_devnull` and called it from `stat_resolved` and
`access_resolved` as well — one predicate, three call sites:

    $ ls -l /dev/null
    crw-rw-rw-    1    0,   0 /dev/null
    $ test -e /dev/null && echo yes
    yes
    $ stat /dev/null
      File: '/dev/null'
      ...    character special file

`readlink("/dev/null")` gets `-EINVAL` for free, via
`access_resolved`.  Still an exact-path predicate, not the dispatch
seam.  **Boundary:** `readdir("/dev")` fails (no `/dev` directory
to list), and `/dev/tty` and `/dev/urandom` do not exist, so `tty`
still prints `not a tty`.

**`pipe_step1` — a test that went stale and hung.**  It asserted
Step-1 non-blocking semantics (empty read → `-EAGAIN`, full write
→ `-EAGAIN`).  Steps 2/3 made read and write block, so re-running
the assertions hangs rather than fails: the blocked reader has
nothing to wake it.  Found by running the pipe suite per the
`open-issues.md` rule; the suite had not been run since session 36.
Removed the EAGAIN checks; the empty/full behavior is covered by
`pipe_step2`/`pipe_step3`/`pipe_step3b`, which fork a peer.  See
`gotchas.md`, "A test can encode an earlier version's behavior."

**The lesson of the session.**  Three stale artifacts were found by
reading the source rather than the doc: the handoff's `realpath`
expectation, `open-issues.md` item 8 (a fix that was already in),
and `pipe_step1` (a test from six sessions earlier).  All three are
the same shape — an artifact asserting a state the repository does
not have — and all three were caught by going to the source.

### Session 42, in full

**The opening themes (envp, layout, shim):**

| Commit subject | What |
|---|---|
| `execve: pass envp through, as Linux does` | `sys_execve` copies `envp` onto the new stack; argv region 4 KB → 16 KB; envp snapshot kmalloc'd |
| `config: enable busybox env and printenv` | config-only |
| `docs: session 42 — envp` | docs (first pass) |
| `layout: executables under /usr/bin; canary program` | executables to `/usr/bin`; `canary.c` the smoke test |
| `layout: drop the .ELF suffix; binaries staged bare` | binaries staged bare |
| `execve: remove the bare-name attempt; keep (a) and (b)` | the shim shrinks to path-as-given + `"0:"` translation |
| `docs: session 42 — envp, the /usr/bin layout, the shim removal` | docs (second pass) |

**The envp regression test and the bugs it surfaced:**

| Commit subject | What |
|---|---|
| `envp: regression test for execve passing the environment through` | `tests/envp_step1.c` + `envp_helper.c`; proved envp survives `execve`.  Found the `%rax`-clobber bug in `puts_raw` |
| `userland tests: stop hand-counting puts_raw lengths; rewrite musl_exec2` | `puts_raw` computes its own length; `musl_exec2` asserts the shim is gone; two gotchas added |

**The page-table bug:**

| Commit subject | What |
|---|---|
| `vmm: huge-page split must not silently fail` | the split in `vmm_map_page_in_cr3` halts with `VMM: FATAL` on allocation failure.  The intermittent `#PF` at `0x400000` |
| `vmm: remove the huge-page-split diagnostic` | removed the temporary `VMM: SPLIT` prints |
| `open-issues: record the remaining silent page-table-allocation returns` | the six remaining silent `vmm_map_page*` returns, item 7 |

**The dead code and the syscalls:**

| Commit subject | What |
|---|---|
| `execve: delete exec_resolve_bin_name, dead since the shim removal` | dead since the shim removal |
| `readlink: implement syscall 89 as an honest -EINVAL` | no symlinks; removes `ttyname`'s `Unknown syscall: 89` noise |
| `clock_gettime: implement syscall 228 from g_ticks; enable mktemp` | reports `g_ticks` (100 Hz PIT) as both clocks |
| `nanosleep: implement syscall 35 as a g_ticks deadline loop; enable sleep, usleep` | `hlt` loop, 10 ms granularity |
| `munmap: replace the stub with a real implementation` | frees frames, removes them from `elf_page_list`; the stub leaked |

**The applets:**

| Commit subject | What |
|---|---|
| `busybox: enable basename, dirname, unlink` | config-only |
| `busybox: enable ttysize, tty, arch` | config-only |
| `busybox: enable truncate` | config-only — it uses `ftruncate`, not `truncate(2)` |

**The doc cleanups and the last gaps:**

| Commit subject | What |
|---|---|
| `open-issues: no /dev, no /proc; ttyname cannot name the console` | open-issues |
| `open-issues: mktemp needs clock_gettime, not getpid+open` | open-issues |
| `fcntl: accept fd 0/1/2 for all subcommands, not just F_DUPFD` | new `fcntl_lowfd` test |
| `keyboard: Ctrl-[ produces ESC` | 0x1B |
| `open-issues: drop the resolved entries` | cleanup |

**The bump:**

| Commit subject | What |
|---|---|
| `handoff: rewrite fresh for the v0.6.9 bump` | **did not rewrite the body** — see the note at the top |
| `session-log: the rest of session 42` | session-42 commit rows |
| `ROADMAP: v0.6.9 is shipped; refresh hardening list` | ROADMAP |
| `kmain: bump the shell banner to v0.6.9` | banner |
| `README: refresh the applet and syscall lists for v0.6.9` | README |

**What it means:**

- **envp:** `execve` passes the caller's environment through
  verbatim.  `export FOO=bar` then `echo $FOO` prints `bar`;
  `busybox env` lists variables.  Regression-tested by
  `tests/envp_step1.c`.
- **The layout:** `/bin` holds busybox, `/usr/bin` holds the
  donix-native ELFs, `/tmp` is empty, `/` holds data.  **This is
  the split the two shells' search rules need** — ash's applets
  never consult `PATH` so busybox wins there; `musl_sh` searches
  `/usr/bin` first so a bare name resolves to the donix-native tool.
- **The shim:** `sys_execve`'s bare-name attempt is gone.  What
  remains is (a) the path as given and (b) the `"0:"` prefix
  translation — the smallest the shim can be without a VFS.
  `musl_exec2` now asserts that a bare name does *not* resolve.

**Key facts for future work:**

- **Every donix-native binary is staged BARE.**  `/usr/bin/HELLO`,
  not `HELLO.ELF`; `/bin/busybox`, no suffix.  **A file named with
  `.ELF` will not be found by anything.**
- **`execve` now takes a path.**  No bare-name resolution.  Callers
  pass `/usr/bin/NAME` or `/bin/NAME`.
- **ash does not export `PATH`.**  It keeps `PATH` as a shell
  variable, so `$PATH` expands and command lookup works, but a child
  does not see `PATH` in its environment.
- **The envp snapshot is `kmalloc`'d, not a stack array.**
  `EXEC_MAX_ENVC` × `EXEC_MAX_ARG_LEN` = 16 KB, and
  `PROC_STACK_SIZE` is 16 KB.  See `docs/gotchas.md`, "The kernel
  stack is 16 KB."
- **The canary is now `canary`, a program.**  `tests/canary.c`.
  Run from `donix>` or ash, both find `/usr/bin/CANARY`.
- **`clock_gettime` (228) is the clock.**  It reports `g_ticks`
  (100 Hz PIT) as both `CLOCK_REALTIME` and `CLOCK_MONOTONIC`.  No
  wall clock exists.
- **`nanosleep` (35) is a `hlt` loop** on a `g_ticks` deadline, 10 ms
  granularity.  Not a scheduler block; correct for a single-process
  system.
- **`munmap` (11) is real** and frees frames, removing them from
  `elf_page_list`.  The old stub was a leak.
- **`realpath` is enabled** and config-only.  `realpath
  /nonexistent` succeeds (parent `/` exists); `realpath
  /no/such/dir/file` fails on stderr.
- **`readlink` (89) returns `-ENOENT` for a missing path and
  `-EINVAL` for an existing non-symlink**, verified by
  `readlink_errno.c`.  Item 8 closed.
- **`/dev/null` works for `open`, `stat`, and `access`**, one
  predicate (`path_is_devnull`) shared by `open_resolved`,
  `stat_resolved`, and `access_resolved`.  `ls -l /dev/null`
  shows `crw-rw-rw-`; `readlink` returns `-EINVAL` for free.
  `readdir("/dev")` fails; no other device exists.  An exact-path
  predicate, not the dispatch seam.

### What `v0.6.8` contributed (prior milestone, pushed)

The `*at()` family: one resolver (`resolve_at`) for every
dirfd-taking syscall.  `newfstatat` (262), `openat` (257),
`unlinkat` (263), `faccessat` (269), `utimensat` (280) all route
through it; the stat family is inverted to wrappers as on Linux.
busybox `find` (with `-type`) is enabled and works.

The findings that milestone recorded, still load-bearing:
- **`unlinkat` has no consumer in busybox.**  `rm -r`
  (`libbb/remove_file.c`) and `find -delete`
  (`findutils/find.c:923-925`) construct path strings and call
  `unlink`/`rmdir`.  What made `rm -r` correct was the
  `unlink`/`rmdir` type check, not `unlinkat`.
- **`sys_faccessat` must not validate `flags`.**  musl calls it with
  three arguments, so `%r10` holds the previous syscall's return
  value.  `sys_utimensat` *does* validate — musl passes it four.

### Known limitations

- **Redirection of a builtin is silently ignored.**  `cd /bin > log`
  runs `cd`, creates no file, prints no error.
- **A builtin in a pipeline is refused.**  `cd /bin | cat` prints
  `sh: builtin in pipeline not supported`.
- **`/dev/null` works for `open`/`stat`/`access`** (session 43),
  but `readdir("/dev")` fails (no directory), and no other device
  exists — so `tty` still prints `not a tty`.  The full `/dev`
  layer is a direction, not scheduled.
- **`diff`, `chmod`, `ln`, `mount` are off** — the last three need
  their own syscalls.
- **`tty` prints `not a tty`.**  Correct for donix: no `/dev`, no
  `/proc`.  `readlink` (89) is implemented and now returns the
  correct errnos, but `ttyname` still cannot name the console.  See
  `open-issues.md`.
- **`musl_wait`'s WNOHANG loop spins.**  Pre-existing.  See
  `docs/session-log.md`, session 41.

---

## NEXT SESSION — the `/dev` + `/proc` direction

`realpath`, the `readlink` errno, and `/dev/null` across
open/stat/access are all done.  The next real work is the **`/dev`
and `/proc` device layer**.  It is not a scheduled milestone; it is
a direction, and the session that picks it up plans it then.  What
follows is the shape, not the schedule.

What the `/dev/null` work did **not** do, and what the direction
still needs:

- **A real `/dev` that `readdir` can list.**  Today `/dev/null` is
  an exact-path predicate, one name, not a directory.  `ls /dev`
  fails.  A device layer that appears in the path namespace is the
  thing that fixes this, and it is the dispatch seam's job.
- **`tty` naming its terminal.**  `ttyname(3)` walks
  `/proc/self/fd/N` then `/dev`.  Both fail today; with either one
  present, `tty` can print a path instead of `not a tty`.
- **`/dev/tty`, `/dev/urandom`.**

This is item 1's customer and the reason the dispatch seam gets
built.  See `ROADMAP.md`, "Make `/proc` possible."  **/proc is the
first feature the current architecture cannot express, so it forces
the seam.**  Symlinks and `/dev` are consumers of it.  Do not
implement standalone `/dev` handling in individual syscalls — the
`path_is_devnull` predicate and its three call sites are the one
deliberate exception, and they are recorded as such, to be deleted
when the seam lands.

### After that — candidates

- **Signal delivery (`SIGPIPE`, `SIGBUS`).**  `open-issues.md`
  item 5.  Also the prerequisite for job control, `kill(2)`, and a
  Wayland `wl_shm` client's `SIGBUS`.
- **The six remaining silent `vmm_map_page*` returns.**
  `open-issues.md` item 7.  A deliberate decision per site.
- **VFS layer.**  `open-issues.md` item 1.  Eventually; delete the
  remaining shims when it lands, do not extend them.
- **Wayland (long horizon).**  See `ROADMAP.md`.
- **PS/2 mouse driver + framebuffer cursor.**  The natural first
  step toward any interactive GUI.
- **Font size / resolution.**  See the `v0.6.7` "changing the VBE
  mode" note in `docs/session-log.md`.

---

## Busybox enablement

**All the applets in the handoff's original "Ready now" and "Needs
one small syscall" tables are now ENABLED, including `realpath`.**
The tables below are kept for the record and for the still-off ones.

### Enabled in session 43

`realpath` — config-only.  See session 43 above for the trace and
the test expectations.

### Enabled in session 42

`basename`, `dirname`, `unlink`, `ttysize`, `tty`, `arch`,
`mktemp`, `sleep`, `usleep`, `truncate`, plus `env`/`printenv`
(earlier in the session).

Syscalls added to unblock them: `readlink` (89), `clock_gettime`
(228), `nanosleep` (35).  `truncate` needed none — it uses
`ftruncate` (77), which was already there.

### Still off — needs a subsystem (do not enable yet)

| Config | Applet | Blocked by |
|---|---|---|
| `CONFIG_DIFF` | `diff` | `mmap` of files (non-anonymous `mmap`); deliberate |
| `CONFIG_CHMOD` | `chmod` | `chmod`/`fchmodat`; FAT has no permissions |
| `CONFIG_CHOWN` | `chown` | `chown`/`fchownat`; FAT has no ownership |
| `CONFIG_LN` | `ln` | `link`/`symlink`; FAT has no links |
| `CONFIG_LINK` | `link` | same |
| `CONFIG_MOUNT`/`UMOUNT` | `mount`/`umount` | `mount` (165); no VFS |
| `CONFIG_TAR`/`UNZIP`/`CPIO`/`GZIP`/`BZIP2`/`XZ` | archives | `mkdirat`, `symlinkat`, `utimensat` storage, file-backed `mmap`, decompression |
| `CONFIG_AWK` | `awk` | large; needs `FEATURE_AWK_LIBM`; `system()`/`getline` need signal delivery |
| `CONFIG_LESS`/`MORE` | pagers | raw-mode terminal control donix's line-discipline stubs do not model |
| `CONFIG_TOP`/`PS`/`KILL`/`PIDOF` | process tools | `/proc`; donix has none |
| `CONFIG_NETWORKING` (all) | `ping`, `wget`, etc. | no network stack |
| `CONFIG_FEATURE_FIND_DELETE` | `find -delete` | works via `unlink`/`rmdir`; needs `FEATURE_FIND_DEPTH`, also off |
| `CONFIG_ASH_JOB_CONTROL` | ash job control | signal delivery |
| `CONFIG_FEATURE_TAB_COMPLETION` | ash completion | needs `stat` on many paths; probably works, test it |

### The rule

**Enable an applet only when the syscalls it actually calls are
implemented — read the applet's source, do not guess from its
name.**  Two applets were mis-classified by name in session 42:
`truncate` (uses `ftruncate`, not `truncate(2)`) and `mktemp`
(needs `clock_gettime` through musl's `__randname`, not the
`getpid`+`open` the table said).  Read the source.

---

## Canary state

**Green as of session 43** — `canary` and `canary --full` from
both shells, 14/14 and 27/27, unchanged from the `v0.6.9` baseline.
The kernel self-test runs at boot and reports 17/17.

**The canary is a program: `canary`.**  `tests/canary.c` runs every
non-interactive canary row, checks exit status and output
substrings, and reports pass/fail.  Run from `donix>` or ash; both
find `/usr/bin/CANARY`.

    canary          # read-only rows
    canary --full   # also the mutating rows (create/remove under /)

**What stays manual**, printed at the end of a `canary` run:

    busybox ash      # interactive; then pwd, cd /bin, ls, exit
    vi test          # fills screen; :wq; ./test

**Regression tests (`userland/musl/tests/`, not canary rows):**

    at_step1       # resolve_at via dirfd; fstatat flags;
                   # AT_EMPTY_PATH; faccessat dirfd; utimensat dirfd
                   # (read-only)
    at_step2       # unlinkat + the unlink/rmdir type check
                   # (mutates the disk)
    envp_step1     # envp survives execve, 3 checks (forks)
    musl_exec2     # the bare-name shim is gone, 3 checks (forks)
    fcntl_lowfd    # fcntl on fd 0/1/2, 3 checks (redirects fd 0)
    readlink_errno # readlink(2) errno split: ENOENT for a missing
                   # path, EINVAL for an existing non-symlink, 3
                   # checks (read-only); closes item 8

**Pipe regression suite (`userland/musl/tests/`):**

    pipe_step1    # create, round-trip, close, re-close EBADF
                  # (no blocking assertions; see below)
    pipe_step2    # blocking + directed wake
    pipe_step3    # EOF, EPIPE, dup-aware counts
    pipe_step3b   # exit-path wake

Run these when changing `sys_read`/`sys_write`/`sys_close`/
`put_file_slot`/`sys_fork`/`sys_pipe` or adding a `FILE_KIND_*`.
**Run them, do not just read the rule** — the suite went six
sessions without being run after a `sys_read`/`sys_write` change,
and `pipe_step1` went stale and hung.  See `docs/gotchas.md`, "A
test can encode an earlier version's behavior."

**New tests must be added to both `USERLAND_ELFS` and the
`mcopy_one` chain in `05_boot_kernel64/Makefile`.**  The image now
stages 39 files.

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`; use
`busybox sh` or `/bin/busybox sh`.

**Expected noise:** no `Unknown syscall:` lines; no `[fd]` lines
(trace off); no `[a|b|c]` debug line (removed); no `[faccessat]`
trace line (removed).  The `FB: mapped N pages ...` line is
expected.  The `sys_open: f_open FAIL path=...` lines from `vi` on
a new file and from `busybox stat` on nonexistent paths (it probes
`/etc/group`, `/etc/passwd`, `/etc/localtime`) are expected
diagnostics.  `sys_execve: f_open FAIL` lines no longer appear, and
neither do the `f_open FAIL path=dev/null` lines from
`2>/dev/null` — that redirect now succeeds (session 43).

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **VFS layer (eventual).**  `sys_execve`'s two remaining path
   attempts and `resolve_against_cwd` are shims a VFS would
   subsume.  When a VFS lands, delete them; do not extend.
2. **Redirection of a builtin is silently ignored.**
3. **A builtin in a pipeline is refused.**
4. **`rename(2)` does not replace; `chmod`/`ln`/`mount` need their
   own syscalls.**  Deliberate FatFs-limitation first cuts.
5. **`-EPIPE` is delivered without `SIGPIPE`.**  Narrower than the
   old docs claimed: `busybox yes | busybox head -n 1` does *not*
   hang.  Fixing it means signal delivery.

Also open: `unlinkat` has no consumer; the six remaining silent
`vmm_map_page*` returns; `sys_brk`'s fixed `heap_base` and the
4 MB mmap window are latent collisions; real FatFs timestamp
storage; `prctl` is minimal; busybox applet symlinks not installed;
syscall-table audit script; `musl_wait`'s WNOHANG loop spins;
`sys_mmap` rejects all non-anonymous mappings; pipes support one
concurrent reader and one concurrent writer; `put_file_slot`'s pipe
wake is coupled to `sys_close`'s wake; **no `/dev` directory and no
`/proc`** — `/dev/null` is one name recognized by a predicate in
`open`/`stat`/`access`, not a listable directory.

---

## State on disk

**Real project root:** `/home/noneya/code/donix/`.

**The FAT layout:**

    /            HELLO-WORLD.TXT and other data
    /bin         busybox
    /usr/bin     the donix-native ELFs (apps + tests), staged BARE
    /tmp         empty

- `configs/busybox.config` — tracked canonical busybox config.
  Enabled applets: `cat`, `cp`, `cut`, `echo`, `env`, `false`,
  `find`, `head`, `ls`, `mkdir`, `mv`, `od`, `printenv`, `pwd`,
  `rm`, `rmdir`, `seq`, `sort`, `stat`, `tail`, `tee`, `test`,
  `touch`, `tr`, `true`, `uname`, `uniq`, `wc`, `yes`, `cmp`,
  `grep`, `sed`, `vi`, `clear`, `basename`, `dirname`, `unlink`,
  `ttysize`, `tty`, `arch`, `mktemp`, `sleep`, `usleep`,
  `truncate`, `realpath`, plus `ash`.
  `CONFIG_FEATURE_VI_WIN_RESIZE=y`.  `CONFIG_FIND=y` and
  `CONFIG_FEATURE_FIND_TYPE=y`.  Other `FEATURE_FIND_*` predicates
  off deliberately.  Off with reasons: `diff`, `chmod`, `ln`,
  `mount`, and the archive/network/process tools — see "Busybox
  enablement."
- `userland/musl/` — tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.  New tests must be added to both
  `USERLAND_ELFS` and the `mcopy_one` chain in
  `05_boot_kernel64/Makefile`.
- `04_kernel_64bit/fonts/ter-u18n.psf` — tracked font source.  The
  `.psf` is tracked; the generated `ter_u18n_data.c` is gitignored.
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  — gitignored; rebuild with `./toolchain/install_musl.sh`.
- `toolchain/{install_musl.sh,musl-gcc.sh}` — tracked.

Kernel sources: `04_kernel_64bit/`.

Scratch workspaces: transient, if any exist.  Not referenced here.

---

## Where things live

Not needed to start a session; ask for a file when the current task
needs it.  Paths relative to the tree root
(`/home/noneya/code/donix/`).

- `docs/strategy.md` — Phase A/B plan, rules, files-not-to-touch,
  tagging convention, git hygiene, recovery.
- `docs/gotchas.md` — every bug writeup, by subsystem.  Session 43
  added "A test can encode an earlier version's behavior" and "A
  fix with no test is indistinguishable from an unfixed defect."
  Session 42 added "The kernel stack is 16 KB," "A shim's dead
  code is only dead if you watch it not run," "An input-only
  `syscall` asm block does not tell GCC that `%rax` is
  overwritten," and "A hand-counted string length in a syscall
  wrapper will be wrong."
- `docs/session-log.md` — commit tables and per-test canary notes.
  **Session 42's rows are written, and session 43's rows are folded
  in as a tail on session 42.**
- `docs/open-issues.md` — full open-issues list.  Session 43
  sharpened the `/dev` entry with the `2>/dev/null` finding,
  recorded `/dev/null` working across open/stat/access, and
  **removed item 8** (the `readlink` errno defect), closed by
  `readlink_errno.c`.
- `docs/migration-history.md`, `docs/dons-os-history.md` —
  historical narrative (A1-A6, pre-fork).
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` — the first two
  frozen at `v0.6.0`; `LLD_BUG_REPORT.md` current.
- `ROADMAP.md` — future work only; direction of travel beyond the
  current milestone.

---

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  `v0.6.7` shipped the shell and the framebuffer;
`v0.6.8` shipped the `*at()` family; `v0.6.9` shipped envp, the
`/usr/bin` layout, the `execve` shim removal, the huge-page-split
`#PF` fix, four syscalls, and ten busybox applets — tagged and
pushed.  Session 43 added `realpath` (config-only), closed
`readlink`'s errno defect with a regression test, and made
`/dev/null` work across open, stat, and access.  Next: the
`/dev`+`/proc` direction, which forces the pathname dispatch seam.
One change at a time.**

---

## How to use this file

At session start, paste this file and say "Continue from here and
request any files you need."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.

**When a session's findings change an earlier numbered step, edit
the step in place — do not just add a paragraph above it.**  The
session-43 handoff carried a "NEXT SESSION" paragraph that stated
the `realpath` conclusion and a numbered step 5 that still said
"read the source, it may need more" — both live, both current-
looking, contradicting.  The fix, now applied: the step carries
the finding, and the rule moves to "apply to the next candidate."
A document assembled from parts carries the state of each part,
not the state of the whole.  This is the same lesson as "read the
diff, not the subject."

**Two session-43 gotchas worth reading before the next change.**
"A fix with no test is indistinguishable from an unfixed defect":
`sys_readlink` was correct while three docs said it was not; when
a doc entry names its own fix, `grep` before scheduling the work.
"A test can encode an earlier version's behavior": `pipe_step1`'s
EAGAIN assertions were Step-1 semantics, and after read/write
began blocking they hung instead of failing.  Run the suite a
change's own rule names, or the staleness is invisible.  See
`docs/gotchas.md`.
