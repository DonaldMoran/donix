/*
 * race_probe.c -- storage and dump for include/race_probe.h.
 *
 * One translation unit so the counters have exactly one definition.
 * Gated by the same #defines as the header: with every probe 0, this
 * file compiles to an empty object.
 *
 * The dump is deliberately NOT on the hot path.  Call it from the
 * kernel shell or from a panic; it walks the ring and prints.
 */

#include "include/race_probe.h"
#include "include/serial.h"

#if RACE_PROBE_ANY

race_rec_t        race_log[RACE_SLOTS];
volatile uint64_t race_head[RACE_SLOTS];
volatile uint64_t race_hits;
volatile uint64_t race_total;

#if RACE_PROBE_P1_BLOCK_WAKE
volatile uint64_t race_p1_last_block_tick = 0;
volatile uint64_t race_p1_last_block_pid  = 0;
volatile uint64_t race_p1_early_wakes     = 0;
volatile uint64_t race_p1_dumped          = 0;
#endif
#if RACE_PROBE_P2_PCB_REUSE
volatile uint64_t race_p2_unused_touches  = 0;
#endif
#if RACE_PROBE_P3_LOST_READY
volatile uint64_t race_p3_lost            = 0;
#endif

void race_probe_dump(void) {
    serial_lock();
    serial_print("\n=== RACE PROBE DUMP ===\n");
    serial_print("total records: "); serial_print_dec(race_total); serial_print("\n");
    serial_print("hits:          "); serial_print_dec(race_hits);  serial_print("\n");

#if RACE_PROBE_P1_BLOCK_WAKE
    serial_print("P1 early_wakes: "); serial_print_dec(race_p1_early_wakes);
    serial_print("\n");
#endif
#if RACE_PROBE_P2_PCB_REUSE
    serial_print("P2 unused_touches: "); serial_print_dec(race_p2_unused_touches);
    serial_print("\n");
#endif
#if RACE_PROBE_P3_LOST_READY
    serial_print("P3 lost_ready: "); serial_print_dec(race_p3_lost);
    serial_print("\n");
#endif

    serial_print("--- ring (up to 32) ---\n");
    for (int i = 0; i < RACE_SLOTS && i < 32; i++) {
        if (race_head[i] == 0) continue;
        serial_print("  slot ");
        serial_print_dec((uint64_t)i);
        serial_print(" pid=");
        serial_print_dec(race_log[i].pid);
        serial_print(" a=");
        serial_print_dec(race_log[i].a);
        serial_print(" b=");
        serial_print_dec(race_log[i].b);
        serial_print(" tick=");
        serial_print_dec(race_log[i].tick);
        serial_print(" ra=0x");
        serial_print_hex(race_log[i].ra);
        serial_print("\n");
    }
    serial_print("=== END RACE PROBE DUMP ===\n\n");
    serial_unlock();
}

#endif /* RACE_PROBE_ANY */
