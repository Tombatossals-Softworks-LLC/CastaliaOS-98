/*
 * office_ui.c - Shared office-suite chrome (see office_ui.h).
 *
 * Every icon is drawn from primitives rather than shipped as art, so the suite
 * stays original and needs no icon pack to look right.
 */
#include "office_ui.h"
#include "castalia/ui.h"
#include "castalia/wm.h"
#include "castalia/sys.h"
#include "castalia/plat.h"

/* ---- palette helpers -------------------------------------------------- */
static CColor of_band_top(void)  { return gfx_tint(ui_palette()->face, 0xFFFFFF, 150); }
static CColor of_band_bot(void)  { return gfx_tint(ui_palette()->face, 0x000000, 12); }
static CColor of_hot_fill(void)  { return gfx_tint(ui_palette()->accent, 0xFFFFFF, 170); }
static CColor of_hot_edge(void)  { return ui_palette()->accent; }

void office_band(GfxSurface *s, const CRect *r)
{
    const UiPalette *p = ui_palette();
    gfx_vgradient(s, r, of_band_top(), of_band_bot());
    gfx_hline(s, r->x0, r->y0, crect_w(r), gfx_tint(p->face, 0xFFFFFF, 200));
    gfx_hline(s, r->x0, r->y1 - 1, crect_w(r), gfx_tint(p->face, 0x000000, 60));
}

/* ---- menu bar --------------------------------------------------------- */
CRect office_menu_word(const CRect *bar, const char *const *names, int idx)
{
    int i, x = bar->x0 + 3;
    for (i = 0; i < idx; i++) {
        x += gfx_text_width(GFX_FONT_SYSTEM, names[i]) + 13;
    }
    return crect_make(x, bar->y0 + 1,
                      gfx_text_width(GFX_FONT_SYSTEM, names[idx]) + 12,
                      crect_h(bar) - 2);
}

void office_menubar(GfxSurface *s, const CRect *bar,
                    const char *const *names, int count, int hot)
{
    const UiPalette *p = ui_palette();
    int i;
    office_band(s, bar);
    for (i = 0; i < count; i++) {
        CRect w = office_menu_word(bar, names, i);
        CColor tc = p->text;
        if (i == hot) {                      /* Office XP highlights flat */
            gfx_fill_rect(s, &w, p->accent);
            tc = p->accent_text;
        }
        gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &w, names[i], tc,
                           GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
    }
}

/* ---- buttons ---------------------------------------------------------- */
void office_sep(GfxSurface *s, int x, int y, int h)
{
    const UiPalette *p = ui_palette();
    gfx_vline(s, x, y + 2, h - 4, gfx_tint(p->face, 0x000000, 60));
    gfx_vline(s, x + 1, y + 2, h - 4, gfx_tint(p->face, 0xFFFFFF, 200));
}

void office_button(GfxSurface *s, const CRect *r, int icon, int state)
{
    int ix = r->x0 + (crect_w(r) - OF_ICON) / 2;
    int iy = r->y0 + (crect_h(r) - OF_ICON) / 2;
    switch (state) {
    case OFB_HOVER:
        gfx_fill_rect(s, r, of_hot_fill());
        gfx_frame_rect(s, r, of_hot_edge());
        break;
    case OFB_PRESSED:
        gfx_fill_rect(s, r, gfx_tint(of_hot_fill(), 0x000000, 30));
        gfx_frame_rect(s, r, of_hot_edge());
        ix++; iy++;
        break;
    case OFB_CHECKED:
        gfx_fill_rect(s, r, gfx_tint(of_hot_fill(), 0x000000, 20));
        gfx_frame_rect(s, r, of_hot_edge());
        break;
    default:
        break;                                /* flat: nothing behind it */
    }
    office_icon(s, ix, iy, icon, (state == OFB_DISABLED) ? CTRUE : CFALSE);
}

/* ---- icons ------------------------------------------------------------ */
/* Draw a letter glyph centered in the icon cell, in the given face. */
static void icon_letter(GfxSurface *s, int x, int y, const char *ch,
                        GfxFontId face, CColor col)
{
    CRect cell = crect_make(x, y, OF_ICON, OF_ICON);
    gfx_draw_text_rect(s, face, &cell, ch, col,
                       GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
}

/* Four "text lines" used by the alignment icons; 'mode' 0=left 1=center 2=right. */
static void icon_lines(GfxSurface *s, int x, int y, int mode, CColor col)
{
    static const int wide[4] = { 12, 12, 12, 12 };
    static const int shrt[4] = { 12, 8, 12, 7 };
    int i;
    for (i = 0; i < 4; i++) {
        int w = ((i & 1) == 0) ? wide[i] : shrt[i];
        int lx = x + 2;
        if (mode == 1) { lx = x + 2 + (12 - w) / 2; }
        else if (mode == 2) { lx = x + 2 + (12 - w); }
        gfx_hline(s, lx, y + 3 + i * 3, w, col);
    }
}

void office_icon(GfxSurface *s, int x, int y, int icon, cbool dim)
{
    const UiPalette *p = ui_palette();
    CColor ink   = dim ? p->text_disabled : GFX_RGB(0x20, 0x24, 0x30);
    CColor paper = dim ? gfx_tint(p->face, 0xFFFFFF, 120) : GFX_RGB(0xFF, 0xFF, 0xFF);
    CColor gold  = dim ? p->text_disabled : GFX_RGB(0xE8, 0xB8, 0x40);
    CColor blue  = dim ? p->text_disabled : GFX_RGB(0x30, 0x50, 0xA0);
    CRect r;

    switch (icon) {
    case OFI_NEW:                                   /* page, folded corner */
        r = crect_make(x + 3, y + 1, 10, 14);
        gfx_fill_rect(s, &r, paper);
        gfx_frame_rect(s, &r, ink);
        gfx_line(s, x + 9, y + 1, x + 12, y + 4, ink);
        gfx_hline(s, x + 5, y + 6, 6, ink);
        gfx_hline(s, x + 5, y + 9, 6, ink);
        break;
    case OFI_OPEN:                                  /* open folder         */
        r = crect_make(x + 1, y + 5, 13, 8);
        gfx_fill_rect(s, &r, gold);
        gfx_frame_rect(s, &r, ink);
        gfx_hline(s, x + 1, y + 4, 6, ink);
        gfx_vline(s, x + 1, y + 4, 2, ink);
        break;
    case OFI_SAVE:                                  /* floppy disk         */
        r = crect_make(x + 1, y + 2, 13, 12);
        gfx_fill_rect(s, &r, blue);
        gfx_frame_rect(s, &r, ink);
        r = crect_make(x + 4, y + 2, 7, 5);         /* shutter             */
        gfx_fill_rect(s, &r, gfx_tint(paper, 0x000000, 40));
        gfx_frame_rect(s, &r, ink);
        r = crect_make(x + 3, y + 9, 9, 5);         /* label               */
        gfx_fill_rect(s, &r, paper);
        gfx_frame_rect(s, &r, ink);
        break;
    case OFI_PRINT:
        r = crect_make(x + 2, y + 6, 12, 6);
        gfx_fill_rect(s, &r, gfx_tint(p->face, 0x000000, 30));
        gfx_frame_rect(s, &r, ink);
        r = crect_make(x + 4, y + 2, 8, 4);
        gfx_fill_rect(s, &r, paper);
        gfx_frame_rect(s, &r, ink);
        r = crect_make(x + 4, y + 11, 8, 4);
        gfx_fill_rect(s, &r, paper);
        gfx_frame_rect(s, &r, ink);
        break;
    case OFI_CUT:                                   /* scissors            */
        gfx_line(s, x + 4, y + 2, x + 11, y + 11, ink);
        gfx_line(s, x + 11, y + 2, x + 4, y + 11, ink);
        gfx_fill_circle(s, x + 4, y + 13, 2, ink);
        gfx_fill_circle(s, x + 11, y + 13, 2, ink);
        break;
    case OFI_COPY:                                  /* two pages           */
        r = crect_make(x + 2, y + 1, 8, 11);
        gfx_fill_rect(s, &r, paper);
        gfx_frame_rect(s, &r, ink);
        r = crect_make(x + 6, y + 4, 8, 11);
        gfx_fill_rect(s, &r, paper);
        gfx_frame_rect(s, &r, ink);
        break;
    case OFI_PASTE:                                 /* clipboard           */
        r = crect_make(x + 2, y + 2, 12, 13);
        gfx_fill_rect(s, &r, gold);
        gfx_frame_rect(s, &r, ink);
        r = crect_make(x + 5, y + 1, 6, 3);
        gfx_fill_rect(s, &r, gfx_tint(p->face, 0x000000, 20));
        gfx_frame_rect(s, &r, ink);
        r = crect_make(x + 4, y + 6, 8, 7);
        gfx_fill_rect(s, &r, paper);
        gfx_frame_rect(s, &r, ink);
        break;
    case OFI_BOLD:   icon_letter(s, x, y, "B", GFX_FONT_BOLD, ink); break;
    case OFI_ITALIC: icon_letter(s, x, y, "I", GFX_FONT_ITALIC, ink); break;
    case OFI_UNDER:
        icon_letter(s, x, y, "U", GFX_FONT_SYSTEM, ink);
        gfx_hline(s, x + 4, y + 12, 8, ink);
        break;
    case OFI_ALEFT:   icon_lines(s, x, y, 0, ink); break;
    case OFI_ACENTER: icon_lines(s, x, y, 1, ink); break;
    case OFI_ARIGHT:  icon_lines(s, x, y, 2, ink); break;
    case OFI_SUM:                                   /* sigma               */
        gfx_hline(s, x + 4, y + 3, 8, ink);
        gfx_hline(s, x + 4, y + 12, 8, ink);
        gfx_line(s, x + 4, y + 3, x + 8, y + 8, ink);
        gfx_line(s, x + 4, y + 12, x + 8, y + 8, ink);
        break;
    case OFI_FUNC:                                  /* fx                  */
        icon_letter(s, x - 3, y, "f", GFX_FONT_BOLDITALIC, ink);
        icon_letter(s, x + 4, y + 2, "x", GFX_FONT_SYSTEM, ink);
        break;
    case OFI_TABLE:                                 /* small grid          */
        r = crect_make(x + 2, y + 3, 12, 10);
        gfx_fill_rect(s, &r, paper);
        gfx_frame_rect(s, &r, ink);
        gfx_hline(s, x + 2, y + 6, 12, ink);
        gfx_vline(s, x + 7, y + 3, 10, ink);
        break;
    default:
        break;
    }
}

/* ---- status bar ------------------------------------------------------- */
void office_statusbar(GfxSurface *s, const CRect *r)
{
    const UiPalette *p = ui_palette();
    gfx_fill_rect(s, r, p->face);
    gfx_hline(s, r->x0, r->y0, crect_w(r), gfx_tint(p->face, 0xFFFFFF, 200));
}

void office_status_panel(GfxSurface *s, const CRect *r, const char *text,
                         int align_flags)
{
    const UiPalette *p = ui_palette();
    CRect tr = *r;
    gfx_bevel(s, r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, GFX_NO_FILL);
    tr.x0 += 5;
    tr.x1 -= 5;
    gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &tr, text, p->text,
                       align_flags | GFX_ALIGN_VCENTER);
}

/* ---- documents folder -------------------------------------------------- */
const char *office_docs_dir(void)
{
    static char dir[CASTALIA_MAX_PATH];
    sys_home_path(dir, (cu32)sizeof dir, "DOCS");
    /* Created on the way past: an app that offers Save must not fail because
     * the folder it defaults to has never been made. */
    plat_mkdir(dir);
    return dir;
}

void office_doc_path(char *out, cu32 outsz, const char *name)
{
    char dir[CASTALIA_MAX_PATH];
    sys_home_path(dir, (cu32)sizeof dir, "DOCS");
    ui_path_default_dir(out, outsz, dir, name);
}

/* ---- keyboard navigation for a menu bar -------------------------------- */
/* Open menu 'idx' and put the highlight nowhere yet, so the first Down lands
 * on the first live row rather than the second. */
static void omn_open(const OfficeMenuNav *nav, int idx)
{
    CRect w = office_menu_word(&nav->bar, nav->names, idx);
    *nav->open = idx;
    *nav->x = w.x0;
    *nav->y = nav->bar.y1;
    nav->build(nav->user, idx);
    nav->menu->highlight = -1;
}

cbool office_menu_key(const OfficeMenuNav *nav, int key, int *cmd)
{
    if (cmd != NULL) { *cmd = -1; }
    if (nav == NULL || nav->open == NULL || nav->menu == NULL ||
        nav->build == NULL || nav->count < 1) {
        return CFALSE;
    }

    if (*nav->open < 0) {
        /* Closed: F10 is the only key that means anything here. Alt+Space is
         * the window menu and belongs to the WM, not to us. */
        if (key == PLAT_KEY_F10) { omn_open(nav, 0); return CTRUE; }
        return CFALSE;
    }

    switch (key) {
    case PLAT_KEY_ESC:
        *nav->open = -1;
        return CTRUE;
    case PLAT_KEY_LEFT:
    case PLAT_KEY_RIGHT: {
        /*
         * Along the bar, wrapping. Wrapping rather than stopping because a
         * menu bar is a ring in every system this one is imitating, and
         * because stopping at the end is indistinguishable from a dead key.
         */
        int n = nav->count;
        int i = *nav->open + ((key == PLAT_KEY_RIGHT) ? 1 : -1);
        if (i < 0)  { i = n - 1; }
        if (i >= n) { i = 0; }
        omn_open(nav, i);
        return CTRUE;
    }
    case PLAT_KEY_UP:
        ui_menu_step(nav->menu, -1);
        return CTRUE;
    case PLAT_KEY_DOWN:
        ui_menu_step(nav->menu, 1);
        return CTRUE;
    case PLAT_KEY_HOME:
    case PLAT_KEY_END:
        /* Jump to the first or last live row: with nothing highlighted, one
         * step in the right direction lands there, because ui_menu_step wraps
         * and skips separators and disabled rows for us. */
        nav->menu->highlight = -1;
        ui_menu_step(nav->menu, (key == PLAT_KEY_HOME) ? 1 : -1);
        return CTRUE;
    case PLAT_KEY_ENTER: {
        int h = nav->menu->highlight;
        /*
         * Enter with nothing highlighted closes the menu and runs NOTHING.
         * The alternative -- treating it as "the first item" -- means a
         * stray Enter silently invokes File > New, and the first item of a
         * File menu is rarely one you want by accident.
         */
        *nav->open = -1;
        if (h >= 0 && ui_menu_selectable(nav->menu, h) && cmd != NULL) {
            *cmd = nav->menu->items[h].id;
        }
        return CTRUE;
    }
    default:
        break;
    }
    return CFALSE;
}

/*
 * The same key, plus the repaint it deserves.
 *
 * office_menu_key is deliberately window-free -- it is arithmetic over a
 * menu and a bar -- so it cannot repaint anything, and all four apps
 * answered that by repainting themselves whole. Walking down a File menu
 * with the arrows therefore cost one full window per row, which is the
 * price the MOUSE used to pay for the same gesture and no longer does.
 *
 * The distinction that makes this safe is whether the drop-down MOVED. If
 * the same menu is still open at the same place, the only thing on the
 * screen that differs is the two rows the highlight went between. Anything
 * else -- Esc, Enter, F10, sliding along the bar to another menu -- either
 * removes the drop-down or draws a differently sized one somewhere else,
 * and there is no rectangle smaller than the window that covers both.
 */
cbool office_menu_key_win(struct WmWindow *win, const OfficeMenuNav *nav,
                          int key, int *cmd)
{
    int was_open, was_x, was_y, was_hl;
    cbool used;
    if (nav == NULL || nav->open == NULL || nav->menu == NULL ||
        nav->x == NULL || nav->y == NULL) {
        return office_menu_key(nav, key, cmd);
    }
    was_open = *nav->open;
    was_x    = *nav->x;
    was_y    = *nav->y;
    was_hl   = nav->menu->highlight;

    used = office_menu_key(nav, key, cmd);
    if (!used) { return CFALSE; }
    /* An item was chosen: the caller runs it, and whatever it does to the
     * document decides what has to be redrawn. Not our repaint to make. */
    if (cmd != NULL && *cmd >= 0) { return CTRUE; }

    if (*nav->open >= 0 && *nav->open == was_open &&
        *nav->x == was_x && *nav->y == was_y) {
        ui_menu_repaint(win, nav->menu, *nav->x, *nav->y, GFX_FONT_SYSTEM,
                        was_hl, nav->menu->highlight);
    } else {
        wm_invalidate(win, NULL);
    }
    return CTRUE;
}
