## Open issues

### Resolved in session 34

- **`uniq` hangs.**  Not an `uniq` bug.  `alloc_file_slot` started at
  fd 3, so `uniq`'s `close(0); open(file)` idiom landed the file on
  fd 3 and `read(0, ...)` blocked on the keyboard.  Fixed by console
  sentinels, lowest-free-fd `open`, and relaxing the `sys_dup2` /
  `sys_fcntl(F_DUPFD)` fd<3 guards.  See `gotchas.md`, "Low fds
  (0/1/2) are first-class."  Commit `20260930-low-fd-io`.

### Open

1. **VFS layer (eventual).**  `sys_execve`'s three-attempt path
   resolution and `resolve_against_cwd` are both shims.  When a VFS
   lands, delete them; do not extend.

2. **`pipe(2)` is absent** — `|` does not work in any shell.  The
   biggest remaining gap.  Needs a pipe object, per-fd read/write
   ends, and scheduler integration for blocking on an empty/full
   pipe.

3. **`sys_fcntl` refuses fd < 3 for subcommands other than
   `F_DUPFD`/`F_DUPFD_CLOEXEC`.**  Deliberate: `F_GETFL`, `F_SETFL`,
   `F_GETFD`, `F_SETFD` are not meaningful on a console sentinel.
   Linux does allow e.g. `fcntl(0, F_GETFL, ...)` on a redirected fd;
   donix returns `EBADF` there.  Not currently on any path, and
   relaxing it is a small extension of the session-34 work rather
   than a new problem.  Track here so it is not rediscovered.

4. **`musl_sh` does not strip quotes or parse redirection.**
   Userland-only fix.

5. **`rename(2)` does not replace; `chmod`/`ln`/`mount` need their
   own syscalls.**  Deliberate FatFs-limitation first cuts.

Also open: `newfstatat` (262) reserved, no dispatch case;
`sys_open` accepts non-directories with `O_DIRECTORY`; Ctrl- `[` not
mapped to ESC; `sys_utimensat` lacks `resolve_against_cwd`;
`sys_munmap` is a stub returning 0; `sys_brk`'s fixed `heap_base` and
the 4 MB mmap window are latent collisions; real FatFs timestamp
storage (the three timestamp syscalls return 0 without storing);
`prctl` is minimal (`PR_SET_NAME` accepted and dropped); busybox
applet symlinks not installed; syscall-table audit script;
`musl_wait`'s WNOHANG loop spins; `sys_mmap` rejects all
non-anonymous mappings (a file-backed `mmap` caller will get
`-ENOMEM` and must fall back to `read`).

### Test-design notes

- **`uniq -c < file` is an ash-only test.**  From `donix>` (`musl_sh`),
  the `<` is passed to `uniq` as a literal argument — `musl_sh` does
  not parse redirection — so `uniq` tries to open a file named `<`
  and fails with `can't open '<'`.  That is the `musl_sh`
  redirection gap (issue 4 above), not a `uniq` bug.  Same rule as
  the other redirect checks: run them from ash.
