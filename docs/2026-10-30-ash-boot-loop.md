# 2026-10-30: the ash boot loop (tcgetpgrp(0) != getpgid(0))

Durable note for the session that fixed the 2026-10-30 boot hang. The
handoff points here; it is not needed to start a session.

## Symptom

Boot printed the BusyBox banner and `Enter 'help' ...`, then nothing:
no `~ #` prompt, no echo of typed keys, in either the serial terminal
or the SDL window. The CPU was idle (`kernel_idle_loop`, `hlt`). The
`sh: can't access tty; job control turned off` line was **gone**, so
the tty was now accessible and the hang was downstream of it.

## What the shell actually is

The boot shell is **`musl_sh`** (`userland/musl/apps/musl_sh.c`), which
**forks and execs `/bin/busybox sh`** and waits for it
(`run_busybox_sh()`, called at startup). The PCB name of the executed
process stays `musl_sh`; the banner is busybox's. So the boot path is
`musl_sh` -> fork/exec `busybox sh` -> ash. The old handoff's "NEXT is
the busybox ash tty side" assumed ash is the boot shell; it is the
*exec'd* shell.

## The loop, named by objdump

busybox `0x4205b2`-`0x4205de`:

    call 0x450166        ; tcgetpgrp(0)
    ...
    call 0x44fefa        ; getpgid(0)  (mov $0x79,%eax; xor edi,edi; syscall)
    cmp  %r12d,%eax
    je   0x4205ec        ; equal -> proceed
    ...                  ; else retry, jmp 0x4205b2

and `0x450166` is `tcgetpgrp(0)`: it calls `ioctl(0, TIOCGPGRP,
&stack[0xc])` and returns the `pid_t` in that slot on success.

The observed registers at `0x4205de`: `eax = 3` (`getpgid(0)` =
the shell's pgrp), `r12d = 0` (`tcgetpgrp(0)`). They never agreed,
so ash retried forever.

## The two kernel causes, and the pair

1. **`getpgid(0)` did not exist.** The dispatch `default:` returned
   `-ENOSYS` and ash printed `Unknown syscall: 121` (121 =
   `getpgid`) in a loop. The pgrp syscalls -- `getpgid` (121),
   `getpgrp` (111), `setpgid` (109) -- over `pcb_t.pgrp` were
   required, not optional.
2. **`TIOCGPGRP` returned 0 before any `TIOCSPGRP`.** Nothing set the
   console's foreground pgrp at boot, so `tcgetpgrp(0)` read 0 while
   `getpgid(0)` read the shell's pgrp (3).

Either one alone leaves ash looping (on `Unknown syscall: 121`, or on
`tcgetpgrp != getpgid`). **They are a pair**; landing one without the
other produces a different hang with the same shape.

## The fix (commit 3e8bfc5)

- `TIOCGPGRP` answers with the caller's own `pgrp` when
  `g_console_fg_pgrp == 0`, and remembers it: the first process to ask
  is the terminal's foreground group, which is what a boot shell is
  before it calls `tcsetpgrp`. The check and the lazy set are one
  `cli` region (contract rule 1).
- The wire type is a `pid_t` (4 bytes), not `uint64_t` (8). musl's
  `tcgetpgrp` reads into a 4-byte slot; writing 8 stomps the word
  after it. Both `TIOCGPGRP` and `TIOCSPGRP` move a `uint32_t`.
- The pgrp syscalls are added (see above), `setpgid`'s write under
  `cli`.

## The scheduler scans (commit abccc8c) -- not this hang

Both item-7d "stranded-READY recovery" scans (`process_exit`'s and
`process_yield`'s) carried a comment saying "Re-add READY or
stranded-RUNNING" and a predicate testing only
`state == PROC_STATE_READY`. A non-current `RUNNING` process fell
through. The fix inserts the `RUNNING -> READY` transition before the
READY test (insertions only). This is a real code/comment divergence,
but it was **not** what made the 2026-10-30 boot hang, and it did not
by itself produce a prompt. The commit message says so.

## Diagnostics that cost time

- **`EXTRA_CFLAGS` is not forwarded by `04_kernel_64bit/Makefile`.**
  `SW2_TRACE` is a one-line `#define` in `scheduler.c`; enabling it via
  `make EXTRA_CFLAGS=-DSW2_TRACE=1` silently does nothing. The
  `SW2-*` dispatch trace (one `SW2-ENTRY`/`SW2-ASSIGN`/`SW2-CALL`
  per dispatch) names every transition.
- **`nm` moves between builds.** Re-run it every build.
  `pcb_pool` was `0xffffffff80281d90`, stride `0x840`
  (`sizeof(pcb_t) = 0x10800/32`); **`current_process` has two
  symbols** (`process.c` and `scheduler.c` copies).
- **The PCB walk** over `pcb_pool` by raw offsets (`pid` 0x000,
  `state` 0x028, `block_kind` 0x158, `sig_pending` 0x618, `name`
  0x008) is the fastest way to see a lost process.
- **`objdump` of `busybox.elf`** names user-side addresses: `0x44fefa`
  = `getpgid`, `0x450166` = `tcgetpgrp`, `0x44be01` =
  `__syscall_ret`.
- **`git show HEAD --stat` is not optional.** The first commit of the
  pair (`879c50c`) contained the `case SYS_*` lines but not the
  `#define`s in `include/syscall.h` -- `HEAD` did not build. The
  stat line (`1 file changed`) and `syscall.h` still `M` named it;
  the amend (`3e8bfc5`) fixed it. The `04b` lesson in a new form.
