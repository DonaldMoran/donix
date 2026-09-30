## Open issues

### Open

1. **VFS layer (eventual).**  `sys_execve`'s three-attempt path
   resolution and `resolve_against_cwd` are both shims.  When a VFS
   lands, delete them; do not extend.

2. **Redirection of a builtin is silently ignored.**  `musl_sh`'s
   builtins (`cd`, `pwd`) run in the parent, before any fork, so
   there is no child to install a redirected fd into.  `cd /bin >
   log` runs `cd`, drops the `>` and `log` as ordinary argv the
   builtin ignores, creates no `log`, and prints no error.  Same for
   `<` and `>>`.

   Verified (session 37):
   - `cd / > log` — no output, no error, no file; `cd` succeeded.
   - `pwd > log` — prints `/` to the screen, not into `log`.
   - `ls` shows no `log`; `cat log` fails with `cannot open`.

   A loud failure would be better than silence; so would actually
   redirecting the builtin (which needs an fd-save / fd-restore dance
   in the parent, not a child fork).  Not on any current path.

3. **A builtin in a pipeline is refused.**  `cd /bin | cat` prints
   `sh: builtin in pipeline not supported` and runs nothing.  A
   builtin cannot be forked without changing its meaning (`cd` in a
   pipeline would not affect the parent's cwd), and donix's builtins
   have no subshell form.  Real shells run the builtin in a
   subshell; adding that is its own change.  Deliberate limitation.

4. **`sys_fcntl` refuses fd < 3 for subcommands other than
   `F_DUPFD`/`F_DUPFD_CLOEXEC`.**  Deliberate: `F_GETFL`, `F_SETFL`,
   `F_GETFD`, `F_SETFD` are not meaningful on a console sentinel.
   Linux does allow e.g. `fcntl(0, F_GETFL, ...)` on a redirected fd;
   donix returns `EBADF` there.  Not currently on any path, and
   relaxing it is a small extension of the session-34 work rather
   than a new problem.  Track here so it is not rediscovered.

5. **`rename(2)` does not replace; `chmod`/`ln`/`mount` need their
   own syscalls.**  Deliberate FatFs-limitation first cuts.

6. **`-EPIPE` is delivered without `SIGPIPE`.**  `sys_write` on a
   pipe with no reader returns `-EPIPE` (32), matching Linux's
   errno.  Real Linux *also* raises `SIGPIPE` first, which by default
   terminates the process before `write` returns.  donix's signal
   path is a stub (`sys_rt_sigaction` returns 0 and installs
   nothing), so the signal is not delivered and the process sees the
   errno instead of dying.  A program that checks `write`'s return
   value sees the right answer; a program that relies on dying from
   `SIGPIPE` does not.

   This gap is narrower than `v0.6.6`'s docs suggested.  Session 37
   ran the case the old docs named as the poster child —
   `busybox yes | busybox head -n 1` — and it does **not** hang:
   `head` prints `y` and exits, `yes` gets `-EPIPE`, handles it,
   prints `yes: Broken pipe`, and exits.  busybox apps generally
   check `write`'s return value, so the common pipelines are fine.
   The remaining exposure is a program that expects to be *killed*
   by `SIGPIPE` and does not check `write` — none has been found.

   Fixing this means implementing signal delivery: a real
   `sys_rt_sigaction`, per-process signal handlers, and a `SIGPIPE`
   raise on the `-EPIPE` write path.  That is a subsystem, not a
   small change, and out of scope for the pipe work.

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

- **The old `musl_sh` ash-only caveats are gone.**  Through
  `v0.6.6`, `donix>` did not parse `<`, `>`, `|`, `&&`, `;`, or
  quoting, so redirection and pipeline tests had to run from ash.
  Session 37 closed that gap.  Any test that was "ash-only for
  `musl_sh` reasons" is now valid from `donix>` as well.  (`busybox
  sh` and the boot ash still exist and still work; they are just no
  longer *required* for redirection or pipeline tests.)

- **Pipe regression suite is not a canary.**  `pipe_step1` …
  `pipe_step3b` fork and take seconds; run them when changing
  `sys_read`/`sys_write`/`sys_close`/`put_file_slot`/`sys_fork`/
  `sys_pipe` or adding a `FILE_KIND_*`, but not as part of the boot
  canary.
