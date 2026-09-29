Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-09-29 (session 34 → pre-session 35)
**Current HEAD:** tag `20260930-busybox-uniq`, branch `dev`
**Last milestone:** `v0.6.4` (published) — **the basics are done**
**Next milestone:** `v0.6.5` candidate — sessions 33 and 34
together are large enough to tag (see below)

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

## Where we are — session 34, low fds first-class, `uniq` works

**Twenty-one commits on `dev`, all scratch-tagged, all unpushed.**
The last is `9307141` (`busybox: enable uniq applet`).

Session 34 opened with "investigate `uniq`" and closed with `uniq`
working and a kernel fix that turned out to be the actual bug.  Two
threads:

### Thread 1 — low fds (0/1/2) are first-class

| Tag | What |
|---|---|
| `20260930-low-fd-io` | console sentinels; lowest-free-fd `open`; `dup2`/`fcntl` accept 0/1/2 |

`uniq FILE` hangs because `alloc_file_slot` started at fd 3.  busybox
`uniq` does `close(0); open(file)` and expects `open` to return fd 0;
on donix it returned fd 3, so `stdin` stayed pointed at the closed
fd 0 and `read(0, ...)` fell through to the keyboard path in
`sys_read` and blocked forever.  Three interlocking changes:

1. **Console sentinels.**  `process_create` installs a
   `FILE_KIND_CONSOLE` slot in fds 0/1/2 so a fresh process's stdio
   fds are occupied and `open(2)` returns fd 3, matching Linux.
   `sys_read`/`sys_write` treat a console slot as keyboard/screen;
   `put_file_slot` frees it without `f_close`.

2. **Lowest-free-fd `open`.**  `alloc_file_slot` starts at fd 0.
   With sentinels in place this returns 3 for a fresh process and 0
   only after `close(0)` — the `uniq` idiom.

3. **`dup2`/`fcntl` accept low fds.**  `sys_dup2` and the `F_DUPFD` /
   `F_DUPFD_CLOEXEC` cases of `sys_fcntl` use `get_file_slot_any`
   (accepts 0/1/2).  Without this, ash's redirect save/restore failed
   with `EBADF` once an earlier redirect had freed a low fd, so the
   *second* redirect in a shell broke.  The other `fcntl` subcommands
   still refuse `fd < 3`.

Full writeup with the trace: `docs/gotchas.md`, "Low fds (0/1/2) are
first-class."

### Thread 2 — busybox

| Tag | What |
|---|---|
| `20260930-busybox-uniq` | `CONFIG_UNIQ=y` |

The applet's hang was the kernel fd bug, not an applet bug.  With low
fds first-class it works in both the FILE-argument and
redirected-stdin forms.

### Docs commit

| Tag | What |
|---|---|
| `20260930-docs-session-34` | this pass: gotchas, open-issues, session-log, handoff |

### Known failures, deliberately off

- **`diff`** — deliberately off; larger surface area.
- **`chmod`, `ln`, `mount`/`umount`** — need `chmod(2)` (90),
  `link(2)`/`symlink(2)` (86/88), and `mount(2)` respectively.
- **`uniq`** — **no longer on this list.**  It was never an applet
  bug; it was the kernel fd bug fixed in `20260930-low-fd-io`.  The
  applet is enabled and tested.

### Known limitation: `musl_sh`

`donix>` (musl_sh) does not strip shell quotes and does not parse
redirection.  All tests involving `<`, `>`, `|`, `&&`, `;`, or
quoting must be run from ash (`busybox sh` from `donix>`, or the
auto-launched ash at boot).  See `docs/gotchas.md`.

Note this includes `uniq -c < file`: from `donix>` the `<` reaches
`uniq` as a literal argument, which is why the `donix>` run in the
canary shows `uniq: can't open '<'`.  That is the `musl_sh` gap, not
an `uniq` bug.

### Known limitation: no `pipe(2)`

`|` does not work in any shell — not ash, not `donix>`.  The shell
never creates the pipe; `|` reaches the applet as a literal argv
argument.  Not an applet bug; `pipe(2)` is simply absent.  See
`docs/open-issues.md`.

### Not carried forward from `testme`

The extra `busybox.config` applets that need kernel work, plus
`FEATURE_ALLOW_EXEC` (proven unnecessary) and `env` (cosmetic —
`envp` is ignored by `sys_execve`).  `testme` still has these; they
are deliberately left out.

---

## NEXT SESSION — pick a direction

Session 34 was two commits.  No committed next milestone.

**Recommended first: `pipe(2)`.**  The biggest remaining gap in the
shell.  Without it, `|` reaches applets as a literal argument.
`pipe(2)` is syscall 22; the implementation needs a pipe object,
per-fd read/write ends, and scheduler integration for blocking on an
empty/full pipe.  Bigger than anything since the redirection fix.
Worth its own session.

**Small, close gaps:**

1. **`newfstatat` (262)** — reserved number, no dispatch case.
2. **`sys_open` `O_DIRECTORY` fix** — return `-ENOTDIR` when the
   target is a file.
3. **Ctrl-`[` as ESC** — `scancode_to_ascii` has no Ctrl parameter
   yet.
4. **`sys_utimensat` cwd resolution** — found in session 32; it
   calls `strip_dot_prefix` but not `resolve_against_cwd`.
5. **`musl_sh` quote stripping + redirection parsing** — the two
   userland gaps.  A tokenizer fix, userland-only.
6. **`sys_fcntl` fd < 3 for the other subcommands** — a small
   extension of the session-34 work.  `F_GETFL`, `F_SETFL`,
   `F_GETFD`, `F_SETFD` still refuse `fd < 3`; Linux allows them on
   a redirected fd.  Not on any current path.

**More applets, once `pipe(2)` lands:** `awk`, `find` (needs
`newfstatat`), `tar`, `diff`.  `chmod` (90), `ln` (86/88), and
`mount` still need their own syscalls.

**Or tag `v0.6.5`.**  Sessions 33 and 34 together are a coherent
milestone: "scripts run; redirection works; low fds are first-class;
broad busybox command set including `uniq`."  If tagged, drop all
`20260929-*` and `20260930-*` scratch tags first, tag `v0.6.5`,
rewrite this file, push.

Pick **one**, do it, test it, tag it.  One change at a time.

---

## Canary state

**The focused canary is green as of `20260930-busybox-uniq`.**  Full
table in `docs/session-log.md`.  Script execution, redirection, and
the applet one-offs are verification, not canary rows — the canary
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

**New read-only rows (added session 34), run from ash:**

    uniq hello-world.txt
    uniq -c < hello-world.txt

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`; use
`busybox sh` or `/bin/busybox sh`.

**Do NOT add a `donix>` redirection row.**  `musl_sh` does not
parse `<` or `>`.

**Do NOT run `uniq -c < file` from `donix>`.**  It is an ash-only
test; from `donix>` the `<` reaches `uniq` as a literal argument and
`uniq` reports `can't open '<'`.

**Do NOT add a pipe (`|`) row.**  `pipe(2)` is not implemented;
`|` reaches the applet as a literal argument in both shells.

The **full canary** (milestone-only) adds: `echo`, `cat`,
`musl_stat`, `musl_min`, `musl_malloc`, `musl_printf`, `musl_exec`,
`musl_readdir`, `musl_r10probe`, `brk_verify`, `brkraw`,
`brkgrow`, `musl_dup2`, `musl_dupfd`, `musl_ids`, `musl_getcwd`,
`busybox echo`, `busybox wc hello-world.txt`.

**Canary rows must not mutate the disk.**  The new read-only applets
(`head`, `tail`, `grep`, `sed`, `cut`, `sort`, `stat`, `cmp`, `wc`,
`od`, `uniq`) could be added as canary rows.  `tee`, `cp`, `mv`, and
the redirection tests mutate, so they stay one-offs.

**Expected noise:** none.  The serial log has no `Unknown syscall:`
lines and (with the trace off) no `[fd]` lines.  Two informational
lines are expected: `EXIT: pid=N state=1 parent=2 qhead=N` (a forked
busybox shell's own exit) and `EXIT-FALLBACK: switching to idle, ...`
(in `musl_fork` when the child is the last runnable process).  The
`sys_open: f_open FAIL path=etc/...` lines from `busybox stat` are
expected — those files do not exist and `stat` falls back cleanly.
The `sys_rename: f_rename FAIL ...` line from `busybox mv` on a
failed rename is expected — same style of diagnostic `sys_unlink` and
`sys_rmdir` carry, and only prints on failure.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **VFS layer (eventual).**  `sys_execve`'s three-attempt path
   resolution and `resolve_against_cwd` are both shims.  When a VFS
   lands, delete them; do not extend.
2. **`pipe(2)` is absent** — `|` does not work in any shell.
   The biggest remaining gap.
3. **`sys_fcntl` refuses fd < 3** for subcommands other than
   `F_DUPFD`/`F_DUPFD_CLOEXEC`.  Deliberate; a small extension of
   the session-34 work rather than a new problem.
4. **`musl_sh` does not strip quotes or parse redirection.**
   Userland-only fix.
5. **`rename(2)` does not replace; `chmod`/`ln`/`mount` need
   their own syscalls.**  Deliberate FatFs-limitation first cuts.

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

- `configs/busybox.config` — tracked canonical busybox config.
  Enabled applets: `cat`, `cp`, `cut`, `echo`, `head`, `ls`,
  `mkdir`, `mv`, `od`, `pwd`, `rm`, `rmdir`, `sort`, `stat`,
  `tail`, `tee`, `test`, `touch`, `tr`, `uname`, `uniq`, `wc`,
  `cmp`, `grep`, `sed`, `vi`, plus `ash`.  Off (with reasons):
  `diff` (deliberate), `chmod`/`ln`/`mount` (need kernel work).
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
`userland/musl/`.  Session 34: low fds (0/1/2) are first-class --
console sentinels, lowest-free-fd `open`, `dup2`/`fcntl` accept
0/1/2 -- and busybox `uniq` works.  Twenty-one commits, all
scratch-tagged, unpushed.  Session 35: implement `pipe(2)` -- the
biggest remaining gap -- or tag `v0.6.5`.  One change at a time.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
