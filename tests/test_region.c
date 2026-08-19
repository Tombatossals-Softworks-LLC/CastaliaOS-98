/*
 * test_region.c - Dirty-region accumulator: merge, containment, overflow
 * coalescing (correctness: never drop a rectangle).
 */
#include "ctest.h"
#include "castalia/rect.h"

void test_region(void)
{
    CRegion rgn;
    CRect a = crect_make(0, 0, 10, 10);
    CRect b = crect_make(100, 100, 10, 10);
    CRect inside = crect_make(2, 2, 3, 3);
    CRect bounds;
    int i;

    printf("- region\n");

    cregion_clear(&rgn);
    CHECK(cregion_is_empty(&rgn));

    cregion_add(&rgn, &a);
    CHECK(!cregion_is_empty(&rgn));
    CHECK_EQI(rgn.count, 1);

    /* A rect fully inside an existing one is absorbed (no growth). */
    cregion_add(&rgn, &inside);
    CHECK_EQI(rgn.count, 1);

    /* A distant rect is a separate entry. */
    cregion_add(&rgn, &b);
    CHECK_EQI(rgn.count, 2);

    /* Empty rects are ignored. */
    {
        CRect empty = crect_make(5, 5, 0, 0);
        cregion_add(&rgn, &empty);
        CHECK_EQI(rgn.count, 2);
    }

    bounds = cregion_bounds(&rgn);
    CHECK_EQI(bounds.x0, 0);
    CHECK_EQI(bounds.y0, 0);
    CHECK_EQI(bounds.x1, 110);
    CHECK_EQI(bounds.y1, 110);

    /* Overflow: add many disjoint rects; the region must coalesce to a
     * single bounding box rather than drop any. */
    cregion_clear(&rgn);
    for (i = 0; i < CRGN_MAX + 20; i++) {
        CRect r = crect_make(i * 4, i * 4, 2, 2);
        cregion_add(&rgn, &r);
    }
    CHECK(rgn.count <= CRGN_MAX);
    bounds = cregion_bounds(&rgn);
    /* The very last rect must still be covered. */
    {
        int last = (CRGN_MAX + 20 - 1) * 4;
        CHECK(bounds.x1 >= last + 2);
        CHECK(bounds.y1 >= last + 2);
    }

    /*
     * cregion_disjoin_over: the region is ALLOWED to hold overlapping
     * rectangles, and a painter that reads what is already on the buffer --
     * the popups' drop shadow -- cannot survive one. Two long thin crossing
     * rects are the shape cregion_add declines to merge, because their union
     * is enormous next to their areas, and the shape that overlaps.
     */
    {
        CRect horiz = crect_make(0, 100, 200, 6);
        CRect vert  = crect_make(100, 0, 6, 200);
        CRect cross, elsewhere;

        cregion_clear(&rgn);
        cregion_add(&rgn, &horiz);
        cregion_add(&rgn, &vert);
        /* The premise: they were kept apart, and they do overlap. */
        CHECK_EQI(rgn.count, 2);
        CHECK(crect_overlaps(&rgn.rects[0], &rgn.rects[1]));

        /* An area away from the crossing leaves the region exactly alone. */
        elsewhere = crect_make(400, 400, 20, 20);
        cregion_disjoin_over(&rgn, &elsewhere);
        CHECK_EQI(rgn.count, 2);

        /* An area over the crossing merges them. */
        cross = crect_make(98, 98, 10, 10);
        cregion_disjoin_over(&rgn, &cross);
        CHECK_EQI(rgn.count, 1);
        /* And merging never loses coverage. */
        bounds = cregion_bounds(&rgn);
        CHECK(bounds.x0 <= 0 && bounds.y0 <= 0);
        CHECK(bounds.x1 >= 200 && bounds.y1 >= 200);
    }

    /*
     * A rectangle that does NOT touch the area is left alone even when it
     * overlaps one that does. Merging it in would be safe but wasteful, and
     * the point of asking about an area is to pay only where it matters.
     */
    {
        CRect over_a = crect_make(0, 100, 200, 6);
        CRect over_b = crect_make(100, 0, 6, 200);
        CRect far_x  = crect_make(0, 98, 40, 40);   /* overlaps over_a only  */
        CRect cross  = crect_make(100, 100, 6, 6);  /* where the two cross   */

        cregion_clear(&rgn);
        cregion_add(&rgn, &over_a);
        cregion_add(&rgn, &over_b);
        cregion_add(&rgn, &far_x);
        CHECK_EQI(rgn.count, 3);
        cregion_disjoin_over(&rgn, &cross);
        /* over_a and over_b merged; far_x, which the area never touched,
         * survives as its own rectangle. */
        CHECK_EQI(rgn.count, 2);
    }

    /*
     * Three that all overlap in the area collapse to one, not to two: a merge
     * can create a rectangle that overlaps a third, so one pass is not enough
     * and the loop has to run to a fixed point.
     */
    {
        CRect r1 = crect_make(0, 0, 100, 10);
        CRect r2 = crect_make(90, 0, 10, 100);
        CRect r3 = crect_make(90, 90, 100, 10);
        CRect area = crect_make(0, 0, 200, 200);
        cregion_clear(&rgn);
        cregion_add(&rgn, &r1);
        cregion_add(&rgn, &r2);
        cregion_add(&rgn, &r3);
        cregion_disjoin_over(&rgn, &area);
        CHECK_EQI(rgn.count, 1);
    }

    /* Nothing to do on an empty region, and no crash on a null area. */
    {
        CRect area = crect_make(0, 0, 10, 10);
        cregion_clear(&rgn);
        cregion_disjoin_over(&rgn, &area);
        CHECK_EQI(rgn.count, 0);
        cregion_disjoin_over(&rgn, (const CRect *)0);
        CHECK_EQI(rgn.count, 0);
    }
}
