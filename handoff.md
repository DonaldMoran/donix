Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-04 (session 53: a reproducible zombie
leak, a wired diagnostic, and a test row corrected.  Two code
commits and three docs commits on `dev`, all untagged.  **Next:
item 11 -- the `pipe_wake_probe.sh` zombie leak -- is the
clearest target: it has a reproducer, a transition point, and a
place to read.  Item 7e now has a working diagnostic that will
print the sysret target on the next fault.  Item 10 (the SSE gap)
remains filed and large.**)

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
> reading the source.  **Read the function before proposing the
> fix.**  The greps cost seconds; the wrong fixes cost boots.

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

**Sessions 51 through 53 are untagged work on `dev`, after
`v0.6.12`.**  Session 51: five commits -- the item-7a instrument,
the gzip fix, two applet batches, the image tree and harness, and a
session-log rewrite.  Session 52: three commits -- the `sha512sum`
correction and item-7c retraction, the gotcha, and open-issues item
10.  Session 53: two code commits (`96153e6` the `g_last_sysret`
wiring, `d2ad311` the `find -not` row and the no-fork `ran`
failure path) plus three docs commits.  No `v*` bump; the banner
still reads `v0.6.12`.

Commits are named by tag only, never by SHA.  **Working tags
(`YYYYMMDD-*`) are local scratch restore points** -- they exist while
a milestone is being developed and are **dropped before the
milestone is pushed**.  Only `v*` tags go to the remote and are
permanent.  The commit record is `docs/session-log.md`; the commit
message carries the narrative.  Once a scratch tag is dropped it
resolves to nothing; do not cite one as if it were a stable
reference.

**A plain commit can exist with no tag.**  Session 45's `run:` fix
is one.  Sessions 51's, 52's, and 53's docs commits are the same --
docs-only, no tag.  Such commits ride along on `dev` until the next
`v*` bump.  Do not invent a tag for one, and do not be surprised by
a commit with no tag.

**Note on commit messages, and now on this file too:** a commit
message is a claim, not a fact.  Read the diff, not the subject.
**This file's item text is the same kind of claim** -- session 52
exists because session 51's item 7c was believed, and session 53
falsified four mechanisms that were plausible stories read off a
trace.  Two instances to remember from earlier sessions.  Session
49's commit `20261003-process-create-cleanup` says "correct by
inspection; UNEXERCISED" -- honest when written, and now
**superseded for three of its four exits** by session 50's
`20261003-fail-inject`; exit 3 is still untested and its commit
says so.  And session 45's Fix B (`157627c`, later reverted)
claimed to close the boot-time `#PF`; it did not.

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
  deletions; that is the difference.  Session 53 inserted into
  `gotchas.md`, `open-issues.md`, and `session-log.md`, and the
  7e edit was a replacement -- insertions and deletions both, as
  expected.

**Ask for source you do not have.**  The assistant does not have
direct file access.  Before patching a file whose current contents
it has not seen in this session, it must **ask for that file**.
Never guess at a file's contents, never patch from memory of an
earlier version, never assume a file is unchanged.  This is how
stale patches and reverted work have been avoided.  Session 53
asked for and was given `open-issues.md`, `gotchas.md`,
`session-log.md`, `handoff.md`, `process.c`, `interrupts.c`,
`user_syscall_entry.asm`, `user_syscall.c`, and `test.sh` before
writing into any of them.

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
Sessions 51, 52, and 53 are the same shape: no new subsystem, no
`v*` yet.

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

## Where we are -- session 53, after `v0.6.12`

**Two code commits and three docs commits on `dev`, all untagged,
no scratch tags.**

### Item 11: the zombie leak (`open-issues.md`)

`pipe_wake_probe.sh` run past ~107 iterations exhausts the 32-slot
PCB pool.  28 of the 32 slots end up `PROC_STATE_ZOMBIE`, all
children of the script's shell.  The `WW:` trace shows the
transition exactly: `drop=c state=1` (parent not blocked, reap
succeeds) for the first ~90 iterations, then `drop=d kind=2`
(parent blocked on `PIPE_READ`) and the zombie is never reaped.
Candidate mechanism: the lost pipe-EOF wake.  **This is the
clearest next target -- it has a reproducer, a transition point,
and a place to read (`put_file_slot`'s `FILE_PIPE` case in
`user_syscall.c`).**

### Item 7e: the second capture, and the wired diagnostic (`96153e6`)

Two 7e captures now, both `#PF`, both error `0x15` (present +
user + **instruction fetch**), at `RIP = CR2 = 0x1` and
`RIP = CR2 = 0x9`.  Both carry the `r8`/`r9` fingerprint.  Both
addresses are `< 0x1000`, which is the exact signature the
`g_last_sysret_rcx` comment in `user_syscall_entry.asm` names --
and the globals it describes were never written or read until
`96153e6`.  **The next 7e fault will print the sysret target.**

### `find -not` was an `ENABLE_DESKTOP` gate, not a donix bug (`d2ad311`)

The `find -not` row failed every run with `find: unrecognized:
-not`.  `-not` sits inside an `#if ENABLE_DESKTOP` block in
`findutils/find.c`, alongside `-and`/`-or`/`-wholename`.  This
build sets `CONFIG_DESKTOP=n`, so `-not` is not compiled in even
though `CONFIG_FEATURE_FIND_NOT=y` -- that flag controls the
POSIX `!` operator.  The row now uses `!`.

### The `ran` failure path no longer forks (`d2ad311`)

`test.sh`'s `ran` used `echo "$out" | while read` to indent a
failing row's output.  That pipeline forks a subshell, and the
fork can lose its wake -- the shell blocks in `wait4` with no
runnable process, and the scheduler falls to idle.  Replaced with
`printf '%s\n' "$out"`, which does not fork.  The harness now
reaches `57 passed, 0 failed` on a clean run.

### The intermittent family, condensed

**7a** is the boot-time `#PF` at `0x400000` (session 50,
instrumented, not fired).  **7b** is the `#GP` at `0x42F1A7`
(session 51, unobserved).  **7e** is the intermittent `#GP`/`#PF`
control-flow family in fork-heavy workloads (two `test.sh`
captures now, both `#PF`, both `error 0x15`, shared fingerprint).

**Four faults in one family are unobserved, not fixed:** session
45's virtual-1 `#PF`, session 50's `0x400000` `#PF` (7a), session
51's `#GP` at `0x42F1A7` (7b), and session 51's `#PF` at
`CR2=0x44` / `RIP=0x419FD9` / error `0x5`.  All are intermittent
and layout-dependent.  Diagnostics in place: `isr14_handler`
prints the four page types of every `#PF` walk; the item-7a sites
print `site`/`virt`/`cr3`/`free`; `isr13` and `isr14` both dump
the register frame; and, new in session 53, both print the last
`sysret` target when `fault_rip < 0x1000`.  **Standing caution:**
do not filter `vmm_clone_page_table`'s leaf copy on `PT_USER` --
session 45 found it breaks the kernel's own identity map.

### The older milestones, condensed

**The pathname dispatch seam (`v0.6.11`, session 44).**
`resolve_at` returns a backend tag from a path's first component;
FAT / DEV / PROC are selected by it.  No VFS.

**`/proc` per-pid and `ps` (`v0.6.11`, session 47).**  `/proc` is a
listable directory; `ps` and `pstree` work.  The lesson is in
`gotchas.md`.

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

## NEXT SESSION -- item 11 is the clearest target

**The zombie leak has everything a real fix wants: a
deterministic reproducer, a transition point in the trace, and a
specific place to read.**  It is not intermittent in the way the
7e family is -- once the pool is near exhaustion, every iteration
leaks.  The failure is arithmetic.

### The candidates, in the order the handoff would pick them

1. **Item 11, the zombie leak.**  Reproducer:
   `./pipe_wake_probe.sh` past ~107 iterations.  Read
   `put_file_slot`'s `FILE_KIND_PIPE` case in
   `04_kernel_64bit/user_syscall.c` (~lines 1895-1910), where
   `writers_open` drops to 0 and `pipe_wake_waiter` wakes
   `reader_waiting`; then the `close_all_files` path a subshell's
   exit takes, and whether it reaches that wake.  The exit trace
   in the item names the transition.

2. **Item 7e, now with a working diagnostic.**  `96153e6` wires
   the `g_last_sysret` recorder and reporter.  The next 7e fault
   -- whenever one fires, in `test.sh` or a fork-heavy workload
   -- will print the sysret target.  That reading bisects the
   problem: `rcx == 1/9` means the frame was already corrupt at
   the exit path; a sane address means the corruption is
   post-resume.

3. **Item 10, the SSE/`CR4.OSFXSR` gap.**  Large, filed, latent;
   a kernel feature (`FXSAVE`/`FXRSTOR` on the switch and
   interrupt paths).

4. **The small independent items** (below).  Each is a short,
   patterned change with an obvious test.

### If you would rather do something small and clean

**`sys_gettimeofday` (99)** -- a few lines from `g_ticks`, like the
existing `sys_clock_gettime` (228).  It unlocks busybox `ps -l` /
`ps -e` (`PS_LONG`, `PS_TIME`), which currently hit
`Unknown syscall: 99`.  Small, patterned, independent.

**`open("/proc/<pid>", O_DIRECTORY)`** -- the "complete" half of
session 47's fix.  The `PROC_DIR_SENTINEL` mechanism exists.

**`/dev` as a listable directory** -- `ls /dev` fails.  Same
directory shape `/proc` got.  Prerequisite for `/dev/tty` and
`/dev/urandom`.

**`CONFIG_FEATURE_FANCY_SLEEP=y`** -- makes `sleep 0.1` work.
Small.

**More applets from the "still-off" table** -- the zero-syscall
ones.

**Symlinks** (`open-issues.md` item 8) -- larger; design recorded.

### Do not

Do not re-open item 7 (closed, `v0.6.12`) or the session-48 zone
scan (fixed, `v0.6.11`).  Do not re-litigate the fault-injection
design.  **Do not re-read the fork path** -- session 53 read
`sys_fork`'s stack copy, `process_fork_copy_frame`, and
`exec_alloc_user_stack`, and all three are correct.  Do not chase
the intermittent family (7a, 7b) without a reproducer -- every
session that tried produced a "did not fire" result.  **Do not
drop the four older non-`2026*` tags.**  One change at a time.
Do not edit `third_party/`.

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
found a wrong test.**  Session 53 is a third shape: a real applet
found a **missing feature in the config** (`-not` requires
`ENABLE_DESKTOP`).  The rule holds: when a test and an applet
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
| `CONFIG_HALT`/`POWEROFF`/`REBOOT` | | signal delivery (item 5); no init, no ACPI |
| `CONFIG_TAR`/`UNZIP`/`CPIO`/`BZIP2`/`XZ` | archives | `mkdirat`, `symlinkat`, `utimensat` storage, file-backed `mmap`, decompression |
| `CONFIG_AWK` | `awk` | large; needs `FEATURE_AWK_LIBM` |
| `CONFIG_LESS`/`MORE` | pagers | raw-mode terminal control; `/dev/tty` does not exist |
| `CONFIG_TOP` | `top` | `ps -l`-class fields (`gettimeofday`), a redraw loop |
| `CONFIG_KILL` | `kill` | signal delivery |
| `CONFIG_NETWORKING` (all) | `ping`, `wget`, etc. | no network stack |
| `CONFIG_ASH_JOB_CONTROL` | ash job control | signal delivery |
| `CONFIG_FEATURE_FANCY_SLEEP` | `sleep 0.1` | nothing -- the flag itself |

---

## Canary state

**Green as of session 53** -- `canary` 15/15 and `canary --full`
28/28 on the clean boot; `selftest` 18/18; `test.sh` **57 passed,
0 failed** (was 56/1 before the `find -not` row was corrected).

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
failure path no longer forks.  It can still be driven into the
item-11 zombie leak by `pipe_wake_probe.sh`, which is a separate
reproducer.

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
item-7d instrument.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **`open-issues.md` item 11: the `pipe_wake_probe.sh` zombie
   leak.**  Reproducible; has a transition point in the trace and
   a place to read.  The clearest next target.
2. **`open-issues.md` item 7e: the intermittent `#GP`/`#PF`
   control-flow family.**  Two `test.sh` captures now, both
   `error 0x15` at tiny addresses, shared fingerprint.  The
   `g_last_sysret` diagnostic is wired (`96153e6`) and will print
   on the next fault.
3. **`open-issues.md` item 10: `CR4.OSFXSR` without an XMM
   save.**  Filed session 52; latent; the fix is a kernel feature.
4. **`open-issues.md` item 9: no privilege model.**  uid/gid are
   0; the session that adds one changes all five identity
   syscalls.
5. **`sys_gettimeofday` (99) is not implemented**, so `ps -l` is
   off.  Small.

**Item 7 is closed** (session 49, shipped in `v0.6.12`).  **Item
7c is retracted** (session 52 -- it was a test bug).  **Item 7d is
fixed** (session 52, the scheduler fix `86ffc7c` and the kept
`WW:` trace `c143cae`).  **Item 7a** is the boot-time `#PF` at
`0x400000`; **7b** is the `#GP` at `0x42F1A7`; **7e** is the
control-flow family; **item 8** is symlinks; **item 9** is the
privilege model; **item 10** is the SSE/CR4 gap; **item 11** is
the zombie leak.

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
  `build/` gitignored.  `tests/sha512_probe.c` is from session 52.
  The Makefile's `CFLAGS` carries the `-mno-sse*` stopgap and both
  `%.elf` rules have `Makefile` as a prerequisite.
- `userland/scripts/` -- tracked shell scripts staged to
  `/root/scripts/`.  `test.sh` was fixed in session 53
  (`d2ad311`).
- `04_kernel_64bit/fonts/ter-u18n.psf` -- tracked font source.
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  -- gitignored; rebuild with `./toolchain/install_musl.sh`.
  **Do not edit these.**
- `toolchain/{install_musl.sh,musl-gcc.sh}` -- tracked.
  **`musl-gcc.sh` invokes the host gcc with the musl specs file;
  it does not restrict the ISA.**  See item 10.
- `PFcapture.txt`, `DFAULT.txt`, `capture.txt` -- gitignored
  captures.
- `run` -- tracked; the build-and-capture fix lives here.

Kernel sources: `04_kernel_64bit/`.

---

## Where things live

Not needed to start a session; ask for a file when the current task
needs it.  Paths relative to the tree root.

- `docs/strategy.md` -- Phase A/B plan, rules, tagging convention,
  git hygiene, recovery.
- `docs/gotchas.md` -- every bug writeup, by subsystem, newest
  first.  **Session 53 added "a diagnostic that is declared but
  never wired is not a diagnostic."**
- `docs/session-log.md` -- commit tables and per-test canary notes,
  newest first.  Session 53's section is at the top.
- `docs/open-issues.md` -- full open-issues list.  Items 7a, 7b,
  7e, 8, 9, 10, **11**; item 7c is retracted in place.
- `docs/migration-history.md`, `docs/dons-os-history.md` --
  historical narrative.
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` -- the first two
  frozen at `v0.6.0`; `LLD_BUG_REPORT.md` current.
- `ROADMAP.md` -- future work only.  Sessions 52 and 53 have no
  `ROADMAP.md` section.
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
four missing identity syscalls, and wrote `test.sh` -- which
reported a wrong `sha512sum` digest.  **Session 52** found that
report was itself wrong: the harness expected the SHA-512 of
lowercase `abc` for the bytes `ABC`.  **Item 7c is retracted;
`sha512sum` is correct.**  The chase turned up a real latent bug
along the way -- `CR4.OSFXSR` is set but the kernel saves no XMM
state -- now filed as item 10.  **Session 53** found a
reproducible zombie leak (`pipe_wake_probe.sh` exhausts the
32-slot PCB pool with unreaped children -- item 11); found that
`find -not` was a GNU alias gated behind `ENABLE_DESKTOP`, not a
donix bug; wired a declared-but-dark `g_last_sysret` diagnostic
that item 7e's two captures were waiting for; and fixed the
`ran` failure path in `test.sh` to not fork.  **The recommended
next session is item 11 -- the zombie leak -- because it has a
reproducer, a transition point, and a place to read.**  See NEXT
SESSION.  One change at a time.**

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

**Eight gotchas worth reading before the next change.**  "A fix
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
**This file's item text is the same kind of claim.**
