/*
 * app_welcome.c - "Welcome to CastaliaOS" first-run tour.
 *
 * The friendly landing window the desktop opens on an interactive first boot
 * (like the classic out-of-box welcome screens of the era): a glossy banner,
 * a short list of things to try -- each row is a live link that opens the real
 * app -- and a "show at startup" checkbox persisted to CASTALIA.INI. Reopenable
 * any time from the launcher.
 */
#include "apps.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/settings.h"
#include "castalia/sys.h"
#include "castalia/castalia.h"

/* The system mark, drawn by the shell (sh_logo.c) -- the same one the
 * Start button and the About window use. */
void sh_logo_draw_plated(GfxSurface *s, int x, int y, int size);

#define WL_BANNER_H 62
#define WL_ROW_H    31
#define WL_ROWS     8
#define WL_FOOT_H   40

typedef struct {
    int   hot;      /* row under the pointer, or -1 */
    UiHot hot_btn;  /* the Close button, under the pointer or held */
} Welcome;

/* Row content: bold link label + gray description. */
static const char *const WL_LABEL[WL_ROWS] = {
    "Browse your files",
    "Write a letter",
    "Add up the numbers",
    "Draw something",
    "Play some music",
    "Make it yours",
    "Take a card break",
    "Learn the basics"
};
static const char *const WL_DESC[WL_ROWS] = {
    "The File Manager, with thumbnails of your pictures",
    "CastaliaWrite -- bold, italics, a ruler and a page",
    "CastaliaSheet -- formulas, three sheets, CSV files",
    "CastaliaPaint -- eleven tools and three undos",
    "The Media Player scans MEDIA for WAV songs",
    "The Theme Editor: every colour, previewed live",
    "Solitaire -- the classic Klondike patience",
    "Help covers the apps, the keys and where files live"
};

/* Client-relative rect of tour row 'i'. */
static CRect wl_row(int cw, int i)
{
    return crect_make(14, WL_BANNER_H + 12 + i * WL_ROW_H, cw - 28, WL_ROW_H - 4);
}

static CRect wl_check(int ch)
{
    return crect_make(14, ch - WL_FOOT_H + 10, 220, 18);
}

static CRect wl_close(int cw, int ch)
{
    return crect_make(cw - 84, ch - WL_FOOT_H + 8, 70, 22);
}

/* Row icons, ~18x18, drawn with primitives -- one per tour row. */
static void wl_icon(GfxSurface *s, int x, int y, int kind)
{
    const UiPalette *p = ui_palette();
    CColor steel = GFX_RGB(0x6A, 0x74, 0x84);
    switch (kind) {
    case 0: { /* folder */
        CRect tab  = crect_make(x + 1, y + 3, 7, 3);
        CRect body = crect_make(x, y + 5, 17, 10);
        gfx_fill_rect(s, &tab, GFX_RGB(0xF0, 0xC2, 0x50));
        gfx_vgradient(s, &body, GFX_RGB(0xFF, 0xE7, 0x9B), GFX_RGB(0xF0, 0xC2, 0x50));
        gfx_frame_rect(s, &body, GFX_RGB(0xB8, 0x8A, 0x22));
        break;
    }
    case 1: { /* a page with lines of writing */
        CRect page = crect_make(x + 2, y + 1, 13, 16);
        int i;
        gfx_fill_rect(s, &page, GFX_RGB(0xFF, 0xFF, 0xFF));
        gfx_frame_rect(s, &page, steel);
        for (i = 0; i < 4; i++) {
            gfx_hline(s, x + 4, y + 4 + i * 3, (i == 3) ? 5 : 9,
                      GFX_RGB(0x38, 0x50, 0x80));
        }
        break;
    }
    case 2: { /* a grid of cells with one filled */
        CRect grid = crect_make(x + 1, y + 2, 15, 14);
        CRect cell = crect_make(x + 2, y + 3, 4, 3);
        int i;
        gfx_fill_rect(s, &grid, GFX_RGB(0xFF, 0xFF, 0xFF));
        gfx_fill_rect(s, &cell, GFX_RGB(0x9E, 0xC8, 0xA0));
        gfx_frame_rect(s, &grid, steel);
        for (i = 1; i < 4; i++) {
            gfx_hline(s, x + 1, y + 2 + i * 4, 15, GFX_RGB(0xB0, 0xB8, 0xC4));
        }
        for (i = 1; i < 3; i++) {
            gfx_vline(s, x + 1 + i * 5, y + 2, 14, GFX_RGB(0xB0, 0xB8, 0xC4));
        }
        break;
    }
    case 3: { /* a pencil over a colour swatch */
        CRect sw = crect_make(x + 1, y + 11, 6, 6);
        gfx_fill_rect(s, &sw, GFX_RGB(0xC8, 0x30, 0x30));
        gfx_frame_rect(s, &sw, steel);
        gfx_line(s, x + 5, y + 12, x + 15, y + 2, GFX_RGB(0x30, 0x30, 0x38));
        gfx_line(s, x + 6, y + 13, x + 16, y + 3, GFX_RGB(0x30, 0x30, 0x38));
        gfx_put_pixel(s, x + 15, y + 1, GFX_RGB(0xC0, 0x80, 0x40));
        break;
    }
    case 4: { /* twin music note */
        CColor g = GFX_RGB(0x18, 0x8A, 0x34);
        gfx_fill_circle(s, x + 5, y + 13, 3, g);
        gfx_fill_circle(s, x + 13, y + 11, 3, g);
        gfx_vline(s, x + 7, y + 3, 10, g);
        gfx_vline(s, x + 15, y + 1, 10, g);
        gfx_hline(s, x + 7, y + 3, 9, g);
        gfx_hline(s, x + 7, y + 4, 9, g);
        break;
    }
    case 5: { /* a painter's palette of colours */
        static const CColor SW[4] = { 0x2A5BC8UL, 0xC03030UL,
                                      0x1E5A38UL, 0xC8A020UL };
        int i;
        gfx_fill_circle(s, x + 9, y + 9, 8, GFX_RGB(0xE8, 0xE2, 0xD4));
        gfx_fill_circle(s, x + 12, y + 12, 2, p->face);
        for (i = 0; i < 4; i++) {
            gfx_fill_circle(s, x + 5 + (i % 2) * 7, y + 5 + (i / 2) * 6, 2,
                            SW[i]);
        }
        break;
    }
    case 6: { /* playing card with a red heart pip */
        CRect card = crect_make(x + 2, y + 1, 12, 16);
        gfx_fill_round_rect(s, &card, 2, GFX_RGB(0xFF, 0xFF, 0xFF));
        gfx_frame_rect(s, &card, steel);
        gfx_fill_circle(s, x + 6, y + 8, 2, GFX_RGB(0xC2, 0x20, 0x20));
        gfx_fill_circle(s, x + 10, y + 8, 2, GFX_RGB(0xC2, 0x20, 0x20));
        gfx_fill_circle(s, x + 8, y + 11, 2, GFX_RGB(0xC2, 0x20, 0x20));
        break;
    }
    default: /* question mark */
        gfx_draw_text(s, GFX_FONT_BOLD, x + 6, y + 4, "?", GFX_RGB(0x1B, 0x3F, 0x9A));
        break;
    }
}

/* Launch the app behind row 'i'. */
static void wl_launch(int i)
{
    switch (i) {
    case 0: app_fileman_open(); break;
    case 1: app_write_open(); break;
    case 2: app_sheet_open(); break;
    case 3: app_paint_open(); break;
    case 4: app_media_open(); break;
    case 5: app_theme_open(); break;
    case 6: app_solitaire_open(); break;
    default: app_help_open(); break;
    }
}

/* ---- paint ------------------------------------------------------------ */
static void wl_paint(WmWindow *win, GfxSurface *s)
{
    Welcome *wl = (Welcome *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c), i;
    CRect r;
    if (wl == NULL) { return; }

    /* Body. */
    r = crect_make(o.x, o.y, cw, ch);
    gfx_fill_rect(s, &r, p->face);

    /* Glossy banner. */
    r = crect_make(o.x, o.y, cw, WL_BANNER_H);
    gfx_vgradient3(s, &r, GFX_RGB(0x2E, 0x5C, 0xC8),
                   GFX_RGB(0x4A, 0x7C, 0xE2), GFX_RGB(0x10, 0x2A, 0x74), 320);
    gfx_hline(s, o.x, o.y + WL_BANNER_H - 1, cw, GFX_RGB(0xD8, 0xA8, 0x3C));
    /* The system's own mark, the same drawing as the Start button. */
    sh_logo_draw_plated(s, o.x + 12, o.y + 12, 38);
    gfx_draw_text_shadow(s, GFX_FONT_BOLD, o.x + 60, o.y + 14,
                         "Welcome to CastaliaOS 98 PE",
                         GFX_RGB(0xFF, 0xFF, 0xFF), GFX_RGB(0x0A, 0x1E, 0x4E));
    gfx_draw_text(s, GFX_FONT_SYSTEM, o.x + 60, o.y + 30,
                  "An original retro desktop. Here are a few things to try:",
                  GFX_RGB(0xD6, 0xE2, 0xFA));
    gfx_draw_text(s, GFX_FONT_SYSTEM, o.x + 60, o.y + 44,
                  "Version " CASTALIA_VER_STRING "  -  " CASTALIA_VER_STAGE,
                  GFX_RGB(0xA8, 0xBC, 0xE8));

    /* Tour rows. */
    for (i = 0; i < WL_ROWS; i++) {
        CRect cr2 = wl_row(cw, i);
        CRect row = crect_offset(&cr2, o.x, o.y);
        if (i == wl->hot) {
            gfx_fill_round_rect(s, &row, 4, gfx_tint(p->face, p->accent, 40));
            gfx_frame_rect(s, &row, gfx_tint(p->face, p->accent, 120));
        }
        wl_icon(s, row.x0 + 6, row.y0 + 7, i);
        gfx_draw_text(s, GFX_FONT_BOLD, row.x0 + 34, row.y0 + 6, WL_LABEL[i],
                      (i == wl->hot) ? p->accent : p->text);
        gfx_draw_text(s, GFX_FONT_SYSTEM, row.x0 + 34, row.y0 + 19, WL_DESC[i],
                      ui_text_dim());
    }

    /* Footer: startup checkbox + Close. */
    r = crect_make(o.x + 8, o.y + ch - WL_FOOT_H, cw - 16, 1);
    gfx_bevel(s, &r, GFX_BEVEL_ETCHED, p->light, p->dark, GFX_NO_FILL);
    {
        CRect ck = wl_check(ch);
        CRect bt = wl_close(cw, ch);
        ck = crect_offset(&ck, o.x, o.y);
        bt = crect_offset(&bt, o.x, o.y);
        ui_draw_check(s, &ck, "Show this screen at startup",
                      settings_get()->welcome_startup, UI_BTN_NORMAL);
        ui_draw_button(s, &bt, "Close",
                       ui_hot_state(&wl->hot_btn, 0, UI_BTN_NORMAL));
    }
}

/* ---- input ------------------------------------------------------------ */
static void wl_click(WmWindow *win, Welcome *wl, int x, int y)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c), i;
    CRect ck = wl_check(ch);
    CRect bt = wl_close(cw, ch);
    if (crect_contains(&bt, x, y)) {
        (void)ui_hot_press(&wl->hot_btn, 0);
        wm_destroy(win);
        return;
    }
    if (crect_contains(&ck, x, y)) {
        settings_get()->welcome_startup = !settings_get()->welcome_startup;
        settings_save();
        wm_invalidate(win, NULL);
        return;
    }
    for (i = 0; i < WL_ROWS; i++) {
        CRect row = wl_row(cw, i);
        if (crect_contains(&row, x, y)) { wl_launch(i); return; }
    }
    CASTALIA_UNUSED(wl);
}

static void wl_motion(WmWindow *win, Welcome *wl, int x, int y)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), i, hot = -1;
    for (i = 0; i < WL_ROWS; i++) {
        CRect row = wl_row(cw, i);
        if (crect_contains(&row, x, y)) { hot = i; break; }
    }
    if (hot != wl->hot) { wl->hot = hot; wm_invalidate(win, NULL); }
    {
        CRect bt = wl_close(cw, crect_h(&c));
        if (ui_hot_move(&wl->hot_btn,
                        crect_contains(&bt, x, y) ? 0 : -1)) {
            ui_hot_repaint(win, &wl->hot_btn, &bt, 1);
        }
    }
}

static cbool welcome_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    Welcome *wl = (Welcome *)wm_user(win);
    switch (msg) {
    case WM_MSG_PAINT:       wl_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: if (wl) { wl_click(win, wl, (int)a, (int)b); } return CTRUE;
    case WM_MSG_MOUSEMOVE:   if (wl) { wl_motion(win, wl, (int)a, (int)b); } return CTRUE;
    case WM_MSG_LBUTTONUP:
    case WM_MSG_MOUSELEAVE:
        if (wl != NULL) {
            cbool redraw = ui_hot_release(&wl->hot_btn);
            cbool rows = CFALSE;
            if (msg == WM_MSG_MOUSELEAVE) {
                if (ui_hot_move(&wl->hot_btn, -1)) { redraw = CTRUE; }
                if (wl->hot != -1) { wl->hot = -1; rows = CTRUE; }
            }
            /* A hovered ROW is a link highlight down the middle of the
             * window; only the button has a rectangle worth narrowing to. */
            if (rows) { wm_invalidate(win, NULL); }
            else if (redraw) {
                CRect c2 = wm_client_rect(win);
                CRect bt = wl_close(crect_w(&c2), crect_h(&c2));
                ui_hot_repaint(win, &wl->hot_btn, &bt, 1);
            }
        }
        return CTRUE;
    case WM_MSG_KEYDOWN:
        if ((int)a == PLAT_KEY_ESC || (int)a == PLAT_KEY_ENTER) {
            wm_destroy(win);
            return CTRUE;
        }
        return CFALSE;
    case WM_MSG_DESTROY:
        if (wl != NULL) { sys_free(wl, (cu32)sizeof(Welcome)); }
        return CTRUE;
    default: return CFALSE;
    }
}

void app_welcome_open(void)
{
    Welcome *wl;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    /* Client-height budget, plus the frame overhead (title bar + borders)
     * since wm_create() takes the whole frame rect. */
    int cw = 440, ch = WL_BANNER_H + 12 + WL_ROWS * WL_ROW_H + WL_FOOT_H + 32;
    int fx, fy;

    wl = (Welcome *)sys_calloc(1, (cu32)sizeof(Welcome));
    if (wl == NULL) { SYS_LOGE("app", "welcome: OOM"); return; }
    wl->hot = -1;

    plat_video_info(&vi);
    fx = (vi.width - cw) / 2;
    fy = (vi.height - ch) / 2 - 16;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, cw, ch);

    w = wm_create("Welcome", &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE |
                  WM_STYLE_BORDER,
                  welcome_proc, wl);
    if (w == NULL) { sys_free(wl, (cu32)sizeof(Welcome)); return; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened Welcome tour");
}
