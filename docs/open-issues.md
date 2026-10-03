### Open

1. **The remaining FatFs-form conversions, now that the seam has
   landed.**  The pathname dispatch seam shipped in session 44
   (six commits on `dev`, scratch-tagged `20261002-seam` through
   `20261002-execve-seam`).  `resolve_at` now dispatches by first
   component to FAT, DEV, or PROC; `resolve_against_cwd` has one
   caller (`resolve_at`); `sys_execve` resolves through
   `resolve_at`, and its unreachable `"0:"` retry is deleted.  What
   remains is **not** a shim the seam subsumes: it is the FatFs-form
   translation that belongs to the FAT caller — `strip_dot_prefix`
   (FatFs rejects a leading `/`) and, where a caller still needs it,
   a `"0:"` drive prefix.  These are the FAT backend's own business,
   not a path-resolution layer.  A future VFS would absorb them;
   nothing today should extend them.  See `ROADMAP.md`, "Make
   `/proc` possible," and `docs/strategy.md`, "When a feature may
   force architecture."

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

4. **`rename(2)` does not replace; `chmod`/`ln`/`mount` need their
   own syscalls.**  Deliberate FatFs-limitation first cuts.

5. **`-EPIPE` is delivered without `SIGPIPE`.**  `sys_write` on a
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

6. **`unlinkat` (263) has no consumer in busybox as configured.**
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

7. **The silent `vmm_map_page*` returns, and the boot-time `#PF`
   they cause.**  This is **the next session's work**, and it is
   now observed, not theoretical.

   `vmm_map_page_in_cr3` and `vmm_map_page` walk the page tables
   and, at each level, call `pmm_alloc_page_for_tables()` if the
   next table does not exist.  Every one of those calls is followed
   by `if (!phys) return;` — the function gives up without mapping
   anything, and **the caller has no way to know.**
   `elf_load_into_process`, `sys_brk`, `sys_mmap`, and `sys_execve`
   all call these and treat a return as "the mapping was made."

   **The huge-page-split path was fixed in session 42**
   (`20261001-splitfix`), because it was the one with evidence: a
   silent return there leaves the bootloader's 2 MB **supervisor**
   identity-map page in place where the caller asked for a **user**
   page, and the process later faults on a user instruction fetch
   of `0x400000`.  The split path now halts with a `VMM: FATAL`
   message instead of returning.

   **That fix did not fire on the crash session 44 saw.**  One boot
   of the session-44 tree hit the same `#PF`, at the same address,
   **with no `VMM: FATAL`**:

   ```
       === PAGE FAULT (#PF) ===
         CR2 (Bad Address) : 0x0000000000400000
         Faulting RIP      : 0x0000000000400000
         Raw Error Code    : 0x0000000000000015
         pde               : 0x0000000000400083
         PDE IS 2 MB PAGE, phys base 0x400000 -> effective phys 0x400000
   ```

   `0x83` is present, write, PS — and **`PT_USER` clear**.  `0x15`
   is present + read + user + instruction-fetch.  The capture is
   saved as **`BOOT_PF.TXT`** at the project root.

   **Two observations make it reproducible, and they are the
   useful part:**

   1. **It usually follows a rebuild that adds userland
      programs.**  The last one came after a session-44 rebuild
      that added `proc_fd.elf` and `proc_status.elf`.
   2. **After a reboot or two — by the third, say — it goes away
      and stays away for a long time.**  A fresh boot of the same
      image is usually clean.

   That combination is the signature of a **page-frame allocation
   condition**, not a logic error in the split: the split needs a
   fresh frame, the allocation's outcome depends on how many
   frames were consumed before it ran, and a reboot reinitializes
   the allocator's state so the condition clears.

   **The likely reason no `VMM: FATAL` printed:** `20261001-splitfix`
   added the halt to *one* return in the split path.  If the failure
   is a *different* silent return in the same path — one of the six
   below — the split leaves the supervisor page in place without a
   message.  **The first thing to read for is which `if (!phys)
   return;` the split takes.**

   **The remaining sites are still silent.**  In
   `vmm_map_page_in_cr3`: the PDPT, PD, and PT allocation paths each
   have their own `if (!phys) return;`.  In `vmm_map_page`: the same
   three.  The pattern is the same defect: **a void function that
   cannot report failure, called by code that assumes success.**

   The fix is one of:
   - **change the signature** to return an error, and check it at
     every caller (the honest fix, and the larger one — it touches
     every `vmm_map_page*` call site in the tree); or
   - **halt on failure at each site**, as the split path now does
     (matches the kernel idiom, smaller, but turns a recoverable
     allocation failure into a dead machine).

   **Do not tag a `v*` until this is closed.**  See `handoff.md`
   for the next session's reading list: `vmm.c`, the diff of
   `20261001-splitfix`, and `BOOT_PF.TXT`.

   **Read `gotchas.md`, "A shim's dead code is only dead if you
   watch it not run," together with this.**  That entry is about a
   deletion whose consumers were not all found; this is about a
   *failure path* whose consumers do not check.  Both are
   "correct only for the cases known at the time."

8. **Symlinks: recorded design, not scheduled — and now
   buildable.**  FAT16 has no native symlink storage, and donix is
   committed to FAT.  The correct frame is therefore **Unix
   semantics, not FAT storage**: the question is not "how does FAT
   store a symlink" but "what should POSIX userspace observe."  If
   `ln -s`, `readlink`, `realpath`, and archive restoration all
   behave correctly, the kernel is semantically correct; the
   on-disk encoding is an implementation detail.

   The encoding is a magic marker in an ordinary file — e.g.
   `DONIX_LINK:/usr/bin/busybox` — hidden **entirely inside the
   pathname dispatch seam**.  This is "contained ugly": the same
   category as ext4's inline symlinks or btrfs's extent-based ones,
   and Linux likewise hides filesystem-specific ugliness behind
   its VFS.

   **Do not implement standalone symlink handling in individual
   syscalls** (`open`, `stat`, `lstat`, `execve`, `chdir`,
   `access`).  That is the technical debt the dispatch seam exists
   to prevent.  **The seam now exists** (session 44), so this is no
   longer "scheduled after the seam" — it is buildable as the
   seam's next consumer, once the `vmm` bug is closed.

   Payoff when it lands: `ln`, `link`, `readlink` with real
   targets, `realpath` correctness, `tar`/`unzip` link restoration,
   and the quiet assumptions (`/bin/sh -> busybox`) that many
   configure scripts and build systems make.

Also open: `sys_brk`'s fixed `heap_base` and the 4 MB mmap window
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

**`readdir("/dev")` and `readdir("/proc")` fail.**  The seam
serves individual entries, not directories.  `open_resolved` has a
DEV branch (`dev_lookup`) and a PROC branch (`proc_lookup`), each a
small table keyed by the path component; there is no synthesized
directory for either prefix, so `ls /dev` and `ls /proc` fail with
`ENOENT`.  Adding them is a **directory shape**, not an
architectural change — the mechanism is proven — and it is the
piece of work that would let `readdir("/proc")` return `self`,
which is what `ps`/`top`/`kill`/`pidof` need before they can be
enabled.  See `handoff.md`, "Busybox enablement."

**`st_rdev` is 0 on device nodes.**  `ls -l /dev/console` prints
`0, 0` for the major/minor column; a real Unix prints the tty
driver's `4, 0`.  `fill_kstat_as_chardev` sets `st_dev` and
`st_ino` (needed for `ttyname_r`'s gate 3b) but not `st_rdev`.
Cosmetic: `tty` does not read it.  One line when someone wants
`ls -l /dev/...` to look right.

**`f_stat_with_retry` has dead `has_drive` at line 3077.**  It
computes `has_drive`, does nothing with it, and casts it to
`(void)`.  Same shape as the `"0:"` retry that commit 6 deleted:
code that is present, correct-looking, and has no effect.  Small
cleanup; not urgent.

**No `/dev` directory and no `/proc` directory.**  `ttyname(3)` now
names the console — `readlink("/proc/self/fd/0")` returns
`/dev/console` and the `(st_dev, st_ino)` match passes, so `tty`
prints `/dev/console`.  What remains is that **neither `/dev` nor
`/proc` is listable**: `readdir` on either fails.  `/dev/tty` and
`/dev/urandom` do not exist, and `/dev/console` is stat-able but
not openable (`open("/dev/console")` returns `-ENOENT`; nothing
opens it yet).  The seam is the mechanism; the directories and the
remaining entries are the work.

**The `/dev/null` history, kept because it is the seam's first
consumer.**  Session 43 made `/dev/null` work across
`open`/`stat`/`access` with `path_is_devnull`, an exact-path
predicate in three call sites.  Session 44's seam **deleted that
predicate** and replaced it with `g_dev_table[]` + `dev_lookup()`,
the DEV backend's first entry.  `2>/dev/null` now works — the
command runs, and stderr is discarded:

    $ realpath /no/such/dir/file 2>/dev/null
    $ echo $?
    1

Silent, non-zero.  And the path is visible:

    $ ls -l /dev/null
    crw-rw-rw-    1    0,   2 /dev/null
    $ test -e /dev/null && echo yes
    yes
    $ stat /dev/null
      File: '/dev/null'
      Size: 0    ...    character special file
      Access: (0666/crw-rw-rw-)

`open` returns a `FILE_KIND_DEV_NULL` slot: read returns 0, write
returns count, close frees.  `stat` and `fstat` agree, both
reporting `S_IFCHR | 0666`.  `readlink("/dev/null")` returns
`-EINVAL` (not a symlink) for free, via `access_resolved`.

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

- **`readlink_errno` is read-only and fast** but is run by hand,
  not as a canary row, like `at_step1` and `fcntl_lowfd`.  Run it
  when changing `sys_readlink`, the `resolve_at` family, or
  `access_resolved`.  Session 43.

- **`proc_status` and `proc_fd` are read-only and fast** (session
  44), run by hand like `at_step1`.  `proc_status` opens, reads,
  stats, and closes `/proc/self/status`, checking the five keys.
  `proc_fd` checks that `readlink("/proc/self/fd/N")` returns
  `/dev/console` for fds 0/1/2, that `stat("/dev/console")` reports
  `S_IFCHR` matching `fstat(0)` on `(st_dev, st_ino)`, and that a
  non-console fd is `-EINVAL`.  Run them when changing
  `resolve_at`, the DEV or PROC backend, `sys_readlink`, or
  `access_resolved`.  The `tty` canary row is the fast check; these
  are the detailed ones.

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
  are printed at the end of a run.  Session 44 added a `tty` row
  (`busybox tty` must print `/dev/console`), so the count is
  **15/15** read-only and **28/28** `--full`.
