# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-18.  **The `C` is FIXED and the heap cycle
hang is CONTAINED.**  pipe7e passes at 100 kHz (250000 iterations,
zero events) and at 100 Hz (applet suite green, pipe7e clean); the
heap ran 10000 iterations clean with no `HEAP:` lines.  Read "What was
fixed" first, then "Releases" for the version-bump procedure, then
"Next" for what remains.

**THE `C`, in one paragraph.**  `context_switch_resume` loads
`next->rsp` and reads 20 slots as a resume frame.  The `C` fired when
`next` was a ZOMBIE (a process that had called `process_exit` and was
waiting to be reaped) whose slot had been reclaimed and reused, so
`next->rsp` named a zeroed region, not a resume frame; the resume read
CS = 0 and halted.  The selection came from a check-then-act race:
`process_yield` popped `next` from the ready queue (READY), then did
its checks with interrupts ON, then dispatched.  A tick between the
pop and the dispatch could turn `next`'s slot READY->ZOMBIE, and the
dispatch used the stale value.  Fixed by holding `cli` from before the
pop through the dispatch, with `sti` on every return; the tail check
in `scheduler_switch_to` (state != READY -> refuse, restore
`current_process = prev`) is the atomic backstop.

**THE HEAP, in one paragraph.**  A corrupt free-list `next` that
pointed at itself made `find_free_block`'s unbounded walk spin forever
with interrupts off, hanging the kernel.  The cycle came from
`coalesce_blocks` and `heap_validate` running on a list that an
interleaved `kfree` (from a tick's scheduler) was mid-way through
mutating.  The 2026-10-18 enriched trace proved the merge arithmetic
is correct at every step; the "corruption" was the validator reading
a transient.  Fixed by bounding the walk (a cycle returns NULL, not a
hang), guarding `split_block` against a self-link, and holding `cli`
across `coalesce_blocks()` + `heap_validate()` in `kfree`.  **There is
no evidence of a caller overflow** -- the heap had a race, not an
overflow.  The bounded walk and split guard remain as real protection
against a future corruption.

**What was fixed.**

  `C` (tags `20261016-c-contained`, `20261016-destroy-both-current`,
  `20261016-tail-state-check`, `20261017-pipe7e-all-pass`):
  - `process_yield` holds `cli` from before `scheduler_ready_queue_next`
    through `scheduler_switch_to` (`ef85962`).
  - `scheduler_switch_to`'s tail: `state != PROC_STATE_READY` ->
    refuse, restore `current_process = prev`, `sti`, return
    (`e05d1fa`).
  - `scheduler_switch_to`'s top: `state == ZOMBIE || TERMINATED` ->
    refuse (tag `SWITCH-DEAD`).  `6f7344b`.  Fast path; the tail
    check is the atomic one.
  - `process_destroy` clears both `current_process` copies via
    `scheduler_set_current` (`c2a3f89`).

  Selftest fixes (tags `20261018-shell-fallback`,
  `20261018-create-fail-queue-remove`):
  - `process_exit`'s kernel-shell fallback wakes the shell as
    `READY` (not `RUNNING`), lets `scheduler_switch_to` do the
    transition, and falls through to idle on refusal instead of
    halting (`e9d6b7a`).  The old code pre-set `RUNNING`, which the
    tail check refused, and `[exception_de]` halted the selftest.
  - `process_create`'s four failure exits remove the PCB from the
    ready queue before marking it `UNUSED` (`19719b7`).  The timer
    found the failed `fail_slot` PCB on the queue and printed
    `TIMER-REFUSE: next pid=0 name=fail_slot rsp=0`.
    `process_reclaim` and `process_destroy` already did this; the
    failure exits were the one site left out.

  Heap (tag `20261018-heap-cycle-contained`, `1c7fe91`):
  - `find_free_block`: bounded walk; a cycle prints and returns NULL.
  - `split_block`: refuses a self-link and a `new_block <= block`.
  - `kfree`: `coalesce_blocks()` + `heap_validate()` under one `cli`,
    caller flag saved/restored.
  - Instrumentation: `heap_validate` prints `check=/exp=/got=`;
    the corrupt print names the block's fields; `HEAP_TRACE_MERGE`
    (default 0) traces the coalesce arithmetic.

**THE VERIFICATION.**
  - `C`: pipe7e at `pit_init(100000)` -- **PIPE7E-ALL-PASS**,
    250000 iterations, zero `SWITCH-DEAD`, zero `C`.  At
    `pit_init(100)`: canary `--full` 28/28, `test.sh` 57/57, pipe7e
    clean past 77500.
  - Heap: pipe7e 10000 iterations clean (no `HEAP:` lines, no hang,
    no `C`), canary `--full` 28/28, `test.sh` 57/57.  The run is
    faster than the corrupt runs -- the allocator was spending time
    on the broken list.
  - Selftest: 18/18, `[exception_de]`/`[exception_pf]`/
    `[exception_gp]` PASS, the kernel shell returns; the
    `TIMER-REFUSE ... fail_slot` line is gone; the `create_fail`
    rows still report no leak.
  - The two rates cover the two classes: 100 kHz stresses
    interleavings; 100 Hz covers the user-mode-preemption paths.

**NOT PROVEN.**  "Clean at both rates" is strong evidence, not proof.
The runs are not a soak; a race that fires 1-in-500000 did not fire in
these runs, which does not mean it cannot.  A multi-hour soak at
100 Hz is the validation the fast run cannot give.

**THE INVARIANT -- signals must honor it.**  `state`, `block_kind`,
and ready-queue membership are changed only under `cli`; a dispatch's
check and act are one critical section; a refused dispatch restores
`current_process`.  Heap list mutation and validation are also one
critical section.  Signals (open-issues item 12) are the first feature
that adds an ASYNCHRONOUS state writer -- exactly the shape of the
races just fixed.  Any signal delivery that changes a process's state
from outside its own execution must follow this discipline.

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
FAILURE path is signal.**  `SW2-*`, `HEAP_TRACE_MERGE`, the
`HEAP: corrupt` prints, and `heap_validate`'s check prints are
success-or-transient-path; they are gated.  The refuse lines
(`SWITCH-DEAD`, `YIELD-REFUSE`, `SWITCH-REFUSE`, `EXIT-REFUSE`) and
the `C` report are failure-path (kept).

**GDB over `-s` has no types** (`kernel.elf` is built WITHOUT `-g`).
Use raw offsets: `pid` 0x000, `name` 0x008, `state` 0x028, `cr3`
0x030, `entry_point` 0x038, `kernel_stack_virt` 0x048,
`kernel_stack_top` 0x050, `rsp` 0x110, `rip` 0x118, `next` 0x120,
`prev` 0x128, `block_kind` 0x158.  `sizeof(pcb_t)` = 0x618.
**CONSIDER BUILDING WITH `-g`.**

**Do not run `git reset --hard` without stashing first.  Do not edit
`third_party/`.  Do not commit `kmain.c`'s `pit_init` knob -- the
committed value is 100.  Do not call a race closed on one clean run.**

---

## Releases

**Before a release:** the scratch `2026MMDD-*` tags are annotated and
are the source for the version summary.  Do not delete them until the
`vX.Y.Z` tag is made and the summary is in the merge commit.

**The release procedure** (from `vX.Y.Z` on `dev`):

    git tag vX.Y.Z
    git push origin dev
    git push origin vX.Y.Z
    git checkout main
    git merge --no-ff dev -m "<version summary>"
    git push origin main
    git checkout dev

`--no-ff` keeps the merge point in `main`'s history.  The summary is
written from the scratch tags' annotations and the
`vPREV..HEAD` commit log, in the project's voice.

**Then drop the scratch tags:**

    git tag --list '2026*' > /tmp/scratch-tags.txt
    for t in $(git tag --list '2026*'); do git tag -d "$t"; done

(Local tags only; if any were pushed, `git push origin :refs/tags/<t>`
for each.)

**v0.6.13 is closed.**  The banner is bumped (`f1b4a21`,
tag `20261018-v0614-bump`) and this is the first change under
**v0.6.14**.  The release tag will be `v0.6.14`, covering
sessions 60-68: the migration, the pipe/scheduler lost-wakeup and
queue-mutator family, the `C`, the heap cycle, the selftest fixes,
and the `run` rewrite.  The long-form narrative is the `v0.6.13`
entry in `docs/version-history.md` (commit `033bd53`).

---

## What donix is

A fork of dons-os.  Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 ABI, natively.  Newlib is gone.
Userland is a tracked source tree at `userland/musl/`.  Phase B
(busybox) is well underway.

Full strategy: `docs/strategy.md`.  Direction: `ROADMAP.md`.

---

## Where we are

### CLOSED -- the `C` (stale-ZOMBIE dispatch) and the heap cycle hang

Both fixed.  See "What was fixed" and "THE VERIFICATION" at the top.
Do NOT re-walk the refuted paths below.

### Next -- the highest-value work

1. **A 100 Hz soak.**  The runs above are not a soak.  Let pipe7e or
   the applet suite run for hours at `pit_init(100)` and confirm no
   `C`, no `SWITCH-DEAD`, no `YIELD-REFUSE`, no `HEAP:`.  This is the
   validation the fast run cannot give.
2. **Signals (open-issues item 12).**  The first asynchronous state
   writer.  Honor the invariant above.
3. **Shebang + `/bin/sh`.**  `./script.sh` works via ash's `ENOEXEC`
   fallback, but `/bin/sh` does not exist and `sys_execve` has no
   `#!` handling.  Two parts: put `/bin/sh` on the image (copy of
   busybox or a kernel alias -- a decision), and add `#!` parsing to
   `sys_execve` with a recursion guard.  Not a bug -- a feature gap.
4. The open-issues list (top 6, below).

### Refuted -- do NOT re-walk

- **The reserve / frame placement.**  `67701df` + `5587d70`.
- **The `next->rsp = 0` clear.**  Broke blocking; reverted.
- **The pipe-waiter fields as the `C`'s stale holder.**  The pipe7e
  helper writes 2 bytes and never blocks, so they are never set.
- **The `current_process` dual-copy divergence at the `C`.**  Both
  copies agreed.  (The `process_destroy` fix is still correct.)
- **A switch-to-self guard in `scheduler_switch_to`'s TAIL.**  It
  fires after `current_process = next` and refuses every dispatch.
  The tail check is `state != READY`, not `next == current_process`.
- **A caller heap overflow.**  The enriched trace + the merge trace
  proved the merge arithmetic correct; the "corruption" was an
  interleaved `kfree`.  The heap had a race, not an overflow.
- Do not re-open: item 11, the session-48 zone scan,
  the `fs_base` path, the `longjmp`/`setjmp` family.

---

## Open issues (top 6)

1. **`open-issues.md` item 12: signal delivery is a stub.**
2. **`open-issues.md` item 10: `CR4.OSFXSR` without an XMM save.**
3. **`open-issues.md` item 13: `CONFIG_ASH_JOB_CONTROL` gap.**
4. **`sys_gettimeofday` (99)** so `ps -l` works.
5. **`process_dump_all`'s `DETACHED` state** -- may mask a stranded
   process bug.
6. **Item 7a/7b** -- the boot-time `#PF` and the busybox `#GP`.

Plus: **shebang + `/bin/sh`** (see Next), and **a 100 Hz soak**.

---

## State on disk

Kernel sources: `04_kernel_64bit/`.  `capture.txt` is gitignored.
Scratch tags from the `C`/heap work (drop before release):
`20261016-c-contained`, `20261016-destroy-both-current`,
`20261016-tail-state-check`, `20261017-pipe7e-all-pass`,
`20261018-heap-cycle-contained`.
Earlier: `20261014-queue-race-closed`, `20261014-pipe7e-100k-clean`,
`20261013-session-start`.

---

## One-line summary

**The `C` is FIXED (pop-to-dispatch `cli`, atomic tail check) and the
heap cycle hang is CONTAINED (bounded walk, split guard,
`cli`-across-coalesce-and-validate).  pipe7e passes at 100 kHz
(250000, zero events) and at 100 Hz; the heap ran 10000 iterations
clean.  Next: a 100 Hz soak, then signals -- honoring the invariant
that state changes happen under `cli`.**
