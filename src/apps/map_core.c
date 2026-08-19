/*
 * map_core.c - Squarified treemap layout (see map_core.h).
 *
 * Two decisions shape the whole file.
 *
 * First, exact tiling. Every boundary inside a row is computed from a running
 * CUMULATIVE weight rather than by adding up per-item widths, so rounding
 * error cannot accumulate into a gap or an overlap: the last item's right edge
 * is the strip's right edge by construction, not by luck. Same for the final
 * row, which is given the whole remaining depth instead of a computed one.
 *
 * Second, no overflow and no float. The squarified algorithm normally compares
 * aspect ratios computed from the weights, which on a 32-bit long overflows
 * the moment the weights are byte counts. Here the comparison is done on the
 * resulting PIXEL dimensions instead -- identical decisions, operands bounded
 * by the screen. Weights are pre-scaled once so that (dimension * weight)
 * cannot overflow either; the cap is derived from the rectangle actually being
 * filled, so it holds for any size the caller asks for.
 */
#include "castalia/ctypes.h"
#include "castalia/rect.h"
#include "map_core.h"

/* A conservative stand-in for LONG_MAX: the code targets a 32-bit long and
 * must not depend on <limits.h> being sane on the DOS compiler. */
#define MAP_LONG_SAFE 2000000000L

/* An aspect ratio we treat as infinitely bad (a zero-width or zero-depth
 * cell). Kept small enough that comparing it still cannot overflow. */
#define MAP_BAD_RATIO 1000000L

/* (a * b) / c for the ranges this file guarantees. c > 0 is the caller's job. */
static long map_muldiv(long a, long b, long c)
{
    if (c <= 0) { return 0; }
    return (a * b) / c;
}

/* Compare two ratios n1/d1 and n2/d2 without dividing. Returns <0, 0 or >0. */
static int map_ratio_cmp(long n1, long d1, long n2, long d2)
{
    long l, r;
    if (d1 <= 0) { d1 = 1; n1 = MAP_BAD_RATIO; }
    if (d2 <= 0) { d2 = 1; n2 = MAP_BAD_RATIO; }
    l = n1 * d2;
    r = n2 * d1;
    if (l < r) { return -1; }
    if (l > r) { return 1; }
    return 0;
}

/*
 * The worst (least square) aspect ratio in a row of 'm' weights totalling
 * 'sum', laid across 'span' pixels in a strip 'depth' pixels deep. Returned
 * as a fraction *num / *den with num >= den.
 */
static void map_row_worst(const long *w, int m, long sum, int span, int depth,
                          long *num, long *den)
{
    int j;
    long bn = 1, bd = 1;
    cbool have = CFALSE;
    if (m <= 0 || sum <= 0 || span <= 0 || depth <= 0) {
        *num = MAP_BAD_RATIO; *den = 1; return;
    }
    for (j = 0; j < m; j++) {
        long len = map_muldiv((long)span, w[j], sum);
        long n, d;
        if (len <= 0) { n = MAP_BAD_RATIO; d = 1; }
        else if (len >= (long)depth) { n = len; d = (long)depth; }
        else { n = (long)depth; d = len; }
        if (!have || map_ratio_cmp(n, d, bn, bd) > 0) {
            bn = n; bd = d; have = CTRUE;
        }
    }
    *num = bn; *den = bd;
}

int map_sort_index(const long *weight, int n, int *order)
{
    int i, j;
    if (weight == NULL || order == NULL || n <= 0) { return 0; }
    for (i = 0; i < n; i++) { order[i] = i; }
    /* Insertion sort: n is bounded by the item cap, and stability on ties is
     * what keeps a redrawn treemap from reshuffling equal-sized siblings. */
    for (i = 1; i < n; i++) {
        int key = order[i];
        j = i - 1;
        while (j >= 0 && weight[order[j]] < weight[key]) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
    return n;
}

int map_hit(const CRect *rects, int n, int x, int y)
{
    int i;
    if (rects == NULL) { return -1; }
    for (i = 0; i < n; i++) {
        if (crect_empty(&rects[i])) { continue; }
        if (crect_contains(&rects[i], x, y)) { return i; }
    }
    return -1;
}

int map_layout(const long *weight, int n, const CRect *area, CRect *out)
{
    long w[MAP_MAX_ITEMS];
    int  live[MAP_MAX_ITEMS];       /* input indices with a positive weight  */
    long total = 0, cap;
    int  nlive = 0, i, placed = 0, guard;
    CRect r;
    int  maxdim;

    if (weight == NULL || out == NULL || area == NULL || n <= 0) { return 0; }
    if (n > MAP_MAX_ITEMS) { n = MAP_MAX_ITEMS; }
    for (i = 0; i < n; i++) { out[i] = crect_make(0, 0, 0, 0); }
    r = *area;
    if (crect_empty(&r)) { return 0; }

    for (i = 0; i < n; i++) {
        if (weight[i] > 0) { w[nlive] = weight[i]; live[nlive] = i; nlive++; }
    }
    if (nlive == 0) { return 0; }

    /* Scale the weights down until (dimension * weight) is guaranteed to fit
     * in a long. Halving is lossy by design -- an item that rounds to zero is
     * one that could not have covered a pixel anyway -- but never below 1, so
     * a scaled item keeps a rectangle if it had any share at all. */
    maxdim = crect_w(&r) > crect_h(&r) ? crect_w(&r) : crect_h(&r);
    if (maxdim < 1) { maxdim = 1; }
    cap = MAP_LONG_SAFE / (long)maxdim;
    if (cap < 1) { cap = 1; }
    for (i = 0; i < nlive; i++) { total += w[i]; }
    for (guard = 0; total > cap && guard < 48; guard++) {
        total = 0;
        for (i = 0; i < nlive; i++) {
            w[i] >>= 1;
            if (w[i] < 1) { w[i] = 1; }
            total += w[i];
        }
    }
    if (total <= 0) { return 0; }

    i = 0;
    while (i < nlive) {
        int span, depth_avail, k, m, horiz;
        long sum = 0, bn = 0, bd = 1;

        if (crect_empty(&r)) { break; }
        horiz = crect_w(&r) <= crect_h(&r);   /* rows run across the SHORT side */
        span        = horiz ? crect_w(&r) : crect_h(&r);
        depth_avail = horiz ? crect_h(&r) : crect_w(&r);
        if (span <= 0 || depth_avail <= 0) { break; }

        /* Grow the row while it keeps getting squarer. The first item always
         * joins: a row of one is the only shape available to it. */
        k = i;
        while (k < nlive) {
            long cand = sum + w[k];
            long cn, cd;
            int  depth = (int)map_muldiv((long)depth_avail, cand, total);
            if (depth < 1) { depth = 1; }
            if (depth > depth_avail) { depth = depth_avail; }
            map_row_worst(&w[i], k - i + 1, cand, span, depth, &cn, &cd);
            if (k > i && map_ratio_cmp(cn, cd, bn, bd) > 0) { break; }
            bn = cn; bd = cd;
            sum = cand;
            k++;
        }
        m = k - i;

        /* Place the row. The last row takes whatever depth is left so the
         * area is covered exactly. */
        {
            int depth = (k >= nlive) ? depth_avail
                                     : (int)map_muldiv((long)depth_avail, sum, total);
            long cum = 0;
            int j, prev = 0;
            if (depth < 1) { depth = 1; }
            if (depth > depth_avail) { depth = depth_avail; }
            for (j = 0; j < m; j++) {
                int next;
                cum += w[i + j];
                next = (j == m - 1) ? span
                                    : (int)map_muldiv((long)span, cum, sum);
                if (next < prev) { next = prev; }
                if (next > span) { next = span; }
                if (horiz) {
                    out[live[i + j]] = crect_make_xyxy(r.x0 + prev, r.y0,
                                                       r.x0 + next, r.y0 + depth);
                } else {
                    out[live[i + j]] = crect_make_xyxy(r.x0, r.y0 + prev,
                                                       r.x0 + depth, r.y0 + next);
                }
                if (next > prev) { placed++; }
                prev = next;
            }
            if (horiz) { r.y0 += depth; } else { r.x0 += depth; }
        }
        total -= sum;
        i = k;
        if (total <= 0) { break; }
    }
    return placed;
}
