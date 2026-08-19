/*
 * wm_move.c - Where a window is allowed to end up (see wm_move.h).
 *
 * No manager state, no globals, no drawing: rectangles in, rectangles out.
 */
#include "wm_move.h"

CRect wm_move_clamp(const CRect *f, int nx, int ny, int sw, int sh)
{
    int w, h;
    if (f == NULL) { return crect_make(0, 0, 0, 0); }
    w = crect_w(f);
    h = crect_h(f);
    /* Horizontally the window may hang off either edge, as long as WM_KEEP_X
     * of it is still over the screen to grab. */
    if (nx < WM_KEEP_X - w) { nx = WM_KEEP_X - w; }
    if (nx > sw - WM_KEEP_X) { nx = sw - WM_KEEP_X; }
    /* Vertically it may not go above the top at all -- the title bar is the
     * only handle, and above the top edge there is nothing left to grab. */
    if (ny < 0) { ny = 0; }
    if (ny > sh - WM_KEEP_Y) { ny = sh - WM_KEEP_Y; }
    return crect_make(nx, ny, w, h);
}

CRect wm_size_clamp(const CRect *f, int nw, int nh, int sw, int sh)
{
    int room_w, room_h;
    if (f == NULL) { return crect_make(0, 0, 0, 0); }
    /* What is left of the screen from where the window starts. A window whose
     * left edge is off-screen has more room than the arithmetic suggests, so
     * measure from the visible part. */
    room_w = sw - (f->x0 > 0 ? f->x0 : 0);
    room_h = sh - (f->y0 > 0 ? f->y0 : 0);
    if (nw > room_w) { nw = room_w; }
    if (nh > room_h) { nh = room_h; }
    /* The minimum is applied LAST, so a window pinned against the right edge
     * still comes back usable rather than one pixel wide. */
    if (nw < WM_MIN_W) { nw = WM_MIN_W; }
    if (nh < WM_MIN_H) { nh = WM_MIN_H; }
    return crect_make(f->x0, f->y0, nw, nh);
}
