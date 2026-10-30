# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-29 (session close).  **`v0.6.18` is
released; work is pre-`v0.6.19`.**  Signals slices 1-5 are in it,
with `tkill`, the busybox `KILL` applet, and the `SIG_IGN` fix.
Slice 6 (signals interrupt blocking syscalls) covers `wait4`, pipe
`read`, pipe `write`, `pause`, and `sigsuspend`.  **Session
2026-10-29 (evening) landed three things the previous handoff
listed as next:**

1. **The `YIELD-REFUSE`-on-`BLOCKED` scheduler fix** -- a refused
   yield leaves a `BLOCKED` caller `BLOCKED` instead of forcing it
   `RUNNING` with no saved frame (the `RSP=0` `#PF` shape).
2. **Fork inherits signal dispositions, exec resets them** (one
   POSIX pair, landed in two commits: `c7a7962` + `14c50a0`).
3. **`nanosleep` blocks and is interruptible** -- the documented
   limitation is gone.  `BLOCK_KIND_NANOSLEEP`, a `pcb_t.wake_tick`
   deadline, the timer waking the deadline, `-EINTR` on a caught
   signal.  `NANOSLEEP_INTR_PROBE` is the acceptance test and it
   passes (`rc=-1 errno=4 got=1`).

Plus an idle-dispatch cleanup (`7f43ac4`): `process_yield` now
dispatches idle instead of refusing it, so `sleep` no longer prints
`YIELD-REFUSE`.  Every path syscall reaches its filesystem through
the mount table (`mount_driver(backend)->op(...)`); the resolver is
`vfs_resolve(path, follow_final)`; symlinks work end to end.
`test.sh` is 57/57 and `canary --full` is 28/28, verified from `/`
on a freshly built image twice interleaved after the scheduler
changes, with `NANOSLEEP_INTR_PROBE` PASS.  Read "The concurrency
contract" in `docs/contract.md` before touching shared state, then
"Where we are".

**THE RESTORE TAGS FROM THIS SESSION.**
`20261029-nanosleep-blocks-verified` (probe PASS, gate clean,
`wake_tick` landed) and `20261029-idle-dispatch-fixed` (idle
dispatch, `sleep` silent, gate clean x2).  Both on `dev`.  The
slice-6 tags (`20261029-slice6-wait4`, `-pipe-read`, `-pipe-write`,
`-pause`, `-sigsuspend`, `-done`) remain.  Nothing is pushed.

**WHAT THE VFS IS NOW, in one paragraph.**  `g_mounts` maps `/`
(FAT), `/dev`, and `/proc` to `fs_driver_t` vtables: `unlink`,
`rename`, `mkdir`, `open`, `stat`, `access`.  `resolve_at` became
`vfs_resolve`, which canonicalizes the path, selects the mount by
longest prefix on a component boundary, and -- when `follow_final`
is 1 -- follows a `DONIX_LINK:` marker (an ordinary FAT file whose
first line is `DONIX_LINK:<target>`), capped at 8 hops (`-ELOOP`).
Every path syscall calls `vfs_resolve` and then the mount's
operation; no syscall tests `BACKEND_*` to choose an operation.
Devices are rows in `g_dev_table` with `(major, minor, ino, read,
write)`; `/dev/null` is 1,3; `console` 5,1; `tty` 5,0; `zero` 1,5;
`urandom` 1,9.  `st_dev`/`st_ino` are identity, `st_rdev` is the
device number.

**WHAT IS NOT DONE.**  A real `mount(2)` (the table is `static
const`); file-backed `mmap`; `link(2)`/`mknod(2)` (FAT cannot store
them); **shebang (`#!`) parsing is not implemented in the kernel**
(the image half of `/bin/sh` exists; see "Shebang" below and
open-issues item 14); `chmod`/`chown` (90-93) accept and drop, FAT
has no mode or owner; and job control -- SIGSTOP/SIGTSTP
default-terminate rather than stop.  SSE state is not saved
(open-issues item 10).  No privilege model (item 9).

**Shebang (`#!`) is not parsed by the kernel.**  `sys_execve`
opens the file, checks for ELF magic, finds `#!` (or any non-ELF),
prints `sys_execve: not an ELF file`, and returns `-ENOEXEC`.
**`./script.sh` works only because busybox ash catches `ENOEXEC`
and runs `sh script.sh`** -- a shell fallback, not a kernel
feature.  A script with `#!/bin/sh` at the top, invoked directly
(`./script.sh`) from a non-shell caller, fails; a script whose
interpreter is not a shell (`#!/usr/bin/python`, `#!/usr/bin/awk`)
has no fallback and fails outright.  **The kernel never reads the
`#!` line.**  The image half of `/bin/sh` exists (busybox soft
link, after the one-time `--install -s /bin`); the **kernel half --
`#!` parsing in `sys_execve` with a `[interp, optional_arg,
script, ...]` argv and a depth guard -- is not done** (open-issues
item 14).

**THE RELEASE TAGS.**  `v0.6.16` (the VFS), `v0.6.17` (FAT
`st_ino`, `tar`, `chmod`/`chown`), and `v0.6.18` (signals slices
1-5) are the release record; their annotations carry the
per-release summary.  `v0.6.18`'s scratch tags have been dropped.

**WHERE WE ARE.**  `v0.6.15` (busybox batch 1), `v0.6.16` (the
VFS), `v0.6.17` (FAT `st_ino`, `tar`, `chmod`/`chown`), and
`v0.6.18` (signals slices 1-5) are released.  Work is now
pre-`v0.6.19`.  The working tree is `dev`, clean, ahead of
`origin/dev` by 24 commits, nothing pushed.

**NEXT.**  Signals are done for the scope they were given, **and
`nanosleep` is now interruptible**.  The remaining signal work is
**job control** (open-issues item 13), and it has three parts, all
of which are needed:

(a) **the kernel side** -- SIGSTOP/SIGTSTP stop, SIGCONT resume, a
    `PROC_STATE_STOPPED` state, process groups, controlling
    terminal, `tcsetpgrp`;
(b) **the busybox side** -- `CONFIG_ASH_JOB_CONTROL=y`; without it
    ash does not put foreground jobs in their own process group
    and does not issue `tcsetpgrp`, so `Ctrl-Z` does not work even
    when the kernel is correct;
(c) **the tty side** -- `console_read` / `keyboard` must map
    `Ctrl-Z` to SIGTSTP (and the other control chars to their
    signals) and must track the foreground process group.

**The three prerequisites job control needed all landed this
session:** a blocked process parks and wakes correctly
(`YIELD-REFUSE`-on-`BLOCKED` fix + idle dispatch), a signal
interrupts a blocking syscall (`nanosleep` and the slice-6
syscalls return `-EINTR`), and dispositions cross fork and reset
on exec.  Job control opens from a correct substrate.

After job control, in order: `console_read` interruption (it
blocks with `BLOCK_KIND_NONE`; needs the `sig_pending` check the
pipe read/write paths have); **shebang + `/bin/sh` kernel half**
(item 14 -- see "Shebang" above and the `NEXT` item below); SSE
save/restore then remove the userland `-mno-sse*` flags (item 10);
the privilege model (item 9); busybox batch 2.  `test.sh`'s md5 is
still listed in the old work list; it is not blocking anything.

**Two open observations from this session, neither a live bug:**

- **KVM was slower than TCG on the author's host** during this
  session's gate.  That is backwards from the usual; a one-line
  check next session (`-cpu host` vs `-cpu max`, nested-virt) is
  worth it.  Not chased.
- **PIPE7E soak bound:** the last run reached `iter 5000`+ and was
  clean there.  It was **not** confirmed through the full 250000.
  Treat the session's pipe evidence as "clean past 5000", not "a
  completed soak."  A full soak is still the standard for a change
  that touches the timer (the `nanosleep` wake loop did).

### Signals: what is in, and what was proven

Delivered at three points: syscall exit (after the switch, with
the interrupted syscall's result passed to `signal_deliver_frame`
and preserved in `ucontext.gregs[RAX]`), the tick's `iretq`
(`signal_deliver_frame_iretq`), and the fault frame
(`signal_deliver_frame_exc`).  Each refuses a kernel-mode frame
(`(cs & 3) != 3`); the tick path additionally refuses to dispatch
mid-syscall.  `sys_rt_sigreturn` returns `v[G_RAX]` -- the
interrupted syscall's result -- so a `wait4` that a signal was
delivered over still reports the child pid.

Raises: SIGCHLD in `process_exit` (bit-set only, after the wake
and ZOMBIE, inside the existing `cli`); SIGPIPE in `sys_write`'s
pipe branch (`readers_open == 0`); SIGKILL acted on at the raise
(it cannot be caught or blocked); everything else via
`kill`/`tgkill`/`tkill`.

Dispositions: `signal_handler[]` stores 0 (`SIG_DFL`), 1
(`SIG_IGN`), or a handler address.  The three deliver helpers
branch: `0` -> `signal_default_action`; `1` -> clear the bit,
resume without a handler; otherwise build the user-stack frame
and enter the handler.  `signal_default_action` is ignore for
SIGCHLD, SIGURG, SIGWINCH, SIGCONT and terminate for everything
else.

**Fork and exec, the POSIX pair (landed 2026-10-29):**
- **fork** copies `signal_handler[]` and `signal_restorer[]`,
  copies `sig_blocked`, clears `sig_pending` (`c7a7962`).
- **exec** resets caught dispositions to `SIG_DFL`, preserves
  `SIG_IGN`, preserves the blocked mask, clears pending and
  `in_signal_handler` (`14c50a0`, in `sys_execve` step 8).
- **Landing fork without exec was a regression:** a
  forked-then-exec'd busybox child carried the pre-exec handler
  address (`0x41E1A1`) into a new image that did not map it;
  SIGCHLD delivery jumped there and faulted (`CR2 == RIP ==
  0x41E1A1`, PTE not present).  `canary` and the probe's parent
  hit it.  The pair is one change; either half alone is a bug.

**`nanosleep` is now interruptible (landed 2026-10-29):** it was
a `sti; hlt` spin that held the CPU; the timer does not preempt a
kernel-mode `hlt` to run a peer, so a parent could not run `kill`
until the sleeper exited (trace: `kill` num 62 landed after the
child's `exit_group` num 231; SIGUSR1 raised on a zombie, never
delivered).  Now: `BLOCK_KIND_NANOSLEEP` (5), `pcb_t.wake_tick`
(absolute `g_ticks` deadline, **appended at the END of `pcb_t`**
next to the signal fields -- placing it after `block_kind` shifted
`user_rsp` to `0x618` and the `_Static_assert` caught it),
`sys_nanosleep` parks `BLOCKED` with `wake_tick`, `process_yield`,
and returns `-EINTR` on a deliverable signal (before and after
sleeping); `timer_preempt_handler` wakes
`BLOCK_KIND_NANOSLEEP` PCBs with `wake_tick <= g_ticks` in the
IRQ's existing `cli`.  Acceptance test: `NANOSLEEP_INTR_PROBE` ->
`child: nanosleep rc=-1 errno=4 got=1` / `PASS`.

**`process_yield` dispatches idle (landed 2026-10-29):** the
`!next` fallback selects idle (pid 1), which is `RUNNING` by
design, so guard 2 refused it and printed `YIELD-REFUSE` on every
`sleep` once `nanosleep` started yielding.  Guard 2 now exempts
idle (`next->pid != 1`); a non-idle `RUNNING` target is still
refused.  `sleep` is silent; the gate is clean x2.

**Verified by running, 2026-10-29 (evening):**

- `NANOSLEEP_INTR_PROBE`: `child: nanosleep rc=-1 errno=4 got=1`,
  `WEXITSTATUS=0`, `PASS (child caught SIGUSR1; nanosleep
  returned -EINTR)`.
- `echo hi | wc` -> `1 1 3`.
- `SIGCHLD_PROBE`: handler ran, `wait4` returned the child pid,
  reaped.
- `sleep 1` returns, **no `YIELD-REFUSE` line**.
- `canary --full`: 28/28, no `#PF`.
- `test.sh`: 57/57.
- Gate x2 interleaved after the scheduler changes; PIPE7E clean
  past 5000 (not a completed soak -- see the open observation).

**Known limits carried forward:**
- SIGCHLD interrupts a blocked `wait4`; `sys_read` on a pipe and
  `sys_write` on a full pipe also interrupt.
- **`sys_nanosleep` is interruptible now**, but it returns
  `-EINTR` only when a *deliverable* signal arrives; an ignored
  or blocked signal does not interrupt it.  `rem` is still not
  written back.
- No `sigwait` (the synchronous wait-for-a-specific-signal
  primitive); `pause` and `sigsuspend` are implemented.
- SIGSTOP/SIGTSTP default-terminate (STOP is not implemented);
  SIGCONT's default is ignore, which is correct, but there is no
  stopped state to continue.  **Job control is the next item.**
- `signal_deliver_frame*` requires a nonzero restorer (musl
  always sets `SA_RESTORER`).
- No SIGBUS generator.
- `siginfo` is passed as NULL; `sigaction` masks and `SA_*` flags
  beyond `SA_RESTORER` are not honored.
- `signal_deliver_frame` and `sys_rt_sigreturn` both compute the
  frame base as `g_syscall_stack_top - 120`; the stub pushes 120
  bytes, the 128 is the `call`'s return address.  See
  `docs/open-issues.md` "The `g_syscall_stack_top - 120` frame
  base is derived and correct."  A change to the stub's pushes or
  the delivery point must re-derive it.

**What slice 2 taught the slice-3 author, so it is not re-walked:**

- **Do NOT call `signal_deliver_frame` from the asm stub.**  The
  deliver check runs at the top of `syscall_dispatch`, inside the
  C frame every syscall already returns through.
- **The handler entry `RSP` is `frame`, not `frame + 8`.**
- **`killtest` cannot close a slice's delivery acceptance.**  Use
  `kill_self` (delivery-and-restore), `sigspin` (tick path),
  `sigsegv_probe` (fault path), `sigchld_probe` (SIGCHLD),
  `sigterm_ignore` (SIG_IGN), `nanosleep_intr_probe` (blocking
  syscall interrupted; asserts `got=1` and `-EINTR`).

**Probe discipline (learned 2026-10-29 the hard way):**
`nanosleep_intr_probe` failed four different ways before it passed,
and three of the four were probe bugs, not kernel bugs:

- a fixed `usleep(300ms)` in the parent raced the child out of
  musl's `__post_fork`, so the handler was not yet installed and
  SIGUSR1 default-terminated it.  **Use a pipe handshake, not a
  sleep.**
- a 1s nanosleep raced the parent's read-then-kill, so the signal
  arrived after the child had exited.  **Sleep long enough that
  the kill lands inside it (3s).**
- the pass line said "child caught SIGUSR1" while nothing
  asserted it; a `got=0` run printed PASS.  **A pass line must be
  backed by the code that proves it** -- the child `_exit(10)`s
  when `got != 1`, and the parent's `WEXITSTATUS` check turns
  that into FAIL.

The `DBG-SYSCALL` per-syscall trace is what ended the guessing: it
showed the child ran only `218/14/14` (`__post_fork`) and no
`open`, no `sigaction`, no `nanosleep` -- and later showed the
`kill` landing after the child's `exit_group`.  **A probe that
fails is signal, but the failure's location must be observed, not
deduced.**

**DONE SINCE `v0.6.17`** (restore points: `20261021-slice5-done`,
`20261023-tkill`, `20261023-sigign`, the `20261029-slice6-*`
tags, `20261029-nanosleep-blocks-verified`,
`20261029-idle-dispatch-fixed`, on `dev`, no version bump):

*Batch 1 -- `ps -l` / sysinfo* (`20261020-psl-sysinfo`): `ps -l`
works; the kernel gained `sys_sysinfo` (99).  **99 is `sysinfo`,
not `gettimeofday`** (96; `time` is 201).  `ps -l`'s `STIME` was a
separate defect: `proc_build_stat` emits `p->creation_time`, now
recorded in `process_initialize_pcb`.

*Batch 2 -- kernel shell echo* (`b06787d`): the kernel shell's
echo and command output go to the serial terminal.

*Batch 3 -- signals slices 1-5:* (slice 1 `588ae53`/`3f60a19`/
`ce69a7b`; slice 2 `866588f` design, `d2126d8` code; slice 3
`17ffeb9`/`a1c90ff`/`c4b3ba7`; slice 4 `1d256ed`/`f90d517`;
slice 5 `7245390`; SIGCHLD half `593b349`/`dfd5240`/`42302e7`;
`sigchld_probe` source brought in `7994fce`; `tkill` `b976b40`;
`CONFIG_KILL` `33c4384`; `SIG_IGN` `6de2d63`.)

*Session 2026-10-29 (evening) -- the "next" work, landed:*
- `5854566` scheduler: a `BLOCKED` caller stays `BLOCKED` on a
  refused yield (the `YIELD-REFUSE` clobber; contract rule 2).
- `c7a7962` fork: the child inherits the parent's signal
  dispositions.
- `14c50a0` exec: reset signal dispositions (the missing half;
  the `0x41E1A1` `#PF` fix).
- `1226dee`, `6d3a0b4` debug: gate the signal-delivery
  instrumentation off (twice).
- `af0f9f6` probe: `nanosleep_intr_probe` asserts `got=1`.
- `4939ca8` nanosleep: block instead of spinning; interruptible
  by a caught signal.
- `7f43ac4` scheduler: dispatch idle from a yield instead of
  refusing it.

The slice-6 scratch tags (`20261029-slice6-*`) and the two new
restore tags are the source for the `v0.6.19` summary.  Nothing
is pushed: the policy is not to push until `v0.6.19` is ready and
verified.

**Working style -- how this project gets changed**

Read this before proposing any command block.

**One change at a time.** A "change" is a commit, verified before
the next one starts.  **Verify by RUNNING, not by committing** --
build, run, see the behavior, THEN commit.

**The state check comes before the command block.** Every command
block starts with `git status -sb` (or `git log`).

**A command block is one paste -- write it to a file first.**
`git add`, `git status`, `git commit`, and `git tag` belong in
**one** block, with the message inline via heredoc.  **Do not
paste a block with `cd ... || exit 1` into an interactive
shell** -- the `exit` closes the terminal.  Write to
`/tmp/<name>.sh` with `cat > ... <<'SCRIPT_EOF'`, then
`bash /tmp/<name>.sh`.

**Python edits with `count != 1` abort checks are the preferred
edit style.**  **`set -e` at the top.**  **Idempotent edit
scripts.**  **Do the count check IN PYTHON (`str.count`), not
with `grep`.**  `grep -Fc` does not do multi-line matches;
`grep -Fxc` requires a whole-line match.  `str.count` is the only
honest multi-line count.

**BEFORE ASSERTING A COUNT, count the occurrences in the file and
write the expected number from that count, not from memory.**

**ANCHOR ON STRUCTURE UNIQUE TO THE TARGET FUNCTION.**  Grep the
file, confirm the count is 1 **and** the match is in the intended
function.  **A bare line like `process_wake_all_blocked();` can
appear in two functions** (`timer_preempt_handler` and
`irq1_handler`); anchor on the surrounding lines that make it
unique (this session's second anchor miss).  **`next->state ==
PROC_STATE_RUNNING` appears three times** (`process_exit`,
`scheduler_switch_to`, `process_yield`); the guard that prints
`YIELD-REFUSE` is `process_yield`'s (this session's third anchor
miss).

**A post-write check string must be copied from the `new`
literal, not retyped.**  **And an anchor must be read from the
file's current bytes (`cat -A`), not from memory of what was
written a few turns ago** -- the probe's `_exit` block gained a
comment between the write and the next edit, and the anchor that
omitted it counted 0.

**Ask for source you do not have.**  Before patching a file it
has not seen **in this session**, ask for it.  **Before writing
an anchor, read the file with `cat -A` and quote the bytes.**

**A `_Static_assert` failing the build is the guard working.**
`pcb_t` layout is pinned by `process.c`'s `_Static_assert` block
(offsets through `user_rsp` at `0x610`).  New fields go **after
the last asserted field**, appended at the end of the struct next
to `sig_pending`/`sig_blocked`/`signal_restorer[]`.  A field
inserted before `user_rsp` moves it and the build fails -- which
is what happened to `wake_tick` and is the intended safety net.

**The build is TWO steps.**  `make -C 04_kernel_64bit
FAT_CONFIG=single` builds the kernel; `make -C 05_boot_kernel64
hdd-single.img` builds the image.  Running only the second copies
a STALE `kernel.bin`.  Always run both, and verify `kernel.elf`
is newer than the source you edited.

**`nm` output moves between builds.**  Re-run `nm` before using
any address.

**A diagnostic on the SUCCESS path is noise.  A diagnostic on
the FAILURE path is signal.**  `SW2-*`, `HEAP_TRACE_MERGE`, the
`HEAP: corrupt` prints, and `heap_validate`'s check prints are
success-or-transient-path; they are gated.  The refuse lines
(`SWITCH-DEAD`, `YIELD-REFUSE`, `SWITCH-REFUSE`, `EXIT-REFUSE`)
and the `C` report are failure-path (kept).  **Corollary learned
this session:** if a failure-path line becomes routine (the
`YIELD-REFUSE` on every `sleep`), that is a scheduler decision
defect to fix, not noise to filter -- the line printed because
`process_yield` refused a dispatch it should have made.

**GDB over `-s` has no types** (`kernel.elf` is built WITHOUT
`-g`).  Use raw offsets: `pid` 0x000, `name` 0x008, `state`
0x028, `cr3` 0x030, `entry_point` 0x038,
`kernel_stack_virt` 0x048, `kernel_stack_top` 0x050, `rsp`
0x110, `rip` 0x118, `next` 0x120, `prev` 0x128, `block_kind`
0x158.  `user_rsp` is 0x610.  **CONSIDER BUILDING WITH `-g`.**

**Do not run `git reset --hard` without stashing first.  Do not
edit `third_party/`.  Do not commit `kmain.c`'s `pit_init` knob
-- the committed value is 100.  Do not call a race closed on one
clean run.**

---

## Releases

**Before a release:** the scratch `2026MMDD-*` tags are annotated
and are the source for the version summary.  **Write the summary
from them BEFORE dropping them.**  The release tag and the merge
commit are local objects; once they exist, the scratch tags may
be dropped.

**The release procedure** (on `dev`, pre-release):

1. **Patch the docs first.**  Update this file's release
   paragraph and (optionally) `docs/version-history.md`.  Commit.
2. **Bump the version string in `kmain.c` manually** (the
   banner).  Commit.
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

**Current release state.**  **`v0.6.18` is released.**  Work is
now pre-**`v0.6.19`**.  The release tags `v0.6.15` through
`v0.6.18` carry their summaries; the scratch tags they were drawn
from have been dropped.

---

## What donix is

A fork of dons-os.  Goal: a Unix-like OS that runs static
musl-linked binaries on the Linux x86_64 ABI, natively.  Newlib
is gone.  Userland is a tracked source tree at `userland/musl/`.
Phase B (busybox) is well underway: the shell is interactive,
pipelines and redirection work, and the applet set now includes
`bc`, `dc`, `diff`, `du`, `awk` in addition to the file/text
applets.

Full strategy: `docs/strategy.md`.  Direction: `ROADMAP.md`.
Concurrency rules: `docs/contract.md`.

---

## Where we are

### Current baseline

- `test.sh` 57/57; `canary --full` 28/28; `NANOSLEEP_INTR_PROBE`
  PASS.  Verified 2026-10-29 on a freshly built image, twice
  interleaved, on the KVM gate (which was, unusually, slower than
  TCG on the author's host -- see the open observation).
- PIPE7E clean past 5000 iterations on the 2026-10-29 build; the
  250000 soak was **not** completed this session.
- Busybox batch 1 and five applets enabled and verified.
- Signals slices 1-5 complete and verified; `tkill` (200) in;
  `CONFIG_KILL=y`; `SIG_IGN` fixed.
- Signals slice 6 complete: `wait4`, pipe `read`, pipe `write`,
  `pause`, `sigsuspend` return `-EINTR`.
- **`nanosleep` blocks and is interruptible** (`4939ca8`); the
  old "cannot be interrupted" limitation is gone.
- **Fork inherits dispositions; exec resets them** (`c7a7962` +
  `14c50a0`), the POSIX pair.
- **`process_yield` dispatches idle** (`7f43ac4`); `sleep` is
  silent.
- `docs/contract.md` is the durable concurrency rule; read it
  before any shared-state change.

### Next -- the highest-value work

1. **Job control** (open-issues item 13): SIGSTOP/SIGTSTP stop,
   SIGCONT resume, `PROC_STATE_STOPPED`, process groups,
   controlling terminal, `tcsetpgrp`.  Three parts, all needed:
   - the kernel state machine (`PROC_STATE_STOPPED`, stop/resume;
     `signal_default_action` currently maps SIGSTOP/SIGTSTP to
     TERMINATE -- that is the placeholder to replace),
   - `CONFIG_ASH_JOB_CONTROL=y` in the busybox build,
   - the tty side: `console_read` / `keyboard` maps `Ctrl-Z` to
     SIGTSTP (and the other control chars to their signals) and
     tracks the foreground process group.

   The three prerequisites landed this session (park/wake, signal
   interrupting a blocking syscall, dispositions across fork and
   exec).  A `PROC_STATE_STOPPED` process is a third non-READY
   state; the `process_yield` refuse guards and the timer wake
   path must treat it as "not dispatchable, not woken by the
   deadline loop," which is the same discipline as `BLOCKED`.
2. **`console_read` interruption.**  It blocks with
   `BLOCK_KIND_NONE`; it needs the same `sig_pending` check as
   pipe read/write.
3. **Shebang + `/bin/sh` kernel half** (open-issues item 14).
   **What exists:** a `/bin/sh` file exists on the image
   (busybox soft link); a shell that catches `ENOEXEC` can run a
   script by hand.
   **What does not:** `sys_execve` reads the ELF header and, on
   no ELF magic, prints `sys_execve: not an ELF file` and returns
   `-ENOEXEC`.  It does not read the first line, does not
   recognize `#!`, does not build
   `argv[interp, optional_arg, script, ...]`, and has no
   recursion depth guard.
   **Why a script with `#!/bin/sh` fails today:** the kernel
   never looks at the `#!` line.  The shell fallback is what
   makes `./script.sh` appear to work -- and only when the caller
   is a shell.  A direct `execve("./script.sh")` from a non-shell
   process, or a script whose interpreter is not a shell, fails.
   **What the kernel half must do:** in `sys_execve`, after the
   ELF magic check fails, read the first two bytes; if `#!`,
   parse the interpreter path (and one optional argument),
   rewrite argv as `[interp, opt_arg, script_path, original
   argv[1..]]`, and `sys_execve` the interpreter -- with a depth
   guard (e.g. 4) so a shebang pointing at itself cannot loop.
   **Image half prerequisite:** `/bin/sh` must exist and be
   executable; the current busybox `--install -s /bin` step
   creates it, but only as a manual post-boot step (see "State
   on disk").
4. **SSE** (item 10): save/restore XMM on context switch and the
   interrupt path, then remove the userland `-mno-sse*` flags.
5. **Privilege model** (item 9).
6. busybox batch 2; `test.sh`'s md5.
7. **A completed PIPE7E soak** at 100 Hz on the post-nanosleep
   build -- the timer wake loop touched every tick, and "clean
   past 5000" is not the soak standard.

### Refuted -- do NOT re-walk

- **The reserve / frame placement.**  `67701df` + `5587d70`.
- **The `next->rsp = 0` clear.**  Broke blocking; reverted.
- **The pipe-waiter fields as the `C`'s stale holder.**
- **The `current_process` dual-copy divergence at the `C`.**
- **A switch-to-self guard in `scheduler_switch_to`'s TAIL.**
- **A caller heap overflow.**
- **`YIELD-REFUSE` on `sleep` as a real refusal.**  It was
  `process_yield` selecting idle (RUNNING by design) in its
  `!next` fallback; idle is now exempt from guard 2 (`7f43ac4`).
- **"The `nanosleep` limitation is permanent."**  It was a
  `sti; hlt` spin; it blocks now (`4939ca8`).
- **"`./script.sh` works, so shebang works."**  It does not.
  busybox ash catches `ENOEXEC` and runs `sh script.sh`.  The
  kernel never reads the `#!` line.  This is why a script
  `#!/usr/bin/python` has no fallback and fails, and why the
  `sys_execve: not an ELF file` print appears for a `.sh`
  invocation from a non-shell caller.  The bug is **in the
  kernel**, and the fix is item 14.
- **Probe hypotheses for the `got=0` failure** (stale
  `current_process`, fork disposition, `sigaction`
  mis-targeting, an iretq-vs-pop `rax` mismatch).  All four were
  wrong; the cause was the probe's fixed `usleep` racing the
  child out of `__post_fork`, then a 1s sleep racing the kill.
- Do not re-open: item 11, the session-48 zone scan,
  the `fs_base` path, the `longjmp`/`setjmp` family.

---

## Open issues (top 6)

1. **Job control is the remaining signal gap.**
   SIGSTOP/SIGTSTP default-terminate rather than stop.  Delivery,
   dispositions (including `SIG_IGN`), the default-action table,
   SIGPIPE, SIGCHLD-to-`wait4`, `tkill`, slice 6, `nanosleep`
   interruption, fork/exec dispositions are all done and
   verified.
2. **`open-issues.md` item 10: `CR4.OSFXSR` without an XMM save.**
3. **`open-issues.md` item 13: `CONFIG_ASH_JOB_CONTROL` gap.**
4. **`process_dump_all`'s `DETACHED` state** -- may mask a stranded
   process bug.
5. **Item 7a/7b** -- the boot-time `#PF` and the busybox `#GP`.
6. **`gettimeofday` (96) / `time` (201) not implemented** -- no
   caller on the image today; `date`/`time` need them if enabled.

Plus: **the VFS** (see Next), **shebang + `/bin/sh`** (item 14),
and the open-issues list.

---

## State on disk

Kernel sources: `04_kernel_64bit/`.  `capture.txt` is gitignored.

**After a fresh image, one manual step:** the image stages
`/bin/busybox` alone.  To get the applet symlinks (including
`/bin/sh`, which shebang will need), boot and run
`/bin/busybox --install -s /bin`.  The absolute path is required:
busybox reads `/proc/self/exe` to find itself, donix has no
`/proc/self/exe`, and it falls back to `argv[0]` -- which must
start with `/`.  This is a manual step, not part of the build;
automating it is a Makefile/boot change and is not done.

**The `/bin/sh` symlink is a soft link to busybox, and without
`--install -s /bin` it does not exist.**  If `./script.sh` with
`#!/bin/sh` fails, check `ls -l /bin/sh` first -- that is the
common cause; the kernel's shebang parsing (item 14) is the
second.

`v0.6.15`, `v0.6.16`, `v0.6.17`, and `v0.6.18` are the release
record.  The slice-6 tags and the two 2026-10-29 restore tags
(`20261029-nanosleep-blocks-verified`,
`20261029-idle-dispatch-fixed`) are the current restore points;
nothing is pushed until `v0.6.19` is complete and verified.  The
next release procedure draws the release summary from these tags
before dropping them.

---

## One-line summary

**`v0.6.18` is the last release: signals slices 1-5, `tkill`,
busybox `KILL`, `SIG_IGN` fixed.**  Since then: slice 6
(`wait4`/pipe read/write/`pause`/`sigsuspend` interrupt on a
pending signal), **`nanosleep` blocks and is interruptible** (the
old limitation gone), **fork inherits dispositions and exec resets
them** (the POSIX pair, after a regression and its completion),
**a `BLOCKED` caller stays `BLOCKED` on a refused yield**, and
**`process_yield` dispatches idle** so `sleep` is silent.
**Shebang (`#!`) is still not parsed in `sys_execve`** -- `./script.sh`
works only via busybox ash's `ENOEXEC` fallback; a non-shell
interpreter has no fallback (open-issues item 14).  Gate
57/57 + 28/28 + probe PASS, twice.  **Next is job control**
(kernel stop/resume + `PROC_STATE_STOPPED` + process groups +
`CONFIG_ASH_JOB_CONTROL=y` + tty control chars), then
`console_read` interruption, shebang+/bin/sh, SSE, privilege.
Read `docs/contract.md` and "NEXT" in this file before any code.
