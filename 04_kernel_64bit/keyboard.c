#include <stdint.h>
#include "include/keyboard.h"

#define KBD_BUFFER_SIZE 128

static char kbd_buffer[KBD_BUFFER_SIZE];
static int  kbd_head = 0;
static int  kbd_tail = 0;

// Full US keyboard scancode → ASCII tables (set 1)
static char scancode_ascii[128];
static char scancode_shift[128];

void keyboard_init(void) {
    // Reset buffer
    kbd_head = 0;
    kbd_tail = 0;

    // Zero tables
    for (int i = 0; i < 128; i++) {
        scancode_ascii[i] = 0;
        scancode_shift[i] = 0;
    }

    // Unshifted ASCII
    scancode_ascii[0x02] = '1';
    scancode_ascii[0x03] = '2';
    scancode_ascii[0x04] = '3';
    scancode_ascii[0x05] = '4';
    scancode_ascii[0x06] = '5';
    scancode_ascii[0x07] = '6';
    scancode_ascii[0x08] = '7';
    scancode_ascii[0x09] = '8';
    scancode_ascii[0x0A] = '9';
    scancode_ascii[0x0B] = '0';
    scancode_ascii[0x0C] = '-';
    scancode_ascii[0x0D] = '=';
    scancode_ascii[0x0E] = '\b';
    scancode_ascii[0x0F] = '\t';

    scancode_ascii[0x10] = 'q';
    scancode_ascii[0x11] = 'w';
    scancode_ascii[0x12] = 'e';
    scancode_ascii[0x13] = 'r';
    scancode_ascii[0x14] = 't';
    scancode_ascii[0x15] = 'y';
    scancode_ascii[0x16] = 'u';
    scancode_ascii[0x17] = 'i';
    scancode_ascii[0x18] = 'o';
    scancode_ascii[0x19] = 'p';
    scancode_ascii[0x1A] = '[';
    scancode_ascii[0x1B] = ']';
    scancode_ascii[0x1C] = '\n';

    scancode_ascii[0x1E] = 'a';
    scancode_ascii[0x1F] = 's';
    scancode_ascii[0x20] = 'd';
    scancode_ascii[0x21] = 'f';
    scancode_ascii[0x22] = 'g';
    scancode_ascii[0x23] = 'h';
    scancode_ascii[0x24] = 'j';
    scancode_ascii[0x25] = 'k';
    scancode_ascii[0x26] = 'l';
    scancode_ascii[0x27] = ';';
    scancode_ascii[0x28] = '\'';
    scancode_ascii[0x29] = '`';

    scancode_ascii[0x2C] = 'z';
    scancode_ascii[0x2D] = 'x';
    scancode_ascii[0x2E] = 'c';
    scancode_ascii[0x2F] = 'v';
    scancode_ascii[0x30] = 'b';
    scancode_ascii[0x31] = 'n';
    scancode_ascii[0x32] = 'm';
    scancode_ascii[0x33] = ',';
    scancode_ascii[0x34] = '.';
    scancode_ascii[0x35] = '/';
    scancode_ascii[0x39] = ' ';

    // Shifted ASCII
    scancode_shift[0x02] = '!';
    scancode_shift[0x03] = '@';
    scancode_shift[0x04] = '#';
    scancode_shift[0x05] = '$';
    scancode_shift[0x06] = '%';
    scancode_shift[0x07] = '^';
    scancode_shift[0x08] = '&';
    scancode_shift[0x09] = '*';
    scancode_shift[0x0A] = '(';
    scancode_shift[0x0B] = ')';
    scancode_shift[0x0C] = '_';
    scancode_shift[0x0D] = '+';

    scancode_shift[0x10] = 'Q';
    scancode_shift[0x11] = 'W';
    scancode_shift[0x12] = 'E';
    scancode_shift[0x13] = 'R';
    scancode_shift[0x14] = 'T';
    scancode_shift[0x15] = 'Y';
    scancode_shift[0x16] = 'U';
    scancode_shift[0x17] = 'I';
    scancode_shift[0x18] = 'O';
    scancode_shift[0x19] = 'P';
    scancode_shift[0x1A] = '{';
    scancode_shift[0x1B] = '}';

    scancode_shift[0x1E] = 'A';
    scancode_shift[0x1F] = 'S';
    scancode_shift[0x20] = 'D';
    scancode_shift[0x21] = 'F';
    scancode_shift[0x22] = 'G';
    scancode_shift[0x23] = 'H';
    scancode_shift[0x24] = 'J';
    scancode_shift[0x25] = 'K';
    scancode_shift[0x26] = 'L';
    scancode_shift[0x27] = ':';
    scancode_shift[0x28] = '"';
    scancode_shift[0x29] = '~';

    scancode_shift[0x2C] = 'Z';
    scancode_shift[0x2D] = 'X';
    scancode_shift[0x2E] = 'C';
    scancode_shift[0x2F] = 'V';
    scancode_shift[0x30] = 'B';
    scancode_shift[0x31] = 'N';
    scancode_shift[0x32] = 'M';
    scancode_shift[0x33] = '<';
    scancode_shift[0x34] = '>';
    scancode_shift[0x35] = '?';
    scancode_shift[0x39] = ' ';
}

int kbd_buffer_put(char c) {
    // FIX 1: Explicitly prevent hardware null break-bytes from contaminating the buffer ring!
    if (c == '\0') {
        return 0;
    }
    
    int next = (kbd_head + 1) % KBD_BUFFER_SIZE;
    if (next == kbd_tail)
        return 0; // buffer full
    kbd_buffer[kbd_head] = c;
    kbd_head = next;
    return 1;
}

int kbd_buffer_get(char *c) {
    if (kbd_head == kbd_tail)
        return 0; // empty
    *c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
    return 1;
}

/*
 * Non-destructive readability check.
 *
 * Returns 1 iff kbd_buffer_get would succeed on the next call,
 * 0 otherwise.  This is the exact predicate sys_poll needs: it
 * must answer "would a read return data?" without consuming the
 * byte, because the reader (ash's line editor) is about to call
 * read(0) itself and expects to see that byte.
 *
 * The head/tail comparison is the same one kbd_buffer_get uses,
 * so the two cannot disagree in the absence of a concurrent
 * writer.  There IS a concurrent writer: irq1_handler runs
 * asynchronously and can call kbd_buffer_put between the
 * sys_poll check and the subsequent read.  That interleaving is
 * safe in both directions:
 *
 *   - If has_data() returns 0 and a byte arrives before read,
 *     read sees it and returns it.  poll's "not ready" answer
 *     was stale but the reader never lost a byte.
 *   - If has_data() returns 1 and the byte is still there at
 *     read, read returns it.  Nothing else consumes it: there
 *     is exactly one reader (the current process).
 *
 * So no cli/sti is needed here.  If a future change makes
 * kbd_buffer_get consume more than one byte, or adds a second
 * reader, revisit this.
 */
int kbd_buffer_has_data(void) {
    return kbd_head != kbd_tail;
}

// FIX 2: Added a high-utility queue flush helper to clear outstanding trailing data arrays
void keyboard_buffer_flush(void) {
    __asm__ volatile("cli" ::: "memory");
    kbd_head = 0;
    kbd_tail = 0;
    __asm__ volatile("sti" ::: "memory");
}

char scancode_to_ascii(uint8_t sc, int shift, int caps) {
    // Ignore break codes
    if (sc & 0x80)
        return 0;

    uint8_t code = sc & 0x7F;

    char ch = shift ? scancode_shift[code] : scancode_ascii[code];
    if (!ch)
        return 0;

    // Caps Lock only affects letters when shift is NOT active
    if (caps && !shift) {
        if (ch >= 'a' && ch <= 'z')
            ch = ch - 'a' + 'A';
    }

    return ch;
}
