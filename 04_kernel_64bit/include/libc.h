#ifndef LIBC_H
#define LIBC_H

#include <stddef.h>

// string functions
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);

// memory functions
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);

// simple I/O built on sys_write
int putchar(int c);
int puts(const char *s);

#endif
