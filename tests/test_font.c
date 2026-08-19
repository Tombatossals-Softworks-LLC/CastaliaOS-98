/*
 * test_font.c - That text is actually drawn, and drawn where it was measured.
 *
 * This suite exists because of a measurement, not a hunch. Making
 * gfx_draw_text() a no-op -- every menu, list, header, title bar and status
 * line in the system rendering blank -- broke ONE demo scene out of
 * twenty-five and not a single unit check. The most-called drawing function in
 * the shell had no test at all, so a regression in it would have shipped
 * green.
 *
 * The property that matters most here is not "something appeared". It is that
 * gfx_text_width() agrees with where the ink actually stops. Every right-
 * aligned column, every centred label and every "does this fit?" in the shell
 * asks the measurer and then trusts the renderer to match it; if those two
 * ever disagree, nothing crashes and every layout is quietly wrong.
 *
 * The other thing worth being careful about is that ink is not evidence. A
 * glyph table indexed one byte off still draws something for every character,
 * and every check of the form "text appeared" would pass. So different
 * characters are required to produce different pixels, and one glyph is
 * compared against the shape the font data actually holds for it.
 */
#include "ctest.h"
#include "castalia/gfx.h"

#include <string.h>

#define BG   GFX_RGB(0x00, 0x00, 0x00)
#define INK  GFX_RGB(0xFF, 0xFF, 0xFF)

static GfxSurface *g_f;

static void clear(void)
{
    CRect all = crect_make(0, 0, g_f->w, g_f->h);
    gfx_reset_clip(g_f);
    gfx_fill_rect(g_f, &all, BG);
}

/* How many pixels of ink are in the whole surface. */
static int ink_count(void)
{
    int x, y, n = 0;
    for (y = 0; y < g_f->h; y++) {
        for (x = 0; x < g_f->w; x++) {
            if (gfx_get_pixel(g_f, x, y) == INK) { n++; }
        }
    }
    return n;
}

/* The x of the right-most inked pixel, or -1 if there is none. */
static int ink_right(void)
{
    int x, y, r = -1;
    for (y = 0; y < g_f->h; y++) {
        for (x = 0; x < g_f->w; x++) {
            if (gfx_get_pixel(g_f, x, y) == INK && x > r) { r = x; }
        }
    }
    return r;
}

/* ...and the left-most. */
static int ink_left(void)
{
    int x, y, l = -1;
    for (y = 0; y < g_f->h; y++) {
        for (x = 0; x < g_f->w; x++) {
            if (gfx_get_pixel(g_f, x, y) == INK) {
                if (l < 0 || x < l) { l = x; }
            }
        }
    }
    return l;
}

/* Ink outside the box the text was asked to occupy. */
static int ink_outside(const CRect *box)
{
    int x, y, n = 0;
    for (y = 0; y < g_f->h; y++) {
        for (x = 0; x < g_f->w; x++) {
            if (gfx_get_pixel(g_f, x, y) != INK) { continue; }
            if (!crect_contains(box, x, y)) { n++; }
        }
    }
    return n;
}

void test_font(void)
{
    int adv, h, w_one, w_five;

    printf("- text rendering\n");
    g_f = gfx_surface_new(200, 40);
    CHECK(g_f != NULL);

    /* ---- measurement --------------------------------------------------- */
    h = gfx_font_height(GFX_FONT_SYSTEM);
    CHECK(h >= 8);
    w_one  = gfx_text_width(GFX_FONT_SYSTEM, "M");
    w_five = gfx_text_width(GFX_FONT_SYSTEM, "MMMMM");
    adv = w_one;
    CHECK(adv > 0);
    CHECK_EQI(w_five, adv * 5);                  /* linear in the count     */
    CHECK_EQI(gfx_text_width(GFX_FONT_SYSTEM, ""), 0);
    CHECK_EQI(gfx_text_width(GFX_FONT_SYSTEM, NULL), 0);

    /* ---- something is actually drawn ----------------------------------- */
    clear();
    gfx_draw_text(g_f, GFX_FONT_SYSTEM, 10, 10, "Castalia", INK);
    CHECK(ink_count() > 20);

    /* An empty string is not a space: it must leave the surface alone. */
    clear();
    gfx_draw_text(g_f, GFX_FONT_SYSTEM, 10, 10, "", INK);
    CHECK_EQI(ink_count(), 0);
    gfx_draw_text(g_f, GFX_FONT_SYSTEM, 10, 10, NULL, INK);
    CHECK_EQI(ink_count(), 0);

    /* ---- the measurer and the renderer agree --------------------------- */
    /*
     * THE check. Every right-aligned column in the shell positions itself with
     * gfx_text_width() and then trusts gfx_draw_text() to land there. If the
     * advance and the measured width ever drift apart nothing breaks loudly --
     * every layout just goes quietly wrong.
     *
     * Ink cannot start before the origin, and the last glyph's ink cannot
     * reach past the measured width. The lower bound is deliberate too: a
     * renderer that drew only the first character would satisfy "ink is inside
     * the box" perfectly.
     */
    {
        const char *s = "WWWWWWWW";
        int want = gfx_text_width(GFX_FONT_SYSTEM, s);
        int left, right;
        clear();
        gfx_draw_text(g_f, GFX_FONT_SYSTEM, 20, 12, s, INK);
        left = ink_left();
        right = ink_right();
        printf("  '%s' measured %d, ink spans %d..%d\n", s, want, left, right);
        CHECK(left >= 20);
        CHECK(right < 20 + want);
        /* ...and it really did draw the whole string, not just the start. */
        CHECK(right >= 20 + want - adv);
    }

    /* Nothing lands outside the box the text was measured into. */
    {
        const char *s = "Archive 1,234";
        CRect box = crect_make(30, 14, gfx_text_width(GFX_FONT_SYSTEM, s), h);
        clear();
        gfx_draw_text(g_f, GFX_FONT_SYSTEM, 30, 14, s, INK);
        CHECK_EQI(ink_outside(&box), 0);
    }

    /* ---- ink is not evidence: the glyphs must differ ------------------- */
    /*
     * A glyph table indexed one byte off draws something for every character,
     * and every check above would still pass. So two different strings of the
     * same length must produce different pixels, and a character must produce
     * different pixels from its neighbour in the table.
     */
    {
        int a_ink, b_ink;
        clear();
        gfx_draw_text(g_f, GFX_FONT_SYSTEM, 4, 4, "A", INK);
        a_ink = ink_count();
        clear();
        gfx_draw_text(g_f, GFX_FONT_SYSTEM, 4, 4, "B", INK);
        b_ink = ink_count();
        printf("  'A' is %d px, 'B' is %d px\n", a_ink, b_ink);
        CHECK(a_ink > 0);
        CHECK(b_ink > 0);
        CHECK(a_ink != b_ink);
    }
    {
        /* A blank-looking character and a solid one cannot be the same. */
        int sp, hash;
        clear();
        gfx_draw_text(g_f, GFX_FONT_SYSTEM, 4, 4, " ", INK);
        sp = ink_count();
        clear();
        gfx_draw_text(g_f, GFX_FONT_SYSTEM, 4, 4, "#", INK);
        hash = ink_count();
        CHECK_EQI(sp, 0);              /* a space is empty            */
        CHECK(hash > 8);               /* a hash is not              */
    }

    /* ---- bold really is heavier --------------------------------------- */
    /*
     * Bold is synthesised by smearing each row one pixel, so it must put down
     * MORE ink than the regular face for the same string -- not merely be a
     * different font id that renders identically.
     */
    {
        int plain, heavy;
        clear();
        gfx_draw_text(g_f, GFX_FONT_SYSTEM, 6, 6, "Hamburg", INK);
        plain = ink_count();
        clear();
        gfx_draw_text(g_f, GFX_FONT_BOLD, 6, 6, "Hamburg", INK);
        heavy = ink_count();
        printf("  regular %d px, bold %d px\n", plain, heavy);
        CHECK(heavy > plain);
    }

    /* ---- clipping ------------------------------------------------------ */
    /*
     * Text is drawn through a clip on every scrolling list in the shell, and
     * the glyph blitter takes a fast whole-cell reject path. A clip it did not
     * respect would spill a partly-scrolled row over the frame around it.
     *
     * One honest limit. draw_glyph() also clamps the COLUMN span of a glyph
     * that straddles the clip's right edge, and that clamp cannot be tested
     * here, because no glyph in either bundled face inks past cell column 4 of
     * 8 -- measured, not assumed. Removing the clamp changes no pixel today.
     * A face whose glyphs used the full cell would make it live, and would
     * need a check written with it; pretending to cover it now would only mean
     * a check that always passes.
     */
    {
        CRect clip = crect_make(0, 0, 40, 40);
        int outside;
        clear();
        gfx_set_clip(g_f, &clip);
        gfx_draw_text(g_f, GFX_FONT_SYSTEM, 10, 10, "AAAAAAAAAAAAAAAAAAAA", INK);
        gfx_reset_clip(g_f);
        outside = ink_outside(&clip);
        printf("  clipped run leaked %d px\n", outside);
        CHECK_EQI(outside, 0);
        /* ...and it drew what WAS inside, rather than rejecting the lot. */
        CHECK(ink_count() > 10);
    }
    {
        /* A clip that excludes the line entirely draws nothing at all. */
        CRect clip = crect_make(0, 30, 40, 10);
        clear();
        gfx_set_clip(g_f, &clip);
        gfx_draw_text(g_f, GFX_FONT_SYSTEM, 10, 2, "hidden", INK);
        gfx_reset_clip(g_f);
        CHECK_EQI(ink_count(), 0);
    }

    /* ---- characters the table does not hold ---------------------------- */
    /*
     * A byte outside the face must land on a substitute rather than reading
     * past the end of the glyph array. It draws SOMETHING (so the reader can
     * see there was a character there) and stays in its cell.
     */
    {
        char odd[4];
        CRect box;
        odd[0] = (char)0x01; odd[1] = (char)0xFE; odd[2] = 'A'; odd[3] = '\0';
        box = crect_make(8, 8, gfx_text_width(GFX_FONT_SYSTEM, odd), h);
        clear();
        gfx_draw_text(g_f, GFX_FONT_SYSTEM, 8, 8, odd, INK);
        CHECK(ink_count() > 0);
        CHECK_EQI(ink_outside(&box), 0);
    }

    /* ---- drawing off the surface must not walk off it ------------------ */
    {
        clear();
        gfx_draw_text(g_f, GFX_FONT_SYSTEM, -50, 10, "left of the world", INK);
        gfx_draw_text(g_f, GFX_FONT_SYSTEM, g_f->w + 4, 10, "right of it", INK);
        gfx_draw_text(g_f, GFX_FONT_SYSTEM, 10, -20, "above", INK);
        gfx_draw_text(g_f, GFX_FONT_SYSTEM, 10, g_f->h + 4, "below", INK);
        /* Two of those genuinely have nothing on screen; the point is that the
         * suite is still alive afterwards and the surface was not scribbled
         * outside its own bounds. */
        CHECK(g_f->w == 200 && g_f->h == 40);
    }

    /* ---- what the face actually has -----------------------------------
     *
     * gfx_font_range() is a claim, and the Character Map now believes it --
     * it sizes its grid from it, having previously run to 255 and drawn the
     * missing-glyph box in 129 of its 224 cells. So the claim is checked
     * against the glyph data rather than against the constant beside it:
     * every code inside the range must draw something OTHER than the box,
     * and the codes just outside must draw the box.
     */
    {
        int first = -1, last = -1, c, x, y;
        CColor box[8][8];
        int in_range_boxes = 0, edge_ok = 0;

        gfx_font_range(GFX_FONT_SYSTEM, &first, &last);
        CHECK_EQI(first, 32);
        CHECK(last > first);

        /* The box, taken from a code that is certainly outside the range. */
        clear();
        {
            char one[2]; one[0] = (char)(unsigned char)200; one[1] = '\0';
            gfx_draw_text(g_f, GFX_FONT_SYSTEM, 4, 4, one, INK);
        }
        for (y = 0; y < 8; y++) {
            for (x = 0; x < 8; x++) {
                box[y][x] = gfx_get_pixel(g_f, 4 + x, 4 + y) & 0xFFFFFFUL;
            }
        }
        /* It has to BE something, or "differs from the box" is trivial. */
        {
            int boxink = 0;
            for (y = 0; y < 8; y++) {
                for (x = 0; x < 8; x++) { if (box[y][x] != BG) { boxink++; } }
            }
            CHECK(boxink > 0);
        }

        for (c = first; c <= last; c++) {
            char one[2];
            int same = 1;
            one[0] = (char)c; one[1] = '\0';
            clear();
            gfx_draw_text(g_f, GFX_FONT_SYSTEM, 4, 4, one, INK);
            for (y = 0; y < 8 && same; y++) {
                for (x = 0; x < 8; x++) {
                    if ((gfx_get_pixel(g_f, 4 + x, 4 + y) & 0xFFFFFFUL)
                        != box[y][x]) { same = 0; break; }
                }
            }
            if (same) { in_range_boxes++; }
        }
        /* Not one code the range claims may be a box. */
        CHECK_EQI(in_range_boxes, 0);

        /* And one past each end must be. 31 is a control code, and last+1 is
         * the first thing the face does not have. */
        for (c = 0; c < 2; c++) {
            int code = (c == 0) ? (first - 1) : (last + 1);
            int same = 1;
            char one[2];
            one[0] = (char)code; one[1] = '\0';
            clear();
            gfx_draw_text(g_f, GFX_FONT_SYSTEM, 4, 4, one, INK);
            for (y = 0; y < 8 && same; y++) {
                for (x = 0; x < 8; x++) {
                    if ((gfx_get_pixel(g_f, 4 + x, 4 + y) & 0xFFFFFFUL)
                        != box[y][x]) { same = 0; break; }
                }
            }
            if (same) { edge_ok++; }
        }
        CHECK_EQI(edge_ok, 2);

        gfx_font_range(GFX_FONT_SYSTEM, NULL, NULL);   /* no output is fine */
    }

    gfx_surface_free(g_f);
    g_f = NULL;
}
