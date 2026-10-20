# The kernel concurrency contract

This file is the durable statement of the rule every change to shared
kernel state must follow.  It exists because the project has paid for
this rule in real bugs -- the `C` (stale-ZOMBIE dispatch), the heap
cycle, the lost pipe wake, the `current_process` dual-copy -- and
because the next features (signals, the VFS) add new writers that will
break it if they are designed without it.

`handoff.md` is rewritten every session; this file is not.  When the
handoff says "honor the invariant," it means this file.

---

## The rule

**A mutation of shared state and the check that authorizes it are one
critical section.**

Concretely:

1. **`state`, `block_kind`, and ready-queue membership are changed only
   under `cli`.**  That is the ready queue, the process state machine,
   and the block/wake bookkeeping.  No tick, no fault, no other process
   may interleave between the check ("is this process READY?") and the
   act ("dispatch it" / "remove it from the queue").

2. **A dispatch's check and act are one critical section.**  The
   selection and the switch are atomic with respect to interrupts.
   `process_yield` holds `cli` from before `scheduler_ready_queue_next`
   through `scheduler_switch_to`; `scheduler_switch_to`'s tail refuses
   a non-READY target and restores `current_process = prev` under the
   same protection.

3. **A refused dispatch restores `current_process`.**  If the tail
   check (`state != PROC_STATE_READY`) refuses, `current_process` is
   set back to `prev`, `sti`, return.  There is no path that leaves
   `current_process` naming a process the scheduler did not switch to.

4. **Heap list mutation and validation are one critical section.**
   `kfree` holds `cli` across `coalesce_blocks()` and
   `heap_validate()`.  `find_free_block` is bounded (a cycle returns
   NULL, not a hang); `split_block` refuses a self-link.

5. **`current_process` has one source of truth.**  `process_destroy`
   clears both copies via `scheduler_set_current`.  Any write to
   `current_process` goes through the accessor; no direct writes.

Where each of these was learned:

- `docs/gotchas.md` -- "the rule" (check and act are one critical
  section), the `current_process` dual-copy (session 65).
- `docs/CHECKLIST.md` -- the `cli`/`sti` fixes for the PMM allocator
  (reentrancy), the shared print lock, and the scheduler.
- `docs/dons-os-history.md` -- the historical race writeups, including
  the `current_process = next` / `context_switch` race.
- `handoff.md` -- "THE INVARIANT," which points here.

---

## Signals: the first asynchronous state writer

Signals are the first feature that changes a process's state **from
outside its own execution** -- a tick, a fault, or another process.
That is exactly the shape of the `C` and the heap cycle.  Before any
signal code is written, these questions must be answered in the design:

1. **Where is the safe point?**  A running process cannot be mutated
   out from under itself.  Delivery must mark a **pending** flag and
   arrange for the process to observe it at a well-defined point:
   - syscall return,
   - interrupt return (`iretq` path),
   - fault entry (for `SIGSEGV`/`SIGFPE`/`SIGBUS`),
   - or `rt_sigreturn` setup after a handler returns.

   The chosen point(s) must be named, and the observation must happen
   before the process resumes user mode.

2. **What is under `cli`?**  The write that sets the pending flag, and
   any state/queue change it implies (e.g. waking a blocked process
   because a signal arrived), must be one critical section.  Setting a
   flag on a running process is cheap; **waking a blocked process is a
   state change** and follows rule 1.

3. **Does delivery dispatch?**  If a signal causes a blocked process to
   become runnable, that transition goes through
   `scheduler_ready_queue_add` under `cli`, and any dispatch it
   triggers goes through `scheduler_switch_to` -- including the tail
   check and the `current_process` restore.  A signal path must not
   bypass the dispatcher.

4. **`rt_sigreturn` and the dispatch tail.**  The handler-return frame
   is set up on the user stack and restored on `rt_sigreturn`.  That
   path must not race the tick.  The frame write is under `cli`, or the
   process is in user mode where the tick cannot see it mid-write.

5. **What is the pending representation?**  A per-process bitmask, a
   queued list, or both (mask for pending, queue for `siginfo`).  The
   representation must be readable under `cli` and must not be a
   pointer into user memory that another writer can free.

6. **What does "unobserved" mean?**  7a/7b's lesson: a fault that stops
   reproducing is not closed.  The same applies to signal races -- a
   clean soak is evidence, not proof.  The design should make the
   pending-flag write and the safe-point read **inspectable** (a
   counter, a trace), so a recurrence has a signature.

**Status: design not yet written.  No signal code lands until these are
answered here or in a linked design note.**

---

## VFS: path resolution and file-slot state

The VFS (see `docs/open-issues.md` item 8) is the next structural
change.  It adds a mount table, inode operations, and per-slot
refcounts.  The contract applies to each:

1. **Mount table and inode-op mutation are one critical section.**
   Adding/removing a mount, changing an inode's ops, or transitioning a
   path's backend is a shared-state write.  It happens under `cli`, or
   under the same lock discipline the existing pathname-dispatch seam
   uses.

2. **`file_slot_t` refcount changes are atomic with the operations
   that read them.**  `put_file_slot` and the close path must not
   interleave with a fork that copies the slot table.

3. **The pipe-wake coupling is load-bearing.**  `put_file_slot`'s pipe
   wake is coupled to `sys_close`'s wake (see the `put_file_slot`
   comment in `user_syscall.c`, and `docs/open-issues.md`).  Read that
   comment before touching either.  A pipe slot whose `end` is neither
   READ nor WRITE is refused, recorded, and left alive -- a leak is
   survivable, a lost wakeup is not.

4. **`..` canonicalization and symlink resolution happen in one
   place.**  Item 8's whole point: do not implement symlink handling in
   individual syscalls.  The VFS owns `vfs_resolve(path, follow_final)`
   and every path-taking syscall calls it.  `realpath ..`'s bug
   (`docs/open-issues.md`) lives in the shared resolver and should be
   fixed there.

---

## Reviewer checklist

Every change that touches shared kernel state must answer these.  A
"no" is a bug.

1. **Is every write to `state`, `block_kind`, or ready-queue membership
   under `cli`?**
2. **Is the check that authorizes the write in the same critical
   section as the write?**
3. **If a dispatch is refused, does `current_process` get restored?**
4. **Does the new writer go through the existing dispatcher (tail
   check, restore), or does it bypass it?**
5. **Is there a new async writer (tick, fault, signal, another
   process)?  If so, where is its safe point, and is the observation of
   its effect under `cli`?**
6. **Does the change add a shared list or refcount?  Is every mutation
   of it one critical section, and is the read side atomic with the
   mutation it depends on?**

---

## Provenance and the "not proven" rule

This contract exists because the project closed a family of races in
v0.6.13 (`C`, heap cycle, pipe lost-wakeup, queue mutators) and the
lesson is: **do not call a race closed on one clean run.**  The
handoff's verification section says the fast and slow runs are "strong
evidence, not proof."  The same standard applies to every future fix:
a clean soak at the relevant rate is evidence; a 1-in-500000 race that
did not fire is not proof it cannot.

When a new feature adds a writer, the design must say **how it will be
tested** and **what the failure signature looks like**, so a recurrence
is diagnosable.  That is part of the contract, not an afterthought.
