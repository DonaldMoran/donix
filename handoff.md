# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-30 (session close, fourth session this
date).  **`v0.6.18` is released; work is pre-`v0.6.19`.**  This
session made **one code change** (`a1f4eed`, the `wait4` late
re-scan, committed and tagged) and **found two live bugs** that the
prior session's docs cleanup had wrongly recorded as refuted.  The
prior commit `783b684` is **partially wrong** and must be corrected
before anything builds on it.

**Two open bugs, both with deterministic repros:**

1. **Backgrounded applet dispatch, order-dependent.**  On a fresh
   boot, `sleep N &` as the first backgrounded command fails:
   `f_open(/bin/busybox) -> 5`, then the PATH walk
   (`sleep`, `/sbin/sleep`, …, all `-> 5`), then
   `sh: sleep: not found`.  **`yes > /dev/null &` first makes it
   work.**  The warmer is `yes` specifically, not `/dev/null`, not
   redirection, not a live child, not a stopped child:
   - `echo hello > /dev/null &` first → does **not** warm.
   - `sleep 1 > /dev/null &` first → fails.
   - `sleep 100 &` first → fails, does not warm.
   - `sleep 100` foreground, `^Z`, then `sleep 1 &` → fails.
   - `yes > /dev/null &` first → **warms**; the next `sleep N &`
     runs and reaches `Done` in `jobs`.
2. **`kill` on a `RUNNING` background job hangs the OS.**  Killing
   a `STOPPED` background job is clean.  Same build, same applets,
   same job table; the only variable is the target's state when the
   signal lands.
   - `[2]+ Running sleep 300`, `kill %2` → **hang** (twice).
   - `[2]+ Stopped sleep 300`, `kill %2` → prompt returns,
     `echo hello` works.
   - **`kill` on a background job *did* work once** this session,
     in the `wait4` acceptance transcript, after `fg`/`^Z` cycles:
     `kill %2` (stopped `sleep`) then `kill %1` (running `yes`),
     prompt returned.  So the `kill` hang is either state-dependent
     (the `fg`/`^Z` history matters) or intermittent.  It is **not
     closed**; the Running-kill hang is the standing repro.

**Not yet run, and the first test of the next session:**
`sleep 300 &`, `fg`, `^C` — does killing a **foreground** `RUNNING`
process hang?  If yes, the bug is "terminate from `RUNNING`."  If
no, it is "terminate a `RUNNING` *background* process whose parent
is in `wait4`."  Those need different fixes.

**A fix shape to evaluate, not yet agreed:** make `kill` park a
`RUNNING` target in the state the `STOPPED` case uses, then
terminate from there, so the bad case follows the known-good path.
Two cautions before writing it: `STOPPED` is observable
(`wait4(WUNTRACED)`, `jobs`, `ps`) so the park must be atomic or
invisible; and the hang may be in the shell's `wait4` wake rather
than the target's exit, which parking would not fix.  The
foreground `^C` test above decides which.

Baseline on `HEAD`: `canary --full` 28/28 and `test.sh` 57/57 were
green at `701dcc1` and the docs commit did not touch code; the
`wait4` change built clean and passed its stop/report/resume
acceptance.  `dev` is ahead of `origin/dev` by **54 commits**
(nothing pushed).  **Tree: `a1f4eed` on `783b684` on `701dcc1`.**

**Read "The concurrency contract" in `docs/contract.md` before
touching shared state, then "Where we are".**

---

**THE TWO COMMITS THIS SESSION** (on `dev`, tagged, nothing
pushed):

- `783b684` `docs: retire items 13/14; drop the unreproduced FatFs
  diagnosis` (tag `20261030-retire-13-14`) -- retires 13 and 14
  correctly, and drops the fourth-session FatFs block.  **But it
  also records the `&` bug as refuted, which is wrong** -- see the
  two open bugs above.  The next session corrects this.
- `a1f4eed` `wait4: the late re-scan reports a child that stopped
  in the window` (tag `20261030-wait4-late-stop`) -- `sys_wait4`'s
  late re-scan (inside the `cli`, after the block-store) checked
  only `ZOMBIE`/`TERMINATED`.  A child that transitioned to
  `STOPPED` in the window missed the wake and the re-scan, so a
  `wait4(WUNTRACED)` parent blocked forever.  The late re-scan
  gains a `late_stopped` branch with the same `WUNTRACED` guard as
  the first scan; on a late stop it un-blocks and continues, and
  the first scan reports the stop.  Fix is in `sys_wait4`, not in
  `process_wake_parent_if_waiting` (three call sites; the re-scan
  is call-site-agnostic).  Acceptance: two `fg`/`^Z` cycles on
  `sleep 300`, `jobs` showing `Stopped` each time.

**CORRECTIONS THIS SESSION TO PRIOR DOCS:**

- **The `&` bug is NOT refuted.**  The prior session ran
  `yes > /dev/null &`, `sleep 1 &`, `sleep 300 &` and saw them
  work; that was a warmed boot.  The failure reproduces when
  `sleep N &` is first.  `783b684` wrongly deleted it from NOT DONE
  and the one-line summary; the next session restores it with the
  order-dependence.
- **The FatFs `dp->dir`/`fs->win` diagnosis:** still unreproduced
  on its own, and `git log v0.6.18..HEAD -- '*ff.c' '*fatfs*'
  'third_party/'` is empty, so no `ff.c` change survives.  But the
  `&` bug it was built on **is real** (order-dependent, above), so
  the diagnosis is back to "needs a fresh repro," not "refuted."
  Do not modify `ff.c` without reading the `dir_*`/`move_window`
  contract in full first.
- **The `wait4` late re-scan is done** (`a1f4eed`).  It comes out
  of the follow-ups list.

**WHERE WE ARE.**  Job control on the framebuffer/PS/2 console
works end to end: Ctrl-Z raises SIGTSTP to the foreground pgrp;
`wait4(WUNTRACED)` reports `[1]+ Stopped`; `jobs` lists it; `fg`
resumes it for both a `console_read` job (`cat`) and a `nanosleep`
job (`sleep 300`).  `bg` works.  Shebang / `/bin/sh` is done
(`0d93f8c` + `62ee388`).  The `wait4` stopped-child window is
closed (`a1f4eed`).

**NOT DONE, live:**

- **Backgrounded applet dispatch, order-dependent.**  Above.
  `sleep N &` first fails; `yes > /dev/null &` first warms it.  The
  warmer is `yes` specifically.
- **`kill` on a `RUNNING` background job hangs.**  Above.  Stopped
  target is clean; foreground `^C` not yet tested.
- `process_stop_and_yield`'s `while (STOPPED) process_yield()` loop
  has not been exercised with **no runnable peer**.
- A `SWITCH-REFUSE: next pid=8 name=busybox rsp outside stack ->
  idle` was observed once, after `fg` cycles, and not since.  Same
  family as the `kill`-RUNNING hang; `scheduler.c:1252` is the site
  and the `context_switch.asm` `0x110` consume is the mechanism.
  **Do not patch `process_stop_and_yield`'s trailing yield
  speculatively.**
- `kill(pid <= 0)` group semantics: `kill(-pgid, sig)` works;
  `kill(0, sig)` still returns `-EINVAL`.
- Ctrl-C on the serial console is not mapped to SIGINT (host
  `intr = ^C` eats it).

**REFUTED SHAPES (do not retry):**

1. `cli`-depth lock around the ATA transfer.  Crashed on boot.
2. `f_sync` on inherited `FIL`s.
3. `f_opendir("/tmp")` + `f_opendir("/")` warm-up.
4. `user_syscall_clear_file_table(self)` alone.
5. Removing `strip_dot_prefix(exec_path)` -- a real latent bug, but
   not the `&` bug.  One-line cleanup for a later commit.
6. `f_mount(NULL)` + `f_mount(fs)` in `sys_execve`.
7. `f_read`/`f_write` `winsect = -1`.
8. `move_window` in `dir_sdi` and `dir_next` tails.  Broke boot.

**WHERE WE ARE, the rest:** no real `mount(2)`; file-backed `mmap`;
`link(2)`/`mknod(2)`; `chmod`/`chown` accept and drop; SSE state is
not saved (item 5); no privilege model (item 6).

**NEXT.**

1. **Test `sleep 300 &`, `fg`, `^C`** -- does a foreground `RUNNING`
   kill hang?  Decides which `kill` bug we have.
2. **Correct the docs** -- restore the `&` bug with the
   order-dependence; add the `kill`-RUNNING hang; put the FatFs
   diagnosis back to "needs repro"; remove the `wait4` late re-scan
   from the follow-ups.  One commit, both files.
3. **Fix the `kill`-RUNNING hang** -- the more serious bug.  Start
   from the repro, read `sys_kill`'s TERMINATE branch and
   `process_exit`, and evaluate the park-then-kill shape against
   the foreground-`^C` result.
4. **Fix the `&` dispatch** -- order-dependent; diagnose the first
   backgrounded `execve` when no `yes` has run.
5. SSE (item 5).  6. Privilege model (item 6).  7. busybox batch 2.
8. PIPE7E soak at 100 Hz.  9. `run`'s `stty susp undef` restore.
10. The no-peer stop soak; the interpreter-argv proof.

### Working style -- how this project gets changed

Read this before proposing any command block.

**One change at a time.**  A "change" is a commit, verified before
the next one starts.  **Verify by RUNNING, not by committing.**  A
build can pass and the test still be the wrong test.

**A handoff claim is not evidence.**  This session found two bugs
the prior handoff had recorded as refuted, because the prior
session ran the repro on a warmed boot and concluded from it.
**Re-run every named repro on a fresh boot before acting on its
diagnosis**, and run it in the *unwarmed* state as well as the
convenient one.  The `yes > /dev/null &` warmer is the tell: a
clean run after a warmer is not a clean run.

**A command block is one paste -- write it to a file first.**
`git add`, `git status`, `git commit`, `git tag` in one block,
message inline via heredoc.  `cat > /tmp/x.sh <<'SCRIPT_EOF'` and
run it in the same paste.  Prove with `make -n` before building.

**Python edits with `count != 1` abort checks.**  `set -e` at the
top.  Count in Python, not `grep`.  Count first, assert the
counted number.  **Anchor on structure unique to the target
function**, read the anchor from the file's current bytes
(`cat -A`), and copy post-write check strings from the `new`
literal, not from memory.

**A Makefile recipe is not plain shell.**  `#` inside a
backslash-continued recipe is stripped; comments go above the
recipe.  `make -n` is the proof.

**A build can pass and the test still be the wrong test.**  Run
the feature from a non-root cwd as well as `/`.  Run it the way a
user would reach it.

**Ask for source you do not have.**  Before patching a file not
seen this session, ask for it.

**The build is TWO steps.**  `make -C 04_kernel_64bit
FAT_CONFIG=single`; `make -C 05_boot_kernel64 hdd-single.img`.
Running only the second copies a STALE `kernel.bin`.  Verify
`kernel.elf` is newer than the source and that `kernel.bin` was
regenerated from it.  A patch with no new string has only the
mtime check; say so in the commit message rather than claiming
more.

**A config change does not relink by itself.**  Touch
`third_party/busybox/.config` and remove
`userland/musl/build/.busybox.stamp` before the two-step build.

**`EXTRA_CFLAGS` is NOT forwarded.**  `SW2_TRACE` is a `#define` in
`scheduler.c`.  Any diagnostic rebuild is a deliberate change with
its own risk of perturbing the bug.

**`nm` output moves between builds.**  Re-run `nm`.  `pcb_pool`
stride `0x840`; `_Static_assert` pins offsets through `user_rsp`
(0x610); `PROC_STATE_STOPPED == 6`.  `kernel.elf` has no `-g`.

**A commit message is not a diff and not a test.**  `git show
<sha> --stat` is the check; do not write "verified" for something
the transcript does not show.

**Test notes.**  `pidof <applet>` finds nothing (every applet's
`comm` is `busybox`); use `ps` and the `STAT` column (`T` is
stopped).  `/proc/<pid>/` exists only for live pids.

**Do not run `git reset --hard` without stashing.  Do not edit
`third_party/`.  Do not commit `kmain.c`'s `pit_init` knob.  Do
not call a race closed on one clean run.**

---

## Releases

**`v0.6.18` is released.**  Work is pre-**`v0.6.19`**.  The
`20261030-*` tags are the restore points.  **Nothing is pushed**
until `v0.6.19` is ready and verified.  Items 13 and 14 close into
this release.  The two live bugs above are **not** release
blockers for `v0.6.19` unless the next session decides they are;
they are recorded so they are not lost.

---

## One-line summary

**Job control and shebang work; the `wait4` stopped-child window is
closed; two live bugs are open.**  `a1f4eed` fixed `sys_wait4`'s
late re-scan to report a child that stopped in the block-store
window, acceptance-tested with two `fg`/`^Z` cycles.  Two bugs
found this session, both deterministic: (1) `sleep N &` as the
first backgrounded command fails with `f_open(/bin/busybox) -> 5`
and a PATH walk, unless `yes > /dev/null &` runs first; (2)
`kill` on a `RUNNING` background job hangs the OS, while a
`STOPPED` target kills cleanly.  The prior docs cleanup
(`783b684`) wrongly recorded (1) as refuted; the next session
corrects the docs, tests whether a foreground `^C` also hangs,
and fixes the `kill`-RUNNING hang first.  Read
`docs/contract.md` and "NEXT" before any code.
