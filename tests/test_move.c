/*
 * test_move.c - Where a window is allowed to end up (wm_move.c).
 *
 * The interesting cases are all at the edges, which is exactly where a mouse
 * drag is hard to aim and a keyboard walk arrives every time: hold an arrow
 * down and the window WILL reach the boundary, so the boundary has to be right
 * rather than merely unreached.
 *
 * The one invariant running through the move checks is that the size never
 * changes. A clamp that quietly resizes the window it was asked to move is the
 * failure mode this is easiest to write by accident -- crect_make(x0,y0,x1,y1)
 * where crect_make(x,y,w,h) was meant -- and it is invisible until a window
 * shrinks a little every time it is pushed against an edge.
 */
#include "ctest.h"
#include "../src/wm/wm_move.h"

#define SW 800
#define SH 600

/* Move 'f' to (nx,ny) and require the result to be exactly (x,y,w,h). */
static void moves_to(const CRect *f, int nx, int ny, int x, int y)
{
    CRect r = wm_move_clamp(f, nx, ny, SW, SH);
    CHECK_EQI(r.x0, x);
    CHECK_EQI(r.y0, y);
    /* Same size, always. */
    CHECK_EQI(crect_w(&r), crect_w(f));
    CHECK_EQI(crect_h(&r), crect_h(f));
}

void test_move(void)
{
    CRect f = crect_make(100, 100, 300, 200);
    CRect r;

    printf("- window placement\n");

    /* ---- moving ------------------------------------------------------- */
    /* Somewhere in the middle: taken as asked. */
    moves_to(&f, 250, 180, 250, 180);
    moves_to(&f, 0, 0, 0, 0);

    /* Off the top is refused outright -- the title bar is the only handle,
     * and above the screen there is nothing left to grab. */
    moves_to(&f, 250, -1, 250, 0);
    moves_to(&f, 250, -5000, 250, 0);

    /* Off the left and right: allowed, up to the point where WM_KEEP_X of the
     * window is still over the screen. */
    moves_to(&f, -260, 100, WM_KEEP_X - 300, 100);
    moves_to(&f, -5000, 100, WM_KEEP_X - 300, 100);
    moves_to(&f, 5000, 100, SW - WM_KEEP_X, 100);
    /* Just inside the limit is untouched, so the clamp is not off by one. */
    moves_to(&f, SW - WM_KEEP_X - 1, 100, SW - WM_KEEP_X - 1, 100);
    moves_to(&f, WM_KEEP_X - 300 + 1, 100, WM_KEEP_X - 300 + 1, 100);

    /* Off the bottom: WM_KEEP_Y rows of title bar stay visible. */
    moves_to(&f, 100, 5000, 100, SH - WM_KEEP_Y);
    moves_to(&f, 100, SH - WM_KEEP_Y - 1, 100, SH - WM_KEEP_Y - 1);

    /* A window wider than the screen can still be positioned, and the keep
     * margin is measured against ITS width, not the screen's. */
    {
        CRect wide = crect_make(0, 0, 1200, 300);
        moves_to(&wide, -5000, 0, WM_KEEP_X - 1200, 0);
        moves_to(&wide, 5000, 0, SW - WM_KEEP_X, 0);
    }

    /* ---- resizing ----------------------------------------------------- */
    /* An ordinary resize keeps the top-left corner exactly where it was. */
    r = wm_size_clamp(&f, 400, 250, SW, SH);
    CHECK_EQI(r.x0, 100); CHECK_EQI(r.y0, 100);
    CHECK_EQI(crect_w(&r), 400);
    CHECK_EQI(crect_h(&r), 250);

    /* Too small is raised to the minimum, not refused. */
    r = wm_size_clamp(&f, 1, 1, SW, SH);
    CHECK_EQI(crect_w(&r), WM_MIN_W);
    CHECK_EQI(crect_h(&r), WM_MIN_H);
    r = wm_size_clamp(&f, -9999, -9999, SW, SH);
    CHECK_EQI(crect_w(&r), WM_MIN_W);
    CHECK_EQI(crect_h(&r), WM_MIN_H);
    /* One below the minimum, which is where an off-by-one would hide. */
    r = wm_size_clamp(&f, WM_MIN_W - 1, WM_MIN_H - 1, SW, SH);
    CHECK_EQI(crect_w(&r), WM_MIN_W);
    CHECK_EQI(crect_h(&r), WM_MIN_H);
    r = wm_size_clamp(&f, WM_MIN_W, WM_MIN_H, SW, SH);
    CHECK_EQI(crect_w(&r), WM_MIN_W);
    CHECK_EQI(crect_h(&r), WM_MIN_H);

    /* Too big is capped to what is left of the screen from where it starts. */
    r = wm_size_clamp(&f, 5000, 5000, SW, SH);
    CHECK_EQI(crect_w(&r), SW - 100);
    CHECK_EQI(crect_h(&r), SH - 100);
    CHECK_EQI(r.x1, SW);
    CHECK_EQI(r.y1, SH);

    /* A window pinned against the right edge has less room than the minimum.
     * The minimum wins: refusing to shrink further is a limit, handing back a
     * window four pixels wide is a bug. */
    {
        CRect pinned = crect_make(SW - 4, SH - 4, 200, 200);
        r = wm_size_clamp(&pinned, 200, 200, SW, SH);
        CHECK_EQI(crect_w(&r), WM_MIN_W);
        CHECK_EQI(crect_h(&r), WM_MIN_H);
        CHECK_EQI(r.x0, SW - 4);   /* and it did not move to make room */
    }

    /* A window whose left edge is off-screen measures its room from the edge
     * of the screen, not from its own negative corner -- otherwise the cap
     * would GROW as the window was pushed further off. */
    {
        CRect off = crect_make(-200, -50, 300, 200);
        r = wm_size_clamp(&off, 5000, 5000, SW, SH);
        CHECK_EQI(crect_w(&r), SW);
        CHECK_EQI(crect_h(&r), SH);
    }

    /* ---- refusals ----------------------------------------------------- */
    r = wm_move_clamp(NULL, 10, 10, SW, SH);
    CHECK(crect_empty(&r));
    r = wm_size_clamp(NULL, 10, 10, SW, SH);
    CHECK(crect_empty(&r));
}
