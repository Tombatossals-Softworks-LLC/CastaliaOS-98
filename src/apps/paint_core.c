/*
 * paint_core.c - Raster editing for Paint (pure, host-tested).
 *
 * See paint_core.h. Integer math only: the ellipse is the midpoint algorithm,
 * the airbrush is a 32-bit LCG, and the flood fill is an explicit-stack
 * scanline fill so a big region cannot blow the return stack on DOS.
 */
#include "paint_core.h"
#include "castalia/sys.h"

#define PC_FILL_SPANS 2048          /* queued spans; a bound, not a guess */

static int pc_clampi(int v, int lo, int hi)
{
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

/* ---- brushes ----------------------------------------------------------- */
void pc_dab(GfxSurface *cv, int x, int y, int size, CColor col)
{
    if (cv == NULL) { return; }
    if (size <= 1) { gfx_put_pixel(cv, x, y, col); return; }
    if (size == 2) {                       /* an even dab has no center     */
        CRect r = crect_make(x, y, 2, 2);
        gfx_fill_rect(cv, &r, col);
        return;
    }
    gfx_fill_circle(cv, x, y, size / 2, col);
}

void pc_stroke(GfxSurface *cv, int x0, int y0, int x1, int y1,
               int size, CColor col)
{
    int dx, dy, sx, sy, err, e2, guard;
    if (cv == NULL) { return; }
    if (size <= 1) { gfx_line(cv, x0, y0, x1, y1, col); return; }

    /* Bresenham, dabbing the brush at every step so a fast drag still leaves
     * a continuous stroke instead of a dotted line. */
    dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    sx = (x0 < x1) ? 1 : -1;
    sy = (y0 < y1) ? 1 : -1;
    err = dx - dy;
    guard = dx + dy + 2;                    /* every step burns one         */
    for (;;) {
        pc_dab(cv, x0, y0, size, col);
        if ((x0 == x1 && y0 == y1) || guard-- <= 0) { break; }
        e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void pc_spray(GfxSurface *cv, int x, int y, int radius, int density,
              CColor col, cu32 *seed)
{
    cu32 s;
    int i;
    if (cv == NULL || seed == NULL || radius < 1 || density < 1) { return; }
    s = *seed;
    for (i = 0; i < density; i++) {
        int ox, oy;
        s = s * 1103515245UL + 12345UL;     /* the classic LCG              */
        ox = (int)((s >> 16) % (cu32)(radius * 2 + 1)) - radius;
        s = s * 1103515245UL + 12345UL;
        oy = (int)((s >> 16) % (cu32)(radius * 2 + 1)) - radius;
        if (ox * ox + oy * oy <= radius * radius) {
            gfx_put_pixel(cv, x + ox, y + oy, col);
        }
    }
    *seed = s;
}

/* ---- shapes ------------------------------------------------------------ */
/* The outline of a rectangle, 'width' px thick, drawn inside 'r'. */
static void pc_frame_thick(GfxSurface *cv, const CRect *r, int width, CColor c)
{
    int i;
    if (width < 1) { width = 1; }
    for (i = 0; i < width; i++) {
        CRect in = crect_inset(r, i);
        if (crect_w(&in) <= 0 || crect_h(&in) <= 0) { break; }
        gfx_frame_rect(cv, &in, c);
    }
}

void pc_rect(GfxSurface *cv, const CRect *r, int width,
             CColor line, CColor fill, PcStyle style)
{
    if (cv == NULL || r == NULL || crect_empty(r)) { return; }
    if (style != PC_OUTLINE) {
        CRect in = (style == PC_FILLED) ? crect_inset(r, width) : *r;
        if (crect_w(&in) > 0 && crect_h(&in) > 0) {
            gfx_fill_rect(cv, &in, fill);
        }
    }
    if (style != PC_SOLID) { pc_frame_thick(cv, r, width, line); }
}

/* Midpoint ellipse. 'plot' style: four symmetric points per step. */
static void pc_ell_points(GfxSurface *cv, int cx, int cy, int x, int y,
                          int width, CColor c)
{
    int size = (width < 1) ? 1 : width;
    pc_dab(cv, cx + x, cy + y, size, c);
    pc_dab(cv, cx - x, cy + y, size, c);
    pc_dab(cv, cx + x, cy - y, size, c);
    pc_dab(cv, cx - x, cy - y, size, c);
}

/* One scanline pair of the interior, used when the shape is filled. */
static void pc_ell_span(GfxSurface *cv, int cx, int cy, int x, int y, CColor c)
{
    gfx_hline(cv, cx - x, cy + y, x * 2 + 1, c);
    gfx_hline(cv, cx - x, cy - y, x * 2 + 1, c);
}

void pc_ellipse(GfxSurface *cv, const CRect *r, int width,
                CColor line, CColor fill, PcStyle style)
{
    long a, b, aa, bb, x, y, sigma;
    int cx, cy;
    if (cv == NULL || r == NULL || crect_empty(r)) { return; }
    a = crect_w(r) / 2;
    b = crect_h(r) / 2;
    if (a < 1 || b < 1) {                    /* degenerate: draw the box     */
        pc_rect(cv, r, width, line, fill, style);
        return;
    }
    cx = r->x0 + (int)a;
    cy = r->y0 + (int)b;
    aa = a * a;
    bb = b * b;

    if (style != PC_OUTLINE) {
        /* Fill first, then lay the outline over it -- the same order the era's
         * editors used, and it hides the seam where the spans meet. */
        x = 0; y = b; sigma = 2 * bb + aa * (1 - 2 * b);
        while (bb * x <= aa * y) {
            pc_ell_span(cv, cx, cy, (int)x, (int)y, fill);
            if (sigma >= 0) { sigma += 4 * aa * (1 - y); y--; }
            sigma += bb * (4 * x + 6);
            x++;
        }
        x = a; y = 0; sigma = 2 * aa + bb * (1 - 2 * a);
        while (aa * y <= bb * x) {
            pc_ell_span(cv, cx, cy, (int)x, (int)y, fill);
            if (sigma >= 0) { sigma += 4 * bb * (1 - x); x--; }
            sigma += aa * (4 * y + 6);
            y++;
        }
    }
    if (style == PC_SOLID) { return; }

    x = 0; y = b; sigma = 2 * bb + aa * (1 - 2 * b);
    while (bb * x <= aa * y) {
        pc_ell_points(cv, cx, cy, (int)x, (int)y, width, line);
        if (sigma >= 0) { sigma += 4 * aa * (1 - y); y--; }
        sigma += bb * (4 * x + 6);
        x++;
    }
    x = a; y = 0; sigma = 2 * aa + bb * (1 - 2 * a);
    while (aa * y <= bb * x) {
        pc_ell_points(cv, cx, cy, (int)x, (int)y, width, line);
        if (sigma >= 0) { sigma += 4 * bb * (1 - x); x--; }
        sigma += aa * (4 * y + 6);
        y++;
    }
}

/* ---- fill and pick ----------------------------------------------------- */
CColor pc_pick(const GfxSurface *cv, int x, int y, CColor fallback)
{
    if (cv == NULL || x < 0 || y < 0 || x >= cv->w || y >= cv->h) {
        return fallback;
    }
    return gfx_get_pixel(cv, x, y) & 0xFFFFFFUL;
}

int pc_flood(GfxSurface *cv, int x, int y, CColor col)
{
    static int qx[PC_FILL_SPANS], qy[PC_FILL_SPANS];
    int head = 0, tail = 0, painted = 0;
    long budget;
    CColor target, want;
    if (cv == NULL || x < 0 || y < 0 || x >= cv->w || y >= cv->h) { return 0; }
    target = gfx_get_pixel(cv, x, y) & 0xFFFFFFUL;
    want = col & 0xFFFFFFUL;
    if (target == want) { return 0; }

    /* A span is queued only while it still reads as the target colour, so the
     * loop ends because painting changes what the next read sees. That makes
     * termination depend on the drawing actually landing -- and gfx_hline is
     * gfx_fill_rect, which honours the surface clip. Narrow the clip past the
     * fill area and every seed survives its own re-check, so the queue refills
     * forever. No honest fill covers a pixel twice, so once more than the whole
     * canvas has been painted the drawing is going nowhere and we stop. */
    budget = (long)cv->w * (long)cv->h;

    qx[tail] = x; qy[tail] = y; tail = 1;
    while (head != tail) {
        int px = qx[head], py = qy[head];
        int lx, rx, i, row;
        head = (head + 1) % PC_FILL_SPANS;
        if ((gfx_get_pixel(cv, px, py) & 0xFFFFFFUL) != target) { continue; }

        lx = px;
        while (lx > 0 &&
               (gfx_get_pixel(cv, lx - 1, py) & 0xFFFFFFUL) == target) { lx--; }
        rx = px;
        while (rx < cv->w - 1 &&
               (gfx_get_pixel(cv, rx + 1, py) & 0xFFFFFFUL) == target) { rx++; }
        gfx_hline(cv, lx, py, rx - lx + 1, col);
        painted += rx - lx + 1;
        if ((long)painted > budget) { break; }

        /* Seed the row above and below once per contiguous run, which is what
         * keeps the queue small enough to bound. */
        for (row = py - 1; row <= py + 1; row += 2) {
            if (row < 0 || row >= cv->h) { continue; }
            i = lx;
            while (i <= rx) {
                if ((gfx_get_pixel(cv, i, row) & 0xFFFFFFUL) == target) {
                    int next = (tail + 1) % PC_FILL_SPANS;
                    if (next != head) {
                        qx[tail] = i; qy[tail] = row; tail = next;
                    }
                    while (i <= rx &&
                           (gfx_get_pixel(cv, i, row) & 0xFFFFFFUL) == target) {
                        i++;
                    }
                } else {
                    i++;
                }
            }
        }
    }
    return painted;
}

/* ---- regions ----------------------------------------------------------- */
/* The part of 'r' that is actually on the canvas. */
static CRect pc_clip_rect(const GfxSurface *cv, const CRect *r)
{
    CRect all = crect_make(0, 0, cv->w, cv->h);
    return crect_intersect(&all, r);
}

GfxSurface *pc_copy_region(const GfxSurface *cv, const CRect *r)
{
    GfxSurface *out;
    CRect c;
    int x, y;
    if (cv == NULL || cv->pixels == NULL || r == NULL) { return NULL; }
    c = pc_clip_rect(cv, r);
    if (crect_w(&c) <= 0 || crect_h(&c) <= 0) { return NULL; }
    out = gfx_surface_new(crect_w(&c), crect_h(&c));
    if (out == NULL) { return NULL; }
    for (y = 0; y < out->h; y++) {
        const CColor *src = cv->pixels + (long)(c.y0 + y) * cv->pitch + c.x0;
        CColor *dst = out->pixels + (long)y * out->pitch;
        for (x = 0; x < out->w; x++) { dst[x] = src[x]; }
    }
    return out;
}

void pc_paste(GfxSurface *cv, const GfxSurface *src, int x, int y)
{
    int sx, sy;
    if (cv == NULL || src == NULL || cv->pixels == NULL ||
        src->pixels == NULL) {
        return;
    }
    for (sy = 0; sy < src->h; sy++) {
        int dy = y + sy;
        if (dy < 0 || dy >= cv->h) { continue; }
        for (sx = 0; sx < src->w; sx++) {
            int dx = x + sx;
            if (dx < 0 || dx >= cv->w) { continue; }
            cv->pixels[(long)dy * cv->pitch + dx] =
                src->pixels[(long)sy * src->pitch + sx];
        }
    }
}

void pc_clear_region(GfxSurface *cv, const CRect *r, CColor col)
{
    CRect c;
    if (cv == NULL || r == NULL) { return; }
    c = pc_clip_rect(cv, r);
    if (crect_w(&c) <= 0 || crect_h(&c) <= 0) { return; }
    gfx_fill_rect(cv, &c, col);
}

GfxSurface *pc_scale(const GfxSurface *src, int w, int h)
{
    GfxSurface *out;
    int x, y;
    if (src == NULL || src->pixels == NULL || w < 1 || h < 1) { return NULL; }
    if (src->w < 1 || src->h < 1) { return NULL; }
    out = gfx_surface_new(w, h);
    if (out == NULL) { return NULL; }
    /*
     * The source column for each output column is the same on every row, so
     * it is worked out once per column instead of once per PIXEL. That turns
     * w*h divides into w -- for a 640x480 image scaled to a 30-pixel
     * thumbnail it is the difference between 900 divides and 30, and the File
     * Manager's Icons view builds one of these per file on screen.
     *
     * The arithmetic is unchanged, so the output is identical: same integer
     * source coordinates, no interpolation, no float.
     */
    {
        int *cols = (int *)sys_alloc((cu32)w * (cu32)sizeof(int));
        if (cols == NULL) { gfx_surface_free(out); return NULL; }
        gfx_scale_map(cols, w, src->w);
        for (y = 0; y < h; y++) {
            int sy = (int)(((long)y * src->h) / h);
            const CColor *s = src->pixels + (long)sy * src->pitch;
            CColor *d = out->pixels + (long)y * out->pitch;
            for (x = 0; x < w; x++) { d[x] = s[cols[x]]; }
        }
        sys_free(cols, (cu32)w * (cu32)sizeof(int));
    }
    return out;
}

/* ---- whole-image operations ------------------------------------------- */
void pc_invert(GfxSurface *cv)
{
    int x, y;
    if (cv == NULL || cv->pixels == NULL) { return; }
    for (y = 0; y < cv->h; y++) {
        CColor *row = cv->pixels + (long)y * cv->pitch;
        for (x = 0; x < cv->w; x++) {
            row[x] = (~row[x]) & 0xFFFFFFUL;
        }
    }
}

void pc_grayscale(GfxSurface *cv)
{
    int x, y;
    if (cv == NULL || cv->pixels == NULL) { return; }
    for (y = 0; y < cv->h; y++) {
        CColor *row = cv->pixels + (long)y * cv->pitch;
        for (x = 0; x < cv->w; x++) {
            CColor c = row[x];
            /* The usual luma weights, in 1/256ths so it stays integer. */
            int g = (GFX_R(c) * 77 + GFX_G(c) * 150 + GFX_B(c) * 29) >> 8;
            g = pc_clampi(g, 0, 255);
            row[x] = GFX_RGB(g, g, g);
        }
    }
}

void pc_flip_h(GfxSurface *cv)
{
    int x, y;
    if (cv == NULL || cv->pixels == NULL) { return; }
    for (y = 0; y < cv->h; y++) {
        CColor *row = cv->pixels + (long)y * cv->pitch;
        for (x = 0; x < cv->w / 2; x++) {
            CColor t = row[x];
            row[x] = row[cv->w - 1 - x];
            row[cv->w - 1 - x] = t;
        }
    }
}

void pc_flip_v(GfxSurface *cv)
{
    int x, y;
    if (cv == NULL || cv->pixels == NULL) { return; }
    for (y = 0; y < cv->h / 2; y++) {
        CColor *a = cv->pixels + (long)y * cv->pitch;
        CColor *b = cv->pixels + (long)(cv->h - 1 - y) * cv->pitch;
        for (x = 0; x < cv->w; x++) {
            CColor t = a[x];
            a[x] = b[x];
            b[x] = t;
        }
    }
}

/* ---- undo -------------------------------------------------------------- */
static void pc_copy_into(GfxSurface *dst, const GfxSurface *src)
{
    int y;
    if (dst == NULL || src == NULL || dst->pixels == NULL ||
        src->pixels == NULL || dst->w != src->w || dst->h != src->h) {
        return;
    }
    for (y = 0; y < src->h; y++) {
        int x;
        const CColor *s = src->pixels + (long)y * src->pitch;
        CColor *d = dst->pixels + (long)y * dst->pitch;
        for (x = 0; x < src->w; x++) { d[x] = s[x]; }
    }
}

/* Exchange two same-sized canvases in place -- no third buffer needed. */
static void pc_swap_pixels(GfxSurface *a, GfxSurface *b)
{
    int x, y;
    if (a == NULL || b == NULL || a->pixels == NULL || b->pixels == NULL ||
        a->w != b->w || a->h != b->h) {
        return;
    }
    for (y = 0; y < a->h; y++) {
        CColor *pa = a->pixels + (long)y * a->pitch;
        CColor *pb = b->pixels + (long)y * b->pitch;
        for (x = 0; x < a->w; x++) {
            CColor t = pa[x];
            pa[x] = pb[x];
            pb[x] = t;
        }
    }
}

void pc_undo_init(PcUndo *u)
{
    int i;
    if (u == NULL) { return; }
    for (i = 0; i < PC_UNDO_LEVELS; i++) { u->slot[i] = NULL; }
    u->head = 0;
    u->depth = 0;
    u->redo = 0;
}

void pc_undo_free(PcUndo *u)
{
    int i;
    if (u == NULL) { return; }
    for (i = 0; i < PC_UNDO_LEVELS; i++) {
        if (u->slot[i] != NULL) { gfx_surface_free(u->slot[i]); u->slot[i] = NULL; }
    }
    u->head = 0;
    u->depth = 0;
    u->redo = 0;
}

/* The slot a snapshot lives in, wrapped into the ring. */
static int pc_slot(int index)
{
    int i = index % PC_UNDO_LEVELS;
    return (i < 0) ? i + PC_UNDO_LEVELS : i;
}

cbool pc_undo_push(PcUndo *u, const GfxSurface *cv)
{
    int at;
    if (u == NULL || cv == NULL) { return CFALSE; }
    at = pc_slot(u->head);
    if (u->slot[at] == NULL) {
        u->slot[at] = gfx_surface_new(cv->w, cv->h);
        if (u->slot[at] == NULL) { return CFALSE; }
    }
    if (u->slot[at]->w != cv->w || u->slot[at]->h != cv->h) {
        gfx_surface_free(u->slot[at]);
        u->slot[at] = gfx_surface_new(cv->w, cv->h);
        if (u->slot[at] == NULL) { return CFALSE; }
    }
    pc_copy_into(u->slot[at], cv);
    u->head = pc_slot(u->head + 1);
    if (u->depth < PC_UNDO_LEVELS - 1) { u->depth++; }
    u->redo = 0;                 /* a new edit forks the timeline */
    return CTRUE;
}

cbool pc_undo_undo(PcUndo *u, GfxSurface *cv)
{
    int at;
    GfxSurface *snap;
    if (u == NULL || cv == NULL || u->depth <= 0) { return CFALSE; }
    at = pc_slot(u->head - 1);
    snap = u->slot[at];
    if (snap == NULL || snap->w != cv->w || snap->h != cv->h) { return CFALSE; }

    /* Swap rather than copy one way: the slot then holds the state we are
     * leaving, which is exactly what redo needs -- and swapping in place costs
     * no allocation at the moment the user is waiting for the screen. */
    pc_swap_pixels(cv, snap);
    u->head = at;
    u->depth--;
    if (u->redo < PC_UNDO_LEVELS - 1) { u->redo++; }
    return CTRUE;
}

cbool pc_undo_redo(PcUndo *u, GfxSurface *cv)
{
    int at;
    GfxSurface *snap;
    if (u == NULL || cv == NULL || u->redo <= 0) { return CFALSE; }
    at = pc_slot(u->head);
    snap = u->slot[at];
    if (snap == NULL || snap->w != cv->w || snap->h != cv->h) { return CFALSE; }
    pc_swap_pixels(cv, snap);
    u->head = pc_slot(u->head + 1);
    u->redo--;
    if (u->depth < PC_UNDO_LEVELS - 1) { u->depth++; }
    return CTRUE;
}

int pc_undo_depth(const PcUndo *u) { return (u == NULL) ? 0 : u->depth; }
int pc_undo_redo_depth(const PcUndo *u) { return (u == NULL) ? 0 : u->redo; }
