# Signal delivery: design

Status: **design, slice 1 not yet written.**  This answers the six
questions in `docs/contract.md`'s Signals section, which says no
signal code lands until they are answered here.  Read the contract
first; read this second; then the slice list at the bottom.

The single decision that shapes everything below: **a signal is
raised where it is generated, but delivered only on the way out to
user mode.**  That is one delivery mechanism, not three.

---

## 1. Where is the safe point?

Two points, and only two:

- **syscall return**, in `user_syscall_entry.asm` after
  `syscall_dispatch` returns and before `sysret`.  The stub already
  owns a complete frame there: `kernel_stack_top` minus 128 bytes,
  with every GPR, the user RIP at `-56`, user RFLAGS at `-64`, and
  user RSP at `-72` (see `process_fork_copy_frame` in `process.c`,
  whose comment enumerates the offsets).  Delivering here means
  rewriting *that* frame -- the same operation `sys_execve` already
  performs for `argc`/`argv` -- and pushing the handler frame onto
  the *user* stack.
- **interrupt return**, in the `iretq` path
  (`context_switch.asm` / `irq0_stub`'s `POP_ALL_GPRS` + `iretq`).
  A tick-delivered signal is observed here.  The frame shape is the
  20-slot layout that `process_create` builds and
  `process_fork_copy_frame` copies: 15 GPRs then RIP, CS, RFLAGS,
  RSP, SS.

**Fault entry is a raise point, not a delivery point.**  For
`SIGSEGV`/`SIGBUS`/`SIGILL`/`SIGFPE`, the fault handler sets the
pending bit and then returns through the *same* return path; the
signal is delivered on the way out, exactly as a tick-delivered one
is.  One delivery path, so the frame code is written and tested
once.

The observation must happen **before the process resumes user
mode** -- that is the contract's wording, and both points satisfy
it.

## 2. What is under `cli`?

- **The pending-bit write.**  `kill(2)`/`tgkill(2)` sets the bit
  under `cli`.  It is one `uint64_t` on `pcb_t`, no list.
- **Any state change it implies.**  If the target is BLOCKED and
  the signal is not blocked, the raise transitions it to READY and
  calls `scheduler_ready_queue_add` -- rule 1 of the contract.
  That whole check-and-act is one critical section, the same shape
  as `pipe_wake_waiter`.
- **The delivery frame write**, on the syscall-return path, is
  already inside the stub's `cli` tail (`user_syscall_entry.asm`
  does `cli` before the exit checks and does not `sti` until
  `sysret` restores the user's IF).  On the `iretq` path, the
  process is in user mode when the tick fires, so no user-visible
  structure is mid-write.

## 3. Does delivery dispatch?

Only when the signal *wakes* a blocked process.  Then:
`state = READY`, `block_kind = NONE`,
`scheduler_ready_queue_add(target)` -- under `cli`.  The dispatch
that may follow is `scheduler_switch_to`, with its existing tail
check (`state != READY` refuses) and its existing
`current_process = prev` restore.  **No signal path calls
`context_switch` directly, and none bypasses the dispatcher.**

A signal delivered to the *running* process does not dispatch at
all: it rewrites the frame the process is about to return through.

## 4. `rt_sigreturn` and the dispatch tail

The handler-return frame is written on the **user** stack (below
the current user RSP), and the signal frame's saved RIP points at
the installed handler.  The process then resumes in user mode.

`rt_sigreturn` (syscall 15) is a normal syscall: it enters through
`user_syscall_entry.asm`, so the tick cannot see it mid-write --
the kernel runs with interrupts off across the syscall-return
tail, and user mode cannot be preempted between the frame's fields
being written by the kernel and read by the handler.  **The
`rt_sigreturn` body restores RIP/RSP/RFLAGS and the 15 GPRs from
the frame and rewrites the syscall-return frame; it does not
dispatch.**

The one hazard is a handler that faults: the handler's fault raises
again, and the raise must not re-deliver into the handler forever.
`pcb_t` already carries `in_signal_handler` for exactly this (the
7e instrument's re-entry guard); slice 1 reuses it, and its
semantics are stated in the `pcb_t` comment.

## 5. What is the pending representation?

**`uint64_t sig_pending` -- a bitmask, one bit per signal number
(1..64), appended at the end of `pcb_t`.**  Rules:

- Written under `cli` by the raise points.
- Read at the safe points, under the same `cli`.
- **Not a queue in slice 1.**  `siginfo` (`rt_sigqueueinfo`,
  `SA_SIGINFO` handlers) is a later slice; a queue added now would
  be designed from a single example.
- Not a pointer into user memory -- it is a field on `pcb_t`, and
  the PCB is kernel memory the user cannot free.
- Appended at the end, after `user_rsp`, so no offset
  `context_switch.asm` reads (`block_kind` at 0x158, `user_rsp` at
  0x610) moves.  The `_Static_assert` block in `process.c` pins
  those; a new field after `user_rsp` needs no assert, same rule as
  `cwd`, `fault_signal`, `signal_handler`, `in_signal_handler`.

`sig_blocked` (the mask set by `rt_sigprocmask`) is a second
`uint64_t`, same placement rule.  Slice 1 stores it; slice 2 makes
delivery consult it.

## 6. What does "unobserved" mean?

The pending-bit write and the safe-point read get a counter and a
trace, per the contract's "how it will be tested" clause:

- `sig_raise_count`, `sig_deliver_count` -- `uint64_t` globals,
  incremented at the two sites, readable from the kernel shell.
- A `race_record` call at the raise site and at the delivery site,
  reusing the existing ring (`race_probe.h`) and a reserved
  `RACE_SITE_SIGNAL_*` pair, so a recurrence has a signature.

A clean soak is evidence, not proof.  The counters make a
"delivered but never raised" or "raised but never delivered"
divergence visible in one `ps`-adjacent dump, which is what the
7a/7b lesson asks for.

---

## Slices

Each is one commit, verified by running, per the working style.
Later slices may be revised when an earlier one lands.

**Slice 1 -- the pending mask, no delivery.**
`sys_rt_sigaction` stores a real per-process handler table (it
currently stores one `uint64_t` in a debug hook; `pcb_t` already
has `signal_handler[64]`, so this is making the existing field
authoritative).  `sys_rt_sigprocmask` stores `sig_blocked`.
`kill(2)` (62) and `tgkill(2)` (234) set `sig_pending` and, if the
target is BLOCKED and the signal is unblocked, wake it per
question 3.  **No delivery.**  Verified by: `sigsegv_probe` still
passes (handler installed, fault redirects, process exits);
a new `kill_self` test installs a handler, calls `kill(getpid(),
SIGUSR1)`, and the kernel-shell counter dump shows
`sig_raise_count == 1, sig_deliver_count == 0`.  Failure
signature: the counters stay 0, or the wake does not happen.

**Slice 2 -- delivery on syscall return only.**
`user_syscall_entry.asm` observes `sig_pending & ~sig_blocked`
after `syscall_dispatch` returns, and if nonzero, builds the
handler frame on the user stack and rewrites the syscall-return
frame.  `rt_sigreturn` (15) restores.  Verified by: `kill_self`'s
handler runs and prints, then `sigreturn` resumes the caller.
Failure signature: the handler runs but the caller does not
resume (frame wrong), or the process faults at a user address
(the frame's saved RIP/RSP is wrong).  This is the slice the 7e
history warns about; it needs the entry stub in front of the
author.

**Slice 3 -- delivery on `iretq`.**
The interrupt-return path observes the same mask and builds the
same frame.  Verified by: a busy-loop process gets `SIGUSR1` from
another process and its handler runs while it is in user mode.
Failure signature: the frame is built but `iretq` resumes at the
wrong RIP.

**Slice 4 -- fault raises.**
`SIGSEGV`/`SIGBUS`/`SIGILL`/`SIGFPE` set the pending bit at fault
entry instead of killing; delivery goes through slice 2/3.
Replaces the 7e `signal_maybe_redirect` instrument.  Verified by:
`sigsegv_probe` now returns from its handler instead of exiting.
This is where `in_signal_handler` gets its real semantics.

**Slice 5 -- `SIGPIPE`, `SIGCHLD`, default actions.**
`SIGPIPE` on the `-EPIPE` write (open-issues item 5); `SIGCHLD`
on child exit; the default-action table (terminate, ignore, stop,
core).  Verified by: `yes | head -n 1` still works; a program that
does not handle `SIGPIPE` dies.

Not in any slice yet: `siginfo`, `SA_SIGINFO`, `sigqueue`,
`SA_RESTART` semantics for interrupted syscalls, `SIGSTOP`/`SIGCONT`
job control (open-issues item 13).

---

## What this design explicitly does not do

- It does not add a signal queue.
- It does not implement `sigaltstack`.
- It does not deliver to a process mid-syscall (the syscall
  completes; delivery happens on the way out).
- It does not change `sys_wait4`'s existing signal-kill status
  encoding (`fault_signal`), which stays as the parent's view.

Those are later slices, and each gets its own design note.
