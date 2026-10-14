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
#if RACE_PROBE_P8_READYQ
volatile uint64_t race_p8_bad_head_prev = 0;
volatile uint64_t race_p8_bad_tail_next = 0;
volatile uint64_t race_p8_bad_links     = 0;
volatile uint64_t race_p8_bad_self      = 0;
volatile uint64_t race_p8_checks        = 0;
#endif
#if RACE_PROBE_P13_PIPE_TRACE
volatile uint64_t race_p13_watch_pipe = 0;
volatile uint64_t race_p13_events     = 0;
volatile uint64_t race_p13_saw_create = 0;
volatile uint64_t race_p13_saw_block  = 0;
volatile uint64_t race_p13_wake_lost  = 0;
volatile uint64_t race_p13_saw_wake   = 0;
volatile uint64_t race_p13_saw_eof    = 0;
#endif
#if RACE_PROBE_P12_PIPE_CLOSE
volatile uint64_t race_p12_calls          = 0;
volatile uint64_t race_p12_refcount_gt1   = 0;
volatile uint64_t race_p12_pipe_case      = 0;
volatile uint64_t race_p12_wake_fired     = 0;
volatile uint64_t race_p12_wake_no_waiter = 0;
volatile uint64_t race_p12_writers_nonzero = 0;
#endif
#if RACE_PROBE_P11_WAKE
volatile uint64_t race_p11_blocks       = 0;
volatile uint64_t race_p11_wake_tries   = 0;
volatile uint64_t race_p11_wake_refused = 0;
volatile uint64_t race_p11_wake_ok      = 0;
volatile uint64_t race_p11_tick_skips   = 0;
volatile uint64_t race_p11_skip_by_pid[32];
volatile uint64_t race_p11_kind_by_pid[32];
volatile uint64_t race_p11_refuse_by_reason[8];
volatile uint64_t race_p11_wake_by_pid[32];
volatile uint64_t race_p11_wake_ok_by_pid[32];
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
#if RACE_PROBE_P8_READYQ
    serial_print("P8 checks: "); serial_print_dec(race_p8_checks);
    serial_print(" bad_head_prev: "); serial_print_dec(race_p8_bad_head_prev);
    serial_print(" bad_tail_next: "); serial_print_dec(race_p8_bad_tail_next);
    serial_print(" bad_links: "); serial_print_dec(race_p8_bad_links);
    serial_print(" bad_self: "); serial_print_dec(race_p8_bad_self);
    serial_print("\n");
#endif
#if RACE_PROBE_P13_PIPE_TRACE
    serial_print("P13 watch_pipe: 0x"); serial_print_hex(race_p13_watch_pipe);
    serial_print(" events: ");          serial_print_dec(race_p13_events);
    serial_print(" create: ");          serial_print_dec(race_p13_saw_create);
    serial_print(" block: ");           serial_print_dec(race_p13_saw_block);
    serial_print(" wake: ");            serial_print_dec(race_p13_saw_wake);
    serial_print(" eof: ");             serial_print_dec(race_p13_saw_eof);
    serial_print("\n");
#endif
#if RACE_PROBE_P12_PIPE_CLOSE
    serial_print("P12 calls: ");         serial_print_dec(race_p12_calls);
    serial_print(" refcount_gt1: ");     serial_print_dec(race_p12_refcount_gt1);
    serial_print(" pipe_case: ");        serial_print_dec(race_p12_pipe_case);
    serial_print(" wake_fired: ");       serial_print_dec(race_p12_wake_fired);
    serial_print(" wake_no_waiter: ");   serial_print_dec(race_p12_wake_no_waiter);
    serial_print(" writers_nonzero: ");  serial_print_dec(race_p12_writers_nonzero);
    serial_print("\n");
#endif
#if RACE_PROBE_P11_WAKE
    serial_print("P11 blocks: ");       serial_print_dec(race_p11_blocks);
    serial_print(" wake_tries: ");      serial_print_dec(race_p11_wake_tries);
    serial_print(" wake_ok: ");         serial_print_dec(race_p11_wake_ok);
    serial_print(" wake_refused: ");    serial_print_dec(race_p11_wake_refused);
    serial_print(" tick_skips: ");      serial_print_dec(race_p11_tick_skips);
    serial_print("\n");
    serial_print("P11 skip_by_pid:");
    for (int _i = 0; _i < 32; _i++) {
        if (race_p11_skip_by_pid[_i]) {
            serial_print("  pid=");
            serial_print_dec((uint64_t)_i);
            serial_print(" count=");
            serial_print_dec(race_p11_skip_by_pid[_i]);
            serial_print(" kind=");
            serial_print_dec(race_p11_kind_by_pid[_i]);
            serial_print("\n");
        }
    }
    serial_print("P11 refuse_by_reason:  r0=");
    serial_print_dec(race_p11_refuse_by_reason[0]);
    for (int _r = 1; _r < 8; _r++) {
        serial_print(" r");
        serial_print_dec((uint64_t)_r);
        serial_print("=");
        serial_print_dec(race_p11_refuse_by_reason[_r]);
    }
    serial_print("\n");
    serial_print("P11 wake_by_pid:");
    for (int _i = 0; _i < 32; _i++) {
        if (race_p11_wake_by_pid[_i]) {
            serial_print("  pid=");
            serial_print_dec((uint64_t)_i);
            serial_print(" tries=");
            serial_print_dec(race_p11_wake_by_pid[_i]);
            serial_print(" ok=");
            serial_print_dec(race_p11_wake_ok_by_pid[_i]);
            serial_print("\n");
        }
    }
#endif

    /*
     * Ring, in chronological order: oldest surviving to newest.
     *
     * CHANGED 2026-10-14: race_record now writes a ring, so this
     * prints the last RACE_SLOTS calls in the order they were made,
     * not one value per slot number.  That is the queue-mutator
     * sequence that produced the empty queue -- the interleaving.
     *
     * Print the last 64 to keep the serial output bounded; the
     * interesting events are the few calls immediately before the
     * dump fired, and 64 covers several mutator rounds.
     */
    {
        uint64_t wr  = race_head[0];
        uint64_t n   = (wr < RACE_SLOTS) ? wr : RACE_SLOTS;
        uint64_t show = (n < 64) ? n : 64;
        uint64_t start = (wr >= show) ? (wr - show) : 0;
        serial_print("--- ring: last ");
        serial_print_dec(show);
        serial_print(" of ");
        serial_print_dec(n);
        serial_print(" (total ");
        serial_print_dec(wr);
        serial_print(") ---\n");
        for (uint64_t k = start; k < wr; k++) {
            uint64_t idx = k % RACE_SLOTS;
            serial_print("  ");
            serial_print_dec(k);
            serial_print(" slot=");
            serial_print_dec(race_log[idx].slot);
            serial_print(" pid=");
            serial_print_dec(race_log[idx].pid);
            serial_print(" a=");
            serial_print_dec(race_log[idx].a);
            serial_print(" b=");
            serial_print_dec(race_log[idx].b);
            serial_print(" site=");
            serial_print_dec(race_log[idx].site);
            serial_print(" tick=");
            serial_print_dec(race_log[idx].tick);
            serial_print(" ra=0x");
            serial_print_hex(race_log[idx].ra);
            serial_print("\n");
        }
    }
    serial_print("=== END RACE PROBE DUMP ===\n\n");
    serial_unlock();
}

#endif /* RACE_PROBE_ANY */
