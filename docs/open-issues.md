Full list.  `handoff.md` carries the top 5.  Priority order,
highest first.

1. The user-mode `#PF` kill path (`fault_kill_current(0x0E)` in
   `isr14_handler`) is in but unverified end-to-end -- needs a test
   binary that dereferences a bad pointer without setting
   `g_expect_fault`.
2. `cd ..` at `donix>` fails.  The `musl_sh` `cd` builtin passes
   the raw `..` to `chdir`, and FatFs has no `..` directory entry,
   so `sys_chdir`'s `f_stat_with_retry("..")` returns ENOENT.
   **`cd ..` inside ash works** (ash resolves `..` against its own
   `$PWD` before calling `chdir`).  Fix options: (a) `builtin_cd`
   resolves `.`/`..` against `getcwd()` before calling `chdir`;
   (b) `sys_chdir` resolves through `resolve_against_cwd` the way
   `sys_open`/`sys_stat`/`sys_access` now do.  (b) is more
   Unix-shaped -- chdir should accept relative paths like every
   other path syscall.  Session 30.
3. **VFS layer (eventual).**  `sys_execve` resolves paths through
   a three-attempt block in the kernel (attempt b: leading `/` ->
   `0:` + path; attempt c: `0:/NAME.ELF`, `0:/BIN/NAME`,
   `0:/BIN/NAME.ELF`).  This is a stand-in for a virtual
   filesystem.  **When a VFS lands, DELETE the block** and call
   the VFS resolver once.  Do not add a fourth attempt.  Also:
   `resolve_against_cwd` (used by `sys_open`/`sys_stat`/
   `sys_access`) is part of the same shim -- it should become a
   VFS path-resolution step, not a per-syscall helper.
4. `newfstatat` (262): `SYS_NEWFSTATAT` is defined but no
   `case` exists in `syscall_dispatch`.  musl routes `fstatat`
   through `stat`/`lstat` on x86_64 for the common case, so it is
   not hit yet; a caller passing `AT_FDCWD` plus flags would
   reach it.
5. Busybox applet symlinks are not installed on the FAT volume.
   Standalone-shell mode side-steps this for applets; symlinks
   are still relevant for `/bin/NAME` as a real file.
6. Syscall-table audit script.  The table in `include/syscall.h`
   was audited against `syscall_64.tbl` in session 30 and is
   correct; a script would keep it correct.  (`mkdir` at 7 and
   `setsid` at 107 were found by accident; the point is to not
   need accidents.)
7. Fork is O(~6 MB) per call -- real COW is the long-term fix.
8. `sys_munmap` is a stub returning 0.
9. `sys_brk` uses a fixed `heap_base = 0x8000200000`.
10. The mmap window is a fixed 4 MB.
11. `sys_open` accepts non-directories with `O_DIRECTORY`.
12. `musl_sh` echoes garbage on backspace.
13. busybox ash's line editor emits ANSI escapes the console
    prints literally.  (May be resolved by the session-26 VGA
    subset work -- verify before removing.)
14. `musl_wait`'s WNOHANG loop spins.
15. `f_stat_with_retry` collapses path components for nonexistent
    directories (theoretical).
16. `prctl(2)` (157) is minimal: `PR_SET_NAME` accepted and
    dropped.  A Unix-shaped implementation would add a per-process
    `comm` field.  Session 30.

## Deferred cleanups

- Audit `puthex`/`put_dec` helpers in `userland/musl/tests/`.
- Remove `PMM_ALLOC_DIAG` from `pmm.c`.
- Small `REBOOT.ELF` (raw `syscall(503)`).
- Delete `~/code/x` and `~/code/y` (both fully ported).
- Add session-30 gotchas to `docs/gotchas.md`: the `sys_getcwd`
  absolute-cwd requirement, and the `puts_raw`-vs-`printf` newline
  quirk.
