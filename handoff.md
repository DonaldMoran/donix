Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-09-30 (post-`v0.6.6`, framebuffer console)
**Current HEAD:** tag `20260930-viwinsize`, branch `dev`
**Last milestone:** `v0.6.6` (published) — `pipe(2)` done, pipelines
work
**Milestone status:** **no bump yet.**  21 commits on `dev`,
scratch-tagged, unpushed.  A `v0.6.7` bump is deferred until the
accumulated changes feel substantial enough; the scratch tags carry
the narrative.

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
annotations seed the final milestone narrative.

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

## Where we are — framebuffer console done, `vi` fills the screen

Session 38 put the console on a **1024×768 linear framebuffer** and
made `vi` use the whole screen.  Session 37 made `donix>`'s shell
real (`musl_sh` parses quotes, redirection, pipelines) and gave
donix-native `cat` a stdin mode.  All committed on `dev`,
scratch-tagged, unpushed; no milestone bump yet.

### Session 38 — framebuffer console

Eight commits, all tagged:

| Tag | What |
|---|---|
| `20260930-vbe` | VBE mode 0x118 (1024×768×24), framebuffer descriptor captured |
| `20260930-fb` | framebuffer mapped; `fb_putpixel`/`fb_fill`/`fb_fillrect` |
| `20260930-font` | Terminus 10×18 glyph blitter |
| `20260930-fbcon` | console drawing routed through a framebuffer-aware primitive |
| `20260930-shadow` | shadow cell grid: scroll, insert/delete, alt-screen |
| `20260930-fbclean` | test pattern removed |
| `20260930-winsz` | `TIOCGWINSZ` reports the real console grid |
| `20260930-viwinsize` | busybox `FEATURE_VI_WIN_RESIZE=y` — `vi` fills the screen |

**What it means:** the console — boot messages, shell prompt, typed
input, program output, `vi` — renders on the framebuffer at Terminus
10×18 (102×42 cells).  `vi` fills the screen, edits, saves, and the
alt-screen restores cleanly on exit.

**Key facts for future work:**
- Mode 0x118 on QEMU is **24bpp, pitch 3072**, framebuffer at
  physical **0xFD000000** — not 32bpp/4096.  The kernel uses the
  values captured in `BootInfo`, so this is data, not assumption.
- Pixels are **BGR** in memory (`fb.c`'s `FB_BYTE_R/G/B`).
- PSF glyph rows are **MSB-first** (`fb.c`'s `fb_putchar`).
- `vga.c` has a **shadow cell grid** (the source of truth on both
  backends); `con_put_cell` is the one draw chokepoint; the grid is
  `con_cols()`×`con_rows()` (102×42 fb, 80×25 VGA).
- **All of `vga.c`'s VT100 logic is unchanged** and drives either
  backend; callers outside `vga.c` are untouched.

### Session 37 — `musl_sh` is a real shell; `cat` has stdin

`donix>` strips quotes and parses `<`, `>`, `>>`, `|`, `&&`, `;`,
and pipelines.  `cat < file` works without busybox.  Userland only.
The `musl_sh` limitation every prior handoff carried is closed.

### What `v0.6.6` contributed

`pipe(2)` end to end (4 KB ring, directed wake, EOF, `-EPIPE`, the
exit-path wake, the stdio-guard inversion).  `dup(2)` (32).  Keyboard
fix (Shift+backslash).  See `docs/session-log.md`, Session 36.

### Known limitations

- **Redirection of a builtin is silently ignored.**  `cd /bin > log`
  runs `cd`, creates no file, prints no error.
- **A builtin in a pipeline is refused.**  `cd /bin | cat` prints
  `sh: builtin in pipeline not supported`.
- **`diff`, `chmod`, `ln`, `mount` are off** — the last three need
  their own syscalls.

---

## NEXT SESSION — pick one

The framebuffer was the big item and it is done.  What's left is a
set of small, independent items and the large subsystems.  Pick
**one**, do it, test it, tag it.

### Small, close gaps (recommended next)

1. **`newfstatat` (262)** — reserved number, no dispatch case.
   Delegates to `sys_stat` for `AT_FDCWD` or an absolute path.
   Unblocks `find`.
2. **Ctrl-`[` as ESC** — `scancode_to_ascii` has no Ctrl parameter
   yet; touches the keyboard layer.  Low urgency.
3. **`sys_fcntl` fd < 3 for the other subcommands** — `F_GETFL`,
   `F_SETFL`, `F_GETFD`, `F_SETFD` still refuse `fd < 3`.  Not on
   any current path.
4. **`sys_utimensat` cwd resolution** — it calls `strip_dot_prefix`
   but not `resolve_against_cwd`; one-line fix.
5. **More applets** — `awk`, `find` (needs `newfstatat`), `tar`.

### Deliberately later — larger

- **Signal delivery (`SIGPIPE`).**  See `open-issues.md` item 6.
  A subsystem (real `sys_rt_sigaction`, per-process handlers, a
  `SIGPIPE` raise on the `-EPIPE` write path).  Nothing currently
  exercises it — `busybox yes | busybox head -n 1` does *not* hang.
  Also the prerequisite for job control and `kill(2)`.  Its own
  milestone-scale effort when ready.
- **VFS layer.**  `open-issues.md` item 1.  Eventually; delete the
  shims when it lands, do not extend them.
- **Font size / resolution.**  The console is 10×18 at 1024×768.
  A bigger glyph (`ter-u24n.psf`, 12×24) or a bigger mode is a
  data change (`FB_FONT_*` + the `.psf` the Makefile embeds; the
  VBE mode number in `stage2.asm`) — do it when the current size
  strains, not before.

---

## Canary state

**The focused canary is green as of `v0.6.6`.**  Full table in
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

**Redirection rows (session 37):**

    echo hi > out.txt ; cat out.txt
    echo hi2 >> out.txt ; cat out.txt
    cat < out.txt
    busybox cat < out.txt

**Pipe regression suite (`userland/musl/tests/`):**

    pipe_step1    # object + non-blocking I/O
    pipe_step2    # blocking + directed wake
    pipe_step3    # EOF, EPIPE, dup-aware counts
    pipe_step3b   # exit-path wake

Run these when changing `sys_read`/`sys_write`/`sys_close`/
`put_file_slot`/`sys_fork`/`sys_pipe` or adding a `FILE_KIND_*`.
Not canary rows (they fork), but they are the only pipe regression
suite.

**Framebuffer / `vi` verification (session 38, not canary rows):**

    vi test            # fills the screen; status line on the last row
    # edit, :wq
    ./test             # the saved script runs

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
   resolution and `resolve_against_cwd` are both shims.  When a VFS
   lands, delete them; do not extend.
2. **Redirection of a builtin is silently ignored.**
3. **A builtin in a pipeline is refused.**
4. **`sys_fcntl` refuses fd < 3** for subcommands other than
   `F_DUPFD`/`F_DUPFD_CLOEXEC`.  Deliberate; not on any path.
5. **`-EPIPE` is delivered without `SIGPIPE`.**  Narrower than the
   old docs claimed: `busybox yes | busybox head -n 1` does *not*
   hang.  Fixing it means signal delivery.

Also open: `newfstatat` (262) reserved, no dispatch case; `sys_open`
accepts non-directories with `O_DIRECTORY`; Ctrl- `[` not mapped to
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

- `configs/busybox.config` — tracked canonical busybox config.
  Enabled applets: `cat`, `cp`, `cut`, `echo`, `false`, `head`,
  `ls`, `mkdir`, `mv`, `od`, `pwd`, `rm`, `rmdir`, `seq`, `sort`,
  `stat`, `tail`, `tee`, `test`, `touch`, `tr`, `true`, `uname`,
  `uniq`, `wc`, `yes`, `cmp`, `grep`, `sed`, `vi`, `clear`, plus
  `ash`.  `CONFIG_FEATURE_VI_WIN_RESIZE=y` (session 38).  Off (with
  reasons): `diff` (deliberate), `chmod`/`ln`/`mount` (need kernel
  work).
- `userland/musl/` — tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.  New tests must be added to both
  `USERLAND_ELFS` and the `mcopy_one` chain in
  `05_boot_kernel64/Makefile`.
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
  multi-write-interleave entry (session 37), and the byte-order
  entries (session 38).  New in session 38: "Assumed byte-order
  conventions are the same shape" (BGR pixels, MSB-first glyphs)
  and "The option name describes its most visible effect, not its
  scope" (`FEATURE_VI_WIN_RESIZE`).  Read them together; expect
  more.
- `docs/session-log.md` — commit tables and per-test canary notes.
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
pipelines).  Session 37 made `donix>`'s own shell real and gave
`cat` a stdin mode.  Session 38 put the console on a 1024×768 linear
framebuffer with Terminus 10×18 text — `vi` fills the screen, edits,
saves, and the alt-screen restores.  All committed on `dev`,
scratch-tagged, milestone deferred.  Next: pick one small item
(`newfstatat`, Ctrl-`[`, or similar), or the signal-delivery
subsystem.  One change at a time.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
