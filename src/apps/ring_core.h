/*
 * ring_core.h - "keep the last N of these", written once.
 *
 * Four places in this tree keep a fixed ring of recent things, and all four
 * spelled the wrapping arithmetic differently:
 *
 *   hist_core.c   at = head - count + i; while (at < 0) at += MAX;
 *                 return sample[at % MAX];
 *   app_console.c (line_head - line_count + i + CON_LINES * 2) % CON_LINES
 *   app_console.c (base + pos) % CON_HIST, over a count that never caps
 *   app_logview.c (start + i) % LV_MAX_LINES
 *
 * Every one of them is correct today -- that was checked before any of this
 * was written, and it is why this is housekeeping rather than a bug fix. The
 * point is that they are four chances to be wrong about the same question,
 * each with its own off-by-one to make, and the arithmetic is exactly the
 * kind that looks right in review and fails only once the ring has wrapped
 * (which, for a 240-line scrollback, is well after anybody stops watching).
 *
 * This core owns ONLY the indices. The caller keeps its own storage, because
 * the four differ in what they store (a long, a 96-byte line, a 48-byte
 * command) and there is no allocation anywhere in this system to paper over
 * that with. So:
 *
 *     sys_strlcpy(c->lines[ring_push(&c->ring)], text, CON_COLS);
 *     ... ring_at(&c->ring, i) ...      index 0 is the OLDEST still held
 */
#ifndef CASTALIA_RING_CORE_H
#define CASTALIA_RING_CORE_H

#include "castalia/ctypes.h"

typedef struct {
    int cap;     /* slots the caller has provided                          */
    int count;   /* how many hold something (<= cap)                       */
    int head;    /* slot the next push will use                            */
} Ring;

/* Set up an empty ring over 'cap' slots. A cap below 1 becomes 1, because
 * every reader below divides by it. */
void ring_init(Ring *r, int cap);

/* Empty it without disturbing the capacity. */
void ring_reset(Ring *r);

/*
 * The slot to write the next item into, advancing the ring. Once full this
 * returns the slot holding the OLDEST item, which the caller overwrites --
 * that is what "keep the last N" means. Returns 0 for a NULL ring so a caller
 * that ignores the failure writes to a real slot rather than off the front.
 */
int ring_push(Ring *r);

/*
 * The slot holding logical item 'i', where 0 is the OLDEST still held and
 * count-1 the newest. -1 when 'i' is out of range, so a caller cannot read a
 * slot that was never written.
 */
int ring_at(const Ring *r, int i);

/* How many items are held, and whether the next push will evict one. */
int   ring_count(const Ring *r);
cbool ring_full(const Ring *r);

#endif /* CASTALIA_RING_CORE_H */
