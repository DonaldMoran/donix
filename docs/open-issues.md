### Open

> **Item 7 is closed and the list is not renumbered.**  Item 7 was
> the silent `vmm_map_page*` returns; session 49 fixed it (the
> signature change, the nine callers, and the `isr14_handler`
> diagnostic) and it is no longer an open issue.  The items below
> keep their original numbers, so there is a gap where 7 was.
> References to "item 7" in older docs resolve to nothing, which is
> correct: it is closed.  *(This note is scaffolding for the docs
> edited in session 49; the next documentation round removes it and
> the references it exists to satisfy.)*

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

7a. **The boot-time `#PF` at `0x400000` is back, and diagnosed
   better.**  Session 50's first boot after the fault-injection
   commit reproduced it:

       === PAGE FAULT (#PF) ===
         CR2 (Bad Address) : 0x0000000000400000
         Faulting RIP      : 0x0000000000400000
         Raw Error Code    : 0x0000000000000015
         CS                : 0x0000000000000033
         CR3               : 0x000000000030D000
         pde               : 0x0000000000400083
         PDE IS 2 MB PAGE, phys base 0x400000
       ELF: COPY-FAIL phys=0 at vaddr=0000000000400000
       PANIC: musl_sh ELF load failed

   It did **not** reproduce on the next two boots.  This is the
   shape sessions 45 and 48 described: layout-dependent, appears on
   the first boot after an image change, clears on the next.  It is
   **not** session 48's PMM zone scan (that wraps now) and **not**
   the closed item 7 (the returns are plumbed).

   **What is new is the reporting, not the fault.**  Session 45's
   version was silent: `vmm_map_page_in_cr3` returned without the
   caller knowing, and the process faulted later in user mode at
   virtual 1.  Session 49's item-7 plumbing is what makes this boot
   print `ELF: COPY-FAIL phys=0 at vaddr=0x400000` and panic *at
   the ELF load*, at the call site.  A silent failure became a
   reported one; the underlying allocation still failed.

   **Two candidate causes, not distinguished by the capture.**
   Either (a) `pmm_alloc_page` genuinely returned 0 — an
   exhaustion in a shape the session-48 wrap does not cover — or
   (b) the clone `vmm_clone_page_table` built for `musl_sh` was
   missing the PDPT or PD for `0x400000`'s region, so
   `vmm_map_page_in_cr3`'s own table allocation failed.  The log
   shows the walk against the *parent's* cr3 (`0x30D000`), whose
   PDPT/PD are present and whose PDE is the bootloader's user 2 MB
   huge page (`0x400083`); it does not show the *child's* clone.
   Distinguishing the two needs the child's cr3 walked, or a
   free-page count printed at the failure site.

   **Unobserved, not fixed** — the standing rule from session 45.
   An intermittent fault that stops reproducing is not closed.
   Next session that touches it should instrument (print which
   allocation in `vmm_map_page_in_cr3` returned 0, and
   `pmm_get_free_pages()`) rather than reason from the dump.  The
   session-45 virtual-1 fault's diagnostic is permanently in
   `isr14_handler`; this one is its sibling and wants the same
   treatment.

   **Instrumented (session 51), not fixed.**  The three silent
   `return -1;` sites in `vmm_map_page_in_cr3` — PDPT, PD, and the
   final PT after the huge-page split — now each print `VMM: map
   failed site=<NAME> virt=... cr3=... free=NNN` before returning
   -1.  The huge-page split block's own alloc failure already
   halted loudly and is unchanged; it is the fourth site under a
   different name.  The commit is scratch-tagged
   `20261003-vmm-map-diag`.

   The diagnostic **did not fire** on the three boots that
   followed: the item-7a `#PF` did not reproduce.  That is the
   session-48 result, and the diagnostic stays in place — failure
   branch only, no new code on a healthy boot, zero cost until the
   fault returns.  When it does, the `free` field is the piece
   that separates the two candidates below: a healthy count with
   `site=PDPT` or `site=PD` is (b); a near-zero count is (a).

7b. **A user-mode `#GP` at `0x42F1A7` in busybox during `find`.**
   Session 51's first interactive boot — after the
   `20261003-vmm-map-diag` commit — hit this during the canary's
   `find / -type d` row:

       === GENERAL PROTECTION FAULT (#GP) ===
         Faulting RIP : 0x000000000042F1A7
         Code Seg (CS): 0x0000000000000033
         Stack (RSP)  : 0x00000080000FBD98
         Error Code   : 0x0000000000000000
         Current PID : 15
         Name        : busybox
         entry_point : 0x0000000000411A92
       EXIT: pid=15 state=2 parent=4 qhead=(empty)
       EXIT-FALLBACK: switching to idle, exiting pid=15 name=busybox

   The process was killed and the shell fell back to idle; the
   `find` never completed.  The next boot ran the same row to
   completion, `canary` 15/15 and `canary --full` 28/28.  The
   full raw frame dump is in `capture.txt` and in
   `docs/session-log.md`, session 51.

   **This is not item 7a.**  Item 7a is a `#PF` at
   `CR2 = RIP = 0x400000`, error `0x15`, in `musl_sh`'s ELF load,
   before any user instruction runs.  This is a `#GP` (vector 13,
   not 14), error `0` — not present/write/user/fetch — at a user
   text address in busybox (`0x42F1A7`), during a syscall in a
   process that had already been running.  Different vector,
   different location, different phase.  The
   `20261003-vmm-map-diag` sites are not on this path and none of
   them printed.

   **Third in the same family.**  Session 45's virtual-1 `#PF`
   and session 50's `0x400000` `#PF` are the first two: all three
   are intermittent, layout-dependent, appear on the first boot
   after an image change, and clear on the next.  A user `#GP`
   with error `0` is raised for a privileged instruction, a
   non-canonical address in a base register, or a segment
   violation — the frame dump's `[2] 0x0000008010001030` and
   `[3] 0x0000008010000230` are the values the faulting code was
   working with, and `[6] 0x00000000FFFFFF9C` is `AT_FDCWD`
   sign-extended, consistent with `newfstatat`.  Which instruction
   at `0x42F1A7` raised it is not yet known; busybox is a
   third-party binary and its symbols are not in the tree.

   **Unobserved, not fixed.**  It did not reproduce on the next
   boot.  It needs its own instrument — a first-party reproduction
   of the `find` sequence, or a kernel-side `#GP` handler trace —
   before it is guessed at.  It is not the same fault as 7a and
   must not be folded into 7a's writeup.

7c. **`sha512sum` is correct; the harness had the wrong expected
   value.  RETRACTED (session 52).**

   This item claimed `sha512sum` computes a wrong digest for
   `ABC`.  It does not.

   The digest the harness expected,

       ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f

   is the SHA-512 of lowercase `abc` — the FIPS 180-4 test vector
   for the three lowercase letters, not the three uppercase ones.

   The digest `sha512sum` actually produces,

       397118fdac8d83ad98813c50759c85b8c47565d8268bf10da483153b747a74743a58a90e85aa9f705ce6984ffc128db567489817e4092d050d8a1cc596ddc119

   is the correct SHA-512 of the three bytes 0x41 0x42 0x43.

   Confirmed four ways, all agreeing: the busybox applet on donix;
   a self-contained FIPS 180-4 implementation in
   `userland/musl/tests/sha512_probe.c` run on donix; the same
   implementation run on the fedora host; and `sha512sum` on the
   fedora host.

   The row in `userland/scripts/test.sh` now expects
   `397118fd...`.  The lesson is in `gotchas.md`: a test's
   expected value is a claim like any other — verify it against a
   known-good source before treating a mismatch as a bug in the
   thing under test.

7d. **A chain of command substitutions can lose a wake — racy.**
   A shell running many `x=$(command)` rows will eventually block
   forever on one of them.  The shell sits in `PROC_STATE_BLOCKED`,
   the scheduler finds no runnable process, and prints
   `EXIT-FALLBACK: switching to idle`.  From the console, the
   machine looks frozen: keystrokes go into the keyboard buffer and
   nothing consumes them.  **The only recovery is a reboot.**

   **Confirmed racy by a controlled experiment.**  The **same
   image, no rebuild between runs** ran to row 54 on one boot and
   hung at row 29 on the next.  If it were image- or layout-
   dependent, the same image would behave the same way.  It does
   not; it is timing.

   **Seven runs, seven different hang rows:**

   | Run | Hung at | Row |
   |---|---|---|
   | 1 | `id -u` | 24 |
   | 2 | `seq` | 13 |
   | 3 | `hostid` | 39 |
   | 4 | `pidof` | 38 |
   | 5 | `nohup` | 40 |
   | 6 | `crc32` | 29 |
   | 7 | `find -maxdepth` | 55 |

   `nohup` hung on run 5 and passed on run 6, which is the clearest
   single piece of evidence that the applet is not the cause.

   **A kernel bug in the wait/pipe wake path.**  The same family as
   the `put_file_slot` pipe-wake warning above.  Two things have to
   happen when a `$( )` child exits: the shell's read of the child's
   output pipe must get EOF, and the shell's `wait4` must reap the
   child.  If either wake is lost, the shell blocks with a zombie
   child and nothing else runnable.  The next session should read
   `sys_wait4` and `process_wake_parent_if_waiting` together with
   `sys_read`'s `FILE_KIND_PIPE` case, looking for the window
   between the child's exit and the parent's transition to
   `BLOCKED`.

   **Reproducer:** boot, then `sh /root/scripts/test.sh`.  The
   harness prints `[N] run  <name>` before every row, so the last
   line in the capture names the row.  A *smaller* reproducer —
   `i=0; while [ $i -lt 50 ]; do x=$(seq 1 3); i=$((i+1)); done` —
   has not yet been run.


7e. **An intermittent `#GP`/`#PF` control-flow family in fork-heavy
   workloads.**  Four captures in one session: three `#GP` and one
   `#PF`, at three different faulting addresses, all in a busybox
   `NOEXEC` applet's forked child.  None of them is the same fault
   address twice; all of them are the CPU executing or reading at an
   address that is not busybox text and is not a mapped data page.

   The faulting `RIP`s, in order:

   | capture | vector | `RIP` | `CR2` | notes |
   |---|---|---|---|---|
   | 1 | `#PF` | `0x0000008010000985` | `0x0000008083206710` | before the frame dump existed |
   | 2 | `#GP` | `0x00000080000FB700` | — | `r12 = r13 = 0x80100009B0` |
   | 3 | `#GP` | `0x0400000000000000` | — | `r14/r13/r12 = 1/2/3` |
   | 4 | `#PF` | `0x0000000000000000` | `0x0000000000000000` | after the `#PF` frame dump landed |

   **The fingerprint.**  Two captures, on different fault vectors,
   hold the same two values in the same two registers:

       r8 = 0x0000000000415516            (a busybox text address)
       r9 = 0x2F2F2F2F2F2F2F2F            (eight '/' bytes)

   `0x2F2F2F2F2F2F2F2F` is the ASCII `/` character repeated eight
   times.  `r8` holds a legitimate text address.  Both appeared in
   capture 2 (`#GP`) and again in capture 4 (`#PF`), in the same
   slots.  That is not random corruption: a specific code path loads
   those two values into `r8` and `r9` and then transfers control to
   something derived from them.  The four `RIP`s are downstream of
   the same corruption, not four separate bugs.

   **Where it appears.**  Every capture was taken while running
   `test.sh` or `pipe_wake_probe.sh`, in a `NOEXEC` busybox applet
   (`seq`, `cut`, `base32`), in the applet's forked child.  A clean
   `canary --full` run does not produce it; a 200-iteration
   `pipe_wake_probe.sh` run does not reliably produce it; a
   `test.sh` run produces it within the first dozen rows on most
   boots.  It is intermittent and layout-dependent, the same way
   7a and 7b are, but it has now produced four captures with a
   shared register fingerprint, so it is narrower than 7a or 7b.

   **Not 7d.**  The `WW:` wake trace is clean in all four captures;
   the wait/pipe wake path is not implicated.  The fault is in user
   code, after a fork, and the shell does exactly what the 7d fix
   makes it do (wakes, reaps, continues).  The two families are
   separate.

   **Not the fork brk eager-copy fix.**  That fix closed a real
   coverage gap -- `sys_brk` could grow `brk_virt` past the 1 MB
   window the copy walked -- and the family still reproduces after
   it.  The brk fix is correct on its own; it is not the cause of
   7e and not the cure.

   **Where to look next.**  Start from the two fingerprint values,
   not from the fault addresses.  `0x415516` is a busybox text
   address; a `call` return address lands there.  `0x2F2F2F2F2F2F2F2F`
   is a buffer of `/`s; busybox path handling (`concat_path_file`,
   `bb_ask_noecho`, `xchdir`'s parents) builds such buffers.  The
   likely shape is a structure or a buffer whose field should hold a
   function pointer or a return address and instead holds a pointer
   into a `/`-filled buffer, in a code path that only runs when the
   applet is forked (a `NOEXEC` child, not an `exec`'d fresh binary).

   **Instrument in place.**  `isr14_handler` now prints the same
   48-slot raw frame dump as `isr13_handler`, in `PUSH_ALL_GPRS`
   order.  A future `#PF` and a future `#GP` are comparable
   slot-for-slot.

   **Unobserved, not fixed.**  The standing rule: an intermittent
   fault that has not been reproduced under control is not closed.
   Four captures and a register fingerprint is a narrowing, not a
   fix.

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
   seam's next consumer.

   Payoff when it lands: `ln`, `link`, `readlink` with real
   targets, `realpath` correctness, `tar`/`unzip` link restoration,
   and the quiet assumptions (`/bin/sh -> busybox`) that many
   configure scripts and build systems make.

9. **No privilege model: uid/gid are 0 for every process.**
   donix has no privilege model — no per-process uid/euid split,
   no setuid bit (FAT has no mode bits to hold one), no `chown`,
   and no way to become root.  In that world 0 is the honest
   answer: everything runs as root, and a program that checks "am
   I root" gets yes.  The identity syscalls are all consistent:

       getuid   (102) -> 0
       getgid   (104) -> 0
       geteuid  (107) -> 0     (was a fixed 1000 before session 51)
       getegid  (108) -> 0
       getgroups(115) -> 0 groups

   `/etc/passwd` and `/etc/group` each carry one `root` entry so
   the applets that resolve a number to a name (`id -un`, `whoami`,
   `ps`'s USER column, `groups`) print `root` instead of a bare
   number or an error.  Before session 51, `geteuid` returned 1000
   and the other four were missing, so `id` printed a mix of one
   real value and three errno values.

   **This is TEMPORARY and it is the value that changes when a
   privilege model lands.**  That session adds `setuid`/`seteuid`
   (105/117), a setuid-root marker on a FAT-hostile filesystem
   (probably a magic marker in a file, the same shape as the
   symlink design in item 8), a `sudoers` equivalent, and then
   changes these five to return the unprivileged default (1000)
   with root as an escalation.  Getting a `/usr/bin/sudo` working
   needs all of that; the goal of the 0 value now is to let software
   install and run, not to model privilege.

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

**`readdir("/dev")` fails; `readdir("/proc")` works.**  `/proc` is
a listable directory as of session 47: `readdir("/proc")` returns
`self` and the live pids, and `ls /proc` works.  `/dev` has not had
the same treatment — `ls /dev`, `ls /dev/`, and `ls dev` all fail
with `ENOENT`, confirmed session 48.  Adding it is the **same
directory shape** `/proc` got: a synthesized entry list in
`sys_getdents64`, and an `open_resolved`/`stat_resolved` branch
for the prefix.  The mechanism is proven and the pattern is
established; this is small, patterned work.  It is what would let
`tty` and `null` appear in `ls /dev`, and it is the prerequisite
for `/dev/tty` and `/dev/urandom`.  See `handoff.md`, "Busybox
enablement."

**`st_rdev` is 0 on device nodes.**  `ls -l /dev/console` prints
`0, 0` for the major/minor column; a real Unix prints the tty
driver's `4, 0`.  `fill_kstat_as_chardev` sets `st_dev` and
`st_ino` (needed for `ttyname_r`'s gate 3b) but not `st_rdev`.
Cosmetic: `tty` does not read it.  One line when someone wants
`ls -l /dev/...` to look right.

**No `/dev` directory.**  `ttyname(3)` names the console —
`readlink("/proc/self/fd/0")` returns `/dev/console` and the
`(st_dev, st_ino)` match passes, so `tty` prints `/dev/console`.
What remains is that **`/dev` is not a directory**: `readdir` on it
fails, so `ls /dev` fails (see above).  `/dev/tty` and
`/dev/urandom` do not exist, and `/dev/console` is stat-able but
not openable (`open("/dev/console")` returns `-ENOENT`; nothing
opens it yet).  The seam is the mechanism; the directory and the
remaining entries are the work.

**`open("/proc/<pid>", O_DIRECTORY)` is not done.**  `ps` *stats*
the per-pid directory (that is what session 47 fixed); it does not
*open* it.  `ls /proc/1` and `opendir("/proc/1")` would need
`open_resolved` to accept the same two paths `stat_resolved` now
does, producing a `FILE_KIND_DIR` slot with `PROC_DIR_SENTINEL` —
the mechanism session 47 already built for `/proc` itself.  Small.

**A nonexistent pid stats as a directory.**  `stat("/proc/999999/")`
reports `S_IFDIR` rather than `ENOENT`, because the check is on the
path shape, not on `process_find_by_pid`.  `ps` only stats pids
`readdir` gave it, so it does not affect the consumer.  Worth one
line when `/proc` is next touched.

**`sys_gettimeofday` (99) is not implemented.**  `clock_gettime`
(228) is, and is what musl reaches for in most cases, but
`PS_LONG`/`PS_TIME` in `ps` call `time()`/`localtime()`, which
reach 99, and `ps -l` therefore hits `Unknown syscall: 99`.  A
small syscall from `g_ticks`, like `clock_gettime`.

### Test-design notes

> **This section is not an open-issue list and does not belong in
> this file.**  It is here because it was here, and moving it is a
> separate edit.  It should move to `handoff.md`'s canary section
> (where the test list already lives) in the next documentation
> round, together with the item-7 note at the top of this list.

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

- **`proc_dir`, `proc_stat`, `proc_walk`, `proc_walk_fds`,
  `mmap_stress`, and `exec_churn`** (sessions 47 and 48) are run by
  hand, not as canary rows.  `proc_dir` (8 checks) and
  `proc_stat` (7 checks) are the `/proc` file tests; `proc_walk`
  and `proc_walk_fds` **report** rather than assert, and reproduce
  the `procps_scan` sequence; `mmap_stress` and `exec_churn` drive
  the two free paths (munmap and process exit) that the PMM
  zone-scan bug depended on.  `exec_churn` needs its helper,
  `churn_helper`, staged as `/usr/bin/CHURN_HELPER`.  **Session 49
  added a second use for `exec_churn`:** it is the test that
  exercises `process_free_clone` on the process-exit path, so run
  it when changing `process_reclaim` or `process_destroy`.

- **`test.sh` is a new harness (session 51), staged at
  `/root/scripts/test.sh`.**  It runs ~58 non-interactive applet
  rows, asserts known values where there is one, and prints
  `[N] run  <name>` before every row so a hang names its own row.
  Run it with `sh /root/scripts/test.sh`.  It is **not** a canary
  row: it takes minutes, it fails `sha512sum` deterministically
  (item 7c), and it can hang on the lost wakeup (item 7d).  Run it
  by hand when changing the applet config or the pipe/wait paths,
  and expect either a hang or a `FAIL sha512sum` on every run until
  those two items are fixed.  The harness found both of them, so
  neither a hang nor the `sha512sum` failure is a sign the harness
  is broken.

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
