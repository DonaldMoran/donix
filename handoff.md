Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-03 (session 51: the item-7a `#PF` got its
instrument -- the three silent `return -1;` sites in
`vmm_map_page_in_cr3` now name themselves -- and did not fire.  A
new fault appeared while verifying: a user-mode `#GP` at
`0x42F1A7` in busybox, recorded as `open-issues.md` item 7b.
**Next: choose between item 7b (the fresh fault, no instrument yet)
and item 7a (instrument in place, not yet fired).**)

**Repo state -- run these; do not write it here.**  A header that
names a commit or a tag count is wrong the moment the same commit
lands, so this file does not carry one:

    git log --oneline -1            # HEAD
    git status -sb                  # branch, dirty?, ahead/behind
    git tag --list 'v*' | tail -1   # last milestone
    git tag --list '2026*'          # live scratch tags

> **Two live unobserved faults, and that is the session-51 state.**
> The item-7a `#PF` at `0x400000` now has an instrument (commit
> `20261003-vmm-map-diag`); the diagnostic did not fire on three
> boots, and the `#PF` did not reproduce -- the session-48 result,
> diagnostic kept.  A **new** fault appeared on one of those boots:
> a user-mode `#GP` at `RIP=0x42F1A7`, error `0`, in `busybox` pid
> 15 during the canary's `find / -type d` row, gone on the next
> boot.  It is `open-issues.md` **item 7b**, recorded as a sibling
> of item 7a and of session 45's virtual-1 fault -- same family
> (intermittent, layout-dependent, first boot after an image
> change, clears on the next), different vector, location, and
> phase.  **Which to work is the choice the next session makes.**
> See NEXT SESSION.
>
> **A note on docs edits, learned the hard way in session 51.**
> `docs/session-log.md` is ~1600 lines with non-ASCII characters
> (em-dashes).  Returning the **whole file** for an edit re-encodes
> every one of them and shows up as a large deletion count in
> `git diff --stat`.  For an edit to a large file with non-ASCII
> content: **insert a block, do not return the whole file.**  A
> copy-block insertion at one anchor, or a `git apply` patch that
> touches only the new lines, leaves the existing bytes alone.

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

**Session 51 is untagged work on `dev`, after `v0.6.12`.**  One
kernel commit (`20261003-vmm-map-diag`), two docs commits, no `v*`
bump; the banner still reads `v0.6.12`.

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
  ~130-line insertion.  The fix is a copy-block insertion or a
  `git apply` patch that touches only the new lines.  **Check
  `git diff --stat` before committing: a docs insertion should show
  zero deletions.**  Non-zero deletions mean existing bytes were
  rewritten.

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

**Do not edit `third_party/`.**  Session 47 added this.  The
vendored sources there are gitignored and rebuilt by the toolchain,
so an edit is invisible to the repo and vanishes on the next build.
When a diagnostic needs to see inside a third-party applet -- a
`printf` in busybox, say -- the right instrument is a **first-party
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

The tag is **annotated** (`-a -F -`) -- the annotation is the
milestone narrative, and it is what a future reader sees first.
The merge is `--no-ff`, so `main` keeps a real merge commit for
each version (`Merge dev into main for v0.6.12`, etc.).

**A version is not necessarily a milestone.**  `v0.6.10` is a tail
on `v0.6.9` with no new subsystem.  When that happens, the tag
annotation and the session-log row should *say so*, so a future
reader does not hunt for a milestone narrative that is not there.
**`v0.6.12` is a correctness milestone, not a feature one** -- the
annotation should say so plainly.  Session 51 may be the same
shape: one kernel commit, two docs commits, no `v*` yet.

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

**One kernel commit and two docs commits on `dev`, untagged.**

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
the third only by reading past the split.  Session 51 checked the
count against the file before patching.

**Why it is three prints and not one:** the site name is the missing
information, and the `free` count is what separates item 7a's two
candidates -- a healthy count with `site=PDPT`/`site=PD` means the
child's clone was missing that table (a clone-correctness fix); a
near-zero count means genuine exhaustion (an allocator fix).

**The diagnostic is failure-branch only.**  A healthy boot reaches
no new code and prints nothing; the linked `kernel.bin` is the same
size as session 50's.  It is permanent, and it **did not fire** on
the three boots that followed.  The item-7a `#PF` did not
reproduce.  This is the session-48 result: keep the instrument,
wait.

### The new fault: `#GP` at `0x42F1A7` (item 7b)

On one of those three boots, the canary's `find / -type d` row hit:

    === GENERAL PROTECTION FAULT (#GP) ===
      Faulting RIP : 0x000000000042F1A7
      Code Seg (CS): 0x0000000000000033
      Error Code   : 0x0000000000000000
      Current PID : 15
      Name        : busybox
      entry_point : 0x0000000000411A92
    EXIT-FALLBACK: switching to idle, exiting pid=15 name=busybox

The process was killed; the `find` never completed.  The next boot
ran the same row to completion.  **It is not item 7a** -- different
vector (`#GP`, 13, not `#PF`, 14), different error code (`0`, not
`0x15`), different location (busybox user text, not `0x400000`),
different phase (a syscall in a running process, not an ELF load).
It is the **third** fault in the intermittent-allocation-failure
family, after session 45's virtual-1 `#PF` and session 50's
`0x400000` `#PF`.  **Unobserved, not fixed.**  It is
`open-issues.md` item 7b; the raw frame dump is in `capture.txt`
and `docs/session-log.md`, session 51.

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

### Session 49 and 50 -- the failure-path work

Session 49 closed item 7 (the silent `vmm_map_page*` returns), fixed
`process_create`'s failure exits, and closed the exit-path
page-table leak.  Session 50 added the fault-injection hooks
(`pmm_debug_fail_next_of_type`, `process_debug_fail_next_stack_slot`)
and the `create_fail` selftest row, so **three of `process_create`'s
four failure exits now run** -- exit 3 is still by inspection, and
the commit says so.

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

## NEXT SESSION -- choose between item 7b and item 7a

**Two live unobserved faults.  Either is a defensible target; the
session picks one and says why.**  Both are the same *family*
(intermittent, layout-dependent, first boot after an image change,
gone on the next), and neither is understood.

### Option 1 -- item 7b, the `#GP` at `0x42F1A7` (the fresher fault)

**Why this first:** it is the newest, it has **no instrument at
all**, and it is a `#GP` -- a vector nothing in the tree currently
traces.  Item 7a has an instrument waiting; 7b has nothing.

**What is known:** user-mode `#GP` (vector 13), error `0`, `CS=0x33`,
in `busybox` pid 15 during `find / -type d`, `RIP=0x42F1A7`.  Error
`0` means a fault that is not a page fault -- a privileged
instruction, a non-canonical address in a base register, or a
segment violation.  The frame dump is in `capture.txt`.

**The instrument:** the kernel has a `#GP` handler (it printed the
dump).  A first change is to make it print the **canonicality** of
the base/index registers in the faulting frame, or the instruction
bytes at `RIP`, so the dump says *which* of the three error-`0`
causes it was.  Busybox symbols are not in the tree, so the address
alone does not name the instruction.

**Read first:** `interrupts.c` / `isr.asm` (the `#GP` handler),
`capture.txt` (the frame dump), and `open-issues.md` item 7b.

**Do not** edit `third_party/`; a first-party reproduction of the
`find` sequence, or a kernel-side trace, is the committable
instrument.

### Option 2 -- item 7a, the `#PF` at `0x400000` (the instrument is in)

**Why this first:** the instrument is already in place, and if the
fault returns it names itself in one line.  No new code is needed to
*observe* it -- only a boot that triggers it.

**What to do:** build the trigger image (the layout that reproduced
it before -- adding a userland ELF is the reliable way), boot it
several times, and watch for `VMM: map failed site=...`.  If it
fires, the `free` count picks candidate (a) exhaustion or (b) a
clone missing a table, and the fix follows from that.

**If it does not fire in N boots:** that is the session-48 result
again; record it, keep the diagnostic, and this is not the session
that closes 7a.  Do **not** guess at a fix from the session-50
dump.

**Read first:** `vmm.c`'s `vmm_map_page_in_cr3` and
`vmm_clone_page_table`, `pmm.c`'s `pmm_alloc_page` and
`pmm_scan_zone`, and `docs/open-issues.md` item 7a.

### Do not

Do not re-open item 7 (closed, `v0.6.12`) or the session-48 zone
scan (fixed, `v0.6.11`).  Do not re-litigate the fault-injection
design (session 50; exit 3's untested status is documented on
purpose).  **Do not drop the four older non-`2026*` tags.**  One
change at a time.  Do not edit `third_party/`.

### If you would rather do something small

**Exit 3 of `process_create`.**  The one untested failure exit.  A
hook that fails the *n*th `PAGE_PAGE_TABLE` allocation after a mark
-- e.g. `pmm_debug_fail_after(n, type)` -- would reach it
deterministically.  Slightly larger facility than the type-filtered
one; the commit message must say what it adds and what it still
does not reach.

**`sys_gettimeofday` (99)** -- a few lines from `g_ticks`, like the
existing `sys_clock_gettime` (228).  It unlocks busybox `ps -l` /
`ps -e` (`PS_LONG`, `PS_TIME`), which currently hit
`Unknown syscall: 99`.  Small, patterned, independent.

### Candidates after that, none blocking

- **`open("/proc/<pid>", O_DIRECTORY)`** -- the "complete" half of
  session 47's fix.  Same two paths in `open_resolved`, producing a
  `FILE_KIND_DIR` slot with `PROC_DIR_SENTINEL`, which exists.
- **`/dev` as a listable directory** -- `ls /dev` fails.  Same
  directory shape `/proc` got.  Prerequisite for `/dev/tty` and
  `/dev/urandom`.
- **`musl_sh` as a child of `idle`** -- so `pstree` shows a
  Unix-like tree.  A `kmain`/`process_create` change.
- **`/etc/passwd`** -- makes `ps`'s USER column show names.
- **`kill` / `pidof`** -- `pidof` needs the per-pid entries (now
  present) and a lookup; `kill` additionally needs signal delivery.
- **The `proc_walk` assertion test** -- turn the diagnostic into a
  test that asserts.
- **Symlinks** (`open-issues.md` item 8) -- larger; design recorded,
  the seam exists to hide the encoding.

---

## Busybox enablement -- state of play

**`ps`, `pstree`, `stty`, and `tty` are enabled.**  `ps` lists
processes; `pstree` shows `idle`; `tty` prints `/dev/console`;
`stty` prints the terminal state.

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
Not a defect -- a limit.

### What each still-off applet needs

Each row is a **cost estimate, not a prohibition**.  Sometimes the
missing piece is small and we write it -- that is how `tty` and `ps`
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

**Green as of session 51** -- `canary` 15/15 and `canary --full`
28/28 on the clean boot; `selftest` 18/18.  (One boot of three hit
the item-7b `#GP` and did not complete the `find` row -- see NEXT
SESSION.)

`exec_churn` has two uses: it exercises the process-exit page-table
teardown (`process_free_clone` via `process_reclaim` /
`process_destroy`), and it is a 24-round ELF-load stress -- run it
when changing those, or when changing the ELF load path.

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
                   # path, the process-exit page-table teardown,
                   # and a 24-round ELF-load stress
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
stages **48 files** -- session 51 added no ELF, only kernel code and
docs, so the count is unchanged.

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

**Session 51's own noise to expect:** on a **healthy** boot, none.
The three `VMM: map failed site=...` lines appear **only** when a
page-table allocation in `vmm_map_page_in_cr3` fails -- which a
healthy boot does not do.  If one appears, that is the instrument
reporting a real failure: capture the whole line (`site`, `virt`,
`cr3`, `free`) and treat it as the item-7a reproduction.  Do not
filter it out.

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
   signal delivery -- the same subsystem `reboot`/`halt`/`poweroff`
   need.

**Item 7 is closed** (session 49, shipped in `v0.6.12`).  The list
keeps its numbering; **item 7a** is the boot-time `#PF` at
`0x400000` (session 50), **instrumented in session 51 and not
fired**; **item 7b** is the `#GP` at `0x42F1A7` (session 51,
unobserved).  Item 8 is symlinks.

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

**Three faults in one family are unobserved, not fixed:** session
45's virtual-1 `#PF`, session 50's `#PF` at `0x400000` (item 7a,
instrumented), and session 51's `#GP` at `0x42F1A7` (item 7b).
All three are intermittent, layout-dependent, first boot after an
image change, gone on the next.  Diagnostics in place:
`isr14_handler` prints the four page types of every `#PF` walk; the
item-7a sites print `site`/`virt`/`cr3`/`free`.  **Standing
caution:** do not filter `vmm_clone_page_table`'s leaf copy on
`PT_USER` -- session 45 found it breaks the kernel's own identity
map.

---

## State on disk

**Real project root:** `/home/noneya/code/donix/`.

**The FAT layout:**

    /            HELLO-WORLD.TXT and other data
    /bin         busybox
    /usr/bin     the donix-native ELFs (apps + tests), staged BARE
    /tmp         empty

- `configs/busybox.config` -- tracked canonical busybox config.
  Enabled applets: `cat`, `cp`, `cut`, `echo`, `env`, `false`,
  `find`, `head`, `ls`, `mkdir`, `mv`, `od`, `printenv`, `pwd`,
  `rm`, `rmdir`, `seq`, `sort`, `stat`, `tail`, `tee`, `test`,
  `touch`, `tr`, `true`, `uname`, `uniq`, `wc`, `yes`, `cmp`,
  `grep`, `sed`, `vi`, `clear`, `basename`, `dirname`, `unlink`,
  `ttysize`, `tty`, `arch`, `mktemp`, `sleep`, `usleep`,
  `truncate`, `realpath`, `stty`, `ps`, `pstree`, plus `ash`.
- `userland/musl/` -- tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.  New tests must be added to both
  `USERLAND_ELFS` and the `mcopy_one` chain in
  `05_boot_kernel64/Makefile`.
- `04_kernel_64bit/fonts/ter-u18n.psf` -- tracked font source.
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  -- gitignored; rebuild with `./toolchain/install_musl.sh`.
  **Do not edit these.**
- `toolchain/{install_musl.sh,musl-gcc.sh}` -- tracked.
- `PFcapture.txt` -- the session-45 fault capture (presentation 2).
  Gitignored; **still on disk -- `ls` it, `git status` will not
  show it.**
- `DFAULT.txt` -- the session-48 double-fault capture.  Gitignored.
- `capture.txt` -- the session-51 capture (the clean boot and the
  `#GP` boot).  Gitignored.
- `run` -- tracked; the build-and-capture fix lives here.

Kernel sources: `04_kernel_64bit/`.

---

## Where things live

Not needed to start a session; ask for a file when the current task
needs it.  Paths relative to the tree root
(`/home/noneya/code/donix/`).

- `docs/strategy.md` -- Phase A/B plan, rules, files-not-to-touch,
  tagging convention, git hygiene, recovery.
- `docs/gotchas.md` -- every bug writeup, by subsystem.
  **Session 51 added no gotcha** -- the instrument is a fix to an
  unexercised diagnostic path, not a new lesson; the encoding
  lesson is in this file's Working style instead.  Session 49
  added two entries; session 48, 47, 46, and 45 each added one.
- `docs/session-log.md` -- commit tables and per-test canary notes.
  Session 51's section is at the top; then 50's, 49's, 48's, 47's,
  46's, and 44's.  Session 44's section is recorded but **misplaced**
  (it sits after session 34), noted at the top of the file and
  deferred.  There is no session-45 section.
- `docs/open-issues.md` -- full open-issues list.  **Item 7 is
  closed; the list has a gap where it was, holding 7a and now 7b.**
  Item 8 is symlinks.  The "Test-design notes" section at the bottom
  is **misplaced** (it is instructions, not issues) and is flagged
  for a move to this file's canary section in the next documentation
  round.
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
page-table leak closed, and one dead `f_stat_with_retry` block
deleted.  **Session 51** instrumented `vmm_map_page_in_cr3`'s three
silent allocation returns (they name themselves now) and the
item-7a `#PF` did not fire; a **new** fault -- a user-mode `#GP` at
`0x42F1A7` in busybox -- appeared and is recorded as item 7b.
**The recommended next session is a choice between item 7b (the
fresher fault, no instrument) and item 7a (the instrument is in,
not yet fired).**  See NEXT SESSION.  One change at a time.**

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

**Six gotchas worth reading before the next change.**  "A fix with
no test is indistinguishable from an unfixed defect."  "A test can
encode an earlier version's behavior."  "A consumer inferred from
behavior is not a consumer."  Session 45's: a fix can fail to close
the thing it claims to close, and an intermittent fault that stops
reproducing is not fixed -- it is unobserved.  Session 49's: **a
function that has never run is correct by inspection only.**  And
session 51's, for docs specifically: **a whole-file return for a
large file with non-ASCII content re-encodes the file -- insert a
block, check `git diff --stat` for zero deletions.**
