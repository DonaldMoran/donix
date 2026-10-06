## Session 58 — the `fault_rip` walk, the ELF loader fix, and a diagnostic that perturbs the race

Three code commits on `dev`, scratch-tagged
`20261005-elf-pf-x-and-page0-guard`,
`20261005-sys-mmap-exec-print`, and
`20261005-pipe7e-stdio-probe`; a docs commit to follow.  **Not a
milestone** -- a session that produced a walk correction, one
real correctness fix, one diagnostic, one probe falsification,
and a `#GP` capture that points at `setjmp`/`longjmp`.  No `v*`
bump; the banner still reads `v0.6.12`.

| Tag | What |
|---|---|
| `20261005-elf-pf-x-and-page0-guard` | the ELF loader honors `PF_X` per segment and refuses a `PT_LOAD` at page 0 |
| `20261005-sys-mmap-exec-print` | a print in `sys_mmap`'s NX gate, in the branch that runs only for an executable mapping |
| `20261005-pipe7e-stdio-probe` | `pipe7e_stdio.c` -- the `x=$(cmd)` shape with `FILE *` I/O and a fault reporter; passes 20000 iterations |

### The `fault_rip` walk, and what it got wrong

Added a PTE-for-`fault_rip` walk to `isr14_handler` next to the
`CR2` walk.  It fired on real captures and produced a result that
turned out to be a misreading.

For an all-zero `fault_rip` (0x1, 0x9, 0x24), the walk's
all-zero indices descend into the KERNEL's identity map.  Page 0
there is a present, executable-for-the-kernel mapping with
`PT_USER` clear.  The walk read that PTE and printed
`NX clear -- page is EXECUTABLE`.  The user fetch faults on the
privilege bit, not on present and not on NX.  **The "every 7e
fault RIP is on an executable page" reading was a walk artifact
and is retracted.**

Fixed by a `PT_USER` check at each level of the walk.  Verified:
`RIP = 0x1`/`0x9`/`0x24` now print
`RIP PTE NOT USER -- page is supervisor-only`.

For the mmap-window `fault_rip` (`0x8010000985`, `0x8010000833`)
the walk shows all four levels present and `PT_USER`, NX clear --
so that shape is real.  But the `sys_mmap` EXEC print did not
fire, so `mmap(PROT_EXEC)` was never called and the executable
mmap-window page is not from `sys_mmap`.

### The strongest capture: a `#GP` in the shell at a stack address

    Faulting RIP : 0x00000080000FB700   (a user stack address)
    Stack (RSP)  : 0x00000080000FB760
    Error Code   : 0
    Current PID  : the busybox running the script

Frame fingerprint live: `r8 = 0x415516`,
`r9 = 0x2F2F2F2F2F2F2F2F`.  User stack window:

    rsp-0x08 = 0x00000080000FB700   (the faulting RIP; what a ret popped)
    rsp+0x00 = 0x000000000041BFDC   (redirectsafe+0x2f, return from call __setjmp)
    rsp+0x08 = 0x000000010041B347   (a text address with a stray 0x1 in the high dword)

A `ret` popped a STACK address and the CPU refused to execute it.
`redirectsafe` does `__setjmp` before `redirect`; a `jmp_buf` on
the stack holds a saved `rsp`/`rip`.  **This points at the
`setjmp`/`longjmp` path, and it is the strongest lead the family
has had.**

### The `elf.c` fix, and the A/B that cleared it

The ELF loader's `PT_LOAD` loop mapped every segment with
`map_flags = 0x1FULL` -- no `PT_NX` -- so `.rodata`, `.data`, and
`.bss` were all executable.  And it had no lower bound on
`p_vaddr`, so a `PT_LOAD` at `vaddr 0` mapped page 0.  Both
fixed: NX unless the segment says `PF_X`, and a `PT_LOAD` whose
`start_page` is 0 is refused.

Investigated as a 7e candidate; **not the cause.**  A/B test:
the workload completes with the fix in and with it out.  The
variable that tracks the outcome is a diagnostic, not this fix.
Committed as a real correctness fix on its own terms.

### A diagnostic that perturbs the race

Four runs:

| `elf.c` fix | `fault_rip` walk | outcome |
|---|---|---|
| in | in | `#GP` in shell at 352 |
| out | in | `#GP` in shell at ~1170 |
| out | out | completed |
| in | out | completed |

**Every run with the walk in failed; every run with it out
completed.**  The walk reads memory and prints serial output
inside `isr14_handler`, and serial I/O on the fault path is
slow; the extra time widens a race's window.  The walk's
presence is not what is wrong -- the race is -- but the walk
makes it fatal.  This is a gotcha: a diagnostic on the fault
path is part of the system.

The walk is not in the tree.  It was added, corrected, and
reverted for the A/B test.  A future session that wants it
should re-add it gated to known-7e `fault_rip` values, or
narrowed to one line.

### Three probes passed, three mechanisms falsified

`pipe7e_stdio` runs the `x=$(cmd)` shape with the loop's I/O
through `FILE *` and a `SIGSEGV` reporter installed.  20000
iterations, `PIPE7E-STDIO-ALL-PASS`.  Adding a `SIGCHLD` install
(the way ash does; the handler never runs, item 12 is a stub)
also passes.  So the syscall shape, the stdio `FILE *` path, and
the `SIGCHLD` disposition are each insufficient to trigger the
family in a first-party program.

### What a next session should do

The `setjmp`/`longjmp` lead is the target.  A probe that does
`setjmp` before the fork and `longjmp` in the parent, in the
`pipe7e_stdio` loop, would exercise the `jmp_buf` machinery in a
first-party program -- and with the `sigsegv_probe` handler
installed, a fault there prints the process's own state.

## Session 56-57 — the NX test, the signal instrument, and five falsifications

Two code commits on `dev`, scratch-tagged
`20261005-mmap-nx-test` and
`20261005-sigsegv-redirect-instrument`; one docs commit to
follow.  **Not a milestone** -- a correctness session that
produced a positive result (NX works), five falsifications, and
one working debug instrument.  No `v*` bump; the banner still
reads `v0.6.12`.

| Tag | What |
|---|---|
| `20261005-mmap-nx-test` | `userland/musl/tests/mmap_nx.c` -- a first-party proof that `sys_mmap`'s `PT_NX` gate reaches the leaf PTE |
| `20261005-sigsegv-redirect-instrument` | `signal_handler[64]` on `pcb_t`, a real `sys_rt_sigaction`, `signal_maybe_redirect` in `interrupts.c`, and `sigsegv_probe.c` -- a process can catch its own fault and print its own state |

### The `NOFORK` experiment

A throwaway run with `CONFIG_FEATURE_SH_NOFORK=y`, reverted
without a commit, tested whether item 7e's control-transfer face
is fork-specific.  It is not: the family reproduces with `NOFORK`
on, at a lower count (4 faults vs 7), with the same
mmap-window-execution shape.  One boot is not a rate measurement,
but it is a clean falsification of the "in fork-heavy workloads"
framing -- which was a description of where the family was
observed, not a claim about its mechanism, because on the
standard config every applet forks.

### `mmap_nx`: NX is enforced end-to-end

`sys_mmap` sets `PT_NX` when `PROT_EXEC` is absent (a session-54
fix).  `mmap_nx` proves it reaches the leaf PTE: the test maps
one page `PROT_READ|PROT_WRITE`, plants two nops and a `ret`, and
calls it.  The call faults with error `0x15` (present + user +
instruction fetch) and `isr14_handler`'s walk prints
`*** PTE HAS NX BIT SET ***` for the PTE
(`0x8000000007E47067`).  **Consequence for 7e:** the fault RIPs
in the mmap window execute on pages mapped executable by some
path that does not go through `sys_mmap`'s `prot` check -- most
likely a `sys_mmap` call whose `prot` carried `PROT_EXEC` from
corrupted state.

### `sigsegv_probe`: a process can catch its own fault

A minimal `sys_rt_sigaction` stores a handler in `pcb_t`.
`signal_maybe_redirect`, called from `isr13_handler` and
`isr14_handler` before `fault_kill_current`, rewrites the
interrupt frame's `RIP` and `RSP` so the stub's
`POP_ALL_GPRS`/`iretq` resumes at the handler.  The handler runs
on the faulting stack; its own `rsp`/`rbp` reads are the faulting
frame's, and its stack window matches the kernel's
`dump_user_stack_window` for the same fault **slot for slot** --
two independent reads of the same memory.  The handler does not
return; it prints and calls `exit_group`.  **NOT item 12:** no
`rt_sigreturn`, no masks, no `SA_*` flags, no `oldact`, no
queueing, no cross-process delivery.

Two bugs found and fixed while building it, both of which
produced a patch that looked right:

- `exception_frame_t`'s named fields (`error_code`, `rip`,
  `cs`, ...) describe the CPU-pushed frame.  The pointer an
  exception handler receives is the base of the GPR block, which
  sits `EXC_OFF_ERROR_CODE` (15) slots below the CPU frame.
  Reading `frame->cs` reads a GPR slot, not the CS, so the ring
  check `(cs & 3) == 3` always failed and the redirect silently
  never fired.  Fix: use `raw[EXC_OFF_*]`, as the existing
  handlers already do.
- The first version moved the handler's stack a page below the
  fault, so the window it printed read fresh zeros.  Fix: leave
  the handler on the faulting stack.

### Five falsifications

Each was proposed from a fault window or a trace, each died
against source.  Recorded so a future session does not
re-derive them:

1. **Kernel FS base wrong on fork.**  Correct: `pcb_t.fs_base`,
   saved/restored on four scheduler/interrupt paths, copied by
   `sys_fork`, written by `arch_prctl`.
2. **TLS page collides with the user stack.**  Impossible:
   `TP_ADJ(p) == p` on x86_64, so the FS base points at the
   `struct pthread` in `.bss` or the mmap window.
3. **`__post_Fork` writes the pid to a wrong offset.**  Correct:
   `0x30` is `struct pthread->tid`.
4. **`freejob` in `forkchild`'s `curjob` walk is a
   use-after-free.**  Safe: `jobtab` is static, `freejob` frees
   only contents, `set_curjob` unlinks but does not free.
5. **`sysret` target corrupted between the store and the
   `sysret`.**  Falsified: the asm stores `rcx`/`r11` and
   nothing touches them before `sysret`; the stored value *is*
   the target.

The `last sysret rcx` values across the session-57 captures are
musl text addresses in syscall-issuing functions
(`__post_Fork`, `__stdio_write`, the `__lockfile`/`__unlock`
neighborhood), and in every case the `sysret` returns correctly.
The fault is downstream of a correct return, on a return from an
ordinary stdio/locking syscall.

### The carry-back

The instrument was built and verified in a scratch tree
(`/home/noneya/code/testme`), then carried to the real tree as a
patch: `git apply --check` before `git apply`, `git diff --stat`
before commit, and the tree was rebuilt and both tests rerun
here.  Same result.  One change per commit, one scratch tag per
commit.

## Session 55 — the fork-path sentinel leak, and item 7e's heap-corruption face closed

One code commit on `dev`, scratch-tagged
`20261005-fork-sentinel-leak`; three docs commits to follow.
**Not a milestone** -- a correctness session.  Item 7e's
heap-corruption face is diagnosed and fixed; its busybox
control-transfer face is corrected from "closed" to "a suspicion
with no proof" and gets its first non-ash data point.

| Tag | What |
|---|---|
| `20261005-fork-sentinel-leak` | `user_syscall_clear_file_table`, called from `sys_fork`; `pipe7e.c` and `pipe7e_helper.c` as the acceptance test |

### The leak

`sys_fork` copied the parent's file table over the child's,
overwriting fd 0, 1, and 2 without freeing the child's own three
console sentinels -- the slots `process_create` installed via
`user_syscall_init_console_fds`.  Three `kmalloc`'d
`file_slot_t` at 32 bytes each, plus three heap headers, is
**240 bytes = 0xF0 per fork**.  The leak compounds: over ~4800
iterations of a fork-heavy reproducer, `kmalloc(12800)`
eventually returned a block overlapping a live allocation, and
the corruption landed on the four-byte displacement of a
`movq %rbx, 0x2c35(%rip)` in the helper, faulting at `CR2 =
0xFFFFFFFFE4A01F38`.

### The fix

Two parts, both in `04_kernel_64bit/`:

- **`user_syscall_clear_file_table`**, a `close_all_files` with a
  different call site: it drops every slot in a PCB's
  `file_table[]` and leaves the table empty.  Defined in
  `user_syscall.c`, declared in `include/user_syscall.h`.
- **`sys_fork` calls it** on the child immediately before the
  fd-inheritance loop.  The loop that follows still copies
  every fd the parent has, including 0/1/2; the parent's slots
  arrive there with refcounts bumped.

### The acceptance test

`userland/musl/tests/pipe7e.c` is the `x=$(cmd)` shape written
in C with no shell in the loop: pipe, fork, dup2 the write end
to stdout, execve a helper, read the pipe to EOF, wait4.  The
helper is `userland/musl/tests/pipe7e_helper.c`, staged as
`/usr/bin/PIPE7E_HELPER`; it writes two bytes and exits 0.  The
test prints a marker `[pipe7e] iter N` before every 1000th
iteration -- the session-51 rule -- and checks each child's
`wait4` status.

**Two runs of 50000 iterations each on one boot, both
`PIPE7E-ALL-PASS`.**  The second run is the stronger signal: it
starts from a heap that has already survived the first 50000
fork/execve cycles.  The unfixed clone faulted at iteration
4827 of the first run.

### Item 7e stays open, and its session-54 text is corrected

Session 54 wrote item 7e as "closed as a BUSYBOX bug, not a
donix bug."  That overstated what the evidence supported, and
`open-issues.md` is edited in place to say so.  What the
evidence supports:

- **The heap-corruption face is diagnosed and fixed** -- this
  session's work.
- **The busybox control-transfer face remains a suspicion with
  no proof.**  Every capture has `last sysret rcx` in busybox
  text and a subsequent transfer to a data value; the kernel
  returns correctly every time.  That is consistent with an ash
  bug and with an ash/donix interaction the project has not
  isolated.
- **`pipe7e` is the first non-ash data point.**  A non-ash
  program doing the same syscall shape did not transfer control
  to a data value in 100000 iterations.  Consistent with the
  ash-specific reading; not proof.

### `pipe7e`'s trace

The two runs print ~150000 lines of serial output because the
`sys_execve` line, the `EXIT:` line, and the session-53 `WW:`
trace all fire per iteration.  Expected, not a defect.  If a
future session wants to shrink the trace, the `WW:` lines are
the ones to gate; the `sys_execve` and `EXIT` lines are
informative in their own right.

### What this session did *not* change

- The session-53 `g_last_sysret` diagnostic is not touched by
  this session's code commit.  It did not fire during either
  `pipe7e` run (no `fault_rip < 0x1000` events), and it
  **did** fire on a subsequent `pipe_wake_probe.sh` run: eight
  `#PF` captures, all with the diagnostic live, all in
  busybox ash's `seq` applet child.  See the addendum below.
- The NX/non-canonical family from session 54 is not touched.
- Item 10 (the SSE gap), item 12 (signal delivery), and item 13
  (ash job control) remain open and are unaffected.

### Addendum -- the busybox control-transfer face, captured

A `pipe_wake_probe.sh` run after the code commit produced eight
`#PF` captures, all in the same boot that includes `2fc7fde`.
They are **not** the leak: `pipe_wake_probe.sh` completed all
4000 iterations with `loopdone`, which is the fix working.
They are the busybox control-transfer face of item 7e, visible
now without the leak's heap pressure underneath them.

Eight faults, six distinct (two exact repeats).  Three shapes:

| shape | `RIP` | `CR2` | error | note |
|---|---|---|---|---|
| fetch, tiny address | `0x1`, `0x9` | same as `RIP` | `0x15` | register fingerprint present |
| read, mmap window | `0x8010000985` | `0x8083206710` | `0x4` | PDPTE not present |
| read, user stack | `0x80000FB6B6` | `0x80000F` | `0x5` | PDE is the bootloader 2 MB page |

Every capture carries the `*** corrupted control target ***`
line -- the session-53 wiring.  `last sysret rcx` values are
`0x43A5D4`, `0x43F89A`, `0x43B1B7`, all real ash `.text`.  The
fingerprint (`r8 = 0x415516`, `r9 = 0x2F2F2F2F2F2F2F2F`) is
present in every tiny-address fault, matching the item-7e entry.

**The mechanism is not identified.**  What the captures rule
out is now more than what they point to: the leak is not the
cause (fixed, run completes), the sysret target is not corrupt
(the values are real ash text), and the fault is not a single
instruction.  The next session's first move is to disassemble
around `0x415516` and `0x43F89A` in busybox and find what writes
the fingerprint registers.  `open-issues.md` item 7e carries the
full framing; `handoff.md` carries the captures.

Faults were **fewer than prior runs on this image, by
impression** -- no before-count was kept, so the reduction is
not a number.


### Verification

| Test | Result |
|---|---|
| Boot to shell prompt | OK |
| `pipe7e` first run | **50000 iterations, `PIPE7E-ALL-PASS`** |
| `pipe7e` second run, same boot | **50000 iterations, `PIPE7E-ALL-PASS`** |
| Kernel `#PF`, `#GP`, `#DF`, hang | none |
| Heap-exhaustion signature (`CR2 = 0xFFFFFFFFE4A01F38`) | not observed |

The image stages two new ELFs: `PIPE7E` and `PIPE7E_HELPER`.
Both added to `USERLAND_ELFS` and the `mcopy_one` chain in
`05_boot_kernel64/Makefile`.  The Makefile change is one
`git diff --stat` line.

**Scratch tag kept:** `20261005-fork-sentinel-leak`, local, not
pushed.  It is to be dropped before the next `v*` milestone.

---

## Session 54 — the zombie leak closed, the NX/non-canonical address family fixed, and user faults made honest

One commit on `dev`, scratch-tagged, unpushed.  **Not a
milestone** — a correctness session.  Seven fixes and three
diagnostic changes, all verified against a 4000-iteration
`pipe_wake_probe.sh` run that completes with `loopdone`.  The run
surfaced two user-mode `#PF` events the kernel survived; the shell
now prints `Segmentation fault` for each.  Before this session,
the same faults were silent.

| Tag | What |
|---|---|
| `20261004-zombie-leak-and-nx-family` | the zombie-leak fix; the NX / non-canonical address family; the honest exit-status change |

### The zombie leak (item 11)

`pipe_wake_probe.sh` had been leaking one pipe per iteration past
~1074, then 2438, then completing — the leak moved as the fixes
landed.  Session 53 read the trace and proposed the lost
pipe-EOF wake; the actual cause was that `process_exit` did not
call `close_all_files`.  `sys_exit` did, but `process_exit` is
also reached directly from `fault_kill_current`, from
`user_syscall_entry.asm`'s exit jmp, and from `sys_execve`'s
failure branch — and those three did not close the file table.

`close_all_files` was `static` in `user_syscall.c`.  It is now
un-static'd, declared in `include/process.h`, and called at the
top of `process_exit`.  It is idempotent — NULLs each slot as it
closes — so a call after `sys_exit`'s own close is a no-op.

Verified: 4000 iterations complete, no PCB exhaustion, no leaks.

### The NX / non-canonical address family

Four fixes, one mechanism.  Every one was found by reading the
function and the actual bytes, after the trace pointed at the
wrong thing.

**`vmm_get_phys` and `vmm_get_phys_from_cr3` strip `PT_NX`.**
`~0xFFFULL` alone leaves bit 63 set when a PTE or PDE has `PT_NX`.
Callers add `HHDM_START`; with bit 63 set the sum is non-canonical
and the next load faults with `#GP` error 0.  This was latent
until `sys_mmap` began honoring `PROT_EXEC` and setting `PT_NX`
on anonymous mappings.  Both functions now mask `~PT_NX` in
addition to `~0xFFFULL`, including the huge-page branch in
`vmm_get_phys_from_cr3`.

**`sys_mmap` honors `PROT_EXEC`.**  Every anonymous mapping was
previously `PT_PRESENT | PT_WRITE | PT_USER` with no `PT_NX` —
every mmap window page was executable.  That turned a stray jump
into the window into a silent fetch-then-run (a fault inside the
window on whatever the fetched bytes first dereference) instead of
an immediate `#PF` at the jump.  Now: if `PROT_EXEC` is not set,
`PT_NX` is.  `PROT_READ`, `PROT_WRITE`, and `PROT_NONE` are
accepted and ignored; donix has no user page-permission model
beyond user / not-user.

**The huge-page split sets NX on the PTEs, not on the PDE.**  A
PDE's NX bit propagates to every page below it, including the
caller's own page — which the caller then wrote an executable
PTE for, only to have the CPU refuse the fetch at the PDE level.
That was the boot `#PF` at `RIP=CR2=0x4010A6` with `error=0x15`:
musl_sh's `.text` page was NX at the PDE level despite the loader
asking for an executable mapping.  Now: `carry` (no NX) goes into
the PDE, `carry | PT_NX` goes into the 512 split PTEs.

**`pte_phys`: a helper that strips `PT_NX` from a table entry.**
`vmm.c` had `entry & ~0xFFFULL` at every walk step, and every walk
step fed a pointer to the next level.  With `PT_NX` set, the same
non-canonical-address bug fired at each level.  A single
`static inline pte_phys(entry)` does
`entry & ~0xFFFULL & ~PT_NX`, and every walk site in `vmm.c` uses
it: `vmm_map_page`, `vmm_unmap_page`, `vmm_get_phys`,
`vmm_is_mapped`, `vmm_dump_page_table`, `vmm_clone_page_table`,
`vmm_get_phys_from_cr3`, `vmm_map_page_in_cr3`,
`vmm_unmap_page_in_cr3`.

### The fault-model changes

**`isr14_handler`'s walk does not halt.**  The `#PF` walk's
"`PML4E` / `PDPTE` / `PDE` not present" paths used to halt the CPU
with `while (1) hlt`.  For a user-mode fault at a genuinely
unmapped address — which is the normal case — that skipped the
register dump and the stack window, the only diagnostics that
name the fault.  Each case now sets `walk_incomplete`, prints the
level, and falls through.

**`fault_signal`: the kernel reports a faulted child honestly.**
Before this, a faulted child exited with status 0 — the shell
could not tell a clean exit from a kernel kill.  `fault_kill_current`
maps the CPU exception vector to a Linux signal number
(`0x0E` -> `SIGSEGV` = 11, `0x0D` -> `SIGSEGV` = 11, `0x06` ->
`SIGILL` = 4, `0x00` -> `SIGFPE` = 8, `0x01` -> `SIGTRAP` = 5)
and stores it on the PCB's new `fault_signal` field.  `sys_wait4`
encodes a nonzero `fault_signal` as a signal-kill status (the low
byte is the signal number).  busybox ash's `waitforjob` checks
`WIFSIGNALED` and prints `Segmentation fault`.  `process_reclaim`
and `process_destroy` clear `fault_signal` so a reused PCB slot
cannot carry a stale fault.

Verified: during the 4000-iteration run, two faults printed
`Segmentation fault` at their iteration; before the change, the
same faults printed nothing.

### The three diagnostic changes

- **`pmm.c`**: `pmm_init`'s kernel-reservation line prints
  `pmm_free_pages`.  `pmm_alloc_page`'s HIGH and LOW branches
  print `PMM REUSE: page 0x... was <type> now <type>` when a page
  transitions between non-`FREE` types.
- **`run`**: a commented-out QEMU invocation documenting the
  `-d int` capture method.
- **`pipe_wake_probe.sh`**: the iteration count is 4000, up from
  200.  At 200 the script never reached the fault; at 4000 the
  leak and the fault family both reproduce.

### Item 7e is a busybox bug, not a donix bug

Every capture across sessions 45-54 has `last sysret rcx` in
busybox text.  The kernel returns correctly every time.  The user
process then executes a control transfer to a data value.  donix
executes the jump faithfully; the CPU faults.  Session 54's
diagnostic work makes the fault produce a clean `error=0x15` with
`CR2 == RIP`, and the shell now reports it.  Nothing more for the
kernel to fix.  A future session that wants to chase it further
is chasing a busybox bug and would need a first-party reproducer,
not a kernel change.

### Verification

| Test | Result |
|---|---|
| Boot to shell prompt | OK |
| `sh pipe_wake_probe.sh` | **4000 iterations, `loopdone`** |
| User-mode `#PF` events during the run | two; each killed only the faulting child; the shell printed `Segmentation fault`; the loop continued |
| Kernel `#GP` / kernel `#PF` / hang | none |

The image stages the same 49 files as before; the run is against
the same tree.  `kernel.bin` grew from 164376 to 165144 across
the session.


## Session 53 — the zombie leak, the wired sysret diagnostic, and four falsified mechanisms

Two code commits and three docs commits on `dev`, all untagged.
**Not a milestone and not a feature.**  A reproducible resource
leak was found and filed, a declared-but-dark diagnostic was
wired, and a test row that asserted an option the applet does not
have was corrected.

| Commit | What |
|---|---|
| `96153e6` | kernel: wire the `g_last_sysret` diagnostic — the asm stores `rcx`/`r11` before `sysret`, and `isr13`/`isr14` print them when `fault_rip < 0x1000`.  Item 7e's two captures are exactly that signature and the instrument was never connected |
| `d2ad311` | test.sh: the `find -not` row uses the POSIX `!` (`-not` is gated behind `ENABLE_DESKTOP`, which `CONFIG_DESKTOP=n` does not enable); `ran`'s failure path no longer forks |
| (docs) | open-issues item 11 (the zombie leak) and the 7e update; gotchas "a diagnostic that is declared but never wired"; this log |

A temporary PCB dump added to `process.c` answered its question
and was reverted — it is recorded in item 11, not kept in the
tree.

### Item 11 — `pipe_wake_probe.sh` exhausts the PCB pool with zombies

Running `./pipe_wake_probe.sh` past ~107 iterations exhausts the
32-slot PCB pool.  The dump at exhaustion (a temporary diagnostic
in `get_free_pcb`) showed **28 of the 32 slots in
`PROC_STATE_ZOMBIE`**, every one a child of the script's shell,
every one named `busybox`.

The `WW:` trace shows the transition exactly.  For the first ~90
iterations:

    WW: child=NNN parent=810 drop=c state=1

The parent was not `BLOCKED` when the child exited, so the child
was reaped on the parent's next `wait4` — clean.  From ~iteration
95:

    WW: child=NNN parent=810 drop=d kind=2

The parent *was* `BLOCKED`, on `BLOCK_KIND_PIPE_READ`
(`kind=2`).  `process_wake_parent_if_waiting` wakes only a parent
blocked on `BLOCK_KIND_WAITPID`, so a pipe-blocked parent is
correctly not woken by a child's exit — and the child's zombie is
then never reaped.  Every iteration after that leaks one PCB.

The candidate mechanism is the lost pipe-EOF wake: the subshell
exits, its write end closes, the reader should see EOF, and if
that wake is lost the reader stays blocked and the zombie is
never reaped.  Same shape as the existing "also open" entry about
`put_file_slot`'s pipe wake coupling.  The place to read is
`put_file_slot`'s `FILE_KIND_PIPE` case in `user_syscall.c`
(~1895-1910).  Filed as item 11.  Not yet shown to be 7e.

### Item 7e — a second capture, and the diagnostic that was never wired

A `#PF` at `CR2 = RIP = 0x9`, error `0x15`, fingerprint present,
fired on a **passing** `test.sh` run (`57 passed, 0 failed`).  With
the earlier `RIP = 0x1` dump, 7e is now two instruction-fetch
faults at tiny addresses.

That is the exact signature the `g_last_sysret_rcx` comment in
`user_syscall_entry.asm` names — and the globals the comment
describes were never written and never read.  Commit `96153e6`
wires them.  On the next 7e fault the sysret target will be in
the dump.

The fork path was also read and cleared: the stack copy covers
all 16 pages, `process_fork_copy_frame`'s offsets match
`context_switch.asm` and `PUSH_ALL_GPRS` slot-for-slot, and
`exec_alloc_user_stack` is correct.  None of them is the bug.

### `find -not` was an `ENABLE_DESKTOP` gate, not a donix bug

The row `find / -maxdepth 1 -name busybox -not -path /tmp` failed
every run with `find: unrecognized: -not`.  `CONFIG_FEATURE_FIND_NOT=y`
in both configs, the object was fresh, the binary was fresh — and
the row still failed.  The cause is in the source: `-not` sits
inside an `#if ENABLE_DESKTOP` block in `findutils/find.c`,
alongside `-and`, `-or`, and `-wholename`.  This build sets
`CONFIG_DESKTOP=n`, so `-not` is not compiled in even though
`FEATURE_FIND_NOT=y` is — that flag controls the POSIX `!`
operator.  `d2ad311` uses `!`, which passes.

### Four mechanisms proposed and falsified in one session

Each was plausible, each read off a trace line or an arithmetic
pattern, each killed by reading the code:

- **the stack copy skips the top page** — false; the loop covers
  all 16 pages;
- **the `rcx` slot in the fork frame is wrong** — false; the
  layout matches `context_switch.asm` and `PUSH_ALL_GPRS`
  slot-for-slot;
- **`drop=d kind=2` is the leak** — false; it is a correctly
  declined wake, and the leak is the *zombies* it leaves;
- **the busybox build was stale** — false four ways (config,
  object, archive, include); the applet was never the problem.

**The lesson, stated plainly.**  Every falsified mechanism was a
fix proposed before the function containing the bug had been
read.  The greps that killed each one cost seconds; the proposals
cost build cycles.  A *mechanism* read off a trace is a claim, and
the source is what settles it.

### Verification

| Test | Result |
|---|---|
| `canary` | **15 passed, 0 failed** |
| `canary --full` | **28 passed, 0 failed** |
| `test.sh` | **57 passed, 0 failed** (was 56/1) |
| `pipe_wake_probe.sh` | runs to `loopdone` on the first boot; exhausts the pool on the second, which is the item 11 reproducer |

No `VMM: map failed` line.  No `PMM: WARNING - Double free`.  The
one 7e fault in the test.sh run was absorbed by
`isr14_handler`'s kill-and-continue path, which is why the run
still reported `57 passed`.


## Session 52 — a wrong constant in the harness, and a latent kernel bug found while chasing it

Three commits on `dev`, all untagged, unpushed.  **Not a milestone
and not a session with a product** — the work is a correction: a
test's expected value was wrong, the code it tested was correct,
and a session was spent reading correct code before anyone checked
the constant.  One real finding came out of the chase and is filed
as item 10.

| Commit | What |
|---|---|
| `b9f78f4` | test: `sha512sum` is correct; retract item 7c — the row's expected value was the SHA-512 of lowercase `abc`, not of the three bytes the row feeds it; the new `sha512_probe.c` is the independent implementation that settled it |
| `5b2245b` | gotchas: "a test's expected value is a claim" — the shape, the FIPS-180-4 trap, and the handoff-source of the wrong constant |
| `4f183b2` | open-issues: item 10, `CR4.OSFXSR` is set but the kernel saves no XMM state |

Also in `b9f78f4`: `userland/musl/Makefile` gained
`-mno-sse -mno-sse2 -mno-avx -mno-mmx` (stopgap, see item 10) and
`Makefile` as a prerequisite of the `%.elf` rules (a CFLAGS edit
now forces a rebuild); `userland/scripts/shatest.sh`, the
diagnostic for the non-bug, was deleted.

### Item 7c was not a bug

Session 51 filed item 7c as "`sha512sum` computes a WRONG digest
for `ABC`", with the expected value

    ddaf35a1...4ca49f

and the produced value

    397118fd...dc119

The symptom was real and reproducible: `printf ABC | sha512sum`
produced a digest that differed from what the harness expected,
every run, through a pipe, through a file, and through a redirect.
`md5sum`, `sha1sum`, and `sha256sum` on the same bytes agreed with
their harness values.

The expected value was the wrong one.  `ddaf35a1...` is the
**FIPS 180-4 test vector for lowercase `abc`**; `397118fd...` is
the correct SHA-512 of the three bytes `0x41 0x42 0x43`.  The
row had been comparing `ABC`'s digest against `abc`'s digest.

Settled by one command on the fedora host:

    echo -n abc | sha512sum   # ddaf35a1...4ca49f
    echo -n ABC | sha512sum   # 397118fd...dc119

Four independent producers agree on both values: the busybox
applet on donix, `userland/musl/tests/sha512_probe.c` on donix,
the same implementation on the fedora host, and `sha512sum` on the
fedora host.  **There is no applet bug.**

### What was read before the constant was checked

The session read, in order, and found each correct:

- `sha512_begin`, `sha512_hash`, `sha512_end`, and
  `sha512_process_block128` in `third_party/busybox/libbb/hash_md5_sha.c`
  against FIPS 180-4 — the init arrays, the message schedule, all
  80 rounds, the padding, and the 128-bit length field;
- the applet's dispatch (`md5_sha1_sum.c`), the `HASH_*` macros,
  the read loop, the swap macros (`platform.h`), and `rotr64`;
- the config (`CONFIG_SHA512SUM=y`, and no SHA-512 variant flag to
  set);
- the file bytes on disk (`hexdump -C`: `41 42 43`), the pipe
  path, the file path, and the redirect path.

It then built a first-party SHA-512
(`userland/musl/tests/sha512_probe.c`), which reproduced the same
digests for every input, and proposed two fixes — an SSE-state
theory, and a `-mno-sse` CFLAGS change to the userland Makefile.
The CFLAGS change was committed and fixed nothing, because there
was nothing to fix.

**The one command that would have ended it in the first minute was
`sha512sum` on the input, on a host known correct.**  That check
was never run because the handoff's item 7c stated the expected
value as a fact.

### Item 10, the latent bug found on the way

The kernel sets `CR4.OSFXSR` (`kmain.c:37-40`), which tells the
CPU that the kernel saves SSE state on context switch.  It does
not: there is no `fxsave`/`fxrstor`/`xsave` anywhere in
`04_kernel_64bit/`, and `context_switch.asm` saves no XMM
registers.  Userland compiled by `musl-gcc.sh` can emit SSE —
before the CFLAGS change, `sha512_probe.elf` carried 543 XMM
instructions and `busybox.elf` 485 — so an XMM register can be
clobbered across a switch or an interrupt.

No test currently fails because of this; it is latent.  The
session's `-mno-sse*` CFLAGS change is a **stopgap** that makes
the userland ABI match what the kernel preserves, not a fix.  The
fix is kernel-side `FXSAVE`/`FXRSTOR` (or `XSAVE`/`XRSTOR`) on the
switch and interrupt paths, and it is filed as item 10.

### Two lessons, one of them about the handoff

`gotchas.md` gained "a test's expected value is a claim, like any
other" (`5b2245b`).  Its tell: a failure that is deterministic,
specific, and identical across every input path is more often a
wrong reference value than a wrong algorithm.

The second lesson is not in a file yet, and the session-end handoff
now carries it: **a handoff's item text is a claim.**  The wrong
constant entered through session 51's item 7c, which said both
"`sha512sum` computes a WRONG digest" and "the correct SHA-512 of
`ABC` is `ddaf35a1…`".  The session read the first claim, believed
the second, and spent its time on the first.  The handoff's own
rule — "a commit message is a claim, not a fact; read the diff,
not the subject" — applies to handoff prose as much as to commit
messages.


## Session 51 — the applet batches, the shell-script harness, and two bugs the harness found

Five commits on `dev`, four scratch-tagged, unpushed.  **Not a
milestone** — three pieces of work: an instrument for the item-7a
`#PF`, a fix that makes `gzip` work, and a large applet batch plus
the first-party harness that tests it.  The harness found two bugs
that no existing test could see.

| Tag | What |
|---|---|
| `20261003-vmm-map-diag` | the three silent `return -1;` sites in `vmm_map_page_in_cr3` (PDPT / PD / PT) print `site`/`virt`/`cr3`/`free` before returning |
| `20261003-gzip-ioctl` | `sys_ioctl`'s `TCGETS`/`TIOCGWINSZ`/`TCSETS` answer for the console only, not for any fd number 0/1/2; enables busybox `gzip`/`gunzip` |
| `20261003-applets` | busybox: enable 22 applets and 9 feature flags (batch 2) |
| `20261003-applets-harness` | applet batch 3, the standard FAT directory tree, `/etc/passwd` + `/etc/group`, and `test.sh` at `/root/scripts/test.sh` |
| (none — docs commits) | `open-issues: …item 7b…`; `open-issues: sha512sum…, lost…, privilege model` |

### The item-7a instrument, and why it is three sites

Session 50's first boot reproduced the item-7a `#PF` and printed
`ELF: COPY-FAIL phys=0 at vaddr=0x400000` — item 7's return-value
plumbing working, the failure reported instead of swallowed.  But
`vmm_map_page_in_cr3` has **four** page-table allocation sites, and
the log could not say which one returned `-1`:

- **PDPT** and **PD** — the first two directory levels, each still
  a bare `if (!new_*_phys) return -1;`.
- **SPLIT-PT** — the huge-page split's own allocation.  This one
  **already halted loudly** (`VMM: FATAL page-table alloc failed;
  cannot split huge page`) since session 42.  It was not the
  silent site and is unchanged.
- **PT** — the final level, *after* the split block, and the one
  easy to miss: it shares the pointer name `new_pt_phys` with the
  split's already-loud block.  Also a bare `return -1;`.

The commit adds a one-line print to the **three silent** sites:

    VMM: map failed site=PDPT virt=0x... cr3=0x... free=NNN
    VMM: map failed site=PD   virt=0x... cr3=0x... free=NNN
    VMM: map failed site=PT   virt=0x... cr3=0x... free=NNN

**Failure branch only.**  A healthy boot reaches no new code and
prints nothing.  Permanent, not temporary.

**The diagnostic did not fire.**  The item-7a `#PF` did not
reproduce on the boots that followed — the session-48 result.  The
instrument stays in place.  See `open-issues.md` item 7a.

### The `#GP` at `0x42F1A7`, and the `#PF` at `0x44`

Two more faults appeared this session, both during applet runs,
both distinct from item 7a.

**`#GP` at `0x42F1A7`, error `0`, in busybox during `find`.**  The
canary's `find / -type d` row faulted once; the next boot ran it
to completion.  Recorded as `open-issues.md` item 7b, deliberately
not folded into 7a: different vector, location, and phase.

**`#PF` at `CR2=0x44`, `RIP=0x419FD9`, error `0x5`.**  A near-null
**data read** — error `0x5` is present + user + read, not fetch —
in busybox.  Page 0 is present but supervisor, so the read faults.
This is the same page as the session-45 virtual-1 fault (which was
an *instruction fetch* at `RIP=0x1`, error `0x15`), with a
different error code.  It appeared once, on the `od` row, and has
not been given its own item number yet; it is the fourth fault in
the family and the most legible capture of it.

**Both are unobserved, not fixed.**  Neither reproduced.

### The gzip fix: an `isatty` bug found by running a real applet

`gzip` and `gunzip` were enabled (`CONFIG_GZIP=y`) and nearly
worked: `gzip FILE` created the `.gz` and then died with
`compressed data not read from terminal, use -f to force it`.
`-f` made it work.  The guard is in busybox's `bbunzip.c`:

    if (!(option_mask32 & BBUNPK_OPT_FORCE) && isatty(STDIN_FILENO))
        bb_simple_error_msg_and_die("compressed data not read from
            terminal, use -f to force it");

`gunzip FILE` opens FILE, puts it on fd 0, and then asks
`isatty(0)`.  On a real Unix that is false for a regular file.  On
donix `sys_ioctl`'s `TCGETS` tested the fd **number** —

    if (fd != 0 && fd != 1 && fd != 2) return -ENOTTY;

— so `TCGETS` on fd 0 succeeded even when fd 0 was a file,
`isatty(0)` returned true, and the guard fired.  The fix is
`fd_is_console(fd)`: ask the file table whether the slot's kind is
`FILE_KIND_CONSOLE`, and use it in `TCGETS`, `TIOCGWINSZ`, and the
`TCSETS*`/`TIOCSWINSZ` ignore-case.  A file on fd 0 now gets
`-ENOTTY`, which is the honest answer.

**Why kernel-side, not a `third_party/` patch:** the honest test
for "is a tty" belongs in `sys_ioctl`, and it is the same answer
for every applet that asks.  `third_party/` is gitignored and
rebuilt; a patch there is invisible and vanishes.

**Verified:** `gzip test` / `gunzip test.gz` round-trips `hello`
with **no `-f`**; `busybox tty` still prints `/dev/console`;
`canary` 15/15, `canary --full` 28/28.  Committed as
`20261003-gzip-ioctl`.

**Lesson, and it is the good kind:** a real applet found a real bug
that no existing test covered.  `isatty` had been *asserted*
correct by the session-44 comment; running gzip *tested* it.  The
comment was a claim, the applet was the test.

### The identity syscalls, and why uid/gid is 0

`id` printed a mix of one real value and three errno values:

    Unknown syscall: 102     (getuid)
    Unknown syscall: 104     (getgid)
    Unknown syscall: 108     (getegid)
    Unknown syscall: 115     (getgroups)
    uid=4294967258 gid=4294967258 euid=1000

`4294967258` is `-166`, the low 32 bits of a failed syscall return.
`geteuid` (107) was the only one implemented, and it returned a
fixed `1000` — chosen in session 30 as "a plausible non-root uid"
to silence a once-per-boot diagnostic, never a decision about
privilege.

The commit adds the four missing syscalls — `getuid` (102),
`getgid` (104), `getegid` (108), `getgroups` (115), all returning
0 — and **changes `geteuid` from 1000 to 0**, so all four agree.
`getgroups` takes the POSIX shape: `size == 0` returns the count
(0); `size > 0` copies nothing and returns 0.

**Why 0 and not 1000:** donix has no privilege model — no
per-process uid/euid split, no setuid bit (FAT has no mode bits),
no `chown`, no way to become root.  In that world 0 is the honest
answer: everything runs as root, and a program that checks "am I
root" gets yes.  Returning 1000 would make root checks fail with
no sudo to fix them.  This is TEMPORARY and it is the value that
changes when a privilege model lands; recorded as `open-issues.md`
item 9.

### The `/etc` files and the directory tree

`groups` exited 1 without `/etc/group`, because busybox's `groups`
treats a missing group file as a hard error.  The fix is the file,
not a syscall: the Makefile stages

    /etc/passwd: root:x:0:0:root:/root:/bin/sh
    /etc/group:  root:x:0:

With them present, `id` prints `uid=0 gid=0`, `id -un` prints
`root`, `groups` exits 0 and prints `root`, `whoami` prints `root`,
and `ps`'s USER column resolves the numeric uid to a name.

The image also gains the standard Unix directory shape: `/etc`,
`/root`, `/root/scripts`, `/home`, `/dev`, `/var`, alongside the
existing `/bin`, `/usr`, `/usr/bin`, `/tmp`.

### The applet batches

**Batch 2** (`20261003-applets`): 22 applets and 9 feature flags.
`cksum`, `crc32`, `comm`, `expand`, `unexpand`, `expr`, `fold`,
`id`, `groups`, `logname`, `md5sum`, `sha1sum`, `sha256sum`, `nl`,
`paste`, `printf`, `split`, `tac`, `base64`, `whoami`, `rev`,
`hexdump`.  Flags: fancy `echo`/`head`/`tail`/`sleep`, `wc`
large, `find -maxdepth`/`-not`, `grep -A/-B/-C`, `test2`.

**Batch 3** (`20261003-applets-harness`): 20 more applets — `sum`,
`uuencode`/`uudecode`, `base32`, `sha512sum`, `sha3sum`, `shuf`,
`strings`, `tree`, `tsort`, `nohup`, `dos2unix`/`unix2dos`,
`which`, `hostid`, `reset`, `egrep`/`fgrep`, `pidof`, `ascii` —
and 18 feature flags: `sort`/`split`/`find` options, ash
`alias`/`getopts`/`help`/`$RANDOM`/`$(( ))`, tab completion, resize
reflow.

**Both verified by hand first, then by the harness.**  The
checksums match their known values (`md5sum`/`sha1sum`/`sha256sum`
of `ABC`), the encoders round-trip, `tree` walks the image, `id`/
`groups`/`whoami` resolve through `/etc/passwd` and `/etc/group`,
and the shell features (`$((2 + 3 * 4))` → 14, `$((1 << 40))` →
the 64-bit value) work.

### `test.sh` — the harness, and the two bugs it found

`userland/musl/tests/test.sh`, staged at `/root/scripts/test.sh`.
A first-party script that runs each enabled applet through a
command substitution, asserts known values where there is one, and
reports pass/fail.  Its design point is the trace:

    [N] run  <name>

printed **before** each row, so a hang names its own row.  That
trace is what made the session's second bug findable.

**It found two bugs, and neither was visible to any existing test:**

**`sha512sum` computes a wrong digest.**  `printf ABC | sha512sum`
produces `397118fd…` every time, at the prompt and in a script,
through a plain pipe.  `md5sum`/`sha1sum`/`sha256sum` are all
correct on the same three bytes through the same pipe.  **An
applet bug** — the interactive-prompt reproducer excludes the shell
and the pipe.  Suspect block size.  Recorded as `open-issues.md`
item 7c.

**A chain of command substitutions can lose a wake.**  The shell
blocks, the scheduler falls to `EXIT-FALLBACK`, and no process
reads the keyboard; only a reboot recovers.  **Confirmed racy by a
controlled experiment:** the *same image, no rebuild between runs*
ran to row 54 on one boot and hung at row 29 on the next.  Seven
runs, seven different hang rows (`id -u`, `seq`, `hostid`,
`pidof`, `nohup`, `crc32`, `find -maxdepth`), with `nohup` hanging
one run and passing the next.  **A kernel bug** in the wait/pipe
wake path.  Recorded as `open-issues.md` item 7d.

**A test-expectation error, corrected:** the first `sleep` row was
`sleep 0.1`, which fails because `FEATURE_FANCY_SLEEP` is off, so
busybox `sleep` accepts integers only.  The row is now `sleep 1`.
This is the "a test can encode an earlier version's behavior"
gotcha — the row asserted a feature that was not enabled.

**The harness is not a canary.**  It takes minutes, it fails
`sha512sum` on every run, and it can hang.  Run it by hand, and
expect a hang or a `FAIL sha512sum` until 7c and 7d are fixed —
neither is a sign the harness is broken.

### Verification

| Test | Result |
|---|---|
| `selftest` (`k` path) | **18 passed, 0 failed** |
| `canary` | **15 passed, 0 failed** |
| `canary --full` | **28 passed, 0 failed** |
| `test.sh` | 39–54 rows pass per run; `FAIL sha512sum` every run; may hang |

No `VMM: map failed` line on a healthy boot.  No `PMM: WARNING -
Double free`.  `kernel.bin` grew from 162456 to 162648 across the
session; `busybox.elf` from 264184 to 321528.  The image stages 48
files plus the two `/etc` entries and the script.

### A process note

The harness found the lost wakeup because the trace printed the row
*before* running it.  Six earlier runs of the same script, without
a trace, would have left "it hung somewhere" as the entire record.
The row marker turned a hang into a named row — and the fact that
the row **moved** between runs is what proved it a race rather than
an applet bug.  **A test that cannot say where it stopped cannot
say much.**

A second process note, smaller: `capture.txt` was appended with
`>>` once, which concatenated two runs into one file and briefly
made them look like one long run.  Truncate (`>`) per run.

### Scratch tags kept

`20261003-vmm-map-diag`, `20261003-gzip-ioctl`, `20261003-applets`,
`20261003-applets-harness`, local, not pushed.  The two docs
commits ride untagged on `dev`.

---

## Session 50 — the fault-injection test; `process_create`'s failure exits run

One commit on `dev`, scratch-tagged, unpushed.  **Not a milestone**
— the test session 49's own commit message asked for.  Three of
`process_create`'s four failure exits are now exercised; the fourth
is documented and left by inspection.

| Tag | What |
|---|---|
| `20261003-fail-inject` | two kernel-side fault-injection hooks; `test_create_fail`, one selftest row, 18 passed (was 17) |

### What the commit does

Session 49's `20261003-process-create-cleanup` added real cleanup to
`process_create`'s four failure exits — `process_free_clone` and
friends — and its commit message said plainly: *correct by
inspection; UNEXERCISED*.  A healthy boot does not fail an
allocation, so none of the new cleanup had ever run.  This session
adds the facility that makes it run.

**Two hooks, not one.**  The handoff proposed a single `static int`
in `pmm.c`.  Reading `process_create` and `vmm_clone_page_table`
showed two problems with that shape:

- **A bare boolean cannot target exits independently.**  If the
  flag is armed before `process_create`, it trips inside
  `vmm_clone_page_table` — which makes *several*
  `pmm_alloc_page` calls — so it can only ever reach exit 1.  The
  user-stack loop (exit 2) runs after the clone, and there is no
  hook for "arm after the clone, before the stack loop."
- **Exit 4 is not an allocation at all.**  It fires when
  `kernel_stack_slot_alloc` finds all `MAX_PROCESSES` slots taken.
  No `pmm` hook can reach it.

So the commit adds two:

    pmm_debug_fail_next_of_type(page_type_t type)

armed for a *type*, not a count.  The clone's allocations are
`PAGE_PAGE_TABLE`; the user-stack page is `PAGE_USER_DATA`.  Arming
for `PAGE_USER_DATA` lets the clone succeed and trips the first
stack-page allocation — which is exit 2, deterministically, without
counting the clone's allocations (a count that would be brittle
against any future change to `vmm_clone_page_table`'s shape).

    process_debug_fail_next_stack_slot(void)

for exit 4.  Both are one branch, both self-disarming, both
kernel-side only.

### What each case exercises, and what each does *not*

This is the part worth carrying forward, because the four exits do
not all do the same thing:

- **Exit 1** (`vmm_clone_page_table` returns 0): armed for
  `PAGE_PAGE_TABLE`, trips on the clone's PML4.  The clone returns
  0 with **nothing allocated**, and `process_create` takes the
  `pcb->cr3 == 0` branch, which undoes the PCB slot and does **not**
  call `process_free_clone` — there is nothing to free.  **This
  case exercises the undo-PCB-slot logic, not the walk.**  The
  handoff's framing ("the cleanup is what stands between a failed
  allocation and a corrupted machine") implies exit 1 runs cleanup;
  it does not.
- **Exit 2** (`pmm_alloc_page_for_elf` returns 0): the clone
  succeeded and built a real hierarchy, then the first user-stack
  page failed.  **This is the exit that exercises
  `process_free_clone` on a fully-built clone** — the case the
  session-49 gotcha entry is actually about.
- **Exit 4** (`kernel_stack_slot_alloc` exhausts): the 16
  user-stack pages are already allocated and tracked when the slot
  call fails, so this runs the most cleanup of the three
  (`process_cleanup_elf_pages` frees 16 pages, `process_free_clone`
  frees the clone, the slot is undone).
- **Exit 3** (`vmm_map_page_in_cr3` returns -1 in the stack loop):
  **NOT TESTED**, and the commit message says so.  Reaching it
  needs a page-table allocation to fail after the clone succeeded
  but before the stack loop's map call; the clone's own tables are
  the same type, so a type-filtered hook cannot separate them, and
  a countdown would be brittle.  Exit 3 remains correct by
  inspection, and this commit does not change that.

### The assertion, and the two things it catches

Each case reads `pmm_get_free_pages()` before and after the failed
`process_create` and asserts the value is unchanged.  A leak in
any cleanup path shows as a deficit; an overlap between the
table-free (`process_free_clone`) and the frame-free
(`process_cleanup_elf_pages`) shows as a `PMM: WARNING - Double
free` line from `pmm_free_page`.  Neither appeared.

The three `PROCESS:` lines in the output are the **production**
diagnostics on the failure paths:

    PROCESS: page-table clone failed for fail_clone
    PROCESS: Failed to allocate user stack page
    PROCESS: kernel stack pool exhausted

That is the point of the design — the hooks drove `process_create`
into its real exits, not into a test-only copy of them.

### What is now superseded, and what is not

`20261003-process-create-cleanup`'s "correct by inspection;
UNEXERCISED" is superseded **for exits 1, 2, and 4**.  Exit 3 is
not.  The gotcha entry "A function that has never run is correct by
inspection only" is now three-quarters closed: `process_free_clone`
has run on a real clone (exit 2 and exit 4), and the
undo-PCB-slot path has run (exit 1).  The entry's *rule* still
stands — a path that does not fire is still a claim — but the
specific claim it recorded is now answered for three of its four
cases.

### The first boot's `#PF`, and why it is a separate matter

**The first boot after this image change reproduced a `#PF`** at
`CR2 = RIP = 0x400000` during the `musl_sh` ELF load:

    === PAGE FAULT (#PF) ===
      CR2 (Bad Address) : 0x0000000000400000
      Faulting RIP      : 0x0000000000400000
      Raw Error Code    : 0x0000000000000015
      ...
      pde               : 0x0000000000400083
      PDE IS 2 MB PAGE, phys base 0x0000000000400000
    ELF: COPY-FAIL phys=0 at vaddr=0000000000400000
    PANIC: musl_sh ELF load failed

It did **not** reproduce on the next two boots, which is the shape
sessions 45 and 48 both described: layout-dependent, appears on
the first boot after an image change, clears on the next.  It is
**not related to this commit's hooks** — no test ran on that boot,
and both hooks are `static`, zero-initialized, and unarmed.

**It is diagnosed better than it was in session 45, and that is
session 49's doing.**  Session 45's version of this fault was
silent — the mapping failed, `vmm_map_page_in_cr3` returned without
the caller knowing, and the process faulted later in user mode.
This boot printed `ELF: COPY-FAIL phys=0 at vaddr=0x400000` and
panicked at the ELF load, at the call site.  That is item 7's
return-value plumbing working: the failure is reported, not
swallowed.

**It is not fixed.**  An intermittent fault that stops reproducing
is unobserved, not closed — the standing rule from session 45.  It
is recorded as an open issue; the two candidate causes (a genuine
PMM exhaustion in a new shape, or a half-built clone whose
PDPT/PD was missing so the map's own table allocation failed) are
not distinguished by this capture, and the next session that
touches it should instrument rather than guess.

### Verification

Both boot paths, one boot after the tag:

| Test | Result |
|---|---|
| `selftest` (`k` path) | **18 passed, 0 failed** (was 17) |
| `canary` | **15 passed, 0 failed** |
| `canary --full` | **28 passed, 0 failed** |

`test_create_fail` reports `SUCCESS (3 of 4 exits exercised)`; the
three sub-cases each print `no leak`; no `PMM: WARNING - Double
free`.  The image is unchanged at **48 files** staged — this
commit adds no new ELF, only kernel code.

The first boot's `#PF` is the one red mark, and it is a
pre-existing fault, not a result of the test.

### A process note

The handoff's proposed design — "one `static` in `pmm.c`" — was
read against the actual source before it was written, and reading
it showed the design could reach at most one of the four exits.
That is the same rule the handoff opens with ("Ask for source you
do not have"), applied to a *design* rather than a patch: the
handoff's sketch was a claim about code the session had not yet
seen, and the code said otherwise.  The two-hook shape is what the
source dictated; the handoff's one-static shape is what the sketch
assumed.

**Scratch tag kept:** `20261003-fail-inject`, local, not pushed.

---

## Session 49 — item 7 closed; the `process_create` failure paths and the exit-path page-table leak

Five commits on `dev`, scratch-tagged, unpushed.  **Not a
milestone** — five separate changes that add up to a session on
kernel failure paths, plus one dead-code deletion.

| Tag | What |
|---|---|
| `20261003-vmm-int-return` | `vmm_map_page*` return `int`; the six silent returns now report `-1`; `isr14_handler` gains the `pmm_get_page_type` diagnostic (item 7, commit 1 of 2) |
| `20261003-vmm-callers` | the nine callers handle the `-1`; **item 7 closed** (commit 2 of 2) |
| `20261003-process-create-cleanup` | `process_create`'s failure exits clean up; the unchecked `vmm_clone_page_table` return now fails the create; new `process_free_clone` |
| `20261003-dead-has-drive` | `f_stat_with_retry`'s dead `has_drive` and its `if` block deleted |
| `20261003-exit-frees-tables` | `process_reclaim` and `process_destroy` free the page tables via `process_free_clone` |

### Item 7, and how it closed

Item 7 was the silent `if (!phys) return;` in `vmm_map_page_in_cr3`
and `vmm_map_page`: six sites — the PDPT, PD, and PT allocation paths
in each — where a page-table allocation failure meant no mapping was
made and the caller could not tell.  Item 7's own text calls this "a
function that cannot report failure, called by code that assumes
success."

**The reason this was not done in session 45.**  Session 45
attempted the signature change and reverted it, because `canary
--full` then faulted at a different address and mechanism:

    CR2 = RIP = 0x1
    Raw Error Code = 0x15       (present, write, user, fetch)
    pte = 0x0000000000000003    (present, write, NO user)
    PTE PRESENT, phys 0x0000000000000001
    pmm: pml4=2 pdpt=2 pd=2 pt=2    (every page PAGE_TABLE)

A `pmm_get_page_type` diagnostic in `isr14_handler` proved the walk's
pages were all `PAGE_TABLE`, ruling out a use-after-free of a
page-table page.  The mechanism was never isolated, and the fault
has not been seen since.  Item 7's instruction was to **re-apply the
diagnostic before redoing the signature change**, and to treat the
fault as unknown.

**What this session did, in order:**

- **`20261003-vmm-int-return`** — the signature change only, with
  the `isr14_handler` diagnostic in the same commit.  `vmm_map_page`
  and `vmm_map_page_in_cr3` return `int` (0 = mapped, -1 =
  page-table allocation failed); the six `if (!phys) return;` sites
  became `return -1;`; both functions `return 0` on success.  The
  huge-page split's halt was left alone — it already halts with
  `VMM: FATAL` (session 42).  Two `kmain.c` inline `extern`s
  (`test_map`, `test_nx`) changed to match.  **No caller handled the
  return yet** — the point was to isolate the signature change from
  the caller changes.  Verified on both boot paths; the
  `pmm_get_page_type` diagnostic **did not fire**, so the session-45
  fault did not reproduce.

- **`20261003-vmm-callers`** — the nine callers handle the `-1`,
  in the shape the callers themselves dictated:

  | Site | Class |
  |---|---|
  | `vmm.c`, `ensure_hhdm_mapped` | **halt** — no caller to report to |
  | `vmm.c`, `vmm_init`'s identity map | **halt** — boot |
  | `elf.c`, `elf_load_into_process` | recover — returns 0; both callers check |
  | `process.c`, `process_create`'s stack loop | recover — `return NULL` |
  | `heap.c`, `heap_extend` | recover — unmaps and frees its partial region, returns 0 |
  | `user_syscall.c`, `sys_mmap` | recover — `-ENOMEM` |
  | `user_syscall.c`, `sys_brk` | recover — returns `old_brk`, per the brk ABI |
  | `user_syscall.c`, `exec_alloc_user_stack` | recover — returns 0 |
  | `user_syscall.c`, `sys_fork`'s `EAGER_COPY_REGION` macro | recover — destroys the child, `-ENOMEM` |

  `heap_extend` is the one nontrivial site: `heap_brk`/`heap_mapped`
  advance only on full success, so a partial extension is invisible
  to the heap's own bookkeeping *and* to process exit (the heap uses
  the raw PMM, not `elf_add_page_to_pcb`).  On failure it unmaps and
  frees every page the call allocated.

  **Item 7 is closed.**  The session-45 virtual-1 fault did not
  reproduce on either boot path, with the diagnostic in place to
  catch it if it had.

### The defect the item-7 work surfaced

Reading the nine call sites turned up something adjacent: in
`process_create`,

    pcb->cr3 = vmm_clone_page_table(current_cr3);

**had no check.**  A `0` return became `pcb->cr3 = 0`, and the next
`vmm_map_page_in_cr3(pcb->cr3, …)` walked page tables at
`HHDM_START + 0` — a wild write into physical page 0's HHDM alias.
For a kernel-mode `entry_point` it avoided the mapping but still
handed out a PCB whose `cr3` would be loaded into `%cr3` by
`context_switch.asm`.  Either way it is worse than a leak.

The same function's failure exits also leaked: the user-stack loop's
two `return NULL`s and the kernel-stack-pool exhaustion exit left
the cloned page-table hierarchy, the pages already in `elf_page_list`,
a live pid, and `process_count` all behind.

**`20261003-process-create-cleanup`** fixes both: a new
`process_free_clone(uint64_t cr3)` walks the cloned PML4 and frees
entries 0..255 and the PML4 itself, skipping `RECURSIVE_PML4_INDEX`,
skipping entries >= 256 (shared with the parent — the HHDM and the
kernel image), and skipping 2 MB huge PDEs (leaves, not tables).
The four failure exits now run
`process_cleanup_elf_pages` + `process_free_clone` + the PCB-slot
bookkeeping.

**No test.**  All four are failure paths, and nothing on a healthy
boot runs them.  The commit message says so: "correct by inspection;
UNEXERCISED."

### The exit-path leak, and the run that verified it

**`20261003-exit-frees-tables`** closes the same leak on the
*success* path: `process_reclaim` and `process_destroy` freed a
process's ELF pages and its kernel stack slot, but abandoned the
cloned page-table hierarchy when the PCB slot was recycled.

Calling `process_free_clone` from those two functions is correct for
a process that has *run*, even though the function was written for
one that never did.  On the exit path the cr3 is still the clone,
but its low half has grown — `elf_load_into_process`,
`exec_alloc_user_stack`, `sys_brk`, `sys_mmap`, `sys_execve`, and
`munmap` all added or removed tables under it.  The walk frees every
present table it finds, which is right: `vmm_clone_page_table`
deep-copied the low half, so every PDPT/PD/PT under index 256
belongs to this process.  The walk frees **tables only**, never the
data pages the leaf PTEs point at; `process_cleanup_elf_pages` runs
first and frees those, and the two are disjoint.

**This commit is what exercised `process_free_clone` for the first
time.**  `exec_churn` — 24 rounds of fork/execve/wait4, all children
exit 0 — ran the walk 24 times, and the `k`-shell selftest's three
exception children ran it 3 more.  No fault, and **no `PMM: WARNING
- Double free`** from `pmm_free_page`, which is where an overlap
between the table-free and the frame-free would have shown.

The helper's header comment was rewritten in the same commit: it
said "called only from `process_create`'s failure paths," which the
two new callers had just falsified.

### Dead code

**`20261003-dead-has-drive`** deletes `f_stat_with_retry`'s
`has_drive`: it computed the variable, cast it to `(void)`, and did
nothing with it, and the whole `if (r == FR_INVALID_NAME || …)`
block was dead — the function returned `r` unchanged either way.
All five call sites collapse the return to `OK`-or-`fatfs_errno`, so
no caller could observe the deletion.  The comment explaining why
the bare-name retry is gone was kept, reflowed to the top of the
function.

`kernel.bin` was **unchanged** at 160632 bytes after this commit —
the code was already optimized away at `-O2`, which is itself the
confirmation that it was dead.

### A process note

This session's own lesson, worth carrying forward: **the helper
commit 3 added was unexercised until commit 4 put it on a hot path.**
Before the `exec_churn` run, "the cleanup is correct" and "the
cleanup has never run" were the same state.  The two are recorded as
separate gotchas — "A function that has never run is correct by
inspection only" and "A comment that names its callers goes stale
the moment a new caller appears" — and the distinction (a claim
about *exercise*, not about *existence*) is why they are separate
from the older session-43 entry about a fix with no test.

The same session re-taught an older rule the cheap way: the first
attempt to write item 4's edits described them from memory of the
pre-commit-2 `process.c`, and the correction was to ask for the
current bytes and quote them.  See the handoff's "When editing a
large file, quote the bytes."

### Verification

Both boot paths, every commit:

| Test | Result |
|---|---|
| `canary` | **15 passed, 0 failed** |
| `canary --full` | **28 passed, 0 failed** |
| `selftest` (`k` path) | **17 passed, 0 failed** |
| `exec_churn` | **24 rounds, all children exit 0, EXEC_CHURN-ALL-PASS** |
| `mmap_stress` | **16 rounds, MMAP_STRESS-ALL-PASS** |

No `#PF`, no `#DF`, no `#GP`, no `Unknown syscall:`, no
`VMM: FATAL`, no `PMM: WARNING - Double free`, and no new
diagnostics from the failure paths — which is the expected result,
since every message the session added is on a path a healthy boot
does not take.

`kernel.bin` grew across the session from 158744 (commit 1) to
160696 (commit 4), the signature change plus the checks plus
`process_free_clone`.

The one-page PMM shift at commit 3 (`free=31967` → `31966`,
`KERNEL` 674 → 675) is the kernel image crossing a page boundary,
not a leak: the reservation line moved from `[0x100000, 0x301000)`
to `[0x100000, 0x302000)`.

### Scratch tags kept

All five `20261003-*` tags, local, not pushed.  `dev` is five ahead
of `origin/dev`; nothing is on `main`.

---

## Session 48 — the PMM zone scan wraps; the boot-time `#PF` is fixed

One commit on `dev`, scratch-tagged, unpushed.  **Not a milestone**
— a bug fix.  Found the cause of the intermittent boot-time `#PF`
at `0x400000` that had been tabled since session 46, fixed it, and
added two tests that drive the allocator free paths.

| Tag | What |
|---|---|
| `20261003-pmm-wrap` | `pmm_scan_zone` wraps; the allocator finds pages behind its cursor.  `mmap_stress`, `exec_churn`, `churn_helper`. |

### The bug

The `#PF` reproduced this session — first boot after adding
userland ELFs, cleared by the second or third, came back on the
next image change.  It was **not** item 7's silent
`vmm_map_page_in_cr3` returns.  It was the **PMM zone scan**:

`pmm_alloc_page` scanned for a free page starting at a cursor
(`pmm_next_low_page` / `pmm_next_high_page`) and moved **one
direction**.  The free paths rewound the cursor to a freed page on
the *near* side, but a page that had been free the whole time on
the **far** side was never reached: the scan starts at the cursor
and walks away from it.

Adding ELFs to the image pushed the cursor past such pages before
the boot ELF load ran, so the split's page-table allocation
returned 0 — **with `pmm_free_pages` healthy.**  The counter and
the scan disagreed, and the counter was right.  A reboot cleared
it because `pmm_compute_zones` resets the cursor to the start of
the zone.

**The tell:** an "Out of ... zone memory" message with a nonzero
`Free pages:` count.

### How it was found

Not by reasoning about the allocator — by reading the fault
capture and then instrumenting.  The session's order:

1. **The capture** (`DFAULT.txt`) showed both faults in one run:
   the `#PF` at `0x400000` (`pde = 0x400083`, the supervisor huge
   page), then a **double fault** with `CS = 0x33` (ring 3) but
   `SS = 0x2B` and a kernel `RSP` — a corrupted privilege state,
   reached via the `EXIT-FALLBACK` path in `scheduler.c` after the
   shell died.
2. **The allocator was read in full** (`pmm.c`).  The free paths
   *do* take `pmm_irq_save` and *do* rewind the cursor; the
   interrupt race the file's header comment warns about was
   already fixed.  What the scan never does is look on the far
   side of the cursor.
3. **The fix** wraps the scan: `pmm_scan_zone` tries
   `[cursor, far]` then `[near, cursor)`.  A failed scan does not
   move the cursor.  The per-type `phys < 0x200000` skip became a
   `min_page` parameter so both zones share one function.

### Why this was not item 7

Item 7 was the silent `if (!phys) return;` in
`vmm_map_page_in_cr3`: the caller cannot see that the mapping
failed.  That was a real defect.  But *this* bug is why the
allocation returned 0 in the first place.  **Fixing item 7 alone
would have turned "boots into a broken shell that double-faults"
into "refuses to boot the shell"** — a better failure, not a fix.
The session's first plan was the signature change (item 7); the
observation that it converts a corrupt boot into a failed boot is
what sent the work at the allocator instead.  Item 7 was then
closed in session 49.

### The two new tests

- **`mmap_stress`** — map / touch every page / unmap / remap, 16
  rounds plus a final.  Drives the `munmap` free path.  Chosen over
  a `brk` test because `sys_brk` never shrinks (`brk` moves the
  break up only; freeing happens at process exit and `munmap`).
- **`exec_churn` + `churn_helper`** — fork / execve / wait4, 24
  rounds.  Drives the **exit** free path and does 24 ELF loads per
  run, so the split that failed runs 24 times.  The child's **exit
  status** is checked, because "the ELF loaded but faulted
  immediately" is exactly the failure this catches.  `churn_helper`
  is a separate trivial "exit 0" program, deliberately not
  `envp_helper` (which exits 2 when its variable is absent and
  would make a churn run red for an unrelated reason).

### Verification

**More than ten consecutive boots on the trigger image** — two new
ELFs added, 48 files staged, the condition that reproduced the
fault — with no `#PF`, no double fault, no `EXIT-FALLBACK`.
`exec_churn` and `mmap_stress` pass on every run; `canary` 15/15
and `canary --full` 28/28.  **This is "does not reproduce in ten
plus boots", not a proof the fault can never occur** — and the
commit message says so.

### A process note

The first instinct when the walk needed to be seen was to add
`fprintf`s to `third_party/busybox/libbb/procps.c`.  That is wrong:
`third_party/` is gitignored and rebuilt by the toolchain, so the
edit is invisible to the repo and vanishes on the next build.  The
right instrument — used successfully for `ps` in session 47 — is a
**first-party test that reproduces the consumer's sequence**, plus
a **kernel-side trace** in our own file.  Recorded in the handoff's
working style as "Do not edit `third_party/`."

**Scratch tag kept:** `20261003-pmm-wrap`, local, not pushed.

---

## Session 47 — /proc per-pid files and the pid directory; `ps` works

Four commits on `dev`, scratch-tagged, unpushed.  **Not a
milestone** — a working feature with known edges.  Finishes the
`/proc` work session 44's seam started: `readdir("/proc")` lists
the pids, `/proc/<pid>/stat`, `status`, and `cmdline` read, and
`ps` and `pstree` are enabled and work.

| Tag | What |
|---|---|
| `20261003-proc-dir` | `/proc` and `/proc/self` are directories; `readdir` returns `self` |
| `20261003-proc-pids` | `readdir("/proc")` lists the live pids |
| `20261003-proc-stat` | `/proc/<pid>/stat`, with `/proc/self/stat` |
| `20261003-proc-ps` | `/proc/<pid>/status`, `cmdline`, and the pid directory; `ps` works |

### The four commits, and what each needed

**Commit 1 — `/proc` is a directory.**  `open_resolved` and
`stat_resolved` accept `/proc` and `/proc/self` and produce a
`FILE_KIND_DIR` slot whose `obj` is `PROC_DIR_SENTINEL` — a tag,
not a `DIR*`, because there is no FatFs directory behind it.
`sys_getdents64` synthesizes one entry, `self`, and the cursor is
`slot->end`.  `put_file_slot` checks for the sentinel before
`f_closedir`.  `fill_kstat_as_dir` reports `KSTAT_IFDIR | 0555`.
Test `proc_dir.c`, 8 checks.  **`ps` still printed nothing** — the
directory was necessary but not sufficient.

**Commit 2 — numeric pids.**  `process_get_pcb(int)` is added to
`process.c` and declared in `process.h`, because `pcb_pool` is
`static` there and `user_syscall.c` cannot index it.  `readdir`
grows a second phase: after `self`, one decimal pid per live
process, from the pool.  `proc_dir.c` gains checks 7 and 8 (a
numeric entry, and this process's own pid).  **`ps` still printed
nothing.**

**Commit 3 — `/proc/<pid>/stat`.**  `PROC_ENTRY_PID_STAT`; the
`(pid, tag)` pair packed into the slot's `obj`
(`PROC_OBJ_MAKE`/`PROC_OBJ_TAG`/`PROC_OBJ_PID`); `proc_build_stat`
emits the field order libbb's `procps_scan` parses with a fixed
`sscanf`.  Real fields from the `pcb`: pid, comm, state, ppid,
utime, stime, start_time, vsize.  Zeros where donix tracks nothing.
`open_resolved` and `stat_resolved` accept `/proc/<digits>/stat`
**and the literal `self`**.  Test `proc_stat.c`, 7 checks.
`cat /proc/self/stat` printed the right line.  **`ps` still printed
nothing.**

**Commit 4 — the rest, and the fix.**  `PROC_ENTRY_PID_STATUS` and
`PROC_ENTRY_PID_CMDLINE`; `proc_build_pid_status` and
`proc_build_pid_cmdline`; the read-path dispatch grows both cases;
`open_resolved` and `stat_resolved` accept the two files.  And the
fix for the skip: **`stat_resolved` accepts `/proc/<digits>` and
`/proc/<digits>/` and reports a directory.**  `ps` works.

### The bug: `ps` skips every entry on a directory stat

`ps` printed its header and no rows.  The cause is in
`libbb/procps.c`'s `procps_scan`, under `PSSCAN_UIDGID` (which
`ps`'s default flag set and `pstree`'s both have):

    if (flags & PSSCAN_UIDGID) {
        struct stat sb;
        if (stat(filename, &sb))     /* "/proc/<pid>/" */
            continue;                 /* skip the entry */
        sp->uid = sb.st_uid;
        sp->gid = sb.st_gid;
    }

`filename` has a **trailing slash**.  donix served the per-pid
*files* but not the per-pid *directory*, so `stat("/proc/1/")` fell
through to FAT and returned `-ENOENT`, and every entry was skipped
before any file under it was read.

**How it was found, in the order it took.**  The tests passed
(`proc_dir`, `proc_stat`, `proc_status`, `proc_walk`); the config
was right (`PS=y`, `!DESKTOP`, `PS_WIDE=y`); fd state was not the
cause (`proc_walk_fds` held six fds and the walk still worked); the
kernel trace showed `ps` opening `/proc`, calling `getdents64` six
times, and then **nothing** — no row, and no open of
`/proc/<n>/cmdline` from the row printer.  So `procps_scan`
returned NULL after draining the directory, and the only `continue`
not yet reproduced was the `PSSCAN_UIDGID` stat.  `proc_walk` was
extended with `stat("/proc/<n>")` and `stat("/proc/<n>/")`, and
both printed `FAILED ... <-- procps_scan would SKIP` for every pid.
That was the bug.

### Two process notes, both worth keeping

- **The diagnostic was a kernel trace, not a `third_party/` patch.**
  When the walk had to be seen, the first proposal was to add
  `fprintf`s to `libbb/procps.c`.  That is wrong: `third_party/` is
  gitignored and rebuilt by the toolchain, so the edit is invisible
  to the repo and vanishes on the next build.  The right instrument
  is a **first-party test that reproduces the consumer's sequence**
  — `proc_walk` — plus, when needed, a **kernel-side trace** in our
  own file.  Both are committable; the `third_party/` patch is not.
- **`proc_walk` did not call `stat` on the directory.**  It opened
  the files and ran the consumer's `sscanf`, so it proved every
  *file* worked.  It did not make the one *directory* call
  `procps_scan` makes.  A test of the files under a directory says
  nothing about whether the directory can be stat'd.  See
  `gotchas.md`, "A /proc consumer can stat a path it never opens."

### Busybox

`configs/busybox.config`: `CONFIG_PS=y`, `CONFIG_FEATURE_PS_WIDE=y`,
`CONFIG_PSTREE=y`.  `PS_LONG` and `PS_TIME` stay **off** — they pull
in `time()`/`localtime()` → `gettimeofday`, which donix does not
implement (syscall 99).  `DESKTOP` stays off, so it is the simple
`!DESKTOP` `ps_main`.

**`ps` works:**

    $ ps
      PID USER       VSZ STAT COMMAND
        1 0            0 RW   idle
        2 0           92 S    musl_sh
        3 0          364 S    busybox
        4 0          424 R    busybox

The `USER` column reads `0` because `/etc/passwd` does not exist;
`get_cached_username` falls back to the numeric uid.  The
`sys_open: f_open FAIL path=etc/passwd` line is that fallback.

**`pstree` works but shows only `idle`** — which is correct for
donix's process model: `musl_sh`'s `ppid` is 0, so the shell is a
second root, not a child of pid 1.  `pstree` starts at pid 1 and
prints pid 1's subtree.  Not a bug; a consequence of the shell
having no parent process.

### Known edges, not closed by this session

- **`open("/proc/<pid>", O_DIRECTORY)` is not done.**  `ps` *stats*
  the directory; it does not open it.  `ls /proc/1` would need
  `open_resolved` to accept the same two paths `stat_resolved` now
  does.
- **`stat("/proc/<pid>/")` for a pid with no live process still
  reports a directory.**  The check is on the path shape, not on
  `process_find_by_pid`.  `ps` only stats pids `readdir` gave it, so
  it does not affect the consumer.
- **`sys_gettimeofday` (99) is not implemented**, so `PS_LONG` and
  `PS_TIME` stay off and `ps -l` still hits the unknown syscall.
- **`proc_walk` and `proc_walk_fds` report and pass** — they are
  diagnostics, not assertion tests.  Now that the answer is known,
  the assertion version is the permanent regression test.

### Verification

`proc_dir` 8/8; `proc_stat` 7/7; `proc_status` ALL PASS;
`proc_walk` runs the consumer's own `sscanf` and reports `n=11` for
every pid; `ps` lists four processes.  The tabled boot-time `#PF`
did not appear in any boot this session.

**Scratch tags kept:** the four `20261003-*` tags, local, not
pushed.

---

## Session 46 — the boot-time `#PF` is tabled; the next session is busybox enablement

One commit on `dev`, untagged, unpushed.  **Not a milestone** — a
docs change that tables the boot-time `#PF` and re-points the next
session.  No kernel change.

| Tag | What |
|---|---|
| (none — docs commit) | `handoff: table the boot-time #PF; point the next session at busybox enablement` |

**What it does.**  The boot-time `#PF` at `0x400000` is
**intermittent and allocator-state dependent**, and is not
currently observed.  Rather than being the next session's work, it
is now **TABLED** in `open-issues.md` item 7.  The handoff is
rewritten so a fresh session starts on feature work, and the next
session is pointed at **busybox enablement**.

- `open-issues.md` item 7: marked TABLED; the "do not tag a `v*`"
  gate is lifted; session 45's findings (the virtual-1 fault, the
  `pmm_get_page_type` result, the `vmm_clone_page_table` lessons)
  are folded into the item so they are not lost when a session
  reopens it.
- `handoff.md`: the warning block, the "NEXT SESSION — item 7"
  section, and "Session 45, and what it reverted" are removed or
  replaced; the busybox-enablement table is reframed as a **cost
  estimate** ("what each applet needs"), not a prohibition.  The
  stale `BOOT_PF.TXT` reference is corrected — that file is not on
  disk; `PFcapture.txt` holds presentation 2.  The stale HEAD line
  and the stale "item 1 must be rewritten" paragraph are removed.
- `gotchas.md`: a new entry, "A redirection binds to the last
  command in an `&&` chain" (session 45's `run` build-capture
  finding).

**Session 45, in one line, for the record.**  Session 45 attempted
the item-7 kernel fix three ways and reverted all three; **no
kernel change was committed.**  Its one kept commit is `6cfb0e6`
(the `run` script's build-and-capture fix), which rides on `dev`
untagged.  Its findings live in `open-issues.md` item 7.  There is
no session-45 section in this log; the tabled item is the record.

**Session 48 note:** the allocator half of what session 45 was
chasing turned out to be the PMM zone scan, and it is fixed.  The
rest of session 45's findings are still open.

---

## Session 44 — the pathname dispatch seam

Six commits on `dev`, scratch-tagged, **unpushed**, **not tagged
`v*`**.  Opens the `/dev`+`/proc` direction that `ROADMAP.md` calls
the next major work.  It is the first feature the previous
architecture **could not express at all** — `resolve_against_cwd`
plus `fat_lookup` cannot produce `/proc/self/status`, because there
is no FAT entry and never will be — so it is the feature that forces
the pathname dispatch seam into existence.  The seam landed, with
three consumers, and `tty` now prints a path.

| Tag | What |
|---|---|
| `20261002-seam` | `resolve_at` returns a backend tag (FAT/DEV/PROC); `path_backend()` computes it from the first component; every path syscall adopts the new signature; five callers move off `resolve_against_cwd`; cwd-overflow errno becomes `-ENAMETOOLONG` |
| `20261002-dev-null-backend` | `g_dev_table[]` + `dev_lookup()`; **`path_is_devnull` and its three call sites deleted**; `open_resolved`/`stat_resolved`/`access_resolved` take the tag; `fill_kstat_as_chardev` extracted |
| `20261002-proc-status` | `g_proc_table[]` + `proc_lookup()`; `FILE_KIND_PROC`; `proc_build_status()`; `FILE_KIND_PROC` cases in `sys_read`/`put_file_slot`/`sys_fstat_body`; five real fields |
| `20261002-dev-console-tty` | `/dev/console` (`FILE_KIND_DEV_CHAR`, stat-able not openable); `fill_kstat_as_chardev` takes `(st_dev, st_ino)`; `proc_readlink()` + the `sys_readlink` backend branch; **`tty` prints `/dev/console`**; fixes a pre-existing `fstat(0)` bug |
| `20261002-canary-tty` | one read-only canary row: `busybox tty` must print `/dev/console` |
| `20261002-execve-seam` | `sys_execve` calls `resolve_at`; non-FAT backend is `-ENOEXEC`; the `"0:" + path` retry is **deleted** — it was unreachable |

**The seam, in one paragraph.**  `resolve_at` returns, alongside the
resolved absolute path, a backend tag from the path's first
component.  FatFs serves `BACKEND_FAT`; a small table
(`dev_lookup`, `proc_lookup`) serves `BACKEND_DEV` and
`BACKEND_PROC`, or falls through to FAT for an unknown path under
those prefixes.  **No VFS**: no inode, no vnode, no mount table.
`path_backend()` is the one place the mapping lives; `resolve_against_cwd`
now has exactly one caller (`resolve_at`).  See `docs/strategy.md`,
"When a feature may force architecture."

**Three consumers, and what each proves.**

- **`/dev/null`** — `open`, `stat`, `access`.  Before the seam this
  was `path_is_devnull`, an exact-path predicate in three call sites.
  After, it is a table entry.  The seam's first DEV consumer.
- **`/proc/self/status`** — a synthesized *file*: `open`, `read`
  (five real fields — `Name`, `Pid`, `PPid`, `Uid`, `Gid`), `stat`
  (`S_IFREG | 0444`), `close`.  The seam's first PROC consumer.
- **`/dev/console` + `/proc/self/fd/N`** — the first positive
  `readlink` in the tree.  `readlink("/proc/self/fd/0")` returns
  `/dev/console`; `stat("/dev/console")` reports `S_IFCHR` with
  `(st_dev, st_ino) = (1, 1)`, **the same pair `fstat(0)` reports**.
  That match is `ttyname_r(3)`'s gate 3b, and it is why **`tty`
  prints `/dev/console`** instead of `not a tty`.

**A pre-existing bug the new test found.**  `sys_fstat_body` used
`get_file_slot`, which refuses fds below 3, so `fstat(0)` on a
console sentinel returned `-EBADF` **before reaching the
`FILE_KIND_CONSOLE` case that would have answered it** — since
console sentinels were introduced.  The new `proc_fd` test reported
exactly that one failure.  Fixed with `get_file_slot_any`, the same
relaxation `sys_close`/`sys_read`/`sys_write`/`sys_dup2`/`sys_fcntl`
already have.  See `gotchas.md`, "A case in a switch is not reached
if an earlier guard refuses the input."

**The dead retry commit 6 deleted.**  `sys_execve` had a `"0:" +
path` retry whose condition was `exec_path[0] == '/'` — but
`strip_dot_prefix` ran **before** it and removed the leading `/`, so
the condition was false by construction.  The retry was **not
unused; it was unreachable**, and the distinction is the gotcha:
unused code has no caller, unreachable code has a caller whose path
can never satisfy the guard.  Deleted in commit 6, with a comment
recording why.  See `gotchas.md`, "A fix can make an earlier branch
unreachable."

**A build error, and the gotcha it became.**  `proc_readlink` was
placed next to `proc_lookup` for thematic locality — and calls
`safe_copy_to_user`, which is defined **later in the file** with no
forward declaration above line 649.  The compiler caught it
(implicit declaration, then "static declaration follows non-static").
Fix: one line in the forward-declaration block.  See `gotchas.md`,
"Placing a function near its conceptual neighbors does not place it
after its callees."

**Verification.**  canary **15/15** (read-only) and **28/28**
(`--full`); `readlink_errno` 3/3; `at_step1` 10/10; `at_step2` 8/8;
`proc_status` **ALL PASS** (new); `proc_fd` **ALL PASS** (new);
`pipe_step1/2/3/3b` all OK (run for commit 2's `FILE_KIND_*`).
Boot clean, no `Unknown syscall:` lines, no faults.  `tty` prints
`/dev/console`.

**New tests:** `userland/musl/tests/proc_status.c` (9 checks) and
`proc_fd.c` (7 checks).  Both added to `USERLAND_ELFS` and the
`mcopy_one` chain; staged as `::/usr/bin/PROC_STATUS` and
`::/usr/bin/PROC_FD`.  The image now stages **41 files**.

**Busybox: no applet turns on from the seam.**  `ps`/`top`/`kill`/
`pidof` need `readdir("/proc")` and `/proc/<pid>/...`, which the
seam does not provide; `less`/`more` need `/dev/tty` and raw mode,
which it does not provide.  **`tty` is the one applet whose behavior
changed** — it was already enabled and printed `not a tty`; it now
prints `/dev/console`.  The seam's payoff is that the next `/proc`
piece (`readdir`, per-pid entries) is a table entry and a directory
shape, not an architectural change.

**A process failure, recorded because it cost four build cycles.**
The edits to `user_syscall.c` in commits 4 and 6 were described from
memory ("the block above", "after line N") rather than quoted from
the file, and four builds failed before the file was re-read and the
edits quoted.  The rule added to the handoff's working-style section:
**when editing a large file, quote the bytes.**  It is the same
lesson as the whole session — a claim about the source is checked
against the source, not against the claim.

**Open, and not closed by this session:** `readdir("/dev")` and
`readdir("/proc")` fail (the seam serves entries, not directories);
`st_rdev` is 0 on device nodes; `f_stat_with_retry` has dead
`has_drive` at line 3077; `open-issues.md` item 1 is **now stale**
(it says the seam has not landed) and must be rewritten.

**The vmm bug is the next session, and nothing tags `v*` before it.**
`BOOT_PF.TXT` — the huge-page-split `#PF` at `CR2 = 0x400000`,
before any user code ran.  It reproduces on the first boot after an
image rebuild that adds programs and clears by the second or third.
`open-issues.md` item 7.  See `handoff.md`.

**The session's headline is not "the seam works."**  It is that
every claim about the source was checked against the source, and the
one that was not — the edits described from memory — cost four build
cycles.  The seam is the work; reading before writing is what made
it correct.

---

## Session 43 — `realpath`, and the `readlink` errno closed by test

Two commits on `dev`, after `v0.6.9` was tagged and pushed.  A tail
on `v0.6.9`.  **The detail is in `ROADMAP.md`'s `v0.6.9` section
and the `20261002-realpath` / `20261002-readlink-test` tag
annotations.**  In one paragraph:

`CONFIG_REALPATH=y` (config-only — the trace showed the applet
routes through musl's `realpath()` plus `libbb`'s `xmalloc_readlink`
and `getcwd`, all present).  Two of the handoff's own test
expectations were wrong: `realpath /nonexistent` *succeeds* (its
parent `/` exists; the failing case is a path whose parent does not
exist), and the diagnostic goes to stderr.  The run sharpened the
`/dev` entry — `2>/dev/null` does not merely fail to discard, it
stops the command from running, because the shell opens the redirect
target before forking.  And `sys_readlink` already returned
`-ENOENT` for a missing path and `-EINVAL` for an existing
non-symlink; the fix had been in the tree, untested, since session
42.  `tests/readlink_errno.c` calls `readlink(2)` directly (no
applet does) and asserts both answers — 3/3, closing item 8.  See
`gotchas.md`, "A fix with no test is indistinguishable from an
unfixed defect."

**Tags:** `20261002-realpath`, `20261002-readlink-test`.

---

## Session 42 — envp, the `/usr/bin` layout, the shim removal (opens `v0.6.9`)

**The `v0.6.9` milestone.**  Twenty-five commits on `dev`, in two
parts.  **The full narrative is in `ROADMAP.md`'s `v0.6.9` section
and in the `20261001-*` tag annotations**; the log records the shape
and the two bugs it found.

**Part 1 — the opening six commits** (the milestone's original
scope):

| Tag | What |
|---|---|
| `20261001-envp` | `sys_execve` copies `envp` onto the new stack; argv region 4 KB → 16 KB; envp snapshot kmalloc'd |
| `20261001-env-applets` | config: enable busybox `env` and `printenv` |
| `20261001-envdocs` | session-42 docs (the first pass) |
| `20261001-usrbin` | executables to `/usr/bin`; `canary.c` the smoke test |
| `20261001-nosuffix` | drop the `.ELF` suffix; binaries staged bare |
| `20261001-noshim` | `execve`: remove the bare-name attempt; (a) and (b) remain |

**Part 2 — the 19 commits that followed, because each was the next
thing the last one exposed.**  A test for envp that found a
`%rax`-clobber bug; a rewrite of `musl_exec2`; the huge-page-split
`#PF` fix and the recording of item 7; four syscalls (`readlink`,
`clock_gettime`, `nanosleep`, `munmap`); ten busybox applets; three
small gaps (`fcntl` low fds, Ctrl- `[`, `munmap`); and four gotchas.

**The two bugs, both in the tests:**

- **The `%rax` clobber.**  `puts_raw`'s inline `syscall` asm declared
  `%rax` only as an *input*, so GCC believed `%rax` survived the
  block and issued a second `syscall` with the first's return value
  as its number — `Unknown syscall: 18446744073709551578`
  (`2^64 - 38`, the bit pattern of `-ENOSYS`).  Fixed with an
  `"=a"(ret)` output.  See `gotchas.md`.
- **Hand-counted string lengths.**  Eight of `envp_step1.c`'s
  fourteen `puts_raw` literals were wrong by one or two — invisible
  on a console (a NUL prints as nothing) until one dropped a
  newline and ran two lines together.  Fixed by removing the length
  parameter; the callee computes it.  See `gotchas.md`.

**The page-table bug the shim removal surfaced.**  An intermittent
`#PF` at `0x400000` — the huge-page split in `vmm_map_page_in_cr3`
silently returning on allocation failure, leaving a supervisor page
where a user page was asked for.  Fixed in `20261001-splitfix`
(halt with `VMM: FATAL`); the six remaining silent sites recorded
as `open-issues.md` item 7.

**Canary:** green from both shells, `canary` 14/14 and
`canary --full` 27/27; kernel self-test 17/17.

**New tests:** `envp_step1`, `musl_exec2` (rewritten),
`fcntl_lowfd`.  The image staged 38 files.

**Scratch tags kept:** all 24 `20261001-*` tags, local, part of the
open `v0.6.9` milestone at the time — dropped at the bump.

---

## Index — sessions 34 through 41

Each row's detail is in `ROADMAP.md` (the "Done — Phases A through
B" and `v0.6.6`/`v0.6.7`/`v0.6.8` sections), the `v*` tag
annotations, and `git log`.  One line each, for order and quick
reference.

| Session | Milestone / tags | What it did |
|---|---|---|
| 41 | `v0.6.8` | `faccessat` (269) and `utimensat` (280) close the `*at()` family.  `sys_faccessat` must **not** validate flags — musl calls it with three arguments, so `%r10` holds the previous syscall's return value.  `sys_utimensat` must, because musl passes four.  Also a `musl_wait` spin bisect (pre-existing; the console, not `fork`) and the incremental-build trap. |
| 40 | `v0.6.8` | `unlinkat` (263) implemented and shown to have **no consumer** in busybox (`rm -r` uses `lstat`+`unlink`+`rmdir`).  The feature that mattered was the `unlink`/`rmdir` type check in the same commit.  `at_step2.c`. |
| 39 | `v0.6.8` | `20260930-at`: `resolve_at`, `file_slot_t.dir_path`, `newfstatat` (262), `openat` (257); stat family inverted; `find -type` enabled.  Also the software block cursor and the PIT-rate comment fix. |
| 38 | `v0.6.7` | Framebuffer console: VBE mode 0x118, Terminus 10×18, glyph blitter, dual-backend cell primitive, shadow grid; `vi` fills the screen.  Byte order is BGR, not RGB. |
| 37 | `v0.6.7` | `musl_sh` becomes a real shell: tokenizer, redirection, sequences, pipelines.  Also `cat` gains stdin mode.  The `-EPIPE`-without-`SIGPIPE` prediction was **wrong** — `yes | head` does not hang. |
| 36 | `v0.6.6` | `pipe(2)` (22) in five steps; blocking, EOF, `-EPIPE`, dup-aware counts, exit-path wake; `dup(2)` (32); the stdio-guard inversion.  Keyboard fix (Shift+backslash). |
| 34 | (pre-`v0.6.6`) | Low fds (0/1/2) first-class: console sentinels, `alloc_file_slot` from 0, `dup2`/`fcntl` accept 0/1/2.  Fixes `uniq`. |

---

## A note on this file's shape

This file was 1575 lines and growing without bound before session 48.
Every session appended, and much of what it held — the per-commit
rows — duplicated the commit messages and the annotated `v*` tags.
It was restructured in session 48: recent sessions in full, recent
milestones condensed, older sessions indexed, with pointers to where
the full narrative actually lives.

**What stays here:** the per-session narrative that no commit or tag
carries — the order things happened in, the bugs found along the
way, the canary counts, the new-test lists, and the attempts that
were abandoned.

**What does not:** the per-commit tables.  `git log` and the tag
annotations have them.
