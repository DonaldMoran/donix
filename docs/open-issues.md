# donix open issues

Full list.  `handoff.md` carries the top 3.  Priority order,
highest first.

1. `isr14_handler` halts on user-mode `#PF` -- see `docs/gotchas.md`.
2. Fork is O(~6 MB) per call -- real COW is the long-term fix.
3. `sys_newfstatat` (262) not implemented.
4. `sys_munmap` is a stub returning 0.
5. `sys_brk` uses a fixed `heap_base = 0x8000200000`.
6. The mmap window is a fixed 4 MB.
7. `sys_open` accepts non-directories with `O_DIRECTORY`.
8. `sys_mkdir` returns `-ENOENT` for the empty path; `FR_EXIST`
   maps to `-EPERM`, not `-EEXIST`.
9. `Unknown syscall: N` cluster in `musl_readdir` (8, 15, 17).
10. `musl_sh` echoes garbage on backspace.
11. busybox ash's line editor emits ANSI escapes the console
    prints literally.
12. `musl_wait`'s WNOHANG loop spins.
13. `f_stat_with_retry` collapses path components for nonexistent
    directories (theoretical).

## Deferred cleanups

- Audit `puthex`/`put_dec` helpers in `userland/musl/tests/`.
- Remove `PMM_ALLOC_DIAG` from `pmm.c`.
- Small `REBOOT.ELF` (raw `syscall(503)`).
