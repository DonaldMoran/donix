Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-01 (session 40, `unlinkat` done)
**Current HEAD:** branch `dev`, nine commits past `origin/dev`;
scratch tags `20261001-atrefactor`, `20261001-unlinkat`,
`20261001-docs` (all local)
**Last milestone:** `v0.6.7` (published) — `musl_sh` is a real shell;
the console is a 1024×768 linear framebuffer
**Milestone status:** **open — `v0.6.8` in progress.**  `20260930-at`
opened it; `20261001-atrefactor` and `20261001-unlinkat` continue it.
`dev` is nine commits ahead of `origin/dev`, not pushed.  Scratch
tags are local and are dropped before the milestone is pushed.

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

## Where we are — `v0.6.8` in progress

`v0.6.7` shipped the shell and the framebuffer.  The current
milestone, `v0.6.8`, is the **`*at()` family**.  It opened in
session 39 with the resolver and the two syscalls busybox `find`
needs.  Session 40 continued it with `unlinkat` and the
file-vs-directory type check.

### Session 40 — `unlinkat`; `unlink`/`rmdir` type check

| Tag (kept, milestone open) | What |
|---|---|
| `20261001-atrefactor` | `unlink_body` extracted; `sys_unlink`/`sys_rmdir` become `resolve_at` wrappers (pure refactor) |
| `20261001-unlinkat` | `unlinkat` (263); `want_dir` + `f_stat_with_retry` type check; `cli` critical section; `at_step2.c` |

**What it means:** `unlink`/`rmdir` now enforce Linux's
file-vs-directory distinction — `unlink(dir)` is `-EISDIR`,
`rmdir(file)` is `-ENOTDIR`, `rmdir("/")` is `-EBUSY`.  Before this,
FatFs's `f_unlink` accepted both, so the two syscalls were silently
interchangeable.  `unlinkat` (263) is implemented and tested.

**The finding that matters:** `unlinkat` has **no consumer** in
busybox — not `rm -r`, not `find -delete`, not any config-flagged
applet.  It was added on the belief that `rm -r` needs it; both
`rm -r` (`libbb/remove_file.c`) and `find -delete`
(`findutils/find.c:923-925`) construct a path string and call
`unlink`/`rmdir`.  What actually made `rm -r` work was the type
check in the *same* commit, which turned `remove_file`'s
`lstat`-branch from advisory into load-bearing.  See
`docs/gotchas.md`, "A consumer inferred from behavior is not a
consumer."

**Key facts for future work:**
- **`unlink_body(abs_path, want_dir, tag)` is the one unlink body.**
  `sys_unlink` (`want_dir=0`), `sys_rmdir` (`want_dir=1`), and
  `sys_unlinkat` (`want_dir = flags & AT_REMOVEDIR`) all funnel
  through it via `resolve_at`.  Do **not** grow a second copy.
- **The type check and the `f_unlink` are one `cli` critical
  section**, released by restoring the caller's saved RFLAGS (not
  `sti`'d, because `unlink_body` can run inside `sys_execve`'s
  `cli`).  `cli` is safe across FatFs because `diskio.c` is
  synchronous and polling (`ata_poll_bsy_clear`/`ata_poll_drq`
  spin on the ATA status port; no `hlt`, no IRQ wait).  If a disk
  op ever waits on an IRQ, this becomes a hang — the fix would be a
  lock, not `cli`.
- **`unlinkat` (263) is correct but unconsumed.**  `at_step2` is
  its only exerciser.
- **`sys_faccessat` (269) still ignores `dirfd`/`flags`** — a
  latent bug, noted in `open-issues.md`.

### Session 39 — the `*at()` family; `find`

| Tag (kept, milestone open) | What |
|---|---|
| `20260930-at` | `resolve_at`, `file_slot_t.dir_path`, `newfstatat` (262), `openat` (257); stat family inverted to wrappers; `find` enabled with `-type` |
| `20260930-cursor` | software block cursor on the framebuffer; PIT-rate comment correction |

**What it means:** the `*at()` path-resolution rule now exists in
one place.  `newfstatat(2)` and `openat(2)` are implemented; the
busybox `find` applet is enabled and works — `find /bin`,
`find / -type d`, and `find / -type f -name busybox` all behave.
`find` recurses with `openat` and stats entries with
`newfstatat`, exactly as on Linux.
- The cursor came back too.  `v0.6.7` moved the console to the
  framebuffer and the cursor silently disappeared — the VGA text
  backend had a hardware cursor, the framebuffer has none, and
  nothing drew a software replacement.  `20260930-cursor` adds a
  software block cursor: inverse-video repaint of the cursor cell,
  drawn and erased through the existing `paint_cell` chokepoint, so
  it works on the framebuffer and the VGA text fallback is
  unchanged.  Blink is driven from the PIT tick (100 Hz), 500 ms per
  state.
**Key facts for future work:**
- **`resolve_at(dirfd, path, out, cap)` is the one resolver.**  An
  absolute path ignores `dirfd`; `AT_FDCWD` delegates to
  `resolve_against_cwd`; a real dirfd concatenates the directory
  slot's stored path with the relative path.  Every `*at` syscall
  calls it.  Do **not** grow a second copy.
- **`file_slot_t.dir_path`** is a `kmalloc`'d absolute Unix-form
  path, set only for `FILE_KIND_DIR` slots, freed in
  `put_file_slot`.  It rides `fork`/`dup2` via refcount slot
  sharing.  Stored in **Unix form** (`/bin`), not FatFs form
  (`0:/BIN`).
- **The stat family is inverted, as on Linux.**  `sys_newfstatat`
  (262) is the general implementation.  `sys_stat` is
  `newfstatat(AT_FDCWD, p, st, 0)`; `sys_lstat` is the same with
  `AT_SYMLINK_NOFOLLOW`; `sys_fstat` is a wrapper over
  `sys_fstat_body`, which `newfstatat`'s `AT_EMPTY_PATH` case calls.
- **`open(2)` is split.**  `open_resolved(path, flags)` is the one
  open body; `sys_open` and `sys_openat` are thin wrappers over it
  through `resolve_at`.
- **musl's routing, confirmed.**  On x86_64, `stat`/`lstat`/`fstat`
  are `fstatat` variants, and musl's `fstatat_kstat()` fast-paths
  the common cases to syscalls 4/6/5.  **Syscall 262 is reached
  only when those fast paths do not apply** — a real dirfd with a
  relative path, or non-standard flags.  `statx` (332) is
  **unreachable** on x86_64 musl 1.2.5.  Do not "implement statx"
  thinking it will help.
- **The `sys_execve` three-attempt path shim is unchanged.**
  `resolve_at` is a *different* seam with a different job (dirfd
  resolution, not bare-name search).  Do not confuse them.

### What `v0.6.7` contributed (prior milestone)

`musl_sh` became a real shell (quote stripping, `<`, `>`, `>>`,
`|`, `&&`, `;`, pipelines), and the console became a 1024×768
linear framebuffer with Terminus 10×18 text.  `vi` fills the
screen, edits, saves, alt-screen restores.  VGA text is the
fallback.  See `docs/session-log.md` and the `v0.6.7` commit
message.

### Known limitations

- **Redirection of a builtin is silently ignored.**  `cd /bin > log`
  runs `cd`, creates no file, prints no error.
- **A builtin in a pipeline is refused.**  `cd /bin | cat` prints
  `sh: builtin in pipeline not supported`.
- **`diff`, `chmod`, `ln`, `mount` are off** — the last three need
  their own syscalls.

---

## NEXT SESSION — pick one

`20261001-unlinkat` closed the `unlinkat` work.  Two open threads
and one housekeeping item are the natural candidates.

### Finishing `v0.6.8`'s theme (recommended)

The remaining `*at` syscalls share `resolve_at` and are cheap.  But
note the session-40 lesson: **before adding one "for" a consumer,
find the consumer** — and *read its source*, not the pattern.

1. **`faccessat` (269) is implemented but wrong for a real dirfd** —
   it ignores `dirfd`/`flags`, so `faccessat(dirfd, "rel", ...)`
   resolves against the cwd.  This is the one `*at` gap with an
   actual bug, not a missing syscall.  Fixing it to use
   `resolve_at` is small and honest.  **This is the recommended
   next step** if continuing the theme.
2. **`mkdirat` (258), `renameat` (264)** — no consumer in busybox;
   do not add until one exists and its source has been read.
3. **`linkat`/`symlinkat`/`readlinkat` (265/266/267)** — FAT has no
   links; skip entirely.

### Consumer status of the `*at` family (verified)

| Syscall | Num | Consumer in busybox | Verified by |
|---|---|---|---|
| `openat` | 257 | `find` — the directory walk | session 39; `find` works |
| `newfstatat` | 262 | `find` — statting entries; musl `fstatat` | session 39; `find` works |
| `unlinkat` | 263 | **none** — not `rm -r`, not `find -delete` | `libbb/remove_file.c`; `findutils/find.c:923-925` |
| `mkdirat` | 258 | none; `tar`/`cpio`/`unzip` would, but each needs a VFS first | flags off; not checked |
| `renameat` | 264 | none; `rm`-style code uses `rename` (82) | pattern only; not read |
| `linkat`/`symlinkat`/`readlinkat` | 265/266/267 | none possible — FAT has no links | structural |

`unlinkat` is correct and tested (`at_step2`), and it belongs to
the `*at` family the milestone is about, but **no busybox code
calls it**, and no config flag in the current config would make
one that does.  If a future busybox version, or a tool donix gains,
walks a directory and removes entries relative to a dirfd, it will
use `unlinkat` — but that is a prediction, not a consumer.  Do not
cite one.  Two predictions about `unlinkat`'s consumer were made in
session 40; both were wrong; both were caught by reading the
source.

**Do not add `mkdirat`, `renameat`, `linkat`, `symlinkat`, or
`readlinkat`** until a caller exists and its source has been read.

### Small, close gaps (non-theme)

1. **`sys_faccessat` dirfd/flags** — see above; also listed under
   the theme.
2. **`sys_utimensat` cwd resolution** — it calls `strip_dot_prefix`
   but not `resolve_against_cwd`; one-line fix.  (`sys_unlink` and
   `sys_rmdir` had the same gap; it was closed for them.)
3. **Ctrl-`[` as ESC** — `scancode_to_ascii` has no Ctrl
   parameter yet; touches the keyboard layer.  Low urgency.
4. **`sys_fcntl` fd < 3 for the other subcommands** — `F_GETFL`,
   `F_SETFL`, `F_GETFD`, `F_SETFD` still refuse `fd < 3`.  Not on
   any current path.

### Busybox config — applets/features to turn on next

The goal is every busybox feature donix can support, enabled as
soon as it is supportable.  See "Busybox enablement" below for the
current audit; the short list of what is ready now is `basename`,
`dirname`, `readlink`, `realpath`, `truncate`, `sleep`, `usleep`,
`unlink`, and `printenv`/`env`, each gated on a syscall donix
already has.  Add them one at a time, with the applet exercised
before the commit.

### Deliberately later — larger

- **Signal delivery (`SIGPIPE`).**  See `open-issues.md` item 6.
  A subsystem (real `sys_rt_sigaction`, per-process handlers, a
  `SIGPIPE` raise on the `-EPIPE` write path).  Nothing currently
  exercises it.  Also the prerequisite for job control and
  `kill(2)`.  Same subsystem a Wayland `wl_shm` client needs for
  `SIGBUS`.  Its own milestone-scale effort when ready.
- **VFS layer.**  `open-issues.md` item 1.  Eventually; delete the
  shims when it lands, do not extend them.  Note: the VFS is
  **not** a Wayland prerequisite; see `ROADMAP.md`.
- **Wayland (long horizon).**  See `ROADMAP.md`.  Not a `v0.6.x`
  target; the one concrete kernel gap it shares with existing
  work is non-anonymous `mmap`.
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
only when the syscalls it actually calls are implemented — read
the applet's source, do not guess from its name.**  This is the
session-40 lesson applied forward.

### Ready now (syscalls all present)

| Config | Applet | Needs | Notes |
|---|---|---|---|
| `CONFIG_BASENAME` | `basename` | none (string op) | off; pure userland |
| `CONFIG_DIRNAME` | `dirname` | none (string op) | off; pure userland |
| `CONFIG_READLINK` | `readlink` | `readlink` (89) | **syscall missing** — see below |
| `CONFIG_REALPATH` | `realpath` | `readlink` (89) | needs `readlink` first |
| `CONFIG_TRUNCATE` | `truncate` | `truncate` (76) or `ftruncate` (77) | `ftruncate` done; `truncate` (76) missing |
| `CONFIG_SLEEP` | `sleep` | `nanosleep` (35) | **syscall missing** |
| `CONFIG_USLEEP` | `usleep` | `nanosleep` (35) | **syscall missing** |
| `CONFIG_UNLINK` | `unlink` | `unlink` (87) | done — applet just off |
| `CONFIG_PRINTENV` | `printenv` | none | off; pure userland |
| `CONFIG_ENV` | `env` | `execve` (59) | done — applet just off |

The genuinely free ones (no new syscall) are **`basename`,
`dirname`, `printenv`, `env`, `unlink`**.  They are pure userland
or use syscalls already implemented.  `CONFIG_ENV` needs
`execve`, which works (`busybox ash` proves it).  These five are
the safe batch.

### Needs one small syscall first

| Config | Applet | Missing syscall | Size |
|---|---|---|---|
| `CONFIG_READLINK` | `readlink` | `readlink` (89) | small — FAT has no symlinks, so an honest `-EINVAL`/`-ENOENT` may be all it needs |
| `CONFIG_REALPATH` | `realpath` | `readlink` (89) | follows from `readlink` |
| `CONFIG_SLEEP` | `sleep` | `nanosleep` (35) | small — a `g_ticks` deadline loop |
| `CONFIG_USLEEP` | `usleep` | `nanosleep` (35) | same |
| `CONFIG_TRUNCATE` | `truncate` | `truncate` (76) | small — mirrors `ftruncate` by path |
| `CONFIG_MKTEMP` | `mktemp` | `getpid` (39) + `open` (2) | both present; probably ready — check source |
| `CONFIG_TTY` | `tty` | `ioctl` (16) | present; check source |
| `CONFIG_TTYSIZE` | `ttysize` | `ioctl` (16) | present; check source |
| `CONFIG_UNAME`-adjacent | `arch`, `bb_arch` | `uname` (63) | present; check source |

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
| `CONFIG_FEATURE_FIND_DELETE` | `find -delete` | works today via `unlink`/`rmdir`; **would not use `unlinkat`** (see `findutils/find.c:923-925`), but needs `FEATURE_FIND_DEPTH` (post-order walk), also off |
| `CONFIG_ASH_JOB_CONTROL` | ash job control | signal delivery |
| `CONFIG_FEATURE_EDITING_HISTORY`-adjacent (`FEATURE_TAB_COMPLETION`) | ash completion | needs `stat` on many paths; probably works, test it |

### Suggested order (one per commit)

1. **`basename`, `dirname`, `printenv`, `env`, `unlink`** — free, no
   new syscall.  One config commit, one exercise-each commit, or
   one combined config commit with the exercises in the message.
2. **`sleep`, `usleep`** — add `nanosleep` (35) as a `g_ticks`
   deadline loop, then enable both.
3. **`truncate`** — add `truncate` (76) by path (mirror
   `ftruncate`), then enable.
4. **`readlink`, `realpath`** — add `readlink` (89) as an honest
   `-EINVAL` on FAT (no symlinks), then enable.
5. **`mktemp`, `tty`, `ttysize`, `arch`** — check each source for
   the syscalls it actually calls; enable the ones that only use
   what exists.

Each step: read the applet's source, enable, exercise from ash,
commit.  Do not batch-enable without exercising.

---

## Canary state

**The focused canary is green as of `20261001-unlinkat`.**  Full
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
    cat busybox                 # reads /bin/busybox
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

**`*at` regression tests (`userland/musl/tests/`, not canary rows):**

    at_step1    # resolve_at via dirfd; fstatat flags; AT_EMPTY_PATH  (read-only)
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
(trace off); no `[a|b|c]` debug line (removed).  The `FB: mapped N
pages ...` line is expected.  The `sys_open: f_open FAIL path=...`
lines from `vi` on a new file, and from `busybox stat` on
nonexistent paths, are expected diagnostics.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **VFS layer (eventual).**  `sys_execve`'s three-attempt path
   resolution is a shim; `resolve_at` and `resolve_against_cwd`
   are two more seams that a VFS would subsume.  When a VFS lands,
   delete them; do not extend.
2. **Redirection of a builtin is silently ignored.**
3. **A builtin in a pipeline is refused.**
4. **`sys_fcntl` refuses fd < 3** for subcommands other than
   `F_DUPFD`/`F_DUPFD_CLOEXEC`.  Deliberate; not on any path.
5. **`-EPIPE` is delivered without `SIGPIPE`.**  Narrower than the
   old docs claimed: `busybox yes | busybox head -n 1` does *not*
   hang.  Fixing it means signal delivery.

Also open: `unlinkat` has no consumer; `faccessat` ignores
`dirfd`/`flags`; Ctrl- `[` not mapped to ESC; `sys_utimensat` lacks
`resolve_against_cwd`; `sys_munmap` is a stub returning 0;
`sys_brk`'s fixed `heap_base` and the 4 MB mmap window are latent
collisions; real FatFs timestamp storage; `prctl` is minimal;
busybox applet symlinks not installed; syscall-table audit script;
`musl_wait`'s WNOHANG loop spins; `sys_mmap` rejects all
non-anonymous mappings; pipes support one concurrent reader and one
concurrent writer; `put_file_slot`'s pipe wake is coupled to
`sys_close`'s wake.

---

## State on disk

**Real project root:** `/home/noneya/code/donix/`.

- `configs/busybox.config` — tracked canonical busybox config.
  Enabled applets: `cat`, `cp`, `cut`, `echo`, `false`, `find`,
  `head`, `ls`, `mkdir`, `mv`, `od`, `pwd`, `rm`, `rmdir`, `seq`,
  `sort`, `stat`, `tail`, `tee`, `test`, `touch`, `tr`, `true`,
  `uname`, `uniq`, `wc`, `yes`, `cmp`, `grep`, `sed`, `vi`,
  `clear`, plus `ash`.  `CONFIG_FEATURE_VI_WIN_RESIZE=y`.
  `CONFIG_FIND=y` and `CONFIG_FEATURE_FIND_TYPE=y` (added
  `20260930-at`).  The other `FEATURE_FIND_*` predicates are off
  deliberately (no timestamps, no users, no links).  Off (with
  reasons): `diff` (deliberate), `chmod`/`ln`/`mount` (need kernel
  work).  See the "Busybox enablement" section above for what can
  be turned on next.
- `userland/musl/` — tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.  New tests must be added to both
  `USERLAND_ELFS` and the `mcopy_one` chain in
  `05_boot_kernel64/Makefile`.  `tests/at_step1.c` (session 39) and
  `tests/at_step2.c` (session 40) are the `*at` regression suites.
- `04_kernel_64bit/fonts/ter-u18n.psf` — tracked font source
  (generated from Terminus 4.49 via `make psf`).  The `.psf` is
  tracked; the generated `ter_u18n_data.c` is gitignored (like
  `test_program_data.c`).
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
  (session 39), and now "A consumer inferred from behavior is not
  a consumer" (session 40).  Read them together; expect more.
- `docs/session-log.md` — commit tables and per-test canary notes.
  Rows named by scratch tag.
- `docs/open-issues.md` — full open-issues list.
- `docs/migration-history.md`, `docs/dons-os-history.md` —
  historical narrative (A1-A6, pre-fork).
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` — the first two
  frozen at `v0.6.0`; `LLD_BUG_REPORT.md` current.
- `ROADMAP.md` — future work only; direction of travel beyond the
  current milestone (currently: the Wayland long-horizon section).

---

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  `v0.6.7` is published: `donix>`'s own shell is
real, and the console is a 1024×768 linear framebuffer.  The
`v0.6.8` milestone is open: `20260930-at` implemented `resolve_at`,
`newfstatat` (262), and `openat` (257); `20261001-unlinkat`
implemented `unlinkat` (263) and gave `unlink`/`rmdir` the
file-vs-directory type check.  `unlinkat` turned out to have no
consumer in busybox — both `rm -r` (`libbb/remove_file.c`) and
`find -delete` (`findutils/find.c`) use `unlink`/`rmdir` with
constructed paths — and the type check, not `unlinkat`, is what
made `rm -r` correct.  Next: the free busybox applets
(`basename`, `dirname`, `printenv`, `env`, `unlink`) and then the
`*at` bug fix (`faccessat`'s dirfd).  One change at a time.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
