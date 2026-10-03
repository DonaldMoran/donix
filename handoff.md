Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-02 (session 44 closed; the seam shipped
on `dev`, **not tagged `v*`**)
**Current HEAD:** branch `dev` at `8e8a9df` (scratch tag
`20261002-execve-seam`), **not pushed** — `origin/dev` is at
`v0.6.10` (`70c85c6`), six commits behind.  `main` is at the
`v0.6.10` merge commit `cc38646`.  Working tree clean; two
untracked files: `BOOT_PF.TXT` (see the next section) and
`split_for_share.sh` (a helper script, not project material).
**Last milestone:** `v0.6.10` (published).  **No milestone has been
opened or closed for session 44** — the seam work is on `dev` under
scratch tags, awaiting a `v*` bump *and* the vmm fix below.

> ### ⚠ Do not tag a `v*` until the vmm bug is fixed.
>
> One boot of this tree hit a `#PF` at `CR2 = 0x400000` **before
> any user code ran**.  The capture is saved as **`BOOT_PF.TXT`**.
> The symptom is the huge-page split in `vmm_map_page_in_cr3`
> leaving the bootloader's 2 MB **supervisor** page in place where
> a user page was asked for — the defect that `6557f05` (session
> 42, "vmm: huge-page split must not silently fail") was supposed
> to close.  It printed **no `VMM: FATAL`**.
>
> **Two observations make this reproducible, not random:**
>
> 1. **It usually follows a rebuild that adds userland programs.**
>    The last one came after a session-44 rebuild that added
>    `proc_fd.elf` and `proc_status.elf`.
> 2. **After a reboot or two — by the third, say — it goes away
>    and stays away for a long time.**  A fresh boot of the same
>    image is usually clean.
>
> That combination is the signature of a **page-frame allocation
> condition**, not a logic error in the split: the split needs a
> fresh frame, the allocation's outcome depends on how many frames
> were consumed before it ran (more programs, more frames), and a
> reboot reinitializes the allocator's state so the condition
> clears.  It is a **one-time boot state**.
>
> **This is likely why no `VMM: FATAL` printed.**  `6557f05` added
> the halt to *one* return in the split path.  If the failure is a
> *different* silent return in the same path — one of the returns
> `open-issues.md` item 7 lists — the split leaves the supervisor
> page in place without a message.  **The first thing the next
> session reads for: which `if (!phys) return;` the split takes,
> and whether it is the one `6557f05` fixed.**
>
> This is `open-issues.md` item 7, **still open**.  The next session
> is this bug, not feature work.  Source needed: `04_kernel_64bit/vmm.c`,
> the diff of `6557f05`, `BOOT_PF.TXT`, and `docs/open-issues.md`
> item 7.
>
> **A program that adds to the image should expect this** until the
> bug is fixed.  The fix should come before the image grows again.

**The version history, in one line each:** `v0.6.6` pipes; `v0.6.7`
a real shell and a framebuffer console; `v0.6.8` the `*at()` family;
`v0.6.9` envp, the `/usr/bin` layout, the `execve` shim removal;
`v0.6.10` a tail on `v0.6.9` — `realpath`, the `readlink` errno,
and `/dev/null`.  **Post-`v0.6.10`, on `dev`, untagged:** session
44's **pathname dispatch seam** — FAT/DEV/PROC backends,
`/dev/null`, `/proc/self/status`, `/dev/console`,
`/proc/self/fd/N`, `tty`.

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

**Seven scratch tags are live on `dev`** (all local, none pushed):
`20261002-handoff-v0610`, `20261002-seam`,
`20261002-dev-null-backend`, `20261002-proc-status`,
`20261002-dev-console-tty`, `20261002-canary-tty`,
`20261002-execve-seam`.

**Note on commit messages:** a commit message is a claim, not a
fact.  `95c6337 handoff: rewrite fresh for the v0.6.9 bump` did not
rewrite the handoff body.  Read the diff, not the subject.

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

## Where we are — the seam, shipped on `dev`

Session 44 built the **pathname dispatch seam**: the first feature
the previous architecture could not express, and the thing that
forces "path -> first component -> FAT | DEV | PROC" into
existence.  It is six commits on `dev`, all verified, none tagged
`v*` and none pushed.

### The seam, in one paragraph

`resolve_at` now returns, alongside the resolved absolute path, a
**backend tag** computed from the path's first component:
`/dev/...` -> `BACKEND_DEV`, `/proc/...` -> `BACKEND_PROC`,
anything else -> `BACKEND_FAT`.  Every path syscall passes the tag.
FatFs serves FAT; a small table (`dev_lookup`, `proc_lookup`) serves
DEV and PROC, or falls through to FAT for an unknown path under
those prefixes.  There is **no VFS**: no inode, no vnode, no mount
table.  It is "which of three things does this path name," and
nothing more.  See `docs/strategy.md`, "When a feature may force
architecture."

### Session 44, in full — six commits

| Commit | What |
|---|---|
| `seam: resolve_at returns a backend tag; FAT is the only backend` | `resolve_at` gains `int* backend_out`; `path_backend()` computes the tag from the first component; every path syscall adopts the new signature.  Five callers move off `resolve_against_cwd` onto `resolve_at`, leaving it with one caller.  One named behavior change: cwd-overflow errno becomes `-ENAMETOOLONG`, matching the four callers it absorbs. |
| `seam: /dev/null through the DEV backend; delete path_is_devnull` | `g_dev_table[]` + `dev_lookup()`; the exact-path predicate and its three call sites are **deleted**.  `open_resolved`, `stat_resolved`, `access_resolved` take the backend tag.  `fill_kstat_as_chardev` extracted. |
| `proc: /proc/self/status through the PROC backend` | `g_proc_table[]` + `proc_lookup()`; `FILE_KIND_PROC`; `proc_build_status()`; the `FILE_KIND_PROC` cases in `sys_read` / `put_file_slot` / `sys_fstat_body`.  Five real fields (`Name`, `Pid`, `PPid`, `Uid`, `Gid`), no invented ones. |
| `dev: /dev/console; proc: /proc/self/fd/N readlinks; tty flips` | The DEV table gains `console` (`FILE_KIND_DEV_CHAR`, stat-able not openable); `fill_kstat_as_chardev` takes `(st_dev, st_ino)` — `/dev/null` `(1,2)`, `/dev/console` `(1,1)`, console sentinel `(1,1)`; `proc_readlink()` and the `sys_readlink` backend branch.  **`tty` prints `/dev/console`.**  Also fixes a pre-existing bug the new test found: `sys_fstat_body` used `get_file_slot` (fd < 3 refused), so `fstat(0)` on a console sentinel was `-EBADF`. |
| `canary: a tty row` | `busybox tty` must print `/dev/console`; read-only; runs in both canary modes. |
| `execve: resolve the path through the seam; delete the dead retry` | `sys_execve` calls `resolve_at`; a non-FAT backend is `-ENOEXEC`.  The `"0:" + path` retry is **deleted — it was unreachable**, because `strip_dot_prefix` ran before the retry's check for a leading `/`. |

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

### Verification

| Test | Result |
|---|---|
| canary (read-only) | **15 passed, 0 failed** |
| canary `--full` | **28 passed, 0 failed** |
| readlink_errno | **3/3** |
| at_step1 | **10/10** |
| at_step2 | **8/8** |
| proc_status | **ALL PASS** (new) |
| proc_fd | **ALL PASS** (new) |
| pipe_step1/2/3/3b | **all OK** (run for commit 2's `FILE_KIND_*`) |

Boot clean, no `Unknown syscall:` lines, no faults.

### New tests this session

`userland/musl/tests/proc_status.c` — opens, reads (checks the five
keys), stats (`S_IFREG`, nonzero size), closes `/proc/self/status`.
`userland/musl/tests/proc_fd.c` — three `readlink`s return
`/dev/console`; `stat("/dev/console")` is `S_IFCHR` and matches
`fstat(0)` on `(st_dev, st_ino)`; `readlink` on a non-console fd is
`-EINVAL`.  Both added to `USERLAND_ELFS` and the `mcopy_one`
chain; staged as `::/usr/bin/PROC_STATUS` and `::/usr/bin/PROC_FD`.

### Session 43, in one line

`v0.6.10` — `realpath` enabled; the `readlink` errno split closed
by `readlink_errno.c` (item 8 removed); `/dev/null` across
`open`/`stat`/`access`.  Details in `docs/session-log.md`.

---

## NEXT SESSION — the vmm bug

**This is the only thing between the tree and a `v*` bump.**  It is
not feature work; it is a correctness bug with a saved capture and
a reproducer.

### What happened

One boot of the session-44 tree crashed before any user code ran:

    === PAGE FAULT (#PF) ===
      CR2 (Bad Address) : 0x0000000000400000
      Faulting RIP      : 0x0000000000400000
      Raw Error Code    : 0x0000000000000015
      pde               : 0x0000000000400083
      PDE IS 2 MB PAGE, phys base 0x400000 -> effective phys 0x400000

`0x400000` is the ELF image base.  `0x15` is present + read + user
+ instruction-fetch.  The PDE `0x83` is present, write, PS — and
**`PT_USER` clear**.  So the kernel tried to fetch a user program's
first instruction from a **supervisor** mapping, in ring 3.  That
is the huge-page-split defect `6557f05` was meant to close, and it
printed **no `VMM: FATAL`**.

### The correlation — this is the reproducer

1. **It usually follows a rebuild that adds userland programs.**
   The last one came after a session-44 rebuild that added
   `proc_fd.elf` and `proc_status.elf`.
2. **After a reboot or two — by the third, say — it goes away and
   stays away for a long time.**  A fresh boot of the same image is
   usually clean.

That combination is the signature of a **page-frame allocation
condition**, not a logic error in the split: the split needs a
fresh frame, the allocation's outcome depends on how many frames
were consumed before it ran (more programs, more frames), and a
reboot reinitializes the allocator's state so the condition
clears.  It is a **one-time boot state**.

**This is likely why no `VMM: FATAL` printed.**  `6557f05` added
the halt to *one* return in the split path.  If the failure is a
*different* silent return in the same path — one of the returns
item 7 lists — the split leaves the supervisor page in place
without a message, which is exactly what the capture shows.

### What the session needs to read

- **`04_kernel_64bit/vmm.c`** — `vmm_map_page_in_cr3` and the
  huge-page-split block inside it.  **The first thing to look for:
  which `if (!phys) return;` the split takes, and whether it is the
  one `6557f05` fixed.**
- **The diff of `6557f05`** — what the session-42 fix actually
  changed, and why it did not fire here.
- **`BOOT_PF.TXT`** — the capture.
- **`docs/open-issues.md` item 7** — the six remaining silent
  `vmm_map_page*` returns.
- **`docs/gotchas.md`** — "A shim's dead code is only dead if you
  watch it not run," for the "correct only for the cases known at
  the time" shape.

### Do not

Do not schedule feature work ahead of this.  Do not tag `v*`.  Do
not add `st_rdev` or the other cosmetic `/dev` gaps until the page
fault is closed.  Do not add a userland program to the image until
this is fixed — that is the condition that reproduces it.

---

## Busybox enablement — what the seam did and did not unblock

**The seam did not make any applet in the "still off" table
enableable.**  This is worth stating plainly, because the seam is
`/dev` and `/proc` work and the intuition is that `/proc` applets
should now turn on.  They do not, and here is why, applet by applet:

- **`ps`** reads `/proc/<pid>/stat` for **every** pid and
  `readdir`s `/proc` to enumerate them.  The seam provides
  `/proc/self/status` and `/proc/self/fd/N`, and **neither a
  listable `/proc` nor any per-pid entry**.  `ps` does not turn on.
- **`top`** reads `/proc/stat`, `/proc/meminfo`, `/proc/loadavg`,
  and per-pid stat.  None exist.  Does not turn on.
- **`kill`** and **`pidof`** walk `/proc` to find a process by name
  or id.  Same: no listable `/proc`, no `/proc/<pid>`.  Do not
  turn on.
- **`less`/`more`** need raw-mode terminal control and open
  `/dev/tty`.  `/dev/tty` does not exist, and `/dev/console` is
  stat-able but **not openable** (commit 4 refuses `open` on it
  with `-ENOENT`).  Do not turn on.

**The one applet whose behavior changed is `tty`.**  It was already
enabled; before the seam it printed `not a tty`, and now it prints
`/dev/console`.  That is the enablement story of session 44: **one
existing applet started working, and no new applet became
enableable.**

**What the seam did do is make the next piece of `/proc` smaller.**
Adding `readdir("/proc")` and a `/proc/<pid>/stat` entry is now a
**table entry and a directory shape**, not an architectural change —
the mechanism (`BACKEND_PROC`, `proc_lookup`, `FILE_KIND_PROC`,
`proc_build_status`) is proven.  That is the payoff, and it is
deferred: it is the piece of work after the vmm fix, and it is
what would turn `ps` on.

### Still off — needs a subsystem (do not enable yet)

| Config | Applet | Blocked by |
|---|---|---|
| `CONFIG_DIFF` | `diff` | `mmap` of files (non-anonymous `mmap`); deliberate |
| `CONFIG_CHMOD` | `chmod` | `chmod`/`fchmodat`; FAT has no permissions |
| `CONFIG_CHOWN` | `chown` | `chown`/`fchownat`; FAT has no ownership |
| `CONFIG_LN` | `ln` | `link`/`symlink`; FAT has no links |
| `CONFIG_LINK` | `link` | same |
| `CONFIG_MOUNT`/`UMOUNT` | `mount`/`umount` | `mount` (165); no VFS |
| `CONFIG_HALT`/`POWEROFF`/`REBOOT` | `halt`/`poweroff`/`reboot` | signal delivery (item 5); no init, no ACPI |
| `CONFIG_TAR`/`UNZIP`/`CPIO`/`GZIP`/`BZIP2`/`XZ` | archives | `mkdirat`, `symlinkat`, `utimensat` storage, file-backed `mmap`, decompression |
| `CONFIG_AWK` | `awk` | large; needs `FEATURE_AWK_LIBM`; `system()`/`getline` need signal delivery |
| `CONFIG_LESS`/`MORE` | pagers | raw-mode terminal control; `/dev/tty` does not exist |
| `CONFIG_TOP`/`PS`/`KILL`/`PIDOF` | process tools | `/proc` needs to be **listable** and **per-pid**; the seam provides only `/proc/self/status` and `/proc/self/fd/N` |
| `CONFIG_NETWORKING` (all) | `ping`, `wget`, etc. | no network stack |
| `CONFIG_FEATURE_FIND_DELETE` | `find -delete` | works via `unlink`/`rmdir`; needs `FEATURE_FIND_DEPTH`, also off |
| `CONFIG_ASH_JOB_CONTROL` | ash job control | signal delivery |
| `CONFIG_FEATURE_TAB_COMPLETION` | ash completion | needs `stat` on many paths; probably works, test it |

### The rule

**Enable an applet only when the syscalls it actually calls are
implemented — read the applet's source, do not guess from its
name.**  Two applets were mis-classified by name in session 42:
`truncate` (uses `ftruncate`, not `truncate(2)`) and `mktemp`
(needs `clock_gettime` through musl's `__randname`, not the
`getpid`+`open` the table said).  Read the source.

---

## Canary state

**Green as of session 44** — `canary` and `canary --full` from both
shells, **15/15** and **28/28**.  The kernel self-test runs at boot
and reports 17/17.

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
                   # stat (S_IFREG), close; 9 checks (new, s44)
    proc_fd        # /proc/self/fd/N -> /dev/console; the
                   # (st_dev, st_ino) match with fstat(0); 7 checks
                   # (new, s44)

**Pipe regression suite (`userland/musl/tests/`):**

    pipe_step1    # create, round-trip, close, re-close EBADF
                  # (no blocking assertions; see below)
    pipe_step2    # blocking + directed wake
    pipe_step3    # EOF, EPIPE, dup-aware counts
    pipe_step3b   # exit-path wake

Run these when changing `sys_read`/`sys_write`/`sys_close`/
`put_file_slot`/`sys_fork`/`sys_pipe` or adding a `FILE_KIND_*`.
**Run them, do not just read the rule** — the suite went six
sessions without being run after a `sys_read`/`sys_write` change,
and `pipe_step1` went stale and hung.  See `docs/gotchas.md`, "A
test can encode an earlier version's behavior."

**New tests must be added to both `USERLAND_ELFS` and the
`mcopy_one` chain in `05_boot_kernel64/Makefile`.**  The image now
stages **41 files**.

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`; use
`busybox sh` or `/bin/busybox sh`.

**Expected noise:** no `Unknown syscall:` lines; no `[fd]` lines
(trace off); no `[a|b|c]` debug line (removed); no `[faccessat]`
trace line (removed).  The `FB: mapped N pages ...` line is
expected.  The `sys_open: f_open FAIL path=...` lines from `vi` on
a new file and from `busybox stat` on nonexistent paths (it probes
`/etc/group`, `/etc/passwd`, `/etc/localtime`) are expected
diagnostics.  `sys_execve: f_open FAIL` lines no longer appear, and
neither do the `f_open FAIL path=dev/null` lines from
`2>/dev/null` — that redirect now succeeds.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **Item 1 in `open-issues.md` is now stale and must be rewritten.**
   It says `sys_execve`'s path attempts and `resolve_against_cwd`
   are "what the dispatch seam subsumes, when the seam lands."
   **The seam landed in session 44.**  `resolve_against_cwd` has
   one caller (`resolve_at`); `execve` routes through `resolve_at`.
   Item 1's premise is false and the entry should be reduced to
   "the remaining `strip_dot_prefix` / `0:` translations belong to
   FatFs, not the seam" or deleted.  This is the "a doc asserting a
   state the repository does not have" shape — see `docs/gotchas.md`.
2. **Redirection of a builtin is silently ignored.**
3. **A builtin in a pipeline is refused.**
4. **`rename(2)` does not replace; `chmod`/`ln`/`mount` need their
   own syscalls.**  Deliberate FatFs-limitation first cuts.
5. **`-EPIPE` is delivered without `SIGPIPE`.**  Fixing it means
   signal delivery — the same subsystem `reboot`/`halt`/`poweroff`
   need.

**The open-issues file's numbering changed in session 43** — the
`readlink` errno item was removed, and Symlinks is now item 8.  Do
not trust an older paste of that file; read it from disk.

Also open: `unlinkat` has no consumer; the six remaining silent
`vmm_map_page*` returns (**the next session's work**); `sys_brk`'s
fixed `heap_base` and the 4 MB mmap window; real FatFs timestamp
storage; `prctl` is minimal; busybox applet symlinks not installed;
syscall-table audit script; `musl_wait`'s WNOHANG loop spins;
`sys_mmap` rejects all non-anonymous mappings; pipes support one
concurrent reader and one concurrent writer; `put_file_slot`'s pipe
wake is coupled to `sys_close`'s wake; **`readdir("/dev")` and
`readdir("/proc")` fail** — the seam serves entries, not
directories; **`st_rdev` is 0 on device nodes**; **`f_stat_with_retry`
has dead `has_drive`** at line 3077.

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
  `truncate`, `realpath`, plus `ash`.
  `CONFIG_FEATURE_VI_WIN_RESIZE=y`.  `CONFIG_FIND=y` and
  `CONFIG_FEATURE_FIND_TYPE=y`.  Other `FEATURE_FIND_*` predicates
  off deliberately.  Off with reasons: `diff`, `chmod`, `ln`,
  `mount`, `halt`/`poweroff`/`reboot`, and the archive/network/
  process tools — see "Busybox enablement."
- `userland/musl/` — tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.  New tests must be added to both
  `USERLAND_ELFS` and the `mcopy_one` chain in
  `05_boot_kernel64/Makefile`.
- `04_kernel_64bit/fonts/ter-u18n.psf` — tracked font source.  The
  `.psf` is tracked; the generated `ter_u18n_data.c` is gitignored.
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  — gitignored; rebuild with `./toolchain/install_musl.sh`.
- `toolchain/{install_musl.sh,musl-gcc.sh}` — tracked.
- `BOOT_PF.TXT` — untracked, the vmm crash capture; **keep it**.
- `split_for_share.sh` — untracked helper, not project material.

Kernel sources: `04_kernel_64bit/`.

---

## Where things live

Not needed to start a session; ask for a file when the current task
needs it.  Paths relative to the tree root
(`/home/noneya/code/donix/`).

- `docs/strategy.md` — Phase A/B plan, rules, files-not-to-touch,
  tagging convention, git hygiene, recovery.  **"When a feature may
  force architecture"** is the rule the seam passed.
- `docs/gotchas.md` — every bug writeup, by subsystem.  **12
  entries** as of session 43.  Session 44 adds three (below, to be
  written).  Session 43 added "A test can encode an earlier
  version's behavior" and "A fix with no test is indistinguishable
  from an unfixed defect."  Session 42 added "The kernel stack is
  16 KB," "A shim's dead code is only dead if you watch it not
  run," "An input-only `syscall` asm block does not tell GCC that
  `%rax` is overwritten," and "A hand-counted string length in a
  syscall wrapper will be wrong."
- `docs/session-log.md` — commit tables and per-test canary notes.
  Session 43's commits are recorded; **session 44's are not yet**.
- `docs/open-issues.md` — full open-issues list.  **Item 1 is now
  stale** (see "Open issues" above) and should be rewritten; item 7
  is the vmm bug.  Numbering changed in session 43.
- `docs/migration-history.md`, `docs/dons-os-history.md` —
  historical narrative (A1-A6, pre-fork).
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` — the first two
  frozen at `v0.6.0`; `LLD_BUG_REPORT.md` current.
- `ROADMAP.md` — future work only; direction of travel beyond the
  current milestone.  Its "Make `/proc` possible" section is now
  **partly done** — the seam exists and has three consumers; it
  needs a refresh at the `v*` bump.
- `README.md` — needs a review at the `v*` bump: `tty` and the
  `/dev/console` / `/proc` entries are new and not yet described.

---

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  `v0.6.7` shipped the shell and the framebuffer;
`v0.6.8` the `*at()` family; `v0.6.9` envp, the `/usr/bin` layout,
the `execve` shim removal; `v0.6.10` `realpath`, the `readlink`
errno, and `/dev/null`.  On top of `v0.6.10`, on `dev` and
untagged, session 44 built the pathname dispatch seam — three
backends, `/dev/null`, `/proc/self/status`, `/dev/console`,
`/proc/self/fd/N`, and `tty` printing a path.  Six commits, all
verified, none pushed.  **Next: the vmm bug** (`BOOT_PF.TXT`),
which reproduces on the first boot after the image grows and
clears by the second or third.  Then the seam's `v*` bump.  Do not
tag a `v*` until the page fault is closed.  One change at a
time.**

---

## How to use this file

At session start, paste this file and say "Continue from here and
request any files you need."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.

**When a session's findings change an earlier numbered step, edit
the step in place — do not just add a paragraph above it.**  A
document assembled from parts carries the state of each part, not
the state of the whole.  This is the same lesson as "read the diff,
not the subject" — and the session-43 handoff had exactly this
problem until it was rewritten.

**Before proposing any command block, read the "Working style"
section at the top.**  It is the project's git and file-return
convention, and it is what every prior session has used.  Session
44 added one rule to it: **when editing a large file, quote the
bytes.**

**Four gotchas worth reading before the next change.**  "A fix with
no test is indistinguishable from an unfixed defect" (`grep` before
scheduling).  "A test can encode an earlier version's behavior"
(run the suite a change's own rule names).  "A consumer inferred
from behavior is not a consumer" (read the caller's source).  And
session 44's own: a case in a switch is not reached if an earlier
guard in the same function refuses the input — `fstat(0)` was
`-EBADF` for many sessions while the `FILE_KIND_CONSOLE` case that
would have answered it sat one line below the `get_file_slot` guard
that rejected it.  See `docs/gotchas.md`.
