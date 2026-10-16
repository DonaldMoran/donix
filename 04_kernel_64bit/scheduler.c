#include "include/scheduler.h"
#include "include/serial.h"
#include "include/vga.h"
#include "include/user_msr.h"
#include "include/trace.h"
#include "include/race_probe.h"

/*
 * SW2_TRACE -- dispatch-trace instrumentation for scheduler_switch_to.
 *
 * Default 0: the SW2-* prints compile to nothing, so the dispatcher is
 * as fast as it was before the trace and the serial console is clean.
 * Set to 1 for one build when diagnosing a dispatch-path problem.
 *
 * The three traced sites (entry, after the current_process assignment,
 * and immediately before context_switch) fire on EVERY dispatch, so
 * they are success-path noise and are gated.  The zombie/terminated
 * refuse print (SWITCH-DEAD) is failure-path signal and is NOT gated.
 */
#ifndef SW2_TRACE
#define SW2_TRACE 0
#endif

#if SW2_TRACE
#define SW2_PRINT(...) do { serial_lock(); __VA_ARGS__; serial_unlock(); } while (0)
#else
#define SW2_PRINT(...) do {} while (0)
#endif

#define KERNEL_BASE 0xFFFFFFFF80000000ULL

static pcb_t* ready_queue_head = NULL;
static pcb_t* ready_queue_tail = NULL;
static pcb_t* current_process = NULL;

static uint64_t schedule_count = 0;
static uint64_t yield_count = 0;

extern void context_switch(pcb_t* prev, pcb_t* next);
/*
 * Hand-off entry point: like context_switch, but skips the
 * save side entirely.  For process_exit, which is
 * __attribute__((noreturn)) and must not build a resume
 * frame whose RIP is its own hlt guard.  See the comment on
 * context_switch_no_save in context_switch.asm.
 */
extern void context_switch_no_save(pcb_t* prev, pcb_t* next);

pcb_t* scheduler_ready_queue_peek_next(void) {
    return ready_queue_head;
}

void scheduler_ready_queue_add(pcb_t* process) {
    /*
     * CRITICAL SECTION.  The whole function, not just the link.
     *
     * _add is called from process_yield with interrupts ON, and
     * from the timer's process_wake_all_blocked and from
     * scheduler_switch_to with interrupts OFF.  With interrupts on,
     * a tick can fire in the middle of this function -- and that
     * tick runs process_wake_all_blocked, which calls _add and
     * _remove.  So one queue mutation can nest inside another, and
     * the queue is left inconsistent (a tail move with no matching
     * clear).  The 2026-10-14 ring showed exactly that: an add's
     * entry record, then the next add's entry record with the tail
     * moved back one node, and no _remove record in between --
     * because the remove ran inside the first add's window.
     *
     * Save and restore the caller's interrupt flag rather than a
     * bare cli/sti: _add is called from contexts where interrupts
     * must stay OFF (process_exit, scheduler_switch_to's cli,
     * process_wake_all_blocked from an IRQ).  A bare sti on the way
     * out would re-enable interrupts inside those contexts.
     *
     * EVERY early return restores the flag first.
     */
    uint64_t saved_flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(saved_flags));

    /*
     * Snapshot head and tail ONCE.  race_p8_check reads head/tail
     * several times (the per-call record and each violation record),
     * and race_record evaluates its arguments in an unspecified
     * order.  Reading the globals more than once lets a tick change
     * one of them between reads, and the check then compares a head
     * from one instant against a tail from another -- a state that
     * was never simultaneously true.  That is what produced the
     * spurious bad_tail_next violations: the same node read as head
     * in one argument and as not-head in another.
     */
    pcb_t* h = ready_queue_head;
    pcb_t* t = ready_queue_tail;
    race_p8_check(h, t, RACE_SITE_Q_ADD, process);
    if (!process) { __asm__ volatile("pushq %0; popfq" :: "r"(saved_flags)); return; }

    /*
     * Idle (pid 1) is never on the ready queue.  It is reached by
     * process_find_by_pid(1) in the scheduler's fallback, and
     * process.c:111 removes it at init.  Several callers guard the
     * pid themselves, but several do not -- process_create adds
     * every new PCB including idle, and process_wake_all_blocked
     * runs every tick and re-adds every READY process.  A stray
     * idle on the queue makes scheduler_ready_queue_next() return
     * it every tick, so the empty-queue branch never runs and the
     * CPU stays in kernel_idle_loop with next never NULL -- the
     * shape the timer dump could not see.  Guard here, once, for
     * every caller.
     */
    if (process->pid == 1) { __asm__ volatile("pushq %0; popfq" :: "r"(saved_flags)); return; }

    /*
     * Idempotency guard.  See scheduler_ready_queue_contains for
     * the failure mode.  A process that is already on the queue
     * must not be linked in again: the second link overwrites its
     * prev/next and can produce a self-link (prev == self) or a
     * cycle.
     */
    if (scheduler_ready_queue_contains(process)) {
        __asm__ volatile("pushq %0; popfq" :: "r"(saved_flags));
        return;
    }

    /*
     * The queue is in the state {head=NULL, tail=process}: head was
     * cleared without clearing the tail.  contains() walks from the
     * head, so it returns 0 and does not catch this.  If we proceed
     * with the add, we set:
     *     process->prev = ready_queue_tail;   // = process
     *     ready_queue_tail->next = process;   // = process
     *     ready_queue_tail = process;
     * producing a self-link on both prev and next.  The process is
     * then on no reachable chain, but the tail still points at it,
     * and every later add to the same process repeats the self-link.
     *
     * The Q-FIRST-BAD capture on 2026-10-12 showed this happening
     * first inside pipe_wake_waiter's call to add, at t=27637, on a
     * tail PCB whose next pointed at itself.  The self-link was
     * observed before the add and remained after.
     *
     * The healing here is local: if head is NULL, the tail is a
     * stale reference to a node that is not in any chain, and the
     * correct recovery is to clear the tail and add the process as
     * the sole entry.  If head is non-NULL, the tail is real and we
     * are already on the queue; refuse the add.
     */
    if (ready_queue_tail == process) {
        if (ready_queue_head == NULL) {
            ready_queue_tail = NULL;
            /* fall through: add as the sole entry, below */
        } else {
            __asm__ volatile("pushq %0; popfq" :: "r"(saved_flags));
            return;
        }
    }

    if (process->state == PROC_STATE_RUNNING) {
        process->state = PROC_STATE_READY;
    }

    process->next = NULL;
    if (ready_queue_head == NULL) {
        /*
         * Head is NULL: whatever ready_queue_tail held, it is not a
         * walkable chain -- if it were, the head would not be NULL.
         * Link the new process as the sole entry with itself as both
         * head and tail.  This covers {head=NULL, tail=process},
         * which the block above already heals, and the sibling
         * {head=NULL, tail=<other PCB>} which it does not: taking
         * the tail branch there would link the new process after a
         * node nothing can reach and leave head NULL, so every
         * reader would still see an empty queue.  That is the
         * pipe7e stall of 2026-10-13.
         */
        /*
         * The queue was empty: this process is both head and tail.
         * Order matters -- set the tail BEFORE clearing prev, so at
         * every instant the published state is consistent.  (For the
         * empty case there is no old tail, so any order works; the
         * order is kept the same as the else branch below so the two
         * read alike.)
         */
        ready_queue_head = process;
        ready_queue_tail = process;
        process->prev = NULL;
    } else {
        /*
         * ORDER MATTERS.  Advance the tail BEFORE linking the old
         * tail to the new node.
         *
         * The old order was:
         *     process->prev = ready_queue_tail;
         *     ready_queue_tail->next = process;   <-- old tail->next set
         *     ready_queue_tail = process;         <-- tail moved
         * Between those last two lines, the old tail has a non-NULL
         * next AND is still the published tail.  A timer tick in that
         * window runs timer_preempt_handler -> scheduler_ready_queue_
         * next -> race_p8_check and finds tail->next != NULL.  That
         * is the bad_tail_next violation: a single-node queue
         * (head == tail) whose tail has a non-NULL next.
         *
         * The 2026-10-14 race ring showed it exactly: record 29545
         * (add, head == tail == 73336), then two tick-skip records,
         * then record 29549 (next, tail == 73336, tail->next !=
         * NULL).  No queue-mutator record between -- the write was
         * inside this add, after its race_p8_check at the top.
         *
         * With the tail advanced first, the invariant holds at every
         * instant: if old_tail->next is set, the tail has already
         * moved past old_tail; if the tail has moved, old_tail->next
         * is set.  There is no window where a node is the published
         * tail and its next is non-NULL.
         */
        pcb_t* old_tail = ready_queue_tail;
        ready_queue_tail = process;
        process->prev = old_tail;
        old_tail->next = process;
    }

    /*
     * EXIT check: record the queue state AFTER the linking.  The
     * entry check above shows the state before; this shows what the
     * operation produced.  Together they bracket the write, so the
     * ring names the operation that left an inconsistent queue
     * rather than the next operation that noticed it.
     *
     * Snapshot head and tail ONCE, exactly as the entry check does.
     * Passing the globals directly lets a tick change one between
     * race_p8_check's reads, and the record then compares a head
     * from one instant against a tail from another -- a state that
     * was never simultaneously true.  The entry checks were fixed
     * this way in 8e3c53f; the exit checks were added without it.
     */
    pcb_t* eh = ready_queue_head;
    pcb_t* et = ready_queue_tail;
    race_p8_check(eh, et, RACE_SITE_Q_ADD, process);

    __asm__ volatile("pushq %0; popfq" :: "r"(saved_flags));
}

void scheduler_ready_queue_remove(pcb_t* process) {
    /* CRITICAL SECTION; see the note in _add.  Every early return
     * restores the caller's interrupt flag. */
    uint64_t saved_flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(saved_flags));

    /* Snapshot head and tail once; see the note in _add. */
    pcb_t* h = ready_queue_head;
    pcb_t* t = ready_queue_tail;
    race_p8_check(h, t, RACE_SITE_Q_REMOVE, process);
    if (!process) { __asm__ volatile("pushq %0; popfq" :: "r"(saved_flags)); return; }

    /* Defensive: if the process has no prev and no next and is not
       the head, it is not on the queue. Removing it would set
       ready_queue_head = process->next = NULL and corrupt the
       queue. */
    if (!process->prev && !process->next && ready_queue_head != process) {
        __asm__ volatile("pushq %0; popfq" :: "r"(saved_flags));
        return;
    }

    if (process->prev) {
        process->prev->next = process->next;
    } else {
        ready_queue_head = process->next;
    }

    if (process->next) {
        process->next->prev = process->prev;
    } else {
        /*
         * Removing the LAST node: the tail becomes process->prev.
         * process->prev->next still points at process, and it must
         * be cleared -- otherwise the new tail has a non-NULL next
         * and the tail invariant is violated.
         *
         * This was the bad_tail_next producer.  The 2026-10-14 race
         * ring showed it: an add created a two-node queue
         * (head == 73336, tail == process, 73336->next == process),
         * a remove then took the queue back to one node with
         * ready_queue_tail = 73336 but left 73336->next = process,
         * and the next _next check found head == tail == 73336 with
         * tail->next != NULL.
         */
        ready_queue_tail = process->prev;
        if (ready_queue_tail) {
            ready_queue_tail->next = NULL;
        }
    }

    process->next = NULL;
    process->prev = NULL;

    /* EXIT check: record the queue state AFTER the unlink; see the
     * note in _add.  Snapshot once, like the entry check. */
    pcb_t* eh = ready_queue_head;
    pcb_t* et = ready_queue_tail;
    race_p8_check(eh, et, RACE_SITE_Q_REMOVE, process);

    __asm__ volatile("pushq %0; popfq" :: "r"(saved_flags));
}

pcb_t* scheduler_ready_queue_next(void) {
    /* CRITICAL SECTION; see the note in _add.  _next pops from the
     * head and can run with interrupts on (process_yield).  Every
     * return restores the caller's interrupt flag. */
    uint64_t saved_flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(saved_flags));

    /*
     * Snapshot head and tail ONCE, and pass the SAME snapshot as
     * both head and process.  Previously this passed
     * ready_queue_head as head and read it AGAIN for process; with
     * race_record's unspecified argument evaluation order, a tick
     * could change ready_queue_head between the two reads, so the
     * record showed a head from one instant and a process from
     * another.
     */
    pcb_t* h = ready_queue_head;
    pcb_t* t = ready_queue_tail;
    race_p8_check(h, t, RACE_SITE_Q_NEXT, h);

    /*
     * Skip any head that is a RUNNING process other than the one on
     * the CPU.
     *
     * The rescans in process_exit, process_yield,
     * scheduler_switch_to, and timer_preempt_handler re-add READY or
     * stranded-RUNNING pool members that are not on the queue.  The
     * RUNNING acceptance is a workaround for a process left RUNNING
     * and off-queue (item 7d).  The workaround is what lets a
     * RUNNING non-current process onto the queue.
     *
     * Dispatching such a process is wrong regardless of how it got
     * there: a RUNNING process is not runnable-from-a-frame, and its
     * pcb->rsp names whatever frame its last legitimate save left --
     * for a process that has been running since, that frame has been
     * overwritten.  Resuming it loads a garbage frame and the resume
     * side stops (the 'C' halt, or a validation branch with a bad
     * RIP).  The 2026-10-13 GDB read of a pipe7e halt showed exactly
     * this: PIPE7E_HELPER pid 12222, state RUNNING, dispatched as
     * next, pcb->rsp = kernel_stack_pool+81456, frame slots not a
     * CPU frame, g_ticks frozen.
     *
     * Unlink and skip.  The process is left off the queue; if it is
     * genuinely the running process, the caller that selected it is
     * the bug, and if it is stale, it must not be dispatched.  Either
     * way this function must not return it.
     *
     * Loop, because there can be more than one such head (a rescan
     * can re-add more than one RUNNING member).
     */
    /*
     * Skip any RUNNING head, unconditionally.
     *
     * A RUNNING process must never be returned by this function.
     * Two cases, and the earlier form of this guard handled only
     * one of them:
     *
     *   - a RUNNING process that is NOT current_process: a stale
     *     dispatch, caught by the guard's second clause.
     *
     *   - a RUNNING process that IS current_process: the process
     *     already on the CPU.  The caller asked for "the next
     *     process to run" and the head is the one already running.
     *     Resuming it from its own pcb->rsp -- a save from before
     *     it was last dispatched, with the stack long since moved
     *     past it -- loads a garbage frame and the resume side
     *     stops.  The earlier guard's `!= current_process` clause
     *     let exactly this case through.
     *
     * The 2026-10-13 second GDB read of a pipe7e halt showed the
     * current_process case: PIPE7E_HELPER pid 4383, state RUNNING,
     * next->rsp == rsp, g_ticks frozen, the CPU at the resume
     * side's kernel-CS validation with the frame's CS slot holding
     * a PCB pointer.  The first read (pid 12222) showed the same
     * frame shape with the process NOT current -- that case the
     * old clause caught.  This clause is the remaining hole.
     *
     * current_process is RUNNING by the scheduler's invariant, so
     * "skip any RUNNING head" subsumes both cases: there is no
     * RUNNING head that is safe to return.
     *
     * A NULL return is handled by every caller: process_exit falls
     * to idle/halt, process_yield rescans the pool, the timer
     * falls back to idle.  Skipping current_process is correct --
     * you never switch to the process already on the CPU.
     */
    while (ready_queue_head &&
           ready_queue_head->state == PROC_STATE_RUNNING) {
        pcb_t* skip = ready_queue_head;
        ready_queue_head = skip->next;
        if (ready_queue_head) {
            ready_queue_head->prev = NULL;
        } else {
            ready_queue_tail = NULL;
        }
        skip->next = NULL;
        skip->prev = NULL;
    }

    if (!ready_queue_head) {
        __asm__ volatile("pushq %0; popfq" :: "r"(saved_flags));
        return NULL;
    }

    pcb_t* next = ready_queue_head;
    ready_queue_head = next->next;
    if (ready_queue_head) {
        ready_queue_head->prev = NULL;
    } else {
        ready_queue_tail = NULL;
    }
    next->next = NULL;
    next->prev = NULL;
    __asm__ volatile("pushq %0; popfq" :: "r"(saved_flags));
    return next;
}

int scheduler_ready_queue_empty(void) {
    return ready_queue_head == NULL;
}

int scheduler_ready_queue_contains(pcb_t* process) {
    if (!process) return 0;
    /*
     * Walk the queue.  The previous implementation used link
     * presence as a proxy for membership; a process with
     * stale links from a previous membership reported true
     * even when off-queue, and scheduler_ready_queue_add()
     * then no-opped on it.  Walking is O(n) with n <= 32 and
     * is the only membership test that cannot give a false
     * positive.
     */
    for (pcb_t* p = ready_queue_head; p; p = p->next) {
        if (p == process) return 1;
    }
    return 0;
}

void scheduler_reset(void) {
    /*
     * Clear the queue AND every process's link fields.  A
     * process whose links survive the reset makes
     * scheduler_ready_queue_contains() return true forever
     * after (link presence was used as a proxy for queue
     * membership), and scheduler_ready_queue_add() then
     * silently no-ops on it -- the process is stranded
     * off-queue.  That is exactly what the 7e pipe stall
     * showed: qhead_after_add=0 after a call to add.
     */
    ready_queue_head = NULL;
    ready_queue_tail = NULL;
    for (int _i = 0; _i < MAX_PROCESSES; _i++) {
        pcb_t* _p = process_get_pcb(_i);
        if (!_p) continue;
        _p->next = NULL;
        _p->prev = NULL;
    }
    current_process = NULL;
    process_set_current(NULL);
}

void scheduler_set_current(pcb_t* proc) {
    current_process = proc;
    process_set_current(proc);
}

void __attribute__((noreturn)) process_exit(void) {
    if (!current_process) {
        while(1) asm volatile("hlt");
    }

    /* Disable interrupts for the entire exit sequence. See the
       previous version's comment for the full rationale. */
    __asm__ volatile("cli");

    pcb_t* exiting = current_process;

    /*
     * Drop the exiting process's file references.
     *
     * sys_exit calls close_all_files before this function runs, but
     * process_exit is also reached directly -- from fault_kill_current,
     * from user_syscall_entry.asm's exit path, and from sys_execve's
     * failure branch -- and those paths do NOT close the file table.
     *
     * Without this call, a pipe end inherited from a fork parent leaks
     * its reference on every direct-path exit: pipe->readers_open and
     * pipe->writers_open are decremented only in put_file_slot when a
     * slot's refcount reaches zero, and pipe->refcount (which frees
     * the pipe_t and its 4096-byte ring buffer) is likewise only
     * decremented there.  A slot's refcount does not reach zero until
     * every process holding it has closed it, so a child that exits
     * without closing leaves the parent's reference as the last one,
     * and if the parent never closes (ash's command-substitution
     * bookkeeping does not on this path), the pipe_t and its ring
     * buffer leak forever.
     *
     * pipe_wake_probe.sh creates one pipe per iteration via
     * x=$(seq 1 3) and reaches the fault family at iteration ~1074,
     * when the leaked ring buffers have grown into several megabytes
     * and the heap has been forced to expand into the PMM's free
     * pool.  This call closes the child's half deterministically and
     * is the first half of the fix.
     *
     * close_all_files is idempotent: it NULLs each slot as it
     * closes, so a call after sys_exit's own close finds an empty
     * table and returns immediately.
     */
    close_all_files(exiting);

    {
        serial_lock();
        #if TRACE_EXIT
        serial_print("EXIT: pid=");
        serial_print_dec(exiting->pid);
        serial_print(" state=");
        serial_print_dec((uint64_t)exiting->state);
        serial_print(" parent=");
        serial_print_dec(exiting->parent_pid);
        serial_print(" qhead=");
        if (head) serial_print_dec(head->pid);
        else serial_print("(empty)");
        serial_print("\n");
        #endif
        serial_unlock();
    }

    exiting->state = PROC_STATE_TERMINATED;
    scheduler_ready_queue_remove(exiting);

    if (process_get_current() == exiting) {
        process_set_current(NULL);
    }

    /*
     * Decide between "no parent, reclaim now" and "has parent, become
     * a zombie and let the parent reap us".
     *
     * A process with parent_pid == 0 is either idle, the kernel
     * shell, or the user shell.  These are the terminal processes of
     * the system; there is no one to reap them, so they reclaim
     * their own PCB as before.
     *
     * A process with parent_pid != 0 was created by a SYS_EXEC or a
     * SYS_FORK-style call.  It becomes a zombie and is reaped by the
     * parent's SYS_WAITPID.  This is what makes spawn/wait
     * meaningful.
     */
    if (exiting->parent_pid != 0) {
        exiting->state = PROC_STATE_ZOMBIE;
        process_wake_parent_if_waiting(exiting);
    } else {
        process_reclaim(exiting);
    }

    pcb_t* next = scheduler_ready_queue_next();
    if (!next) {
        /* Nothing else runnable. Two cases:
           - exiting is a kernel process (diagnostic like runproc or
             testyield): resume the kernel shell so the operator can
             run more diagnostics.
           - exiting is a user process (the user shell): halt. The
             user shell is the terminal interactive console; there is
             no kernel shell to fall back to by design. */
        scheduler_reset();

        /* Same stranded-READY recovery as process_yield: a READY
         * process off the queue must be re-added before deciding
         * the queue is genuinely empty. */
        for (int _i = 0; _i < MAX_PROCESSES; _i++) {
            pcb_t* _p = process_get_pcb(_i);
            if (!_p) continue;
            /*
             * Re-add READY or stranded-RUNNING.  A process in state
             * RUNNING is, by the scheduler's invariant, the one on
             * the CPU -- current_process.  Any OTHER process left in
             * RUNNING is stale: it was switched away from without a
             * requeue, and every requeue guard (which tests state ==
             * RUNNING) treats it as current and skips it.  Nothing
             * reschedules it.  That is the state the YIELD-TO-IDLE
             * dump showed: pid 4 pipe7e RUNNING, off the queue, with
             * its PIPE7E_HELPER child ZOMBIE and unreaped, and the
             * shell BLOCKED in wait4 -- the child cannot reap, the
             * shell cannot wake.  Item 7d.
             */
            if (_p == current_process) continue;
            if (_p->pid == 1) continue;   /* idle is never queued */
            if (_p->state != PROC_STATE_READY) continue;
            if (scheduler_ready_queue_contains(_p)) continue;
            scheduler_ready_queue_add(_p);
        }
        pcb_t* _n = scheduler_ready_queue_next();
        if (_n) {
            scheduler_switch_to(_n);
        }

        extern void tss_set_kernel_stack(uint64_t stack);
        extern void tss_set_syscall_stack(uint64_t stack);
        pcb_t* idle = process_find_by_pid(1);
        if (idle) {
            tss_set_kernel_stack(idle->kernel_stack_top);
            tss_set_syscall_stack(idle->kernel_stack_top);
        } else {
            tss_set_kernel_stack(0);
            tss_set_syscall_stack(0);
        }

        if (exiting->entry_point >= KERNEL_BASE) {
            pcb_t* shell = process_get_kernel_shell();
            if (shell && shell->state == PROC_STATE_BLOCKED) {
                shell->state = PROC_STATE_RUNNING;
                extern void scheduler_switch_to(pcb_t* next);
                scheduler_switch_to(shell);
                /* Not reached. */
            }
            serial_print("process_exit: no kernel shell to resume, halting\n");
            __asm__ volatile("cli");
            while (1) __asm__ volatile("hlt");
        }

        /*
         * No runnable process.  But idle (pid 1) is always a valid
         * target: it is not on the ready queue by design (removed at
         * init), and it just hlt's.  Switching to idle lets the
         * timer keep ticking; the next keyboard IRQ will wake the
         * shell (process_wake_all_blocked sets it READY and adds it
         * to the queue), and timer_preempt_handler will then pick
         * it up on the next tick.
         *
         * The previous behavior — halt here — broke any sequence
         * where a forked child exited while its parent (the shell)
         * was blocked in sys_read.  The shell was off the queue,
         * the queue was empty, and the halt path is not what a
         * preemptible kernel should do when idle exists.
         *
         * If for some reason idle is gone, fall through to the old
         * halt path as a last resort.
         */
        if (idle) {
            serial_lock();
            #if TRACE_EXIT
            serial_print("EXIT-FALLBACK: switching to idle, exiting pid=");
            serial_print_dec(exiting->pid);
            serial_print(" name=");
            serial_print(exiting->name);
            serial_print("\n");
            #endif
            serial_unlock();

            current_process = idle;
            process_set_current(idle);
            idle->state = PROC_STATE_RUNNING;
            idle->total_ticks++;

            /* MSR_FS_BASE is per-process; save the outgoing FS base
               and install the incoming one before the CR3 switch.
               See the fs_base comment in process.h. */
            exiting->fs_base = rdmsr(0xC0000100);
            wrmsr(0xC0000100, idle->fs_base);

            context_switch_no_save(exiting, idle);
            /* Not reached. */
        }

        serial_lock();
        serial_print("HALT: exiting pid=");
        serial_print_dec(exiting->pid);
        serial_print(" name=");
        serial_print(exiting->name);
        serial_print(" entry=0x");
        serial_print_hex(exiting->entry_point);
        serial_print(" parent=");
        serial_print_dec(exiting->parent_pid);
        serial_print("\n");
        serial_unlock();

        serial_print("process_exit: no runnable process, halting\n");
        __asm__ volatile("cli");
        while (1) __asm__ volatile("hlt");
    }

    /*
     * Refuse to dispatch a next whose rsp is not inside its own
     * kernel stack.  This is the process_exit counterpart to
     * scheduler_switch_to's SWITCH-REFUSE, and it catches the
     * 2026-10-16 'C': next->rip was close_all_files, next->rsp
     * pointed at a frame the stack had moved past, and
     * context_switch_no_save resumed it and halted.
     *
     * kernel_stack_virt and kernel_stack_top are both virtual (see
     * process.c), so this range check is in the same address space as
     * rsp.  A next outside its own stack has no dispatchable frame.
     *
     * Fall to idle: process_exit's own idle fallback below is the
     * safe path, and it never resumes a stale frame.
     */
    if (next->pid != 1 &&
        (next->rsp < next->kernel_stack_virt ||
         next->rsp >= next->kernel_stack_virt + PROC_STACK_SIZE)) {
        serial_lock();
        serial_print("EXIT-REFUSE: next pid=");
        serial_print_dec(next->pid);
        serial_print(" name=");
        serial_print(next->name);
        serial_print(" rsp outside stack -> idle\n");
        serial_unlock();
        pcb_t* _idle = process_find_by_pid(1);
        if (_idle) {
            extern void tss_set_kernel_stack(uint64_t stack);
            extern void tss_set_syscall_stack(uint64_t stack);
            current_process = _idle;
            process_set_current(_idle);
            _idle->state = PROC_STATE_RUNNING;
            _idle->total_ticks++;
            exiting->fs_base = rdmsr(0xC0000100);
            wrmsr(0xC0000100, _idle->fs_base);
            tss_set_kernel_stack(_idle->kernel_stack_top);
            tss_set_syscall_stack(_idle->kernel_stack_top);
            context_switch_no_save(exiting, _idle);
        }
        __asm__ volatile("cli");
        while (1) __asm__ volatile("hlt");
    }

    /*
     * A RUNNING next is the process on the CPU; it must not be
     * handed to.  next came from scheduler_ready_queue_next, which
     * skips a RUNNING head at pop time -- but the process can become
     * RUNNING between the pop and here (a rescan, a peer wake, or the
     * self-switch case in scheduler_switch_to).  This is the same
     * window e7e3e6b closes for scheduler_switch_to; process_exit
     * does not go through scheduler_switch_to, so it needs its own
     * check.
     *
     * Fall to idle rather than dispatch.  Do NOT re-enter
     * process_yield: it calls scheduler_switch_to, which calls
     * context_switch with prev = current_process, and prev is the
     * ZOMBIE exiting process -- that would re-open the Bug 2
     * hlt-guard frame that 6bfa008 closed.
     */
    if (next->state == PROC_STATE_RUNNING) {
        pcb_t* idle = process_find_by_pid(1);
        if (idle) {
            extern void tss_set_kernel_stack(uint64_t stack);
            extern void tss_set_syscall_stack(uint64_t stack);
            current_process = idle;
            process_set_current(idle);
            idle->state = PROC_STATE_RUNNING;
            idle->total_ticks++;
            exiting->fs_base = rdmsr(0xC0000100);
            wrmsr(0xC0000100, idle->fs_base);
            tss_set_kernel_stack(idle->kernel_stack_top);
            tss_set_syscall_stack(idle->kernel_stack_top);
            context_switch_no_save(exiting, idle);
        }
        __asm__ volatile("cli");
        while (1) __asm__ volatile("hlt");
    }

    current_process = next;
    process_set_current(next);
    next->state = PROC_STATE_RUNNING;
    next->total_ticks++;

    extern void tss_set_kernel_stack(uint64_t stack);
    extern void tss_set_syscall_stack(uint64_t stack);
    tss_set_kernel_stack(next->kernel_stack_top);
    tss_set_syscall_stack(next->kernel_stack_top);

    /* MSR_FS_BASE is per-process; save the outgoing FS base and
       install the incoming one before the CR3 switch.  See the
       fs_base comment in process.h. */
    exiting->fs_base = rdmsr(0xC0000100);
    wrmsr(0xC0000100, next->fs_base);

    context_switch_no_save(exiting, next);

    while(1) asm volatile("hlt");
}

void scheduler_switch_to(pcb_t* next) {
    if (!next) return;
    if (next == current_process) {
        /*
         * Asked to switch to ourselves: nothing to switch, but the
         * invariant "the running process is RUNNING" must hold.  A
         * peer wake (process_wake_parent_if_waiting, pipe_wake_waiter,
         * process_wake_all_blocked) can set state to READY while we
         * are still on the CPU, before we reach process_yield.  Every
         * requeue guard in the scheduler (here, process_yield, the
         * timer handler) tests `state == RUNNING`, so a running
         * process left in READY is skipped by all of them and ends
         * up off the ready queue with nothing to reschedule it.
         * Item 7d.
         */
        next->state = PROC_STATE_RUNNING;
        next->block_kind = BLOCK_KIND_NONE;
        /*
         * Requeue.  A process left RUNNING here is current, and a
         * current process does not need to be on the ready queue --
         * until the CPU leaves it.  The paths that change
         * `current` requeue the outgoing process only when its state
         * is RUNNING, and the timer path requeues only when its own
         * local `current` matches.  A RUNNING process left off the
         * queue by this early return is the state the
         * TIMER-NO-NEXT dump showed: pid 3 busybox RUNNING, off the
         * queue, CPU in idle.  Adding it here is idempotent
         * (scheduler_ready_queue_add checks membership), so a
         * process already queued is unchanged.  Idle (pid 1) is
         * deliberately never on the queue -- it is reached through
         * process_find_by_pid(1) -- so skip it.  Item 7d.
         */
        if (next->pid != 1) {
            scheduler_ready_queue_add(next);
        }
        return;
    }

    if (next->pid == 1 && current_process &&
        current_process->state == PROC_STATE_RUNNING) {
        return;
    }

    SW2_PRINT(
        serial_print("SW2-ENTRY next=0x"); serial_print_hex((uint64_t)next);
        serial_print(" pid="); serial_print_dec(next->pid);
        serial_print(" st="); serial_print_dec(next->state);
        serial_print(" nm="); serial_print(next->name);
        serial_print(" cur=0x"); serial_print_hex((uint64_t)current_process);
        serial_print("\n");
    );

    /*
     * A RUNNING process that is not current_process must not be
     * dispatched.
     *
     * scheduler_ready_queue_next's guard checks state at POP time.
     * Between the pop and this point, the process's state can change:
     * a rescan, a peer wake, or this function's own self-switch case
     * can set it RUNNING.  The pop-time guard cannot see that; this
     * check is on the current value, at dispatch time, and it sees it.
     *
     * A RUNNING non-current process has been made RUNNING without
     * being put on the CPU.  Dispatching it resumes it from a
     * pcb->rsp that names a frame its last legitimate save left; for
     * a process that has been running since, the stack has moved past
     * that frame and the resume side loads a transposed region of
     * stack.  The 2026-10-13 third GDB read of a pipe7e halt showed
     * this: PIPE7E_HELPER pid 82550, state RUNNING, next->rsp ==
     * $rsp, g_ticks frozen, the frame's CPU slots holding the values
     * of its GPR slots shifted -- a frame read at the wrong base.
     *
     * Refuse.  This is the dispatch-time half of the invariant
     * 'a RUNNING process is the one on the CPU'.  The pop-time half
     * is in scheduler_ready_queue_next; the two together close the
     * window between the pop and the resume.
     *
     * The self-switch case above (next == current_process) is
     * handled before this point and is not affected: it returns
     * without dispatching.  A freshly created process is READY, not
     * RUNNING (process_create sets it), so process_start's direct
     * call is not affected either.
     */
    __asm__ volatile("cli");

    /*
     * Second check inside the cli: next == current_process.
     *
     * The self-switch check at function entry reads current_process
     * with interrupts ON.  A timer tick between that check and this
     * cli can set current_process = next (the timer's
     * scheduler_set_current).  Then prev = current_process at the
     * dispatch below is next, and context_switch(next, next) runs:
     * the save path writes next->rsp = its own frame base, and the
     * resume loads that same frame and reads its CPU slots from the
     * middle of its own stack -- the 'C' halt at
     * context_switch_resume.frame_cs_kernel, with N == R (next ==
     * the value in the RIP slot).
     *
     * Re-check here, under the cli, so the state cannot change
     * between the check and the dispatch.  Same shape as d3c7f20
     * (the test.sh row-30 'C'): the guard that was left above the
     * cli is the window.
     */
    if (next == current_process) {
        __asm__ volatile("sti");
        return;
    }

    /*
     * A RUNNING next must not be dispatched.  This check is INSIDE
     * the cli, not before it.
     *
     * It used to sit above the cli.  That read next->state with
     * interrupts on, and a timer tick between the read and the cli
     * could change the state.  The guard then passed on a READY
     * process, and the dispatch proceeded with a process that was
     * RUNNING by the time context_switch resumed its frame.
     *
     * The 2026-10-14 GDB read of a test.sh row-30 'C' showed
     * exactly this: busybox at pcb_pool+9368, current_process ==
     * that same PCB, state RUNNING, and the frame at its pcb->rsp
     * holding user-space values in the CPU slots (CS = a user stack
     * address, RFLAGS = a user code address).  The guard above the
     * cli had read READY; the state changed before the cli; the
     * dispatch resumed a stale frame.
     *
     * With the check here, the state cannot change between the read
     * and the resume -- the cli holds until context_switch's resume
     * side has loaded the frame.
     */
    if (next->state == PROC_STATE_RUNNING) {
        __asm__ volatile("sti");
        return;
    }

    /* A ZOMBIE or TERMINATED process must never be dispatched.
     *
     * The 2026-10-16 SW2 instrumentation caught exactly this:
     *
     *   SW2-ENTRY next=... pid=169 st=4 nm=PIPE7E_HELPER
     *   SW2-ASSIGN ... nextst=4
     *   SW2-CALL  ... nextst=2 rsp=0xFFFFFFFF8029C080 kv=... kt=...
     *   C
     *   N=FFFFFFFF8027D650 R=... S=0000000000000000 F=0 P=0
     *
     * state 4 is PROC_STATE_ZOMBIE.  next->rsp was inside its own
     * kernel stack (kv <= rsp < kt), so the range detector did not
     * fire, but the contents at rsp were zeroed -- a zombie has no
     * resume frame.  The resume read S=F=P=0 and halted.
     *
     * A zombie has called process_exit; it is not runnable and is
     * only waiting to be reaped by sys_wait4.  A terminated process
     * is already reclaimed.  Neither has a frame to resume.  Refuse
     * before the assignment, so current_process is not overwritten
     * and the caller's prev stays current. */
    if (next->state == PROC_STATE_ZOMBIE ||
        next->state == PROC_STATE_TERMINATED) {
        serial_lock();
        serial_print("SWITCH-DEAD next=0x"); serial_print_hex((uint64_t)next);
        serial_print(" pid="); serial_print_dec(next->pid);
        serial_print(" st="); serial_print_dec(next->state);
        serial_print(" nm="); serial_print(next->name);
        serial_print("\n");
        serial_unlock();
        return;
    }

    pcb_t* prev = current_process;
    current_process = next;
    process_set_current(next);

    SW2_PRINT(
        serial_print("SW2-ASSIGN prev=0x"); serial_print_hex((uint64_t)prev);
        serial_print(" next=0x"); serial_print_hex((uint64_t)next);
        serial_print(" nextpid="); serial_print_dec(next->pid);
        serial_print(" nextst="); serial_print_dec(next->state);
        serial_print("\n");
    );

    if (next->pid != 1) {
        scheduler_ready_queue_remove(next);
    }

    if (prev && prev->state == PROC_STATE_RUNNING) {
        prev->state = PROC_STATE_READY;
        if (prev->pid != 1) {
            scheduler_ready_queue_add(prev);
        }
    }

    /* scheduler_ready_queue_remove/_add save and restore the caller's
     * interrupt flag.  process_yield calls this function with
     * interrupts ON, so those restore IF=1, and a timer tick in the
     * window between them and context_switch re-enters the timer's
     * dispatch, sets current_process = next, and marks next RUNNING
     * before this function reaches context_switch.  The dispatch then
     * runs with next == current_process, the resume loads next->rsp --
     * the live stack -- and context_switch_resume halts on the CS
     * slot.  Re-close the window the queue helpers opened. */
    __asm__ volatile("cli");

    /* Re-check next->state AFTER the cli.  The check at the top of this
     * function ran before scheduler_ready_queue_remove/_add, and those
     * restore the caller's interrupt flag.  A timer tick in that window
     * runs timer_preempt_handler, which can set next->state = RUNNING
     * and scheduler_set_current(next) before this function reaches
     * context_switch.  The dispatch then runs with next already
     * current, the resume loads next->rsp -- the live stack -- and
     * context_switch_resume halts on the CS slot.  Re-check under the
     * cli so the state cannot change between the check and the
     * dispatch. */
    if (next->state == PROC_STATE_RUNNING) {
        serial_lock();
        serial_print("SW2-RET-RUNNING next=0x"); serial_print_hex((uint64_t)next);
        serial_print(" pid="); serial_print_dec(next->pid);
        serial_print(" nm="); serial_print(next->name);
        serial_print(" cur=0x"); serial_print_hex((uint64_t)current_process);
        serial_print("\n");
        serial_unlock();
        __asm__ volatile("sti");
        return;
    }

    next->state = PROC_STATE_RUNNING;
    next->total_ticks++;

    extern void tss_set_kernel_stack(uint64_t stack);
    extern void tss_set_syscall_stack(uint64_t stack);
    tss_set_kernel_stack(next->kernel_stack_top);
    tss_set_syscall_stack(next->kernel_stack_top);

    /* MSR_FS_BASE is per-process; save the outgoing FS base and
       install the incoming one before the CR3 switch.  prev may be
       NULL on the very first dispatch (idle has no predecessor);
       context_switch.asm tolerates that, and we match it here.
       See the fs_base comment in process.h. */
    if (prev) prev->fs_base = rdmsr(0xC0000100);
    wrmsr(0xC0000100, next->fs_base);

    /* SWITCH-RANGE: refuse a next whose rsp is outside its own stack.
     * kernel_stack_virt and kernel_stack_top are both virtual, so the
     * range is in the same address space as rsp.  The 2026-10-16
     * capture had next->rsp at slot 5's base while kernel_stack_virt
     * named slot 5 + 0x1D0: the frame was in the wrong slot, and the
     * rsp == 0 check below did not catch it. */
    if (next->pid != 1 &&
        (next->rsp < next->kernel_stack_virt ||
         next->rsp >= next->kernel_stack_virt + PROC_STACK_SIZE)) {
        serial_lock();
        serial_print("SWITCH-REFUSE: next pid=");
        serial_print_dec(next->pid);
        serial_print(" name=");
        serial_print(next->name);
        serial_print(" rsp outside stack -> idle\n");
        serial_unlock();
        pcb_t* _idle2 = process_find_by_pid(1);
        if (_idle2 && _idle2 != current_process) {
            current_process = _idle2;
            process_set_current(_idle2);
            _idle2->state = PROC_STATE_RUNNING;
            next = _idle2;
        } else {
            __asm__ volatile("sti");
            return;
        }
    }

    /*
     * Refuse to dispatch a next whose rsp is not a live frame.
     *
     * The stale-frame captures showed next RUNNING with next->rsp
     * pointing at a frame from an earlier save -- a timer preemption
     * in serial_putc, or a prior context_switch -- which the resume
     * then read as a 20-slot frame and halted on (the 'C').
     *
     * context_switch.asm now clears next->rsp (offset 0x110) when the
     * resume consumes it, so a zero here means "this process has not
     * been saved since it was last resumed."  It is not dispatchable:
     * its rsp names no frame.  Refuse, requeue it as READY, and return
     * so the caller can pick again.  The print names the process, so
     * the capture records every occurrence without a GDB attach.
     */
    if (next->rsp == 0) {
        /*
         * Refuse to dispatch a next that has no frame.
         *
         * next->rsp == 0 means the field was consumed by a resume
         * and never re-written by a save: this process was dispatched
         * without being saved.  Dispatching it resumes a stale frame
         * and halts (the 'C').  Requeue it and DO NOT RETURN.
         *
         * The callers of this function do not handle a return: it is
         * called from process_yield (which parked the outgoing process
         * as BLOCKED) and its return would unwind into a frame whose
         * process is not runnable.  That unwind produced the #GP ->
         * #DF with rsp == 0 at isr13_stub.  Instead, fall to idle:
         * idle is always dispatchable, and switching to it never
         * returns to this frame.  The timer and the next wake will
         * pick the requeued process up again.
         */
        serial_lock();
        serial_print("SWITCH-REFUSE: next pid=");
        serial_print_dec(next->pid);
        serial_print(" name=");
        serial_print(next->name);
        serial_print(" rsp=0 -> idle\n");
        serial_unlock();
        next->state = PROC_STATE_READY;
        scheduler_ready_queue_add(next);

        pcb_t* idle = process_find_by_pid(1);
        if (idle && idle != current_process) {
            /*
             * Fall to idle.  context_switch does NOT update
             * current_process -- scheduler_switch_to does that before
             * the dispatch, and the refuse path bypassed it.  Without
             * this, current_process stays at the refused process (or
             * 0 after a scheduler_reset), and timer_preempt_handler's
             * `if (!current) return` makes every subsequent tick a
             * no-op: the queue is never drained, nothing is dispatched,
             * and idle runs hlt forever.  That was the hang.
             */
            current_process = idle;
            process_set_current(idle);
            idle->state = PROC_STATE_RUNNING;
            next = idle;
        } else {
            /* No idle to fall to: keep the caller running rather
             * than returning into a parked frame.  This is the old
             * behavior and is reached only if idle is gone. */
            __asm__ volatile("sti");
            return;
        }
    }

    SW2_PRINT(
        serial_print("SW2-CALL prev=0x"); serial_print_hex((uint64_t)prev);
        serial_print(" next=0x"); serial_print_hex((uint64_t)next);
        serial_print(" nextpid="); serial_print_dec(next->pid);
        serial_print(" nextst="); serial_print_dec(next->state);
        serial_print(" rsp=0x"); serial_print_hex(next->rsp);
        serial_print(" kv=0x"); serial_print_hex(next->kernel_stack_virt);
        serial_print(" kt=0x"); serial_print_hex(next->kernel_stack_top);
        serial_print("\n");
    );

    context_switch(prev, next);
}

void scheduler_init(void) {
    ready_queue_head = NULL;
    ready_queue_tail = NULL;
    current_process = NULL;
    process_set_current(NULL);
    schedule_count = 0;
    yield_count = 0;
}

pcb_t* scheduler_schedule(void) {
    schedule_count++;
    pcb_t* next = scheduler_ready_queue_next();
    if (!next) return NULL;
    return next;
}

void process_yield(void) {
    if (!current_process) return;
    if (current_process->pid == 1) return;

    yield_count++;

    if (current_process->state == PROC_STATE_RUNNING) {
        current_process->state = PROC_STATE_READY;
        scheduler_ready_queue_add(current_process);
    } else {
        /*
         * Blocked (or otherwise not runnable).  The process must not
         * remain on the ready queue: the timer handler pops a RUNNING
         * process from the queue and then re-adds it, and a process
         * that blocks between the pop and the yield can be left on
         * the queue.  A process that is on the ready queue while
         * BLOCKED will be picked by scheduler_ready_queue_next()
         * before other runnable processes, delaying them.
         *
         * scheduler_ready_queue_remove is idempotent: it returns
         * without doing anything if the process is not on the queue.
         */
        scheduler_ready_queue_remove(current_process);
    }

    pcb_t* next = scheduler_ready_queue_next();
    if (!next) {
        if (current_process->state == PROC_STATE_READY) {
            scheduler_ready_queue_add(current_process);
            next = current_process;
        } else {
            /*
             * The ready queue is empty and the current process is
             * not runnable.  Before falling to idle, scan the pool
             * for any process that is READY but not on the ready
             * queue.  That is the item-7d lost-process shape: a
             * peer wake set state = READY, but a requeue guard
             * (which tests state == RUNNING) later skipped it, so
             * it ended up READY and off the queue.  Falling to
             * idle with a READY process stranded is the hang the
             * pool dump showed: helper pid 0x251 state=READY, on
             * no queue, CPU in kernel_idle_loop.  Re-add every
             * such process so the scheduler can run it.  This scan
             * runs only when the queue is otherwise empty; it is
             * 32 pool entries and it is off the hot path. */
            for (int _i = 0; _i < MAX_PROCESSES; _i++) {
                pcb_t* _p = process_get_pcb(_i);
                if (!_p) continue;
                /*
                 * Re-add READY or stranded-RUNNING.  See the same
                 * scan in process_exit for the full note.  A
                 * RUNNING process that is not current_process is
                 * stale and must be requeued.  Item 7d.
                 */
                if (_p == current_process) continue;
                if (_p->pid == 1) continue;   /* idle is never queued */
                if (_p->state != PROC_STATE_READY) continue;
                if (scheduler_ready_queue_contains(_p)) continue;
                scheduler_ready_queue_add(_p);
            }
            next = scheduler_ready_queue_next();
            if (!next) {
                /*
                 * About to fall to idle.  Dump every PCB so the
                 * capture names the state each process is in at the
                 * moment the scheduler gave up: a shell BLOCKED in
                 * wait4 and never woken, or a process READY and
                 * stranded.  Those are two different bugs and the
                 * dump is what tells them apart.  Diagnostic only.
                 */
#if RACE_PROBE_IDLE_DUMP
                serial_lock();
                serial_print("YIELD-TO-IDLE: queue empty, pool state:\n");
                process_dump_all();
                serial_unlock();
                /* Ring dump for the race, on the failure path. */
                race_probe_dump();
#endif
                next = process_find_by_pid(1);
                if (!next) return;
            }
        }
    }

    /*
     * Refuse to yield to the process already on the CPU.
     *
     * Two ways next can equal current_process here:
     *
     *   - the fallback above set next = current_process when the
     *     queue was empty and the current process was READY;
     *
     *   - a tick landed between scheduler_ready_queue_next() and this
     *     point and made current_process = next.
     *
     * Either way, dispatching it is wrong: scheduler_switch_to would
     * save current_process as prev and resume it as next, on its own
     * live stack, and context_switch_resume would read the live stack
     * as a 20-slot frame and halt on the CS slot.  Stay on the CPU
     * instead: the process is already running, and its frame is
     * whatever it is executing on.
     */
    if (next == current_process) {
        current_process->state = PROC_STATE_RUNNING;
        return;
    }

    /*
     * Refuse to dispatch a next that is already RUNNING.
     *
     * A RUNNING process is, by the scheduler's invariant, the one on
     * the CPU.  Dispatching it as a switch target resumes it from a
     * pcb->rsp that names a frame its last save left -- and for a
     * process that has been running since, the stack has moved past
     * that frame.  The 2026-10-16 capture caught exactly this:
     * sys_wait4 -> process_yield picked a next whose state was
     * RUNNING and whose rip was serial_putc+44 (a timer preemption),
     * and context_switch resumed it from the stale frame.
     *
     * scheduler_switch_to has its own RUNNING guard, but it is above
     * the queue-helper calls that restore interrupts, and the check
     * here is at the decision point.  Same shape as the
     * next == current_process guard above.
     */
    if (next->state == PROC_STATE_RUNNING) {
        serial_lock();
        serial_print("YIELD-REFUSE: next pid=");
        serial_print_dec(next->pid);
        serial_print(" name=");
        serial_print(next->name);
        serial_print(" state=RUNNING\n");
        serial_unlock();
        current_process->state = PROC_STATE_RUNNING;
        return;
    }

    scheduler_switch_to(next);
}

pcb_t* scheduler_get_current(void) {
    return current_process;
}

void scheduler_stats(void) {
    serial_print("\n=== SCHEDULER STATS ===\n");
    serial_print("Schedule count: "); serial_print_dec(schedule_count); serial_print("\n");
    serial_print("Yield count: "); serial_print_dec(yield_count); serial_print("\n");
    serial_print("=== END SCHEDULER STATS ===\n\n");
}
