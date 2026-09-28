# donix gotchas

Bug writeups, by subsystem.  Lookup material: grep this file when
you hit a specific problem.  Append-only; new gotchas go here, not
into `handoff.md`.

Each entry is dated when it was learned so you can tell which are
fresh and which are long-settled.

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
