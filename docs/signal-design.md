# Signal delivery: design

Status: **slices 1-3 are committed** (slice 1: `588ae53`, `3f60a19`,
`ce69a7b`; slice 2: `866588f`, `d2126d8`; slice 3: `17ffeb9`,
`a1c90ff`, `c4b3ba7`); **slice 4 is next.**  This answers the six
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

**Slice 1 -- the pending mask, no delivery.  (DONE)**
`sys_rt_sigaction` stores a real per-process handler table.
`sys_rt_sigprocmask` stores `sig_blocked`.
`kill(2)` (62) and `tgkill(2)` (234) set `sig_pending` and, if the
target is BLOCKED and the signal is unblocked, wake it per
question 3.  **No delivery.**  Verified by: `sigsegv_probe` still
passes; `kill_self` (user shell) and `killtest` (kernel shell,
added because the two shells are mutually exclusive per boot) both
raise the signal.  The counter dump (`sigdump`) shows
`sig_raise_count > 0, sig_deliver_count == 0`.
Note: the counters are global and accumulate across raises; the
clear in `process_reclaim`/`process_destroy` zeroes the per-process
mask but not the global counter.

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

**`rt_sigreturn` (15) is ADDED in this slice.**  It does not
exist today; syscall.h skips 15, the dispatcher has no case for
it, and there is no body.  Slice 2 adds `SYS_RT_SIGRETURN 15`,
`sys_rt_sigreturn`, and a dispatcher case, alongside the frame
builder.  musl's `__restore_rt` is three instructions
(`mov $15, %rax; syscall`) and reads no arguments, so the frame
is located from the user `%rsp` saved by the syscall stub --
`sys_rt_sigreturn` reads `ucontext_t` directly from that saved
RSP, with no offset arithmetic and no `%rdi` dependency.

**The user-stack frame is NOT the kernel-side 20-slot frame.**
It is `restorer` (8 bytes) followed by a musl `ucontext_t`
(816 bytes).  The handler returns by `ret`-ing to `restorer`,
which advances the user RSP by 8 before `syscall`, so at
`rt_sigreturn` entry the user RSP points at the `ucontext_t`'s
first byte.  The kernel-side 20-slot frame is what the stub
rewrites from that `ucontext_t` -- two distinct frames, one on
each side of the syscall boundary.

`ucontext_t` layout (musl x86_64, `_GNU_SOURCE` form, which is
what musl is compiled with internally):

    offset  size  field
       0      8    uc_flags
       8      8    uc_link
      16     24    uc_stack (ss_sp 16, ss_flags 24, ss_size 32)
      40    256    uc_mcontext -- gregs[23] at 40..223,
                                fpregs at 224, __reserved1 at 232
     296      8    uc_sigmask
     304    512    __fpregs_mem
     816          total

`gregs[]` indices, each 8 bytes, base at ucontext+40:
R8=0 R9=1 R10=2 R11=3 R12=4 R13=5 R14=6 R15=7
RDI=8 RSI=9 RBP=10 RBX=11 RDX=12 RAX=13 RCX=14
RSP=15 RIP=16 EFL=17 CSGSFS=18 ERR=19 TRAPNO=20
OLDMASK=21 CR2=22.

**The restorer is read from `sa_restorer`.**  musl's
`k_sigaction` on x86_64 is 32 bytes: handler (0), flags (8),
restorer (16), mask[2] (24).  `sys_rt_sigaction` reads all 32
and stores `handler` and `restorer` per signal; a new
`signal_restorer[64]` array on `pcb_t`, appended after
`sig_blocked`, holds the restorer.  The kernel does not supply a
fixed `__restore_rt` address: that would silently depend on
every signal-using binary being static non-PIE, which is not a
pinned build invariant.  Reading `sa_restorer` is the shape
musl was compiled against and is what keeps slice 2 working if
a PIE binary ever lands.

**Deliver point.**  In `user_syscall_entry.asm`, between the
exit-check `cli` (after `cmp rbx, 231 / je .handle_exit`) and
the first `pop`, with the frame at post-`add rsp, 8` offsets:
`[rsp+48]` user RSP, `[rsp+56]` user RFLAGS, `[rsp+64]` user
RIP, `[rsp+104]` rdi, `[rsp+112]` rbx.  Reading these before
the first `pop` is what makes the frame valid; reading them
after the exit-check `cli` keeps the deliver block inside the
stub's interrupt-off tail, as §1 requires.

`sa_mask` is read into a scratch field and ignored: slice 2
delivers one signal at a time, with no `siginfo`, no deferral,
and no `SA_RESTART`.  Deferral and `sa_mask` are a later slice.
Widening `sys_rt_sigaction`'s `oldact` write to 32 bytes and
widening `sigsegv_probe.c`'s test struct to match are a
follow-up commit; slice 2 only widens the read path, so a caller
that reads `oldact` today gets the same 16 bytes it always did.

**Slice 3 -- delivery on `iretq`.  (DONE: `17ffeb9`, `a1c90ff`,
`c4b3ba7`.)**
The tick path observes the mask in `timer_preempt_handler` and
builds the same user-stack frame as slice 2, rewriting the
20-slot iretq frame in place.  Three deliver sites, each targeting
the frame it resumes from: the identity self-pick (`next ==
current`, target `stack_pointer`), the by-value self-pick
(`next->rsp == stack_pointer`, target `stack_pointer`), and the
dispatch branch (`return next->rsp`, target `next->rsp`).  The
refuse paths do not deliver: `current` resumes its own frame
unchanged, and its pending signal is observed on its next
dispatch.

The fix that mattered: `signal_deliver_frame_iretq` takes the
target PCB as a parameter, not `process_get_current()`.  The tick
path runs outside any syscall, and the scheduler's and process.c's
`current_process` are two copies kept in step by convention
(contract rule 5); passing the target makes the helper correct
regardless.  `process_get_current()` stays correct for slice 2,
which runs inside the syscall.

Verified by `sigspin`: a child in a no-syscall loop gets `SIGUSR1`
from another process, the handler runs on the tick,
`sys_rt_sigreturn` resumes the loop, the child exits 0, the parent
reaps.  Failure signature: handler runs but the loop does not
resume (the frame's saved RIP/RSP is wrong), or the tick never
delivers (no `got SIGUSR1`).  Known limit surfaced by the test:
`kill(pid, SIGKILL)` does not terminate (default actions are slice
5), so `sigspin`'s child exits itself.

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
