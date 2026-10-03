Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-03 (session 49: item 7 was closed, and the
session's work shipped as `v0.6.12`.  **Next: the failure paths
session 49 added are correct by inspection and unexercised; the
fault-injection test is the recommended next session.**)

**Repo state — run these; do not write it here.**  A header that
names a commit or a tag count is wrong the moment the same commit
lands, so this file does not carry one:

    git log --oneline -1            # HEAD
    git status -sb                  # branch, dirty?, ahead/behind
    git tag --list 'v*' | tail -1   # last milestone
    git tag --list '2026*'          # live scratch tags

> **Item 7 is closed and shipped in `v0.6.12`; one thing the session
> added is unverified.**  Item 7 — the silent `vmm_map_page*`
> returns — is fixed: `vmm_map_page` and `vmm_map_page_in_cr3`
> return `int`, all nine callers act on it, and the `isr14_handler`
> diagnostic item 7 required is in the tree.  The session-45
> virtual-1 fault did not reproduce.  **What is not verified:**
> `process_create`'s four failure exits, which now run a real
> cleanup (`process_free_clone` and friends) but have never
> executed — a healthy boot does not fail an allocation.  The
> commit message says "correct by inspection; UNEXERCISED," and
> that is still true.  Closing it needs a **fault-injection
> facility** — one `static` in `pmm.c` and a `selftest` row — which
> is the recommended next session.  See `gotchas.md`, "A function
> that has never run is correct by inspection only," and
> `docs/session-log.md`, session 49.

**The version history, in one line each:** `v0.6.6` pipes; `v0.6.7`
a real shell and a framebuffer console; `v0.6.8` the `*at()` family;
`v0.6.9` envp, the `/usr/bin` layout, the `execve` shim removal;
`v0.6.10` a tail on `v0.6.9` — `realpath`, the `readlink` errno,
and `/dev/null`; `v0.6.11` the pathname dispatch seam, `/proc`
per-pid and `ps`, and the PMM zone-scan fix; **`v0.6.12` a
correctness milestone** — item 7 closed, `process_create`'s failure
exits fixed, the exit-path page-table leak closed, and one dead
`f_stat_with_retry` block deleted.  **No new subsystem in
`v0.6.12`.**

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
fact.  Three instances to remember.  `95c6337 handoff: rewrite fresh
for the v0.6.9 bump` did not rewrite the handoff body.  Session
45's Fix B (`157627c`, later reverted) claimed to close the
boot-time `#PF`; it did not — the fault reproduces with a different
presentation.  And session 49's commit
`20261003-process-create-cleanup` says "correct by inspection;
UNEXERCISED" — which is honest, and is the reason the next session
exists.  Read the diff, not the subject.

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
each version (`Merge dev into main for v0.6.12`, etc.).

**A version is not necessarily a milestone.**  `v0.6.10` is a tail
on `v0.6.9` with no new subsystem.  When that happens, the tag
annotation and the session-log row should *say so*, so a future
reader does not hunt for a milestone narrative that is not there.
**`v0.6.12` is a correctness milestone, not a feature one** — the
annotation should say so plainly.

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

## Where we are — session 49, shipped as `v0.6.12`

**A correctness milestone.**  Five commits, all on kernel failure
paths.  No new subsystem, no new applet, no new syscall.

### Item 7, closed

The silent `if (!phys) return;` in `vmm_map_page_in_cr3` and
`vmm_map_page` — six sites, the PDPT/PD/PT allocation paths in each
— where a failed page-table allocation meant no mapping and the
caller could not tell.  Fixed in two commits:

- **`20261003-vmm-int-return`** — the signature change.  Both
  functions return `int` (0 = mapped, -1 = failed); the six sites
  `return -1;`; both `return 0` on success.  The huge-page split's
  halt was left alone — it already halts with `VMM: FATAL` (session
  42).  The `isr14_handler` `pmm_get_page_type` diagnostic went in
  **in the same commit**, because item 7 required it be in place
  before the signature change was attempted.  **The session-45
  virtual-1 fault did not reproduce.**
- **`20261003-vmm-callers`** — the nine callers handle the `-1`.
  Two **halt** (`ensure_hhdm_mapped`, `vmm_init`'s identity map —
  boot paths with no caller to report to); seven **recover**
  (`elf_load_into_process`, `process_create`'s stack loop,
  `heap_extend`, `sys_mmap`, `sys_brk`, `exec_alloc_user_stack`,
  `sys_fork`'s `EAGER_COPY_REGION` macro).  `heap_extend` is the
  nontrivial one: it unmaps and frees its partial region, because
  nothing else tracks those pages.

### The defect the item-7 work surfaced

`process_create` had `pcb->cr3 = vmm_clone_page_table(current_cr3);`
**with no check** — a `0` return became `pcb->cr3 = 0`, and the next
`vmm_map_page_in_cr3` walked page tables at `HHDM_START + 0`.  Worse
than a leak.  The same function's failure exits also leaked the
cloned page-table hierarchy.

**`20261003-process-create-cleanup`** fixes both: a new
`process_free_clone(cr3)` walks the cloned PML4 and frees entries
0..255 and the PML4, skipping the recursive index, entries >= 256
(shared with the parent — the HHDM and kernel image), and 2 MB huge
PDEs.  The four failure exits run full cleanup.

**"Correct by inspection; UNEXERCISED."**  Every one of those four
is a path a healthy boot does not take.

### The exit-path leak, and the run that verified the helper

**`20261003-exit-frees-tables`** — `process_reclaim` and
`process_destroy` abandoned the cloned page-table hierarchy at exit;
they now call `process_free_clone`.  Correct for a process that has
*run*, though written for one that never did: the low half has grown
(ELF, stack, brk, mmap), but it is still this process's own, and the
walk frees tables only — `process_cleanup_elf_pages` frees the data
pages, and the two are disjoint.

**This is what first exercised `process_free_clone`.**  `exec_churn`
(24 rounds of fork/execve/wait4) ran it 24 times, the `k`-shell
selftest's three exception children 3 more — no fault, and no
`PMM: WARNING - Double free`.  `exec_churn` now has a second use:
it is the test that exercises the exit-path teardown.

### Dead code

**`20261003-dead-has-drive`** — `f_stat_with_retry`'s `has_drive`
computed a variable, cast it to `(void)`, and did nothing with it.
Deleted.  `kernel.bin` unchanged after the commit, confirming the
code was dead.

### The two older milestones, condensed

**The pathname dispatch seam (`v0.6.11`, session 44).**
`resolve_at` returns a backend tag from a path's first component;
FAT / DEV / PROC are selected by it.  No VFS.  Its consumers:
`/dev/null`, `/proc/self/status`, `/dev/console` +
`/proc/self/fd/N`.  Full narrative in `ROADMAP.md`.

**`/proc` per-pid and `ps` (`v0.6.11`, session 47).**  `/proc` is a
listable directory; `ps` and `pstree` work.  The bug worth
remembering: `procps_scan` stats `/proc/<pid>/` **with a trailing
slash** and skips the entry when that fails.  Full narrative in
`ROADMAP.md`; the lesson is in `gotchas.md`.

**The PMM zone-scan fix (`v0.6.11`, session 48).**  The
intermittent boot-time `#PF` at `0x400000` was `pmm_alloc_page`'s
one-directional scan, not item 7.  `pmm_scan_zone` now wraps.  Full
narrative in `ROADMAP.md`.

### Session 45 — the one thing it left

Session 45 attempted the item-7 fix three ways and reverted all
three.  Its **fault is the surviving artifact**: a user-mode `#PF`
at virtual 1 (`CR2 = RIP = 0x1`, error `0x15`, `pte = 0x3` present /
write / **no user**, phys 1, every page of the walk `PAGE_TABLE`),
observed with the signature change and **not seen since** — including
through session 49's full signature change.

**Disposition: instrumented, not fixed.**  `isr14_handler` prints
the four page types of every `#PF` walk, permanently.  If it
recurs, the dump is in the serial log at the moment it happens.  Its
one kept commit is the `run` script's build-capture fix (`6cfb0e6`),
which rides on `dev` untagged.

---

## NEXT SESSION — the failure paths session 49 added are unexercised

**This is the recommended target.**  Session 49's commit
`20261003-process-create-cleanup` added real cleanup to
`process_create`'s four failure exits — `pmm_alloc_page_for_elf`
failure, `vmm_map_page_in_cr3` failure, `kernel_stack_slot_alloc`
failure, and the `vmm_clone_page_table == 0` case.  **None has ever
run**, because a healthy boot does not fail an allocation.  The
commit message says so; the session log says so; the gotcha entry
"A function that has never run is correct by inspection only" says
so.

**Why it is worth a session now.**  The cleanup is what stands
between a failed allocation and a corrupted machine.  Today it is
verified by reading only.  The first time it matters may be the day
an allocation fails on a real system, at which point the cleanup
either works or makes the failure worse.  Closing it while the code
is fresh is cheaper than closing it after a fault.

### What the work is

A **fault-injection facility**, permanent and non-production:

- In `pmm.c`: a `static int pmm_fail_next_alloc` and a
  `void pmm_debug_fail_next(void)` that sets it; at the top of
  `pmm_alloc_page`, `if (pmm_fail_next_alloc) { pmm_fail_next_alloc
  = 0; return 0; }`.  One branch on the hot path — predictable and
  cheap.
- In `kmain.c`'s self-test: a `test_create_fail` row that sets the
  flag, calls `process_create` four times (once per failing exit —
  the clone case needs the flag to trip on the very first
  `pmm_alloc_page_for_tables`, so the test may need to call
  `vmm_clone_page_table` indirectly or order the calls), and
  asserts `pmm_get_free_pages()` is unchanged across each.

**No new syscall.**  The flag is kernel-side and the self-test runs
in kernel context, so the facility does not need a user-visible
door.  That keeps the change small and keeps the debug hook out of
the syscall table.

### What "done" looks like

- `selftest` gains the row and reports **18 passed, 0 failed** (was
  17).
- `pmm_get_free_pages()` before each `process_create` equals the
  value after its failure — the leak is measurably gone.
- The `PMM: WARNING - Double free` message does not appear.
- `process_create`'s failure path is exercised, and the "correct by
  inspection; UNEXERCISED" claim in
  `20261003-process-create-cleanup`'s message is superseded by the
  new commit's message — which can say "exercised by `test_create_fail`."

**Read first:** `process.c`'s `process_create` (the four exits and
`process_free_clone`), `pmm.c`'s `pmm_alloc_page`, and
`docs/gotchas.md`, "A function that has never run is correct by
inspection only."

### If you would rather do something small

**`sys_gettimeofday` (99)** — a few lines from `g_ticks`, like the
existing `sys_clock_gettime` (228).  It unlocks busybox `ps -l` /
`ps -e` (`PS_LONG`, `PS_TIME`), which currently hit
`Unknown syscall: 99`.  Small, patterned, and independent.

### Candidates after that, none blocking

- **`open("/proc/<pid>", O_DIRECTORY)`** — the "complete" half of
  session 47's fix.  Same two paths in `open_resolved`, producing a
  `FILE_KIND_DIR` slot with `PROC_DIR_SENTINEL`, which exists.
- **`/dev` as a listable directory** — `ls /dev` fails.  Same
  directory shape `/proc` got.  Prerequisite for `/dev/tty` and
  `/dev/urandom`.
- **`musl_sh` as a child of `idle`** — so `pstree` shows a
  Unix-like tree.  A `kmain`/`process_create` change.
- **`/etc/passwd`** — makes `ps`'s USER column show names.
- **`kill` / `pidof`** — `pidof` needs the per-pid entries (now
  present) and a lookup; `kill` additionally needs signal delivery.
- **The `proc_walk` assertion test** — turn the diagnostic into a
  test that asserts.
- **Symlinks** (`open-issues.md` item 8) — larger; design recorded,
  the seam exists to hide the encoding.

### Do not

Do not reopen the PMM fix or item 7 — both are committed, shipped
in `v0.6.12`, and verified.  One change at a time.  Do not edit
`third_party/`.

---

## Busybox enablement — state of play

**`ps`, `pstree`, `stty`, and `tty` are enabled.**  `ps` lists
processes; `pstree` shows `idle`; `tty` prints `/dev/console`;
`stty` prints the terminal state.

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

### A note on `stty`

`stty` runs and prints a plausible state (speed, control
characters, flags).  **It cannot change the terminal:** the kernel
console has no termios, so `stty -icanon`, `stty erase X`, and the
like do nothing.  The applet reads and reports; it cannot write.
Line editing in `ash` is busybox's own, independent of the kernel.
Not a defect — a limit.

### What each still-off applet needs

Each row is a **cost estimate, not a prohibition**.  Sometimes the
missing piece is small and we write it — that is how `tty` and `ps`
were enabled.  Sometimes it is a whole subsystem and we do not
write it just to turn on one applet.  The rule is **know what you
are signing up for** before you enable.

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

**Enabled and working, for the record:** `ps`, `pstree`, `stty`,
`tty` (prints `/dev/console`), plus the applets listed under
"State on disk."

---

## Canary state

**Green as of session 49** — `canary` 15/15 and `canary --full`
28/28, plus `exec_churn` and `mmap_stress` pass.  The kernel
self-test runs at boot and reports 17/17.  **Session 49 added a
second use for `exec_churn`:** it exercises the process-exit
page-table teardown (`process_free_clone` via `process_reclaim` /
`process_destroy`), so run it when changing those.

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
    mmap_stress    # map/touch/unmap/remap; the munmap free path
    exec_churn     # fork/execve/wait4, 24 rounds; the exit free
                   # path, and the process-exit page-table teardown
                   # (needs CHURN_HELPER staged)

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
stages **48 files**.

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

**Session 49's own noise to expect:** none.  Every message session
49 added is on a failure path, and a healthy boot runs none of
them.  If a `PROCESS:` or `sys_mmap: map failed` or `sys_brk: map
failed` or `VMM: FATAL` line appears, that is a real failure the
new checks are now reporting — not noise.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **`open-issues.md` item 1: the remaining FatFs-form
   conversions.**  `strip_dot_prefix` and the `"0:"` prefix are the
   FAT backend's own business now; a future VFS absorbs them.
2. **Redirection of a builtin is silently ignored.**
3. **A builtin in a pipeline is refused.**
4. **`rename(2)` does not replace; `chmod`/`ln`/`mount` need their
   own syscalls.**  Deliberate FatFs-limitation first cuts.
5. **`-EPIPE` is delivered without `SIGPIPE`.**  Fixing it means
   signal delivery — the same subsystem `reboot`/`halt`/`poweroff`
   need.

**Item 7 is closed** (session 49, shipped in `v0.6.12`).  The
`open-issues.md` list keeps its numbering and has a **gap where 7
was**; a one-line note at the top of the list says so, and that note
plus the "item 7" references in older docs come out in the next
documentation round.

Also open: `unlinkat` has no consumer; `sys_brk`'s fixed
`heap_base` and the 4 MB mmap window; real FatFs timestamp
storage; `prctl` is minimal; busybox applet symlinks not
installed; syscall-table audit script; `musl_wait`'s WNOHANG loop
spins; `sys_mmap` rejects all non-anonymous mappings; pipes
support one concurrent reader and one concurrent writer;
`put_file_slot`'s pipe wake is coupled to `sys_close`'s wake;
**`readdir("/dev")` fails**; **`st_rdev` is 0 on device nodes**;
**`open("/proc/<pid>", O_DIRECTORY)` is not done**; **a
nonexistent pid stats as a directory**; **`sys_gettimeofday` (99)
is not implemented**, so `ps -l` is off.

**The session-45 virtual-1 fault is unobserved, not fixed.**  It
did not reproduce through session 49's full signature change.  Its
diagnostic is permanently in `isr14_handler`; if it recurs, the
four page types print at the moment it happens.  **Standing
caution:** do not filter `vmm_clone_page_table`'s leaf copy on
`PT_USER` — session 45 found it breaks the kernel's own identity
map.

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
- `PFcapture.txt` — the session-45 fault capture (presentation 2).
  Gitignored; **still on disk — `ls` it, `git status` will not
  show it.**  Presentation 1's capture is not on disk; its dump is
  in `docs/session-log.md`, session 49.
- `DFAULT.txt` — the session-48 double-fault capture.  Gitignored.
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
  **Session 49 added two entries**: "A function that has never run
  is correct by inspection only" and "A comment that names its
  callers goes stale the moment a new caller appears."  Session 48
  added "A zone scan that moves one way does not find pages behind
  its cursor."  Session 47 added "A /proc consumer can stat a path
  it never opens."  Session 46 added "A redirection binds to the
  last command in an `&&` chain."  Session 45's `pmm_get_page_type`
  result (the virtual-1 fault's walk was all `PAGE_TABLE`, ruling
  out use-after-free) is a finding about one fault, not a general
  lesson, and lives in `docs/session-log.md`, session 49, rather
  than here.
- `docs/session-log.md` — commit tables and per-test canary notes.
  Session 49's section is at the top; then 48's, 47's, 46's, and
  44's.  Session 44's section is recorded but **misplaced** (it
  sits after session 34), noted at the top of the file and
  deferred.  There is no session-45 section: session 45's one kept
  commit is the tooling fix, named in the session-46 section.
- `docs/open-issues.md` — full open-issues list.  **Item 7 is
  closed; the list has a gap where it was, with a one-line note.**
  Item 8 is symlinks.  The "Test-design notes" section at the
  bottom is **misplaced** (it is instructions, not issues) and is
  flagged for a move to this file's canary section in the next
  documentation round, together with the item-7 note.
- `docs/migration-history.md`, `docs/dons-os-history.md` —
  historical narrative.
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` — the first two
  frozen at `v0.6.0`; `LLD_BUG_REPORT.md` current.
- `ROADMAP.md` — future work only.  **Its `v0.6.12` section is the
  session-49 narrative**; the "Done — Phases A through B" list
  includes `v0.6.12`.  The `v0.6.11` "does not close" list is now
  a historical statement (item 7 was closed in `v0.6.12`).  Its
  "`/proc` — shipped, with edges" section is largely done; the
  remaining direction is the post-`/proc` list.
- `README.md` — reviewed at the `v0.6.11` bump; **review it at the
  `v0.6.12` bump.**
- `run` — tracked; the build-and-capture fix lives here.

---

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  `v0.6.7` shipped the shell and the framebuffer;
`v0.6.8` the `*at()` family; `v0.6.9` envp, the `/usr/bin` layout,
the `execve` shim removal; `v0.6.10` `realpath`, the `readlink`
errno, and `/dev/null`; `v0.6.11` the pathname dispatch seam,
`/proc` per-pid and `ps`, and the PMM zone-scan fix; **`v0.6.12` a
correctness milestone** — item 7's silent `vmm_map_page*` returns
closed, `process_create`'s failure exits fixed, the exit-path
page-table leak closed, and one dead `f_stat_with_retry` block
deleted.  **The recommended next session is the fault-injection
test for `process_create`'s four failure exits** — they are correct
by inspection and unexercised, and that test is what would make
them verified.  See NEXT SESSION and `gotchas.md`, "A function that
has never run is correct by inspection only."  One change at a
time.**

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

**Five gotchas worth reading before the next change.**  "A fix with
no test is indistinguishable from an unfixed defect."  "A test can
encode an earlier version's behavior."  "A consumer inferred from
behavior is not a consumer."  Session 45's: a fix can fail to close
the thing it claims to close, and an intermittent fault that stops
reproducing is not fixed — it is unobserved.  And session 49's: **a
function that has never run is correct by inspection only** — which
is the reason the next session exists.
