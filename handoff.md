Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-04 (session 54: the zombie leak closed,
the NX/non-canonical address family fixed, and user faults made
honest.  One code commit and three docs commits on `dev`, all
untagged.  **Next: item 12 -- signal delivery -- is the largest
and the most-reaching target; item 10 (the SSE gap) remains
filed and large; `sys_gettimeofday` (99) is the smallest clean
independent item.**)

**Repo state -- run these; do not write it here.**  A header that
names a commit or a tag count is wrong the moment the same commit
lands, so this file does not carry one:

    git log --oneline -1            # HEAD
    git status -sb                  # branch, dirty?, ahead/behind
    git tag --list 'v*' | tail -1   # last milestone
    git tag --list '2026*'          # live scratch tags

> **The rule this file carries forward, from session 52: an item
> here that quotes an expected value is a claim.**  Session 51
> filed item 7c as "`sha512sum` computes a WRONG digest for
> `ABC`" with a value that turned out to be the SHA-512 of
> lowercase `abc`.  Session 52 spent its time reading correct
> code.  Session 53 falsified four mechanisms the same way -- each
> was a plausible story read off a trace, and each was killed by
> reading the source.  Session 54 falsified three more before
> finding the right mechanism for each.  **Read the function
> before proposing the fix.**  The greps cost seconds; the wrong
> fixes cost boots.

**The version history, in one line each:** `v0.6.6` pipes; `v0.6.7`
a real shell and a framebuffer console; `v0.6.8` the `*at()` family;
`v0.6.9` envp, the `/usr/bin` layout, the `execve` shim removal;
`v0.6.10` a tail on `v0.6.9` -- `realpath`, the `readlink` errno,
and `/dev/null`; `v0.6.11` the pathname dispatch seam, `/proc`
per-pid and `ps`, and the PMM zone-scan fix; **`v0.6.12` a
correctness milestone** -- item 7 closed, `process_create`'s failure
exits fixed, the exit-path page-table leak closed, and one dead
`f_stat_with_retry` block deleted.  **No new subsystem in
`v0.6.12`.**

**Sessions 51 through 54 are untagged work on `dev`, after
`v0.6.12`.**  Session 51: five commits -- the item-7a instrument,
the gzip fix, two applet batches, the image tree and harness, and a
session-log rewrite.  Session 52: three commits -- the `sha512sum`
correction and item-7c retraction, the gotcha, and open-issues item
10.  Session 53: two code commits (`96153e6` the `g_last_sysret`
wiring, `d2ad311` the `find -not` row and the no-fork `ran`
failure path) plus three docs commits.  **Session 54: one code
commit (the zombie-leak fix, the NX/non-canonical address family,
and the honest exit-status change) plus three docs commits; seven
kernel fixes and three diagnostic changes in one commit; verified
by 4000 iterations of `pipe_wake_probe.sh` completing with
`loopdone`.**  No `v*` bump; the banner still reads `v0.6.12`.

Commits are named by tag only, never by SHA.  **Working tags
(`YYYYMMDD-*`) are local scratch restore points** -- they exist while
a milestone is being developed and are **dropped before the
milestone is pushed**.  Only `v*` tags go to the remote and are
permanent.  The commit record is `docs/session-log.md`; the commit
message carries the narrative.  Once a scratch tag is dropped it
resolves to nothing; do not cite one as if it were a stable
reference.

**A plain commit can exist with no tag.**  Session 45's `run:` fix
is one.  Sessions 51's, 52's, 53's, and 54's docs commits are the
same -- docs-only, no tag.  Such commits ride along on `dev` until
the next `v*` bump.  Do not invent a tag for one, and do not be
surprised by a commit with no tag.

**Note on commit messages, and now on this file too:** a commit
message is a claim, not a fact.  Read the diff, not the subject.
**This file's item text is the same kind of claim** -- session 52
exists because session 51's item 7c was believed; session 53
falsified four mechanisms that were plausible stories read off a
trace; session 54 falsified three more.  Two instances to remember
from earlier sessions.  Session 49's commit
`20261003-process-create-cleanup` says "correct by inspection;
UNEXERCISED" -- honest when written, and now **superseded for three
of its four exits** by session 50's `20261003-fail-inject`; exit 3
is still untested and its commit says so.  And session 45's Fix B
(`157627c`, later reverted) claimed to close the boot-time `#PF`;
it did not.

---

## Working style -- how this project gets changed

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

**Check the directory, too.**  Session 52 learned this: a block
without a `cd` was pasted at `/tmp`, and every path resolved
wrong.  Start every command block with `cd /home/noneya/code/donix
|| exit 1` and a `pwd`.  The state check does not catch a wrong
directory; `cd ... || exit 1` does.

**A command block is one paste.**  `git add`, `git status`,
`git commit`, and `git tag` belong in **one** block, with the
commit message inline via heredoc (`git commit -F - <<'EOF' ... EOF`),
not spread across several turns.  The same for the push/merge
sequence below.  Do not split a single operation across messages;
that is how a commit and its tag get separated.

**Two file-return styles.**

- **Small files:** return the complete file, four-backtick fenced.
- **Large files, or files with non-ASCII content:** return a
  unified-diff hunk or a copy-block insertion, each located by
  enough surrounding context to be unambiguous, preceded by a bold
  **`WORK BEGINS HERE.`** marker on its own line so it is never
  confused with thinking-out-loud.

  The marker is load-bearing: before it, the assistant is
  discussing; after it, the content is applyable.  Never mix the
  two -- do not post file parts while reasoning.

  **A whole-file return for a large file with non-ASCII content
  re-encodes every non-ASCII character and shows up as a large
  deletion count in `git diff --stat`.**  This was learned in
  session 51.  The fix is a copy-block insertion that touches only
  the new lines.  **Check `git diff --stat` before committing: a
  docs insertion should show zero deletions.**  A full
  *replacement* of a section correctly shows both insertions and
  deletions; that is the difference.  Sessions 53 and 54 inserted
  into `gotchas.md`, `open-issues.md`, and `session-log.md`;
  session 54's `handoff.md` is a full replacement, with both
  insertions and deletions expected.

**Ask for source you do not have.**  The assistant does not have
direct file access.  Before patching a file whose current contents
it has not seen in this session, it must **ask for that file**.
Never guess at a file's contents, never patch from memory of an
earlier version, never assume a file is unchanged.  This is how
stale patches and reverted work have been avoided.  Session 54
asked for and was given `open-issues.md`, `gotchas.md`,
`session-log.md`, `handoff.md`, `vmm.c`, `stage2.asm`,
`scheduler.c`, `pmm.c`, `elf.c`, `process.c`, `process.h`,
`user_syscall_entry.asm`, and `interrupts.c` before writing into
any of them.

**When editing a large file, quote the bytes.**  For an edit inside
a big file, the instruction must **quote the exact text being
replaced and the exact text that replaces it**, both copied from
output the other side just pasted.  Do not describe an edit as
"the block above" or "after line N" when N has not been seen.

**A redirection binds to the last command in an `&&` chain.**
`a && b && c && d > file` sends only `d`'s output to `file`; `a`,
`b`, and `c` write to the terminal.  When the whole chain's output
matters, wrap it: `{ a && b && c && d ; } 2>&1 | tee file`.  See
`docs/gotchas.md`.

**A capture file is one run.  Truncate, do not append.**  `>` per
run.

**A build flag change does not trigger a rebuild unless the
Makefile is a prerequisite.**  Session 52 changed the userland
CFLAGS and `make` printed `Nothing to be done for 'all'` twice,
because the flag is not a prerequisite of the `.elf` targets.  The
`userland/musl/Makefile` now has `Makefile` as a prerequisite of
both `%.elf` rules; a future flag change rebuilds on the next
`make`.  When a build "does nothing" after a flag change, `make
clean` first, or check the prerequisite list.

**Do not edit `third_party/`.**  The vendored sources there are
gitignored and rebuilt by the toolchain, so an edit is invisible to
the repo and vanishes on the next build.  When a diagnostic needs
to see inside a third-party applet, the right instrument is a
**first-party test** that reproduces the applet's sequence, or a
**trace in our own kernel**, not a patch to `third_party/`.  Both
are committable; the patch is not.

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

The tag is **annotated** (`-a -F -`) -- the annotation is the
milestone narrative, and it is what a future reader sees first.
The merge is `--no-ff`, so `main` keeps a real merge commit for
each version.

**A version is not necessarily a milestone.**  When that happens,
the tag annotation and the session-log row should *say so*.
**`v0.6.12` is a correctness milestone, not a feature one.**
Sessions 51, 52, 53, and 54 are the same shape: no new subsystem,
no `v*` yet.

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
name before editing -- do not trust the number alone.
`05_boot_kernel64/` holds the boot chain assembly and the image
builder, not the kernel C sources.

Scratch workspaces (e.g. a copy at `/home/noneya/code/testme/`)
are **transient**: never the source of truth, never where a `v*`
tag lives, and never referenced by this file.  If one exists, it is
safe to reset or delete.  Do not port from a scratch workspace into
the real tree without a build and test in the real tree.

**Run command blocks from the repository root.**  Session 52 ran a
block from `/tmp` and every path resolved wrong; the block now
starts with `cd /home/noneya/code/donix || exit 1`.

---

## Where we are -- session 54, after `v0.6.12`

**One code commit and three docs commits on `dev`, all untagged,
plus one annotated scratch tag (`20261004-zombie-leak-and-nx-family`
on the code commit).**

### Item 11 is closed -- the zombie leak (`open-issues.md`)

The cause was **not** the lost pipe-EOF wake.  It was that
`process_exit` did not call `close_all_files`.  `sys_exit` did, but
`process_exit` is also reached directly from `fault_kill_current`,
from `user_syscall_entry.asm`'s exit jmp, and from `sys_execve`'s
failure branch -- and those three did not close the file table.  A
pipe end inherited from a fork parent leaked its reference on every
direct-path exit, `pipe->refcount` never reached zero, and the
`pipe_t` and its 4096-byte ring buffer were never freed.

**The fix:** `close_all_files` is un-static'd, declared in
`include/process.h`, and called at the top of `process_exit`.  It is
idempotent, so the two paths that already closed are no-ops.

**Verified:** `pipe_wake_probe.sh` now completes 4000 iterations and
prints `loopdone`.  No PCB exhaustion, no leaks.

### Item 7e is closed as a BUSYBOX bug (`open-issues.md`)

Every capture across sessions 45-54 has `last sysret rcx` in busybox
text (`0x4377A3`, `0x43A89A`, `0x43F89A`, `0x43A5D4`, `0x43D639`
among them); the kernel returns correctly every time.  The user
process then executes a control transfer to a data value (`0x24`,
`0x80100009B0`, `0x80000FB700`, `0x0400000000000000` in session
54's captures).  donix executes the jump faithfully; the CPU faults.

Session 54's diagnostic work makes the fault produce a clean
`error=0x15` with `CR2 == RIP`, and the shell now prints
`Segmentation fault`.  Nothing more for the kernel to fix.

### The NX / non-canonical address family

Four fixes, one mechanism:

- **`vmm_get_phys` and `vmm_get_phys_from_cr3` strip `PT_NX`.**
  `~0xFFFULL` alone left bit 63 set; adding `HHDM_START` produced a
  non-canonical address and the next load faulted with `#GP` error 0.
- **`sys_mmap` honors `PROT_EXEC`.**  Every anonymous mapping was
  executable before; now a `PROT_READ|PROT_WRITE` mapping is NX, and
  a stray jump into the mmap window faults at the jump instead of
  running garbage.
- **The huge-page split sets NX on the 512 split PTEs, not on the
  PDE.**  A PDE's NX bit propagates to every page below it; that
  broke the musl_sh boot at `RIP=CR2=0x4010A6`.
- **`pte_phys`**: a helper that strips both the low 12 bits and
  `PT_NX` from a table entry.  Every walk site in `vmm.c` uses it.

### The fault model

**`isr14_handler`'s walk does not halt.**  A user-mode `#PF` at an
unmapped address now prints the full register dump and stack window
before killing only the faulting process; the kernel keeps running.
This was the diagnostic that had been silently skipped on every 7e
capture before.

**`fault_signal` + `sys_wait4`.**  A faulted child's wait status is
now a signal-kill encoding (low byte = signal number, mapped from
the CPU exception vector to a Linux signal).  busybox ash prints
`Segmentation fault`; before this, a faulted child exited with
status 0 and the shell could not tell a crash from a clean exit.

### The intermittent family, condensed

**7a** is the boot-time `#PF` at `0x400000` (session 50,
instrumented, not fired).  **7b** is the `#GP` at `0x42F1A7`
(session 51, unobserved).  **7e** is the `#GP`/`#PF` control-flow
family in fork-heavy workloads -- **now closed as a busybox bug**,
see above.

**Three faults in one family remain unobserved, not fixed:**
session 45's virtual-1 `#PF`, session 50's `0x400000` `#PF` (7a),
session 51's `#GP` at `0x42F1A7` (7b).  All are intermittent and
layout-dependent.  Diagnostics in place: `isr14_handler` prints the
four page types of every `#PF` walk; the item-7a sites print
`site`/`virt`/`cr3`/`free`; `isr13` and `isr14` both dump the
register frame and the user stack window; both print the last
`sysret` target when `fault_rip < 0x1000`.  **Standing caution:**
do not filter `vmm_clone_page_table`'s leaf copy on `PT_USER` --
session 45 found it breaks the kernel's own identity map.

### The older milestones, condensed

**The pathname dispatch seam (`v0.6.11`, session 44).**
`resolve_at` returns a backend tag from a path's first component;
FAT / DEV / PROC are selected by it.  No VFS.

**`/proc` per-pid and `ps` (`v0.6.11`, session 47).**  `/proc` is a
listable directory; `ps` and `pstree` work.

**The PMM zone-scan fix (`v0.6.11`, session 48).**  The
intermittent boot-time `#PF` at `0x400000` was `pmm_alloc_page`'s
one-directional scan, not item 7.

### Sessions 49 and 50 -- the failure-path work

Session 49 closed item 7 (the silent `vmm_map_page*` returns), fixed
`process_create`'s failure exits, and closed the exit-path
page-table leak.  Session 50 added the fault-injection hooks
(`pmm_debug_fail_next_of_type`, `process_debug_fail_next_stack_slot`)
and the `create_fail` selftest row, so **three of `process_create`'s
four failure exits now run** -- exit 3 is still by inspection.

---

## NEXT SESSION

**Item 11 is closed and item 7e is closed as a busybox bug.**  The
handoff's previous "clearest target" is gone.  The next session
should pick one of:

### The candidates, in the order the handoff would pick them

1. **Item 12 -- signal delivery.**  `sys_rt_sigaction` returns 0
   and installs nothing; `sys_rt_sigprocmask` returns 0 and does
   nothing.  A process that faults is killed; a program that wants
   to catch `SIGSEGV` cannot; a program that expects `SIGPIPE` on
   a write with no reader sees the errno instead.  **This is the
   largest target on the list and the most-reaching.**  It is what
   unblocks `CONFIG_KILL`, `CONFIG_ASH_JOB_CONTROL` (item 13), and
   the Wayland `wl_shm` `SIGBUS` path (`ROADMAP.md`).  It is a
   subsystem, not a small change: per-process signal handlers, a
   raise on the fault path that runs the handler if installed, a
   `SIGPIPE` raise on the `-EPIPE` write path, and an
   `rt_sigreturn` path to restore the frame after the handler runs.
   Session 54's `fault_signal` field is the first half of the
   *reporting* side; delivery is the other half.

2. **Item 10 -- the SSE / `CR4.OSFXSR` gap.**  Large, filed,
   latent.  A kernel feature (`FXSAVE`/`FXRSTOR` on the switch and
   interrupt paths).  The `-mno-sse*` CFLAGS stopgap from session
   52 can come off once this lands.

3. **`sys_gettimeofday` (99).**  A few lines from `g_ticks`, like
   the existing `sys_clock_gettime` (228).  Unlocks `PS_LONG` and
   `PS_TIME` for busybox `ps -l` / `ps -e`, which currently hit
   `Unknown syscall: 99`.  **Small, patterned, independent.**  A
   good session if item 12 is too large to start cleanly.

4. **`open("/proc/<pid>", O_DIRECTORY)`.**  The "complete" half of
   session 47's fix.  The `PROC_DIR_SENTINEL` mechanism exists;
   `stat_resolved` already accepts the path.

5. **`/dev` as a listable directory.**  `ls /dev` fails.  Same
   directory shape `/proc` got.  Prerequisite for `/dev/tty` and
   `/dev/urandom`.

6. **A first-party reproducer for item 7e** if a future session
   wants to close it further.  The busybox bug is real, but a
   minimal C program that does `x=$(cmd)`'s sequence would
   reproduce it (or not reproduce it) without busybox in the loop.
   That would tell us whether it is a musl `mallocng` bug, an
   ash bookkeeping bug, or something else.  **Lower priority than
   item 12** -- the kernel-side work here is done.

### If you would rather do something small and clean

**`CONFIG_FEATURE_FANCY_SLEEP=y`** -- makes `sleep 0.1` work.
One config flag.

**`sys_gettimeofday` (99)** -- see above.

**More applets from the "still-off" table** -- the zero-syscall
ones.

### Do not

Do not re-open item 7 (closed, `v0.6.12`), item 11 (closed,
session 54), item 7e (closed as busybox, session 54), or the
session-48 zone scan (fixed, `v0.6.11`).  Do not re-litigate the
fault-injection design.  **Do not re-read the fork path** --
session 53 read `sys_fork`'s stack copy,
`process_fork_copy_frame`, and `exec_alloc_user_stack`, and all
three are correct.  Do not chase the intermittent family (7a, 7b)
without a reproducer -- every session that tried produced a "did
not fire" result.  **Do not drop the older non-`2026*` tags.**
One change at a time.  Do not edit `third_party/`.

---

## Busybox enablement -- state of play

**Enabled and working:** `ps`, `pstree`, `stty`, `tty`
(prints `/dev/console`), `gzip`, `gunzip`, plus everything in
batches 2 and 3.  `sha512sum` **works** -- item 7c was a test bug.
`find -not` **works with `!`** -- `-not` is a GNU alias gated
behind `ENABLE_DESKTOP`, which this build does not set.

### The rule

**Enable an applet only when the syscalls it actually calls are
implemented -- read the applet's source, do not guess from its
name.**  When something is missing, the question is "how big is
it?" -- a table entry and a directory shape is a session's work; a
signal-delivery subsystem is not.  A syscall that exists but
cannot do its job (a `chmod` on a filesystem with no permissions)
is worse than a missing one: it makes the applet lie.

**A `/proc` consumer can stat a path it never opens.**  Session
47's lesson.  See `gotchas.md`.

**A real applet finds real bugs.**  Session 51's `gzip` found the
`isatty` bug.  **Session 52 is the counter-example: a real applet
found a wrong test.**  Session 53 found a **missing feature in the
config** (`-not` requires `ENABLE_DESKTOP`).  Session 54 found no
new applet bug.  The rule holds: when a test and an applet
disagree, find out which is right, and do not assume the test is.

**Do not edit `third_party/`.**

### A note on `stty`

`stty` runs and prints a plausible state.  **It cannot change the
terminal:** the kernel console has no termios.  Not a defect -- a
limit.

### What each still-off applet needs

Each row is a **cost estimate, not a prohibition**.

| Config | Applet | Needs |
|---|---|---|
| `CONFIG_DIFF` | `diff` | `mmap` of files (non-anonymous); deliberate |
| `CONFIG_CHMOD` | `chmod` | `chmod`/`fchmodat`; FAT has no permissions |
| `CONFIG_CHOWN` | `chown` | `chown`/`fchownat`; FAT has no ownership |
| `CONFIG_LN` / `LINK` | `ln` / `link` | `link`/`symlink`; FAT has no links |
| `CONFIG_MOUNT`/`UMOUNT` | `mount`/`umount` | `mount` (165); no VFS |
| `CONFIG_HALT`/`POWEROFF`/`REBOOT` | | signal delivery (item 12); no init, no ACPI |
| `CONFIG_TAR`/`UNZIP`/`CPIO`/`BZIP2`/`XZ` | archives | `mkdirat`, `symlinkat`, `utimensat` storage, file-backed `mmap`, decompression |
| `CONFIG_AWK` | `awk` | large; needs `FEATURE_AWK_LIBM` |
| `CONFIG_LESS`/`MORE` | pagers | raw-mode terminal control; `/dev/tty` does not exist |
| `CONFIG_TOP` | `top` | `ps -l`-class fields (`gettimeofday`), a redraw loop |
| `CONFIG_KILL` | `kill` | signal delivery (item 12) |
| `CONFIG_NETWORKING` (all) | `ping`, `wget`, etc. | no network stack |
| `CONFIG_ASH_JOB_CONTROL` | ash job control | signal delivery (item 12); item 13 |
| `CONFIG_FEATURE_FANCY_SLEEP` | `sleep 0.1` | nothing -- the flag itself |

---

## Canary state

**Green as of session 54** -- `canary` 15/15 and `canary --full`
28/28 on the clean boot; `selftest` 18/18; `test.sh` **57 passed,
0 failed**.  `pipe_wake_probe.sh` **now runs to `loopdone`** at
4000 iterations; it is no longer a hang reproducer.

**A note on the pipe_wake_probe.sh run.**  During the 4000
iterations, two user-mode `#PF` events fired.  Each printed the
full register and stack diagnostic; each killed only the faulting
child; each made the shell print `Segmentation fault`; and the
loop continued to completion.  **That is the semantics the project
has been working toward since session 45** -- a user fault kills
the process, the parent learns about it, and the machine keeps
running.

`exec_churn` has two uses: it exercises the process-exit page-table
teardown, and it is a 24-round ELF-load stress.

**The canary is a program: `canary`.**

    canary          # read-only rows
    canary --full   # also the mutating rows (create/remove under /)

**What stays manual:** interactive `busybox ash`, `vi test`.

**Regression tests (`userland/musl/tests/`, not canary rows):**

    at_step1 at_step2 envp_step1 musl_exec2 fcntl_lowfd
    readlink_errno proc_status proc_fd proc_dir proc_stat
    proc_walk proc_walk_fds mmap_stress exec_churn
    sha512_probe

**Pipe regression suite:**

    pipe_step1 pipe_step2 pipe_step3 pipe_step3b

Run these when changing `sys_read`/`sys_write`/`sys_close`/
`put_file_slot`/`sys_fork`/`sys_pipe` or adding a `FILE_KIND_*`.

**The harness:**

    test.sh        # ~58 applet rows, staged at /root/scripts/test.sh

**It is not a canary.**  It takes minutes.  **It passes now** --
item 7c is retracted, the `find -not` row uses `!`, and `ran`'s
failure path no longer forks.

    sh /root/scripts/test.sh

**New ELFs must be added to both `USERLAND_ELFS` and the
`mcopy_one` chain in `05_boot_kernel64/Makefile`; `test.sh` is
staged by the directory rule, not a per-file list.**  The image
stages 49 files plus the two `/etc` entries and the scripts.

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`.

**Expected noise:** no `Unknown syscall:` lines; no `[fd]` lines;
no `[a|b|c]` debug line.  The `FB: mapped N pages ...` line is
expected.  `WW:` lines appear on wake-path activity (item 7d's
trace, kept as a diagnostic); they are not noise, they are the
item-7d instrument.  **A `Segmentation fault` line is now expected
when a user process faults** -- it is the honest exit-status
change (session 54).  It is not noise; it is the report.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **`open-issues.md` item 12: signal delivery is a stub.**  The
   largest target; unblocks `kill`, ash job control, and the
   Wayland `SIGBUS` path.
2. **`open-issues.md` item 10: `CR4.OSFXSR` without an XMM save.**
   Filed session 52; latent; the fix is a kernel feature.
3. **`open-issues.md` item 13: `CONFIG_ASH_JOB_CONTROL` gap.**
   Same signal subsystem as item 12, plus process groups and a
   controlling terminal.
4. **`open-issues.md` item 9: no privilege model.**  uid/gid are
   0; the session that adds one changes all five identity
   syscalls.
5. **`sys_gettimeofday` (99) is not implemented**, so `ps -l` is
   off.  Small.

**Item 7 is closed** (session 49, shipped in `v0.6.12`).  **Item
7c is retracted** (session 52 -- it was a test bug).  **Item 7d is
fixed** (session 52, the scheduler fix and the kept `WW:` trace).
**Item 7a** is the boot-time `#PF` at `0x400000`; **7b** is the
`#GP` at `0x42F1A7`; **7e** is **closed as a busybox bug** (session
54); **item 8** is symlinks; **item 9** is the privilege model;
**item 10** is the SSE/CR4 gap; **item 11** is **closed** (session
54); **item 12** is signal delivery (new); **item 13** is the ash
job-control gap (new).

---

## State on disk

**Real project root:** `/home/noneya/code/donix/`.

**The FAT layout:**

    /            HELLO-WORLD.TXT and other data
    /bin         busybox
    /usr/bin     the donix-native ELFs (apps + tests), staged BARE
    /tmp         empty
    /etc         passwd, group
    /root        scripts/test.sh, scripts/pipe_wake_probe.sh
    /home        empty
    /dev         empty (a real FAT directory; the device entries
                 are synthesized by the seam, not stored here)
    /var         empty

- `configs/busybox.config` -- tracked canonical busybox config.
  **`sha512sum` is enabled and correct.**  **`FEATURE_FIND_NOT` is
  on**, but `-not` is a GNU spelling gated behind `ENABLE_DESKTOP`
  (off), so the applet accepts `!` only.
- `userland/musl/` -- tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.  The Makefile's `CFLAGS` carries the
  `-mno-sse*` stopgap and both `%.elf` rules have `Makefile` as a
  prerequisite.  **Session 54 did not change this file.**
- `userland/scripts/` -- tracked shell scripts staged to
  `/root/scripts/`.  `test.sh` was fixed in session 53
  (`d2ad311`).  `pipe_wake_probe.sh`'s iteration count is 4000
  (session 54).
- `04_kernel_64bit/fonts/ter-u18n.psf` -- tracked font source.
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  -- gitignored; rebuild with `./toolchain/install_musl.sh`.
  **Do not edit these.**
- `toolchain/{install_musl.sh,musl-gcc.sh}` -- tracked.
  **`musl-gcc.sh` invokes the host gcc with the musl specs file;
  it does not restrict the ISA.**  See item 10.
- `PFcapture.txt`, `DFAULT.txt`, `capture.txt`, `qemu-int.log` --
  gitignored captures.
- `run` -- tracked; the build-and-capture fix lives here, and
  session 54 added a commented-out `-d int` QEMU invocation as
  documentation.

Kernel sources: `04_kernel_64bit/`.

---

## Where things live

Not needed to start a session; ask for a file when the current task
needs it.  Paths relative to the tree root.

- `docs/strategy.md` -- Phase A/B plan, rules, tagging convention,
  git hygiene, recovery.
- `docs/gotchas.md` -- every bug writeup, by subsystem, newest
  first.  **Session 54 added two: "a halt in a fault handler is a
  diagnostic that does not run" and "an exit path that does not
  close the file table leaks a reference per exit."**
- `docs/session-log.md` -- commit tables and per-test canary notes,
  newest first.  Session 54's section is at the top.
- `docs/open-issues.md` -- full open-issues list.  **Item 11 and
  item 7e are closed (session 54); items 12 and 13 are new.**
- `docs/migration-history.md`, `docs/dons-os-history.md` --
  historical narrative.
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` -- the first two
  frozen at `v0.6.0`; `LLD_BUG_REPORT.md` current.
- `ROADMAP.md` -- future work only.  Sessions 52, 53, and 54 have
  no `ROADMAP.md` section.
- `README.md` -- reviewed at the `v0.6.11` bump; **review it at the
  next `v*` bump.**
- `run` -- tracked; the build-and-capture fix lives here.

---

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  `v0.6.7` shipped the shell and the framebuffer;
`v0.6.8` the `*at()` family; `v0.6.9` envp, the `/usr/bin` layout,
the `execve` shim removal; `v0.6.10` `realpath`, the `readlink`
errno, and `/dev/null`; `v0.6.11` the pathname dispatch seam,
`/proc` per-pid and `ps`, and the PMM zone-scan fix; **`v0.6.12` a
correctness milestone** -- item 7's silent `vmm_map_page*` returns
closed, `process_create`'s failure exits fixed, the exit-path
page-table leak closed.  **Session 51** enabled two applet batches,
built the standard FAT directory tree, fixed `isatty`, added the
four missing identity syscalls, and wrote `test.sh`.  **Session
52** retracted item 7c (`sha512sum` was correct; the harness's
expected value was `abc`'s digest) and filed item 10.  **Session
53** found a reproducible zombie leak, corrected the `find -not`
row, wired the `g_last_sysret` diagnostic, and fixed `ran` to not
fork.  **Session 54** closed item 11 (the zombie leak was
`process_exit`'s missing `close_all_files`, not the lost pipe-EOF
wake); closed item 7e as a busybox bug (the kernel returns
correctly every time; the process jumps to a data value); fixed
the NX / non-canonical address family (`PT_NX` masking,
`PROT_EXEC`, NX on split PTEs not the PDE, the `pte_phys`
helper); made user faults honest (the fault walk no longer
halts; `fault_signal` + `sys_wait4` report a signal-kill status,
and the shell prints `Segmentation fault`); and verified the
whole thing by 4000 iterations of `pipe_wake_probe.sh`
completing with `loopdone`.  **The recommended next session is
item 12 -- signal delivery -- the largest and most-reaching
target, or `sys_gettimeofday` (99) if a small clean change is
preferred.**  See NEXT SESSION.  One change at a time.**

---

## How to use this file

At session start, paste this file and say "Continue from here and
request any files you need."  Run the four `git` commands in the
repo-state block -- they are the state; this file is the narrative.

At session end, **rewrite the narrative**: what changed, what is
next, what is tabled.  Do **not** write repo state -- no HEAD, no
ahead/behind count, no tag list, no dirty/clean claim.  State is
what git is for; a hand-written state line is wrong the moment the
commit that writes it lands.  New gotchas go to `docs/gotchas.md`;
new commit rows go to `docs/session-log.md`; new open issues go to
`docs/open-issues.md`.  This file never grows.  Name commits by tag
only, never by SHA.

**When a session's findings change an earlier numbered step, edit
the step in place -- do not just add a paragraph above it.**

**Before proposing any command block, read the "Working style"
section at the top.**

**Ten gotchas worth reading before the next change.**  "A fix
with no test is indistinguishable from an unfixed defect."  "A
test can encode an earlier version's behavior."  "A consumer
inferred from behavior is not a consumer."  Session 45's: a fix
can fail to close the thing it claims to close, and an intermittent
fault that stops reproducing is not fixed -- it is unobserved.
Session 49's: **a function that has never run is correct by
inspection only.**  Session 51's two: **a whole-file return for a
large file with non-ASCII content re-encodes the file -- insert a
block and check `git diff --stat`**; and **a test that cannot say
where it stopped cannot say much -- print the row marker *before*
the row runs.**  Session 52's: **a test's expected value is a
claim -- check it against a known-good source before you check the
code.**  Session 53's: **a diagnostic that is declared but never
wired is not a diagnostic -- grep for the writer and the reader.**
Session 54's two: **a halt in a fault handler is a diagnostic
that does not run -- every `hlt` in a fault handler is a
diagnostic below it that will never print**; and **an exit path
that does not close the file table leaks a reference per exit --
put the cleanup in the teardown, not in each entry point.**
**This file's item text is the same kind of claim.**
