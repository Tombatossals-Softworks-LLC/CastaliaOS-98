/*
 * app_logview.c - CastaliaOS Log Viewer.
 *
 * Tails the rotating session log (the same file `sys_log` writes, located via
 * sys_log_path()) in a scrollable, read-only window. Severity is colored so a
 * problem stands out at a glance: ERROR/FATAL in red, WARN in amber. The whole
 * file is streamed through the platform file API (so it works the same on the
 * host and on DOS) and only the last LV_MAX_LINES are kept in a ring buffer,
 * bounding memory regardless of log size.
 *
 * The window owns a heap payload and frees it on WM_MSG_DESTROY.
 */
#include "apps.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"
#include "ring_core.h"

#define LV_MAX_LINES  300
#define LV_LINE_MAX   140
#define LV_TOOLBAR_H  26
#define LV_PATHBAR_H  18
#define LV_STATUS_H   16
#define LV_ROW_H      12
#define LV_READ_CHUNK 1024

typedef struct {
    char lines[LV_MAX_LINES][LV_LINE_MAX];
    Ring ring;    /* which of those slots hold something, and in what order */
    int  top;     /* first visible line (scroll offset)           */
    int  visible; /* rows that fit in the content well            */
    int  hot_sb;  /* active scroll-bar part (UI_SB_*)              */
    cbool sb_drag;
    char path[CASTALIA_MAX_PATH];
    char status[64];
    UiHot hot;    /* toolbar button under the pointer */
} LogView;

/* ---- ring buffer ----------------------------------------------------- *
 *
 * The wrapping lives in ring_core.c now. This file used to keep `start` and
 * `count` and spell the read as `(start + i) % LV_MAX_LINES`, which is
 * correct -- and was one of four different correct spellings of the same
 * thing in this tree. See ring_core.h.
 */
static void lv_reset(LogView *lv)
{
    ring_init(&lv->ring, LV_MAX_LINES);
    lv->top = 0;
}

static void lv_push(LogView *lv, const char *line)
{
    sys_strlcpy(lv->lines[ring_push(&lv->ring)], line, LV_LINE_MAX);
}

/* The i-th kept line (0 == oldest), or "" out of range. */
static const char *lv_line(const LogView *lv, int i)
{
    int at = ring_at(&lv->ring, i);
    return (at >= 0) ? lv->lines[at] : "";
}

static int lv_count(const LogView *lv) { return ring_count(&lv->ring); }

/* ---- load ------------------------------------------------------------ */
static void lv_load(LogView *lv)
{
    PlatFile *f;
    char      chunk[LV_READ_CHUNK];
    char      cur[LV_LINE_MAX];
    int       cl = 0;
    cu32      n;

    lv_reset(lv);
    sys_log_flush(); /* make sure our own recent records are on disk */

    if (lv->path[0] == '\0') {
        lv_push(lv, "(no log file is configured -- logging to stderr only)");
    } else {
        f = plat_fopen(lv->path, "rb");
        if (f == NULL) {
            lv_push(lv, "(could not open the log file)");
        } else {
            while ((n = plat_fread(f, chunk, sizeof(chunk))) > 0) {
                cu32 i;
                for (i = 0; i < n; i++) {
                    char c = chunk[i];
                    if (c == '\n' || c == '\r') {
                        if (c == '\r') { continue; } /* fold CRLF */
                        cur[cl] = '\0';
                        lv_push(lv, cur);
                        cl = 0;
                    } else if (cl < LV_LINE_MAX - 1) {
                        cur[cl++] = c;
                    }
                    /* characters past the line cap are dropped (bounded) */
                }
            }
            if (cl > 0) { cur[cl] = '\0'; lv_push(lv, cur); } /* trailing */
            plat_fclose(f);
        }
        if (lv_count(lv) == 0) { lv_push(lv, "(the log file is empty)"); }
    }

    sys_snprintf(lv->status, sizeof(lv->status), "%d line%s", lv_count(lv),
                 (lv_count(lv) == 1) ? "" : "s");
}

/* Does 'ln' begin with 'prefix'? (No <string.h> dependency.) */
static cbool lv_starts(const char *ln, const char *prefix)
{
    while (*prefix != '\0') {
        if (*ln != *prefix) { return CFALSE; }
        ln++; prefix++;
    }
    return CTRUE;
}

/* Colour a line by its severity tag ("[ERROR" etc. at column 0). */
static CColor lv_line_color(const char *ln, const UiPalette *p)
{
    if (ln[0] == '[') {
        if (lv_starts(ln, "[ERROR") || lv_starts(ln, "[FATAL")) {
            return GFX_RGB(0xC0, 0x20, 0x20);
        }
        if (lv_starts(ln, "[WARN")) { return GFX_RGB(0xB0, 0x70, 0x00); }
    }
    return p->text;
}

/* ---- layout ---------------------------------------------------------- */
enum { LB_REFRESH, LB_TOP, LB_BOTTOM, LB_COUNT };
static const char *LB_LABEL[LB_COUNT] = { "Refresh", "Top", "Bottom" };
static const int   LB_WIDTH[LB_COUNT] = { 58, 40, 52 };

typedef struct { CRect toolbar, well, vbar, status, btn[LB_COUNT]; } LvLayout;

static void lv_layout(WmWindow *win, LogView *lv, LvLayout *L)
{
    CRect c = wm_client_rect(win);
    int   cw = crect_w(&c), ch = crect_h(&c);
    int   x = 4, i, well_h;
    L->toolbar = crect_make(0, 0, cw, LV_TOOLBAR_H);
    for (i = 0; i < LB_COUNT; i++) {
        L->btn[i] = crect_make(x, 3, LB_WIDTH[i], LV_TOOLBAR_H - 6);
        x += LB_WIDTH[i] + 3;
    }
    well_h = ch - LV_TOOLBAR_H - LV_PATHBAR_H - LV_STATUS_H;
    if (well_h < LV_ROW_H) { well_h = LV_ROW_H; }
    L->well   = crect_make(0, LV_TOOLBAR_H + LV_PATHBAR_H, cw - UI_SB_W, well_h);
    L->vbar   = crect_make(cw - UI_SB_W, LV_TOOLBAR_H + LV_PATHBAR_H,
                           UI_SB_W, well_h);
    L->status = crect_make(0, ch - LV_STATUS_H, cw, LV_STATUS_H);
    lv->visible = well_h / LV_ROW_H;
}

/*
 * What a scroll changed: the well the lines are in, and the bar whose thumb
 * moved with them. Screen coordinates, because that is what wm_invalidate
 * takes.
 */
static void lv_invalidate_scroll(WmWindow *win, const LvLayout *L)
{
    CPoint o = wm_client_origin(win);
    CRect r;
    r = crect_offset(&L->well, o.x, o.y);
    wm_invalidate(win, &r);
    r = crect_offset(&L->vbar, o.x, o.y);
    wm_invalidate(win, &r);
}

/* Clamp the scroll offset into range. */
static void lv_clamp(LogView *lv)
{
    int max = lv_count(lv) - lv->visible;
    if (max < 0) { max = 0; }
    if (lv->top > max) { lv->top = max; }
    if (lv->top < 0) { lv->top = 0; }
}

static void lv_to_bottom(LogView *lv) { lv->top = lv_count(lv); lv_clamp(lv); }

/* ---- paint ----------------------------------------------------------- */
static void lv_paint(WmWindow *win, GfxSurface *s)
{
    LogView *lv = (LogView *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    LvLayout L;
    CRect r;
    CColor white = GFX_RGB(0xFF, 0xFF, 0xFF);
    int i;

    if (lv == NULL) { return; }
    lv_layout(win, lv, &L);
    lv_clamp(lv);

    /* Toolbar. */
    r = crect_offset(&L.toolbar, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    for (i = 0; i < LB_COUNT; i++) {
        CRect br = crect_offset(&L.btn[i], o.x, o.y);
        ui_draw_button(s, &br, LB_LABEL[i],
                       ui_hot_state(&lv->hot, i, UI_BTN_NORMAL));
    }

    /* Path bar: which file we are tailing. */
    r = crect_make(o.x, o.y + LV_TOOLBAR_H, crect_w(&L.toolbar), LV_PATHBAR_H);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);
    {
        /* Elided from the LEFT when it does not fit -- a deep path loses its
         * root, not the file it is actually tailing. This used to draw the
         * raw string, which simply ran off the right edge with nothing to say
         * it had been cut. */
        char shown[CASTALIA_MAX_PATH];
        ui_path_fit(shown, sizeof shown,
                    (lv->path[0] ? lv->path : "(stderr only)"),
                    crect_w(&r) - 8, GFX_FONT_SYSTEM);
        gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 4, r.y0 + 4, shown, p->text);
    }

    /* Content well. */
    r = crect_offset(&L.well, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);
    {
        int tx = r.x0 + 5;
        int ty = r.y0 + 2;
        for (i = 0; i < lv->visible; i++) {
            int li = lv->top + i;
            const char *ln;
            if (li >= lv_count(lv)) { break; }
            ln = lv_line(lv, li);
            gfx_draw_text(s, GFX_FONT_SYSTEM, tx, ty + i * LV_ROW_H, ln,
                          lv_line_color(ln, p));
        }
    }

    /* Scrollbar when the log overflows the well. */
    if (lv_count(lv) > lv->visible && lv->visible > 0) {
        int track_x = r.x1 - 8, track_y = r.y0 + 1, track_h = crect_h(&r) - 2;
        int thumb_h = (lv->visible * track_h) / lv_count(lv);
        int thumb_y;
        CRect track, thumb;
        if (thumb_h < 8) { thumb_h = 8; }
        thumb_y = track_y + (lv->top * (track_h - thumb_h)) /
                  (lv_count(lv) - lv->visible);
        track = crect_make(track_x, track_y, 7, track_h);
        gfx_fill_rect(s, &track, p->face);
        thumb = crect_make(track_x, thumb_y, 7, thumb_h);
        gfx_bevel(s, &thumb, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    }

    /* Status bar. */
    /* The log's scroll bar: a long tail is exactly where you need to see
     * where you are. */
    r = crect_offset(&L.vbar, o.x, o.y);
    ui_scrollbar_draw(s, &r, CTRUE, lv_count(lv), lv->visible, lv->top,
                      lv->hot_sb);

    r = crect_offset(&L.status, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, p->face);
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 6, r.y0 + 4, lv->status, p->text);
}

/* ---- input ----------------------------------------------------------- */
static void lv_action(WmWindow *win, LogView *lv, int id)
{
    switch (id) {
    case LB_REFRESH: lv_load(lv); lv_to_bottom(lv); break;
    case LB_TOP:     lv->top = 0; break;
    case LB_BOTTOM:  lv_to_bottom(lv); break;
    default: return;
    }
    wm_invalidate(win, NULL);
}

static cbool lv_click(WmWindow *win, LogView *lv, int x, int y)
{
    LvLayout L;
    int i;
    lv_layout(win, lv, &L);
    if (crect_contains(&L.toolbar, x, y)) {
        for (i = 0; i < LB_COUNT; i++) {
            if (crect_contains(&L.btn[i], x, y)) {
                (void)ui_hot_press(&lv->hot, i);
                lv_action(win, lv, i);
                return CTRUE;
            }
        }
    }
    /* Click in the well: page toward the click (above thumb = up, below = down). */
    if (crect_contains(&L.vbar, x, y)) {
        int part = ui_scrollbar_hit(&L.vbar, CTRUE, lv_count(lv), lv->visible,
                                    lv->top, x, y);
        lv->hot_sb = part;
        switch (part) {
        case UI_SB_LINE_UP:   lv->top--; break;
        case UI_SB_LINE_DOWN: lv->top++; break;
        case UI_SB_PAGE_UP:   lv->top -= lv->visible; break;
        case UI_SB_PAGE_DOWN: lv->top += lv->visible; break;
        case UI_SB_THUMB:     lv->sb_drag = CTRUE; break;
        default: break;
        }
        lv_clamp(lv);
        /* The lines and the bar. Clicking an arrow is the same gesture as
         * dragging the thumb -- people hold it down -- and it was the half
         * of the scroll path still repainting the window. */
        lv_invalidate_scroll(win, &L);
        return CTRUE;
    }
    if (crect_contains(&L.well, x, y)) {
        int mid = L.well.y0 + crect_h(&L.well) / 2;
        lv->top += (y < mid) ? -lv->visible : lv->visible;
        lv_clamp(lv);
        /* Clicking the well pages the view, which is a scroll like any
         * other: the lines and the bar, not the window. */
        lv_invalidate_scroll(win, &L);
        return CTRUE;
    }
    return CFALSE;
}

static cbool lv_key(WmWindow *win, LogView *lv, int key)
{
    int before = lv->top;
    switch (key) {
    case PLAT_KEY_UP:    lv->top -= 1; break;
    case PLAT_KEY_DOWN:  lv->top += 1; break;
    case PLAT_KEY_PGUP:  lv->top -= lv->visible; break;
    case PLAT_KEY_PGDN:  lv->top += lv->visible; break;
    case PLAT_KEY_HOME:  lv->top = 0; break;
    case PLAT_KEY_END:   lv_to_bottom(lv); break;
    case 'r': case 'R':  lv_action(win, lv, LB_REFRESH); return CTRUE;
    default: return CFALSE;
    }
    lv_clamp(lv);
    if (lv->top != before) { wm_invalidate(win, NULL); }
    return CTRUE;
}

static cbool logview_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    LogView *lv = (LogView *)wm_user(win);
    CASTALIA_UNUSED(b);
    switch (msg) {
    case WM_MSG_PAINT:      lv_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN:return lv_click(win, lv, (int)a, (int)b);
    case WM_MSG_LBUTTONUP:
        if (lv->sb_drag || lv->hot_sb != UI_SB_NONE) {
            LvLayout L;
            CPoint o = wm_client_origin(win);
            CRect r;
            lv->sb_drag = CFALSE;
            lv->hot_sb = UI_SB_NONE;
            /* The bar goes from held back to idle. Nothing else moved --
             * and this was the frame that made a scroll-arrow CLICK still
             * cost 110% of the window after the click itself had been
             * narrowed: the release is a separate repaint, and it was the
             * one being measured. */
            lv_layout(win, lv, &L);
            r = crect_offset(&L.vbar, o.x, o.y);
            wm_invalidate(win, &r);
        }
        if (ui_hot_release(&lv->hot)) {
            LvLayout L;
            lv_layout(win, lv, &L);
            ui_hot_repaint(win, &lv->hot, L.btn, LB_COUNT);
        }
        return CTRUE;
    case WM_MSG_MOUSELEAVE: {
        cbool redraw = ui_hot_release(&lv->hot);
        if (ui_hot_move(&lv->hot, -1)) { redraw = CTRUE; }
        if (redraw) {
            LvLayout L;
            lv_layout(win, lv, &L);
            ui_hot_repaint(win, &lv->hot, L.btn, LB_COUNT);
        }
        return CTRUE;
    }
    case WM_MSG_MOUSEMOVE:
        if (lv->sb_drag) {
            LvLayout L;
            int was = lv->top;
            lv_layout(win, lv, &L);
            lv->top = ui_scroll_pos_from_coord(crect_h(&L.vbar), lv_count(lv),
                                               lv->visible, (int)b - L.vbar.y0);
            lv_clamp(lv);
            /*
             * On the TRANSITION, and only over what moved.
             *
             * This repainted the whole window on every motion event for as
             * long as the thumb was held -- 110% of the client, per event.
             * Two things were wrong with that and both are fixed here: the
             * toolbar, the path bar and the status line never move when the
             * lines scroll, and a pointer that slid a pixel without landing
             * on a new row changed nothing at all.
             */
            if (lv->top != was) { lv_invalidate_scroll(win, &L); }
            return CTRUE;
        }
        {
            LvLayout L;
            int i, hit = -1;
            lv_layout(win, lv, &L);
            for (i = 0; i < LB_COUNT; i++) {
                if (crect_contains(&L.btn[i], (int)a, (int)b)) { hit = i; }
            }
            if (ui_hot_move(&lv->hot, hit)) {
                ui_hot_repaint(win, &lv->hot, L.btn, LB_COUNT);
            }
        }
        return CFALSE;
    case WM_MSG_MOUSEWHEEL: {
        LvLayout L;
        lv_layout(win, lv, &L);
        if (ui_scroll_wheel(&lv->top, (int)a, lv_count(lv), lv->visible)) {
            lv_invalidate_scroll(win, &L);
        }
        return CTRUE;
    }
    case WM_MSG_KEYDOWN:    return lv_key(win, lv, (int)a);
    case WM_MSG_DESTROY:
        if (lv != NULL) { sys_free(lv, (cu32)sizeof(LogView)); }
        return CTRUE;
    default: return CFALSE;
    }
}

void app_logview_open(void)
{
    LogView *lv;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int cw = 560, ch = 360, fx, fy;

    lv = (LogView *)sys_calloc(1, (cu32)sizeof(LogView));
    if (lv == NULL) { SYS_LOGE("app", "logview: OOM"); return; }
    sys_strlcpy(lv->path, sys_log_path(), sizeof(lv->path));
    lv_load(lv);
    lv_to_bottom(lv);

    plat_video_info(&vi);
    fx = (vi.width - cw) / 2 + 20;
    fy = (vi.height - ch) / 2 + 20;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, cw, ch);

    w = wm_create("Log Viewer", &frame, WM_STYLE_APP, logview_proc, lv);
    if (w == NULL) { sys_free(lv, (cu32)sizeof(LogView)); return; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened Log Viewer (%d lines)", lv_count(lv));
}

/*
 * The vertical scroll bar, in SCREEN coordinates, for the headless driver.
 *
 * A scene that grabs a scroll thumb at a hard-coded offset stops grabbing it
 * the moment the toolbar grows a button -- and it does not fail, it drags
 * nothing and reports a very cheap frame. The rectangle comes from the same
 * layout the window draws from.
 */
cbool app_logview_vbar(WmWindow *win, CRect *out)
{
    LogView *lv = (win != NULL) ? (LogView *)wm_user(win) : NULL;
    LvLayout L;
    CPoint o;
    if (lv == NULL || out == NULL) { return CFALSE; }
    lv_layout(win, lv, &L);
    o = wm_client_origin(win);
    *out = crect_offset(&L.vbar, o.x, o.y);
    return CTRUE;
}

/*
 * ...and the well the lines are in, which is what a scroll is ALLOWED to
 * repaint. Together with the bar it is the whole of what a scroll changes,
 * and a scene can then hold the drag to that rather than to a percentage
 * somebody picked.
 */
/* The first visible line, for the headless driver: the one number that says
 * whether a scroll happened and by how much. */
int app_logview_top(WmWindow *win)
{
    LogView *lv = (win != NULL) ? (LogView *)wm_user(win) : NULL;
    return (lv != NULL) ? lv->top : -1;
}

cbool app_logview_well(WmWindow *win, CRect *out)
{
    LogView *lv = (win != NULL) ? (LogView *)wm_user(win) : NULL;
    LvLayout L;
    CPoint o;
    if (lv == NULL || out == NULL) { return CFALSE; }
    lv_layout(win, lv, &L);
    o = wm_client_origin(win);
    *out = crect_offset(&L.well, o.x, o.y);
    return CTRUE;
}
