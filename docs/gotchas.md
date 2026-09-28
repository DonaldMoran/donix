Bug writeups, by subsystem.  Lookup material: grep this file when
you hit a specific problem.  Append-only; new gotchas go here, not
into `handoff.md`.

Each entry is dated when it was learned so you can tell which are
fresh and which are long-settled.

### Console / VGA

- **`\b` in the VGA driver erases; ANSI says it should only move the
  cursor.**  `vga_putc_raw` writes a space over the cell to the left
  and moves the cursor there.  Standard VT100 `\b` leaves the cell
  alone.  busybox's `"\b \b"` idiom (erase, space, erase) works under
  either behavior because the operations cancel out, which is why
  this has not caused a visible bug.  A future program that emits
  bare `\b` expecting cursor-only movement will misbehave; the fix
  is to move the erase into the explicit `ESC[K`/`ESC J` paths and
  make `\b` pure cursor movement.  Not urgent.  (Learned 2026-09-28,
  session 26.)

- **The VGA console understands a subset of ANSI.**  `ESC[K`,
  `ESC[J`, `ESC[nD`, `ESC[nC` are handled; `ESC[...m` (SGR) and
  cursor addressing are ignored; unknown `ESC[...X` sequences are
  swallowed.  Only the `0J`/`0K` (or bare) erase variants are
  implemented -- `1J`, `2J`, `2K` are not.  busybox only emits the
  bare forms.  Parser lives in `vga_putc_unlocked`.  (Added
  2026-09-28, session 26.)

### Syscall ABI

- **Syscall numbers must match the Linux x86_64 ABI; never guess from
  the diagnostic.**  donix's syscall table is supposed to use the
  real Linux x86_64 numbers.  When an entry diverges, the failure is
  invisible to any test built against donix's own expectations, and
  the `Unknown syscall: N` diagnostic names the *number the caller
  used* -- which may be a completely different syscall from the one
  you think is missing.

  Found in session 27, two instances of the same mistake:

    - `mkdir` was at 7.  Linux has `poll` at 7 and `mkdir` at 83.
    - `setsid` was at 107.  Linux has `geteuid` at 107 and `setsid`
      at 112.

  In both cases the handler was written because a caller logged
  `Unknown syscall: 7` / `Unknown syscall: 107`, and the number was
  assumed to name the missing call.  It did not: 7 was `poll`, 107
  was `geteuid`.  The handlers sat at numbers no correct caller
  uses, so they were dead code -- and they shadowed the real
  syscalls at those numbers (a caller doing `poll(fds,1,timeout)`
  got `sys_mkdir`'s return values; a caller doing `geteuid()` got
  `sys_setsid`'s pid).

  Symptom that exposed it: `busybox mkdir` printed `Function not
  implemented` while `Unknown syscall: 83` appeared in the serial
  log.  busybox calls the real Linux number 83; the kernel's handler
  was at 7.

  Corrected in tag `20260928-05` (mkdir 83, setsid 112).  The
  previously masked gaps -- `poll(2)` at 7 and `geteuid(2)` at 107 --
  are now visible and tracked in `docs/open-issues.md`.  `poll(2)`
  was closed in session 28 (tag `20260928-08`); `geteuid(2)` is the
  last remaining gap from this audit.

  Lesson: any new syscall entry must be checked against the
  canonical Linux x86_64 table,
  `arch/x86/entry/syscalls/syscall_64.tbl`.  Do NOT infer the number
  from what appears to be missing.  The diagnostic names the
  caller's number, which is authoritative; use it.  (Learned
  2026-09-28, session 27.)

- **musl is unmodified upstream; it uses the real Linux numbers.**
  `third_party/musl-install/include/bits/syscall.h` has
  `__NR_mkdir 83`, `__NR_setsid 112`, `__NR_poll 7`.  So a
  musl-built binary calls the real numbers, and a kernel handler at
  any other number is simply never reached from musl.  This is what
  made the divergences above invisible to the musl tests: nothing
  musl calls ever went to 7 or 107 expecting mkdir/setsid.  (Learned
  2026-09-28, session 27.)

- **`poll(fds, 1, -1)` on Linux never returns 0; busybox ash treats
  0 as end-of-input.**  The first `sys_poll` (session 28) was
  non-blocking and returned 0 whenever nothing was buffered.
  busybox ash's line editor (FEATURE_EDITING=y) runs
  `poll(&pfd, 1, -1)` once per readline iteration and interprets a
  0 return as "no more input" -- it exits the shell immediately
  after the first poll.  On real Linux a poll with an infinite
  timeout cannot return 0, so ash has no code path for it.

  The handler MUST block on fd 0 when `timeout < 0`, using the
  same `cli` / `state = BLOCKED` / `sti; hlt` sequence `sys_read`
  uses, so `irq1_handler` -> `process_wake_all_blocked` wakes it
  on the next keystroke.  The `cli` is load-bearing: without it
  there is a missed-wakeup window between the `has_data()` check
  and the `state = BLOCKED` store -- irq1 fires, puts a byte,
  calls `process_wake_all_blocked`, sees the state is still
  RUNNING, does nothing; then we set BLOCKED and hlt and nobody
  ever wakes us.

  Returning `-EINTR` was considered as a cheaper workaround and
  rejected.  It produces correct behavior for ash by coincidence
  (ash's error path retries), but lies to any future caller that
  distinguishes "timeout" from "signal interrupted".

  `timeout >= 0` is still non-blocking in the current handler.
  No caller uses a finite timeout yet; if one appears, arm a
  `g_ticks` deadline and loop, per `docs/open-issues.md`.
  (Learned 2026-09-28, session 28.)

## Process / scheduler
(existing entries: fork_copy_frame preserves %r8/%r9; fork eager
stack copy; fork inherits fs_base; fork copies ELF image region;
execve updates frame RIP/RSP; execve atomic; process_create bakes
entry_point; scheduler queue idempotency; wake_all_blocked skip;
yield removes BLOCKED; exit empty-queue fallback)

## Scheduler queue discipline
(existing entries)

## Context switch
(existing entries: MSR_FS_BASE save/restore/inherit; pcb_t field
ordering after block_kind)

## Syscall ABI (continued)
(existing entries: return path preserves all but rax/rcx/r11;
sys_read returns first byte; SYS_EXIT=60; -mcmodel=large note;
16-slot frame layout; epilogue loads user RSP last; execve argv
layout; execve passes argc/argv in rdi/rsi; raw-syscall memory
output constraint; register-pinned GPR read; musl wrapper routing;
musl fstatat routing; stat/lstat root synthesis; ./ prefix
stripping; O_DIRECTORY routing; fcntl for opendir; mmap VAs;
fill_kstat_from_filinfo sharing; getdents64 one-record;
musl_sh argv[0] normalization; directory ports; ls stats first;
file_slot_t refcounting; execve bare-name retry; proper errnos;
sys_access/faccessat; FR_NO_PATH retry; f_stat_with_retry
sharing; sys_ioctl; busybox FEATURE_EDITING; PREFER_APPLETS;
blocking poll blocks on fd 0 with timeout < 0)

## Build system
(existing entries)

## Musl userland tree
(existing entries)

## Git hygiene
(cross-reference: full text in docs/strategy.md)

## Open issues
(see docs/open-issues.md for the full list; short entries that are
really gotchas stay here)

## Cosmetic / housekeeping
(existing entries: puthex/put_dec audits; SYS_REBOOT; PMM_ALLOC_DIAG)

## Bare-name resolution has two layers, and the shell is the
## wrong place for it

**Symptom (session 29):** after busybox moved from `BUSYBOX.ELF`
at the FAT root to `/bin/busybox`, `busybox ls` from `donix>`
started failing with:

    sys_execve: f_open(busybox) -> 4
    EXEC-FAILED

while `ls` and `hello` kept working.  Also, earlier in the same
session, `/LS.ELF` from `donix>` had failed with:

    sys_execve: f_open(0://LS.ELF.ELF) -> 4
    EXEC-FAILED

— note the doubled slash and the doubled `.ELF`.

**Root cause, part 1: the shell was rewriting paths.**
`userland/musl/apps/musl_sh.c` built a `char path[128]` in the
child by prepending `0:/` and appending `.ELF` to `argv[0]`
before calling `execve`.  That predates the kernel's path
resolution work.  For a leading-slash input like `/LS.ELF` it
produced `0://LS.ELF.ELF`, which FatFs rejects before the
kernel's own retry can see the original `/LS.ELF`.  **The
kernel's attempt (b) could never fire, because the shell had
already mangled the path.**

**Root cause, part 2: bare-name resolution only knew about the
FAT root.**  `sys_execve`'s bare-name retry
(`exec_resolve_bare_name`) produced `0:/NAME.ELF` — root only.
That worked for donix-native binaries (`ls`, `hello`) but not
for `busybox` once busybox lived at `/bin/busybox`.

**Fix (session 29, tags `20260928-13` and `20260928-14`):**

- **Delete the shell's rewriting.**  `musl_sh` now calls
  `execve(argv[0], argv, NULL)` and passes `argv[0]` through
  unchanged.  The kernel does the translation.
- **Extend attempt (c) to three sub-attempts**, in order:
  `0:/NAME.ELF` (root, uppercased, `.ELF` appended), then
  `0:/BIN/NAME`, then `0:/BIN/NAME.ELF`.  Root wins so
  donix-native binaries shadow same-named `/bin` entries.

**The lesson:** the kernel is the layer that translates a
Unix-style path to the form FatFs accepts.  The shell should
pass `argv[0]` through unchanged.  Two layers each trying to
normalize produces mangled paths (`0://LS.ELF.ELF`) that neither
layer can recognize.

**Corollary:** this whole block is a **shim for a VFS**.  On
real Unix, `execve` hands the path to the VFS and the VFS
resolves it; there is no guessing and no retry.  When a VFS
lands, delete attempts (b) and (c) and the two `exec_resolve_*`
helpers.  See `docs/open-issues.md` item 4 and the VFS SHIM
comment in `sys_execve`.
