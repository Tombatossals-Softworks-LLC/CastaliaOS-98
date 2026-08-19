/*
 * test_ring.c - the shared ring index (ring_core.c).
 *
 * The interesting cases are all after the wrap. Before it, every wrong
 * formula in this family still works: while count < cap the oldest item is at
 * slot 0 and `(start + i) % cap` and `(head - count + i) % cap` agree with
 * each other and with plain `i`. It is only once the ring has turned over
 * that they separate -- which for a 240-line scrollback is long after anyone
 * is still watching.
 *
 * So the shape of this file is: fill it partly, check; fill it exactly, check;
 * then wrap it repeatedly and check that index 0 is still the oldest thing
 * held and that the order never breaks.
 */
#include "ctest.h"
#include "../src/apps/ring_core.h"

void test_ring(void)
{
    Ring r;
    int i, slot;

    printf("- ring index\n");

    /* ---- empty ---------------------------------------------------------- */
    ring_init(&r, 4);
    CHECK_EQI(ring_count(&r), 0);
    CHECK(!ring_full(&r));
    CHECK_EQI(ring_at(&r, 0), -1);
    CHECK_EQI(ring_at(&r, -1), -1);

    /* ---- partly filled: slots come out in order ------------------------- */
    CHECK_EQI(ring_push(&r), 0);
    CHECK_EQI(ring_count(&r), 1);
    CHECK_EQI(ring_at(&r, 0), 0);
    CHECK_EQI(ring_at(&r, 1), -1);      /* nothing there yet */
    CHECK_EQI(ring_push(&r), 1);
    CHECK_EQI(ring_push(&r), 2);
    CHECK_EQI(ring_count(&r), 3);
    CHECK(!ring_full(&r));
    for (i = 0; i < 3; i++) { CHECK_EQI(ring_at(&r, i), i); }

    /* ---- exactly full --------------------------------------------------- */
    CHECK_EQI(ring_push(&r), 3);
    CHECK_EQI(ring_count(&r), 4);
    CHECK(ring_full(&r));
    for (i = 0; i < 4; i++) { CHECK_EQI(ring_at(&r, i), i); }
    CHECK_EQI(ring_at(&r, 4), -1);

    /* ---- one past full: the oldest is evicted, not appended ------------- *
     *
     * This is the first case that separates a right formula from a wrong one.
     * The fifth push must reuse slot 0 -- the slot holding the oldest item --
     * and afterwards index 0 must be slot 1, not slot 0.
     */
    CHECK_EQI(ring_push(&r), 0);
    CHECK_EQI(ring_count(&r), 4);       /* still four, not five */
    CHECK_EQI(ring_at(&r, 0), 1);       /* oldest moved along   */
    CHECK_EQI(ring_at(&r, 1), 2);
    CHECK_EQI(ring_at(&r, 2), 3);
    CHECK_EQI(ring_at(&r, 3), 0);       /* newest is what we just wrote */

    /* ---- wrapped many times over ---------------------------------------- *
     *
     * Push far more than the capacity and check the invariant that actually
     * matters at every step: the slots for indices 0..count-1 are all
     * different, all inside the ring, and the newest is the slot the last
     * push returned.
     */
    ring_init(&r, 5);
    for (i = 0; i < 97; i++) {
        int seen[5];
        int k, j;
        slot = ring_push(&r);
        CHECK(slot >= 0 && slot < 5);
        CHECK_EQI(ring_at(&r, ring_count(&r) - 1), slot);
        for (k = 0; k < ring_count(&r); k++) {
            int at = ring_at(&r, k);
            CHECK(at >= 0 && at < 5);
            for (j = 0; j < k; j++) { CHECK(seen[j] != at); }
            seen[k] = at;
        }
    }
    CHECK_EQI(ring_count(&r), 5);
    CHECK(ring_full(&r));

    /* ---- a ring of one --------------------------------------------------- */
    ring_init(&r, 1);
    CHECK_EQI(ring_push(&r), 0);
    CHECK_EQI(ring_push(&r), 0);
    CHECK_EQI(ring_count(&r), 1);
    CHECK_EQI(ring_at(&r, 0), 0);
    CHECK_EQI(ring_at(&r, 1), -1);

    /* ---- reset keeps the capacity ---------------------------------------- */
    ring_init(&r, 3);
    ring_push(&r); ring_push(&r); ring_push(&r); ring_push(&r);
    CHECK(ring_full(&r));
    ring_reset(&r);
    CHECK_EQI(ring_count(&r), 0);
    CHECK(!ring_full(&r));
    CHECK_EQI(ring_at(&r, 0), -1);
    CHECK_EQI(ring_push(&r), 0);        /* starts over at the front */

    /* ---- refusals -------------------------------------------------------- */
    /* A capacity of zero would divide by zero in every reader. ring_init
     * raises it to one; that is a caller asking for a silly ring, not a
     * caller who forgot. */
    ring_init(&r, 0);
    CHECK(r.cap >= 1);
    CHECK_EQI(ring_push(&r), 0);

    /*
     * An UNINITIALISED ring is the different case, and it is the one that
     * matters. A zeroed struct -- which is what sys_calloc hands every app
     * here -- has cap 0. Pushing must not crash, and must not quietly repair
     * itself into a working one-slot ring either: the Console did exactly
     * that and became a one-line console, plausible enough to miss.
     *
     * It stays EMPTY instead. A window showing nothing is a bug report; a
     * window showing its last line only is a puzzle.
     */
    {
        Ring z;
        z.cap = 0; z.count = 0; z.head = 0;
        CHECK_EQI(ring_push(&z), 0);
        CHECK_EQI(ring_count(&z), 0);       /* did NOT start working */
        CHECK_EQI(z.cap, 0);                /* and did not repair itself */
        CHECK_EQI(ring_at(&z, 0), -1);
        CHECK(!ring_full(&z));
    }
    ring_init(&r, -7);
    CHECK(r.cap >= 1);
    ring_init(NULL, 4);                 /* no crash */
    ring_reset(NULL);
    CHECK_EQI(ring_push(NULL), 0);
    CHECK_EQI(ring_at(NULL, 0), -1);
    CHECK_EQI(ring_count(NULL), 0);
    CHECK(!ring_full(NULL));
}
