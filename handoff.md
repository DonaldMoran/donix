# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-11-02 (late). **`v0.6.18` is released.**
Work is pre-`v0.6.19`. `dev` = `873be27` (tag
`reset-baseline-873be27`). `origin/dev` = `799e98f` = `v0.6.18`;
run `git rev-list --count origin/dev..dev` for the real ahead-count
on `dev`. Working tree clean. No stashes. Build green, canary
`28 passed, 0 failed`.

**WHERE WE ARE.** The 2026-11-02 session ended with a reset. The
C-halt investigation and the four scheduler guards that had been
layered on trying to fix it were discarded; the tree is at
`873be27` (`context_switch: extend the C dump with the frame GPRs
and PCB fields`), which is the extended-C-dump baseline, with tag
`reset-baseline-873be27` marking it. Both stashes from that
session were dropped. The C is **open** (see below). Work now
moves to the top 3 in "NEXT."

**NEXT -- top 3.**
1. **`exec_churn` performance: the `prnt`/`total` variance.**
   Rounds print `prnt 0` on most iterations and `prnt 70-140` on
   some, with `total` swinging 30-170 ms between otherwise
   identical rounds (seen on the `873be27` run). Split the slow
   rounds: `fork`, `execve`, `wait4`, or the printf path; and
   check whether the variance tracks `process_free_clone` on the
   exit path (`exec_churn` is the test that exercises it -- see
   `docs/open-issues.md`'s test-notes history). Do not guess at a
   scroll cause; the `873be27` run reproduced the bimodality with
   the row blit already in.
2. **`docs/open-issues.md` item 9 -- the privilege model.**
   uid/gid are 0 for every process; no per-process uid/euid
   split, no setuid, no `chown`. The item lists the shape:
   `setuid`/`seteuid` (105/117), a setuid-root marker on a
   FAT-hostile filesystem (magic-marker, same shape as item 8), a
   `sudoers` equivalent, then 102/104/107/108/115 return 1000 with
   root as an escalation.
3. **`docs/open-issues.md` item 10 -- SSE save/restore.**
   `CR4.OSFXSR` is set but the kernel saves no SSE state;
   userland is pinned to `-mno-sse*` as a stopgap. The fix is
   `fxsave`/`fxrstor`/`xsave` in `context_switch.asm` and the
   interrupt path, then remove the `-mno-sse*` flags from the
   userland build.

**THE C HALT -- OPEN, NOT THE CURRENT FOCUS.** The C is
`context_switch_resume`'s unknown-CS branch: the frame at
`next->rsp` has a CS slot that is neither `0x18` nor `0x33`. It
was last characterized at `dev = efc8e08` with four uncommitted
scheduler guards; that commit and those guards were discarded on
2026-11-02, the tree was reset to `873be27`, and both stashes
were dropped. `efc8e08` is no longer on `dev` (it survives as a
commit object). The extended C dump in the tree is `873be27`'s.
**One `canary --full` at `873be27` passed 28/28** -- per this
file's own rule, that does not close the C; it is one run. No C
work is planned until the top 3 are addressed. The investigation's
discarded paths are listed under "Refuted -- do NOT re-walk"
below; do not re-derive them from scratch without new evidence.

**THE CANARY ROW COUNT.** `canary --full` at `873be27` printed
the full 28-row sequence and `28 passed, 0 failed`, ending at the
`/ #` prompt. The 28 rows include the 15 read-only rows and the
13 mutating rows. `exec_churn` was **not** part of that run; its
bimodality data is from the earlier `873be27` run.

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

**Never put a destructive git command inside a block that also
runs diagnostics.** A `git checkout -- <file>` or `git restore`
in a block whose other lines are `git status`/`git log`/`grep` is
a trap: it discards working-tree work while the operator is
reading diagnostics. If a revert is wanted, name it on its own
line, say what it discards, and run it as its own paste. (The
2026-11-02 session lost the four scheduler guards to exactly this
shape.)

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
`-enable-kvm`). The Makefile run targets also use `-cpu max` + TCG
-- **state which launcher was used.** Default cwd `/root` (prompt
`~`).

**In-guest gate.** `cd /`, then `canary --full` (28/28) and
`/root/scripts/test.sh` (57/57). **Use multiple `^Z`/`fg` cycles.**
One is not always enough to reach an intermittent fault; several
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
PTE does not mask it.

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
green; it was not. **Re-run every repro on a fresh boot and read the
bytes before acting.**

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
pre-**`v0.6.19`**. There is no release gate recorded for
`v0.6.19`; the previous gate (the canary `.text` `#PF` probe)
was tied to a tree that has since been reset. If a gate is wanted
for `v0.6.19`, it is set in this file before the release session,
not during it.

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static
musl-linked binaries on the Linux x86_64 ABI, natively. Newlib is
gone. Userland is a tracked source tree at `userland/musl/`. Phase B
(busybox) is well underway: the shell is interactive, pipelines and
redirection work, and the applet set now includes `bc`, `dc`, `diff`,
`du`, `awk` in addition to the file/text applets.

Full strategy: `docs/strategy.md`. Direction: `ROADMAP.md`.
Concurrency rules: `docs/contract.md`. Open issues:
`docs/open-issues.md`.

---

## Where we are

### Current baseline

- `test.sh` 57/57 (last run before the reset; re-run on a fresh
  boot before relying on it after any scheduler change).
- `canary --full` **28/28** at `873be27` (2026-11-02 late). The
  full 28-row sequence, no skips.
- `exec_churn` 24 rounds pass (from the same `873be27` run); the
  `prnt`/`total` variance is the top NEXT item.
- `^Z` on a foreground child -> `[1]+ Stopped`; `jobs` lists it;
  `fg` resumes.
- `^C` -> SIGINT reaches the foreground pgrp; the console survives
  and the prompt returns.
- `busybox sh` nesting -> works.
- The C is open; see "THE C HALT" above.

### Next -- the highest-value work

1. `exec_churn` performance: the `prnt`/`total` variance.
2. `docs/open-issues.md` item 9 -- the privilege model.
3. `docs/open-issues.md` item 10 -- SSE save/restore.

### Refuted -- do NOT re-walk

These were discarded with the 2026-11-02 reset. They are listed so
a future session does not spend the same cycles. If any turns out
to be a different root cause, that is a **new** hypothesis, not a
revival of these.

- **The `efc8e08` rsp/irq0_rsp split, and `frame_kind`.** Discarded
  with the reset; not on `dev`. The C was not prevented by it.
- **The four uncommitted scheduler guards** (`process_yield`
  FRAME_IRQ0 skip, `scheduler_ready_queue_add` non-PCB guard,
  `scheduler_switch_to` non-PCB guard, `scheduler_switch_to`
  frame-CS validate). Discarded with the reset and the stash drop.
- **The `EXIT-REFUSE frame-CS` guard in `process_exit`.** Discarded
  with the reset and the stash drop.
- **The canary `.text` `#PF` probe stack** (`[PT-CANARY]`,
  `[PT-WP]`, `[PHDR]`/`[EHDR]`, `[UNMAP_IN_CR3]`, `[FREE_CLONE]`,
  `[PCLAIM]`). It was in `stash@{1}` and was dropped. The probe
  arm was gated on canary `pid == 4`, which did not match the
  first user process at `873be27` (pid 2); it was a non-test on
  that boot. Not revived.
- **The physical-address gates on `.text` PTE zeroing.** Boot
  dependent; do not reapply without new evidence. Gate on the
  invariant (VA, pid, state, pgrp), not on the address that
  happens to hold today.
- **The `f91d31a` "scheduler guards fix canary/pipe7e/ctrl-Z"
  claim.** Disproved by running.

### Open issues

The full list is `docs/open-issues.md`. The numbered items are 9
(no privilege model) and 10 (no SSE save/restore). The
un-numbered items (7a, 7b, 7e, the `0x413962` `#PF`, the
`.bss`-adjacent faults, and the small VFS/proc/pipe entries) are
real and stay in that file; they are not in this session's NEXT.
Read `docs/open-issues.md` before touching the VFS, fork, exec,
or the signal paths.

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

**`~/code/saved`** -- not a baseline. Run
`git -C ~/code/saved status --short` before any comparison; treat
"not a baseline" as the default. It has been dirty twice.

**The reset baseline.** `dev` = `873be27`, tag
`reset-baseline-873be27`. No stashes exist. `efc8e08` is not on
`dev`; it survives as a commit object. If a future session needs
the discarded guards, they are not in the tree -- read
`git log --all --oneline` for `efc8e08` and the session log, but
treat them as **refuted** per the section above.

---

## One-line summary

**`v0.6.18` is the last release.** The tree is at `873be27`
(`reset-baseline-873be27`), clean, no stashes; the C-halt
investigation and its guards were discarded in the 2026-11-02
reset and the C is open with one clean `canary --full` (28/28)
recorded. NEXT is the top 3: `exec_churn` performance, item 9
(privilege model), item 10 (SSE save/restore). Read
`docs/contract.md` and this file's NEXT before any code.
