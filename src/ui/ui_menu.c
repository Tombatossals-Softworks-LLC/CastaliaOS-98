/*
 * ui_menu.c - Pop-up menu model, layout, drawing, and hit testing.
 *
 * Used by the launcher and (later) context menus. The menu is a passive data
 * structure; the shell owns its position and lifetime and routes clicks
 * through ui_menu_hit().
 */
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/sys.h"

#define MENU_ITEM_H   16   /* tall enough for a 16px gutter icon           */
#define MENU_SEP_H    5
#define MENU_PAD_L    22   /* left gutter (icon column)   */
#define MENU_PAD_R    16   /* right gutter                */
#define MENU_PAD_V    3    /* top/bottom padding          */
#define MENU_ICON_MAX 16   /* gutter icon clamp (square)  */

void ui_menu_clear(UiMenu *m)
{
    if (m == NULL) { return; }
    m->count = 0;
    m->highlight = -1;
}

cbool ui_menu_selectable(const UiMenu *m, int i)
{
    if (m == NULL || i < 0 || i >= m->count) { return CFALSE; }
    return (m->items[i].id != UI_MENU_SEPARATOR_ID && m->items[i].enabled)
           ? CTRUE : CFALSE;
}

cbool ui_menu_step(UiMenu *m, int dir)
{
    int i, guard;
    if (m == NULL || m->count == 0 || dir == 0) { return CFALSE; }
    i = m->highlight;
    /* At most one lap. A menu whose every row is a separator or disabled --
     * which a caller can build without meaning to -- must come back rather
     * than spin, and must leave the highlight where it was. */
    for (guard = 0; guard < m->count; guard++) {
        i += dir;
        if (i < 0) { i = m->count - 1; }
        else if (i >= m->count) { i = 0; }
        if (ui_menu_selectable(m, i)) {
            if (i == m->highlight) { return CFALSE; }
            m->highlight = i;
            return CTRUE;
        }
    }
    return CFALSE;
}

CResult ui_menu_add(UiMenu *m, int id, const char *label, cbool enabled)
{
    UiMenuItem *it;
    if (m == NULL) { return CE_INVALID; }
    if (m->count >= UI_MENU_MAX_ITEMS) { return CE_OVERFLOW; }
    it = &m->items[m->count++];
    it->id = id;
    it->enabled = enabled;
    it->has_submenu = CFALSE;
    it->icon = NULL;
    {
        int i = 0;
        if (label != NULL) {
            while (label[i] != '\0' && i < (int)sizeof(it->label) - 1) {
                it->label[i] = label[i];
                i++;
            }
        }
        it->label[i] = '\0';
    }
    return CE_OK;
}

CResult ui_menu_add_separator(UiMenu *m)
{
    UiMenuItem *it;
    if (m == NULL) { return CE_INVALID; }
    if (m->count >= UI_MENU_MAX_ITEMS) { return CE_OVERFLOW; }
    it = &m->items[m->count++];
    it->id = UI_MENU_SEPARATOR_ID;
    it->enabled = CFALSE;
    it->has_submenu = CFALSE;
    it->icon = NULL;
    it->label[0] = '\0';
    return CE_OK;
}

void ui_menu_set_last_icon(UiMenu *m, const GfxSurface *icon)
{
    if (m == NULL || m->count == 0) { return; }
    m->items[m->count - 1].icon = icon;
}

static int item_height(const UiMenuItem *it)
{
    return (it->id == UI_MENU_SEPARATOR_ID) ? MENU_SEP_H : MENU_ITEM_H;
}

/*
 * A label may carry its keyboard shortcut after a TAB: "Undo\tCtrl+Z".
 *
 * The menu did not know that. It handed the whole string to gfx_draw_text,
 * and 0x09 is below the font's first code point, so every accelerator in the
 * system was drawn as  Undo[box]Ctrl+Z  -- the missing-glyph box, sitting in
 * the middle of the word, in Paint's Edit menu, for as long as Paint has had
 * one. Nothing failed; it simply looked wrong, which is the only way a
 * drawing bug ever announces itself.
 *
 * Returns the offset of the tab, or -1 when the label is a plain one.
 */
static int accel_at(const char *label)
{
    int i;
    if (label == NULL) { return -1; }
    for (i = 0; label[i] != '\0'; i++) {
        if (label[i] == '\t') { return i; }
    }
    return -1;
}

/* Width of the first 'n' characters -- gfx_text_width takes whole strings. */
static int prefix_width(GfxFontId font, const char *text, int n)
{
    int w = gfx_text_width(font, text);
    int len = 0;
    while (text[len] != '\0') { len++; }
    if (len <= 0 || n >= len) { return w; }
    if (n <= 0) { return 0; }
    return (w / len) * n;      /* the font is fixed-pitch; see FONT_ADVANCE */
}

/* The two halves of a label, and the gap that keeps them from touching. */
#define MENU_ACCEL_GAP 18

static void label_parts(const UiMenuItem *it, GfxFontId font,
                        int *lab_w, int *acc_w)
{
    int t = accel_at(it->label);
    if (t < 0) {
        *lab_w = gfx_text_width(font, it->label);
        *acc_w = 0;
        return;
    }
    *lab_w = prefix_width(font, it->label, t);
    *acc_w = gfx_text_width(font, it->label + t + 1);
}

void ui_menu_measure(const UiMenu *m, GfxFontId font, int *out_w, int *out_h)
{
    int i, maxw = 0, maxa = 0, h = MENU_PAD_V * 2;
    if (m == NULL) { if (out_w) *out_w = 0; if (out_h) *out_h = 0; return; }
    for (i = 0; i < m->count; i++) {
        int lw, aw;
        label_parts(&m->items[i], font, &lw, &aw);
        if (lw > maxw) { maxw = lw; }
        if (aw > maxa) { maxa = aw; }
        h += item_height(&m->items[i]);
    }
    /*
     * The accelerators share ONE column, so they line up down the menu
     * instead of each floating at its own label's end. That means the width
     * is the widest label plus the widest accelerator, not the widest
     * label-plus-its-own-accelerator: measuring per item would leave a menu
     * whose longest label and longest shortcut are on different rows too
     * narrow, and the shortcut would run off the edge.
     */
    if (maxa > 0) { maxw += MENU_ACCEL_GAP + maxa; }
    if (out_w) { *out_w = maxw + MENU_PAD_L + MENU_PAD_R; }
    if (out_h) { *out_h = h; }
}

/*
 * The XP-style treatment, drawn only where the pipeline can carry it
 * (ui_glossy(): truecolor, not safe mode). Three things, all cheap, and all
 * of them things the flat menu did without:
 *
 *   - a tinted GUTTER behind the icon column, which is what stops a menu from
 *     reading as a floating list of words and gives the icons somewhere to sit;
 *   - a SHEEN on the selected row instead of a flat block of accent, with a
 *     darker rim so the selection has an edge;
 *   - separators that start after the gutter rather than cutting across it.
 *
 * Menus are drawn only while one is open, so none of this is in the per-frame
 * budget; the idle desktop pays nothing for it.
 */
static int gutter_w(void) { return MENU_PAD_L - 4; }

/*
 * The body goes nearly white and the gutter stays near the face colour. That
 * relationship is the whole effect: a gutter tinted a few percent off a panel
 * that is already almost white is invisible, which is what the first attempt
 * at this looked like. Both are derived from the theme's own face, so a dark
 * theme gets a dark body and a lighter gutter rather than a white slab.
 */
static void draw_body(GfxSurface *s, const UiPalette *p, int x, int y,
                      int w, int h)
{
    CRect body = crect_make(x + 2, y + 2, w - 4, h - 4);
    CRect g = crect_make(x + 2, y + 2, gutter_w(), h - 4);
    gfx_fill_rect(s, &body, gfx_tint(p->face, p->light, 190));
    gfx_vgradient(s, &g, p->face, gfx_tint(p->face, p->dark, 70));
    /* A light edge down the gutter's right side, so it reads as a raised strip
     * rather than a stain. */
    gfx_vline(s, g.x1, g.y0, crect_h(&g), gfx_tint(p->face, p->light, 220));
}

static void draw_selection(GfxSurface *s, const UiPalette *p, const CRect *hl)
{
    /* Bright at the top, the base colour two thirds down, a shade darker at
     * the bottom -- the same three-stop recipe as the title bars, so the
     * selection belongs to the same desktop as the window it sits over. */
    gfx_vgradient3(s, hl, gfx_tint(p->accent, 0xFFFFFF, 70), p->accent,
                   gfx_tint(p->accent, 0x000000, 40), 660);
    gfx_frame_rect(s, hl, gfx_tint(p->accent, 0x000000, 70));
}

void ui_menu_draw(GfxSurface *s, const UiMenu *m, int x, int y, GfxFontId font)
{
    const UiPalette *p = ui_palette();
    cbool glossy = ui_glossy();
    int w, h, i, cy, sep_x0, sep_w;
    CRect frame;
    if (m == NULL) { return; }
    ui_menu_measure(m, font, &w, &h);
    frame = crect_make(x, y, w, h);

    /* Panel with a raised bevel. */
    gfx_bevel(s, &frame, GFX_BEVEL_RAISED, p->light, p->dark, p->face);
    if (glossy) { draw_body(s, p, x, y, w, h); }

    /* Separators stop short of the gutter when there is one; without it they
     * run the full width, as they always did. */
    sep_x0 = glossy ? (x + MENU_PAD_L - 1) : (x + 3);
    sep_w  = (x + w - 3) - sep_x0;

    cy = y + MENU_PAD_V;
    for (i = 0; i < m->count; i++) {
        const UiMenuItem *it = &m->items[i];
        int ih = item_height(it);
        if (it->id == UI_MENU_SEPARATOR_ID) {
            gfx_hline(s, sep_x0, cy + ih / 2, sep_w, p->dark);
            gfx_hline(s, sep_x0, cy + ih / 2 + 1, sep_w, p->light);
        } else {
            CColor tcol = it->enabled ? p->text : p->text_disabled;
            if (i == m->highlight && it->enabled) {
                CRect hl = crect_make(x + 2, cy, w - 4, ih);
                if (glossy) { draw_selection(s, p, &hl); }
                else        { gfx_fill_rect(s, &hl, p->accent); }
                tcol = p->accent_text;
            }
            if (it->icon != NULL) {
                const GfxSurface *ic = it->icon;
                int iw = (ic->w < MENU_ICON_MAX) ? ic->w : MENU_ICON_MAX;
                int ihh = (ic->h < MENU_ICON_MAX) ? ic->h : MENU_ICON_MAX;
                CRect src = crect_make(0, 0, iw, ihh);
                gfx_blit(s, x + 3 + (MENU_ICON_MAX - iw) / 2,
                         cy + (ih - ihh) / 2, ic, &src, GFX_BLIT_KEYED);
            }
            /* Disabled items are engraved rather than merely pale: a light
             * copy one pixel down-right, then the grey text over it. Greyed
             * text on a pale menu is the first thing to become unreadable on
             * a 1999 CRT, and the second edge gives it back. */
            {
                char lab[CASTALIA_MAX_NAME];
                const char *acc = NULL;
                int t = accel_at(it->label);
                if (t >= 0) {
                    int k;
                    for (k = 0; k < t && k < (int)sizeof lab - 1; k++) {
                        lab[k] = it->label[k];
                    }
                    lab[k] = '\0';
                    acc = it->label + t + 1;
                } else {
                    sys_strlcpy(lab, it->label, sizeof lab);
                }
                if (!it->enabled && glossy) {
                    gfx_draw_text(s, font, x + MENU_PAD_L + 1, cy + 5, lab,
                                  gfx_tint(p->face, 0xFFFFFF, 200));
                }
                gfx_draw_text(s, font, x + MENU_PAD_L, cy + 4, lab, tcol);
                /*
                 * The accelerator is right-aligned against the menu's inner
                 * edge, which is what puts every shortcut in one column. It
                 * is also drawn a shade back from the label when the row is
                 * not selected: the shortcut is a reminder, not the command,
                 * and at equal weight it competes with the word the eye is
                 * actually looking for.
                 */
                if (acc != NULL && acc[0] != '\0') {
                    int aw = gfx_text_width(font, acc);
                    int ax = x + w - MENU_PAD_R - aw;
                    CColor ac = tcol;
                    if (i != m->highlight && it->enabled) {
                        ac = gfx_tint(p->text, p->face, 96);
                    }
                    if (ax < x + MENU_PAD_L) { ax = x + MENU_PAD_L; }
                    gfx_draw_text(s, font, ax, cy + 4, acc, ac);
                }
            }
        }
        cy += ih;
    }
}

/*
 * The rectangle row i is highlighted in -- exactly the `hl` ui_menu_draw
 * fills, so a caller repainting a highlight change repaints what changed and
 * not the window behind it.
 *
 * It is here rather than beside the window code because it is arithmetic: the
 * same walk down the item heights that ui_menu_hit does, answering the other
 * direction of the same question. That keeps it in the hermetic test link,
 * where tests/test_ui.c can check it against ui_menu_hit -- the row a point
 * hits must be the row whose rectangle contains that point, and a menu whose
 * two answers disagree paints one row and lights another.
 */
cbool ui_menu_row_rect(const UiMenu *m, int x, int y, GfxFontId font, int i,
                       CRect *out)
{
    int w, h, k, cy;
    if (m == NULL || out == NULL) { return CFALSE; }
    if (i < 0 || i >= m->count) { return CFALSE; }
    if (m->items[i].id == UI_MENU_SEPARATOR_ID) { return CFALSE; }
    ui_menu_measure(m, font, &w, &h);
    cy = y + MENU_PAD_V;
    for (k = 0; k < i; k++) { cy += item_height(&m->items[k]); }
    *out = crect_make(x + 2, cy, w - 4, item_height(&m->items[i]));
    return CTRUE;
}

int ui_menu_hit(const UiMenu *m, int x, int y, GfxFontId font, int px, int py)
{
    int w, h, i, cy;
    if (m == NULL) { return -1; }
    ui_menu_measure(m, font, &w, &h);
    if (px < x || px >= x + w || py < y || py >= y + h) { return -1; }
    cy = y + MENU_PAD_V;
    for (i = 0; i < m->count; i++) {
        int ih = item_height(&m->items[i]);
        if (py >= cy && py < cy + ih) {
            if (m->items[i].id == UI_MENU_SEPARATOR_ID) { return -1; }
            if (!m->items[i].enabled) { return -1; }
            return i;
        }
        cy += ih;
    }
    return -1;
}
