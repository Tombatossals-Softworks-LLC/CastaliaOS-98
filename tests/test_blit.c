/*
 * test_blit.c - Copying pixels from one surface to another (gfx_blit).
 *
 * Another one the mutation sweep found: making gfx_blit() copy nothing broke
 * no unit check, no demo scene and no abuse world. That is the function behind
 * every desktop icon, every File Manager thumbnail, the cursor sprites, the
 * Start orb, the wallpaper -- and, every single frame, the blit of the cached
 * desktop background onto the screen. The desktop could have gone blank and
 * the whole gate would have reported success.
 *
 * The checks are about placement and about what is NOT written. A blit that
 * lands one pixel off, or that writes past its destination rectangle, produces
 * a picture that still looks like a picture, so "something was copied" is not
 * a useful thing to assert. Every case here fills the destination with a
 * sentinel first and requires every pixel outside the expected rectangle to
 * still be the sentinel afterwards.
 */
#include "ctest.h"
#include "castalia/gfx.h"

#define SENT GFX_RGB(0x11, 0x22, 0x33)

static GfxSurface *g_dst;
static GfxSurface *g_src;

/* Source pixel values are a function of position, so a blit that lands in the
 * wrong place is visible as a wrong VALUE rather than merely a wrong count. */
static CColor src_at(int x, int y) { return GFX_RGB(x * 8 + 1, y * 8 + 1, 0x40); }

static void fill_sentinel(void)
{
    CRect all = crect_make(0, 0, g_dst->w, g_dst->h);
    gfx_reset_clip(g_dst);
    gfx_fill_rect(g_dst, &all, SENT);
}

/* Pixels inside 'box' that are still the sentinel (i.e. were not written). */
static int unwritten_in(const CRect *box)
{
    int x, y, n = 0;
    for (y = box->y0; y < box->y1; y++) {
        for (x = box->x0; x < box->x1; x++) {
            if (gfx_get_pixel(g_dst, x, y) == SENT) { n++; }
        }
    }
    return n;
}

/* Pixels outside 'box' that are NOT the sentinel (i.e. were written and
 * should not have been). */
static int written_outside(const CRect *box)
{
    int x, y, n = 0;
    for (y = 0; y < g_dst->h; y++) {
        for (x = 0; x < g_dst->w; x++) {
            if (crect_contains(box, x, y)) { continue; }
            if (gfx_get_pixel(g_dst, x, y) != SENT) { n++; }
        }
    }
    return n;
}

void test_blit(void)
{
    int x, y, wrong;

    printf("- surface blit\n");
    g_dst = gfx_surface_new(40, 30);
    g_src = gfx_surface_new(8, 6);
    CHECK(g_dst != NULL);
    CHECK(g_src != NULL);
    for (y = 0; y < 6; y++) {
        for (x = 0; x < 8; x++) { g_src->pixels[y * g_src->pitch + x] = src_at(x, y); }
    }

    /* ---- a whole-surface copy lands where it was told ------------------ */
    {
        CRect box = crect_make(10, 5, 8, 6);
        fill_sentinel();
        gfx_blit(g_dst, 10, 5, g_src, NULL, GFX_BLIT_COPY);
        wrong = 0;
        for (y = 0; y < 6; y++) {
            for (x = 0; x < 8; x++) {
                if (gfx_get_pixel(g_dst, 10 + x, 5 + y) != src_at(x, y)) { wrong++; }
            }
        }
        CHECK_EQI(wrong, 0);
        CHECK_EQI(written_outside(&box), 0);
        CHECK_EQI(unwritten_in(&box), 0);
    }

    /* ---- a sub-rectangle copies only itself ---------------------------- */
    {
        CRect part = crect_make(2, 1, 3, 2);      /* src x2..4, y1..2      */
        CRect box  = crect_make(20, 20, 3, 2);
        fill_sentinel();
        gfx_blit(g_dst, 20, 20, g_src, &part, GFX_BLIT_COPY);
        wrong = 0;
        for (y = 0; y < 2; y++) {
            for (x = 0; x < 3; x++) {
                if (gfx_get_pixel(g_dst, 20 + x, 20 + y) != src_at(2 + x, 1 + y)) {
                    wrong++;
                }
            }
        }
        CHECK_EQI(wrong, 0);
        CHECK_EQI(written_outside(&box), 0);
    }

    /* ---- keyed: the key colour is a hole, not a colour ------------------ */
    /*
     * Every icon in the system is a keyed blit -- the magenta is what makes an
     * icon icon-shaped rather than a magenta square. So the check is in both
     * directions: the keyed pixels must be left alone AND the others must
     * still arrive, because a keyed blit that copied nothing would satisfy the
     * first half perfectly.
     */
    {
        CRect box = crect_make(4, 4, 8, 6);
        int kept = 0, copied = 0;
        for (x = 0; x < 8; x++) { g_src->pixels[2 * g_src->pitch + x] = GFX_COLORKEY; }
        fill_sentinel();
        gfx_blit(g_dst, 4, 4, g_src, NULL, GFX_BLIT_KEYED);
        for (y = 0; y < 6; y++) {
            for (x = 0; x < 8; x++) {
                CColor got = gfx_get_pixel(g_dst, 4 + x, 4 + y);
                if (y == 2) {
                    if (got == SENT) { kept++; }         /* row left alone   */
                } else {
                    if (got == src_at(x, y)) { copied++; }
                }
            }
        }
        CHECK_EQI(kept, 8);                              /* the whole row    */
        CHECK_EQI(copied, 8 * 5);                        /* everything else  */
        CHECK_EQI(written_outside(&box), 0);
        /* ...and a COPY of the same source does write the key row, so the
         * difference above is the mode and not the data. */
        fill_sentinel();
        gfx_blit(g_dst, 4, 4, g_src, NULL, GFX_BLIT_COPY);
        CHECK_EQI((long)gfx_get_pixel(g_dst, 4, 6), (long)GFX_COLORKEY);
        /* put the row back */
        for (x = 0; x < 8; x++) { g_src->pixels[2 * g_src->pitch + x] = src_at(x, 2); }
    }

    /* ---- clipping ------------------------------------------------------ */
    {
        CRect clip = crect_make(0, 0, 12, 12);
        CRect box  = crect_make(8, 8, 4, 4);   /* the part inside the clip  */
        fill_sentinel();
        gfx_set_clip(g_dst, &clip);
        gfx_blit(g_dst, 8, 8, g_src, NULL, GFX_BLIT_COPY);
        gfx_reset_clip(g_dst);
        CHECK_EQI(written_outside(&box), 0);
        /* The surviving corner has to be the RIGHT corner of the source, not
         * the top-left of it shifted -- a clip that moved the source origin
         * would still fill the same box with the wrong pixels. */
        wrong = 0;
        for (y = 0; y < 4; y++) {
            for (x = 0; x < 4; x++) {
                if (gfx_get_pixel(g_dst, 8 + x, 8 + y) != src_at(x, y)) { wrong++; }
            }
        }
        CHECK_EQI(wrong, 0);
    }

    /* ---- off the edges ------------------------------------------------- */
    /*
     * Negative destinations are the interesting half: the source origin has to
     * move with the clamp. A blit at (-3,-2) must show the source from (3,2)
     * onward at (0,0), not the source from (0,0).
     */
    {
        CRect box = crect_make(0, 0, 5, 4);
        fill_sentinel();
        gfx_blit(g_dst, -3, -2, g_src, NULL, GFX_BLIT_COPY);
        wrong = 0;
        for (y = 0; y < 4; y++) {
            for (x = 0; x < 5; x++) {
                if (gfx_get_pixel(g_dst, x, y) != src_at(3 + x, 2 + y)) { wrong++; }
            }
        }
        CHECK_EQI(wrong, 0);
        CHECK_EQI(written_outside(&box), 0);
    }
    {
        /* Entirely off: nothing is written and nothing walks off the end. */
        fill_sentinel();
        gfx_blit(g_dst, -20, 0, g_src, NULL, GFX_BLIT_COPY);
        gfx_blit(g_dst, g_dst->w + 2, 0, g_src, NULL, GFX_BLIT_COPY);
        gfx_blit(g_dst, 0, -20, g_src, NULL, GFX_BLIT_COPY);
        gfx_blit(g_dst, 0, g_dst->h + 2, g_src, NULL, GFX_BLIT_COPY);
        {
            CRect none = crect_make(0, 0, 0, 0);
            CHECK_EQI(written_outside(&none), 0);
        }
    }

    /* ---- refusals ------------------------------------------------------ */
    {
        CRect empty = crect_make(3, 3, 0, 0);
        CRect outside = crect_make(50, 50, 4, 4);   /* not in the source    */
        CRect none = crect_make(0, 0, 0, 0);
        fill_sentinel();
        gfx_blit(g_dst, 5, 5, g_src, &empty, GFX_BLIT_COPY);
        gfx_blit(g_dst, 5, 5, g_src, &outside, GFX_BLIT_COPY);
        CHECK_EQI(written_outside(&none), 0);
    }

    gfx_surface_free(g_src);
    gfx_surface_free(g_dst);
    g_src = NULL;
    g_dst = NULL;
}

/*
 * gfx_scale_map: the nearest-neighbour column table.
 *
 * Four scalers computed this inside their inner loops -- the wallpaper
 * stretch, the image viewer, the Control Center preview and pc_scale -- which
 * is a divide per PIXEL for a value that only depends on the column. The
 * wallpaper stretch alone did 480,000 per bake where 800 would do.
 *
 * These pin the exact indices, because the whole claim is that the picture
 * does not change: a scaler that is merely "close" would shift a thumbnail by
 * a pixel and nobody would notice until the screenshots stopped matching.
 */
void test_scale_map(void)
{
    int cols[16];
    int i;

    printf("- scale column map\n");

    /* 1:1 is the identity. */
    gfx_scale_map(cols, 8, 8);
    for (i = 0; i < 8; i++) { CHECK_EQI(cols[i], i); }

    /* Halving takes every other column, starting at 0. */
    gfx_scale_map(cols, 4, 8);
    CHECK_EQI(cols[0], 0); CHECK_EQI(cols[1], 2);
    CHECK_EQI(cols[2], 4); CHECK_EQI(cols[3], 6);

    /* Doubling repeats each column twice. */
    gfx_scale_map(cols, 8, 4);
    CHECK_EQI(cols[0], 0); CHECK_EQI(cols[1], 0);
    CHECK_EQI(cols[2], 1); CHECK_EQI(cols[3], 1);
    CHECK_EQI(cols[7], 3);

    /* A ratio that does not divide evenly: 10 -> 3. */
    gfx_scale_map(cols, 3, 10);
    CHECK_EQI(cols[0], 0); CHECK_EQI(cols[1], 3); CHECK_EQI(cols[2], 6);

    /* The last output column never runs off the end of the source. */
    gfx_scale_map(cols, 7, 5);
    CHECK(cols[6] <= 4);
    gfx_scale_map(cols, 16, 3);
    CHECK(cols[15] <= 2);
    /* ...and the map never goes backwards. */
    for (i = 1; i < 16; i++) { CHECK(cols[i] >= cols[i - 1]); }

    /* A one-pixel output takes the first column, not the middle. */
    gfx_scale_map(cols, 1, 100);
    CHECK_EQI(cols[0], 0);

    /* Degenerate input is answered, not divided by zero. */
    gfx_scale_map(cols, 4, 0);
    for (i = 0; i < 4; i++) { CHECK_EQI(cols[i], 0); }
    gfx_scale_map(cols, 4, -3);
    for (i = 0; i < 4; i++) { CHECK_EQI(cols[i], 0); }
    gfx_scale_map(NULL, 4, 4);      /* no crash */
    gfx_scale_map(cols, 0, 4);      /* no crash */
    gfx_scale_map(cols, -1, 4);     /* no crash */

    /* A wide source does not overflow the multiply: 4096 * 1600 needs the
     * long the implementation casts to. */
    gfx_scale_map(cols, 4, 4096);
    CHECK_EQI(cols[3], 3072);
}
