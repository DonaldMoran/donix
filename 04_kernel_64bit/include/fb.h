#ifndef FB_H
#define FB_H

#include <stdint.h>

/*
 * Linear framebuffer console.
 *
 * Initialized from BootInfo's framebuffer fields, which stage2 fills
 * in with VBE mode-info values.  If framebuffer_addr is 0 (VBE failed
 * and stage2 fell back to VGA text), fb_init leaves the framebuffer
 * disabled and fb_available() returns 0.
 *
 * Bit depth: the mode we set (VBE 0x118 on QEMU) is 24bpp -- 3 bytes
 * per pixel, R,G,B in memory order, no alpha, pitch 3072 for 1024
 * pixels wide.  fb_putpixel packs 3 bytes and steps rows by `pitch`,
 * not by width*bytes_per_pixel, so it is correct for any pitch the
 * mode reports.
 */
typedef struct {
    uint64_t addr;      /* physical address of the framebuffer */
    uint8_t* base;      /* kernel virtual address (mapped from addr) */
    uint32_t width;     /* pixels */
    uint32_t height;    /* pixels */
    uint32_t pitch;     /* bytes per scanline */
    uint32_t bpp;       /* bits per pixel (24 for our mode) */
    uint32_t bytes_pp;  /* bytes per pixel (bpp/8) */
    int      available; /* 1 if the framebuffer is mapped and usable */
} fb_info_t;

/* Initialize from BootInfo.  Returns 1 if usable, 0 if not. */
int fb_init(uint64_t phys_addr, uint32_t width, uint32_t height,
            uint32_t pitch, uint32_t bpp);

/* 1 if fb_init succeeded, 0 otherwise. */
int fb_available(void);

/* Fill the whole screen with one color. */
void fb_fill(uint8_t r, uint8_t g, uint8_t b);

/* Set one pixel.  Bounds-checked. */
void fb_putpixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b);

/* Fill a rectangle. */
void fb_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                 uint8_t r, uint8_t g, uint8_t b);

/* Accessor for other modules (glyph blitter in step 3). */
const fb_info_t* fb_get_info(void);

#endif
