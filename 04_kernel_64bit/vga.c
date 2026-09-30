#include <stdint.h>
#include <stddef.h>
#include "include/vga.h"
#include "include/serial.h"
#include "include/fb.h"

/* ------------------------------------------------------------------ */
/* Tunables                                                            */
/* ------------------------------------------------------------------ */

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

/* The VGA text framebuffer. VGA text mode has no second hardware
 * buffer; the CRTC scans this address unconditionally. Any "alternate
 * screen" is therefore simulated by saving/restoring this region. */
#define VGA_MAIN_MEM ((uint16_t *)0xB8000)

#define RESERVED_ROWS 0

/* Default cell attribute when SGR 0 is issued. Yellow on blue matches
 * the console's historical look; flip to 0x07 for gray-on-black. */
#define VGA_DEFAULT_ATTR 0x1E

/* Set to 1 to emit a one-line diagnostic on the serial port for any
 * CSI sequence the parser recognizes but does not dispatch. */
#define VGA_TRACE_UNHANDLED 0

/* Set to 1 to reply to DSR (ESC[6n) and DA (ESC[c) queries on the
 * serial port. */
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
/* Color palette (VGA 16-color -> RGB)                                 */
/* ------------------------------------------------------------------ */

/*
 * The standard IBM CGA/EGA 16-color palette.  A VGA attribute byte is
 * (background << 4) | foreground; the low 4 bits index the foreground,
 * the high 4 bits the background.  These RGB values are what the
 * framebuffer console draws; the VGA backend keeps using the raw
 * attribute byte and ignores this table.
 *
 * Index 6 is "brown" (0xAA,0x55,0x00), the traditional non-linear
 * value.  Some systems use 0xAA,0xAA,0x00 (a dark yellow); either is
 * defensible, brown is what VGA text hardware actually showed.
 */
static const uint8_t vga_palette[16][3] = {
    { 0x00, 0x00, 0x00 },  /*  0 black        */
    { 0x00, 0x00, 0xAA },  /*  1 blue         */
    { 0x00, 0xAA, 0x00 },  /*  2 green        */
    { 0x00, 0xAA, 0xAA },  /*  3 cyan         */
    { 0xAA, 0x00, 0x00 },  /*  4 red          */
    { 0xAA, 0x00, 0xAA },  /*  5 magenta      */
    { 0xAA, 0x55, 0x00 },  /*  6 brown        */
    { 0xAA, 0xAA, 0xAA },  /*  7 light gray   */
    { 0x55, 0x55, 0x55 },  /*  8 dark gray    */
    { 0x55, 0x55, 0xFF },  /*  9 light blue   */
    { 0x55, 0xFF, 0x55 },  /* 10 light green  */
    { 0x55, 0xFF, 0xFF },  /* 11 light cyan   */
    { 0xFF, 0x55, 0x55 },  /* 12 light red    */
    { 0xFF, 0x55, 0xFF },  /* 13 light magenta*/
    { 0xFF, 0xFF, 0x55 },  /* 14 yellow       */
    { 0xFF, 0xFF, 0xFF },  /* 15 white        */
};

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

/* Always points at VGA_MAIN_MEM. Used only by the VGA fallback draw
 * path. */
static uint16_t *active_screen = VGA_MAIN_MEM;

/*
 * Compile-time maxima for the shadow grid.  The live grid is
 * con_cols() x con_rows(); these are the largest values those return,
 * so the shadow is big enough for either backend.
 *   102 = 1024 / 10   (framebuffer width / FB_FONT_W)
 *    42 =  768 / 18   (framebuffer height / FB_FONT_H)
 */
#define VGA_MAX_COLS 102
#define VGA_MAX_ROWS 42

/*
 * Shadow cell grid -- the single source of truth for what is on the
 * screen, on BOTH backends.
 *
 * On VGA text, 0xB8000 is a readable cell buffer, but the framebuffer
 * is write-only pixels.  Rather than special-case every read-back
 * operation (scroll, insert/delete chars, alt-screen save/restore),
 * we keep this array as the authoritative screen model and paint the
 * visible output from it.  Cell layout is (attr << 8) | ch, the same
 * as a VGA cell.
 */
static uint16_t shadow[VGA_MAX_ROWS * VGA_MAX_COLS];

/* Storage for the saved screen while ?1049h is active.  On commit 2
 * this holds a real saved screen on both backends, because it is
 * copied from the shadow (see the alt-screen functions). */
static uint16_t alt_screen[VGA_MAX_ROWS * VGA_MAX_COLS];

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
/* Console geometry: backend-dependent                                 */
/* ------------------------------------------------------------------ */

/*
 * The console grid is 80x25 on VGA text and (framebuffer_width /
 * FB_FONT_W) x (framebuffer_height / FB_FONT_H) on the framebuffer --
 * 102x42 at 10x18 in 1024x768.  Everywhere the drawing code used the
 * VGA_WIDTH / VGA_HEIGHT constants it now calls these, so the same
 * VT100 logic drives either grid.
 *
 * VGA_WIDTH and VGA_HEIGHT are still used, but only for: the
 * alt_screen[] array size (always a VGA-sized cell buffer) and the
 * VGA fallback draw path.  The *live* grid size is con_cols()/
 * con_rows().
 */
static int con_cols(void) {
    const fb_info_t* fb = fb_get_info();
    if (fb->available) return (int)(fb->width / 10);   /* FB_FONT_W */
    return VGA_WIDTH;
}

static int con_rows(void) {
    const fb_info_t* fb = fb_get_info();
    if (fb->available) return (int)(fb->height / 18);  /* FB_FONT_H */
    return VGA_HEIGHT;
}

/* ------------------------------------------------------------------ */
/* The one cell-draw chokepoint                                        */
/* ------------------------------------------------------------------ */

/*
 * Draw one cell.  This is the ONLY place that turns a (ch, attr) pair
 * into pixels or into a VGA cell; every other drawing routine in this
 * file calls it.  When the framebuffer is available the glyph is
 * blitted with fb_putchar, with foreground and background taken from
 * the VGA palette; otherwise the cell is written to VGA memory.
 *
 * Out-of-range (row, col) is silently ignored: the parser clamps
 * cursor positions to con_rows()/con_cols(), but the bulk operations
 * (scroll, erase) occasionally run a column or row past the edge and
 * rely on this being harmless.
 */
/* Read a cell from the shadow.  Out-of-range reads return 0. */
static uint16_t shadow_get(int row, int col) {
    if (row < 0 || col < 0) return 0;
    if (row >= VGA_MAX_ROWS || col >= VGA_MAX_COLS) return 0;
    return shadow[row * VGA_MAX_COLS + col];
}

/* Write a cell to the shadow only (no visible output).  Used by the
 * scroll/insert/delete routines, which move shadow cells and then
 * repaint the affected region. */
static void shadow_set(int row, int col, uint16_t cell) {
    if (row < 0 || col < 0) return;
    if (row >= VGA_MAX_ROWS || col >= VGA_MAX_COLS) return;
    shadow[row * VGA_MAX_COLS + col] = cell;
}

/* Paint one already-known cell to the visible backend.  This is the
 * output half of con_put_cell, split out so scroll/insert/delete can
 * move shadow cells and then repaint without rewriting the shadow. */
static void paint_cell(int row, int col, uint16_t cell) {
    uint8_t ch   = (uint8_t)(cell & 0xFF);
    uint8_t attr = (uint8_t)(cell >> 8);
    const fb_info_t* fb = fb_get_info();

    if (fb->available) {
        if (row < 0 || col < 0) return;
        if (row >= (int)(fb->height / 18)) return;
        if (col >= (int)(fb->width / 10)) return;

        const uint8_t* fg = vga_palette[attr & 0x0F];
        const uint8_t* bg = vga_palette[(attr >> 4) & 0x0F];

        fb_putchar(ch,
                   (uint32_t)col * 10, (uint32_t)row * 18,
                   fg[0], fg[1], fg[2],
                   bg[0], bg[1], bg[2]);
        return;
    }

    if (row < 0 || col < 0) return;
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH) return;
    active_screen[row * VGA_WIDTH + col] = cell;
}

/*
 * Draw one cell: update the shadow AND the visible output.  Every
 * drawing routine calls this (or shadow_set + paint_cell, when it is
 * moving many cells and wants to repaint once).
 */
static void con_put_cell(int row, int col, uint8_t ch, uint8_t attr) {
    uint16_t cell = ((uint16_t)attr << 8) | ch;
    shadow_set(row, col, cell);
    paint_cell(row, col, cell);
}

static void con_fill_cells(int row, int col, int count, uint8_t ch, uint8_t attr) {
    for (int i = 0; i < count; i++) {
        con_put_cell(row, col + i, ch, attr);
    }
}

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
    /* The VGA hardware cursor only exists in text mode.  When the
     * framebuffer is active there is no CRTC text cursor to move;
     * the visible cursor is drawn by the framebuffer backend (a
     * later change).  Skip the port writes entirely then, so we do
     * not poke CRTC registers while in a graphics mode. */
    const fb_info_t* fb = fb_get_info();
    if (fb->available) return;

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
/* Erase / scroll primitives                                           */
/* ------------------------------------------------------------------ */

static void vga_scroll(void) {
    const int cols = con_cols();
    const int rows = con_rows();

    /* Shift the shadow up one row, then repaint the whole grid. */
    for (int row = 0; row < rows - 1; row++) {
        for (int col = 0; col < cols; col++) {
            shadow_set(row, col, shadow_get(row + 1, col));
        }
    }
    for (int col = 0; col < cols; col++) {
        shadow_set(rows - 1, col, blank_cell());
    }
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            paint_cell(row, col, shadow_get(row, col));
        }
    }

    cursor_row = rows - 1;
    cursor_col = 0;
    vga_update_hardware_cursor();
}

static void vga_scroll_region_up(int top, int bottom, int n) {
    const int cols = con_cols();
    const int rows = con_rows();
    if (top < RESERVED_ROWS) top = RESERVED_ROWS;
    if (bottom > rows - 1) bottom = rows - 1;
    if (n <= 0 || top > bottom) return;
    int height = bottom - top + 1;
    if (n > height) n = height;

    for (int row = top; row <= bottom - n; row++) {
        for (int col = 0; col < cols; col++) {
            shadow_set(row, col, shadow_get(row + n, col));
        }
    }
    for (int row = bottom - n + 1; row <= bottom; row++) {
        for (int col = 0; col < cols; col++) {
            shadow_set(row, col, blank_cell());
        }
    }
    for (int row = top; row <= bottom; row++) {
        for (int col = 0; col < cols; col++) {
            paint_cell(row, col, shadow_get(row, col));
        }
    }
}

static void vga_scroll_region_down(int top, int bottom, int n) {
    const int cols = con_cols();
    const int rows = con_rows();
    if (top < RESERVED_ROWS) top = RESERVED_ROWS;
    if (bottom > rows - 1) bottom = rows - 1;
    if (n <= 0 || top > bottom) return;
    int height = bottom - top + 1;
    if (n > height) n = height;

    for (int row = bottom; row >= top + n; row--) {
        for (int col = 0; col < cols; col++) {
            shadow_set(row, col, shadow_get(row - n, col));
        }
    }
    for (int row = top; row < top + n; row++) {
        for (int col = 0; col < cols; col++) {
            shadow_set(row, col, blank_cell());
        }
    }
    for (int row = top; row <= bottom; row++) {
        for (int col = 0; col < cols; col++) {
            paint_cell(row, col, shadow_get(row, col));
        }
    }
}

/* ESC[K family. mode: 0=to end of line, 1=to start, 2=whole line. */
static void vga_erase_line(int mode) {
    const int cols = con_cols();
    int from, to;
    switch (mode) {
        case 1: from = 0;            to = cursor_col;    break;
        case 2: from = 0;            to = cols - 1;      break;
        default: from = cursor_col;  to = cols - 1;      break;
    }
    if (to >= cols) to = cols - 1;
    if (from < 0) from = 0;
    con_fill_cells(cursor_row, from, to - from + 1, ' ', cursor_attr);
    vga_update_hardware_cursor();
}

/* ESC[J family. mode: 0=below, 1=above, 2=all. */
static void vga_erase_display(int mode) {
    const int cols = con_cols();
    const int rows = con_rows();

    if (mode == 2) {
        for (int r = 0; r < rows; r++)
            con_fill_cells(r, 0, cols, ' ', cursor_attr);
        vga_update_hardware_cursor();
        return;
    }

    if (mode == 1) {
        for (int r = 0; r < cursor_row; r++)
            con_fill_cells(r, 0, cols, ' ', cursor_attr);
        con_fill_cells(cursor_row, 0, cursor_col + 1, ' ', cursor_attr);
    } else {
        con_fill_cells(cursor_row, cursor_col, cols - cursor_col,
                       ' ', cursor_attr);
        for (int r = cursor_row + 1; r < rows; r++)
            con_fill_cells(r, 0, cols, ' ', cursor_attr);
    }
    vga_update_hardware_cursor();
}

static void vga_delete_chars(int n) {
    const int cols = con_cols();
    if (n < 1) n = 1;
    if (n > cols - cursor_col) n = cols - cursor_col;

    for (int c = cursor_col; c + n < cols; c++) {
        shadow_set(cursor_row, c, shadow_get(cursor_row, c + n));
    }
    for (int c = cols - n; c < cols; c++) {
        shadow_set(cursor_row, c, blank_cell());
    }
    for (int c = cursor_col; c < cols; c++) {
        paint_cell(cursor_row, c, shadow_get(cursor_row, c));
    }
    vga_update_hardware_cursor();
}

static void vga_insert_chars(int n) {
    const int cols = con_cols();
    if (n < 1) n = 1;
    if (n > cols - cursor_col) n = cols - cursor_col;

    for (int c = cols - 1; c - n >= cursor_col; c--) {
        shadow_set(cursor_row, c, shadow_get(cursor_row, c - n));
    }
    for (int c = cursor_col; c < cursor_col + n; c++) {
        shadow_set(cursor_row, c, blank_cell());
    }
    for (int c = cursor_col; c < cols; c++) {
        paint_cell(cursor_row, c, shadow_get(cursor_row, c));
    }
    vga_update_hardware_cursor();
}

static void vga_erase_chars(int n) {
    const int cols = con_cols();
    if (n < 1) n = 1;
    if (n > cols - cursor_col) n = cols - cursor_col;
    con_fill_cells(cursor_row, cursor_col, n, ' ', cursor_attr);
    vga_update_hardware_cursor();
}

static void vga_insert_lines(int n) {
    const int rows = con_rows();
    vga_scroll_region_down(cursor_row, rows - 1, n);
    cursor_col = 0;
    vga_update_hardware_cursor();
}

static void vga_delete_lines(int n) {
    const int rows = con_rows();
    vga_scroll_region_up(cursor_row, rows - 1, n);
    cursor_col = 0;
    vga_update_hardware_cursor();
}

/* ------------------------------------------------------------------ */
/* Raw character output                                                */
/* ------------------------------------------------------------------ */

static void vga_putc_raw(char c) {
    const int cols = con_cols();
    const int rows = con_rows();

    if (c == '\n') {
        cursor_row++;
        cursor_col = 0;
        if (cursor_row >= rows) {
            vga_scroll();
            cursor_row = rows - 1;
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
            con_put_cell(cursor_row, cursor_col, ' ', cursor_attr);
        }
        vga_update_hardware_cursor();
        return;
    }
    if (c == '\t') {
        int next = (cursor_col + 8) & ~7;
        if (next > cols - 1) next = cols - 1;
        cursor_col = next;
        vga_update_hardware_cursor();
        return;
    }
    if (c == 0x07) {
        return;   /* BEL: no speaker; ignore */
    }
    if (c >= ' ' && c <= '~') {
        con_put_cell(cursor_row, cursor_col, (uint8_t)c, cursor_attr);
        cursor_col++;
        if (cursor_col >= cols) {
            cursor_col = 0;
            cursor_row++;
            if (cursor_row >= rows) {
                vga_scroll();
                cursor_row = rows - 1;
            }
        }
        vga_update_hardware_cursor();
    }
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
            case 0:  cursor_attr = VGA_DEFAULT_ATTR; break;
            case 1:  cursor_attr |= 0x08;            break;
            case 7:  cursor_attr = sgr_reverse(cursor_attr); break;
            case 22: cursor_attr &= (uint8_t)~0x08;  break;
            case 27: cursor_attr = VGA_DEFAULT_ATTR; break;
            case 39: cursor_attr = (uint8_t)((cursor_attr & 0xF0) | 0x07); break;
            case 49: cursor_attr = (uint8_t)((cursor_attr & 0x0F) | 0x00); break;
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

    /*
     * Save the whole shadow (the screen) into alt_screen[], then
     * optionally clear.  This works identically on both backends now,
     * because the shadow is the screen model -- there is no separate
     * "read 0xB8000" step, which is what made the framebuffer path
     * restore blank before.
     */
    const int cols = con_cols();
    const int rows = con_rows();

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < VGA_MAX_COLS; c++) {
            alt_screen[r * VGA_MAX_COLS + c] = shadow_get(r, c);
        }
    }

    if (clear) {
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                con_put_cell(r, c, ' ', VGA_DEFAULT_ATTR);
            }
        }
    }

    alt_screen_active = 1;
    cursor_row = 0;
    cursor_col = 0;
    vga_update_hardware_cursor();
}

static void vga_alt_screen_leave(void) {
    if (!alt_screen_active) return;

    /*
     * Restore: write the saved cells back into the shadow and
     * repaint.  This gives back the pre-alt-screen contents on both
     * backends -- the fix for vi exit leaving the screen black.
     */
    const int cols = con_cols();
    const int rows = con_rows();

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < VGA_MAX_COLS; c++) {
            shadow_set(r, c, alt_screen[r * VGA_MAX_COLS + c]);
        }
    }
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            paint_cell(r, c, shadow_get(r, c));
        }
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
            break;   /* DECCKM: accepted, not acted on */
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
    const int cols = con_cols();
    const int rows = con_rows();

    if (p.n_intermediates > 0) {
        vga_trace_csi("(with intermediates)");
        return;
    }

    switch (final) {
        case 'A': {
            int n = csi_param(0, 1);
            cursor_row = clamp(cursor_row - n, RESERVED_ROWS, rows - 1);
            vga_update_hardware_cursor();
            break;
        }
        case 'B': {
            int n = csi_param(0, 1);
            cursor_row = clamp(cursor_row + n, RESERVED_ROWS, rows - 1);
            vga_update_hardware_cursor();
            break;
        }
        case 'C': {
            int n = csi_param(0, 1);
            cursor_col = clamp(cursor_col + n, 0, cols - 1);
            vga_update_hardware_cursor();
            break;
        }
        case 'D': {
            int n = csi_param(0, 1);
            cursor_col = clamp(cursor_col - n, 0, cols - 1);
            vga_update_hardware_cursor();
            break;
        }
        case 'E': {
            int n = csi_param(0, 1);
            cursor_row = clamp(cursor_row + n, RESERVED_ROWS, rows - 1);
            cursor_col = 0;
            vga_update_hardware_cursor();
            break;
        }
        case 'F': {
            int n = csi_param(0, 1);
            cursor_row = clamp(cursor_row - n, RESERVED_ROWS, rows - 1);
            cursor_col = 0;
            vga_update_hardware_cursor();
            break;
        }
        case 'G': {
            int c = csi_param(0, 1) - 1;
            cursor_col = clamp(c, 0, cols - 1);
            vga_update_hardware_cursor();
            break;
        }
        case 'H':
        case 'f': {
            int r = csi_param(0, 1) - 1;
            int c = csi_param(1, 1) - 1;
            cursor_row = clamp(r, RESERVED_ROWS, rows - 1);
            cursor_col = clamp(c, 0, cols - 1);
            vga_update_hardware_cursor();
            break;
        }
        case 'J': vga_erase_display(csi_param(0, 0)); break;
        case 'K': vga_erase_line(csi_param(0, 0));    break;
        case 'L': vga_insert_lines(csi_param(0, 1));  break;
        case 'M': vga_delete_lines(csi_param(0, 1));  break;
        case 'P': vga_delete_chars(csi_param(0, 1));  break;
        case '@': vga_insert_chars(csi_param(0, 1));  break;
        case 'X': vga_erase_chars(csi_param(0, 1));   break;
        case 'S': {
            int n = csi_param(0, 1);
            for (int i = 0; i < n; i++)
                vga_scroll_region_up(0, rows - 1, 1);
            break;
        }
        case 'T': {
            int n = csi_param(0, 1);
            for (int i = 0; i < n; i++)
                vga_scroll_region_down(0, rows - 1, 1);
            break;
        }
        case 'Z': {
            int n = csi_param(0, 1);
            cursor_col = clamp(cursor_col - 8 * n, 0, cols - 1);
            vga_update_hardware_cursor();
            break;
        }
        case 'm': vga_apply_sgr(); break;
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
        case 'c': vga_serial_reply_da(); break;
        default:  vga_trace_csi("(unknown final)"); break;
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
        if (c == 0x1B) { p.state = ANSI_ESC; return; }
        vga_putc_raw(c);
        return;

    case ANSI_ESC:
        if (c == '[') {
            p.state = ANSI_CSI;
            p.n_params = 0; p.n_intermediates = 0;
            p.cur_param = 0; p.have_cur_param = 0; p.private_marker = 0;
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
        ansi_reset();
        vga_putc_raw(c);
        return;

    case ANSI_ESC_CHARSET:
        ansi_reset();
        return;

    case ANSI_CSI:
        if (c >= '0' && c <= '9') {
            p.cur_param = p.cur_param * 10 + (c - '0');
            if (p.cur_param > 9999) p.cur_param = 9999;
            p.have_cur_param = 1;
            return;
        }
        if (c == ';') { ansi_push_param(); return; }
        if (c == '?') { p.private_marker = 1; return; }
        if (c == '>' || c == '<' || c == '=') {
            if (p.n_intermediates < MAX_CSI_INTERMEDIATES)
                p.intermediates[p.n_intermediates++] = c;
            return;
        }
        if (c >= 0x20 && c <= 0x2F) {
            if (p.n_intermediates < MAX_CSI_INTERMEDIATES)
                p.intermediates[p.n_intermediates++] = c;
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
        if (c == 0x1B) { ansi_reset(); p.state = ANSI_ESC; return; }
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
int vga_rows(void) { return con_rows(); }
int vga_cols(void) { return con_cols(); }

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
    const int rows = con_rows();
    const int cols = con_cols();

    row = clamp(row, RESERVED_ROWS, rows - 1);
    col = clamp(col, 0, cols - 1);
    cursor_row = row;
    cursor_col = col;
    vga_update_hardware_cursor();

    while (*s) {
        vga_putc_unlocked(*s++);
    }

    cursor_row = clamp(saved_row, RESERVED_ROWS, rows - 1);
    cursor_col = clamp(saved_col, 0, cols - 1);
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
    const int cols = con_cols();
    const int rows = con_rows();
    for (int i = 0; i < 16; i++) {
        con_put_cell(cursor_row, cursor_col, (uint8_t)buf[i], cursor_attr);
        cursor_col++;
        if (cursor_col >= cols) {
            cursor_col = 0;
            cursor_row++;
            if (cursor_row >= rows) {
                vga_scroll();
                cursor_row = rows - 1;
            }
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
    const int cols = con_cols();
    const int rows = con_rows();
    for (int r = 0; r < rows; r++) {
        con_fill_cells(r, 0, cols, ' ', cursor_attr);
    }
    cursor_row = 0;
    cursor_col = 0;
    vga_update_hardware_cursor();
    serial_unlock();
}

void vga_set_cursor(int row, int col) {
    serial_lock();
    const int rows = con_rows();
    const int cols = con_cols();
    cursor_row = clamp(row, RESERVED_ROWS, rows - 1);
    cursor_col = clamp(col, 0, cols - 1);
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
