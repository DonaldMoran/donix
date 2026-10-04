Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-04 (session 52: a wrong constant in the
harness.  `test.sh`'s `sha512sum` row expected the SHA-512 of
lowercase `abc` for the three bytes `ABC`, so the row failed
forever and a session was spent reading correct sha512 code.  The
row is corrected, item 7c is **retracted**, and the one real
finding from the chase is filed as **item 10**: `CR4.OSFXSR` is
set but the kernel saves no XMM state.  **Next: pick a real
target -- item 10 is filed and large; items 7a/7b/7e are the
intermittent family and need boots; the small independent items
are in "If you would rather do something small and clean."**)

**Repo state -- run these; do not write it here.**  A header that
names a commit or a tag count is wrong the moment the same commit
lands, so this file does not carry one:

    git log --oneline -1            # HEAD
    git status -sb                  # branch, dirty?, ahead/behind
    git tag --list 'v*' | tail -1   # last milestone
    git tag --list '2026*'          # live scratch tags

> **The session's real result is a correction, and the lesson is
> about this file.**  Session 51 filed item 7c as "`sha512sum`
> computes a WRONG digest for `ABC`", quoting the expected value
> `ddaf35a1…` and the produced value `397118fd…`.  Session 52
> read that sentence, believed the expected value, and spent the
> session reading `sha512_begin`/`hash`/`end`/
> `sha512_process_block128`, the applet's dispatch and read loop,
> the swap macros, and `rotr64` — all correct — before anyone
> checked the constant.  `ddaf35a1…` is the FIPS 180-4 vector for
> lowercase `abc`; `397118fd…` is the correct SHA-512 of the
> three bytes `0x41 0x42 0x43`.  **The row had been comparing
> `ABC`'s digest against `abc`'s digest.**
>
> Settled by one command on the fedora host:
>
>     echo -n abc | sha512sum   # ddaf35a1...4ca49f
>     echo -n ABC | sha512sum   # 397118fd...dc119
>
> Four producers agree on both values: the busybox applet on
> donix, `userland/musl/tests/sha512_probe.c` on donix, the same
> implementation on the fedora host, and `sha512sum` on the fedora
> host.
>
> **The rule this file must carry forward: a handoff's item text
> is a claim, like a commit message.**  Item 7c stated two things
> — that `sha512sum` was wrong, and that `ddaf35a1…` was the
> correct value for `ABC`.  The session read the first, believed
> the second, and spent its time on the first.  When the next
> session reads an item here, and the item quotes an expected
> value, **check the value against a known-good source before
> acting on the claim it supports.**

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

**Sessions 51 and 52 are untagged work on `dev`, after `v0.6.12`.**
Session 51: five commits -- the item-7a instrument, the gzip fix,
two applet batches, the image tree and harness, and a session-log
rewrite.  Session 52: three commits -- the `sha512sum` correction
and item-7c retraction, the gotcha, and open-issues item 10.  No
`v*` bump; the banner still reads `v0.6.12`.

Commits are named by tag only, never by SHA.  **Working tags
(`YYYYMMDD-*`) are local scratch restore points** -- they exist while
a milestone is being developed and are **dropped before the
milestone is pushed**.  Only `v*` tags go to the remote and are
permanent.  The commit record is `docs/session-log.md`; the commit
message carries the narrative.  Once a scratch tag is dropped it
resolves to nothing; do not cite one as if it were a stable
reference.

**A plain commit can exist with no tag.**  Session 45's `run:` fix
is one.  Sessions 51's and 52's docs commits are the same --
docs-only, no tag.  **All three of session 52's commits are
untagged.**  Such commits ride along on `dev` until the next `v*`
bump.  Do not invent a tag for one, and do not be surprised by a
commit with no tag.

**Note on commit messages, and now on this file too:** a commit
message is a claim, not a fact.  Read the diff, not the subject.
**This file's item text is the same kind of claim** -- session 52
exists because session 51's item 7c was believed.  Two instances
to remember from earlier sessions.  Session 49's commit
`20261003-process-create-cleanup` says "correct by inspection;
UNEXERCISED" -- honest when written, and now **superseded for
three of its four exits** by session 50's `20261003-fail-inject`;
exit 3 is still untested and its commit says so.  And session 45's
Fix B (`157627c`, later reverted) claimed to close the boot-time
`#PF`; it did not.

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
  deletions; that is the difference.  Session 52 inserted into
  `gotchas.md`, `open-issues.md`, and `session-log.md`, and each
  insertion showed insertions only -- the check was run each time.

**Ask for source you do not have.**  The assistant does not have
direct file access.  Before patching a file whose current contents
it has not seen in this session, it must **ask for that file**.
Never guess at a file's contents, never patch from memory of an
earlier version, never assume a file is unchanged.  This is how
stale patches and reverted work have been avoided.  Session 52
asked for and was given `test.sh`, `open-issues.md`,
`gotchas.md`, `session-log.md`, `userland/musl/Makefile`, and
`05_boot_kernel64/Makefile` before writing into any of them.

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
Sessions 51 and 52 are the same shape: no new subsystem, no `v*`
yet.

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

## Where we are -- session 52, after `v0.6.12`

**Three commits on `dev`, all untagged, no scratch tags.**

### Item 7c is retracted (`b9f78f4`)

The harness expected the SHA-512 of lowercase `abc` for the three
bytes `ABC`.  The expected value was wrong.  `sha512sum` is
correct.  The row is corrected, the "KNOWN BUGS" header entry and
the "EXPECTED TO FAIL" comment are gone, and
`userland/musl/tests/sha512_probe.c` is in the tree as the
independent implementation that settled it.

### Item 10: `CR4.OSFXSR` without an XMM save (`4f183b2`)

The kernel sets CR4.OSFXSR (bit 16) at `kmain.c:37-40`, which
tells the CPU that the kernel saves SSE state on context switch.
It does not: no `fxsave`/`fxrstor`/`xsave` anywhere in
`04_kernel_64bit/`, and `context_switch.asm` saves no XMM
registers.  Userland compiled by `musl-gcc.sh` can emit SSE --
before the session's CFLAGS change, `sha512_probe.elf` had 543 XMM
instructions and `busybox.elf` 485.

The session added `-mno-sse -mno-sse2 -mno-avx -mno-mmx` to the
userland CFLAGS as a **stopgap**, not a fix.  The fix is
kernel-side `FXSAVE`/`FXRSTOR` on the switch and interrupt paths.
No test currently fails because of this; it is latent.

### The gotcha (`5b2245b`)

`gotchas.md` gained "a test's expected value is a claim, like any
other."  Tell: a failure that is deterministic, specific, and
identical across every input path is more often a wrong reference
value than a wrong algorithm.

### The intermittent family, condensed

**7a** is the boot-time `#PF` at `0x400000` (session 50,
instrumented, not fired).  **7b** is the `#GP` at `0x42F1A7`
(session 51, unobserved).  **7e** is the intermittent `#GP`/`#PF`
control-flow family in fork-heavy workloads (session 51, four
captures, a cross-vector register fingerprint: `r8 = 0x415516`,
`r9 = 0x2F2F2F2F2F2F2F2F`).

**Four faults in one family are unobserved, not fixed:** session
45's virtual-1 `#PF`, session 50's `0x400000` `#PF` (7a), session
51's `#GP` at `0x42F1A7` (7b), and session 51's `#PF` at
`CR2=0x44` / `RIP=0x419FD9` / error `0x5`.  All are intermittent
and layout-dependent.  Diagnostics in place: `isr14_handler`
prints the four page types of every `#PF` walk; the item-7a sites
print `site`/`virt`/`cr3`/`free`; `isr13` and `isr14` both dump
the register frame.  **Standing caution:** do not filter
`vmm_clone_page_table`'s leaf copy on `PT_USER` -- session 45
found it breaks the kernel's own identity map.

### The two older milestones, condensed

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

## NEXT SESSION -- pick a real target

**Item 7c is retracted.  There is no `sha512sum` bug.**  The
recommendation that opened session 52 was built on a wrong
constant in this file, and the first thing the next session should
internalize is the rule above: **an item here that quotes an
expected value is a claim -- check the value before acting on it.**

### The candidates, in the order the handoff would pick them

1. **Item 10, the SSE/`CR4.OSFXSR` gap.**  Filed this session.
   Large: it is a kernel feature, `FXSAVE`/`FXRSTOR` on the switch
   and interrupt paths.  Not a one-session fix if done carefully,
   but it is the one *known* correctness gap and it has no
   reproducer to chase -- the code either saves XMM state or it
   does not.

2. **Items 7a / 7b / 7e, the intermittent family.**  Fresher, and
   7e is the best-instrumented (a cross-vector register
   fingerprint, four captures).  Intermittent; needs boots.  If
   you take 7e, **start from the fingerprint, not the fault
   addresses** -- the four faulting `RIP`s are downstream of one
   corruption, and the shared `r8`/`r9` pair is the narrow end.

3. **The small independent items** (below).  Each is a short,
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
design.  Do not chase the intermittent family (7a, 7b) without a
reproducer -- every session that tried produced a "did not fire"
result.  **Do not drop the four older non-`2026*` tags.**  One
change at a time.  Do not edit `third_party/`.

---

## Busybox enablement -- state of play

**Enabled and working:** `ps`, `pstree`, `stty`, `tty`
(prints `/dev/console`), `gzip`, `gunzip`, plus everything in
batches 2 and 3.  `sha512sum` **works** -- item 7c was a test bug.

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
found a wrong test.**  Both are valuable.  The rule is: when a
test and an applet disagree, find out which is right, and do not
assume the test is.

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

**Green as of session 51** -- `canary` 15/15 and `canary --full`
28/28 on the clean boot; `selftest` 18/18.

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

**It is not a canary.**  It takes minutes.  **It is expected to
pass now** -- item 7c is retracted and the row is corrected.  It
can still hang on the lost-wakeup race (item 7d is fixed, but the
harness exercises more than one path), so a hang is a finding, not
a known state.

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

1. **`open-issues.md` item 7e: the intermittent `#GP`/`#PF`
   control-flow family.**  Best-instrumented of the intermittent
   faults; start from the register fingerprint.
2. **`open-issues.md` item 10: `CR4.OSFXSR` without an XMM
   save.**  Found session 52; latent; the fix is a kernel feature.
3. **`open-issues.md` item 9: no privilege model.**  uid/gid are
   0; the session that adds one changes all five identity
   syscalls.
4. **`-EPIPE` is delivered without `SIGPIPE`.**  Needs signal
   delivery.
5. **`sys_gettimeofday` (99) is not implemented**, so `ps -l` is
   off.  Small.

**Item 7 is closed** (session 49, shipped in `v0.6.12`).  **Item
7c is retracted** (session 52 -- it was a test bug).  **Item 7d is
fixed** (session 52, the scheduler fix `86ffc7c` and the kept
`WW:` trace `c143cae`).  **Item 7a** is the boot-time `#PF` at
`0x400000`; **7b** is the `#GP` at `0x42F1A7`; **7e** is the
control-flow family; **item 8** is symlinks; **item 9** is the
privilege model; **item 10** is the SSE/CR4 gap.

---

## State on disk

**Real project root:** `/home/noneya/code/donix/`.

**The FAT layout:**

    /            HELLO-WORLD.TXT and other data
    /bin         busybox
    /usr/bin     the donix-native ELFs (apps + tests), staged BARE
    /tmp         empty
    /etc         passwd, group
    /root        scripts/test.sh, scripts/shatest.sh (deleted), scripts/pipe_wake_probe.sh
    /home        empty
    /dev         empty (a real FAT directory; the device entries
                 are synthesized by the seam, not stored here)
    /var         empty

- `configs/busybox.config` -- tracked canonical busybox config.
  **`sha512sum` is enabled and correct.**
- `userland/musl/` -- tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.  **`tests/sha512_probe.c` is new in session
  52.**  The Makefile's `CFLAGS` carries the `-mno-sse*` stopgap
  and both `%.elf` rules have `Makefile` as a prerequisite.
- `userland/scripts/` -- tracked shell scripts staged to
  `/root/scripts/`.  `shatest.sh` was deleted in session 52.
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
  first.  **Session 52 added "a test's expected value is a
  claim".**
- `docs/session-log.md` -- commit tables and per-test canary notes,
  newest first.  Session 52's section is at the top.
- `docs/open-issues.md` -- full open-issues list.  Items 7a, 7b,
  7e, 8, 9, 10; item 7c is retracted in place.
- `docs/migration-history.md`, `docs/dons-os-history.md` --
  historical narrative.
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` -- the first two
  frozen at `v0.6.0`; `LLD_BUG_REPORT.md` current.
- `ROADMAP.md` -- future work only.  Session 52 has no
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
state -- now filed as item 10.  **The recommended next session is
a real target: item 10 (the SSE gap), item 7e (the best-
instrumented intermittent fault, start from the fingerprint), or
one of the small independent items.**  See NEXT SESSION.  One
change at a time.**

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
the row runs.**  Session 52's: **a test's expected value is a
claim -- check it against a known-good source before you check the
code.**  **This file's item text is the same kind of claim.**
