/*
 * app_theme.c - CastaliaOS Theme Editor.
 *
 * Every color the shell draws with, in one window: pick a slot on the left,
 * mix it with the RGB sliders or the quick palette, and watch a live miniature
 * of the desktop -- wallpaper gradient, a window with its title bar, a button,
 * a menu highlight and the taskbar -- repaint as you drag. Nothing is applied
 * to the real desktop until you press Apply, so a bad guess costs nothing.
 *
 * The preview draws with the theme being EDITED rather than the active palette,
 * which is why it passes its colors explicitly to every primitive instead of
 * reading ui_palette().
 *
 * Themes save to plain INI (see sh_theme_io.c, host-tested round trip), so a
 * theme is a file you can mail to somebody.
 */
#include "apps.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"
#include "castalia/shell.h"
#include <stdlib.h>

#define TH_PAD     8
#define TH_ROW     15
#define TH_SLOTS   16
#define TH_SWATCH  22          /* quick-palette cell                        */

static const char *const TH_NAME[TH_SLOTS] = {
    "Desktop top", "Desktop bottom",
    "Title active L", "Title active R",
    "Title idle L", "Title idle R", "Title text",
    "Control face", "Control light", "Control shadow", "Control dark",
    "Text", "Disabled text", "Accent", "Accent text", "Taskbar"
};

/* The quick palette: a spread that covers what a theme actually needs --
 * neutrals to build the control grays from, and saturated hues for accents. */
static const CColor TH_QUICK[28] = {
    0x000000UL, 0x202020UL, 0x404040UL, 0x606060UL, 0x808080UL, 0xA0A0A0UL,
    0xC0C0C0UL, 0xFFFFFFUL,
    0x1B3F7AUL, 0x2A5BC8UL, 0x3674D6UL, 0x4E8BD8UL, 0x8EA0C2UL, 0xD8E0EEUL,
    0x1E5A38UL, 0x3C8458UL, 0x76B08AUL, 0x2A4030UL,
    0x7A2020UL, 0xC03030UL, 0xE07850UL, 0xC8A020UL, 0xF0D060UL,
    0x5A2A6AUL, 0x8A4AA0UL, 0x206A6AUL, 0x2AA0A0UL, 0x3C4048UL
};

typedef struct {
    ShTheme t;
    int  slot;                  /* selected color slot                      */
    int  drag;                  /* 0 none, 1..3 = R/G/B slider              */
    int  preset;                /* last preset loaded, for the button label */
    char path[CASTALIA_MAX_PATH];
    char status[64];
    UiHot hot;                  /* which of the eight buttons the pointer is on */
} ThemeApp;

typedef struct {
    CRect list, preview, mixer, quick;
    CRect bar[3];               /* R/G/B slider tracks                      */
    CRect btn_preset, btn_apply, btn_save, btn_open;
    CRect title_dec, title_inc, border_dec, border_inc;
} ThLayout;

/* The eight push buttons, in one order, so the pointer state and the painting
 * cannot disagree about which is which. */
enum { THB_TITLE_DEC = 0, THB_TITLE_INC, THB_BORDER_DEC, THB_BORDER_INC,
       THB_PRESET, THB_APPLY, THB_SAVE, THB_OPEN, THB_COUNT };

static CRect th_btn_rect(const ThLayout *L, int i)
{
    switch (i) {
    case THB_TITLE_DEC:  return L->title_dec;
    case THB_TITLE_INC:  return L->title_inc;
    case THB_BORDER_DEC: return L->border_dec;
    case THB_BORDER_INC: return L->border_inc;
    case THB_PRESET:     return L->btn_preset;
    case THB_APPLY:      return L->btn_apply;
    case THB_SAVE:       return L->btn_save;
    default:             return L->btn_open;
    }
}

static int th_btn_at(const ThLayout *L, int px, int py)
{
    int i;
    for (i = 0; i < THB_COUNT; i++) {
        CRect r = th_btn_rect(L, i);
        if (crect_contains(&r, px, py)) { return i; }
    }
    return -1;
}

/* ---- the slot table ---------------------------------------------------- */
static CColor *th_slot(ShTheme *t, int i)
{
    switch (i) {
    case 0:  return &t->desktop_top;
    case 1:  return &t->desktop_bottom;
    case 2:  return &t->title_active_l;
    case 3:  return &t->title_active_r;
    case 4:  return &t->title_inactive_l;
    case 5:  return &t->title_inactive_r;
    case 6:  return &t->title_text;
    case 7:  return &t->ui.face;
    case 8:  return &t->ui.light;
    case 9:  return &t->ui.dark;
    case 10: return &t->ui.darker;
    case 11: return &t->ui.text;
    case 12: return &t->ui.text_disabled;
    case 13: return &t->ui.accent;
    case 14: return &t->ui.accent_text;
    default: return &t->taskbar_face;
    }
}

/* ---- layout ------------------------------------------------------------ */
static void th_layout(WmWindow *win, ThLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c);
    int lw = 150, rx, rw, y, bw, i;

    L->list = crect_make(TH_PAD, TH_PAD, lw, TH_SLOTS * TH_ROW + 4);

    rx = TH_PAD + lw + 8;
    rw = cw - rx - TH_PAD;
    L->preview = crect_make(rx, TH_PAD, rw, 132);

    y = L->preview.y1 + 10;
    L->mixer = crect_make(rx, y, rw, 3 * 18 + 6);
    for (i = 0; i < 3; i++) {
        L->bar[i] = crect_make(rx + 22, y + 3 + i * 18, rw - 70, 12);
    }
    y = L->mixer.y1 + 6;
    /* The box holds the hex label first, then the two swatch rows -- so it is
     * 14 taller than the swatches themselves, and the hit test agrees. */
    L->quick = crect_make(rx, y, rw, 14 + 2 * 14 + 2);

    y = L->quick.y1 + 8;
    L->title_dec = crect_make(rx + 62, y, 16, 16);
    L->title_inc = crect_make(rx + 80, y, 16, 16);
    L->border_dec = crect_make(rx + 62 + 110, y, 16, 16);
    L->border_inc = crect_make(rx + 80 + 110, y, 16, 16);

    bw = (cw - 2 * TH_PAD - 3 * 6) / 4;
    y = ch - TH_PAD - 20;
    L->btn_preset = crect_make(TH_PAD, y, bw, 20);
    L->btn_apply  = crect_make(TH_PAD + (bw + 6), y, bw, 20);
    L->btn_save   = crect_make(TH_PAD + 2 * (bw + 6), y, bw, 20);
    L->btn_open   = crect_make(TH_PAD + 3 * (bw + 6), y, bw, 20);
}

/* ---- the live preview -------------------------------------------------- */
/* A miniature of the whole environment, drawn entirely from 't'. */
static void th_preview(GfxSurface *s, const CRect *r, const ShTheme *t)
{
    CRect desk = crect_inset(r, 2);
    CRect win, title, client, btn, menu, task;
    int tw;

    gfx_vgradient(s, &desk, t->desktop_top, t->desktop_bottom);

    /* Taskbar along the foot. */
    task = crect_make(desk.x0, desk.y1 - 14, crect_w(&desk), 14);
    gfx_fill_rect(s, &task, t->taskbar_face);
    gfx_hline(s, task.x0, task.y0, crect_w(&task), t->ui.light);
    {
        CRect orb = crect_make(task.x0 + 3, task.y0 + 2, 34, 10);
        gfx_bevel(s, &orb, GFX_BEVEL_RAISED_THIN, t->ui.light, t->ui.dark,
                  t->ui.face);
        gfx_draw_text(s, GFX_FONT_SYSTEM, orb.x0 + 4, orb.y0 + 1, "Start",
                      t->ui.text);
    }

    /* A window: title bar, client area, a button and a menu highlight. */
    win = crect_make(desk.x0 + 14, desk.y0 + 12, crect_w(&desk) - 60,
                     crect_h(&desk) - 44);
    if (crect_w(&win) < 80 || crect_h(&win) < 50) { return; }
    /* An inactive window first, so the active one overlaps it -- that is the
     * only way to show both title-bar states at once. */
    {
        CRect back = crect_make(win.x1 - 26, win.y0 + 14, 40, crect_h(&win) - 8);
        CRect bt = crect_make(back.x0 + 2, back.y0 + 2, crect_w(&back) - 4, 10);
        gfx_bevel(s, &back, GFX_BEVEL_RAISED, t->ui.light, t->ui.darker,
                  t->ui.face);
        gfx_vgradient(s, &bt, t->title_inactive_l, t->title_inactive_r);
        gfx_draw_text(s, GFX_FONT_SYSTEM, bt.x0 + 2, bt.y0 + 1, "Idle",
                      t->title_text);
    }
    gfx_bevel(s, &win, GFX_BEVEL_RAISED, t->ui.light, t->ui.darker, t->ui.face);
    title = crect_make(win.x0 + 2, win.y0 + 2, crect_w(&win) - 4,
                       (t->title_height > 8) ? t->title_height - 4 : 8);
    gfx_vgradient(s, &title, t->title_active_l, t->title_active_r);
    gfx_draw_text(s, GFX_FONT_BOLD, title.x0 + 4, title.y0 + 2, "Preview",
                  t->title_text);
    {
        CRect x = crect_make(title.x1 - 14, title.y0 + 2, 11, 11);
        gfx_bevel(s, &x, GFX_BEVEL_RAISED_THIN, t->ui.light, t->ui.dark,
                  t->ui.face);
        gfx_draw_text(s, GFX_FONT_SYSTEM, x.x0 + 3, x.y0 + 2, "x", t->ui.text);
    }
    client = crect_make(win.x0 + 3, title.y1 + 2, crect_w(&win) - 6,
                        win.y1 - title.y1 - 5);
    gfx_fill_rect(s, &client, t->ui.face);

    menu = crect_make(client.x0 + 4, client.y0 + 4, crect_w(&client) - 8, 12);
    gfx_fill_rect(s, &menu, t->ui.accent);
    gfx_draw_text(s, GFX_FONT_SYSTEM, menu.x0 + 3, menu.y0 + 2,
                  "Selected item", t->ui.accent_text);
    gfx_draw_text(s, GFX_FONT_SYSTEM, menu.x0 + 3, menu.y1 + 4,
                  "Normal text", t->ui.text);
    gfx_draw_text(s, GFX_FONT_SYSTEM, menu.x0 + 3, menu.y1 + 16,
                  "Disabled text", t->ui.text_disabled);

    tw = gfx_text_width(GFX_FONT_SYSTEM, "Button") + 16;
    btn = crect_make(client.x1 - tw - 6, client.y1 - 20, tw, 16);
    gfx_bevel(s, &btn, GFX_BEVEL_RAISED, t->ui.light, t->ui.darker, t->ui.face);
    gfx_draw_text(s, GFX_FONT_SYSTEM, btn.x0 + 8, btn.y0 + 4, "Button",
                  t->ui.text);

}

/* ---- painting ---------------------------------------------------------- */
static void th_paint(WmWindow *win, GfxSurface *s)
{
    ThemeApp *a = (ThemeApp *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    ThLayout L;
    CRect c, r, cell;
    char buf[64];
    CColor cur;
    int i, ch[3];

    if (a == NULL) { return; }
    th_layout(win, &L);
    c = wm_client_rect(win);
    gfx_fill_rect(s, &c, p->face);
    cur = *th_slot(&a->t, a->slot);
    ch[0] = GFX_R(cur); ch[1] = GFX_G(cur); ch[2] = GFX_B(cur);

    /* The slot list. */
    r = crect_offset(&L.list, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark,
              GFX_RGB(0xFF, 0xFF, 0xFF));
    for (i = 0; i < TH_SLOTS; i++) {
        CRect row = crect_make(r.x0 + 2, r.y0 + 2 + i * TH_ROW,
                               crect_w(&r) - 4, TH_ROW);
        CColor tc = p->text;
        if (i == a->slot) { gfx_fill_rect(s, &row, p->accent); tc = p->accent_text; }
        cell = crect_make(row.x0 + 2, row.y0 + 2, 18, TH_ROW - 5);
        gfx_fill_rect(s, &cell, *th_slot(&a->t, i));
        gfx_frame_rect(s, &cell, p->text);
        gfx_draw_text(s, GFX_FONT_SYSTEM, row.x0 + 24, row.y0 + 3,
                      TH_NAME[i], tc);
    }

    /* Who we are editing, under the list. */
    r = crect_offset(&L.list, o.x, o.y);
    gfx_draw_text(s, GFX_FONT_BOLD, r.x0, r.y1 + 8, a->t.name, p->text);
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0, r.y1 + 22,
                  "Themes are plain INI", p->text_disabled);
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0, r.y1 + 33,
                  "files under THEMES\\.", p->text_disabled);

    /* Preview. */
    r = crect_offset(&L.preview, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, GFX_NO_FILL);
    th_preview(s, &r, &a->t);

    /* Mixer: three tracks with a thumb, plus the value and the hex string. */
    for (i = 0; i < 3; i++) {
        static const char *NM[3] = { "R", "G", "B" };
        CColor lo, hi;
        int x;
        r = crect_offset(&L.bar[i], o.x, o.y);
        lo = (i == 0) ? GFX_RGB(0, ch[1], ch[2])
           : (i == 1) ? GFX_RGB(ch[0], 0, ch[2])
                      : GFX_RGB(ch[0], ch[1], 0);
        hi = (i == 0) ? GFX_RGB(255, ch[1], ch[2])
           : (i == 1) ? GFX_RGB(ch[0], 255, ch[2])
                      : GFX_RGB(ch[0], ch[1], 255);
        gfx_draw_text(s, GFX_FONT_BOLD, r.x0 - 20, r.y0 + 2, NM[i], p->text);
        /* The track shows what the channel would do -- a gradient from this
         * color with the channel at 0 to the same with it at full. */
        {
            int w = crect_w(&r), k;
            for (k = 0; k < w; k++) {
                CColor mix = gfx_tint(lo, hi, (k * 256) / (w > 1 ? w - 1 : 1));
                gfx_vline(s, r.x0 + k, r.y0, crect_h(&r), mix);
            }
        }
        gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, GFX_NO_FILL);
        x = r.x0 + (ch[i] * (crect_w(&r) - 7)) / 255;
        cell = crect_make(x, r.y0 - 2, 7, crect_h(&r) + 4);
        gfx_bevel(s, &cell, GFX_BEVEL_RAISED, p->light, p->darker, p->face);
        sys_snprintf(buf, sizeof buf, "%3d", ch[i]);
        gfx_draw_text(s, GFX_FONT_SYSTEM, r.x1 + 6, r.y0 + 2, buf, p->text);
    }
    r = crect_offset(&L.mixer, o.x, o.y);
    sys_snprintf(buf, sizeof buf, "%s  #%02X%02X%02X", TH_NAME[a->slot],
                 ch[0], ch[1], ch[2]);
    gfx_draw_text(s, GFX_FONT_BOLD, r.x0, r.y1 - 1, buf, p->text);

    /* Quick palette. */
    r = crect_offset(&L.quick, o.x, o.y);
    for (i = 0; i < 28; i++) {
        int w = crect_w(&r) / 14;
        cell = crect_make(r.x0 + (i % 14) * w, r.y0 + 14 + (i / 14) * 14,
                          w - 1, 13);
        gfx_fill_rect(s, &cell, TH_QUICK[i]);
        gfx_bevel(s, &cell, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark,
                  GFX_NO_FILL);
    }

    /* Metrics. */
    r = crect_offset(&L.title_dec, o.x, o.y);
    sys_snprintf(buf, sizeof buf, "Title %2d", a->t.title_height);
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 - 60, r.y0 + 4, buf, p->text);
    ui_draw_button(s, &r, "-", ui_hot_state(&a->hot, THB_TITLE_DEC,
                                            UI_BTN_NORMAL));
    r = crect_offset(&L.title_inc, o.x, o.y);
    ui_draw_button(s, &r, "+", ui_hot_state(&a->hot, THB_TITLE_INC,
                                            UI_BTN_NORMAL));
    r = crect_offset(&L.border_dec, o.x, o.y);
    sys_snprintf(buf, sizeof buf, "Border %d", a->t.border_width);
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 - 60, r.y0 + 4, buf, p->text);
    ui_draw_button(s, &r, "-", ui_hot_state(&a->hot, THB_BORDER_DEC,
                                            UI_BTN_NORMAL));
    r = crect_offset(&L.border_inc, o.x, o.y);
    ui_draw_button(s, &r, "+", ui_hot_state(&a->hot, THB_BORDER_INC,
                                            UI_BTN_NORMAL));

    /* Buttons and the status line. */
    r = crect_offset(&L.btn_preset, o.x, o.y);
    ui_draw_button(s, &r, "Next Preset",
                   ui_hot_state(&a->hot, THB_PRESET, UI_BTN_NORMAL));
    r = crect_offset(&L.btn_apply, o.x, o.y);
    ui_draw_button(s, &r, "Apply",
                   ui_hot_state(&a->hot, THB_APPLY, UI_BTN_NORMAL));
    r = crect_offset(&L.btn_save, o.x, o.y);
    ui_draw_button(s, &r, "Save As...",
                   ui_hot_state(&a->hot, THB_SAVE, UI_BTN_NORMAL));
    r = crect_offset(&L.btn_open, o.x, o.y);
    ui_draw_button(s, &r, "Open...",
                   ui_hot_state(&a->hot, THB_OPEN, UI_BTN_NORMAL));

    r = crect_offset(&L.btn_preset, o.x, o.y);
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0, r.y0 - 13, a->status,
                  p->text_disabled);
}

/* ---- files ------------------------------------------------------------- */
static void th_path(char *out, cu32 outsz, const char *name)
{
    char dir[CASTALIA_MAX_PATH];
    sys_home_path(dir, (cu32)sizeof dir, "THEMES");
    ui_path_default_dir(out, outsz, dir, name);
}

/* Where themes live, created on demand so the dialog opens somewhere real. */
static const char *th_dir(void)
{
    static char dir[CASTALIA_MAX_PATH];
    sys_home_path(dir, (cu32)sizeof dir, "THEMES");
    plat_mkdir(dir);
    return dir;
}

static void th_on_save(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    ThemeApp *a = (win != NULL) ? (ThemeApp *)wm_user(win) : NULL;
    char path[CASTALIA_MAX_PATH], dir[CASTALIA_MAX_PATH];
    if (!ok || a == NULL || text == NULL || text[0] == '\0') { return; }
    sys_home_path(dir, (cu32)sizeof dir, "THEMES");
    plat_mkdir(dir);
    th_path(path, sizeof path, text);
    if (sh_theme_save(&a->t, path) == CE_OK) {
        sys_strlcpy(a->path, text, sizeof a->path);
        sys_snprintf(a->status, sizeof a->status, "Saved '%s'", text);
    } else {
        sys_snprintf(a->status, sizeof a->status, "Could not save '%s'", text);
    }
    wm_invalidate(win, NULL);
}

static void th_on_open(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    ThemeApp *a = (win != NULL) ? (ThemeApp *)wm_user(win) : NULL;
    char path[CASTALIA_MAX_PATH];
    if (!ok || a == NULL || text == NULL || text[0] == '\0') { return; }
    th_path(path, sizeof path, text);
    if (sh_theme_load(&a->t, path) == CE_OK) {
        sys_strlcpy(a->path, text, sizeof a->path);
        sys_snprintf(a->status, sizeof a->status, "Opened '%s'", a->t.name);
    } else {
        sys_snprintf(a->status, sizeof a->status, "Could not open '%s'", text);
    }
    wm_invalidate(win, NULL);
}

/* ---- input ------------------------------------------------------------- */
/* 'chan', not 'ch': everywhere else in this tree 'ch' is a character, and a
 * colour channel that answers to the same name reads like keyboard input. */
static void th_set_channel(ThemeApp *a, int chan, int v)
{
    CColor *c = th_slot(&a->t, a->slot);
    int r = GFX_R(*c), g = GFX_G(*c), b = GFX_B(*c);
    if (v < 0) { v = 0; }
    if (v > 255) { v = 255; }
    if (chan == 0) { r = v; } else if (chan == 1) { g = v; } else { b = v; }
    *c = GFX_RGB(r, g, b);
}

static cbool th_slider_hit(ThemeApp *a, const ThLayout *L, int i, int px)
{
    int w = crect_w(&L->bar[i]) - 7;
    if (w < 1) { w = 1; }
    th_set_channel(a, i, ((px - L->bar[i].x0 - 3) * 255) / w);
    return CTRUE;
}

static cbool th_click(WmWindow *win, ThemeApp *a, int px, int py)
{
    ThLayout L;
    int i;
    th_layout(win, &L);
    (void)ui_hot_press(&a->hot, th_btn_at(&L, px, py));

    if (crect_contains(&L.list, px, py)) {
        int row = (py - L.list.y0 - 2) / TH_ROW;
        if (row >= 0 && row < TH_SLOTS) { a->slot = row; }
        wm_invalidate(win, NULL);
        return CTRUE;
    }
    for (i = 0; i < 3; i++) {
        CRect grab = crect_inset(&L.bar[i], -4);
        if (crect_contains(&grab, px, py)) {
            th_slider_hit(a, &L, i, px);
            a->drag = i + 1;
            wm_invalidate(win, NULL);
            return CTRUE;
        }
    }
    if (crect_contains(&L.quick, px, py)) {
        int w = crect_w(&L.quick) / 14;
        int col = (px - L.quick.x0) / (w > 0 ? w : 1);
        int row = (py - L.quick.y0 - 14) / 14;
        if (col >= 0 && col < 14 && row >= 0 && row < 2) {
            *th_slot(&a->t, a->slot) = TH_QUICK[row * 14 + col];
            wm_invalidate(win, NULL);
        }
        return CTRUE;
    }
    if (crect_contains(&L.title_dec, px, py)) {
        if (a->t.title_height > 14) { a->t.title_height--; }
        wm_invalidate(win, NULL); return CTRUE;
    }
    if (crect_contains(&L.title_inc, px, py)) {
        if (a->t.title_height < 32) { a->t.title_height++; }
        wm_invalidate(win, NULL); return CTRUE;
    }
    if (crect_contains(&L.border_dec, px, py)) {
        if (a->t.border_width > 1) { a->t.border_width--; }
        wm_invalidate(win, NULL); return CTRUE;
    }
    if (crect_contains(&L.border_inc, px, py)) {
        if (a->t.border_width < 6) { a->t.border_width++; }
        wm_invalidate(win, NULL); return CTRUE;
    }
    if (crect_contains(&L.btn_preset, px, py)) {
        a->preset = (a->preset + 1) % 5;
        sh_theme_preset(&a->t, a->preset);
        sys_snprintf(a->status, sizeof a->status, "Loaded %s",
                     sh_theme_preset_name(a->preset));
        wm_invalidate(win, NULL);
        return CTRUE;
    }
    if (crect_contains(&L.btn_apply, px, py)) {
        sh_theme_apply_live(&a->t);
        sys_strlcpy(a->status, "Applied to the desktop", sizeof a->status);
        return CTRUE;
    }
    if (crect_contains(&L.btn_save, px, py)) {
        ui_file_dialog("Save Theme", th_dir(), "INI", CTRUE,
                       (a->path[0] != '\0') ? a->path : "MYTHEME.INI",
                       th_on_save, win);
        return CTRUE;
    }
    if (crect_contains(&L.btn_open, px, py)) {
        ui_file_dialog("Open Theme", th_dir(), "INI", CFALSE,
                       (a->path[0] != '\0') ? a->path : "MYTHEME.INI",
                       th_on_open, win);
        return CTRUE;
    }
    return CFALSE;
}

static cbool theme_proc(WmWindow *win, WmMessage msg, long a, long b,
                        void *param)
{
    ThemeApp *app = (ThemeApp *)wm_user(win);
    switch (msg) {
    case WM_MSG_PAINT:       th_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: return th_click(win, app, (int)a, (int)b);
    case WM_MSG_MOUSEMOVE: {
        ThLayout L;
        if (app == NULL) { return CFALSE; }
        if (app->drag > 0) {
            th_layout(win, &L);
            th_slider_hit(app, &L, app->drag - 1, (int)a);
            wm_invalidate(win, NULL);
            return CTRUE;
        }
        th_layout(win, &L);
        if (ui_hot_move(&app->hot, th_btn_at(&L, (int)a, (int)b))) {
            CRect br[THB_COUNT];
            int k;
            for (k = 0; k < THB_COUNT; k++) { br[k] = th_btn_rect(&L, k); }
            ui_hot_repaint(win, &app->hot, br, THB_COUNT);
        }
        return CFALSE;
    }
    case WM_MSG_LBUTTONUP:
        if (app != NULL) {
            app->drag = 0;
            if (ui_hot_release(&app->hot)) {
                ThLayout L;
                CRect br[THB_COUNT];
                int k;
                th_layout(win, &L);
                for (k = 0; k < THB_COUNT; k++) { br[k] = th_btn_rect(&L, k); }
                ui_hot_repaint(win, &app->hot, br, THB_COUNT);
            }
        }
        return CTRUE;
    case WM_MSG_MOUSELEAVE:
        if (app != NULL) {
            cbool redraw = ui_hot_release(&app->hot);
            ThLayout L;
            CRect br[THB_COUNT];
            int k;
            if (ui_hot_move(&app->hot, -1)) { redraw = CTRUE; }
            th_layout(win, &L);
            for (k = 0; k < THB_COUNT; k++) { br[k] = th_btn_rect(&L, k); }
            if (redraw) { ui_hot_repaint(win, &app->hot, br, THB_COUNT); }
        }
        return CTRUE;
    case WM_MSG_KEYDOWN:
        if (app == NULL) { return CFALSE; }
        if ((int)a == PLAT_KEY_UP && app->slot > 0) { app->slot--; }
        else if ((int)a == PLAT_KEY_DOWN && app->slot < TH_SLOTS - 1) { app->slot++; }
        else { return CFALSE; }
        wm_invalidate(win, NULL);
        return CTRUE;
    case WM_MSG_DESTROY:
        if (app != NULL) { sys_free(app, (cu32)sizeof(ThemeApp)); }
        return CTRUE;
    default:
        (void)b;
        return CFALSE;
    }
}

void app_theme_open(void)
{
    ThemeApp *a;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int fw = 520, fh = 372, fx, fy;

    a = (ThemeApp *)sys_calloc(1, (cu32)sizeof(ThemeApp));
    if (a == NULL) { SYS_LOGE("app", "theme: OOM"); return; }
    /* Start from what is on screen right now, so the first edit is a tweak of
     * the current look rather than a jump to somewhere else. */
    a->t = *sh_theme_active();
    a->slot = 0;
    a->preset = 0;
    sys_strlcpy(a->status, "Pick a color, then Apply", sizeof a->status);

    plat_video_info(&vi);
    if (fw > vi.width)  { fw = vi.width; }
    if (fh > vi.height) { fh = vi.height; }
    fx = (vi.width - fw) / 2;
    fy = (vi.height - fh) / 2 - 8;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, fw, fh);

    w = wm_create("Theme Editor", &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE |
                  WM_STYLE_BORDER, theme_proc, a);
    if (w == NULL) { sys_free(a, (cu32)sizeof(ThemeApp)); return; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened Theme Editor (%s)", a->t.name);
}
