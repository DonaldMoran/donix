Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-06 (session 59 closed: the 7e mechanism
found by reading the disassembly and reproduced first-party; a real
kernel defect surfaced and was fixed.  The docs are updated and the
tree is clean.  **The next act is the `v0.6.13` bump** -- banner
commit, drop the 22 `2026*` scratch tags, push, tag, merge.  Then
item 12, signal delivery.)

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
> code.  Sessions 53 through 58 falsified mechanism after
> mechanism, each a plausible story read off a trace, each killed
> by reading the source.  **Session 59's method was the
> counter-example that paid off: every claim was checked against
> the source, and the mechanism was found by *reading the
> disassembly*, not by reasoning from a trace.**  The greps cost
> seconds; the wrong readings cost sessions.  Read the walk, read
> the disassembly, read the function -- not just its output.

**The version history, in one line each:** `v0.6.6` pipes; `v0.6.7`
a real shell and a framebuffer console; `v0.6.8` the `*at()` family;
`v0.6.9` envp, the `/usr/bin` layout, the `execve` shim removal;
`v0.6.10` a tail on `v0.6.9`; `v0.6.11` the pathname dispatch seam,
`/proc` per-pid and `ps`, and the PMM zone-scan fix; **`v0.6.12` a
correctness milestone** -- item 7 closed, `process_create`'s failure
exits fixed, the exit-path page-table leak closed.  **No new
subsystem in `v0.6.12`.**

**`v0.6.13` is pending.**  Sessions 51 through 59 are untagged work
on `dev`, after `v0.6.12`: the applet batches and the harness; item
7c retracted; the zombie leak and the NX / non-canonical family; the
fork-path sentinel leak; the `isr14` phys-mask fix; the vector-6
`#UD` handler; the `exit_group` termination fix; `mmap_nx` and
`sigsegv_probe`; the ELF loader's `PF_X` and page-0 fixes; and
**session 59's headline -- the ash-side 7e mechanism found by
reading and reproduced, and the `signal_maybe_redirect` re-entry
guard.**  The bump is a whole procedure; see below.  **It is a
decision, not a default.**

Commits are named by tag only, never by SHA.  **Working tags
(`YYYYMMDD-*`) are local scratch restore points** -- they exist while
a milestone is being developed and are **dropped before the
milestone is pushed**.  Only `v*` tags go to the remote and are
permanent.  The commit record is `docs/session-log.md`; the commit
message carries the narrative.  Once a scratch tag is dropped it
resolves to nothing; do not cite one as if it were a stable
reference.

**A plain commit can exist with no tag.**  Session 45's `run:` fix
is one.  Sessions 51 through 59's docs commits are the same.  Such
commits ride along on `dev` until the next `v*` bump.  Do not invent
a tag for one.

**Note on commit messages, and on this file too:** a commit message
is a claim, not a fact.  Read the diff, not the subject.  **This
file's item text is the same kind of claim.**  Session 52 exists
because session 51's item 7c was believed; sessions 53 through 58
falsified a dozen mechanisms that were plausible stories read off a
trace.  **Session 59 is the one where reading source first found the
mechanism** -- `evaltree`'s missing restore was found in the
disassembly, not in a trace.  **Session 59 also produced two wrong
calls of the opposite kind, by this file's own rules: a section
title and a commit subject were read instead of the body, and both
were wrong.**  See "Gotchas worth reading," below.

---

## Working style -- how this project gets changed

Read this before proposing any command block.

**One change at a time.**  A "change" is a commit, verified before
the next one starts.

**The state check comes before the command block.**  Every commit
block starts with `git status` (or `git log`).  This is a rule
because it was learned: a `git commit` was once written against an
assumed state and landed wrong.

**Check the directory, too.**  Start every command block with
`cd /home/noneya/code/donix || exit 1` and a `pwd`.

**A command block is one paste.**  `git add`, `git status`,
`git commit`, and `git tag` belong in **one** block, with the commit
message inline via heredoc.  Do not split a single operation.

**Two file-return styles.**

- **Small files:** return the complete file, four-backtick fenced.
- **Large files:** return a unified-diff hunk or a copy-block
  insertion, preceded by a bold **`WORK BEGINS HERE.`** marker.
  The marker is load-bearing: before it, the assistant is
  discussing; after it, the content is applyable.

  **A whole-file return for a large file with non-ASCII content
  re-encodes every non-ASCII character and shows up as a large
  deletion count.**  For an **insertion**, check `git diff --stat`:
  it should show **zero deletions**.  A full **replacement** of a
  section correctly shows both.  **A third shape: a Python edit
  with `count != 1` abort checks, which refuses to write if the
  anchor doesn't match exactly once.**  That shape caught a real
  ambiguity (an anchor matching twice) and wrote nothing.  Use it
  for edits into large files.

**Ask for source you do not have.**  The assistant does not have
direct file access.  Before patching a file it has not seen **in
this session**, it must ask for that file.  Never guess.

**When editing a large file, quote the bytes.**  For an edit inside
a big file, the instruction must quote the exact text being replaced
and the exact text that replaces it, both copied from output the
other side just pasted.  Read files with `cat -A` before writing
anchors into them.

**A redirection binds to the last command in an `&&` chain.**

**A capture file is one run.  Truncate, do not append.**

**A build flag change does not trigger a rebuild unless the
Makefile is a prerequisite.**

**Do not edit `third_party/`.**  The vendored sources there are
gitignored and rebuilt by the toolchain.  **But read them freely** --
session 59's mechanism was found by disassembling
`third_party/busybox/busybox_unstripped` and grepping
`busybox_unstripped.map`.  Read-only is the point.

**Hand-written `@@` hunk headers are a trap.**  Edit the file and
let `git diff` produce the patch; always `git apply --check` before
`git apply`.

**A diagnostic that runs on the fault path is part of the system.**
A fault-path diagnostic reads memory and prints serial output inside
an exception handler, and the extra time can widen a race's window.
Gate a fault-path diagnostic to the addresses it is for; do not
assume it is inert.

**The push / merge / tag sequence** (used for every `v*` bump):

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

**Before this:** the banner bump is the **last** code commit; the
session's `YYYYMMDD-*` scratch tags are **dropped**; and the
session-log and handoff are written.  The tag is **annotated**; the
merge is `--no-ff`.

**A version is not necessarily a milestone.**  **`v0.6.12` is a
correctness milestone.**  **`v0.6.13` is a candidate** -- a
mechanism found, reproduced, and a real kernel defect fixed -- and
the work since `v0.6.12` is 60+ commits across applets, the zombie
leak, the NX family, and the probes.  The bump is a **decision**,
not a default.

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 syscall ABI, natively. Newlib is gone.
Userland is a tracked source tree at `userland/musl/`. Phase B
(busybox) is well underway.

Full strategy: `docs/strategy.md`.  Direction: `ROADMAP.md`.

---

## Tree on disk

One working copy.

- **Real project:** `/home/noneya/code/donix/`.  Tracked repository,
  source of truth, where the `v*` tags live.

The kernel C sources live in `04_kernel_64bit/`.  Verify by file
name before editing.  `05_boot_kernel64/` holds the boot chain
assembly and the image builder.

Scratch workspaces are **transient**.  Do not port from one without
a build and test in the real tree.

**Run command blocks from the repository root.**

---

## Where we are -- after session 59

**The 7e mechanism is identified, reproduced, and the kernel defect
it surfaced is fixed.  7e stays open because the bug is ash's.**
The full narrative is `docs/session-log.md`, session 59, and
`docs/open-issues.md` item 7e.  In one paragraph each:

**The mechanism.**  `evaltree` stores a pointer to its own stack
frame's `jmp_buf` into `globals+0x38` (`0x41C403`) and does not
restore it on the normal return; its epilogue, `popstackmark`,
`dotrap`, and `int_on` all read the field, none writes it.
`redirectsafe` manages the same field, saving the old value on its
own frame (`rsp+0x20`) and restoring from there -- so a stale
`_longjmp` into a returned frame reads a dead slot.
`raise_exception` (`0x415521`, 27 bytes) `_longjmp`s to
`globals+0x38` with no liveness check.  A stale `%rsp` results.

**The reproduction.**  `pipe7e_sjlj_stale` mirrors the shape
first-party; phase 1 faults.  Fault #1: `RIP = 0x80000FBDBF` (a
user stack address), `RSP = 0x34`, error `0x5`.  Fault #2: the
handler on the garbage stack, `RIP = 0x400640`, `CR2 = 0x28`,
error `0x7`.

**The kernel defect the reproduction found.**
`signal_maybe_redirect` had no re-entry guard, so a handler that
itself faulted re-entered itself forever -- a live-lock.  Fixed
with `in_signal_handler` on `pcb_t`, set when the redirect fires,
cleared in **both** `process_reclaim` and `process_destroy`.
`signal_handler[]` was also never cleared on teardown despite the
comment; fixed in the same commit.

**What this closes and what it does not.**  The **loop** is closed;
7e will still fire in ash, now taking two faults and a kill
instead of live-locking.  The bug is ash's and `third_party/` is
not editable.  **The only donix-side path to making 7e stop
costing whole runs is item 12** -- a real signal subsystem, so ash
can catch the fault and continue.

### The NX / non-canonical address family (session 54, unchanged)

Four fixes, one mechanism: `vmm_get_phys*` strip `PT_NX`;
`sys_mmap` honors `PROT_EXEC`; the huge-page split sets NX on the
512 split PTEs, not the PDE; `pte_phys` strips both.  Every walk
site in `vmm.c` uses `pte_phys`.

### The fault model (session 54, unchanged)

**`isr14_handler`'s walk does not halt.**  **`fault_signal` +
`sys_wait4`:** a faulted child's wait status is a signal-kill
encoding; busybox ash prints `Segmentation fault`.  **`g_last_sysret`**
is wired.  User-mode faults are diagnosed and the process killed;
the machine lives.

---

## NEXT SESSION

**Item 12 -- signal delivery -- is the lead.**  `sys_rt_sigaction`
is still a stub for every real signal.  Session 59's fix means
`signal_handler[]` is per-process and cleared on teardown, and the
redirect is guarded; what remains is the subsystem: a real
`sys_rt_sigaction` reading the real `struct sigaction`, a raise on
the fault path, an `rt_sigreturn` that restores the frame after the
handler returns, a restorer trampoline, and a `SIGPIPE` raise on
the `-EPIPE` write path.  Unblocks `kill`, ash job control, the
Wayland `SIGBUS` path, and a real first-party `SIGSEGV` reproducer
that **returns** from its handler.

**Item 12 is a workstream, not a commit.**  Scope it first: the
`struct sigaction` layout, the return-path frame, the restorer
convention, the pending/blocked sets, the raise sites, the default
actions, and cross-process delivery.  Write the design down before
writing code.  A half-built signal subsystem is worse than the
current honest stub.

### The candidates, in order

1. **Item 12 -- signal delivery.**  Largest; the only path to
   making 7e stop costing runs; unblocks several.
2. **`sys_gettimeofday` (96).**  A few lines from `g_ticks`, like
   `sys_clock_gettime` (228).  Unlocks `ps -l`.  **Small,
   patterned, independent.**  Note: the number is **96**, not 99;
   the handoff carried 99 in error until session 59.
3. **The `v0.6.13` bump.**  A whole procedure: banner commit, drop
   the 22 `2026*` scratch tags, push, tag, merge.  **A decision,
   not a default.**
4. **Documenting the ash mechanism** for a third-party patch or a
   workaround.  The donix side has no fix left for 7e.

### If you would rather do something small and clean

**`sys_gettimeofday` (96)** -- see above.  **`open("/proc/<pid>",
O_DIRECTORY)`.**  **`/dev` as a listable directory.**  **More
applets from the "still-off" table** -- the zero-syscall ones.

### Do not

Do not re-open item 7 (closed), item 11 (closed), or the session-48
zone scan (fixed).  **Do not re-open the fork-sentinel leak.**
**Do not re-litigate the five 7e mechanisms falsified in sessions
56-57.**  **Do not re-open the "is the mmap window executable"
question.**  **Do not trust a page walk's verdict without reading
the walk.**  **Do not re-run the two `setjmp` probes to "check" --
they pass; the mechanisms are falsified.**  Do not chase the
intermittent family (7a, 7b) without a reproducer.  **Do not drop
the older non-`2026*` tags.**  One change at a time.  Do not edit
`third_party/` -- but do read it.

---

## Busybox enablement -- state of play

**Enabled and working:** `ps`, `pstree`, `stty`, `tty`, `gzip`,
`gunzip`, plus batches 2 and 3.  `sha512sum` **works** -- item 7c was
a test bug.  `find -not` **works with `!`**.

### The rule

**Enable an applet only when the syscalls it actually calls are
implemented -- read the applet's source, do not guess from its
name.**  A syscall that exists but cannot do its job is worse than
a missing one.

**A real applet finds real bugs.**  Session 52 is the
counter-example: a real applet found a wrong test.

**Do not edit `third_party/`.**

### What each still-off applet needs

Each row is a **cost estimate, not a prohibition**.

| Config | Applet | Needs |
|---|---|---|
| `CONFIG_DIFF` | `diff` | `mmap` of files (non-anonymous) |
| `CONFIG_CHMOD`/`CHOWN` | | FAT has no permissions/ownership |
| `CONFIG_LN`/`LINK` | | FAT has no links |
| `CONFIG_MOUNT`/`UMOUNT` | | no VFS |
| `CONFIG_HALT`/`POWEROFF`/`REBOOT` | | item 12; no init, no ACPI |
| `CONFIG_TAR`/`UNZIP`/`CPIO`/`BZIP2`/`XZ` | archives | `mkdirat`, `symlinkat`, `utimensat` storage, file-backed `mmap`, decompression |
| `CONFIG_AWK` | `awk` | large |
| `CONFIG_LESS`/`MORE` | pagers | raw-mode terminal control; `/dev/tty` does not exist |
| `CONFIG_TOP` | `top` | `ps -l`-class fields (`gettimeofday`), a redraw loop |
| `CONFIG_KILL` | `kill` | item 12 |
| `CONFIG_NETWORKING` | | no network stack |
| `CONFIG_ASH_JOB_CONTROL` | | item 12; item 13 |
| `CONFIG_FEATURE_FANCY_SLEEP` | `sleep 0.1` | nothing -- the flag itself |

---

## Canary state

**Green as of session 57's carry-back** -- `canary` 15/15,
`canary --full` 28/28, `selftest` 18/18, `test.sh` **57 passed, 0
failed**.

**`pipe_wake_probe.sh` completion varies per boot.**  The 7e family
and the 7d lost-wakeup are racy.  **The completion rate is not a
regression indicator on its own.**

**Session 59 verified:** `pipe7e_stdio_sjlj` passes 20000;
`pipe7e_stdio_heapjmp` passes 20000; `pipe7e_sjlj_stale` phase 1
faults and is killed (two faults, no loop).

**Regression tests (`userland/musl/tests/`, not canary rows):**

    at_step1 at_step2 envp_step1 musl_exec2 fcntl_lowfd
    readlink_errno proc_status proc_fd proc_dir proc_stat
    proc_walk proc_walk_fds mmap_stress exec_churn
    sha512_probe pipe7e mmap_nx sigsegv_probe pipe7e_stdio
    pipe7e_stdio_sjlj pipe7e_stdio_heapjmp pipe7e_sjlj_stale

**Pipe regression suite:** `pipe_step1 pipe_step2 pipe_step3
pipe_step3b`.  Run when changing `sys_read`/`sys_write`/`sys_close`/
`put_file_slot`/`sys_fork`/`sys_pipe` or adding a `FILE_KIND_*`.

**`pipe7e` is the item-7e acceptance test.**  **`mmap_nx`** proves
`sys_mmap`'s `PT_NX` gate.  **`sigsegv_probe`** lets a process catch
its own fault.  **`pipe7e_stdio`** is the `x=$(cmd)` shape with
`FILE *`.  **`pipe7e_stdio_sjlj` / `pipe7e_stdio_heapjmp`** test the
`setjmp` shapes and pass.  **`pipe7e_sjlj_stale`** is the
stale-`jmp_buf` reproduction and the acceptance test for the
`signal_maybe_redirect` re-entry guard.

**Run `pipe7e_sjlj_stale` when changing `signal_maybe_redirect`,
`isr13_handler`, `isr14_handler`, or the `pcb_t` signal fields.**

**The harness:** `test.sh` (staged at `/root/scripts/test.sh`),
~58 rows, not a canary, takes minutes, passes now.

**New ELFs must be added to both `USERLAND_ELFS` and the `mcopy_one`
chain in `05_boot_kernel64/Makefile`.**

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`.

**Expected noise:** no `Unknown syscall:` lines; no `[fd]` lines;
the `FB:` line is expected.  `WW:` lines are the item-7d instrument.
**A `Segmentation fault` line is expected** -- the honest
exit-status change.  Multiple in a `pipe_wake_probe.sh` run are the
7e family firing, each absorbed.

---

## Open issues (top 6; full list in `docs/open-issues.md`)

1. **`open-issues.md` item 7e: the control-transfer face.**  The
   **mechanism is identified** on the ash side and reproduced; the
   **kernel re-entry loop is fixed**.  7e stays open because the
   stale-`jmp_buf` bug is ash's.  **No donix-side fix remains.**
2. **`open-issues.md` item 12: signal delivery is a stub.**  The
   largest target; unblocks `kill`, ash job control, the Wayland
   `SIGBUS` path, and a real `SIGSEGV` reproducer.  **The next
   session's lead.**
3. **`open-issues.md` item 10: `CR4.OSFXSR` without an XMM save.**
   Latent; the fix is a kernel feature.
4. **`open-issues.md` item 13: `CONFIG_ASH_JOB_CONTROL` gap.**
5. **`open-issues.md` item 9: no privilege model.**
6. **`sys_gettimeofday` (96) is not implemented**, so `ps -l` is
   off.  Small.  **Note: 96, not 99.**

**Item 7 closed; item 7c retracted; item 7d fixed.**  **Item 11
closed.**  **Item 7e: mechanism identified and reproduced; loop
fixed; open because the bug is ash's.**

---

## State on disk

**Real project root:** `/home/noneya/code/donix/`.

**The FAT layout:**

    /            HELLO-WORLD.TXT and other data
    /bin         busybox
    /usr/bin     the donix-native ELFs, staged BARE
    /tmp         empty
    /etc         passwd, group
    /root        scripts/test.sh, scripts/pipe_wake_probe.sh
    /home        empty
    /dev         empty (a real FAT directory)
    /var         empty

- `configs/busybox.config` -- tracked canonical config.
- `userland/musl/` -- tracked musl userland.  Session 59 added
  `tests/pipe7e_stdio_sjlj.c`, `tests/pipe7e_stdio_heapjmp.c`,
  `tests/pipe7e_sjlj_stale.c`; the Makefile unchanged.
- `userland/scripts/` -- `test.sh`, `pipe_wake_probe.sh` (4000
  iterations).
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  -- gitignored; **read freely, do not write.**
- `toolchain/{install_musl.sh,musl-gcc.sh}` -- tracked.
- `capture.txt` -- gitignored, truncated per run.  Captures
  preserved outside the tree survive a rebuild and do not dirty
  the tracked tree.
- `run` -- tracked; the build-and-capture fix.

Kernel sources: `04_kernel_64bit/`.

---

## Where things live

Not needed to start a session; ask for a file when the task needs
it.

- `docs/strategy.md`, `docs/gotchas.md`, `docs/session-log.md`,
  `docs/open-issues.md`.
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md`.
- `ROADMAP.md`, `README.md` -- review at the next `v*` bump.
- `run`.

---

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  `v0.6.12` is a correctness milestone; `v0.6.13`
is pending.  Session 51** built the applet batches and harness.
**Session 52** retracted item 7c.  **Session 53** found the zombie
leak.  **Session 54** closed item 11, fixed the NX family, made user
faults honest.  **Session 55** closed the fork-sentinel leak.
**Sessions 56-57** made three kernel fixes, built `mmap_nx` and
`sigsegv_probe`, and falsified five mechanisms.  **Session 58**
added the `fault_rip` walk (found to misread, retracted), fixed the
ELF loader, wrote `pipe7e_stdio`.  **Session 59** wrote two
`setjmp`/`longjmp` probes (both pass, falsifying two mechanisms);
found the ash-side mechanism by reading the disassembly
(`evaltree` never restores `globals+0x38` on its normal return, and
`redirectsafe` restores the same field from a frame-local slot) and
reproduced it first-party (`pipe7e_sjlj_stale`); and found and
fixed a real kernel defect the reproduction surfaced
(`signal_maybe_redirect` had no re-entry guard, so a handler that
faulted looped forever; also `signal_handler[]` was never cleared
on teardown).  **The next act is the `v0.6.13` bump; then the
session-60 lead is item 12, signal delivery.**  See NEXT SESSION.
One change at a time.**

---

## How to use this file

At session start, paste this file and say "Continue from here and
request any files you need."  Run the four `git` commands -- they
are the state; this file is the narrative.

At session end, **rewrite the narrative.**  Do **not** write repo
state.  New gotchas go to `docs/gotchas.md`; new commit rows go to
`docs/session-log.md`; new open issues go to `docs/open-issues.md`.
This file never grows.  Name commits by tag only.

**When a session's findings change an earlier numbered step, edit
the step in place.**

**Before proposing any command block, read the "Working style"
section at the top.**

**Gotchas worth reading before the next change.**  "A fix with no
test is indistinguishable from an unfixed defect."  "A test can
encode an earlier version's behavior."  "A consumer inferred from
behavior is not a consumer."  Session 49's: **a function that has
never run is correct by inspection only.**  Session 51's two: **a
whole-file return for a large non-ASCII file re-encodes it**; and
**print the row marker before the row runs.**  Session 52's: **a
test's expected value is a claim.**  Session 53's: **a diagnostic
declared but never wired is not a diagnostic.**  Session 54's two:
**a halt in a fault handler is a diagnostic that never runs**; and
**put cleanup in the teardown, not each entry point.**  Session
55's two: **a fork that overwrites a child's file table leaks per
fork**; and **a captured fault is not a diagnosed one.**  Session
57's two: **hand-written `@@` hunk headers are a trap**; and **a
type's named fields are not what the pointer points at.**  Session
58's two: **a page walk that descends through a supervisor level
prints a confident wrong verdict**; and **a diagnostic on the fault
path is part of the system.**  Session 59's three: **a Python edit
with a `count != 1` abort check catches an ambiguous anchor and
writes nothing -- use it for edits into large files; a handler on
the fault path with a corrupt `rsp` cannot run, so a redirect
without a re-entry guard loops forever -- guard it; and reading the
disassembly found the mechanism a dozen trace-readings had
missed.**  **And session 59's meta-gotcha: a section title is a
claim about its body, and a commit subject is a claim about its
diff -- read the body, read the diff, not the title and not the
subject.  Session 59 made that mistake twice.**  **This file's item
text is the same kind of claim.**
