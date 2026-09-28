#include <stdint.h>
#include <stddef.h>
#include "include/vga.h"
#include "include/serial.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEM    ((volatile uint16_t *)0xB8000)

#define RESERVED_ROWS 0

// VGA ports for cursor control
#define VGA_CRTC_INDEX  0x3D4
#define VGA_CRTC_DATA   0x3D5
#define CURSOR_HIGH     0x0E
#define CURSOR_LOW      0x0F

static int cursor_row = 0;
static int cursor_col = 0;

/* Background 0 — Black
static uint8_t cursor_attr = 0x00;   // black on black
...
*/

/* Background 7 — Light Gray
...
*/

// static uint8_t cursor_attr = 0x07;   // light gray on black
static uint8_t cursor_attr = 0x1E;   // yellow on blue

/*
 * ANSI/VT100 escape-sequence parser state.
 *
 * The console is not a real terminal emulator; we handle only the
 * small subset of sequences that busybox's line editor emits during
 * interactive line editing and redraw:
 *
 *     ESC [ K        erase from cursor to end of line
 *     ESC [ J        erase from cursor to end of screen
 *     ESC [ n D      cursor left n columns
 *     ESC [ n C      cursor right n columns
 *     ESC [ m        SGR (color) -- ignored
 *
 * Anything else starting with ESC is swallowed silently.  A lone ESC
 * followed by a non-'[' byte is dropped and the following byte is
 * treated as ordinary input, matching common terminal behavior for an
 * unrecognized escape.
 *
 * The parser is single-threaded and only reached through
 * vga_putc_unlocked, which is always called under the print lock.  It
 * is not reentrant and does not need to be.
 *
 * Only the "0J" / "0K" (or bare) variants of the erase sequences are
 * implemented.  busybox only emits the bare forms.  If a future
 * caller needs 1J (to cursor), 2J (whole screen), or 2K (whole line),
 * add those cases in the dispatch below.
 */
enum vga_ansi_state {
    ANSI_NORMAL = 0,
    ANSI_ESC,       /* saw ESC, waiting for '[' */
    ANSI_CSI        /* saw ESC '[', accumulating params until final byte */
};

static enum vga_ansi_state ansi_state = ANSI_NORMAL;
static int ansi_param = 0;
static int ansi_have_param = 0;

static int clamp(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/*
 * VGA print routines use the shared print lock defined in serial.c
 * (serial_lock / serial_unlock). The lock is a reentrant cli/sti
 * critical section, so nested lock/unlock pairs are safe.
 *
 * Design:
 *   - vga_putc_unlocked and vga_update_hardware_cursor are UNLOCKED
 *     primitives.  They must be called from within a locked region.
 *   - Every higher-level function (vga_print, vga_clear, etc.) takes
 *     the lock around its whole operation.
 *
 * This makes vga_print atomic against timer preemption: a timer tick
 * cannot fire between characters of a printed string. Without this,
 * the cursor state and the string output can be split by an interrupt
 * and the tail of the string lands in the wrong place.
 *
 * See include/serial.h for the full contract and MAINTENANCE.md for
 * the design discussion (items 4d and 4e).
 */

static void vga_update_hardware_cursor(void) {
    uint16_t pos = cursor_row * VGA_WIDTH + cursor_col;
    outb(VGA_CRTC_INDEX, CURSOR_HIGH);
    outb(VGA_CRTC_DATA, (pos >> 8) & 0xFF);
    outb(VGA_CRTC_INDEX, CURSOR_LOW);
    outb(VGA_CRTC_DATA, pos & 0xFF);
}

void vga_set_cursor_shape(uint8_t start_scanline, uint8_t end_scanline) {
    serial_lock();
    outb(VGA_CRTC_INDEX, 0x0A);
    outb(VGA_CRTC_DATA, start_scanline);
    outb(VGA_CRTC_INDEX, 0x0B);
    outb(VGA_CRTC_DATA, end_scanline);
    serial_unlock();
}

void vga_hide_cursor(void) {
    serial_lock();
    outb(VGA_CRTC_INDEX, CURSOR_HIGH);
    outb(VGA_CRTC_DATA, 0x20);
    outb(VGA_CRTC_INDEX, CURSOR_LOW);
    outb(VGA_CRTC_DATA, 0x00);
    serial_unlock();
}

/*
 * Unlocked primitive: scroll the screen up one line. Caller must hold
 * the print lock.
 */
static void vga_scroll(void) {
    volatile uint16_t *vga = VGA_MEM;
    
    cursor_row = clamp(cursor_row, RESERVED_ROWS, VGA_HEIGHT - 1);
    cursor_col = clamp(cursor_col, 0, VGA_WIDTH - 1);
    
    for (int row = RESERVED_ROWS; row < (VGA_HEIGHT - 1); row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            int src_idx = (row + 1) * VGA_WIDTH + col;
            int dst_idx = row * VGA_WIDTH + col;
            if (src_idx >= 0 && src_idx < (VGA_WIDTH * VGA_HEIGHT) &&
                dst_idx >= 0 && dst_idx < (VGA_WIDTH * VGA_HEIGHT)) {
                vga[dst_idx] = vga[src_idx];
            }
        }
    }
    
    uint16_t blank = ((uint16_t)cursor_attr << 8) | ' ';
    int last_row_start = (VGA_HEIGHT - 1) * VGA_WIDTH;
    for (int col = 0; col < VGA_WIDTH; col++) {
        int idx = last_row_start + col;
        if (idx >= 0 && idx < (VGA_WIDTH * VGA_HEIGHT)) {
            vga[idx] = blank;
        }
    }
    
    cursor_row = VGA_HEIGHT - 1;
    cursor_col = 0;
    vga_update_hardware_cursor();
}

/*
 * Unlocked primitives: erase region helpers for the ESC[K / ESC[J
 * family.  Caller must hold the print lock.
 *
 * vga_erase_to_end_of_line wipes from the current cell to the end of
 * the current row.  vga_erase_to_end_of_screen wipes from the current
 * cell to the bottom-right corner.
 */

static void vga_erase_to_end_of_line(void) {
    volatile uint16_t *vga = VGA_MEM;
    uint16_t blank = ((uint16_t)cursor_attr << 8) | ' ';
    int row = clamp(cursor_row, RESERVED_ROWS, VGA_HEIGHT - 1);
    int col = clamp(cursor_col, 0, VGA_WIDTH - 1);
    int base = row * VGA_WIDTH;
    for (int c = col; c < VGA_WIDTH; c++) {
        int idx = base + c;
        if (idx >= 0 && idx < (VGA_WIDTH * VGA_HEIGHT)) {
            vga[idx] = blank;
        }
    }
    vga_update_hardware_cursor();
}

static void vga_erase_to_end_of_screen(void) {
    volatile uint16_t *vga = VGA_MEM;
    uint16_t blank = ((uint16_t)cursor_attr << 8) | ' ';
    int row = clamp(cursor_row, RESERVED_ROWS, VGA_HEIGHT - 1);
    int col = clamp(cursor_col, 0, VGA_WIDTH - 1);
    int start = row * VGA_WIDTH + col;
    int end = VGA_WIDTH * VGA_HEIGHT;
    for (int i = start; i < end; i++) {
        vga[i] = blank;
    }
    vga_update_hardware_cursor();
}

/*
 * Unlocked primitive: write one character with no escape handling.
 * Caller must hold the print lock.  Public callers should go through
 * vga_putc_unlocked, which runs the ANSI parser first.
 *
 * '\r' returns the cursor to column 0 without advancing the row.
 * '\b' moves the cursor left one cell and erases the cell it lands
 * on.  Note that this is NOT standard VT100 '\b' behavior (which
 * only moves the cursor); busybox's "\b \b" idiom happens to work
 * under either.  See docs/gotchas.md for the quirk.
 */
static void vga_putc_raw(char c) {
    volatile uint16_t *vga = VGA_MEM;
    
    cursor_row = clamp(cursor_row, RESERVED_ROWS, VGA_HEIGHT - 1);
    cursor_col = clamp(cursor_col, 0, VGA_WIDTH - 1);
    
    if (c == '\n') {
        cursor_row++;
        cursor_col = 0;
        if (cursor_row >= VGA_HEIGHT) {
            vga_scroll();
            cursor_row = VGA_HEIGHT - 1;
        }
        vga_update_hardware_cursor();
        return;
    }

    if (c == '\r') {
        cursor_col = 0;
        vga_update_hardware_cursor();
        return;
    }

    if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            int idx = cursor_row * VGA_WIDTH + cursor_col;
            if (idx >= 0 && idx < (VGA_WIDTH * VGA_HEIGHT)) {
                vga[idx] = ((uint16_t)cursor_attr << 8) | ' ';
            }
        }
        vga_update_hardware_cursor();
        return;
    }

    if (c >= ' ' && c <= '~') {
        cursor_row = clamp(cursor_row, RESERVED_ROWS, VGA_HEIGHT - 1);
        cursor_col = clamp(cursor_col, 0, VGA_WIDTH - 1);
        
        int idx = cursor_row * VGA_WIDTH + cursor_col;
        if (idx >= 0 && idx < (VGA_WIDTH * VGA_HEIGHT)) {
            vga[idx] = ((uint16_t)cursor_attr << 8) | (uint8_t)c;
        }

        cursor_col++;
        if (cursor_col >= VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
            if (cursor_row >= VGA_HEIGHT) {
                vga_scroll();
                cursor_row = VGA_HEIGHT - 1;
            }
        }
        vga_update_hardware_cursor();
    }
}

/*
 * Unlocked primitive: write one character, running the ANSI parser
 * first.  Caller must hold the print lock.  This is the function the
 * rest of the file should call.
 */
static void vga_putc_unlocked(char c) {
    switch (ansi_state) {
    case ANSI_NORMAL:
        if (c == 0x1B) {           /* ESC */
            ansi_state = ANSI_ESC;
            return;
        }
        vga_putc_raw(c);
        return;

    case ANSI_ESC:
        if (c == '[') {
            ansi_state = ANSI_CSI;
            ansi_param = 0;
            ansi_have_param = 0;
            return;
        }
        /* Unknown escape; drop it and re-feed c as ordinary input. */
        ansi_state = ANSI_NORMAL;
        vga_putc_raw(c);
        return;

    case ANSI_CSI:
        if (c >= '0' && c <= '9') {
            ansi_param = ansi_param * 10 + (c - '0');
            ansi_have_param = 1;
            return;
        }
        if (c == ';') {
            /* Parameter separator.  We only care about the first
             * parameter in the sequences we support; reset the
             * accumulator so the next parameter starts fresh. */
            ansi_param = 0;
            ansi_have_param = 0;
            return;
        }
        /* Final byte.  Dispatch and return to NORMAL. */
        switch (c) {
        case 'K':   /* erase to end of line */
            /* Only the "to end" variant (param 0 or absent) is
             * implemented; 1K and 2K would need extra cases. */
            vga_erase_to_end_of_line();
            break;
        case 'J':   /* erase to end of screen */
            /* Only the "to end" variant (param 0 or absent) is
             * implemented; 1J and 2J would need extra cases. */
            vga_erase_to_end_of_screen();
            break;
        case 'D':   /* cursor left n */
            {
                int n = ansi_have_param ? ansi_param : 1;
                if (n < 1) n = 1;
                int col = cursor_col - n;
                if (col < 0) col = 0;
                cursor_col = col;
                vga_update_hardware_cursor();
            }
            break;
        case 'C':   /* cursor right n */
            {
                int n = ansi_have_param ? ansi_param : 1;
                if (n < 1) n = 1;
                int col = cursor_col + n;
                if (col >= VGA_WIDTH) col = VGA_WIDTH - 1;
                cursor_col = col;
                vga_update_hardware_cursor();
            }
            break;
        case 'm':   /* SGR (color) -- ignore */
        case 'H':   /* cursor position -- ignore for now */
        case 'f':
        case 'h':   /* mode set -- ignore */
        case 'l':   /* mode reset -- ignore */
        case 'A':   /* cursor up -- ignore */
        case 'B':   /* cursor down -- ignore */
            break;
        default:
            /* Unknown final byte; drop the whole sequence. */
            break;
        }
        ansi_state = ANSI_NORMAL;
        ansi_param = 0;
        ansi_have_param = 0;
        return;
    }
}

/*
 * Public single-character write. Takes the lock so callers that write
 * one character at a time do not need to manage the lock themselves.
 * Prefer this over vga_putc_unlocked unless you are inside a locked
 * region.
 */
void vga_putc(char c) {
    serial_lock();
    vga_putc_unlocked(c);
    serial_unlock();
}

void vga_write(const char* data, size_t count) {
    serial_lock();
    for (size_t i = 0; i < count; i++) {
        vga_putc_unlocked(data[i]);
    }
    serial_unlock();
}

void vga_print(const char *s) {
    serial_lock();
    while (*s) {
        vga_putc_unlocked(*s++);
    }
    serial_unlock();
}

void vga_print_at(int row, int col, const char *s) {
    serial_lock();
    int saved_row = cursor_row;
    int saved_col = cursor_col;
    
    row = clamp(row, RESERVED_ROWS, VGA_HEIGHT - 1);
    col = clamp(col, 0, VGA_WIDTH - 1);
    cursor_row = row;
    cursor_col = col;
    vga_update_hardware_cursor();
    
    while (*s) {
        vga_putc_unlocked(*s++);
    }
    
    cursor_row = clamp(saved_row, RESERVED_ROWS, VGA_HEIGHT - 1);
    cursor_col = clamp(saved_col, 0, VGA_WIDTH - 1);
    vga_update_hardware_cursor();
    serial_unlock();
}

void vga_print_hex_cur(uint64_t val) {
    const char *hex = "0123456789ABCDEF";
    char buf[17];
    buf[16] = '\0';
    
    // Build the string from right to left
    for (int i = 0; i < 16; i++) {
        buf[15 - i] = hex[(val >> (i * 4)) & 0xF];
    }
    
    serial_lock();
    
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    int pos = cursor_row * 80 + cursor_col;
    uint16_t attr = (uint16_t)cursor_attr << 8;
    
    for (int i = 0; i < 16; i++) {
        if (pos + i < 80 * 25) {
            vga[pos + i] = attr | (uint8_t)buf[i];
        }
    }
    cursor_col += 16;
    if (cursor_col >= 80) {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= 25) {
            // Need to scroll
            vga_scroll();
            cursor_row = 24;
        }
    }
    vga_update_hardware_cursor();
    
    serial_unlock();
}

void vga_print_dec_cur(uint64_t val) {
    char buf[32];
    int idx = 31;
    buf[idx--] = '\0';

    serial_lock();
    if (val == 0) {
        vga_putc_unlocked('0');
    } else {
        while (val > 0 && idx >= 0) {
            buf[idx--] = '0' + (val % 10);
            val /= 10;
        }
        int start = idx + 1;
        while (buf[start] != '\0') {
            vga_putc_unlocked(buf[start++]);
        }
    }
    serial_unlock();
}

void vga_clear(void) {
    volatile uint16_t *vga = VGA_MEM;
    serial_lock();
    
    uint16_t blank = ((uint16_t)cursor_attr << 8) | ' ';
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = blank;
    }
    cursor_row = 0;
    cursor_col = 0;
    vga_update_hardware_cursor();
    
    serial_unlock();
}

void vga_set_cursor(int row, int col) {
    serial_lock();
    cursor_row = clamp(row, RESERVED_ROWS, VGA_HEIGHT - 1);
    cursor_col = clamp(col, 0, VGA_WIDTH - 1);
    vga_update_hardware_cursor();
    serial_unlock();
}

void vga_print_color(const char *s, uint8_t color) {
    serial_lock();
    
    uint8_t old_attr = cursor_attr;
    cursor_attr = color;
    
    while (*s) {
        vga_putc_unlocked(*s++);
    }
    
    cursor_attr = old_attr;
    
    serial_unlock();
}
