/*
 * gfx_rect.c - Rectangle math and the dirty-region accumulator.
 *
 * Half-open rectangles (x0<=x<x1, y0<=y<y1). All functions are pure and
 * allocation-free; this is the geometric core the clipping and dirty-repaint
 * paths depend on, and it is exercised directly by tests/test_rect.c.
 */
#include "castalia/rect.h"

CRect crect_make(int x, int y, int w, int h)
{
    CRect r;
    r.x0 = x;
    r.y0 = y;
    r.x1 = (w > 0) ? x + w : x;
    r.y1 = (h > 0) ? y + h : y;
    return r;
}

CRect crect_make_xyxy(int x0, int y0, int x1, int y1)
{
    CRect r;
    r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1;
    return r;
}

int crect_w(const CRect *r) { return (r->x1 > r->x0) ? r->x1 - r->x0 : 0; }
int crect_h(const CRect *r) { return (r->y1 > r->y0) ? r->y1 - r->y0 : 0; }

cbool crect_empty(const CRect *r)
{
    return (r->x1 <= r->x0 || r->y1 <= r->y0) ? CTRUE : CFALSE;
}

cbool crect_contains(const CRect *r, int x, int y)
{
    return (x >= r->x0 && x < r->x1 && y >= r->y0 && y < r->y1) ? CTRUE : CFALSE;
}

CRect crect_intersect(const CRect *a, const CRect *b)
{
    CRect r;
    r.x0 = (a->x0 > b->x0) ? a->x0 : b->x0;
    r.y0 = (a->y0 > b->y0) ? a->y0 : b->y0;
    r.x1 = (a->x1 < b->x1) ? a->x1 : b->x1;
    r.y1 = (a->y1 < b->y1) ? a->y1 : b->y1;
    if (r.x1 < r.x0) { r.x1 = r.x0; }
    if (r.y1 < r.y0) { r.y1 = r.y0; }
    return r;
}

CRect crect_union(const CRect *a, const CRect *b)
{
    CRect r;
    if (crect_empty(a)) { return *b; }
    if (crect_empty(b)) { return *a; }
    r.x0 = (a->x0 < b->x0) ? a->x0 : b->x0;
    r.y0 = (a->y0 < b->y0) ? a->y0 : b->y0;
    r.x1 = (a->x1 > b->x1) ? a->x1 : b->x1;
    r.y1 = (a->y1 > b->y1) ? a->y1 : b->y1;
    return r;
}

cbool crect_overlaps(const CRect *a, const CRect *b)
{
    if (a->x0 >= b->x1 || b->x0 >= a->x1) { return CFALSE; }
    if (a->y0 >= b->y1 || b->y0 >= a->y1) { return CFALSE; }
    return CTRUE;
}

CRect crect_inset(const CRect *r, int d)
{
    CRect o;
    o.x0 = r->x0 + d;
    o.y0 = r->y0 + d;
    o.x1 = r->x1 - d;
    o.y1 = r->y1 - d;
    if (o.x1 < o.x0) { o.x1 = o.x0; }
    if (o.y1 < o.y0) { o.y1 = o.y0; }
    return o;
}

CRect crect_offset(const CRect *r, int dx, int dy)
{
    CRect o;
    o.x0 = r->x0 + dx; o.y0 = r->y0 + dy;
    o.x1 = r->x1 + dx; o.y1 = r->y1 + dy;
    return o;
}

cbool crect_equal(const CRect *a, const CRect *b)
{
    return (a->x0 == b->x0 && a->y0 == b->y0 &&
            a->x1 == b->x1 && a->y1 == b->y1) ? CTRUE : CFALSE;
}

/* ---------------------------------------------------------------------- */
/* Dirty region                                                           */
/* ---------------------------------------------------------------------- */

void cregion_clear(CRegion *rgn)
{
    rgn->count = 0;
    rgn->coalesced = CFALSE;
}

/* Area of a rect as a long, to compare merge cost without overflow on the
 * modest resolutions we target. */
static long rect_area(const CRect *r)
{
    return (long)crect_w(r) * (long)crect_h(r);
}

void cregion_add(CRegion *rgn, const CRect *r)
{
    int i;
    if (crect_empty(r)) { return; }

    if (rgn->coalesced) {
        /* Already a single bounding box; just grow it. */
        rgn->rects[0] = crect_union(&rgn->rects[0], r);
        return;
    }

    /* If the new rect is already covered by an existing one, skip it. */
    for (i = 0; i < rgn->count; i++) {
        CRect u = crect_union(&rgn->rects[i], r);
        if (crect_equal(&u, &rgn->rects[i])) { return; }
    }
    /* If merging with an overlapping neighbor doesn't waste much area,
     * merge in place (cheap coalescing that keeps the list short). */
    for (i = 0; i < rgn->count; i++) {
        if (crect_overlaps(&rgn->rects[i], r)) {
            CRect u = crect_union(&rgn->rects[i], r);
            long slack = rect_area(&u) - rect_area(&rgn->rects[i]) - rect_area(r);
            if (slack <= (rect_area(r) / 2)) {
                rgn->rects[i] = u;
                return;
            }
        }
    }

    if (rgn->count < CRGN_MAX) {
        rgn->rects[rgn->count++] = *r;
        return;
    }

    /* Overflow: coalesce everything into one bounding box. We never drop a
     * rectangle -- correctness over optimality. */
    {
        CRect bounds = rgn->rects[0];
        for (i = 1; i < rgn->count; i++) {
            bounds = crect_union(&bounds, &rgn->rects[i]);
        }
        bounds = crect_union(&bounds, r);
        rgn->rects[0] = bounds;
        rgn->count = 1;
        rgn->coalesced = CTRUE;
    }
}

cbool cregion_is_empty(const CRegion *rgn)
{
    return (rgn->count == 0) ? CTRUE : CFALSE;
}

CRect cregion_bounds(const CRegion *rgn)
{
    CRect b;
    int i;
    if (rgn->count == 0) { return crect_make(0, 0, 0, 0); }
    b = rgn->rects[0];
    for (i = 1; i < rgn->count; i++) {
        b = crect_union(&b, &rgn->rects[i]);
    }
    return b;
}

/* See the header for why a caller would want this. It terminates because
 * every merge takes one rectangle out of the list, and the list is finite. */
void cregion_disjoin_over(CRegion *rgn, const CRect *area)
{
    int i, j;
    cbool merged = CTRUE;
    if (rgn == NULL || area == NULL) { return; }
    while (merged) {
        merged = CFALSE;
        for (i = 0; i < rgn->count && !merged; i++) {
            if (!crect_overlaps(area, &rgn->rects[i])) { continue; }
            for (j = i + 1; j < rgn->count; j++) {
                if (!crect_overlaps(area, &rgn->rects[j])) { continue; }
                if (!crect_overlaps(&rgn->rects[i], &rgn->rects[j])) {
                    continue;
                }
                rgn->rects[i] = crect_union(&rgn->rects[i], &rgn->rects[j]);
                rgn->rects[j] = rgn->rects[rgn->count - 1];
                rgn->count--;
                merged = CTRUE;
                break;
            }
        }
    }
}
