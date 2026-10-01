#pragma once
#include <stdint.h>
#include <stddef.h>   // For size_t

#ifndef VGA_H
#define VGA_H

void vga_clear(void);
void vga_putc(char c);

void vga_print(const char *s);
void vga_print_at(int row, int col, const char *s);

/* Current console grid size, in character cells.  Reports the
 * framebuffer grid (102x42 at 10x18 in 1024x768) when the framebuffer
 * is active, or 80x25 in VGA text mode.  Used by the TIOCGWINSZ
 * ioctl so full-screen programs (vi) size themselves to the real
 * console. */
int vga_rows(void);
int vga_cols(void);
void vga_set_cursor(int row, int col);
void vga_set_cursor_shape(uint8_t start_scanline, uint8_t end_scanline);
void vga_hide_cursor(void);
/*
 * Advance the software cursor blink.  Called from the PIT ISR on
 * every tick.  No-op on the VGA text backend (the CRTC blinks the
 * cursor itself).  Must not take any lock -- see the definition in
 * vga.c for why.
 */
void vga_cursor_tick(void);
void vga_print_hex_cur(uint64_t val);
void vga_print_dec_cur(uint64_t val);

void vga_print_color(const char *s, uint8_t color);

void vga_write(const char* data, size_t count);

#endif
