/*
 * map_core.h - Squarified treemap layout, in integers.
 *
 * A treemap turns "which of these is big?" into one glance: every item gets a
 * rectangle whose AREA is its share of the whole, and the rectangles tile the
 * box exactly with no gaps and no overlaps. That last part is what makes it
 * honest -- if the rectangles did not tile, the areas would not be shares of
 * anything.
 *
 * The layout is the squarified algorithm (Bruls/Huizing/van Wijk): items are
 * laid in rows across the shorter side of what is left, and a row is closed as
 * soon as adding one more item would make its worst rectangle less square.
 * Long thin slivers are unreadable and unclickable, so squareness is not a
 * cosmetic goal here.
 *
 * Everything is integer arithmetic on a caller-owned array -- no allocation,
 * no drawing, no float -- because the target has no FPU and because a layout
 * you can test is a layout you can trust. The aspect comparisons are done in
 * PIXELS rather than in weights, which keeps every product inside a 32-bit
 * long no matter how large the byte counts get. Weights are pre-scaled once
 * for the same reason; see map_layout.
 *
 * Hermetically tested in tests/test_map.c: tiling, coverage, ordering,
 * proportionality, and the degenerate cases (zero weights, one item, a box
 * too small to divide).
 */
#ifndef CASTALIA_MAP_CORE_H
#define CASTALIA_MAP_CORE_H

#include "castalia/ctypes.h"
#include "castalia/rect.h"

/* The most items one layout will place. Beyond this the caller should group
 * the tail into an "other" bucket -- a thousand two-pixel rectangles inform
 * nobody. */
#define MAP_MAX_ITEMS 64

/*
 * Lay 'n' items with the given weights into 'area'.
 *
 * weight[] must be non-negative and is expected to be sorted DESCENDING --
 * that is what makes the squarified algorithm produce square rectangles.
 * map_sort_index() below builds such an order without moving the caller's
 * data. Weight 0 items get an empty rect (they have no share to show).
 *
 * out[] receives one rect per input item, in the SAME order as the input.
 * Returns how many rects are non-empty. A NULL argument, n <= 0, an empty
 * area, or an all-zero weight set lays nothing out and returns 0.
 */
int map_layout(const long *weight, int n, const CRect *area, CRect *out);

/*
 * Fill order[0..n) with the item indices sorted by DESCENDING weight, ties
 * broken by ascending index so the result is stable. The caller then reads
 * its own arrays through that permutation. Returns n (0 on a bad argument).
 */
int map_sort_index(const long *weight, int n, int *order);

/*
 * Which laid-out rect contains (x, y), or -1. Linear over 'n' -- a treemap
 * has tens of rects, not thousands, and a hit test that walks them in order
 * needs no spatial index to feel instant.
 */
int map_hit(const CRect *rects, int n, int x, int y);

#endif /* CASTALIA_MAP_CORE_H */
