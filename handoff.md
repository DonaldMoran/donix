Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-02 (session 42, cont.)
**Current HEAD:** branch `dev`, 25 commits past `origin/dev`;
24 scratch tags, all `20261001-*` (local; see the tag list below)
**Last milestone:** `v0.6.8` (published) — the `*at()` family;
`resolve_at`, `newfstatat` (262), `openat` (257), `unlinkat` (263),
`faccessat` (269), `utimensat` (280), all through one resolver
**Milestone status:** **`v0.6.9` complete, at the bump.**  Session
42 grew it far past its opening scope (envp, layout, shim) — see
"Where we are."  Next step is the milestone bump: drop the scratch
tags, write the `v0.6.9` narrative, rewrite this file, push.

Commits are named by tag only, never by SHA.  **Working tags
(`20261001-*`) are local scratch restore points** — they exist while
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

## Where we are — `v0.6.9` complete, at the bump

`v0.6.8` shipped the `*at()` family and is pushed.  `v0.6.9` opened
as **envp** and grew, in session 42, into the **`/usr/bin` layout**,
the **removal of `execve`'s bare-name guess**, a **page-table bug
fix**, **four new syscalls**, **ten busybox applets**, **three
small gaps**, and **four new gotchas**.  It is no longer a
three-subject milestone; it is a large one.  See "Session 42, in
full" below.

### Session 42, in full

**The opening themes (envp, layout, shim):**

| Tag | What |
|---|---|
| `20261001-envp` | `sys_execve` copies `envp` onto the new stack; argv region 4 KB → 16 KB; envp snapshot kmalloc'd |
| `20261001-env-applets` | config: enable busybox `env` and `printenv` |
| `20261001-envdocs` | session-42 docs (first pass) |
| `20261001-usrbin` | executables to `/usr/bin`; `canary.c` the smoke test |
| `20261001-nosuffix` | drop the `.ELF` suffix; binaries staged bare |
| `20261001-noshim` | `execve`: bare-name attempt and the `.ELF` helpers removed |
| `20261001-session42-docs` | session-42 docs (second pass) |

**The envp regression test and the bugs it surfaced:**

| Tag | What |
|---|---|
| `20261001-envtest` | `tests/envp_step1.c` + `envp_helper.c`; proved envp survives `execve`.  Found the `%rax`-clobber bug in `puts_raw` |
| `20261001-lenfix` | `puts_raw` computes its own length; `musl_exec2` rewritten to assert the shim is gone; two gotchas added |

**The page-table bug:**

| Tag | What |
|---|---|
| `20261001-splitfix` | the huge-page split in `vmm_map_page_in_cr3` no longer silently returns on allocation failure; it halts with `VMM: FATAL`.  The intermittent `#PF` at `0x400000` |
| `20261001-splitdiag-off` | removed the temporary `VMM: SPLIT` diagnostic prints |
| `20261001-vmm-issues` | recorded the six remaining silent `vmm_map_page*` returns as item 7 |

**The dead code and the syscalls:**

| Tag | What |
|---|---|
| `20261001-deadname` | deleted `exec_resolve_bin_name`, dead since the shim removal |
| `20261001-readlink` | `readlink` (89) as an honest `-EINVAL` (no symlinks); removes `ttyname`'s `Unknown syscall: 89` noise |
| `20261001-clock` | `clock_gettime` (228) from `g_ticks`; enables `mktemp` |
| `20261001-nanosleep` | `nanosleep` (35) as a `g_ticks` deadline loop; enables `sleep`, `usleep` |
| `20261001-munmap` | `munmap` (11) for real — the stub leaked; mallocng calls it |

**The applets:**

| Tag | What |
|---|---|
| `20261001-applets-free` | `basename`, `dirname`, `unlink` |
| `20261001-applets-tty` | `ttysize`, `tty`, `arch` |
| `20261001-truncate` | `truncate` (config-only — it uses `ftruncate`, not `truncate(2)`) |

**The doc cleanups and the last gaps:**

| Tag | What |
|---|---|
| `20261001-dev-issue` | open-issues: no `/dev`, no `/proc`; `ttyname` cannot name the console |
| `20261001-mktemp-issue` | open-issues: `mktemp` needs `clock_gettime`, not `getpid`+`open` |
| `20261001-fcntl-lowfd` | `sys_fcntl` accepts fd 0/1/2 for all subcommands; new `fcntl_lowfd` test |
| `20261001-ctrl-bracket` | Ctrl-`[` produces ESC (0x1B) |
| `20261001-issues-cleanup` | removed the resolved entries from `open-issues.md` |

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
- **`diff`, `chmod`, `ln`, `mount` are off** — the last three need
  their own syscalls.
- **`tty` prints `not a tty`.**  Correct for donix: no `/dev`, no
  `/proc`.  `readlink` (89) is implemented, so no `Unknown syscall`
  noise, but `ttyname` still cannot name the console.  See
  `open-issues.md`.
- **`musl_wait`'s WNOHANG loop spins.**  Pre-existing.  See
  `docs/session-log.md`, session 41.

---

## NEXT SESSION — the bump, then what follows

**The immediate task is the `v0.6.9` bump**, which is a session-end
operation:

1. Harvest the 24 scratch-tag annotations into `docs/session-log.md`
   as the session-42 commit table.
2. Write the `v0.6.9` tag annotation from them — the milestone
   narrative.
3. Rewrite this file fresh.
4. Drop the scratch tags:
   ```
   git tag -d 20261001-envp 20261001-env-applets ... (all 24)
   ```
5. `git tag -a v0.6.9` on HEAD.
6. Push `dev` and `v0.6.9` to `origin`.

### After the bump — candidates

- **The remaining busybox applets.**  `realpath` (needs `readlink`,
  done — config-only now).  Then the "needs a subsystem" set stays
  off for the reasons listed below.
- **The `/dev` and `/proc` device layer.**  Unblocks `tty` naming
  its terminal, `/dev/null`, `/dev/urandom`.  A subsystem.
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
one small syscall" tables are now ENABLED.**  The tables below are
kept for the record and for the still-off ones.

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
| `CONFIG_REALPATH` | `realpath` | config-only now (`readlink` done); not yet enabled |
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
name.**  Two applets were mis-classified by name this session:
`truncate` (uses `ftruncate`, not `truncate(2)`) and `mktemp`
(needs `clock_gettime` through musl's `__randname`, not the
`getpid`+`open` the table said).  Read the source.

---

## Canary state

**Green as of `20261001-fcntl-lowfd`** — `canary` and `canary
--full` from both shells, 14/14 and 27/27.  The kernel self-test
runs at boot and reports 17/17.

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

**Pipe regression suite (`userland/musl/tests/`):**

    pipe_step1    # object + non-blocking I/O
    pipe_step2    # blocking + directed wake
    pipe_step3    # EOF, EPIPE, dup-aware counts
    pipe_step3b   # exit-path wake

Run these when changing `sys_read`/`sys_write`/`sys_close`/
`put_file_slot`/`sys_fork`/`sys_pipe` or adding a `FILE_KIND_*`.

**New tests this session must be added to both `USERLAND_ELFS` and
the `mcopy_one` chain in `05_boot_kernel64/Makefile`.**  The image
now stages 38 files.

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`; use
`busybox sh` or `/bin/busybox sh`.

**Expected noise:** no `Unknown syscall:` lines; no `[fd]` lines
(trace off); no `[a|b|c]` debug line (removed); no `[faccessat]`
trace line (removed).  The `FB: mapped N pages ...` line is
expected.  The `sys_open: f_open FAIL path=...` lines from `vi` on
a new file and from `busybox stat` on nonexistent paths are
expected diagnostics.  `sys_execve: f_open FAIL` lines no longer
appear.

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
wake is coupled to `sys_close`'s wake; no `/dev`, no `/proc`.

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
  `truncate`, plus `ash`.
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
- `docs/gotchas.md` — every bug writeup, by subsystem.  Session 42
  added: "The kernel stack is 16 KB" and "A shim's dead code is
  only dead if you watch it not run" (the first pass), plus
  "An input-only `syscall` asm block does not tell GCC that `%rax`
  is overwritten" and "A hand-counted string length in a syscall
  wrapper will be wrong."  Read them together; expect more.
- `docs/session-log.md` — commit tables and per-test canary notes.
  Rows named by scratch tag.  **Session 42's rows are not yet
  written** — that is part of the bump.
- `docs/open-issues.md` — full open-issues list.
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
`v0.6.8` shipped the `*at()` family.  `v0.6.9` — session 42 — is
complete and at the bump: envp passes through `execve`, the
donix-native binaries live in `/usr/bin` (busybox in `/bin`, bare
names, no `.ELF` suffix), `sys_execve`'s bare-name guess is gone,
the huge-page-split `#PF` is fixed, and four syscalls (`readlink`,
`clock_gettime`, `nanosleep`, `munmap`) plus ten busybox applets
landed.  Twenty-five commits, 24 scratch tags.  Next: the bump.
One change at a time.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
