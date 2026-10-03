Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-03 (session 47: `/proc` per-pid files and
the pid directory shipped; **`ps` works**.  Next target not yet
chosen.)

**Repo state — run these; do not write it here.**  A header that
names a commit or a tag count is wrong the moment the same commit
lands, so this file does not carry one:

    git log --oneline -1            # HEAD
    git status -sb                  # branch, dirty?, ahead/behind
    git tag --list 'v*' | tail -1   # last milestone
    git tag --list '2026*'          # live scratch tags

> **Tabled: the boot-time `#PF` at `0x400000`.**  It is
> `open-issues.md` item 7, now marked TABLED.  It is intermittent
> and allocator-state dependent, and not currently observed.  **Do
> not start a session on it** unless it reappears.  The findings —
> both presentations, session 45's virtual-1 fault, and the
> `vmm_clone_page_table` lessons — are in item 7.  The one capture
> on disk is `PFcapture.txt` (presentation 2, gitignored; `ls` it,
> it will not show in `git status`); presentation 1's capture is
> not on disk and its dump lives in item 7.

**The version history, in one line each:** `v0.6.6` pipes; `v0.6.7`
a real shell and a framebuffer console; `v0.6.8` the `*at()` family;
`v0.6.9` envp, the `/usr/bin` layout, the `execve` shim removal;
`v0.6.10` a tail on `v0.6.9` — `realpath`, the `readlink` errno,
and `/dev/null`.  **Post-`v0.6.10`, on `dev`, untagged:** session
44's **pathname dispatch seam** — FAT/DEV/PROC backends,
`/dev/null`, `/proc/self/status`, `/dev/console`,
`/proc/self/fd/N`, `tty`.  Eight commits, all verified, none
pushed.

**Session 47 added `/proc` per-pid support** on top of the seam:
`readdir("/proc")` lists the pids, `/proc/<pid>/stat`, `status`,
and `cmdline` read, `/proc/<pid>/` stats as a directory, and **`ps`
and `pstree` are enabled and work.**  Four commits, scratch-tagged
`20261003-*`.  Not a milestone — a working feature with known
edges.

Commits are named by tag only, never by SHA.  **Working tags
(`YYYYMMDD-*`) are local scratch restore points** — they exist while
a milestone is being developed and are **dropped before the
milestone is pushed**.  Only `v*` tags go to the remote and are
permanent.  The commit record is `docs/session-log.md`; the commit
message carries the narrative.  Once a scratch tag is dropped it
resolves to nothing; do not cite one as if it were a stable
reference.

**Scratch tags are annotated.**  The annotation is the fuller
per-commit summary; at a milestone bump the annotations seed the
final milestone narrative.  This is why each step gets a tag even
when no `v*` tag is imminent.

**Scratch tags are also state.**  Which ones are live is
`git tag --list '2026*'`; this file does not list them.

**A plain commit can exist with no tag.**  Session 45's `run:` fix
is one: it is not a milestone, so it gets no `v*`; it is not a
scratch restore point for a milestone in development, so it gets
no `YYYYMMDD-*` either.  It rides along on `dev` until the next
`v*` bump.  Do not invent a tag for it, and do not be surprised by
a commit with no tag.

**Note on commit messages:** a commit message is a claim, not a
fact.  Two instances to remember.  `95c6337 handoff: rewrite fresh
for the v0.6.9 bump` did not rewrite the handoff body.  And session
45's Fix B (`157627c`, later reverted) claimed to close the
boot-time `#PF`; it did not — the fault reproduces with a different
presentation.  Read the diff, not the subject.

---

## Working style — how this project gets changed

Read this before proposing any command block.  It is how the
project has been run for many sessions, and it is what the next
session should preserve.

**One change at a time.**  A "change" is a commit.  The handoff's
work, the docs' rules, and the session-log's pattern all assume a
single coherent change per commit, verified before the next one
starts.

**The state check comes before the command block.**  Every commit
block starts with `git status` (or `git log`) so the working tree
is known *before* a command runs.  This is a rule because it was
learned: a `git commit` was once written against an assumed state
and landed wrong.  Ask for the status, then give the block.

**A command block is one paste.**  `git add`, `git status`,
`git commit`, and `git tag` belong in **one** block, with the
commit message inline via heredoc (`git commit -F - <<'EOF' … EOF`),
not spread across several turns.  The same for the push/merge
sequence below.  Do not split a single operation across messages;
that is how a commit and its tag get separated.

**Two file-return styles.**

- **Small files:** return the complete file, four-backtick fenced.
- **Large files:** return unified-diff hunks, each located by
  enough surrounding context to be unambiguous, preceded by a bold
  **`WORK BEGINS HERE.`** marker on its own line so it is never
  confused with thinking-out-loud.

  The marker is load-bearing: before it, the assistant is
  discussing; after it, the content is applyable.  Never mix the
  two — do not post file parts while reasoning.

**Ask for source you do not have.**  The assistant does not have
direct file access.  Before patching a file whose current contents
it has not seen in this session, it must **ask for that file**.
Never guess at a file's contents, never patch from memory of an
earlier version, never assume a file is unchanged.  This is how
stale patches and reverted work have been avoided.

**When editing a large file, quote the bytes.**  Session 44 added
this the hard way.  For an edit inside a big file, the instruction
must **quote the exact text being replaced and the exact text that
replaces it**, both copied from output the other side just pasted.
Do not describe an edit as "the block above" or "after line N" when
N has not been seen.  Four build failures in session 44 came from
edits described from memory rather than quoted from the file — the
fix each time was to read the current bytes and quote them.  A
whole-half or whole-file return is the safe form; a one-line
insertion into a 6500-line file is the form that breaks.

**A redirection binds to the last command in an `&&` chain.**
Session 45 added this.  `a && b && c && d > file` sends only `d`'s
output to `file`; `a`, `b`, and `c` write to the terminal.  The
`run` script's build line used this form, so every capture it
produced was missing the front of the build — including whether the
kernel was rebuilt at all.  Half a session was spent comparing
binaries whose build log had been silently discarded.  When the
whole chain's output matters, wrap it:
`{ a && b && c && d ; } 2>&1 | tee file`.  See `docs/gotchas.md`.

**Do not edit `third_party/`.**  Session 47 added this.  The
vendored sources there are gitignored and rebuilt by the toolchain,
so an edit is invisible to the repo and vanishes on the next build.
When a diagnostic needs to see inside a third-party applet — a
`printf` in busybox, say — the right instrument is a **first-party
test that reproduces the applet's sequence** (e.g. `proc_walk` for
`procps_scan`), or a **trace in our own kernel**, not a patch to
`third_party/`.  Both are committable; the patch is not.

**The push / merge / tag sequence** (the project's own order,
used for every `v*` bump):

    git tag -a vX.Y.Z -F - <<'EOF'
    <annotation>
    EOF

    git push origin dev && \
    git push origin vX.Y.Z && \
    git checkout main && \
    git merge --no-ff dev -m "Merge dev into main for vX.Y.Z

    <merge narrative>" && \
    git push origin main && \
    git checkout dev && \
    git status && \
    git log --oneline -3

Before this: the banner bump is the **last** code commit of the
version; the session's `YYYYMMDD-*` scratch tags are **dropped**
(`git tag -d`); and the session-log and handoff are written.  The
banner commit is named `kmain: bump the shell banner to vX.Y.Z`.

The tag is **annotated** (`-a -F -`) — the annotation is the
milestone narrative, and it is what a future reader sees first.
The merge is `--no-ff`, so `main` keeps a real merge commit for
each version (`Merge dev into main for v0.6.10`, etc.).

**A version is not necessarily a milestone.**  `v0.6.10` is a tail
on `v0.6.9` with no new subsystem.  When that happens, the tag
annotation and the session-log row should *say so*, so a future
reader does not hunt for a milestone narrative that is not there.

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

## Session 45 — reverted, and what it left

Session 45 attempted the item-7 kernel fix three ways and reverted
all three; **no kernel change was committed.**  Its findings are
now in `open-issues.md` item 7 (the tabled item).  The one thing it
kept was the `run` script's build-and-capture fix.

---

## Where we are — the seam, shipped on `dev`

Session 44 built the **pathname dispatch seam**: the first feature
the previous architecture could not express.  Eight commits on
`dev`, all verified, none tagged `v*` and none pushed.

### The seam, in one paragraph

`resolve_at` now returns, alongside the resolved absolute path, a
**backend tag** computed from the path's first component:
`/dev/...` -> `BACKEND_DEV`, `/proc/...` -> `BACKEND_PROC`,
anything else -> `BACKEND_FAT`.  Every path syscall passes the tag.
FatFs serves FAT; a small table (`dev_lookup`, `proc_lookup`) serves
DEV and PROC, or falls through to FAT for an unknown path under
those prefixes.  There is **no VFS**: no inode, no vnode, no mount
table.  See `docs/strategy.md`, "When a feature may force
architecture."

### Session 47 — `/proc` per-pid, and `ps` works

**This is the newest thing on `dev`, and it builds on the seam.**
Four commits, scratch-tagged `20261003-*`:

| Tag | What |
|---|---|
| `20261003-proc-dir` | `/proc` and `/proc/self` are directories; `readdir` returns `self` |
| `20261003-proc-pids` | `readdir("/proc")` lists the live pids |
| `20261003-proc-stat` | `/proc/<pid>/stat`, with `/proc/self/stat` |
| `20261003-proc-ps` | `/proc/<pid>/status`, `cmdline`, and the pid directory; **`ps` works** |

**The bug that took the session:** `ps` printed a header and no
rows.  `libbb`'s `procps_scan`, under `PSSCAN_UIDGID`, does
`stat("/proc/<pid>/")` — **with a trailing slash** — and `continue`s
the entry when it fails.  donix served the per-pid *files* but not
the per-pid *directory*, so every entry was skipped before any file
was read.  See `gotchas.md`, "A /proc consumer can stat a path it
never opens."

**`ps` and `pstree` are enabled** (`CONFIG_PS`, `CONFIG_PSTREE`;
`PS_WIDE` on, `PS_LONG`/`PS_TIME` off because they need
`gettimeofday`, syscall 99).  `ps` lists the processes; `pstree`
shows only `idle`, because `musl_sh`'s ppid is 0 (the shell is a
second root, not a child of pid 1) — correct for donix, not a bug.

**Known edges, not closed:** `open("/proc/<pid>", O_DIRECTORY)` is
not done (`ps` only stats the directory); a nonexistent pid still
stats as a directory; `PS_LONG`/`PS_TIME` need syscall 99; and
`proc_walk`/`proc_walk_fds` are diagnostics that report and pass,
not assertion tests.

### Session 44, in full — eight commits

| Commit | What |
|---|---|
| `seam: resolve_at returns a backend tag; FAT is the only backend` | `resolve_at` gains `int* backend_out`; `path_backend()` computes the tag from the first component. |
| `seam: /dev/null through the DEV backend; delete path_is_devnull` | `g_dev_table[]` + `dev_lookup()`; the exact-path predicate deleted. |
| `proc: /proc/self/status through the PROC backend` | `g_proc_table[]` + `proc_lookup()`; `FILE_KIND_PROC`; `proc_build_status()`. |
| `dev: /dev/console; proc: /proc/self/fd/N readlinks; tty flips` | DEV table gains `console`; `proc_readlink()`; `tty` prints `/dev/console`. |
| `canary: a tty row` | `busybox tty` must print `/dev/console`; read-only. |
| `execve: resolve the path through the seam; delete the dead retry` | `sys_execve` calls `resolve_at`; the unreachable `"0:"` retry deleted. |
| `docs: session 44 — the seam, its tests, and the state` | Session log and doc updates for the above. |

(Two more commits are in the session-44 span; count them from
`git log --oneline 20261002-handoff-v0610..20261002-docs-seam` when
writing the session log.)

### The seam's three consumers

- **`/dev/null`** — `open`, `stat`, `access`.  `ls -l /dev/null`
  shows `crw-rw-rw-`; `read` returns 0, `write` returns the count.
- **`/proc/self/status`** — opens, reads five real per-process
  fields, stats as `S_IFREG | 0444`, closes.
- **`/dev/console` + `/proc/self/fd/N`** — `readlink`
  (`/proc/self/fd/0`) returns `/dev/console`; `stat("/dev/console")`
  reports `S_IFCHR` with `(st_dev, st_ino) = (1, 1)`, **the same
  pair `fstat(0)` reports**.  That match is `ttyname_r(3)`'s gate
  3b, and it is what makes **`tty` print `/dev/console`**.

### Verification (session 44)

| Test | Result |
|---|---|
| canary (read-only) | **15 passed, 0 failed** |
| canary `--full` | **28 passed, 0 failed** |
| readlink_errno | **3/3** |
| at_step1 | **10/10** |
| at_step2 | **8/8** |
| proc_status | **ALL PASS** |
| proc_fd | **ALL PASS** |
| pipe_step1/2/3/3b | **all OK** |

Boot clean, no `Unknown syscall:` lines, no faults.

---

## NEXT SESSION — no single target chosen

`ps` works; `/proc` is done for the consumer that needed it.  The
next step is a **choice** from the list below, not a blocked
dependency.  Pick one, read its consumer's source before writing
(see the rule in the busybox section), and make the smallest change
that does it.

### Candidates, and what each actually needs

- **`open("/proc/<pid>", O_DIRECTORY)`** — the "complete" half of
  session 47's fix.  `stat` accepts `/proc/<pid>` and
  `/proc/<pid>/`; `open` does not.  Small: the same two paths in
  `open_resolved`, producing a `FILE_KIND_DIR` slot with
  `PROC_DIR_SENTINEL`, which already exists.  Makes `ls /proc/1`
  and `opendir("/proc/1")` work.
- **`musl_sh` as a child of `idle`** — so `pstree` shows a
  Unix-like tree instead of one node.  A `kmain`/`process_create`
  change: set the shell's `parent_pid` to 1.  Small, and it
  changes `ps`'s `PPid` column for the shell.
- **`/etc/passwd`** — makes `ps`'s USER column show names instead
  of `0`.  A synthesized file through the seam, or a real one on
  FAT; either way small, and it removes the
  `sys_open: f_open FAIL path=etc/passwd` line.
- **`sys_gettimeofday` (99)** — enables `ps -l` / `ps -e`
  (`PS_LONG`, `PS_TIME`).  A small syscall from `g_ticks`, like
  `clock_gettime`.
- **`kill` / `pidof`** — `pidof` needs the per-pid entries (now
  present) and a lookup; `kill` additionally needs signal
  delivery, which is item 5's subsystem.  Read
  `procps/kill.c` before deciding.
- **`top`** — needs `ps -l`-class fields and a redraw loop; larger.
- **The `proc_walk` assertion test** — turn the diagnostic into a
  test that asserts (stat `/proc/<n>/` must succeed, the
  consumer's `sscanf` must give ≥ 11).  Small, and it pins the
  session-47 fix.
- **Symlinks** (`open-issues.md` item 8) — larger; design is
  recorded, the seam exists to hide the encoding.
- **`/dev/tty`** — unblocks the pagers (`less`/`more`).

### Do not

Do not tag `v*` in this session unless the change is verified and
you choose to.  Do not reopen item 7.  One change at a time.  Do
not edit `third_party/`.

---

## Busybox enablement — state of play

**`ps` and `pstree` are enabled and work.**  That is the change
from session 47, and it is worth stating plainly because the
sessions before it said they could not be enabled.

### The rule

**Enable an applet only when the syscalls it actually calls are
implemented — read the applet's source, do not guess from its
name.**  When something is missing, the question is "how big is
it?" — a table entry and a directory shape is a session's work; a
signal-delivery subsystem is not.  A syscall that exists but
cannot do its job (a `chmod` on a filesystem with no permissions)
is worse than a missing one: it makes the applet lie.

**A `/proc` consumer can stat a path it never opens.**  Session
47's lesson: `procps_scan` stats `/proc/<pid>/` (trailing slash)
under `PSSCAN_UIDGID` and skips the entry when it fails, before
reading any file.  Every per-pid *file* can exist and the applet
still prints nothing.  See `gotchas.md`, "A /proc consumer can
stat a path it never opens."

**Do not edit `third_party/`.**  When a diagnostic needs to see
inside an applet, write a first-party test that reproduces its
sequence, or trace our own kernel.  Both are committable.

### What each still-off applet needs

Each row is a **cost estimate, not a prohibition**.  Sometimes the
missing piece is small and we write it — that is how `tty` and
`ps` were enabled.  Sometimes it is a whole subsystem and we do
not write it just to turn on one applet.  The rule is **know what
you are signing up for** before you enable.

| Config | Applet | Needs |
|---|---|---|
| `CONFIG_DIFF` | `diff` | `mmap` of files (non-anonymous `mmap`); deliberate |
| `CONFIG_CHMOD` | `chmod` | `chmod`/`fchmodat`; FAT has no permissions |
| `CONFIG_CHOWN` | `chown` | `chown`/`fchownat`; FAT has no ownership |
| `CONFIG_LN` | `ln` | `link`/`symlink`; FAT has no links |
| `CONFIG_LINK` | `link` | same |
| `CONFIG_MOUNT`/`UMOUNT` | `mount`/`umount` | `mount` (165); no VFS |
| `CONFIG_HALT`/`POWEROFF`/`REBOOT` | `halt`/`poweroff`/`reboot` | signal delivery (item 5); no init, no ACPI |
| `CONFIG_TAR`/`UNZIP`/`CPIO`/`GZIP`/`BZIP2`/`XZ` | archives | `mkdirat`, `symlinkat`, `utimensat` storage, file-backed `mmap`, decompression |
| `CONFIG_AWK` | `awk` | large; needs `FEATURE_AWK_LIBM` |
| `CONFIG_LESS`/`MORE` | pagers | raw-mode terminal control; `/dev/tty` does not exist |
| `CONFIG_TOP` | `top` | `ps -l`-class fields (`gettimeofday`), a redraw loop |
| `CONFIG_KILL` | `kill` | signal delivery; `procps/kill.c` |
| `CONFIG_PIDOF` | `pidof` | per-pid entries (now present) and a lookup |
| `CONFIG_NETWORKING` (all) | `ping`, `wget`, etc. | no network stack |
| `CONFIG_ASH_JOB_CONTROL` | ash job control | signal delivery |
| `CONFIG_FEATURE_TAB_COMPLETION` | ash completion | needs `stat` on many paths; probably works, test it |

**Enabled and working, for the record:** `ps`, `pstree`, `tty`
(prints `/dev/console`), plus the applets listed under "State on
disk."

---

## Canary state

**Green as of session 44** — `canary` and `canary --full` from both
shells, **15/15** and **28/28**.  The kernel self-test runs at boot
and reports 17/17.  Session 47 did not add a canary row; `ps` is
exercised by hand, and `proc_walk` is the detailed check.

**The canary is a program: `canary`.**  `tests/canary.c` runs every
non-interactive canary row, checks exit status and output
substrings, and reports pass/fail.  Run from `donix>` or ash; both
find `/usr/bin/CANARY`.

    canary          # read-only rows
    canary --full   # also the mutating rows (create/remove under /)

**What stays manual**, printed at the end of a `canary` run:

    busybox ash      # interactive; then pwd, cd /bin, ls, exit
    vi test          # fills screen; :wq; ./test

**Regression tests (`userland/musl/tests/`, not canary rows):**

    at_step1       # resolve_at via dirfd; fstatat flags;
                   # AT_EMPTY_PATH; faccessat dirfd; utimensat dirfd
                   # (read-only)
    at_step2       # unlinkat + the unlink/rmdir type check
                   # (mutates the disk)
    envp_step1     # envp survives execve, 3 checks (forks)
    musl_exec2     # the bare-name shim is gone, 3 checks (forks)
    fcntl_lowfd    # fcntl on fd 0/1/2, 3 checks (redirects fd 0)
    readlink_errno # readlink(2) errno split: ENOENT for a missing
                   # path, EINVAL for an existing non-symlink, 3
                   # checks (read-only)
    proc_status    # /proc/self/status: open, read the five keys,
                   # stat (S_IFREG), close; 9 checks
    proc_fd        # /proc/self/fd/N -> /dev/console; the
                   # (st_dev, st_ino) match with fstat(0); 7 checks
    proc_dir       # /proc is a directory; readdir returns self and
                   # numeric pids; 8 checks
    proc_stat      # /proc/<pid>/stat opens, parses; 7 checks
    proc_walk      # walks /proc the way procps_scan does and
                   # REPORTS (does not assert); the detailed check
    proc_walk_fds  # the same walk with low fds occupied

**Pipe regression suite (`userland/musl/tests/`):**

    pipe_step1    # create, round-trip, close, re-close EBADF
    pipe_step2    # blocking + directed wake
    pipe_step3    # EOF, EPIPE, dup-aware counts
    pipe_step3b   # exit-path wake

Run these when changing `sys_read`/`sys_write`/`sys_close`/
`put_file_slot`/`sys_fork`/`sys_pipe` or adding a `FILE_KIND_*`.
**Run them, do not just read the rule.**

**New tests must be added to both `USERLAND_ELFS` and the
`mcopy_one` chain in `05_boot_kernel64/Makefile`.**  The image now
stages **45 files**.

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`; use
`busybox sh` or `/bin/busybox sh`.

**Expected noise:** no `Unknown syscall:` lines; no `[fd]` lines
(trace off); no `[a|b|c]` debug line (removed); no `[faccessat]`
trace line (removed).  The `FB: mapped N pages ...` line is
expected.  The `sys_open: f_open FAIL path=...` lines from `vi` on
a new file, from `busybox stat` on nonexistent paths, and from `ps`
looking for `/etc/passwd` are expected diagnostics.  `sys_execve:
f_open FAIL` lines no longer appear, and neither do the `f_open
FAIL path=dev/null` lines from `2>/dev/null`.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **`open-issues.md` item 7: the silent `vmm_map_page*` returns,
   and the boot-time `#PF` they cause.**  **TABLED; not currently
   observed.**  Do not start a session on it unless it reappears.
2. **Redirection of a builtin is silently ignored.**
3. **A builtin in a pipeline is refused.**
4. **`rename(2)` does not replace; `chmod`/`ln`/`mount` need their
   own syscalls.**  Deliberate FatFs-limitation first cuts.
5. **`-EPIPE` is delivered without `SIGPIPE`.**  Fixing it means
   signal delivery — the same subsystem `reboot`/`halt`/`poweroff`
   need.

Also open: `unlinkat` has no consumer; `sys_brk`'s fixed
`heap_base` and the 4 MB mmap window; real FatFs timestamp
storage; `prctl` is minimal; busybox applet symlinks not
installed; syscall-table audit script; `musl_wait`'s WNOHANG loop
spins; `sys_mmap` rejects all non-anonymous mappings; pipes
support one concurrent reader and one concurrent writer;
`put_file_slot`'s pipe wake is coupled to `sys_close`'s wake;
**`readdir("/dev")` fails** (a directory shape, like `/proc` now
has); **`st_rdev` is 0 on device nodes**;
**`f_stat_with_retry` has dead `has_drive`**;
**`open("/proc/<pid>", O_DIRECTORY)` is not done** (the "complete"
half of session 47); **a nonexistent pid stats as a directory**;
**`sys_gettimeofday` (99) is not implemented**, so `ps -l` is off.

**Session 45 added:** the user-mode `#PF` at virtual 1
(`CR2 = RIP = 0x1`, `pte = 0x3`, phys 1, `pmm=2` on every page of
the walk) — observed with the signature change, not since,
**unresolved**.  Recorded in `open-issues.md` item 7.

---

## State on disk

**Real project root:** `/home/noneya/code/donix/`.

**The FAT layout:**

    /            HELLO-WORLD.TXT and other data
    /bin         busybox
    /usr/bin     the donix-native ELFs (apps + tests), staged BARE
    /tmp         empty

- `configs/busybox.config` — tracked canonical busybox config.
  Enabled applets: `cat`, `cp`, `cut`, `echo`, `env`, `false`,
  `find`, `head`, `ls`, `mkdir`, `mv`, `od`, `printenv`, `pwd`,
  `rm`, `rmdir`, `seq`, `sort`, `stat`, `tail`, `tee`, `test`,
  `touch`, `tr`, `true`, `uname`, `uniq`, `wc`, `yes`, `cmp`,
  `grep`, `sed`, `vi`, `clear`, `basename`, `dirname`, `unlink`,
  `ttysize`, `tty`, `arch`, `mktemp`, `sleep`, `usleep`,
  `truncate`, `realpath`, `stty`, `ps`, `pstree`, plus `ash`.
- `userland/musl/` — tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.  New tests must be added to both
  `USERLAND_ELFS` and the `mcopy_one` chain in
  `05_boot_kernel64/Makefile`.
- `04_kernel_64bit/fonts/ter-u18n.psf` — tracked font source.
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  — gitignored; rebuild with `./toolchain/install_musl.sh`.
  **Do not edit these.**
- `toolchain/{install_musl.sh,musl-gcc.sh}` — tracked.
- `PFcapture.txt` — the vmm crash capture (presentation 2).
  Gitignored; **still on disk — `ls` it, `git status` will not
  show it.**  Presentation 1's capture is not on disk; its dump is
  in `docs/open-issues.md` item 7.
- `run` — tracked; the build-and-capture fix lives here.

Kernel sources: `04_kernel_64bit/`.

---

## Where things live

Not needed to start a session; ask for a file when the current task
needs it.  Paths relative to the tree root
(`/home/noneya/code/donix/`).

- `docs/strategy.md` — Phase A/B plan, rules, files-not-to-touch,
  tagging convention, git hygiene, recovery.
- `docs/gotchas.md` — every bug writeup, by subsystem.
  **Session 47 added one entry**: "A /proc consumer can stat a path
  it never opens" (the `stat("/proc/<pid>/")` skip that made `ps`
  print nothing).  Session 46 added "A redirection binds to the
  last command in an `&&` chain."  Session 45's `pmm_get_page_type`
  result (the virtual-1 fault's walk was all `PAGE_TABLE`, ruling
  out use-after-free) is a finding about one fault, not a general
  lesson, and lives in `open-issues.md` item 7 rather than here.
- `docs/session-log.md` — commit tables and per-test canary notes.
  Session 47's section is at the top; session 46's is below it.
  Session 44's section is recorded but **misplaced** (it sits after
  session 34), noted at the top of the file and deferred.  There is
  no session-45 section: session 45's one kept commit is the
  tooling fix, named in the session-46 section.
- `docs/open-issues.md` — full open-issues list.  Item 7 is
  TABLED (the boot-time `#PF`); item 8 is symlinks.
- `docs/migration-history.md`, `docs/dons-os-history.md` —
  historical narrative.
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` — the first two
  frozen at `v0.6.0`; `LLD_BUG_REPORT.md` current.
- `ROADMAP.md` — future work only.  Its "Make `/proc` possible"
  section is now largely done.
- `README.md` — needs a review at the `v*` bump.
- `run` — tracked; the build-and-capture fix lives here.

---

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  `v0.6.7` shipped the shell and the framebuffer;
`v0.6.8` the `*at()` family; `v0.6.9` envp, the `/usr/bin` layout,
the `execve` shim removal; `v0.6.10` `realpath`, the `readlink`
errno, and `/dev/null`.  On top of `v0.6.10`, on `dev` and
untagged, session 44 built the pathname dispatch seam, and session
47 finished `/proc`: per-pid `stat`, `status`, and `cmdline`, the
pid directory, and **`ps` and `pstree` work.**  Session 45
attempted the item-7 fix and reverted all kernel changes; it
committed one tooling fix (the `run` script's build line).  **The
boot-time `#PF` is TABLED, not fixed** — see `open-issues.md` item
7.  **Next: no single target chosen** — see "NEXT SESSION."  One
change at a time.**

---

## How to use this file

At session start, paste this file and say "Continue from here and
request any files you need."  Run the four `git` commands in the
repo-state block — they are the state; this file is the narrative.

At session end, **rewrite the narrative**: what changed, what is
next, what is tabled.  Do **not** write repo state — no HEAD, no
ahead/behind count, no tag list, no dirty/clean claim.  State is
what git is for; a hand-written state line is wrong the moment the
commit that writes it lands.  New gotchas go to `docs/gotchas.md`;
new commit rows go to `docs/session-log.md`; new open issues go to
`docs/open-issues.md`.  This file never grows.  Name commits by tag
only, never by SHA.

**When a session's findings change an earlier numbered step, edit
the step in place — do not just add a paragraph above it.**

**Before proposing any command block, read the "Working style"
section at the top.**

**Four gotchas worth reading before the next change.**  "A fix with
no test is indistinguishable from an unfixed defect."  "A test can
encode an earlier version's behavior."  "A consumer inferred from
behavior is not a consumer."  And session 45's own: a fix can fail
to close the thing it claims to close, and an intermittent fault
that stops reproducing is not fixed — it is unobserved.
