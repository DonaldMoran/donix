## Open issues

### Resolved in session 34

- **`uniq` hangs.**  Not an `uniq` bug.  `alloc_file_slot` started at
  fd 3, so `uniq`'s `close(0); open(file)` idiom landed the file on
  fd 3 and `read(0, ...)` blocked on the keyboard.  Fixed by console
  sentinels, lowest-free-fd `open`, and relaxing the `sys_dup2` /
  `sys_fcntl(F_DUPFD)` fd<3 guards.  See `gotchas.md`, "Low fds
  (0/1/2) are first-class."  Commit `20260930-low-fd-io`.

### Resolved in session 36 (v0.6.6)

- **`pipe(2)` absent — `|` does not work in any shell.**  Implemented
  as a 4 KB ring buffer shared by a read end and a write end, with
  blocking `read`/`write`, a directed wake (`reader_waiting` /
  `writer_waiting` on the pipe, not a broadcast), EOF when the last
  writer closes, `-EPIPE` when the last reader closes, and a wake on
  the process-exit path so a writer that `_exit`s without closing does
  not strand its reader.  The `sys_read`/`sys_write` stdio guards were
  inverted so a pipe `dup2`'d onto fd 0/1 is routed as a real fd, not
  as the console.  Verified: `cat hello-world.txt | head -n 2`,
  `echo hi | wc`, `echo hello | cat`, all from ash.  See
  `docs/gotchas.md`, "Negative fd-kind tests don't extend to new
  kinds."  Five commits, all in `v0.6.6`; see `docs/session-log.md`
  for the table.

- **`dup(2)` (syscall 32) missing.**  Found by `pipe_step3`'s first
  run: musl's `dup()` reaches `SYS_dup` directly, and donix had no
  handler, so `dup` returned `-ENOSYS`.  Fixed as a one-line
  delegation to `sys_fcntl(fd, F_DUPFD, 0)`.  Commit in `v0.6.6`;
  see `docs/session-log.md`.

### Open

1. **VFS layer (eventual).**  `sys_execve`'s three-attempt path
   resolution and `resolve_against_cwd` are both shims.  When a VFS
   lands, delete them; do not extend.

2. **`musl_sh` does not strip quotes or parse redirection.**
   Userland-only fix.  Headline item for `v0.6.7`.  Affects `<`,
   `>`, `|`, `&&`, `;`, and any quoting — all tests involving those
   must currently be run from ash.  A tokenizer fix, no kernel
   change.

3. **`sys_fcntl` refuses fd < 3 for subcommands other than
   `F_DUPFD`/`F_DUPFD_CLOEXEC`.**  Deliberate: `F_GETFL`, `F_SETFL`,
   `F_GETFD`, `F_SETFD` are not meaningful on a console sentinel.
   Linux does allow e.g. `fcntl(0, F_GETFL, ...)` on a redirected fd;
   donix returns `EBADF` there.  Not currently on any path, and
   relaxing it is a small extension of the session-34 work rather
   than a new problem.  Track here so it is not rediscovered.

4. **`rename(2)` does not replace; `chmod`/`ln`/`mount` need their
   own syscalls.**  Deliberate FatFs-limitation first cuts.

5. **`-EPIPE` is delivered without `SIGPIPE`.**  v0.6.6's `sys_write`
   on a pipe with no reader returns `-EPIPE` (32), matching Linux's
   errno.  Real Linux *also* raises `SIGPIPE` first, which by default
   terminates the process before `write` returns.  donix's signal
   path is a stub (`sys_rt_sigaction` returns 0 and installs
   nothing), so the signal is not delivered and the process sees the
   errno instead of dying.  A program that checks `write`'s return
   value sees the right answer; a program that relies on dying from
   `SIGPIPE` does not.  Consequence for pipelines: `yes | head -n 1`
   would, on Linux, have `yes` killed by `SIGPIPE` when `head` exits;
   on donix, `yes` gets `-EPIPE` from `write` and must handle it
   itself.  busybox's `yes` does not, because on Linux it never has
   to.  Fixing this means implementing signal delivery, which is out
   of scope for the pipe work.

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
`-ENOMEM` and must fall back to `read`); **pipes support one
concurrent reader and one concurrent writer** (see `pipe_t`'s comment
in `user_syscall.c` — a second blocked reader on the same pipe end
has nowhere to record itself and will only wake on a keyboard IRQ);
**`put_file_slot`'s pipe wake is coupled to `sys_close`'s wake** (if
`sys_close`'s wake is ever removed on the theory that `put_file_slot`
covers everything, non-final closes in a `dup`'d chain stop waking
the peer and the peer hangs until a keystroke — read the
`put_file_slot` comment and this entry before touching either).

### Test-design notes

- **`uniq -c < file` is an ash-only test.**  From `donix>` (`musl_sh`),
  the `<` is passed to `uniq` as a literal argument — `musl_sh` does
  not parse redirection — so `uniq` tries to open a file named `<`
  and fails with `can't open '<'`.  That is the `musl_sh`
  redirection gap (issue 2 above), not a `uniq` bug.  Same rule as
  the other redirect checks: run them from ash.

- **Pipeline tests are ash-only too.**  `musl_sh` does not parse
  `|` any more than it parses `<`.  A pipeline typed at `donix>`
  passes `|` to the first applet as a literal argument.  All pipe
  verification — `cat file | head`, `echo hi | wc`, `echo hello |
  cat` — must run from ash.
