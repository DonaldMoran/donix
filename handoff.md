# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-20.  **The `C` is FIXED, the heap cycle hang
is CONTAINED, and busybox batch 1 + five applets are in.**  pipe7e
passed at 100 kHz (250000 iterations, zero events) and a >3.5-hour
100 Hz soak passed clean; the heap ran 10000 iterations clean with no
`HEAP:` lines.  `test.sh` is 57/57 and the busybox applets `bc`, `dc`,
`diff`, `du`, `awk` are enabled and verified by running them.  Read
"The concurrency contract" first, then "Where we are", then "Next".

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

**THE CONCURRENCY CONTRACT.**  `docs/contract.md` is the durable
statement; this file summarizes it.  A shared-state mutation and the
check that authorizes it are one critical section; a refused dispatch
restores `current_process`.  `state`, `block_kind`, and ready-queue
membership are changed only under `cli`.  Heap list mutation and
validation are one critical section; file-slot refcounts and the
`put_file_slot`/`sys_close` pipe-wake coupling are the VFS's version
of the same rule.  **Signals are the first ASYNCHRONOUS state writer**
-- the exact shape of the races just fixed -- and must be designed
against `docs/contract.md` before code lands.  Read that file before
touching signals or the VFS.

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

  Busybox (tags `20261019-busybox-batch1`,
  `20261019-busybox-applets`, `20261019-readme-current`):
  - Batch 1: fancy prompt (`\w` in `PS1`), `readlink` (+follow),
    `ls` filetypes/timestamps/sort/recursive, human-readable sizes,
    long options.  Verified 57/57.
  - Five applets enabled and verified by running them on the booted
    image: `bc`, `dc`, `diff`, `du`, `awk`.  `test.sh` 57/57.
  - `docs/contract.md` added (`963f4b8`); handoff points at it
    (`a93991c`).
  - Process lesson: enable busybox applets ONE AT A TIME from a clean
    baseline and rebuild; stacking several config changes in one build
    tree leaves stale `built-in.o` / `applet_tables.h` and produces a
    binary missing the new applets.

**THE VERIFICATION.**
  - `C`: pipe7e at `pit_init(100000)` -- **PIPE7E-ALL-PASS**,
    250000 iterations, zero `SWITCH-DEAD`, zero `C`.  At
    `pit_init(100)`: canary `--full` 28/28, `test.sh` 57/57, pipe7e
    clean past 77500.
  - Heap: pipe7e 10000 iterations clean (no `HEAP:` lines, no hang,
    no `C`), canary `--full` 28/28, `test.sh` 57/57.  The run is
    faster than the corrupt runs -- the allocator was spending time
    on the broken list.
  - Soak: pipe7e `pit_init(100)`, 250000 iterations, >3.5 hours,
    **PIPE7E-ALL-PASS**, zero `C`/`SWITCH-DEAD`/`YIELD-REFUSE`/`HEAP:`.
    Recorded in `docs/open-issues.md` 7b.
  - Selftest: 18/18, `[exception_de]`/`[exception_pf]`/
    `[exception_gp]` PASS, the kernel shell returns; the
    `TIMER-REFUSE ... fail_slot` line is gone; the `create_fail`
    rows still report no leak.
  - The two rates cover the two classes: 100 kHz stresses
    interleavings; 100 Hz covers the user-mode-preemption paths.

**NOT PROVEN.**  "Clean at both rates" is strong evidence, not proof.
The runs are not proof a 1-in-N race cannot fire; the soak is strong
evidence, not closure.  Do not call a race closed on one clean run.

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
whole-line match.  `str.count` is the only honest multi-line count.

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
are the source for the version summary.  **Write the summary from
them BEFORE dropping them.**  The release tag and the merge commit
are local objects; once they exist, the scratch tags may be dropped.

**The release procedure** (on `dev`, pre-release):

1. **Patch the docs first.**  Update this file's release paragraph and
   (optionally) `docs/version-history.md`.  Commit.
2. **Bump the version string in `kmain.c` manually** (the banner).
   Commit.
3. **Tag** `vX.Y.Z` annotated, on `dev`, summary from the scratch
   tags' annotations and `vPREV..HEAD`:

       git tag -a vX.Y.Z -m "<version summary>"

4. **Drop the scratch tags locally** (the tag and summary now exist):

       for t in $(git tag --list '2026*'); do git tag -d "$t"; done

5. **Push `dev`, then the tag:**

       git push origin dev
       git push origin vX.Y.Z

6. **Merge to `main` with `--no-ff`** (keeps the merge point in
   `main`'s history), push, return to `dev`:

       git checkout main
       git merge --no-ff dev -m "<version summary>"
       git push origin main
       git checkout dev

Do not use `git push --tags` -- it would push the scratch tags.  Push
`vX.Y.Z` by name.

**Current release state.**  **`v0.6.14` is released.**  Work is now
pre-**`v0.6.15`**.  The scratch tags `20261019-*` (7b investigation,
busybox batch 1, the five applets, README) are the source for the
`v0.6.15` summary; the bump is the maintainer's decision.  Do not
delete them until the `v0.6.15` tag and merge summary exist.

---

## What donix is

A fork of dons-os.  Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 ABI, natively.  Newlib is gone.
Userland is a tracked source tree at `userland/musl/`.  Phase B
(busybox) is well underway: the shell is interactive, pipelines and
redirection work, and the applet set now includes `bc`, `dc`, `diff`,
`du`, `awk` in addition to the file/text applets.

Full strategy: `docs/strategy.md`.  Direction: `ROADMAP.md`.
Concurrency rules: `docs/contract.md`.

---

## Where we are

### CLOSED -- the `C` (stale-ZOMBIE dispatch) and the heap cycle hang

Both fixed.  See "What was fixed" and "THE VERIFICATION" at the top.
Do NOT re-walk the refuted paths below.

### Current baseline

- `test.sh` 57/57; canary `--full` 28/28; pipe7e clean at 100 kHz and
  on a >3.5-hour 100 Hz soak.
- Busybox batch 1 (prompt, readlink, `ls` features, human-readable,
  long options) and five applets (`bc`, `dc`, `diff`, `du`, `awk`)
  enabled and verified by running them.
- `docs/contract.md` is the durable concurrency rule; read it before
  any shared-state change.

### Next -- the highest-value work

1. **The VFS.**  The pathname-dispatch seam exists (`/proc` is a real
   directory, `/dev` is not); the VFS makes the seam a real layer:
   a mount table, inode operations, FatFs as one driver, and one
   `vfs_resolve(path, follow_final)` that every path-taking syscall
   calls.  It unblocks `/dev` as a directory, symlinks (item 8),
   file-backed `mmap` (a Wayland prerequisite), and the display
   stack.  Read `docs/contract.md`'s VFS section first.  Scope it as
   small, verified commits: mount table, FatFs driver, one syscall
   through `vfs_resolve`, then `/dev`, then `/proc`, then symlinks.
2. **Signals (open-issues item 12).**  The first asynchronous state
   writer; design against `docs/contract.md` before code.
3. **Shebang + `/bin/sh`.**  `sys_execve` `#!` parsing with a
   recursion guard, and an interpreter on the image.
4. **`sys_gettimeofday` (99)** so `ps -l` works.
5. The open-issues list (top 6, below).

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

Plus: **the VFS** (see Next), **shebang + `/bin/sh`**, and the
open-issues list.

---

## State on disk

Kernel sources: `04_kernel_64bit/`.  `capture.txt` is gitignored.
Scratch tags for the `v0.6.15` summary (drop only after the tag and
merge summary exist): `20261019-7b-investigated`,
`20261019-busybox-batch1`, `20261019-busybox-applets`,
`20261019-readme-current`.

---

## One-line summary

**The `C` is FIXED, the heap cycle hang is CONTAINED, and busybox
batch 1 + five applets are in (57/57; a >3.5-hour 100 Hz soak is
clean).  `docs/contract.md` is the durable concurrency rule.  Next:
the VFS -- read the contract's VFS section, then scope it as small,
verified commits.**
