Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-01 (session 41, `faccessat` + `utimensat`)
**Current HEAD:** branch `dev`, twelve commits past `origin/dev`;
scratch tags `20261001-atrefactor`, `20261001-unlinkat`,
`20261001-atdocs`, `20261001-faccessat`, `20261001-utimensat`,
`20261001-docs` (all local)
**Last milestone:** `v0.6.7` (published) — `musl_sh` is a real shell;
the console is a 1024×768 linear framebuffer
**Milestone status:** **`v0.6.8` is closeable.**  The `*at()`
family is done: `openat`, `newfstatat`, `unlinkat`, `faccessat`,
`utimensat` all implemented, tested, routed through `resolve_at`.
The remaining `*at` syscalls have no verified consumer.  The next
milestone is `v0.6.9`, envp.  The milestone bump itself (the `v*`
tag and push) is a deliberate step, not a docs edit.

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

## Where we are — `v0.6.8` closeable; `v0.6.9` (envp) next

`v0.6.7` shipped the shell and the framebuffer.  `v0.6.8` is the
**`*at()` family**, and it is now functionally complete.

### Session 41 — `faccessat` and `utimensat` close the family

| Tag (kept, milestone open) | What |
|---|---|
| `20261001-faccessat` | `access_resolved` extracted; `sys_faccessat` routes through `resolve_at`; flags **not** validated |
| `20261001-utimensat` | `sys_utimensat` routes through `resolve_at`; flags validated |

**What it means:** `sys_faccessat` no longer ignores `dirfd`;
`sys_utimensat` no longer ignores `dirfd` *and* now resolves
against the cwd.  Both share `access_resolved` with `sys_access`,
which keeps its cwd-only behavior (`access(2)` has no dirfd).

**The finding that matters:** `sys_faccessat` must **not** validate
its `flags` argument.  musl calls it with three arguments
(`third_party/musl-src/src/unistd/faccessat.c`), so `%r10` — the
register the kernel reads as argument 4 — holds the **previous
syscall's return value**.  The trace showed `flags=0xFFFFFFEA`,
which is `-EINVAL`, from the preceding test.  Validating it turned
valid calls into `-EINVAL`, deterministically.  `sys_utimensat` is
the opposite case and *does* validate: musl passes it four
arguments.  See `docs/gotchas.md`, "A syscall argument the caller
did not set holds the previous syscall's return value."

**Key facts for future work:**
- **`access_resolved(abs_path)` is the one existence-check body.**
  `sys_access` (cwd-relative) and `sys_faccessat` (`resolve_at`) and
  `sys_utimensat` (which stores nothing, so this is its whole body)
  all call it.  Do **not** grow a second copy.
- **`resolve_at` is still the one resolver.**  `faccessat` and
  `utimensat` now join `openat`/`newfstatat`/`unlinkat` in using it.
- **Do not validate a syscall argument without checking whether the
  caller sets it.**  Read the libc wrapper.  `faccessat` and
  `utimensat` differ on exactly this, and the difference is which
  one musl passes four arguments to.
- **The `make -C 04_kernel_64bit` incremental build is not
  trustworthy.**  It reported "Nothing to be done" while
  `user_syscall.c` was edited, because `kernel.bin`'s mtime was
  newer.  Use `./run`, which cleans first.  See `docs/gotchas.md`,
  "The incremental kernel build can silently skip."

### Session 40 — `unlinkat`; `unlink`/`rmdir` type check

| Tag (kept, milestone open) | What |
|---|---|
| `20261001-atrefactor` | `unlink_body` extracted; `sys_unlink`/`sys_rmdir` become `resolve_at` wrappers (pure refactor) |
| `20261001-unlinkat` | `unlinkat` (263); `want_dir` + `f_stat_with_retry` type check; `cli` critical section; `at_step2.c` |

**What it means:** `unlink`/`rmdir` now enforce Linux's
file-vs-directory distinction — `unlink(dir)` is `-EISDIR`,
`rmdir(file)` is `-ENOTDIR`, `rmdir("/")` is `-EBUSY`.  `unlinkat`
(263) is implemented and tested.

**The finding that matters:** `unlinkat` has **no consumer** in
busybox — not `rm -r`, not `find -delete`.  What made `rm -r` work
was the type check in the *same* commit.  See `docs/gotchas.md`,
"A consumer inferred from behavior is not a consumer."

**Key facts for future work:**
- **`unlink_body(abs_path, want_dir, tag)` is the one unlink body.**
  `sys_unlink` (`want_dir=0`), `sys_rmdir` (`want_dir=1`), and
  `sys_unlinkat` (`want_dir = flags & AT_REMOVEDIR`) all funnel
  through it via `resolve_at`.  Do **not** grow a second copy.
- **The type check and the `f_unlink` are one `cli` critical
  section**, released by restoring the caller's saved RFLAGS (not
  `sti`'d, because `unlink_body` can run inside `sys_execve`'s
  `cli`).  `cli` is safe across FatFs because `diskio.c` is
  synchronous and polling; if a disk op ever waits on an IRQ, this
  becomes a hang — the fix would be a lock, not `cli`.
- **`unlinkat` (263) is correct but unconsumed.**  `at_step2` is
  its only exerciser.

### Session 39 — the `*at()` family; `find`

| Tag (kept, milestone open) | What |
|---|---|
| `20260930-at` | `resolve_at`, `file_slot_t.dir_path`, `newfstatat` (262), `openat` (257); stat family inverted to wrappers; `find` enabled with `-type` |
| `20260930-cursor` | software block cursor on the framebuffer; PIT-rate comment correction |

**Key facts for future work:**
- **`resolve_at(dirfd, path, out, cap)` is the one resolver.**  An
  absolute path ignores `dirfd`; `AT_FDCWD` delegates to
  `resolve_against_cwd`; a real dirfd concatenates the directory
  slot's stored path with the relative path.  Every `*at` syscall
  calls it.  Do **not** grow a second copy.
- **`file_slot_t.dir_path`** is a `kmalloc`'d absolute Unix-form
  path, set only for `FILE_KIND_DIR` slots, freed in
  `put_file_slot`.  Stored in **Unix form** (`/bin`), not FatFs
  form (`0:/BIN`).
- **The stat family is inverted, as on Linux.**  `sys_newfstatat`
  (262) is the general implementation; `sys_stat`, `sys_lstat`,
  `sys_fstat` are wrappers.
- **musl's routing, confirmed.**  On x86_64, `stat`/`lstat`/`fstat`
  are `fstatat` variants, and musl fast-paths the common cases to
  syscalls 4/6/5.  Syscall 262 is reached only for a real dirfd with
  a relative path, or non-standard flags.  `statx` (332) is
  unreachable on x86_64 musl 1.2.5.

### What `v0.6.7` contributed (prior milestone)

`musl_sh` became a real shell (quote stripping, `<`, `>`, `>>`,
`|`, `&&`, `;`, pipelines), and the console became a 1024×768
linear framebuffer with Terminus 10×18 text.  `vi` fills the
screen, edits, saves, alt-screen restores.  VGA text is the
fallback.

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
  regression from any `v0.6.8` work.  See `session-log.md`,
  session 41.

---

## NEXT SESSION — pick one

`v0.6.8` is closeable.  The natural next step is to close it and
open `v0.6.9` with envp, but that is a milestone decision, not a
session's first move.  The candidate next sessions:

### `v0.6.9` — envp (recommended)

`sys_execve` does `(void)user_envp;` — it ignores the environment
entirely.  `env` and `printenv` run but show an empty environment;
`ash`'s `$VAR` expansion is always empty.  Env passing is a small
kernel change (copy envp strings onto the new stack exactly as argv
is copied) with a real consumer — every applet that reads the
environment.  **Read `sys_execve`'s argv-layout block first**; it is
the most delicate part of the file (`safe_copy_to_user_cr3` writes,
the `argv_region_bottom` arithmetic, the `%rsi` frame slot), and
envp adds a second region to it.  `MAINTENANCE.md` item 3l
(historical, frozen) warns that the argv region shares the top of
the user stack and the gap shrinks if envp is added — worth reading
before starting.

### The free busybox applets (if not folded into `v0.6.9`)

`basename`, `dirname`, `printenv`, `env`, `unlink` — pure userland
or syscalls already implemented.  But `printenv`/`env` are only
*useful* once envp is plumbed, so these pair naturally with
`v0.6.9` rather than preceding it.

### Close `v0.6.8` properly

Bump the milestone: drop the scratch tags, write the milestone
narrative from the tag annotations, tag `v0.6.8`, push.  This is a
deliberate step (per `docs/strategy.md`) and can be a session of
its own.

### Small, close gaps (non-theme)

1. **Ctrl-`[` as ESC** — `scancode_to_ascii` has no Ctrl parameter
   yet; touches the keyboard layer.  Low urgency.
2. **`sys_fcntl` fd < 3 for the other subcommands** — `F_GETFL`,
   `F_SETFL`, `F_GETFD`, `F_SETFD` still refuse `fd < 3`.  Not on
   any current path.
3. **`sys_munmap`** — a stub returning 0.

### Busybox config — applets/features to turn on next

The goal is every busybox feature donix can support, enabled as
soon as it is supportable.  The short list of what is ready now is
`basename`, `dirname`, `readlink`, `realpath`, `truncate`, `sleep`,
`usleep`, `unlink`, and `printenv`/`env`, each gated on a syscall
donix already has or a small one.  Add them one at a time, with the
applet exercised before the commit.

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

## Canary state

**The focused canary is green as of `20261001-utimensat`.**  Full
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
spins (pre-existing; duration increased at the console swap); `sys_mmap`
rejects all non-anonymous mappings; pipes support one concurrent
reader and one concurrent writer; `put_file_slot`'s pipe wake is
coupled to `sys_close`'s wake.

---

## State on disk

**Real project root:** `/home/noneya/code/donix/`.

- `configs/busybox.config` — tracked canonical busybox config.
  Enabled applets: `cat`, `cp`, `cut`, `echo`, `false`, `find`,
  `head`, `ls`, `mkdir`, `mv`, `od`, `pwd`, `rm`, `rmdir`, `seq`,
  `sort`, `stat`, `tail`, `tee`, `test`, `touch`, `tr`, `true`,
  `uname`, `uniq`, `wc`, `yes`, `cmp`, `grep`, `sed`, `vi`,
  `clear`, plus `ash`.  `CONFIG_FEATURE_VI_WIN_RESIZE=y`.
  `CONFIG_FIND=y` and `CONFIG_FEATURE_FIND_TYPE=y`.  The other
  `FEATURE_FIND_*` predicates are off deliberately.  Off (with
  reasons): `diff` (deliberate), `chmod`/`ln`/`mount` (need kernel
  work).
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
  consumer" (session 40), and now "A syscall argument the caller
  did not set holds the previous syscall's return value" and "The
  incremental kernel build can silently skip" (session 41).  Read
  them together; expect more.
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
`userland/musl/`.  `v0.6.7` is published: `donix>`'s own shell is
real, and the console is a 1024×768 linear framebuffer.  `v0.6.8`
is the `*at()` family and is now closeable: `resolve_at`,
`newfstatat` (262), `openat` (257), `unlinkat` (263), `faccessat`
(269), and `utimensat` (280) are all implemented, tested, and
routed through the one resolver; the remaining `*at` syscalls have
no verified consumer.  The next milestone is `v0.6.9`, envp —
`sys_execve` ignores the environment today, and plumbing it is what
makes `env`, `printenv`, and `$VAR` actually work.  One change at a
time.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
