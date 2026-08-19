/*
 * ui_scroll.c - Scroll bars (Layer 3 common control).
 *
 * The geometry is deliberately split out as pure integer math
 * (ui_scroll_thumb / ui_scroll_part) so the host unit tests can pin the
 * fiddly cases -- an empty range, content that fits, a thumb clamped to a
 * minimum length, and the rounding at the very end of the range -- without a
 * surface (tests/test_scroll.c). Drawing is a thin shell over them.
 */
#include "castalia/ui.h"
#include "castalia/plat.h"

void ui_scroll_thumb(int track_len, int total, int visible, int pos,
                     int *out_off, int *out_len)
{
    int len, off, span, range;
    if (out_off != NULL) { *out_off = 0; }
    if (out_len != NULL) { *out_len = (track_len > 0) ? track_len : 0; }
    if (track_len <= 0) { return; }
    if (total <= 0 || visible <= 0 || visible >= total) { return; }

    /* Proportional, but never so small it cannot be grabbed. */
    len = (int)(((long)track_len * visible) / total);
    if (len < UI_SB_MIN_THUMB) { len = UI_SB_MIN_THUMB; }
    if (len > track_len) { len = track_len; }

    range = total - visible;           /* the largest legal 'pos'        */
    if (pos < 0) { pos = 0; }
    if (pos > range) { pos = range; }
    span = track_len - len;            /* travel available to the thumb  */
    off = (range > 0) ? (int)(((long)span * pos) / range) : 0;
    if (off < 0) { off = 0; }
    if (off > span) { off = span; }

    if (out_off != NULL) { *out_off = off; }
    if (out_len != NULL) { *out_len = len; }
}

int ui_scroll_part(int bar_len, int total, int visible, int pos, int coord)
{
    int track, off, len;
    if (bar_len <= 0) { return UI_SB_NONE; }
    if (coord < 0 || coord >= bar_len) { return UI_SB_NONE; }

    /* Two arrow buttons eat the ends; a very short bar has none. */
    if (bar_len >= UI_SB_W * 3) {
        if (coord < UI_SB_W) { return UI_SB_LINE_UP; }
        if (coord >= bar_len - UI_SB_W) { return UI_SB_LINE_DOWN; }
        track = bar_len - UI_SB_W * 2;
        coord -= UI_SB_W;
    } else {
        track = bar_len;
    }
    if (total <= 0 || visible <= 0 || visible >= total) { return UI_SB_NONE; }

    ui_scroll_thumb(track, total, visible, pos, &off, &len);
    if (coord < off) { return UI_SB_PAGE_UP; }
    if (coord >= off + len) { return UI_SB_PAGE_DOWN; }
    return UI_SB_THUMB;
}

cbool ui_scroll_wheel(int *pos, int notches, int total, int visible)
{
    int max, want;
    if (pos == NULL || notches == 0) { return CFALSE; }
    /*
     * Nothing to scroll is not the same as scrolling to zero: a list that
     * fits its window must stay where it is, and a caller that passed a
     * nonsense total must not be handed a position it will index with.
     */
    if (total <= 0 || visible <= 0) { return CFALSE; }
    max = total - visible;
    if (max <= 0) { return CFALSE; }

    /* Positive notches are away from the user: the content goes up, so the
     * first visible line goes back toward the start. */
    want = *pos - notches * UI_WHEEL_LINES;
    if (want < 0) { want = 0; }
    if (want > max) { want = max; }
    if (want == *pos) { return CFALSE; }
    *pos = want;
    return CTRUE;
}

int ui_scroll_pos_from_coord(int bar_len, int total, int visible, int coord)
{
    int track, len, span;
    if (bar_len <= 0 || total <= 0 || visible <= 0 || visible >= total) {
        return 0;
    }
    if (bar_len >= UI_SB_W * 3) {
        track = bar_len - UI_SB_W * 2;
        coord -= UI_SB_W;
    } else {
        track = bar_len;
    }
    ui_scroll_thumb(track, total, visible, 0, NULL, &len);
    span = track - len;
    coord -= len / 2;                       /* grab the thumb by its middle */
    if (span <= 0) { return 0; }
    if (coord < 0) { coord = 0; }
    if (coord > span) { coord = span; }
    return (int)(((long)coord * (total - visible)) / span);
}

/* ---- drawing ---------------------------------------------------------- */
/* A small arrow glyph pointing up/down/left/right inside 'r'. */
static void sb_arrow(GfxSurface *s, const CRect *r, int dir, CColor col)
{
    int cx = (r->x0 + r->x1) / 2, cy = (r->y0 + r->y1) / 2, i;
    for (i = 0; i < 4; i++) {
        switch (dir) {
        case 0: gfx_hline(s, cx - i, cy - 2 + i, i * 2 + 1, col); break; /* up   */
        case 1: gfx_hline(s, cx - i, cy + 2 - i, i * 2 + 1, col); break; /* down */
        case 2: gfx_vline(s, cx - 2 + i, cy - i, i * 2 + 1, col); break; /* left */
        default: gfx_vline(s, cx + 2 - i, cy - i, i * 2 + 1, col); break;
        }
    }
}

void ui_scrollbar_draw(GfxSurface *s, const CRect *r, cbool vertical,
                       int total, int visible, int pos, int hot_part)
{
    const UiPalette *p = ui_palette();
    CColor trough = gfx_tint(p->face, 0xFFFFFF, 60);
    int bar_len = vertical ? crect_h(r) : crect_w(r);
    int thick   = vertical ? crect_w(r) : crect_h(r);
    int track_len, off, len;
    CRect part;

    if (s == NULL || r == NULL || bar_len <= 0 || thick <= 0) { return; }
    gfx_fill_rect(s, r, trough);

    if (bar_len >= UI_SB_W * 3) {
        /* the two end buttons */
        part = vertical ? crect_make(r->x0, r->y0, thick, UI_SB_W)
                        : crect_make(r->x0, r->y0, UI_SB_W, thick);
        ui_draw_button(s, &part, "",
                       (hot_part == UI_SB_LINE_UP) ? UI_BTN_PRESSED : UI_BTN_NORMAL);
        sb_arrow(s, &part, vertical ? 0 : 2, p->text);

        part = vertical ? crect_make(r->x0, r->y1 - UI_SB_W, thick, UI_SB_W)
                        : crect_make(r->x1 - UI_SB_W, r->y0, UI_SB_W, thick);
        ui_draw_button(s, &part, "",
                       (hot_part == UI_SB_LINE_DOWN) ? UI_BTN_PRESSED : UI_BTN_NORMAL);
        sb_arrow(s, &part, vertical ? 1 : 3, p->text);

        track_len = bar_len - UI_SB_W * 2;
    } else {
        track_len = bar_len;
    }

    /* Content that fits still shows a full-length thumb -- an empty trough
     * reads as broken, and ui_scroll_thumb already returns that shape. */
    ui_scroll_thumb(track_len, total, visible, pos, &off, &len);
    if (len <= 0) { return; }
    if (bar_len >= UI_SB_W * 3) { off += UI_SB_W; }
    part = vertical ? crect_make(r->x0, r->y0 + off, thick, len)
                    : crect_make(r->x0 + off, r->y0, len, thick);
    ui_draw_button(s, &part, "",
                   (hot_part == UI_SB_THUMB) ? UI_BTN_HOVER : UI_BTN_NORMAL);
}

int ui_scrollbar_hit(const CRect *r, cbool vertical,
                     int total, int visible, int pos, int px, int py)
{
    if (r == NULL || !crect_contains(r, px, py)) { return UI_SB_NONE; }
    return ui_scroll_part(vertical ? crect_h(r) : crect_w(r), total, visible,
                          pos, vertical ? (py - r->y0) : (px - r->x0));
}
