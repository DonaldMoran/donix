Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-09-29 (session 32 → pre-session 33)
**Current HEAD:** tag `v0.6.4`, branch `dev`
**Last milestone:** `v0.6.4` (published) — **the basics are done**
**Next milestone:** none yet — pick a direction (see below)

Commits are named by tag only, never by SHA.  **Working tags
(`2026092x-*`) are local scratch restore points** — they exist while
a milestone is being developed and are **dropped before the
milestone is pushed**.  Only `v*` tags go to the remote and are
permanent.  The commit record is `docs/session-log.md`; the commit
message carries the narrative.

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 syscall ABI, natively. Newlib is gone.
Userland is a tracked source tree at `userland/musl/`. Phase B
(busybox) is well underway.

Full strategy, rules, and the one-change-at-a-time discipline:
`docs/strategy.md`.

---

## Trees on disk

Two working copies exist.  Know which is which before touching
anything.

- **Real project:** `/home/noneya/code/donix/`.  This is the
  tracked repository, the source of truth, and where the `v*`
  tags live.  All work in the milestone ends up here.
- **Experimental tree:** `/home/noneya/code/testme/`.  A scratch
  copy used for exploratory work and for testing changes before
  they go into the real project.  Safe to reset; nothing in it is
  permanent.

The two trees share the same top-level layout.  The C sources for
the kernel live under a numbered subdirectory in each:

- `/home/noneya/code/donix/04_kernel_64bit/` — real project
  kernel sources.
- `/home/noneya/code/testme/04_kernel_64bit/` — experimental tree
  kernel sources.

Both trees use `04_kernel_64bit/` for the kernel source.  Verify
by file name before porting — do not trust the number alone.
`05_boot_kernel64/` holds the boot chain assembly and the image
builder, not the kernel C sources.

---

## Where we are — `v0.6.4`, the basics are done

**The basics all work.**  Session 31 ported the VT100/vi work and
session 32 finished the remaining basics items.  Everything below
is on `dev` at tag `v0.6.4`.

### What landed in session 31 (the VT100/vi port)

Five files copied from the experimental tree to the real project:

| File | Change |
|---|---|
| `04_kernel_64bit/keyboard.c` | ESC (0x01→0x1B), Backspace (0x0E→0x7F), Enter (0x1C→CR) |
| `04_kernel_64bit/vga.c` | full VT100/ANSI CSI parser, alt-screen, SGR |
| `04_kernel_64bit/user_syscall.c` | `open(2)` flag fix + 4 new syscalls |
| `04_kernel_64bit/include/syscall.h` | 4 new `SYS_*` defines (77, 235, 261, 280) |
| `configs/busybox.config` | `CONFIG_VI=y`, `CONFIG_TOUCH=y`, full `FEATURE_VI_*` |

Not copied (identical in both trees): `include/vga.h`,
`fatfs/ffconf.h`.

**Corrections to the pre-session-31 handoff, worth keeping:**

- That handoff claimed `configs/busybox.config` in the real tree had
  **already been updated** for Session 31. It had **not** — donix
  still had `CONFIG_VI` and `CONFIG_TOUCH` off.  The config was
  updated as part of the port.
- That handoff described a **four-file** port. It was **five**:
  `include/syscall.h` was missing from the list. The
  `SYS_FTRUNCATE` / `SYS_UTIMES` / `SYS_FUTIMESAT` /
  `SYS_UTIMENSAT` macros live there, and `user_syscall.c` will not
  compile without them.

### What landed in session 32 (the basics)

| Item | What |
|---|---|
| `rm` | `sys_unlink` + `sys_mkdir` now call `resolve_against_cwd`; `CONFIG_RM=y` |
| `rmdir` | `SYS_RMDIR` (84) handler + dispatch; `CONFIG_RMDIR=y` |
| `cd ..` at `donix>` | `sys_chdir` now calls `resolve_against_cwd` |
| `#PF` test | `userland/musl/tests/fault_pf.c`; user-mode `#PF` kills the process |
| `#GP` fix | `isr13_handler` kills ring-3 `#GP` (was halting the kernel) |
| kernel shell | `kmain_shell_loop` reads DEL/CR, matching `keyboard.c` |

The `#GP` fix came from the first `fault_pf` run: the test used a
non-canonical address (`0xDEADBEEF0000`), which raises `#GP`, not
`#PF`, and the kernel halted instead of killing the process.  The
handler now keys its ring test on `CS & 3`, not the error code —
`#GP`'s error code is 0 for the non-canonical case and carries no
ring information.  `#PF` keeps its `error_code & 4` test, which is
correct for that vector.

### Verification (session 32)

All of the following were run on the real project with a captured
serial log and **zero `Unknown syscall:` lines**:

```
# vi round-trip
vi don.txt; i; This is a test!; ESC; :wq
cat don.txt                  # prints the text

# file and directory removal
touch a.txt; rm a.txt; ls a.txt          # gone
mkdir y; touch y/f; rmdir y              # fails: directory not empty
rm y/f; rmdir y                          # succeeds
mkdir z; cd z; mkdir w; rmdir w; cd ..; rmdir z   # cwd-relative, works

# fault kill
fault_pf                     # #PF diagnostic, process killed, shell returns
```

The startup `sys_open: f_open FAIL path=don.txt flags=0x8000
mode=0x01 r=4` on vi launch is **expected and correct** — vi
opens a nonexistent file for read (`O_RDONLY`, `FA_READ`), FatFs
returns `FR_NO_FILE`, and vi falls back to new-file mode. Do not
try to silence it.

### The two VGA tunables are 0

`vga.c` ships with `VGA_TRACE_UNHANDLED 0` and
`VGA_REPLY_TO_QUERIES 0`.  Set to 1 during development for
diagnostics; leave at 0.  If `CONFIG_FEATURE_VI_ASK_TERMINAL` is
ever turned on, `VGA_REPLY_TO_QUERIES` must go back to 1 or vi
stalls at launch waiting for a DSR reply.

---

## NEXT SESSION — pick a direction

`v0.6.4` closed the basics.  There is no committed next milestone.
The candidate list (full text in `ROADMAP.md`, "After v0.6.4"):

Small, close gaps:

1. **`newfstatat` (262)** — reserved number, no dispatch case.
   Small wrapper over `sys_stat` now that cwd resolution exists.
2. **`sys_open` `O_DIRECTORY` fix** — return `-ENOTDIR` when the
   target is a file.  Latent today, correct to close.
3. **Ctrl-`[` as ESC** — `scancode_to_ascii` has no Ctrl parameter
   yet.  Deferred during the terminal work.

Broaden busybox coverage — `cp`, `mv`, `grep`, `sed`, `awk`, `tar`.
Each missing syscall is its own commit.  Watch for
`Unknown syscall: N`.

Larger: pipes and redirection (`pipe(2)`), environment variables
(`envp`), the VFS layer (`sys_execve`'s three-attempt block and
`resolve_against_cwd` are both shims), kernel hardening (real COW,
munmap, page-table teardown).

Pick **one**, do it, test it, tag it.  One change at a time.

---

## Canary state

**The focused canary below is green as of `v0.6.4`.**  The vi
round-trip and `fault_pf` are one-off verifications, not canary
rows — the canary must not mutate the disk, and vi save and
`fault_pf` both do.

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
    busybox pwd                 # /bin (after cd /bin)
    busybox ash
    # at the ash prompt: pwd, cd /bin, pwd, ls, exit
    # back at donix>: hello

**`cd ..` at `donix>` now works** and can be added as a canary row
in the next session's run.  It was a known failure through
`v0.6.3`; `sys_chdir`'s `resolve_against_cwd` call fixed it.

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`; use
`busybox sh` or `/bin/busybox sh`.

The **full canary** (milestone-only) adds: `echo`, `cat`,
`musl_stat`, `musl_min`, `musl_malloc`, `musl_printf`,
`musl_exec`, `musl_readdir`, `musl_r10probe`, `brk_verify`,
`brkraw`, `brkgrow`, `musl_dup2`, `musl_dupfd`, `musl_ids`,
`musl_getcwd`, `busybox echo`, `busybox wc hello-world.txt`.

**Canary rows must not mutate the disk.**  A paired
`mkdir`/`rmdir` row is now possible — `rmdir` exists — but it
still mutates, so it stays out of the canary.  Use it as a
one-off verification instead.

**Expected noise:** none.  The serial log has no `Unknown syscall:`
lines.  `sys_execve: pid=... (name)` trace lines are informational.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **VFS layer (eventual).**  `sys_execve`'s three-attempt path
   resolution and `resolve_against_cwd` are both shims for a
   filesystem layer donix does not have.  When a VFS lands, delete
   them; do not extend.
2. `newfstatat` (262) — reserved number, no dispatch case.
3. `sys_open` accepts non-directories with `O_DIRECTORY`.  Return
   `-ENOTDIR` when the target is a file.
4. Fork is O(~6 MB) per call — real COW is the long-term fix.
5. `sys_munmap` is a stub returning 0.

Also open: `sys_brk`'s fixed `heap_base` and the 4 MB mmap window
are latent collisions; real FatFs timestamp storage (the three
timestamp syscalls return 0 without storing); `prctl` is minimal
(`PR_SET_NAME` accepted and dropped); `sys_utimensat` also lacks
`resolve_against_cwd` (found in session 32, not on the basics
list); busybox applet symlinks not installed; syscall-table audit
script; `musl_sh` echoes garbage on backspace; `musl_wait`'s
WNOHANG loop spins.

---

## State on disk

Paths, with the correct roots.

- **Real project root:** `/home/noneya/code/donix/`.
- **Experimental tree root:** `/home/noneya/code/testme/`.

Config and source locations (relative to whichever root is being
edited; both trees have the same top-level layout):

- `configs/busybox.config` — tracked canonical busybox config.
  Enables `CONFIG_VI`, `CONFIG_TOUCH`, `CONFIG_RM`, `CONFIG_RMDIR`.
- `userland/musl/` — tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  — gitignored; rebuild with `./toolchain/install_musl.sh`.
- `toolchain/{install_musl.sh,musl-gcc.sh}` — tracked.

Kernel sources:

- Real project kernel: `/home/noneya/code/donix/04_kernel_64bit/`.
- Experimental kernel: `/home/noneya/code/testme/04_kernel_64bit/`.

Scratch copies from earlier exploring:

- `/home/noneya/code/x/` — an older scratch copy.  Fully ported;
  safe to delete whenever.
- `/home/noneya/code/y/` — a second scratch copy used for a
  bisection experiment.  Safe to delete.

Docs live under `docs/` in each tree; see below.

---

## Where things live

Not needed to start a session; ask for a file when the current task
needs it.  Paths below are relative to a tree root
(`/home/noneya/code/donix/` or `/home/noneya/code/testme/`).

- `docs/strategy.md` — Phase A/B plan, rules, files-not-to-touch,
  tagging convention, git hygiene, recovery.
- `docs/gotchas.md` — every bug writeup, by subsystem.
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
`userland/musl/`.  `v0.6.4`: the basics are done — create, read,
write, remove files and directories; `cd` up and down from both
shells; full-screen software runs; a faulting process is killed
cleanly.  Session 33: pick one item from ROADMAP.md's "After
v0.6.4" list and do it.  One change at a time.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
