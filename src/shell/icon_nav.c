/*
 * icon_nav.c - Which icon an arrow key should move to (see icon_nav.h).
 *
 * Integer arithmetic on rectangle centres. No allocation, no shell state, no
 * drawing -- the whole file is one comparison written carefully.
 */
#include "icon_nav.h"

/* The perpendicular offset counts this much more than distance travelled, so
 * an icon in the same row wins over a nearer one in the row below. Three is
 * enough to make columns and rows read as columns and rows without making a
 * slightly-misaligned icon unreachable. */
#define NAV_PERP_WEIGHT 3

static void centre_of(const CRect *r, int *cx, int *cy)
{
    *cx = (r->x0 + r->x1) / 2;
    *cy = (r->y0 + r->y1) / 2;
}

/* The top-left-most icon: where an arrow key lands when nothing is selected. */
static int first_icon(const CRect *rects, int count)
{
    int i, best = -1, bx = 0, by = 0;
    for (i = 0; i < count; i++) {
        int cx, cy;
        centre_of(&rects[i], &cx, &cy);
        /* Rows first, then columns -- reading order. */
        if (best < 0 || cy < by || (cy == by && cx < bx)) {
            best = i; bx = cx; by = cy;
        }
    }
    return best;
}

int icon_nav_next(const CRect *rects, int count, int from, int dir)
{
    int fx, fy, i, best = -1;
    long best_cost = 0;

    if (rects == NULL || count < 1) { return -1; }
    if (from < 0 || from >= count) { return first_icon(rects, count); }
    centre_of(&rects[from], &fx, &fy);

    for (i = 0; i < count; i++) {
        int cx, cy;
        long along, perp, cost;
        if (i == from) { continue; }
        centre_of(&rects[i], &cx, &cy);
        switch (dir) {
        case ICON_NAV_LEFT:  along = fx - cx; perp = (cy > fy) ? cy - fy : fy - cy; break;
        case ICON_NAV_RIGHT: along = cx - fx; perp = (cy > fy) ? cy - fy : fy - cy; break;
        case ICON_NAV_UP:    along = fy - cy; perp = (cx > fx) ? cx - fx : fx - cx; break;
        default:             along = cy - fy; perp = (cx > fx) ? cx - fx : fx - cx; break;
        }
        /* Strictly that way. An icon level with this one is not "to the
         * right" of it, however far across the screen it sits. */
        if (along <= 0) { continue; }
        cost = along + perp * NAV_PERP_WEIGHT;
        /* Ties break on the lower index so a given desktop always navigates
         * the same way, rather than depending on which was compared first. */
        if (best < 0 || cost < best_cost) { best = i; best_cost = cost; }
    }
    return best;
}
