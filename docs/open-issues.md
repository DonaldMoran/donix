Full list.  `handoff.md` carries the top 5.  Priority order,
highest first.

1. **VFS layer (eventual).**  `sys_execve` resolves paths through
   a three-attempt block in the kernel (attempt b: leading `/` ->
   `0:` + path; attempt c: `0:/NAME.ELF`, `0:/BIN/NAME`,
   `0:/BIN/NAME.ELF`).  This is a stand-in for a virtual
   filesystem.  **When a VFS lands, DELETE the block** and call
   the VFS resolver once.  Do not add a fourth attempt.  Also:
   `resolve_against_cwd` (used by `sys_open`/`sys_stat`/
   `sys_access`/`sys_chdir`/`sys_unlink`/`sys_mkdir`) is part of
   the same shim -- it should become a VFS path-resolution step,
   not a per-syscall helper.
2. `newfstatat` (262): `SYS_NEWFSTATAT` is defined but no
   `case` exists in `syscall_dispatch`.  musl routes `fstatat`
   through `stat`/`lstat` on x86_64 for the common case, so it is
   not hit yet; a caller passing `AT_FDCWD` plus flags would
   reach it.  Now that `sys_stat` resolves against cwd, this is a
   small wrapper rather than a stub.
3. `sys_open` accepts non-directories with `O_DIRECTORY`.  In
   the `wants_dir` branch, check `fattrib & AM_DIR` and return
   `-ENOTDIR` when the target is a file.  Latent today, but
   correct to close.
4. `sys_utimensat` does not resolve relative paths against the
   cwd.  It calls `strip_dot_prefix` but not
   `resolve_against_cwd`, unlike `sys_open` / `sys_stat` /
   `sys_access`.  Same gap `sys_unlink` and `sys_mkdir` had before
   session 32 fixed them.  Small; same one-line fix.  (Found
   session 32 while auditing comments; `sys_utimes` and
   `sys_futimesat` alias it, so fixing this one covers all
   three.)
5. Fork is O(~6 MB) per call -- real COW is the long-term fix.
6. `sys_munmap` is a stub returning 0.
7. `sys_brk` uses a fixed `heap_base = 0x8000200000`.
8. The mmap window is a fixed 4 MB.
9. Real FatFs timestamp storage.  The three timestamp syscalls
   (`utimes`, `futimesat`, `utimensat`) return 0 without storing
   anything.  Enough for `touch` and vi's save path; not enough
   for a tool that reads timestamps back.  FatFs stores
   modification time in a coarse two-second field that donix does
   not currently write.
10. Busybox applet symlinks are not installed on the FAT volume.
    Standalone-shell mode side-steps this for applets; symlinks
    are still relevant for `/bin/NAME` as a real file.
11. Syscall-table audit script.  The table in `include/syscall.h`
    was audited against `syscall_64.tbl` in session 30 and is
    correct; a script would keep it correct.  (`mkdir` at 7 and
    `setsid` at 107 were found by accident; the point is to not
    need accidents.)
12. `musl_sh` echoes garbage on backspace.
13. `musl_wait`'s WNOHANG loop spins.
14. `f_stat_with_retry` collapses path components for nonexistent
    directories (theoretical).
15. `prctl(2)` (157) is minimal: `PR_SET_NAME` accepted and
    dropped.  A Unix-shaped implementation would add a
    per-process `comm` field.  Session 30.
16. Ctrl- `[` does not produce ESC.  `scancode_to_ascii` has no
    fourth parameter for Ctrl state yet.  The literal ESC key is
    enough for vi, but terminal users expect the alias.

## Constraints to remember (not work items)

- **`VGA_REPLY_TO_QUERIES` is 0.**  If
  `CONFIG_FEATURE_VI_ASK_TERMINAL` is turned on in the busybox
  config, vi will send `ESC[6n` at startup and wait for a reply.
  The tunable must go back to 1 first, or vi stalls at launch.
  `VGA_TRACE_UNHANDLED` is also 0; set it to 1 when diagnosing a
  new curses program and back to 0 before tagging.
- **Path-taking syscalls call `resolve_against_cwd` then
  `strip_dot_prefix`, in that order.**  Any new path syscall must
  follow the pattern `sys_open` uses, or it will resolve relative
  paths against the FAT root instead of the cwd.  See gotchas.md.

## Deferred cleanups

- Audit `puthex`/`put_dec` helpers in `userland/musl/tests/`.
- Remove `PMM_ALLOC_DIAG` from `pmm.c`.
- Small `REBOOT.ELF` (raw `syscall(503)`).
- Delete `~/code/x` and `~/code/y` (both fully ported).
- Add session-30 gotchas to `docs/gotchas.md`: the `sys_getcwd`
  absolute-cwd requirement, and the `puts_raw`-vs-`printf` newline
  quirk.  (Session 31 and 32 gotchas are already in
  `docs/gotchas.md`.)
