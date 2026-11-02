# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-11-02 (evening). **`v0.6.18` is released.**
Work is pre-`v0.6.19`. `origin/dev` = `799e98f` = `v0.6.18`; run
`git rev-list --count origin/dev..dev` for the real ahead-count
on `dev`. Working tree clean, build green (`MAKE_EXIT=0`).

**WHERE WE ARE.** `v0.6.15` (busybox batch 1), `v0.6.16` (the VFS),
`v0.6.17` (FAT `st_ino`, `tar`, `chmod`/`chown`), and `v0.6.18`
(signals slices 1-5) are released. Work is now pre-`v0.6.19`, on
`dev`, tree clean. Three fault classes were live at the start of
this session; two are now closed, one is closed pending a probe.
See "NEXT" and "Where we are".

**NEXT.** (1) **The canary `.text` `#PF` probe (Session 2)** -- the
release gate for `v0.6.19`. Add the VA-gated probe described in
"The canary `.text` `#PF`" below, run `canary --full` with `^Z`/`fg`
cycles on **3+ fresh boots**, copy `capture.txt` aside each run,
revert the probe. If silent across all boots, the canary `.text`
`#PF` is **closed with evidence** and `v0.6.19` ships. If it fires,
the fault is independent of the IF fix and stays open -- no release.
(2) **The TCG scheduler fault** (`rsp=0`, bad cr3, `SWITCH-REFUSE`
after dispatch) -- its own session, distinct from everything else.
(3) `open-issues.md` is three sessions out of date (last touched
at `701dcc1`); patch it after the probe settles the canary `.text`
`#PF` so the entry list is accurate. Do not start the TCG fault or
the probe in the same session as this handoff rewrite.

**THE CANARY `.text` `#PF` -- CLOSED PENDING PROBE.** Before the
2026-11-02 evening IF fix (`162a1d3`), an unlucky `canary --full`
run faulted at a moving `.text` slot: `RIP=CR2=0x450630`, `pt=80`,
`error 0x14` (present, user, fetch), `pte=0`, canary's own cr3
(pre-fix `7cb26fb`, one `^Z`/`fg` cycle reliably faulted at
`0x413966`/`pt=19`). After `162a1d3`, `canary --full` passed
**28/28 twice** on 2026-11-02 evening -- once with six `^Z`/`fg`
cycles, once standalone -- and `pipe7e` reached **27500 iterations
clean**. **This is evidence the IF fix also closed the canary
fault, but the canary fault was intermittent before the fix, and
"did not reproduce" is not "closed."** The VA-gated probe below
is the closing test. Do NOT call it closed on one clean run; the
handoff's own rule applies.

**THE PIPE7E `#GP` (`error 0x4640`) -- CLOSED 2026-11-02 evening.**
`7cb26fb` introduced two defects in `process_exit`'s `cli` region:
a duplicated `console_fg_pgrp_reclaim_if` call in `scheduler.c`
(removed in `0196d66`) and five `console_*_get`/`_set` accessors in
`user_syscall.c` ending in bare `sti` while called from a `cli`
region (converted to `pushfq/popfq` in `162a1d3`). Symptom: `#GP
error 0x4640` at `RIP=0xFFFFFFFF80123D5B`, which disassembles to the
`iretq` in `context_switch_resume.pop_frame`; the error code decodes
to a GDT segment-selector error, index `0x8C8` -- neither `0x18`
(kernel CS) nor `0x33` (user CS). The `context_switch_resume` CS
check at `[rsp+0x80]` had passed (saw `0x18`), so the resume frame
changed between that check and the `iretq`. **This is a DIFFERENT
fault class from the canary `.text` `#PF` -- do not conflate them.**
Mechanism note: the "interrupt landed in the CS-check..iretq
window" explanation is inferred, not directly observed; a gdb write
watchpoint on the resume frame was not run. If a future regression
reproduces the `0x4640`-at-`iretq` signature, that watchpoint is
the next tool.

**THE TCG SCHEDULER FAULT (`./run tcg`) -- OPEN, SEPARATE.** 2026-11-02:
a `sh` child resumed with `rsp=0` and a bad cr3, faulted at
`RIP=0x451CC3` on `CR2=0xFFFFFFFFFFFFFFF8` (`error 0x6`, write,
user, not-present), with a recursive PML4 walk not present
(`pml4=511 pdpt=511 pd=511 pt=511`, `pdpte=0`). The scheduler's
`SWITCH-REFUSE: next pid=46 name=sh rsp outside stack -> idle`
fired *after* the bad dispatch -- the frame was already broken
when dispatched. `sha256sum ABC` failed (`got: []`) in the same
run, likely the same process dying. **Not fixed; needs its own
session** (move the `SWITCH-REFUSE` guard before dispatch; find who
zeroes `rsp`). This is a context-switch / cr3 bug under TCG,
**distinct** from both the canary `.text` PTE zeroing (KVM-timing,
`error 0x14`, `pte=0`, 4 KB PT) and the pipe7e `#GP` (`error
0x4640`, `iretq`, bad segment selector).

**THE RELEASE TAGS.** `v0.6.15` through `v0.6.18` are the released
release record; their annotations carry the per-release summary.
**The scratch `2026MMDD-*` tags are PRESERVED until the user
decides to push to GitHub.** They are the source material for the
release summary: read each tag's annotation and
`git log <prev-tag>..<this-tag>` to see what that slice
accomplished. **Do NOT drop a scratch tag before the summary is
written and reviewed by the user.** Run
`git tag --list '2026*' | wc -l` for the current count; do not
hardcode it. `origin/dev` = `799e98f` = `v0.6.18`; `dev` is
ahead of it by the unpushed work since that tag (see "THE
GITHUB PUSH POLICY" below). `fixA-stable` = `2c0df3b` and
`20261101-stable-scheduler-fixes` = `f91d31a` are **not on
`dev`** -- they are preserved as tags because the scheduler
guards might be independently valuable, but they do not fix the
canary `.text` `#PF` and were not merged.

**WHAT IS NOT DONE.** No privilege model (item 9); SSE state is not
saved (item 10); SIGCHLD-interrupts-a-blocked-`wait4` was done
(`7cb26fb`); `sigsuspend`/`pause` done (also `7cb26fb`); job control
done (`7cb26fb`); shebang + `/bin/sh` kernel half done (`7cb26fb`);
file-backed `mmap` is still `-ENOMEM`; `chmod`/`chown` accept and
drop (FAT has no mode bits); `gettimeofday`/`time` (96/201) still
unimplemented. **The canary `.text` `#PF` is closed pending the
probe; the TCG scheduler fault is open; item 7a and 7b remain
unobserved-and-unfixed.** See `docs/open-issues.md` for the full
list. Do not rely on this paragraph alone.

**THE GITHUB PUSH POLICY.** **Nothing is pushed until a `vX.Y.Z`
release is ready and verified.** The release tag and the merge
commit are local objects; once they exist, the scratch tags may be
dropped. **Never** `git push --tags` -- it would push the scratch
tags. Push `vX.Y.Z` by name. `origin/dev` must not be force-pushed
without first reading every unpushed `dev` commit; they are
intentional work, not accidental divergence. Run
`git log --oneline origin/dev..dev | wc -l` for the current count,
or `git rev-list --count origin/dev..dev`; do not hardcode a number
here.

**THE `~/code/saved` LESSON.** A comparison tree is not necessarily
the commit it claims to be. `~/code/saved` was "`d1ae213`" by
`git log`, but had **five uncommitted files** -- the entire
2026-10-31 `^C` WIP. Every "it works at `d1ae213` but not at
`HEAD`" comparison was `d1ae213+WIP` vs `HEAD`, i.e. **WIP vs no-WIP**,
not baseline vs regression. The fix took two sessions to find because
nobody ran `git status` on the comparison tree. **Rule: before
calling anything a regression, `git status` BOTH trees. If the
"known-good" tree is dirty, `git diff` it first.** `~/code/saved`
has now been dirty **twice**; the second time (2026-11-02 evening)
its cli/sti lines matched `dev` HEAD, so it was not a different
code tree -- but it was still not a baseline. Treat `~/code/saved`
as **"not a baseline" by default**, not "a baseline with possible
WIP." Run `git -C ~/code/saved status --short` before any comparison.

---

## The canary `.text` `#PF`: probe design (Session 2)

**Two observed instances, same class, different slot:**

    # old (before 7cb26fb, first ^Z/fg reliably):
    === PAGE FAULT (#PF) ===
      CR2 / RIP : 0x413966   error 0x14 (present, user, fetch)  pt=19  pte=0

    # new (after 7cb26fb, unlucky run):
    === PAGE FAULT (#PF) ===
      CR2 / RIP : 0x450630   error 0x14 (present, user, fetch)  pt=80  pte=0

Both are canary's own cr3, both are a `present` 4 KB PT (`pde=0x...027`),
both have the faulting slot `pte=0`. `0x413966` is in `.text`; `0x450630`
is also in `.text` (the ELF image is `0x400000`-`0x600000`). **The
faulting VA is stable per instance but the specific PT slot varies**,
and the `.text` PT's *physical* address also varies by boot (see
"physical-address probes" below).

**What 7cb26fb changed:** the fault went from "fires on the first
`^Z`/`fg`" to "fires occasionally, at a moving `.text` slot." That is a
real improvement in behavior, and it names the job-control state machine
(pgrp inheritance, `wait4` pgroup semantics, console-fg handoff) as **a**
cause. It is not the whole cause.

**Post-`162a1d3` status:** the fault did not reproduce. `canary
--full` 28/28 twice (once with six `^Z`/`fg`), `pipe7e` 27500. See
"THE CANARY `.text` `#PF` -- CLOSED PENDING PROBE" in the top block.

**Ruled out (for the boots where the probes fired):**
- Bitmap corruption of the specific page under test.
- `memset(phys_to_virt(<pt>), ...)` in `vmm.c` (gated on the literal
  physical page -- silent).
- `elf.c` copy/BSS writes to that page -- silent.
- Zombie dispatch -- fixed separately (`b931603`), fault unchanged by
  that fix alone.
- `sys_munmap` from canary -- the gated `vmm_unmap_page_in_cr3` events
  on the ELF range were `exec_free_and_unmap_user_pages`, legitimate
  exec teardown.

**The next probe -- VA-based, generalized to any `.text` slot:**

Gate on the **virtual address**, not the physical page. In
`vmm_unmap_page_in_cr3` and `vmm_map_page_in_cr3`'s `else`-branch write
(and the clone's `new_pt[m] = src_pt[m]` loop for the matching slot),
print whenever the write **clears the present bit of a PTE whose `va`
is in `[0x400000, 0x600000)`** (the ELF image range). Print `va`, `cr3`,
`pt_idx`, `old`, `new`. **This catches the zeroing regardless of which
`.text` slot and which physical PT page the boot happens to use.** The
two known faulting VAs (`0x413966`, `0x450630`) are both in that range,
so gating on "any `.text` PTE becoming non-present" is the right
generalization.

Also record **`interrupts_enabled` (RFLAGS.IF)** and **`g_ticks`** at
each such write. The IF fixes from `162a1d3` are now in; if a `.text`
PTE clear still lands with IF=1 in a caller that assumed `cli`, the
fix did not cover all callers.

**Secondary tool:** gdb hardware watchpoint on the **virtual** PTE. The
PT phys must be read from the fault dump's `pde` first (it changes by
boot), then the watchpoint is `HHDM_START | (pde & ~0xFFF) + pt_idx*8`.
Arm after the fault dump prints, re-run.

**Test plan.** Three or more fresh boots. Each boot: `cd /`,
`canary --full`, with multiple `^Z`/`fg` cycles; copy `capture.txt`
aside as `capture-N.txt` before the next run. Probe silent across
all boots -> canary `.text` `#PF` is closed with evidence. Probe
fires -> canary `.text` `#PF` is independent of the IF fix; no
release; read the gate output before any edit.

**What was tried and reverted (do not reapply):** every
physical-address gate (`[BITMAP_*_33F]`, `[FREECLONE]`, `[FREE_TARGET]`,
`[UNMAP_33F]`, `[MAP33F]`, `[MEMSET_33F]`, `[ELF_WRITE_33F]`,
`[EXIT]`). They are boot-dependent. The `sys_munmap` ELF-range guard is
**kept** -- it is a range check, not a phys check.

**The correctness fixes from `b931603` (`pmm_alloc_page` type check,
`scheduler_ready_queue_next` zombie/terminated/unused skip,
`sys_munmap` ELF-range guard) and the job-control WIP from `7cb26fb`
are all in and stay.** They are independent of the remaining `.text`
zeroing.

**Note on `pmm_alloc_page`'s type check:** the `PMM: bitmap desync!`
print fires only when the bitmap and `pmm_page_info[page].type`
disagree. That is a general "metadata desync" signal, not a
`0x33F000`-specific gate -- keep it. If it fires, that is a distinct
bug from the `.text` PTE zeroing.

---

## The pipe7e `#GP`: record (closed)

**Commit `0196d66` -- duplicate `console_fg_pgrp_reclaim_if` removed.**
`7cb26fb`'s `scheduler.c` hunk added the reclaim block twice, back to
back, in `process_exit`. `console_fg_pgrp_reclaim_if` is not
idempotent in the way the second call assumes: the first call may
already have moved `g_console_fg_pgrp` to `g_console_owner_pgrp`, so
the second sees a different precondition. More importantly, each call
ended in bare `sti`, and `process_exit` holds `cli` from its top --
two `sti` sites in a `cli` region instead of one. Removed the second
block; kept the first call and the explanatory comment.

**Commit `162a1d3` -- five accessors restore caller's IF.** Converted
`console_fg_pgrp_get`, `console_owner_pgrp_get`,
`console_owner_pid_get`, `console_fg_pgrp_set`, and
`console_fg_pgrp_reclaim_if` to the `pushfq; popq %0; cli ...
pushq %0; popfq` idiom already used elsewhere in `user_syscall.c`
(lines 4073, 8187, 9025, 9074, 9121, 9231, 10495, 10692, 11163). The
trailing bare `sti` was the actual defect: it let an interrupt into
`process_exit`'s `cli` region.

**Observed reproduction and fix verification (KVM, fresh boots):**
- Pre-fix: `pipe7e` reached ~5000 iterations clean, then faulted
  around ~7000 with `#GP error 0x4640` at `0xFFFFFFFF80123D5B`.
- Post-fix: `pipe7e` reached **27500** iterations clean.
- Post-fix: `canary --full` **28/28** with six `^Z`/`fg` cycles,
  then **28/28** standalone.
- Post-fix: `^C` twice with `CTRL-C-IRQ` / `CRP` diagnostics;
  prompt returned; `ls` worked.
- Post-fix: `/root/scripts/test.sh` **57/57**.

**Mechanism note:** the fault is closed. The "interrupt landed in the
CS-check..iretq window" explanation is the **inferred mechanism, not
directly observed**; a gdb write watchpoint on the resume frame was
not run. If a future regression reproduces the `0x4640`-at-`iretq`
signature, that watchpoint is the next tool.

**Do not re-investigate.** The canary `.text` `#PF` is a separate
fault and remains open pending the probe.

---

## Fixed previously (do not re-investigate)

**`^C` -> the console no longer dies after `/ #`.** Fixed by `7cb26fb`.
The WIP was written 2026-10-31, never committed, and recovered from
`~/code/saved` (see "THE `~/code/saved` LESSON" above). What it does:
deferred `^C` signal via `console_raise_pending` (never `sys_kill` inside
IRQ1), pgrp inheritance in `sys_fork`, `wait4` pid/pgrp/wait-any forms,
`console_read` foreground check, `sys_poll` EINTR, deferred `setpgid`,
`console_fg_pgrp_set`/`_reclaim_if`, `tcgetpgrp`/`tcsetpgrp` latching,
and an IRQ1 unmask safety net in the timer.

**`^Z`/`fg` on a foreground child works across many cycles.** `jobs`
lists the stopped child; `fg` resumes. Fixed/robust by the same WIP.

**`busybox sh` nesting works.** `exit` from busybox returns to the Donix
shell; `busybox sh` from the Donix prompt returns the ash banner and
prompt. On the first PATH attempt, `sys_execve: f_open(usr/bin/BUSYBOX)
-> 4` is printed (FatFs `FR_NO_FILE`); the fallback to `/bin/busybox`
(which is staged) then succeeds. **The image does NOT have
`/usr/bin/busybox`** -- only `/bin/busybox` and `/bin/sh`. If the
fallback ever stops working, that is the `^C`/job-control state again,
not a missing file.

**`7cb26fb` is BOTH the `^C`/`^Z`/nesting fix AND the origin of the
pipe7e `#GP`.** The fix is real and stays. Two of its `cli`/`sti`
placements were wrong (`0196d66`, `162a1d3` fixed them). A future
session reading `7cb26fb`'s commit message alone would not know it
introduced a regression -- see "do not trust a commit message" below.

---

## Working style -- how this project gets changed

Read this before proposing any command block.

**One change at a time.** A "change" is a commit, verified by
**RUNNING** before the next starts -- build, run, see the behavior,
THEN commit. A build passing is not a test passing.

**The state check comes before the command block.** Every command
block starts with `git status -sb` (or `git log`).

**A command block is one paste -- write it to a file first.**
`git add`, `git status`, `git commit`, and `git tag` belong in **one**
block, with the message inline via heredoc. **Do not paste a block
with `cd ... || exit 1` into an interactive shell** -- the `exit`
closes the terminal. Write to `/tmp/<name>.sh` with
`cat > ... <<'SCRIPT_EOF'`, then run it as a **separate** paste.
Never paste a multi-line block with trailing backslashes into an
interactive shell.

**Python edits with `count != 1` abort checks are the preferred
edit style.** **`set -e` at the top.** **Idempotent edit scripts.**
**Do the count check IN PYTHON (`str.count`), not with `grep`.**
`grep -Fc` does not do multi-line matches; `grep -Fxc` requires a
whole-line match. `str.count` is the only honest multi-line count.

**BEFORE ASSERTING A COUNT, count the occurrences in the file and
write the expected number from that count, not from memory.**

**ANCHOR ON STRUCTURE UNIQUE TO THE TARGET FUNCTION.** Grep the
file, confirm the count is 1 **and** the match is in the intended
function. Anchor from the file's current bytes (`cat -A`), open
`rb`/`wb` to preserve line endings. If an anchor aborts, **ask for
the current bytes** -- do not guess a second anchor.

**A post-write check string must be copied from the `new`
literal, not retyped.**

**Ask for source you do not have.** Before patching a file it has
not seen **in this session**, ask for it. **Before writing an
anchor, read the file with `cat -A` and quote the bytes.**

**The build is TWO steps.** `make -C 04_kernel_64bit
FAT_CONFIG=single` builds the kernel; `make -C 05_boot_kernel64
hdd-single.img` builds the image. Running only the second copies
a STALE `kernel.bin`. Always run both, and verify the mtime chain
`source` < `.o` < `kernel.elf` < `kernel.bin` < `hdd-single.img`.
**After a session of probe edits and reverts, run a full
`make clean` before the next behavioral test** -- per-file `rm`
is not enough when multiple files were patched.

**`nm` output moves between builds.** Re-run `nm` before using
any address.

**A diagnostic on the SUCCESS path is noise. A diagnostic on
the FAILURE path is signal.** `SW2-*`, `HEAP_TRACE_MERGE`, the
`HEAP: corrupt` prints, and `heap_validate`'s check prints are
success-or-transient-path; they are gated. The refuse lines
(`SWITCH-DEAD`, `YIELD-REFUSE`, `SWITCH-REFUSE`, `EXIT-REFUSE`)
and the `C` report are failure-path (kept).

**Test harness.** `./run` (no arg) = clean build + fresh
`hdd-single.img` + `-enable-kvm -cpu host -display sdl`, no freeze,
gdb stub on `:1234`, `capture.txt` written (overwritten each run --
**copy it aside after any failure or pass you want to keep**).
`./run debug` adds `-S` (frozen at reset). **`./run tcg` /
`./run tcg-debug`** = the same flow in TCG (`-cpu max`, no
`-enable-kvm`): use TCG for the `mmap`-window fault
(`CR2=0x8010000000`, `cr3=0x347000`, `pt[0]=0`, `error=0x6`), KVM
for the canary `.text` `#PF` and for the pipe7e `#GP`. The Makefile
run targets also use `-cpu max` + TCG -- **state which launcher was
used.** Default cwd `/root` (prompt `~`).

**In-guest gate.** `cd /`, then `canary --full` (28/28) and
`/root/scripts/test.sh` (57/57). **Use multiple `^Z`/`fg` cycles.**
One is not always enough to reach the canary `.text` `#PF`; several
reliably do.

**GDB over `-s` has no types** (`kernel.elf` is built WITHOUT `-g`).
Use raw offsets: `pid` 0x000, `name` 0x008, `state` 0x028,
`cr3` 0x030, `entry_point` 0x038, `kernel_stack_virt` 0x048,
`kernel_stack_top` 0x050, `rsp` 0x110, `rip` 0x118, `next` 0x120,
`prev` 0x128, `block_kind` 0x158. `sizeof(pcb_t)` = 0x618.
**CONSIDER BUILDING WITH `-g`.**

**gdb write watchpoint is safe for the canary `.text` `#PF`.** The
"do not use gdb" applies to timing-sensitive repro (gdb-at-fault
masks the race). A **write watchpoint** on the physical or virtual
PTE does not mask it; it caught writers in single runs for earlier
writers in the same family.

**Do not** `git reset --hard` without stashing; **do not** edit
`third_party/`; **do not** commit `kmain.c`'s `pit_init` knob -- the
committed value is 100; **do not** call a race closed on one clean
run.

**Do not** assume a commit fixed something because its message says
it did. `f91d31a` claims to fix canary/pipe7e/ctrl-Z; it does not.
`7cb26fb` claims to fix `^C`/`^Z`/nesting and DOES -- and it also
introduces the pipe7e `#GP` (`0196d66`, `162a1d3` fixed that).
**Test every commit before trusting its message -- and note that one
commit can be both a fix and a regression at once.**

**A handoff claim is not evidence.** The old handoff said `d1ae213` was
green; it was not. The old handoff said the `^C` WIP was "lost from
`/tmp`"; it was not -- it was uncommitted in `~/code/saved`. **Re-run
every repro on a fresh boot and read the bytes before acting.**

---

## Releases

**Before a release:** the scratch `2026MMDD-*` tags are annotated
and are the source for the version summary. **The scratch tags
are PRESERVED until the user decides to push to GitHub, and
are dropped ONLY after the summary is written and reviewed by
the user.** A session does NOT drop them on its own initiative,
even when the release tag and merge commit exist locally. The
drop is step 4 of the release procedure below, and it is taken
by the user, at release time, after reviewing the summary.

**The release procedure** (on `dev`, pre-release):

1. **Patch the docs first.** Update this file's release paragraph
   and (optionally) `docs/version-history.md`. Commit.
2. **Bump the version string in `kmain.c` manually** (the banner).
   Commit.
3. **Tag** `vX.Y.Z` annotated, on `dev`, summary from the scratch
   tags' annotations and `vPREV..HEAD`:

       git tag -a vX.Y.Z -m "<version summary>"

4. **Drop the scratch tags locally** (the tag and summary now
   exist):

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

Do not use `git push --tags` -- it would push the scratch tags.
Push `vX.Y.Z` by name.

**The GitHub push policy.** **Nothing is pushed until `vX.Y.Z` is
ready and verified.** `origin/dev` is not force-pushed; every
unpushed `dev` commit is intentional work. Run
`git log --oneline origin/dev..dev | wc -l` for the current count.

**Current release state.** **`v0.6.18` is released.** Work is now
pre-**`v0.6.19`**. **Release gate: the canary `.text` `#PF` probe
(Session 2) must complete before `v0.6.19` is tagged.** If the
VA-gated probe is silent across 3+ fresh boots with `^Z`/`fg`
cycles, the canary `.text` `#PF` is closed with evidence and
`v0.6.19` ships carrying the `^C`/`^Z`/nesting fix plus the pipe7e
`#GP` fix. If the probe fires, the canary `.text` `#PF` is
independent of the IF fix and stays open; no release. Do NOT ship
with the canary `.text` `#PF` accepted as a known open bug without
an explicit decision recorded here.

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static
musl-linked binaries on the Linux x86_64 ABI, natively. Newlib is
gone. Userland is a tracked source tree at `userland/musl/`. Phase B
(busybox) is well underway: the shell is interactive, pipelines and
redirection work, and the applet set now includes `bc`, `dc`, `diff`,
`du`, `awk` in addition to the file/text applets.

Full strategy: `docs/strategy.md`. Direction: `ROADMAP.md`.
Concurrency rules: `docs/contract.md`.

---

## Where we are

### Current baseline

- `test.sh` 57/57; `canary --full` 28/28; `pipe7e` clean at 27500
  iterations (post-2026-11-02 evening IF fix). The
  `^C`/`^Z`/nesting fixes from `7cb26fb` are in and verified.
- **The canary `.text` `#PF` did not reproduce after the IF fix**
  (`162a1d3`), including with six `^Z`/`fg` cycles. **Closed pending
  the VA-gated probe (Session 2)**, which is the `v0.6.19` release
  gate.
- **The pipe7e `#GP` (`error 0x4640`) is closed** (`0196d66`,
  `162a1d3`). Post-fix: `pipe7e` 27500, `canary --full` 28/28 x 2,
  `test.sh` 57/57, `^C` twice with prompt recovery.
- **The TCG scheduler fault** (`./run tcg`, `rsp=0`, bad cr3,
  `SWITCH-REFUSE` after dispatch) -- **open, separate**, needs its
  own session. See "THE TCG SCHEDULER FAULT" in the top block.
- `^Z` on a foreground child -> `[1]+ Stopped`; `jobs` lists it; `fg`
  resumes. Works across many cycles.
- `^C` -> SIGINT reaches the foreground pgrp; the console survives and
  the prompt returns. **Fixed 2026-11-02** (`7cb26fb`).
- `busybox sh` nesting -> works. `exit` returns to Donix shell;
  `busybox sh` returns the ash prompt. **Fixed 2026-11-02**
  (`7cb26fb`).
- `exec_churn` -> 24 rounds pass.

### Next -- the highest-value work

1. **The canary `.text` `#PF` probe (Session 2).** The `v0.6.19`
   release gate. Add the VA-gated probe, run `canary --full` with
   `^Z`/`fg` on 3+ fresh boots, revert the probe. Silence across all
   boots -> closed with evidence -> release. Fires -> open, no
   release.
2. **The TCG scheduler fault** (`rsp=0`, bad cr3, `SWITCH-REFUSE`
   after dispatch). Its own session. Move the `SWITCH-REFUSE` guard
   before dispatch; find who zeroes `rsp`.
3. **`open-issues.md` refresh.** Last touched at `701dcc1`; three
   sessions of work since. File the TCG fault as a new un-numbered
   item, and note that `7cb26fb` closed SIGCHLD-interrupts-wait,
   `sigsuspend`/`pause`, job control, and shebang + `/bin/sh` (all
   listed as open in the old version). Drop the old entries that are
   now done; keep 7a, 7b, 9, 10, 7e, and the un-numbered signal / VFS
   items that remain.
4. **Item 9 -- the privilege model.** setuid/seteuid (105/117), a
   setuid-root marker on FAT (magic-marker shape, same as item 8), a
   `sudoers` equivalent, then change 102/104/107/108/115 to the
   unprivileged default (1000) with root as an escalation.
5. **Item 10 -- SSE save/restore.** `fxsave`/`fxrstor`/`xsave` in
   `context_switch.asm` and the interrupt path, then remove the
   userland `-mno-sse*` flags.
6. **busybox batch 2; `test.sh`'s md5.**

### Refuted -- do NOT re-walk

These were tried and reverted; do not reapply without new evidence.
They are listed so a future session does not spend the same cycles.
If any turns out to be a different root cause, that is a **new**
hypothesis, not a revival of these.

- **The physical-address gates on `.text` PTE zeroing.** `[BITMAP_*_33F]`,
  `[FREECLONE]`, `[FREE_TARGET]`, `[UNMAP_33F]`, `[MAP33F]`,
  `[MEMSET_33F]`, `[ELF_WRITE_33F]`, `[EXIT]`. Boot-dependent (the
  `.text` PT physical address varies by boot); silent on the boot
  where the `.text` PT landed at `0x340000`. **Gate on the invariant
  (VA, pid, state, pgrp), not on the address that happens to hold
  today.**
- **The "canary `.text` `#PF` needs a keystroke" hypothesis (2026-11-02).**
  Disproved by the pre-`162a1d3` run that faulted at iter ~7000
  *after* a `^Z`/`fg` but could not be distinguished from a run that
  faulted without one -- and by the post-`162a1d3` run that reached
  27500 clean with `^Z`/`fg` and without. The IF-window mechanism is
  the explanation for the pipe7e `#GP`; whether it is also the
  explanation for the canary `.text` `#PF` is exactly what the
  Session 2 probe tests. Do not settle it by argument.
- **The "`~/code/saved` has a different cli/sti discipline" hypothesis
  (2026-11-02).** Disproved: `~/code/saved`'s working tree has the
  same `cli`/`sti` lines as `dev` HEAD (the second time it was dirty,
  2026-11-02 evening). The tree is not a "different code" tree. It
  is still not a baseline -- see "THE `~/code/saved` LESSON".
- **The "`console_fg_pgrp_reclaim_if` duplicate is idempotent"
  hypothesis.** Disproved: the first call may already have moved
  `g_console_fg_pgrp` to `g_console_owner_pgrp`, so the second sees a
  different precondition. The duplicate was removed (`0196d66`).
- **The `f91d31a` "scheduler guards fix canary/pipe7e/ctrl-Z" claim.**
  Disproved by running: `f91d31a` turns the canary `#PF` into a `Z`
  halt; it does not fix the fault. Preserved as tag
  `20261101-stable-scheduler-fixes` for cherry-pick value only.
- **The reserve / frame placement; the `next->rsp = 0` clear; the
  pipe-waiter fields as the `C`'s stale holder; the
  `current_process` dual-copy divergence at the `C`; a
  switch-to-self guard in `scheduler_switch_to`'s tail; a caller
  heap overflow.** All closed in earlier sessions; see the old
  `open-issues.md` and `git log` for the commit history. Do not
  re-open item 11, the session-48 zone scan, the `fs_base` path, or
  the `longjmp`/`setjmp` family.

### Open issues (top 6)

1. **The canary `.text` `#PF` -- closed pending the probe (Session 2).**
   The `v0.6.19` release gate. See "THE CANARY `.text` `#PF`" in the
   top block.
2. **The TCG scheduler fault** (`./run tcg`, `rsp=0`, bad cr3,
   `SWITCH-REFUSE` after dispatch) -- open, separate. See "THE TCG
   SCHEDULER FAULT" in the top block.
3. **`open-issues.md` item 9 -- no privilege model.** uid/gid are 0
   for every process; no `setuid`, no `chown`, no way to become root.
   See `docs/open-issues.md` item 9.
4. **`open-issues.md` item 10 -- `CR4.OSFXSR` set, no SSE saved.**
   The session-52 stopgap (`-mno-sse*` in userland CFLAGS) is not a
   fix. See `docs/open-issues.md` item 10.
5. **`open-issues.md` item 7a -- boot-time `#PF` at `0x400000`.**
   Unobserved, not fixed; the `VMM: map failed site=<NAME>`
   instrumentation is in the tree and did not fire on the boots that
   followed. **Distinct from the canary `.text` `#PF`** (different
   address, different mechanism). See `docs/open-issues.md`.
6. **`open-issues.md` item 7b -- user-mode `#GP` at `0x42F1A7` in
   busybox during `find`.** Unobserved, not fixed; the disassembly
   note in `open-issues.md` says the recorded address is not an
   instruction boundary and the real fault is either `0x42F1A3` or
   `0x42F1AB` on a non-canonical register. See `docs/open-issues.md`.

Items 13 (job control) and 14 (shebang + `/bin/sh` kernel half) from
older handoffs are now **closed** by `7cb26fb`; they do not appear
here. `open-issues.md` item 7e (ash stale `jmp_buf` in `third_party/`)
is **not editable in-tree** and belongs in "Refuted -- do NOT
re-walk" above, not here.

---

## State on disk

Kernel sources: `04_kernel_64bit/`. `capture.txt` is gitignored,
overwritten each `./run` -- **copy it aside after any failure or
pass you want to keep.**

**After a fresh image, one manual step:** the image stages
`/bin/busybox` alone. To get the applet symlinks (including
`/bin/sh`, which shebang needs), boot and run
`/bin/busybox --install -s /bin`. The absolute path is required:
busybox reads `/proc/self/exe` to find itself, donix has no
`/proc/self/exe`, and it falls back to `argv[0]` -- which must
start with `/`. This is a manual step, not part of the build.

**`third_party/`** is **gitignored** -- do not commit edits to it.
`third_party/busybox/` has `busybox` (stripped) and
`busybox_unstripped` + `.map` if a userspace symbol is ever needed.
**`third_party/musl-install/` is gitignored and is needed for the
build.** If it's missing after a `git clean -xdf`, copy it back from
a working tree, or rebuild it with `toolchain/install_musl.sh`.
`04_kernel_64bit/fatfs/ff.c` is **tracked** (not `third_party/`),
built by the cross-GCC.

**`~/code/saved`** -- not a baseline. See "THE `~/code/saved`
LESSON" in the top block. Run `git -C ~/code/saved status --short`
before any comparison; treat "not a baseline" as the default.

**`~/donix-20261101-canary-probes.patch`** -- the old physical-address
probe stack. **Do not apply it as a stack**; it broke the boot.

**`capture.txt` variants.** Post-2026-11-02 evening: keep `capture-A.txt`
(no-keystroke) and `capture-B.txt` (`^Z`/`fg`) from the fix
verification in a safe place -- they are the "did not reproduce"
record for the canary `.text` `#PF`. The Session 2 probe runs will
overwrite `capture.txt`; the A/B captures must be copied aside.

---

## One-line summary

**`v0.6.18` is the last release.** `^C`/`^Z`/nesting are fixed
(`7cb26fb`); the pipe7e `#GP error 0x4640` they introduced is closed
(`0196d66`, `162a1d3`). The canary `.text` `#PF` did not reproduce
after the IF fix and is **closed pending the VA-gated probe
(Session 2)**, which is the `v0.6.19` release gate. The TCG
scheduler fault (`rsp=0`, bad cr3) is separate and open. Read
`docs/contract.md` and "NEXT" in this file before any code.
