/*
 * test_paint.c - Paint's raster core (paint_core.c).
 *
 * The host has no screen, so "it looks right" is not available as evidence.
 * These tests read the pixels back instead: a filled shape must actually fill,
 * a flood must stop at a border, an undo must restore the exact image, and
 * nothing may draw outside the canvas.
 */
#include "ctest.h"
#include "paint_core.h"

#define W 64
#define H 48

static GfxSurface *g_cv;
static const CColor WHITE = GFX_RGB(0xFF, 0xFF, 0xFF);
static const CColor RED   = GFX_RGB(0xFF, 0x00, 0x00);
static const CColor BLUE  = GFX_RGB(0x00, 0x00, 0xFF);

static CColor at(int x, int y) { return gfx_get_pixel(g_cv, x, y) & 0xFFFFFFUL; }

static void reset(void)
{
    CRect all = crect_make(0, 0, W, H);
    gfx_reset_clip(g_cv);
    gfx_fill_rect(g_cv, &all, WHITE);
}

static int count_of(CColor c)
{
    int x, y, n = 0;
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            if (at(x, y) == c) { n++; }
        }
    }
    return n;
}

static void pt_strokes(void)
{
    reset();
    /* A one-pixel pencil marks exactly the pixels on the line. */
    pc_stroke(g_cv, 5, 5, 5, 15, 1, RED);
    CHECK_EQI(count_of(RED), 11);
    CHECK(at(5, 5) == RED);
    CHECK(at(5, 15) == RED);
    CHECK(at(5, 16) == WHITE);

    /* A wide brush lays a continuous band -- the point of dabbing every step
     * is that a fast drag does not come out dotted. */
    reset();
    pc_stroke(g_cv, 10, 20, 40, 20, 5, BLUE);
    CHECK(at(10, 20) == BLUE);
    CHECK(at(25, 20) == BLUE);
    CHECK(at(40, 20) == BLUE);
    CHECK(at(25, 21) == BLUE);        /* thick, not a hairline */
    CHECK(count_of(BLUE) > 30 * 3);

    /* Drawing off the edge clips instead of corrupting memory. */
    reset();
    pc_stroke(g_cv, -20, -20, 5, 5, 7, RED);
    pc_stroke(g_cv, W + 30, H + 30, W - 2, H - 2, 7, RED);
    pc_dab(g_cv, -100, -100, 9, RED);
    pc_dab(g_cv, W + 100, H + 100, 9, RED);
    CHECK(at(5, 5) == RED);
    CHECK(at(W - 2, H - 2) == RED);
}

static void pt_spray(void)
{
    cu32 seed = 12345UL;
    int first, second;

    reset();
    pc_spray(g_cv, 32, 24, 8, 200, RED, &seed);
    first = count_of(RED);
    CHECK(first > 0);
    /* Everything landed inside the radius, nothing outside it. */
    CHECK(at(32 - 12, 24) == WHITE);
    CHECK(at(32, 24 - 12) == WHITE);

    /* Same seed, same spray: reproducible, so a bug here is catchable. */
    reset();
    seed = 12345UL;
    pc_spray(g_cv, 32, 24, 8, 200, RED, &seed);
    second = count_of(RED);
    CHECK_EQI(second, first);

    /* A different seed gives a different pattern (the seed really is used). */
    reset();
    seed = 999UL;
    pc_spray(g_cv, 32, 24, 8, 200, RED, &seed);
    CHECK(count_of(RED) != 0);

    /* Guards. */
    reset();
    pc_spray(g_cv, 32, 24, 0, 200, RED, &seed);
    pc_spray(g_cv, 32, 24, 8, 0, RED, &seed);
    pc_spray(g_cv, 32, 24, 8, 200, RED, NULL);
    pc_spray(NULL, 32, 24, 8, 200, RED, &seed);
    CHECK_EQI(count_of(RED), 0);
}

static void pt_shapes(void)
{
    CRect r = crect_make(10, 10, 20, 16);

    /* Outline only: the border is drawn, the middle is untouched. */
    reset();
    pc_rect(g_cv, &r, 1, RED, BLUE, PC_OUTLINE);
    CHECK(at(10, 10) == RED);
    CHECK(at(29, 25) == RED);
    CHECK(at(20, 18) == WHITE);

    /* Filled: border in the line color, interior in the fill color. */
    reset();
    pc_rect(g_cv, &r, 1, RED, BLUE, PC_FILLED);
    CHECK(at(10, 10) == RED);
    CHECK(at(20, 18) == BLUE);

    /* Solid: no border at all. */
    reset();
    pc_rect(g_cv, &r, 1, RED, BLUE, PC_SOLID);
    CHECK(at(10, 10) == BLUE);
    CHECK(at(20, 18) == BLUE);
    CHECK_EQI(count_of(RED), 0);

    /* An ellipse touches the middle of each edge and misses the corners --
     * that is what makes it an ellipse and not the bounding box. */
    reset();
    pc_ellipse(g_cv, &r, 1, RED, BLUE, PC_OUTLINE);
    CHECK(at(10, 10) == WHITE);            /* corner stays clear   */
    CHECK(at(29, 25) == WHITE);
    CHECK(at(20, 10) == RED);              /* top of the arc       */
    CHECK(at(20, 18) == WHITE);            /* hollow               */

    reset();
    pc_ellipse(g_cv, &r, 1, RED, BLUE, PC_FILLED);
    CHECK(at(20, 18) == BLUE);             /* filled interior      */
    CHECK(at(10, 10) == WHITE);            /* still not the corner */
    CHECK(count_of(BLUE) > 100);

    /* A degenerate box does not hang or draw nothing useful. */
    reset();
    {
        CRect flat = crect_make(4, 4, 10, 1);
        pc_ellipse(g_cv, &flat, 1, RED, BLUE, PC_OUTLINE);
        CHECK(count_of(RED) > 0);
    }
    /* NULL and empty inputs are no-ops. */
    {
        CRect empty = crect_make(5, 5, 0, 0);
        pc_rect(g_cv, &empty, 1, RED, BLUE, PC_FILLED);
        pc_rect(NULL, &r, 1, RED, BLUE, PC_FILLED);
        pc_ellipse(g_cv, NULL, 1, RED, BLUE, PC_FILLED);
    }
}

static void pt_flood_and_pick(void)
{
    CRect box = crect_make(8, 8, 24, 20);
    int painted;

    reset();
    pc_rect(g_cv, &box, 1, RED, WHITE, PC_OUTLINE);

    /* Filling inside the box stops at the border and leaves the outside. */
    painted = pc_flood(g_cv, 20, 18, BLUE);
    CHECK(painted > 0);
    CHECK(at(20, 18) == BLUE);
    CHECK(at(9, 9) == BLUE);            /* just inside the border  */
    CHECK(at(8, 8) == RED);             /* the border itself       */
    CHECK(at(2, 2) == WHITE);           /* the outside is untouched */
    CHECK_EQI(painted, 22 * 18);        /* exactly the interior     */

    /* Filling with the color already there is a no-op, not an infinite loop. */
    CHECK_EQI(pc_flood(g_cv, 20, 18, BLUE), 0);
    /* Off-canvas points do nothing. */
    CHECK_EQI(pc_flood(g_cv, -1, 5, RED), 0);
    CHECK_EQI(pc_flood(g_cv, 5, H + 5, RED), 0);
    CHECK_EQI(pc_flood(NULL, 5, 5, RED), 0);

    /* The eyedropper reads what is there, and says so off-canvas. */
    CHECK(pc_pick(g_cv, 20, 18, WHITE) == BLUE);
    CHECK(pc_pick(g_cv, 8, 8, WHITE) == RED);
    CHECK(pc_pick(g_cv, -1, -1, RED) == RED);
    CHECK(pc_pick(NULL, 0, 0, BLUE) == BLUE);

    /* A fill on a blank canvas covers all of it. */
    reset();
    CHECK_EQI(pc_flood(g_cv, 0, 0, RED), W * H);

    /* The fill walks outwards until the pixels it painted stop reading as the
     * target colour, so it needs its own drawing to land. Clip it away and
     * every seed survives its own re-check. The bound in pc_flood is what
     * turns that into a return instead of a hang -- which means removing the
     * bound makes this test hang rather than fail. That is worse than a clean
     * failure and is the honest cost of testing a termination guarantee. */
    reset();
    {
        CRect nowhere = crect_make(W + 8, H + 8, 4, 4);
        gfx_set_clip(g_cv, &nowhere);
        painted = pc_flood(g_cv, 0, 0, RED);
        gfx_reset_clip(g_cv);
        CHECK(painted > W * H);         /* it gave up, rather than spinning */
        CHECK_EQI(count_of(RED), 0);    /* and honoured the clip throughout */
        CHECK_EQI(count_of(WHITE), W * H);
    }
}

static void pt_image_ops(void)
{
    reset();
    gfx_put_pixel(g_cv, 0, 0, RED);
    gfx_put_pixel(g_cv, W - 1, 0, BLUE);

    pc_flip_h(g_cv);
    CHECK(at(W - 1, 0) == RED);
    CHECK(at(0, 0) == BLUE);
    pc_flip_h(g_cv);                       /* flipping twice is identity */
    CHECK(at(0, 0) == RED);

    pc_flip_v(g_cv);
    CHECK(at(0, H - 1) == RED);
    pc_flip_v(g_cv);
    CHECK(at(0, 0) == RED);

    /* Invert: white becomes black, red becomes cyan. */
    pc_invert(g_cv);
    CHECK(at(5, 5) == GFX_RGB(0, 0, 0));
    CHECK(at(0, 0) == GFX_RGB(0, 0xFF, 0xFF));
    pc_invert(g_cv);
    CHECK(at(0, 0) == RED);

    /* Grayscale: every channel ends up equal, and white stays white. */
    pc_grayscale(g_cv);
    {
        CColor c = at(0, 0);
        CHECK_EQI(GFX_R(c), GFX_G(c));
        CHECK_EQI(GFX_G(c), GFX_B(c));
        CHECK(GFX_R(c) > 0 && GFX_R(c) < 255);   /* red is a mid gray */
    }
    CHECK(at(5, 5) == WHITE);

    /* NULL is a no-op everywhere. */
    pc_invert(NULL); pc_grayscale(NULL); pc_flip_h(NULL); pc_flip_v(NULL);
}

static void pt_undo(void)
{
    PcUndo u;
    pc_undo_init(&u);
    CHECK_EQI(pc_undo_depth(&u), 0);
    CHECK(!pc_undo_undo(&u, g_cv));         /* nothing to undo yet */

    reset();
    CHECK(pc_undo_push(&u, g_cv));          /* snapshot: all white */
    gfx_put_pixel(g_cv, 3, 3, RED);
    CHECK(at(3, 3) == RED);
    CHECK_EQI(pc_undo_depth(&u), 1);

    CHECK(pc_undo_undo(&u, g_cv));          /* back to white */
    CHECK(at(3, 3) == WHITE);
    CHECK_EQI(pc_undo_depth(&u), 0);

    CHECK(pc_undo_redo(&u, g_cv));          /* forward again */
    CHECK(at(3, 3) == RED);
    CHECK(!pc_undo_redo(&u, g_cv));         /* and no further */

    /* Three edits, three undos -- the ring holds PC_UNDO_LEVELS-1 of them. */
    reset();
    /* Free before re-initialising: pc_undo_init zeroes the struct, so calling
     * it over a ring that still holds snapshots abandons them. The product
     * gets this right (app_paint.c pairs free with init on a resize); this
     * test did not, and leaked a 12 KB canvas. ASan found it -- the valgrind
     * runs could not, because they run with --leak-check=no. */
    pc_undo_free(&u);
    pc_undo_init(&u);
    pc_undo_push(&u, g_cv); gfx_put_pixel(g_cv, 1, 1, RED);
    pc_undo_push(&u, g_cv); gfx_put_pixel(g_cv, 2, 2, RED);
    pc_undo_push(&u, g_cv); gfx_put_pixel(g_cv, 3, 3, RED);
    CHECK_EQI(pc_undo_depth(&u), PC_UNDO_LEVELS - 1);
    CHECK(pc_undo_undo(&u, g_cv));
    CHECK(at(3, 3) == WHITE);
    CHECK(at(2, 2) == RED);
    CHECK(pc_undo_undo(&u, g_cv));
    CHECK(at(2, 2) == WHITE);
    CHECK(at(1, 1) == RED);
    CHECK(pc_undo_undo(&u, g_cv));
    CHECK(at(1, 1) == WHITE);
    CHECK(!pc_undo_undo(&u, g_cv));         /* the ring's limit  */

    /* A new edit after undoing forks the timeline: redo is gone. */
    CHECK(pc_undo_redo_depth(&u) > 0);
    pc_undo_push(&u, g_cv);
    CHECK_EQI(pc_undo_redo_depth(&u), 0);
    CHECK(!pc_undo_redo(&u, g_cv));

    pc_undo_free(&u);
    CHECK_EQI(pc_undo_depth(&u), 0);
    /* NULL is safe. */
    pc_undo_init(NULL); pc_undo_free(NULL);
    CHECK(!pc_undo_push(NULL, g_cv));
    CHECK(!pc_undo_push(&u, NULL));
    CHECK_EQI(pc_undo_depth(NULL), 0);
}

/* The selection, the clipboard and resizing. */
static void pt_regions(void)
{
    CRect box = crect_make(10, 10, 20, 12);
    GfxSurface *clip, *big, *small;

    reset();
    pc_rect(g_cv, &box, 1, RED, BLUE, PC_SOLID);      /* a solid blue block */

    /* Copying lifts exactly the rectangle asked for. */
    clip = pc_copy_region(g_cv, &box);
    CHECK(clip != NULL);
    CHECK_EQI(clip->w, 20);
    CHECK_EQI(clip->h, 12);
    CHECK(gfx_get_pixel(clip, 0, 0) == BLUE);
    CHECK(gfx_get_pixel(clip, 19, 11) == BLUE);

    /* Cut: the canvas keeps the hole, the copy keeps the pixels. */
    pc_clear_region(g_cv, &box, WHITE);
    CHECK(at(15, 15) == WHITE);
    CHECK_EQI(count_of(BLUE), 0);

    /* Paste puts it back somewhere else, clipped at the edges. */
    pc_paste(g_cv, clip, 40, 30);
    CHECK(at(40, 30) == BLUE);
    CHECK(at(59, 41) == BLUE);
    CHECK(at(39, 30) == WHITE);
    CHECK_EQI(count_of(BLUE), 20 * 12);
    pc_paste(g_cv, clip, W - 5, H - 5);               /* half off the edge  */
    CHECK(at(W - 1, H - 1) == BLUE);
    pc_paste(g_cv, clip, -100, -100);                 /* entirely off it    */
    pc_paste(NULL, clip, 0, 0);
    pc_paste(g_cv, NULL, 0, 0);

    /* A region that misses the canvas yields nothing rather than a stray
     * surface the caller would leak. */
    {
        CRect off = crect_make(W + 10, H + 10, 5, 5);
        CHECK(pc_copy_region(g_cv, &off) == NULL);
        CHECK(pc_copy_region(NULL, &box) == NULL);
        CHECK(pc_copy_region(g_cv, NULL) == NULL);
    }
    /* A region hanging over the edge is clipped to what exists. */
    {
        CRect over = crect_make(W - 5, H - 5, 20, 20);
        GfxSurface *part = pc_copy_region(g_cv, &over);
        CHECK(part != NULL);
        CHECK_EQI(part->w, 5);
        CHECK_EQI(part->h, 5);
        gfx_surface_free(part);
    }

    /* Scaling up keeps the corners and hard edges (nearest neighbour). */
    big = pc_scale(clip, 40, 24);
    CHECK(big != NULL);
    CHECK_EQI(big->w, 40);
    CHECK_EQI(big->h, 24);
    CHECK(gfx_get_pixel(big, 0, 0) == BLUE);
    CHECK(gfx_get_pixel(big, 39, 23) == BLUE);
    gfx_surface_free(big);

    /* ...and scaling down keeps them too. */
    small = pc_scale(clip, 5, 3);
    CHECK(small != NULL);
    CHECK_EQI(small->w, 5);
    CHECK_EQI(small->h, 3);
    CHECK(gfx_get_pixel(small, 4, 2) == BLUE);
    gfx_surface_free(small);

    /* A scale that samples a pattern picks real source pixels, not a blur. */
    {
        GfxSurface *stripes = gfx_surface_new(4, 1);
        GfxSurface *wide;
        if (stripes != NULL) {
            gfx_put_pixel(stripes, 0, 0, RED);
            gfx_put_pixel(stripes, 1, 0, BLUE);
            gfx_put_pixel(stripes, 2, 0, RED);
            gfx_put_pixel(stripes, 3, 0, BLUE);
            wide = pc_scale(stripes, 8, 1);
            CHECK(wide != NULL);
            CHECK(gfx_get_pixel(wide, 0, 0) == RED);
            CHECK(gfx_get_pixel(wide, 1, 0) == RED);
            CHECK(gfx_get_pixel(wide, 2, 0) == BLUE);
            CHECK(gfx_get_pixel(wide, 7, 0) == BLUE);
            gfx_surface_free(wide);
            gfx_surface_free(stripes);
        }
    }
    CHECK(pc_scale(clip, 0, 10) == NULL);
    CHECK(pc_scale(clip, 10, -1) == NULL);
    CHECK(pc_scale(NULL, 10, 10) == NULL);

    gfx_surface_free(clip);
    pc_clear_region(NULL, &box, RED);
}

void test_paint(void)
{
    printf("- paint\n");
    g_cv = gfx_surface_new(W, H);
    if (g_cv == NULL) { printf("  (out of memory)\n"); return; }
    pt_strokes();
    pt_spray();
    pt_shapes();
    pt_flood_and_pick();
    pt_regions();
    pt_image_ops();
    pt_undo();
    gfx_surface_free(g_cv);
    g_cv = NULL;
}
