/*
 * card_draw.c - How a playing card is drawn (see card_draw.h).
 *
 * Lifted verbatim out of app_solitaire.c, where it was a set of statics that
 * FreeCell could not reach. The only edits are the four names and their
 * linkage; the artwork is untouched, and Solitaire renders byte for byte the
 * same afterwards, which is the check that says so.
 */
#include "card_draw.h"
#include "castalia/ui.h"

const char *card_rank_str(int rank)
{
    static const char *names[14] = {
        "", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"
    };
    if (rank < 1 || rank > 13) { return "?"; }
    return names[rank];
}

/* LCG (project convention: deterministic, integer-only). */

/* ---- suit pips (primitives only) -------------------------------------- */
static void sol_diamond(GfxSurface *s, int cx, int cy, int r, CColor col)
{
    int y, half;
    for (y = -r; y <= r; y++) {
        half = r - ((y < 0) ? -y : y);
        gfx_hline(s, cx - half, cy + y, 2 * half + 1, col);
    }
}

static void sol_heart(GfxSurface *s, int cx, int cy, int r, CColor col)
{
    int rr = (r + 1) / 2;
    int topy = cy - r + rr;
    int by = cy + r;
    int span = by - topy;
    int y, half;
    if (span < 1) { span = 1; }
    gfx_fill_circle(s, cx - rr, topy, rr, col);
    gfx_fill_circle(s, cx + rr, topy, rr, col);
    for (y = topy; y <= by; y++) {
        half = r * (by - y) / span;
        gfx_hline(s, cx - half, y, 2 * half + 1, col);
    }
}

static void sol_spade(GfxSurface *s, int cx, int cy, int r, CColor col)
{
    int rr = (r + 1) / 2;
    int apex = cy - r;
    int base = cy + r - rr - 1;
    int span = base - apex;
    int y, half;
    CRect st;
    if (span < 1) { span = 1; }
    for (y = apex; y <= base; y++) {
        half = r * (y - apex) / span;
        gfx_hline(s, cx - half, y, 2 * half + 1, col);
    }
    gfx_fill_circle(s, cx - rr, base, rr, col);
    gfx_fill_circle(s, cx + rr, base, rr, col);
    st = crect_make(cx - 1, cy, 3, r + 1);
    gfx_fill_rect(s, &st, col);
}

static void sol_club(GfxSurface *s, int cx, int cy, int r, CColor col)
{
    int rr = (r * 2) / 3;
    CRect st;
    if (rr < 1) { rr = 1; }
    gfx_fill_circle(s, cx, cy - rr, rr, col);
    gfx_fill_circle(s, cx - rr, cy + rr / 2, rr, col);
    gfx_fill_circle(s, cx + rr, cy + rr / 2, rr, col);
    st = crect_make(cx - 1, cy, 3, r + 1);
    gfx_fill_rect(s, &st, col);
}

/*
 * The corner pips, drawn pixel by pixel rather than scaled down.
 *
 * The vector shapes below are built from a triangle, two circles and a stem,
 * and they read beautifully at the size of the central pip. At the size of a
 * CORNER pip they do not: the lobes swallow the triangle, the stem lands
 * inside them, and the spade comes out as a featureless blob that on screen is
 * indistinguishable from the diamond. A card whose corner says "diamond" and
 * whose middle says "spade" is worse than one with no corner pip at all, and
 * the corner is what you read when the cards are fanned.
 *
 * Seven by seven, so each suit keeps the one feature that identifies it: the
 * spade a point on top, the club a flat cap, the heart a notch, the diamond
 * neither. Bit 6 is the leftmost column.
 */
#define SOL_PIP_SM 7
static const unsigned char SOL_SMALL_PIP[4][SOL_PIP_SM] = {
    /* SUIT_CLUB    */ { 0x1C, 0x1C, 0x7F, 0x7F, 0x7F, 0x08, 0x1C },
    /* SUIT_DIAMOND */ { 0x08, 0x1C, 0x3E, 0x7F, 0x3E, 0x1C, 0x08 },
    /* SUIT_HEART   */ { 0x36, 0x7F, 0x7F, 0x7F, 0x3E, 0x1C, 0x08 },
    /* SUIT_SPADE   */ { 0x08, 0x1C, 0x3E, 0x7F, 0x7F, 0x08, 0x1C }
};

/* One corner pip, centred on (cx, cy). */
void card_pip_small(GfxSurface *s, int suit, int cx, int cy, CColor col)
{
    int row, bit;
    const unsigned char *g;
    if (suit < 0 || suit > 3) { suit = SUIT_SPADE; }
    g = SOL_SMALL_PIP[suit];
    for (row = 0; row < SOL_PIP_SM; row++) {
        for (bit = 0; bit < SOL_PIP_SM; bit++) {
            if (g[row] & (unsigned char)(0x40u >> bit)) {
                gfx_put_pixel(s, cx - 3 + bit, cy - 3 + row, col);
            }
        }
    }
}

void card_pip(GfxSurface *s, int suit, int cx, int cy, int r, CColor col)
{
    switch (suit) {
    case SUIT_CLUB:    sol_club(s, cx, cy, r, col);    break;
    case SUIT_DIAMOND: sol_diamond(s, cx, cy, r, col); break;
    case SUIT_HEART:   sol_heart(s, cx, cy, r, col);   break;
    default:           sol_spade(s, cx, cy, r, col);   break;
    }
}

/* ---- card / slot rendering (rects already in surface coords) ---------- */
void card_draw_face(GfxSurface *s, const CRect *r, const Card *c,
                          cbool selected)
{
    const UiPalette *p = ui_palette();
    CColor white = GFX_RGB(0xFF, 0xFF, 0xFF);
    CColor border = GFX_RGB(0x50, 0x50, 0x50);
    CColor col = sol_is_red(c->suit) ? GFX_RGB(0xC8, 0x14, 0x14)
                                     : GFX_RGB(0x18, 0x18, 0x18);
    CRect inner;
    const char *rk = card_rank_str(c->rank);
    int cx, cy, tw;

    gfx_fill_round_rect(s, r, 5, border);
    inner = crect_inset(r, 1);
    gfx_fill_round_rect(s, &inner, 4, white);

    /* top-left index (visible even when the card is fanned under another) */
    gfx_draw_text(s, GFX_FONT_BOLD, r->x0 + 4, r->y0 + 3, rk, col);
    card_pip_small(s, c->suit, r->x0 + 8, r->y0 + 15, col);

    /* central pip */
    cx = (r->x0 + r->x1) / 2;
    cy = (r->y0 + r->y1) / 2;
    card_pip(s, c->suit, cx, cy, 10, col);

    /* bottom-right index */
    tw = gfx_text_width(GFX_FONT_BOLD, rk);
    gfx_draw_text(s, GFX_FONT_BOLD, r->x1 - 4 - tw, r->y1 - 11, rk, col);
    card_pip_small(s, c->suit, r->x1 - 8, r->y1 - 19, col);

    if (selected) {
        gfx_frame_rect(s, r, p->accent);
        inner = crect_inset(r, 1);
        gfx_frame_rect(s, &inner, p->accent);
    }
}

void card_draw_back(GfxSurface *s, const CRect *r)
{
    CColor edge = GFX_RGB(0xF0, 0xF0, 0xF0);
    CColor dark = GFX_RGB(0x14, 0x22, 0x5A);
    CColor base = GFX_RGB(0x27, 0x40, 0x93);
    CColor line = GFX_RGB(0x4A, 0x66, 0xC8);
    CRect inner, saved;
    int i, w, h;

    gfx_fill_round_rect(s, r, 5, edge);
    inner = crect_inset(r, 1);
    gfx_fill_round_rect(s, &inner, 4, dark);
    inner = crect_inset(r, 3);
    gfx_fill_round_rect(s, &inner, 3, base);

    saved = gfx_clip_narrow(s, &inner);
    w = crect_w(&inner);
    h = crect_h(&inner);
    for (i = -h; i < w; i += 6) {
        gfx_line(s, inner.x0 + i, inner.y0, inner.x0 + i + h, inner.y0 + h, line);
        gfx_line(s, inner.x0 + i, inner.y0 + h, inner.x0 + i + h, inner.y0, line);
    }
    gfx_set_clip(s, &saved);
}
