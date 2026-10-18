#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>
#include <stdint.h>

void serial_init(void);
void serial_putc(char c);
void serial_print(const char* str);
void serial_print_hex(uint64_t value);
void serial_print_dec(uint64_t value);
void serial_write(const char* buf, size_t count);

/*
 * Shared print lock.
 *
 * Reentrant cli/sti critical section with a nesting counter. Both the
 * serial driver and the VGA driver use this lock, so a whole
 * serial_print or vga_print (or a PRINT_BOTH pair that wraps its two
 * calls in an explicit lock/unlock) is atomic against timer
 * preemption.
 *
 * serial_lock() disables interrupts and increments a depth counter.
 * serial_unlock() decrements it and re-enables interrupts only when
 * the depth returns to zero. Nested lock/unlock pairs are safe.
 *
 * The individual *_putc functions do NOT take the lock. They are
 * intended to be called from within a locked region. Any function that
 * prints more than one character must take the lock around the whole
 * operation, or a timer tick between characters will interleave other
 * output.
 *
 * Limitations (see MAINTENANCE.md items 4d / 4e):
 *   - Holds interrupts off for the duration of the print. At 115200
 *     baud a 40-char line takes ~3.5 ms; VGA is faster but still
 *     preemptible for the duration. Fine for boot messages and short
 *     diagnostics. Not a design for high-frequency kernel printing.
 *   - Global, not per-console. With multiple users this would need to
 *     become a per-tty lock.
 *   - Not SMP-safe. cli/sti is meaningless on the other CPU.
 *
 * The correct long-term design is a ring buffer drained by a console
 * task, with per-console locks. This lock is a pragmatic fix for the
 * current single-console, single-core configuration.
 */
void serial_lock(void);
void serial_unlock(void);


/* Non-blocking UART RX.  Returns the next byte from the serial
 * port, or -1 if none is available.  Used by the console input
 * multiplexer so -serial stdio is interactive. */
int serial_rx_poll(void);


/* Non-consuming UART readiness check.  Returns 1 if a byte is
 * waiting in the RX FIFO, 0 otherwise.  sys_poll uses this to
 * report POLLIN without consuming the byte read(2) will fetch. */
int serial_rx_available(void);

#endif
