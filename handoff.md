Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-09-28 (session 31 → pre-session 32)
**Current HEAD:** tag `v0.6.3`, branch `dev`
**Last milestone:** `v0.6.3` (published) — the shell is fully usable
**Next milestone:** `v0.6.4` — **the basics are done** (see the list below)

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

**Both trees use `04_kernel_64bit/` for the kernel source.**  The
numbering was historically inconsistent; as of session 31 the two
match.  Still verify before porting — locate the four files
(`vga.c`, `keyboard.c`, `user_syscall.c`, `include/syscall.h`) by
name rather than trusting the number.  There is also a
`05_boot_kernel64/` directory in each tree: it holds the boot
chain assembly and the image builder, not the kernel C sources.

---

## Where we are — the Session 31 port is done

**The VT100/vi work is ported and verified on the real project.**
Session 31 copied five files from the experimental tree to the
real project and verified the full vi round-trip end to end.
Nothing is committed yet; the tree is dirty at `v0.6.3` and the
port is the uncommitted change set.

### What was ported (session 31)

The diff survey found **five files differ** and **two are
identical**.  Identical files were not copied; the five that
differ were.

| File | Change |
|---|---|
| `04_kernel_64bit/keyboard.c` | ESC (0x01→0x1B), Backspace (0x0E→0x7F), Enter (0x1C→CR) |
| `04_kernel_64bit/vga.c` | full VT100/ANSI CSI parser, alt-screen, SGR |
| `04_kernel_64bit/user_syscall.c` | `open(2)` flag fix + 4 new syscalls |
| `04_kernel_64bit/include/syscall.h` | 4 new `SYS_*` defines (77, 235, 261, 280) |
| `configs/busybox.config` | `CONFIG_VI=y`, `CONFIG_TOUCH=y`, full `FEATURE_VI_*` |

Not copied (identical in both trees): `include/vga.h`,
`fatfs/ffconf.h`.

**Corrections to the previous handoff, worth keeping:**

- The prior handoff claimed `configs/busybox.config` in the real
  tree had **already been updated** for Session 31. It had
  **not** — donix still had `CONFIG_VI` and `CONFIG_TOUCH` off.
  The config was updated as part of the port.
- The prior handoff described a **four-file** port. It was
  **five**: `include/syscall.h` was missing from the list. The
  `SYS_FTRUNCATE` / `SYS_UTIMES` / `SYS_FUTIMESAT` / `SYS_UTIMENSAT`
  macros live there, and `user_syscall.c` will not compile
  without them.

### Verification (session 31)

Full vi round-trip on the real project, serial log captured:

```
vi don.txt          # launches; alt-screen (?1049h); ~ rows painted
i                   # insert mode
This is a new test in donix!
ESC                 # leaves insert mode
:wq                 # saves; 'don.txt' 1L, 29C; alt-screen (?1049l)
ls                  # don.txt present
cat don.txt         # prints "This is a new test in donix!"
```

Earlier in the session, `touch don2.txt` was also verified to
create a file that `ls` then lists. **Zero `Unknown syscall:`
lines** in any of these runs.

The startup `sys_open: f_open FAIL path=don.txt flags=0x8000
mode=0x01 r=4` on vi launch is **expected and correct** — vi
opens a nonexistent file for read (`O_RDONLY`, `FA_READ`), FatFs
returns `FR_NO_FILE`, and vi falls back to new-file mode. Do not
try to silence it.

### The two VGA tunables are currently 0

`vga.c` ships with:

```
#define VGA_TRACE_UNHANDLED   0
#define VGA_REPLY_TO_QUERIES  0
```

Set to **1** during development for diagnostics (`[vga] unhandled
CSI:` lines on the serial port, and DSR/DA replies). Both were
flipped to 0 before the final verification run, and that run
passed with them off. **Leave them at 0.**

If `CONFIG_FEATURE_VI_ASK_TERMINAL` is ever turned on in the
busybox config, vi will send `ESC[6n` at startup and wait for a
reply. `VGA_REPLY_TO_QUERIES` must go back to 1 first, or vi
will stall at launch.

### Still uncommitted

Nothing in this port is committed. The tree is dirty at `v0.6.3`.
The plan is to commit as **`v0.6.4`** once the basics list below
is finished and the documentation is updated.  `kmain.c` will
need its version string bumped to match; the project owner will
do that when the time comes.

### Known limitation (unchanged)

`cd ..` at `donix>` fails (`cd: cannot cd to ..`).  The `musl_sh`
`cd` builtin passes the raw `..` to `chdir`, and FatFs has no `..`
entry.  **`cd ..` inside ash works** (ash resolves `..` against
its own `$PWD` first).  This is now an item on the basics list —
see below.

---

## The milestone: `v0.6.4` — the basics

`v0.7.0` was previously proposed as "vi is usable".  The project
owner has redefined the milestone: **`v0.6.4` is "the basics are
done"**, and vi is one item inside that, not the whole milestone.

The basics are:

1. **Create, read, write files.** Done — `touch`, `cat`, redirection.
2. **Create directories.** Done — `mkdir` exists and works.
3. **Remove files.** **Missing.** `rm`/`unlink` are off in the
   busybox config, and `SYS_UNLINK` (87) has no dispatch case.
4. **Remove directories.** **Missing.** `CONFIG_RMDIR` is off,
   and `SYS_RMDIR` (84) has no dispatch case. Kept off
   deliberately in session 31 because the kernel side does not
   exist; leaving it on would ship a `rmdir` applet that fails
   at runtime and could produce `Unknown syscall:` noise.
5. **`cd` up and down from both shells.** Partial. `cd ..` works
   inside ash; it fails at `donix>`. See the known limitation
   above.
6. **A process that faults is killed cleanly.** The
   `fault_kill_current(0x0E)` path in `isr14_handler` is in but
   unverified end-to-end. Needs a test binary that dereferences a
   bad pointer without setting `g_expect_fault`.
7. **Full-screen software runs.** Done — session 31, vi verified.

Items 3, 4, 5, and 6 remain.  Items 1, 2, and 7 are done.

**Nothing else should be added to this list in this session.** If
a candidate arises, record it in `docs/open-issues.md` for after
the milestone.

---

## NEXT SESSION — do this first

**Session 32.  Pick one of the remaining basics items and finish
it.  One at a time, as always.**

Suggested order, smallest first:

1. **`cd ..` at `donix>`** (item 5).  Make `builtin_cd` resolve
   `.`/`..` against `getcwd()` before calling `chdir`, or make
   `sys_chdir` resolve through `resolve_against_cwd` the way the
   path syscalls do.  Small; well understood.
2. **`unlink` / `rm`** (item 3).  Two halves: add `SYS_UNLINK`
   (87) dispatch to a `sys_unlink` that calls FatFs `f_unlink`,
   and turn `CONFIG_RM=y` back on in `configs/busybox.config`.
   Test by creating and removing a file.
3. **`rmdir`** (item 4).  Same shape as `unlink`: `SYS_RMDIR`
   (84) dispatch, FatFs `f_unlink` with an empty-directory check,
   and `CONFIG_RMDIR=y`.  Wait until `unlink` works so the
   pattern is established.
4. **The user-mode `#PF` test binary** (item 6).  Larger than
   the others; needs a new userland test that faults on purpose
   and confirms the process is killed without taking the kernel
   down.

Pick **one**, do it, test it, and record it. Do not bundle.

When all four are done and the README / handoff / docs are
updated and `kmain.c`'s version string is bumped, commit as
`v0.6.4` with a message naming the milestone.

---

## Canary state

**The focused canary below is green as of `v0.6.3`.**  The session
31 port has not been committed yet, so the canary is still
recorded against `v0.6.3`, but everything in it passes on the
current dirty tree too.  The vi round-trip above is a one-off
verification of the port, not a canary row — the canary must not
mutate the disk, and vi save does.

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

**Do NOT add `cd ..` at `donix>` as a canary row** -- it fails (see
Known limitation).  `cd ..` inside ash is fine.  Once item 5 on
the basics list is fixed, this note can be revisited.

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`; use
`busybox sh` or `/bin/busybox sh`.

The **full canary** (milestone-only) adds: `echo`, `cat`,
`musl_stat`, `musl_min`, `musl_malloc`, `musl_printf`,
`musl_exec`, `musl_readdir`, `musl_r10probe`, `brk_verify`,
`brkraw`, `brkgrow`, `musl_dup2`, `musl_dupfd`, `musl_ids`,
`musl_getcwd`, `busybox echo`, `busybox wc hello-world.txt`.

**Canary rows must not mutate the disk.**  No standing `mkdir`
row; `rmdir`/`unlink` do not exist yet to clean up.  Once
`unlink` and `rmdir` land, a paired create/remove canary row
becomes possible and should be added.

**Expected noise:** none.  The serial log has no `Unknown syscall:`
lines as of this milestone.  The `sys_execve: pid=... (name)`
trace lines are informational, not errors.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **`unlink` / `rm` missing.**  `SYS_UNLINK` (87) has no
   dispatch case; `CONFIG_RM` is off.  On the basics list.
2. **`rmdir` missing.**  `SYS_RMDIR` (84) has no dispatch case;
   `CONFIG_RMDIR` is off (deliberately — see session 31 notes).
   On the basics list.
3. **`cd ..` at `donix>` fails** (the `musl_sh` builtin passes
   raw `..` to FatFs).  ash is unaffected.  On the basics list.
4. The user-mode `#PF` kill path (`fault_kill_current(0x0E)` in
   `isr14_handler`) is in but unverified end-to-end.  On the
   basics list.
5. **VFS layer (eventual).**  `sys_execve`'s path resolution is a
   shim for a filesystem layer donix does not have.  When it
   lands, delete the shim; do not extend it.

Also open: `newfstatat` (262) has a reserved number but no
dispatch case; busybox applet symlinks not installed; syscall-table
audit script (the table itself was audited in session 30 and is
correct; the script would keep it correct); fork is O(~6 MB) per
call; `sys_munmap` is a stub; `sys_brk`'s fixed `heap_base` and
the 4 MB mmap window are latent collisions; `musl_wait` emits
hundreds of progress dots; real FatFs timestamp storage (the three
timestamp syscalls added in session 31 return 0 without storing).

---

## State on disk

Paths, with the correct roots.

- **Real project root:** `/home/noneya/code/donix/`.
- **Experimental tree root:** `/home/noneya/code/testme/`.

Config and source locations (relative to whichever root is being
edited; both trees have the same top-level layout):

- `configs/busybox.config` — tracked canonical busybox config.
  **Updated in session 31** to enable `CONFIG_VI=y` (with
  `FEATURE_VI_MAX_LEN=4096` and the `FEATURE_VI_*` set),
  `CONFIG_TOUCH=y`.  **`CONFIG_RMDIR` is deliberately off** — the
  kernel side does not exist yet.
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
- `docs/gotchas.md` — every bug writeup, by subsystem.  Session
  31 candidates to add (not yet written):
  - The `sys_getcwd` absolute-cwd requirement (musl rejects a
    non-absolute cwd).
  - The `puts_raw`-vs-`printf` newline quirk.
  - The `open(2)` flag-bit mismatch (`O_CREAT=0x40`, not `0x200`;
    `O_TRUNC=0x200`, not `0x400`; `O_APPEND=0x400`, not `0x8`).
  - The ESC-key scancode table gap (`scancode_ascii[0x01]` was
    unassigned, so ESC produced 0 and was dropped by the buffer's
    NUL guard).
  - The alt-screen-cannot-be-a-pointer-swap fact on VGA text mode.
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
`userland/musl/`.  The shell is fully usable and vi works — the
VT100/vi port from the experimental tree landed in session 31 and
is verified, but is not yet committed.  Session 32: finish the
basics list (`rm`, `rmdir`, `cd ..` at `donix>`, the user-mode
`#PF` test), then commit as `v0.6.4`.  Do not pick any candidate
outside the basics list until the list is done.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
