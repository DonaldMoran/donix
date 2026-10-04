#include "include/scheduler.h"
#include "include/serial.h"
#include "include/vga.h"
#include "include/user_msr.h"

#define KERNEL_BASE 0xFFFFFFFF80000000ULL

static pcb_t* ready_queue_head = NULL;
static pcb_t* ready_queue_tail = NULL;
static pcb_t* current_process = NULL;

static uint64_t schedule_count = 0;
static uint64_t yield_count = 0;

extern void context_switch(pcb_t* prev, pcb_t* next);

pcb_t* scheduler_ready_queue_peek_next(void) {
    return ready_queue_head;
}

void scheduler_ready_queue_add(pcb_t* process) {
    if (!process) return;

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

    if (process->state == PROC_STATE_RUNNING) {
        process->state = PROC_STATE_READY;
    }

    process->next = NULL;
    process->prev = ready_queue_tail;
    if (ready_queue_tail) {
        ready_queue_tail->next = process;
    } else {
        ready_queue_head = process;
    }
    ready_queue_tail = process;
}

void scheduler_ready_queue_remove(pcb_t* process) {
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
     * A process is on the queue if any of these hold:
     *   - it has a prev (not the head)
     *   - it has a next (not the tail)
     *   - it is the head (covers the single-element case)
     */
    return process->prev != NULL
        || process->next != NULL
        || process == ready_queue_head;
}

void scheduler_reset(void) {
    ready_queue_head = NULL;
    ready_queue_tail = NULL;
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

    {
        pcb_t* head = scheduler_ready_queue_peek_next();
        serial_lock();
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
            serial_print("EXIT-FALLBACK: switching to idle, exiting pid=");
            serial_print_dec(exiting->pid);
            serial_print(" name=");
            serial_print(exiting->name);
            serial_print("\n");
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

    context_switch(exiting, next);

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
            next = process_find_by_pid(1);
            if (!next) return;
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
