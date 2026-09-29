#include <stdint.h>
#include "include/keyboard.h"

#define KBD_BUFFER_SIZE 128

static char kbd_buffer[KBD_BUFFER_SIZE];
static int  kbd_head = 0;
static int  kbd_tail = 0;

/* Full US keyboard scancode → ASCII tables (set 1).
 *
 * Unassigned entries are left as 0. scancode_to_ascii returns 0 for
 * those, and callers must treat 0 as "no character produced" (not as
 * a NUL character). The break-code path in scancode_to_ascii uses the
 * same convention: it returns 0 for the key-release event, and callers
 * drop it. This is why scancode_ascii[0x01] MUST be assigned: if it
 * is left as 0, ESC is indistinguishable from "no key" and gets
 * silently dropped before it ever reaches the buffer.
 *
 * ASCII control codes that are intentional:
 *   0x08 BS   (rarely used; we emit 0x7F for Backspace instead)
 *   0x09 TAB
 *   0x0A LF   (output only; input uses CR, see 0x1C below)
 *   0x0D CR   (Enter; cooked-mode line discipline translates to LF)
 *   0x1B ESC  (the byte the terminal's escape parser keys on)
 *   0x7F DEL  (Backspace, as Unix software expects) */
static char scancode_ascii[128];
static char scancode_shift[128];

void keyboard_init(void) {
    /* Reset buffer */
    kbd_head = 0;
    kbd_tail = 0;

    /* Zero tables */
    for (int i = 0; i < 128; i++) {
        scancode_ascii[i] = 0;
        scancode_shift[i] = 0;
    }

    /* ---- Unshifted ASCII ---- */

    /* 0x01 = ESC. Without this, the ESC key produces 0 and is dropped
     * by kbd_buffer_put's NUL guard, and no full-screen program (vi,
     * less, curses, ...) can ever leave insert mode or enter a command
     * prefix. This one line is the difference between "vi doesn't
     * work" and "vi works". */
    scancode_ascii[0x01] = 0x1B;

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
    /* 0x0E = Backspace. Unix software (busybox vi/ash/less, ncurses)
     * expects DEL (0x7F), not BS (0x08). BS is Ctrl+H, a distinct
     * command in most editors. Emitting 0x7F here is what makes
     * Backspace delete instead of inserting a literal ^H. */
    scancode_ascii[0x0E] = 0x7F;
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
    /* 0x1C = Enter. A terminal in raw mode (which vi sets) delivers
     * CR, not LF. Cooked mode translates CR→LF via ICRNL for
     * line-oriented readers like the shell. If we emitted LF here,
     * raw-mode programs would see LF where they expect CR and
     * command lines would not terminate. */
    scancode_ascii[0x1C] = '\r';

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

    /* ---- Shifted ASCII ---- */

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
    /* Drop NUL. Two callers can produce 0: scancode_to_ascii for an
     * unassigned scancode (including break codes, which it handles
     * separately), and the IRQ handler when it decides not to enqueue
     * a modifier-only key. In both cases the byte carries no
     * information and must not enter the ring. This guard is correct,
     * but note what it does NOT do: it cannot distinguish "no key" from
     * "the key is NUL". That is why every scancode that should produce
     * a real character — especially ESC at 0x01 — must be assigned in
     * the tables above. If a table entry is missing, the resulting 0
     * is silently dropped here, and the key appears to do nothing. */
    if (c == '\0') {
        return 0;
    }

    int next = (kbd_head + 1) % KBD_BUFFER_SIZE;
    if (next == kbd_tail)
        return 0; /* buffer full */
    kbd_buffer[kbd_head] = c;
    kbd_head = next;
    return 1;
}

int kbd_buffer_get(char *c) {
    if (kbd_head == kbd_tail)
        return 0; /* empty */
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

/* Flush outstanding input. Used when the shell execs a new process
 * and wants to discard anything the user typed while the previous
 * one was running. */
void keyboard_buffer_flush(void) {
    __asm__ volatile("cli" ::: "memory");
    kbd_head = 0;
    kbd_tail = 0;
    __asm__ volatile("sti" ::: "memory");
}

/*
 * Translate a set-1 scancode to a byte, or 0 for "no byte".
 *
 * shift: left or right shift held
 * caps:  caps lock toggled on
 *
 * Modifier-only scancodes (shift, ctrl, alt, caps, num, scroll) are
 * left unassigned in the tables, so they produce 0 and are dropped by
 * kbd_buffer_put. That is the correct behavior: the IRQ handler is
 * responsible for tracking their state and for suppressing the
 * break-code event when the corresponding make-code should be
 * suppressed.
 *
 * Ctrl+letter and Ctrl+symbol handling is NOT implemented here yet.
 * It requires a fourth parameter carrying Ctrl state from the IRQ
 * handler, which we are deliberately deferring until after the ESC
 * fix is confirmed. Until then, the literal ESC key (scancode 0x01)
 * is the only source of 0x1B. Ctrl+[ will not produce ESC. That is
 * fine for getting vi usable today; it is not fine for the long
 * term, and it is the first thing to add once the immediate work is
 * done.
 */
char scancode_to_ascii(uint8_t sc, int shift, int caps) {
    /* Ignore break codes */
    if (sc & 0x80)
        return 0;

    uint8_t code = sc & 0x7F;

    char ch = shift ? scancode_shift[code] : scancode_ascii[code];
    if (!ch)
        return 0;

    /* Caps Lock only affects letters when shift is NOT active */
    if (caps && !shift) {
        if (ch >= 'a' && ch <= 'z')
            ch = ch - 'a' + 'A';
    }

    return ch;
}
