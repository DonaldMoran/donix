Full list.  `handoff.md` carries the top 5.  Priority order,
highest first.  Items marked **[v0.6.4]** are on the current
milestone's basics list.

1. **[v0.6.4]** The user-mode `#PF` kill path
   (`fault_kill_current(0x0E)` in `isr14_handler`) is in but
   unverified end-to-end -- needs a test binary that dereferences
   a bad pointer without setting `g_expect_fault`.
2. **[v0.6.4]** `cd ..` at `donix>` fails.  The `musl_sh` `cd`
   builtin passes the raw `..` to `chdir`, and FatFs has no `..`
   directory entry, so `sys_chdir`'s `f_stat_with_retry("..")`
   returns ENOENT.  **`cd ..` inside ash works** (ash resolves
   `..` against its own `$PWD` before calling `chdir`).  Fix
   options: (a) `builtin_cd` resolves `.`/`..` against `getcwd()`
   before calling `chdir`; (b) `sys_chdir` resolves through
   `resolve_against_cwd` the way `sys_open`/`sys_stat`/
   `sys_access` now do.  (b) is more Unix-shaped -- chdir should
   accept relative paths like every other path syscall.
   Session 30.
3. **[v0.6.4]** `unlink` / `rm` are missing.  `SYS_UNLINK` (87)
   has no dispatch case, and `CONFIG_RM` is off in
   `configs/busybox.config`.  FatFs has `f_unlink`; the work is
   the dispatch case plus turning the applet back on.  Without
   this, nothing on the image can be removed.
4. **[v0.6.4]** `rmdir` is missing.  `SYS_RMDIR` (84) has no
   dispatch case, and `CONFIG_RMDIR` is off -- deliberately, in
   session 31, because shipping the applet without the kernel
   side would produce `Unknown syscall:` noise and a `rmdir`
   that always fails.  Same shape as `unlink`: FatFs `f_unlink`
   plus an empty-directory check.  Do after `unlink` so the
   pattern is established.
5. **VFS layer (eventual).**  `sys_execve` resolves paths through
   a three-attempt block in the kernel (attempt b: leading `/` ->
   `0:` + path; attempt c: `0:/NAME.ELF`, `0:/BIN/NAME`,
   `0:/BIN/NAME.ELF`).  This is a stand-in for a virtual
   filesystem.  **When a VFS lands, DELETE the block** and call
   the VFS resolver once.  Do not add a fourth attempt.  Also:
   `resolve_against_cwd` (used by `sys_open`/`sys_stat`/
   `sys_access`) is part of the same shim -- it should become a
   VFS path-resolution step, not a per-syscall helper.
6. `newfstatat` (262): `SYS_NEWFSTATAT` is defined but no
   `case` exists in `syscall_dispatch`.  musl routes `fstatat`
   through `stat`/`lstat` on x86_64 for the common case, so it is
   not hit yet; a caller passing `AT_FDCWD` plus flags would
   reach it.
7. Busybox applet symlinks are not installed on the FAT volume.
   Standalone-shell mode side-steps this for applets; symlinks
   are still relevant for `/bin/NAME` as a real file.
8. Syscall-table audit script.  The table in `include/syscall.h`
   was audited against `syscall_64.tbl` in session 30 and is
   correct; a script would keep it correct.  (`mkdir` at 7 and
   `setsid` at 107 were found by accident; the point is to not
   need accidents.)
9. Fork is O(~6 MB) per call -- real COW is the long-term fix.
10. `sys_munmap` is a stub returning 0.
11. `sys_brk` uses a fixed `heap_base = 0x8000200000`.
12. The mmap window is a fixed 4 MB.
13. `sys_open` accepts non-directories with `O_DIRECTORY`.  In
    the `wants_dir` branch, check `fattrib & AM_DIR` and return
    `-ENOTDIR` when the target is a file.  Latent today, but
    correct to close.
14. Real FatFs timestamp storage.  The three timestamp syscalls
    added in session 31 (`utimes`, `futimesat`, `utimensat`)
    return 0 without storing anything.  Enough for `touch` and
    vi's save path; not enough for a tool that reads timestamps
    back.  FatFs stores modification time in a coarse
    two-second field that donix does not currently write.
15. `musl_sh` echoes garbage on backspace.
16. `musl_wait`'s WNOHANG loop spins.
17. `f_stat_with_retry` collapses path components for nonexistent
    directories (theoretical).
18. `prctl(2)` (157) is minimal: `PR_SET_NAME` accepted and
    dropped.  A Unix-shaped implementation would add a
    per-process `comm` field.  Session 30.
19. - **[v0.6.4]** `sys_unlink` and `sys_mkdir` do not resolve relative
    paths against the cwd.  Both call `strip_dot_prefix` but not
    `resolve_against_cwd`, unlike `sys_open` / `sys_stat` /
    `sys_access`.  So `rm foo.txt` or `mkdir foo` in a non-root cwd
    resolves against the FAT root, not the cwd.  The fix is to add
    the same `resolve_against_cwd` call the other three path syscalls
    make, before `strip_dot_prefix`.  Fold into the `rm`/`unlink`
    work: turn on `CONFIG_RM`, add cwd resolution to both handlers,
    test with `cd /bin; mkdir x; rmdir`-style paths.  (Found session
    31 while auditing comments.)
  
## Constraints to remember (not work items)

- **`VGA_REPLY_TO_QUERIES` is 0.**  If
  `CONFIG_FEATURE_VI_ASK_TERMINAL` is turned on in the busybox
  config, vi will send `ESC[6n` at startup and wait for a reply.
  The tunable must go back to 1 first, or vi stalls at launch.
  `VGA_TRACE_UNHANDLED` is also 0; set it to 1 when diagnosing a
  new curses program and back to 0 before tagging.
- **`CONFIG_RMDIR` is off deliberately.**  See item 4.  Do not
  turn it on until `SYS_RMDIR` is dispatched.

## Deferred cleanups

- Audit `puthex`/`put_dec` helpers in `userland/musl/tests/`.
- Remove `PMM_ALLOC_DIAG` from `pmm.c`.
- Small `REBOOT.ELF` (raw `syscall(503)`).
- Delete `~/code/x` and `~/code/y` (both fully ported).
- Add session-30 and session-31 gotchas to `docs/gotchas.md`.
  Session 30: the `sys_getcwd` absolute-cwd requirement, and the
  `puts_raw`-vs-`printf` newline quirk.  Session 31: the
  `open(2)` flag-bit mismatch (`O_CREAT=0x40`, not `0x200`;
  `O_TRUNC=0x200`, not `0x400`; `O_APPEND=0x400`, not `0x8`);
  the ESC-key scancode table gap (`scancode_ascii[0x01]` was
  unassigned, so ESC produced 0 and was dropped by the buffer's
  NUL guard); and the alt-screen-cannot-be-a-pointer-swap fact
  on VGA text mode.
