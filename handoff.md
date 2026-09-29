Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-09-29 (session 33 â†’ pre-session 34)
**Current HEAD:** tag `20260930-busybox-text-utils`, branch `dev`
**Last milestone:** `v0.6.4` (published) â€” **the basics are done**
**Next milestone:** `v0.6.5` candidate â€” session 33 is large enough
to tag (see below)

Commits are named by tag only, never by SHA.  **Working tags
(`2026092x-*`) are local scratch restore points** â€” they exist while
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

- `/home/noneya/code/donix/04_kernel_64bit/` â€” real project
  kernel sources.
- `/home/noneya/code/testme/04_kernel_64bit/` â€” experimental tree
  kernel sources.

Both trees use `04_kernel_64bit/` for the kernel source.  Verify
by file name before porting â€” do not trust the number alone.
`05_boot_kernel64/` holds the boot chain assembly and the image
builder, not the kernel C sources.

---

## Where we are â€” session 33, scripts run, redirection works

**Thirteen commits on `dev`, all scratch-tagged, all unpushed.**
The last is `88a504d` (`busybox: enable text utilities ...`).

Session 33 opened with "make shell scripts run" and closed with a
working shell, working redirection, and a broad busybox command
set.  Four threads:

### Thread 1 -- script execution

| Tag | What |
|---|---|
| `20260929-max-process-files-64` | `MAX_PROCESS_FILES` 8 â†’ 64 |
| `20260929-execve-script-fallback` | execve strips `./` before open; returns `ENOEXEC` for short non-ELF |

**`MAX_PROCESS_FILES` 8 â†’ 64.**  At 8, busybox ash's script
fork+exec path fails with `sh: 3: Invalid argument`; at 64 the
same script prints its output.  Controlled: same build, same
script, only the constant varied.  Cost +448 B/pcb; assembly-safe
(`file_table` sits past `block_kind`, which `context_switch.asm`
pins).

**`sys_execve` two fixes.**  (1) Normalize the path with
`strip_dot_prefix` before the first `f_open`, so `./test.sh` no
longer fails with `FR_INVALID_NAME` at the open.  (2) Return
`ENOEXEC`, not `EIO`, for a short non-ELF read, so ash falls back
to running the script through `sh`.

Result: `./test.sh`, `sh test.sh`, and `busybox sh test.sh` from
`donix>` all print the script output.

### Thread 2 -- two new syscalls

| Tag | What |
|---|---|
| `20260929-uname` | `uname(2)` 63 + busybox applet |
| `20260929-lseek` | `lseek(2)` 8 |

**`uname` is not needed for scripting.**  The experimental tree's
comment attributing `sh: 3: Invalid argument` to a missing `uname`
was wrong -- that string came from fd exhaustion.  `uname` is a
correctness port.

**`lseek` was surfaced by `head -n N`,** which seeks to `SEEK_END`
to size the file.  Without it: `Unknown syscall: 8` after correct
output.

### Thread 3 -- shell redirection (the biggest fix of the session)

| Tag | What |
|---|---|
| `20260930-redirect` | inherit fds across fork; honor dup2'd fds 0/1/2; close them on exit |

`cmd < file` and `cmd > file` did not work.  Temporary serial
traces found **three interlocking bugs**, all in one commit:

1. **`sys_read`/`sys_write` hard-coded fds 0/1/2.**  `sys_read`
   sent `fd == 0` to the keyboard; `sys_write` sent `fd == 1 || 2`
   to the screen -- both *before* consulting `file_table[]`.  After
   ash's `dup2(file_fd, 0)`, `read(0, ...)` still read the
   keyboard (`cat < file` hung).  After `dup2(file_fd, 1)`,
   `write(1, ...)` still wrote the screen (the file stayed empty).
2. **`sys_fork` never copied `file_table[]`.**  A forked child
   started with no fds.  ash opens the redirect file, `dup2`s it
   onto fd 0, then forks and execs -- the child saw fd 0 as NULL.
   Fixed by copying every open slot and bumping its refcount.
3. **`close_all_files` started at `i = 3`.**  fds 0/1/2 were never
   closed on exit, so a redirect-created file's refcount never
   reached 0, `f_close` never ran, and FatFs never committed the
   directory entry -- `cat out.txt` after `echo hi > out.txt` read
   an empty file.

Plus a new `get_file_slot_any()` helper that accepts fds 0-2; the
general `get_file_slot()` keeps its `fd >= 3` guard.

Verified: `cat < hello-world.txt` prints the file and returns;
`echo hi > out.txt` is silent; `cat out.txt` prints `hi`.

### Thread 4 -- busybox applets

Twelve applets enabled across two commits, each **probed first**:

| Tag | Applet(s) |
|---|---|
| `20260929-busybox-head` | `head` |
| `20260929-busybox-tail` | `tail` |
| `20260929-busybox-cp` | `cp` |
| `20260929-busybox-grep` | `grep` |
| `20260929-busybox-sed` | `sed` |
| `20260930-busybox-text-utils` | `cut`, `sort`, `stat`, `tee`, `test`, `tr`, `cmp` |

All tested and confirmed working.  `wc`, `echo`, `cat`, `ls`,
`pwd`, `mkdir`, `rm`, `rmdir`, `touch`, `vi` were already enabled
from earlier sessions.

### Docs commits

| Tag | What |
|---|---|
| `20260929-docs-session-33` | first docs pass (script execution) |
| `20260929-docs-session-33-complete` | second docs pass (uname, lseek, five applets) |

This session-end pass (the third) extends both `gotchas.md` and
`session-log.md` for the redirection fix and the text-utilities
batch, and rewrites this file.

### Known failures, deliberately off

- **`uniq`** -- hangs on any invocation, with or without `-c`.
  Cause unknown; needs a trace-first investigation.  `CONFIG_UNIQ`
  is off.
- **`od`** -- needs `readv(2)` (19), which donix lacks.
  `CONFIG_OD` is off.
- **`diff`** -- deliberately off; larger surface area.

### Known limitation: `musl_sh`

`donix>` (musl_sh) does not strip shell quotes and does not parse
redirection.  All tests involving `<`, `>`, `|`, `&&`, `;`, or
quoting must be run from ash (`busybox sh` from `donix>`, or the
auto-launched ash at boot).  See `docs/gotchas.md`.

### Not carried forward from `testme`

The extra `busybox.config` applets that need kernel work
(`chmod`, `ln`, `mount`/`umount`), `FEATURE_ALLOW_EXEC` (proven
unnecessary), and `env` (cosmetic -- `envp` is ignored by
`sys_execve`).  `testme` still has these; they are deliberately
left out.

---

## NEXT SESSION â€” pick a direction

Session 33 was large (13 commits).  No committed next milestone.

**Recommended first: `mv` via `rename(2)`.**  This was the
"bigger payoff" item deferred from earlier in session 33.  It is
real kernel work, not a config toggle:

- **`rename(2)` is syscall 82.**  `arg0 = oldpath`,
  `arg1 = newpath`, returns 0 or `-errno`.
- It is the **first two-path syscall** -- both `oldpath` and
  `newpath` must go through `resolve_against_cwd` then
  `strip_dot_prefix`.  That pattern will be reused for `link`
  and `symlink`.
- **FatFs wrinkle:** `f_rename` does **not** replace an existing
  destination -- it returns `FR_EXIST`, which `fatfs_errno` maps
  to `-EPERM`.  Linux `rename` replaces.  First cut: match FatFs
  (don't replace), document the difference.  Add unlink-then-
  rename later if needed.
- **FatFs wrinkle 2:** `f_rename` on FAT16 only works within a
  directory.  Cross-directory rename fails.  Fine for the
  common `mv a b` case; note the limitation.
- **Plan:** commit `sys_rename` first (kernel only), then
  `CONFIG_MV=y` and test `mv hello-world.txt hi.txt` -- same
  two-commit pattern as `lseek` + `head`.

**Then: `readv` (19) + `od`.**  Small, self-contained.  `readv`
is the mirror of `writev` (20), which exists:
- Copy the iov array through `safe_copy_from_user` (same
  discipline as `writev`).
- Loop over `sys_read`, accumulate; return short count on a
  short read.
- Then re-enable `CONFIG_OD` and commit.

**Then: `uniq`.**  Unknown cause.  Trace-first: add temporary
`serial_print` lines (or reuse the `[read]` pattern) to see what
syscalls `uniq` makes before it hangs.  Probably a missing
syscall or a busybox-internal loop on EOF.

**Small, close gaps:**

1. **`newfstatat` (262)** -- reserved number, no dispatch case.
2. **`sys_open` `O_DIRECTORY` fix** -- return `-ENOTDIR` when the
   target is a file.
3. **Ctrl-`[` as ESC** -- `scancode_to_ascii` has no Ctrl
   parameter yet.
4. **`sys_utimensat` cwd resolution** -- found in session 32; it
   calls `strip_dot_prefix` but not `resolve_against_cwd`.
5. **`musl_sh` quote stripping + redirection parsing** -- the two
   userland gaps surfaced this session.  A tokenizer fix,
   userland-only.

**Larger:** pipes and redirection (`pipe(2)`), environment
variables (`envp`), the VFS layer (`sys_execve`'s three-attempt
block and `resolve_against_cwd` are both shims), kernel hardening
(real COW, munmap, page-table teardown).

**Or tag `v0.6.5`.**  Session 33 is a coherent milestone:
"scripts run; redirection works; broad busybox command set."  If
tagged, drop all 13 `20260929-*` / `20260930-*` scratch tags
first, tag `v0.6.5`, rewrite this file, push.

Pick **one**, do it, test it, tag it.  One change at a time.

---

## Canary state

**The focused canary is green as of
`20260930-busybox-text-utils`.**  Full table in
`docs/session-log.md`.  Script execution, redirection, and the
applet one-offs are verification, not canary rows -- the canary
must not mutate the disk.

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

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`; use
`busybox sh` or `/bin/busybox sh`.

**Do NOT add a `donix>` redirection row.**  `musl_sh` does not
parse `<` or `>`.  Redirection tests go through ash.

The **full canary** (milestone-only) adds: `echo`, `cat`,
`musl_stat`, `musl_min`, `musl_malloc`, `musl_printf`,
`musl_exec`, `musl_readdir`, `musl_r10probe`, `brk_verify`,
`brkraw`, `brkgrow`, `musl_dup2`, `musl_dupfd`, `musl_ids`,
`musl_getcwd`, `busybox echo`, `busybox wc hello-world.txt`.

**Canary rows must not mutate the disk.**  The new read-only
applets (`head`, `tail`, `grep`, `sed`, `cut`, `sort`, `stat`,
`cmp`, `wc`) could be added as canary rows.  `tee`, `cp`, and the
redirection tests mutate, so they stay one-offs.

**Expected noise:** none.  The serial log has no `Unknown syscall:`
lines.  Two informational lines are expected:
`EXIT: pid=N state=1 parent=2 qhead=N` (a forked busybox shell's
own exit) and `EXIT-FALLBACK: switching to idle, ...` (in
`musl_fork` when the child is the last runnable process).  The
`sys_open: f_open FAIL path=etc/...` lines from `busybox stat` are
expected -- those files do not exist and `stat` falls back cleanly.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **VFS layer (eventual).**  `sys_execve`'s three-attempt path
   resolution and `resolve_against_cwd` are both shims for a
   filesystem layer donix does not have.  When a VFS lands, delete
   them; do not extend.
2. **`mv` needs `rename(2)` (82); `ln` needs `link`/`symlink`;**
   `chmod` needs `chmod(2)` (90).  Each is its own syscall.
3. **`od` needs `readv(2)` (19).**  Mirror of `writev` (20).
4. **`uniq` hangs.**  Cause unknown; trace-first.
5. **`musl_sh` does not strip quotes or parse redirection.**
   Userland-only fix.  See `docs/gotchas.md`.

Also open: `newfstatat` (262) reserved, no dispatch case;
`sys_open` accepts non-directories with `O_DIRECTORY`; Ctrl- `[`
not mapped to ESC; `sys_utimensat` lacks `resolve_against_cwd`;
`sys_munmap` is a stub returning 0; `sys_brk`'s fixed `heap_base`
and the 4 MB mmap window are latent collisions; real FatFs
timestamp storage (the three timestamp syscalls return 0 without
storing); `prctl` is minimal (`PR_SET_NAME` accepted and dropped);
busybox applet symlinks not installed; syscall-table audit script;
`musl_wait`'s WNOHANG loop spins; `sys_mmap` rejects all
non-anonymous mappings (a file-backed `mmap` caller will get
`-ENOMEM` and must fall back to `read`).

---

## State on disk

Paths, with the correct roots.

- **Real project root:** `/home/noneya/code/donix/`.
- **Experimental tree root:** `/home/noneya/code/testme/`.

Config and source locations (relative to whichever root is being
edited; both trees have the same top-level layout):

- `configs/busybox.config` â€” tracked canonical busybox config.
  Enabled applets: `cat`, `cp`, `cut`, `echo`, `head`, `ls`,
  `mkdir`, `pwd`, `rm`, `rmdir`, `sort`, `stat`, `tail`, `tee`,
  `test`, `touch`, `tr`, `uname`, `wc`, `cmp`, `grep`, `sed`,
  `vi`, plus `ash`.  Off (with reasons): `uniq` (hangs), `od`
  (needs `readv`), `diff` (deliberate), `mv`/`ln`/`chmod`/`mount`
  (need kernel work).
- `userland/musl/` â€” tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  â€” gitignored; rebuild with `./toolchain/install_musl.sh`.
- `toolchain/{install_musl.sh,musl-gcc.sh}` â€” tracked.

Kernel sources:

- Real project kernel: `/home/noneya/code/donix/04_kernel_64bit/`.
- Experimental kernel: `/home/noneya/code/testme/04_kernel_64bit/`.

Scratch copies from earlier exploring:

- `/home/noneya/code/x/` â€” an older scratch copy.  Fully ported;
  safe to delete whenever.
- `/home/noneya/code/y/` â€” a second scratch copy used for a
  bisection experiment.  Safe to delete.

**`testme` still has unported changes** from the session-33 work:
`configs/busybox.config` (the extra applets), and the deliberate
non-port of `FEATURE_ALLOW_EXEC` and `env`.  See "Not carried
forward" above.

Docs live under `docs/` in each tree; see below.

---

## Where things live

Not needed to start a session; ask for a file when the current task
needs it.  Paths below are relative to a tree root
(`/home/noneya/code/donix/` or `/home/noneya/code/testme/`).

- `docs/strategy.md` â€” Phase A/B plan, rules, files-not-to-touch,
  tagging convention, git hygiene, recovery.
- `docs/gotchas.md` â€” every bug writeup, by subsystem.
- `docs/session-log.md` â€” commit tables and per-test canary notes.
  Rows are named by tag; scratch tags are dropped before a
  milestone push, so a row's tag may no longer resolve â€” the
  commit message is the record.
- `docs/open-issues.md` â€” full open-issues list.
- `docs/migration-history.md`, `docs/dons-os-history.md` â€”
  historical narrative (A1-A6, pre-fork).
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` â€” the first two
  frozen at `v0.6.0`; `LLD_BUG_REPORT.md` current.

---

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  Session 33: shell scripts run, shell
redirection works, and the busybox file/text utilities (head,
tail, cp, grep, sed, cut, sort, stat, tee, test, tr, cmp) work,
on top of two new syscalls (uname 63, lseek 8).  Thirteen
commits, all scratch-tagged, unpushed.  Session 34: do `mv` via
`rename(2)` -- the deferred "bigger payoff" item -- or tag
`v0.6.5`.  One change at a time.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
