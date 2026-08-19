/*
 * test_snap.c - Host unit tests for window-snap geometry (wm_snap.c).
 */
#include "ctest.h"
#include "castalia/wm.h"

void test_snap(void)
{
    CRect work = crect_make(0, 0, 800, 570);   /* full width minus taskbar */
    CRect r;

    /* ---- zones ---- */
    CHECK_EQI(wm_snap_zone(&work, 5, 300, WM_SNAP_EDGE),   WM_SNAP_LEFT);
    CHECK_EQI(wm_snap_zone(&work, 795, 300, WM_SNAP_EDGE), WM_SNAP_RIGHT);
    CHECK_EQI(wm_snap_zone(&work, 400, 4, WM_SNAP_EDGE),   WM_SNAP_TOP);
    CHECK_EQI(wm_snap_zone(&work, 400, 300, WM_SNAP_EDGE), WM_SNAP_NONE);
    /* a top corner tiles to a half (vertical edge wins over the top) */
    CHECK_EQI(wm_snap_zone(&work, 3, 3, WM_SNAP_EDGE),     WM_SNAP_LEFT);

    /* ---- rects ---- */
    r = wm_snap_rect(&work, WM_SNAP_LEFT);
    CHECK_EQI(r.x0, 0); CHECK_EQI(r.y0, 0);
    CHECK_EQI(crect_w(&r), 400); CHECK_EQI(crect_h(&r), 570);
    r = wm_snap_rect(&work, WM_SNAP_RIGHT);
    CHECK_EQI(r.x0, 400); CHECK_EQI(crect_w(&r), 400); CHECK_EQI(crect_h(&r), 570);
    r = wm_snap_rect(&work, WM_SNAP_TOP);
    CHECK_EQI(crect_w(&r), 800); CHECK_EQI(crect_h(&r), 570);

    /* odd width tiles with no gap and no overlap */
    {
        CRect w2 = crect_make(0, 0, 801, 570);
        CRect a = wm_snap_rect(&w2, WM_SNAP_LEFT);
        CRect b = wm_snap_rect(&w2, WM_SNAP_RIGHT);
        CHECK_EQI(crect_w(&a) + crect_w(&b), 801);
        CHECK_EQI(b.x0, a.x0 + crect_w(&a));
    }

    /* work area with a non-zero origin */
    {
        CRect w3 = crect_make(10, 20, 400, 300);
        CHECK_EQI(wm_snap_zone(&w3, 12, 100, WM_SNAP_EDGE), WM_SNAP_LEFT);
        CHECK_EQI(wm_snap_zone(&w3, 100, 22, WM_SNAP_EDGE), WM_SNAP_TOP);
        r = wm_snap_rect(&w3, WM_SNAP_RIGHT);
        CHECK_EQI(r.x0, 210); CHECK_EQI(r.y0, 20);
    }

    /* NULL work area is safe. */
    CHECK_EQI(wm_snap_zone(NULL, 0, 0, WM_SNAP_EDGE), WM_SNAP_NONE);
}
