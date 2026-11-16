/*
 * MC146818A real-time clock, read once at boot.
 *
 * The RTC is at I/O ports 0x70 (index) and 0x71 (data).  It is the
 * PC standard; QEMU's pc machine provides it by default (set to the
 * host's UTC at startup), and every real x86 board has one.  The
 * register layout is identical on both, so the code below runs
 * unchanged under QEMU and on bare metal.
 *
 * Registers we read:
 *   0x00 seconds (BCD, or binary if register B bit 2 is set)
 *   0x02 minutes
 *   0x04 hours
 *   0x07 day of month
 *   0x08 month
 *   0x09 year, last two digits (0x26 = 2026)
 *   0x0A register A: bit 7 is Update In Progress (UIP)
 *   0x0B register B: bit 1 = 24-hour, bit 2 = binary vs BCD
 *
 * Read sequence (the shape Linux's read_persistent_clock64 uses):
 *   1. wait for UIP to clear
 *   2. read sec..year
 *   3. re-read sec; if it changed, an update raced -- retry
 *   4. BCD -> binary
 *   5. days_from_civil + hh:mm:ss -> unix_sec
 *
 * A failure to read, or a value outside 2000..2107 (dead CMOS
 * battery, nonstandard emulation), returns 0.  The rest of the
 * kernel treats 0 as "no RTC" and falls back to g_ticks/100 as
 * epoch seconds, which is what donix did before this file existed.
 */

#include <stdint.h>
#include "include/rtc.h"
#include "include/serial.h"

/*
 * The RTC epoch, set once at boot by kmain's call to
 * rtc_read_unix_seconds(), read by every time syscall
 * (sys_time, sys_clock_gettime, sys_gettimeofday, and
 * sys_utimensat's "now" path).  0 means "no RTC"; the
 * kernel then falls back to g_ticks/100 as epoch seconds,
 * which is what donix did before this file existed.
 *
 * Defined here rather than in kmain.c because the offset
 * is RTC state; kmain.c is a caller.  Same home as g_ticks
 * (interrupts.c).
 */
int64_t g_rtc_epoch_offset = 0;

static inline uint8_t rtc_inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void rtc_outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static uint8_t cmos_read(uint8_t reg) {
    rtc_outb(0x70, reg);
    return rtc_inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t b) {
    return (uint8_t)((b & 0x0F) + ((b >> 4) * 10));
}

/*
 * Howard Hinnant's days_from_civil: the number of days from
 * 1970-01-01 to (y, m, d), where y is a full year and m is 1..12.
 * The same algorithm epoch_to_fat uses, in reverse.  Kept local to
 * this file because rtc.c is the only caller and the kernel has no
 * libc.
 */
static int64_t days_from_civil(int y, int m, int d) {
    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* And its inverse, civil_from_days, for the boot line. */
static void civil_from_days(int64_t z, int* out_y, int* out_m, int* out_d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    int64_t y   = yoe + era * 400;
    int64_t doy = doe - (365*yoe + yoe/4 - yoe/100);
    int64_t mp  = (5*doy + 2) / 153;
    int d       = (int)(doy - (153*mp + 2)/5 + 1);
    int m       = (int)(mp < 10 ? mp + 3 : mp - 9);
    *out_y      = (int)(y + (m <= 2));
    *out_m      = m;
    *out_d      = d;
}

int64_t rtc_read_unix_seconds(void) {
    uint8_t sec, min, hour, day, mon, year;

    /* Wait out Update In Progress (register A bit 7).  If it never
     * clears, give up and return 0 -- the caller falls back to
     * g_ticks/100.  1,000,000 iterations at a few cycles each is
     * plenty for the 244 us max UIP window; if it has not cleared
     * by then, the CMOS is not answering. */
    int spins = 0;
    while ((cmos_read(0x0A) & 0x80) && spins++ < 1000000) { }

    do {
        sec  = cmos_read(0x00);
        min  = cmos_read(0x02);
        hour = cmos_read(0x04);
        day  = cmos_read(0x07);
        mon  = cmos_read(0x08);
        year = cmos_read(0x09);
    } while (sec != cmos_read(0x00));

    uint8_t regB = cmos_read(0x0B);

    int s, mi, h, dy, mo, yr;
    if (regB & 0x04) {
        /* Binary mode. */
        s  = sec;  mi = min;  h  = hour;
        dy = day;  mo = mon;  yr = year;
    } else {
        /* BCD mode (the default). */
        s  = bcd_to_bin(sec);
        mi = bcd_to_bin(min);
        h  = bcd_to_bin(hour);
        dy = bcd_to_bin(day);
        mo = bcd_to_bin(mon);
        yr = bcd_to_bin(year);
    }

    /* 12-hour mode: bit 1 of register B clear, and bit 7 of the
     * hours byte is the PM flag.  Force 24-hour. */
    if (!(regB & 0x02) && (h & 0x80)) {
        h = (h & 0x7F) + 12;
    }

    int full_year = 2000 + yr;
    /* Sanity check.  A bad CMOS read is usually all 0x00 or all
     * 0xFF; either lands outside 2000..2107.  2107 is FAT's upper
     * bound; above that there is nowhere on disk to store it, so
     * refusing here keeps the rest of the kernel consistent. */
    if (full_year < 2000 || full_year > 2107) return 0;
    if (mo < 1 || mo > 12 || dy < 1 || dy > 31 ||
        h  < 0 || h  > 23 || mi < 0 || mi > 59 ||
        s  < 0 || s  > 59) {
        return 0;
    }

    int64_t days = days_from_civil(full_year, mo, dy);
    return days * 86400 + (int64_t)h * 3600 + (int64_t)mi * 60 + s;
}

void rtc_print_boot_line(int64_t epoch) {
    if (epoch <= 0) return;   /* caller prints "no RTC"; we print nothing */

    int64_t days = epoch / 86400;
    int64_t rem  = epoch % 86400;
    int hh = (int)(rem / 3600);
    int mm = (int)((rem % 3600) / 60);
    int ss = (int)(rem % 60);

    int y, mo, dy;
    civil_from_days(days, &y, &mo, &dy);

    /* serial_print only takes strings and uint64_t; format in
     * a small stack buffer.  Every field is fixed width so the
     * line is greppable. */
    char buf[32];
    int o = 0;
    #define APPEND_U(v, w) do {                                    \
        unsigned long _v = (unsigned long)(v);                     \
        int _w = (w);                                              \
        for (int _i = _w - 1; _i >= 0; _i--) {                     \
            buf[o + _i] = (char)('0' + (int)(_v % 10));            \
            _v /= 10;                                              \
        }                                                          \
        o += _w;                                                   \
    } while (0)

    APPEND_U(y, 4);  buf[o++] = '-';
    APPEND_U(mo, 2); buf[o++] = '-';
    APPEND_U(dy, 2); buf[o++] = ' ';
    APPEND_U(hh, 2); buf[o++] = ':';
    APPEND_U(mm, 2); buf[o++] = ':';
    APPEND_U(ss, 2); buf[o++] = ' ';
    buf[o++] = 'U'; buf[o++] = 'T'; buf[o++] = 'C';
    buf[o] = '\0';
    #undef APPEND_U

    serial_print(buf);
}
