/*
 * test_map.c - The treemap layout (map_core.c).
 *
 * A treemap makes exactly one promise: area is share. Everything worth
 * checking here follows from taking that promise literally.
 *
 * - The rectangles must TILE the box: every pixel covered exactly once. If
 *   they leave a gap, some share went missing; if they overlap, some share
 *   got counted twice. Both are checked here by stamping a pixel grid and
 *   demanding every cell reads exactly 1.
 * - A bigger weight must never get a smaller rectangle.
 * - The layout must survive the degenerate inputs a real folder produces:
 *   one item, all-equal items, zero-byte files, a box one pixel tall, and
 *   byte counts large enough to overflow naive weight arithmetic.
 */
#include "ctest.h"
#include "../src/apps/map_core.h"

#define GW 64
#define GH 48

/* Stamp every laid-out rect into a grid and report the coverage extremes. */
static void mp_cover(const CRect *rects, int n, const CRect *area,
                     int *out_min, int *out_max)
{
    static unsigned char grid[GH][GW];
    int x, y, i, mn = 255, mx = 0;
    for (y = 0; y < GH; y++) {
        for (x = 0; x < GW; x++) { grid[y][x] = 0; }
    }
    for (i = 0; i < n; i++) {
        for (y = rects[i].y0; y < rects[i].y1; y++) {
            for (x = rects[i].x0; x < rects[i].x1; x++) {
                if (x >= 0 && x < GW && y >= 0 && y < GH) { grid[y][x]++; }
            }
        }
    }
    for (y = area->y0; y < area->y1; y++) {
        for (x = area->x0; x < area->x1; x++) {
            int v = grid[y][x];
            if (v < mn) { mn = v; }
            if (v > mx) { mx = v; }
        }
    }
    *out_min = mn; *out_max = mx;
}

static long mp_area(const CRect *r)
{
    return (long)crect_w(r) * (long)crect_h(r);
}

void test_map(void)
{
    CRect area = crect_make(0, 0, GW, GH);
    CRect out[MAP_MAX_ITEMS];
    long  w[MAP_MAX_ITEMS];
    int   order[MAP_MAX_ITEMS];
    int   i, n, mn, mx;

    printf("- treemap\n");

    /* ---- the ordinary case: a folder of mixed sizes ------------------- */
    w[0] = 600; w[1] = 300; w[2] = 200; w[3] = 100; w[4] = 60; w[5] = 40;
    n = 6;
    CHECK_EQI(map_layout(w, n, &area, out), 6);
    mp_cover(out, n, &area, &mn, &mx);
    CHECK_EQI(mn, 1);                    /* no gaps                        */
    CHECK_EQI(mx, 1);                    /* no overlaps                    */
    /* Bigger weight, never a smaller rectangle. */
    for (i = 1; i < n; i++) {
        CHECK(mp_area(&out[i - 1]) >= mp_area(&out[i]));
    }
    /* And roughly proportional: the biggest is ~600/1300 of the box. Allow
     * generous slack for integer rounding on a 64x48 grid. */
    {
        long share = (mp_area(&out[0]) * 100L) / mp_area(&area);
        CHECK(share >= 38 && share <= 54);
    }

    /* Nothing should be a sliver -- that is the whole point of squarifying.
     * Every rect with a real share must be at most 6:1. */
    for (i = 0; i < n; i++) {
        int rw = crect_w(&out[i]), rh = crect_h(&out[i]);
        CHECK(rw > 0 && rh > 0);
        if (rw > 0 && rh > 0) {
            int lo = rw < rh ? rw : rh, hi = rw < rh ? rh : rw;
            CHECK(hi <= lo * 6);
        }
    }

    /* ---- one item takes the whole box --------------------------------- */
    w[0] = 1234;
    CHECK_EQI(map_layout(w, 1, &area, out), 1);
    CHECK(crect_equal(&out[0], &area));

    /* ---- all equal: still tiles exactly ------------------------------- */
    for (i = 0; i < 9; i++) { w[i] = 100; }
    CHECK_EQI(map_layout(w, 9, &area, out), 9);
    mp_cover(out, 9, &area, &mn, &mx);
    CHECK_EQI(mn, 1);
    CHECK_EQI(mx, 1);

    /* ---- zero-weight items get no rectangle at all -------------------- */
    w[0] = 500; w[1] = 0; w[2] = 500; w[3] = 0;
    CHECK_EQI(map_layout(w, 4, &area, out), 2);
    CHECK(crect_empty(&out[1]));
    CHECK(crect_empty(&out[3]));
    CHECK(!crect_empty(&out[0]));
    CHECK(!crect_empty(&out[2]));
    mp_cover(out, 4, &area, &mn, &mx);
    CHECK_EQI(mn, 1);                    /* the zeros cost no coverage     */
    CHECK_EQI(mx, 1);

    /* ---- byte counts big enough to overflow naive arithmetic ---------- */
    w[0] = 1900000000L; w[1] = 1000000000L; w[2] = 500000000L;
    CHECK_EQI(map_layout(w, 3, &area, out), 3);
    mp_cover(out, 3, &area, &mn, &mx);
    CHECK_EQI(mn, 1);
    CHECK_EQI(mx, 1);
    CHECK(mp_area(&out[0]) > mp_area(&out[1]));
    CHECK(mp_area(&out[1]) > mp_area(&out[2]));

    /* ---- a box only one pixel tall: still tiles, nothing escapes ------- */
    {
        CRect thin = crect_make(0, 0, GW, 1);
        w[0] = 5; w[1] = 3; w[2] = 2;
        map_layout(w, 3, &thin, out);
        mp_cover(out, 3, &thin, &mn, &mx);
        CHECK_EQI(mn, 1);
        CHECK_EQI(mx, 1);
    }

    /* ---- an offset box: the layout lands where it was told ------------ */
    {
        CRect off = crect_make(11, 7, 40, 30);
        w[0] = 7; w[1] = 5; w[2] = 3; w[3] = 1;
        CHECK_EQI(map_layout(w, 4, &off, out), 4);
        mp_cover(out, 4, &off, &mn, &mx);
        CHECK_EQI(mn, 1);
        CHECK_EQI(mx, 1);
        for (i = 0; i < 4; i++) {
            CHECK(out[i].x0 >= off.x0 && out[i].x1 <= off.x1);
            CHECK(out[i].y0 >= off.y0 && out[i].y1 <= off.y1);
        }
    }

    /* ---- unsorted input still tiles (it is only less square) ---------- */
    w[0] = 10; w[1] = 900; w[2] = 40; w[3] = 300; w[4] = 90;
    CHECK_EQI(map_layout(w, 5, &area, out), 5);
    mp_cover(out, 5, &area, &mn, &mx);
    CHECK_EQI(mn, 1);
    CHECK_EQI(mx, 1);

    /* ---- the sort that makes it square -------------------------------- */
    CHECK_EQI(map_sort_index(w, 5, order), 5);
    CHECK_EQI(order[0], 1);              /* 900 */
    CHECK_EQI(order[1], 3);              /* 300 */
    CHECK_EQI(order[2], 4);              /* 90  */
    CHECK_EQI(order[3], 2);              /* 40  */
    CHECK_EQI(order[4], 0);              /* 10  */
    /* Ties keep their input order, so equal siblings do not reshuffle. */
    w[0] = 5; w[1] = 5; w[2] = 5;
    map_sort_index(w, 3, order);
    CHECK_EQI(order[0], 0);
    CHECK_EQI(order[1], 1);
    CHECK_EQI(order[2], 2);

    /* ---- hit testing --------------------------------------------------- */
    w[0] = 600; w[1] = 300; w[2] = 100;
    map_layout(w, 3, &area, out);
    for (i = 0; i < 3; i++) {
        int cx = (out[i].x0 + out[i].x1) / 2;
        int cy = (out[i].y0 + out[i].y1) / 2;
        CHECK_EQI(map_hit(out, 3, cx, cy), i);
    }
    CHECK_EQI(map_hit(out, 3, -1, -1), -1);
    CHECK_EQI(map_hit(out, 3, GW + 5, 0), -1);
    CHECK_EQI(map_hit(NULL, 3, 0, 0), -1);

    /* ---- refusals ------------------------------------------------------ */
    CHECK_EQI(map_layout(NULL, 3, &area, out), 0);
    CHECK_EQI(map_layout(w, 3, &area, NULL), 0);
    CHECK_EQI(map_layout(w, 3, NULL, out), 0);
    CHECK_EQI(map_layout(w, 0, &area, out), 0);
    CHECK_EQI(map_sort_index(NULL, 3, order), 0);
    CHECK_EQI(map_sort_index(w, 3, NULL), 0);
    {
        CRect empty = crect_make(4, 4, 0, 0);
        CHECK_EQI(map_layout(w, 3, &empty, out), 0);
    }
    {
        long zeros[3];
        zeros[0] = 0; zeros[1] = 0; zeros[2] = 0;
        CHECK_EQI(map_layout(zeros, 3, &area, out), 0);
        CHECK(crect_empty(&out[0]));
    }
    /* More items than the cap: the extras are dropped, not scribbled past
     * the end of the caller's array. */
    {
        CRect big[MAP_MAX_ITEMS];
        for (i = 0; i < MAP_MAX_ITEMS; i++) { w[i] = MAP_MAX_ITEMS - i; }
        CHECK(map_layout(w, MAP_MAX_ITEMS + 40, &area, big) > 0);
        mp_cover(big, MAP_MAX_ITEMS, &area, &mn, &mx);
        CHECK_EQI(mx, 1);
    }
}
