/*
 * ui_controls.c - Checkbox, radio button, and listbox controls.
 *
 * Checkbox and radio are stateless draw helpers (the caller owns the boolean
 * and does row hit-testing), matching ui_draw_button's style. The listbox is a
 * small stateful control (UiList) with its own selection and scroll, drawn into
 * a sunken well. All colors come from the active UiPalette so a theme switch
 * recolors them.
 */
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#define BOX_SZ      13
#define LIST_ROW_H  14

/* ---- little geometry helpers ----------------------------------------- */
static void fill_disc(GfxSurface *s, int cx, int cy, int r, CColor col)
{
    int dy;
    for (dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) { dx++; }
        gfx_hline(s, cx - dx, cy + dy, 2 * dx + 1, col);
    }
}

/* ---- checkbox -------------------------------------------------------- */
static void draw_check_glyph(GfxSurface *s, int bx, int by, CColor col)
{
    /* Two thick strokes forming a check inside the 13x13 box. */
    gfx_line(s, bx + 3, by + 6, bx + 5, by + 9, col);
    gfx_line(s, bx + 4, by + 6, bx + 6, by + 9, col);
    gfx_line(s, bx + 5, by + 9, bx + 10, by + 3, col);
    gfx_line(s, bx + 6, by + 9, bx + 11, by + 3, col);
}

/*
 * The label of a checkbox or radio, fitted to what is left of the row after
 * the box, and clipped to the row as a guarantee.
 *
 * Both of these drew the raw string with gfx_draw_text, which clips to
 * nothing at all. Measured on a 70-pixel row with a 33-character label: 240
 * pixels painted outside the control, each -- worse than the 168 the button
 * managed, and for the same reason. ui_draw_button was fixed first; these two
 * were found by asking whether anything ELSE drew a caller's string into a
 * fixed box, rather than by assuming the button was special.
 */
static void label_in_row(GfxSurface *s, const CRect *r, int lx,
                         const char *label, CColor txt)
{
    char fitted[96];
    CRect saved;
    int th = gfx_font_height(GFX_FONT_SYSTEM);
    if (label == NULL) { return; }
    ui_text_fit(fitted, sizeof fitted, label, r->x1 - lx - 2, GFX_FONT_SYSTEM);
    saved = gfx_clip_narrow(s, r);
    gfx_draw_text(s, GFX_FONT_SYSTEM, lx, r->y0 + (crect_h(r) - th) / 2,
                  fitted, txt);
    gfx_set_clip(s, &saved);
}

void ui_draw_check(GfxSurface *s, const CRect *r, const char *label,
                   cbool checked, UiButtonState state)
{
    const UiPalette *p = ui_palette();
    CColor white = GFX_RGB(0xFF, 0xFF, 0xFF);
    CColor txt = (state == UI_BTN_DISABLED) ? p->text_disabled : p->text;
    int bx = r->x0 + 1;
    int by = r->y0 + (crect_h(r) - BOX_SZ) / 2;
    CRect box = crect_make(bx, by, BOX_SZ, BOX_SZ);
    int th = gfx_font_height(GFX_FONT_SYSTEM);
    gfx_bevel(s, &box, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);
    if (checked) { draw_check_glyph(s, bx, by, txt); }
    label_in_row(s, r, bx + BOX_SZ + 6, label, txt);
    CASTALIA_UNUSED(th);
}

void ui_draw_radio(GfxSurface *s, const CRect *r, const char *label,
                   cbool selected, UiButtonState state)
{
    const UiPalette *p = ui_palette();
    CColor white = GFX_RGB(0xFF, 0xFF, 0xFF);
    CColor txt = (state == UI_BTN_DISABLED) ? p->text_disabled : p->text;
    int bx = r->x0 + 1;
    int by = r->y0 + (crect_h(r) - BOX_SZ) / 2;
    int cx = bx + BOX_SZ / 2, cy = by + BOX_SZ / 2;
    int th = gfx_font_height(GFX_FONT_SYSTEM);
    fill_disc(s, cx, cy, 6, p->dark);   /* outer ring   */
    fill_disc(s, cx, cy, 5, white);     /* white face   */
    if (selected) { fill_disc(s, cx, cy, 2, txt); } /* filled center */
    label_in_row(s, r, bx + BOX_SZ + 6, label, txt);
    CASTALIA_UNUSED(th);
}

/* ---- listbox --------------------------------------------------------- */
void ui_list_clear(UiList *l)
{
    if (l == NULL) { return; }
    l->count = 0; l->sel = -1; l->top = 0;
}

CResult ui_list_add(UiList *l, const char *text)
{
    if (l == NULL) { return CE_INVALID; }
    if (l->count >= UI_LIST_MAX) { return CE_OVERFLOW; }
    sys_strlcpy(l->items[l->count], (text ? text : ""), CASTALIA_MAX_NAME);
    if (l->sel < 0) { l->sel = 0; }
    l->count++;
    return CE_OK;
}

int ui_list_sel(const UiList *l) { return (l != NULL) ? l->sel : -1; }

static int list_visible(const CRect *r) { return (crect_h(r) - 4) / LIST_ROW_H; }

/* Keep the selected row within the visible window. */
static void list_reveal(UiList *l, int visible)
{
    if (l->sel < 0) { return; }
    if (l->sel < l->top) { l->top = l->sel; }
    if (l->sel >= l->top + visible) { l->top = l->sel - visible + 1; }
    if (l->top < 0) { l->top = 0; }
}

CColor ui_text_dim(void)
{
    const UiPalette *p = ui_palette();
    /* A bit under half way to the control face: clearly quieter than body
     * text, clearly louder than the disabled grey next to it. */
    return gfx_tint(p->text, p->face, 110);
}

/*
 * The two colours a SELECTION is drawn in, and the one place that decides.
 *
 * A window that does not have the keyboard draws its selection quiet: the
 * band goes from the accent to a grey off the control face and the text goes
 * back to ordinary ink. That is the convention the systems this desktop
 * copies used, and the reason is not decoration -- an accent-blue row says
 * "the arrow keys move this", and in a window nobody is typing into that is
 * false.
 *
 * It lives here because five things draw a selection: this list control, the
 * File Manager's rows, Notepad's, CastaliaWrite's and CastaliaSheet's. Four
 * of them spelled the live colour out and none of them spelled the quiet one,
 * so four windows disagreed with the fifth about what "not focused" looks
 * like -- by drawing nothing at all.
 */
void ui_sel_colors(cbool focused, CColor *band, CColor *ink)
{
    const UiPalette *p = ui_palette();
    if (band != NULL) {
        *band = focused ? p->accent : gfx_tint(p->face, p->dark, 90);
    }
    if (ink != NULL) {
        *ink = focused ? p->accent_text : p->text;
    }
}

void ui_list_draw(GfxSurface *s, const CRect *r, UiList *l, cbool focused)
{
    const UiPalette *p = ui_palette();
    CColor white = GFX_RGB(0xFF, 0xFF, 0xFF);
    int visible = list_visible(r);
    int i;
    CRect saved_clip;
    /*
     * 'focused' had a CASTALIA_UNUSED on it. Every caller passed one and the
     * list drew a live accent-blue selection either way -- so the Control
     * Center, which moves focus off its category list with Tab and draws a
     * ring on the panel row that gains it, showed TWO things claiming the
     * keyboard at once.
     */
    CColor band, ink;
    ui_sel_colors(focused, &band, &ink);
    if (l == NULL) { return; }
    gfx_bevel(s, r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);
    list_reveal(l, visible);
    /* Fitted AND clipped, for the reason ui_draw_button documents: fitting is
     * what makes a long entry read "A VERY LO..." instead of running out of
     * the control, and the clip is what holds when the box is too narrow for
     * even the mark. A 14-pixel list still leaked 167 pixels with the fit
     * alone. */
    saved_clip = gfx_clip_narrow(s, r);
    for (i = 0; i < visible; i++) {
        int idx = l->top + i;
        int ry = r->y0 + 2 + i * LIST_ROW_H;
        CRect row;
        if (idx >= l->count) { break; }
        row = crect_make(r->x0 + 2, ry, crect_w(r) - 4, LIST_ROW_H);
        /* Fitted to the row. An item wider than the box used to run straight
         * out of the control -- 194 pixels outside an 80-pixel list, measured
         * with one long entry. The list has a scrollbar for its HEIGHT and
         * nothing at all for its width. */
        {
            char it[CASTALIA_MAX_NAME + 8];
            CColor tc = (idx == l->sel) ? ink : p->text;
            if (idx == l->sel) { gfx_fill_rect(s, &row, band); }
            gfx_text_fit(it, sizeof it, l->items[idx],
                         crect_w(&row) - 8 - ((l->count > visible) ? 8 : 0),
                         GFX_FONT_SYSTEM);
            gfx_draw_text(s, GFX_FONT_SYSTEM, row.x0 + 4, ry + 3, it, tc);
        }
    }
    gfx_set_clip(s, &saved_clip);
    /* Scrollbar thumb when the list overflows. */
    if (l->count > visible && visible > 0) {
        int tx = r->x1 - 8, ty = r->y0 + 1, tk = crect_h(r) - 2;
        int th = (visible * tk) / l->count;
        int thy;
        CRect track, thumb;
        if (th < 8) { th = 8; }
        thy = ty + (l->top * (tk - th)) / (l->count - visible);
        track = crect_make(tx, ty, 7, tk);
        gfx_fill_rect(s, &track, p->face);
        thumb = crect_make(tx, thy, 7, th);
        gfx_bevel(s, &thumb, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    }
}

cbool ui_list_click(UiList *l, const CRect *r, int px, int py)
{
    int idx;
    if (l == NULL || !crect_contains(r, px, py)) { return CFALSE; }
    idx = l->top + (py - (r->y0 + 2)) / LIST_ROW_H;
    if (idx >= 0 && idx < l->count && idx != l->sel) {
        l->sel = idx;
        return CTRUE;
    }
    return CFALSE;
}

cbool ui_list_key(UiList *l, const CRect *r, int key)
{
    int before;
    if (l == NULL || l->count == 0) { return CFALSE; }
    before = l->sel;
    if (key == PLAT_KEY_UP && l->sel > 0) { l->sel--; }
    else if (key == PLAT_KEY_DOWN && l->sel < l->count - 1) { l->sel++; }
    else if (key == PLAT_KEY_HOME) { l->sel = 0; }
    else if (key == PLAT_KEY_END) { l->sel = l->count - 1; }
    else { return CFALSE; }
    list_reveal(l, list_visible(r));
    return (l->sel != before) ? CTRUE : CFALSE;
}

/* ---- meter (progress / gauge) ----------------------------------------- */
/*
 * The widest value the multiply below may see. Deliberately a cs32 and not a
 * long: see the note in ui.h. This mirrors map_core.c's MAP_LONG_SAFE.
 */
#define UI_METER_SAFE ((cs32)2000000000)

int ui_meter_fill(cs32 value, cs32 maxv, int width)
{
    if (width <= 0 || maxv <= 0 || value <= 0) { return 0; }
    if (value >= maxv) { return width; }
    /*
     * value * width overflows 32 bits far sooner than it looks. The Task
     * Manager's memory gauge measures BYTES against an 8 MB budget across a
     * 386-pixel bar: the product passes 2^31 at 5.31 MB, wraps negative, and
     * the clamp that used to follow turned that into a fill of zero. The bar
     * read EMPTY for the top third of its own range -- reporting "no memory
     * in use" at precisely the moment memory was nearly exhausted.
     *
     * Halving both sides until the multiply is safe costs at most a bit of
     * precision per step, invisible in a bar a few hundred pixels wide. A
     * negative product is not invisible.
     */
    while (value > UI_METER_SAFE / (cs32)width) {
        value /= (cs32)2;
        maxv  /= (cs32)2;
        if (maxv <= 0) { return width; }   /* scaled away: value dominates */
    }
    return (int)((value * (cs32)width) / maxv);
}

void ui_draw_tri(GfxSurface *s, const CRect *r, int dir, CColor col)
{
    int w, h, i;
    if (s == NULL || r == NULL) { return; }
    w = crect_w(r);
    h = crect_h(r);
    if (w <= 0 || h <= 0) { return; }

    if (dir == UI_TRI_RIGHT || dir == UI_TRI_LEFT) {
        /* One column per x. The span at the base is the full height and it
         * narrows to a point at the apex; which end is which is the ONLY
         * thing that differs between the two, and it is written once. */
        for (i = 0; i < w; i++) {
            int step = (dir == UI_TRI_RIGHT) ? (w - 1 - i) : i;
            int len  = (w > 1) ? (1 + (h - 1) * step / (w - 1)) : h;
            if (len < 1) { len = 1; }
            gfx_vline(s, r->x0 + i, r->y0 + (h - len) / 2, len, col);
        }
    } else {
        for (i = 0; i < h; i++) {
            int step = (dir == UI_TRI_DOWN) ? (h - 1 - i) : i;
            int len  = (h > 1) ? (1 + (w - 1) * step / (h - 1)) : w;
            if (len < 1) { len = 1; }
            gfx_hline(s, r->x0 + (w - len) / 2, r->y0 + i, len, col);
        }
    }
}

void ui_path_fit(char *dst, cu32 dstsz, const char *path, int width,
                 GfxFontId font)
{
    const char *tail;
    if (dst == NULL || dstsz == 0u) { return; }
    if (path == NULL) { dst[0] = '\0'; return; }
    sys_strlcpy(dst, path, dstsz);
    if (width <= 0 || gfx_text_width(font, dst) <= width) { return; }
    /*
     * Walk forward until what remains fits BESIDE the ellipsis -- so the room
     * reserved for it has to be the ellipsis's real width, measured in the
     * font being used.
     *
     * The version this was extracted from reserved a hard-coded 12 pixels.
     * "..." is three cells of the 8-pixel system font, so it is 24, and the
     * finished string could come out about twelve pixels wider than the space
     * it was fitted to. Nobody would have seen it as anything but a path that
     * sat a little close to the edge. Found by fitting a deep path to 120 px
     * and measuring the result, which is a thing worth doing to any function
     * whose whole job is "make it fit".
     */
    tail = path;
    {
        int ell = gfx_text_width(font, "...");
        while (*tail != '\0' && gfx_text_width(font, tail) > width - ell) {
            tail++;
        }
    }
    sys_snprintf(dst, dstsz, "...%s", tail);
}

void ui_text_fit(char *dst, cu32 dstsz, const char *text, int width,
                 GfxFontId font)
{
    /*
     * The implementation lives in gfx_font.c, because fitting text to a width
     * needs nothing but gfx_text_width -- and the window manager needs it too.
     * src/wm has never depended on castalia/ui.h and should not start: the
     * controls library and the window manager are peers, not a stack. This
     * stays so the apps that already call it keep working.
     */
    gfx_text_fit(dst, dstsz, text, width, font);
}

int ui_percent(cs32 part, cs32 whole)
{
    /* A hundred pixels wide, and therefore a percentage. Deliberately not a
     * second copy of the arithmetic: the overflow this avoids is the one
     * ui_meter_fill documents, and it has now been written by hand -- wrongly
     * -- in six places in this tree. */
    return ui_meter_fill(part, whole, 100);
}

void ui_draw_meter(GfxSurface *s, const CRect *r, cs32 value, cs32 maxv,
                   CColor fill_color)
{
    const UiPalette *p = ui_palette();
    CColor white = GFX_RGB(0xFF, 0xFF, 0xFF);
    CRect fill;
    int inner, fw;
    if (s == NULL || r == NULL) { return; }
    gfx_bevel(s, r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);
    inner = crect_w(r) - 2;
    fw = ui_meter_fill(value, maxv, inner);
    if (fw <= 0) { return; }
    fill = crect_make(r->x0 + 1, r->y0 + 1, fw, crect_h(r) - 2);
    gfx_fill_rect(s, &fill, fill_color);
}
