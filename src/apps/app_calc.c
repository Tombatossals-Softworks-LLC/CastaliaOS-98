/*
 * app_calc.c - CastaliaOS Calculator: the window.
 *
 * A classic desk calculator: a display, a 5x6 button grid, a memory register
 * and full keyboard support (digits, + - * /, Enter or '=' for equals, Esc or
 * 'c' to clear, Backspace, '.').
 *
 * The machine itself is calc_core.c and knows nothing about windows, which is
 * what lets tests/test_calc.c press the sequences that go wrong -- an
 * operator twice, equals with nothing pending, a second decimal point, a
 * divide by zero followed by more typing. This file is the skin: geometry,
 * painting, and turning a click or a keypress into a button label.
 */
#include "apps.h"
#include "calc_core.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#include <stdlib.h>   /* atof */
#include <string.h>

/* The grid itself is calc_core.c -- labels and spans are pure layout and
 * are checked there, without a window. */
#define CALC_COLS CALC_KEY_COLS
#define CALC_ROWS CALC_KEY_ROWS
#define CALC_DISP_H 30
#define CALC_MARGIN 8
#define CALC_GAP 4

/*
 * The machine plus the pointer state of its keypad. Calc itself is the pure
 * core -- tests/test_calc.c links it with no window anywhere -- so where the
 * pointer is lives out here with the skin.
 */
typedef struct {
    Calc  c;
    UiHot hot;
} CalcWin;

/* ---- layout ---------------------------------------------------------- */
static CRect calc_disp_rect(int cw)
{
    return crect_make(CALC_MARGIN, CALC_MARGIN, cw - CALC_MARGIN * 2, CALC_DISP_H);
}
static CRect calc_btn_rect(int cw, int ch, int row, int col)
{
    int gx0 = CALC_MARGIN;
    int gy0 = CALC_MARGIN + CALC_DISP_H + CALC_GAP;
    int gw = cw - CALC_MARGIN * 2;
    int gh = ch - gy0 - CALC_MARGIN;
    int bw = (gw - (CALC_COLS - 1) * CALC_GAP) / CALC_COLS;
    int bh = (gh - (CALC_ROWS - 1) * CALC_GAP) / CALC_ROWS;
    CRect r;
    r.x0 = gx0 + col * (bw + CALC_GAP);
    r.y0 = gy0 + row * (bh + CALC_GAP);
    r.x1 = r.x0 + bw;
    r.y1 = r.y0 + bh;
    return r;
}

/*
 * The rectangle a key occupies: its own cell, extended over the cells its span
 * covers. Which cells those are is calc_key_span() -- pure layout, checked in
 * tests/test_calc.c without a window.
 *
 * Painting used to walk CELLS and draw a complete bevelled button in each, so
 * the grid's deliberate double-height '+', triple-height '=' and double-width
 * '0' came out as two, three and two separate buttons wearing the same label,
 * each in its own frame with a gap down the middle. It looked like the layout
 * table had been filled in wrong.
 *
 * Pressing any cell of a key already did the same thing, so only the drawing
 * was ever wrong: hit-testing stays per-cell below.
 */
static CRect calc_key_rect(int cw, int ch, int row, int col, int rows, int cols)
{
    CRect a = calc_btn_rect(cw, ch, row, col);
    CRect b = calc_btn_rect(cw, ch, row + rows - 1, col + cols - 1);
    a.x1 = b.x1;
    a.y1 = b.y1;
    return a;
}

/*
 * The key under (px,py), as its origin cell's linear index, or -1.
 *
 * Against the rectangle each key is DRAWN in, spans included. Hit-testing per
 * cell while drawing per key means the pointer can be over the tall '+' and
 * over nothing at the same time -- the highlight would blink on and off down
 * its length, and the pointer would be inside the button the whole time.
 */
static int calc_key_at(int cw, int ch, int px, int py)
{
    int row, col;
    for (row = 0; row < CALC_ROWS; row++) {
        for (col = 0; col < CALC_COLS; col++) {
            int rs, cs;
            CRect br;
            if (calc_key_label(row, col)[0] == '\0') { continue; }
            if (!calc_key_span(row, col, &rs, &cs)) { continue; }
            br = calc_key_rect(cw, ch, row, col, rs, cs);
            if (crect_contains(&br, px, py)) { return row * CALC_COLS + col; }
        }
    }
    return -1;
}

/* ---- painting / input ------------------------------------------------ */
static void calc_paint(WmWindow *win, GfxSurface *s)
{
    CalcWin *cw_ = (CalcWin *)wm_user(win);
    Calc *c = (cw_ != NULL) ? &cw_->c : (Calc *)0;
    const UiPalette *p = ui_palette();
    CRect client = wm_client_rect(win);
    CPoint o = wm_client_origin(win);
    int cw = crect_w(&client), ch = crect_h(&client);
    int row, col;
    CRect dr;
    if (c == NULL) { return; }

    dr = calc_disp_rect(cw);
    dr = crect_offset(&dr, o.x, o.y);
    gfx_bevel(s, &dr, GFX_BEVEL_SUNKEN, p->light, p->dark, GFX_RGB(0xE8, 0xF0, 0xE0));
    {
        CRect tr = dr; tr.x1 -= 6;
        gfx_draw_text_rect(s, GFX_FONT_BOLD, &tr, c->disp, GFX_RGB(0x10,0x20,0x10),
                           GFX_ALIGN_RIGHT | GFX_ALIGN_VCENTER);
    }
    for (row = 0; row < CALC_ROWS; row++) {
        for (col = 0; col < CALC_COLS; col++) {
            const char *label = calc_key_label(row, col);
            CRect br;
            int rs, cs;
            if (label[0] == '\0') { continue; }
            /* A continuation cell is not a key: its run was drawn whole by the
             * cell it started from. */
            if (!calc_key_span(row, col, &rs, &cs)) { continue; }
            br = calc_key_rect(cw, ch, row, col, rs, cs);
            br = crect_offset(&br, o.x, o.y);
            ui_draw_button(s, &br, label,
                           ui_hot_state(&cw_->hot, row * CALC_COLS + col,
                                        UI_BTN_NORMAL));
        }
    }
}

/*
 * Every key's rectangle, in the order calc_key_at numbers them, so a hover
 * transition can repaint the two keys it changed instead of the window. The
 * keypad is a grid with spans -- a key two columns wide is one button, not
 * two -- so the rectangles come from the same calc_key_span/calc_key_rect
 * pair the painter uses rather than from a second guess at the geometry.
 * Slots with no key are left empty, and ui_hot_repaint skips an empty one
 * because it only ever looks at the two indices UiHot is holding.
 */
static void calc_hot_repaint(WmWindow *win, CalcWin *w)
{
    CRect keys[CALC_ROWS * CALC_COLS];
    CRect client = wm_client_rect(win);
    int cw = crect_w(&client), ch = crect_h(&client), row, col;
    for (row = 0; row < CALC_ROWS; row++) {
        for (col = 0; col < CALC_COLS; col++) {
            int rs, cs;
            CRect none = crect_make(0, 0, 0, 0);
            if (calc_key_label(row, col)[0] == '\0' ||
                !calc_key_span(row, col, &rs, &cs)) {
                keys[row * CALC_COLS + col] = none;
                continue;
            }
            keys[row * CALC_COLS + col] = calc_key_rect(cw, ch, row, col, rs, cs);
        }
    }
    ui_hot_repaint(win, &w->hot, keys, CALC_ROWS * CALC_COLS);
}

static void calc_on_click(WmWindow *win, int px, int py)
{
    CalcWin *w = (CalcWin *)wm_user(win);
    CRect client = wm_client_rect(win);
    int cw = crect_w(&client), ch = crect_h(&client);
    int idx;
    if (w == NULL) { return; }
    idx = calc_key_at(cw, ch, px, py);
    (void)ui_hot_press(&w->hot, idx);
    if (idx >= 0) {
        (void)calc_press(&w->c, calc_key_label(idx / CALC_COLS,
                                               idx % CALC_COLS));
    }
    wm_invalidate(win, NULL);
}

/*
 * Where the pointer is, so the key under it lifts and the one being held
 * sinks. The whole keypad was drawn UI_BTN_NORMAL: pressing a key changed the
 * number in the display and nothing else, which on a window that IS its
 * buttons reads as a picture of a calculator.
 *
 * The repaint is on the TRANSITION, not on the movement -- ui_hot_move only
 * answers CTRUE when what should be drawn changed -- so crossing one key costs
 * one frame rather than one per pixel.
 */
static void calc_on_motion(WmWindow *win, int px, int py)
{
    CalcWin *w = (CalcWin *)wm_user(win);
    CRect client = wm_client_rect(win);
    if (w == NULL) { return; }
    if (ui_hot_move(&w->hot, calc_key_at(crect_w(&client), crect_h(&client),
                                         px, py))) {
        calc_hot_repaint(win, w);
    }
}

static void calc_on_key(WmWindow *win, int key, int ch)
{
    CalcWin *w = (CalcWin *)wm_user(win);
    Calc *c = (w != NULL) ? &w->c : (Calc *)0;
    if (c == NULL) { return; }
    if (ch >= '0' && ch <= '9') { calc_digit(c, ch - '0'); }
    else if (ch == '.') { calc_dot(c); }
    else if (ch == '+' || ch == '-' || ch == '*' || ch == '/') { calc_op(c, ch); }
    else if (ch == '=' || key == PLAT_KEY_ENTER) { calc_equals(c); }
    else if (ch == 'c' || ch == 'C' || key == PLAT_KEY_ESC) { calc_clear(c); }
    else if (key == PLAT_KEY_BACKSP) { calc_backspace(c); }
    /* The keys a desk calculator has a dedicated button for and a keyboard
     * does not. 'r' for root and 'p' for percent are the two that have no
     * obvious character; the rest match their labels. */
    else if (ch == 'r' || ch == 'R') { calc_sqrt(c); }
    else if (ch == '%')              { calc_percent(c); }
    else if (ch == 'i' || ch == 'I') { calc_recip(c); }
    else { return; }
    wm_invalidate(win, NULL);
}

static cbool calc_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    switch (msg) {
    case WM_MSG_PAINT:       calc_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: calc_on_click(win, (int)a, (int)b);   return CTRUE;
    case WM_MSG_MOUSEMOVE:   calc_on_motion(win, (int)a, (int)b);  return CTRUE;
    case WM_MSG_LBUTTONUP:
    case WM_MSG_MOUSELEAVE: {
        CalcWin *w = (CalcWin *)wm_user(win);
        cbool redraw;
        if (w == NULL) { return CTRUE; }
        redraw = ui_hot_release(&w->hot);
        if (msg == WM_MSG_MOUSELEAVE && ui_hot_move(&w->hot, -1)) {
            redraw = CTRUE;
        }
        if (redraw) { calc_hot_repaint(win, w); }
        return CTRUE;
    }
    case WM_MSG_KEYDOWN:     calc_on_key(win, (int)a, (int)b);     return CTRUE;
    case WM_MSG_DESTROY: {
        CalcWin *w = (CalcWin *)wm_user(win);
        if (w != NULL) { sys_free(w, (cu32)sizeof(CalcWin)); }
        return CTRUE;
    }
    default: return CFALSE;
    }
}

void app_calc_open(void)
{
    CalcWin *c;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    /* Five columns and six rows now rather than four and five, so the window
     * grew in both directions. */
    int fw = 248, fh = 300, fx, fy;

    c = (CalcWin *)sys_calloc(1, (cu32)sizeof(CalcWin));
    if (c == NULL) { SYS_LOGE("app", "calc: OOM"); return; }
    calc_reset(&c->c);
    ui_hot_init(&c->hot);

    plat_video_info(&vi);
    fx = (vi.width - fw) / 2 - 40;
    fy = (vi.height - fh) / 2 + 20;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, fw, fh);
    w = wm_create("Calculator", &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE |
                  WM_STYLE_BORDER,
                  calc_proc, c);
    if (w == NULL) { sys_free(c, (cu32)sizeof(CalcWin)); return; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
}

cbool app_calc_key_rect(WmWindow *win, int row, int col, CRect *out)
{
    CRect client;
    CPoint o;
    int rs, cs;
    if (win == NULL || out == NULL) { return CFALSE; }
    if (row < 0 || row >= CALC_ROWS || col < 0 || col >= CALC_COLS) {
        return CFALSE;
    }
    if (calc_key_label(row, col)[0] == '\0') { return CFALSE; }
    if (!calc_key_span(row, col, &rs, &cs)) { return CFALSE; }
    client = wm_client_rect(win);
    o = wm_client_origin(win);
    *out = calc_key_rect(crect_w(&client), crect_h(&client), row, col, rs, cs);
    *out = crect_offset(out, o.x, o.y);
    return CTRUE;
}

const char *app_calc_display(WmWindow *win)
{
    CalcWin *c = (win != NULL) ? (CalcWin *)wm_user(win) : NULL;
    return (c != NULL) ? c->c.disp : NULL;
}
