Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-09-30 (post-`v0.6.6`, `musl_sh` work + `cat`)
**Current HEAD:** tag `20260930-cat`, branch `dev`
**Last milestone:** `v0.6.6` (published) — `pipe(2)` done, pipelines
work
**Milestone status:** **no bump this session.**  Eight commits on
`dev`, scratch-tagged, unpushed (six `musl_sh` + one docs + one
`cat`).  A `v0.6.7` bump is deferred until the accumulated changes
feel substantial enough; until then the scratch tags carry the
narrative.

Commits are named by tag only, never by SHA.  **Working tags
(`2026093x-*`) are local scratch restore points** — they exist while
a milestone is being developed and are **dropped before the
milestone is pushed**.  Only `v*` tags go to the remote and are
permanent.  The commit record is `docs/session-log.md`; the commit
message carries the narrative.  Once a scratch tag is dropped it
resolves to nothing; do not cite one as if it were a stable
reference.

**Scratch tags are now annotated** (session 37 onward).  The
annotation is the fuller per-commit summary; at a milestone bump the
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
have been used in past sessions to experiment before porting a
change back.  They are **transient**: never the source of truth,
never where a `v*` tag lives, and never referenced by this file.
If one exists, it is safe to reset or delete.  Do not port from a
scratch workspace into the real tree without a build and test in
the real tree.

---

## Where we are — `musl_sh` work committed, milestone deferred

Session 37 closed the gap that `v0.6.6`'s handoff named as the
`v0.6.7` headline: **`donix>` (musl_sh) now strips quotes and parses
`<`, `>`, `>>`, `|`, `&&`, `;`, and pipelines.**  It was userland
only — a real tokenizer and command runner in
`userland/musl/apps/musl_sh.c` — with no kernel change, because
everything it needed (`pipe`, `dup`, `dup2`, `fork`, `execve`,
`wait4`, redirect via `dup2`, a working cwd) was already in place
from `v0.6.6`.

Six commits on `dev`, scratch-tagged, unpushed:

| Tag | What |
|---|---|
| `20260930-tokenizer` | two-phase tokenizer: quote stripping + operator splitting |
| `20260930-redir` | `<`, `>`, `>>` via `open` + `dup2` + `execve` |
| `20260930-seq` | `;` and `&&`; builtins report their own status |
| (untagged) | `configs/busybox.config`: enable `false`, `true`, `yes`, `seq`, `clear` |
| `20260930-pipe` | `\|`; `fork_child` refactor; N-stage pipelines |
| `20260930-nodebug` | remove the tokenizer debug print |

Followed by:
- `20260930-docs` — the session-37 documentation pass.
- `20260930-cat` — donix-native `cat` gained stdin mode, multiple
  files, and `-` (so `cat < file` works without busybox).  HEAD.

Full per-commit narrative: `docs/session-log.md`, Session 37.
`git log --oneline v0.6.6..HEAD` shows all eight.

**What the shell does now:** `donix>` tokenizes a line into argv
(quote-aware, operators split out), parses redirection and pipelines,
runs sequences, and execs.  The `[a|b|c]` debug print that showed
the tokenization during development has been removed.

### What this closes

The `musl_sh` limitation that every prior handoff carried — *"does
not strip shell quotes and does not parse redirection or pipes; all
tests involving `<`, `>`, `|`, `&&`, `;`, or quoting must be run
from ash"* — is **gone.**  Redirection and pipeline tests now run
from `donix>` directly.  `busybox sh` and the boot ash still exist
and still work; they are just no longer *required* for those tests.

### Known limitations introduced by this work

- **Redirection of a builtin is silently ignored.**  `cd /bin > log`
  runs `cd`, creates no `log`, prints no error.  Verified this
  session (`pwd > log` prints to the screen, not the file).
- **A builtin in a pipeline is refused.**  `cd /bin | cat` prints
  `sh: builtin in pipeline not supported` and runs nothing.  A
  builtin cannot be forked without changing its meaning and donix's
  builtins have no subshell form.

Both are in `docs/open-issues.md`, items 2 and 3.

### What `v0.6.6` contributed (prior milestone)

`pipe(2)` end to end: `FILE_KIND_PIPE`, a shared `pipe_t` (4 KB ring),
blocking read/write with a directed wake, EOF when the last writer
closes, `-EPIPE` when the last reader closes, an exit-path wake, and
the stdio-guard inversion that made pipelines actually run.
`dup(2)` (syscall 32).  Keyboard fix (Shift+backslash).  See
`docs/session-log.md`, Session 36.

### Known failures, deliberately off

- **`diff`** — deliberately off; larger surface area.
- **`chmod`, `ln`, `mount`/`umount`** — need `chmod(2)` (90),
  `link(2)`/`symlink(2)` (86/88), and `mount(2)` respectively.

---

## NEXT SESSION — `sys_open` `O_DIRECTORY`, then the framebuffer

**Recommended first item: `sys_open` `O_DIRECTORY` fix.**  Return
`-ENOTDIR` when the target is a file.  Small kernel change,
well-scoped, a correctness fix rather than a feature.  This was the
second item last session; `cat` stdin mode (the first) is done.

**Second item (higher value, listed second by choice): a
framebuffer console, then Terminus.**

The VGA text-mode console is hard to read in a half-screen QEMU
window on a widescreen monitor.  That makes development itself
painful, which is why this item is worth more than its position
suggests — it pays back every future session.  It is listed after
`O_DIRECTORY` only because that is smaller and was already queued;
swapping this to first is justified.

**This is a build, not a revive.**  A framebuffer was built in an
earlier session but it lived in a scratch tree that no longer
exists; it was never committed and the code is gone (confirmed,
session 37).  `git log --all -- 04_kernel_64bit/` shows no
framebuffer / VBE / mode-setting work — the closest is the VGA
text-mode VT100 parser, `22c8329 vga: ANSI/VT100 subset parser`.
So the work is:

  - boot-time mode setting (VBE/VESA linear framebuffer, or a
    bootloader handoff), a linear-framebuffer write path, and a
    glyph blitter;
  - then **Terminus** at a native size — 10×18, 12×24, or 16×32 —
    blitted **unscaled**.

Terminus is what Alpine and the Linux kernel use for the console.
It ships in native sizes from 6×12 to 16×32; the 16×32 size exists
precisely because VGA 8×16 is unreadable on modern displays.  It is
OFL-1.1, so bundling it is fine.  The likely reason the earlier
font "looked terrible" is that a small bitmap was scaled up; a
native-size blit avoids that.  For a future scalable/anti-aliased
GUI font, **Hack** (MIT) is the code-oriented choice.

Do the size experiment in a scratch copy first (the standing rule:
scratch is transient, do not port without re-doing and testing in
the real tree), then implement cleanly here.

### Smaller, when ready

1. **`newfstatat` (262)** — reserved number, no dispatch case.
   Delegates to `sys_stat` for `AT_FDCWD` or an absolute path.
   Unblocks `find`.
2. **Ctrl-`[` as ESC** — `scancode_to_ascii` has no Ctrl parameter
   yet; this touches the keyboard layer.  Low urgency.
3. **`sys_fcntl` fd < 3 for the other subcommands** — `F_GETFL`,
   `F_SETFL`, `F_GETFD`, `F_SETFD` still refuse `fd < 3`; Linux
   allows them on a redirected fd.  Not on any current path.
4. **More applets** — `awk`, `find` (needs `newfstatat`), `tar`.
   `chmod` (90), `ln` (86/88), and `mount` need their own syscalls.

### Deliberately later — larger

- **Signal delivery (`SIGPIPE`).**  See `open-issues.md` item 6.
  This is a subsystem (real `sys_rt_sigaction`, per-process handlers,
  a `SIGPIPE` raise on the `-EPIPE` write path), not a small change,
  and nothing currently exercises it — `busybox yes | busybox head
  -n 1` does *not* hang.  Its own milestone-scale effort when ready.
- **VFS layer.**  `open-issues.md` item 1.  Eventually; delete the
  shims when it lands, do not extend them.

**Pick one, do it, test it, tag it.**  One change at a time.

---

## Canary state

**The focused canary is green as of `v0.6.6` and unchanged this
session.**  Full table in `docs/session-log.md`.

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

**Pipeline rows (added v0.6.6) — now runnable from `donix>` as well
as ash:**

    cat hello-world.txt | head -n 2
    echo hi | wc
    echo hello | cat

These are the milestone's headline verification.  `cat | head`
exercises blocking on both ends; `echo hi | wc` exercises EOF; `echo
hello | cat` is the smallest end-to-end case.

**Redirection rows (verified session 37, runnable from `donix>`):**

    echo hi > out.txt ; cat out.txt
    echo hi2 >> out.txt ; cat out.txt
    cat < out.txt
    busybox cat < out.txt

(`cat < out.txt` with donix-native `cat` works as of session 37 —
`cat.elf` gained stdin mode, multiple files, and `-`.)

**Pipe regression suite (`userland/musl/tests/`):**

    pipe_step1    # object + non-blocking I/O
    pipe_step2    # blocking + directed wake
    pipe_step3    # EOF, EPIPE, dup-aware counts
    pipe_step3b   # exit-path wake

These four are the only regression suite the pipe code has.  Each
prints a `STEPn OK` (or `STEP3B OK`) line on success.  If you change
anything in `sys_read`/`sys_write`/`sys_close`/`put_file_slot`/
`sys_fork`/`sys_pipe`, or add a new `FILE_KIND_*`, run them.  They
are **not** canary rows: they fork and they take seconds, but they do
not mutate the disk.

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`; use
`busybox sh` or `/bin/busybox sh`.

**Canary rows must not mutate the disk.**  The read-only applets
(`head`, `tail`, `grep`, `sed`, `cut`, `sort`, `stat`, `cmp`, `wc`,
`od`, `uniq`) can be canary rows.  `tee`, `cp`, `mv`, the
redirection tests, and the pipelines mutate or fork, so they stay
one-offs.  (The old note forbidding `donix>` redirection/pipeline
rows is **void** — `musl_sh` parses them now.)

The **full canary** (milestone-only) adds: `echo`, `cat`,
`musl_stat`, `musl_min`, `musl_malloc`, `musl_printf`, `musl_exec`,
`musl_readdir`, `musl_r10probe`, `brk_verify`, `brkraw`,
`brkgrow`, `musl_dup2`, `musl_dupfd`, `musl_ids`, `musl_getcwd`,
`busybox echo`, `busybox wc hello-world.txt`.

**Expected noise:** none beyond the known lines.  The serial log
has no `Unknown syscall:` lines, no `[fd]` lines (the fd trace is
off), and no `[a|b|c]` debug line (removed this session).  Expected
informational lines: `EXIT: pid=N state=2 parent=P qhead=Q` for
every forked child; `EXIT: pid=N state=1 parent=P qhead=Q` for a
forked shell's own exit; `EXIT-FALLBACK: switching to idle, ...`
when a child is the last runnable process.  `yes: Broken pipe`
from `busybox yes | busybox head -n 1` is expected.  The
`sys_open: f_open FAIL path=etc/...` lines from `busybox stat` and
the `sys_rename: f_rename FAIL ...` line from `busybox mv` are
expected diagnostics on failure paths.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **VFS layer (eventual).**  `sys_execve`'s three-attempt path
   resolution and `resolve_against_cwd` are both shims.  When a VFS
   lands, delete them; do not extend.
2. **Redirection of a builtin is silently ignored.**  `cd /bin >
   log` runs `cd`, creates no file, prints nothing.  Verified.
3. **A builtin in a pipeline is refused.**  `cd /bin | cat` prints
   `sh: builtin in pipeline not supported` and runs nothing.
4. **`sys_fcntl` refuses fd < 3** for subcommands other than
   `F_DUPFD`/`F_DUPFD_CLOEXEC`.  Deliberate; not on any path.
5. **`-EPIPE` is delivered without `SIGPIPE`.**  Narrower than the
   old docs claimed: `busybox yes | busybox head -n 1` does *not*
   hang.  The gap is a program that expects to be *killed* by
   `SIGPIPE` and does not check `write` — none found.  Fixing it
   means implementing signal delivery.

Also open: `newfstatat` (262) reserved, no dispatch case; `sys_open`
accepts non-directories with `O_DIRECTORY`; Ctrl-`[` not mapped to
ESC; `sys_utimensat` lacks `resolve_against_cwd`; `sys_munmap` is a
stub returning 0; `sys_brk`'s fixed `heap_base` and the 4 MB mmap
window are latent collisions; real FatFs timestamp storage;
`prctl` is minimal; busybox applet symlinks not installed;
syscall-table audit script; `musl_wait`'s WNOHANG loop spins;
`sys_mmap` rejects all non-anonymous mappings; pipes support one
concurrent reader and one concurrent writer; `put_file_slot`'s pipe
wake is coupled to `sys_close`'s wake.

---

## State on disk

**Real project root:** `/home/noneya/code/donix/`.

Config and source locations (relative to the root):

- `configs/busybox.config` — tracked canonical busybox config.
  Enabled applets: `cat`, `cp`, `cut`, `echo`, `false`, `head`,
  `ls`, `mkdir`, `mv`, `od`, `pwd`, `rm`, `rmdir`, `seq`, `sort`,
  `stat`, `tail`, `tee`, `test`, `touch`, `tr`, `true`, `uname`,
  `uniq`, `wc`, `yes`, `cmp`, `grep`, `sed`, `vi`, `clear`, plus
  `ash`.  Off (with reasons): `diff` (deliberate),
  `chmod`/`ln`/`mount` (need kernel work).
- `userland/musl/` — tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.  The pipe regression binaries
  (`pipe_step1`…`pipe_step3b`) live in `tests/` and are staged to
  the FAT image by `05_boot_kernel64/Makefile` — the image
  Makefile lists every userland ELF explicitly in two places
  (`USERLAND_ELFS` and the `mcopy_one` chain), so a new test must
  be added to both or it builds and never reaches the disk.
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  — gitignored; rebuild with `./toolchain/install_musl.sh`.
- `toolchain/{install_musl.sh,musl-gcc.sh}` — tracked.

Kernel sources: `04_kernel_64bit/`.

Scratch workspaces: transient, if any exist.  Not referenced here.

---

## Where things live

Not needed to start a session; ask for a file when the current task
needs it.  Paths below are relative to the tree root
(`/home/noneya/code/donix/`).

- `docs/strategy.md` — Phase A/B plan, rules, files-not-to-touch,
  tagging convention, git hygiene, recovery.
- `docs/gotchas.md` — every bug writeup, by subsystem.  Three
  entries now form a family, all "a decision correct only for the
  set of cases known at the time, invalidated when the set grew":
  "Low fds (0/1/2) are first-class" (session 34), "Negative
  fd-kind tests don't extend to new kinds" (session 36), and
  "Multi-write output races the child's kernel prints" (session
  37).  Read them together; expect a fourth.  Also new in session
  37: "`argv[cmd_argc] = 0` mutates argv in the child", "Blunt fd
  close in `fork_child`…", and the diagnostic note "`capture.txt`
  shows backspace history, not the corrected line."
- `docs/session-log.md` — commit tables and per-test canary notes.
  Rows are named by tag; scratch tags are dropped before a
  milestone push, so a row's tag may no longer resolve — the
  commit message is the record.
- `docs/open-issues.md` — full open-issues list.
- `docs/migration-history.md`, `docs/dons-os-history.md` —
  historical narrative (A1-A6, pre-fork).
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` — the first two
  frozen at `v0.6.0`; `LLD_BUG_REPORT.md` current.

---

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  `v0.6.6` is the last milestone (`pipe(2)`,
pipelines).  Session 37 made `donix>`'s own shell real: quote
stripping, `<`/`>`/`>>`/`|`/`&&`/`;` parsing, sequences, and
pipelines — all userland, no kernel change — committed on `dev`,
scratch-tagged, milestone deferred.  Session 37 also gave
donix-native `cat` a stdin mode (`cat < f` works without busybox).
Next: `sys_open` `O_DIRECTORY`, then a framebuffer console with
Terminus (the higher-value item).  One change at a time.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
