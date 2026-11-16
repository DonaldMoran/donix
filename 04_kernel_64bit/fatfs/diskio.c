/* 04_kernel_64bit/fatfs/diskio.c */
#include "ff.h"
#include "diskio.h"
#include "../include/ata.h"
#include "../include/fat_config.h"

/* Map physical drive 0 to the configured FatFs drive */
#define DEV_FAT 0

/*
 * Partition offset. In single-drive mode the FAT volume starts at
 * LBA 2048 of the master disk, and FF_MULTI_PARTITION is 0 in
 * ffconf.h, so FatFs is NOT partition-aware: it treats the volume as
 * starting at LBA 0 of the physical drive. diskio.c must translate
 * every FatFs sector number (which is volume-relative) into a device
 * LBA by adding this offset.
 *
 * In dual-drive mode the FAT volume is a whole-disk image (fat.img)
 * with no partition table, so the offset is 0.
 */
#if FAT_CONFIG_SINGLE_DRIVE
    #define FAT_PARTITION_OFFSET  2048
#else
    #define FAT_PARTITION_OFFSET  0
#endif

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/
DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != DEV_FAT) return STA_NOINIT;

    if (!ata_present(FAT_DRIVE)) {
        return STA_NODISK;
    }
    return 0;
}

/*-----------------------------------------------------------------------*/
/* Initialize a Drive                                                    */
/*-----------------------------------------------------------------------*/
DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != DEV_FAT) return STA_NOINIT;

    if (!ata_present(FAT_DRIVE)) {
        return STA_NODISK;
    }
    return 0;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/
DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != DEV_FAT || count == 0) return RES_PARERR;

    uint32_t dev_lba = (uint32_t)sector + FAT_PARTITION_OFFSET;

    int rc = ata_read_sectors_drive(FAT_DRIVE, dev_lba,
                                    (uint32_t)count, buff);
    return (rc == 0) ? RES_OK : RES_ERROR;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/
#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != DEV_FAT || count == 0) return RES_PARERR;

    uint32_t dev_lba = (uint32_t)sector + FAT_PARTITION_OFFSET;

    int rc = ata_write_sectors_drive(FAT_DRIVE, dev_lba,
                                     (uint32_t)count, buff);
    return (rc == 0) ? RES_OK : RES_ERROR;
}
#endif

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    if (pdrv != DEV_FAT) return RES_PARERR;

    switch (cmd) {
        case CTRL_SYNC:
            if (ata_flush_cache_drive(FAT_DRIVE) != 0) {
                return RES_ERROR;
            }
            return RES_OK;

        case GET_SECTOR_COUNT:
            /*
             * Return the size of the VOLUME, not the physical device.
             * FatFs uses this for f_getfree() and cluster accounting.
             */
            *(LBA_t*)buff = FAT_VOLUME_SECTORS;
            return RES_OK;

        case GET_SECTOR_SIZE:
            *(WORD*)buff = ATA_SECTOR_SIZE;
            return RES_OK;

        case GET_BLOCK_SIZE:
            *(DWORD*)buff = 1;
            return RES_OK;
    }

    return RES_PARERR;
}

#include <stdint.h>

/*
 * FatFs calls get_fattime when it stamps a new file or directory
 * with "the current time."  Returns a FAT-packed 32-bit value:
 *
 *   bits 31..25  year - 1980   (0..127)
 *   bits 24..21  month         (1..12)
 *   bits 20..16  day           (1..31)
 *   bits 15..11  hour          (0..23)
 *   bits 10..5   minute        (0..59)
 *   bits 4..0    second / 2    (0..29)
 *
 * Computed from g_rtc_epoch_offset (the RTC read at boot,
 * 20261106-rtc-read) plus g_ticks / 100 (the same value sys_time
 * returns), so `vi`, `touch`, and every f_open(O_CREAT) path stamp
 * the real date.  A missing RTC returns 0; FatFs writes the FAT
 * floor 1980-01-01.
 *
 * Replaces the previous static fallback (2026-01-01 00:00:00),
 * which made every created file show Jan 1 instead of today.
 */
uint32_t get_fattime(void) {
    extern int64_t g_rtc_epoch_offset;
    extern volatile uint64_t g_ticks;

    int64_t now = (int64_t)(g_ticks / 100) + g_rtc_epoch_offset;
    if (now <= 0) return 0;

    int64_t days = now / 86400;
    int64_t rem  = now % 86400;

    /* civil_from_days, same as rtc.c's.  Days since 1970-01-01. */
    int64_t z   = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    int64_t y   = yoe + era * 400;
    int64_t doy = doe - (365*yoe + yoe/4 - yoe/100);
    int64_t mp  = (5*doy + 2) / 153;
    int dy      = (int)(doy - (153*mp + 2)/5 + 1);
    int mo      = (int)(mp < 10 ? mp + 3 : mp - 9);
    int yr      = (int)(y + (mo <= 2));

    int hh = (int)(rem / 3600);
    int mm = (int)((rem % 3600) / 60);
    int ss = (int)(rem % 60);

    /* FAT range clamps. */
    if (yr < 1980) yr = 1980;
    if (yr > 2107) yr = 2107;
    if (mo < 1)  mo = 1;
    if (mo > 12) mo = 12;
    if (dy < 1)  dy = 1;
    if (dy > 31) dy = 31;

    return ((uint32_t)(yr - 1980) << 25) |
           ((uint32_t)mo << 21) |
           ((uint32_t)dy << 16) |
           ((uint32_t)hh << 11) |
           ((uint32_t)mm << 5)  |
           ((uint32_t)(ss / 2));
}
