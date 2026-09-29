Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-09-29 (session 33 â†’ pre-session 34)
**Current HEAD:** tag `20260929-execve-script-fallback`, branch `dev`
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

## Where we are â€” session 33, shell scripts run

**Shell scripts execute.**  Session 33 landed two commits on `dev`,
both scratch-tagged, both unpushed:

| Tag | What |
|---|---|
| `20260929-max-process-files-64` | `MAX_PROCESS_FILES` 8 â†’ 64; `sh script.sh` works |
| `20260929-execve-script-fallback` | execve strips `./` before open; returns `ENOEXEC` for short non-ELF; `./script.sh` works |

Both scratch tags are local and **must be dropped before the next
`v*` push**.

### What landed in session 33

**Commit 1 â€” `MAX_PROCESS_FILES` 8 â†’ 64** (`include/process.h`).
Sizes `void* file_table[MAX_PROCESS_FILES]` in `pcb_t`.  At 8,
busybox ash's script fork+exec path fails with
`sh: 3: Invalid argument`; at 64 the same script prints its
output.  Controlled: same build, same script, only the constant
varied.  Cost +448 B/pcb (56 pointers Ã— 8); ~14 KB across the
32-entry process table if static.  Assembly-safe: `file_table`
sits *after* `block_kind`, and `context_switch.asm` reads offsets
only up to `block_kind` (0x158), which `process.c`'s
`_Static_assert` pins.

**Commit 2 â€” `sys_execve` script fallback** (`user_syscall.c`).
Two coordinated fixes in one function:

1. **Normalize the path** with `strip_dot_prefix` before the first
   `f_open`, into a local `exec_path[USER_PATH_MAX]`.  FatFs
   rejects a leading `./` with `FR_INVALID_NAME`, so `./test.sh`
   failed at the open (`sys_execve: f_open(./test.sh) -> 6`)
   before any retry could run.  The original `path` is kept for
   `proc_name` and the diagnostic.  Every open attempt uses
   `exec_path`.
2. **Return `ENOEXEC`, not `EIO`, for a short non-ELF read.**  A
   shell script is shorter than the 64-byte ELF header; the old
   `got != sizeof(ehdr)` check returned `EIO` before the magic
   test, and ash does not fall back to an interpreter on `EIO`.
   Now: `fr != FR_OK` â†’ `EIO`; `got < 4` or magic mismatch â†’
   `ENOEXEC`; short ELF header â†’ `ENOEXEC`; empty file â†’ `ENOEXEC`;
   file too large â†’ `EIO`.

Both were needed, and each is individually attributable.  The
`exec_path` change makes the **open** succeed; the `ENOEXEC`
change makes the **fallback** run; `MAX_PROCESS_FILES` lets ash
**complete the fork+exec**.

### Verification (session 33)

Focused canary green (full table in `docs/session-log.md`,
"Per-test canary notes (session 33)").  No `Unknown syscall:`
lines.  The three script invocations, all printing the script
output:

```
$ ./test.sh
sys_execve: not an ELF file
sys_execve: pid=5 entry=... argc=2 ... (busybox)
Hello, world
EXIT: pid=6 state=2 parent=5 qhead=5
EXIT: pid=5 state=2 parent=3 qhead=(empty)

$ sh test.sh
sys_execve: pid=7 entry=... argc=2 ... (busybox)
Hello, world
EXIT: pid=8 state=2 parent=7 qhead=7
EXIT: pid=7 state=2 parent=3 qhead=(empty)

donix> busybox sh test.sh
sys_execve: pid=9 entry=... argc=3 ... (busybox)
Hello, world
```

The `sys_open: f_open FAIL path=test.sh ... r=4` line on `vi
test.sh` is the expected vi-opens-nonexistent-file path -- do not
silence it (see `docs/gotchas.md`).

**Not ported from the experimental tree:** `uname(2)` (syscall
63) and the extra `busybox.config` applets (cp, mv, grep, sed,
chmod, ln, head, tail, test, mount, umount, env, ASH_* features).
Neither is needed for script execution.  Both remain candidates
for a later milestone.

### The `sh: N: ...` messages are not diagnostic

Ash's `sh: 3: Invalid argument` and `sh: ./test.sh: not found` are
generic; they do not name the failing syscall.  Two unrelated bugs
produced `Invalid argument` this week (fd exhaustion and a
`uname` `-ENOSYS`).  Read the kernel trace, not the shell message:

```
sys_execve: f_open(<path>) -> <FRESULT>   open stage failed
sys_execve: not an ELF file               open OK, format rejected
sys_execve: pid=... entry=... (name)      exec succeeded
Unknown syscall: N                         missing handler
```

Full writeup in `docs/gotchas.md`.

---

## NEXT SESSION â€” pick a direction

Session 33 closed script execution.  No committed next milestone.
The candidate list (`ROADMAP.md`, "After v0.6.4"):

**Small, close gaps:**

1. **`newfstatat` (262)** â€” reserved number, no dispatch case.
   Small wrapper over `sys_stat` now that cwd resolution exists.
2. **`sys_open` `O_DIRECTORY` fix** â€” return `-ENOTDIR` when the
   target is a file.  Latent today, correct to close.
3. **Ctrl-`[` as ESC** â€” `scancode_to_ascii` has no Ctrl parameter
   yet.  Deferred during the terminal work.
4. **`sys_utimensat` cwd resolution** â€” found in session 32; it
   calls `strip_dot_prefix` but not `resolve_against_cwd`, so it
   resolves against the FAT root in a non-root cwd.

**Broaden busybox coverage** â€” `cp`, `mv`, `grep`, `sed`, `awk`,
`tar`.  The `busybox.config` additions from the experimental tree
enable several of these already; each missing syscall is its own
commit.  Watch for `Unknown syscall: N`.

**Consider tagging `v0.6.5`** â€” session 33 is a coherent
milestone ("shell scripts run").  If tagged, drop both scratch
tags first, rewrite this file, and push.

**Larger:** pipes and redirection (`pipe(2)`), environment
variables (`envp`), the VFS layer (`sys_execve`'s three-attempt
block and `resolve_against_cwd` are both shims), kernel hardening
(real COW, munmap, page-table teardown).

Pick **one**, do it, test it, tag it.  One change at a time.

---

## Canary state

**The focused canary is green as of
`20260929-execve-script-fallback`.**  Full table in
`docs/session-log.md`.  `./test.sh`, `sh test.sh`, and
`fault_pf` are one-off verifications, not canary rows -- the
canary must not mutate the disk, and script execution, vi save,
and `fault_pf` all do.

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

**Canary rows must not mutate the disk.**  A paired
`mkdir`/`rmdir` row is possible but still mutates, so it stays out
of the canary.  Use it as a one-off verification instead.

**Expected noise:** none.  The serial log has no `Unknown syscall:`
lines.  `sys_execve: pid=... (name)` trace lines are informational.
Two other lines are informational and expected:
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
5. Fork is O(~6 MB) per call â€” real COW is the long-term fix.

Also open: `sys_munmap` is a stub returning 0; `sys_brk`'s fixed
`heap_base` and the 4 MB mmap window are latent collisions; real
FatFs timestamp storage (the three timestamp syscalls return 0
without storing); `prctl` is minimal (`PR_SET_NAME` accepted and
dropped); busybox applet symlinks not installed; syscall-table
audit script; `musl_sh` echoes garbage on backspace; `musl_wait`'s
WNOHANG loop spins.

---

## State on disk

Paths, with the correct roots.

- **Real project root:** `/home/noneya/code/donix/`.
- **Experimental tree root:** `/home/noneya/code/testme/`.

Config and source locations (relative to whichever root is being
edited; both trees have the same top-level layout):

- `configs/busybox.config` â€” tracked canonical busybox config.
  Enables `CONFIG_VI`, `CONFIG_TOUCH`, `CONFIG_RM`, `CONFIG_RMDIR`.
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
`include/syscall.h` (+`SYS_UNAME` 63), `user_syscall.c` (`sys_uname`
+ dispatch case), `configs/busybox.config` (extra applets), and
`include/process.h` (the 8â†’64 already ported).  The `uname` and
config changes are deliberately left unported; see above.

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
`userland/musl/`.  Session 33: shell scripts run â€” `sh script`,
`./script`, and `busybox sh script` all work, via a per-process fd
table raised to 64 and two coordinated `sys_execve` fixes (strip
`./` before open; return `ENOEXEC` for short non-ELF).  Session
34: pick one item from ROADMAP.md's "After v0.6.4" list, or tag
`v0.6.5`.  One change at a time.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
