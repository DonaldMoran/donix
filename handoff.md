Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-09-29 (session 33 â†’ pre-session 34)
**Current HEAD:** tag `20260929-busybox-sed`, branch `dev`
**Last milestone:** `v0.6.4` (published) â€” **the basics are done**
**Next milestone:** none yet â€” `v0.6.5` candidate below

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

## Where we are â€” session 33, scripts run and file utilities work

**Ten commits on `dev`, all scratch-tagged, all unpushed.**  The
last is `8a88792` (`busybox: enable sed applet`).

Session 33 opened with "make shell scripts run" and closed with a
useful busybox command set.  Two threads:

### Thread 1 -- script execution (the original goal)

Three kernel fixes made `./script.sh` and `sh script.sh` work:

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

### Thread 2 -- busybox command set

Prompted by "carry forward the experimental changes worth
keeping," we added two kernel syscalls and enabled five applets,
**each probed first** for `Unknown syscall:` noise:

| Tag | What | Kernel work |
|---|---|---|
| `20260929-uname` | `uname(2)` 63 + busybox applet | new syscall |
| `20260929-lseek` | `lseek(2)` 8 | new syscall |
| `20260929-busybox-head` | `head` applet | needs `lseek` |
| `20260929-busybox-tail` | `tail` applet | needs `lseek(SEEK_END)` |
| `20260929-busybox-cp` | `cp` applet | none |
| `20260929-busybox-grep` | `grep` applet | none |
| `20260929-busybox-sed` | `sed` applet | none |

**`uname` is not needed for scripting.**  The experimental tree's
comment attributing `sh: 3: Invalid argument` to a missing `uname`
was wrong -- that string came from fd exhaustion.  `uname` is a
correctness port, done for `busybox uname`.

**`lseek` was surfaced by `head -n N`,** which seeks to `SEEK_END`
to size the file.  Without it: `Unknown syscall: 8` after correct
output.  Backed by `f_lseek`/`f_tell`; result clamped to
`[0, file_size]`.

**`cp`, `grep`, `sed` needed no kernel work.**  I had predicted
`cp` would need `chmod`/`umask`; it did not, because this build
has `CONFIG_CHMOD` off and `FEATURE_CP_LONG_OPTIONS` off.  See
the "applet syscall surface depends on compiled features" entry
in `docs/gotchas.md`.

### The docs commit

`20260929-docs-session-33` brought `gotchas.md`, `session-log.md`,
and `handoff.md` up to date for the script-execution work, before
the applets started.  This session-end doc pass extends that.

### Verification

Focused canary green (full table in `docs/session-log.md`).
No `Unknown syscall:` lines.  One-off verifications cover all
five applets plus `uname` and the three script invocations.

### Not carried forward from `testme`

- The extra `busybox.config` applets that need kernel work:
  `chmod` (90), `ln`, `mv` (needs `rename` 82), `mount`/`umount`.
- `FEATURE_ALLOW_EXEC` -- proven unnecessary (the kernel-level
  `ENOEXEC` fix covers the fallback).
- `env` applet -- `envp` is ignored by `sys_execve`, so it would
  only work cosmetically.

`testme` still has these unported changes.  They are deliberately
left out, not forgotten.

### Known limitation surfaced this session

`musl_sh` (`donix>`) does not strip shell quotes.  `busybox sed
-n '1p' file` fails from there with `sed: unsupported command '`.
The same command works from `busybox ash`, or without the quotes
(`sed -n 1p file`).  See `docs/gotchas.md` and
`docs/open-issues.md`.

---

## NEXT SESSION â€” pick a direction

Session 33 grew large (10 commits).  No committed next milestone.
The candidate list (`ROADMAP.md`, "After v0.6.4"):

**Wrap and tag `v0.6.5`.**  Session 33 is a coherent milestone:
"shell scripts run; busybox file utilities work."  If tagged, drop
all scratch tags first (`20260929-*`, see `git tag`), tag
`v0.6.5`, rewrite this file, push.

**More applets.**  `wc`, `echo`, `test`, `sort`, `uniq`, `cut`,
`tr` are pure read/write and would likely need no kernel work.
Each is its own probe/commit, same as session 33.  `mv` needs
`rename(2)` (82); `chmod` needs `chmod(2)` (90); both are small
but not free.

**Small, close gaps:**

1. **`newfstatat` (262)** â€” reserved number, no dispatch case.
2. **`sys_open` `O_DIRECTORY` fix** â€” return `-ENOTDIR` when the
   target is a file.
3. **Ctrl-`[` as ESC** â€” `scancode_to_ascii` has no Ctrl parameter
   yet.
4. **`sys_utimensat` cwd resolution** â€” found in session 32; it
   calls `strip_dot_prefix` but not `resolve_against_cwd`.
5. **`musl_sh` quote stripping** â€” the limitation surfaced this
   session.  A tokenizer fix, userland-only.

**Larger:** pipes and redirection (`pipe(2)`), environment
variables (`envp`), the VFS layer (`sys_execve`'s three-attempt
block and `resolve_against_cwd` are both shims), kernel hardening
(real COW, munmap, page-table teardown).

Pick **one**, do it, test it, tag it.  One change at a time.

---

## Canary state

**The focused canary is green as of `20260929-busybox-sed`.**
Full table in `docs/session-log.md`.  Script execution and the
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

The **full canary** (milestone-only) adds: `echo`, `cat`,
`musl_stat`, `musl_min`, `musl_malloc`, `musl_printf`,
`musl_exec`, `musl_readdir`, `musl_r10probe`, `brk_verify`,
`brkraw`, `brkgrow`, `musl_dup2`, `musl_dupfd`, `musl_ids`,
`musl_getcwd`, `busybox echo`, `busybox wc hello-world.txt`.

**Canary rows must not mutate the disk.**  The new applets
(`head`, `tail`, `grep`, `sed`) are read-only and could be added
as canary rows.  `cp` mutates, so it stays a one-off.  If a row
is added, keep it non-mutating: `busybox head -n 1
hello-world.txt`, etc.

**Expected noise:** none.  The serial log has no `Unknown syscall:`
lines.  Two informational lines are expected:
`EXIT: pid=N state=1 parent=2 qhead=N` (a forked busybox shell's
own exit) and `EXIT-FALLBACK: switching to idle, ...` (in
`musl_fork` when the child is the last runnable process).

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **VFS layer (eventual).**  `sys_execve`'s three-attempt path
   resolution and `resolve_against_cwd` are both shims for a
   filesystem layer donix does not have.  When a VFS lands, delete
   them; do not extend.
2. `newfstatat` (262) â€” reserved number, no dispatch case.
3. `sys_open` accepts non-directories with `O_DIRECTORY`.  Return
   `-ENOTDIR` when the target is a file.
4. `sys_utimensat` lacks `resolve_against_cwd` (found session 32).
5. `musl_sh` does not strip shell quotes (surfaced session 33).

Also open: `sys_munmap` is a stub returning 0; `sys_brk`'s fixed
`heap_base` and the 4 MB mmap window are latent collisions; real
FatFs timestamp storage (the three timestamp syscalls return 0
without storing); `prctl` is minimal (`PR_SET_NAME` accepted and
dropped); busybox applet symlinks not installed; syscall-table
audit script; `musl_wait`'s WNOHANG loop spins; `sys_mmap`
rejects all non-anonymous mappings (a file-backed `mmap` caller
will get `-ENOMEM` and must fall back to `read`).

---

## State on disk

Paths, with the correct roots.

- **Real project root:** `/home/noneya/code/donix/`.
- **Experimental tree root:** `/home/noneya/code/testme/`.

Config and source locations (relative to whichever root is being
edited; both trees have the same top-level layout):

- `configs/busybox.config` â€” tracked canonical busybox config.
  Enables `CONFIG_VI`, `CONFIG_TOUCH`, `CONFIG_RM`, `CONFIG_RMDIR`,
  `CONFIG_UNAME`, `CONFIG_HEAD`, `CONFIG_TAIL`, `CONFIG_CP`,
  `CONFIG_GREP`, `CONFIG_SED`.
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
`userland/musl/`.  Session 33: shell scripts run, and the busybox
file-utility set (head, tail, cp, grep, sed) works, on top of two
new syscalls (uname 63, lseek 8).  Ten commits, all scratch-tagged,
unpushed.  Session 34: either tag `v0.6.5`, or pick one item from
ROADMAP.md's "After v0.6.4" list.  One change at a time.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
