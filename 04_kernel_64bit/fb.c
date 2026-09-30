#include "include/fb.h"
#include "include/vmm.h"
#include "include/serial.h"

/*
 * QEMU's Bochs VBE, and most VBE linear modes, store 24bpp and 32bpp
 * pixels in BGR order, not RGB: byte 0 is blue, byte 1 is green,
 * byte 2 is red.  Our API takes (r, g, b); these indices map a
 * requested color onto the byte order the hardware wants.
 *
 * Verified empirically: fb_fill(0,0,0x40) with RGB order came out
 * red; swapping R and B fixed it.  If donix ever targets hardware
 * that stores RGB, this is the one place to change.
 */
#define FB_BYTE_R 2
#define FB_BYTE_G 1
#define FB_BYTE_B 0

/*
 * The framebuffer lives at a high physical address (QEMU's VBE puts
 * it at 0xFD000000) that the boot page tables do not cover -- stage2
 * maps only the first ~1 GB.  fb_init maps the framebuffer's physical
 * range into the kernel's higher half so we can write to it.
 *
 * FB_VIRT_BASE is chosen well above the kernel image
 * (0xFFFFFFFF80100000) and the heap, in a region the kernel's page
 * tables leave unmapped.
 *
 * Flags: present + writable + NX.  A framebuffer is never executed;
 * EFER.NXE is enabled before this runs (kmain's enable_nx).
 */
#define FB_VIRT_BASE 0xFFFFFFFFA0000000ULL

static fb_info_t g_fb;

int fb_available(void) { return g_fb.available; }
const fb_info_t* fb_get_info(void) { return &g_fb; }

int fb_init(uint64_t phys_addr, uint32_t width, uint32_t height,
            uint32_t pitch, uint32_t bpp) {
    g_fb.available = 0;

    if (phys_addr == 0 || width == 0 || height == 0 || pitch == 0 || bpp == 0) {
        serial_print("FB: not available (BootInfo fields are zero)\n");
        return 0;
    }

    if (bpp != 24 && bpp != 32) {
        serial_print("FB: unsupported bpp ");
        serial_print_dec(bpp);
        serial_print("; framebuffer disabled\n");
        return 0;
    }

    uint32_t bytes_pp = bpp / 8;
    uint64_t fb_size  = (uint64_t)pitch * height;
    uint64_t pages    = (fb_size + 0xFFF) / 0x1000;

    uint64_t flags = PT_PRESENT | PT_WRITE | PT_NX;
    uint64_t active_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(active_cr3));

    for (uint64_t i = 0; i < pages; i++) {
        vmm_map_page_in_cr3(active_cr3,
                            FB_VIRT_BASE + i * 0x1000,
                            phys_addr   + i * 0x1000,
                            flags);
    }

    g_fb.addr      = phys_addr;
    g_fb.base      = (uint8_t*)FB_VIRT_BASE;
    g_fb.width     = width;
    g_fb.height    = height;
    g_fb.pitch     = pitch;
    g_fb.bpp       = bpp;
    g_fb.bytes_pp  = bytes_pp;
    g_fb.available = 1;

    serial_print("FB: mapped ");
    serial_print_dec(pages);
    serial_print(" pages at 0x");
    serial_print_hex(FB_VIRT_BASE);
    serial_print(" (phys 0x");
    serial_print_hex(phys_addr);
    serial_print(", ");
    serial_print_dec(width);
    serial_print("x");
    serial_print_dec(height);
    serial_print("x");
    serial_print_dec(bpp);
    serial_print(", pitch ");
    serial_print_dec(pitch);
    serial_print(")\n");

    return 1;
}

void fb_putpixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (!g_fb.available) return;
    if (x >= g_fb.width || y >= g_fb.height) return;

    uint8_t* p = g_fb.base + (uint64_t)y * g_fb.pitch
                          + (uint64_t)x * g_fb.bytes_pp;
    p[FB_BYTE_R] = r;
    p[FB_BYTE_G] = g;
    p[FB_BYTE_B] = b;
}

void fb_fill(uint8_t r, uint8_t g, uint8_t b) {
    if (!g_fb.available) return;

    for (uint32_t y = 0; y < g_fb.height; y++) {
        uint8_t* row = g_fb.base + (uint64_t)y * g_fb.pitch;
        for (uint32_t x = 0; x < g_fb.width; x++) {
            uint8_t* p = row + (uint64_t)x * g_fb.bytes_pp;
            p[FB_BYTE_R] = r;
            p[FB_BYTE_G] = g;
            p[FB_BYTE_B] = b;
        }
    }
}

void fb_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                 uint8_t r, uint8_t g, uint8_t b) {
    if (!g_fb.available) return;

    for (uint32_t yy = y; yy < y + h && yy < g_fb.height; yy++) {
        uint8_t* row = g_fb.base + (uint64_t)yy * g_fb.pitch;
        for (uint32_t xx = x; xx < x + w && xx < g_fb.width; xx++) {
            uint8_t* p = row + (uint64_t)xx * g_fb.bytes_pp;
            p[FB_BYTE_R] = r;
            p[FB_BYTE_G] = g;
            p[FB_BYTE_B] = b;
        }
    }
}

/* ============================================================
 * GLYPH BLITTER -- Terminus 10x18
 *
 * The embedded font is ter_u18n_data[], produced by `xxd -i` from
 * fonts/ter-u18n.psf and linked in via ter_u18n_data.o.  Layout
 * (decoded from the file header):
 *
 *   offset 0    magic 72 b5 4a 86   (PSF v2)
 *   offset 4    version
 *   offset 8    header size = 32
 *   offset 12   flags       = 1  (Unicode table present)
 *   offset 16   char count  = 256
 *   offset 20   charsize    = 36  (bytes per glyph)
 *   offset 24   height      = 18  (pixels)
 *   offset 28   width       = 10  (pixels)
 *   offset 32   glyph data, 256 glyphs of 36 bytes
 *
 * Each glyph is 18 rows of 2 bytes (little-endian).  The low 10
 * bits of a row are the 10 pixels, bit 0 = leftmost.  A set bit
 * is a foreground pixel.
 *
 * The Unicode table (signalled by flags bit 0) is present but is
 * the identity mapping 0..255 for this font, so glyph index ==
 * character code and the table can be ignored.  If a future font
 * has a non-identity table, this assumption breaks and the table
 * must be read.
 *
 * These dimensions are #defines, not reads from the header, so
 * the blitter is fast and the sizes are visible at the call site.
 * They MUST match the embedded .psf; swapping to a different
 * Terminus size means changing FB_FONT_* and the .psf the
 * Makefile embeds.
 * ============================================================ */

#define FB_FONT_HEADER  32
#define FB_FONT_W       10
#define FB_FONT_H       18
#define FB_FONT_CHARSIZE 36     /* bytes per glyph */
#define FB_FONT_ROWBYTES 2      /* bytes per glyph row */

extern const unsigned char fonts_ter_u18n_psf[];
extern const unsigned int  fonts_ter_u18n_psf_len;

/* Return a pointer to glyph `c`'s bitmap inside the embedded font,
 * or NULL if c is out of range. */
static const uint8_t* font_glyph(int c) {
    if (c < 0 || c > 255) return NULL;
    unsigned int need = FB_FONT_HEADER + (unsigned int)c * FB_FONT_CHARSIZE;
    if (need + FB_FONT_CHARSIZE > fonts_ter_u18n_psf_len) return NULL;
    return fonts_ter_u18n_psf + need;
}

/* Blit one character at pixel position (x, y) with the given
 * foreground and background colors.  (x, y) is the top-left of the
 * glyph cell.  Cells are FB_FONT_W x FB_FONT_H pixels. */
void fb_putchar(int c, uint32_t x, uint32_t y,
                uint8_t fr, uint8_t fg, uint8_t fb_,
                uint8_t br, uint8_t bg, uint8_t bb) {
    if (!g_fb.available) return;

    const uint8_t* g = font_glyph(c);
    if (!g) return;

    for (uint32_t row = 0; row < FB_FONT_H; row++) {
        /* Row is 2 little-endian bytes; assemble the 16-bit value
         * and use its low FB_FONT_W bits. */
        uint16_t bits = (uint16_t)g[row * FB_FONT_ROWBYTES]
                      | ((uint16_t)g[row * FB_FONT_ROWBYTES + 1] << 8);

        for (uint32_t col = 0; col < FB_FONT_W; col++) {
            uint8_t r, gg, b;
            if (bits & (1u << (FB_FONT_W - 1 - col))) {
                r = fr; gg = fg; b = fb_;
            } else {
                r = br; gg = bg; b = bb;
            }
            fb_putpixel(x + col, y + row, r, gg, b);
        }
    }
}

/* Draw a NUL-terminated string starting at pixel (x, y).  Advances
 * x by FB_FONT_W per character; does NOT wrap at the right edge
 * (a console layer above this handles wrapping).  Returns the x
 * position just past the last character drawn. */
uint32_t fb_puts(const char* s, uint32_t x, uint32_t y,
                 uint8_t fr, uint8_t fg, uint8_t fb_,
                 uint8_t br, uint8_t bg, uint8_t bb) {
    if (!g_fb.available) return x;
    while (*s) {
        fb_putchar((unsigned char)*s, x, y, fr, fg, fb_, br, bg, bb);
        x += FB_FONT_W;
        s++;
    }
    return x;
}
