### Open

1. **VFS layer (eventual).**  `sys_execve`'s two remaining path
   attempts — the path as given, and the `"0:"` prefix translation
   for an absolute path — and `resolve_against_cwd` are all shims.
   When a VFS lands, delete them; do not extend.  (The bare-name
   attempt was removed in session 42; see `session-log.md`.  A VFS
   is on the critical path for donix generally, **not** for
   Wayland -- see `ROADMAP.md`.)

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

8. **`vmm_map_page_in_cr3` and `vmm_map_page` silently abandon a
   mapping when a page-table allocation fails.**  Both functions
   walk the page tables and, at each level, call
   `pmm_alloc_page_for_tables()` if the next table does not exist.
   Every one of those calls is followed by `if (!phys) return;` —
   the function gives up without mapping anything, and **the caller
   has no way to know.**  `elf_load_into_process`, `sys_brk`,
   `sys_mmap`, and `sys_execve` all call these and treat a return
   as "the mapping was made."

   The huge-page-split path in `vmm_map_page_in_cr3` was the first
   of these to be fixed (session 42, tag `20261001-splitfix`),
   because it was the one with evidence: a silent return there
   leaves the bootloader's 2 MB **supervisor** identity-map page in
   place where the caller asked for a **user** page, and the process
   later faults on a user instruction fetch of `0x400000` —

       === PAGE FAULT (#PF) ===
         CR2 (Faulting Address) : 0x0000000000400000
         Raw Error Code         : 0x0000000000000015
         pde                    : 0x0000000000400083
         PDE IS 2 MB PAGE, phys base 0x400000

   `0x400083` is present, write, PS, `PT_USER` clear; `0x15` is
   present + read + user + instruction-fetch.  The split path now
   halts with a `VMM: FATAL` message instead of returning.

   **The remaining sites are still silent.**  In
   `vmm_map_page_in_cr3`: the PDPT, PD, and PT allocation paths each
   have their own `if (!phys) return;`.  In `vmm_map_page`: the same
   three.  None of them has produced a visible bug yet — a failed
   allocation there leaves a not-present page rather than a
   supervisor one, and the caller faults on a *missing* page, which
   is at least the right kind of fault — but the pattern is the
   same defect: **a void function that cannot report failure, called
   by code that assumes success.**

   The fix is one of:
   - **change the signature** to return an error, and check it at
     every caller (the honest fix, and the larger one — it touches
     every `vmm_map_page*` call site in the tree); or
   - **halt on failure at each site**, as the split path now does
     (matches the kernel idiom, smaller, but turns a recoverable
     allocation failure into a dead machine).

   Not urgent: allocation failure during page-table growth has not
   been observed on a 128 MB machine with the current workload.  But
   it is a real gap, it is the same shape as the bug we just fixed,
   and it should be closed deliberately rather than one site at a
   time as each produces its own confusing fault.

   **Read `gotchas.md`, "A shim's dead code is only dead if you
   watch it not run," together with this.**  That entry is about a
   deletion whose consumers were not all found; this is about a
   *failure path* whose consumers do not check.  Both are
   "correct only for the cases known at the time."

Also open: Ctrl- `[` not mapped to ESC; `sys_munmap` is a stub
returning 0; `sys_brk`'s fixed `heap_base` and the 4 MB mmap window
are latent collisions; real FatFs timestamp storage (the three
timestamp syscalls return 0 without storing); `prctl` is minimal
(`PR_SET_NAME` accepted and dropped); busybox applet symlinks not
installed; syscall-table audit script; `musl_wait`'s WNOHANG loop
spins (pre-existing; the spin's wall-clock duration increased
between `v0.6.6` and `v0.6.7`, when the console changed from VGA
text to framebuffer — see `session-log.md`, session 41, for the
bisect); `sys_mmap` rejects all non-anonymous mappings (a
file-backed `mmap` caller will get `-ENOMEM` and must fall back to
`read`; **a Wayland prerequisite -- see `ROADMAP.md`**); **pipes
support one concurrent reader and one concurrent writer** (see
`pipe_t`'s comment in `user_syscall.c` — a second blocked reader on
the same pipe end has nowhere to record itself and will only wake
on a keyboard IRQ); **`put_file_slot`'s pipe wake is coupled to
`sys_close`'s wake** (if `sys_close`'s wake is ever removed on the
theory that `put_file_slot` covers everything, non-final closes in
a `dup`'d chain stop waking the peer and the peer hangs until a
keystroke — read the `put_file_slot` comment and this entry before
touching either).

  **`vmm_map_page_in_cr3`/`vmm_map_page` still silently return
  without mapping when a page-table allocation fails** (the
  split-path instance is fixed; see item 8).

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
  `unlinkat` family, or `unlink_body`.  `at_step1` (session 39,
  extended in session 41) is read-only and is the analogous suite
  for `resolve_at`, the stat family, `faccessat`, and `utimensat`.

- **`at_step1` sections, as of session 41.**  Sections 1–7 exercise
  `resolve_at` via dirfd, `fstatat` flags, and `AT_EMPTY_PATH`.
  Section 8–9 exercise `faccessat` dirfd resolution and the
  `AT_FDCWD` control.  Section 11 exercises `utimensat` dirfd
  resolution.  (There is no section 10; it was removed — it tested a
  kernel-side flag check that does not exist, because musl returns
  the `EINVAL` itself.  See `gotchas.md`, session 41.)

- **The canary is now `canary`, a program.**  Session 42 replaced
  the hand-typed list with `userland/musl/tests/canary.c`: it runs
  every non-interactive canary row, checks exit status and output
  substrings, and reports pass/fail.  `canary` (read-only) and
  `canary --full` (also the mutating rows).  Run from `donix>` or
  from ash; both search lists find `/usr/bin/CANARY`.  The rows it
  does not cover (interactive `busybox ash`, `vi`) stay manual and
  are printed at the end of a run.
