/*
 * ui_hotpaint.c - Repainting what the pointer actually changed.
 *
 * Every window carrying the hover treatment called wm_invalidate(win, NULL)
 * on a transition, so sweeping the pointer across a toolbar cost one full
 * window repaint per button it passed over -- 112% of the client, per button,
 * measured on the Control Centre. The two buttons whose drawing changed are
 * the one the pointer left and the one it arrived on, and that is all that
 * has to come back.
 *
 * This lives in its own file rather than beside the rest of UiHot in
 * ui_button.c for one reason: ui_button.c is in TEST_CORE_SRC, the hermetic
 * link tests/run_tests is built from, and a call to wm_invalidate would drag
 * the window manager into it. The arithmetic of "which button is hot" stays
 * there, pure and unit-tested; the part that needs a window is here.
 */
#include "castalia/ui.h"
#include "castalia/wm.h"

void ui_hot_repaint(WmWindow *win, const UiHot *h, const CRect *rects, int n)
{
    CPoint o;
    int which[2], k;
    if (win == NULL || h == NULL || rects == NULL) { return; }
    o = wm_client_origin(win);
    which[0] = ui_hot_left(h);
    which[1] = ui_hot_hot(h);
    for (k = 0; k < 2; k++) {
        CRect r;
        if (which[k] < 0 || which[k] >= n) { continue; }
        /*
         * Two pixels out. A button carries marks that sit OUTSIDE the
         * rectangle it is drawn in -- the default ring is two pixels beyond
         * its bevel -- and a region that stopped at the rectangle would
         * leave them behind. CastaliaWrite's caret cost an afternoon for
         * exactly this, one pixel at a time.
         */
        r = crect_inset(&rects[which[k]], -2);
        r = crect_offset(&r, o.x, o.y);
        wm_invalidate(win, &r);
    }
}

/*
 * The same idea one level up: an open drop-down highlights the row under the
 * pointer, and sliding down a six-item File menu repainted the editor six
 * times. The menu is drawn OVER the window, so the row that lit and the row
 * that went dark are the only two rectangles on the screen that differ --
 * and both are inside the drop-down, which is a fraction of the window it
 * covers.
 *
 * (mx,my) is the menu's origin in CLIENT coordinates, which is how every
 * caller already stores it; the offset to screen happens here, once, rather
 * than in each of the six windows that open a menu.
 */
void ui_menu_repaint(WmWindow *win, const UiMenu *m, int mx, int my,
                     GfxFontId font, int prev, int now)
{
    CPoint o;
    int which[2], k;
    if (win == NULL || m == NULL) { return; }
    o = wm_client_origin(win);
    which[0] = prev;
    which[1] = now;
    for (k = 0; k < 2; k++) {
        CRect r;
        if (!ui_menu_row_rect(m, mx, my, font, which[k], &r)) { continue; }
        /* One pixel out for the selection's own frame, which is drawn on the
         * rectangle's edge rather than inside it. */
        r = crect_inset(&r, -1);
        r = crect_offset(&r, o.x, o.y);
        wm_invalidate(win, &r);
    }
}
