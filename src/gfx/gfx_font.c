/*
 * gfx_font.c - Bitmap font rendering.
 *
 * Renders the original 8x8 system font (gfx_font_data.c). Advance is 6px so
 * the 5-wide glyphs sit with one column of tracking; line height is 9px.
 * The bold face is synthesized by smearing each glyph row one pixel to the
 * right, so only one glyph table is stored.
 *
 * Unknown code points render as a hollow box so missing glyphs are visible
 * rather than silent.
 */
#include "castalia/gfx.h"
#include "castalia/sys.h"

#define FONT_FIRST   32
#define FONT_LAST    126
#define FONT_CELL    8
#define FONT_ADVANCE 6
#define FONT_HEIGHT  9   /* 8 glyph rows + 1 line gap */

extern const unsigned char g_castalia_font8[FONT_LAST - FONT_FIRST + 1][FONT_CELL];
extern const unsigned char g_spleen_font8[FONT_LAST - FONT_FIRST + 1][FONT_CELL];

/* Active physical face (same cell/advance for all faces -> no layout impact). */
static const unsigned char (*g_face)[FONT_CELL] = g_castalia_font8;

void gfx_font_select(int face)
{
    switch (face) {
    case GFX_FACE_SPLEEN: g_face = g_spleen_font8;   break;
    default:              g_face = g_castalia_font8; break;
    }
}

static const unsigned char g_box_glyph[FONT_CELL] = {
    0x00,0x78,0x48,0x48,0x48,0x78,0x00,0x00
};

void gfx_font_range(GfxFontId font, int *first, int *last)
{
    /* Both bundled faces carry the same 95 printable-ASCII cells, and the
     * bold and italic faces are synthesized from the regular one, so there is
     * a single answer today. It is still asked per font, because the day a
     * face arrives with more in it is the day a hard-coded 126 somewhere else
     * becomes a bug nobody is looking for. */
    CASTALIA_UNUSED(font);
    if (first != NULL) { *first = FONT_FIRST; }
    if (last  != NULL) { *last  = FONT_LAST; }
}

int gfx_font_height(GfxFontId font)
{
    CASTALIA_UNUSED(font);
    return FONT_HEIGHT;
}

int gfx_text_width(GfxFontId font, const char *text)
{
    int n = 0;
    CASTALIA_UNUSED(font);
    if (text == NULL) { return 0; }
    while (text[n] != '\0') { n++; }
    return n * FONT_ADVANCE;
}

static const unsigned char *glyph_rows(int ch)
{
    if (ch < FONT_FIRST || ch > FONT_LAST) { return g_box_glyph; }
    return g_face[ch - FONT_FIRST];
}

/*
 * Blit one glyph cell.
 *
 * Text is the single most-drawn thing in the shell (menus, lists, headers,
 * every app), so the clip is resolved ONCE per cell here rather than per lit
 * pixel: reject the whole cell if it misses the clip, clamp the row/column
 * spans that survive it, then write straight into the destination row. The
 * pixels produced are identical to the per-pixel gfx_put_pixel path.
 */
/* Italic shear: the top row leans this many pixels right of the baseline. */
#define FONT_SLANT(ry) (((FONT_CELL - 1) - (ry)) / 3)

static void draw_glyph(GfxSurface *s, int gx, int gy, int ch,
                       CColor color, cbool bold, cbool italic)
{
    const unsigned char *rows = glyph_rows(ch);
    int ry, ry0, ry1, rx0, rx1;
    int lean = italic ? FONT_SLANT(0) : 0;

    /* Whole-cell reject (scrolled-out text costs one test per glyph). */
    if (gx + FONT_CELL + lean <= s->clip.x0 || gx >= s->clip.x1) { return; }
    if (gy + FONT_CELL <= s->clip.y0 || gy >= s->clip.y1) { return; }

    /* The cell-local spans that survive the clip. */
    ry0 = (s->clip.y0 > gy) ? s->clip.y0 - gy : 0;
    ry1 = (s->clip.y1 < gy + FONT_CELL) ? s->clip.y1 - gy : FONT_CELL;
    rx0 = (s->clip.x0 > gx) ? s->clip.x0 - gx : 0;
    rx1 = (s->clip.x1 < gx + FONT_CELL) ? s->clip.x1 - gx : FONT_CELL;

    for (ry = ry0; ry < ry1; ry++) {
        unsigned int bits = rows[ry];
        CColor *dst;
        int rx;
        if (bold) { bits |= (bits >> 1); } /* thicken stems */
        if (bits == 0) { continue; }
        dst = s->pixels + (long)(gy + ry) * s->pitch;
        if (italic) {
            /* Sheared: the destination column moves per row, so clip each
             * pixel (italic runs are rare; upright text keeps the fast path). */
            int dx = FONT_SLANT(ry);
            for (rx = 0; rx < FONT_CELL; rx++) {
                int px = gx + rx + dx;
                if (!(bits & (0x80 >> rx))) { continue; }
                if (px >= s->clip.x0 && px < s->clip.x1) { dst[px] = color; }
            }
            continue;
        }
        for (rx = rx0; rx < rx1; rx++) {
            if (bits & (0x80 >> rx)) { dst[gx + rx] = color; }
        }
    }
}

void gfx_draw_text(GfxSurface *s, GfxFontId font, int x, int y,
                   const char *text, CColor color)
{
    int i = 0;
    cbool bold = (font == GFX_FONT_BOLD || font == GFX_FONT_BOLDITALIC)
               ? CTRUE : CFALSE;
    cbool italic = (font == GFX_FONT_ITALIC || font == GFX_FONT_BOLDITALIC)
                 ? CTRUE : CFALSE;
    if (text == NULL) { return; }
    /* Nothing on this line is visible: one test instead of a glyph loop. */
    if (y + FONT_CELL <= s->clip.y0 || y >= s->clip.y1) { return; }
    while (text[i] != '\0') {
        int gx = x + i * FONT_ADVANCE;
        /* Advance is monotonic, so once we pass the clip the rest is off it. */
        if (gx >= s->clip.x1) { break; }
        if (gx + FONT_CELL > s->clip.x0) {
            draw_glyph(s, gx, y, (unsigned char)text[i], color, bold, italic);
        }
        i++;
    }
}

void gfx_draw_text_shadow(GfxSurface *s, GfxFontId font, int x, int y,
                          const char *text, CColor color, CColor shadow)
{
    gfx_draw_text(s, font, x + 1, y + 1, text, shadow);
    gfx_draw_text(s, font, x, y, text, color);
}

void gfx_text_fit(char *dst, cu32 dstsz, const char *text, int width,
                  GfxFontId font)
{
    cu32 n;
    int ell;
    if (dst == NULL || dstsz == 0u) { return; }
    if (text == NULL) { dst[0] = '\0'; return; }
    sys_strlcpy(dst, text, dstsz);
    if (width <= 0 || gfx_text_width(font, dst) <= width) { return; }

    /* Shorten from the end until the text AND its ellipsis fit. Measured, not
     * assumed: reserving a guessed number of pixels for "..." is how the path
     * version ended up producing strings wider than the space they were fitted
     * to -- it reserved 12 for a mark that is 18 wide in this font. */
    ell = gfx_text_width(font, "...");
    n = sys_strnlen(dst, dstsz);
    while (n > 0u && gfx_text_width(font, dst) > width - ell) {
        n--;
        dst[n] = '\0';
    }
    /* Only room for the mark itself, or less: say as much as will go rather
     * than writing past the buffer. */
    if (n + 4u <= dstsz) {
        dst[n] = '.'; dst[n + 1u] = '.'; dst[n + 2u] = '.';
        dst[n + 3u] = '\0';
    }
}

void gfx_draw_text_rect(GfxSurface *s, GfxFontId font, const CRect *r,
                        const char *text, CColor color, int align_flags)
{
    int tw = gfx_text_width(font, text);
    int th = gfx_font_height(font) - 1; /* visible glyph height */
    int x = r->x0;
    int y = r->y0;
    int rw = crect_w(r);
    int rh = crect_h(r);

    if (align_flags & GFX_ALIGN_HCENTER) { x = r->x0 + (rw - tw) / 2; }
    else if (align_flags & GFX_ALIGN_RIGHT) { x = r->x1 - tw; }

    if (align_flags & GFX_ALIGN_VCENTER) { y = r->y0 + (rh - th) / 2; }
    else if (align_flags & GFX_ALIGN_BOTTOM) { y = r->y1 - th; }

    gfx_draw_text(s, font, x, y, text, color);
}
