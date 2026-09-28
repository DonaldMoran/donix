Full list.  `handoff.md` carries the top 3.  Priority order,
highest first.

1. `geteuid(2)` (syscall 107) not implemented -- busybox ash calls
   it once at startup; the kernel logs `Unknown syscall: 107`.
   Session 24 masked this by (incorrectly) implementing setsid at
   107; tag `20260928-05` moved setsid to 112 and re-exposed the
   gap.  This is now the last remaining gap from session 27's
   syscall-number audit -- `poll(2)` at 7 was closed in session 28
   (tag `20260928-08`).  Fix: trivial -- return a fixed uid.
2. The user-mode `#PF` kill path (`fault_kill_current(0x0E)` in
   `isr14_handler`, tag `20260928-04`) is in but unverified
   end-to-end -- needs a test binary that dereferences a bad
   pointer without setting `g_expect_fault`.
3. Busybox applet symlinks are not installed on the FAT volume.
   busybox is a single multicall binary (`BUSYBOX.ELF`); applets
   like `mkdir`, `ls` are reached as `busybox <applet>` but not as
   bare commands from ash, which fails with `<applet>: not found`
   because ash's exec-based command lookup finds no file of that
   name.  Options: install symlinks on the image
   (`CONFIG_INSTALL_APPLET_SYMLINKS` is set but the symlinks are
   not created on the FAT volume), or enable
   `FEATURE_SH_STANDALONE`.
4. Audit the whole syscall table against the canonical Linux x86_64
   table.  `mkdir` (7 -> 83) and `setsid` (107 -> 112) were found by
   accident; there may be others.  A script that diffs `syscall.h`
   against `syscall_64.tbl` would make this a canary-able check
   instead of a manual pass.
5. Fork is O(~6 MB) per call -- real COW is the long-term fix.
6. `sys_newfstatat` (262) not implemented.
7. `sys_munmap` is a stub returning 0.
8. `sys_brk` uses a fixed `heap_base = 0x8000200000`.
9. The mmap window is a fixed 4 MB.
10. `sys_open` accepts non-directories with `O_DIRECTORY`.
11. `Unknown syscall: N` cluster in `musl_readdir` (8, 15, 17).
12. `musl_sh` echoes garbage on backspace.
13. busybox ash's line editor emits ANSI escapes the console
    prints literally.  (May be resolved by the session-26 VGA
    subset work -- verify before removing.)
14. `musl_wait`'s WNOHANG loop spins.
15. `f_stat_with_retry` collapses path components for nonexistent
    directories (theoretical).

## Deferred cleanups

- Audit `puthex`/`put_dec` helpers in `userland/musl/tests/`.
- Remove `PMM_ALLOC_DIAG` from `pmm.c`.
- Small `REBOOT.ELF` (raw `syscall(503)`).
