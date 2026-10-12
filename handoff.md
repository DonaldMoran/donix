# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-12 (donix is patched; two known bugs
remain, one verified and one not yet re-tested.  Read "NEXT
SESSION" first.)

**THE HEADLINE: donix contains the eight fixes and five cleanups
from the 2026-10-10/11/12 cycle.  The pipe wake race, the wait4
lost wakeup, and the ready-queue self-link are all closed.  TWO
known bugs remain:**

**Bug 1 (VERIFIED): a user-mode page fault at user RIP
`0x40786B` with `%rsp` holding a kernel stack address.**  Timing-
dependent.  Fired once in `canary --full` on the row `echo hi |
wc`.  Capture: `capture-fork-rsp-fault-20261012.txt`.

**Bug 2 (NOT RE-TESTED): the pipe7e stall.**  The last pipe7e
run was on a build that PREDATED the cleanups.  It stalled at
iteration ~20500 with the helper at RIP `0x40022B` (its ELF
entry, `_start`) and the reader BLOCKED in a pipe read.  **We do
not know whether pipe7e stalls on the patched tree.**  That is
the FIRST thing the next session must check.

**THE NEXT SESSION'S FIRST ACTION IS TO VERIFY, NOT TO PATCH.**
Boot `patch_donix` and run `pipe7e` to completion.  Then boot
`donix` and run `pipe7e`.  Compare.  If `pipe7e` stalls, the
session works on Bug 2 first (the pipe7e hypothesis is written
out below).  If `pipe7e` completes, the session works on Bug 1
(the user-RSP corruption).  **Do not apply the script or run
the fix sequence on `donix` again until the next session has
verified the tree.**

**Repo state -- run these; do not write it here.**

    cd /home/noneya/code/patch_donix || exit 1
    git log --oneline -1
    git status -sb
    git tag --list '20261012*'

    cd /home/noneya/code/donix || exit 1
    git log --oneline -1
    git status -sb

**Real project is `donix` at `/home/noneya/code/donix/`.  It is
patched with the eight fixes and five cleanups.  The script
`apply-fixes-to-donix.sh` reproduces the patched state from a
fresh donix at `5537465`.  The script was verified: a fresh copy
plus the script produces the identical tree hash
`aaa2ad3659ef039ff13a8be32f1717127935cace`.**

**The cherry-pick sequence, in order:**

    e1e8549  user_syscall: atomic RMW on file_slot_t.refcount
    e8361ec  user_syscall: pipe_wake_waiter returns success; gate field clear
    6962c34  user_syscall_entry: move sti to just before call syscall_dispatch
    e3705d4  interrupts: rescan pool in kernel-mode timer before idling
    a0f9779  scheduler: reset clears pcb links; contains walks the queue
    c3e5c97  pipe: reader clears its own publish; wakers leave the field set
    364be3b  process_reclaim: remove PCB from ready queue before UNUSED
    cfff858  scheduler: heal {head=NULL, tail=process} instead of self-linking
    <drop>   scheduler: drop sched4_record call from cfff858 (testme-only)

**The five cleanups:**

    cleanup: silence three compiler warnings
    cleanup: remove 7e-queue success-path diagnostic from timer
    cleanup: gate PPS-* pipe diagnostics behind RACE_PROBE_PPS_VERBOSE
    cleanup: gate process_dump_all VGA half; lengthen TIMER-IDLE-STALL
    cleanup: gate 7e-fork behind RACE_PROBE_FORK_SLOT

---

## Working style -- how this project gets changed

Read this before proposing any command block.

**One change at a time.** A "change" is a commit, verified before
the next one starts.

**The state check comes before the command block.** Every command
block starts with `git status -sb` (or `git log`).

**A command block is one paste.** `git add`, `git status`,
`git commit`, and `git tag` belong in **one** block, with the
commit message inline via heredoc.

**Two file-return styles.** Small files: complete file,
four-backtick fenced. Large files: unified-diff hunk or
copy-block insertion, preceded by `WORK BEGINS HERE.`.

**Python edits with `count != 1` abort checks are the preferred
edit style.** **Put `set -e` at the top of the command block**
if the block edits and commits; without it, an edit script that
fails mid-way still runs the `git commit`, producing an orphan
commit.  This cycle paid for that lesson: an abandoned edit
script produced a commit that contained only one of the three
intended files.

**Ask for source you do not have.** Before patching a file it
has not seen **in this session**, ask for it.  **Before writing
an anchor, read the file with `cat -A` and quote the bytes.**
**Anchor on structure, not whitespace.**

**Idempotent edit scripts.** A Python edit that adds a block
must check whether the block already exists.  This cycle: the
`scheduler.c` statics block was added twice because the script
only guarded the last insertion.

**Never run `./run` while `git status` shows a conflict.**

**A diagnostic that saturates the serial console changes the
timing of the system it is trying to observe.**  This cycle
confirmed it four times.  The stall point moved from ~180 to
~1900 to ~5200 to ~7900 to ~20500 across the cycle, and each
move correlated with an instrument change.

**A diagnostic on the SUCCESS path is noise.  A diagnostic on
the FAILURE path is signal.**  This is why the cleanups gated
PPS-*, 7e-queue, and 7e-fork behind RACE_PROBE_* gates (default
0) but left TIMER-IDLE-STALL, TIMER-NO-NEXT, YIELD-TO-IDLE, and
the fault dumps unconditional.

**Commit diagnostics before any `git reset --hard`.**

**The QEMU monitor socket is a diagnostic tool.**  `qemu-system`
started with `-monitor unix:/tmp/donix-mon,server,nowait` can be
read with `socat - UNIX-CONNECT:/tmp/donix-mon`.

**GDB is the second diagnostic tool.**  `-s` on the QEMU
command line opens a GDB stub on tcp:1234.  `gdb
04_kernel_64bit/kernel.elf`, then `target remote :1234`.
`watch *(unsigned long long *)<addr>` sets a hardware
watchpoint.  **The QEMU monitor does not have a `watch`
command** -- only GDB.

**`nm` output moves between builds.**  Re-run
`nm 04_kernel_64bit/kernel.elf | sort` before using any address
from this file, a previous paste, or a commit message.

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static
musl-linked binaries on the Linux x86_64 syscall ABI, natively.
Newlib is gone. Userland is a tracked source tree at
`userland/musl/`. Phase B (busybox) is well underway.

Full strategy: `docs/strategy.md`. Direction: `ROADMAP.md`.

---

## Where we are

**Three bugs closed this cycle:**

1. **The 7e pipe wake race.** Closed by `c3e5c97`.
2. **The wait4 lost wakeup.** Closed by `364be3b`.
3. **The ready-queue self-link.** Closed by `cfff858`. The four
   P8 counters are the test: all zero in the patched tree.

**Two bugs remain open:**

### Bug 1 -- user-mode #PF with kernel RSP (VERIFIED)

Fault dump: `capture-fork-rsp-fault-20261012.txt`.  Key facts:

- Faulting RIP: `0x40786B` -- `mov 0x98(%rsp),%rbx` in busybox.
- Faulting RSP: `0xFFFFFFFF802A2640` -- a kernel stack address
  in slot 7 of `kernel_stack_pool`.
- CR2: `0xFFFFFFFF802A26D8` -- `RSP + 0x98`.
- Error code: `0x5` -- user-mode read of a present supervisor
  page.
- The `7e-fork` diagnostic did not fire on the fork preceding
  the fault, so the parent's user RSP was correct at fork time.
  The corruption happens after the fork.
- The fault fired on `canary --full`, row `echo hi | wc`.
- It is timing-dependent: the next run with the same binary
  passed 28/28.

**Candidate mechanism:** the user RSP slot in the syscall entry
frame, or the frame loaded by `context_switch.asm`'s
user-preempted save path, is being overwritten with a kernel
stack address between syscall entry and syscall return.

**Next-session instrument for Bug 1:** a runtime invariant check
in `user_syscall_entry.asm`, just before the `mov rsp, [rsp -
72]`.  Verify the loaded value is in `[0x8000000000,
0x8000100000)`.  If not, print `SYS-RETURN: bad user RSP =
0x...` and `process_get_current()->pid`, then `hlt`.  Scratch
change only; revert once the writer is named.  Pair with a GDB
hardware watchpoint on the child's user RSP slot at
`<child_kernel_stack_top> - 72`.

### Bug 2 -- the pipe7e stall (NOT RE-TESTED on the patched tree)

**This is the FIRST thing the next session must verify.  Do not
assume the eight fixes closed it.  They might have.  We do not
know, because pipe7e was not run on the final patched tree.**

Last observation (on a pre-cleanup build): stall at iteration
~20500.  Process list:

    4      pipe7e         BLOCKED bk=2 (pipe read)  entry=0x400652
    20591  PIPE7E_HELPER  RUNNING                    entry=0x40022B

The helper is `RUNNING` in **user mode** at `0x40022B`, which is
`_start` in the helper binary.  `_start` is five instructions
and a `call _start_c`; it is not a loop.  The helper's ELF entry
is:

    40022b: xor  %rbp,%rbp
    40022e: mov  %rsp,%rdi
    400231: lea  -0x400238(%rip),%rsi
    400238: and  $0xfffffffffffffff0,%rsp
    40023c: call 400241 <_start_c>

**The hypothesis:** the kernel is resuming the helper at its ELF
entry on each syscall return, not at the saved user RIP.  Every
syscall the helper makes (`write(1, "x\n", 2)`), the return path
loads RIP = `0x40022B` from the frame instead of the correct
post-syscall RIP.

**Candidates for the writer of the wrong RIP:**

1. The frame's `-56` slot (user RIP) is being overwritten
   between entry and return.  The slot is written by
   `push rcx` at syscall entry and read by `mov rcx, [rsp + 16]`
   at return.  Anything that pushes at or above the current RSP
   on the kernel stack during the syscall corrupts it.  The timer
   is the main candidate: it pushes 15 GPRs plus the CPU frame
   below the current RSP.  If the current RSP is at the wrong
   depth (because `g_syscall_stack_top` was changed, or because
   a preemption resumed the process at the wrong depth), the
   timer's pushes land on the frame's slots.

2. `g_syscall_stack_top` is being overwritten with a value in
   the middle of the process's kernel stack.  That would make
   the syscall entry push the frame at the wrong depth, and the
   return path read from the wrong slots.  Look at
   `tss_set_syscall_stack` and every writer.

3. `sys_execve`'s frame rewrite (`ktop[-7] = entry`) writes the
   ELF entry into the RIP slot.  That is correct for the first
   return.  If a subsequent syscall does not overwrite the slot
   (because the entry did not push `rcx` for some reason), the
   execve's entry value would be reused.  That is the shape
   described as "the execve-to-run handoff."  Verify whether
   the helper's syscall entry actually writes the `-56` slot.

**The run to make:**

    cd /home/noneya/code/patch_donix || exit 1
    ./run

At the shell, run `pipe7e` (see `userland/scripts/pipe_wake_probe.sh`
for the invocation, or `/usr/bin/PIPE7E` directly).  Watch for a
`TIMER-IDLE-STALL` banner and the process list.  If pipe7e
completes to a high iteration count without a stall, Bug 2 is
closed on the patched tree.  If it stalls with the helper at
`0x40022B`, Bug 2 is live and the next-session lead is the
syscall-return frame.

### Refuted -- do NOT re-walk

- **The pipe wake handshake** -- closed by `c3e5c97`.
- **The atomic refcount race** -- closed by `e1e8549`.
- **The stale-link queue membership** -- closed by `a0f9779`.
- **The timer's missing pool rescan** -- closed by `e3705d4`.
- **The reader-check print as a cause** -- timing perturbation only.
- **The wait4 lost wakeup** -- closed by `364be3b`.
- **The ready-queue self-link** -- closed by `cfff858`.
- **"Two RUNNING processes in one dump violates the one-current
  invariant."**  This was a previous handoff's claim and it is
  **wrong**.  `timer_preempt_handler` deliberately excludes
  pid 1 from the demote.
- **"The execve-to-run handoff is the lead"** was a previous
  handoff's framing.  It is a candidate for Bug 2, not a
  verified bug on its own.  Do not assume it is the cause until
  pipe7e is re-tested.
- Also do not re-open: item 7 (closed), item 7c (retracted),
  item 11 (closed), the session-48 zone scan, the `fs_base`
  path, the `longjmp`/`setjmp` family.

---

## NEXT SESSION

**Step 0: do not patch.  Verify first.**

Boot `patch_donix`, run `pipe7e`, capture the result.  Then
boot `donix`, run `pipe7e`, capture the result.  Diff the two.

- **If both complete:** Bug 2 is closed on the patched tree.
  Move to Bug 1 (the user-RSP corruption at `0x40786B`).
- **If both stall:** Bug 2 is live.  Work on the syscall-return
  frame.  Bug 1 is deferred.
- **If `donix` differs from `patch_donix`:** the patch is not
  equivalent and the trees need to be reconciled before anything
  else.

### Diagnostics to enable for the pipe7e verification run

The patched tree ships with the following diagnostic gates.  Do
NOT change them without a reason; these are the settings the
verification run needs.

**Leave ON (they fire only on the failure path or at stall):**

    RACE_PROBE_P1_BLOCK_WAKE   (race_probe.h)  -- initial dump once
    RACE_PROBE_P8_READYQ       (race_probe.h)  -- queue invariants
    RACE_PROBE_P11_WAKE        (race_probe.h)  -- wait4 wake path
    RACE_PROBE_P12_PIPE_CLOSE  (race_probe.h)  -- pipe slot close
    RACE_PROBE_P13_PIPE_TRACE  (race_probe.h)  -- per-pipe events

**Leave OFF (they fire on the success path and change timing):**

    RACE_PROBE_PPS_VERBOSE     (race_probe.h)  -- PPS-* pipe prints
    RACE_PROBE_DUMP_VGA        (race_probe.h)  -- process list on VGA

**Turn ON for the verification run only:**

    RACE_PROBE_FORK_SLOT       (race_probe.h)  -- 7e-fork print

The `7e-fork` diagnostic fires when `process_fork_copy_frame`
sees a non-zero slot at the child's user-RSP address.  It fired
exactly once on the last canary run (at the first fork, with a
benign `p_slot=0x1E`).  If the pipe7e stall involves a bad
fork-frame, this print will catch it.  One-line change:

    #define RACE_PROBE_FORK_SLOT 0   ->   1

Rebuild after flipping.  Turn it back to 0 before committing any
fix.

**Unconditional, compiled in, and must stay on:**

- The `TIMER-IDLE-STALL` banner in `timer_preempt_handler`
  (fires when the queue is empty for 10 minutes).
- The process list on serial (`process_dump_all`).
- The `#PF`, `#GP`, `#UD` frame dumps.
- The preemption ring (`race_log_dump`) printed from the fault
  handler.
- `PIPE RING` dumped at the stall banner.

**The one diagnostic to ADD before the run** (scratch, do not
commit):

In `04_kernel_64bit/user_syscall_entry.asm`, just before the
`mov rsp, [rsp - 72]` at the return path, verify the loaded
value is in `[0x8000000000, 0x8000100000)`.  If not, print
`SYS-RETURN: bad user RSP = 0x...` plus
`process_get_current()->pid` and `hlt`.  This catches the
user-RSP corruption (Bug 1) at the moment it would cause the
#PF at `0x40786B`, and names the process.  Revert after the
writer is identified.

**How to capture:** the `run` script already tees to
`capture.txt`.  For the verification, copy the result aside:

    cp capture.txt pipe7e-$(date +%Y%m%d)-$(hostname).txt

so a subsequent run does not overwrite it.  If the pipe7e run
completes without a stall, `TIMER-IDLE-STALL` will not fire and
the capture will be short.  If it stalls, the capture will
contain the stall banner, the process list, the RACE PROBE DUMP,
and (if it faults) the `#PF` frame.

**Step 1: write the check for whichever bug fires first.**
If Bug 2 fires, add a diagnostic in `user_syscall_entry.asm` that
prints the value at the frame's `-56` slot immediately before
`sysret`.  If it ever reads `0x40022B` for a process that has
already run past `_start`, that is the bug.  Then set a GDB
watchpoint on that slot.

If Bug 1 fires, add the runtime invariant check on the user-RSP
slot described above.

**Step 2: one change, one commit, then re-test.**

### The candidates, in order

1. **Bug 2 verification (pipe7e).**  The FIRST action.
2. **Bug 1 (user-RSP corruption at `0x40786B`).**  Deferred until
   Bug 2 is resolved or ruled out.
3. **Item 12 -- signal delivery.**  Largest; unblocks `kill`,
   ash job control, the Wayland `SIGBUS` path.  A workstream.
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

---

## Open issues (top 6)

1. **Bug 2 -- the pipe7e stall.**  Not re-tested on the patched
   tree.  Verify first.
2. **Bug 1 -- user-mode #PF with kernel RSP at `0x40786B`.**
   Timing-dependent.  Capture in
   `capture-fork-rsp-fault-20261012.txt`.
3. **`open-issues.md` item 12: signal delivery is a stub.**  The
   largest single target.
4. **`open-issues.md` item 10: `CR4.OSFXSR` without an XMM
   save.**
5. **`open-issues.md` item 13: `CONFIG_ASH_JOB_CONTROL` gap.**
6. **`open-issues.md` item 9: no privilege model.**
   **`sys_gettimeofday` (96)** so `ps -l` works.  Small.

---

## State on disk

Kernel sources: `04_kernel_64bit/`.  `capture.txt` is gitignored;
one run per truncate.

The pre-patch backup `donix-before-apply-20261012` (or its
tarball) holds the un-patched donix at `5537465` and the
capture `capture-fork-rsp-fault-20261012.txt`.

The script `apply-fixes-to-donix.sh` lives at
`/home/noneya/code/apply-fixes-to-donix.sh` (or a copy in
`docs/`).

`patch_donix` at `/home/noneya/code/patch_donix/` is the
scratch tree used to build and verify the cherry-pick sequence.
It matches the patched `donix` in tree content.  If it still
exists, boot it for the Bug 2 verification.

---

## One-line summary

**donix is patched with eight fixes and five cleanups.  Three
bugs closed.  Two open: (1) a user-mode #PF at `0x40786B` with
a kernel `%rsp`, captured; (2) the pipe7e stall, last seen on a
pre-cleanup build at iteration ~20500 with the helper RUNNING
at `0x40022B` (its `_start`), not re-tested on the patched
tree.  The FIRST action of the next session is to boot
`patch_donix` and `donix`, run pipe7e on each, and compare.  The
`7e-fork` diagnostic (`RACE_PROBE_FORK_SLOT`) must be turned ON
for that run.  Add the syscall-return user-RSP invariant check
before the run.  Do not patch anything until that verification
is done.**

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

**Gotchas worth reading before the next change.**  "A fix with
no test is indistinguishable from an unfixed defect."  "A test
can encode an earlier version's behavior."  "A consumer inferred
from behavior is not a consumer."  Session 49's: **a function
that has never run is correct by inspection only.**  Session
51's: **a whole-file return for a large non-ASCII file
re-encodes it**; and **print the row marker before the row
runs.**  Session 52's: **a test's expected value is a claim.**
Session 53's: **a diagnostic declared but never wired is not a
diagnostic.**  Session 54's two: **a halt in a fault handler is
a diagnostic that never runs**; and **put cleanup in the
teardown, not each entry point.**  Session 55's: **a fork that
overwrites a child's file table leaks per fork**; and **a
captured fault is not a diagnosed one.**  Session 57's:
**hand-written `@@` hunk headers are a trap**; and **a type's
named fields are not what the pointer points at.**  Session
58's: **a page walk that descends through a supervisor level
prints a confident wrong verdict**; and **a diagnostic on the
fault path is part of the system.**  Session 59's three: **a
Python edit with a `count != 1` abort check catches an
ambiguous anchor and writes nothing; a handler on the fault
path with a corrupt `rsp` cannot run, so a redirect without a
re-entry guard loops forever; and reading the disassembly
found the mechanism a dozen trace-readings had missed.**
Session 59's meta-gotcha: **a section title is a claim about
its body, and a commit subject is a claim about its diff --
read the body, read the diff.**  Session 62's three: **a race
is not memory corruption -- there is no store to find, so do
not hunt the writer; a session number, a comment, and an
address pairing are claims too -- count it, read the body, and
quote the bytes; and before writing an edit anchor, read the
file with `cat -A` and quote the bytes.**  This cycle's meta:
**a diagnostic that saturates the serial console changes the
timing of the system it is trying to observe**; **`git reset
--hard` discards uncommitted work with no recovery path other
than `git fsck` -- commit diagnostics before resetting**;
**anchor on structure, not whitespace**; **a Python edit with
a `count != 1` guard writes nothing, but without `set -e` the
command block keeps going and commits a partially-edited
tree**; **an edit script's idempotency guard must cover every
insertion**; **`nm` output moves between builds -- re-run it
before every watchpoint address**; **the QEMU monitor has no
`watch` command; use GDB `target remote` for hardware
watchpoints**; and **a diagnostic on the SUCCESS path is
noise; a diagnostic on the FAILURE path is signal.**  And
this cycle's most expensive lesson: **do not write a handoff
that claims a bug is closed based on a test that was never
run on the final tree -- the pipe7e stall was declared "the
next-session lead" and then dropped from the handoff before
the tree was ever re-tested.**

**This file's item text is the same kind of claim.**
