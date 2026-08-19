/*
 * test_sw.c - Most-recently-used window order (sw_core.c).
 *
 * The property that makes Alt+Tab worth having is that pressing it twice puts
 * you back where you started, so the first check below is exactly that, driven
 * as a sequence rather than as a single call -- it is an emergent property of
 * touch and pick together, and testing either alone would miss it.
 *
 * The rest are the situations that only turn up during use: closing the window
 * you are standing on, focusing something already in the list, and running past
 * the bound, where dropping the wrong end would throw away the window in front
 * of you instead of the one nobody has touched in an hour.
 */
#include "ctest.h"
#include "../src/shell/sw_core.h"

void test_sw(void)
{
    SwList l;
    int i;

    printf("- window switch order\n");

    /* ---- the property the whole thing exists for ---------------------- */
    /* Three windows opened in order 1, 2, 3 -- so 3 has focus. */
    sw_clear(&l);
    sw_touch(&l, 1);
    sw_touch(&l, 2);
    sw_touch(&l, 3);
    CHECK_EQI(sw_count(&l), 3);
    CHECK_EQI(sw_at(&l, 0), 3);

    /* One Alt+Tab goes to the window you were in before this one. */
    CHECK_EQI(sw_pick(&l, 1), 2);
    sw_touch(&l, 2);                 /* ...and switching to it commits      */
    /* Pressing it again comes straight back. THIS is the property; z-order
     * cycling would have gone on to a third window instead. */
    CHECK_EQI(sw_pick(&l, 1), 3);
    sw_touch(&l, 3);
    CHECK_EQI(sw_pick(&l, 1), 2);

    /* Holding it further walks back through the history in order. */
    sw_clear(&l);
    for (i = 1; i <= 4; i++) { sw_touch(&l, i); }   /* front is 4          */
    CHECK_EQI(sw_pick(&l, 0), 4);
    CHECK_EQI(sw_pick(&l, 1), 3);
    CHECK_EQI(sw_pick(&l, 2), 2);
    CHECK_EQI(sw_pick(&l, 3), 1);
    CHECK_EQI(sw_pick(&l, 4), 4);    /* wraps                               */
    CHECK_EQI(sw_pick(&l, 9), 3);    /* and keeps wrapping                  */
    /* Shift+Alt+Tab walks the other way. */
    CHECK_EQI(sw_pick(&l, -1), 1);
    CHECK_EQI(sw_pick(&l, -2), 2);
    CHECK_EQI(sw_pick(&l, -5), 1);

    /* ---- focusing something already held moves it, never duplicates ---- */
    sw_clear(&l);
    sw_touch(&l, 7);
    sw_touch(&l, 8);
    sw_touch(&l, 9);
    sw_touch(&l, 7);
    CHECK_EQI(sw_count(&l), 3);
    CHECK_EQI(sw_at(&l, 0), 7);
    CHECK_EQI(sw_at(&l, 1), 9);
    CHECK_EQI(sw_at(&l, 2), 8);
    /* Touching the front again changes nothing at all. */
    sw_touch(&l, 7);
    CHECK_EQI(sw_count(&l), 3);
    CHECK_EQI(sw_at(&l, 0), 7);
    CHECK_EQI(sw_at(&l, 1), 9);

    /* ---- closing windows ----------------------------------------------- */
    sw_clear(&l);
    for (i = 1; i <= 4; i++) { sw_touch(&l, i); }   /* 4 3 2 1             */
    sw_remove(&l, 4);                                /* close the front     */
    CHECK_EQI(sw_count(&l), 3);
    CHECK_EQI(sw_at(&l, 0), 3);
    CHECK_EQI(sw_pick(&l, 1), 2);
    sw_remove(&l, 2);                                /* close the middle    */
    CHECK_EQI(sw_count(&l), 2);
    CHECK_EQI(sw_at(&l, 0), 3);
    CHECK_EQI(sw_at(&l, 1), 1);
    sw_remove(&l, 99);                               /* never held          */
    CHECK_EQI(sw_count(&l), 2);
    sw_remove(&l, 1);
    sw_remove(&l, 3);
    CHECK_EQI(sw_count(&l), 0);
    CHECK_EQI(sw_pick(&l, 1), -1);                   /* nothing to pick     */

    /* ---- the bound drops the OLDEST, never the front ------------------- */
    sw_clear(&l);
    for (i = 1; i <= SW_MAX + 5; i++) { sw_touch(&l, i); }
    CHECK_EQI(sw_count(&l), SW_MAX);
    CHECK_EQI(sw_at(&l, 0), SW_MAX + 5);             /* newest kept         */
    CHECK_EQI(sw_at(&l, SW_MAX - 1), 6);             /* 1..5 fell off       */
    /* The window in front is still reachable, which is the whole point. */
    CHECK_EQI(sw_pick(&l, 0), SW_MAX + 5);

    /* ---- one window, and none ------------------------------------------ */
    sw_clear(&l);
    sw_touch(&l, 42);
    CHECK_EQI(sw_pick(&l, 0), 42);
    CHECK_EQI(sw_pick(&l, 1), 42);   /* nowhere else to go                  */
    CHECK_EQI(sw_pick(&l, -1), 42);
    sw_clear(&l);
    CHECK_EQI(sw_count(&l), 0);
    CHECK_EQI(sw_at(&l, 0), -1);

    /* ---- refusals ------------------------------------------------------ */
    CHECK_EQI(sw_count(NULL), 0);
    CHECK_EQI(sw_at(NULL, 0), -1);
    CHECK_EQI(sw_pick(NULL, 1), -1);
    sw_clear(NULL);
    sw_touch(NULL, 1);
    sw_remove(NULL, 1);
    sw_touch(&l, 5);
    CHECK_EQI(sw_at(&l, -1), -1);
    CHECK_EQI(sw_at(&l, 99), -1);
}
