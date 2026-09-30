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
