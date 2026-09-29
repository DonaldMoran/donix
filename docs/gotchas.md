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
  session 26.  Still true after the session-31 parser rewrite --
  `vga_putc_raw`'s `\b` handling was not changed.)

- **The VGA console is a VT100 emulator (session 31).**  `vga.c` has
  a full CSI parser: four states (`ANSI_NORMAL`, `ANSI_ESC`,
  `ANSI_ESC_CHARSET`, `ANSI_CSI`), a parameter array
  (`params[]`, up to 8), a private-marker flag, and an intermediates
  array.  Final-byte dispatch covers
  `A B C D E F G H f J K L M P @ X S T Z m h l n c`: cursor motion,
  cursor addressing, all three modes each of `J`/`K`,
  insert/delete line/char, scroll, SGR, private mode set/reset, DSR,
  and DA.  SGR handles `0 1 7 22 27 30-37 39 40-47 49` (bold,
  reverse video, standard 8 fg / 8 bg).  A software alternate screen
  handles `?1049` (save + clear), `?1047` (save only), and `?25`
  (cursor visibility).  (Replaces the session-26 entry that said SGR
  and cursor addressing were ignored and only `0J`/`0K` were
  implemented.  That was true of the old minimal parser; the parser
  was replaced in session 31.  Learned 2026-09-28, session 31.)

- **`ESC ( B`-style charset-select sequences used to leak to the
  screen as `(B`.**  The old parser had no state for the byte after
  `ESC (`, so it fell through to `vga_putc_raw` and printed the `(`
  and the designator.  The session-31 parser adds
  `ANSI_ESC_CHARSET`, entered on `ESC (`, `ESC )`, `ESC *`, or
  `ESC +`, which swallows exactly one designator byte.  If you see a
  stray `(B` on screen from a new program, this state is the place
  to look.  (Learned 2026-09-28, session 31.)

- **`?` is a private marker, not a CSI intermediate.**  In
  `ESC[?1049h`, the `?` must be recorded in `p.private_marker` and
  must NOT be pushed into `p.intermediates[]`.  `csi_dispatch` bails
  out early when `p.n_intermediates > 0` (intermediates mark
  sequences we do not implement), so pushing `?` there would make
  every private-mode sequence -- including `?1049h` for the
  alternate screen and `?25l` for cursor hide -- unreachable, and
  they would be reported as `[vga] unhandled CSI: ?1049 (with
  intermediates)`.  The `h`/`l` dispatch checks `private_marker` and
  routes to `vga_set_private_mode`.  (Learned 2026-09-28, session
  31.)

- **Alt-screen on VGA text mode cannot be a pointer swap.**  VGA
  text mode has exactly one framebuffer, at physical `0xB8000`.  The
  CRT controller scans that address unconditionally; there is no
  second hardware buffer to point it at.  An "alternate screen" is
  therefore simulated by save/restore: `?1049h` copies `0xB8000`
  into a BSS array (`alt_screen[]`), then clears the visible screen;
  `?1049l` copies the array back and restores the saved cursor
  position.  A pointer-swap approach was tried and failed -- the
  screen goes dead, because the CRT controller keeps reading
  `0xB8000` regardless of what any software pointer says.  (Learned
  2026-09-28, session 31.)

- **`VGA_TRACE_UNHANDLED` and `VGA_REPLY_TO_QUERIES` must be 0 for a
  tag.**  Both live at the top of `vga.c`.  `VGA_TRACE_UNHANDLED=1`
  emits `[vga] unhandled CSI: ...` lines to the serial port for any
  CSI sequence that parses but does not dispatch -- the single most
  useful tool when bringing up a new curses program, but noise in a
  shipped build.  `VGA_REPLY_TO_QUERIES=1` makes the console answer
  DSR (`ESC[6n`) with `ESC[r;cR` and DA (`ESC[c`) with `ESC[?1;0c`
  on the serial port.  Both were set to 0 before the `v0.6.4`/vi
  verification run and that run passed, so leave them at 0.  If
  `CONFIG_FEATURE_VI_ASK_TERMINAL` is turned on in the busybox
  config, vi will send `ESC[6n` at startup and wait; the reply
  tunable must go back to 1 first or vi stalls at launch.  (Learned
  2026-09-28, session 31.)

### Keyboard

- **An unassigned scancode in `scancode_ascii[]` silently drops the
  key.**  `scancode_to_ascii` returns 0 for an unassigned scancode,
  and `kbd_buffer_put` has a NUL guard that drops 0 bytes (it exists
  to filter hardware break-code noise).  So "table entry missing"
  and "no key pressed" are indistinguishable downstream, and the key
  appears to do nothing.  **`scancode_ascii[0x01]` (ESC) was
  unassigned**, which is why no full-screen program could leave
  insert mode before session 31 -- pressing ESC produced 0, the
  guard dropped it, and vi never saw the byte.  The fix is the one
  line `scancode_ascii[0x01] = 0x1B;` in `keyboard_init`.  Any
  future "this key does nothing" report should check this table
  first.  (Learned 2026-09-28, session 31.)

- **Backspace must be DEL (0x7F), not BS (0x08).**  Unix software
  (busybox vi/ash/less, ncurses) treats `0x7F` as Backspace and
  `0x08` as Ctrl+H, a distinct command.  `keyboard_init` sets
  `scancode_ascii[0x0E] = 0x7F`.  Emitting `0x08` makes Backspace
  insert a literal `^H` in editors instead of deleting.  (Learned
  2026-09-28, session 31.)

- **Enter must be CR (`\r`), not LF (`\n`).**  A terminal in raw
  mode -- which vi sets -- delivers CR; cooked mode translates CR to
  LF via `ICRNL` for line-oriented readers like the shell.  If the
  keyboard emitted LF directly, raw-mode programs would see LF where
  they expect CR and command lines would not terminate.
  `keyboard_init` sets `scancode_ascii[0x1C] = '\r'`.  The donix
  shell still works because it reads in cooked mode.  (Learned
  2026-09-28, session 31.)

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
  was closed in session 28 (tag `20260928-08`); `geteuid(2)` was
  closed in session 30.  Session 30 also moved `setsid` to 112 and
  fixed `geteuid` at 107.

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

- **`open(2)` flag bits: `O_CREAT` is 0x40, not 0x200.**  The
  original `sys_open` tested the wrong bits and as a result never
  set a FatFs creation flag for `open(O_RDWR|O_CREAT)` -- what
  `touch`, vi's `:wq`, `cp`, and every file-creating program issues.
  FatFs tried to open an existing file, found none, and returned
  `FR_NO_FILE`.  The trace that proved it:

      sys_open: f_open FAIL path=don.txt flags=0x8042 mode=0x03 r=4

  `flags=0x8042` is `O_RDWR|O_CREAT|O_LARGEFILE`; `mode=0x03` is
  `FA_READ|FA_WRITE` with no create bit.  The old check was
  `flags & 0x0200` (which is `O_TRUNC`) instead of `flags & 0x0040`
  (`O_CREAT`).

  Correct Linux x86_64 flags (from `include/uapi/asm-generic/fcntl.h`):

      0x0001  O_WRONLY
      0x0002  O_RDWR
      0x0040  O_CREAT     <-- old code tested 0x0200
      0x0080  O_EXCL      <-- old code tested 0x0800
      0x0200  O_TRUNC     <-- old code tested 0x0400
      0x0400  O_APPEND    <-- old code tested 0x0008
      0x8000  O_LARGEFILE (ignore)
      0x10000 O_DIRECTORY

  FatFs modes: `FA_READ 0x01`, `FA_WRITE 0x02`, `FA_CREATE_NEW
  0x04`, `FA_CREATE_ALWAYS 0x08`, `FA_OPEN_ALWAYS 0x10`,
  `FA_OPEN_APPEND 0x30`.  FatFs expresses the creation choice as a
  single mode value, so precedence matters:

      O_CREAT && O_EXCL      -> FA_CREATE_NEW
      O_CREAT && O_TRUNC     -> FA_CREATE_ALWAYS
      O_CREAT                -> FA_OPEN_ALWAYS
      O_TRUNC (no O_CREAT)   -> FA_CREATE_ALWAYS
      otherwise              -> FA_OPEN_EXISTING
      O_APPEND               -> additionally OR in FA_OPEN_APPEND

  The whole `sys_open` was rewritten in session 31; do not patch
  just the flag block, since the path-resolution, `strip_dot_prefix`,
  root-alias, and directory-fallback logic must stay consistent with
  the flag logic.  (Learned 2026-09-28, session 31.)

- **`r=4` from FatFs is `FR_NO_FILE`, and it is often correct.**
  On vi launch you will see:

      sys_open: f_open FAIL path=don.txt flags=0x8000 mode=0x01 r=4

  `flags=0x8000` is `O_LARGEFILE|O_RDONLY`, `mode=0x01` is
  `FA_READ`, `r=4` is `FR_NO_FILE`.  vi is opening a nonexistent
  file for read; FatFs correctly reports no such file; vi correctly
  falls back to new-file mode.  **Do not try to silence this line.**
  Full FatFs result codes, for reference: 0=OK, 1=INT_ERR,
  2=NOT_READY, 3=NO_FILE, 4=NO_PATH, 5=INVALID_NAME, 6=DENIED,
  7=EXIST, 8=INVALID_OBJECT, 9=WRITE_PROTECTED, 10=INVALID_DRIVE,
  11=NOT_ENABLED, 12=NO_FILESYSTEM, 13=MKFS_ABORTED, 14=TIMEOUT,
  15=LOCKED, 16=NOT_ENOUGH_CORE, 17=TOO_MANY_OPEN_FILES.  (Learned
  2026-09-28, session 31.)

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
blocking poll blocks on fd 0 with timeout < 0; session 31:
ftruncate seeks then truncates; utimes/futimesat/utimensat are
no-op stubs that verify the path exists)

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
helpers.  See `docs/open-issues.md` item 5 and the VFS SHIM
comment in `sys_execve`.
