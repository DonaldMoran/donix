/* include/race_probe.h
 *
 * Compile-time race instrumentation.  Include in any .c that
 * participates in a block/wake transition or reclaims a PCB.
 *
 * Every probe is gated by a #define.  With all probes 0, this header
 * expands to nothing.  Turn on one probe at a time.
 *
 * DESIGN RULES:
 *   1. NEVER serial_print from a probe on the hot path.  Increment a
 *      counter and record an address; dump on demand.
 *   2. NEVER halt.  Count, record, continue.
 *   3. Record the return address, not a function name.
 *   4. A probe must not itself change the interrupt flag.
 */

#ifndef RACE_PROBE_H
#define RACE_PROBE_H

#include <stdint.h>

/* ---- probe gates: exactly one on at a time ---- */
#ifndef RACE_PROBE_P1_BLOCK_WAKE
#define RACE_PROBE_P1_BLOCK_WAKE 1
#endif
#ifndef RACE_PROBE_P2_PCB_REUSE
#define RACE_PROBE_P2_PCB_REUSE 0
#endif
#ifndef RACE_PROBE_P3_LOST_READY
#define RACE_PROBE_P3_LOST_READY 0
#endif

#if (RACE_PROBE_P1_BLOCK_WAKE || RACE_PROBE_P2_PCB_REUSE || \
     RACE_PROBE_P3_LOST_READY)
#define RACE_PROBE_ANY 1
#else
#define RACE_PROBE_ANY 0
#endif

#if RACE_PROBE_ANY

#define RACE_SLOTS 64

/* Forward declaration: every static inline below that dumps on a
 * hit needs this visible.  It used to sit at the bottom of the
 * header, which made the inlines see an implicit declaration. */
void race_probe_dump(void);

typedef struct {
    uint64_t pid;
    uint64_t a;
    uint64_t b;
    uint64_t ra;
    uint64_t tick;
} race_rec_t;

extern race_rec_t        race_log[RACE_SLOTS];
extern volatile uint64_t race_head[RACE_SLOTS];
extern volatile uint64_t race_hits;
extern volatile uint64_t race_total;

extern volatile uint64_t g_ticks;

__attribute__((noinline))
static void race_record(uint64_t slot,
                        uint64_t pid,
                        uint64_t a, uint64_t b,
                        uint64_t ra) {
    if (slot >= RACE_SLOTS) return;
    uint64_t idx = race_head[slot];
    race_rec_t* r = &race_log[slot];
    r->pid  = pid;
    r->a    = a;
    r->b    = b;
    r->ra   = ra;
    r->tick = g_ticks;
    race_head[slot] = idx + 1;
    race_total++;
    if (idx == 0) race_hits++;
}

#endif /* RACE_PROBE_ANY */

/* ---- P1: block/wake ordering ---- */
#if RACE_PROBE_P1_BLOCK_WAKE
extern volatile uint64_t race_p1_last_block_tick;
extern volatile uint64_t race_p1_last_block_pid;
extern volatile uint64_t race_p1_early_wakes;

#define RACE_P1_BLOCK(pid, kind) do {                       \
    race_p1_last_block_tick = g_ticks;                      \
    race_p1_last_block_pid  = (pid);                        \
    race_record(0, (pid), (kind), 0,                        \
                (uint64_t)__builtin_return_address(0));      \
} while (0)

#define RACE_P1_WAKE(pid, kind) do {                        \
    if (race_p1_last_block_pid == (pid) &&                  \
        g_ticks <= race_p1_last_block_tick) {               \
        race_p1_early_wakes++;                              \
        race_record(1, (pid), (kind), race_p1_last_block_tick,   \
                    (uint64_t)__builtin_return_address(0));      \
    }                                                       \
} while (0)
#else
#define RACE_P1_BLOCK(pid, kind) do {} while (0)
#define RACE_P1_WAKE(pid, kind)  do {} while (0)
#endif

/* Auto-dump: called from timer_preempt_handler every tick and
 * from the fault handlers on the user-mode path.  Prints once,
 * the first time race_p1_early_wakes goes nonzero, then latches
 * off.  One compare per tick while the counter is zero. */
#if RACE_PROBE_P1_BLOCK_WAKE
extern volatile uint64_t race_p1_dumped;
static inline void race_p1_tick_dump(void) {
    if (race_p1_early_wakes && !race_p1_dumped) {
        race_p1_dumped = 1;
        race_probe_dump();
    }
}
#else
#define race_p1_tick_dump() do {} while (0)
#endif

/* ---- P2: PCB reuse ---- */
#if RACE_PROBE_P2_PCB_REUSE
extern volatile uint64_t race_p2_unused_touches;
#define RACE_P2_TOUCH(pcb) do {                             \
    if ((pcb) && (pcb)->state == 0) {                       \
        race_p2_unused_touches++;                           \
        race_record(2, (pcb)->pid, (uint64_t)(pcb), 0,     \
                    (uint64_t)__builtin_return_address(0)); \
    }                                                       \
} while (0)
#else
#define RACE_P2_TOUCH(pcb) do {} while (0)
#endif

/* ---- P3: lost READY ---- */
#if RACE_PROBE_P3_LOST_READY
extern volatile uint64_t race_p3_lost;
void race_p3_scan(void);
#define RACE_P3_TICK() race_p3_scan()
#else
#define RACE_P3_TICK() do {} while (0)
#endif


#endif /* RACE_PROBE_H */
