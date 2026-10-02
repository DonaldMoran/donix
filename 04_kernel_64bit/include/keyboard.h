#pragma once
#include <stdint.h>

#ifndef KEYBOARD_H
#define KEYBOARD_H

void keyboard_init(void);
void keyboard_isr(void);

int  kbd_buffer_put(char c);
int  kbd_buffer_get(char *c);

/*
 * Non-destructive readability check.  Returns 1 if a call to
 * kbd_buffer_get would succeed right now, 0 otherwise.  Does not
 * move head or tail.
 *
 * Used by sys_poll (syscall 7).  See the comment there for why
 * poll needs this and why it must not consume a byte.
 */
int  kbd_buffer_has_data(void);

// Expose our new flush primitive tool to your kernel loader files
void keyboard_buffer_flush(void);

char scancode_to_ascii(uint8_t sc, int shift, int caps, int ctrl);

#endif
