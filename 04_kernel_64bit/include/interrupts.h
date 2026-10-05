#pragma once

#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

void pic_remap(void);
void irq0_handler(void);
void irq1_handler(void);
void pit_init(uint32_t freq);

// Declare g_ticks as extern so kmain can access it
extern volatile uint64_t g_ticks;

typedef struct exception_frame {
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} exception_frame_t;

/*
 * Default-handler frame.  This is what isr_default_common passes to
 * isr_default_handler().  It extends exception_frame_t with the vector
 * number, which is pushed by the default stub before the common
 * trampoline pushes all GPRs.
 *
 * The GPR fields are present for completeness; the default handler
 * prints only the control and vector fields.
 */
typedef struct default_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) default_frame_t;

/*
 * Expected-fault protocol for the kernel self-test.
 *
 * g_expect_fault is -1 when no fault is expected, or the vector number
 * of a fault the self-test has deliberately triggered.  The exception
 * handlers check this on entry: if it matches their own vector, they
 * call fault_kill_current() instead of the diagnostic-and-halt path.
 * Otherwise they behave exactly as before.
 *
 * g_fault_observed is -1 until a fault is taken with g_expect_fault
 * set; the handler then records the vector here.  The self-test reads
 * this after the kernel shell resumes to decide pass/fail.
 *
 * -1 is used rather than 0 because 0 is the #DE vector and would be
 * indistinguishable from "no fault expected".
 *
 * Both are written from interrupt context and read from process
 * context.  The kernel is single-CPU, so a plain volatile int is
 * sufficient; no atomics or memory barriers are required.
 */
extern volatile int g_expect_fault;
extern volatile int g_fault_observed;

/*
 * Terminate the current process from an expected fault.
 *
 * Called by the exception handlers when g_expect_fault matches their
 * vector.  Records the vector in g_fault_observed, clears g_expect_fault,
 * and invokes process_exit(), which reclaims the process and switches
 * back to the kernel shell.  Does not return.
 *
 * The child process that triggers the fault must be a kernel-mode
 * process (entry_point >= KERNEL_BASE).  process_exit() uses that
 * distinction to decide between "resume the kernel shell" and "halt",
 * so a user-mode fault trigger would halt instead of resuming.
 */
void fault_kill_current(int vec) __attribute__((noreturn));

void isr0_handler(exception_frame_t *frame);
void isr6_handler(exception_frame_t *frame);
void isr8_handler(exception_frame_t *frame);
void isr13_handler(exception_frame_t *frame);
void isr14_handler(exception_frame_t *frame);
void isr_default_handler(default_frame_t *frame);

#endif
