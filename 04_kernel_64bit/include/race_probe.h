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
#ifndef RACE_PROBE_P8_READYQ
#define RACE_PROBE_P8_READYQ 1
#endif
#ifndef RACE_PROBE_P11_WAKE
#define RACE_PROBE_P11_WAKE 1
#endif
#ifndef RACE_PROBE_P12_PIPE_CLOSE
#define RACE_PROBE_P12_PIPE_CLOSE 1
#endif
#ifndef RACE_PROBE_P13_PIPE_TRACE
#define RACE_PROBE_P13_PIPE_TRACE 1
#endif
#ifndef RACE_PROBE_PPS_VERBOSE
#define RACE_PROBE_PPS_VERBOSE 0
#endif
/* VGA half of process_dump_all().  The serial half is what
 * capture.txt records and is unconditional.  The VGA half
 * scribbles the framebuffer every time the dump fires, which
 * is what puts the PROCESS LIST on the visible console.  Set
 * to 1 only when doing an interactive on-screen debugging
 * session; default 0 for headless capture runs. */
#ifndef RACE_PROBE_DUMP_VGA
#define RACE_PROBE_DUMP_VGA 0
#endif
/* Fork-time user-stack slot check in process_fork_copy_frame.
 * Prints the 8 bytes the child's first post-syscall `ret` will
 * pop, resolved through the parent's and child's cr3.  Fires
 * on almost every fork on a healthy system, so it is
 * success-path noise unless a fork-frame bug is being chased.
 * Default 0. */
#ifndef RACE_PROBE_FORK_SLOT
#define RACE_PROBE_FORK_SLOT 0
#endif
/* Timer-tick auto-dump of the race ring.  race_p1_tick_dump()
 * fires the first time a P1 early wake or a P13 pipe-reader block
 * is recorded, which happens on a healthy busy system -- so the
 * print is success-path noise and it saturates the serial console.
 * The timer call site is gated by this; the two fault-handler call
 * sites (isr13, isr14) are NOT, because a fault is a failure-path
 * event and the ring is signal there.  Default 0. */
#ifndef RACE_PROBE_P1_AUTODUMP
#define RACE_PROBE_P1_AUTODUMP 0
#endif

#if (RACE_PROBE_P1_BLOCK_WAKE || RACE_PROBE_P2_PCB_REUSE || \
     RACE_PROBE_P3_LOST_READY  || RACE_PROBE_P8_READYQ   || \
     RACE_PROBE_P11_WAKE      || RACE_PROBE_P12_PIPE_CLOSE || \
     RACE_PROBE_P13_PIPE_TRACE)
#define RACE_PROBE_ANY 1
#else
#define RACE_PROBE_ANY 0
#endif

#if RACE_PROBE_ANY

#define RACE_SLOTS 256

/* Site tags.  A compile-time constant passed at the macro call
 * site.  Names the call site by construction -- does not depend
 * on a return address, which for the block site belongs to the
 * assembly syscall stub, not to sys_wait4. */
#define RACE_SITE_WAIT4_BLOCK  1
#define RACE_SITE_WAKE_PARENT  2
#define RACE_SITE_Q_ADD        3
#define RACE_SITE_Q_REMOVE     4
#define RACE_SITE_Q_NEXT       5

/* Forward declaration: every static inline below that dumps on a
 * hit needs this visible.  It used to sit at the bottom of the
 * header, which made the inlines see an implicit declaration. */
void race_probe_dump(void);

typedef struct {
    uint64_t pid;
    uint64_t a;
    uint64_t b;
    uint64_t site;
    uint64_t ra;
    uint64_t tick;
    uint64_t slot;   /* event class: which race_record call site */
} race_rec_t;

extern race_rec_t        race_log[RACE_SLOTS];
extern volatile uint64_t race_head[RACE_SLOTS];
extern volatile uint64_t race_hits;
extern volatile uint64_t race_total;

extern volatile uint64_t g_ticks;

__attribute__((noinline))
/*
 * race_record -- write one record into the shared ring.
 *
 * CHANGED 2026-10-14: this used to write race_log[slot], one record
 * per slot NUMBER, so the dump showed the LAST value per class and
 * could not show an interleaving.  It now writes a single ring: each
 * call overwrites the oldest entry, so the dump shows the last
 * RACE_SLOTS calls in chronological order, across all classes.  That
 * is what names the queue-mutator interleaving.
 *
 * `slot` is now stored as a field (race_rec_t.slot), not used as an
 * index.  The value is the class the caller passes (the RACE_SITE_*
 * numbers for the queue mutators).  race_head[0] is the ring's write
 * counter; race_head[1..] are unused.
 */
static inline void race_record(uint64_t slot,
                        uint64_t pid,
                        uint64_t a, uint64_t b,
                        uint64_t site, uint64_t ra) {
    uint64_t wr  = race_head[0];
    uint64_t idx = wr % RACE_SLOTS;
    race_rec_t* r = &race_log[idx];
    r->pid  = pid;
    r->a    = a;
    r->b    = b;
    r->site = site;
    r->ra   = ra;
    r->tick = g_ticks;
    r->slot = slot;
    race_head[0] = wr + 1;
    race_total++;
    if (wr == 0) race_hits++;
}

#endif /* RACE_PROBE_ANY */

/* ---- P1: block/wake ordering ---- */
#if RACE_PROBE_P1_BLOCK_WAKE
extern volatile uint64_t race_p1_last_block_tick;
extern volatile uint64_t race_p1_last_block_pid;
extern volatile uint64_t race_p1_early_wakes;

#define RACE_P1_BLOCK(pid, kind, site) do {                 \
    race_p1_last_block_tick = g_ticks;                      \
    race_p1_last_block_pid  = (pid);                        \
    race_record(0, (pid), (kind), 0, (site),                \
                (uint64_t)__builtin_return_address(0));      \
} while (0)

#define RACE_P1_WAKE(pid, kind, site) do {                  \
    if (race_p1_last_block_pid == (pid) &&                  \
        g_ticks <= race_p1_last_block_tick) {               \
        race_p1_early_wakes++;                              \
        race_record(1, (pid), (kind), race_p1_last_block_tick,   \
                    (site),                                     \
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
extern volatile uint64_t race_p13_saw_block;
extern volatile uint64_t race_p13_wake_lost;
static inline void race_p1_tick_dump(void) {
    /* Fire on the first pipe-reader block (P13) OR on the first
     * wait4 same-tick wake (P1), whichever comes first.  The
     * P13 trigger is the one we want for the pipe lost-wakeup
     * trace: it captures the ring at the moment pid 4 blocks on
     * the pipe, before the trace window is overwritten. */
    if ((race_p13_wake_lost || race_p1_early_wakes) && !race_p1_dumped) {
        race_p1_dumped = 1;
        race_probe_dump();
    }
}
#else
#define race_p1_tick_dump() do {} while (0)
#endif

/* Gated timer-tick call site.  The dump function above is called
 * unconditionally by the fault handlers; this macro is what the
 * timer calls, so the auto-dump can be silenced on a healthy run
 * without silencing the fault-path dump. */
#if RACE_PROBE_P1_AUTODUMP
#define race_p1_timer_dump() race_p1_tick_dump()
#else
#define race_p1_timer_dump() do {} while (0)
#endif

/* ---- P2: PCB reuse ---- */
#if RACE_PROBE_P2_PCB_REUSE
extern volatile uint64_t race_p2_unused_touches;
#define RACE_P2_TOUCH(pcb) do {                             \
    if ((pcb) && (pcb)->state == 0) {                       \
        race_p2_unused_touches++;                           \
        race_record(2, (pcb)->pid, (uint64_t)(pcb), 0, 0,  \
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


/* ---- P8: ready-queue well-formedness ----
 *
 * The invariant: ready_queue_head->prev == NULL,
 * ready_queue_tail->next == NULL, and every on-queue pcb's
 * prev/next are consistent.  The mutating functions write five
 * or six slots; a tick between two of them sees a half-linked
 * list.  This check runs at the top of each mutating entry and
 * at the top of scheduler_ready_queue_next.  It records and
 * continues; it never halts.
 *
 * race_p8_check takes the head, tail, and the function's own
 * pcb (or NULL).  It is header-only so it costs nothing when
 * RACE_PROBE_P8_READYQ is 0. */
/* ---- P8: ready-queue well-formedness ----
 *
 * The invariant: ready_queue_head->prev == NULL,
 * ready_queue_tail->next == NULL, head->next != head, and
 * head->next->prev == head.  The mutating functions write five
 * or six slots; a tick between two of them sees a half-linked
 * list.  This check runs at the top of each mutating entry and
 * at the top of scheduler_ready_queue_next.  It records and
 * continues; it never halts.
 *
 * The check is a MACRO, not an inline function, because
 * race_probe.h does not know struct pcb -- the header is
 * self-contained and depends only on stdint.h.  The macro
 * expands at the call site, where pcb_t is complete. */
#if RACE_PROBE_P8_READYQ
extern volatile uint64_t race_p8_bad_head_prev;
extern volatile uint64_t race_p8_bad_tail_next;
extern volatile uint64_t race_p8_bad_links;
extern volatile uint64_t race_p8_bad_self;
extern volatile uint64_t race_p8_checks;
#define race_p8_check(head, tail, site, process) do {                    \
    race_p8_checks++;                                                    \
    /*                                                                    \
     * Record EVERY call, not just the violations.  The ring then holds   \
     * the last 256 queue operations, and the call that left             \
     * tail->next != NULL is one of them.                                \
     *                                                                    \
     * Before this, race_record ran only in the four violation branches.  \
     * The ring at a violation showed the violation itself but not the   \
     * call that produced it -- the producer was earlier and had been    \
     * overwritten by slot=11 tick-skip records.  The 2026-10-14        \
     * P8-gated dumps showed exactly that: slot=8 site=5 (the check     \
     * that FOUND it) with slot=11 site=13 in the two records before    \
     * it, and no queue-mutator record in the window.                   \
     *                                                                    \
     * a=head, b=tail.  The transition to tail->next != NULL is visible  \
     * as one record where b is a node whose next is not NULL, with the  \
     * next record's b being a different (or NULL) tail.                 \
     */                                                                   \
    /*                                                                    \
     * ra carries the process this call is operating on, so a grep        \
     * for a node's address finds every operation that touched it --       \
     * not just the ones where it happened to be head or tail.             \
     * _next passes head (it operates on the head); _add and _remove       \
     * pass their process argument.                                        \
     */                                                                   \
    race_record(8, 0, (uint64_t)(head), (uint64_t)(tail), (site),        \
                (uint64_t)(process));                                    \
    if ((head) && (head)->prev != NULL) {                                \
        race_p8_bad_head_prev++;                                         \
        race_record(8, (head)->pid, (uint64_t)(head), 0, (site), 0);     \
    }                                                                    \
    if ((tail) && (tail)->next != NULL) {                                \
        race_p8_bad_tail_next++;                                         \
        race_record(8, (tail)->pid, (uint64_t)(tail), 0, (site), 0);     \
        /*                                                               \
         * Dump the ring at the moment of the violation.  The ring       \
         * holds the last 256 race_record calls; the queue-mutator       \
         * calls immediately before this one are the sequence that       \
         * left tail->next != NULL.                                      \
         *                                                               \
         * Gated to the first three: the console cost is bounded, and    \
         * three violations are enough to see the shape.  After that,    \
         * the counter still climbs (it is the before/after metric)      \
         * and the dump is silent.                                       \
         *                                                               \
         * race_probe_dump takes serial_lock, which is nesting-safe      \
         * and restores the caller's interrupt flag; race_p8_check       \
         * runs inside the scheduler's cli, not inside serial_lock,      \
         * so the nesting is clean.                                      \
         */                                                              \
        if (race_p8_bad_tail_next <= 3) race_probe_dump();               \
    }                                                                    \
    if ((head) && (head)->next == (head)) {                              \
        race_p8_bad_self++;                                              \
        race_record(8, (head)->pid, (uint64_t)(head), 0, (site), 1);     \
    }                                                                    \
    if ((head) && (head)->next && (head)->next->prev != (head)) {        \
        race_p8_bad_links++;                                             \
        race_record(8, (head)->pid, (uint64_t)(head)->next, 0, (site), 2); \
    }                                                                    \
} while (0)
#else
#define race_p8_check(head, tail, site) do {} while (0)
#endif

/* ---- P11: wait4 lost-wakeup handshake ----
 *
 * Three records per handshake:
 *   BLOCK       -- sys_wait4 stored state=BLOCKED, inside cli,
 *                  wait_pid set, about to run the late re-scan
 *   WAKE_TRY    -- process_wake_parent_if_waiting called for a
 *                  child whose parent is `pid`.  reason names the
 *                  return path: 0=ok, 1=no_parent, 2=no_pcb,
 *                  3=parent_not_blocked, 4=parent_block_kind_mismatch,
 *                  5=wait_pid_mismatch
 *   TICK_SKIP   -- process_wake_all_blocked skipped a blocked
 *                  process because block_kind != NONE.  Records
 *                  pid and block_kind so we can see the shell being
 *                  skipped every tick while it waits.
 *
 * A lost wakeup is: BLOCK for pid P, then a WAKE_TRY for pid P
 * with reason != 0, then no further records for P.  The reason
 * names the exact line. */
#define RACE_SITE_WAIT4_BLOCK2  11
#define RACE_SITE_WAKE_TRY      12
#define RACE_SITE_TICK_SKIP     13

#if RACE_PROBE_P11_WAKE
extern volatile uint64_t race_p11_blocks;
extern volatile uint64_t race_p11_wake_tries;
extern volatile uint64_t race_p11_wake_refused;
extern volatile uint64_t race_p11_tick_skips;
extern volatile uint64_t race_p11_wake_ok;
extern volatile uint64_t race_p11_skip_by_pid[32];
extern volatile uint64_t race_p11_kind_by_pid[32];
extern volatile uint64_t race_p11_refuse_by_reason[8];
extern volatile uint64_t race_p11_wake_by_pid[32];
extern volatile uint64_t race_p11_wake_ok_by_pid[32];

#define RACE_P11_BLOCK(pid, wait_pid) do {                               \
    race_p11_blocks++;                                                   \
    race_record(11, (pid), (wait_pid), 0, RACE_SITE_WAIT4_BLOCK2,        \
                (uint64_t)__builtin_return_address(0));                  \
} while (0)

/* reason: 0=ok 1=no_parent 2=no_pcb 3=not_blocked 4=kind 5=waitpid */
#define RACE_P11_WAKE_TRY(child_pid, parent_pid, reason, pstate, pkind, pwait) \
    do {                                                                 \
        race_p11_wake_tries++;                                           \
        if ((reason) != 0) race_p11_wake_refused++;                      \
        else                race_p11_wake_ok++;                          \
        if ((reason) < 8) race_p11_refuse_by_reason[(reason)]++;         \
        if ((parent_pid) < 32) {                                         \
            race_p11_wake_by_pid[(parent_pid)]++;                        \
            if ((reason) == 0)                                       \
                race_p11_wake_ok_by_pid[(parent_pid)]++;                 \
        }                                                                \
        race_record(11, (child_pid), (parent_pid),                       \
                    ((uint64_t)(reason) << 56) |                         \
                    ((uint64_t)(pstate) << 48) |                         \
                    ((uint64_t)(pkind)  << 40) |                         \
                    ((uint64_t)(pwait) & 0xFFFFFFFFFFULL),               \
                    RACE_SITE_WAKE_TRY, 0);                              \
    } while (0)

#define RACE_P11_TICK_SKIP(pid, block_kind) do {                         \
    race_p11_tick_skips++;                                               \
    if ((pid) < 32) {                                                    \
        race_p11_skip_by_pid[(pid)]++;                                   \
        race_p11_kind_by_pid[(pid)] = (block_kind);                      \
    }                                                                    \
    race_record(11, (pid), (block_kind), 0, RACE_SITE_TICK_SKIP, 0);     \
} while (0)
#else
#define RACE_P11_BLOCK(pid, wait_pid) do {} while (0)
#define RACE_P11_WAKE_TRY(c, p, r, s, k, w) do {} while (0)
#define RACE_P11_TICK_SKIP(pid, kind) do {} while (0)
#endif

/* ---- P12: pipe-slot close path (put_file_slot PIPE case) ----
 *
 * A lost reader wakeup on process exit can only mean one of:
 *   - put_file_slot reached the write-end slot but refcount > 1
 *     (another reference alive), so the PIPE case did not run
 *   - the PIPE case ran, decremented writers_open, and did not
 *     wake because reader_waiting was NULL
 *   - the PIPE case ran, decremented writers_open to a non-zero
 *     value (another writer end alive), so no wake
 *   - the PIPE case ran, writers_open reached 0, and the wake
 *     fired but the reader re-blocked because it saw the old
 *     value
 *
 * P12 records all four fields on every put_file_slot call for a
 * pipe slot, so the sequence names which path was taken. */
#define RACE_SITE_PIPE_CLOSE     20

#if RACE_PROBE_P12_PIPE_CLOSE
extern volatile uint64_t race_p12_calls;
extern volatile uint64_t race_p12_refcount_gt1;
extern volatile uint64_t race_p12_pipe_case;
extern volatile uint64_t race_p12_wake_fired;
extern volatile uint64_t race_p12_wake_no_waiter;
extern volatile uint64_t race_p12_writers_nonzero;

/* end: 0=read, 1=write.  refc_before/after: slot refcount around
 * the decrement.  writers_before/after: pipe->writers_open around
 * the decrement.  waiter: reader_waiting at the moment of the wake
 * decision.  Recorded into one record per put_file_slot call on a
 * pipe slot. */
#define RACE_P12_SLOT(slot_end, refc_before, refc_after,           \
                      writers_before, writers_after, waiter)        \
    do {                                                            \
        race_p12_calls++;                                           \
        if ((refc_before) > 1) race_p12_refcount_gt1++;             \
        else {                                                      \
            race_p12_pipe_case++;                                   \
            if ((waiter) != 0) race_p12_wake_fired++;               \
            else if ((writers_after) == 0)                          \
                race_p12_wake_no_waiter++;                          \
            if ((writers_after) != 0) race_p12_writers_nonzero++;   \
        }                                                           \
        race_record(12, (slot_end),                                  \
                    ((uint64_t)(refc_before) << 40) | ((uint64_t)(refc_after) << 32) |  \
                    ((writers_before) << 16) | (writers_after),     \
                    (waiter), RACE_SITE_PIPE_CLOSE, 0);             \
    } while (0)
#else
#define RACE_P12_SLOT(a,b,c,d,e,f) do {} while (0)
#endif

/* ---- P13: per-pipe event timeline ----
 *
 * Records the sequence of events on one pipe so the interleaving
 * that loses the wake can be read directly.  Filtered to the pipe
 * whose reader is (or was) RACE_P13_WATCH_PID (default 4).
 *
 * Each record: pid=the process performing the event,
 * a=the pipe_id (pipe_t* cast), b=packed payload, site names the
 * event kind.
 */
#define RACE_SITE_READ_CHECK    30
#define RACE_SITE_READ_BLOCK    31
#define RACE_SITE_READ_WAKE     32
#define RACE_SITE_READ_EOF      33
#define RACE_SITE_WRITE_PUT     34
#define RACE_SITE_WRITE_WAKE    35
#define RACE_SITE_CLOSE_READ    36
#define RACE_SITE_CLOSE_WRITE   37
#define RACE_SITE_PIPE_CREATE   38
#define RACE_SITE_PIPE_FREE     39

#ifndef RACE_P13_WATCH_PID
#define RACE_P13_WATCH_PID 4
#endif

#if RACE_PROBE_P13_PIPE_TRACE
extern volatile uint64_t race_p13_watch_pipe;
extern volatile uint64_t race_p13_events;
extern volatile uint64_t race_p13_saw_create;
extern volatile uint64_t race_p13_saw_block;
extern volatile uint64_t race_p13_saw_wake;
extern volatile uint64_t race_p13_saw_eof;

/* filter: record only when pipe_id matches the watched pipe
 * OR when the event is the create/close of a pipe whose reader
 * is pid RACE_P13_WATCH_PID.  The reader's pid is stored in
 * race_p13_watch_pipe when the reader first blocks. */
#define RACE_P13_EVENT(pipe_id, pid, payload, site) do {            \
    if ((pipe_id) == race_p13_watch_pipe ||                         \
        (pipe_id) == 0) {                                           \
        race_p13_events++;                                          \
        race_record(13, (pid), (pipe_id), (payload), (site), 0);    \
    }                                                               \
} while (0)

#define RACE_P13_READER_PID(pipe_id, pid) do {                      \
    if ((pid) == RACE_P13_WATCH_PID) {                              \
        race_p13_watch_pipe = (pipe_id);                            \
        race_p13_saw_create++;                                      \
    }                                                               \
} while (0)
#else
#define RACE_P13_EVENT(pipe_id, pid, payload, site) do {} while (0)
#define RACE_P13_READER_PID(pipe_id, pid) do {} while (0)
#endif

#endif /* RACE_PROBE_H */
