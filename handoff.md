Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-03 (session 51: two applet batches
enabled, the standard FAT directory tree built, `/etc/passwd` and
`/etc/group` staged, and the first-party harness `test.sh` written
and run.  The harness found two bugs with reproducers -- a
deterministic wrong `sha512sum` digest and a racy lost wakeup in a
chain of command substitutions -- plus one corrected test
expectation.  **Next: `sha512sum` (item 7c), which is deterministic
and therefore debuggable.**)

**Repo state -- run these; do not write it here.**  A header that
names a commit or a tag count is wrong the moment the same commit
lands, so this file does not carry one:

    git log --oneline -1            # HEAD
    git status -sb                  # branch, dirty?, ahead/behind
    git tag --list 'v*' | tail -1   # last milestone
    git tag --list '2026*'          # live scratch tags

> **The session's real result is a harness, and the bugs it
> found.**  `test.sh` (staged at `/root/scripts/test.sh`, source at
> `userland/musl/tests/test.sh`) runs each enabled applet through a
> command substitution and prints `[N] run  <name>` **before** every
> row, so a hang names its own row.  Six runs of the same script
> without that trace would have left "it hung somewhere" as the
> whole record.
>
> It found:
>
> - **`sha512sum` computes a WRONG digest for `ABC`.
>   Deterministic.**  `printf ABC | sha512sum` produces
>   `397118fd...` every time, at the interactive prompt and in a
>   script, through a plain pipe.  `md5sum`/`sha1sum`/`sha256sum`
>   are all correct on the same input.  **An applet bug** --
>   `open-issues.md` item 7c.  Suspect block size.
> - **A chain of command substitutions can lose a wake.  Racy.**
>   The shell blocks, the scheduler falls to `EXIT-FALLBACK`, and
>   no process reads the keyboard; only a reboot recovers.
>   **Confirmed racy by a controlled experiment:** the same image,
>   no rebuild between runs, ran to row 54 on one boot and hung at
>   row 29 on the next.  Seven runs, seven different hang rows.
>   **A kernel bug** in the wait/pipe wake path -- `open-issues.md`
>   item 7d.
>
> Neither is a candidate for a quick fix; both are recorded with
> reproducers.  See NEXT SESSION.
>
> **A note on docs edits, learned the hard way in session 51.**
> `docs/session-log.md` is ~1600 lines with non-ASCII characters
> (em-dashes).  Returning the **whole file** for an edit re-encodes
> every one of them and shows up as a large deletion count in
> `git diff --stat`.  For an edit to a large file with non-ASCII
> content: **insert a block, do not return the whole file.**  A
> copy-block insertion at one anchor leaves the existing bytes
> alone.  **Check `git diff --stat` before committing: a docs
> insertion should show zero deletions** (a *replacement* shows
> both, and that is correct for a replacement).

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

**Session 51 is untagged work on `dev`, after `v0.6.12`.**  Five
commits: the item-7a instrument, the gzip fix, the applet batch,
the image tree + harness, and a session-log rewrite.  Two docs
commits ride untagged.  No `v*` bump; the banner still reads
`v0.6.12`.

Commits are named by tag only, never by SHA.  **Working tags
(`YYYYMMDD-*`) are local scratch restore points** -- they exist while
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

**Four older tags are present and are not ours.**  `git tag` also
shows `Useo64Sysret_usermode_return`, `elfload-baseline`,
`restore_userland_shell_memory_refactor`, and `ring3_sysret_stable`
-- pre-convention restore points, older than the `YYYYMMDD-*`
scheme.  They are not `2026*` scratch tags and not `v*` milestones.
**Do not drop them as part of a session cleanup**; they are not
this or any recent session's to delete.  Leave them alone.

**A plain commit can exist with no tag.**  Session 45's `run:` fix
is one.  Session 51's two docs commits are the same -- docs-only,
no tag.  Such commits ride along on `dev` until the next `v*` bump.
Do not invent a tag for one, and do not be surprised by a commit
with no tag.

**Note on commit messages:** a commit message is a claim, not a
fact.  Read the diff, not the subject.  Two instances to remember.
Session 49's commit `20261003-process-create-cleanup` says "correct
by inspection; UNEXERCISED" -- honest when written, and now
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
  session 51: a whole-file return of `docs/session-log.md` produced
  `252 insertions(+), 115 deletions(-)` for what should have been a
  ~130-line insertion.  The fix is a copy-block insertion that
  touches only the new lines.  **Check `git diff --stat` before
  committing: a docs insertion should show zero deletions.**  A
  full *replacement* of a section correctly shows both insertions
  and deletions; that is the difference.

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
edits described from memory rather than quoted from the file -- the
fix each time was to read the current bytes and quote them.  A
whole-half or whole-file return is the safe form; a one-line
insertion into a 6500-line file is the form that breaks.

**A redirection binds to the last command in an `&&` chain.**
Session 45 added this.  `a && b && c && d > file` sends only `d`'s
output to `file`; `a`, `b`, and `c` write to the terminal.  The
`run` script's build line used this form, so every capture it
produced was missing the front of the build -- including whether the
kernel was rebuilt at all.  Half a session was spent comparing
binaries whose build log had been silently discarded.  When the
whole chain's output matters, wrap it:
`{ a && b && c && d ; } 2>&1 | tee file`.  See `docs/gotchas.md`.

**A capture file is one run.  Truncate, do not append.**  Session
51 used `>>` once for `capture.txt`; two runs landed in one file
and briefly read as one long run.  `>` per run.

**Do not edit `third_party/`.**  Session 47 added this.  The
vendored sources there are gitignored and rebuilt by the toolchain,
so an edit is invisible to the repo and vanishes on the next build.
When a diagnostic needs to see inside a third-party applet -- a
`printf` in busybox, say -- the right instrument is a **first-party
test that reproduces the applet's sequence** (e.g. `proc_walk` for
`procps_scan`, or `test.sh` for the applet set), or a **trace in
our own kernel**, not a patch to `third_party/`.  Both are
committable; the patch is not.

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
each version (`Merge dev into main for v0.6.12`, etc.).

**A version is not necessarily a milestone.**  `v0.6.10` is a tail
on `v0.6.9` with no new subsystem.  When that happens, the tag
annotation and the session-log row should *say so*, so a future
reader does not hunt for a milestone narrative that is not there.
**`v0.6.12` is a correctness milestone, not a feature one** -- the
annotation should say so plainly.  Session 51 is the same shape:
five commits, no new subsystem, no `v*` yet.

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

---

## Where we are -- session 51, after `v0.6.12`

**Five commits on `dev`, four scratch-tagged, two docs commits
untagged.**

### The item-7a instrument (`20261003-vmm-map-diag`)

`vmm_map_page_in_cr3` has **four** page-table allocation sites.
The huge-page split's own failure already halts loudly
(`VMM: FATAL page-table alloc failed; cannot split huge page`) and
is unchanged.  The other **three** were bare `if (!new_*_phys)
return -1;` -- PDPT, PD, and the final PT *after* the split block.
The commit makes each print one line before returning `-1`:

    VMM: map failed site=PDPT virt=0x... cr3=0x... free=NNN
    VMM: map failed site=PD   virt=0x... cr3=0x... free=NNN
    VMM: map failed site=PT   virt=0x... cr3=0x... free=NNN

**The PT site is the one to be careful about.**  It shares the
pointer name `new_pt_phys` with the split's already-loud block, so
a scan for `if (!new_*_phys) return -1;` finds two at a glance and
the third only by reading past the split.

**The `free` field** is what separates item 7a's two candidates --
a healthy count with `site=PDPT`/`site=PD` means the child's clone
was missing that table (a clone-correctness fix); a near-zero
count means genuine exhaustion (an allocator fix).

**The diagnostic is failure-branch only and did not fire.**  The
item-7a `#PF` did not reproduce.  The session-48 result: keep the
instrument, wait.

### The gzip fix (`20261003-gzip-ioctl`)

`sys_ioctl`'s `TCGETS`, `TIOCGWINSZ`, and the `TCSETS*`/
`TIOCSWINSZ` ignore-case each tested the fd **number** --
`if (fd != 0 && fd != 1 && fd != 2) return -ENOTTY;` -- not what
the fd *is*.  fd 0 can be a regular file: `gunzip FILE` puts FILE
on fd 0 and then asks `isatty(0)`, and busybox's bbunzip guard
fired on a named file, so `gzip FILE` failed without `-f`.

The fix is `fd_is_console(fd)`: ask the file table whether the
slot's kind is `FILE_KIND_CONSOLE`.  A file on fd 0 now gets
`-ENOTTY`, which is the honest answer.  `gzip`/`gunzip` work
without `-f`; `tty`/`stty`/`ash` on the console are unaffected.

**Why kernel-side and not a `third_party/` patch:** the honest test
for "is a tty" belongs in `sys_ioctl`, and it is the same answer
for every applet that asks.

### The applet batches (`20261003-applets`, `20261003-applets-harness`)

**Batch 2:** 22 applets and 9 feature flags (`cksum`, `crc32`,
`comm`, `expand`, `unexpand`, `expr`, `fold`, `id`, `groups`,
`logname`, `md5sum`, `sha1sum`, `sha256sum`, `nl`, `paste`,
`printf`, `split`, `tac`, `base64`, `whoami`, `rev`, `hexdump`;
fancy `echo`/`head`/`tail`/`sleep`, `wc` large, `find -maxdepth`/
`-not`, `grep -A/-B/-C`, `test2`).

**Batch 3:** 20 more applets (`sum`, `uuencode`/`uudecode`,
`base32`, `sha512sum`, `sha3sum`, `shuf`, `strings`, `tree`,
`tsort`, `nohup`, `dos2unix`/`unix2dos`, `which`, `hostid`,
`reset`, `egrep`/`fgrep`, `pidof`, `ascii`) and 18 feature flags
(`sort`/`split`/`find` options, ash `alias`/`getopts`/`help`/
`$RANDOM`/`$(( ))`, tab completion, resize reflow).

**The image gains the standard Unix directory shape:** `/etc`,
`/root`, `/root/scripts`, `/home`, `/dev`, `/var`, alongside
`/bin`, `/usr`, `/usr/bin`, `/tmp`.

**`/etc/passwd` and `/etc/group`** are staged by the Makefile with
one `root` entry each.  With them present, `id` prints
`uid=0 gid=0`, `id -un` prints `root`, `groups` exits 0, `whoami`
prints `root`, and `ps`'s USER column resolves the uid to a name.

### The identity syscalls, and why uid/gid is 0

`getuid` (102), `getgid` (104), `getegid` (108), and `getgroups`
(115) were missing, so `id` printed three errno values and one real
value (`euid=1000`, a fixed value chosen in session 30 to silence a
diagnostic).  The session adds the four, all returning 0, and
**changes `geteuid` from 1000 to 0**, so all four agree.

**Why 0 and not 1000:** donix has no privilege model -- no
per-process uid/euid split, no setuid bit (FAT has no mode bits),
no `chown`, no way to become root.  0 is the honest answer for a
single-user system that runs as root; 1000 would make root checks
fail with no sudo to fix them.  **TEMPORARY.**  `open-issues.md`
item 9.

### The harness, and the two bugs it found

`userland/musl/tests/test.sh`, staged at `/root/scripts/test.sh`.
It runs each enabled applet through a command substitution,
asserts known values, prints `[N] run  <name>` before each row,
and prints `DONE (N rows)` at the end.  The trace is the design
point: a hang names its own row.

**`sha512sum` computes a wrong digest** -- deterministic, every
run, at the prompt and in a script, through a plain pipe.  The
other three checksums are correct on the same input.  **An applet
bug** -- `open-issues.md` item 7c.

**A chain of command substitutions can lose a wake** -- racy;
same image, no rebuild, ran to row 54 on one boot and hung at row
29 on the next.  Seven runs, seven different hang rows.  **A
kernel bug** in the wait/pipe wake path -- `open-issues.md` item
7d.

**A test-expectation error, corrected:** `sleep 0.1` fails because
`FEATURE_FANCY_SLEEP` is off, so busybox `sleep` accepts integers
only.  The row is now `sleep 1`.

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
one-directional scan, not item 7.  `pmm_scan_zone` now wraps.

### Sessions 49 and 50 -- the failure-path work

Session 49 closed item 7 (the silent `vmm_map_page*` returns), fixed
`process_create`'s failure exits, and closed the exit-path
page-table leak.  Session 50 added the fault-injection hooks
(`pmm_debug_fail_next_of_type`, `process_debug_fail_next_stack_slot`)
and the `create_fail` selftest row, so **three of `process_create`'s
four failure exits now run** -- exit 3 is still by inspection.

### Session 45 -- the one thing it left

Session 45 attempted the item-7 fix three ways and reverted all
three.  Its **fault is the surviving artifact**: a user-mode `#PF`
at virtual 1 (`CR2 = RIP = 0x1`, error `0x15`, `pte = 0x3` present
/ write / **no user**, phys 1, every page of the walk `PAGE_TABLE`),
observed once and **not seen since**.  **Disposition:
instrumented, not fixed.**  `isr14_handler` prints the four page
types of every `#PF` walk, permanently.  Its one kept commit is the
`run` script's build-capture fix (`6cfb0e6`), untagged on `dev`.

---

## NEXT SESSION -- `sha512sum` (item 7c), which is deterministic

**Pick the deterministic bug first.**  The intermittent family has
no reproducer; `sha512sum` has one and it fails *every time*.  A
bug that fails on demand is debuggable; a bug that appears once a
month is not.

### The target: item 7c, `sha512sum`

**What is known exactly:**

    printf ABC | sha512sum
    -> 397118fdac8d83ad98813c50759c85b8c47565d8268bf10da483153b747a74743a58a90e85aa9f705ce6984ffc128db567489817e4092d050d8a1cc596ddc119  -

The correct SHA-512 of `ABC` is

    ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f

`md5sum`, `sha1sum`, and `sha256sum` are all correct on the same
three bytes through the same pipe.  Only the 512-bit one is wrong.

**Two things to do first, and neither is a code change:**

1. **Find the input that produces `397118fd...`.**  It is the hash
   of *something*; feed candidate inputs until one matches.  Likely
   candidates: `ABC\n`, `ABC `, ` ABC`, a doubled byte, a
   four-byte input.  When one matches, the bug is named -- the
   applet is hashing that, not `ABC`.
2. **Read busybox's SHA-512 implementation** (`third_party/` --
   read, do not edit).  SHA-512 uses 128-byte blocks where SHA-1/
   256 use 64; a block-size or length-field bug would explain why
   only the 512-bit one is wrong.

**If it is an applet bug and not fixable here:** the fix is either
a first-party reproduction that isolates the block handling, or
disabling `CONFIG_SHA512SUM` in `configs/busybox.config` with the
reason recorded.  **Do not patch `third_party/`** -- it is
gitignored and rebuilt.

**Read first:** `configs/busybox.config` (the `SHA512SUM` line),
`userland/musl/tests/test.sh` (row 4 is the assertion), and
`docs/open-issues.md` item 7c.

### Second target, if you want the harder one: item 7d

**The lost command-substitution wake.**  Racy, but there is a
reproducer *shape*: a loop of `x=$(seq 1 3)`.  Run this and see how
many iterations it takes to hang:

    i=0; while [ $i -lt 50 ]; do x=$(seq 1 3); i=$((i+1)); done; echo loopdone

If it hangs within 50, you have a tiny reproducer and the target is
`s`ys_wait4` and `process_wake_parent_if_waiting` together with
`sys_read`'s `FILE_KIND_PIPE` case -- the window between the
child's exit and the parent's transition to `BLOCKED`.  If it does
**not** hang, the trigger needs the longer script, and the harness
stays the reproducer.

**Read first:** `sys_wait4`, `process_wake_parent_if_waiting`,
`sys_read`'s pipe branch, and `docs/open-issues.md` item 7d.

### Do not

Do not re-open item 7 (closed, `v0.6.12`) or the session-48 zone
scan (fixed, `v0.6.11`).  Do not re-litigate the fault-injection
design.  Do not chase the intermittent family (7a, 7b) -- they
have no reproducer and every session that tried produced a
"did not fire" result.  **Do not drop the four older non-`2026*`
tags.**  One change at a time.  Do not edit `third_party/`.

### If you would rather do something small and clean

**`sys_gettimeofday` (99)** -- a few lines from `g_ticks`, like the
existing `sys_clock_gettime` (228).  It unlocks busybox `ps -l` /
`ps -e` (`PS_LONG`, `PS_TIME`), which currently hit
`Unknown syscall: 99`.  Small, patterned, independent.

**Exit 3 of `process_create`** -- the one untested failure exit.  A
`pmm_debug_fail_after(n, type)` hook would reach it.  The commit
must say what it adds and what it still does not reach.

### Candidates after that, none blocking

- **`open("/proc/<pid>", O_DIRECTORY)`** -- the "complete" half of
  session 47's fix.  The `PROC_DIR_SENTINEL` mechanism exists.
- **`/dev` as a listable directory** -- `ls /dev` fails.  Same
  directory shape `/proc` got.  Prerequisite for `/dev/tty` and
  `/dev/urandom`.  **Note: the image now has a real `/dev`
  directory on disk, which makes `ls /dev` succeed but list
  nothing; the synthesized device entries are still the work.**
- **`CONFIG_FEATURE_FANCY_SLEEP=y`** -- makes `sleep 0.1` work.
  Small, and it would let the harness row go back to the fractional
  form if that is wanted.
- **More applets from the "still-off" table** -- the zero-syscall
  ones.
- **Symlinks** (`open-issues.md` item 8) -- larger; design recorded,
  the seam exists to hide the encoding.

---

## Busybox enablement -- state of play

**Enabled and working:** `ps`, `pstree`, `stty`, `tty`
(prints `/dev/console`), `gzip`, `gunzip`, plus everything in
batches 2 and 3 (see the applet list under "State on disk").

### The rule

**Enable an applet only when the syscalls it actually calls are
implemented -- read the applet's source, do not guess from its
name.**  When something is missing, the question is "how big is
it?" -- a table entry and a directory shape is a session's work; a
signal-delivery subsystem is not.  A syscall that exists but
cannot do its job (a `chmod` on a filesystem with no permissions)
is worse than a missing one: it makes the applet lie.

**A `/proc` consumer can stat a path it never opens.**  Session
47's lesson: `procps_scan` stats `/proc/<pid>/` (trailing slash)
under `PSSCAN_UIDGID` and skips the entry when it fails, before
reading any file.  Every per-pid *file* can exist and the applet
still prints nothing.  See `gotchas.md`.

**A real applet finds real bugs.**  Session 51's `gzip` found the
`isatty` bug, and `test.sh` found `sha512sum` and the lost wakeup.
Enabling an applet is a test, and running it is the assertion.
The session-44 comment had *asserted* `isatty` correct; running
gzip *tested* it.

**Do not edit `third_party/`.**  When a diagnostic needs to see
inside an applet, write a first-party test that reproduces its
sequence, or trace our own kernel.  Both are committable.

### A note on `stty`

`stty` runs and prints a plausible state.  **It cannot change the
terminal:** the kernel console has no termios.  The applet reads
and reports; it cannot write.  Not a defect -- a limit.

### What each still-off applet needs

Each row is a **cost estimate, not a prohibition**.  The rule is
**know what you are signing up for** before you enable.

| Config | Applet | Needs |
|---|---|---|
| `CONFIG_DIFF` | `diff` | `mmap` of files (non-anonymous `mmap`); deliberate |
| `CONFIG_CHMOD` | `chmod` | `chmod`/`fchmodat`; FAT has no permissions |
| `CONFIG_CHOWN` | `chown` | `chown`/`fchownat`; FAT has no ownership |
| `CONFIG_LN` | `ln` | `link`/`symlink`; FAT has no links |
| `CONFIG_LINK` | `link` | same |
| `CONFIG_MOUNT`/`UMOUNT` | `mount`/`umount` | `mount` (165); no VFS |
| `CONFIG_HALT`/`POWEROFF`/`REBOOT` | `halt`/`poweroff`/`reboot` | signal delivery (item 5); no init, no ACPI |
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

**Green as of session 51** -- `canary` 15/15 and `canary --full`
28/28 on the clean boot; `selftest` 18/18.

`exec_churn` has two uses: it exercises the process-exit page-table
teardown (`process_free_clone` via `process_reclaim` /
`process_destroy`), and it is a 24-round ELF-load stress.

**The canary is a program: `canary`.**

    canary          # read-only rows
    canary --full   # also the mutating rows (create/remove under /)

**What stays manual:** interactive `busybox ash`, `vi test`.

**Regression tests (`userland/musl/tests/`, not canary rows):**

    at_step1 at_step2 envp_step1 musl_exec2 fcntl_lowfd
    readlink_errno proc_status proc_fd proc_dir proc_stat
    proc_walk proc_walk_fds mmap_stress exec_churn

**Pipe regression suite:**

    pipe_step1 pipe_step2 pipe_step3 pipe_step3b

Run these when changing `sys_read`/`sys_write`/`sys_close`/
`put_file_slot`/`sys_fork`/`sys_pipe` or adding a `FILE_KIND_*`.

**New harness (session 51):**

    test.sh        # ~58 applet rows, staged at /root/scripts/test.sh

**It is not a canary.**  It takes minutes, it fails `sha512sum`
every run (item 7c), and it can hang (item 7d).  Run it by hand:

    sh /root/scripts/test.sh

Expect either a hang or `FAIL sha512sum ABC` on every run until 7c
and 7d are fixed.  Neither is a sign the harness is broken.

**New ELFs must be added to both `USERLAND_ELFS` and the
`mcopy_one` chain in `05_boot_kernel64/Makefile`; `test.sh` is
staged by a plain `mcopy` to `/root/scripts/`, not the
`mcopy_one` chain.**  The image now stages **48 files plus the two
`/etc` entries and the script**.

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`.

**Expected noise:** no `Unknown syscall:` lines; no `[fd]` lines;
no `[a|b|c]` debug line.  The `FB: mapped N pages ...` line is
expected.  The `sys_open: f_open FAIL path=etc/passwd` lines from
`id`/`whoami`/`ps` are expected -- `/etc/passwd` now exists, so
they should be gone; if they appear, something regressed in the
staging.  `sys_execve: f_open FAIL` lines no longer appear.

**Session 51's own noise:** the three `VMM: map failed site=...`
lines appear only on a `vmm_map_page_in_cr3` allocation failure.
If one appears, capture the whole line and treat it as an item-7a
reproduction.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **`open-issues.md` item 7c: `sha512sum` computes a wrong
   digest.**  Deterministic.  The next session's target.
2. **`open-issues.md` item 7d: a chain of command substitutions
   can lose a wake.**  Racy; kernel wait/pipe path.
3. **`open-issues.md` item 9: no privilege model.**  uid/gid are 0;
   the session that adds one changes all five identity syscalls.
4. **`-EPIPE` is delivered without `SIGPIPE`.**  Needs signal
   delivery -- the same subsystem `reboot`/`halt`/`poweroff` need.
5. **`sys_gettimeofday` (99) is not implemented**, so `ps -l` is
   off.  Small.

**Item 7 is closed** (session 49, shipped in `v0.6.12`).  The list
keeps its numbering; **item 7a** is the boot-time `#PF` at
`0x400000` (session 50, instrumented, not fired); **item 7b** is
the `#GP` at `0x42F1A7` (session 51, unobserved); **item 7c** is
the `sha512sum` wrong digest (session 51, deterministic); **item
7d** is the lost command-substitution wake (session 51, racy).
Item 8 is symlinks; item 9 is the privilege model.

**Four faults in one family are unobserved, not fixed:** session
45's virtual-1 `#PF`, session 50's `0x400000` `#PF` (7a), session
51's `#GP` at `0x42F1A7` (7b), and session 51's `#PF` at
`CR2=0x44` / `RIP=0x419FD9` / error `0x5` (a near-null *data read*
in busybox, not yet item-numbered).  All are intermittent and
layout-dependent.  Diagnostics in place: `isr14_handler` prints
the four page types of every `#PF` walk; the item-7a sites print
`site`/`virt`/`cr3`/`free`.  **Standing caution:** do not filter
`vmm_clone_page_table`'s leaf copy on `PT_USER` -- session 45
found it breaks the kernel's own identity map.

---

## State on disk

**Real project root:** `/home/noneya/code/donix/`.

**The FAT layout:**

    /            HELLO-WORLD.TXT and other data
    /bin         busybox
    /usr/bin     the donix-native ELFs (apps + tests), staged BARE
    /tmp         empty
    /etc         passwd, group
    /root        scripts/test.sh
    /home        empty
    /dev         empty (a real FAT directory; the device entries
                 are synthesized by the seam, not stored here)
    /var         empty

- `configs/busybox.config` -- tracked canonical busybox config.
  Enabled applets: `cat`, `cp`, `cut`, `echo`, `env`, `false`,
  `find`, `head`, `ls`, `mkdir`, `mv`, `od`, `printenv`, `pwd`,
  `rm`, `rmdir`, `seq`, `sort`, `stat`, `tail`, `tee`, `test`,
  `touch`, `tr`, `true`, `uname`, `uniq`, `wc`, `yes`, `cmp`,
  `grep`, `sed`, `vi`, `clear`, `basename`, `dirname`, `unlink`,
  `ttysize`, `tty`, `arch`, `mktemp`, `sleep`, `usleep`,
  `truncate`, `realpath`, `stty`, `ps`, `pstree`, `gzip`,
  `gunzip`, `id`, `groups`, `logname`, `whoami`, `md5sum`,
  `sha1sum`, `sha256sum`, `sha512sum`, `sha3sum`, `cksum`,
  `crc32`, `sum`, `base64`, `base32`, `uuencode`, `uudecode`,
  `comm`, `expand`, `unexpand`, `expr`, `fold`, `nl`, `paste`,
  `printf`, `split`, `tac`, `rev`, `hexdump`, `shuf`, `strings`,
  `tree`, `tsort`, `nohup`, `dos2unix`, `unix2dos`, `which`,
  `hostid`, `reset`, `egrep`, `fgrep`, `pidof`, `ascii`, plus
  `ash`.  **`sha512sum` is enabled but computes a wrong digest --
  item 7c.**
- `userland/musl/` -- tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.  Includes `tests/test.sh` (session 51).
- `04_kernel_64bit/fonts/ter-u18n.psf` -- tracked font source.
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  -- gitignored; rebuild with `./toolchain/install_musl.sh`.
  **Do not edit these.**
- `toolchain/{install_musl.sh,musl-gcc.sh}` -- tracked.
- `PFcapture.txt` -- the session-45 fault capture.  Gitignored;
  `ls` it, `git status` will not show it.
- `DFAULT.txt` -- the session-48 double-fault capture.  Gitignored.
- `capture.txt` -- session 51's captures.  Gitignored.  **Truncate
  per run; do not append (`>>`) -- two runs in one file read as
  one run.**
- `run` -- tracked; the build-and-capture fix lives here.

Kernel sources: `04_kernel_64bit/`.

---

## Where things live

Not needed to start a session; ask for a file when the current task
needs it.  Paths relative to the tree root
(`/home/noneya/code/donix/`).

- `docs/strategy.md` -- Phase A/B plan, rules, files-not-to-touch,
  tagging convention, git hygiene, recovery.
- `docs/gotchas.md` -- every bug writeup, by subsystem.  **Session
  51 added no gotcha** -- the encoding lesson is in this file's
  Working style; the harness findings are open issues.  Session 49
  added two entries; sessions 48, 47, 46, 45 each added one.
- `docs/session-log.md` -- commit tables and per-test canary notes.
  Session 51's section is at the top; then 50's, 49's, 48's, 47's,
  46's, and 44's.  Session 44's section is **misplaced** (after
  session 34).  There is no session-45 section.
- `docs/open-issues.md` -- full open-issues list.  Items 7a, 7b,
  7c, 7d in the gap where item 7 was; item 8 symlinks; item 9 the
  privilege model.  The "Test-design notes" section at the bottom
  is **misplaced** (it is instructions, not issues) and is flagged
  for a move to this file's canary section.
- `docs/migration-history.md`, `docs/dons-os-history.md` --
  historical narrative.
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` -- the first two
  frozen at `v0.6.0`; `LLD_BUG_REPORT.md` current.
- `ROADMAP.md` -- future work only.  Session 51 has no `ROADMAP.md`
  section -- it is not a milestone.
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
page-table leak closed.  **Session 51** enabled two applet batches
(42 applets and 27 feature flags), built the standard FAT directory
tree with `/etc/passwd` and `/etc/group`, fixed `isatty` so `gzip`
works without `-f`, added the four missing identity syscalls, and
wrote the first-party harness `test.sh` -- which found a
**deterministic** wrong `sha512sum` digest (item 7c) and a
**racy** lost command-substitution wake (item 7d).  **The
recommended next session is item 7c, the `sha512sum` bug**, because
it is deterministic and therefore debuggable.  See NEXT SESSION.
One change at a time.**

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

**Seven gotchas worth reading before the next change.**  "A fix
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
the row runs.**
