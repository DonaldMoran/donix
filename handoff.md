# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-14 (long session).  The queue-mutator race
is CLOSED and tagged.  The `pipe7e` `C` is OPEN at two sites; both
have guards now, and the second is being tested as this file is
written.  Read "NEXT SESSION" first.

**THE HEADLINE: `c85d074` closed the queue-mutator race.**
`scheduler_ready_queue_add` had no `cli` and is called with
interrupts on from `process_yield`, so a timer tick could fire
mid-add and nest another `_add`/`_remove` inside it, leaving a tail
move with no matching clear.  Wrapping `_add`, `_remove`, and
`_next` in save-flags/`cli`/restore-flags made each one a critical
section.  Verified: `bad_tail_next = 0` across 3,098,489 P8 checks
(3.1 million) in a `pipe7e` run that reached `iter 80500`.

**The queue race is closed and tagged.**  Tag
`20261014-queue-race-closed` on `3eb1d61`.

**THE OPEN BUG: a RUNNING process dispatched as `next` — two
sites, two guards, unverified.**  `pipe7e` reached `iter 80500` and
hit `context_switch.asm`'s unknown-CS halt:

    C
    N=FFFFFFFF8027C7D0 R=FFFFFFFF8027C7D0 S=FFFFFFFF8029B2A0
    F=FFFFFFFF8011B895 P=0000000000000000

`N == R`: `next` is `C7D0` and the frame's RIP slot holds `C7D0`.
`S` is a kernel stack address, `F` is `process_exit+757`, `P` is
0 — the values of `context_switch`'s own locals on the stack, not a
resume frame.  The dispatch was `context_switch(next, next)`:
`prev == next`.

**Site 1 — `scheduler_switch_to`.**  Its self-switch check at
function entry reads `current_process` with interrupts **on**.  A
timer tick between that check and the `cli` can set
`current_process = next`.  **Fixed by `7fff450`** — a second
`next == current_process` re-check inside the `cli`, before the
RUNNING guard.  **`7fff450` did not close the `C`** — it fired
again on a run with the fix in the tree, capture lost; a later run
was clean.

**Site 2 — `timer_preempt_handler`.**  The timer does **not** call
`scheduler_switch_to`; it returns `next->rsp` directly to
`irq0_stub`, which does `mov rsp, rax`.  `scheduler_ready_queue_next()`
can pop `current`: its guard skips a RUNNING head, but a peer wake
can set `current->state = READY` between the pop and the use, and
the kernel-mode branch's pool rescan re-adds a stranded RUNNING
`current`.  When `next == current`, the quantum block set
`current->state = READY`, re-added `current`, set
`next->state = RUNNING` (the same process), and returned
`next->rsp` — the frame this tick had just saved.  `irq0_stub`
popped its own frame.  **Fixed by `e21ebf0`** — a self-pick now
resumes `current`'s own frame as a no-op switch.  **Being tested
as this file is written.**

**Do not call either guard verified until `pipe7e` reaches
`iter 80500` clean on `capture.txt`, twice.**

**Repo state -- run these; do not write it here.**

    cd /home/noneya/code/donix || exit 1
    git log --oneline -8
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
`cat > ... <<'SCRIPT_EOF'`, then run `bash /tmp/<name>.sh`.

**Two file-return styles.** Small files: complete file, four-backtick
fenced. Large files: unified-diff hunk or copy-block insertion,
preceded by `WORK BEGINS HERE.`.

**Python edits with `count != 1` abort checks are the preferred edit
style.** **Put `set -e` at the top of the script** if the block edits
and commits.  **Idempotent edit scripts.** A Python edit that adds a
block must check whether the block already exists.

**Count occurrences before asserting a count.**  A verification
check whose expected count is wrong makes a correct edit look
broken.  Three times this session a check failed on a wrong
expected count, not on a broken edit.  Check the actual count
first.

**A triple-quoted string in a Python edit must be closed before the
next statement.**  A missing `""", 1)` produced a `SyntaxError` that
consumed the next line.  The tree is unchanged when a script fails
this way -- a `SyntaxError` means nothing ran.

**A post-write check string must be copied from the `new` literal
in the edit script, not retyped.**  Retyping loses a backtick or a
parenthesis and fires a spurious `ERROR:`.

**Ask for source you do not have.** Before patching a file it has
not seen **in this session**, ask for it.  **Before writing an
anchor, read the file with `cat -A` and quote the bytes.** **Anchor
on structure, not whitespace.**

**A diagnostic that saturates the serial console changes the timing
of the system it is trying to observe.  A diagnostic on the SUCCESS
path is noise.  A diagnostic on the FAILURE path is signal.**

**Commit diagnostics before any `git reset --hard`.**

**The QEMU monitor socket is a diagnostic tool.**  `qemu-system`
started with `-monitor unix:/tmp/donix-mon,server,nowait` can be
read with `socat - UNIX-CONNECT:/tmp/donix-mon`.  **Under
`-enable-kvm`, the monitor's `xp` cannot translate guest kernel
virtual addresses -- use `x` instead.**

**GDB over the `-s` stub is the read for the live or halted PCB.**
`gdb 04_kernel_64bit/kernel.elf`, then `target remote :1234`.
**Connecting halts the guest; detach resumes it.**  Do not attach
during a timing-sensitive run -- a pause changes the timing.  Attach
after the run finishes, or immediately when it stalls.

**`nm` output moves between builds.**  Re-run
`nm 04_kernel_64bit/kernel.elf | sort` before using any address.

**Two identical register reads do not distinguish "hung" from
"livelocked at high frequency."  The tick counter (`g_ticks`) does.**

**A frame slot holds a specific kind of value.**  A PCB field that
is the same width may be a different kind.  Check the kind before
moving a value into a slot.

**A field's comment is a claim about its use.**  `pcb->user_rsp`'s
comment said "the USER stack pointer" while the writer put a kernel
address in it.  The comment was right; the writer was wrong.

**The build does not have `-Werror`; a warning is not a failure.**
An `unused variable` warning from a gated block is fine.

**Verify a label's scope before adding a second non-local label to
an asm file.**  NASM scopes `.`-labels to the most recent non-local
label.  A second entry point needs a non-local target for both to
reach.

**A faulting `C` that is not captured is a run that has to be
repeated.**  Run `pipe7e` with the serial teed to `capture.txt`.
The handoff's own rule: "A captured fault is not a diagnosed one."

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static
musl-linked binaries on the Linux x86_64 syscall ABI, natively.
Newlib is gone. Userland is a tracked source tree at
`userland/musl/`. Phase B (busybox) is well underway.

Full strategy: `docs/strategy.md`. Direction: `ROADMAP.md`.

---

## Where we are

### CLOSED -- the queue-mutator race (`c85d074`)

**The race was real and is now closed.**  `scheduler_ready_queue_add`
had no `cli` and is called from `process_yield` with interrupts ON.
A timer tick mid-add runs `process_wake_all_blocked`, which calls
`_add` and `_remove` -- so one queue mutation could nest inside
another, leaving a tail move with no matching clear.

**The fix:** `_add`, `_remove`, `_next` each save RFLAGS, `cli`, do
their work, and restore RFLAGS on every return path (including the
early returns).  The whole function is one critical section.

**The evidence:** `bad_tail_next = 0` across 3,098,489 P8 checks in
a run that reached `iter 80500`.  Before the fix it reached 500+ over
100,000 iterations.  The two earlier fixes (`ecae22a` add link order,
`93eda39` remove tail clear) closed genuine transient windows but
were not the producer; the nesting was.

**Tagged** `20261014-queue-race-closed` on `3eb1d61`.

### CLOSED -- the pipe reader's lost wakeup (`7aaaafd`)

`sys_read`'s pipe loop cleared `pipe->reader_waiting` at the top of
every iteration and re-published at the block site, in a different
`cli` section.  A writer in the gap found NULL and did nothing.
Removed the self-clear; the reader leaves the field set across
wakeups and re-publishes at each block.

### CLOSED -- the `test.sh` row-30 `C` (`d3c7f20`)

`scheduler_switch_to`'s RUNNING-next guard read `next->state` above
the `cli`; a tick changed it before the `cli`; the dispatch resumed
a stale frame.  Moved the guard inside the `cli`, with `sti` on the
refuse path.  `test.sh` 57/57 and `canary --full` 28/28 after it.

### CLOSED -- Bug 1, the user-RSP writer (`d6ada2f`)

`timer_preempt_handler`'s user-mode branch wrote `stack_pointer` (the
kernel frame base) into `pcb->user_rsp`.  The reader is
`context_switch.asm`'s user-preempted save path, which pushes it
into a resume frame's RSP slot; `iretq` loads it with CS=0x33.  The
fix writes `frame->rsp`, the CPU-pushed user RSP.

### CLOSED -- Bug 2, the `process_exit` hlt-guard resume (`6bfa008`)

`process_exit`'s `call context_switch` built a `.ring0_save` frame
whose RIP was the compiler's noreturn `hlt` guard.  Added
`context_switch_no_save`, a hand-off entry point that shares the
prologue and resume side but skips the save block.

### CLOSED -- the four scheduler dispatch guards

`8dc47cf`, `85da375`, `e7e3e6b`, `d7d8c40`: a RUNNING process must
not be dispatched as `next`, at pop time, at `scheduler_switch_to`,
and at `process_exit`'s hand-off.

### OPEN -- Bug 3: a RUNNING process dispatched as `next`

**Two sites, both guarded, neither verified.**  Site 1
(`scheduler_switch_to`) guarded by `7fff450`; site 2
(`timer_preempt_handler`) guarded by `e21ebf0`.  The `C` line's
`N == R` is the signature.  **The test is running as this file is
written.**  Do not call it closed until `pipe7e` reaches
`iter 80500` clean on `capture.txt`, twice.

### OPEN -- item 7a, the boot-time `#PF` at `0x400000`

Unobserved, not fixed.  The `20261003-vmm-map-diag` instrumentation
is in the tree and did not fire on the boots that followed.  When it
returns, the `free` field separates the two candidates.

### OPEN -- item 7b, a user-mode `#GP` at `0x42F1A7` in busybox

Unobserved, not fixed.  Different vector, location, and phase from
7a.  Needs its own instrument.

---

## Refuted -- do NOT re-walk

- The queue-mutator race as an *unfixed* bug -- **closed by
  `c85d074`.**
- The pipe wake handshake -- closed by `c3e5c97`.
- The atomic refcount race -- closed by `e1e8549`.
- The stale-link queue membership -- closed by `a0f9779`.
- The timer's missing pool rescan -- closed by `e3705d4`.
- The wait4 lost wakeup -- refuted; `r3` refusals are the normal
  "child exited before parent blocked" count, the code is correct.
- Framing (c) for Bug 2 -- refuted by `pipe7e`, commit reset.
- The `.ring0_save` RIP-only and RIP+RSP changes -- refuted.
- **The pipe-reader loop-back as the `C`'s site** -- refuted.  The
  `C` is `context_switch(next, next)`, not a pipe wake.
- **The timer-frame-copy fix** -- refuted.  The frame is not
  clobbered by the C stack; it is the self-pick.
- **The queue instrumentation as the producer** -- the snapshot
  fixes (`8e3c53f`, `441dd1d`) and the exit checks (`9db304d`) were
  real fixes to the *instrument*, not the queue.
- Also do not re-open: item 7, item 7c, item 11, the session-48 zone
  scan, the `fs_base` path, the `longjmp`/`setjmp` family.

---

## NEXT SESSION

**Step 0: read the running `pipe7e` result.**

- **Clean to `iter 80500` (and to `iter 100000` if the test goes
  that far):** run it a second time.  Two clean runs close Bug 3.
  Then `test.sh` 57/57 and `canary --full` 28/28, then tag the
  verified state.
- **`C` again with `N == R`, `S` a kernel stack address:** the guard
  did not catch the path.  The next read is `S` and `F`: if
  `S == N` the frame base is the PCB itself; if `S` is a kernel
  stack address, the resume loaded a stack address from somewhere
  that is not `pcb->rsp`.
- **`C` with a different shape:** a third site.  New evidence; add a
  third entry to the top block.

**Step 1: the regression set** -- `test.sh` 57/57, `canary --full`
28/28 -- and tag the verified state.

**Step 2: the candidates, in order.**

1. **Item 12 -- signal delivery.**  Largest; unblocks `kill`, ash
   job control, the Wayland `SIGBUS` path.  A workstream.
2. **`sys_gettimeofday` (99).**  Small, patterned, independent.
   Unlocks `ps -l`.
3. **Item 10 -- the XMM save.**  Kernel-side FXSAVE/FXRSTOR, then
   drop `-mno-sse*` from the userland CFLAGS.
4. **The `process_dump_all` `DETACHED` state** -- a process READY
   with `prev == NULL && next == NULL` is labeled DETACHED, which
   may mask a stranded-process bug.

### Do not

Do not apply the script or re-run the cherry-pick sequence.  The
tree is already patched.  Do not chase the pipe wake path (closed),
the atomic refcount (closed), the `fs_base` path, item 7, item 11,
or the session-48 zone scan.  One change at a time.  Do not edit
`third_party/` -- but do read it.

Do not run `git reset --hard` without first committing every
diagnostic you care about.

Do not attach GDB to a run that is progressing -- a pause changes
the timing.  Attach after it finishes or when it stalls.

Do not call either `C` guard verified on one clean run.  The `C` is
intermittent; one clean run does not settle it.

Do not propose a `.ring0_save` fix -- Bug 2 is closed by `6bfa008`.

---

## Open issues (top 6)

1. **Bug 3 -- a RUNNING process dispatched as `next`.**  Two sites,
   two guards (`7fff450`, `e21ebf0`), unverified.  The read is the
   `pipe7e` result.
2. **`open-issues.md` item 12: signal delivery is a stub.**
3. **`open-issues.md` item 10: `CR4.OSFXSR` without an XMM save.**
4. **`open-issues.md` item 13: `CONFIG_ASH_JOB_CONTROL` gap.**
5. **`sys_gettimeofday` (99)** so `ps -l` works.  Small.
6. **`process_dump_all`'s `DETACHED` state** -- may mask a
   stranded-process bug.

---

## State on disk

Kernel sources: `04_kernel_64bit/`.  `capture.txt` is gitignored;
one run per truncate.

Tags from this cycle, local only:
`20261013-session-start` (`04df9eb`),
`20261013-queue-and-rescan-fixes`,
`20261013-userrsp-writer-fix` (`d6ada2f`),
`20261014-pipe7e-100k-clean` (`7aaaafd`),
`20261014-test57-canary28-clean` (`d3c7f20`),
`20261014-queue-race-closed` (`3eb1d61`).

---

## One-line summary

**`c85d074` closed the queue-mutator race -- `_add`, `_remove`,
`_next` are critical sections, and `bad_tail_next` stayed 0 across
3.1 million checks.  Tag `20261014-queue-race-closed`.  Bug 3 is
open: a RUNNING process dispatched as `next`, with the `C` line's
`N == R` as the signature.  Two sites, two guards -- `7fff450` for
`scheduler_switch_to`, `e21ebf0` for `timer_preempt_handler` -- and
neither is verified.  The `pipe7e` result being captured now decides
it.  Item 12 (signal delivery) is the next workstream if Bug 3
closes.**

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
"A test's expected value is a claim."
"A diagnostic declared but never wired is not a diagnostic."
"A halt in a fault handler is a diagnostic that never runs."
"Put cleanup in the teardown, not each entry point."
"A fork that overwrites a child's file table leaks per fork."
"A captured fault is not a diagnosed one."
"A captured fault that is not written to disk is a run that has to
be repeated."
"Hand-written `@@` hunk headers are a trap."
"A type's named fields are not what the pointer points at."
"A page walk that descends through a supervisor level prints a
confident wrong verdict."
"A diagnostic on the fault path is part of the system."
"A section title is a claim about its body, and a commit subject is
a claim about its diff -- read the body, read the diff."
"A race is not memory corruption -- there is no store to find."
"A session number, a comment, and an address pairing are claims too."
"Before writing an edit anchor, read the file with `cat -A` and quote
the bytes."
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
"A command block written as a script is not safe to paste into an
interactive shell; `exit` in the paste closes the terminal."
"Under `-enable-kvm` the QEMU monitor's `xp` cannot translate guest
kernel virtual addresses; use `x`, or GDB over the `-s` stub."
"Two identical register reads do not distinguish 'hung' from
'livelocked at high frequency'; the tick counter (`g_ticks`) does."
"A `strtonum`/`awk` symbol lookup can return a confidently wrong
nearest symbol; a lexical range filter over equal-length hex `nm`
output is the reliable check."
"A frame slot holds a specific kind of value; a PCB field that is the
same width may be a different kind."
"The tag must be created after the commits it names; a tag created
before them names the wrong tree."
"A field's comment is a claim about its use."
"Two scripts that differ by one character pair will be pasted wrong."
"A guard that aborts when its marker string is absent will silently
no-op on the present-and-correctly-formed case if the marker carries
the wrong delimiters."
"A post-write verification string that does not quote the inserted
text verbatim is a false negative, not a failure."
"A count of writers derived from the files you have read is not a
count of writers."
"A framing that looks safe by code reading can be refuted by a
run."
"A post-write check string must be copied from the `new` literal
in the edit script, not retyped."
"**A verification check with a wrong expected count makes a correct
edit look broken.  Three times this session a check failed on a wrong
count.  Count occurrences before asserting a count.**"
"**A triple-quoted string in a Python edit must be closed before the
next statement; a missing `"""` produces a `SyntaxError` that
consumes the next line.  The tree is unchanged.**"
"**An `#if` block that gates a `static` variable leaves it
unreferenced; the compiler warns but the build does not fail.**"
"**A critical section for a queue must cover the whole function,
not just the link.  The nesting window is between the guard and the
write, and every early return must restore the interrupt flag.**"
"**A RUNNING process on the ready queue is dispatched as `next` and
resumed from a stale frame.  The window is any restore of
`state = RUNNING` followed by a loop-back to a re-block.**"
"**Two sites call `context_switch`: `scheduler_switch_to` and
`timer_preempt_handler`.  A guard at one is not a guard at both.**"
"**The `C` line's `S` slot is the CS slot and is the test; `R` is
context, not the test.  `N == R` is the self-dispatch signature.**"
"**`race_head[0]` is a write counter; the last-written index is
`(race_head[0] - 1) % 256`, not `race_head[0] % 256`.**"
"**`process_initialize_pcb` `memset`s the whole PCB, so a reused
PCB carries no stale fields.  `kernel_stack_slot_free` does not
clear the stack, so a reused slot does carry stale frames.**"
"**`preempt_frame_t` matches `PUSH_ALL_GPRS` exactly; a frame-layout
bug is not the `C`'s cause.**"

**This file's item text is the same kind of claim.**
