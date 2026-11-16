#ifndef RTC_H
#define RTC_H

#include <stdint.h>

/*
 * Read the MC146818A real-time clock once at boot and return the
 * current time as a Unix epoch-1970 second count.
 *
 * Returns 0 if the RTC cannot be read, is mid-update for too long,
 * or reports a value outside 2000..2107 (a dead CMOS battery, a
 * nonstandard emulation).  0 is the honest fallback: the kernel
 * treats "no RTC" as "epoch is 0", which is what donix did before
 * this function existed.
 *
 * The RTC is at I/O ports 0x70 (index) and 0x71 (data).  It is the
 * PC standard -- present on every x86 since 1984, on QEMU's pc
 * machine by default (QEMU sets it to the host's UTC at startup
 * unless -rtc base=localtime), and on every real board.  The same
 * code runs on QEMU and on bare metal.
 */
int64_t rtc_read_unix_seconds(void);

/*
 * The RTC epoch, set once at boot and read by every time
 * syscall.  0 means "no RTC"; the fallback is
 * g_ticks/100.  Defined in rtc.c.
 */
extern int64_t g_rtc_epoch_offset;

/*
 * Format an epoch-1970 second count as "YYYY-MM-DD HH:MM:SS UTC" and
 * print it with serial_print.  Used only by the boot line in kmain;
 * no syscall calls this.  A non-positive epoch is "no RTC" and prints
 * nothing -- the caller handles the empty case.
 */
void rtc_print_boot_line(int64_t epoch);

#endif
