/*
 * ui_button.c - Push button rendering and the shared control palette.
 */
#include "castalia/ui.h"
#include "castalia/gfx.h"

/* Default control palette ("Castalia Classic" grays). The shell overwrites
 * this at theme apply via ui_set_palette(). */
static UiPalette g_pal = {
    0xC6CBD4UL, /* face   - cool stone gray  */
    0xEFF2F6UL, /* light  - highlight        */
    0x8A93A0UL, /* dark   - shadow           */
    0x3A414BUL, /* darker - outer shadow     */
    0x1A1E24UL, /* text                      */
    0x8A93A0UL, /* text_disabled             */
    0x1B3F7AUL, /* accent - royal blue       */
    0xF3F6FBUL  /* accent_text               */
};
static const UiPalette *g_pal_ptr = &g_pal;

const UiPalette *ui_palette(void) { return g_pal_ptr; }
void ui_set_palette(const UiPalette *pal) { if (pal) { g_pal = *pal; } g_pal_ptr = &g_pal; }

/* Off until a shell says otherwise: the flat path is the one that is correct
 * on every pipeline, so it is the one you get by default. */
static cbool g_glossy = CFALSE;
void  ui_set_glossy(cbool on) { g_glossy = on ? CTRUE : CFALSE; }
cbool ui_glossy(void) { return g_glossy; }

/*
 * The glossy face, drawn inside the bevel the classic look still supplies.
 *
 * A button is lit from above, so the gradient runs bright at the top to a
 * shade below the face at the bottom -- and a PRESSED button inverts it,
 * which is what makes it read as pushed in beyond the sunken bevel alone.
 * Disabled buttons stay flat: a control that cannot be used should not look
 * lit, and a gradient is exactly what "lit" means here.
 *
 * This one function is also every scrollbar arrow and every scrollbar thumb
 * (ui_scroll.c builds them out of buttons), so the whole shell's scrolling
 * furniture follows it without knowing anything about it.
 */
static void glossy_face(GfxSurface *s, const CRect *r, const UiPalette *p,
                        UiButtonState state, CColor base)
{
    CRect in = crect_inset(r, 2);   /* the bevel owns the outer two pixels */
    CColor top, mid, bot;
    if (crect_w(&in) <= 0 || crect_h(&in) <= 0) { return; }
    switch (state) {
    case UI_BTN_PRESSED:
        top = gfx_tint(base, p->dark, 70);
        mid = gfx_tint(base, p->dark, 25);
        bot = base;
        break;
    /*
     * Hover has no case of its own. It is the resting shading drawn on a
     * different base -- see ui_draw_button -- so the button keeps its shape
     * and changes its COLOUR. It used to have one, running nearly to white
     * at the top, which on a pale theme is where the resting gradient
     * already was: two treatments that computed to almost the same pixels.
     */
    default:
        top = gfx_tint(base, p->light, 150);
        mid = base;
        bot = gfx_tint(base, p->dark, 40);
        break;
    }
    gfx_vgradient3(s, &in, top, mid, bot, 550);
}

void ui_draw_button(GfxSurface *s, const CRect *r, const char *label,
                    UiButtonState state)
{
    const UiPalette *p = ui_palette();
    GfxBevel bevel = (state == UI_BTN_PRESSED) ? GFX_BEVEL_SUNKEN
                                               : GFX_BEVEL_RAISED;
    CColor face = p->face;
    CColor text = (state == UI_BTN_DISABLED) ? p->text_disabled : p->text;
    int off = (state == UI_BTN_PRESSED) ? 1 : 0;

    if (state == UI_BTN_HOVER) {
        /*
         * A wash of the ACCENT, not "the light colour".
         *
         * The lift used to be face -> light, which is a real change on a grey
         * palette and almost none on a pale one. The shipped theme's face and
         * light are seven levels apart per channel, so the whole hover
         * treatment measured 21 across the three channels off the backbuffer
         * -- present in the code, invisible on the screen, and the reason
         * this was worth measuring rather than looking at.
         *
         * A fixed proportion of the distance to the accent works either way:
         * a theme has to keep those two apart, because the accent is what it
         * paints selections in.
         */
        face = gfx_tint(p->face, p->accent, 36);
    }
    gfx_bevel(s, r, bevel, p->light, p->dark, face);
    if (ui_glossy() && state != UI_BTN_DISABLED) {
        glossy_face(s, r, p, state, face);
    }

    if (label != NULL && label[0] != '\0') {
        /*
         * Fitted to the button before it is centred.
         *
         * gfx_draw_text_rect does not clip: HCENTER is `x0 + (rw - tw) / 2`,
         * which goes NEGATIVE for a label wider than its box, so the text
         * starts left of the button and runs out of both ends. Measured on a
         * 60-px button with a 24-character label: 168 pixels painted outside
         * it, across whatever happened to be next to it.
         *
         * Every containment check in tests/test_ctrl.c passed an EMPTY label
         * -- `ui_draw_button(g_s, &btn, "", ...)` -- so the one thing that can
         * actually escape a button was the one thing never drawn into it.
         */
        char fitted[96];
        CRect saved;
        CRect tr = *r;
        tr.x0 += off; tr.y0 += off;
        ui_text_fit(fitted, sizeof fitted, label, crect_w(&tr) - 4,
                    GFX_FONT_SYSTEM);
        /*
         * Fitted AND clipped, because the two answer different questions.
         * Fitting is what makes a long label read as "A VERY LO..." instead of
         * being sliced at both ends; clipping is what guarantees containment
         * when even the ellipsis does not fit -- a button ten pixels wide has
         * no honest label, and "..." is eighteen pixels. Fitting alone still
         * leaked six pixels there.
         */
        saved = gfx_clip_narrow(s, r);
        gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &tr, fitted, text,
                           GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
        gfx_set_clip(s, &saved);
    }
}

void ui_draw_button_default(GfxSurface *s, const CRect *r)
{
    const UiPalette *p = ui_palette();
    /* Two pixels out: the dialog leaves eight between neighbouring buttons
     * and eight below the row, so the ring has room without touching either. */
    CRect ring = crect_inset(r, -2);
    gfx_frame_rect(s, &ring, p->darker);
}

void ui_draw_button_focus(GfxSurface *s, const CRect *r)
{
    const UiPalette *p = ui_palette();
    CRect ring = crect_inset(r, 3);
    if (crect_w(&ring) < 2 || crect_h(&ring) < 2) { return; }
    gfx_focus_rect(s, &ring, p->text);
}

/* ---- pointer state for a strip of buttons (see ui.h) ------------------ */

void ui_hot_init(UiHot *h)
{
    if (h == NULL) { return; }
    h->hot1 = 0;
    h->down1 = 0;
    h->was1 = 0;
}

int ui_hot_left(const UiHot *h) { return (h != NULL) ? h->was1 - 1 : -1; }
int ui_hot_hot(const UiHot *h)  { return (h != NULL) ? h->hot1 - 1 : -1; }

cbool ui_hot_move(UiHot *h, int idx)
{
    if (h == NULL || h->hot1 == idx + 1) { return CFALSE; }
    h->was1 = h->hot1;
    h->hot1 = idx + 1;
    return CTRUE;
}

cbool ui_hot_press(UiHot *h, int idx)
{
    if (h == NULL) { return CFALSE; }
    if (h->down1 == idx + 1 && h->hot1 == idx + 1) { return CFALSE; }
    h->down1 = idx + 1;
    h->hot1 = idx + 1;
    return CTRUE;
}

cbool ui_hot_release(UiHot *h)
{
    if (h == NULL || h->down1 == 0) { return CFALSE; }
    h->down1 = 0;
    return CTRUE;
}

int ui_hot_pressed(const UiHot *h)
{
    return (h != NULL) ? h->down1 - 1 : -1;
}

UiButtonState ui_hot_state(const UiHot *h, int i, UiButtonState base)
{
    if (h == NULL || i < 0) { return base; }
    /* A control that cannot be used does not react to the pointer: lighting a
     * disabled button is a promise it cannot keep. */
    if (base == UI_BTN_DISABLED) { return base; }
    /*
     * Held down ON this button: sunken. Held down and slid OFF it: back to
     * whatever it was. That is what lets somebody who pressed the wrong
     * button back out of it, and it is why 'down' and 'hot' are two fields
     * and not one.
     *
     * While anything is held, nothing else lights: a pointer dragged across
     * a keypad with the button down would otherwise leave a trail of lit
     * keys behind it, none of which is going to be the one that fires.
     */
    if (h->down1 != 0) {
        return (h->down1 == i + 1 && h->hot1 == i + 1) ? UI_BTN_PRESSED : base;
    }
    /* A latched button stays latched under the pointer -- un-sinking the one
     * that is switched ON is the opposite of feedback. */
    if (h->hot1 == i + 1 && base == UI_BTN_NORMAL) { return UI_BTN_HOVER; }
    return base;
}
