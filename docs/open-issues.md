# Open issues

**Last updated: 2026-10-12** (after the eight-fix cycle, session
63; `donix` HEAD `60d29e8` + doc commits, tree hash
`aaa2ad3659ef039ff13a8be32f1717127935cace`).

## Live status

**Two bugs are open.  Everything below this block is a
historical writeup; read the block first, then the writeup for
the item it names.**

### Bug 1 -- user-mode #PF with kernel RSP

A user-mode page fault at user RIP `0x40786B` with `%rsp`
holding a kernel stack address (`0xFFFFFFFF802A2640`), CR2 =
`RSP + 0x98`, error code `0x5`.  The fault dump is in
`capture-fork-rsp-fault-20261012.txt`.  It fired once in
`canary --full` on the row `echo hi | wc`; the next run with the
same binary passed 28/28.  Timing-dependent, unreproducible on
demand.

**Candidate mechanism:** the user RSP slot in the syscall entry
frame, or the frame loaded by `context_switch.asm`'s
user-preempted save path, is being overwritten with a kernel
stack address between syscall entry and return.

**Next-session instrument:** a runtime invariant check in
`user_syscall_entry.asm`, just before the `mov rsp, [rsp - 72]`.
Verify the loaded value is in `(0x8000000000,
0x8000100000)`.  If not, print `SYS-RETURN: bad user RSP = 0x...`
and `process_get_current()->pid`, then `hlt`.  Scratch only;
revert once the writer is named.  Pair with a GDB hardware
watchpoint on the child's user RSP slot.

### Bug 2 -- the pipe7e stall, NOT re-tested on the patched tree

The last `pipe7e` run was on a build that predated the cleanups.
It stalled at iteration ~20500 with the helper `RUNNING` at its
ELF entry `0x40022B` (its `_start`) and the reader blocked in a
pipe read.  **We do not know whether the patched tree still
stalls.**  That is the first thing the next session must check.

**The hypothesis** (from the pre-cleanup observation): the
kernel is resuming the helper at its ELF entry on each syscall
return, not at the saved user RIP.  `_start` is five
instructions and a `call`; it is not a loop.  The candidates are
the frame's `-56` slot (user RIP) being overwritten between
entry and return, `g_syscall_stack_top` being overwritten with a
mid-stack value, or `sys_execve`'s frame rewrite being re-used
because a subsequent syscall entry did not write the slot.

**The verification:** boot `patch_donix` and `donix`, run
`pipe7e` on each, compare.  Turn on `RACE_PROBE_FORK_SLOT` for
the run; leave `RACE_PROBE_PPS_VERBOSE` off.  Add the
user-RSP invariant check if Bug 1 fires first.

**The old item 7e's pipe wake face -- which was the pre-cycle
"UNSOLVED RACE CONDITION" -- is CLOSED.**  See "Closed since the
last rewrite" below.  The writeup under item 7e in this file is
historical; it describes a race that `c3e5c97` closed.

## Closed since the last rewrite

The eight-fix cycle of 2026-10-10/11/12 closed the following.
Their historical writeups remain below; the item text is
retained for reference, not as a statement of live status.

- **item 7d -- the lost-wakeup hang.**  Closed by `364be3b`
  (`process_reclaim` removes the PCB from the ready queue before
  marking it `UNUSED`).  The P11 counters went from 9% success
  to 99.3% across the cycle.
- **item 7e's pipe wake face.**  Closed by `c3e5c97` (reader
  clears its own publish; wakers leave the field set).  The
  six-session 7e chase ends here for the pipe wake race.
- **item 7e's heap-corruption face.**  Closed by `session 55`'s
  `user_syscall_clear_file_table` (`2fc7fde`), already noted in
  the file.
- **item 11 -- the zombie leak.**  Closed by session 54's
  `close_all_files` call at the top of `process_exit`.
- **item 7c -- the sha512sum "wrong digest".**  Retracted in
  session 52; the file already marks it RETRACTED.
- **item 7 -- the silent `vmm_map_page*` returns.**  Closed in
  session 49; the file already notes this.

## Still open

The following items are genuinely open and unchanged in status:

- **item 5 -- `-EPIPE` delivered without `SIGPIPE`.**  Same
  subsystem as item 12.
- **item 8 -- symlinks: recorded design, not scheduled.**  The
  pathname dispatch seam exists; the symlink consumer has not
  been built.
- **item 9 -- no privilege model.**  uid/gid are 0 for every
  process, temporary.
- **item 10 -- `CR4.OSFXSR` is set, no SSE save.**  Stopgap in
  place (userland compiled with `-mno-sse*`); the real fix is
  kernel-side FXSAVE/FXRSTXR.
- **item 12 -- signal delivery is a stub.**  The largest open
  item.  Unblocks `kill`, ash job control, and the Wayland
  `SIGBUS` path.
- **item 13 -- `CONFIG_ASH_JOB_CONTROL` gap.**  Needs item 12's
  subsystem plus process groups and a controlling terminal.
- **items 1-6, and the "also open" paragraph.**  Not audited
  this cycle.  The "also open" paragraph is marked by the file
  itself as "may be stale"; it predates `v0.6.12`.

**Also open, un-numbered:** the boot-time `#PF` at `0x400000`
(item 7a), the `#GP` at `0x42F1A7` (item 7b), and the
`CR2=0x44` / `RIP=0x419FD9` page fault (mentioned under item
7b).  All three are unobserved since the PMM zone-scan fix and
the session-63 cycle; none is confirmed fixed.

---

## Historical writeups

**Everything below this line predates the 2026-10-12 cycle and is
unchanged.**  Read the item with the live-status block above in
mind.  Where the block says an item is closed, the writeup is
history.

### Open (as of session 62)

**Items 7 and 11 are closed and the list is not renumbered.**
Item 7 was the silent `vmm_map_page*` returns; session 49 fixed
it and it is no longer open.  Item 11 was the
`pipe_wake_probe.sh` zombie leak; session 54 fixed it -- the
direct paths into `process_exit` did not call `close_all_files`,
so a pipe end inherited from a fork parent leaked its reference
on every direct-path exit.  Both items keep their original
numbers; the items below keep theirs.  References to "item 7" or
"item 11" in older docs resolve to closed issues, which is
correct.

**Item 7e is a family.  Its heap-corruption face is fixed.  Its
control-transfer face is an UNSOLVED RACE CONDITION, proven by a
tick-rate curve, that is NOT fork-specific, NOT
NX-on-the-mmap-window, and NOT in the kernel's sysret path.**
Session 54 wrote "closed as a BUSYBOX bug"; session 55
corrected that to "a suspicion"; session 57's work narrows it
and falsifies five candidate mechanisms.  **The rate curve is
the proof and it has now been confirmed live: at
`pit_init(10)` the same kernel and image run past 3000
`pipe7e` iterations clean; at `pit_init(100)` the same binary
faults within the first iterations.**  A deterministic defect
produces the same outcome at every rate.  This one does not.
**The precondition for the ASH faults is ash's stale `jmp_buf`
(`evaltree` never restores `globals+0x38` on its normal return);
that is NOT the precondition for `pipe7e`, which has no
`jmp_buf` at all.  The trigger in both cases is the timing
window.**  What the evidence supports is:

  - **The tick-rate curve.**  Same kernel, same
    image, same `pipe7e`, only `pit_init` varies.  **The
    100 Hz row is the one this session ran throughout: the
    fault reproduces there, three times, byte-identically.
    The 10 Hz row is one end-of-session confirmation run:
    clean past 3000 iterations.**  The earlier-session table
    -- 10 Hz clean at 50000, a threshold between 30 and 40 Hz
    -- is a recorded earlier observation, not re-measured this
    session.  A monotone curve of this shape is the signature
    of a race: a deterministic defect produces the same
    outcome at every rate.  **The stale `jmp_buf` is the ASH
    precondition, not `pipe7e`'s -- `pipe7e` links no `jmp_buf`
    at all.**

  - **The `%fs` path is correct.**  At every
    captured fault, `last fs_base`, `fs_base NOW` (the MSR at
    the fault), and `pcb->fs_base` all read `0x408698`, and the
    per-pid `arch_prctl` record shows the faulting process
    **inherited** the value rather than setting it.  The MSR,
    the PCB, and the syscall-entry record agree; the context
    switch is restoring what the process set.  The value is a
    text address because musl set it once, early.  **The value
    is not the mechanism** -- it is a constant present at every
    fault, at every rate.

  - **The fault's shape is a small integer at a return-address
    slot** (`0x1`, `0x4`, `0x32`, `0x0`, `0xFFFFFFFF`), where a
    `ret` popped it.  **The two `last sysret rcx` values name
    two DIFFERENT thunks' post-syscall `ret`s, not one
    function:** `0x400CF2` is `__restore_sigs`'s `ret` after
    its `rt_sigprocmask` (syscall 14); `0x4041F8` is
    `__syscall_cp_c`'s `ret` after the wrapped syscall.
    **`pipe7e.elf` links NO `longjmp`/`setjmp`** -- the
    disassembly shows no such call -- so for `pipe7e` the
    value is NOT a `_longjmp(buf, 1)` return.  The `jmp_buf`
    shape belongs to the ash faults.  **What IS established
    for `pipe7e`:** the kernel returns to the correct RIP and
    the correct RSP -- `RSP - g_user_rsp_save == 8`, on three
    independent runs.  The corruption is in the USER STACK
    CONTENTS, and the corrupted frame sits in the startup
    region (`"Pe7epip"`, `"WD=/root"` at `rsp+0x70`/`0x78`,
    `main`'s entry `0x400220` at `rsp-0x18`, a DOUBLED saved
    frame at `rsp-0x20` and `rsp-0x1C0`).

  - **The five race windows are closed** (tag
    `20261009-pipe-lifetime-lostwakeup`): the pipe lifetime, the
    stranded `READY` process, the sysret tail, the `hlt`-return
    state, and the invalid-`end` guard.  Several workloads that
    failed at `pit_init(100)` now pass; `pipe7e` moved from
    `iter 0`/`iter 50` to `iter 232` before the race fires.
    **Progress, not closure.**

  - **The heap-corruption face is diagnosed and fixed.**  The
    `movq %rbx, 0x2c35(%rip)` displacement corruption
    (`CR2 = 0xFFFFFFFFE4A01F38`) was `sys_fork` overwriting the
    child's three console sentinels without freeing them, leaking
    0xF0 per fork until `kmalloc` returned a block overlapping a
    live allocation.  Fixed in session 55 by
    `user_syscall_clear_file_table`, called from `sys_fork`
    (commit `2fc7fde`, scratch tag
    `20261005-fork-sentinel-leak`).

  - **The control-transfer face is NOT fork-specific.**  A
    `CONFIG_FEATURE_SH_NOFORK=y` run of the same workload
    reproduces the family.  The "in fork-heavy workloads" framing
    in this item's title is a description of where the family has
    been *observed*, not a claim about the mechanism: on the
    standard config every applet forks, so every fault is in a
    forked child by construction.  The `NOFORK` run puts a small
    fork in that: the family survives, at a lower count (4 faults
    vs 7), with the same mmap-window-execution shape.  One boot
    is not a rate measurement, but it is a clean falsification
    of fork-specificity.

  - **The control-transfer face is NOT "NX is broken on the mmap
    window."**  `sys_mmap`'s `PT_NX` gate is enforced end-to-end,
    proven by a new first-party test `mmap_nx` (commit `6aae96c`,
    scratch tag `20261005-mmap-nx-test`, staged as `/usr/bin/MMAP_NX`).
    The test maps one page `PROT_READ|PROT_WRITE`, plants two nops
    and a `ret`, and calls it.  The call faults with error `0x15`
    (present + user + instruction fetch) and `isr14_handler`'s own
    walk prints `*** PTE HAS NX BIT SET ***` for the faulting PTE.
    Bit 63 is set.  A mapping made without `PROT_EXEC` cannot be
    executed on this kernel.

    **Consequence:** the 7e fault RIPs that land in the mmap
    window (`0x8010000833`, `0x8010000822`, `0x8010000985` across
    the captures) cannot be executing a page `sys_mmap` handed out
    read-write.  They execute on pages mapped executable by some
    path that does not go through `sys_mmap`'s `prot` check --
    most likely a `sys_mmap` call whose `prot` argument carried
    `PROT_EXEC` from corrupted state.  The next instrument is to
    print the PTE for `fault_rip` in `isr14_handler`, in addition
    to the PTE for `CR2` it already prints, so a real 7e capture
    shows whether the executing page is executable and by what.

  - **The `last sysret` target is correct and unchanging.**  The
    asm stores `rcx`/`r11` before `sysret` and nothing between the
    store and the `sysret` touches either register.  Across the
    session-57 captures the `last sysret rcx` values are musl text
    addresses in syscall-issuing functions -- `__post_Fork`
    (`0x43F89A`), `__stdio_write` (`0x43B447`), and one in the
    `__lockfile`/`__unlock` neighborhood -- and in each case the
    `sysret` returns to the correct place.  The fault is
    *downstream* of a correct return.  The "sysret target
    corrupted between the store and the sysret" hypothesis is
    falsified.

  - **The control-transfer face is on a return from a musl
    stdio / locking syscall.**  The `last sysret rcx` values name
    the function the process was in when it last entered the
    kernel, and they cluster on `__stdio_write` (the `writev`
    syscall) and the `__lockfile`/`__unlock` lock protocol.  That
    is consistent with the fault being on the path after an
    ordinary stdio write returns, not on the fork path and not on
    a signal-return path (there is no signal subsystem to return
    from; item 12 is a stub).

  - **`pipe7e` remains the first non-ash data point.**  A C
    program doing the `x=$(cmd)` syscall shape -- pipe, fork,
    dup2 the write end to stdout, execve a helper, read to EOF,
    wait4 -- with no shell in the loop.  Two runs of 50000
    iterations each on one boot, both `PIPE7E-ALL-PASS`.  A
    non-ash program doing the same syscall shape did not transfer
    control to a data value in 100000 iterations.  That is
    consistent with the face being ash-specific *in its trigger*;
    it does not prove it, and the `NOFORK` result says the trigger
    is not the fork.

  - **A first-party instrument now lets a process catch its own
    fault and print its own state.**  `sigsegv_probe` (commit
    tagged `20261005-sigsegv-redirect-instrument`, staged as
    `/usr/bin/SIGSEGV_PROBE`) installs a `SIGSEGV` handler via a
    minimal `sys_rt_sigaction`, faults, and the kernel redirects
    into the handler, which prints `rsp`, `rbp`, and a stack
    window, then exits.  Its window and the kernel's
    `dump_user_stack_window` agree slot for slot -- two
    independent reads of the same memory.  **This is the
    instrument the 7e chase has been missing**: a first-party
    reproducer can now print the faulting process's own view from
    inside the fault, instead of the kernel reconstructing it.
    NOT item 12: no masks, no `SA_*` flags, no `oldact`, no
    queueing, no cross-process delivery, no restorer; the handler
    does not return.

  - **Five candidate mechanisms were falsified this session, all
    by reading source.**  Recorded so the next session does not
    re-derive them:

    1. **Kernel FS base wrong on fork.**  `pcb_t` has a
       per-process `fs_base`; the scheduler saves/restores it on
       four paths (`scheduler.c:278-279`, `315-316`, `378-379`,
       `interrupts.c:439-440`); `sys_fork` copies it
       (`user_syscall.c:7526`); `arch_prctl` writes both the MSR
       and the pcb.  Correct.
    2. **TLS page collides with the user stack.**
       `TP_ADJ(p) == p` on x86_64 (`pthread_impl.h:121`), so the
       FS base points at the `struct pthread` itself, in `.bss`
       or the mmap window -- never the stack.  `__post_Fork`'s
       write to `fs_base + 0x30` lands in the `struct pthread`,
       as intended.  Impossible.
    3. **`__post_Fork` writes the pid to a wrong offset.**
       Follows from (2): `0x30` is `tid`, the correct field.
       Correct.
    4. **`freejob` in `forkchild`'s `curjob` walk is a
       use-after-free.**  `jobtab` is a static array; `freejob`
       (`ash.c:4042`) frees only the job's *contents*
       (`ps_cmd`, `ps` if not `&ps0`), clears `used`, and calls
       `set_curjob(jp, CUR_DELETE)`, which unlinks but does not
       free.  The loop `for (jp = curjob; jp; jp = jp->prev_job)
       freejob(jp);` reads `prev_job` of a still-valid `jobtab`
       slot.  Safe.
    5. **`sysret` target corrupted between the store and the
       `sysret`.**  Falsified above: the asm stores `rcx`/`r11`
       and nothing touches them before `sysret`; the stored value
       *is* the target.

Item 7e **stays open**.  The heap-corruption half is done; the
control-transfer half is a reproducible family on pages mapped
executable by an unidentified path, with the last syscall before
the fault in musl's stdio/lock machinery, and the sysret target
correct.  The two instruments a next session can use are in the
tree: the PTE-for-`fault_rip` print (not yet added), and the
`sigsegv_probe` handler (added).  See `session-log.md`, session
55-57, for the raw dumps and the falsification list.

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

7e. **An intermittent `#GP`/`#PF` control-flow family -- NOT
   fork-specific, NOT NX, mechanism unidentified.**  See the
   summary block at the top of this file for the session-57
   findings; this longer entry records the history and the
   falsification list.

   **What the family is.**  The CPU executes or reads at an
   address that is neither busybox text nor a mapped data page.
   Across all captures the `last sysret rcx` is a real musl text
   address, the sysret returns correctly, and the fault is
   downstream.  Error codes: `0x15` (present + user + instruction
   fetch) for tiny-address shapes; `0x4` or `0x5` for mmap-window
   or stack shapes.  The register fingerprint
   (`r8 = 0x415516`, `r9 = 0x2F2F2F2F2F2F2F2F`) is present in
   every tiny-address fault.

   **Where it appears.**  `test.sh` or `pipe_wake_probe.sh`, in a
   busybox applet child (`seq`, `cut`, `base32`).  A clean
   `canary --full` does not produce it.  It is intermittent and
   layout-dependent, but the count and shape are stable enough
   that two full captures are reproducible.

   **Superseded history (kept because the falsifications point
   here, not because a future session should re-read it).**

   The following blocks recorded earlier sessions' hypotheses.
   **All of them were superseded or falsified.**  They are kept
   because the shape of what was tried is useful context, not
   because any of them is a live lead.

   - *Session 55's `__post_Fork` / `0x415516` narrowing.*
     `0x43F89A` is `__post_Fork+0x27` in musl; `0x415516` is
     `pstrcmp1`, a two-instruction `strcmp` wrapper.  The
     session-55 reading was that the fault RIP's small integer
     matched a pid stored by `__post_Fork`.  **Superseded**: the
     session-57 captures show the `last sysret rcx` varies
     (`__post_Fork`, `__stdio_write`, `__lockfile` neighborhood),
     the sysret returns correctly in every case, and the
     `fs_base + 0x30` write goes to `struct pthread` in `.bss` or
     the mmap window -- not the stack.  The pid-in-a-return-slot
     story is not the mechanism.
   - *The two-instrument A/B for the fork frame.*  Proposed in
     session 54, cleared in session 56: ~4800 forks, zero
     invariant mismatches; `process_fork_copy_frame` matches
     `context_switch.asm`'s restore path slot for slot.  The
     child's frame is not the bug; the corruption is post-fork.
   - *The four-capture table and the "Where to look next"
     paragraph from sessions 53-55.*  Superseded by the
     session-57 findings; the four captures are not reproducible
     from what is on disk (capture files are truncated per run).

   **What the tree has now.**  Two first-party tests and one
   instrument:

   - `mmap_nx` (`/usr/bin/MMAP_NX`) -- proves `sys_mmap`'s
     `PT_NX` gate is enforced.  See the summary block above.
   - `sigsegv_probe` (`/usr/bin/SIGSEGV_PROBE`) -- lets a
     first-party process catch its own fault and print its own
     state.  See the summary block above.
   - The PTE-for-`CR2` print in `isr14_handler`, already in the
     tree.
   - `pipe7e_sjlj_stale` (`/usr/bin/PIPE7E_SJLJ_STALE`) --
     reproduces the stale-`jmp_buf` shape first-party.

   **Item 7e -- what is known.**  It is an UNSOLVED RACE
   CONDITION, proven by a tick-rate curve, and confirmed live
   (10 Hz clean past 3000 iterations; 100 Hz faults within the
   first).  **Do not chase the corrupted values; they are
   symptoms of the racing order, not a bad store.**  The next
   instrument is the `fork` stack-copy range: print
   `parent->rsp`, `parent->user_stack_virt`,
   `parent->user_stack_top` at fork time and compare against
   `child->rsp`.  **One open question to carry:** at 100 Hz the
   fault state reproduces byte-identically across runs (same
   CR2, RIP, RSP, `g_user_rsp_save`, stack window).  A race can
   do that when one ordering wins nearly every time, but it is
   worth naming: why is the losing order always the same?  The
   full narrative is in `session-log.md`.

   **Falsified mechanisms, so they are not re-walked.**  Each was
   proposed from a fault window or a trace and killed by reading
   the source.  See `session-log.md`, sessions 56-57:

   - kernel FS base wrong on fork
   - TLS page collides with the user stack
   - `__post_Fork` writes the pid to a wrong offset
   - `freejob` in `forkchild`'s `curjob` walk is a use-after-free
   - `sysret` target corrupted between the store and the `sysret`

   **Findings.**  See `session-log.md`, sessions 58-59:

   - **The `fault_rip` walk misread supervisor pages.**  For an
     all-zero `fault_rip` it descended into the kernel's identity
     map and printed "page is EXECUTABLE", which is true of the
     kernel's mapping and meaningless for the user fetch.  The
     earlier "every 7e fault RIP is on an executable page" reading
     was a walk artifact and is retracted.  Fixed with a `PT_USER`
     check at each level.
   - **The mmap-window fault RIP IS on a user, executable page** --
     but `sys_mmap(PROT_EXEC)` is never called, so the page is not
     from `sys_mmap` and its executability is unexplained.
   - **The strongest capture is a `#GP` in the shell at a stack
     address** (`RIP = 0x80000FB700`, error 0), with
     `redirectsafe+0x2f` -- the return from `call __setjmp` -- in
     the stack window.  This pointed at the `setjmp`/`longjmp`
     path.
   - **`elf.c` is not the cause.**  The loader's missing per-segment
     `PF_X` and page-0 guard were fixed as correctness fixes; an
     A/B test cleared them as the 7e cause.
   - **Three probes passed**, so the syscall shape, the stdio
     `FILE *` path, and the `SIGCHLD` disposition are each
     insufficient to trigger the family first-party.
   - **A diagnostic on the fault path changes a racy fault's
     outcome.**  Four A/B runs: with the walk in, the workload
     fails; with it out, it completes.  Serial I/O on the fault
     path is slow and widens the race's window.  The `fault_rip`
     walk is not in the tree.
   - **The `setjmp`/`longjmp` instruction path is falsified.**  Two
     first-party probes (`pipe7e_stdio_sjlj`,
     `pipe7e_stdio_heapjmp`) passed 20000 iterations.  The
     heap-`jmp_buf` + nested-`longjmp` structure at depth 2 is
     also falsified.  **Still untested:** `UNWIND_DEPTH` > 2, a
     `longjmp` out of a signal handler, multiple `jmp_buf`s.
   - **The corruption value is `0x1`** -- `_longjmp(buf, 1)`'s
     return value -- sitting where a `ret` popped it.  Two `#PF`
     blocks in one run were byte-identical, and the run completed
     (`loopdone`).  Capture preserved outside the tree at
     `../capture.pipe_wake_probe.2x-pf-0x1.txt`.
   - **The disassembly facts:** `r8 = 0x415516` is
     `pstrcmp+0x8` (a function pointer, not corruption);
     `last sysret rcx = 0x43F89A` is `__post_Fork+0x27` (the
     `set_tid_address` site); `0x41BFDC` is `redirectsafe+0x2f`;
     `__setjmp`/`_longjmp` are the standard musl pair and correct.
   - **The stack window is a real read, not an artifact.**
     `dump_user_stack_window` walks the faulting process's own
     `cr3` before the kill.  This is not the `fault_rip`-walk
     mistake one layer down.
   - **The fork path is exhausted.**  The child resumes with
     `%rax = 0`; `fs_base` is copied before the child can run;
     nothing on this path writes a small value to the user stack.
   - **The mechanism, found by reading the disassembly:**
     `evaltree` stores a pointer to its own stack frame's
     `jmp_buf` into `globals+0x38` and restores it only on the
     `longjmp`-return path, not on the normal return.
     `raise_exception` then `_longjmp`s to that global with no
     liveness check.  First-party reproduction:
     `pipe7e_sjlj_stale` (`/usr/bin/PIPE7E_SJLJ_STALE`).
   - **A real kernel defect surfaced and is fixed:**
     `signal_maybe_redirect` had no re-entry guard, so a handler
     that faulted looped.  Fixed with `in_signal_handler` and
     clears in `process_reclaim` / `process_destroy`;
     `signal_handler[]` was also never cleared on teardown.

   **What is closed and what is not.**  The loop is closed.  The
   mechanism is ash's -- `third_party/` is not editable.
   **7e stays open**: it will still fire in ash, now taking two
   faults and a kill instead of live-locking.

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

10. **`CR4.OSFXSR` is set, but the kernel saves no SSE state.**
    Userland compiled by `toolchain/musl-gcc.sh` can emit SSE: gcc
    16.2.1 auto-vectorizes 64-bit byte-shuffles, and before this
    session's CFLAGS change `sha512_probe.elf` carried 543 XMM
    instructions and `busybox.elf` 485.  The kernel is built with
    `-mno-sse -mno-sse2 -mno-avx -mno-mmx` and contains no
    `fxsave`/`fxrstor`/`xsave` anywhere; `context_switch.asm`
    saves no XMM registers.  But `kmain.c` sets CR4.OSFXSR (bit
    16), which tells the CPU that the kernel *does* save SSE
    state.  So a user process that uses an XMM register can have
    it clobbered across a context switch or an interrupt.

    Found in session 52 while investigating the (retracted) item
    7c.  The session added `-mno-sse -mno-sse2 -mno-avx -mno-mmx`
    to the userland CFLAGS as a **stopgap**: it makes the userland
    ABI match what the kernel actually preserves, so no userland
    binary emits SSE and the corruption cannot happen.  It is not
    a fix.  The real fix is for the kernel to save and restore
    XMM state (FXSAVE/FXRSTOR, or XSAVE/XRSTOR) on context switch
    and on the interrupt path, and then to remove the
    `-mno-sse*` flags from the userland build.

    No test currently fails because of this; the corruption is
    latent, and the surface is *any* 64-bit computation that gcc
    chooses to vectorize.  That is why it is filed rather than
    fixed in this session: a fix is a real feature, not a
    one-line change.

11. **CLOSED (session 54).  `pipe_wake_probe.sh` exhausted the
    32-slot PCB pool with zombies; the fix is in `process_exit`.**

    The symptom, from session 53: run to ~107 iterations, the
    probe printed `PROCESS: No free PCB slots!` and the next fork
    failed with `Resource temporarily unavailable`.  28 of the 32
    slots ended up `PROC_STATE_ZOMBIE`, all children of the
    script's shell.

    **The actual cause was not the lost pipe-EOF wake.**  It was
    that `process_exit` -- reached directly from
    `fault_kill_current`, from `user_syscall_entry.asm`'s exit
    jmp, and from `sys_execve`'s failure branch -- did not call
    `close_all_files`.  Only `sys_exit` did.  So every exit that
    went through one of the direct paths left the process's file
    table populated.  A pipe end inherited from a fork parent
    leaked its reference on every such exit: `put_file_slot`'s
    `FILE_KIND_PIPE` case decrements `pipe->refcount` only when
    the slot's refcount reaches zero, and the slot's refcount
    does not reach zero until every process holding it has closed
    it.  A child that exits without closing leaves the parent's
    reference as the last one, and if the parent never closes
    (ash's command-substitution bookkeeping does not on this
    path), the `pipe_t` and its 4096-byte ring buffer leak
    forever.  The leak was arithmetic: one pipe per iteration,
    heap exhaustion at ~1074 iterations, and the run died before
    the fault family could fire.

    **The fix:** `close_all_files` is un-static'd, declared in
    `include/process.h`, and called at the top of `process_exit`.
    It is idempotent -- it NULLs each slot as it closes -- so a
    call after `sys_exit`'s own close finds an empty table and
    returns immediately.

    **Verified:** `pipe_wake_probe.sh` now completes all 4000
    iterations and prints `loopdone`.  No PCB exhaustion; no
    leaks; the sibling 7e faults still fire and the run continues.

    The transition trace that named the leak (`drop=c state=1` for
    the first ~90 iterations, then `drop=d kind=2`) is in
    `session-log.md`, session 53.  The `WW:` trace stays as a
    diagnostic; it is the instrument that showed the transition,
    and it is what would show a regression.

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

*(Restored verbatim from before `476dd86`, which elided this list to
a `...same text...` placeholder.  The list is from 2026-10-02 and
predates `v0.6.12`; several items may be stale.  Audit it against
current tree state before acting on any one line.)*

12. **Signal delivery is a stub; a faulted process cannot catch
    its own signal.**  `sys_rt_sigaction` returns 0 and installs
    nothing; `sys_rt_sigprocmask` returns 0 and does nothing.  A
    process that faults is killed; a process that receives
    `-EPIPE` on a write with no reader (item 5) sees the errno
    and is not killed by `SIGPIPE`.  **Session 54 made the exit
    path honest** -- a faulted child's wait status is now a
    signal-kill encoding, so the parent's shell prints
    `Segmentation fault` and can tell a crash from a clean exit
    -- but the signal is not delivered to the process itself.

    **Session 57 added a minimal instrument, NOT this subsystem.**
    A first-party process can now install a `SIGSEGV` handler,
    fault, and run it (see the summary block at the top of this
    file).  The handler does not return; there is no
    `rt_sigreturn`, no masks, no `SA_*` flags, no `oldact`, no
    queued signals, no cross-process delivery.  Item 12 remains
    open and remains the largest item on the list.  What the
    instrument gives is a way for a first-party reproducer to see
    its own state at a fault, which is what 7e has needed.

    **Fixing this means implementing signal delivery:** a real
    `sys_rt_sigaction`, per-process signal handlers, a raise on
    the fault path that runs the handler if installed, an
    `rt_sigreturn` that restores the frame after the handler
    returns, and a `SIGPIPE` raise on the `-EPIPE` write path.
    That is a subsystem, not a small change.  Same subsystem a
    Wayland `wl_shm` client needs for `SIGBUS` on buffer
    overrun -- see `ROADMAP.md`.  Doing it once serves both.

13. **`CONFIG_ASH_JOB_CONTROL` gap.**  busybox ash's job-control
    code is gated behind `CONFIG_ASH_JOB_CONTROL`, which this
    build does not set.  The consequence is visible in session
    54's run: when a child faults and `sys_wait4` returns a
    signal-kill status, ash's default message is printed, but the
    shell does not do job-control bookkeeping (no `[1]+ Done`
    lines, no `fg`/`bg`, no `kill %1`).  This is the same gap the
    handoff's "still-off" table names.  It needs the same signal
    delivery as item 12 plus the job-control surface (process
    groups, a controlling terminal, `tcsetpgrp`).  Not small.

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

**A live pipe slot's `end` field is 0; no writer is in the
source.**  Found by the P12 slot-29 probe (session 59/60, the
`pipe7e` hunt).  A `file_slot_t` of kind `FILE_KIND_PIPE` was
captured with `refcount = 2` (two fds sharing it) and
`slot->end = 0`, at a fixed kernel-heap address.  `PIPE_END_READ`
is 1 and `PIPE_END_WRITE` is 2; `0` is neither.

The three writes to a pipe slot's `end` in `user_syscall.c` are
`pipe_alloc_end` (`slot->end = end`), and `sys_pipe`
(`rslot->end = PIPE_END_READ`, `wslot->end = PIPE_END_WRITE`).
All three write 1 or 2, never 0.  The only `end = 0` writes in
the file are the DIR and PROC slot paths in `open_resolved`,
and those run on freshly-allocated slots of their own kind, not
on a live shared pipe slot.

**The writer is not in the source.**  The finding is recorded
rather than chased, per this project's rule to name a window
with a probe rather than reason from a corrupted value.

**Observable consequence, closed.**  `put_file_slot` now refuses
to free a pipe when a slot's `end` is neither READ nor WRITE --
it records the event, frees the slot, and leaves the pipe alive.
A leak is survivable; a lost wakeup is not.

**Root cause, open.**  Next step: a guard-band.  Write a magic
value to `slot->end` at slot creation and check it at every
`put_file_slot`; the window between the last good read and the
first bad one names where the write landed.  If that window is
inside `user_syscall.c`, the writer is in this file and the
grep missed it; if it is outside, the write is from another
translation unit and the guard-band's return address names it.
**`pcb->user_rsp` holds a kernel frame base where a user RSP is expected.**  Two faces, one field.  `interrupts.c:439` writes `current->user_rsp =
stack_pointer` on the user-preempt path, but `stack_pointer` there is the
handler's argument -- the kernel frame base `irq0_stub` passed via `mov rdi,
rsp` (`isr.asm:83`) -- not the user RSP the field's name and consumer imply.
`context_switch.asm:66` reads `pcb->user_rsp` (offset `0x610`) as the user
stack pointer when it builds a resume frame for a process last preempted
from user mode.  Second face: `process_fork_copy_frame` never sets
`child->user_rsp` at all, so a fork child inherits whatever
`process_create` left in the field.  Both faces bite on the same path --
user-mode preemption followed by a resume through `context_switch.asm`'s
user-preempted save path -- and a reader who fixes one without the other
has not fixed it.  Not the 7e fault: 7e resumes from a fork frame built
by `process_fork_copy_frame`, not from a user-preempted save.

**`exec_free_and_unmap_user_pages` walks a fixed 1 MB brk window.**  `user_syscall.c:4358` lists the brk region as `(0x8000200000,
0x8000300000)`.  But `sys_brk` has no upper bound -- it refuses
`new_brk < heap_base` and accepts any `new_brk` above -- and `sys_fork`'s
copy at `user_syscall.c:7964` walks `[0x8000200000, parent->brk_virt)`,
the real bound.  A parent whose `brk_virt` has grown past `0x8000300000`
forks a child that has pages the exec cleanup will neither unmap nor free.
Leak, not corruption.  The comment at `user_syscall.c:7953-7959` records
that the fixed window was replaced in the fork copy; the same window
survives in the cleanup table, so the fix was applied to one of the two
places.  Extend the cleanup row to the live bound, or walk
`elf_page_list` and unmap by VA from the list itself.


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
  row: it takes minutes and it can hang on the lost wakeup (item
  7d).  Run it by hand when changing the applet config or the
  pipe/wait paths.  The harness found both the `sha512sum`
  expected-value bug (item 7c, retracted) and the lost-wakeup hang
  (item 7d), so neither is a sign the harness is broken.

- **`mmap_nx` (session 57).**  Maps one page
  `PROT_READ|PROT_WRITE`, plants a `ret`, and calls it.  On this
  kernel the call faults with error `0x15` and the kernel prints
  `*** PTE HAS NX BIT SET ***`.  Run it when changing `sys_mmap`,
  the `PROT_*` handling, `vmm_map_page_in_cr3`'s leaf-PTE
  construction, or the huge-page split path.  Staged as
  `/usr/bin/MMAP_NX`.

- **`sigsegv_probe` (session 57).**  Installs a `SIGSEGV` handler
  via a minimal `sys_rt_sigaction`, dereferences NULL, and the
  kernel redirects into the handler, which prints `rsp`, `rbp`,
  and a stack window, then exits.  The handler does **not** return
  -- there is no `rt_sigreturn`.  Run it when changing
  `sys_rt_sigaction`, `isr13_handler`, `isr14_handler`, or
  `signal_maybe_redirect`.  Staged as `/usr/bin/SIGSEGV_PROBE`.

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
