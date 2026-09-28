Full list.  `handoff.md` carries the top 5.  Priority order,
highest first.

1. `geteuid(2)` (syscall 107) not implemented -- busybox ash calls
   it once at startup; the kernel logs `Unknown syscall: 107`.
   Session 24 masked this by (incorrectly) implementing setsid at
   107; tag `20260928-05` moved setsid to 112 and re-exposed the
   gap.  Fix: trivial -- return a fixed uid.  Session 30 item 1.
2. `chdir(2)` (syscall 80) not implemented -- busybox ash's `cd`
   calls it and gets `ENOSYS`, printing `Function not
   implemented`.  Minimal first cut: `cwd` field on `pcb_t`,
   `sys_chdir` validating with `f_stat_with_retry` + `AM_DIR`,
   `sys_getcwd` returning the stored path.  Full relative-path
   threading through `sys_open`/`sys_stat`/`sys_access`/
   `sys_execve` is a follow-up.  Session 30 item 2.
3. The user-mode `#PF` kill path (`fault_kill_current(0x0E)` in
   `isr14_handler`, tag `20260928-04`) is in but unverified
   end-to-end -- needs a test binary that dereferences a bad
   pointer without setting `g_expect_fault`.
4. **VFS layer (eventual).**  `sys_execve` resolves paths through
   a three-attempt block in the kernel: attempt (b) prepends `0:`
   to a leading-slash path; attempt (c) tries `0:/NAME.ELF`
   (root), then `0:/BIN/NAME` and `0:/BIN/NAME.ELF` (/bin), via
   the helpers `exec_resolve_bare_name` and
   `exec_resolve_bin_name`.  This is a stand-in for a virtual
   filesystem layer that donix does not have: on real Unix,
   `execve` hands the path to the VFS, which resolves it against
   the root, and there is no guessing and no retry.

   **When a VFS lands, DELETE the whole block** and call the VFS
   resolver once.  **Do not add a fourth attempt.**  `/bin` is
   the only hardcoded directory today; if `/sbin` or `/usr/bin`
   ever comes up, that is the signal to build the VFS, not to
   add another sub-attempt.

   Also note: `exec_resolve_bin_name` uppercases names before
   lookup, relying on FAT's case-insensitivity.  A case-sensitive
   VFS will need a real decision there.

   See the VFS SHIM comment in `sys_execve` for the same note
   in-source.  Session 29 (tag `20260928-14`).
5. Busybox applet symlinks are not installed on the FAT volume.
   busybox is a single multicall binary at `/bin/busybox`;
   standalone-shell mode (session 29) side-steps this for applets
   by running them in-process.  Symlinks are still relevant if a
   future test wants `/bin/NAME` to exist as a real file for an
   external program, or if `FEATURE_SH_STANDALONE` is ever turned
   off.  Options: install symlinks on the image
   (`CONFIG_INSTALL_APPLET_SYMLINKS` is set but the symlinks are
   not created on the FAT volume), or keep relying on standalone
   mode.
6. Audit the whole syscall table against the canonical Linux x86_64
   table.  `mkdir` (7 -> 83) and `setsid` (107 -> 112) were found by
   accident; there may be others.  A script that diffs `syscall.h`
   against `syscall_64.tbl` would make this a canary-able check
   instead of a manual pass.
7. Fork is O(~6 MB) per call -- real COW is the long-term fix.
8. `sys_newfstatat` (262) not implemented.
9. `sys_munmap` is a stub returning 0.
10. `sys_brk` uses a fixed `heap_base = 0x8000200000`.
11. The mmap window is a fixed 4 MB.
12. `sys_open` accepts non-directories with `O_DIRECTORY`.
13. `Unknown syscall: N` cluster in `musl_readdir` (8, 15, 17).
14. `musl_sh` echoes garbage on backspace.
15. busybox ash's line editor emits ANSI escapes the console
    prints literally.  (May be resolved by the session-26 VGA
    subset work -- verify before removing.)
16. `musl_wait`'s WNOHANG loop spins.
17. `f_stat_with_retry` collapses path components for nonexistent
    directories (theoretical).
18. Bare `sh` does not resolve from `donix>`.  `exec_resolve_
    bare_name` produces `0:/SH.ELF` and `exec_resolve_bin_name`
    produces `0:/BIN/SH`, neither of which exists; the binary is
    `/bin/busybox`, not `/bin/sh`.  Workaround: `busybox sh` or
    `/bin/busybox sh`.  A real fix is either an applet symlink
    (item 5) or a shell-level special case; not urgent.  Session
    29 noted.

## Deferred cleanups

- Audit `puthex`/`put_dec` helpers in `userland/musl/tests/`.
- Remove `PMM_ALLOC_DIAG` from `pmm.c`.
- Small `REBOOT.ELF` (raw `syscall(503)`).
