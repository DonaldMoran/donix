# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-16.  The `C` is **CONTAINED, not closed**.
The mechanism is named to the last step (a ZOMBIE reaches dispatch),
a guard refuses it, and the machine now runs past the point every
prior build died.  The **cause** -- how a zombie reaches
`scheduler_switch_to` as `next` -- is OPEN and is the task.  Read
"NEXT SESSION" first.

**THE `C`, in one paragraph.**  `context_switch_resume` loads
`next->rsp` and reads 20 slots as a resume frame.  The 2026-10-16
SW2-trace run named the last step: `scheduler_switch_to` is entered
with `next->state == PROC_STATE_ZOMBIE` (st=4), `next->rsp` points at
a **zeroed** region (no resume frame), the function sets the state
RUNNING and calls `context_switch`, and the resume reads CS = 0 and
halts.  A zombie has called `process_exit`; it is not runnable and has
no frame.  This is **containment**: `scheduler_switch_to` now refuses
`next->state == ZOMBIE || TERMINATED` before the `current_process`
assignment (print tag `SWITCH-DEAD`).  The zombie is left for its
parent to reap and the dispatch does not happen.

**The churn -- the slot is CONSTANT, the owner changes.**  A 100 kHz
pipe7e run produced five `SWITCH-DEAD` events in 24500 iterations,
all on the SAME pool slot:

    iter  9500  SWITCH-DEAD  next=0xFFFFFFFF8027D530  pid=9811   st=4  PIPE7E_HELPER
    iter 11000  SWITCH-DEAD  next=0xFFFFFFFF8027D530  pid=11348  st=4  PIPE7E_HELPER
    iter 18000  SWITCH-DEAD  next=0xFFFFFFFF8027D530  pid=18442  st=4  PIPE7E_HELPER
    iter 20000  SWITCH-DEAD  next=0xFFFFFFFF8027D530  pid=20378  st=4  PIPE7E_HELPER
    iter 24500  SWITCH-DEAD  next=0xFFFFFFFF8027D530  pid=24559  st=4  PIPE7E_HELPER
    iter 28500  YIELD-REFUSE  next pid=1 name=idle state=RUNNING

The pid changes and increases; the slot does not.  So the slot is
reclaimed and reused, and a reference to it survives the reclaim and
reaches dispatch.  **The reference is SLOT-SPECIFIC, not "last
reclaimed"** -- if it were the last reclaimed, the slot would vary.
Whatever holds it names THIS slot (`0xFFFFFFFF8027D530`) and is not
cleared when the slot is reclaimed.  That is the narrowing clue.

`YIELD-REFUSE` on idle also fires (same class: a RUNNING process
selected), so both containment paths are active.

**What landed (commit `6f7344b`).**
  - `scheduler.c`: `scheduler_switch_to` refuses ZOMBIE/TERMINATED
    dispatch before the assignment.  Print tag `SWITCH-DEAD`.  **Keep.**
  - `scheduler.c`: the `SW2-*` dispatch trace (entry, after the
    assignment, before the call) is gated behind `SW2_TRACE`,
    default 0.  Flip to 1 for a diagnosis build.  **Keep.**
  - Plus the session's earlier dispatch work, in the same commit:
    `process_exit`'s zombie `no_save`; the three rescans that no
    longer re-add a RUNNING process; the `noop_switch` guard; the
    extended `C` report; the pipe block paths via `process_yield`;
    the `isr.asm`/`tss.c` fault-stack fallback; the three range-check
    detectors (detectors, NOT fixes).

**The `process_destroy` fix (commit `c2a3f89`, tag
`20261016-destroy-both-current`).**  `process_destroy` wrote
process.c's file-local `current_process` directly, leaving
scheduler.c's copy pointing at the destroyed PCB.  Route the clear
through `scheduler_set_current`.  Independently correct.  **Not** the
`C`'s holder -- at the last `C` both copies agreed.

**THE RATE.**  100 kHz (`pit_init(100000)`) is the **interleaving
lab**: it stresses timing windows.  100 Hz (shipping) is **path
coverage**: user-mode preemption and run-to-completion.  Neither
subsumes the other; validate a fix under both.  ~1 event per 4500
iterations at 100 kHz is ~1 per ~4.5M at 100 Hz.

**Repo state -- run these; do not write it here.**

    cd /home/noneya/code/donix || exit 1
    git log --oneline -8
    git status -sb
    git tag --list '2026101*'

**Working style -- how this project gets changed**

Read this before proposing any command block.

**One change at a time.** A "change" is a commit, verified before the
next one starts.  **Verify by RUNNING, not by committing** -- build,
run, see the behavior, THEN commit.

**The state check comes before the command block.** Every command
block starts with `git status -sb` (or `git log`).

**A command block is one paste -- write it to a file first.**
`git add`, `git status`, `git commit`, and `git tag` belong in **one**
block, with the message inline via heredoc.  **Do not paste a block
with `cd ... || exit 1` into an interactive shell** -- the `exit`
closes the terminal.  Write to `/tmp/<name>.sh` with
`cat > ... <<'SCRIPT_EOF'`, then `bash /tmp/<name>.sh`.

**Python edits with `count != 1` abort checks are the preferred edit
style.**  **`set -e` at the top.**  **Idempotent edit scripts.**
**Do the count check IN PYTHON (`str.count`), not with `grep`.**
`grep -Fc` does not do multi-line matches; `grep -Fxc` requires a
whole-line match.  Both produced false abort counts this session.
`str.count` is the only honest multi-line count.

**BEFORE ASSERTING A COUNT, count the occurrences in the file and
write the expected number from that count, not from memory.**

**ANCHOR ON STRUCTURE UNIQUE TO THE TARGET FUNCTION.**  Grep the
file, confirm the count is 1 **and** the match is in the intended
function.

**A post-write check string must be copied from the `new` literal,
not retyped.**

**Ask for source you do not have.**  Before patching a file it has
not seen **in this session**, ask for it.  **Before writing an anchor,
read the file with `cat -A` and quote the bytes.**

**The build is TWO steps.**  `make -C 04_kernel_64bit
FAT_CONFIG=single` builds the kernel; `make -C 05_boot_kernel64
hdd-single.img` builds the image.  Running only the second copies a
STALE `kernel.bin`.  Always run both, and verify `kernel.elf` is newer
than the source you edited.

**`nm` output moves between builds.**  Re-run `nm` before using any
address.

**A diagnostic on the SUCCESS path is noise.  A diagnostic on the
FAILURE path is signal.**  `SW2-*` is success-path (gated).  The
refuse lines (`SWITCH-DEAD`, `YIELD-REFUSE`, `SWITCH-REFUSE`,
`EXIT-REFUSE`) and the `C` report are failure-path (kept).

**`SWITCH-DEAD` print decision.**  Keep it **unconditional** while
chasing the cause -- it is the only visible evidence the underlying
selection bug is active.  Gate it (or remove it) once the selection is
fixed.

**GDB over `-s` has no types** (`kernel.elf` is built WITHOUT `-g`).
Use raw offsets: `pid` 0x000, `name` 0x008, `state` 0x028, `cr3`
0x030, `entry_point` 0x038, `kernel_stack_virt` 0x048,
`kernel_stack_top` 0x050, `rsp` 0x110, `rip` 0x118, `next` 0x120,
`prev` 0x128, `block_kind` 0x158.  `sizeof(pcb_t)` = 0x618.  Read the
PCB dump and identify fields by value.  **CONSIDER BUILDING WITH
`-g`.**

**Do not run `git reset --hard` without stashing first.  Do not edit
`third_party/`.  Do not commit `kmain.c`'s `pit_init(100000)` -- the
committed value is 100.  Do not call the `C` closed on one clean run.**

---

## What donix is

A fork of dons-os.  Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 ABI, natively.  Newlib is gone.
Userland is a tracked source tree at `userland/musl/`.  Phase B
(busybox) is well underway.

Full strategy: `docs/strategy.md`.  Direction: `ROADMAP.md`.

---

## Where we are

### OPEN -- how a ZOMBIE reaches `scheduler_switch_to` as `next`

**This is the task.  It is the only thing on this list.**

The `C` is contained (`SWITCH-DEAD`).  The **cause** is that a zombie
is being *selected* for dispatch.  A zombie was removed from the ready
queue by `process_exit` (`scheduler_ready_queue_remove(exiting)`), and
the three rescans skip non-READY (`if (_p->state !=
PROC_STATE_READY) continue;`), so no rescan re-adds it.  Yet it
reaches dispatch, on ONE slot, with the pid changing -- so the slot is
reclaimed and reused, and a SLOT-SPECIFIC reference survives.

**Where a stale `pcb_t*` can live.**  Search these, in this order:
  1. `ready_queue_head` / `ready_queue_tail` -- the queue may hold a
     pointer to a reclaimed PCB.
  2. `pcb->next` / `pcb->prev` (offsets 0x120 / 0x128) of *other*
     PCBs -- a link to a reclaimed PCB left in a live neighbor.
  3. `current_process` (both copies -- they agreed at the last `C`).
  4. `pipe->reader_waiting` / `pipe->writer_waiting` -- RULED OUT for
     pipe7e: the helper writes 2 bytes into a 4096-byte pipe and never
     blocks, so the waiter fields are never set on this path.
  5. `parent->wait_pid` -- a pid, not a pointer; resolved via
     `process_find_by_pid`, which skips UNUSED.  Low risk.

**The instrument to run first.**  At `SWITCH-DEAD`, walk the ready
queue and every live PCB's `next`/`prev` and print any that equal the
zombie's `next` pointer.  Gate it behind `SW2_TRACE` so it only fires
when the guard fires.  The zombie slot was `0xFFFFFFFF8027D530` in the
last build; re-run `nm` for the current value.

**The narrowing clue:** the slot is CONSTANT across events.  A
reference that is "the last reclaimed slot" would vary; this one does
not.  So the holder names this slot specifically and is not cleared
on reclaim.  Candidates that fit "names one slot persistently":
`ready_queue_tail`, a specific `pcb->next`/`prev`, or a static local.

### LANDED -- containment (`6f7344b`)

See "What landed" at the top.

### CLOSED

- **The `process_destroy` single-copy write** (`c2a3f89`, tag
  `20261016-destroy-both-current`).
- **The queue-mutator race** (`c85d074`).
- **The pipe reader's lost wakeup** (`7aaaafd`).
- **Bug 1, the user-RSP writer** (`d6ada2f`).
- **Bug 2, the `process_exit` hlt-guard resume** (`6bfa008`).

### Refuted -- do NOT re-walk

- **The reserve / frame placement.**  `67701df` + `5587d70`.
- **The `next->rsp = 0` clear.**  Broke blocking; reverted.
- **The pipe block path as the writer.**  Converted to
  `process_yield`; the `C` remained.
- **`sys_execve`, `sys_wait4`, `sys_brk` as the site.**  Checked.
- **The pipe-waiter fields as the stale holder** -- ruled out: the
  helper never blocks, so they are never set.
- **The `current_process` dual-copy divergence at the `C`** -- both
  copies agreed.  (The `process_destroy` fix is still correct.)
- **A switch-to-self guard in `scheduler_switch_to`'s TAIL** --
  attempted; it fires after `current_process = next` and refuses every
  dispatch, hanging at iter 0.  Reverted.  Any such check must be
  BEFORE the assignment.
- Do not re-open: item 7, item 7c, item 11, the session-48 zone scan,
  the `fs_base` path, the `longjmp`/`setjmp` family.

---

## NEXT SESSION

**The task: find the SLOT-SPECIFIC stale reference that carries the
zombie's slot into `scheduler_switch_to`.**

**The plan, in order:**

1. **Reproduce at 100 kHz.**  Build both steps, run pipe7e at
   `pit_init(100000)` (the current `kmain.c`; do NOT commit it).  Wait
   for a `SWITCH-DEAD`.

2. **Find the holder.**  Add the queue-walk instrument gated behind
   `SW2_TRACE` (default 1 for this run): at `SWITCH-DEAD`, walk
   `ready_queue_head`/`tail` (`nm`) and every live PCB's `next`/`prev`
   and print any that equal the zombie's `next`.  If one does, that is
   the stale reference.

3. **Fix the holder.**  Clear the stale reference in `process_reclaim`
   (or `process_destroy`) -- the function that makes the slot reusable.

**Do not** re-apply a switch-to-self guard in the tail of
`scheduler_switch_to`.  **Do not** call the range checks the fix.

**Before writing any edit, count the anchor IN PYTHON and confirm it
is 1 and in the intended function.**

### Do not

Do not commit `kmain.c`.  Do not call the `C` closed on one clean
run.  Do not run `git reset --hard` without stashing first.  Do not
edit `third_party/`.

---

## Open issues (top 6)

1. **`open-issues.md` item 12: signal delivery is a stub.**
2. **`open-issues.md` item 10: `CR4.OSFXSR` without an XMM save.**
3. **`open-issues.md` item 13: `CONFIG_ASH_JOB_CONTROL` gap.**
4. **`sys_gettimeofday` (99)** so `ps -l` works.
5. **`process_dump_all`'s `DETACHED` state** -- may mask a stranded
   process bug.
6. **Item 7a/7b** -- the boot-time `#PF` and the busybox `#GP`.

---

## State on disk

Kernel sources: `04_kernel_64bit/`.  `capture.txt` is gitignored.
Tags from earlier cycles:
`20261013-session-start` (`04df9eb`),
`20261014-pipe7e-100k-clean` (`7aaaafd`),
`20261014-test57-canary28-clean` (`d3c7f20`),
`20261014-queue-race-closed` (`3eb1d61`),
`20261014-selfswitch-two-sites`,
`20261016-destroy-both-current` (`c2a3f89`).

---

## One-line summary

**The `C` is CONTAINED (`SWITCH-DEAD` refuses a ZOMBIE dispatch), and
the cause is OPEN: a SLOT-SPECIFIC stale `pcb_t*` reaches
`scheduler_switch_to`.  Five events in 24500 iterations at 100 kHz all
named the SAME slot `0xFFFFFFFF8027D530` (this build) with a CHANGING
pid -- so the holder names this slot and is not cleared on reclaim.
Next: at `SWITCH-DEAD`, walk the ready queue and every live PCB's
`next`/`prev` for that slot, and clear it in `process_reclaim`.**
