#include "include/serial.h"
#include <stddef.h>
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/*
 * Shared print lock. See include/serial.h for the contract.
 *
 * The lock saves and restores RFLAGS on the outermost lock/unlock
 * pair. This means:
 *   - At shell level, serial_lock disables interrupts, serial_unlock
 *     re-enables them.
 *   - Inside an interrupt or exception handler (interrupts already
 *     off), serial_lock is a no-op at the CPU level, and serial_unlock
 *     leaves interrupts off. The handler remains undisturbed.
 * Nested lock/unlock pairs are safe: only the outermost pair touches
 * RFLAGS.
 */
static uint32_t g_print_lock_depth = 0;
static uint64_t g_print_lock_saved_rflags = 0;

void serial_lock(void) {
    if (g_print_lock_depth == 0) {
        __asm__ volatile(
            "pushfq\n\t"
            "popq %0\n\t"
            "cli"
            : "=r"(g_print_lock_saved_rflags)
            :
            : "memory"
        );
    } else {
        __asm__ volatile("cli" ::: "memory");
    }
    g_print_lock_depth++;
}

void serial_unlock(void) {
    if (g_print_lock_depth > 0) {
        g_print_lock_depth--;
        if (g_print_lock_depth == 0) {
            uint64_t saved = g_print_lock_saved_rflags;
            __asm__ volatile(
                "pushq %0\n\t"
                "popfq"
                :
                : "r"(saved)
                : "memory", "cc"
            );
        }
    }
}

void serial_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

void serial_putc(char c) {
    /*
     * Emit CR before LF.  A serial console is a "raw" device: the
     * terminal it feeds may be in any mode, and the telnet NVT
     * protocol explicitly requires a carriage return before a line
     * feed ("\r\n").  On -serial stdio the extra \r is harmless
     * (every terminal emulator treats \r\n as one newline); on
     * -serial telnet: it is what makes multi-line output line up
     * instead of stairstepping.
     */
    if (c == '\n') {
        while ((inb(0x3F8 + 5) & 0x20) == 0);
        outb(0x3F8, '\r');
    }
    while ((inb(0x3F8 + 5) & 0x20) == 0);
    outb(0x3F8, c);
}

void serial_write(const char* buf, size_t count) {
    serial_lock();
    for (size_t i = 0; i < count; i++) {
        serial_putc(buf[i]);
    }
    serial_unlock();
}

void serial_print(const char* str) {
    serial_lock();
    while (*str) serial_putc(*str++);
    serial_unlock();
}

void serial_print_hex(uint64_t value) {
    char hex[] = "0123456789ABCDEF";
    char buf[16];

    serial_lock();
    for (int i = 15; i >= 0; i--) {
        buf[i] = hex[value & 0xF];
        value >>= 4;
    }
    for (int i = 0; i < 16; i++) serial_putc(buf[i]);
    serial_unlock();
}

void serial_print_dec(uint64_t value) {
    char buf[32];
    int idx = 31;
    buf[idx--] = '\0';

    serial_lock();
    if (value == 0) {
        serial_putc('0');
        serial_unlock();
        return;
    }
    while (value > 0 && idx >= 0) {
        buf[idx--] = '0' + (value % 10);
        value /= 10;
    }
    for (int i = idx + 1; buf[i] != '\0'; i++) {
        serial_putc(buf[i]);
    }
    serial_unlock();
}

/*
 * Non-blocking UART receive.
 *
 * Returns the next byte from the RX FIFO, or -1 if none is available.
 * This is the RX half of -serial stdio; without it, keystrokes sent
 * by the terminal reach the emulated UART but are never read by the
 * guest, so headless boots appear unresponsive.
 *
 * The Line Status Register (LSR, offset +5) bit 0 (Data Ready) is the
 * only signal we need.  No interrupts are required; polling is enough
 * at shell-input rates and avoids taking an IRQ in the middle of
 * whatever the kernel is doing.
 *
 * Loopback is NOT enabled (MCR bit 4 is clear), so the bytes we read
 * here come from the terminal, not from our own serial_putc output.
 */
/*
 * Serial RX ring.
 *
 * The UART data register read is destructive, so a 0x03 cannot be
 * spotted by "peeking": whoever reads the byte takes it.  A tick
 * that reads to look therefore steals bytes the line editor needs --
 * proven by two failed probes (token printed, no prompt; and a
 * corrupted line with the shell's `>' continuation prompt).
 *
 * Instead the tick DRAINS the UART into this ring.  Ordinary bytes
 * are never lost (they wait in the ring for the reader); a 0x03 is
 * consumed at drain time as the ^C signal.  serial_rx_poll pops the
 * ring, serial_rx_available reports "ring non-empty", so every
 * existing caller (kbd_buffer_get_mux, sys_poll) keeps its contract.
 *
 * Hazards: the ring is 64 bytes; if the reader stalls longer than
 * that, new bytes are DROPPED (the writer never overwrites unread
 * data) -- a stuck reader stalls visibly rather than corrupting.
 * Only the cli-held tick and the cli-held mux touch head/tail, so on
 * this single core no lock is needed.  This is the minimal ring, not
 * the console-task design the header names as long-term.
 */
#define SERIAL_RX_RING_SIZE 64u   /* power of two */
static volatile uint8_t  rx_ring[SERIAL_RX_RING_SIZE];
static volatile unsigned rx_head = 0;   /* write */
static volatile unsigned rx_tail = 0;   /* read  */

/*
 * Drain the UART RX FIFO into the ring.  Returns 1 if a 0x03 was
 * seen (and consumed) during the drain, else 0.  Called from the
 * timer tick, interrupts off.  Does NOT take serial_lock: the tick
 * is already cli-off and this prints nothing.
 */
int serial_rx_drain_ring(void) {
    int saw_ctrlc = 0;
    /* Bounded: at most the FIFO depth per tick.  An unbounded
     * while(data-ready) can spin forever if the emulated FIFO reports
     * data-ready without the read clearing it (seen: the guest froze
     * on ed -s, the first row that stays in one syscall long enough
     * for the tick to spin).  Bounded means a stuck FIFO is retried
     * next tick, never a hang. */
    for (int _i = 0; _i < 16; _i++) {
        if ((inb(0x3F8 + 5) & 0x01) == 0) break;
        uint8_t b = (uint8_t)inb(0x3F8);
        if (b == 0x03) {
            saw_ctrlc = 1;
            continue;   /* consume the signal byte, do not ring it */
        }
        unsigned next = (rx_head + 1u) & (SERIAL_RX_RING_SIZE - 1u);
        if (next == rx_tail) {
            /* Full: drop new, keep unread.  Visible stall, not corruption. */
            continue;
        }
        rx_ring[rx_head] = b;
        rx_head = next;
    }
    return saw_ctrlc;
}

int serial_rx_poll(void) {
    if (rx_head == rx_tail) return -1;
    uint8_t b = rx_ring[rx_tail];
    rx_tail = (rx_tail + 1u) & (SERIAL_RX_RING_SIZE - 1u);
    return (int)b;
}

/* Non-consuming: 1 iff a ring byte is waiting (sys_poll POLLIN). */
int serial_rx_available(void) {
    return rx_head != rx_tail;
}
