#include <stdint.h>
#include <stddef.h>
#include "include/vga.h"
#include "include/serial.h"

/* ------------------------------------------------------------------ */
/* Tunables                                                            */
/* ------------------------------------------------------------------ */

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

/* The one and only framebuffer. VGA text mode has no second hardware
 * buffer; the CRTC scans this address unconditionally. Any "alternate
 * screen" is therefore simulated by saving/restoring this region. */
#define VGA_MAIN_MEM ((uint16_t *)0xB8000)

#define RESERVED_ROWS 0

/* Default cell attribute when SGR 0 is issued. Yellow on blue matches
 * the console's historical look; flip to 0x07 for gray-on-black. */
#define VGA_DEFAULT_ATTR 0x1E

/* Set to 1 to emit a one-line diagnostic on the serial port for any
 * CSI sequence the parser recognizes but does not dispatch. Set to 0
 * for the ported/clean version. */
#define VGA_TRACE_UNHANDLED 0

/* Set to 1 to reply to DSR (ESC[6n) and DA (ESC[c) queries on the
 * serial port. Curses uses these to probe the terminal. Set to 0 if
 * nothing on the host side reads our serial output. */
#define VGA_REPLY_TO_QUERIES 0

/* VGA CRTC ports */
#define VGA_CRTC_INDEX  0x3D4
#define VGA_CRTC_DATA   0x3D5
#define CURSOR_HIGH     0x0E
#define CURSOR_LOW      0x0F

/* ------------------------------------------------------------------ */
/* Parser limits                                                       */
/* ------------------------------------------------------------------ */

#define MAX_CSI_PARAMS        8
#define MAX_CSI_INTERMEDIATES 2

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

/* Always points at VGA_MAIN_MEM. Kept as a named pointer so the
 * drawing primitives read naturally ("active screen" rather than
 * "framebuffer"), and so a future port that gains a real second
 * buffer could swap it. Today it never changes. */
static uint16_t *active_screen = VGA_MAIN_MEM;

/* Storage for the saved primary screen while ?1049h is active. On
 * ?1049h we copy 0xB8000 into here, then clear 0xB8000. On ?1049l we
 * copy back. This is what a real VT100 does in software; on a VGA
 * text console it is the only thing that can work, because the CRT
 * controller has no idea alt_screen[] exists. */
static uint16_t alt_screen[VGA_WIDTH * VGA_HEIGHT];

static int alt_saved_row = 0;
static int alt_saved_col = 0;
static int alt_screen_active = 0;

static int cursor_row = 0;
static int cursor_col = 0;

static uint8_t cursor_attr = VGA_DEFAULT_ATTR;
static int cursor_visible = 1;

enum ansi_state {
    ANSI_NORMAL = 0,
    ANSI_ESC,
    ANSI_ESC_CHARSET,   /* saw ESC ( ) * + ; swallow one designator byte */
    ANSI_CSI
};

static struct {
    enum ansi_state state;
    uint8_t  intermediates[MAX_CSI_INTERMEDIATES];
    int      n_intermediates;
    int      params[MAX_CSI_PARAMS];
    int      n_params;
    int      cur_param;
    int      have_cur_param;
    int      private_marker;   /* saw '?' (or '>' etc.) */
} p;

/* ------------------------------------------------------------------ */
/* Low-level helpers                                                   */
/* ------------------------------------------------------------------ */

static int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void vga_update_hardware_cursor(void) {
    if (!cursor_visible) {
        outb(VGA_CRTC_INDEX, CURSOR_HIGH);
        outb(VGA_CRTC_DATA, 0x20);
        outb(VGA_CRTC_INDEX, CURSOR_LOW);
        outb(VGA_CRTC_DATA, 0x00);
        return;
    }
    uint16_t pos = (uint16_t)(cursor_row * VGA_WIDTH + cursor_col);
    outb(VGA_CRTC_INDEX, CURSOR_HIGH);
    outb(VGA_CRTC_DATA, (pos >> 8) & 0xFF);
    outb(VGA_CRTC_INDEX, CURSOR_LOW);
    outb(VGA_CRTC_DATA, pos & 0xFF);
}

static inline uint16_t blank_cell(void) {
    return ((uint16_t)cursor_attr << 8) | (uint16_t)' ';
}

/* ------------------------------------------------------------------ */
/* Trace hook                                                          */
/* ------------------------------------------------------------------ */

#if VGA_TRACE_UNHANDLED
/* Emit a compact diagnostic for an unhandled CSI sequence. Goes to
 * the serial port, not the screen. serial_print takes the (reentrant)
 * print lock internally, which is safe here because the caller already
 * holds it. */
static void vga_trace_csi(const char *note) {
    char buf[96];
    int  n = 0;

#define APP(s) do { \
        for (const char *_s = (s); *_s && n < (int)sizeof(buf) - 1; _s++) \
            buf[n++] = *_s; \
    } while (0)

    APP("[vga] unhandled CSI: ");
    if (p.private_marker && n < (int)sizeof(buf) - 1) buf[n++] = '?';
    for (int i = 0; i < p.n_intermediates && n < (int)sizeof(buf) - 1; i++) {
        buf[n++] = (char)p.intermediates[i];
    }
    for (int i = 0; i < p.n_params && n < (int)sizeof(buf) - 3; i++) {
        int v = p.params[i];
        char tmp[8]; int t = 0;
        if (v == 0) tmp[t++] = '0';
        while (v > 0) { tmp[t++] = (char)('0' + v % 10); v /= 10; }
        while (t > 0 && n < (int)sizeof(buf) - 1) buf[n++] = tmp[--t];
        if (i + 1 < p.n_params && n < (int)sizeof(buf) - 1) buf[n++] = ';';
    }
    if (note && note[0]) {
        if (n < (int)sizeof(buf) - 1) buf[n++] = ' ';
        APP(note);
    }
    if (n < (int)sizeof(buf) - 2) { buf[n++] = '\r'; buf[n++] = '\n'; }
    buf[n] = '\0';

#undef APP

    serial_print(buf);
}
#else
static void vga_trace_csi(const char *note) { (void)note; }
#endif

#if VGA_REPLY_TO_QUERIES
static void vga_serial_reply_cursor(void) {
    char buf[24];
    int n = 0;
    buf[n++] = 0x1B; buf[n++] = '[';
    int r = cursor_row + 1;
    char t[8]; int tn = 0;
    if (r == 0) t[tn++] = '0';
    while (r > 0) { t[tn++] = (char)('0' + r % 10); r /= 10; }
    while (tn > 0) buf[n++] = t[--tn];
    buf[n++] = ';';
    int c = cursor_col + 1;
    tn = 0;
    if (c == 0) t[tn++] = '0';
    while (c > 0) { t[tn++] = (char)('0' + c % 10); c /= 10; }
    while (tn > 0) buf[n++] = t[--tn];
    buf[n++] = 'R';
    buf[n] = '\0';
    serial_print(buf);
}

static void vga_serial_reply_da(void) {
    serial_print("\x1b[?1;0c");   /* claim VT100 */
}
#else
static void vga_serial_reply_cursor(void) {}
static void vga_serial_reply_da(void) {}
#endif

/* ------------------------------------------------------------------ */
/* Erase / scroll primitives (operate on active_screen)                */
/* ------------------------------------------------------------------ */

static void vga_scroll(void) {
    uint16_t blank = blank_cell();
    for (int row = RESERVED_ROWS; row < VGA_HEIGHT - 1; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            active_screen[row * VGA_WIDTH + col] =
                active_screen[(row + 1) * VGA_WIDTH + col];
        }
    }
    int last = (VGA_HEIGHT - 1) * VGA_WIDTH;
    for (int col = 0; col < VGA_WIDTH; col++) {
        active_screen[last + col] = blank;
    }
    cursor_row = VGA_HEIGHT - 1;
    cursor_col = 0;
    vga_update_hardware_cursor();
}

static void vga_scroll_region_up(int top, int bottom, int n) {
    if (top < RESERVED_ROWS) top = RESERVED_ROWS;
    if (bottom > VGA_HEIGHT - 1) bottom = VGA_HEIGHT - 1;
    if (n <= 0 || top > bottom) return;
    int height = bottom - top + 1;
    if (n > height) n = height;

    for (int row = top; row <= bottom - n; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            active_screen[row * VGA_WIDTH + col] =
                active_screen[(row + n) * VGA_WIDTH + col];
        }
    }
    uint16_t blank = blank_cell();
    for (int row = bottom - n + 1; row <= bottom; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            active_screen[row * VGA_WIDTH + col] = blank;
        }
    }
}

static void vga_scroll_region_down(int top, int bottom, int n) {
    if (top < RESERVED_ROWS) top = RESERVED_ROWS;
    if (bottom > VGA_HEIGHT - 1) bottom = VGA_HEIGHT - 1;
    if (n <= 0 || top > bottom) return;
    int height = bottom - top + 1;
    if (n > height) n = height;

    for (int row = bottom; row >= top + n; row--) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            active_screen[row * VGA_WIDTH + col] =
                active_screen[(row - n) * VGA_WIDTH + col];
        }
    }
    uint16_t blank = blank_cell();
    for (int row = top; row < top + n; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            active_screen[row * VGA_WIDTH + col] = blank;
        }
    }
}

/* ESC[K family. mode: 0=to end of line, 1=to start, 2=whole line. */
static void vga_erase_line(int mode) {
    uint16_t blank = blank_cell();
    int base = cursor_row * VGA_WIDTH;
    int from, to;
    switch (mode) {
        case 1: from = 0;            to = cursor_col;        break;
        case 2: from = 0;            to = VGA_WIDTH - 1;     break;
        default: from = cursor_col;  to = VGA_WIDTH - 1;     break;
    }
    for (int c = from; c <= to; c++) active_screen[base + c] = blank;
    vga_update_hardware_cursor();
}

/* ESC[J family. mode: 0=below, 1=above, 2=all. */
static void vga_erase_display(int mode) {
    uint16_t blank = blank_cell();
    int cur = cursor_row * VGA_WIDTH + cursor_col;
    int total = VGA_WIDTH * VGA_HEIGHT;
    switch (mode) {
        case 1:
            for (int i = 0; i <= cur; i++) active_screen[i] = blank;
            break;
        case 2:
            for (int i = 0; i < total; i++) active_screen[i] = blank;
            break;
        default:
            for (int i = cur; i < total; i++) active_screen[i] = blank;
            break;
    }
    vga_update_hardware_cursor();
}

static void vga_delete_chars(int n) {
    if (n < 1) n = 1;
    int base = cursor_row * VGA_WIDTH;
    if (n > VGA_WIDTH - cursor_col) n = VGA_WIDTH - cursor_col;
    for (int c = cursor_col; c + n < VGA_WIDTH; c++) {
        active_screen[base + c] = active_screen[base + c + n];
    }
    uint16_t blank = blank_cell();
    for (int c = VGA_WIDTH - n; c < VGA_WIDTH; c++) {
        active_screen[base + c] = blank;
    }
    vga_update_hardware_cursor();
}

static void vga_insert_chars(int n) {
    if (n < 1) n = 1;
    int base = cursor_row * VGA_WIDTH;
    if (n > VGA_WIDTH - cursor_col) n = VGA_WIDTH - cursor_col;
    for (int c = VGA_WIDTH - 1; c - n >= cursor_col; c--) {
        active_screen[base + c] = active_screen[base + c - n];
    }
    uint16_t blank = blank_cell();
    for (int c = cursor_col; c < cursor_col + n; c++) {
        active_screen[base + c] = blank;
    }
    vga_update_hardware_cursor();
}

static void vga_erase_chars(int n) {
    if (n < 1) n = 1;
    int base = cursor_row * VGA_WIDTH;
    if (n > VGA_WIDTH - cursor_col) n = VGA_WIDTH - cursor_col;
    uint16_t blank = blank_cell();
    for (int c = cursor_col; c < cursor_col + n; c++) {
        active_screen[base + c] = blank;
    }
    vga_update_hardware_cursor();
}

static void vga_insert_lines(int n) {
    vga_scroll_region_down(cursor_row, VGA_HEIGHT - 1, n);
    cursor_col = 0;
    vga_update_hardware_cursor();
}

static void vga_delete_lines(int n) {
    vga_scroll_region_up(cursor_row, VGA_HEIGHT - 1, n);
    cursor_col = 0;
    vga_update_hardware_cursor();
}

/* ------------------------------------------------------------------ */
/* Raw character output                                                */
/* ------------------------------------------------------------------ */

static void vga_putc_raw(char c) {
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
            active_screen[cursor_row * VGA_WIDTH + cursor_col] = blank_cell();
        }
        vga_update_hardware_cursor();
        return;
    }
    if (c == '\t') {
        int next = (cursor_col + 8) & ~7;
        if (next > VGA_WIDTH - 1) next = VGA_WIDTH - 1;
        cursor_col = next;
        vga_update_hardware_cursor();
        return;
    }
    if (c == 0x07) {
        /* BEL: no speaker yet; ignore. This is where a beep would go. */
        return;
    }
    if (c >= ' ' && c <= '~') {
        active_screen[cursor_row * VGA_WIDTH + cursor_col] =
            ((uint16_t)cursor_attr << 8) | (uint8_t)c;
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
    /* Other C0 controls (0x00..0x1F except the above) are dropped. */
}

/* ------------------------------------------------------------------ */
/* SGR                                                                 */
/* ------------------------------------------------------------------ */

static uint8_t sgr_reverse(uint8_t attr) {
    return (uint8_t)(((attr & 0x0F) << 4) | ((attr & 0xF0) >> 4));
}

static void vga_apply_sgr(void) {
    if (p.n_params == 0) {
        cursor_attr = VGA_DEFAULT_ATTR;
        return;
    }
    for (int i = 0; i < p.n_params; i++) {
        int v = p.params[i];
        switch (v) {
            case 0:
                cursor_attr = VGA_DEFAULT_ATTR;
                break;
            case 1:
                cursor_attr |= 0x08;   /* bold: intensity bit */
                break;
            case 7:
                cursor_attr = sgr_reverse(cursor_attr);
                break;
            case 22:
                cursor_attr &= (uint8_t)~0x08;
                break;
            case 27:
                cursor_attr = VGA_DEFAULT_ATTR;
                break;
            case 39:
                cursor_attr = (uint8_t)((cursor_attr & 0xF0) | 0x07);
                break;
            case 49:
                cursor_attr = (uint8_t)((cursor_attr & 0x0F) | 0x00);
                break;
            default:
                if (v >= 30 && v <= 37) {
                    cursor_attr = (uint8_t)((cursor_attr & 0xF0) | (v - 30));
                } else if (v >= 40 && v <= 47) {
                    cursor_attr = (uint8_t)((cursor_attr & 0x0F) | ((v - 40) << 4));
                }
                break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Private-mode h/l                                                    */
/* ------------------------------------------------------------------ */

static void vga_alt_screen_enter(int clear) {
    if (alt_screen_active) return;
    alt_saved_row = cursor_row;
    alt_saved_col = cursor_col;

    /* Save the visible framebuffer into alt_screen[], then (for
     * ?1049h, not ?1047h) clear the visible screen. There is no
     * second hardware buffer on a VGA text console; the CRT
     * controller scans 0xB8000 unconditionally. The "alternate
     * screen" is simulated by off-screen memory that we copy back
     * on ?1049l. */
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        alt_screen[i] = vga[i];
    }
    if (clear) {
        uint16_t blank = ((uint16_t)VGA_DEFAULT_ATTR << 8) | ' ';
        for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
            vga[i] = blank;
        }
    }

    alt_screen_active = 1;
    cursor_row = 0;
    cursor_col = 0;
    vga_update_hardware_cursor();
}

static void vga_alt_screen_leave(void) {
    if (!alt_screen_active) return;
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = alt_screen[i];
    }
    alt_screen_active = 0;
    cursor_row = alt_saved_row;
    cursor_col = alt_saved_col;
    vga_update_hardware_cursor();
}

static void vga_set_private_mode(int mode, int set) {
    switch (mode) {
        case 25:
            cursor_visible = set ? 1 : 0;
            vga_update_hardware_cursor();
            break;
        case 1:
            /* DECCKM: application cursor keys. Accepted, not acted on. */
            break;
        case 1049:
            if (set) vga_alt_screen_enter(1);
            else     vga_alt_screen_leave();
            break;
        case 1047:
            if (set) vga_alt_screen_enter(0);
            else     vga_alt_screen_leave();
            break;
        default:
            vga_trace_csi("(private mode)");
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Parameter accessor                                                  */
/* ------------------------------------------------------------------ */

/* VT100 convention: parameter 0 (or absent) means "default". */
static int csi_param(int idx, int def) {
    if (idx >= p.n_params) return def;
    int v = p.params[idx];
    if (v == 0) return def;
    return v;
}

/* ------------------------------------------------------------------ */
/* CSI dispatch                                                        */
/* ------------------------------------------------------------------ */

static void csi_dispatch(uint8_t final) {
    /* Intermediates (space, !, ", #, $, ...) mark sequences we don't
     * implement. '?' is NOT stored here; it lives in private_marker. */
    if (p.n_intermediates > 0) {
        vga_trace_csi("(with intermediates)");
        return;
    }

    switch (final) {
        case 'A': {
            int n = csi_param(0, 1);
            cursor_row = clamp(cursor_row - n, RESERVED_ROWS, VGA_HEIGHT - 1);
            vga_update_hardware_cursor();
            break;
        }
        case 'B': {
            int n = csi_param(0, 1);
            cursor_row = clamp(cursor_row + n, RESERVED_ROWS, VGA_HEIGHT - 1);
            vga_update_hardware_cursor();
            break;
        }
        case 'C': {
            int n = csi_param(0, 1);
            cursor_col = clamp(cursor_col + n, 0, VGA_WIDTH - 1);
            vga_update_hardware_cursor();
            break;
        }
        case 'D': {
            int n = csi_param(0, 1);
            cursor_col = clamp(cursor_col - n, 0, VGA_WIDTH - 1);
            vga_update_hardware_cursor();
            break;
        }
        case 'E': {
            int n = csi_param(0, 1);
            cursor_row = clamp(cursor_row + n, RESERVED_ROWS, VGA_HEIGHT - 1);
            cursor_col = 0;
            vga_update_hardware_cursor();
            break;
        }
        case 'F': {
            int n = csi_param(0, 1);
            cursor_row = clamp(cursor_row - n, RESERVED_ROWS, VGA_HEIGHT - 1);
            cursor_col = 0;
            vga_update_hardware_cursor();
            break;
        }
        case 'G': {
            int c = csi_param(0, 1) - 1;
            cursor_col = clamp(c, 0, VGA_WIDTH - 1);
            vga_update_hardware_cursor();
            break;
        }
        case 'H':
        case 'f': {
            int r = csi_param(0, 1) - 1;
            int c = csi_param(1, 1) - 1;
            cursor_row = clamp(r, RESERVED_ROWS, VGA_HEIGHT - 1);
            cursor_col = clamp(c, 0, VGA_WIDTH - 1);
            vga_update_hardware_cursor();
            break;
        }
        case 'J':
            vga_erase_display(csi_param(0, 0));
            break;
        case 'K':
            vga_erase_line(csi_param(0, 0));
            break;
        case 'L':
            vga_insert_lines(csi_param(0, 1));
            break;
        case 'M':
            vga_delete_lines(csi_param(0, 1));
            break;
        case 'P':
            vga_delete_chars(csi_param(0, 1));
            break;
        case '@':
            vga_insert_chars(csi_param(0, 1));
            break;
        case 'X':
            vga_erase_chars(csi_param(0, 1));
            break;
        case 'S': {
            int n = csi_param(0, 1);
            for (int i = 0; i < n; i++)
                vga_scroll_region_up(0, VGA_HEIGHT - 1, 1);
            break;
        }
        case 'T': {
            int n = csi_param(0, 1);
            for (int i = 0; i < n; i++)
                vga_scroll_region_down(0, VGA_HEIGHT - 1, 1);
            break;
        }
        case 'Z': {
            int n = csi_param(0, 1);
            cursor_col = clamp(cursor_col - 8 * n, 0, VGA_WIDTH - 1);
            vga_update_hardware_cursor();
            break;
        }
        case 'm':
            vga_apply_sgr();
            break;
        case 'h':
        case 'l': {
            int set = (final == 'h');
            if (p.private_marker) {
                if (p.n_params == 0) {
                    vga_set_private_mode(0, set);
                } else {
                    for (int i = 0; i < p.n_params; i++) {
                        vga_set_private_mode(p.params[i], set);
                    }
                }
            } else {
                vga_trace_csi("(non-private mode)");
            }
            break;
        }
        case 'n': {
            int v = csi_param(0, 0);
            if (v == 6) vga_serial_reply_cursor();
            else        vga_trace_csi("(DSR)");
            break;
        }
        case 'c':
            vga_serial_reply_da();
            break;
        default:
            vga_trace_csi("(unknown final)");
            break;
    }
}

/* ------------------------------------------------------------------ */
/* ANSI parser                                                         */
/* ------------------------------------------------------------------ */

static void ansi_reset(void) {
    p.state = ANSI_NORMAL;
    p.n_intermediates = 0;
    p.n_params = 0;
    p.cur_param = 0;
    p.have_cur_param = 0;
    p.private_marker = 0;
}

static void ansi_push_param(void) {
    if (p.n_params < MAX_CSI_PARAMS) {
        p.params[p.n_params++] = p.have_cur_param ? p.cur_param : 0;
    }
    p.cur_param = 0;
    p.have_cur_param = 0;
}

static void vga_putc_unlocked(char c) {
    switch (p.state) {
    case ANSI_NORMAL:
        if (c == 0x1B) {
            p.state = ANSI_ESC;
            return;
        }
        vga_putc_raw(c);
        return;

    case ANSI_ESC:
        if (c == '[') {
            p.state = ANSI_CSI;
            p.n_params = 0;
            p.n_intermediates = 0;
            p.cur_param = 0;
            p.have_cur_param = 0;
            p.private_marker = 0;
            return;
        }
        if (c == '(' || c == ')' || c == '*' || c == '+') {
            p.state = ANSI_ESC_CHARSET;
            return;
        }
        if (c == '=' || c == '>' || c == '7' || c == '8' ||
            c == 'D' || c == 'E' || c == 'M' || c == 'c') {
            ansi_reset();
            return;
        }
        /* Unknown ESC x: drop ESC, re-feed x as ordinary input. */
        ansi_reset();
        vga_putc_raw(c);
        return;

    case ANSI_ESC_CHARSET:
        /* Swallow the charset designator byte. */
        ansi_reset();
        return;

    case ANSI_CSI:
        if (c >= '0' && c <= '9') {
            p.cur_param = p.cur_param * 10 + (c - '0');
            if (p.cur_param > 9999) p.cur_param = 9999;
            p.have_cur_param = 1;
            return;
        }
        if (c == ';') {
            ansi_push_param();
            return;
        }
        if (c == '?') {
            /* '?' is the private-marker prefix (e.g. ESC[?1049h).
             * It is NOT an intermediate byte; record it only in
             * private_marker, not in intermediates[]. If it were
             * pushed into intermediates[], csi_dispatch would bail
             * out on the n_intermediates > 0 check and never reach
             * the private-mode handler. */
            p.private_marker = 1;
            return;
        }
        if (c == '>' || c == '<' || c == '=') {
            if (p.n_intermediates < MAX_CSI_INTERMEDIATES) {
                p.intermediates[p.n_intermediates++] = c;
            }
            return;
        }
        if (c >= 0x20 && c <= 0x2F) {
            if (p.n_intermediates < MAX_CSI_INTERMEDIATES) {
                p.intermediates[p.n_intermediates++] = c;
            }
            return;
        }
        if (c >= 0x40 && c <= 0x7E) {
            if (p.have_cur_param) ansi_push_param();
            csi_dispatch((uint8_t)c);
            ansi_reset();
            return;
        }
        if (c == '\n' || c == '\r' || c == '\b' || c == '\t') {
            ansi_reset();
            vga_putc_raw(c);
            return;
        }
        if (c == 0x1B) {
            ansi_reset();
            p.state = ANSI_ESC;
            return;
        }
        /* Any other control byte: swallow. */
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

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
    cursor_visible = 0;
    vga_update_hardware_cursor();
    serial_unlock();
}

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
    for (int i = 0; i < 16; i++) {
        buf[15 - i] = hex[(val >> (i * 4)) & 0xF];
    }

    serial_lock();
    int pos = cursor_row * VGA_WIDTH + cursor_col;
    uint16_t attr = (uint16_t)cursor_attr << 8;
    for (int i = 0; i < 16; i++) {
        if (pos + i < VGA_WIDTH * VGA_HEIGHT) {
            active_screen[pos + i] = attr | (uint8_t)buf[i];
        }
    }
    cursor_col += 16;
    if (cursor_col >= VGA_WIDTH) {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= VGA_HEIGHT) {
            vga_scroll();
            cursor_row = VGA_HEIGHT - 1;
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
    serial_lock();
    uint16_t blank = blank_cell();
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        active_screen[i] = blank;
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
