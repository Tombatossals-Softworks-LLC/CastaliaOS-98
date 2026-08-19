/*
 * gfx_draw.c - Clipped drawing primitives and Win9x-style bevels.
 *
 * All primitives honor the surface's active clip rectangle. Colors are the
 * canonical 0x00RRGGBB XRGB used throughout the UI. There is no per-pixel
 * alpha here by design (Bible: avoid heavy alpha blending).
 */
#include "castalia/gfx.h"

/* Fast internal accessor: row base pointer. */
static CColor *row_ptr(GfxSurface *s, int y)
{
    return s->pixels + (long)y * s->pitch;
}

void gfx_clear(GfxSurface *s, CColor color)
{
    CRect r = crect_make(0, 0, s->w, s->h);
    gfx_fill_rect(s, &r, color);
}

void gfx_put_pixel(GfxSurface *s, int x, int y, CColor color)
{
    if (x < s->clip.x0 || x >= s->clip.x1) { return; }
    if (y < s->clip.y0 || y >= s->clip.y1) { return; }
    row_ptr(s, y)[x] = color;
}

CColor gfx_get_pixel(const GfxSurface *s, int x, int y)
{
    if (x < 0 || x >= s->w || y < 0 || y >= s->h) { return 0; }
    return s->pixels[(long)y * s->pitch + x];
}

/*
 * The single hottest primitive in the system -- a profile of an ordinary
 * desktop puts about sixty percent of all instructions in here, because every
 * panel, well, button face, list row and cleared background is a filled
 * rectangle.
 *
 * The loop bounds are pulled into plain locals FIRST, and that is not a
 * style preference. Written the obvious way -- `for (x = c.x0; x < c.x1; x++)
 * row[x] = color;` -- the compiler cannot keep c.x1 in a register: CColor is
 * an unsigned 32-bit integer and CRect's members are ints, so as far as the
 * language is concerned the store through 'row' might be writing to c.x1
 * itself. The bound is therefore re-read from the stack on EVERY pixel, and
 * the loop cannot be widened. Copying the bounds into locals whose address is
 * never taken removes the question, which on the host lets the loop vectorize
 * and on a 386 removes a memory read per pixel -- the same fix pays on both.
 */
void gfx_fill_rect(GfxSurface *s, const CRect *r, CColor color)
{
    CRect c = crect_intersect(&s->clip, r);
    CColor *row;
    int rows, n, pitch, x;
    if (crect_empty(&c)) { return; }
    n     = c.x1 - c.x0;
    rows  = c.y1 - c.y0;
    pitch = s->pitch;
    row   = s->pixels + (long)c.y0 * pitch + c.x0;
    while (rows-- > 0) {
        for (x = 0; x < n; x++) { row[x] = color; }
        row += pitch;
    }
}

void gfx_hline(GfxSurface *s, int x, int y, int w, CColor color)
{
    CRect r = crect_make(x, y, w, 1);
    gfx_fill_rect(s, &r, color);
}

void gfx_vline(GfxSurface *s, int x, int y, int h, CColor color)
{
    CRect r = crect_make(x, y, 1, h);
    gfx_fill_rect(s, &r, color);
}

void gfx_frame_rect(GfxSurface *s, const CRect *r, CColor color)
{
    int w = crect_w(r);
    int h = crect_h(r);
    if (w <= 0 || h <= 0) { return; }
    gfx_hline(s, r->x0, r->y0, w, color);
    gfx_hline(s, r->x0, r->y1 - 1, w, color);
    gfx_vline(s, r->x0, r->y0, h, color);
    gfx_vline(s, r->x1 - 1, r->y0, h, color);
}

void gfx_line(GfxSurface *s, int x0, int y0, int x1, int y1, CColor color)
{
    int dx = (x1 > x0) ? x1 - x0 : x0 - x1;
    int dy = (y1 > y0) ? y1 - y0 : y0 - y1;
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        gfx_put_pixel(s, x0, y0, color);
        if (x0 == x1 && y0 == y1) { break; }
        {
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 <  dx) { err += dx; y0 += sy; }
        }
    }
}

/*
 * The half-width of the disc at each row is floor(sqrt(r^2 - dy^2)). Scanning
 * up to it from zero on every row, as this used to, costs O(r) multiplies per
 * row and O(r^2) for the circle -- and the clock face redraws four discs every
 * second. But the half-width only SHRINKS as the row moves away from the
 * centre, and the top half mirrors the bottom, so carrying it down from the
 * previous row makes the whole circle O(r). Identical output: the search still
 * ends on the largest dx with dx^2 <= r^2 - dy^2.
 */
void gfx_fill_circle(GfxSurface *s, int cx, int cy, int r, CColor color)
{
    long rr = (long)r * r;
    int dy, dx = r;
    if (r <= 0) { return; }
    for (dy = 0; dy <= r; dy++) {
        long room = rr - (long)dy * dy;
        while (dx > 0 && (long)dx * dx > room) { dx--; }
        gfx_hline(s, cx - dx, cy + dy, 2 * dx + 1, color);
        if (dy > 0) { gfx_hline(s, cx - dx, cy - dy, 2 * dx + 1, color); }
    }
}

void gfx_fill_round_rect(GfxSurface *s, const CRect *r, int radius, CColor color)
{
    int w = crect_w(r);
    int h = crect_h(r);
    int rad = radius;
    int y;
    if (w <= 0 || h <= 0) { return; }
    if (rad < 0) { rad = 0; }
    if (rad > w / 2) { rad = w / 2; }
    if (rad > h / 2) { rad = h / 2; }
    for (y = 0; y < h; y++) {
        int inset = 0;
        if (y < rad) {
            int dy = rad - 1 - y;
            int dx = 0;
            int rr = rad * rad;
            while ((dx + 1) * (dx + 1) + dy * dy <= rr) { dx++; }
            inset = rad - dx;
        } else if (y >= h - rad) {
            int dy = y - (h - rad);
            int dx = 0;
            int rr = rad * rad;
            while ((dx + 1) * (dx + 1) + dy * dy <= rr) { dx++; }
            inset = rad - dx;
        }
        gfx_hline(s, r->x0 + inset, r->y0 + y, w - 2 * inset, color);
    }
}

/* Linear interpolation of two XRGB colors, t in [0,256]. */
static CColor lerp_color(CColor a, CColor b, int t)
{
    int inv = 256 - t;
    int r = (GFX_R(a) * inv + GFX_R(b) * t) >> 8;
    int g = (GFX_G(a) * inv + GFX_G(b) * t) >> 8;
    int bl = (GFX_B(a) * inv + GFX_B(b) * t) >> 8;
    return GFX_RGB(r, g, bl);
}

void gfx_vgradient(GfxSurface *s, const CRect *r, CColor top, CColor bottom)
{
    CRect c = crect_intersect(&s->clip, r);
    int span = crect_h(r);
    int y;
    if (crect_empty(&c) || span <= 0) { return; }
    for (y = c.y0; y < c.y1; y++) {
        int t = (span > 1) ? ((y - r->y0) * 256) / (span - 1) : 0;
        CColor col = lerp_color(top, bottom, t);
        CColor *row = row_ptr(s, y);
        int x;
        for (x = c.x0; x < c.x1; x++) {
            row[x] = col;
        }
    }
}

void gfx_vgradient3(GfxSurface *s, const CRect *r, CColor top, CColor mid,
                    CColor bottom, int mid_permille)
{
    CRect c = crect_intersect(&s->clip, r);
    int span = crect_h(r);
    int y;
    if (crect_empty(&c) || span <= 0) { return; }
    if (mid_permille < 1)    { mid_permille = 1; }
    if (mid_permille > 999)  { mid_permille = 999; }
    for (y = c.y0; y < c.y1; y++) {
        int p = (span > 1) ? ((y - r->y0) * 1000) / (span - 1) : 0;
        CColor col;
        if (p <= mid_permille) {
            col = lerp_color(top, mid, (p * 256) / mid_permille);
        } else {
            col = lerp_color(mid, bottom,
                             ((p - mid_permille) * 256) / (1000 - mid_permille));
        }
        {
            CColor *row = row_ptr(s, y);
            int x;
            for (x = c.x0; x < c.x1; x++) { row[x] = col; }
        }
    }
}

/*
 * See GFX_SCALE_RGB in gfx.h, which is this expression and is what per-pixel
 * loops use. This is the range-checked entry point, and the one the tests
 * drive -- so the clamps are the only thing here that the macro does not do.
 */
CColor gfx_scale_rgb(CColor c, int f)
{
    if (f <= 0)   { return 0; }
    if (f >= 256) { return c; }
    return GFX_SCALE_RGB(c, f);
}

CColor gfx_tint(CColor c, CColor target, int amt)
{
    if (amt < 0)   { amt = 0; }
    if (amt > 256) { amt = 256; }
    return lerp_color(c, target, amt);
}

/* Darken one pixel in place by a/256 (clip-checked). */
static void shadow_px(GfxSurface *s, int x, int y, int a)
{
    CColor c;
    int inv = 256 - a;
    if (x < s->clip.x0 || x >= s->clip.x1) { return; }
    if (y < s->clip.y0 || y >= s->clip.y1) { return; }
    c = s->pixels[(long)y * s->pitch + x];
    s->pixels[(long)y * s->pitch + x] =
        GFX_RGB((GFX_R(c) * inv) >> 8, (GFX_G(c) * inv) >> 8,
                (GFX_B(c) * inv) >> 8);
}

/* The falloff amount depends only on the distance band, and a shadow is at
 * most GFX_SHADOW_MAX bands wide, so the divide comes out of the pixel loop
 * entirely. Every glossy window carries a shadow and every drag frame redraws
 * it, which is what makes a divide per shadow pixel worth removing. */
#define GFX_SHADOW_MAX 32

void gfx_drop_shadow(GfxSurface *s, const CRect *r, int size, int amt)
{
    int fall[GFX_SHADOW_MAX + 1];
    int x, y, d;
    if (r == NULL || size <= 0 || amt <= 0) { return; }
    if (size > GFX_SHADOW_MAX) { size = GFX_SHADOW_MAX; }
    for (d = 0; d <= size; d++) { fall[d] = amt * (size + 1 - d) / (size + 1); }
    /* Right strip (offset a little below the top corner: light from top-left). */
    for (y = r->y0 + 3; y < r->y1; y++) {
        for (d = 1; d <= size; d++) {
            shadow_px(s, r->x1 - 1 + d, y, fall[d]);
        }
    }
    /* Bottom strip including the corner wrap (Chebyshev falloff). */
    for (y = r->y1; y < r->y1 + size; y++) {
        int dy = y - r->y1 + 1;
        for (x = r->x0 + 3; x < r->x1 - 1 + size; x++) {
            int dx = (x >= r->x1) ? (x - r->x1 + 1) : 0;
            d = (dx > dy) ? dx : dy;
            shadow_px(s, x, y, fall[d <= size ? d : size]);
        }
    }
}

void gfx_bevel(GfxSurface *s, const CRect *r, GfxBevel style,
               CColor light, CColor dark, CColor face)
{
    int w = crect_w(r);
    int h = crect_h(r);
    CColor tl, br, tl2, br2;
    int thick = (style == GFX_BEVEL_RAISED || style == GFX_BEVEL_SUNKEN) ? 2 : 1;

    if (w <= 0 || h <= 0) { return; }

    if (face != GFX_NO_FILL) {
        gfx_fill_rect(s, r, face);
    }

    switch (style) {
    case GFX_BEVEL_RAISED:
    case GFX_BEVEL_RAISED_THIN:
        tl = light; br = dark; tl2 = light; br2 = dark;
        break;
    case GFX_BEVEL_SUNKEN:
    case GFX_BEVEL_SUNKEN_THIN:
        tl = dark; br = light; tl2 = dark; br2 = light;
        break;
    case GFX_BEVEL_ETCHED:
    default:
        tl = dark; br = light; tl2 = light; br2 = dark;
        break;
    }

    /* Outer edge. */
    gfx_hline(s, r->x0, r->y0, w, tl);
    gfx_vline(s, r->x0, r->y0, h, tl);
    gfx_hline(s, r->x0, r->y1 - 1, w, br);
    gfx_vline(s, r->x1 - 1, r->y0, h, br);

    if (thick == 2 && w > 2 && h > 2) {
        /* Inner edge for the two-pixel classic look. */
        gfx_hline(s, r->x0 + 1, r->y0 + 1, w - 2, tl2);
        gfx_vline(s, r->x0 + 1, r->y0 + 1, h - 2, tl2);
        gfx_hline(s, r->x0 + 1, r->y1 - 2, w - 2, br2);
        gfx_vline(s, r->x1 - 2, r->y0 + 1, h - 2, br2);
    }
}

void gfx_focus_rect(GfxSurface *s, const CRect *r, CColor color)
{
    int x, y;
    if (s == NULL || r == NULL) { return; }
    if (crect_w(r) < 2 || crect_h(r) < 2) { return; }
    /* Phase from the rectangle's own origin, not from the screen: a ring that
     * changed its dot pattern when the window moved would shimmer. */
    for (x = r->x0; x <= r->x1; x++) {
        if (((x - r->x0) & 1) == 0) {
            gfx_put_pixel(s, x, r->y0, color);
            gfx_put_pixel(s, x, r->y1, color);
        }
    }
    for (y = r->y0; y <= r->y1; y++) {
        if (((y - r->y0) & 1) == 0) {
            gfx_put_pixel(s, r->x0, y, color);
            gfx_put_pixel(s, r->x1, y, color);
        }
    }
}
