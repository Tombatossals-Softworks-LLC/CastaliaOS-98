/*
 * test_rect.c - Rectangle math: intersect, union, overlap, contains, inset.
 */
#include "ctest.h"
#include "castalia/rect.h"

void test_rect(void)
{
    CRect a = crect_make(10, 10, 20, 20);   /* 10,10 .. 30,30 */
    CRect b = crect_make(20, 20, 20, 20);   /* 20,20 .. 40,40 */
    CRect r;

    printf("- rect\n");

    CHECK_EQI(crect_w(&a), 20);
    CHECK_EQI(crect_h(&a), 20);
    CHECK(!crect_empty(&a));
    CHECK(crect_contains(&a, 10, 10));
    CHECK(!crect_contains(&a, 30, 30));  /* exclusive edge */
    CHECK(crect_contains(&a, 29, 29));

    r = crect_intersect(&a, &b);
    CHECK_EQI(r.x0, 20); CHECK_EQI(r.y0, 20);
    CHECK_EQI(r.x1, 30); CHECK_EQI(r.y1, 30);
    CHECK_EQI(crect_w(&r), 10);

    CHECK(crect_overlaps(&a, &b));

    r = crect_union(&a, &b);
    CHECK_EQI(r.x0, 10); CHECK_EQI(r.y0, 10);
    CHECK_EQI(r.x1, 40); CHECK_EQI(r.y1, 40);

    /* Non-overlapping rects intersect to empty. */
    {
        CRect c = crect_make(100, 100, 5, 5);
        CRect e = crect_intersect(&a, &c);
        CHECK(crect_empty(&e));
        CHECK(!crect_overlaps(&a, &c));
    }

    /* inset shrinks; over-inset clamps to empty, not negative. */
    r = crect_inset(&a, 5);
    CHECK_EQI(crect_w(&r), 10);
    r = crect_inset(&a, 100);
    CHECK(crect_empty(&r));

    /* offset translates. */
    r = crect_offset(&a, 5, -3);
    CHECK_EQI(r.x0, 15); CHECK_EQI(r.y0, 7);
}
