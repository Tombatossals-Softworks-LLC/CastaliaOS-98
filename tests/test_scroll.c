/*
 * test_scroll.c - Scroll bar geometry (ui_scroll.c): thumb size and travel,
 * the minimum-thumb clamp, hit testing across arrows/trough/thumb, and the
 * drag mapping back to a scroll position. Pure integer math, no surface.
 */
#include "ctest.h"
#include "castalia/ui.h"

static void scroll_thumb_basics(void)
{
    int off, len;

    /* Half the content visible -> half-length thumb, parked at the top. */
    ui_scroll_thumb(100, 20, 10, 0, &off, &len);
    CHECK_EQI(len, 50);
    CHECK_EQI(off, 0);

    /* Scrolled to the end -> the thumb sits flush against the bottom. */
    ui_scroll_thumb(100, 20, 10, 10, &off, &len);
    CHECK_EQI(len, 50);
    CHECK_EQI(off, 50);
    CHECK_EQI(off + len, 100);          /* no gap left over at the end */

    /* Halfway. */
    ui_scroll_thumb(100, 20, 10, 5, &off, &len);
    CHECK_EQI(off, 25);

    /* A quarter visible -> a quarter-length thumb. */
    ui_scroll_thumb(200, 40, 10, 0, &off, &len);
    CHECK_EQI(len, 50);
}

static void scroll_thumb_edges(void)
{
    int off, len;

    /* Content that fits: a full-length thumb parked at the top. */
    ui_scroll_thumb(80, 10, 10, 0, &off, &len);
    CHECK_EQI(len, 80);
    CHECK_EQI(off, 0);
    ui_scroll_thumb(80, 10, 40, 0, &off, &len);   /* visible > total */
    CHECK_EQI(len, 80);

    /* Degenerate inputs must not divide by zero or run off the track. */
    ui_scroll_thumb(0, 100, 10, 5, &off, &len);
    CHECK_EQI(len, 0);
    CHECK_EQI(off, 0);
    ui_scroll_thumb(80, 0, 10, 0, &off, &len);
    CHECK_EQI(len, 80);
    ui_scroll_thumb(80, 100, 0, 0, &off, &len);
    CHECK_EQI(len, 80);

    /* A huge document still leaves a grabbable thumb. */
    ui_scroll_thumb(100, 10000, 10, 0, &off, &len);
    CHECK_EQI(len, UI_SB_MIN_THUMB);
    /* ...and it still reaches the bottom exactly. */
    ui_scroll_thumb(100, 10000, 10, 9990, &off, &len);
    CHECK_EQI(off + len, 100);

    /* Out-of-range positions clamp instead of overflowing the track. */
    ui_scroll_thumb(100, 20, 10, -5, &off, &len);
    CHECK_EQI(off, 0);
    ui_scroll_thumb(100, 20, 10, 999, &off, &len);
    CHECK_EQI(off + len, 100);
}

static void scroll_hit_parts(void)
{
    /* A 130px bar: 15px arrows at each end, 100px of track between them. */
    CHECK_EQI(ui_scroll_part(130, 20, 10, 0, 0), UI_SB_LINE_UP);
    CHECK_EQI(ui_scroll_part(130, 20, 10, 0, 14), UI_SB_LINE_UP);
    CHECK_EQI(ui_scroll_part(130, 20, 10, 0, 129), UI_SB_LINE_DOWN);
    CHECK_EQI(ui_scroll_part(130, 20, 10, 0, 115), UI_SB_LINE_DOWN);

    /* Parked at the top: the thumb owns the first half of the track. */
    CHECK_EQI(ui_scroll_part(130, 20, 10, 0, 20), UI_SB_THUMB);
    CHECK_EQI(ui_scroll_part(130, 20, 10, 0, 100), UI_SB_PAGE_DOWN);
    /* Scrolled to the end: the trough above it pages up. */
    CHECK_EQI(ui_scroll_part(130, 20, 10, 10, 20), UI_SB_PAGE_UP);
    CHECK_EQI(ui_scroll_part(130, 20, 10, 10, 100), UI_SB_THUMB);

    /* Off the bar, and content that needs no bar. */
    CHECK_EQI(ui_scroll_part(130, 20, 10, 0, -1), UI_SB_NONE);
    CHECK_EQI(ui_scroll_part(130, 20, 10, 0, 130), UI_SB_NONE);
    CHECK_EQI(ui_scroll_part(130, 10, 10, 0, 60), UI_SB_NONE);

    /* A bar too short for arrows is all track. */
    CHECK_EQI(ui_scroll_part(30, 20, 10, 0, 2), UI_SB_THUMB);
}

static void scroll_drag(void)
{
    int pos;
    /* Dragging to the very top / bottom of a 130px bar. */
    pos = ui_scroll_pos_from_coord(130, 20, 10, 15);
    CHECK_EQI(pos, 0);
    pos = ui_scroll_pos_from_coord(130, 20, 10, 115);
    CHECK_EQI(pos, 10);
    /* The middle of the track lands mid-document. */
    pos = ui_scroll_pos_from_coord(130, 20, 10, 65);
    CHECK_EQI(pos, 5);
    /* Past either end clamps. */
    CHECK_EQI(ui_scroll_pos_from_coord(130, 20, 10, -50), 0);
    CHECK_EQI(ui_scroll_pos_from_coord(130, 20, 10, 999), 10);
    /* Nothing to scroll -> always zero. */
    CHECK_EQI(ui_scroll_pos_from_coord(130, 10, 10, 60), 0);

    /* Round trip: a position -> its thumb -> back again. The bar quantizes to
     * pixels, so the tolerance is one pixel's worth of the range (here a 12px
     * thumb travels 88px to cover 475 units -- about 5.4 units per pixel). */
    {
        int off, len, back, step;
        ui_scroll_thumb(100, 500, 25, 300, &off, &len);
        step = (500 - 25) / (100 - len) + 1;
        back = ui_scroll_pos_from_coord(130, 500, 25, 15 + off + len / 2);
        CHECK(back >= 300 - step && back <= 300 + step);
    }
}

/*
 * The wheel.
 *
 * One rule shared by every scrollable window in the system, so the thing
 * worth pinning is not "three lines" -- that is a constant anybody can read
 * -- but the boundaries: what happens at the ends, what happens to a list
 * that fits, and whether the caller is told the truth about having something
 * to repaint. A wheel turned at the bottom of a document that answered CTRUE
 * would cost a frame per notch forever.
 */
static void scroll_wheel(void)
{
    int pos;

    /* A notch away from the user scrolls the content up: the first visible
     * line moves back toward the start, by UI_WHEEL_LINES. */
    pos = 20;
    CHECK(ui_scroll_wheel(&pos, 1, 100, 10) == CTRUE);
    CHECK_EQI(pos, 20 - UI_WHEEL_LINES);
    CHECK(ui_scroll_wheel(&pos, -1, 100, 10) == CTRUE);
    CHECK_EQI(pos, 20);                       /* and exactly back again */

    /* Several notches at once -- a fast flick reports more than one. */
    pos = 40;
    CHECK(ui_scroll_wheel(&pos, -3, 100, 10) == CTRUE);
    CHECK_EQI(pos, 40 + 3 * UI_WHEEL_LINES);

    /* The top: clamped, and the LAST notch that moves still answers CTRUE
     * while the one after it does not. */
    pos = 2;
    CHECK(ui_scroll_wheel(&pos, 1, 100, 10) == CTRUE);
    CHECK_EQI(pos, 0);
    CHECK(ui_scroll_wheel(&pos, 1, 100, 10) == CFALSE);
    CHECK_EQI(pos, 0);

    /*
     * The bottom clamps to total - visible, NOT to total. Scrolling to
     * 'total' leaves the last screenful hanging off the bottom of the
     * window with blank rows under it, which is the mistake nine separate
     * copies of this rule would have made at least once.
     */
    pos = 100 - 10 - 1;
    CHECK(ui_scroll_wheel(&pos, -1, 100, 10) == CTRUE);
    CHECK_EQI(pos, 100 - 10);
    CHECK(ui_scroll_wheel(&pos, -1, 100, 10) == CFALSE);
    CHECK_EQI(pos, 100 - 10);

    /* Content that FITS does not scroll, and is not moved to zero either:
     * a caller may legitimately be showing a list from a nonzero offset
     * while it is being rebuilt. */
    pos = 4;
    CHECK(ui_scroll_wheel(&pos, -1, 8, 8) == CFALSE);
    CHECK_EQI(pos, 4);
    CHECK(ui_scroll_wheel(&pos, -1, 3, 8) == CFALSE);
    CHECK_EQI(pos, 4);

    /* Nonsense in, nothing out -- including a NULL position, which is what
     * a window with no list yet would pass. */
    pos = 7;
    CHECK(ui_scroll_wheel(&pos, 0, 100, 10) == CFALSE);
    CHECK_EQI(pos, 7);
    CHECK(ui_scroll_wheel(&pos, 1, 0, 10) == CFALSE);
    CHECK(ui_scroll_wheel(&pos, 1, 100, 0) == CFALSE);
    CHECK(ui_scroll_wheel(&pos, 1, -5, -5) == CFALSE);
    CHECK_EQI(pos, 7);
    CHECK(ui_scroll_wheel(NULL, 1, 100, 10) == CFALSE);

    /*
     * A flick bigger than the whole document lands at the end rather than
     * somewhere past it -- and answers CTRUE, because it did move.
     */
    pos = 5;
    CHECK(ui_scroll_wheel(&pos, -9999, 100, 10) == CTRUE);
    CHECK_EQI(pos, 90);
    CHECK(ui_scroll_wheel(&pos, 9999, 100, 10) == CTRUE);
    CHECK_EQI(pos, 0);
}

void test_scroll(void)
{
    printf("- scroll\n");
    scroll_thumb_basics();
    scroll_thumb_edges();
    scroll_hit_parts();
    scroll_drag();
    scroll_wheel();
}
