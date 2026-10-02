### Open

1. **The path shims are what the dispatch seam subsumes.**  Not a
   scheduled "VFS" -- see `ROADMAP.md`, "Make `/proc` possible."
   `sys_execve`'s two remaining path attempts (the path as given,
   and the `"0:"` prefix translation for an absolute path) and
   `resolve_against_cwd` are all shims.  When the dispatch seam
   lands for `/proc`, these are its first migration: delete them,
   do not extend them.  (The bare-name attempt was removed in
   session 42; see `session-log.md`.  A VFS is on the critical
   path for donix generally, **not** for Wayland -- see
   `ROADMAP.md`.)

2. **Redirection of a builtin is silently ignored.**  `musl_sh`'s
   builtins (`cd`, `pwd`) run in the parent, before any fork, so
   there is no child to install a redirected fd into.  `cd /bin >
   log` runs `cd`, drops the `>` and `log` as ordinary argv the
   builtin ignores, creates no `log`, and prints no error.  Same for
   `<` and `>>`.

   Verified (session 37):
   - `cd / > log` â€” no output, no error, no file; `cd` succeeded.
   - `pwd > log` â€” prints `/` to the screen, not into `log`.
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
   ran the case the old docs named as the poster child â€”
   `busybox yes | busybox head -n 1` â€” and it does **not** hang:
   `head` prints `y` and exits, `yes` gets `-EPIPE`, handles it,
   prints `yes: Broken pipe`, and exits.  busybox apps generally
   check `write`'s return value, so the common pipelines are fine.
   The remaining exposure is a program that expects to be *killed*
   by `SIGPIPE` and does not check `write` â€” none has been found.

   Fixing this means implementing signal delivery: a real
   `sys_rt_sigaction`, per-process signal handlers, and a `SIGPIPE`
   raise on the `-EPIPE` write path.  That is a subsystem, not a
   small change.  **Same subsystem a Wayland `wl_shm` client needs
   for `SIGBUS` on buffer overrun** â€” see `ROADMAP.md`.  Doing it
   once serves both.

6. **`unlinkat` (263) has no consumer in busybox as configured.**
   Implemented (session 40), correct, and tested by `at_step2.c`,
   but a tree-wide grep for `unlinkat` in `third_party/busybox`
   returns nothing.  busybox `rm -r` uses `lstat` + `unlink` +
   `rmdir` with constructed path strings (`libbb/remove_file.c`);
   `find` recurses with `openat` + `newfstatat` but removes
   nothing.  `unlinkat` is part of the `*at` family and is correct
   to have â€” it will serve the first tool that walks a directory and
   removes entries relative to a dirfd â€” but nothing in the current
   applet set calls it.  See `gotchas.md`, "A consumer inferred
   from behavior is not a consumer."

7. **`vmm_map_page_in_cr3` and `vmm_map_page` silently abandon a
   mapping when a page-table allocation fails.**  Both functions
   walk the page tables and, at each level, call
   `pmm_alloc_page_for_tables()` if the next table does not exist.
   Every one of those calls is followed by `if (!phys) return;` â€”
   the function gives up without mapping anything, and **the caller
   has no way to know.**  `elf_load_into_process`, `sys_brk`,
   `sys_mmap`, and `sys_execve` all call these and treat a return
   as "the mapping was made."

   The huge-page-split path in `vmm_map_page_in_cr3` was the first
   of these to be fixed (session 42, tag `20261001-splitfix`),
   because it was the one with evidence: a silent return there
   leaves the bootloader's 2 MB **supervisor** identity-map page in
   place where the caller asked for a **user** page, and the process
   later faults on a user instruction fetch of `0x400000` â€”

   ```
       === PAGE FAULT (#PF) ===
         CR2 (Faulting Address) : 0x0000000000400000
         Raw Error Code         : 0x0000000000000015
         pde                    : 0x0000000000400083
         PDE IS 2 MB PAGE, phys base 0x400000
   ```

   `0x400083` is present, write, PS, `PT_USER` clear; `0x15` is
   present + read + user + instruction-fetch.  The split path now
   halts with a `VMM: FATAL` message instead of returning.

   **The remaining sites are still silent.**  In
   `vmm_map_page_in_cr3`: the PDPT, PD, and PT allocation paths each
   have their own `if (!phys) return;`.  In `vmm_map_page`: the same
   three.  None of them has produced a visible bug yet â€” a failed
   allocation there leaves a not-present page rather than a
   supervisor one, and the caller faults on a *missing* page, which
   is at least the right kind of fault â€” but the pattern is the
   same defect: **a void function that cannot report failure, called
   by code that assumes success.**

   The fix is one of:
   - **change the signature** to return an error, and check it at
     every caller (the honest fix, and the larger one â€” it touches
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

8. **Symlinks: recorded design, not scheduled.**  FAT16 has no
   native symlink storage, and donix is committed to FAT.  The
   correct frame is therefore **Unix semantics, not FAT storage**:
   the question is not "how does FAT store a symlink" but "what
   should POSIX userspace observe."  If `ln -s`, `readlink`,
   `realpath`, and archive restoration all behave correctly,
   the kernel is semantically correct; the on-disk encoding is an
   implementation detail.

   The encoding is a magic marker in an ordinary file â€” e.g.
   `DONIX_LINK:/usr/bin/busybox` â€” hidden **entirely inside the
   pathname dispatch seam**.  This is "contained ugly": the same
   category as ext4's inline symlinks or btrfs's extent-based ones,
   and Linux likewise hides filesystem-specific ugliness behind
   its VFS.

   **Do not implement standalone symlink handling in individual
   syscalls** (`open`, `stat`, `lstat`, `execve`, `chdir`,
   `access`).  That is the technical debt the dispatch seam exists
   to prevent.  Symlinks are a **consumer** of the seam, scheduled
   after it exists -- see `ROADMAP.md`.

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
text to framebuffer â€” see `session-log.md`, session 41, for the
bisect); `sys_mmap` rejects all non-anonymous mappings (a
file-backed `mmap` caller will get `-ENOMEM` and must fall back to
`read`; **a Wayland prerequisite -- see `ROADMAP.md`**); **pipes
support one concurrent reader and one concurrent writer** (see
`pipe_t`'s comment in `user_syscall.c` â€” a second blocked reader on
the same pipe end has nowhere to record itself and will only wake
on a keyboard IRQ); **`put_file_slot`'s pipe wake is coupled to
`sys_close`'s wake** (if `sys_close`'s wake is ever removed on the
theory that `put_file_slot` covers everything, non-final closes in
a `dup`'d chain stop waking the peer and the peer hangs until a
keystroke â€” read the `put_file_slot` comment and this entry before
touching either).

**no `/dev` and no `/proc`** â€” `ttyname(3)` cannot name the
console, so `tty` prints `not a tty`.  `readlink` (89) is
implemented (session 42), so the `ttyname` fast path
(`readlink("/proc/self/fd/N")`) no longer logs
`Unknown syscall: 89` â€” and it now returns `-ENOENT` for a
missing path and `-EINVAL` for one that exists, verified by
`readlink_errno.c` (session 43) â€” but the fallback walk of `/dev`
still finds nothing, because `/dev` does not exist.

**The user-visible consequence is sharper than "no device
files."  `2>/dev/null` does not merely fail to discard stderr â€”
it prevents the command from running.**  Session 43, testing
`realpath`:

    $ realpath /no/such/dir/file 2>/dev/null
    sys_open: f_open FAIL path=dev/null flags=0x0000000000008241 ...
    sh: can't create /dev/null: nonexistent directory

    (realpath's own diagnostic never appears â€” the command did
     not execute)

The shell opens the redirect target *before* forking, the open
fails, and the whole command is abandoned.  A script that relies
on `2>/dev/null` to run something and silence its noise will find
the *something* never ran.

**`/dev/null` is handled on every path syscall that matters**
(session 43, `20261002-dev-null` and `20261002-dev-null-stat`).
`path_is_devnull` -- one predicate, three call sites -- is
consulted by `open_resolved`, `stat_resolved`, and
`access_resolved`:

    $ realpath /no/such/dir/file 2>/dev/null
    $ echo $?
    1

Silent, non-zero.  And the path is now visible to the rest of the
system:

    $ ls /dev/null
    /dev/null
    $ ls -l /dev/null
    crw-rw-rw-    1    0,   0 /dev/null
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

**The boundary is deliberate.**  This is an exact-path predicate,
not a `/dev` backend and not first-component dispatch.  What it
does NOT cover:

- **`readdir` on `/dev`.**  There is no `/dev` directory to list;
  `readdir("/dev")` fails with `ENOENT`.
- **Any other device.**  `/dev/tty` and `/dev/urandom` do not
  exist, so `ttyname(3)`'s fallback walk of `/dev` still finds
  nothing and `tty` still prints `not a tty`.

The predicate and its three call sites become the dispatch seam's
first `/dev` consumer when `/proc` forces the seam; they are
deleted with it.  See `ROADMAP.md`, "Make `/proc` possible."

The `sys_open` failure path shows `dev/null`, not `/dev/null` â€”
the leading slash is stripped by the `0:` translation shim (item
1) before FatFs sees it.

**This is item 1's customer and the reason the dispatch seam gets
built** â€” see `ROADMAP.md`, "Make `/proc` possible."  A device
layer plus `/dev` entries would let `tty` print a path, and is the
prerequisite for anything wanting `/dev/null`, `/dev/tty`, or
`/dev/urandom`.

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

- **Pipe regression suite is not a canary.**  `pipe_step1` â€¦
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

- **`at_step1` sections, as of session 41.**  Sections 1â€“7 exercise
  `resolve_at` via dirfd, `fstatat` flags, and `AT_EMPTY_PATH`.
  Section 8â€“9 exercise `faccessat` dirfd resolution and the
  `AT_FDCWD` control.  Section 11 exercises `utimensat` dirfd
  resolution.  (There is no section 10; it was removed â€” it tested a
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
