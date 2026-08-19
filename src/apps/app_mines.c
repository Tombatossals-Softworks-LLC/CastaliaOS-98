/*
 * app_mines.c - Mines: the classic grid-of-hidden-mines puzzle, Castalia
 * style. 9x9 board, 10 mines. Left-click (or Enter/Space) reveals, right-click
 * (or F) plants a flag, N / the New button restarts. Fully keyboard-playable
 * (arrows move the cell cursor), like every Castalia app.
 *
 * Board logic lives in mines_core.c (pure, host-tested); this file is only
 * the window: painting, hit-testing, and input.
 */
#include "apps.h"
#include "mines_core.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/snd.h"
#include "castalia/sys.h"

#define MI_W       9
#define MI_H       9
#define MI_MINES   10
#define MI_CELL    18
#define MI_MARGIN  8
#define MI_HEAD_H  24
#define MI_GAP     6

typedef struct {
    MinesGame game;
    int   kx, ky;        /* keyboard cell cursor */
    cu32  t0, t1;        /* first-reveal / game-over ticks (0 = not yet) */
    UiHot hot;           /* the New button, under the pointer or held */
} Mines;

/* Classic number colors, 1..8 (index 0 unused). */
static const CColor MI_NUMCOL[9] = {
    0,
    GFX_RGB(0x00, 0x00, 0xC8),   /* 1 blue      */
    GFX_RGB(0x00, 0x80, 0x00),   /* 2 green     */
    GFX_RGB(0xC8, 0x00, 0x00),   /* 3 red       */
    GFX_RGB(0x00, 0x00, 0x70),   /* 4 navy      */
    GFX_RGB(0x70, 0x00, 0x00),   /* 5 maroon    */
    GFX_RGB(0x00, 0x70, 0x70),   /* 6 teal      */
    GFX_RGB(0x20, 0x20, 0x20),   /* 7 near-black*/
    GFX_RGB(0x60, 0x60, 0x60)    /* 8 gray      */
};

/* ---- layout ----------------------------------------------------------- */
static CRect mi_grid_rect(void)
{
    return crect_make(MI_MARGIN, MI_MARGIN + MI_HEAD_H + MI_GAP,
                      MI_W * MI_CELL, MI_H * MI_CELL);
}
static CRect mi_cell_rect(int x, int y)
{
    CRect g = mi_grid_rect();
    return crect_make(g.x0 + x * MI_CELL, g.y0 + y * MI_CELL, MI_CELL, MI_CELL);
}
static CRect mi_counter_rect(void)
{
    return crect_make(MI_MARGIN, MI_MARGIN, 40, MI_HEAD_H);
}
static CRect mi_new_rect(void)
{
    CRect g = mi_grid_rect();
    return crect_make(g.x1 - 48, MI_MARGIN, 48, MI_HEAD_H);
}
static CRect mi_time_rect(void)
{
    CRect c = mi_counter_rect(), n = mi_new_rect();
    return crect_make(c.x1 + 6, MI_MARGIN, n.x0 - c.x1 - 12, MI_HEAD_H);
}

static void mi_new_game(Mines *m)
{
    mines_init(&m->game, MI_W, MI_H, MI_MINES, plat_ticks_ms() | 1u);
    m->t0 = 0;
    m->t1 = 0;
}

/* Elapsed whole seconds for the header clock (frozen once the game ends). */
static long mi_elapsed_s(const Mines *m)
{
    cu32 end;
    if (m->t0 == 0) { return 0; }
    end = (m->t1 != 0) ? m->t1 : plat_ticks_ms();
    return (long)((end - m->t0) / 1000u);
}

/* ---- tiny glyphs (original pixel art, primitives only) ---------------- */
static void mi_draw_flag(GfxSurface *s, const CRect *c)
{
    int x = (c->x0 + c->x1) / 2 - 1, y0 = c->y0 + 4;
    CRect pennant = crect_make(x - 4, y0, 5, 4);
    gfx_fill_rect(s, &pennant, GFX_RGB(0xC8, 0x00, 0x00));
    gfx_vline(s, x, y0, 8, GFX_RGB(0x20, 0x20, 0x20));
    gfx_hline(s, x - 2, y0 + 8, 6, GFX_RGB(0x20, 0x20, 0x20));
}

static void mi_draw_mine(GfxSurface *s, const CRect *c)
{
    int cx = (c->x0 + c->x1) / 2, cy = (c->y0 + c->y1) / 2;
    CColor k = GFX_RGB(0x18, 0x18, 0x18);
    CRect body = crect_make(cx - 3, cy - 3, 7, 7);
    gfx_fill_rect(s, &body, k);
    gfx_hline(s, cx - 5, cy, 11, k);          /* spikes */
    gfx_vline(s, cx, cy - 5, 11, k);
    gfx_line(s, cx - 4, cy - 4, cx + 4, cy + 4, k);
    gfx_line(s, cx - 4, cy + 4, cx + 4, cy - 4, k);
    gfx_put_pixel(s, cx - 1, cy - 1, GFX_RGB(0xE8, 0xE8, 0xE8));   /* glint */
}

/* ---- painting ---------------------------------------------------------- */
static void mi_paint_cell(GfxSurface *s, const Mines *m, int x, int y,
                          const CPoint *o)
{
    const UiPalette *p = ui_palette();
    const MinesGame *g = &m->game;
    CRect c = mi_cell_rect(x, y);
    c = crect_offset(&c, o->x, o->y);

    if (!mines_is_open(g, x, y)) {
        gfx_bevel(s, &c, GFX_BEVEL_RAISED, p->light, p->dark, p->face);
        if (mines_is_flag(g, x, y)) { mi_draw_flag(s, &c); }
    } else {
        cbool burst = (g->status == MINES_LOST &&
                       g->burst == y * g->w + x) ? CTRUE : CFALSE;
        CColor face = burst ? GFX_RGB(0xD0, 0x40, 0x30)
                            : gfx_tint(p->face, 0xFFFFFF, 40);
        gfx_bevel(s, &c, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, face);
        if (mines_is_mine(g, x, y)) {
            mi_draw_mine(s, &c);
        } else {
            int n = mines_adjacent(g, x, y);
            if (n > 0) {
                char d[2];
                d[0] = (char)('0' + n); d[1] = '\0';
                gfx_draw_text_rect(s, GFX_FONT_BOLD, &c, d, MI_NUMCOL[n],
                                   GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
            }
        }
    }
    /* Keyboard cursor: an accent frame just inside the cell. */
    if (x == m->kx && y == m->ky && g->status == MINES_PLAYING) {
        CRect f = crect_make(c.x0 + 2, c.y0 + 2,
                             crect_w(&c) - 4, crect_h(&c) - 4);
        gfx_frame_rect(s, &f, p->accent);
    }
}

static void mi_paint(WmWindow *win, GfxSurface *s)
{
    Mines *m = (Mines *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    int x, y;
    char txt[24];
    if (m == NULL) { return; }

    /* Header: mines-left counter, elapsed time, status, New button. */
    {
        CRect r = mi_counter_rect();
        r = crect_offset(&r, o.x, o.y);
        gfx_bevel(s, &r, GFX_BEVEL_SUNKEN, p->light, p->dark,
                  GFX_RGB(0x18, 0x10, 0x10));
        sys_snprintf(txt, sizeof(txt), "%03d", mines_flags_left(&m->game));
        gfx_draw_text_rect(s, GFX_FONT_BOLD, &r, txt, GFX_RGB(0xE8, 0x30, 0x20),
                           GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
    }
    {
        CRect r = mi_time_rect();
        const char *st = (m->game.status == MINES_WON)  ? "Cleared!" :
                         (m->game.status == MINES_LOST) ? "Boom!"    : "";
        r = crect_offset(&r, o.x, o.y);
        if (st[0] != '\0') {
            sys_snprintf(txt, sizeof(txt), "%s %lds", st, mi_elapsed_s(m));
        } else {
            sys_snprintf(txt, sizeof(txt), "%lds", mi_elapsed_s(m));
        }
        gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &r, txt, p->text,
                           GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
    }
    {
        CRect r = mi_new_rect();
        r = crect_offset(&r, o.x, o.y);
        ui_draw_button(s, &r, "New", ui_hot_state(&m->hot, 0, UI_BTN_NORMAL));
    }

    for (y = 0; y < MI_H; y++) {
        for (x = 0; x < MI_W; x++) { mi_paint_cell(s, m, x, y, &o); }
    }
}

/* ---- input ------------------------------------------------------------- */
static void mi_after_move(Mines *m, WmWindow *win)
{
    if (m->game.status != MINES_PLAYING && m->t1 == 0) {
        m->t1 = plat_ticks_ms();
        if (m->game.status == MINES_LOST) { snd_error(); }
        else                              { snd_open(); }
    }
    wm_invalidate(win, NULL);
}

static void mi_reveal(Mines *m, WmWindow *win, int x, int y)
{
    if (m->game.status != MINES_PLAYING) { return; }
    if (m->t0 == 0) { m->t0 = plat_ticks_ms(); }
    mines_reveal(&m->game, x, y);
    mi_after_move(m, win);
}

static cbool mi_cell_at(int px, int py, int *cx, int *cy)
{
    CRect g = mi_grid_rect();
    if (px < g.x0 || py < g.y0 || px >= g.x1 || py >= g.y1) { return CFALSE; }
    *cx = (px - g.x0) / MI_CELL;
    *cy = (py - g.y0) / MI_CELL;
    return CTRUE;
}

static void mi_on_click(WmWindow *win, int px, int py, cbool flagging)
{
    Mines *m = (Mines *)wm_user(win);
    int cx, cy;
    CRect nr = mi_new_rect();
    if (m == NULL) { return; }
    if (!flagging && crect_contains(&nr, px, py)) {
        (void)ui_hot_press(&m->hot, 0);
        snd_click();
        mi_new_game(m);
        wm_invalidate(win, NULL);
        return;
    }
    if (mi_cell_at(px, py, &cx, &cy)) {
        m->kx = cx; m->ky = cy;
        if (flagging) {
            mines_toggle_flag(&m->game, cx, cy);
            mi_after_move(m, win);
        } else {
            mi_reveal(m, win, cx, cy);
        }
    }
}

static void mi_on_key(WmWindow *win, int key, int ch)
{
    Mines *m = (Mines *)wm_user(win);
    if (m == NULL) { return; }
    if (key == PLAT_KEY_LEFT)  { if (m->kx > 0)        { m->kx--; } }
    else if (key == PLAT_KEY_RIGHT) { if (m->kx < MI_W - 1) { m->kx++; } }
    else if (key == PLAT_KEY_UP)    { if (m->ky > 0)        { m->ky--; } }
    else if (key == PLAT_KEY_DOWN)  { if (m->ky < MI_H - 1) { m->ky++; } }
    else if (key == PLAT_KEY_ENTER || ch == ' ') {
        mi_reveal(m, win, m->kx, m->ky);
        return;
    }
    else if (ch == 'f' || ch == 'F') {
        mines_toggle_flag(&m->game, m->kx, m->ky);
        mi_after_move(m, win);
        return;
    }
    else if (ch == 'n' || ch == 'N' || key == PLAT_KEY_F2) {
        mi_new_game(m);
    }
    else { return; }
    wm_invalidate(win, NULL);
}

static cbool mi_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    switch (msg) {
    case WM_MSG_PAINT:       mi_paint(win, (GfxSurface *)param);       return CTRUE;
    case WM_MSG_LBUTTONDOWN: mi_on_click(win, (int)a, (int)b, CFALSE); return CTRUE;
    case WM_MSG_RBUTTONDOWN: mi_on_click(win, (int)a, (int)b, CTRUE);  return CTRUE;
    case WM_MSG_MOUSEMOVE: {
        Mines *m = (Mines *)wm_user(win);
        CRect nr = mi_new_rect();
        if (m == NULL) { return CFALSE; }
        if (ui_hot_move(&m->hot,
                        crect_contains(&nr, (int)a, (int)b) ? 0 : -1)) {
            ui_hot_repaint(win, &m->hot, &nr, 1);
        }
        return CTRUE;
    }
    case WM_MSG_LBUTTONUP:
    case WM_MSG_MOUSELEAVE: {
        Mines *m = (Mines *)wm_user(win);
        cbool redraw;
        if (m == NULL) { return CFALSE; }
        redraw = ui_hot_release(&m->hot);
        if (msg == WM_MSG_MOUSELEAVE && ui_hot_move(&m->hot, -1)) {
            redraw = CTRUE;
        }
        if (redraw) {
            CRect nr2 = mi_new_rect();
            ui_hot_repaint(win, &m->hot, &nr2, 1);
        }
        return CTRUE;
    }
    case WM_MSG_KEYDOWN:     mi_on_key(win, (int)a, (int)b);           return CTRUE;
    case WM_MSG_DESTROY: {
        Mines *m = (Mines *)wm_user(win);
        if (m != NULL) { sys_free(m, (cu32)sizeof(Mines)); }
        return CTRUE;
    }
    default: return CFALSE;
    }
}

void app_mines_open(void)
{
    Mines *m;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int fw = MI_W * MI_CELL + MI_MARGIN * 2 + 8;
    int fh = MI_H * MI_CELL + MI_HEAD_H + MI_GAP + MI_MARGIN * 2 + 26;
    int fx, fy;

    m = (Mines *)sys_calloc(1, (cu32)sizeof(Mines));
    if (m == NULL) { SYS_LOGE("app", "mines: OOM"); return; }
    mi_new_game(m);
    m->kx = MI_W / 2;
    m->ky = MI_H / 2;

    plat_video_info(&vi);
    fx = (vi.width - fw) / 2 + 60;
    fy = (vi.height - fh) / 2 - 30;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, fw, fh);
    w = wm_create("Mines", &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE |
                  WM_STYLE_BORDER,
                  mi_proc, m);
    if (w == NULL) { sys_free(m, (cu32)sizeof(Mines)); return; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
}
