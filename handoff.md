# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-13 (three fixes committed and tagged; two
bugs open.  Read "NEXT SESSION" first.)

**THE HEADLINE: the 2026-10-13 session closed the pipe7e stall's
downstream consequences but did not fix its root cause, and it
reproduced Bug 1.  Three fixes are committed under the tag
`20261013-queue-and-rescan-fixes`:**

1. `scheduler_ready_queue_add` no longer builds an un-walkable queue
   when `head == NULL` and `tail` points at a stale PCB.
2. The timer's stranded-process re-scan no longer skips `current`.
3. The P1 race-probe auto-dump is gated behind
   `RACE_PROBE_P1_AUTODUMP` (default 0) on the timer path only; the
   fault-handler calls stay ungated.

**TWO bugs remain open, and they are different bugs:**

**Bug 1 (VERIFIED, REPRODUCED 2026-10-13): a user-mode page fault
with a kernel stack address in `%rsp`.**  Reproduced in the applet
test (`sh /root/scripts/test.sh`) at row 12, `uniq`.  Fault:
`RIP=0x412835`, `RSP=0xFFFFFFFF802A2640`, `CS=0x33`, `CR2` = `RSP`,
error code 0x5 (user read of a present supervisor page).  The same
shape as the 2026-10-12 capture (`0x40786B`).  Capture saved as
`capture-pf-0x412835-*.txt`.  The kernel killed the faulting process
and the shell survived -- correct behavior, and the fault is a real
reproduction.

**Bug 2 (ROOT CAUSE IDENTIFIED, NOT FIXED): `context_switch.asm`'s
`.ring0_save` writes the caller's return address as the resume RIP.**
When a process is saved inside a `call context_switch` at
`process_exit`'s Site-1, and later resumed, it resumes at the `hlt`
guard after the call and stops.  The two downstream fixes made this a
livelock instead of a deadlock: the timer is alive, the queue is
repaired, the helper is re-run, and it resumes into the `hlt` again.
The system cycles rather than hangs.  The root-cause fix is a design
question -- see "Bug 2" below, and do not guess at it.

**Repo state -- run these; do not write it here.**

    cd /home/noneya/code/donix || exit 1
    git log --oneline -5
    git status -sb
    git tag --list '2026101*'

**Working style -- how this project gets changed**

Read this before proposing any command block.

**One change at a time.** A "change" is a commit, verified before the
next one starts.

**The state check comes before the command block.** Every command
block starts with `git status -sb` (or `git log`).

**A command block is one paste -- but write it to a file first.**
`git add`, `git status`, `git commit`, and `git tag` belong in **one**
block, with the commit message inline via heredoc.  **Do not paste a
block with `cd ... || exit 1` into an interactive shell** -- the
`exit` closes the terminal.  Write the block to `/tmp/<name>.sh` with
`cat > ... <<'SCRIPT_EOF'`, then run `bash /tmp/<name>.sh`.  This
cycle paid for that lesson twice.

**Two file-return styles.** Small files: complete file, four-backtick
fenced. Large files: unified-diff hunk or copy-block insertion,
preceded by `WORK BEGINS HERE.`.

**Python edits with `count != 1` abort checks are the preferred edit
style.** **Put `set -e` at the top of the script** if the block edits
and commits; without it, an edit script that fails mid-way still runs
the `git commit`, producing an orphan commit.

**Ask for source you do not have.** Before patching a file it has not
seen **in this session**, ask for it.  **Before writing an anchor,
read the file with `cat -A` and quote the bytes.** **Anchor on
structure, not whitespace.**

**Idempotent edit scripts.** A Python edit that adds a block must
check whether the block already exists.

**A diagnostic that saturates the serial console changes the timing
of the system it is trying to observe.**

**A diagnostic on the SUCCESS path is noise.  A diagnostic on the
FAILURE path is signal.**

**Commit diagnostics before any `git reset --hard`.**

**The QEMU monitor socket is a diagnostic tool.**  `qemu-system`
started with `-monitor unix:/tmp/donix-mon,server,nowait` can be
read with `socat - UNIX-CONNECT:/tmp/donix-mon`.

**Under `-enable-kvm`, the QEMU monitor's `xp` cannot translate guest
kernel virtual addresses.**  Use `x` (guest-virtual) instead.  GDB
over the `-s` stub also works and is the read for the live PCB.  The
`run` script now has both `-s` and `-display none`.

**GDB is the second diagnostic tool.**  `-s` on the QEMU
command line opens a GDB stub on tcp:1234.  `gdb
04_kernel_64bit/kernel.elf`, then `target remote :1234`.
`watch *(unsigned long long *)<addr>` sets a hardware
watchpoint.  **The QEMU monitor does not have a `watch`
command** -- only GDB.

**`nm` output moves between builds.**  Re-run
`nm 04_kernel_64bit/kernel.elf | sort` before using any address
from this file, a previous paste, or a commit message.  **A
`strtonum`/`awk` symbol lookup can return a confidently wrong nearest
symbol; a lexical range filter over equal-length hex `nm` output is
the reliable check.**

**Two identical register reads do not distinguish "hung" from
"livelocked at high frequency."**  The tick counter (`g_ticks`) does.
Read it twice and compare.

**A frame slot holds a specific kind of value -- stack address, code
address, flags.**  A PCB field that is the same width may be a
different kind.  Check the kind before moving a value into a slot.

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static
musl-linked binaries on the Linux x86_64 syscall ABI, natively.
Newlib is gone. Userland is a tracked source tree at
`userland/musl/`. Phase B (busybox) is well underway.

Full strategy: `docs/strategy.md`. Direction: `ROADMAP.md`.

---

## Where we are

**Three fixes committed and tagged `20261013-queue-and-rescan-fixes`:**

1. **`scheduler_ready_queue_add` repair.**  When `head == NULL`, the
   add now starts a fresh queue with the new process as both ends,
   instead of taking the `if (ready_queue_tail)` branch and leaving
   `head` unset.  The old self-tail-heal covered
   `{head=NULL, tail==process}`; this covers `{head=NULL,
   tail==<other PCB>}`, which is the shape the 2026-10-13 stall
   showed.
2. **Timer re-scan hole.**  `interrupts.c`'s re-scan no longer skips
   `current`.  The other two re-scans (in `scheduler.c`) never did.
3. **P1 auto-dump gate.**  `RACE_PROBE_P1_AUTODUMP` (default 0) gates
   the timer's call to the race-ring dump.  The fault-handler calls
   remain ungated -- a fault is failure-path signal.

**Plus a `run` change:** `-display none` (the comment claimed headless
all along) and `-s` (GDB stub).

**Two bugs remain open:**

### Bug 1 -- user-mode #PF with kernel RSP (VERIFIED, REPRODUCED)

Fault: `RIP=0x412835` (user), `RSP=0xFFFFFFFF802A2640` (kernel stack),
`CS=0x33`, `CR2` = `RSP`, error code `0x5`.  The user RSP was
corrupted to a kernel stack address; the next user stack access
faulted on a supervisor page.  Reproduced 2026-10-13 in
`sh /root/scripts/test.sh`, row 12 (`uniq`).  Same shape as the
2026-10-12 capture at `0x40786B`.

**Candidate mechanisms, not yet distinguished:**

- **A.**  The syscall-entry frame's user-RSP slot is overwritten
  between entry and return.  The slot is written by `push qword
  [rel g_user_rsp_save]` and read by `mov rsp, [rsp - 72]`.  A
  kernel-mode interrupt that pushes at the wrong depth lands on it.
- **B.**  The `context_switch.asm` user-preempted save path delivers
  a kernel value.  `pcb->user_rsp` exists specifically because this
  path once did.
- **C.**  `pcb->user_rsp` itself holds a kernel value.  It is written
  in `interrupts.c`'s user-preempted save block as
  `current->user_rsp = stack_pointer;` alongside `current->rsp =
  stack_pointer;`.

**The instrument that names the writer is the one the 2026-10-12
session wrote and this session reverted.**  Re-apply it: in
`04_kernel_64bit/user_syscall_entry.asm`, just before `mov rsp,
[rsp - 72]` on the return path, check that the loaded value is in
`[0x8000000000, 0x8000100000)`.  If not, **store** it to a global and
**halt** -- do not call `serial_print`, because `rsp` is about to
become a user address and a fault-path handler with a bad `rsp`
cannot run.  Read the stored value via GDB over the `-s` stub.

The check was tested and did **not** false-positive across two pipe7e
runs on the 2026-10-13 tree.  It is the right first instrument.

**And pair it with the 7e-fork slot print.**  `RACE_PROBE_FORK_SLOT`
is in the tree, default 0.  Turn it on for the run: it prints the
parent's and child's view of the slot the child's first post-syscall
`ret` will pop.

### Bug 2 -- the `.ring0_save` resume frame (ROOT CAUSE IDENTIFIED)

**Mechanism.**  `context_switch.asm`'s `.ring0_save` builds a resume
frame whose RIP slot is `[rsp + 48]` -- the caller's return address
of `call context_switch`.  For `process_exit`'s Site-1 caller, that
return address is the `nopl` after the call, which falls into the
`hlt` guard at `process_exit+0x100`.  A process saved there and later
resumed resumes at the guard and stops.

**What the 2026-10-13 fixes changed.**  The stall is now a livelock
rather than a deadlock: the timer is alive (`g_ticks` advances at
~100 Hz), the queue is repaired by fix 1, the helper is re-run by
fix 2, and it resumes into the `hlt` again.  The banner repeats every
60000 ticks with an identical process list.  The system runs; the
helper never makes progress.

**The two failed fix attempts and exactly why each failed:**

- **RIP-only**: change `.ring0_save`'s RIP to `prev->rip`, leave RSP
  as `lea rax, [rsp + 64]`.  Produced a kernel-mode `#UD` at `0xBF`.
  The RIP and RSP slots described two different moments; the `iretq`
  popped garbage.  **Refuted.**
- **RIP+RSP**: change RIP to `prev->rip` and RSP to `prev->rsp`.
  **Not run -- caught by reading the frame-build downstream.**
  `prev->rsp` is a *frame base*, and the frame's RSP slot holds the
  *stack address* the `iretq` loads.  Those are different kinds of
  value.  **Category error.**

**The design gap.**  The PCB does not record which path last wrote
`pcb->rip` and `pcb->rsp`.  The timer writes them together
(`current->rip = frame->rip; current->rsp = stack_pointer;`), and the
pair is consistent.  `.ring0_save` writes `pcb->rsp` but not
`pcb->rip`, and uses a *different* RIP source.  So the pair is
consistent only when the timer is the last writer.  `.ring0_save`
cannot tell whether the timer saved the process or it is saving it
itself.

**The read the next session should do first:**  trace which function
last wrote `pcb->rip` and `pcb->rsp` for a stalled process.  Either a
scratch "last writer" field, or a careful reading of the paths.  Do
not propose a fix until the writer is named and the correct pair for
a ring-0 resume is defined.

### Refuted -- do NOT re-walk

- **The pipe wake handshake** -- closed by `c3e5c97`.
- **The atomic refcount race** -- closed by `e1e8549`.
- **The stale-link queue membership** -- closed by `a0f9779`.
- **The timer's missing pool rescan** -- closed by `e3705d4`.
- **The wait4 lost wakeup** -- closed by `364be3b`.
- **The ready-queue self-link** -- closed by `cfff858`.
- **The `.ring0_save` RIP-only change** -- refuted, `#UD` at `0xBF`.
- **The `.ring0_save` RIP+RSP change** -- category error, not run.
- **"Two RUNNING processes in one dump violates the one-current
  invariant."**  Wrong -- `timer_preempt_handler` excludes pid 1.
- **Also do not re-open:** item 7, item 7c, item 11, the session-48
  zone scan, the `fs_base` path, the `longjmp`/`setjmp` family.

---

## NEXT SESSION

**Step 0: re-apply the Bug 1 instrument, then run.**

Re-apply the `user_syscall_entry.asm` user-RSP invariant check
(described under Bug 1 above).  Turn `RACE_PROBE_FORK_SLOT` on.
Rebuild.  Run `pipe7e` and `canary --full`.  Capture aside before
each run overwrites `capture.txt`:

    cp capture.txt canary-bug1-$(date +%Y%m%d-%H%M).txt

**If the check fires:** read the stored bad value via GDB over the
`-s` stub, then set a hardware watchpoint on the user-RSP slot at
`<child_kernel_stack_top> - 72` and re-run to catch the writer.  The
slot address is `pcb->kernel_stack_top - 72` for the faulting
process.

**If the check does not fire and canary faults at a user RIP with a
kernel RSP:** the corruption is after the check's read and before
`mov rsp, [rsp - 72]`.  That narrows the window, and the check moves
to just after the `mov`, read via GDB instead of print.

**Step 1: one change, one commit, then re-test.**

**Step 2: write the Bug 2 root-cause read**, per the section above,
only after Bug 1 has a named writer or is ruled out for this cycle.

### The candidates, in order

1. **Bug 1 -- the user-RSP corruption.**  Instrument first.
2. **Bug 2 -- the `.ring0_save` resume frame.**  A design question;
   see the read above.  Do not guess.
3. **Item 12 -- signal delivery.**  Largest; unblocks `kill`, ash job
   control, the Wayland `SIGBUS` path.  A workstream.
4. **`sys_gettimeofday` (96).**  Small, patterned, independent.
   Unlocks `ps -l`.  Note: 96, not 99.

### Do not

Do not apply the script or re-run the cherry-pick sequence.
The tree is already patched.  Do not chase the pipe wake path,
the atomic refcount, the `fs_base` path, item 7, item 11, or the
session-48 zone scan.  One change at a time.  Do not edit
`third_party/` -- but do read it.

Do not run `git reset --hard` without first committing every
diagnostic you care about.

Do not arm a GDB watchpoint on an address without re-running
`nm` on the current build.

Do not propose a `.ring0_save` fix before naming the writer of
`pcb->rip`/`pcb->rsp`.

---

## Open issues (top 6)

1. **Bug 1 -- user-mode #PF with kernel RSP.**  Reproduced
   2026-10-13.  Instrument first.
2. **Bug 2 -- `.ring0_save` resume frame.**  Root cause identified,
   fix not designed.
3. **`open-issues.md` item 12: signal delivery is a stub.**
4. **`open-issues.md` item 10: `CR4.OSFXSR` without an XMM save.**
5. **`open-issues.md` item 13: `CONFIG_ASH_JOB_CONTROL` gap.**
6. **`open-issues.md` item 9: no privilege model.**
   **`sys_gettimeofday` (96)** so `ps -l` works.  Small.

---

## State on disk

Kernel sources: `04_kernel_64bit/`.  `capture.txt` is gitignored;
one run per truncate.

The pre-patch backup `donix-before-apply-20261012` (or its tarball)
holds the un-patched donix at `5537465`.

The script `apply-fixes-to-donix.sh` lives at
`/home/noneya/code/apply-fixes-to-donix.sh` (or a copy in `docs/`).

`patch_donix` may be archived; `donix` is the working tree.

Tags from this cycle, local only:
`20261013-session-start` (`04df9eb`),
`20261013-queue-and-rescan-fixes` (the four 2026-10-13 commits).

---

## One-line summary

**Three fixes are committed under
`20261013-queue-and-rescan-fixes`: the `scheduler_ready_queue_add`
repair, the timer re-scan hole, and the P1 auto-dump gate.  Two bugs
remain: (1) a user-mode #PF with a kernel RSP, reproduced
2026-10-13 at `RIP=0x412835`, `RSP=0xFFFFFFFF802A2640`; (2) the
`.ring0_save` resume frame, whose root cause is known and whose fix
is a design question.  The next session's first action is to
re-apply the `user_syscall_entry.asm` user-RSP invariant check, turn
on `RACE_PROBE_FORK_SLOT`, and run.  Do not guess at the `.ring0_save`
fix.**

## How to use this file

At session start, paste this file and say "Continue from here and
request any files you need." Run the `git` commands -- they are
the state; this file is the narrative.

At session end, **rewrite the narrative.**  Do **not** write repo
state.  New gotchas go to `docs/gotchas.md`; new commit rows go to
`docs/session-log.md`; new open issues go to `docs/open-issues.md`.
This file never grows.  Name commits by tag only.

**When a session's findings change an earlier numbered step,
edit the step in place.**

**Gotchas worth reading before the next change.**

"A fix with no test is indistinguishable from an unfixed defect."
"A test can encode an earlier version's behavior."
"A consumer inferred from behavior is not a consumer."
"A function that has never run is correct by inspection only."
"A whole-file return for a large non-ASCII file re-encodes it; and
print the row marker before the row runs."
"A test's expected value is a claim."
"A diagnostic declared but never wired is not a diagnostic."
"A halt in a fault handler is a diagnostic that never runs."
"Put cleanup in the teardown, not each entry point."
"A fork that overwrites a child's file table leaks per fork."
"A captured fault is not a diagnosed one."
"Hand-written `@@` hunk headers are a trap."
"A type's named fields are not what the pointer points at."
"A page walk that descends through a supervisor level prints a
confident wrong verdict."
"A diagnostic on the fault path is part of the system."
"A Python edit with a `count != 1` abort check catches an ambiguous
anchor and writes nothing; a handler on the fault path with a
corrupt `rsp` cannot run, so a redirect without a re-entry guard
loops forever; and reading the disassembly found the mechanism a
dozen trace-readings had missed."
"A section title is a claim about its body, and a commit subject is a
claim about its diff -- read the body, read the diff."
"A race is not memory corruption -- there is no store to find."
"A session number, a comment, and an address pairing are claims too."
"Before writing an edit anchor, read the file with `cat -A` and quote
the bytes."
"This cycle's additions:"
"A diagnostic that saturates the serial console changes the timing
of the system it is trying to observe."
"`git reset --hard` discards uncommitted work; commit diagnostics
before resetting."
"Anchor on structure, not whitespace."
"A Python edit with a `count != 1` guard writes nothing, but without
`set -e` the command block keeps going and commits a partially-edited
tree."
"An edit script's idempotency guard must cover every insertion."
"`nm` output moves between builds -- re-run it before every
watchpoint address."
"The QEMU monitor has no `watch` command; use GDB `target remote`."
"A diagnostic on the SUCCESS path is noise; a diagnostic on the
FAILURE path is signal."
"Do not write a handoff that claims a bug is closed based on a test
that was never run on the final tree."
"2026-10-13's additions:"
"A command block written as a script is not safe to paste into an
interactive shell; `exit` in the paste closes the terminal.  Write
the block to a file and run it as a child process."
"Under `-enable-kvm` the QEMU monitor's `xp` cannot translate guest
kernel virtual addresses; use `x`, or GDB over the `-s` stub."
"Two identical register reads do not distinguish 'hung' from
'livelocked at high frequency'; the tick counter (`g_ticks`) does."
"A `strtonum`/`awk` symbol lookup can return a confidently wrong
nearest symbol; a lexical range filter over equal-length hex `nm`
output is the reliable check."
"A frame slot holds a specific kind of value; a PCB field that is the
same width may be a different kind."
"The P1 auto-dump was left ungated in the 2026-10-12 cleanup cycle;
a clean canary printed the whole race ring."
"The tag must be created after the commits it names; a tag created
before them names the wrong tree."

**This file's item text is the same kind of claim.**
