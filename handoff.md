Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-01 (session 42, envp)
**Current HEAD:** branch `dev`, two commits past `v0.6.8`; scratch
tags `20261001-envp`, `20261001-env-applets` (both local)
**Last milestone:** `v0.6.8` (published) — the `*at()` family;
`resolve_at`, `newfstatat` (262), `openat` (257), `unlinkat` (263),
`faccessat` (269), `utimensat` (280), all through one resolver
**Milestone status:** **`v0.6.9` open — envp.**  `20261001-envp`
and `20261001-env-applets` are the first two commits.  The milestone
is small on purpose: one subsystem, with a real consumer.

Commits are named by tag only, never by SHA.  **Working tags
(`2026*`) are local scratch restore points** — they exist while a
milestone is being developed and are **dropped before the milestone
is pushed**.  Only `v*` tags go to the remote and are permanent.
The commit record is `docs/session-log.md`; the commit message
carries the narrative.  Once a scratch tag is dropped it resolves
to nothing; do not cite one as if it were a stable reference.

**Scratch tags are annotated** (session 37 onward).  The annotation
is the fuller per-commit summary; at a milestone bump the
annotations seed the final milestone narrative.  This is the reason
to keep tagging each step even when no `v*` tag is imminent.

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

## Where we are — `v0.6.9` (envp) in progress

`v0.6.8` shipped the `*at()` family and is pushed.  `v0.6.9` is
**envp**: `sys_execve` ignored its third argument, so every program
ran with an empty environment.  That is now fixed.

### Session 42 — envp

| Tag (kept, milestone open) | What |
|---|---|
| `20261001-envp` | `sys_execve` copies `envp` onto the new stack, as Linux does; argv region 4 KB → 16 KB; envp snapshot kmalloc'd |
| `20261001-env-applets` | config: enable busybox `env` and `printenv` |

**What it means:** `execve` now passes the caller's environment
through **verbatim** (Linux semantics — the shell builds it, the
kernel carries it).  `export FOO=bar` in ash, then `echo $FOO`,
prints `bar`; before this session it printed empty.  `busybox env`
lists `VAR=value` lines.

**Key facts for future work:**
- **The envp snapshot is `kmalloc`'d, not a stack array.**
  `PROC_STACK_SIZE` is 16 KB (`process.h`); `EXEC_MAX_ENVC` ×
  `EXEC_MAX_ARG_LEN` is 64 × 256 = 16 KB.  A stack array that size
  would overflow.  The argv snapshot stayed a stack array (4 KB).
  See `docs/gotchas.md`, "The kernel stack is 16 KB; do not put a
  large scratch buffer on it."  **Before adding a scratch buffer to
  a syscall, check `PROC_STACK_SIZE` and count bytes.**
- **The argv region is now 16 KB, not 4 KB.**  It sits at the top
  of the 64 KB user stack; `rsp_init` is one slot below its bottom.
  A larger region only matters if a shell's argv+envp approaches
  16 KB, which it does not.
- **busybox ash does not export `PATH`.**  It keeps `PATH` as a
  shell variable, so `$PATH` expands and command lookup works, but
  a child does not see `PATH` in its environment unless the user
  runs `export PATH`.  This is ash's behavior, not a donix bug, but
  it means every child applet runs with no `PATH` in `envp`.
- **`env` and `printenv` are now enabled.**  Before
  `20261001-env-applets` they reported "applet not found" —
  disabled, not broken.

### What `v0.6.8` contributed (prior milestone, pushed)

The `*at()` family: one resolver (`resolve_at`) for every
dirfd-taking syscall.  `newfstatat` (262), `openat` (257),
`unlinkat` (263), `faccessat` (269), `utimensat` (280) all route
through it; the stat family is inverted to wrappers as on Linux.
busybox `find` (with `-type`) is enabled and works.

The two findings that milestone recorded, still load-bearing:
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
- **`musl_wait`'s WNOHANG loop spins.**  Pre-existing (present at
  `v0.6.6`); the spin's wall-clock duration increased between
  `v0.6.6` (VGA text) and `v0.6.7` (framebuffer).  Not a
  regression.  See `docs/session-log.md`, session 41.

---

## NEXT SESSION — pick one

`v0.6.9` is one subsystem and is closeable.  Candidates, in the
order I'd rank them:

### Finishing `v0.6.9`

1. **A dedicated envp regression test.**  The `$FOO` round trip is
   proven by hand; a `userland/musl/tests/` app that forks, sets a
   variable, `execve`s a helper that prints `getenv`, and checks the
   value would make it a one-command check.  Small, and it closes
   the milestone with a regression suite rather than a manual run.
2. **The free busybox applets.**  `basename`, `dirname`, `readlink`,
   `realpath`, `truncate`, `sleep`, `usleep`, `unlink`.  Each is
   gated on a syscall donix has or a small one; each is pure
   userland or a thin wrapper.  Add one at a time, exercising the
   applet before the commit.  **`env`/`printenv` are done**; the
   rest of the environment-using half is now unblocked by the envp
   change.

### Small, close gaps (non-theme)

1. **Ctrl-`[` as ESC** — `scancode_to_ascii` has no Ctrl parameter
   yet; touches the keyboard layer.  Low urgency.
2. **`sys_fcntl` fd < 3 for the other subcommands** — `F_GETFL`,
   `F_SETFL`, `F_GETFD`, `F_SETFD` still refuse `fd < 3`.  Not on
   any current path.
3. **`sys_munmap`** — a stub returning 0.

### Deliberately later — larger

- **Signal delivery (`SIGPIPE`, `SIGBUS`).**  See `open-issues.md`
  item 6.  A subsystem (real `sys_rt_sigaction`, per-process
  handlers, a `SIGPIPE` raise on the `-EPIPE` write path).  Also
  the prerequisite for job control and `kill(2)`, and for a Wayland
  `wl_shm` client's `SIGBUS`.
- **VFS layer.**  `open-issues.md` item 1.  Eventually; delete the
  shims when it lands, do not extend them.  Note: the VFS is
  **not** a Wayland prerequisite; see `ROADMAP.md`.
- **Wayland (long horizon).**  See `ROADMAP.md`.  Not a `v0.6.x`
  target; the one concrete kernel gap it shares with existing work
  is non-anonymous `mmap`.
- **PS/2 mouse driver + framebuffer cursor.**  Not on the roadmap
  yet, but the natural first step toward any interactive GUI.
- **Font size / resolution.**  The console is 10×18 at 1024×768.
  A bigger glyph (`ter-u24n.psf`, 12×24) or a bigger mode is a
  data change, but **see the `v0.6.7` "changing the VBE mode" note
  in `docs/session-log.md`** — it also needs the grid constants and
  possibly `fb.c`.

---

## Busybox enablement

Every applet and feature in `configs/busybox.config` that is off,
with whether donix can support it today.  **Rule: enable an applet
only when the syscalls it actually calls are implemented — read the
applet's source, do not guess from its name.**

### Ready now (syscalls all present)

| Config | Applet | Needs | Notes |
|---|---|---|---|
| `CONFIG_BASENAME` | `basename` | none (string op) | off; pure userland |
| `CONFIG_DIRNAME` | `dirname` | none (string op) | off; pure userland |
| `CONFIG_UNLINK` | `unlink` | `unlink` (87) | done — applet just off |
| `CONFIG_MKTEMP` | `mktemp` | `getpid` (39) + `open` (2) | both present; check source |
| `CONFIG_TTY` | `tty` | `ioctl` (16) | present; check source |
| `CONFIG_TTYSIZE` | `ttysize` | `ioctl` (16) | present; check source |
| `CONFIG_UNAME`-adjacent | `arch`, `bb_arch` | `uname` (63) | present; check source |

### Needs one small syscall first

| Config | Applet | Missing syscall | Size |
|---|---|---|---|
| `CONFIG_READLINK` | `readlink` | `readlink` (89) | small — FAT has no symlinks, so an honest `-EINVAL`/`-ENOENT` may be all it needs |
| `CONFIG_REALPATH` | `realpath` | `readlink` (89) | follows from `readlink` |
| `CONFIG_SLEEP` | `sleep` | `nanosleep` (35) | small — a `g_ticks` deadline loop |
| `CONFIG_USLEEP` | `usleep` | `nanosleep` (35) | same |
| `CONFIG_TRUNCATE` | `truncate` | `truncate` (76) | small — mirrors `ftruncate` by path |

### Needs a subsystem (do not enable yet)

| Config | Applet | Blocked by |
|---|---|---|
| `CONFIG_DIFF` | `diff` | `mmap` of files (non-anonymous `mmap`); deliberate |
| `CONFIG_CHMOD` | `chmod` | `chmod`/`fchmodat`; FAT has no permissions |
| `CONFIG_CHOWN` | `chown` | `chown`/`fchownat`; FAT has no ownership |
| `CONFIG_LN` | `ln` | `link`/`symlink`; FAT has no links |
| `CONFIG_LINK` | `link` | same |
| `CONFIG_MOUNT`/`UMOUNT` | `mount`/`umount` | `mount` (165); no VFS |
| `CONFIG_TAR`/`UNZIP`/`CPIO`/`GZIP`/`BZIP2`/`XZ` | archives | `mkdirat`, `symlinkat`, `utimensat` storage, file-backed `mmap`, decompression |
| `CONFIG_AWK` | `awk` | `fork`/`execve` work, but `awk` is large and needs `FEATURE_AWK_LIBM`; its `system()`/`getline` paths need signal delivery |
| `CONFIG_LESS`/`MORE` | pagers | `ioctl` works, but they need raw-mode terminal control donix's line-discipline stubs do not model |
| `CONFIG_TOP`/`PS`/`KILL`/`PIDOF` | process tools | `/proc`; donix has none |
| `CONFIG_NETWORKING` (all) | `ping`, `wget`, etc. | no network stack |
| `CONFIG_FEATURE_FIND_DELETE` | `find -delete` | works today via `unlink`/`rmdir`; needs `FEATURE_FIND_DEPTH` (post-order walk), also off |
| `CONFIG_ASH_JOB_CONTROL` | ash job control | signal delivery |
| `CONFIG_FEATURE_EDITING_HISTORY`-adjacent (`FEATURE_TAB_COMPLETION`) | ash completion | needs `stat` on many paths; probably works, test it |

### Suggested order (one per commit)

1. **`basename`, `dirname`, `unlink`** — free, no new syscall.
2. **`mktemp`, `tty`, `ttysize`, `arch`** — check each source for
   the syscalls it actually calls; enable the ones that only use
   what exists.
3. **`sleep`, `usleep`** — add `nanosleep` (35) as a `g_ticks`
   deadline loop, then enable both.
4. **`truncate`** — add `truncate` (76) by path (mirror
   `ftruncate`), then enable.
5. **`readlink`, `realpath`** — add `readlink` (89) as an honest
   `-EINVAL` on FAT (no symlinks), then enable.

Each step: read the applet's source, enable, exercise from ash,
commit.  Do not batch-enable without exercising.

---

## Canary state

**The focused canary is green as of `20261001-env-applets`.**  Full
table in `docs/session-log.md`.

    # on boot, ash is already running
    pwd                         # /
    cd /bin
    pwd                         # /bin
    ls                          # busybox
    cd ..
    pwd                         # /
    ls                          # full root listing
    exit                        # back to donix>
    pwd                         # /
    cd /bin
    pwd                         # /bin
    ls                          # busybox (donix-native ls)
    cd /
    pwd                         # /
    ls hello-world.txt
    memtest
    musl_fork
    musl_exec2
    musl_wait
    busybox ls
    busybox pwd                 # / (after cd /)
    busybox ash
    # at the ash prompt: pwd, cd /bin, pwd, ls, exit
    # back at donix>: hello

**Read-only `uniq` rows (added v0.6.5):**

    uniq hello-world.txt
    uniq -c < hello-world.txt

**Pipeline rows (added v0.6.6):**

    cat hello-world.txt | head -n 2
    echo hi | wc
    echo hello | cat

**Redirection rows (added v0.6.7):**

    echo hi > out.txt ; cat out.txt
    echo hi2 >> out.txt ; cat out.txt
    cat < out.txt
    busybox cat < out.txt

**`find` rows (added `20260930-at`):**

    find /bin                  # /bin, /bin/busybox
    find / -type d             # /, /bin
    find / -type f -name busybox

**`rm`/`rmdir` rows (added `20261001-unlinkat`, from busybox):**

    busybox touch /t ; busybox rm /t            # file: create, remove
    busybox mkdir /d ; busybox rmdir /d         # empty dir: create, remove
    busybox mkdir /d ; busybox rm /d            # rm on a dir -> busybox reports an error
    busybox mkdir /t ; busybox touch /t/a ; busybox rm -r /t   # recursive

These are one-offs, not canary rows (they mutate the disk).

**envp rows (added `20261001-env-applets`, not canary rows — they
mutate the shell's environment):**

    # from ash:
    export FOO=bar
    echo $FOO                   # bar
    env                         # FOO=bar, PWD=/
    export PATH
    printenv PATH               # /sbin:/usr/sbin:/bin:/usr/bin

`env` showing `PWD=/` is ash exporting `PWD`; `FOO=bar` after
`export FOO=bar` is the environment round-tripping through
`execve`.  `printenv PATH` is empty **until** `export PATH`, because
busybox ash does not export `PATH` — that is ash's behavior, not a
bug.

**`*at` regression tests (`userland/musl/tests/`, not canary rows):**

    at_step1    # resolve_at via dirfd; fstatat flags; AT_EMPTY_PATH;
                # faccessat dirfd; utimensat dirfd  (read-only, 10/10)
    at_step2    # unlinkat + the unlink/rmdir type check (mutates the disk)

**Pipe regression suite (`userland/musl/tests/`):**

    pipe_step1    # object + non-blocking I/O
    pipe_step2    # blocking + directed wake
    pipe_step3    # EOF, EPIPE, dup-aware counts
    pipe_step3b   # exit-path wake

Run these when changing `sys_read`/`sys_write`/`sys_close`/
`put_file_slot`/`sys_fork`/`sys_pipe` or adding a `FILE_KIND_*`.
Not canary rows (they fork), but they are the only pipe regression
suite.

**Framebuffer / `vi` verification (added v0.6.7, not canary rows):**

    vi test            # fills the screen; status line on the last row
    # edit, :wq
    ./test             # the saved script runs

**Cursor (added `20260930-cursor`, not a canary row):**

    # At any shell prompt on the framebuffer console, the block
    # cursor at the current input position blinks at 500 ms per
    # state.  Type a character: the cursor moves and does not leave
    # an inverted cell behind.  `vi test` puts the cursor at the
    # edit position; it is hidden while the screen is redrawn and
    # restored on exit.

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`; use
`busybox sh` or `/bin/busybox sh`.

**Canary rows must not mutate the disk.**  Read-only applets can be
canary rows; `tee`, `cp`, `mv`, redirection, pipelines, and `vi`
mutate or fork, so they stay one-offs.

**Expected noise:** no `Unknown syscall:` lines; no `[fd]` lines
(trace off); no `[a|b|c]` debug line (removed); no `[faccessat]`
trace line (session-41 diagnostic, removed).  The `FB: mapped N
pages ...` line is expected.  The `sys_open: f_open FAIL path=...`
lines from `vi` on a new file, and from `busybox stat` on
nonexistent paths, are expected diagnostics.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **VFS layer (eventual).**  `sys_execve`'s three-attempt path
   resolution and `resolve_against_cwd` are shims a VFS would
   subsume.  When a VFS lands, delete them; do not extend.
2. **Redirection of a builtin is silently ignored.**
3. **A builtin in a pipeline is refused.**
4. **`sys_fcntl` refuses fd < 3** for subcommands other than
   `F_DUPFD`/`F_DUPFD_CLOEXEC`.  Deliberate; not on any path.
5. **`-EPIPE` is delivered without `SIGPIPE`.**  Narrower than the
   old docs claimed: `busybox yes | busybox head -n 1` does *not*
   hang.  Fixing it means signal delivery.

Also open: `unlinkat` has no consumer; Ctrl- `[` not mapped to ESC;
`sys_munmap` is a stub returning 0; `sys_brk`'s fixed `heap_base`
and the 4 MB mmap window are latent collisions; real FatFs
timestamp storage; `prctl` is minimal; busybox applet symlinks not
installed; syscall-table audit script; `musl_wait`'s WNOHANG loop
spins (pre-existing; duration increased at the console swap);
`sys_mmap` rejects all non-anonymous mappings; pipes support one
concurrent reader and one concurrent writer; `put_file_slot`'s pipe
wake is coupled to `sys_close`'s wake.

---

## State on disk

**Real project root:** `/home/noneya/code/donix/`.

- `configs/busybox.config` — tracked canonical busybox config.
  Enabled applets: `cat`, `cp`, `cut`, `echo`, `env`, `false`,
  `find`, `head`, `ls`, `mkdir`, `mv`, `od`, `printenv`, `pwd`,
  `rm`, `rmdir`, `seq`, `sort`, `stat`, `tail`, `tee`, `test`,
  `touch`, `tr`, `true`, `uname`, `uniq`, `wc`, `yes`, `cmp`,
  `grep`, `sed`, `vi`, `clear`, plus `ash`.
  `CONFIG_FEATURE_VI_WIN_RESIZE=y`.  `CONFIG_FIND=y` and
  `CONFIG_FEATURE_FIND_TYPE=y`.  `CONFIG_ENV=y`,
  `CONFIG_PRINTENV=y` (added `20261001-env-applets`).  The other
  `FEATURE_FIND_*` predicates are off deliberately.  Off (with
  reasons): `diff` (deliberate), `chmod`/`ln`/`mount` (need kernel
  work).  See "Busybox enablement" above for what can be turned on
  next.
- `userland/musl/` — tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.  New tests must be added to both
  `USERLAND_ELFS` and the `mcopy_one` chain in
  `05_boot_kernel64/Makefile`.  `tests/at_step1.c` (sessions 39,
  41) and `tests/at_step2.c` (session 40) are the `*at` regression
  suites.
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
- `docs/gotchas.md` — every bug writeup, by subsystem.  A growing
  family of "a decision correct only for the cases known at the
  time": the low-fd and fd-kind entries (sessions 34, 36), the
  multi-write-interleave entry (session 37), the byte-order
  entries (session 38), the naming/bookkeeping/PIT entries
  (session 39), "A consumer inferred from behavior is not a
  consumer" (session 40), "A syscall argument the caller did not
  set holds the previous syscall's return value" and "The
  incremental kernel build can silently skip" (session 41), and
  "The kernel stack is 16 KB; do not put a large scratch buffer on
  it" (session 42).  Read them together; expect more.
- `docs/session-log.md` — commit tables and per-test canary notes.
  Rows named by scratch tag.
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
`v0.6.8` shipped the `*at()` family.  `v0.6.9` is envp, and it is
in progress: `execve` now passes the caller's environment through
verbatim, so `export FOO=bar` then `echo $FOO` prints `bar`, and
`busybox env` lists variables.  The envp snapshot is kmalloc'd
because the kernel stack is 16 KB.  One change at a time.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
