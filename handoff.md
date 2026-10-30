# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-30 (session close, second session this
date).  **`v0.6.18` is released; work is pre-`v0.6.19`.**  This
session made **job control work end to end on the serial console**:
`cat`, Ctrl-Z, `[1]+ Stopped`, prompt back, `jobs` lists, `fg`
resumes.  Baseline on `HEAD`: `canary --full` 28/28, `test.sh`
57/57, job control transcript green, `HEAD` builds alone.  `dev` is
ahead of `origin/dev` by **42 commits**, nothing pushed.

**Read "The concurrency contract" in `docs/contract.md` before
touching shared state, then "Where we are".**  The ash boot hang
that blocked the tty side is in `docs/2026-10-30-ash-boot-loop.md`;
this file carries only conclusions.

**THE SIX COMMITS THIS SESSION** (on `dev`, tagged, nothing
pushed):

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
  the serial input and eats the byte.  This is the path the serial
  console actually uses; the PS/2 IRQ never fires for it.
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

**CORRECTIONS THIS SESSION TO PRIOR PROSE** (the code and commit
messages are right; older notes are not):

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

**WHAT THE SHELL IS.**  Unchanged: boot shell is `musl_sh` ->
fork/exec `/bin/busybox sh` -> ash.  The busybox is now built with
`CONFIG_ASH_JOB_CONTROL=y`, so ash is a job-control shell.

**WHERE WE ARE.**  Job control's tty side is **done**: Ctrl-Z on
either the PS/2 keyboard or the serial console raises SIGTSTP to
the console foreground pgrp; the stop holds; `wait4(WUNTRACED)`
reports it; ash prints `[1]+ Stopped` and returns a prompt; `jobs`
and `fg` work.  The foreground pgrp is `g_console_fg_pgrp`, set by
`TIOCSPGRP` (lazily by the first `TIOCGPGRP`), read under `cli` via
`console_fg_pgrp_get()`.

**NOT DONE, from this session's work:**

- `process_wake_parent_if_waiting` can miss a stop in the window
  between `sys_wait4`'s scan and its block-store.  The zombie path
  has a late re-scan for exactly this; the stopped path does not.
  In the end-to-end test it did not bite, but the window is real.
  The fix is the late re-scan also checking `STOPPED &&
  !stop_reported`.  Small commit, own soak.
- `process_stop_and_yield`'s `while (STOPPED) process_yield()` loop
  has not been exercised with **no runnable peer**.  The
  scheduler's fall-to-idle makes it unreachable today; a soak that
  stops a process when nothing else is runnable would prove it.
- `kill(pid <= 0)` group semantics: `kill(-pgid, sig)` works;
  `kill(0, sig)` ("my group") still returns `-EINVAL`.
- Ctrl-C on the serial console is **not** mapped to SIGINT.
  (Host `intr = ^C` usually eats it before it reaches the guest.)

**WHERE WE ARE, the rest** (unchanged from the prior handoff): no
real `mount(2)`; file-backed `mmap`; `link(2)`/`mknod(2)`;
**shebang (`#!`) is not parsed by `sys_execve`** (item 4);
`chmod`/`chown` accept and drop; SSE state is not saved (item 5);
no privilege model (item 6).

**NEXT.**

1. **Shebang + `/bin/sh` kernel half** (open-issues item 14).
2. **SSE** (item 5): save/restore XMM, then remove `-mno-sse*`.
3. **Privilege model** (item 6).
4. busybox batch 2; `test.sh`'s md5.
5. **A completed PIPE7E soak** at 100 Hz.
6. The two follow-ups this session surfaced (above): the `wait4`
   stopped-child late re-scan, and the no-peer stop soak.

### Working style -- how this project gets changed

Read this before proposing any command block.

**One change at a time.**  A "change" is a commit, verified before
the next one starts.  **Verify by RUNNING, not by committing.**
This session showed why: the stop-hold fix, the EINTR rule, and the
`wait4` wake were each proven by a keystroke test, not by a clean
build.

**A command block is one paste -- write it to a file first.**
`git add`, `git status`, `git commit`, and `git tag` belong in
**one** block, with the message inline via heredoc.  This session
lost time twice to `/tmp/<name>.sh: No such file or directory` --
the block was in the message, not on disk.  **Always write the
script with `cat > ... <<'SCRIPT_EOF'` and run it in the same
paste**, and check that the paste actually created the file.

**Python edits with `count != 1` abort checks are the preferred
edit style.**  `set -e` at the top.  Idempotent.  **Do the count
check IN PYTHON (`str.count`)**, not with `grep`.  **Before
asserting a count, count the occurrences in the file and write the
expected number from that count, not from memory.**

**ANCHOR ON STRUCTURE UNIQUE TO THE TARGET FUNCTION.**  Grep the
file, confirm the count is 1 **and** the match is in the intended
function.  **A post-write check string must be copied from the
`new` literal, not retyped** -- this session's `stop_reported`
abort was a retyped check (`self->` vs `stopped->`) on a correct
edit.  **And an anchor must be read from the file's current bytes
(`cat -A`), not from memory of what was written a few turns ago.**
When an anchor has been edited by an earlier script in the same
session, re-`cat -A` it before anchoring again.

**Ask for source you do not have.**  Before patching a file it has
not seen **in this session**, ask for it.  The header greps before
each edit are how `sys_kill`'s missing `syscall.h` include and
`process_dump_all`'s missing `process.h` include were caught by the
compiler instead of at link.

**The build is TWO steps.**  `make -C 04_kernel_64bit
FAT_CONFIG=single`; `make -C 05_boot_kernel64 hdd-single.img`.
Running only the second copies a STALE `kernel.bin`.  Verify
`kernel.elf` is newer than the source you edited.

**A config change does not relink by itself.**  See the busybox
stamp note above.

**`EXTRA_CFLAGS` is NOT forwarded by `04_kernel_64bit/Makefile`.**
`SW2_TRACE` is a one-line `#define` in `scheduler.c`.

**`nm` output moves between builds.**  Re-run `nm` before using
any address.  `current_process` has **two** symbols.
`pcb_pool` stride is `0x840`.  The GDB offsets from the prior
handoff still hold: the two new `pcb_t` fields were appended after
`in_stop_safe_point`, so every offset through `user_rsp` (0x610)
is unchanged; `stop_signal`/`stop_reported` sit after the
previously-last field.

**`kernel.elf` is built WITHOUT `-g`.**  GDB over `-s` has no
types; use the raw offsets.

**A commit message is not a diff.**  `git show <sha> --stat` is the
check.

**Do not run `git reset --hard` without stashing first.  Do not
edit `third_party/`.  Do not commit `kmain.c`'s `pit_init` knob --
the committed value is 100.  Do not call a race closed on one
clean run.**

---

## Releases

**`v0.6.18` is released.**  Work is pre-**`v0.6.19`**.  The
`20261030-*` tags (five this session, plus the two from the prior
session) are the current restore points; the release procedure
draws the `v0.6.19` summary from them before dropping them.
**Nothing is pushed** until `v0.6.19` is ready and verified.

---

## One-line summary

**Job control works end to end on the serial console.**  Six
commits took Ctrl-Z from "puts a literal `z` in the buffer" to
`[1]+ Stopped`, prompt back, `jobs`, `fg`.  The chain: serial byte
`0x1A` (or PS/2 scancode `0x2C`) -> `sys_kill(-fg, SIGTSTP)` to
`g_console_fg_pgrp` -> default-action stop at the blocked
`console_read`'s safe point -> `process_stop_and_yield` holds until
SIGCONT -> `process_stop` wakes the `wait4`-blocked shell ->
`wait4(WUNTRACED)` reports `(sig<<8)|0x7f` -> ash prints and
prompts.  Gate: 28/28 + 57/57 + the job-control transcript on
`HEAD`, and `HEAD` builds alone.  **Next is shebang, then SSE, then
privilege.**  Two follow-ups are recorded above: the `wait4`
stopped-child late re-scan, and a no-peer stop soak.  Read
`docs/contract.md` and "NEXT" before any code.
