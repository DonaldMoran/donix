# donix gotchas

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

## Syscall ABI
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
sharing; sys_ioctl; busybox FEATURE_EDITING; PREFER_APPLETS)

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
