# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-30 (session close, **third** session
this date -- the one that diagnosed the backgrounded-applet bug
and stopped).  **`v0.6.18` is released; work is pre-`v0.6.19`.**
This session diagnosed **why `sleep N &` fails** (FatFs shared
window, no serialization), confirmed a **working interactive
workaround** (`Ctrl-Z` then `bg`), and **refuted a fix shape**
(`FF_FS_REENTRANT` + a `cli`-depth mutex) by crashing the kernel.
No code landed.  Baseline on `HEAD`: `canary --full` 28/28,
`test.sh` 57/57, job-control, shebang, and resume transcripts
green, `HEAD` builds alone.  `dev` is ahead of `origin/dev` by
**48 commits**, nothing pushed.

**Read "The concurrency contract" in `docs/contract.md` before
touching shared state, then "Where we are".**  The ash boot hang
that blocked the tty side is in `docs/2026-10-30-ash-boot-loop.md`;
this file carries only conclusions.

**THE TEN COMMITS FROM THE SECOND SESSION** (on `dev`, tagged,
nothing pushed): six job-control, two shebang, `run`, and the
resume fix.

- `7f83b52` `tty: console_read blocks via process_yield and returns
  -EINTR` (tag `20261030-console-read-eintr`) -- `console_read` was
  the last blocking path still using `sti; hlt` with no
  `sig_pending` check.  It now blocks via `process_yield` (real
  resume frame) and returns `-EINTR` on a caught signal, mirroring
  the pipe read.  Pid 1 keeps its bare `sti; hlt` loop.
- `9b1a4ed` `tty: Ctrl-Z raises SIGTSTP to the console foreground
  pgrp` (tag `20261030-ctrlz-sigtstp`) -- the PS/2 path.
  `sys_kill` grows the `pid < 0` group branch (every process with
  `pgrp == -pid`, via a shared `kill_one`); `console_fg_pgrp_get()`
  exports the static `g_console_fg_pgrp`; `irq1_handler` maps
  **scancode 0x2C (Z)** with Ctrl to `sys_kill(-fg, SIGTSTP)`.
- `16465c8` `busybox: enable ash job control
  (CONFIG_ASH_JOB_CONTROL=y)` (tag `20261030-busybox-jobctl`) --
  the config flip, verified by a rebuild that relinked
  `busybox.elf`.
- `7ec4a10` `tty: Ctrl-Z on the serial console raises SIGTSTP` --
  the UART half.  `kbd_buffer_get_mux` raises on byte `0x1A` from
  the serial input and eats the byte.
- `4adc419` `tty: a default-stop signal parks the blocked reader;
  stop holds` (tag `20261030-console-read-default-stop`) --
  `console_read` returns `-EINTR` only for **caught** signals; a
  default-action SIGTSTP/SIGSTOP is delivered at the blocking
  syscall's safe point, and `process_stop_and_yield` loops while
  still STOPPED so a spurious dispatch cannot resume it.
- `b48ff9e` `jobctl: wait4 reports a stopped child, and a stop
  wakes its parent` (tag `20261030-wait4-wuntraced`) --
  `WUNTRACED` in `sys_wait4` with the Linux stopped encoding
  `(sig<<8)|0x7f`; `pcb_t.stop_signal`/`stop_reported` appended
  after `in_stop_safe_point`; `process_stop` takes the signal and
  wakes a `wait4`-blocked parent; `process_continue` clears both.
- `0d93f8c` `exec: parse a shebang line, re-exec the interpreter,
  stage /bin/sh` (tag `20261030-shebang-kernel`) -- `sys_execve`
  sniffs `#!` before the ELF-magic check, parses
  `[interp, optarg?, script, argv[1..]]` and recurses, with a
  `pcb_t.shebang_depth` guard that refuses at depth >= 1 with
  `-ENOEXEC` (`ELOOP` is not defined in this tree).  Kernel half
  and image half land together: without `/bin/sh` on the image the
  kernel half is a regression (observed: `f_open(/bin/sh) -> 4`,
  then ash's `not found`).  `::/bin/sh` is a second full **copy**
  of busybox on both `fat.img` and `hdd-single.img` -- FAT has no
  symlink store, so it is a copy, not a link.
- `cf0619a` `run: disable host-side Ctrl-Z so it reaches the guest`
  -- `stty susp undef` at the top of `run`.  **See the correction
  below: the commit message claims a host-terminal test this
  session did not demonstrate.**  The terminal setting is still
  necessary and still correct; the headless byte path is unproven.
- `62ee388` `exec: pass the caller's path, not exec_path, to a
  shebang interpreter` (tag `20261030-shebang-path`) -- fixes the
  cwd-doubling bug in `0d93f8c`.  The script argument is the
  caller's raw `path`, not section 1's `exec_path`.
- `4f2b7a3` `jobctl: a stopped nanosleep resumes for its remaining
  time` (tag `20261030-nanosleep-resume`) -- `sys_nanosleep`'s
  post-yield `return 0` was wrong for a stop/continue resume; it
  now loops while `g_ticks < deadline`, the shape `console_read`
  already had.  Local to `sys_nanosleep`.

**CORRECTIONS FROM THE SECOND SESSION TO PRIOR PROSE** (the code
and commit messages are right; older notes are not):

- **Ctrl-Z is not scancode `0x1A`.**  `0x1A` is the `[` key and the
  ASCII control code.  The Z key is scancode `0x2C` (PS/2) and the
  byte `0x1A` on the UART.
- **`process_wake_all_blocked` does not filter on `block_kind`.**
  It guards pid 1, ZOMBIE/TERMINATED, and double-add only.  The
  stale comment in `interrupts.c` saying otherwise is still in the
  tree and should be fixed in a later comment-only commit.
- **The busybox relink needs an mtime nudge.**  The musl Makefile
  rule `build/.busybox.stamp: third_party/busybox/.config` fires
  only if `.config` is strictly newer than the stamp.  A config
  flip and the stamp written in the same second silently skip the
  relink.  `touch third_party/busybox/.config` and remove
  `userland/musl/build/.busybox.stamp` before the two-step build.
- **`exec_path` is the wrong argument for a shebang interpreter.**
  `exec_path` is already resolved against the caller's cwd and
  dot-prefix-stripped for FatFs, so it is cwd-RELATIVE; the
  interpreter inherits that cwd and resolves it again, doubling the
  prefix.  Observed from `/root`: `./don.sh` -> `exec_path`
  `root/don.sh` -> interpreter resolves to `root/root/don.sh` ->
  `can't open 'root/don.sh'`.  The raw caller `path` is correct.
  `0d93f8c` got this backwards; `62ee388` fixes it.  The early
  `#!/tmp/s.sh` tests passed only because they ran from `/`, where
  the prefix does not double.
- **A blocking syscall must not assume *why* its yield returned.**
  `sys_nanosleep` returned 0 whenever no signal was pending --
  including when a SIGTSTP/SIGCONT cycle resumed it early.  The
  fix re-enters the wait while the deadline is in the future, the
  shape `console_read` uses.  Any future blocking syscall should
  re-check its own condition after `process_yield`, not infer the
  reason from the absence of a signal.
- **`cf0619a`'s message overstates what was verified.**  It says
  "with susp undef, Ctrl-Z in the host terminal stops a foreground
  `sleep 100`."  Every job-control transcript this session is the
  **framebuffer/PS/2** path.  In headless mode (`-display none`,
  serial on stdio) `stty susp undef` applied, `echo hello` reached
  the guest, and plain `Ctrl-Z` and `Ctrl-V Ctrl-Z` both did
  nothing.  The commit is not amended (tagged, local, nothing
  pushed); the correction lives here.
- **`stty susp undef` is session-level only.**  It applies to the
  tty of the terminal it ran in and dies with that terminal.  The
  `run` change has no restore: the terminal is left with
  `susp = <undef>` after QEMU exits.  The follow-up is a
  `stty -g` save / `stty "$old"` restore around the QEMU launch
  (not a bare `stty susp ^Z`, which assumes the original value),
  as its own commit.

**CORRECTIONS FROM THE THIRD SESSION** (this session's, to its own
earlier prose -- recorded so the next session does not re-derive
them):

- **The backgrounded-applet bug is NOT "ash skips the applet path."**
  ash *does* try the standalone resolver first: the first `execve`
  of `sleep N &` is `f_open(/bin/busybox)`, and it fails with
  `FR_NO_FILE`.  Only *then* does ash fall back to `execvp`'s PATH
  walk (bare name, then each `$PATH` entry), all `FR_NO_FILE`, then
  `sleep: not found`.  The bug is the **`f_open("/bin/busybox")`**,
  not the fallback.
- **The stripped-vs-absolute path is a red herring.**  `sys_execve`'s
  `f_open` is handed the stripped form (`bin/busybox`); `cat
  /bin/busybox` works because `fat_open` strips its own copy and
  opens the same form.  Both open the stripped form; the string is
  not the problem.  (`sys_execve`'s trace printed `path` -- the
  caller's raw arg -- while opening `exec_path`, the stripped one;
  that mismatch was a diagnostic, not a bug.)
- **The failure is state, not path and not concurrency in the
  syscall.**  `sleep N &` opens `bin/busybox` fine from a
  foreground/startup context; it fails only from a forked `&` child
  whose inherited `FIL`s are live on the shared `FATFS`.  The probe
  that **closed** fds 3..MAX before the open made it succeed; a
  probe that merely **hid** the child's slots from the table did
  not (the parent's live `FIL` is what matters).  So the raced
  structure is FatFs's **`fs->win`** -- one shared disk-access
  window per `FATFS`, written by `move_window`/`sync_window`, with
  **`FF_FS_LOCK=0`** and **no** lock.
- **`FF_FS_LOCK` is not the fix.**  It gates *duplicate open*; it
  does not protect `fs->win`.  Flipping it to 8 changed the PATH
  walk's `FRESULT`s (`FR_NO_FILE` -> `FR_NO_PATH`) but left the
  first `f_open(bin/busybox)` failing.  Reverted.
- **`FF_FS_REENTRANT 1` + a `cli`-depth mutex is the WRONG FIX
  SHAPE.  It corrupts the heap and #GPs the kernel on boot.**  The
  attempt: flip `FF_FS_REENTRANT 0 -> 1` in `ffconf.h`, add
  `ff_mutex_{create,delete,take,give}` to `diskio.c` implemented as
  `pushfq; popq saved; cli` / `pushq saved; popfq` with a depth
  counter.  Result: `HEAP-VALIDATE FAIL ... corrupt after
  coalesce`, a user #PF at `CR2=RIP=0x400000` (musl_sh scheduled
  with `rsp=0`, before its image was mapped), then a **kernel #GP
  at RIP `0xFFFFFFFF80105A49`, PID 1 `idle`**, heap block
  `0xFFFF900000007D30` with magic `0xDEADBEEFCAFEBABE`.  Cause:
  FatFs's reentrancy nesting does not pair `take`/`give` one-to-one
  per API entry (`mount_volume` takes, `validate` takes again,
  `LEAVE_FF` gives), and a **single global saved-RFLAGS** restored
  by the outermost `give` puts back flags captured in a different
  context -- flipping IF under a heap write and letting the timer
  in.  **Do not retry this shape.**  Reverted; tree at `9d6ec02`.

**WHAT THE SHELL IS.**  Unchanged: boot shell is `musl_sh` ->
fork/exec `/bin/busybox sh` -> ash.  The busybox is built with
`CONFIG_ASH_JOB_CONTROL=y`, so ash is a job-control shell.

**WHERE WE ARE.**  Job control on the **framebuffer/PS/2 console**
works end to end:

- Ctrl-Z raises SIGTSTP to the console foreground pgrp; the stop
  holds.
- `wait4(WUNTRACED)` reports it; ash prints `[1]+ Stopped` and
  returns a prompt; `jobs` lists it; `ps` shows the job `T`.
- `fg` resumes it.  Proven for a job blocked in **`console_read`**
  (`cat` resumes and reads input) and for a job blocked in
  **`nanosleep`** (`sleep 60` resumes and blocks for its remaining
  time before the prompt returns).
- **`bg` resumes a stopped job in the background -- CONFIRMED this
  session.**  `sleep 100` foreground (works), `Ctrl-Z` (`[1]+
  Stopped`), `bg` (`[1] sleep 100`), `jobs` (`[1]+ Done`) -- the
  job ran and exited.  `bg` is SIGCONT-to-a-stopped-job; it never
  forks a subshell and never `execve`s the standalone resolver, so
  it does not touch the broken `&` path.  **This is the interactive
  workaround for backgrounding** (see NOT DONE item 1).
- `NANOSLEEP_INTR_PROBE` and `SIGSTOP_PEER_PROBE` both pass.

The foreground pgrp is `g_console_fg_pgrp`, set by `TIOCSPGRP`
(lazily by the first `TIOCGPGRP`), read under `cli` via
`console_fg_pgrp_get()`.

**Headless serial `Ctrl-Z` is unverified on this host.**  With
`-display none` + `-serial stdio` and `susp = <undef>` confirmed
live, typed input reaches the guest (`echo hello` works) but
`Ctrl-Z` does not arrive, plain or `Ctrl-V`-quoted.  Candidates:
the terminal emulator eating the key before the pty, or QEMU's
`-serial stdio` terminal handling.  The one untried experiment is
`-serial mon:stdio` in place of `-serial stdio -s -monitor none`.
This is a **host-environment item, not a kernel defect** -- the
kernel handles both paths, and the serial byte `0x1A` is mapped.
`sendkey ctrl-z` is **not** a substitute: it injects a PS/2 event,
and the headless console is the UART.

**Shebang / `/bin/sh` is done (open-issues item 14 closed by
`0d93f8c` + `62ee388`).**  Verified by running: from `/root`,
`./don.sh` with `#!/bin/sh` prints its body, no `can't open` line;
from `/`, `#!/bin/sh` -> `root-ok`; `#!/tmp/s.sh` ->
`sys_execve: shebang depth` then ash's `ENOEXEC` fallback (guard
fired); `#!/nonexistent/interp` -> `f_open(/nonexistent/i) -> 5`,
body not run.  The optional interpreter argument reaching the
interpreter as a real argv is **not** proven: `-x` is
ash-ignorable and the earlier transcripts were the fallback path.
A test with an interpreter that can observe argv is the way to
prove it.

**NOT DONE, and what this session established about it:**

1. **Backgrounded applet (`sleep N &`) -- DIAGNOSED, NOT FIXED.**
   Reproduction, in one boot:
   ```
   ~ # cd /root
   ~ # sleep 100 &
   sys_execve: f_open(/bin/busybox) -> 5     <- the bug: this open fails
   sys_execve: f_open(sleep)        -> 4
   sys_execve: f_open(/sbin/sleep)  -> 5
   sys_execve: f_open(/usr/sbin/sleep) -> 5
   sys_execve: f_open(/bin/sleep)   -> 5
   sys_execve: f_open(/usr/bin/sleep) -> 5
   sh: sleep: not found
   ```
   Foreground `sleep N` works (NOEXEC, no `execve`).  `cat
   /bin/busybox &` works (NOEXEC).  `sleep N &` forked a subshell
   that `execve`s the standalone resolver; that `f_open` of
   `bin/busybox` returns `FR_NO_FILE`.  **Mechanism: one shared
   `FATFS` (`kmain.c:846`), `FF_FS_LOCK=0` forced by design,
   `FF_FS_REENTRANT=0`, and `f_open`'s directory scan writes
   `fs->win`/`fs->winsect` via `move_window`/`sync_window` with no
   serialization against the parent's live `FIL`.**  The one thing
   that worked: **close fds 3..MAX before the open** (the probe).
   The one thing that *did not*: hide the child's slots from the
   table (the parent's `FIL` is the live one).  **The fix must
   serialize the physical sector I/O -- `disk_read`/`disk_write`,
   the only writers of `fs->win` -- not the FatFs API entry, and
   not `FF_FS_REENTRANT`** (refuted above).  Do this next; see
   "NEXT" item 1.
2. `process_wake_parent_if_waiting` can miss a stop in the window
   between `sys_wait4`'s scan and its block-store.  The zombie path
   has a late re-scan for exactly this; the stopped path does not.
   In the end-to-end test it did not bite, but the window is real.
   The fix is the late re-scan also checking `STOPPED &&
   !stop_reported`.  Small commit, own soak.
3. `process_stop_and_yield`'s `while (STOPPED) process_yield()` loop
   has not been exercised with **no runnable peer**.  The
   scheduler's fall-to-idle makes it unreachable today; a soak that
   stops a process when nothing else is runnable would prove it.
4. A `SWITCH-REFUSE: next pid=8 name=busybox rsp outside stack ->
   idle` was observed **once**, after several `fg` cycles.  It did
   **not** reproduce on `cf0619a` across five clean cycles.  Not
   fixed; not closed.  Same standing shape as item 7a/7b: an
   intermittent fault that stops reproducing is not closed.  If it
   returns, `scheduler.c:1252` is the site and the
   `context_switch.asm` `0x110` consume is the mechanism.  **Do not
   patch `process_stop_and_yield`'s trailing yield speculatively** --
   that was proposed and then refuted by the clean repro.
5. `kill(pid <= 0)` group semantics: `kill(-pgid, sig)` works;
   `kill(0, sig)` ("my group") still returns `-EINVAL`.
6. Ctrl-C on the serial console is **not** mapped to SIGINT.
   (Host `intr = ^C` usually eats it before it reaches the guest.)

**CORRECTIONS FROM THE FOURTH SESSION** (diagnosis; no fix landed):

- **The `&` bug is `fs->win`/`fs->winsect` incoherence with
  `dp->sect`/`dp->dir`.** `dir_sdi` and `dir_next` set
  `dp->dir = fs->win + ofs` **before** any `move_window` loads
  `dp->sect` into `fs->win`.  `move_window` skips when
  `sect == winsect`, so `dp->dir` can point into a stale
  `fs->win`, and `dir_find`/`dir_read` scan the wrong data ->
  `FR_NO_FILE` with zero `disk_read`.  This is the mechanism
  behind: (a) the DRT trace -- zero `disk_read` across six
  `f_open`s in the `&` fork; (b) the WARM trace --
  `f_opendir("0:/")` returning 2/0 entries where the root has 9;
  (c) `cat /bin/busybox &` failing the same way as `sleep N &`
  (the `&` path is uniformly broken; foreground applet exec
  works).

- **Refuted shapes (do not retry, in addition to the prior
  sessions'):**
  (1) `cli`-depth lock (`irq_save` shape) around the ATA
      transfer in `disk_read`/`disk_write`.  Crashed on boot:
      `TIMER-REFUSE musl_sh rsp=0` -> user #PF `0x400047`
      `pte=0x3` phys 0 -> kernel #GP in `idle`.  Second time for
      this crash class (see the `FF_FS_REENTRANT` entry).
  (2) `f_sync` on inherited `FIL`s.  `f_sync` does not reset
      `winsect`.
  (3) `f_opendir("/tmp")` + `f_opendir("/")` warm-up.
      `f_opendir` goes through `move_window`; if `winsect` is
      stale at the directory sector, the warm-up skips too.
  (4) `user_syscall_clear_file_table(self)` alone.
      `put_file_slot`'s refcount-2 early return means a forked
      child's `put_file_slot` does NOT `f_close` the parent's
      `FIL`.
  (5) Removing `strip_dot_prefix(exec_path)`.  The strip IS a
      real latent bug (the comment claiming FatFs rejects a
      leading `/` is stale; `FF_FS_RPATH` is on and a leading `/`
      is correct), but it is NOT the `&` bug.  Record it for a
      separate one-line cleanup.
  (6) `f_mount(NULL)` + `f_mount(fs)` in `sys_execve`.  Hung
      the boot: `user_syscall_clear_file_table` closes fds 0/1/2
      (the console) and `sys_execve` does not repopulate them.
      The remount also cannot `f_close` the parent's `FIL`
      (refcount 2 -> `put_file_slot` early-returns), so it would
      not fix the stale window even if the console survived.
  (7) `f_read`/`f_write` `winsect = -1`.  Under `FF_FS_TINY=0`
      these functions do not touch `fs->win` (they use
      `fp->buf`), so invalidating `winsect` there does nothing.
  (8) `move_window` in `dir_sdi` and `dir_next` tails.  Broke
      the boot: raw ELF bytes on the serial port.  `dir_next` is
      called from `dir_register`/`dir_remove` where `dp->sect`
      may name a sector that has not been written;
      `move_window` there reads uninitialized disk contents into
      `fs->win` and corrupts the write.  Reverted.

- **The fix is in `ff.c`'s `dir_*` / `move_window` contract.**  The
  next session starts by reading `dir_sdi`, `dir_next`,
  `dir_read`, `dir_find`, `dir_register`, `dir_remove`,
  `create_name`, `follow_path`, and `get_fileinfo` **in full**
  before writing any change.  The two `ff.c` attempts this
  session were both written against a contract that was not
  fully read; both were refuted by the next boot.

- **`TRACE_EXECVE_ERR` is turned off** (`include/trace.h`,
  `#define TRACE_EXECVE_ERR 0`).  It is a baseline tree define,
  not a session artifact; flip it back on when execve is being
  traced.  `diskio.c` carries no `DRT` trace.

- **The current tree: `9d6ec02` plus this handoff edit.**  Nothing
  else landed.  The `20261030-*` tags are intact.  Nothing
  pushed.

**WHERE WE ARE, the rest** (unchanged from the prior handoff): no
real `mount(2)`; file-backed `mmap`; `link(2)`/`mknod(2)`;
`chmod`/`chown` accept and drop; SSE state is not saved (item 5);
no privilege model (item 6).

**NEXT.**

1. **Backgrounded applet (`sleep N &`) -- the fix, narrow and
   verified.**  Serialize the physical sector I/O, not the FatFs
   API.  First read `ata_read_sectors_drive` /
   `ata_write_sectors_drive` (are they already `cli`-safe? do they
   sleep? do they take `serial_lock`?) -- the lock's shape depends
   on that.  Then a lock taken in `disk_read`/`disk_write` (the
   only writers of `fs->win`) whose `give` restores **its own
   caller's** flags, or that does not touch IF at all.  **Not**
   `FF_FS_REENTRANT`, **not** a `cli`-depth mutex at the API
   boundary -- both refuted.  Verify by running: `sleep 100 &`,
   `sleep 300 &`, `cat /bin/busybox &`, `jobs`, `kill %1`, `kill
   %2`, **and** `canary --full` 28/28, `test.sh` 57/57, in one
   boot.  Do not close on one clean run.
2. **Headless serial `Ctrl-Z`** (host item): a ten-minute
   experiment, not a feature -- try `-serial mon:stdio` in place
   of `-serial stdio -s -monitor none`.  If it works, a `run`
   commit with a `stty -g` save / `stty "$old"` restore around the
   launch.  If it does not, record it as a host limitation and
   drop it.  It is worth clearing early because `cf0619a`'s
   commit message currently claims a verification that was not
   performed; settling it turns that into a real verification or
   a clean "host limitation, recorded".
3. **SSE** (item 5): save/restore XMM, then remove `-mno-sse*`.
4. **Privilege model** (item 6).
5. busybox batch 2; `test.sh`'s md5.
6. **A completed PIPE7E soak** at 100 Hz.
7. The follow-ups this session surfaced: the `wait4` stopped-child
   late re-scan; the no-peer stop soak; and the optional
   interpreter-argument proof.

### Working style -- how this project gets changed

Read this before proposing any command block.

**One change at a time.**  A "change" is a commit, verified before
the next one starts.  **Verify by RUNNING, not by committing.**
This session showed why: the stop-hold fix, the EINTR rule, the
`wait4` wake, both shebang commits, and the `nanosleep` resume were
each proven by a keystroke test, not by a clean build.  The one
speculative fix proposed this session (a trailing yield guard in
`process_stop_and_yield`) was **refuted** by re-running the repro
on the current tree -- the rule cuts both ways: do not fix what you
have not reproduced.  **The third session sharpened this further:
seven candidate fixes for the `&` bug were proposed, six were
refuted by the next run (strip, cwd, `&`-mechanism, applet-vs-file,
save/restore, `FF_FS_LOCK`), and the seventh (`FF_FS_REENTRANT`)
crashed the kernel.  A refuted fix is not progress; a crash is a
regression.  The diagnosis is the deliverable, and the fix shape
must be read out of `ff.c`/`diskio.c`/`ata.c` before any code.**

**A command block is one paste -- write it to a file first.**
`git add`, `git status`, `git commit`, and `git tag` belong in
**one** block, with the message inline via heredoc.  This session
lost time to `/tmp/<name>.sh: No such file or directory` **and** to
a half-applied edit (`commitB.sh` ran when told not to, leaving a
broken Makefile block that `make -n` caught).  **Always write the
script with `cat > ... <<'SCRIPT_EOF'` and run it in the same
paste**, check the file exists, and **prove the result with a
dry run** (`make -n`) before building.

**Python edits with `count != 1` abort checks are the preferred
edit style.**  `set -e` at the top.  Idempotent.  **Do the count
check IN PYTHON (`str.count`)**, not with `grep`.  **Before
asserting a count, count the occurrences in the file and write the
expected number from that count, not from memory.**  Count first,
then assert the counted number.  The third session's `ff_mutex_*`
abort was a retyped count (`== 2` where the block has `1` each) on
a correct edit -- the rule was right, the expected number was from
memory.

**ANCHOR ON STRUCTURE UNIQUE TO THE TARGET FUNCTION.**  Grep the
file, confirm the count is 1 **and** the match is in the intended
function.  **A post-write check string must be copied from the
`new` literal, not retyped** -- this session's `stop_reported`
abort was a retyped check (`self->` vs `stopped->`) on a correct
edit.  **And an anchor must be read from the file's current bytes
(`cat -A`), not from memory of what was written a few turns ago.**
When an anchor has been edited by an earlier script in the same
session, re-`cat -A` it before anchoring again.  A grep that
returns the right *count* is not enough: this session the
"staging busybox" echo existed **twice** (two targets), and the
count check caught it.

**A Makefile recipe is not plain shell.**  `#` comment lines inside
a backslash-continued recipe are stripped by make and break the
continuation: the command after them is dropped silently.
Comments belong **above** the recipe, outside the continuation.
`make -n` is the proof that a recipe expands to what you intended;
`cat -A` is not, because the bytes can be right while make's
preprocessing is not.

**A build can pass and the test still be the wrong test.**  The
shebang commits passed their first tests and still shipped a
cwd-doubling bug, because every test ran from `/`.  Run the new
feature from a **non-root cwd** as well as `/`.  This is the
running-test rule applied to *where* you run it, not just whether.

**Run the new feature the way a user would reach it, not only the
way that is convenient to test.**  The `nanosleep` resume was only
found because `sleep 1000` was run, stopped, and `fg`'d -- the
probe (`SIGSTOP_PEER_PROBE`) passed the whole time and covered a
different stop path (user-mode busy loop, not a blocked syscall).
The `&` bug was only bounded because `sleep N &` was run in the
shapes a user uses it, and the `Ctrl-Z; bg` workaround was found by
running it, not by reading the job-control code.

**Ask for source you do not have.**  Before patching a file it has
not seen **in this session**, ask for it.  The header greps before
each edit are how `sys_kill`'s missing `syscall.h` include and
`process_dump_all`'s missing `process.h` include were caught by the
compiler instead of at link.  **The third session read
`ff.c`/`ffconf.h`/`diskio.c` only after six fixes had been
proposed; reading first would have bounded the problem in one
turn.**

**The build is TWO steps.**  `make -C 04_kernel_64bit
FAT_CONFIG=single`; `make -C 05_boot_kernel64 hdd-single.img`.
Running only the second copies a STALE `kernel.bin`.  Verify
`kernel.elf` is newer than the source you edited, and that
`strings kernel.bin` carries a string from the new code.

**A config change does not relink by itself.**  See the busybox
stamp note above.  Same class: `ffconf.h` needs `make clean` (or
removing `ff.o`/`diskio.o`) before the build; check the `.o`
timestamps against the header.

**`EXTRA_CFLAGS` is NOT forwarded by `04_kernel_64bit/Makefile`.**
`SW2_TRACE` is a one-line `#define` in `scheduler.c`.

**`nm` output moves between builds.**  Re-run `nm` before using
any address.  `current_process` has **two** symbols.
`pcb_pool` stride is `0x840`.  The `_Static_assert` block in
`process.c` pins offsets through `user_rsp` (0x610) and
`PROC_STATE_STOPPED == 6`; nothing asserts a field past
`in_stop_safe_point`, so the fields appended after it
(`stop_signal`, `stop_reported`, `shebang_depth`) moved no asserted
offset, and the build passing is the proof.

**`kernel.elf` is built WITHOUT `-g`.**  GDB over `-s` has no
types; use the raw offsets.

**A commit message is not a diff.**  `git show <sha> --stat` is the
check.  And a commit message is not a test: do not write
"verified" for something the transcript does not show.

**Test notes.**  `pidof <applet>` finds nothing: every busybox
applet's `comm` is `busybox`, so `pidof sleep` is useless.  Use
`ps` and read the `STAT` column (`T` is stopped).  `/proc/<pid>/`
exists only for live pids; a transient `cat` pid may be gone by the
time you look, and `/proc/4/status` for the long-lived stopped job
reads fine.

**Do not run `git reset --hard` without stashing first.  Do not
edit `third_party/`.  Do not commit `kmain.c`'s `pit_init` knob --
the committed value is 100.  Do not call a race closed on one
clean run.  Do not add a `cli`-depth mutex around the FatFs API
(the third session's crash).**

---

## Releases

**`v0.6.18` is released.**  Work is pre-**`v0.6.19`**.  The
`20261030-*` tags (eight from the second session -- five
job-control, `20261030-shebang-kernel`, `20261030-shebang-path`,
`20261030-nanosleep-resume` -- plus the two from the prior session)
are the current restore points; the release procedure draws the
`v0.6.19` summary from them before dropping them.  **The third
session landed no code and made no tag** (its `FF_FS_REENTRANT`
attempt was reverted; the tree is at `9d6ec02` plus nothing).
**Nothing is pushed** until `v0.6.19` is ready and verified.  Item
14 (shebang / `/bin/sh`) closes into this release.

---

## One-line summary

**Job control works end to end on the framebuffer console, and the 
kernel parses `#!` and runs scripts from any cwd.** Ten commits took 
Ctrl-Z from "puts a literal `z` in the buffer" to `[1]+ Stopped`, 
prompt back, `jobs`, `fg`, with `fg` proven to resume both a 
`console_read` job (`cat`) and a `nanosleep` job (`sleep 60` blocks 
for its remaining time). The chain: PS/2 scancode `0x2C` 
(or serial byte `0x1A`) -> `sys_kill(-fg, SIGTSTP)` to 
`g_console_fg_pgrp` -> default-action stop at the blocked syscall's 
safe point -> `process_stop_and_yield` holds until SIGCONT -> 
`process_stop` wakes the `wait4`-blocked shell -> `wait4(WUNTRACED)` 
reports `(sig<<8)|0x7f` -> ash prints and prompts. Two commits landed 
the shebang kernel half plus `::/bin/sh` on both images (item 14); a 
third fixed the cwd-doubling bug in the script argument; a fourth fixed 
`nanosleep` exiting on a stop/continue resume. The `run` script's 
host-side `stty susp undef` landed but headless serial `Ctrl-Z` remains 
unverified (host item). Gate: 28/28 + 57/57 + the job-control, shebang, 
and resume transcripts on `HEAD`, and `HEAD` builds alone.

**Backgrounded-applet (`&`) bug: diagnosed, not fixed.** `sleep N &` 
fails because ash's standalone resolver `execve`s `/bin/busybox`, and 
that `f_open` returns `FR_NO_FILE`: FatFs's `fs->win` is one shared 
window per `FATFS` with `FF_FS_LOCK=0` and no lock, so the `&` child's 
open races the parent's live `FIL`. The fix that worked in a probe was 
*closing* fds 3..MAX before the open; the fixes that did not were hiding
the slots (refuted), stripping the path (refuted), `FF_FS_LOCK=8` 
(refuted), and `FF_FS_REENTRANT=1` + a `cli`-guarded section (refuted).

**Next is the backgrounded-applet dispatch bug, then settling headless 
serial Ctrl-Z, then SSE and privilege.** Open threads: backgrounded 
applet dispatch, headless serial Ctrl-Z (host), the `wait4` 
stopped-child late re-scan, a no-peer stop soak, and the one-off 
`SWITCH-REFUSE`. Read `docs/contract.md` and "NEXT" before any code.
