# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-13 (second session).  The user-RSP writer
fix is committed and tagged, and the Bug 1 reproduction it targets is
cleared.  `pipe7e` regression is pending.  Two threads remain open:
Bug 2's design question and the queue-mutator race.  Read "NEXT
SESSION" first.

**THE HEADLINE: `d6ada2f` (tag `20261013-userrsp-writer-fix`)
changed the user-mode preemption in `timer_preempt_handler` to write
`frame->rsp` -- the CPU-pushed user RSP -- into `pcb->user_rsp`,
instead of `stack_pointer`, the kernel interrupt-frame base.
`pcb->user_rsp`'s only reader is `context_switch.asm`'s
user-preempted save path at offset 0x610, which pushes it into the
resume frame's RSP slot; `iretq` loads that with CS=0x33.  A kernel
address in that slot faults on the next user stack access -- Bug 1's
exact shape.**

**The fix is verified on Bug 1's known reproduction.**  The
2026-10-13 reproduction was `sh /root/scripts/test.sh` row 12
(`uniq`), faulting at `RIP=0x412835`, `RSP=0xFFFFFFFF802A2640`,
`CS=0x33`, `CR2=RSP`, error code `0x5`.  After `d6ada2f`, that row
passes, twice.  `canary --full` passes 28/28 three times.  No
user-mode #PF, no #GP, no fault of any kind in the post-fix capture.

**The fix is not a regression check yet.**  `pipe7e` -- the workload
that has stressed this class of bug hardest -- has not been run on
the fixed tree.  That is the next session's first action.

**One diagnostic fired, and it is not a fault.**  A `YIELD-TO-IDLE`
queue dump appeared once during `test.sh` row 6, showing `idle`
RUNNING, `busybox` pid 94 RUNNING, `busybox` pid 66 BLOCKED,
`busybox` pid 3 BLOCKED, `musl_sh` pid 2 BLOCKED, `busybox` pid 96
ZOMBIE.  The test continued and passed.  This is the item-7d
scheduler state -- the ready queue transiently emptied and the
rescan re-added what it could -- and it is the visible surface of
the queue-mutator race, still open.  It is NOT Bug 1: no fault
occurred.

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

**A field's comment is a claim about its use.  `pcb->user_rsp`'s
comment said "the USER stack pointer" while the only writer put a
kernel address in it.  The comment was right; the writer was wrong.**

**Two scripts that differ by one character pair will be pasted
wrong.**  If a script and its correction are near-identical, delete
by hand instead of re-pasting.  This session spent three attempts on
a marker string (`/* DIAG-... */` vs `DIAG-...`) before switching to
a manual edit.

**A guard that aborts when its marker string is absent will silently
no-op on the present-and-correctly-formed case if the marker carries
the wrong delimiters.**  Match the smallest substring that is
actually present, and re-verify after the write.

**A post-write verification string that does not quote the inserted
text verbatim is a false negative, not a failure.**  Quote the
check string from the edit itself, not from a paraphrase of the
edit's intent.

**A count of writers derived from the files you have read is not a
count of writers.**  Grep the whole tree before writing the number
into the handoff.  This session's Bug 2 enumeration said "four `rip`
writers" until `grep -n '\->rip *=\|\->rsp *='` over
`user_syscall.c` found the fifth, in `sys_execve`.

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static
musl-linked binaries on the Linux x86_64 syscall ABI, natively.
Newlib is gone. Userland is a tracked source tree at
`userland/musl/`. Phase B (busybox) is well underway.

Full strategy: `docs/strategy.md`. Direction: `ROADMAP.md`.

---

## Where we are

**The user-RSP writer fix is committed and tagged.**

### Bug 1 -- user-mode #PF with kernel RSP -- REPRODUCTION CLEARED

**Symptom.**  Fault at `RIP=0x412835` (user), `RSP=0xFFFFFFFF802A2640`
(kernel), `CS=0x33`, `CR2=RSP`, error code `0x5` (user read of a
present supervisor page).  Reproduced 2026-10-13 in
`sh /root/scripts/test.sh`, row 12 (`uniq`).  Same shape as the
2026-10-12 capture at `0x40786B`.

**Root cause, now named in code.**  `interrupts.c`'s user-preempted
save block had:

    current->user_rsp = stack_pointer;

`stack_pointer` is the kernel interrupt-frame base that `irq0_stub`
passed to `timer_preempt_handler`.  `pcb->user_rsp`'s only reader is
`context_switch.asm:66`:

    push qword [r15 + 0x610]   ; RSP [0x90] <- pcb->user_rsp

which is the resume frame's RSP slot, loaded by `iretq` with CS=0x33.
The kernel address was delivered into user-mode `%rsp`, and the next
user stack access faulted on a supervisor page.

**The fix.**  `frame->rsp` is the CPU-pushed user RSP (EXC_OFF_RSP =
19), already in the frame the handler holds.  Write that.
`current->rsp = stack_pointer` is unchanged: that field is the
kernel resume-frame base and the existing comment explains why it
takes the frame base.

**Observed at boot, on the first user-mode preemption (pid 3, ash):**

    DIAG-USER-RSP: stack_pointer=0xFFFFFFFF80292710
                   frame->rsp=0x00000080000FBF10
                   frame->cs=0x0000000000000033 pid=3

`stack_pointer` is the same shape as the faulting RSP; `frame->rsp`
is a user address in [0x8000000000, 0x8000100000).

**Post-fix verification.**  `canary --full` 28/28, three times.
`sh /root/scripts/test.sh` 57/57, twice.  No fault in either.
Row 12 (`uniq`) -- the exact reproduction -- passes.

**Still pending:** `pipe7e` on the fixed tree.

### Bug 2 -- the `.ring0_save` resume frame -- WRITER NAMED, DESIGN QUESTION OPEN

**Mechanism.**  `context_switch.asm`'s `.ring0_save` builds a resume
frame whose RIP slot is `[rsp + 48]` -- the caller's return address
of `call context_switch`.  For `process_exit`'s Site-1 caller, that
return address is the `nopl` after the call, which falls into the
`hlt` guard at `process_exit+0x100`.  A process saved there and
later resumed resumes at the guard and stops.

**The 2026-10-13 fixes made this a livelock rather than a deadlock:**
the timer is alive, the queue is repaired, the helper is re-run, and
it resumes into the `hlt` again.  The system cycles rather than
hangs.

**The two failed fix attempts and exactly why each failed:**

- **RIP-only**: change `.ring0_save`'s RIP to `prev->rip`, leave RSP
  as `lea rax, [rsp + 64]`.  Produced a kernel-mode `#UD` at `0xBF`.
  The RIP and RSP slots described two different moments; the `iretq`
  popped garbage.  **Refuted.**
- **RIP+RSP**: change RIP to `prev->rip` and RSP to `prev->rsp`.
  **Not run -- caught by reading the frame-build downstream.**
  `prev->rsp` is a *frame base*, and the frame's RSP slot holds the
  *stack address* the `iretq` loads.  **Category error.**

**THE LAST-WRITER READ IS DONE.**  Enumerating every writer:

*`pcb->rip` writers (five):*
1. `process_create`: `pcb->rip = entry_point;` -- a user address, once.
2. `timer_preempt_handler`, kernel-mode branch: `current->rip =
   frame->rip;` -- a kernel address.
3. `timer_preempt_handler`, user-mode branch: `current->rip =
   frame->rip;` -- a user address.
4. `process_fork_copy_frame`: `child->rip = p_rip;` -- the parent's
   user RIP, once.
5. `sys_execve` (section 8, `self->rip = entry;`) -- the new
   program's entry, a user address.  Execve also rewrites the
   syscall-entry frame's RIP slot (`ktop[-7] = entry;`), so it
   writes RIP in TWO places; see the finding below.

*`pcb->rsp` writers (six):*
1. `process_create`: a kernel frame base, once.
2. `timer_preempt_handler`, kernel-mode branch: `stack_pointer`, a
   kernel frame base.
3. `timer_preempt_handler`, user-mode branch: `stack_pointer`, a
   kernel frame base.
4. `.ring0_save`: `mov [r15 + 0x110], rsp` -- the frame base of the
   resume frame it built.
5. user-preempted save path: `mov [r15 + 0x110], rsp` -- the frame
   base of the resume frame it built.
6. `process_fork_copy_frame`: the child's frame base, once.

`sys_execve` writes `pcb->rip` but NOT `pcb->rsp`: it rewrites
the syscall-entry frame's RSP slot (`ktop[-9] = rsp_init;`)
directly and leaves `pcb->rsp` untouched.  So the execve path
maintains RIP in the PCB and in the frame, and maintains RSP
only in the frame.

**The finding.**  Every writer of `pcb->rsp` writes a **kernel
address** -- `rsp` is consistently "kernel frame base."  But the
two fields are not maintained as a pair by any single discipline:

- **two paths write `rsp` and not `rip`**: `.ring0_save` and the
  user-preempted save path.  Both build a resume frame and record
  its base, but neither consults or updates `pcb->rip`.
- **one path writes `rip` and not `rsp`**: `sys_execve`, which
  writes `self->rip = entry` and rewrites the frame's RSP slot
  directly, leaving `pcb->rsp` stale.

So "what should `pcb->rip` / `pcb->rsp` hold right now" is
unanswerable from the PCB alone -- it depends on which path last
touched either field, and the paths do not agree on whether the two
fields move together.  `.ring0_save` is the path that breaks
`process_exit`'s Site-1, but the asymmetry is a property of the
write discipline, not of `.ring0_save` alone.  `.ring0_save` builds
a self-consistent frame (it chooses its own RIP, from `[rsp + 48]`),
but the RIP it chooses is the return address of `context_switch`'s
caller, which for `process_exit`'s Site-1 caller is not a resumable
point.

**The correct pair for a ring-0 resume** is therefore: `rip` = the
kernel address the process should resume at, and `rsp` = the kernel
frame base of a frame whose RIP slot holds that same address.
`.ring0_save` produces a frame satisfying the second half but not
necessarily the first -- it does not consult `pcb->rip` at all.

**The design question, which is what the next session must answer
before proposing a fix:**  *Should `.ring0_save` be resumable at
all?*  Two framings:

- **(a)** `.ring0_save` is the wrong tool for `process_exit`'s
  Site-1, and the fix is to not call `context_switch` from a site
  whose return address is not a resumable point.  The exit path
  would hand off differently.
- **(b)** `.ring0_save` should be resumable, and the fix is to give
  it a `pcb->rip` to resume at, written by whatever path last saved
  the process -- which requires a "which path last saved this" flag,
  because `.ring0_save` cannot tell whether the timer saved the
  process or it is saving it itself.
- **(c)** `.ring0_save` should not save a process that has no resume
  meaning.  `process_exit` sets `exiting->state =
  PROC_STATE_TERMINATED` before calling `context_switch`, and a
  terminated process is never resumed.  The PCB already carries
  the fact that matters; check it at the top of `.ring0_save` and
  jump to `.skip_save` if set.  No new field, no flag, no
  caller-side bookkeeping.

**Framing (c) was chosen this session, implemented, and refuted by
the run.**  The implementation is described under "Refuted"
below; the short version is that it made the stall worse, not
better, and the commit was reset away rather than reverted.

**THE STALL IS LIVE AND CAPTURED.**  A later run of the same
session stalled at `iter 37000` and was read live with GDB over
the `-s` stub.  The frame decode is below; it confirms the
mechanism `.ring0_save` builds a resume frame with the caller's
return address as RIP, and for `process_exit`'s caller that
address is the `while(1) hlt` guard.

**The captured state (post-`d6ada2f`, post-(c)-reset):**

    current_process  = &pcb_pool[4], pid 37822, state RUNNING
    ready_queue_head = 0
    ready_queue_tail = 0
    g_ticks          = 0xa2fba at attach, 0xa355a after 14s
    (the tick counter advanced -- the system is livelocked,
     not frozen; the CPU is looping in the hlt)

**The stalled process's resume frame** (`pcb->rsp` =
`0xffffffff8029a6c0`), decoded by slot:

    +0x78 RIP    = 0xffffffff8011b531   process_exit's hlt guard
    +0x80 CS     = 0x18                 kernel code segment
    +0x88 RFLAGS = 0x202                standard IF=1
    +0x90 RSP    = 0xffffffff8029a760   kernel stack, frame+0xa0

CS=0x18 means `.ring0_save` built the frame (the user-preempted
path builds CS=0x33).  RSP = frame base + 0xa0 is the full
20-slot frame size, so the RSP slot is the caller's stack past
the frame -- exactly what `.ring0_save`'s `lea rax, [rsp + 64]`
computes.  The frame is well-formed: RIP, CS, RSP, RFLAGS are
all the right kind of value for the slots they are in.  The
frame is not malformed; it is a valid resume frame for the
wrong resume point.

**What the pool walk ruled out, for this stall:**

  - `g_bad_user_rsp` is 0: the syscall-return invariant check
    did not fire.  The syscall-entry frame is NOT the source.
  - The stalled process's `pcb->user_rsp` is `0x80000fbe20`, a
    valid user stack address.  The open-issues fork-child
    `user_rsp = 0` gap is NOT this stall.
  - Every live PCB's `next` and `prev` are 0.  No stale queue
    links; the queue-mutator race did NOT produce this.
  - The ready queue is correctly empty.  No self-link, no
    cycle.

**What is not yet named: why the process was resumed.**  It
reached `process_exit`, `.ring0_save` parked it with the hlt
frame, and something resumed it there.  The state in the pool
is RUNNING, not TERMINATED or ZOMBIE, so its state was changed
between the save and the resume.  Candidates: a self-switch
(`next == exiting`), a rescan re-adding it after the save, or
a slot reuse that did not overwrite the frame.  The pool walk
cannot distinguish them; the next read is a `RACE_PROBE` at
`process_exit` around the `context_switch` call, or a GDB
watchpoint on `pcb_pool[4].state` across the stall.

**Do not propose a fix until a framing survives a run.**  (a) and
(b) have not been tried.  (c) has, and failed.  The next step is
to name the resumer, not to pick a framing.

### The queue-mutator race -- OPEN

The `{head=NULL, tail=stale}` state that the 2026-10-13 queue fix
repairs is produced by some interleaving of the queue mutators that
was never identified.  The `YIELD-TO-IDLE` dump in this session's
post-fix capture is a live specimen:

    YIELD-TO-IDLE: queue empty, pool state:
    1  idle      RUNNING
    2  musl_sh   BLOCKED
    3  busybox   BLOCKED
    66 busybox   BLOCKED
    94 busybox   RUNNING
    96 busybox   ZOMBIE

The queue empty, `current` (pid 94) RUNNING, and the rescan recovered.
The mutators are `scheduler_ready_queue_add`, `_remove`, `_next`,
`_contains`, and `scheduler_reset`.  The interleaving that produces
the bad state is still not identified.

### Refuted -- do NOT re-walk

- **The pipe wake handshake** -- closed by `c3e5c97`.
- **The atomic refcount race** -- closed by `e1e8549`.
- **The stale-link queue membership** -- closed by `a0f9779`.
- **The timer's missing pool rescan** -- closed by `e3705d4`.
- **The wait4 lost wakeup** -- closed by `364be3b`.
- **The ready-queue self-link** -- closed by `cfff858`.
- **The `.ring0_save` RIP-only change** -- refuted, `#UD` at `0xBF`.
- **The `.ring0_save` RIP+RSP change** -- category error, not run.
- **The `.ring0_save` frame-build as Bug 1's cause** -- refuted by
  `d6ada2f`.  Bug 1 was the `user_rsp` writer, not `.ring0_save`.
- **Framing (c) -- skip the `.ring0_save` for a terminated `prev`**
  -- refuted by `pipe7e`, 2026-10-13.  The commit was reset, not
  reverted; this line is the record.  The change added, at the
  top of `.ring0_save`:

      mov rax, [r15 + 0x028]      ; prev->state
      cmp rax, 5                  ; PROC_STATE_TERMINATED
      je .skip_save

  The reasoning was internally consistent: a terminated process
  is never resumed, its save is meaningless, and the PCB carries
  the fact that matters.  **The run made the stall worse, not
  better.**  Pre-change, `pipe7e` reached `iter 1000` clean and
  was stopped manually.  Post-change, it reached `iter 2000`,
  then hard-stalled at `iter 3000`: `pipe7e` BLOCKED, its
  `PIPE7E_HELPER` child RUNNING and off-queue, CPU in idle,
  `TIMER-IDLE-STALL` repeating every 60000 ticks forever.  The
  skip is load-bearing beyond the frame build in a way the code
  reading did not see.  **Do not re-try (c) without new
  instrumentation.**
- **"Two RUNNING processes in one dump violates the one-current
  invariant."**  Wrong -- `timer_preempt_handler` excludes pid 1.
- **Also do not re-open:** item 7, item 7c, item 11, the session-48
  zone scan, the `fs_base` path, the `longjmp`/`setjmp` family.

---

## NEXT SESSION

**Step 0: run `pipe7e` on the fixed tree.**  This is the regression
check for `d6ada2f`.  One run is sufficient: the "two runs" in the
previous handoff was tied to the invariant-check discriminator
protocol, which the fix has made moot.  Run the second only if the
first is clean and you want belt-and-suspenders.

Capture aside before the run overwrites `capture.txt`:

    cp capture.txt capture-pre-pipe7e-$(date +%Y%m%d-%H%M).txt

If `pipe7e` faults at a user RIP with a kernel RSP: the `user_rsp`
writer is not the only cause, and the fault's CR2 and RSP are the
next read.  If it passes: `d6ada2f` is the regression-checked fix.

**Step 1: write the Bug 2 design writeup.**  Choose (a) or (b) from
the section above, with reasoning from the code.  Do not write a fix
until the framing is chosen.  The last-writer read is done; this is
the pair-definition step the previous handoff called for.

**Step 2: identify the queue-mutator interleaving.**  The
`YIELD-TO-IDLE` specimen is a starting point.  The five mutators and
their write orders are in `scheduler.c`.  The question is which
interleaving, under preemption, produces `{head=NULL, tail=stale}`.

### The candidates, in order

1. **`pipe7e` regression** for `d6ada2f`.
2. **Bug 2 -- the `.ring0_save` resume frame.**  Design question;
   the writer is named.
3. **The queue-mutator race.**  Specimen in hand; interleaving
   unidentified.
4. **Item 12 -- signal delivery.**  Largest; unblocks `kill`, ash job
   control, the Wayland `SIGBUS` path.  A workstream.
5. **`sys_gettimeofday` (96).**  Small, patterned, independent.
   Unlocks `ps -l`.  Note: 96, not 99.

### Do not

Do not apply the script or re-run the cherry-pick sequence.  The tree
is already patched.  Do not chase the pipe wake path, the atomic
refcount, the `fs_base` path, item 7, item 11, or the session-48 zone
scan.  One change at a time.  Do not edit `third_party/` -- but do
read it.

Do not run `git reset --hard` without first committing every
diagnostic you care about.

Do not arm a GDB watchpoint on an address without re-running
`nm` on the current build.

Do not propose a `.ring0_save` fix before (a)/(b) is chosen.

Do not re-paste a near-identical script; edit by hand.

---

## Open issues (top 6)

1. **`pipe7e` regression for `d6ada2f`.**  Pending.
2. **Bug 2 -- `.ring0_save` resume frame.**  Writer named; framing
   (a) or (b) to be chosen.
3. **The queue-mutator race.**  `{head=NULL, tail=stale}` interleaving
   unidentified.
4. **`open-issues.md` item 12: signal delivery is a stub.**
5. **`open-issues.md` item 10: `CR4.OSFXSR` without an XMM save.**
6. **`open-issues.md` item 13: `CONFIG_ASH_JOB_CONTROL` gap.**
   **`sys_gettimeofday` (96)** so `ps -l` works.  Small.

---

## State on disk

Kernel sources: `04_kernel_64bit/`.  `capture.txt` is gitignored; one
run per truncate.

The pre-patch backup `donix-before-apply-20261012` (or its tarball)
holds the un-patched donix at `5537465`.

The script `apply-fixes-to-donix.sh` lives at
`/home/noneya/code/apply-fixes-to-donix.sh` (or a copy in `docs/`).

Tags from this cycle, local only:
`20261013-session-start` (`04df9eb`),
`20261013-queue-and-rescan-fixes`,
`20261013-userrsp-writer-fix` (`d6ada2f`).

---

## One-line summary

**`d6ada2f` (tag `20261013-userrsp-writer-fix`) fixed the
`pcb->user_rsp` writer: `frame->rsp`, the CPU-pushed user RSP, not
`stack_pointer`, the kernel frame base.  Bug 1's known reproduction
(`test.sh` row 12, `uniq`) is cleared; `canary --full` 28/28 and
`test.sh` 57/57 all pass.  `pipe7e` regression is pending.  Bug 2's
writer is named -- `.ring0_save` writes `rsp` and not `rip`, and the
asymmetry is the bug -- and the next step is choosing framing (a) or
(b) before any fix.  The queue-mutator race is still open, with a
live `YIELD-TO-IDLE` specimen in hand.  The next session's first
action is the `pipe7e` regression run.**

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
"A field's comment is a claim about its use.  `pcb->user_rsp`'s
comment said 'the USER stack pointer' while the only writer put a
kernel address in it.  The comment was right; the writer was wrong."
"Two scripts that differ by one character pair will be pasted wrong.
If a script and its correction are near-identical, delete by hand
instead of re-pasting."
"A guard that aborts when its marker string is absent will silently
no-op on the present-and-correctly-formed case if the marker carries
the wrong delimiters.  Match the smallest substring that is actually
present, and re-verify after the write."
"A post-write verification string that does not quote the inserted
text verbatim is a false negative, not a failure.  Quote the check
string from the edit itself, not from a paraphrase of the edit's
intent."
"A count of writers derived from the files you have read is not a
count of writers.  Grep the whole tree before writing the number
into the handoff.  This session's Bug 2 enumeration said 'four `rip`
writers' until a grep over `user_syscall.c` found the fifth, in
`sys_execve`."
"A framing that looks safe by code reading can be refuted by a
run.  Framing (c) for Bug 2 was internally consistent, agreed with
the PCB state, and made the workload worse.  The refutation cost
one `pipe7e` run; the alternative was discovering it three sessions
later when the shape had changed."
"A post-write check string must be copied from the `new` literal
in the edit script, not retyped.  Three times this session a
retyped check differed from the actual inserted text by one
backtick or one parenthetical, fired a spurious `ERROR:`, and
made a correct edit look broken.  A false-negative check is
worse than no check: it costs a round trip and it teaches you
to distrust the verification step."

**This file's item text is the same kind of claim.**
