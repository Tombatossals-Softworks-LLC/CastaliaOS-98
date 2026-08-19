/*
 * test_ctrl.c - How the shared controls are DRAWN (ui_button.c, ui_scroll.c).
 *
 * Everything else under tests/ checks a computed value. This checks pixels,
 * because the thing being asserted is a visual contract and there is nowhere
 * else for it to live:
 *
 *   - a button's face is lit from above, and a pressed one is lit from below,
 *     which is what makes it read as pushed in beyond the sunken bevel alone;
 *   - a DISABLED button stays flat, because a gradient is what "lit" means
 *     here and a control that cannot be used should not look lit;
 *   - with the glossy switch off -- the 16/256-colour pipelines, and safe mode
 *     -- every one of those inverts and the controls go back to flat fills.
 *
 * That last group is the half that rots. Nobody looks at safe mode until they
 * need it, and by then a treatment that quietly started drawing gradients into
 * a sixteen-colour palette is somebody's unreadable recovery screen.
 *
 * The other thing checked here is containment: the surface is filled with a
 * sentinel first and every pixel outside the control's rectangle must still be
 * the sentinel afterwards. A one-pixel overdraw is invisible in a screenshot
 * and is exactly what an inset written the wrong way round produces.
 */
#include "ctest.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/shell.h"   /* the palette the system ships with */

#include <string.h>

#define SENTINEL GFX_RGB(0xFF, 0x00, 0xFF)
#define INK      GFX_RGB(0x00, 0x00, 0x00)

static GfxSurface *g_s;

/* Brightness of a pixel, as the sum of its channels: enough to say "lighter
 * than" without caring which channel carried it. */
static int lum(int x, int y)
{
    CColor c = gfx_get_pixel(g_s, x, y);
    return (int)((c >> 16) & 0xFF) + (int)((c >> 8) & 0xFF) + (int)(c & 0xFF);
}

static void clear_sentinel(void)
{
    CRect all = crect_make(0, 0, g_s->w, g_s->h);
    gfx_fill_rect(g_s, &all, SENTINEL);
}

/* Every pixel outside 'r' must be untouched. */
static int leaked(const CRect *r)
{
    int x, y, n = 0;
    for (y = 0; y < g_s->h; y++) {
        for (x = 0; x < g_s->w; x++) {
            if (crect_contains(r, x, y)) { continue; }
            if (gfx_get_pixel(g_s, x, y) != SENTINEL) { n++; }
        }
    }
    return n;
}

void test_ctrl(void)
{
    CRect btn = crect_make(4, 4, 60, 24);
    int top, bot, i;

    printf("- control rendering\n");
    g_s = gfx_surface_new(120, 120);
    CHECK(g_s != NULL);

    /* Interior sample rows: inside the two-pixel bevel, near each edge. */
#define TOP_Y (btn.y0 + 3)
#define BOT_Y (btn.y1 - 4)
#define MID_X (btn.x0 + 30)

    /* ---- glossy ------------------------------------------------------- */
    ui_set_glossy(CTRUE);

    clear_sentinel();
    ui_draw_button(g_s, &btn, "", UI_BTN_NORMAL);
    top = lum(MID_X, TOP_Y);
    bot = lum(MID_X, BOT_Y);
    printf("  normal   top %d bottom %d\n", top, bot);
    CHECK(top > bot);                 /* lit from above */
    CHECK_EQI(leaked(&btn), 0);

    clear_sentinel();
    ui_draw_button(g_s, &btn, "", UI_BTN_PRESSED);
    top = lum(MID_X, TOP_Y);
    bot = lum(MID_X, BOT_Y);
    printf("  pressed  top %d bottom %d\n", top, bot);
    CHECK(top < bot);                 /* ...and from below when pushed in */
    CHECK_EQI(leaked(&btn), 0);

    /*
     * Hover is VISIBLY different from resting -- everywhere down the face,
     * not at one point.
     *
     * It used to be "brighter at the top", and that is exactly the assertion
     * that let the treatment ship invisible: hover ran the gradient nearly to
     * white, which on a pale palette is where the resting gradient already
     * was, and "brighter" was still true by a couple of levels. Measured off
     * the running system, hovering a Calculator key changed 21 across three
     * channels. What matters is a difference somebody can see, and that it
     * holds at the bottom of the button as well as the top -- so both ends
     * are checked, against a floor.
     */
    {
        int rt = 0, rm = 0, rb = 0, ht, hm, hb;
        clear_sentinel();
        ui_draw_button(g_s, &btn, "", UI_BTN_NORMAL);
        rt = lum(MID_X, TOP_Y);
        rm = lum(MID_X, (TOP_Y + BOT_Y) / 2);
        rb = lum(MID_X, BOT_Y);
        clear_sentinel();
        ui_draw_button(g_s, &btn, "", UI_BTN_HOVER);
        ht = lum(MID_X, TOP_Y);
        hm = lum(MID_X, (TOP_Y + BOT_Y) / 2);
        hb = lum(MID_X, BOT_Y);
        printf("  hover    top %d/%d  mid %d/%d  bot %d/%d (hover/resting)\n",
               ht, rt, hm, rm, hb, rb);
        CHECK(ht - rt >= 24 || rt - ht >= 24);
        CHECK(hm - rm >= 24 || rm - hm >= 24);
        CHECK(hb - rb >= 24 || rb - hb >= 24);
        /* ...and it is one consistent shift, not a treatment that happens to
         * cross the resting one somewhere in the middle. */
        CHECK((ht < rt && hm < rm && hb < rb) ||
              (ht > rt && hm > rm && hb > rb));
    }
    /*
     * ...and the same, in the palette the system actually SHIPS with rather
     * than the greys this file defaults to.
     *
     * This is where the invisible hover lived. The old treatment was
     * face -> light: a real change on a grey palette and almost none on a
     * pale one, and the shipped theme's face and light are seven levels
     * apart per channel. Every check above passed against it, because every
     * check above ran on the greys.
     */
    {
        UiPalette saved = *ui_palette();
        ShTheme t;
        int rm, hm;
        sh_theme_default(&t);
        ui_set_palette(&t.ui);
        clear_sentinel();
        ui_draw_button(g_s, &btn, "", UI_BTN_NORMAL);
        rm = lum(MID_X, (TOP_Y + BOT_Y) / 2);
        clear_sentinel();
        ui_draw_button(g_s, &btn, "", UI_BTN_HOVER);
        hm = lum(MID_X, (TOP_Y + BOT_Y) / 2);
        printf("  hover    shipped palette mid %d/%d (hover/resting)\n",
               hm, rm);
        CHECK(hm - rm >= 24 || rm - hm >= 24);
        ui_set_palette(&saved);
    }

    top = lum(MID_X, TOP_Y);

    /* Disabled stays flat even with the treatment switched on. */
    clear_sentinel();
    ui_draw_button(g_s, &btn, "", UI_BTN_DISABLED);
    CHECK_EQI(lum(MID_X, TOP_Y), lum(MID_X, BOT_Y));
    CHECK_EQI(leaked(&btn), 0);

    /* The bevel is the treatment's business only on the inside: the outer two
     * pixels must read the same whether the gloss is on or off, so the classic
     * grammar survives underneath it. */
    {
        CColor edge_on, edge_off;
        clear_sentinel();
        ui_draw_button(g_s, &btn, "", UI_BTN_NORMAL);
        edge_on = gfx_get_pixel(g_s, btn.x0, btn.y0 + 8);
        ui_set_glossy(CFALSE);
        clear_sentinel();
        ui_draw_button(g_s, &btn, "", UI_BTN_NORMAL);
        edge_off = gfx_get_pixel(g_s, btn.x0, btn.y0 + 8);
        CHECK_EQI((long)edge_on, (long)edge_off);
    }

    /* ---- flat (16/256-colour pipelines, and safe mode) ----------------- */
    /* ui_set_glossy(CFALSE) is still in force from the check above. */
    CHECK(!ui_glossy());
    for (i = 0; i < 3; i++) {
        UiButtonState st = (i == 0) ? UI_BTN_NORMAL
                         : (i == 1) ? UI_BTN_PRESSED : UI_BTN_HOVER;
        clear_sentinel();
        ui_draw_button(g_s, &btn, "", st);
        /* One flat colour from top to bottom, in every state. */
        CHECK_EQI(lum(MID_X, TOP_Y), lum(MID_X, BOT_Y));
        CHECK_EQI(leaked(&btn), 0);
    }

    /* ---- sizes that would break an inset ------------------------------- */
    /* A button too small to have an interior must still draw its bevel and
     * still stay inside itself, rather than filling a negative rectangle. */
    ui_set_glossy(CTRUE);
    {
        CRect tiny[4];
        tiny[0] = crect_make(10, 10, 1, 1);
        tiny[1] = crect_make(10, 10, 4, 4);
        tiny[2] = crect_make(10, 10, 5, 3);
        tiny[3] = crect_make(0, 0, 2, 2);
        for (i = 0; i < 4; i++) {
            clear_sentinel();
            ui_draw_button(g_s, &tiny[i], "", UI_BTN_NORMAL);
            CHECK_EQI(leaked(&tiny[i]), 0);
        }
    }
    /* Clipped hard against the surface edge -- nothing to compare against
     * outside it, so this is only asking that it does not walk off. */
    {
        CRect edge = crect_make(g_s->w - 3, g_s->h - 3, 20, 20);
        clear_sentinel();
        ui_draw_button(g_s, &edge, "", UI_BTN_NORMAL);
        CHECK_EQI(leaked(&edge), 0);
    }

    /* ---- the clip only ever shrinks ------------------------------------ */
    /*
     * A button drawn with a clip already in force must not paint outside it.
     *
     * This is the containment check above asked the other way round, and it
     * is the half that was missing: every leak check here set no clip at all,
     * so "the label stays inside the button" was proven and "the button stays
     * inside whatever the caller had already restricted it to" was not.
     * ui_draw_button REPLACED the clip for every label it drew -- and the clip
     * it threw away is the window manager's, the one thing keeping an app
     * inside its own window. Measured on the running system: 385 pixels of a
     * Calculator's key labels scattered through an open Notepad in front of
     * it, from nothing but moving the mouse.
     */
    {
        CRect half = crect_make(20, 20, 30, 20);   /* the LEFT half of it   */
        CRect btn2 = crect_make(20, 20, 60, 20);
        CRect right = crect_make(50, 20, 30, 20);  /* ...so this stays clean */
        int x, y, dirty = 0;
        clear_sentinel();
        gfx_set_clip(g_s, &half);
        ui_draw_button(g_s, &btn2, "A VERY LONG LABEL", UI_BTN_NORMAL);
        gfx_reset_clip(g_s);
        for (y = right.y0; y < right.y1; y++) {
            for (x = right.x0; x < right.x1; x++) {
                if (gfx_get_pixel(g_s, x, y) != SENTINEL) { dirty++; }
            }
        }
        printf("  clip     %d px painted outside the clip\n", dirty);
        CHECK_EQI(dirty, 0);
        /* ...and the clip that was in force is still in force afterwards:
         * a control that narrows has to put back exactly what it found. */
        gfx_set_clip(g_s, &half);
        ui_draw_button(g_s, &btn2, "A VERY LONG LABEL", UI_BTN_NORMAL);
        {
            CRect now = gfx_get_clip(g_s);
            CHECK_EQI(now.x0, half.x0);
            CHECK_EQI(now.y0, half.y0);
            CHECK_EQI(now.x1, half.x1);
            CHECK_EQI(now.y1, half.y1);
        }
        gfx_reset_clip(g_s);
        /* The primitive itself: narrowing intersects and hands back what was
         * there, where gfx_set_clip would have widened straight past it. */
        {
            CRect outer = crect_make(10, 10, 40, 40);
            CRect wider = crect_make(0, 0, 200, 200);
            CRect was, now;
            gfx_set_clip(g_s, &outer);
            was = gfx_clip_narrow(g_s, &wider);
            now = gfx_get_clip(g_s);
            CHECK_EQI(was.x0, outer.x0);
            CHECK_EQI(was.y1, outer.y1);
            CHECK_EQI(now.x0, outer.x0);   /* not 0 */
            CHECK_EQI(now.y0, outer.y0);
            CHECK_EQI(now.x1, outer.x1);   /* not 200 */
            CHECK_EQI(now.y1, outer.y1);
            gfx_reset_clip(g_s);
        }
    }

    /* ---- which button the pointer is on -------------------------------- */
    /*
     * The treatments above existed from the start and almost nothing ever
     * asked for them: whole windows drew every button UI_BTN_NORMAL, so no
     * button in them ever lit under the pointer or sank when pressed. UiHot
     * is the shared answer to "which one", and this is its arithmetic --
     * pure, so it is checked here rather than by moving a mouse.
     */
    {
        UiHot h;
        /*
         * A ZEROED UiHot means nothing is hot. That is the check the whole
         * plus-one encoding exists for: every app struct in this system
         * arrives from sys_calloc, and a -1 sentinel would have every
         * toolbar in the shell draw its first button sunken until somebody
         * touched it, with nothing anywhere to hint at why.
         */
        memset(&h, 0, sizeof h);
        CHECK_EQI((int)ui_hot_state(&h, 0, UI_BTN_NORMAL), (int)UI_BTN_NORMAL);
        CHECK_EQI(ui_hot_pressed(&h), -1);

        ui_hot_init(&h);
        CHECK_EQI((int)ui_hot_state(&h, 0, UI_BTN_NORMAL), (int)UI_BTN_NORMAL);

        /* Hovering lights that one and nothing else... */
        CHECK(ui_hot_move(&h, 2));
        CHECK_EQI((int)ui_hot_state(&h, 2, UI_BTN_NORMAL), (int)UI_BTN_HOVER);
        CHECK_EQI((int)ui_hot_state(&h, 1, UI_BTN_NORMAL), (int)UI_BTN_NORMAL);
        /* ...and arriving where it already is is not a repaint. Every caller
         * uses this answer to decide whether to invalidate, so a CTRUE here
         * is a full window redraw on every pixel of mouse movement. */
        CHECK(!ui_hot_move(&h, 2));

        /* Pressing sinks it. */
        CHECK(ui_hot_press(&h, 2));
        CHECK_EQI((int)ui_hot_state(&h, 2, UI_BTN_NORMAL), (int)UI_BTN_PRESSED);
        CHECK_EQI(ui_hot_pressed(&h), 2);
        /* Sliding off the held button lifts it -- that is how somebody backs
         * out of a button they did not mean to press -- and nothing else
         * lights while it is held, or a drag across a keypad would leave a
         * trail of lit keys none of which is going to fire. */
        CHECK(ui_hot_move(&h, 3));
        CHECK_EQI((int)ui_hot_state(&h, 2, UI_BTN_NORMAL), (int)UI_BTN_NORMAL);
        CHECK_EQI((int)ui_hot_state(&h, 3, UI_BTN_NORMAL), (int)UI_BTN_NORMAL);
        /* Sliding back onto it sinks it again. */
        CHECK(ui_hot_move(&h, 2));
        CHECK_EQI((int)ui_hot_state(&h, 2, UI_BTN_NORMAL), (int)UI_BTN_PRESSED);
        /* Releasing leaves it hovered, and releasing again is not a repaint. */
        CHECK(ui_hot_release(&h));
        CHECK_EQI((int)ui_hot_state(&h, 2, UI_BTN_NORMAL), (int)UI_BTN_HOVER);
        CHECK(!ui_hot_release(&h));

        /* Leaving the window is index -1, and puts everything back. */
        CHECK(ui_hot_move(&h, -1));
        CHECK_EQI((int)ui_hot_state(&h, 2, UI_BTN_NORMAL), (int)UI_BTN_NORMAL);
        CHECK_EQI(ui_hot_pressed(&h), -1);

        /* A disabled button does not react: lighting one is a promise it
         * cannot keep. */
        ui_hot_init(&h);
        (void)ui_hot_move(&h, 1);
        CHECK_EQI((int)ui_hot_state(&h, 1, UI_BTN_DISABLED),
                  (int)UI_BTN_DISABLED);
        (void)ui_hot_press(&h, 1);
        CHECK_EQI((int)ui_hot_state(&h, 1, UI_BTN_DISABLED),
                  (int)UI_BTN_DISABLED);

        /* A LATCHED button stays latched under the pointer. Un-sinking the
         * one that is switched ON is the opposite of feedback -- Notepad's
         * Wrap and the File Manager's 2-Pane are drawn that way. */
        ui_hot_init(&h);
        (void)ui_hot_move(&h, 4);
        CHECK_EQI((int)ui_hot_state(&h, 4, UI_BTN_PRESSED),
                  (int)UI_BTN_PRESSED);

        /* NULL is answered rather than dereferenced: these are called from
         * message handlers that run before a window has its state. */
        CHECK(!ui_hot_move((UiHot *)0, 0));
        CHECK(!ui_hot_press((UiHot *)0, 0));
        CHECK(!ui_hot_release((UiHot *)0));
        CHECK_EQI(ui_hot_pressed((const UiHot *)0), -1);
        CHECK_EQI((int)ui_hot_state((const UiHot *)0, 0, UI_BTN_NORMAL),
                  (int)UI_BTN_NORMAL);
    }

    /* ---- the scrollbar is made of buttons ------------------------------ */
    /*
     * Not a restatement of the above: this is the wiring. ui_scroll.c draws
     * its arrows and its thumb by calling ui_draw_button, so the treatment
     * reaches every scrolling surface in the shell without any of them knowing
     * about it -- and if that ever stopped being true, the bar would go flat
     * while every button stayed glossy, and nothing else would notice.
     */
    {
        CRect bar = crect_make(4, 4, UI_SB_W, 100);
        int off = 0, len = 0, ty, by, t2, b2;
        /* The THUMB, not an arrow button: the arrows carry a glyph, and a
         * sample that lands on it measures the triangle rather than the face.
         * The first version of this check did exactly that, and "the top is
         * brighter than the bottom" passed on the strength of a dark arrow
         * head sitting in the lower sample. The thumb is a plain face with
         * nothing drawn on it, so there is nothing else for the reading to
         * come from.
         *
         * Its position comes from the same function the drawing uses, rather
         * than from arithmetic repeated here that could drift away from it. */
        ui_scroll_thumb(crect_h(&bar) - UI_SB_W * 2, 400, 100, 0, &off, &len);
        CHECK(len >= 8);                 /* enough thumb to sample two rows */
        ty = bar.y0 + UI_SB_W + off + 3;
        by = bar.y0 + UI_SB_W + off + len - 4;

        ui_set_glossy(CTRUE);
        clear_sentinel();
        ui_scrollbar_draw(g_s, &bar, CTRUE, 400, 100, 0, UI_SB_NONE);
        CHECK_EQI(leaked(&bar), 0);
        t2 = lum(bar.x0 + 7, ty);
        b2 = lum(bar.x0 + 7, by);
        printf("  sb thumb top %d bottom %d (rows %d..%d)\n", t2, b2, ty, by);
        CHECK(t2 > b2);

        ui_set_glossy(CFALSE);
        clear_sentinel();
        ui_scrollbar_draw(g_s, &bar, CTRUE, 400, 100, 0, UI_SB_NONE);
        CHECK_EQI(lum(bar.x0 + 7, ty), lum(bar.x0 + 7, by));
        CHECK_EQI(leaked(&bar), 0);
    }

    /* ---- menu navigation ---------------------------------------------
     *
     * Stepping a highlight past separators and disabled rows existed twice --
     * in the desktop menu and in the launcher -- and was missing from the File
     * Manager, whose context menu could therefore only be worked with a
     * mouse. One copy now, so it is worth testing once properly.
     */
    {
        UiMenu m;
        int i;

        ui_menu_clear(&m);
        ui_menu_add(&m, 1, "Open", CTRUE);
        ui_menu_add_separator(&m);
        ui_menu_add(&m, 2, "Cut", CTRUE);
        ui_menu_add(&m, 3, "Paste", CFALSE);      /* nothing on the clipboard */
        ui_menu_add(&m, 4, "Delete", CTRUE);
        m.highlight = -1;

        CHECK(ui_menu_selectable(&m, 0) == CTRUE);
        CHECK(ui_menu_selectable(&m, 1) == CFALSE);   /* separator */
        CHECK(ui_menu_selectable(&m, 3) == CFALSE);   /* disabled  */
        CHECK(ui_menu_selectable(&m, -1) == CFALSE);
        CHECK(ui_menu_selectable(&m, 5) == CFALSE);
        CHECK(ui_menu_selectable(NULL, 0) == CFALSE);

        /* From nothing highlighted, Down lands on the first real command --
         * which is what lets a menu opened by keyboard arrive ready to use. */
        CHECK(ui_menu_step(&m, 1) == CTRUE);
        CHECK_EQI(m.highlight, 0);
        /* ...and steps over the separator, not onto it. */
        CHECK(ui_menu_step(&m, 1) == CTRUE);
        CHECK_EQI(m.highlight, 2);
        /* ...and over the disabled row. */
        CHECK(ui_menu_step(&m, 1) == CTRUE);
        CHECK_EQI(m.highlight, 4);
        /* Wrapping at the bottom. */
        CHECK(ui_menu_step(&m, 1) == CTRUE);
        CHECK_EQI(m.highlight, 0);
        /* Wrapping at the top, backwards, skipping the same rows. */
        CHECK(ui_menu_step(&m, -1) == CTRUE);
        CHECK_EQI(m.highlight, 4);
        CHECK(ui_menu_step(&m, -1) == CTRUE);
        CHECK_EQI(m.highlight, 2);
        CHECK(ui_menu_step(&m, -1) == CTRUE);
        CHECK_EQI(m.highlight, 0);

        /* A direction of zero is not a step. */
        CHECK(ui_menu_step(&m, 0) == CFALSE);
        CHECK_EQI(m.highlight, 0);
        CHECK(ui_menu_step(NULL, 1) == CFALSE);

        /* One selectable row: stepping stays on it and reports NO movement,
         * so a caller does not repaint on every key. */
        ui_menu_clear(&m);
        ui_menu_add(&m, 1, "Only", CTRUE);
        m.highlight = 0;
        CHECK(ui_menu_step(&m, 1) == CFALSE);
        CHECK_EQI(m.highlight, 0);

        /* A menu with nothing choosable in it must come back rather than
         * spin looking for a row that is not there, and must not invent a
         * highlight on a separator. */
        ui_menu_clear(&m);
        for (i = 0; i < 4; i++) { ui_menu_add_separator(&m); }
        ui_menu_add(&m, 9, "Greyed", CFALSE);
        m.highlight = -1;
        CHECK(ui_menu_step(&m, 1) == CFALSE);
        CHECK_EQI(m.highlight, -1);
        CHECK(ui_menu_step(&m, -1) == CFALSE);
        CHECK_EQI(m.highlight, -1);

        /* An empty menu is a no-op, not a wrap onto index -1. */
        ui_menu_clear(&m);
        m.highlight = -1;
        CHECK(ui_menu_step(&m, 1) == CFALSE);
        CHECK_EQI(m.highlight, -1);
    }

    /* ---- the accelerator column ---------------------------------------- *
     *
     * A label may carry its shortcut after a tab: "Undo\tCtrl+Z". The menu
     * did not know that for as long as Paint has had shortcuts -- the whole
     * string went to gfx_draw_text, and 0x09 is below the font's first code
     * point, so it came out as the missing-glyph box wedged into the middle
     * of the row. Nothing failed, because nothing was looking.
     *
     * What is checked here is the MEASURE, which is the half that can be
     * wrong invisibly: a menu measured without room for the shortcuts still
     * draws them, right-aligned against an edge that is too close, and the
     * text walks out over the desktop.
     */
    {
        UiMenu m;
        int plain_w = 0, accel_w = 0, tall_w = 0, h = 0;

        ui_menu_clear(&m);
        ui_menu_add(&m, 1, "Undo", CTRUE);
        ui_menu_add(&m, 2, "Redo", CTRUE);
        ui_menu_measure(&m, GFX_FONT_SYSTEM, &plain_w, &h);

        ui_menu_clear(&m);
        ui_menu_add(&m, 1, "Undo\tCtrl+Z", CTRUE);
        ui_menu_add(&m, 2, "Redo\tCtrl+Y", CTRUE);
        ui_menu_measure(&m, GFX_FONT_SYSTEM, &accel_w, &h);

        /*
         * Room was made -- and specifically, room with a GAP in it.
         *
         * "wider than without the shortcut" is not enough, and this was
         * written that way first: the broken menu also measures wider,
         * because the tab is itself one glyph and gfx_text_width counts it.
         * Both weaker forms of this check passed against the very bug they
         * were written to catch. Demanding at least two characters of
         * clearance is what separates a reserved column from an accidental
         * one, and it is also the real requirement -- a shortcut butted up
         * against its label is unreadable.
         */
        CHECK(accel_w - plain_w >= gfx_text_width(GFX_FONT_SYSTEM, "Ctrl+Z  "));

        /*
         * The load-bearing case: the longest LABEL and the longest SHORTCUT
         * on different rows. The accelerators share one column, so the width
         * has to be widest-label plus widest-accelerator. Taking the maximum
         * of each row's total instead -- the obvious implementation -- makes
         * this menu too narrow, and "Select All" would sit under "Ctrl+Q".
         */
        ui_menu_clear(&m);
        ui_menu_add(&m, 1, "Select All", CTRUE);        /* long, no shortcut */
        ui_menu_add(&m, 2, "Go\tCtrl+Shift+Q", CTRUE);  /* short, long one   */
        ui_menu_measure(&m, GFX_FONT_SYSTEM, &tall_w, &h);
        CHECK(tall_w >= gfx_text_width(GFX_FONT_SYSTEM, "Select All") +
                        gfx_text_width(GFX_FONT_SYSTEM, "Ctrl+Shift+Q"));

        /* A tab at the very end names no shortcut and must not reserve a
         * column for one -- an empty accelerator is not a narrow accelerator. */
        ui_menu_clear(&m);
        ui_menu_add(&m, 1, "Undo\t", CTRUE);
        ui_menu_measure(&m, GFX_FONT_SYSTEM, &accel_w, &h);
        ui_menu_clear(&m);
        ui_menu_add(&m, 1, "Undo", CTRUE);
        ui_menu_measure(&m, GFX_FONT_SYSTEM, &plain_w, &h);
        CHECK_EQI(accel_w, plain_w);
    }

    /* ---- the rectangle a row is highlighted in ------------------------- *
     *
     * ui_menu_row_rect answers "where is row i", and ui_menu_hit answers
     * "which row is here". They are the same walk down the item heights in
     * opposite directions, and a window now repaints a highlight change
     * using the first while lighting it using the second -- so if they ever
     * disagree the menu paints one row and lights another, and the row the
     * pointer left keeps its highlight until something else repaints it.
     *
     * Checking them against EACH OTHER is what makes that impossible: a
     * hard-coded list of expected rectangles would only be a second copy of
     * the arithmetic, and would agree with a broken row_rect if the same
     * mistake were made twice.
     */
    {
        UiMenu m;
        CRect r, prev;
        int i, w = 0, h = 0;
        const int MX = 40, MY = 70;

        ui_menu_clear(&m);
        ui_menu_add(&m, 1, "New\tCtrl+N", CTRUE);
        ui_menu_add(&m, 2, "Open\tCtrl+O", CTRUE);
        ui_menu_add_separator(&m);
        ui_menu_add(&m, 3, "Save", CTRUE);
        ui_menu_add(&m, 4, "Print", CFALSE);      /* disabled, but a row */
        ui_menu_add_separator(&m);
        ui_menu_add(&m, 5, "Close", CTRUE);
        ui_menu_measure(&m, GFX_FONT_SYSTEM, &w, &h);

        /* Nothing outside the menu, and no rectangle for a separator. */
        CHECK(ui_menu_row_rect(&m, MX, MY, GFX_FONT_SYSTEM, -1, &r) == CFALSE);
        CHECK(ui_menu_row_rect(&m, MX, MY, GFX_FONT_SYSTEM, m.count, &r) == CFALSE);
        CHECK(ui_menu_row_rect(&m, MX, MY, GFX_FONT_SYSTEM, 2, &r) == CFALSE);
        CHECK(ui_menu_row_rect(&m, MX, MY, GFX_FONT_SYSTEM, 0, NULL) == CFALSE);
        CHECK(ui_menu_row_rect(NULL, MX, MY, GFX_FONT_SYSTEM, 0, &r) == CFALSE);

        for (i = 0; i < m.count; i++) {
            if (!ui_menu_row_rect(&m, MX, MY, GFX_FONT_SYSTEM, i, &r)) {
                continue;                       /* separator */
            }
            /* Inside the panel, and not over its bevel. */
            CHECK(r.x0 >= MX && r.x1 <= MX + w);
            CHECK(r.y0 >= MY && r.y1 <= MY + h);
            CHECK(crect_h(&r) > 0 && crect_w(&r) > 0);
            /*
             * ...and the row a point in it HITS is this row -- for the
             * enabled ones. A disabled row still occupies a rectangle (it
             * has to be repainted when the highlight leaves it) but
             * ui_menu_hit deliberately answers -1 for it, which is the one
             * place the two functions are allowed to differ.
             */
            if (m.items[i].enabled) {
                CHECK_EQI(ui_menu_hit(&m, MX, MY, GFX_FONT_SYSTEM,
                                      (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2), i);
                CHECK_EQI(ui_menu_hit(&m, MX, MY, GFX_FONT_SYSTEM,
                                      r.x0 + 1, r.y0), i);
                CHECK_EQI(ui_menu_hit(&m, MX, MY, GFX_FONT_SYSTEM,
                                      r.x0 + 1, r.y1 - 1), i);
            } else {
                CHECK_EQI(ui_menu_hit(&m, MX, MY, GFX_FONT_SYSTEM,
                                      (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2), -1);
            }
        }

        /* Rows do not overlap and go down the menu in order -- the check
         * that catches an off-by-one in the height walk, which the
         * per-row containment above cannot see. */
        prev = crect_make(0, MY, 0, 0);
        for (i = 0; i < m.count; i++) {
            if (!ui_menu_row_rect(&m, MX, MY, GFX_FONT_SYSTEM, i, &r)) {
                continue;
            }
            CHECK(r.y0 >= prev.y1);
            prev = r;
        }

        /* Moving the menu moves its rows with it, exactly. */
        {
            CRect a, b2;
            CHECK(ui_menu_row_rect(&m, MX, MY, GFX_FONT_SYSTEM, 3, &a));
            CHECK(ui_menu_row_rect(&m, MX + 17, MY + 23, GFX_FONT_SYSTEM, 3, &b2));
            CHECK_EQI(b2.x0 - a.x0, 17);
            CHECK_EQI(b2.y0 - a.y0, 23);
            CHECK_EQI(crect_w(&b2), crect_w(&a));
            CHECK_EQI(crect_h(&b2), crect_h(&a));
        }
    }

    /* Leave the switch where every other suite expects to find it. */
    ui_set_glossy(CFALSE);
    /* ---- a LABEL that does not fit in its button ----------------------- *
     *
     * Every containment check above passes an EMPTY label, so the one thing
     * that can actually escape a button was the one thing never drawn into
     * one. gfx_draw_text_rect does not clip, and HCENTER is
     * `x0 + (rw - tw) / 2` -- negative for a label wider than its box, which
     * starts the text left of the button and runs it out of both ends.
     * Measured before the fix: 168 pixels outside a 60-px button.
     */
    {
        CRect small = crect_make(20, 20, 60, 20);
        const char *longlabel = "A VERY LONG BUTTON LABEL";
        int st;
        for (st = 0; st < 4; st++) {
            UiButtonState state = (st == 0) ? UI_BTN_NORMAL
                                : (st == 1) ? UI_BTN_PRESSED
                                : (st == 2) ? UI_BTN_HOVER : UI_BTN_DISABLED;
            clear_sentinel();
            ui_draw_button(g_s, &small, longlabel, state);
            CHECK_EQI(leaked(&small), 0);
        }
        /* Flat treatment too -- the label is drawn the same way either side
         * of the glossy switch, so both need saying. */
        ui_set_glossy(CFALSE);
        clear_sentinel();
        ui_draw_button(g_s, &small, longlabel, UI_BTN_NORMAL);
        CHECK_EQI(leaked(&small), 0);
        ui_set_glossy(CTRUE);
        /* A button too narrow for even the ellipsis still stays inside. */
        {
            CRect narrow = crect_make(20, 60, 10, 16);
            clear_sentinel();
            ui_draw_button(g_s, &narrow, longlabel, UI_BTN_NORMAL);
            CHECK_EQI(leaked(&narrow), 0);
        }
        /*
         * Containment is the CLIP's doing, so the checks above pass with the
         * fitting removed -- measured: that mutation survived all of them.
         * What fitting adds is that the label is SHORTENED rather than sliced
         * through the middle: with it, a long label renders exactly as its
         * own fitted form does; without it, the centred full string shows its
         * middle and loses both ends.
         */
        {
            static CColor before[120 * 120];
            char want[96];
            int px, py, diff = 0;
            clear_sentinel();
            ui_draw_button(g_s, &small, longlabel, UI_BTN_NORMAL);
            for (py = 0; py < 120; py++) {
                for (px = 0; px < 120; px++) {
                    before[py * 120 + px] = gfx_get_pixel(g_s, px, py);
                }
            }
            ui_text_fit(want, sizeof want, longlabel, crect_w(&small) - 4,
                        GFX_FONT_SYSTEM);
            /* Not the same string, so this is a real comparison. */
            CHECK(test_strcmp(want, longlabel) != 0);
            clear_sentinel();
            ui_draw_button(g_s, &small, want, UI_BTN_NORMAL);
            for (py = 0; py < 120; py++) {
                for (px = 0; px < 120; px++) {
                    if (before[py * 120 + px] != gfx_get_pixel(g_s, px, py)) {
                        diff++;
                    }
                }
            }
            CHECK_EQI(diff, 0);
        }

        /* And a label that DOES fit is not shortened -- the fix must not cost
         * a character of a label that was always fine. */
        {
            CRect wide = crect_make(4, 4, 100, 20);
            clear_sentinel();
            ui_draw_button(g_s, &wide, "OK", UI_BTN_NORMAL);
            CHECK_EQI(leaked(&wide), 0);
        }
    }

    /* ---- and a listbox row --------------------------------------------- *
     *
     * The list has a scrollbar for its HEIGHT and nothing at all for its
     * width: an item wider than the box ran straight out of the control.
     * Measured at 194 pixels outside an 80-pixel list with one long entry.
     */
    {
        UiList l;
        CRect lb = crect_make(20, 20, 80, 40);
        ui_list_clear(&l);
        ui_list_add(&l, "A VERY LONG LIST ITEM INDEED");
        ui_list_add(&l, "SHORT");
        l.sel = 0;
        clear_sentinel();
        ui_list_draw(g_s, &lb, &l, CTRUE);
        CHECK_EQI(leaked(&lb), 0);
        /* The selected row is filled with the accent colour before its text,
         * so check the unselected path as well. */
        l.sel = 1;
        clear_sentinel();
        ui_list_draw(g_s, &lb, &l, CFALSE);
        CHECK_EQI(leaked(&lb), 0);
        /* Enough items to bring the scrollbar out -- the text must not run
         * under it either. */
        {
            int k;
            for (k = 0; k < 12; k++) {
                ui_list_add(&l, "ANOTHER RATHER LONG ENTRY HERE");
            }
            l.sel = 0;
            clear_sentinel();
            ui_list_draw(g_s, &lb, &l, CTRUE);
            CHECK_EQI(leaked(&lb), 0);
        }
        /* A list too narrow for any text at all. */
        {
            CRect tiny = crect_make(20, 70, 14, 20);
            clear_sentinel();
            ui_list_draw(g_s, &tiny, &l, CTRUE);
            CHECK_EQI(leaked(&tiny), 0);
        }
    }

    /* ---- and the same for a checkbox and a radio ----------------------- *
     *
     * Found by asking whether anything ELSE drew a caller's string into a
     * fixed box, rather than assuming the button was special. Both drew the
     * raw label with gfx_draw_text, which clips to nothing: measured at 240
     * pixels outside a 70-pixel row, each -- worse than the button's 168.
     */
    {
        CRect row = crect_make(20, 70, 70, 16);
        const char *longlabel = "A VERY LONG LABEL INDEED FOR THIS";
        clear_sentinel();
        ui_draw_check(g_s, &row, longlabel, CTRUE, UI_BTN_NORMAL);
        CHECK_EQI(leaked(&row), 0);
        clear_sentinel();
        ui_draw_check(g_s, &row, longlabel, CFALSE, UI_BTN_DISABLED);
        CHECK_EQI(leaked(&row), 0);
        clear_sentinel();
        ui_draw_radio(g_s, &row, longlabel, CTRUE, UI_BTN_NORMAL);
        CHECK_EQI(leaked(&row), 0);
        clear_sentinel();
        ui_draw_radio(g_s, &row, longlabel, CFALSE, UI_BTN_DISABLED);
        CHECK_EQI(leaked(&row), 0);
        /* A row with no room for a label at all still stays inside itself. */
        {
            CRect narrow = crect_make(20, 92, 18, 14);
            clear_sentinel();
            ui_draw_check(g_s, &narrow, longlabel, CTRUE, UI_BTN_NORMAL);
            CHECK_EQI(leaked(&narrow), 0);
            clear_sentinel();
            ui_draw_radio(g_s, &narrow, longlabel, CTRUE, UI_BTN_NORMAL);
            CHECK_EQI(leaked(&narrow), 0);
        }
        /* A label that fits is untouched. */
        {
            CRect wide = crect_make(4, 40, 110, 16);
            clear_sentinel();
            ui_draw_check(g_s, &wide, "On", CTRUE, UI_BTN_NORMAL);
            CHECK_EQI(leaked(&wide), 0);
        }
        /* NULL label: no crash, nothing drawn outside. */
        clear_sentinel();
        ui_draw_check(g_s, &row, NULL, CTRUE, UI_BTN_NORMAL);
        CHECK_EQI(leaked(&row), 0);
        clear_sentinel();
        ui_draw_radio(g_s, &row, NULL, CTRUE, UI_BTN_NORMAL);
        CHECK_EQI(leaked(&row), 0);
    }

    /* ---- ui_path_fit: a path that does not fit ------------------------- *
     *
     * Cut from the LEFT, because the tail is the part that says where you
     * are. Disk Usage got this right in its own painting code and the Log
     * Viewer's path bar simply ran off the right edge with nothing to show
     * it had been cut; this is the shared version, so the two agree.
     */
    {
        char out[128];
        const char *deep = "/one/two/three/four/five/six/seven/eight/nine.log";
        int w;

        /* Fits: returned whole, with no ellipsis invented. */
        ui_path_fit(out, sizeof out, "/a/b.log", 1000, GFX_FONT_SYSTEM);
        CHECK_STR(out, "/a/b.log");
        /* Does not fit: marked, and the END is what survived. */
        ui_path_fit(out, sizeof out, deep, 120, GFX_FONT_SYSTEM);
        CHECK(out[0] == '.' && out[1] == '.' && out[2] == '.');
        CHECK(gfx_text_width(GFX_FONT_SYSTEM, out) <= 120);
        /* The tail is kept: the file name is still readable, which is the
         * whole reason for cutting this end rather than the other. */
        CHECK(test_strcmp(out + sys_strnlen(out, sizeof out) - 8,
                          "nine.log") == 0);
        /* Wider budget keeps more, and never less. */
        {
            char wide[128];
            ui_path_fit(wide, sizeof wide, deep, 300, GFX_FONT_SYSTEM);
            CHECK(sys_strnlen(wide, sizeof wide) >=
                  sys_strnlen(out, sizeof out));
            CHECK(gfx_text_width(GFX_FONT_SYSTEM, wide) <= 300);
        }
        /* Absurdly small budgets must terminate rather than walk off the end
         * of the string looking for a fit that cannot exist. */
        for (w = -4; w <= 16; w++) {
            ui_path_fit(out, sizeof out, deep, w, GFX_FONT_SYSTEM);
            CHECK(sys_strnlen(out, sizeof out) < sizeof out);
        }
        /* Refusals: no crash, and a NULL path is an empty string rather than
         * whatever the buffer happened to hold. */
        ui_path_fit(NULL, 64, deep, 100, GFX_FONT_SYSTEM);
        ui_path_fit(out, 0u, deep, 100, GFX_FONT_SYSTEM);
        out[0] = 'x';
        ui_path_fit(out, sizeof out, NULL, 100, GFX_FONT_SYSTEM);
        CHECK_STR(out, "");
    }

    /* ---- ui_text_fit: the other end of the same problem ---------------- *
     *
     * A label is cut from the RIGHT, because a file name is identified by how
     * it starts. The File Manager's Icons view did no fitting at all and
     * centred the raw name with `(FM_CELL_W - 4 - tw) / 2`, which goes
     * negative once the name is wider than the cell -- labels ran out of the
     * list on the left, off the window on the right, and into each other.
     *
     * Handed something too big, then MEASURED. That is the check the path
     * version did not have until it was written, and it found a real bug
     * within a minute of existing.
     */
    {
        char out[64];
        int w;
        const char *longname = "A_VERY_LONG_FILENAME_INDEED.TXT";

        /* Fits: whole, no mark invented. */
        ui_text_fit(out, sizeof out, "SHORT.TXT", 1000, GFX_FONT_SYSTEM);
        CHECK_STR(out, "SHORT.TXT");
        /* Does not fit: marked, and the START is what survived. */
        ui_text_fit(out, sizeof out, longname, 76, GFX_FONT_SYSTEM);
        CHECK(gfx_text_width(GFX_FONT_SYSTEM, out) <= 76);
        CHECK(out[0] == 'A' && out[1] == '_');
        {
            cu32 n = sys_strnlen(out, sizeof out);
            CHECK(n >= 3u);
            CHECK(out[n - 1u] == '.' && out[n - 2u] == '.' && out[n - 3u] == '.');
        }
        /* Every width down to nothing produces something that fits and stays
         * inside the buffer -- the cell can be narrow on a 640x480 desktop. */
        for (w = -4; w <= 120; w++) {
            ui_text_fit(out, sizeof out, longname, w, GFX_FONT_SYSTEM);
            CHECK(sys_strnlen(out, sizeof out) < sizeof out);
            if (w > 30) { CHECK(gfx_text_width(GFX_FONT_SYSTEM, out) <= w); }
        }
        /* A wider budget never keeps less. */
        {
            char a[64], b[64];
            ui_text_fit(a, sizeof a, longname, 60, GFX_FONT_SYSTEM);
            ui_text_fit(b, sizeof b, longname, 140, GFX_FONT_SYSTEM);
            CHECK(sys_strnlen(b, sizeof b) >= sys_strnlen(a, sizeof a));
        }
        /* When not even one character fits beside the mark, the answer is the
         * mark alone -- the shortest thing that still says "there is more".
         * A mutation that stopped the loop one character early survived every
         * other check here, and looked equivalent until the two were compiled
         * and their output compared side by side: at width 8 the original
         * gives "..." (18 px) and the mutant "A..." (24 px), which is strictly
         * worse in a budget that is already impossible. */
        ui_text_fit(out, sizeof out, longname, 8, GFX_FONT_SYSTEM);
        CHECK_STR(out, "...");
        ui_text_fit(out, sizeof out, longname, 1, GFX_FONT_SYSTEM);
        CHECK_STR(out, "...");

        /* A tiny destination must not be written past. */
        {
            char small[6];
            ui_text_fit(small, sizeof small, longname, 500, GFX_FONT_SYSTEM);
            CHECK(sys_strnlen(small, sizeof small) < sizeof small);
        }
        /* Refusals. */
        ui_text_fit(NULL, 64, longname, 100, GFX_FONT_SYSTEM);
        ui_text_fit(out, 0u, longname, 100, GFX_FONT_SYSTEM);
        out[0] = 'x';
        ui_text_fit(out, sizeof out, NULL, 100, GFX_FONT_SYSTEM);
        CHECK_STR(out, "");
    }

    /* ---- ui_draw_tri: which way it actually points --------------------- *
     *
     * The Media Player's Play button pointed LEFT and its Next button was
     * drawn identically to Prev, both visible on screen from the day the deck
     * was written. The cause was the height formula `1 + 2 * i` -- a span that
     * GROWS with x, putting the apex on the left -- copied into three places,
     * two of which wanted the other direction. The scroll bar, which has taken
     * the direction as an argument since it was written, has all four arrows
     * right.
     *
     * So the direction is checked the only way that means anything: draw it
     * and count where the ink actually is. A triangle pointing right has its
     * base (the tall edge) on the LEFT and its point on the right, so the left
     * column must hold strictly more ink than the right one. Getting the
     * formula backwards inverts that comparison and fails here.
     */
    {
        CRect t = crect_make(20, 20, 9, 9);
        int i, y, left, right, top, bottom;

        /* -- pointing RIGHT: fat on the left, a point on the right -- */
        clear_sentinel();
        ui_draw_tri(g_s, &t, UI_TRI_RIGHT, INK);
        left = right = 0;
        for (y = t.y0; y < t.y1; y++) {
            if (gfx_get_pixel(g_s, t.x0, y)     == INK) { left++; }
            if (gfx_get_pixel(g_s, t.x1 - 1, y) == INK) { right++; }
        }
        CHECK(left > right);
        CHECK_EQI(left, 9);          /* the base spans the whole height */
        CHECK_EQI(right, 1);         /* the apex is a single pixel      */

        /* -- pointing LEFT: the mirror, and it must NOT be the same -- */
        clear_sentinel();
        ui_draw_tri(g_s, &t, UI_TRI_LEFT, INK);
        left = right = 0;
        for (y = t.y0; y < t.y1; y++) {
            if (gfx_get_pixel(g_s, t.x0, y)     == INK) { left++; }
            if (gfx_get_pixel(g_s, t.x1 - 1, y) == INK) { right++; }
        }
        CHECK(right > left);
        CHECK_EQI(right, 9);
        CHECK_EQI(left, 1);

        /* -- and the same for the vertical pair -- */
        clear_sentinel();
        ui_draw_tri(g_s, &t, UI_TRI_DOWN, INK);
        top = bottom = 0;
        for (i = t.x0; i < t.x1; i++) {
            if (gfx_get_pixel(g_s, i, t.y0)     == INK) { top++; }
            if (gfx_get_pixel(g_s, i, t.y1 - 1) == INK) { bottom++; }
        }
        CHECK(top > bottom);         /* wide at the top, a point below */
        CHECK_EQI(top, 9);
        CHECK_EQI(bottom, 1);

        clear_sentinel();
        ui_draw_tri(g_s, &t, UI_TRI_UP, INK);
        top = bottom = 0;
        for (i = t.x0; i < t.x1; i++) {
            if (gfx_get_pixel(g_s, i, t.y0)     == INK) { top++; }
            if (gfx_get_pixel(g_s, i, t.y1 - 1) == INK) { bottom++; }
        }
        CHECK(bottom > top);
        CHECK_EQI(bottom, 9);
        CHECK_EQI(top, 1);

        /* Every row of a right-pointing triangle is one contiguous run that
         * never leaves the rectangle -- a triangle that paints outside its
         * own box is how a glyph smears into the button beside it. */
        clear_sentinel();
        ui_draw_tri(g_s, &t, UI_TRI_RIGHT, INK);
        CHECK_EQI(leaked(&t), 0);

        /* Degenerate boxes draw something or nothing, but never crash and
         * never paint outside. */
        {
            CRect one = crect_make(40, 40, 1, 1);
            CRect flat = crect_make(50, 50, 9, 1);
            CRect thin = crect_make(60, 50, 1, 9);
            clear_sentinel();
            ui_draw_tri(g_s, &one, UI_TRI_RIGHT, INK);
            CHECK_EQI(leaked(&one), 0);
            clear_sentinel();
            ui_draw_tri(g_s, &flat, UI_TRI_LEFT, INK);
            CHECK_EQI(leaked(&flat), 0);
            clear_sentinel();
            ui_draw_tri(g_s, &thin, UI_TRI_UP, INK);
            CHECK_EQI(leaked(&thin), 0);
        }
        /* Refusals. */
        ui_draw_tri(NULL, &t, UI_TRI_RIGHT, INK);
        ui_draw_tri(g_s, NULL, UI_TRI_RIGHT, INK);
        {
            CRect empty = crect_make(10, 10, 0, 0);
            ui_draw_tri(g_s, &empty, UI_TRI_RIGHT, INK);
        }
    }

    gfx_surface_free(g_s);
    g_s = NULL;
#undef TOP_Y
#undef BOT_Y
#undef MID_X
}

/*
 * The meter, and the overflow that made one of them lie.
 *
 * Four windows drew their own proportional fill, and the Task Manager's was
 * wrong in the worst possible direction. It measures BYTES live against an
 * 8 MB budget across a 386-pixel bar, and computed (value * width) / max. On
 * a 32-bit long that product passes 2^31 at 5.31 MB, wraps NEGATIVE, and the
 * clamp underneath turns a negative fill into zero -- so the memory gauge
 * read EMPTY for the top third of its own range. It said "nothing is using
 * memory" at exactly the moment memory was nearly exhausted.
 *
 * The numbers below are that real geometry, not invented ones.
 */
void test_meter(void)
{
    const cs32 MB = (cs32)(1024 * 1024);
    const cs32 budget = (cs32)8 * MB;  /* the Task Manager's memory budget */
    const int  bar = 386;              /* its bar, at the real window size */

    printf("- meter fill\n");

    /* ---- the ordinary range ------------------------------------------- */
    CHECK_EQI(ui_meter_fill(0, 100, 200), 0);
    CHECK_EQI(ui_meter_fill(50, 100, 200), 100);
    CHECK_EQI(ui_meter_fill(100, 100, 200), 200);
    CHECK_EQI(ui_meter_fill(25, 100, 400), 100);

    /* ---- never outside the well --------------------------------------- */
    /* Over-full clamps to the width rather than painting past the bevel. */
    CHECK_EQI(ui_meter_fill(200, 100, 200), 200);
    CHECK_EQI(ui_meter_fill(1000000, 100, 50), 50);
    /* Negative and zero inputs are answered, not drawn. */
    CHECK_EQI(ui_meter_fill(-5, 100, 200), 0);
    CHECK_EQI(ui_meter_fill(50, 0, 200), 0);
    CHECK_EQI(ui_meter_fill(50, -100, 200), 0);
    CHECK_EQI(ui_meter_fill(50, 100, 0), 0);
    CHECK_EQI(ui_meter_fill(50, 100, -3), 0);

    /* ---- the overflow, at the real geometry ---------------------------- */
    /* Below the old wrap point, both formulas agreed. */
    CHECK_EQI(ui_meter_fill((cs32)4 * MB, budget, bar), bar / 2);
    /* Above it, the old one wrapped negative and clamped to zero. Half-full
     * must be half a bar and nearly-full must be nearly a full bar. */
    CHECK(ui_meter_fill((cs32)6 * MB, budget, bar) > bar / 2);
    CHECK(ui_meter_fill((cs32)7 * MB, budget, bar) > (bar * 3) / 4);
    /* The exact ones, so a "fix" that merely stops going negative but loses
     * the proportion cannot pass. Halving costs at most a pixel here. */
    {
        int f6 = ui_meter_fill((cs32)6 * MB, budget, bar);
        int f7 = ui_meter_fill((cs32)7 * MB, budget, bar);
        CHECK(f6 >= 288 && f6 <= 290);      /* 6/8 of 386 = 289.5 */
        CHECK(f7 >= 337 && f7 <= 339);      /* 7/8 of 386 = 337.75 */
    }
    /* Monotonic across the wrap point: more memory must never draw less. */
    {
        cs32 v; int prev = -1; int mono = 1;
        for (v = 0; v <= budget; v += (cs32)(64 * 1024)) {
            int f = ui_meter_fill(v, budget, bar);
            if (f < prev) { mono = 0; }
            prev = f;
        }
        CHECK(mono);
    }
    /* Full is full, right at the budget. */
    CHECK_EQI(ui_meter_fill(budget, budget, bar), bar);

    /* ---- the same primitive used as a PERCENTAGE ----------------------- */
    /*
     * Two more windows had this bug, in the same shape and invisible for the
     * same reason. Disk Usage showed "N% of the total" as
     * (bytes * 100) / total, and the Archive viewer showed "N% saved" as
     * 100 - (stored * 100) / original. Both multiply a BYTE COUNT by 100, so
     * both wrap at 21,474,836 bytes -- 20.5 MB. A photo folder or an install
     * tree passes that easily, and the status line then reports a negative or
     * absurd percentage. Scaled to 1000 and rounded, the same helper answers
     * correctly at any size.
     */
    {
        const cs32 MB100 = (cs32)100 * MB;   /* 100 MB: well past the wrap  */
        const cs32 MB200 = (cs32)200 * MB;
        /* Half of a 200 MB tree is 50%, not a wrapped negative. */
        CHECK_EQI((ui_meter_fill(MB100, MB200, 1000) + 5) / 10, 50);
        /* A quarter, and a tenth. */
        CHECK_EQI((ui_meter_fill((cs32)50 * MB, MB200, 1000) + 5) / 10, 25);
        CHECK_EQI((ui_meter_fill((cs32)20 * MB, MB200, 1000) + 5) / 10, 10);
        /* Just above the old wrap point, where the old formula first broke. */
        CHECK_EQI((ui_meter_fill((cs32)21 * MB, (cs32)42 * MB, 1000) + 5) / 10,
                  50);
        /* The archive's "percent saved" on a big archive: 100 MB stored from
         * 200 MB original is 50% saved. */
        CHECK_EQI(100 - (ui_meter_fill(MB100, MB200, 1000) + 5) / 10, 50);
        /* Nothing saved, and everything saved. */
        CHECK_EQI(100 - (ui_meter_fill(MB200, MB200, 1000) + 5) / 10, 0);
        CHECK_EQI(100 - (ui_meter_fill(0, MB200, 1000) + 5) / 10, 100);
        /* A percentage never leaves 0..100, whatever it is handed. */
        CHECK((ui_meter_fill(MB200, MB100, 1000) + 5) / 10 <= 100);
        CHECK((ui_meter_fill((cs32)-1, MB200, 1000) + 5) / 10 >= 0);
    }

    /* ---- ui_percent: the same thing, asked by name --------------------- *
     *
     * The section above found this bug in Disk Usage and in the Archive
     * viewer and fixed both by routing them through ui_meter_fill. It missed
     * the third: the File Manager's "compressed, N% smaller" computed
     * (saved * 100L) / orig by hand, which wraps at the same 20.5 MB. Two of
     * three is how a class of bug survives being found -- so the percentage
     * has a NAME now, and asking for one by hand is the thing that looks
     * wrong at a glance.
     */
    {
        const cs32 MB100 = (cs32)100 * MB;
        const cs32 MB200 = (cs32)200 * MB;
        CHECK_EQI(ui_percent(MB100, MB200), 50);
        CHECK_EQI(ui_percent((cs32)50 * MB, MB200), 25);
        CHECK_EQI(ui_percent(MB200, MB200), 100);
        CHECK_EQI(ui_percent(0, MB200), 0);
        /* Just past the wrap point, where the hand-written form first broke:
         * 21 MB of 42 MB is half, and 30 MB saved from 60 MB is half. */
        CHECK_EQI(ui_percent((cs32)21 * MB, (cs32)42 * MB), 50);
        CHECK_EQI(ui_percent((cs32)30 * MB, (cs32)60 * MB), 50);
        /* The File Manager's real case: a 100 MB file compressed to 25 MB has
         * saved 75 MB, which is 75%. The hand-written product for that is
         * 78,643,200 * 100 -- nearly four times past what 32 bits hold. */
        CHECK_EQI(ui_percent((cs32)75 * MB, MB100), 75);
        /* Small values still answer exactly; the guard must not cost accuracy
         * where there was never a risk. */
        CHECK_EQI(ui_percent(1, 4), 25);
        CHECK_EQI(ui_percent(1, 3), 33);
        CHECK_EQI(ui_percent(2, 3), 66);
        CHECK_EQI(ui_percent(7, 7), 100);
        /* Never outside 0..100, whatever it is handed. */
        CHECK_EQI(ui_percent(MB200, MB100), 100);
        CHECK_EQI(ui_percent((cs32)-1, MB200), 0);
        CHECK_EQI(ui_percent(5, 0), 0);
        CHECK_EQI(ui_percent(5, -1), 0);
        CHECK_EQI(ui_percent(0, 0), 0);
        /* Monotonic across the wrap point: saving more must never report a
         * smaller percentage. */
        {
            cs32 v; int prev = -1; int mono = 1;
            for (v = 0; v <= MB200; v += (cs32)(1024 * 1024)) {
                int p = ui_percent(v, MB200);
                if (p < prev) { mono = 0; }
                prev = p;
            }
            CHECK(mono);
        }
    }

    /* ---- values far past anything real --------------------------------- */
    /* A gauge handed a nonsense reading must still draw something sane, not
     * wrap. Byte counts arrive from platform calls that can fail oddly. */
    CHECK_EQI(ui_meter_fill((cs32)2000000000, (cs32)2000000000, 300), 300);
    CHECK(ui_meter_fill((cs32)1500000000, (cs32)2000000000, 300) > 200);
    CHECK(ui_meter_fill((cs32)1500000000, (cs32)2000000000, 300) <= 300);
    CHECK_EQI(ui_percent((cs32)2000000000, (cs32)2000000000), 100);
    CHECK(ui_percent((cs32)1500000000, (cs32)2000000000) >= 70);
    CHECK(ui_percent((cs32)1500000000, (cs32)2000000000) <= 76);
}
