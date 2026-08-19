/*
 * rect.h - Integer rectangle math and a small dirty-region accumulator.
 *
 * Rectangles use inclusive-left/top, exclusive-right/bottom (half-open)
 * conventions so that width == x1 - x0 and empty == (x0 >= x1). This is the
 * geometric core behind clipping and dirty-rectangle repaint (Layer 3).
 *
 * Everything here is pure, allocation-free, and unit-tested on the host.
 */
#ifndef CASTALIA_RECT_H
#define CASTALIA_RECT_H

#include "castalia/ctypes.h"

typedef struct {
    int x0, y0; /* inclusive top-left     */
    int x1, y1; /* exclusive bottom-right */
} CRect;

typedef struct { int x, y; } CPoint;

/* Construct from x,y,w,h. Negative sizes clamp to empty. */
CRect crect_make(int x, int y, int w, int h);
CRect crect_make_xyxy(int x0, int y0, int x1, int y1);

int  crect_w(const CRect *r);
int  crect_h(const CRect *r);
cbool crect_empty(const CRect *r);
cbool crect_contains(const CRect *r, int x, int y);

/* Intersection. Result may be empty. */
CRect crect_intersect(const CRect *a, const CRect *b);
/* Bounding union of two rects. */
CRect crect_union(const CRect *a, const CRect *b);
/* True if the two rectangles overlap with positive area. */
cbool crect_overlaps(const CRect *a, const CRect *b);
/* Shrink (positive) or grow (negative) by 'd' on every side. */
CRect crect_inset(const CRect *r, int d);
/* Translate. */
CRect crect_offset(const CRect *r, int dx, int dy);
cbool crect_equal(const CRect *a, const CRect *b);

/* ---------------------------------------------------------------------- */
/* Dirty region: a bounded list of rectangles to repaint this frame.      */
/* When it overflows or coverage gets large it coalesces to one bounding  */
/* box rather than dropping updates (correctness over optimality).        */
/* ---------------------------------------------------------------------- */
#define CRGN_MAX 64

typedef struct {
    CRect rects[CRGN_MAX];
    int   count;
    cbool coalesced; /* set when we fell back to a single bounding rect   */
} CRegion;

void  cregion_clear(CRegion *rgn);
/* Add a rectangle to be repainted. Empty rects are ignored. Merges when it
 * cheaply can; falls back to a bounding box on overflow. */
void  cregion_add(CRegion *rgn, const CRect *r);
cbool cregion_is_empty(const CRegion *rgn);
/* Bounding box of everything currently in the region (empty if none). */
CRect cregion_bounds(const CRegion *rgn);
/*
 * Make the rectangles that touch 'area' pairwise disjoint, by merging any two
 * that overlap each other into the box around both, until none do. Rectangles
 * that do not touch 'area' are left exactly as they are.
 *
 * A region is allowed to hold overlapping rectangles -- cregion_add merges an
 * overlapping neighbour only when the union wastes little area -- and that is
 * fine for anything painted idempotently: a fill, a blit, a glyph. It is not
 * fine for a painter that READS what is already there, like the drop shadow
 * under the popups, which darkens each pixel it touches: a pixel inside two
 * rectangles gets darkened twice and comes out too dark.
 *
 * Merging only ever grows what is repainted, never shrinks it, so nothing
 * goes missing if the area asked about is wrong.
 */
void  cregion_disjoin_over(CRegion *rgn, const CRect *area);

#endif /* CASTALIA_RECT_H */
