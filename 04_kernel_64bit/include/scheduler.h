#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process.h"

// Scheduler functions
void scheduler_init(void);
void scheduler_ready_queue_add(pcb_t* process);
void scheduler_ready_queue_remove(pcb_t* process);
pcb_t* scheduler_ready_queue_next(void);
int scheduler_ready_queue_empty(void);
/*
 * Returns non-zero if `process` is currently on the ready queue.
 *
 * Used by process_wake_all_blocked to avoid re-adding a process that
 * is technically BLOCKED but has already been woken by a previous
 * tick and not yet resumed.  The ready-queue add path is not
 * idempotent on its own (it does not detect duplicates), so this
 * check is required to prevent self-links and list corruption.
 */
int scheduler_ready_queue_contains(pcb_t* process);
pcb_t* scheduler_ready_queue_peek_next(void);

pcb_t* scheduler_schedule(void);
void scheduler_switch_to(pcb_t* next);
void process_yield(void);
__attribute__((noreturn)) void process_exit(void);
void scheduler_reset(void);
pcb_t* scheduler_get_current(void);
void scheduler_set_current(pcb_t* proc);
void scheduler_stats(void);

#endif
