/*
 * ui_edit.c - Single-line editable text field.
 *
 * A small, self-contained text control: insert/delete at a caret, arrow/Home/
 * End navigation, and horizontal scrolling to keep the caret visible. Used by
 * the prompt dialog (rename, New Folder, Run) and available to apps.
 */
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/sys.h"
#include "castalia/plat.h"
#include "castalia/clip.h"

#define EDIT_ADVANCE 6   /* must match the system font advance */

/* Ctrl-shortcut detection that works on both backends: the DOS INT 16h path
 * delivers Ctrl+letter as the ASCII control code in 'key' (1/3/22/24), while
 * synthetic/host input may instead carry the letter with PLAT_MOD_CTRL set. */
#define EDIT_IS_CTRL(key, ch, mods, ctlcode, letter) \
    ((key) == (ctlcode) || \
     (((mods) & PLAT_MOD_CTRL) && ((ch) == (letter) || (ch) == (letter) - 32)))

void ui_edit_init(UiEdit *e, const char *initial)
{
    e->text[0] = '\0';
    e->len = 0;
    e->caret = 0;
    e->scroll = 0;
    if (initial != NULL) {
        sys_strlcpy(e->text, initial, sizeof(e->text));
        e->len = (int)sys_strnlen(e->text, sizeof(e->text));
        e->caret = e->len;
    }
}

const char *ui_edit_text(const UiEdit *e) { return e->text; }

static void insert_char(UiEdit *e, int c)
{
    int i;
    if (e->len >= UI_EDIT_MAX - 1) { return; }
    for (i = e->len; i > e->caret; i--) { e->text[i] = e->text[i - 1]; }
    e->text[e->caret] = (char)c;
    e->len++;
    e->caret++;
    e->text[e->len] = '\0';
}

static void delete_at(UiEdit *e, int idx)
{
    int i;
    if (idx < 0 || idx >= e->len) { return; }
    for (i = idx; i < e->len - 1; i++) { e->text[i] = e->text[i + 1]; }
    e->len--;
    e->text[e->len] = '\0';
}

/* Insert a whole string at the caret (bounded), for paste. */
static void insert_str(UiEdit *e, const char *s)
{
    while (*s != '\0') {
        if (*s >= 32 && *s < 127) { insert_char(e, (unsigned char)*s); }
        s++;
    }
}

cbool ui_edit_key(UiEdit *e, int key, int ch, int mods)
{
    /* Clipboard: the single-line field has no selection, so copy/cut act on the
     * whole text and paste inserts at the caret. Checked before text insert so
     * the control codes aren't mistaken for characters. */
    if (EDIT_IS_CTRL(key, ch, mods, 3, 'c')) {       /* Ctrl+C */
        clip_set_text(e->text, e->len);
        return CTRUE;
    }
    if (EDIT_IS_CTRL(key, ch, mods, 24, 'x')) {      /* Ctrl+X */
        clip_set_text(e->text, e->len);
        e->text[0] = '\0'; e->len = 0; e->caret = 0; e->scroll = 0;
        return CTRUE;
    }
    if (EDIT_IS_CTRL(key, ch, mods, 22, 'v')) {      /* Ctrl+V */
        insert_str(e, clip_get_text());
        return CTRUE;
    }

    if (ch >= 32 && ch < 127) {
        insert_char(e, ch);
        return CTRUE;
    }
    switch (key) {
    case PLAT_KEY_BACKSP:
        if (e->caret > 0) { delete_at(e, e->caret - 1); e->caret--; }
        return CTRUE;
    case PLAT_KEY_DELETE:
        if (e->caret < e->len) { delete_at(e, e->caret); }
        return CTRUE;
    case PLAT_KEY_LEFT:
        if (e->caret > 0) { e->caret--; }
        return CTRUE;
    case PLAT_KEY_RIGHT:
        if (e->caret < e->len) { e->caret++; }
        return CTRUE;
    default:
        return CFALSE;
    }
}

static int visible_chars(const CRect *r)
{
    int w = crect_w(r) - 6;
    int n = (w > 0) ? w / EDIT_ADVANCE : 0;
    return (n > 0) ? n : 1;
}

void ui_edit_draw(GfxSurface *s, const CRect *r, UiEdit *e, cbool focused)
{
    const UiPalette *p = ui_palette();
    int vis = visible_chars(r);
    int i, x;
    /* Keep the caret within the visible window. */
    if (e->caret < e->scroll) { e->scroll = e->caret; }
    if (e->caret >= e->scroll + vis) { e->scroll = e->caret - vis + 1; }
    if (e->scroll < 0) { e->scroll = 0; }

    gfx_bevel(s, r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark,
              GFX_RGB(0xFF, 0xFF, 0xFF));

    x = r->x0 + 3;
    for (i = 0; i < vis && (e->scroll + i) < e->len; i++) {
        char ch[2];
        ch[0] = e->text[e->scroll + i];
        ch[1] = '\0';
        gfx_draw_text(s, GFX_FONT_SYSTEM, x + i * EDIT_ADVANCE,
                      r->y0 + (crect_h(r) - 8) / 2, ch, p->text);
    }
    if (focused) {
        int cx = r->x0 + 3 + (e->caret - e->scroll) * EDIT_ADVANCE;
        gfx_vline(s, cx, r->y0 + 3, crect_h(r) - 6, p->text);
    }
}

void ui_edit_click(UiEdit *e, const CRect *r, int px)
{
    int rel = px - (r->x0 + 3);
    int idx;
    if (rel < 0) { rel = 0; }
    idx = e->scroll + (rel + EDIT_ADVANCE / 2) / EDIT_ADVANCE;
    if (idx < 0) { idx = 0; }
    if (idx > e->len) { idx = e->len; }
    e->caret = idx;
}
