/*
 * wm_snap.c - Window-snap geometry (pure, host-tested).
 *
 * Given a work area and a pointer position, decide which edge (if any) the
 * pointer is gripping and what frame the window should take. Kept free of
 * manager state so it is unit-tested in tests/test_snap.c; wm_dispatch.c applies
 * the result to the dragged window.
 */
#include "castalia/wm.h"

WmSnapZone wm_snap_zone(const CRect *work, int px, int py, int edge)
{
    if (work == NULL) { return WM_SNAP_NONE; }
    /* Vertical edges win over the top edge, so a top corner tiles to a half
     * rather than maximizing. */
    if (px <= work->x0 + edge) { return WM_SNAP_LEFT; }
    if (px >= work->x1 - edge) { return WM_SNAP_RIGHT; }
    if (py <= work->y0 + edge) { return WM_SNAP_TOP; }
    return WM_SNAP_NONE;
}

CRect wm_snap_rect(const CRect *work, WmSnapZone zone)
{
    int w, h, hw;
    if (work == NULL) { return crect_make(0, 0, 0, 0); }
    w = crect_w(work);
    h = crect_h(work);
    hw = w / 2;
    switch (zone) {
    case WM_SNAP_LEFT:  return crect_make(work->x0, work->y0, hw, h);
    case WM_SNAP_RIGHT: return crect_make(work->x0 + hw, work->y0, w - hw, h);
    case WM_SNAP_TOP:   return *work;
    default:            return *work;
    }
}
