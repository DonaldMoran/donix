#include "include/scheduler.h"
#include "include/serial.h"
#include "include/vga.h"
#include "include/user_msr.h"
#include "include/trace.h"
#include "include/race_probe.h"

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
    race_p8_check(ready_queue_head, ready_queue_tail, RACE_SITE_Q_ADD);
    if (!process) return;

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
    if (process->pid == 1) return;

    /*
     * Idempotency guard.  See scheduler_ready_queue_contains for
     * the failure mode.  A process that is already on the queue
     * must not be linked in again: the second link overwrites its
     * prev/next and can produce a self-link (prev == self) or a
     * cycle.
     */
    if (scheduler_ready_queue_contains(process)) {
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
        process->prev = NULL;
        ready_queue_head = process;
        ready_queue_tail = process;
    } else {
        process->prev = ready_queue_tail;
        ready_queue_tail->next = process;
        ready_queue_tail = process;
    }
}

void scheduler_ready_queue_remove(pcb_t* process) {
    race_p8_check(ready_queue_head, ready_queue_tail, RACE_SITE_Q_REMOVE);
    if (!process) return;

    /* Defensive: if the process has no prev and no next and is not
       the head, it is not on the queue. Removing it would set
       ready_queue_head = process->next = NULL and corrupt the
       queue. */
    if (!process->prev && !process->next && ready_queue_head != process) {
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
        ready_queue_tail = process->prev;
    }

    process->next = NULL;
    process->prev = NULL;
}

pcb_t* scheduler_ready_queue_next(void) {
    race_p8_check(ready_queue_head, ready_queue_tail, RACE_SITE_Q_NEXT);

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
    while (ready_queue_head &&
           ready_queue_head->state == PROC_STATE_RUNNING &&
           ready_queue_head != current_process) {
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

    if (!ready_queue_head) return NULL;

    pcb_t* next = ready_queue_head;
    ready_queue_head = next->next;
    if (ready_queue_head) {
        ready_queue_head->prev = NULL;
    } else {
        ready_queue_tail = NULL;
    }
    next->next = NULL;
    next->prev = NULL;
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
            if (_p->state != PROC_STATE_READY &&
                _p->state != PROC_STATE_RUNNING) continue;
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

            context_switch(exiting, idle);
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

    __asm__ volatile("cli");

    pcb_t* prev = current_process;
    current_process = next;
    process_set_current(next);

    if (next->pid != 1) {
        scheduler_ready_queue_remove(next);
    }

    if (prev && prev->state == PROC_STATE_RUNNING) {
        prev->state = PROC_STATE_READY;
        if (prev->pid != 1) {
            scheduler_ready_queue_add(prev);
        }
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
                if (_p->state != PROC_STATE_READY &&
                    _p->state != PROC_STATE_RUNNING) continue;
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
                serial_lock();
                serial_print("YIELD-TO-IDLE: queue empty, pool state:\n");
                process_dump_all();
                serial_unlock();
                next = process_find_by_pid(1);
                if (!next) return;
            }
        }
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
