Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-09-30 (session 39, `*at` family opened)
**Current HEAD:** branch `dev`, two commits past `origin/dev`;
scratch tag `20260930-at` (local)
**Last milestone:** `v0.6.7` (published) — `musl_sh` is a real shell;
the console is a 1024×768 linear framebuffer
**Milestone status:** **open — `v0.6.8` in progress.**  `20260930-at`
is the first commit of the `v0.6.8` milestone.  `dev` is two commits
ahead of `origin/dev`, not pushed.  Scratch tags are local and are
dropped before the milestone is pushed.

Commits are named by tag only, never by SHA.  **Working tags
(`2026093x-*`) are local scratch restore points** — they exist while
a milestone is being developed and are **dropped before the
milestone is pushed**.  Only `v*` tags go to the remote and are
permanent.  The commit record is `docs/session-log.md`; the commit
message carries the narrative.  Once a scratch tag is dropped it
resolves to nothing; do not cite one as if it were a stable
reference.

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
`docs/strategy.md`.

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
needs; more of the family is expected before the bump.

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
  slot's stored path with the relative path.  Every future `*at`
  syscall (`unlinkat`, `mkdirat`, `renameat`, `linkat`,
  `readlinkat`, `faccessat`, ...) calls it.  Do **not** grow a
  second copy of this logic.
- **`file_slot_t.dir_path`** is a `kmalloc`'d absolute Unix-form
  path, set only for `FILE_KIND_DIR` slots, freed in
  `put_file_slot`.  It rides `fork`/`dup2` via refcount slot
  sharing.  It is stored in **Unix form** (`/bin`), not FatFs form
  (`0:/BIN`).
- **The stat family is inverted, as on Linux.**  `sys_newfstatat`
  (262) is the general implementation.  `sys_stat` is
  `newfstatat(AT_FDCWD, p, st, 0)`; `sys_lstat` is the same with
  `AT_SYMLINK_NOFOLLOW`; `sys_fstat` is a wrapper over
  `sys_fstat_body`, which `newfstatat`'s `AT_EMPTY_PATH` case calls.
  `sys_stat`/`sys_lstat`/`sys_fstat` no longer contain resolution
  logic.
- **`open(2)` is split.**  `open_resolved(path, flags)` is the one
  open body; `sys_open` and `sys_openat` are thin wrappers over it
  through `resolve_at`.
- **musl's routing, confirmed.**  On x86_64, `stat`/`lstat`/`fstat`
  are `fstatat` variants, and musl's `fstatat_kstat()` fast-paths
  the common cases to syscalls 4/6/5.  **Syscall 262 is reached
  only when those fast paths do not apply** — a real dirfd with a
  relative path, or non-standard flags.  `statx` (332) is
  **unreachable** on x86_64 musl 1.2.5: `SYS_fstatat` is defined
  (as `SYS_newfstatat`), so the `#else` statx branch never compiles.
  Do not "implement statx" thinking it will help; the grep for
  `statx` in the kernel is empty for a reason.
- **The `sys_execve` three-attempt path shim is unchanged.**
  `resolve_at` is a *different* seam with a different job (dirfd
  resolution, not bare-name search).  Do not confuse them; the
  execve shim still needs the VFS to go away.

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

`20260930-at` opened `v0.6.8`.  The milestone's theme is the
`*at()` family; the resolver is built and the two syscalls busybox
needed are done.  Continuing the theme is the natural next step.

### On-theme (recommended, continues `v0.6.8`)

The remaining `*at` syscalls all share `resolve_at` and are now
cheap — each is a wrapper plus a dispatch case:

1. **`unlinkat` (263)** — `unlink`/`rmdir` in one syscall,
   `AT_REMOVEDIR` selects.  Used by busybox `rm -r`.
2. **`mkdirat` (258)** — `mkdir` with a dirfd.  Rarely needed on
   its own; useful once a shell or tool uses a dirfd.
3. **`renameat` (264)** — two-path, needs `resolve_at` twice.
4. **`linkat` (265) / `readlinkat` (267) / `symlinkat` (266)** —
   FAT has no links or symlinks; these would be honest `-EPERM` /
   `-ENOSYS` until the VFS exists.  Probably skip for now.
5. **`faccessat` / `fchmodat` / `fchownat`** — donix ignores
   permissions and ownership; `faccessat` already aliases
   `sys_access`.  Skip.

**Start with `unlinkat` (263).**  It is the one with a real
consumer (`rm -r`), it shares `resolve_at` directly, and it is the
smallest honest extension of the work just done.

### Small, close gaps (non-theme)

1. **Ctrl-`[` as ESC** — `scancode_to_ascii` has no Ctrl
   parameter yet; touches the keyboard layer.  Low urgency.
2. **`sys_fcntl` fd < 3 for the other subcommands** — `F_GETFL`,
   `F_SETFL`, `F_GETFD`, `F_SETFD` still refuse `fd < 3`.  Not on
   any current path.
3. **`sys_utimensat` cwd resolution** — it calls
   `strip_dot_prefix` but not `resolve_against_cwd`; one-line fix.
4. **More applets** — `awk`, `tar`.

### Deliberately later — larger

- **Signal delivery (`SIGPIPE`).**  See `open-issues.md` item 6.
  A subsystem (real `sys_rt_sigaction`, per-process handlers, a
  `SIGPIPE` raise on the `-EPIPE` write path).  Nothing currently
  exercises it — `busybox yes | busybox head -n 1` does *not* hang.
  Also the prerequisite for job control and `kill(2)`.  Its own
  milestone-scale effort when ready.
- **VFS layer.**  `open-issues.md` item 1.  Eventually; delete the
  shims when it lands, do not extend them.
- **PS/2 mouse driver + framebuffer cursor.**  Not on the roadmap
  yet, but the natural first step toward any interactive GUI.  The
  framebuffer console is the foundation; a mouse is what would make
  it interactive.
- **Font size / resolution.**  The console is 10×18 at 1024×768.
  A bigger glyph (`ter-u24n.psf`, 12×24) or a bigger mode is a
  data change, but **see the `v0.6.7` "changing the VBE mode" note
  in `docs/session-log.md`** — it also needs the grid constants and
  possibly `fb.c`.

---

## Canary state

**The focused canary is green as of `20260930-at`.**  Full table in
`docs/session-log.md`.

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

**`*at` regression test (`userland/musl/tests/`, not a canary row):**

    at_step1    # resolve_at via dirfd; fstatat flags; AT_EMPTY_PATH

**Pipe regression suite (`userland/musl/tests/`):**

    pipe_step1    # object + non-blocking I/O
    pipe_step2    # blocking + directed wake
    pipe_step3    # EOF, EPIPE, dup-aware counts
    pipe_step3b   # exit-path wake

Run these when changing `sys_read`/`sys_write`/`sys_close`/
`put_file_slot`/`sys_fork`/`sys_pipe` or adding a `FILE_KIND_*`.
Not canary rows (they fork), but they are the only pipe regression
suite.  `at_step1` is the analogous suite for `resolve_at` and the
stat family.

**Framebuffer / `vi` verification (added v0.6.7, not canary rows):**

    vi test            # fills the screen; status line on the last row
    # edit, :wq
    ./test             # the saved script runs

**Cursor (added `20260930-cursor`, not a canary row):**

    # At any shell prompt on the framebuffer console, the block
    # cursor at the current input position blinks at 500 ms per
    # state.  Type a character: the cursor moves and does not leave
    # an inverted cell behind (the "trail" bug fixed before the
    # commit).  `vi test` puts the cursor at the edit position; it
    # is hidden while the screen is redrawn and restored on exit.

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

Also open: the rest of the `*at` family (`unlinkat`, `mkdirat`,
`renameat`, `linkat`, `readlinkat`, `symlinkat`) — the resolver
exists, the syscalls do not; Ctrl- `[` not mapped to ESC;
`sys_utimensat` lacks `resolve_against_cwd`; `sys_munmap` is a stub
returning 0; `sys_brk`'s fixed `heap_base` and the 4 MB mmap window
are latent collisions; real FatFs timestamp storage; `prctl` is
minimal; busybox applet symlinks not installed; syscall-table audit
script; `musl_wait`'s WNOHANG loop spins; `sys_mmap` rejects all
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
  work).
- `userland/musl/` — tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.  New tests must be added to both
  `USERLAND_ELFS` and the `mcopy_one` chain in
  `05_boot_kernel64/Makefile`.  `tests/at_step1.c` added
  `20260930-at`.
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
  entries (session 38), and now two from session 39 — the
  kernel-name/libc-name confusion, and "when the count disagrees
  with the lines, suspect the test."  Read them together; expect
  more.
- `docs/session-log.md` — commit tables and per-test canary notes.
  Rows named by scratch tag.
- `docs/open-issues.md` — full open-issues list.
- `docs/migration-history.md`, `docs/dons-os-history.md` —
  historical narrative (A1-A6, pre-fork).
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` — the first two
  frozen at `v0.6.0`; `LLD_BUG_REPORT.md` current.

---

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  `v0.6.7` is published: `donix>`'s own shell is
real, and the console is a 1024×768 linear framebuffer.  The
`v0.6.8` milestone is open: `20260930-at` implemented `resolve_at`,
`newfstatat` (262), and `openat` (257), inverting the stat family
to wrappers as on Linux, and enabled busybox `find` (with `-type`).
Next: continue the `*at()` family — `unlinkat` (263) is the
smallest honest extension.  One change at a time.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
