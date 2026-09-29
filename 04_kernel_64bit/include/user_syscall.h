// user_syscall.h
#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stdint.h>

/*
 * Forward declaration only.  The real definition is in process.h;
 * we do not include it here to keep this header dependency-light
 * and avoid an include cycle (process.c includes this header).
 * struct pcb's tag and the pcb_t typedef name the same type.
 */
struct pcb;

// Function prototypes
uint64_t syscall_dispatch(uint64_t num, uint64_t arg0, uint64_t arg1, 
                          uint64_t arg2, uint64_t arg3, 
                          uint64_t arg4, uint64_t arg5);

/*
 * Install console sentinel slots in a new process's fds 0, 1, and 2.
 *
 * Called from process_create after file_table[] has been zeroed.
 * A fresh process's stdio fds are "occupied" so open(2) returns
 * fd 3, matching Linux; closing fd 0/1/2 frees the sentinel and the
 * next open(2) may reuse it.  On kmalloc failure an fd is left NULL,
 * which degrades to the pre-sentinel behavior.
 *
 * Implemented in user_syscall.c, which owns file_slot_t.
 */
void user_syscall_init_console_fds(struct pcb* pcb);

// void syscall_init(void);

#endif
