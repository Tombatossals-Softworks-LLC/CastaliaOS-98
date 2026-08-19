/*
 * app_paint.c - CastaliaPaint: the raster editor, in the 9x idiom.
 *
 * The shape of the era's paint programs, drawn with the engine's own
 * primitives: a menu bar, a two-column tool box down the left with an options
 * box under it, the image on a gray workspace with a drop shadow, the
 * 28-swatch palette along the bottom with the foreground/background pair at
 * its left end, and a status bar that reports the pointer position and the
 * image size.
 *
 * Ten tools -- pencil, brush, airbrush, eraser, fill, color picker, line,
 * rectangle, ellipse and text. Left click draws with the foreground color,
 * right click on a swatch sets the background one. Every edit that changes
 * pixels snapshots first, so Undo (Ctrl+Z) and Redo (Ctrl+Y) go three deep.
 *
 * Everything that touches pixels lives in paint_core.c, which is pure and
 * host-tested (tests/test_paint.c); this file is chrome, hit-testing and the
 * BMP round trip.
 */
#include "apps.h"
#include "paint_core.h"
#include "office_ui.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"
#include <stdlib.h>

/* Ctrl-shortcut detection on both backends (DOS delivers Ctrl+letter as an
 * ASCII control code in 'key'; synthetic input may carry the letter + CTRL). */
#define PT_CTRL(key, ch, mods, code, letter) \
    ((key) == (code) || \
     (((mods) & PLAT_MOD_CTRL) && ((ch) == (letter) || (ch) == (letter) - 32)))

#define PT_CANVAS_W  420
#define PT_CANVAS_H  260
/*
 * The most a canvas may be, in pixels -- which is the canvas it starts with.
 *
 * The canvas does NOT grow to a bigger picture, and the reason is the undo
 * ring: it holds PC_UNDO_LEVELS snapshots at canvas size, so a picture really
 * costs FIVE times its own pixels, and a window is not the only one open.
 *
 * This was measured, not argued. A 500x400 ceiling (200000 px) works out to
 * 3906 KB on paper against a 3767 KB idle desktop and an 8192 KB budget --
 * and --bigfile-demo, driving three Paint windows the way a session actually
 * does, peaked at 9040 KB. Over budget, from a change that read as free.
 *
 * So a canvas larger than this needs a cheaper undo first (a tile or a
 * command log rather than a whole-surface snapshot). Until then a bigger
 * picture is SHOWN in the corner it fits and its file is left alone -- see
 * pt_on_open.
 */
#define PT_CANVAS_MAX_PX ((long)PT_CANVAS_W * (long)PT_CANVAS_H)
#define PT_BOX_W     52     /* tool box column                              */
#define PT_BTN_W     24
#define PT_BTN_H     22
#define PT_OPT_H     70     /* options box under the tool box               */
#define PT_PAL_H     36     /* palette strip                                */
#define PT_PAD       2

enum { PT_PENCIL = 0, PT_BRUSH, PT_AIR, PT_ERASE, PT_FILL, PT_PICK,
       PT_LINE, PT_RECT, PT_ELLIPSE, PT_TEXT, PT_SELECT, PT_TOOL_N };

static const char *const PT_NAME[PT_TOOL_N] = {
    "Pencil", "Brush", "Airbrush", "Eraser", "Fill With Color",
    "Pick Color", "Line", "Rectangle", "Ellipse", "Text", "Select"
};

/* Brush / line widths and airbrush radii, one per options slot. */
static const int PT_WIDTH[4]  = { 1, 2, 4, 7 };
static const int PT_SPRAY_R[4] = { 3, 5, 8, 12 };

/* The era's 28-colour default palette, two rows of fourteen. */
static const CColor PT_PALETTE[28] = {
    0x000000UL, 0x808080UL, 0x800000UL, 0x808000UL, 0x008000UL, 0x008080UL,
    0x000080UL, 0x800080UL, 0x804000UL, 0x004040UL, 0x0080FFUL, 0x004080UL,
    0x8000FFUL, 0x804040UL,
    0xFFFFFFUL, 0xC0C0C0UL, 0xFF0000UL, 0xFFFF00UL, 0x00FF00UL, 0x00FFFFUL,
    0x0000FFUL, 0xFF00FFUL, 0xFF8040UL, 0x00FF80UL, 0x80FFFFUL, 0x8080FFUL,
    0xFF0080UL, 0xFF8000UL
};

static const char *const PT_MENU[] = { "File", "Edit", "Image", "Help" };
#define PT_MENU_N 4

enum {
    MC_NEW = 300, MC_OPEN, MC_SAVE, MC_SAVEAS, MC_CLOSE,
    MC_UNDO, MC_REDO, MC_CLEAR,
    MC_CUT, MC_COPY, MC_PASTE, MC_SELALL,
    MC_FLIPH, MC_FLIPV, MC_INVERT, MC_GRAY, MC_RESIZE,
    MC_ABOUT
};

typedef struct {
    GfxSurface *canvas;
    PcUndo undo;
    int    tool;
    int    opt;                 /* selected slot in the options box     */
    PcStyle style;              /* shape style (shares the options box)  */
    CColor fg, bg;
    cbool  drawing;             /* freehand stroke in progress           */
    cbool  dragging;            /* line/shape rubber band in progress    */
    int    x0, y0, x1, y1;      /* stroke endpoints, canvas coords       */
    int    mx, my;              /* pointer in canvas coords              */
    cbool  over;                /* ...and whether it is over the image   */
    int    tx, ty;              /* where the pending text goes           */
    cbool  has_sel;             /* a rectangle is marked on the image     */
    CRect  sel;                 /* ...this one, in canvas coords          */
    GfxSurface *clip;           /* the image clipboard, or NULL           */
    cu32   seed;                /* airbrush randomness                   */
    int    hot_tool;
    int    menu_open, menu_x, menu_y;
    UiMenu menu;
    char   path[CASTALIA_MAX_PATH];
    char   status[64];
    cu32   saved_sum;        /* the picture as it was when last saved     */
} Paint;

typedef struct {
    CRect menubar, box, opt, well, canvas, pal, fgbg, status;
    CRect tool[PT_TOOL_N];
    CRect slot[4];              /* options slots                         */
} PtLayout;

/* ---- layout ----------------------------------------------------------- */
static void pt_layout(WmWindow *win, Paint *p, PtLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c);
    int i, y, bw, bh;

    L->menubar = crect_make(0, 0, cw, OF_MENUBAR_H);
    L->status  = crect_make(0, ch - OF_STATUS_H, cw, OF_STATUS_H);
    L->pal     = crect_make(0, ch - OF_STATUS_H - PT_PAL_H, cw, PT_PAL_H);
    L->fgbg    = crect_make(4, L->pal.y0 + 6, 24, PT_PAL_H - 12);

    L->box = crect_make(0, OF_MENUBAR_H, PT_BOX_W,
                        PT_BTN_H * ((PT_TOOL_N + 1) / 2) + 2 * PT_PAD);
    for (i = 0; i < PT_TOOL_N; i++) {
        L->tool[i] = crect_make(PT_PAD + (i % 2) * PT_BTN_W,
                                L->box.y0 + PT_PAD + (i / 2) * PT_BTN_H,
                                PT_BTN_W, PT_BTN_H);
    }
    L->opt = crect_make(0, L->box.y1 + 2, PT_BOX_W, PT_OPT_H);
    bw = PT_BOX_W - 8;
    bh = (PT_OPT_H - 8) / 4;
    for (i = 0; i < 4; i++) {
        L->slot[i] = crect_make(4, L->opt.y0 + 4 + i * bh, bw, bh);
    }

    L->well = crect_make(PT_BOX_W, OF_MENUBAR_H, cw - PT_BOX_W,
                         L->pal.y0 - OF_MENUBAR_H);
    y = (p->canvas != NULL) ? p->canvas->h : PT_CANVAS_H;
    L->canvas = crect_make(L->well.x0 + 5, L->well.y0 + 5,
                           (p->canvas != NULL) ? p->canvas->w : PT_CANVAS_W, y);
}

/* ---- tool icons (original 16x16 pixel art, drawn with primitives) ----- */
static void pt_icon(GfxSurface *s, int x, int y, int tool, CColor ink)
{
    CColor tip = GFX_RGB(0xC0, 0x80, 0x40);
    CRect r;
    int i;
    switch (tool) {
    case PT_PENCIL:
        gfx_line(s, x + 3, y + 12, x + 11, y + 4, ink);
        gfx_line(s, x + 4, y + 13, x + 12, y + 5, ink);
        gfx_line(s, x + 2, y + 13, x + 4, y + 13, tip);
        gfx_put_pixel(s, x + 2, y + 12, tip);
        break;
    case PT_BRUSH:
        gfx_line(s, x + 6, y + 9, x + 12, y + 3, ink);
        gfx_line(s, x + 7, y + 10, x + 13, y + 4, ink);
        gfx_fill_circle(s, x + 5, y + 11, 2, tip);
        gfx_line(s, x + 2, y + 14, x + 4, y + 12, ink);
        break;
    case PT_AIR:
        r = crect_make(x + 8, y + 3, 4, 8);
        gfx_fill_rect(s, &r, ink);
        r = crect_make(x + 9, y + 11, 2, 2);
        gfx_fill_rect(s, &r, ink);
        for (i = 0; i < 6; i++) {
            gfx_put_pixel(s, x + 2 + (i % 3) * 2, y + 4 + (i / 3) * 3 + (i % 2), ink);
        }
        break;
    case PT_ERASE:
        gfx_line(s, x + 3, y + 10, x + 9, y + 4, ink);
        gfx_line(s, x + 8, y + 12, x + 14, y + 6, ink);
        gfx_line(s, x + 3, y + 10, x + 8, y + 12, ink);
        gfx_line(s, x + 9, y + 4, x + 14, y + 6, ink);
        r = crect_make(x + 5, y + 7, 6, 4);
        gfx_fill_rect(s, &r, GFX_RGB(0xFF, 0xD0, 0xD0));
        break;
    case PT_FILL:
        gfx_line(s, x + 3, y + 8, x + 8, y + 3, ink);
        r = crect_make(x + 4, y + 7, 8, 6);
        gfx_fill_rect(s, &r, ink);
        gfx_put_pixel(s, x + 13, y + 9, ink);
        gfx_put_pixel(s, x + 13, y + 10, ink);
        gfx_put_pixel(s, x + 12, y + 11, ink);
        break;
    case PT_PICK:
        gfx_line(s, x + 4, y + 12, x + 10, y + 6, ink);
        gfx_line(s, x + 5, y + 13, x + 11, y + 7, ink);
        r = crect_make(x + 9, y + 3, 4, 4);
        gfx_fill_rect(s, &r, ink);
        gfx_put_pixel(s, x + 3, y + 13, ink);
        break;
    case PT_LINE:
        gfx_line(s, x + 3, y + 12, x + 12, y + 3, ink);
        break;
    case PT_RECT:
        r = crect_make(x + 3, y + 4, 10, 8);
        gfx_frame_rect(s, &r, ink);
        break;
    case PT_ELLIPSE: {
        CRect e = crect_make(x + 2, y + 4, 12, 9);
        pc_ellipse(s, &e, 1, ink, ink, PC_OUTLINE);
        break;
    }
    case PT_TEXT:
        gfx_draw_text(s, GFX_FONT_BOLD, x + 5, y + 4, "A", ink);
        gfx_hline(s, x + 3, y + 13, 10, ink);
        break;
    default:                                     /* Select: a marquee      */
        for (i = 3; i <= 12; i += 3) {
            gfx_put_pixel(s, x + i, y + 3, ink);
            gfx_put_pixel(s, x + i, y + 12, ink);
            gfx_put_pixel(s, x + 3, y + i, ink);
            gfx_put_pixel(s, x + 12, y + i, ink);
        }
        break;
    }
}

/* ---- canvas helpers ---------------------------------------------------- */
static void pt_clear(Paint *p)
{
    CRect all;
    if (p->canvas == NULL) { return; }
    all = crect_make(0, 0, p->canvas->w, p->canvas->h);
    gfx_reset_clip(p->canvas);
    gfx_fill_rect(p->canvas, &all, p->bg);
}

/* The color a stroke uses: the eraser paints with the background. */
static CColor pt_ink(const Paint *p)
{
    return (p->tool == PT_ERASE) ? p->bg : p->fg;
}

static int pt_width(const Paint *p)
{
    return PT_WIDTH[(p->opt >= 0 && p->opt < 4) ? p->opt : 0];
}

/* The rectangle a shape drag covers, normalized. */
static CRect pt_drag_rect(const Paint *p)
{
    return crect_make_xyxy(
        (p->x0 < p->x1) ? p->x0 : p->x1, (p->y0 < p->y1) ? p->y0 : p->y1,
        ((p->x0 > p->x1) ? p->x0 : p->x1) + 1,
        ((p->y0 > p->y1) ? p->y0 : p->y1) + 1);
}

static void pt_snapshot(Paint *p)
{
    pc_undo_push(&p->undo, p->canvas);
}

/* Commit a line/shape drag into the canvas. */
static void pt_commit(Paint *p)
{
    CRect r = pt_drag_rect(p);
    if (p->tool == PT_SELECT) {
        /* A marquee marks; it does not draw. One pixel is not a selection. */
        if (crect_w(&r) > 1 && crect_h(&r) > 1) {
            p->sel = r;
            p->has_sel = CTRUE;
            sys_snprintf(p->status, sizeof p->status, "Selected %dx%d",
                         crect_w(&r), crect_h(&r));
        }
        return;
    }
    switch (p->tool) {
    case PT_LINE:
        pc_stroke(p->canvas, p->x0, p->y0, p->x1, p->y1, pt_width(p), p->fg);
        break;
    case PT_RECT:
        pc_rect(p->canvas, &r, pt_width(p), p->fg, p->bg, p->style);
        break;
    case PT_ELLIPSE:
        pc_ellipse(p->canvas, &r, pt_width(p), p->fg, p->bg, p->style);
        break;
    default:
        break;
    }
}

/* ---- painting ---------------------------------------------------------- */
/* A dashed marquee, drawn in alternating black and white so it reads on any
 * image underneath it -- the only honest way to outline a selection when
 * there is no XOR blit. */
static void pt_marquee(GfxSurface *s, const CRect *r)
{
    CColor a = GFX_RGB(0, 0, 0), b = GFX_RGB(0xFF, 0xFF, 0xFF);
    int x, y;
    for (x = r->x0; x < r->x1; x++) {
        CColor c = (((x - r->x0) / 3) & 1) ? b : a;
        gfx_put_pixel(s, x, r->y0, c);
        gfx_put_pixel(s, x, r->y1 - 1, c);
    }
    for (y = r->y0; y < r->y1; y++) {
        CColor c = (((y - r->y0) / 3) & 1) ? b : a;
        gfx_put_pixel(s, r->x0, y, c);
        gfx_put_pixel(s, r->x1 - 1, y, c);
    }
}

static void pt_paint_toolbox(GfxSurface *s, const PtLayout *L, CPoint o,
                             Paint *p)
{
    const UiPalette *pal = ui_palette();
    CRect r;
    int i;

    r = crect_offset(&L->box, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, pal->light, pal->dark, pal->face);
    for (i = 0; i < PT_TOOL_N; i++) {
        CRect b = crect_offset(&L->tool[i], o.x, o.y);
        int down = (i == p->tool);
        /* Flat until it is the pointer's or the current tool -- the same
         * grammar as the office toolbars, with the tool latched sunken. */
        if (down) {
            gfx_bevel(s, &b, GFX_BEVEL_SUNKEN_THIN, pal->light, pal->dark,
                      gfx_tint(pal->face, GFX_RGB(0xFF, 0xFF, 0xFF), 40));
        } else if (i == p->hot_tool) {
            gfx_bevel(s, &b, GFX_BEVEL_RAISED_THIN, pal->light, pal->dark,
                      GFX_NO_FILL);
        }
        pt_icon(s, b.x0 + 4 + (down ? 1 : 0), b.y0 + 3 + (down ? 1 : 0),
                i, pal->text);
    }

    /* Options box: brush sizes, or the three shape styles. */
    r = crect_offset(&L->opt, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, pal->light, pal->dark, pal->face);
    if (p->tool == PT_RECT || p->tool == PT_ELLIPSE) {
        for (i = 0; i < 3; i++) {
            CRect b = crect_offset(&L->slot[i], o.x, o.y);
            CRect sample = crect_inset(&b, 3);
            if (i == (int)p->style) {
                gfx_fill_rect(s, &b, pal->accent);
            }
            if (p->tool == PT_RECT) {
                pc_rect(s, &sample, 1, pal->text,
                        (i == (int)p->style) ? pal->accent_text : pal->light,
                        (PcStyle)i);
            } else {
                pc_ellipse(s, &sample, 1, pal->text,
                           (i == (int)p->style) ? pal->accent_text : pal->light,
                           (PcStyle)i);
            }
        }
    } else if (p->tool != PT_FILL && p->tool != PT_PICK && p->tool != PT_TEXT) {
        for (i = 0; i < 4; i++) {
            CRect b = crect_offset(&L->slot[i], o.x, o.y);
            int cx = b.x0 + crect_w(&b) / 2, cy = b.y0 + crect_h(&b) / 2;
            CColor ink = pal->text;
            if (i == p->opt) { gfx_fill_rect(s, &b, pal->accent); ink = pal->accent_text; }
            if (p->tool == PT_LINE) {
                int w = PT_WIDTH[i], k;
                for (k = 0; k < w; k++) {
                    gfx_hline(s, b.x0 + 5, cy - w / 2 + k, crect_w(&b) - 10, ink);
                }
            } else if (p->tool == PT_AIR) {
                int k, n = 6 + i * 6;
                cu32 sd = 7u + (cu32)i;
                for (k = 0; k < n; k++) {
                    int ox, oy, rr = PT_SPRAY_R[i] / 2 + 1;
                    sd = sd * 1103515245UL + 12345UL;
                    ox = (int)((sd >> 16) % (cu32)(rr * 2 + 1)) - rr;
                    sd = sd * 1103515245UL + 12345UL;
                    oy = (int)((sd >> 16) % (cu32)(rr * 2 + 1)) - rr;
                    gfx_put_pixel(s, cx + ox, cy + oy, ink);
                }
            } else {
                pc_dab(s, cx, cy, PT_WIDTH[i], ink);
            }
        }
    } else if (p->tool == PT_TEXT) {
        /* The text tool's only setting is the face it draws in, so show it. */
        gfx_draw_text_rect(s, GFX_FONT_BOLD, &r, "Aa", pal->text,
                           GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
    }
    /* Fill and Pick have nothing to set: the box stays empty rather than
     * printing a label that says so. */
}

static void pt_paint_palette(GfxSurface *s, const PtLayout *L, CPoint o,
                             Paint *p)
{
    const UiPalette *pal = ui_palette();
    CRect r, sw;
    int i, x0, y0, cell;

    r = crect_offset(&L->pal, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, pal->light, pal->dark, pal->face);

    /* The foreground/background pair, drawn as two overlapping chips. */
    sw = crect_offset(&L->fgbg, o.x, o.y);
    {
        CRect back = crect_make(sw.x0 + 8, sw.y0 + 8, 14, 14);
        CRect front = crect_make(sw.x0, sw.y0, 14, 14);
        gfx_fill_rect(s, &back, p->bg);
        gfx_bevel(s, &back, GFX_BEVEL_SUNKEN_THIN, pal->light, pal->dark,
                  GFX_NO_FILL);
        gfx_fill_rect(s, &front, p->fg);
        gfx_bevel(s, &front, GFX_BEVEL_SUNKEN_THIN, pal->light, pal->dark,
                  GFX_NO_FILL);
    }

    cell = 13;
    x0 = r.x0 + 40;
    y0 = r.y0 + (crect_h(&r) - cell * 2) / 2;
    for (i = 0; i < 28; i++) {
        CRect c = crect_make(x0 + (i % 14) * cell, y0 + (i / 14) * cell,
                             cell - 1, cell - 1);
        gfx_fill_rect(s, &c, PT_PALETTE[i]);
        gfx_bevel(s, &c, GFX_BEVEL_SUNKEN_THIN, pal->light, pal->dark,
                  GFX_NO_FILL);
    }
}

static void pt_paint(WmWindow *win, GfxSurface *s)
{
    Paint *p = (Paint *)wm_user(win);
    const UiPalette *pal = ui_palette();
    CPoint o = wm_client_origin(win);
    PtLayout L;
    CRect r, cvr;
    char buf[48];

    if (p == NULL || p->canvas == NULL) { return; }
    pt_layout(win, p, &L);

    /* Menu bar. */
    r = crect_offset(&L.menubar, o.x, o.y);
    office_menubar(s, &r, PT_MENU, PT_MENU_N, p->menu_open);

    /* Workspace: a flat gray field the image floats on. */
    r = crect_offset(&L.well, o.x, o.y);
    gfx_fill_rect(s, &r, gfx_tint(pal->face, GFX_RGB(0, 0, 0), 40));

    pt_paint_toolbox(s, &L, o, p);
    pt_paint_palette(s, &L, o, p);

    /* The image, with a hairline border and a soft shadow. */
    cvr = crect_offset(&L.canvas, o.x, o.y);
    {
        CRect saved;
        CRect vis = crect_intersect(&r, &cvr);
        CRect src = crect_make(0, 0, p->canvas->w, p->canvas->h);
        CRect edge = crect_inset(&cvr, -1);
        gfx_drop_shadow(s, &cvr, 4, 110);
        gfx_frame_rect(s, &edge, pal->dark);
        saved = gfx_clip_narrow(s, &vis);
        gfx_blit(s, cvr.x0, cvr.y0, p->canvas, &src, GFX_BLIT_COPY);

        /* Live rubber band for the drag tools, over the blit. */
        if (p->dragging) {
            int ox = cvr.x0, oy = cvr.y0;
            CRect d = pt_drag_rect(p);
            CRect dr = crect_offset(&d, ox, oy);
            if (p->tool == PT_LINE) {
                pc_stroke(s, ox + p->x0, oy + p->y0, ox + p->x1, oy + p->y1,
                          pt_width(p), p->fg);
            } else if (p->tool == PT_RECT) {
                pc_rect(s, &dr, pt_width(p), p->fg, p->bg, p->style);
            } else if (p->tool == PT_ELLIPSE) {
                pc_ellipse(s, &dr, pt_width(p), p->fg, p->bg, p->style);
            } else if (p->tool == PT_SELECT) {
                pt_marquee(s, &dr);
            }
        }
        /* The standing selection, when it is not being dragged out. */
        if (p->has_sel && !p->dragging) {
            CRect m = crect_offset(&p->sel, cvr.x0, cvr.y0);
            pt_marquee(s, &m);
        }
        gfx_set_clip(s, &saved);
    }

    /* Status bar: the message, the pointer, and the image size. */
    r = crect_offset(&L.status, o.x, o.y);
    office_statusbar(s, &r);
    {
        int w = crect_w(&r);
        CRect pn = crect_make(r.x0 + 2, r.y0 + 2, w - 176, crect_h(&r) - 4);
        office_status_panel(s, &pn, p->status, GFX_ALIGN_LEFT);
        if (p->over) {
            sys_snprintf(buf, sizeof buf, "%d,%d", p->mx, p->my);
        } else {
            sys_strlcpy(buf, "", sizeof buf);
        }
        pn = crect_make(r.x1 - 172, r.y0 + 2, 84, crect_h(&r) - 4);
        office_status_panel(s, &pn, buf, GFX_ALIGN_HCENTER);
        sys_snprintf(buf, sizeof buf, "%dx%d", p->canvas->w, p->canvas->h);
        pn = crect_make(r.x1 - 86, r.y0 + 2, 84, crect_h(&r) - 4);
        office_status_panel(s, &pn, buf, GFX_ALIGN_HCENTER);
    }

    /* An open drop-down, over everything. */
    if (p->menu_open >= 0) {
        int mx = o.x + p->menu_x, my = o.y + p->menu_y, mw, mh;
        CRect mr;
        ui_menu_measure(&p->menu, GFX_FONT_SYSTEM, &mw, &mh);
        mr = crect_make(mx, my, mw, mh);
        gfx_drop_shadow(s, &mr, 4, 90);
        ui_menu_draw(s, &p->menu, mx, my, GFX_FONT_SYSTEM);
    }
}

/* ---- files ------------------------------------------------------------- */
/*
 * A checksum of the canvas, DERIVED rather than tracked -- see the note in
 * app_sheet.c. Every stroke, fill, flip and paste changes pixels, so there is
 * no edit path that can forget to mark the picture as changed. Walked only
 * when the window is closing, so scanning the whole canvas costs nothing that
 * anybody waits for.
 */
static cu32 pt_doc_sum(const Paint *p)
{
    cu32 h = 2166136261u;
    int x, y;
    if (p == NULL || p->canvas == NULL || p->canvas->pixels == NULL) {
        return h;
    }
    for (y = 0; y < p->canvas->h; y++) {
        const CColor *row = p->canvas->pixels + (long)y * p->canvas->pitch;
        for (x = 0; x < p->canvas->w; x++) {
            h = (h ^ (cu32)(row[x] & 0xFFFFFFUL)) * 16777619u;
        }
    }
    return h;
}

static void pt_on_discard(UiDialogResult result, void *user)
{
    WmWindow *win = (WmWindow *)user;
    if (result == UI_DR_YES && win != NULL) { wm_destroy(win); }
}

/* Closing a picture with unsaved changes asks first -- see app_notepad.c. */
/* One predicate, asked by the close box and by the shutdown dialog. */
static cbool pt_has_unsaved(WmWindow *win)
{
    Paint *p = (win != NULL) ? (Paint *)wm_user(win) : NULL;
    return (p != NULL && pt_doc_sum(p) != p->saved_sum) ? CTRUE : CFALSE;
}

static cbool pt_on_close(WmWindow *win)
{
    Paint *p = (win != NULL) ? (Paint *)wm_user(win) : NULL;
    char msg[160];
    if (!pt_has_unsaved(win)) { return CFALSE; }
    sys_snprintf(msg, sizeof(msg),
                 "%s has changes you have not saved.\n\n"
                 "Close it and lose them?",
                 (p->path[0] != '\0') ? ui_path_base(p->path) : "This picture");
    ui_msgbox("CastaliaPaint", msg, UI_MB_YESNO, pt_on_discard, win);
    return CTRUE;
}

static void pt_on_save(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    Paint *p = (win != NULL) ? (Paint *)wm_user(win) : NULL;
    if (!ok || p == NULL || text == NULL || text[0] == '\0') { return; }
    if (gfx_bmp_save(p->canvas, text) == CE_OK) {
        sys_strlcpy(p->path, text, sizeof p->path);
        p->saved_sum = pt_doc_sum(p);
        sys_snprintf(p->status, sizeof p->status, "Saved %s", text);
    } else {
        sys_snprintf(p->status, sizeof p->status, "Cannot save %s", text);
    }
    wm_invalidate(win, NULL);
}

/*
 * Open a picture WHOLE, or say which part of it you are looking at.
 *
 * The canvas kept its 420x260 whatever it was handed, so a bigger image
 * landed at the top-left and the rest was thrown away -- and then Save wrote
 * the 420x260 canvas back over the file, because the path had been adopted.
 * The status even quoted the image's REAL size, "Opened SHOT.BMP (800x600)",
 * which reads as though all of it arrived.
 *
 * This is not a hypothetical size: the system's own screen capture writes an
 * 800x600 BMP, so opening your own screenshot in Paint and pressing Ctrl+S
 * destroyed 74% of it.
 *
 * A picture that FITS still opens exactly as before. One that does not is
 * shown in the corner it fits, its real size is said next to what you are
 * looking at, and the path is NOT adopted: Ctrl+S then asks for a name rather
 * than writing the crop back over the picture it came from.
 *
 * Growing the canvas to the picture would be better still, and is not
 * affordable yet -- see PT_CANVAS_MAX_PX for the measurement that says so.
 */
static void pt_on_open(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    Paint *p = (win != NULL) ? (Paint *)wm_user(win) : NULL;
    GfxSurface *img;
    cbool whole;
    if (!ok || p == NULL || text == NULL || text[0] == '\0') { return; }
    img = gfx_bmp_load(text);
    if (img == NULL) {
        sys_snprintf(p->status, sizeof p->status, "Cannot open %s",
                     ui_path_base(text));
        wm_invalidate(win, NULL);
        return;
    }
    whole = (img->w <= PT_CANVAS_W && img->h <= PT_CANVAS_H) ? CTRUE : CFALSE;
    {
        /*
         * The canvas takes the picture's size, up to the ceiling. Shrinking
         * is free -- it is the GROWING that the undo ring cannot afford --
         * and without it Save padded every smaller picture out to 420x260,
         * so a 400x240 image came back 420x260 with a border it never had.
         */
        int cw = (img->w < PT_CANVAS_W) ? img->w : PT_CANVAS_W;
        int chh = (img->h < PT_CANVAS_H) ? img->h : PT_CANVAS_H;
        if (cw != p->canvas->w || chh != p->canvas->h) {
            GfxSurface *cv = gfx_surface_new(cw, chh);
            if (cv != NULL) {
                /* The ring holds canvases of one size, so a resize starts it
                 * over rather than leaving snapshots that no longer fit. */
                pc_undo_free(&p->undo);
                pc_undo_init(&p->undo);
                gfx_surface_free(p->canvas);
                p->canvas = cv;
                p->has_sel = CFALSE;
            }
        } else {
            pt_snapshot(p);   /* Ctrl+Z gets the previous picture back */
        }
    }
    pt_clear(p);
    {
        CRect src = crect_make(0, 0, img->w, img->h);
        gfx_reset_clip(p->canvas);
        gfx_blit(p->canvas, 0, 0, img, &src, GFX_BLIT_COPY);
    }
    if (whole) {
        sys_snprintf(p->status, sizeof p->status, "Opened %s (%dx%d)",
                     ui_path_base(text), img->w, img->h);
        sys_strlcpy(p->path, text, sizeof p->path);
        p->saved_sum = pt_doc_sum(p);
    } else {
        p->path[0] = '\0';
        sys_snprintf(p->status, sizeof p->status,
                     "%s is %dx%d -- showing %dx%d. Save will ask for a new "
                     "name.", ui_path_base(text), img->w, img->h,
                     p->canvas->w, p->canvas->h);
    }
    gfx_surface_free(img);
    wm_invalidate(win, NULL);
}

/* Stretch the image by a percentage, the way the era's editors did it: one
 * number, nearest-neighbour, and the canvas takes the new size. */
static void pt_on_resize(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    Paint *p = (win != NULL) ? (Paint *)wm_user(win) : NULL;
    GfxSurface *scaled;
    int pct = 0, i;
    if (!ok || p == NULL || text == NULL) { return; }
    for (i = 0; text[i] >= '0' && text[i] <= '9'; i++) {
        pct = pct * 10 + (text[i] - '0');
        if (pct > 400) { break; }
    }
    if (pct < 10 || pct > 400) {
        sys_strlcpy(p->status, "Use 10 to 400 percent", sizeof p->status);
        wm_invalidate(win, NULL);
        return;
    }
    scaled = pc_scale(p->canvas, (p->canvas->w * pct) / 100,
                      (p->canvas->h * pct) / 100);
    if (scaled == NULL) {
        sys_strlcpy(p->status, "Out of memory -- not resized",
                    sizeof p->status);
        wm_invalidate(win, NULL);
        return;
    }
    /* The undo ring holds canvases of one size, so a resize starts it over
     * rather than leaving snapshots that no longer fit. */
    pc_undo_free(&p->undo);
    pc_undo_init(&p->undo);
    gfx_surface_free(p->canvas);
    p->canvas = scaled;
    p->has_sel = CFALSE;
    sys_snprintf(p->status, sizeof p->status, "Stretched to %dx%d",
                 p->canvas->w, p->canvas->h);
    wm_invalidate(win, NULL);
}

static void pt_on_text(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    Paint *p = (win != NULL) ? (Paint *)wm_user(win) : NULL;
    if (!ok || p == NULL || text == NULL || text[0] == '\0') { return; }
    pt_snapshot(p);
    gfx_reset_clip(p->canvas);
    gfx_draw_text(p->canvas, GFX_FONT_BOLD, p->tx, p->ty, text, p->fg);
    sys_snprintf(p->status, sizeof p->status, "Text at %d,%d", p->tx, p->ty);
    wm_invalidate(win, NULL);
}

/* ---- menus ------------------------------------------------------------- */
static void pt_build_menu(Paint *p, int idx)
{
    UiMenu *m = &p->menu;
    ui_menu_clear(m);
    switch (idx) {
    case 0:
        ui_menu_add(m, MC_NEW,    "New", CTRUE);
        ui_menu_add(m, MC_OPEN,   "Open...", CTRUE);
        ui_menu_add(m, MC_SAVE,   "Save", CTRUE);
        ui_menu_add(m, MC_SAVEAS, "Save As...", CTRUE);
        ui_menu_add_separator(m);
        ui_menu_add(m, MC_CLOSE,  "Exit", CTRUE);
        break;
    case 1:
        ui_menu_add(m, MC_UNDO, "Undo\tCtrl+Z", pc_undo_depth(&p->undo) > 0);
        ui_menu_add(m, MC_REDO, "Redo\tCtrl+Y",
                    pc_undo_redo_depth(&p->undo) > 0);
        ui_menu_add_separator(m);
        ui_menu_add(m, MC_CUT,   "Cut\tCtrl+X", p->has_sel);
        ui_menu_add(m, MC_COPY,  "Copy\tCtrl+C", p->has_sel);
        ui_menu_add(m, MC_PASTE, "Paste\tCtrl+V", p->clip != NULL);
        ui_menu_add(m, MC_SELALL, "Select All", CTRUE);
        ui_menu_add_separator(m);
        ui_menu_add(m, MC_CLEAR, "Clear Image", CTRUE);
        break;
    case 2:
        ui_menu_add(m, MC_FLIPH,  "Flip Horizontal", CTRUE);
        ui_menu_add(m, MC_FLIPV,  "Flip Vertical", CTRUE);
        ui_menu_add_separator(m);
        ui_menu_add(m, MC_INVERT, "Invert Colors", CTRUE);
        ui_menu_add(m, MC_GRAY,   "Grayscale", CTRUE);
        ui_menu_add_separator(m);
        ui_menu_add(m, MC_RESIZE, "Stretch...", CTRUE);
        break;
    default:
        ui_menu_add(m, MC_ABOUT, "About CastaliaPaint...", CTRUE);
        break;
    }
    m->highlight = -1;
}

/* office_menu_key rebuilds the list as it moves along the bar. */
static void pt_menu_build_cb(void *user, int idx)
{
    pt_build_menu((Paint *)user, idx);
}

static void pt_open_menu(WmWindow *win, Paint *p, int idx)
{
    PtLayout L;
    CRect w;
    pt_layout(win, p, &L);
    w = office_menu_word(&L.menubar, PT_MENU, idx);
    p->menu_open = idx;
    p->menu_x = w.x0;
    p->menu_y = L.menubar.y1;
    pt_build_menu(p, idx);
}

static void pt_command(WmWindow *win, Paint *p, int cmd)
{
    switch (cmd) {
    case MC_NEW:
        pt_snapshot(p); pt_clear(p); p->path[0] = '\0';
        p->saved_sum = pt_doc_sum(p);   /* File > New starts clean */
        sys_strlcpy(p->status, "New image", sizeof p->status);
        break;
    case MC_OPEN:
        ui_file_dialog("Open Image", NULL, "BMP", CFALSE, p->path,
                       pt_on_open, win);
        break;
    case MC_SAVE:
        if (p->path[0] != '\0') { pt_on_save(CTRUE, p->path, win); }
        else {
            ui_file_dialog("Save Image", NULL, "BMP", CTRUE, "UNTITLED.BMP",
                           pt_on_save, win);
        }
        break;
    case MC_SAVEAS:
        ui_file_dialog("Save Image As", NULL, "BMP", CTRUE,
                       (p->path[0] != '\0') ? p->path : "UNTITLED.BMP",
                       pt_on_save, win);
        break;
    case MC_CLOSE:
        wm_destroy(win);
        return;
    case MC_UNDO:
        if (pc_undo_undo(&p->undo, p->canvas)) {
            sys_strlcpy(p->status, "Undo", sizeof p->status);
        } else {
            sys_strlcpy(p->status, "Nothing to undo", sizeof p->status);
        }
        break;
    case MC_REDO:
        if (pc_undo_redo(&p->undo, p->canvas)) {
            sys_strlcpy(p->status, "Redo", sizeof p->status);
        } else {
            sys_strlcpy(p->status, "Nothing to redo", sizeof p->status);
        }
        break;
    case MC_CLEAR:
        pt_snapshot(p); pt_clear(p);
        p->has_sel = CFALSE;
        sys_strlcpy(p->status, "Image cleared", sizeof p->status);
        break;
    case MC_SELALL:
        p->sel = crect_make(0, 0, p->canvas->w, p->canvas->h);
        p->has_sel = CTRUE;
        p->tool = PT_SELECT;
        sys_strlcpy(p->status, "Whole image selected", sizeof p->status);
        break;
    case MC_CUT:
    case MC_COPY:
        if (!p->has_sel) {
            sys_strlcpy(p->status, "Select an area first", sizeof p->status);
            break;
        }
        if (p->clip != NULL) { gfx_surface_free(p->clip); }
        p->clip = pc_copy_region(p->canvas, &p->sel);
        if (p->clip == NULL) {
            sys_strlcpy(p->status, "Nothing to copy", sizeof p->status);
            break;
        }
        if (cmd == MC_CUT) {
            pt_snapshot(p);
            pc_clear_region(p->canvas, &p->sel, p->bg);
            p->has_sel = CFALSE;
        }
        sys_snprintf(p->status, sizeof p->status, "%s %dx%d",
                     (cmd == MC_CUT) ? "Cut" : "Copied",
                     p->clip->w, p->clip->h);
        break;
    case MC_PASTE:
        if (p->clip == NULL) {
            sys_strlcpy(p->status, "The clipboard is empty", sizeof p->status);
            break;
        }
        pt_snapshot(p);
        /* Paste lands at the selection's corner when there is one, else at
         * the top-left -- and the pasted block becomes the new selection, so
         * it can be cut or copied straight back out. */
        {
            int x = p->has_sel ? p->sel.x0 : 0;
            int y = p->has_sel ? p->sel.y0 : 0;
            pc_paste(p->canvas, p->clip, x, y);
            p->sel = crect_make(x, y, p->clip->w, p->clip->h);
            p->has_sel = CTRUE;
            p->tool = PT_SELECT;
        }
        sys_snprintf(p->status, sizeof p->status, "Pasted %dx%d",
                     p->clip->w, p->clip->h);
        break;
    case MC_RESIZE:
        ui_prompt("Stretch", "Percent of the current size:", "200",
                  pt_on_resize, win);
        return;
    case MC_FLIPH:
        pt_snapshot(p); pc_flip_h(p->canvas);
        sys_strlcpy(p->status, "Flipped horizontally", sizeof p->status);
        break;
    case MC_FLIPV:
        pt_snapshot(p); pc_flip_v(p->canvas);
        sys_strlcpy(p->status, "Flipped vertically", sizeof p->status);
        break;
    case MC_INVERT:
        pt_snapshot(p); pc_invert(p->canvas);
        sys_strlcpy(p->status, "Colors inverted", sizeof p->status);
        break;
    case MC_GRAY:
        pt_snapshot(p); pc_grayscale(p->canvas);
        sys_strlcpy(p->status, "Converted to grayscale", sizeof p->status);
        break;
    case MC_ABOUT:
        ui_msgbox("About CastaliaPaint",
                  "CastaliaPaint\n\n"
                  "Eleven tools, three levels of undo, and a 24-bit BMP\n"
                  "round trip. All drawing is integer-only.",
                  UI_MB_OK, NULL, NULL);
        break;
    default:
        /*
         * Nothing. About used to live here, which meant every command id this
         * switch does not know opened the About box -- a menu item added and
         * never wired up would not look broken, it would look like it did
         * something else on purpose. The shell had the same shape of bug and
         * it cost two games: a command answered by a catch-all is a command
         * nobody can tell is missing.
         */
        SYS_LOGW("app", "paint: unhandled command %d", cmd);
        return;
    }
    /* Every command reports through the status line; logging it too is what
     * lets the headless demos see what the UI actually did. */
    SYS_LOGI("app", "paint: %s", p->status);
    wm_invalidate(win, NULL);
}

/* ---- input ------------------------------------------------------------- */
/* Map a client point to canvas coords; CTRUE when it lands on the image. */
static cbool pt_canvas_xy(const PtLayout *L, Paint *p, int px, int py,
                          int *cx, int *cy)
{
    *cx = px - L->canvas.x0;
    *cy = py - L->canvas.y0;
    return (*cx >= 0 && *cy >= 0 && *cx < p->canvas->w && *cy < p->canvas->h)
           ? CTRUE : CFALSE;
}

static void pt_press(WmWindow *win, Paint *p, const PtLayout *L, int cx, int cy)
{
    CColor ink = pt_ink(p);
    p->x0 = p->x1 = cx;
    p->y0 = p->y1 = cy;
    (void)L;
    switch (p->tool) {
    case PT_FILL:
        pt_snapshot(p);
        pc_flood(p->canvas, cx, cy, p->fg);
        break;
    case PT_PICK:
        p->fg = pc_pick(p->canvas, cx, cy, p->fg);
        sys_strlcpy(p->status, "Color picked", sizeof p->status);
        break;
    case PT_TEXT:
        p->tx = cx; p->ty = cy;
        ui_prompt("Text", "Text to draw:", "", pt_on_text, win);
        break;
    case PT_LINE: case PT_RECT: case PT_ELLIPSE:
        pt_snapshot(p);
        p->dragging = CTRUE;
        break;
    case PT_SELECT:
        p->has_sel = CFALSE;      /* a new drag replaces the old marquee   */
        p->dragging = CTRUE;
        break;
    case PT_AIR:
        pt_snapshot(p);
        pc_spray(p->canvas, cx, cy, PT_SPRAY_R[p->opt], 12 + p->opt * 8,
                 ink, &p->seed);
        p->drawing = CTRUE;
        break;
    default:
        pt_snapshot(p);
        pc_dab(p->canvas, cx, cy, pt_width(p), ink);
        p->drawing = CTRUE;
        break;
    }
}

static void pt_click(WmWindow *win, int px, int py)
{
    Paint *p = (Paint *)wm_user(win);
    PtLayout L;
    int i, cx, cy;
    if (p == NULL) { return; }
    pt_layout(win, p, &L);

    /* An open menu takes the click first. */
    if (p->menu_open >= 0) {
        int hit = ui_menu_hit(&p->menu, p->menu_x, p->menu_y,
                              GFX_FONT_SYSTEM, px, py);
        if (hit >= 0) {
            int cmd = p->menu.items[hit].id;
            p->menu_open = -1;
            pt_command(win, p, cmd);
            return;
        }
        p->menu_open = -1;
        wm_invalidate(win, NULL);
    }
    if (crect_contains(&L.menubar, px, py)) {
        for (i = 0; i < PT_MENU_N; i++) {
            CRect w = office_menu_word(&L.menubar, PT_MENU, i);
            if (crect_contains(&w, px, py)) {
                pt_open_menu(win, p, i);
                wm_invalidate(win, NULL);
                return;
            }
        }
        return;
    }
    for (i = 0; i < PT_TOOL_N; i++) {
        if (crect_contains(&L.tool[i], px, py)) {
            p->tool = i;
            sys_snprintf(p->status, sizeof p->status, "%s", PT_NAME[i]);
            wm_invalidate(win, NULL);
            return;
        }
    }
    for (i = 0; i < 4; i++) {
        if (crect_contains(&L.slot[i], px, py)) {
            if (p->tool == PT_RECT || p->tool == PT_ELLIPSE) {
                if (i < 3) { p->style = (PcStyle)i; }
            } else {
                p->opt = i;
            }
            wm_invalidate(win, NULL);
            return;
        }
    }
    if (crect_contains(&L.pal, px, py)) {
        int cell = 13, x0 = L.pal.x0 + 40;
        int y0 = L.pal.y0 + (crect_h(&L.pal) - cell * 2) / 2;
        int col = (px - x0) / cell, row = (py - y0) / cell;
        if (px >= x0 && col >= 0 && col < 14 && row >= 0 && row < 2) {
            p->fg = PT_PALETTE[row * 14 + col];
            wm_invalidate(win, NULL);
        }
        return;
    }
    if (pt_canvas_xy(&L, p, px, py, &cx, &cy)) {
        pt_press(win, p, &L, cx, cy);
        wm_invalidate(win, NULL);
    }
}

/* Right click: set the background color from a swatch or from the image. */
static void pt_rclick(WmWindow *win, int px, int py)
{
    Paint *p = (Paint *)wm_user(win);
    PtLayout L;
    int cx, cy;
    if (p == NULL) { return; }
    pt_layout(win, p, &L);
    if (crect_contains(&L.pal, px, py)) {
        int cell = 13, x0 = L.pal.x0 + 40;
        int y0 = L.pal.y0 + (crect_h(&L.pal) - cell * 2) / 2;
        int col = (px - x0) / cell, row = (py - y0) / cell;
        if (px >= x0 && col >= 0 && col < 14 && row >= 0 && row < 2) {
            p->bg = PT_PALETTE[row * 14 + col];
            sys_strlcpy(p->status, "Background color set", sizeof p->status);
            wm_invalidate(win, NULL);
        }
        return;
    }
    if (pt_canvas_xy(&L, p, px, py, &cx, &cy)) {
        p->bg = pc_pick(p->canvas, cx, cy, p->bg);
        sys_strlcpy(p->status, "Background color picked", sizeof p->status);
        wm_invalidate(win, NULL);
    }
}

static void pt_motion(WmWindow *win, int px, int py)
{
    Paint *p = (Paint *)wm_user(win);
    PtLayout L;
    int cx, cy, i, hot = -1;
    if (p == NULL) { return; }
    pt_layout(win, p, &L);

    /*
     * An open menu highlights the row under the pointer. It did not, so
     * running the mouse down Paint's Image menu lit nothing up until a click
     * -- which in this idiom reads as a menu that is not responding.
     */
    if (p->menu_open >= 0) {
        /*
         * ...and sliding along the BAR with one open switches to the menu you
         * slide onto, rather than making you close this one and click again.
         * Checked before the drop-down, because the bar sits above the open
         * menu and the pointer crosses it on the way.
         */
        if (crect_contains(&L.menubar, px, py)) {
            for (i = 0; i < PT_MENU_N; i++) {
                CRect w = office_menu_word(&L.menubar, PT_MENU, i);
                if (crect_contains(&w, px, py) && i != p->menu_open) {
                    pt_open_menu(win, p, i);
                    return;
                }
            }
        }
        {
            int hit = ui_menu_hit(&p->menu, p->menu_x, p->menu_y,
                                  GFX_FONT_SYSTEM, px, py);
            if (hit != p->menu.highlight) {
                int was = p->menu.highlight;
                p->menu.highlight = hit;
                ui_menu_repaint(win, &p->menu, p->menu_x, p->menu_y,
                                GFX_FONT_SYSTEM, was, hit);
            }
        }
        return;
    }

    for (i = 0; i < PT_TOOL_N; i++) {
        if (crect_contains(&L.tool[i], px, py)) { hot = i; break; }
    }
    if (hot != p->hot_tool) { p->hot_tool = hot; wm_invalidate(win, NULL); }

    p->over = pt_canvas_xy(&L, p, px, py, &cx, &cy);
    if (p->over) {
        if (cx != p->mx || cy != p->my) { wm_invalidate(win, NULL); }
        p->mx = cx; p->my = cy;
    }
    if (!p->drawing && !p->dragging) { return; }

    /* Clamp so a drag that leaves the image still draws up to its edge. */
    if (cx < 0) { cx = 0; }
    if (cx >= p->canvas->w) { cx = p->canvas->w - 1; }
    if (cy < 0) { cy = 0; }
    if (cy >= p->canvas->h) { cy = p->canvas->h - 1; }

    if (p->drawing) {
        if (p->tool == PT_AIR) {
            pc_spray(p->canvas, cx, cy, PT_SPRAY_R[p->opt], 6 + p->opt * 4,
                     pt_ink(p), &p->seed);
        } else {
            pc_stroke(p->canvas, p->x1, p->y1, cx, cy, pt_width(p), pt_ink(p));
        }
    }
    p->x1 = cx; p->y1 = cy;
    wm_invalidate(win, NULL);
}

static void pt_release(WmWindow *win)
{
    Paint *p = (Paint *)wm_user(win);
    if (p == NULL) { return; }
    if (p->dragging) {
        pt_commit(p);
        p->dragging = CFALSE;
        wm_invalidate(win, NULL);
    }
    p->drawing = CFALSE;
}

static cbool pt_key(WmWindow *win, int key, int ch, int mods)
{
    Paint *p = (Paint *)wm_user(win);
    if (p == NULL) { return CFALSE; }
    /*
     * The menu bar, from the keyboard: F10 opens it, the arrows walk it,
     * Enter chooses, Esc closes. These menus were mouse-only, and on the
     * target the mouse is a driver somebody may not have loaded.
     */
    {
        OfficeMenuNav nav;
        PtLayout ML;
        int mcmd = -1;
        pt_layout(win, p, &ML);
        nav.open = &p->menu_open; nav.x = &p->menu_x; nav.y = &p->menu_y;
        nav.menu = &p->menu; nav.names = PT_MENU; nav.count = PT_MENU_N;
        nav.bar = ML.menubar; nav.build = pt_menu_build_cb; nav.user = p;
        if (office_menu_key_win(win, &nav, key, &mcmd)) {
            if (mcmd >= 0) { pt_command(win, p, mcmd); wm_invalidate(win, NULL); }
            return CTRUE;
        }
    }
    if (PT_CTRL(key, ch, mods, 26, 'z')) { pt_command(win, p, MC_UNDO); return CTRUE; }
    if (PT_CTRL(key, ch, mods, 25, 'y')) { pt_command(win, p, MC_REDO); return CTRUE; }
    if (PT_CTRL(key, ch, mods, 24, 'x')) { pt_command(win, p, MC_CUT);   return CTRUE; }
    if (PT_CTRL(key, ch, mods,  3, 'c')) { pt_command(win, p, MC_COPY);  return CTRUE; }
    if (PT_CTRL(key, ch, mods, 22, 'v')) { pt_command(win, p, MC_PASTE); return CTRUE; }
    if (PT_CTRL(key, ch, mods, 19, 's')) { pt_command(win, p, MC_SAVE); return CTRUE; }
    if (PT_CTRL(key, ch, mods, 15, 'o')) { pt_command(win, p, MC_OPEN); return CTRUE; }
    if (PT_CTRL(key, ch, mods, 14, 'n')) { pt_command(win, p, MC_NEW);  return CTRUE; }
    /* 1..9,0 pick a tool, the way a paint program's number row always has. */
    if (ch >= '1' && ch <= '9') { p->tool = ch - '1'; }
    else if (ch == '0') { p->tool = PT_TEXT; }
    else if (ch == 's' || ch == 'S') { p->tool = PT_SELECT; }
    else { return CFALSE; }
    sys_snprintf(p->status, sizeof p->status, "%s", PT_NAME[p->tool]);
    wm_invalidate(win, NULL);
    return CTRUE;
}

static cbool paint_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    switch (msg) {
    case WM_MSG_CLOSE:       return pt_on_close(win);
    case WM_MSG_QUERY_UNSAVED: return pt_has_unsaved(win);
    case WM_MSG_PAINT:       pt_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: pt_click(win, (int)a, (int)b);      return CTRUE;
    case WM_MSG_RBUTTONDOWN: pt_rclick(win, (int)a, (int)b);     return CTRUE;
    case WM_MSG_MOUSEMOVE:   pt_motion(win, (int)a, (int)b);     return CTRUE;
    case WM_MSG_LBUTTONUP:   pt_release(win);                    return CTRUE;
    case WM_MSG_KEYDOWN:
        return pt_key(win, (int)a, (int)b,
                      (param != NULL) ? *(const int *)param : 0);
    case WM_MSG_DESTROY: {
        Paint *p = (Paint *)wm_user(win);
        if (p != NULL) {
            pc_undo_free(&p->undo);
            if (p->clip != NULL) { gfx_surface_free(p->clip); }
            if (p->canvas != NULL) { gfx_surface_free(p->canvas); }
            sys_free(p, (cu32)sizeof(Paint));
        }
        return CTRUE;
    }
    default: return CFALSE;
    }
}

static WmWindow *pt_open_window(void)
{
    Paint *p;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int fw, fh, fx, fy;

    p = (Paint *)sys_calloc(1, (cu32)sizeof(Paint));
    if (p == NULL) { SYS_LOGE("app", "paint: OOM"); return NULL; }
    p->canvas = gfx_surface_new(PT_CANVAS_W, PT_CANVAS_H);
    if (p->canvas == NULL) { sys_free(p, (cu32)sizeof(Paint)); return NULL; }
    pc_undo_init(&p->undo);
    p->fg = GFX_RGB(0x00, 0x00, 0x00);
    p->bg = GFX_RGB(0xFF, 0xFF, 0xFF);
    p->tool = PT_PENCIL;
    p->opt = 0;
    p->style = PC_OUTLINE;
    p->seed = 0x5EED1234UL;
    p->hot_tool = -1;
    p->menu_open = -1;
    pt_clear(p);
    p->saved_sum = pt_doc_sum(p);   /* a blank canvas is not unsaved work */
    sys_strlcpy(p->status, "Pick a tool and draw", sizeof p->status);

    plat_video_info(&vi);
    fw = PT_CANVAS_W + PT_BOX_W + 22;
    fh = PT_CANVAS_H + OF_MENUBAR_H + PT_PAL_H + OF_STATUS_H + 36;
    if (fw > vi.width)  { fw = vi.width; }
    if (fh > vi.height) { fh = vi.height; }
    fx = (vi.width - fw) / 2;
    fy = (vi.height - fh) / 2 - 10;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, fw, fh);

    w = wm_create("CastaliaPaint", &frame, WM_STYLE_APP, paint_proc, p);
    if (w == NULL) {
        pc_undo_free(&p->undo);
        gfx_surface_free(p->canvas);
        sys_free(p, (cu32)sizeof(Paint));
        return NULL;
    }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened CastaliaPaint (%dx%d canvas)",
             PT_CANVAS_W, PT_CANVAS_H);
    return w;
}

void app_paint_open(void) { (void)pt_open_window(); }

/* Open Paint on a bitmap -- what a double-click on a .BMP does. */
void app_paint_open_file(const char *path)
{
    WmWindow *w = pt_open_window();
    if (w != NULL && path != NULL && path[0] != '\0') {
        pt_on_open(CTRUE, path, w);
    }
}

/*
 * Menu geometry, for --menuaccel-demo (see apps.h). The scene needs to click
 * a menu-bar word and then look at the drop-down's pixels; both answers come
 * from here so there is no second copy of the layout to drift out of step.
 */
CRect app_paint_menu_word(WmWindow *win, int idx)
{
    CRect c, bar;
    if (win == NULL) { return crect_make(0, 0, 0, 0); }
    c = wm_client_rect(win);
    bar = crect_make(0, 0, crect_w(&c), OF_MENUBAR_H);
    return office_menu_word(&bar, PT_MENU, idx);
}

int app_paint_menu_width(WmWindow *win)
{
    Paint *p = (win != NULL) ? (Paint *)wm_user(win) : NULL;
    int w = 0, h = 0;
    if (p == NULL || p->menu_open < 0) { return 0; }
    ui_menu_measure(&p->menu, GFX_FONT_SYSTEM, &w, &h);
    return w;
}
