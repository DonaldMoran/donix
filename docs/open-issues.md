### Open

1. **VFS layer (eventual).**  `sys_execve`'s three-attempt path
   resolution and `resolve_against_cwd` are both shims.  When a VFS
   lands, delete them; do not extend.  (A VFS is on the critical
   path for donix generally, **not** for Wayland -- see
   `ROADMAP.md`.)

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
   than a new problem.

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
   small change.  **Same subsystem a Wayland `wl_shm` client needs
   for `SIGBUS` on buffer overrun** — see `ROADMAP.md`.  Doing it
   once serves both.

7. **`unlinkat` (263) has no consumer in busybox as configured.**
   Implemented (session 40), correct, and tested by `at_step2.c`,
   but a tree-wide grep for `unlinkat` in `third_party/busybox`
   returns nothing.  busybox `rm -r` uses `lstat` + `unlink` +
   `rmdir` with constructed path strings (`libbb/remove_file.c`);
   `find` recurses with `openat` + `newfstatat` but removes
   nothing.  `unlinkat` is part of the `*at` family and is correct
   to have — it will serve the first tool that walks a directory and
   removes entries relative to a dirfd — but nothing in the current
   applet set calls it.  See `gotchas.md`, "A consumer inferred
   from behavior is not a consumer."

Also open: Ctrl- `[` not mapped to ESC; `sys_utimensat` lacks
`resolve_against_cwd`; `sys_munmap` is a stub returning 0; `sys_brk`'s
fixed `heap_base` and the 4 MB mmap window are latent collisions;
real FatFs timestamp storage (the three timestamp syscalls return 0
without storing); `prctl` is minimal (`PR_SET_NAME` accepted and
dropped); busybox applet symlinks not installed; syscall-table audit
script; `musl_wait`'s WNOHANG loop spins; `sys_mmap` rejects all
non-anonymous mappings (a file-backed `mmap` caller will get
`-ENOMEM` and must fall back to `read`; **a Wayland prerequisite --
see `ROADMAP.md`**); **pipes support one
concurrent reader and one concurrent writer** (see `pipe_t`'s comment
in `user_syscall.c` — a second blocked reader on the same pipe end
has nowhere to record itself and will only wake on a keyboard IRQ);
**`put_file_slot`'s pipe wake is coupled to `sys_close`'s wake** (if
`sys_close`'s wake is ever removed on the theory that `put_file_slot`
covers everything, non-final closes in a `dup`'d chain stop waking
the peer and the peer hangs until a keystroke — read the
`put_file_slot` comment and this entry before touching either);
**`faccessat` (269) ignores its `dirfd` and `flags`** — it is a
direct alias of `sys_access`, so `faccessat(dirfd, "rel", ...)` with
a real dirfd resolves against the cwd, not the dirfd.  Correct for
the only case that reaches it today (`AT_FDCWD`, no flags), but not
a correct alias in general; fix it with `resolve_at` if a caller
ever passes a real dirfd.

**Noted but not a bug:** busybox `vi` calls `TIOCGWINSZ` on every
keystroke (visible as a syscall per key in a trace).  This is
`FEATURE_VI_WIN_RESIZE` re-checking the size; it is `vi`'s behavior,
harmless, and the reason `vi` fills the screen.  No action.

### Test-design notes

- **The old `musl_sh` ash-only caveats are gone.**  Through
  `v0.6.6`, `donix>` did not parse `<`, `>`, `|`, `&&`, `;`, or
  quoting.  Session 37 closed that gap; redirection and pipeline
  tests are valid from `donix>` as well as ash.

- **Framebuffer / `vi` tests are one-offs, not canary rows.**  `vi`
  mutates the disk (it writes the file) and takes over the screen.
  `vi test`, `:wq`, `./test` is the round-trip check; run it by hand
  after framebuffer or console changes, not as part of the boot
  canary.

- **Pipe regression suite is not a canary.**  `pipe_step1` …
  `pipe_step3b` fork and take seconds; run them when changing
  `sys_read`/`sys_write`/`sys_close`/`put_file_slot`/`sys_fork`/
  `sys_pipe` or adding a `FILE_KIND_*`, but not as part of the boot
  canary.

- **`at_step2` is not a canary either.**  Session 40 added it: it
  creates and removes fixtures under `/`, so it **mutates the
  disk**.  Run it when changing `resolve_at`, the `unlink`/`rmdir`/
  `unlinkat` family, or `unlink_body`.  `at_step1` (session 39) is
  read-only and is the analogous suite for `resolve_at` and the
  stat family.
