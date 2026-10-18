# Version history

## v0.6.13 — the `7e` mechanism, the `C`, and the heap cycle

**A diagnosis milestone, twice over.**  The version opened with the
`7e` control-flow-to-data family traced to its mechanism in
`third_party/`, and closed with the `C` — four sessions of framing
theories, then the watchpoint, then the pop-to-dispatch fix — and a
heap free-list cycle that turned out to be an interleaved `kfree`,
not an overflow.  Almost nothing here is a feature.  What shipped
is the removal of three long-standing races and the record that
made each removable.

### The `7e` mechanism (sessions 51–59)

**What the family was.**  A busybox process makes a correct
syscall, `sysret` returns cleanly, and the process transfers control
to a data value.  Four sessions of mechanisms were proposed and
falsified.  Session 59 read the disassembly and named it:

`evaltree` (`0x41C403`) stores a pointer to its own stack frame's
`jmp_buf` into `globals+0x38` and does not restore it on the normal
return — its epilogue, `popstackmark`, `dotrap`, and `int_on` all
read the field, none writes it.  `redirectsafe` manages the same
field on its own frame; a stale `_longjmp` into a returned frame
reads a dead slot.  `raise_exception` (`0x415521`, 27 bytes)
`_longjmps` to `globals+0x38` with no liveness check.

**The instrument.**  `pipe7e_sjlj_stale` mirrors the shape and
faults first-party.  It is the reproduction the tag's annotation
records: fault #1 at RIP `0x80000FBDBF` (a user stack address),
RSP `0x34`, error `0x5`; fault #2 in the handler on the garbage
stack, RIP `0x400640`, CR2 `0x28`, error `0x7`.

**What the chase fixed in the kernel.**  `signal_maybe_redirect`
had no re-entry guard, so a handler that itself faulted re-entered
itself forever — a live-lock.  Fixed with `in_signal_handler` on
`pcb_t`, set when the redirect fires, cleared in `process_reclaim`
and `process_destroy`.  `signal_handler[]` was also never cleared
on teardown despite its comment; fixed in the same commit.

### The migration and the pipe/scheduler race family (sessions 60–65)

**The migration.**  donix was brought up to date from `testme`
(thirteen commits, fast-forwarded onto `dev`), and session 61's
kernel fixes cherry-picked on top: the `pcb->rsp`/`user_rsp` split,
the caller-saved register save at the pipe block point, and the
user-mode save writing both fields.  A grep proved both mechanisms
lived on `dev` — the ring-0 save read 15 slots and wrote 6, and
`pcb->rsp` served both "kernel resume frame" and "user stack
pointer."

**The lost wakeup.**  `pipe_wake_waiter` silently no-opped when
the waiter was not `BLOCKED`, and every caller cleared the waiter
field unconditionally.  The reader's hlt-return set `RUNNING` and
left `reader_waiting = self` in place; a close in that window
no-opped, the caller nulled the field, and the reader re-blocked
with no one to wake it.  Fixed by returning success from
`pipe_wake_waiter` and gating the field clear on it
(`20261011-pipe-wake`).  The subsequent fix removed the reader's
own top-of-loop clear entirely — it reopened the same window
(`20261014-pipe7e-100k-clean`, `7aaaafd`).

**The queue-mutator race.**  `_add`, `_remove`, and `_next` were
not critical sections; a tick could land in the middle of a queue
mutation and leave the tail advanced without the matching clear.
Fixed by making the three functions `pushfq`/`cli` critical
sections (`c85d074`, `20261014-queue-race-closed`).  The queue's
`contains()` walked the queue instead of using link presence as a
membership proxy, and `scheduler_reset` cleared every PCB's links
(`20261011-queue-reset-fix`).

**The strand-rescan family (item 7d).**  A process left `READY`
and off the queue was never dispatched: the timer's kernel-mode
branch printed `TIMER-IDLE-STALL` and returned to idle every tick.
The rescan was added to the timer, `process_exit`, and
`process_yield` (`20261011-timer-rescan`).  A `RUNNING` process
was also being re-added by the rescans; the three now skip
non-`READY` (`6f7344b`'s session work).

**The self-switch guards.**  `scheduler_ready_queue_next` skips
any `RUNNING` head; `scheduler_switch_to` refuses a `RUNNING`
next inside its `cli` (`d3c7f20`); the timer's self-pick resumes
`current` instead of switching (`e21ebf0`, `359c895`).  These
closed reachable self-picks; they are not the `C` fix.

### The `C` (sessions 66–67)

**Four sessions of the wrong theory.**  The `C` is
`context_switch.asm`'s unknown-CS halt.  It fired in four shapes,
and the version tried the reserve/`FRAME_RESERVE` fix
(`67701df`, reverted `5587d70`), the `next->rsp = 0` clear (broke
blocking; reverted), and the switch-to-self guard in the tail
(refused every dispatch; reverted).  All were about the frame.
None was the bug.

**The mechanism.**  The 2026-10-16 SW2-trace run named the last
step: `scheduler_switch_to` was entered with
`next->state == PROC_STATE_ZOMBIE`, `next->rsp` pointing at a
zeroed region, and the resume halted on the CS slot.  A zombie
reached dispatch because `process_yield` popped `next` (READY),
did its checks with interrupts **on**, and dispatched; a tick
between the pop and the dispatch could turn the slot
`READY→ZOMBIE`, and the dispatch used the stale value.  The
slot was reclaimed and reused between consecutive dispatches
(the SW2 trace showed pid 2850 READY then ZOMBIE, then 2851).

**The fix.**  `process_yield` holds `cli` from before
`scheduler_ready_queue_next` through `scheduler_switch_to`
(`ef85962`), with `sti` on every return.  `scheduler_switch_to`'s
tail refuses any non-`READY` next and restores
`current_process = prev` (`e05d1fa`).  The top `SWITCH-DEAD`
guard (`6f7344b`) stays as the fast path.  `process_destroy`
clears both `current_process` copies via `scheduler_set_current`
(`c2a3f89`) — the one confirmed single-copy write.

**Verified.**  pipe7e at `pit_init(100000)` — PIPE7E-ALL-PASS,
250000 iterations, zero `SWITCH-DEAD`, zero `C`.  At
`pit_init(100)`: `canary --full` 28/28, `test.sh` 57/57, pipe7e
clean past 77500.

### The heap cycle (session 68)

**The hang.**  `kmalloc` spun forever with `IF` clear and
`g_ticks` frozen: a free-list block whose `next` pointed at
itself.  The unbounded walk in `find_free_block` followed it.

**The mechanism — a race, not an overflow.**  The enriched trace
and the coalesce arithmetic trace together proved the merge
arithmetic correct at every step.  The "corruption" was
`heap_validate` running on a list that an interleaved `kfree`
(from a tick's scheduler) was midway through mutating.  The heap
had a race.

**The fix.**  `find_free_block`'s walk is bounded; a cycle prints
and returns NULL (`1c7fe91`).  `split_block` refuses a self-link
and a `new_block <= block`.  `kfree` holds `cli` across
`coalesce_blocks()` and `heap_validate()`, caller flag
saved/restored.  The heap instrumentation (`heap_validate`'s
`check=`/`exp=`/`got=`, the enriched corrupt print,
`HEAP_TRACE_MERGE`) is gated, default off.

**Verified.**  pipe7e 10000 iterations clean, no `HEAP:` lines,
no hang; canary 28/28, `test.sh` 57/57.  The run is faster than
the corrupt runs — the allocator was spending time on the broken
list.

### The selftest regression and the queue asymmetry (session 68)

**The shell fallback.**  `process_exit`'s kernel-shell fallback
pre-set the shell to `PROC_STATE_RUNNING` before calling
`scheduler_switch_to`, whose tail check refuses a non-`READY`
next.  The switch was refused, the halt path ran, and
`[exception_de]` halted the selftest.  Restructured: set
`READY`, let the scheduler do the transition, fall through to
idle on refusal instead of halting (`e9d6b7a`).

**The failed-create queue.**  `process_create`'s four failure
exits made the PCB `UNUSED` but did not remove it from the ready
queue.  The timer found it — `TIMER-REFUSE: next pid=0
name=fail_slot rsp=0` — and refused it.  Containment held, but a
dead PCB on the queue is a queue bug.  Added
`scheduler_ready_queue_remove` to all four exits (`19719b7`).
The invariant is now complete: every path that makes a PCB
`UNUSED` removes it from the queue — `process_reclaim`,
`process_destroy`, and the failure exits.

### The run script (session 68)

`run` was a comment/uncomment menu extracted with `sed`/`grep`:
no default worth the name, no GDB stub in the headless line, and
the build output went to the terminal instead of the capture.
Rewritten (`81f5a3c`): `./run` / `./run menu` / `./run <name>` /
`./run help`, every mode has the GDB stub on `:1234` and writes
**everything** to `capture.txt` — the build output, the QEMU
command, the serial console.  `set -o pipefail` makes a failed
`make` fail the pipeline before QEMU launches on a stale image.
The default is `clean-nofreeze`: full rebuild, SDL, no
`-no-reboot -no-shutdown`, so `reboot` works.  The freeze modes
(`clean-sdl` and the rest) keep the pair — that is what every
`C`/heap diagnosis run used.

### The milestone's own lesson

**A guard is not a fix, and a fix is not a guard.**  The `C`
resisted four sessions because each attempt added a guard
(refuse a `RUNNING` next, refuse `rsp == 0`, reserve the frame
top) and each guard converted a fatal halt into a different
fatal halt or a hang.  The fix was not a guard at all — it was
closing the window the guard was checking across.  A guard that
runs *after* the state can change is not a guard; it is a
telescope pointed at the past.  The tail check is correct because
it is inside the same `cli` as the dispatch.  Everything else was
a theory about the frame that the watchpoint disproved.

The second lesson is the heap's: **an instrument on the failure
path is signal, and its absence is not proof.**  `HEAP: corrupt
at kfree` fired on blocks that were consistent when read, and the
enriched print named the field; only the merge trace showed the
arithmetic was right and the *timing* was wrong.  The diagnostic
that ends a mystery is the one that prints the values it compared,
not the one that prints a block address.

### Verification

Both boot paths, every commit: `selftest` 17/17 rising to 18/18
(the `exception_*` and `create_fail` rows), `canary` 15/15,
`canary --full` 28/28, `test.sh` 57/57.  pipe7e: 250000
iterations at 100 kHz (PIPE7E-ALL-PASS, zero events) and clean
past 77500 at 100 Hz.  The heap: 10000 iterations clean, no
`HEAP:` lines.  No `#PF`, no `#DF`, no `#GP`, no
`Unknown syscall:`, no `PMM: WARNING - Double free`.

**What is open.**  7e stays open in `third_party/` (ash's stale
`jmp_buf`); the kernel-side faces are closed.  Item 12 (signal
delivery) is the path to making 7e stop costing whole runs and
the `SIGPIPE` gap.  Items 10 and 13 remain.  The heap had a race,
not an overflow — the bounded walk and split guard remain as
protection against a future corruption that no run has produced.

### donix `v0.6.7` through `v0.6.12`

This continues [`migration-history.md`](migration-history.md),
which covers A1-A6 through `v0.6.0`.  Each version below was a
milestone; the per-session narrative is in
[`session-log.md`](session-log.md), and the annotated `v*` tags
carry the milestone summaries.  For donix's current state, see
[`../handoff.md`](../handoff.md).

---

## v0.6.12 — item 7 closed; the process failure paths

**A correctness milestone.**  Five commits, and every one of them is
about a kernel path that could not report a failure, or a leak on a
path that does.  No new subsystem, no new applet, no new syscall.

**Item 7: the silent `vmm_map_page*` returns.**  `vmm_map_page` and
`vmm_map_page_in_cr3` had six sites — the PDPT, PD, and PT
allocation paths in each — where a failed page-table allocation
meant no mapping was made and the caller could not tell.  The change
was the one item 7 named as the honest fix, in two commits:

- The **signature change** — both functions return `int` (0 =
  mapped, -1 = allocation failed) — with the six `if (!phys)
  return;` sites becoming `return -1;`.  No caller handled the
  return in this commit; the point was to isolate the signature
  change from the caller changes, and to ship the `isr14_handler`
  diagnostic item 7 required be in place first.  **The session-45
  virtual-1 fault did not reproduce.**
- The **nine callers** handle the `-1`, in the shape each caller
  dictated: `ensure_hhdm_mapped` and `vmm_init`'s identity map
  **halt** (boot paths, no caller to report to); `elf_load_into_process`,
  `process_create`'s stack loop, `heap_extend`, `sys_mmap`,
  `sys_brk`, `exec_alloc_user_stack`, and `sys_fork`'s
  `EAGER_COPY_REGION` macro **recover**.  `heap_extend` is the
  nontrivial one: it unmaps and frees its partial region, because
  nothing else tracks those pages.

**`process_create`'s failure exits, and the unchecked clone.**
Reading the nine call sites turned up `pcb->cr3 =
vmm_clone_page_table(current_cr3);` with no check — a `0` return
became `pcb->cr3 = 0`, and the next `vmm_map_page_in_cr3` walked
page tables at `HHDM_START + 0`.  Worse than a leak.  The same
function's failure exits leaked the cloned page-table hierarchy.
Both are fixed by a new `process_free_clone(cr3)` and full cleanup
on every failure exit.

**The exit-path page-table leak.**  `process_reclaim` and
`process_destroy` freed a process's ELF pages and its kernel stack
slot but abandoned the cloned page-table hierarchy.  Both now call
`process_free_clone`.  This is what first *exercised* the helper
commit 3 added — `exec_churn` ran it 24 times, and the `k`-shell
selftest's three exception children 3 more, with no fault and no
double-free.

**Dead code.**  `f_stat_with_retry`'s `has_drive` computed a
variable, cast it to `(void)`, and did nothing with it; the whole
`if` block was dead.  Deleted.

**The milestone's own lesson**, and the reason it is recorded here:
a helper written for failure paths is **correct by inspection
only** until something on a hot path calls it.  Commit 3's
`process_free_clone` and commit 4's use of it on the exit path are
the two states; only the second was verified, and `exec_churn` is
what made it so.  See `docs/gotchas.md`, "A function that has never
run is correct by inspection only."

**Verification.**  Both boot paths, every commit: `canary` 15/15,
`canary --full` 28/28, `selftest` 17/17, `exec_churn` 24 rounds all
children exit 0, `mmap_stress` 16 rounds.  No `#PF`, no `#DF`, no
`#GP`, no `Unknown syscall:`, no `PMM: WARNING - Double free`.

---

## v0.6.11 — the seam, `/proc`, and the PMM fix

**The pathname dispatch seam (session 44).**  The first feature the
previous architecture could not express at all: `resolve_at` returns
a backend tag from a path's first component, and FAT / DEV / PROC
are selected by it.  No VFS — no inode, no vnode, no mount table.
Its first three consumers: `/dev/null`, `/proc/self/status`, and
`/dev/console` + `/proc/self/fd/N` (which is what makes `tty` print
`/dev/console`).  `/dev/null`'s old exact-path predicate was deleted
in the process.

**`/proc` per-pid, and `ps` (session 47).**  `/proc` became a
listable directory: `readdir("/proc")` returns `self` and the live
pids, and `/proc/<pid>/stat`, `status`, and `cmdline` each read back
real per-process fields.  `ps` and `pstree` were enabled and work.
The bug worth remembering: `ps` printed a header and no rows because
libbb's `procps_scan` does `stat("/proc/<pid>/")` — **with a
trailing slash** — and skipped the entry when it failed.  donix
served the per-pid *files* but not the per-pid *directory*.

**The PMM zone-scan fix (session 48).**  The intermittent boot-time
`#PF` at `0x400000` — first boot after adding userland ELFs, cleared
by the second or third — was **not** the silent `vmm_map_page*`
returns.  It was the PMM zone scan: `pmm_alloc_page` started at a
cursor and moved one direction, so a page free on the far side was
never found.  Adding ELFs pushed the cursor past such pages, and the
boot ELF load's page-table allocation returned 0 with
`pmm_free_pages` healthy.  `pmm_scan_zone` now wraps.  Ten-plus
consecutive boots on the trigger image, no fault.

**What `v0.6.11` did not close** (the list as it stood at the
`v0.6.11` bump; item 7 was closed in `v0.6.12`):

- `open("/proc/<pid>", O_DIRECTORY)` — `ps` stats the directory, it
  does not open it.
- `readdir("/dev")` — `/dev` is not a directory; `ls /dev` fails.
- Symlinks (item 8) — design recorded, buildable as the seam's next
  consumer.
- `sys_gettimeofday` (99) — so `ps -l` is off.
- `/etc/passwd` — so `ps`'s USER column shows the numeric uid.

---

## v0.6.10 — `realpath`, the `readlink` errno, `/dev/null` (shipped)

A tail on `v0.6.9` with no new subsystem.  `CONFIG_REALPATH=y`
(config-only — the applet routes through musl's `realpath()` plus
`libbb`'s `xmalloc_readlink` and `getcwd`, all present).  The
`readlink` errno split (`-ENOENT` for a missing path, `-EINVAL` for
an existing non-symlink) was already in the tree and was closed with
a run behind it, `readlink_errno.c`.  `/dev/null` works across
`open`/`stat`/`access` (`2>/dev/null` discards stderr), which was
the seam's first consumer.

---

## v0.6.9 — envp, `/usr/bin`, the shim removal (shipped)

Opened in session 42 (`20261001-envp`) as three subjects and grew,
in the same session, into a large milestone: a regression test, a
rewrite, a page-table bug fix, four syscalls, ten busybox applets,
three small gaps, and four gotchas.  The milestone's own scope
followed each thing the last one exposed.

**envp.**  `sys_execve` ignored its third argument, so every program
ran with an empty environment.  It now passes the caller's `envp`
through verbatim, Linux-style — the shell builds the environment,
the kernel carries it.  `export FOO=bar` in ash, then `echo $FOO`,
prints `bar`.  `env` and `printenv` are enabled and read it.
Regression-tested by `tests/envp_step1.c`.

**The `/usr/bin` layout.**  The donix-native ELFs moved from the FAT
root to `/usr/bin`, `busybox` stays in `/bin`, `/tmp` was created.
This is the split the two shells' search rules need: ash's applets
never consult `PATH`, so busybox always wins there; `musl_sh`
searches `/usr/bin` first, so a bare name resolves to the
donix-native tool.  Every binary is staged **bare** — no `.ELF`
suffix.

**The shim removal.**  `sys_execve`'s bare-name attempt — uppercase
the name, append `.ELF`, try the root and `/bin` — is gone, along
with the helpers it called and a `f_stat` retry that used the same
guesser.  What remains is (a) the path as given and (b) the `"0:"`
prefix translation for an absolute path, which is the smallest the
shim can be without a VFS.  `musl_exec2` now asserts that a bare
name does *not* resolve.

**The canary is a program.**  `tests/canary.c` runs every
non-interactive canary row and reports pass/fail.  It replaces the
hand-typed list, which had already drifted once.

**The bugs and the syscalls.**  A `puts_raw` with an input-only
`syscall` asm block let GCC issue a second syscall with the first's
return value as its number; a set of hand-counted string lengths
were wrong by one or two.  Both are gotchas now.  An intermittent
`#PF` at `0x400000` — the huge-page split in `vmm_map_page_in_cr3`
silently returning on allocation failure, leaving a supervisor page
where a user page was asked for — was found, diagnosed, and fixed.
Four syscalls landed: `readlink` (89, honest `-EINVAL` — no
symlinks), `clock_gettime` (228, from `g_ticks`), `nanosleep` (35, a
`g_ticks` deadline loop), and `munmap` (11, for real — the stub was
a leak that musl's mallocng actually calls).

**Ten busybox applets.**  `basename`, `dirname`, `unlink`, `ttysize`,
`tty`, `arch`, `mktemp`, `sleep`, `usleep`, `truncate`, plus `env`
and `printenv` — all config-only once the syscalls above existed.
(`truncate` needed no new syscall: it uses `ftruncate` (77), not
`truncate` (76), which the handoff's table had wrong.)

**Three small gaps.**  `munmap` (counted above), `fcntl` now accepts
fd 0/1/2 for all subcommands, and Ctrl-`[` produces ESC (0x1B).

The live state, the canary rows, and the NEXT SESSION list are in
[`handoff.md`](handoff.md).

---

## v0.6.8 — the `*at()` family (shipped)

Opened in session 39 (`20260930-at`), shipped and pushed in session
42.  The theme was the `*at()` family: one path resolver shared by
every syscall that takes a dirfd.  `resolve_at` is that resolver;
`newfstatat` (262), `openat` (257), `unlinkat` (263), `faccessat`
(269), and `utimensat` (280) all route through it.  The stat family
is inverted to wrappers as on Linux.  busybox `find` (with `-type`)
is enabled and works.

**Every `*at` syscall with a consumer is implemented.**  The
remaining ones have no caller in the current applet set, verified
by reading busybox's source:

- `mkdirat` (258) — no consumer; `tar`/`cpio`/`unzip` would, but
  each needs a VFS first.
- `renameat` (264) — no consumer; `rm`-style code uses `rename`
  (82).
- `linkat`/`symlinkat`/`readlinkat` (265/266/267) — FAT has no
  links; no consumer is possible.

See `open-issues.md` and `gotchas.md`, "A consumer inferred from
behavior is not a consumer," for the `unlinkat` finding: it was
added on the belief that `rm -r` needs it, and `rm -r` does not.

---

For versions after `v0.6.12`, see the `v*` tag annotations and
[`session-log.md`](session-log.md); this file is not extended per
session.
