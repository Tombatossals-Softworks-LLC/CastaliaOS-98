/*
 * app_taskman.c - CastaliaOS Task Manager.
 *
 * A live list of the open top-level windows with their state and the focused
 * marker, plus a memory gauge (live vs. the 8 MB footprint budget) and session
 * uptime. You can Activate a window (raise + focus, restoring it if minimized)
 * or End Task (force-close it). Fully keyboard-driven -- arrows select, Enter
 * activates, Delete ends -- because the target hardware treats the mouse as
 * optional. The window list is rebuilt from the window manager each time, so it
 * always reflects reality (and never lists the Task Manager itself).
 */
#include "apps.h"
#include "hist_core.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#define TM_TOOLBAR_H 26
#define TM_HEADER_H  16
#define TM_INFO_H    46
#define TM_ROW_H     13
#define TM_MEM_BUDGET (8L * 1024L * 1024L)  /* the Bible's 8 MB idle budget */
#define TM_GRAPH_H   52     /* the two live graphs                        */
#define TM_SAMPLE_MS 500    /* one sample every half second               */

typedef struct {
    WmWindow *self;   /* our own window, excluded from the list */
    int       sel;    /* selected row in the filtered list */
    char      status[80];
    /* Live history for the Performance panel (see hist_core.h). */
    Hist      mem;    /* kilobytes in use                             */
    Hist      frame;  /* milliseconds between frames                  */
    cu32      last_sample_ms;
    cu32      frames_since;     /* frames counted toward the next sample    */
    UiHot hot;    /* toolbar button under the pointer */
} TaskMan;

/* ---- window list ----------------------------------------------------- */
/* Fill 'out' with the listable windows (top-level, titled, not popup/modal,
 * not ourselves). Returns the count. */
static int tm_list(TaskMan *tm, WmWindow **out, int cap)
{
    int i, n = 0;
    int cnt = wm_window_count();
    for (i = 0; i < cnt && n < cap; i++) {
        WmWindow *w = wm_window_at(i);
        unsigned st;
        if (w == NULL || w == tm->self) { continue; }
        st = wm_style(w);
        if (st & (WM_STYLE_POPUP | WM_STYLE_MODAL)) { continue; }
        if (!(st & WM_STYLE_TITLE)) { continue; }
        out[n++] = w;
    }
    return n;
}

static const char *tm_state_str(WmWindow *w)
{
    switch (wm_state(w)) {
    case WM_STATE_MINIMIZED: return "Minimized";
    case WM_STATE_MAXIMIZED: return "Maximized";
    default:                 return "Running";
    }
}

/* ---- layout ---------------------------------------------------------- */
enum { TB_REFRESH, TB_ACTIVATE, TB_END, TB_COUNT };
static const char *TB_LABEL[TB_COUNT] = { "Refresh", "Activate", "End Task" };
static const int   TB_WIDTH[TB_COUNT] = { 58, 64, 64 };

typedef struct {
    CRect toolbar, header, list, info, btn[TB_COUNT];
    CRect g_mem, g_frame;
    int   visible;
} TmLayout;

static void tm_layout(WmWindow *win, TmLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c);
    int x = 4, i, list_h;
    L->toolbar = crect_make(0, 0, cw, TM_TOOLBAR_H);
    for (i = 0; i < TB_COUNT; i++) {
        L->btn[i] = crect_make(x, 3, TB_WIDTH[i], TM_TOOLBAR_H - 6);
        x += TB_WIDTH[i] + 3;
    }
    L->header = crect_make(0, TM_TOOLBAR_H, cw, TM_HEADER_H);
    list_h = ch - TM_TOOLBAR_H - TM_HEADER_H - TM_INFO_H - TM_GRAPH_H;
    if (list_h < TM_ROW_H) { list_h = TM_ROW_H; }
    L->list = crect_make(0, TM_TOOLBAR_H + TM_HEADER_H, cw, list_h);
    {
        int gy = TM_TOOLBAR_H + TM_HEADER_H + list_h;
        int half = (cw - 12) / 2;
        L->g_mem   = crect_make(4, gy + 2, half, TM_GRAPH_H - 6);
        L->g_frame = crect_make(8 + half, gy + 2, cw - 12 - half,
                                TM_GRAPH_H - 6);
    }
    L->info = crect_make(0, ch - TM_INFO_H, cw, TM_INFO_H);
    L->visible = list_h / TM_ROW_H;
}

/* ---- live graphs ------------------------------------------------------- */
/* The memory gauge's rectangle inside the info panel. One definition, used by
 * the painter and by the headless driver's hook -- a hook that computed the
 * rect a second time could agree with a painter that had moved. */
static CRect tm_mem_bar(const CRect *info)
{
    return crect_make(info->x0 + 6, info->y0 + 6, crect_w(info) - 12, 10);
}

/* One history plotted as a filled area with a grid, newest at the right --
 * the shape every resource meter of the era had. */
static void tm_graph(GfxSurface *s, const CRect *r, const Hist *h,
                     const char *label, long floor_scale, const char *unit,
                     CColor ink)
{
    const UiPalette *p = ui_palette();
    CColor bg = GFX_RGB(0x08, 0x1C, 0x12);
    CColor grid = GFX_RGB(0x18, 0x3C, 0x28);
    CRect in;
    char t[48];
    long scale;
    int i, n, w, hgt, x;

    gfx_bevel(s, r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, GFX_NO_FILL);
    in = crect_inset(r, 2);
    gfx_fill_rect(s, &in, bg);
    w = crect_w(&in);
    hgt = crect_h(&in) - 9;          /* the caption sits along the bottom */
    if (hgt < 4 || w < 8) { return; }

    for (i = 1; i < 4; i++) {
        gfx_hline(s, in.x0, in.y0 + (hgt * i) / 4, w, grid);
    }
    scale = hist_scale(hist_max(h), floor_scale);
    n = hist_count(h);
    if (n > w) { n = w; }
    for (i = 0; i < n; i++) {
        /* Right-aligned: the newest sample is always against the right edge. */
        long v = hist_at(h, hist_count(h) - n + i);
        int bar = hist_bar(v, scale, hgt);
        x = in.x1 - n + i;
        if (bar > 0) { gfx_vline(s, x, in.y0 + hgt - bar, bar, ink); }
    }
    sys_snprintf(t, sizeof(t), "%s %ld%s", label, hist_newest(h), unit);
    gfx_draw_text(s, GFX_FONT_SYSTEM, in.x0 + 2, in.y1 - 9, t,
                  GFX_RGB(0x46, 0xF0, 0x74));
}

/* ---- paint ----------------------------------------------------------- */
static void tm_paint(WmWindow *win, GfxSurface *s)
{
    TaskMan *tm = (TaskMan *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    CColor white = GFX_RGB(0xFF, 0xFF, 0xFF);
    WmWindow *list[CASTALIA_MAX_WINDOWS];
    TmLayout L;
    CRect r;
    int i, n;
    WmWindow *focused = wm_focused();

    if (tm == NULL) { return; }
    tm_layout(win, &L);
    n = tm_list(tm, list, CASTALIA_MAX_WINDOWS);
    if (tm->sel >= n) { tm->sel = n - 1; }
    if (tm->sel < 0)  { tm->sel = 0; }

    /* Toolbar. */
    r = crect_offset(&L.toolbar, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    for (i = 0; i < TB_COUNT; i++) {
        CRect br = crect_offset(&L.btn[i], o.x, o.y);
        cbool dis = (i != TB_REFRESH) && (n == 0);
        ui_draw_button(s, &br, TB_LABEL[i],
                       ui_hot_state(&tm->hot, i,
                                    dis ? UI_BTN_DISABLED : UI_BTN_NORMAL));
    }

    /* Header. */
    r = crect_offset(&L.header, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 6, r.y0 + 4, "Task", p->text);
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x1 - 84, r.y0 + 4, "Status", p->text);

    /* List well. */
    r = crect_offset(&L.list, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);
    for (i = 0; i < L.visible && i < n; i++) {
        WmWindow *w = list[i];
        int ry = r.y0 + 2 + i * TM_ROW_H;
        CColor tcol = p->text;
        char line[CASTALIA_MAX_NAME + 8];
        if (i == tm->sel) {
            CRect hl = crect_make(r.x0 + 1, ry - 1, crect_w(&r) - 2, TM_ROW_H);
            gfx_fill_rect(s, &hl, p->accent);
            tcol = p->accent_text;
        }
        sys_snprintf(line, sizeof(line), "%s%s",
                     (w == focused) ? "> " : "  ", wm_title(w));
        gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 5, ry, line, tcol);
        gfx_draw_text(s, GFX_FONT_SYSTEM, r.x1 - 84, ry, tm_state_str(w), tcol);
    }
    if (n == 0) {
        gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 6, r.y0 + 4,
                      "(no other windows are open)", p->text_disabled);
    }

    /* Info panel: memory gauge + counts + uptime. */
    r = crect_offset(&L.info, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    {
        long live = (long)sys_mem_live_bytes();
        long peak = (long)sys_mem_peak_bytes();
        cu32 up = sys_now_ms() / 1000u;
        char t[80];
        CRect bar;

        /* Memory gauge (live vs. the 8 MB budget). */
        /* The gauge this window exists for. It used to compute its own
         * fill as (live * width) / budget, which overflows 32 bits at 5.31 MB
         * of an 8 MB budget -- so it read EMPTY for the top third of its own
         * range, on a 32-bit long. See ui_meter_fill. */
        bar = tm_mem_bar(&r);
        ui_draw_meter(s, &bar, (cs32)live, (cs32)TM_MEM_BUDGET, p->accent);

        sys_snprintf(t, sizeof(t),
                     "Memory %ld.%02ld / 8 MB   %lu block(s)",
                     live / (1024L * 1024L),
                     ((live % (1024L * 1024L)) * 100L) / (1024L * 1024L),
                     (unsigned long)sys_mem_alloc_count());
        gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 6, r.y0 + 20, t, p->text);
        (void)peak;

        sys_snprintf(t, sizeof(t), "%d task(s)   Uptime %lu:%02lu:%02lu",
                     n, (unsigned long)(up / 3600u),
                     (unsigned long)((up / 60u) % 60u),
                     (unsigned long)(up % 60u));
        gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 6, r.y0 + 32, t, p->text);
    }

    /* Performance: memory in use and how long each frame is taking. */
    r = crect_offset(&L.g_mem, o.x, o.y);
    tm_graph(s, &r, &tm->mem, "Memory", 1024L, " KB",
             GFX_RGB(0x2E, 0xC8, 0x5A));
    r = crect_offset(&L.g_frame, o.x, o.y);
    tm_graph(s, &r, &tm->frame, "Frame", 20L, " ms/f",
             GFX_RGB(0xE0, 0xB0, 0x30));

    if (tm->status[0] != '\0') {
        r = crect_offset(&L.toolbar, o.x, o.y);
        gfx_draw_text(s, GFX_FONT_SYSTEM,
                      L.btn[TB_COUNT - 1].x1 + o.x + 8, r.y0 + 8,
                      tm->status, p->text);
    }
}

/* ---- actions --------------------------------------------------------- */
static void tm_activate(WmWindow *win, TaskMan *tm)
{
    WmWindow *list[CASTALIA_MAX_WINDOWS];
    int n = tm_list(tm, list, CASTALIA_MAX_WINDOWS);
    WmWindow *w;
    if (tm->sel < 0 || tm->sel >= n) { return; }
    w = list[tm->sel];
    if (!wm_is_visible(w)) { wm_show(w, CTRUE); }
    if (wm_state(w) == WM_STATE_MINIMIZED) { wm_set_state(w, WM_STATE_NORMAL); }
    wm_focus(w);
    wm_bring_to_front(w);
    /* Keep the Task Manager itself visible/in front so the user can keep going. */
    wm_bring_to_front(win);
    wm_focus(win);
    sys_snprintf(tm->status, sizeof(tm->status), "Activated %s", wm_title(w));
    wm_invalidate(win, NULL);
}

static void tm_end(WmWindow *win, TaskMan *tm)
{
    WmWindow *list[CASTALIA_MAX_WINDOWS];
    int n = tm_list(tm, list, CASTALIA_MAX_WINDOWS);
    WmWindow *w;
    if (tm->sel < 0 || tm->sel >= n) { return; }
    w = list[tm->sel];
    sys_snprintf(tm->status, sizeof(tm->status), "Ended %s", wm_title(w));
    wm_destroy(w); /* force-close: frees the app payload via WM_MSG_DESTROY */
    if (tm->sel > 0) { tm->sel--; }
    wm_invalidate(win, NULL);
}

static void tm_button(WmWindow *win, TaskMan *tm, int id)
{
    switch (id) {
    case TB_REFRESH:  tm->status[0] = '\0'; wm_invalidate(win, NULL); break;
    case TB_ACTIVATE: tm_activate(win, tm); break;
    case TB_END:      tm_end(win, tm); break;
    default: break;
    }
}

static cbool tm_click(WmWindow *win, TaskMan *tm, int x, int y)
{
    TmLayout L;
    int i;
    tm_layout(win, &L);
    if (crect_contains(&L.toolbar, x, y)) {
        for (i = 0; i < TB_COUNT; i++) {
            if (crect_contains(&L.btn[i], x, y)) {
                (void)ui_hot_press(&tm->hot, i);
                tm_button(win, tm, i);
                return CTRUE;
            }
        }
    }
    if (crect_contains(&L.list, x, y)) {
        int row = (y - (L.list.y0 + 2)) / TM_ROW_H;
        WmWindow *list[CASTALIA_MAX_WINDOWS];
        int n = tm_list(tm, list, CASTALIA_MAX_WINDOWS);
        if (row >= 0 && row < n) {
            tm->sel = row;
            tm->status[0] = '\0';
            wm_invalidate(win, NULL);
        }
        return CTRUE;
    }
    return CFALSE;
}

static cbool tm_key(WmWindow *win, TaskMan *tm, int key, int ch)
{
    WmWindow *list[CASTALIA_MAX_WINDOWS];
    int n = tm_list(tm, list, CASTALIA_MAX_WINDOWS);
    switch (key) {
    case PLAT_KEY_UP:    if (tm->sel > 0) { tm->sel--; } break;
    case PLAT_KEY_DOWN:  if (tm->sel < n - 1) { tm->sel++; } break;
    case PLAT_KEY_HOME:  tm->sel = 0; break;
    case PLAT_KEY_END:   tm->sel = (n > 0) ? n - 1 : 0; break;
    case PLAT_KEY_ENTER: tm_activate(win, tm); return CTRUE;
    case PLAT_KEY_DELETE: tm_end(win, tm); return CTRUE;
    default:
        if (ch == 'r' || ch == 'R') { tm_button(win, tm, TB_REFRESH); return CTRUE; }
        return CFALSE;
    }
    tm->status[0] = '\0';
    wm_invalidate(win, NULL);
    return CTRUE;
}

/*
 * How many samples the Performance graphs are holding. Exposed for
 * --taskman-demo, which checked the memory GAUGE (fed straight from
 * sys_mem_live_bytes) and never the graphs -- so an uninitialised sample ring
 * would have drawn two empty boxes and passed.
 */
int app_taskman_samples(WmWindow *win)
{
    TaskMan *tm = (TaskMan *)wm_user(win);
    return (tm != NULL) ? hist_count(&tm->mem) : 0;
}

static cbool taskman_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    TaskMan *tm = (TaskMan *)wm_user(win);
    switch (msg) {
    case WM_MSG_TIMER:
        if (tm != NULL) {
            cu32 now = sys_now_ms();
            cu32 span = now - tm->last_sample_ms;
            tm->frames_since++;
            /* Count every frame, but only sample and REPAINT twice a second.
             * A resource meter that costs a repaint per frame to display is
             * measuring itself; this way the window is 2 Hz of work whatever
             * the shell is running at, and each frame-time sample is the mean
             * over that half second rather than one frame's luck. */
            if (tm->last_sample_ms != 0 && span < TM_SAMPLE_MS) {
                return CTRUE;
            }
            if (tm->last_sample_ms != 0 && tm->frames_since > 0) {
                hist_push(&tm->frame, (long)(span / tm->frames_since));
            }
            hist_push(&tm->mem, (long)(sys_mem_live_bytes() / 1024u));
            tm->last_sample_ms = now;
            tm->frames_since = 0;
            wm_invalidate(win, NULL);
        }
        return CTRUE;
    case WM_MSG_PAINT:       tm_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: return tm_click(win, tm, (int)a, (int)b);
    case WM_MSG_MOUSEMOVE: {
        TmLayout L;
        int i, hit = -1;
        if (tm == NULL) { return CFALSE; }
        tm_layout(win, &L);
        for (i = 0; i < TB_COUNT; i++) {
            if (crect_contains(&L.btn[i], (int)a, (int)b)) { hit = i; }
        }
        if (ui_hot_move(&tm->hot, hit)) {
            ui_hot_repaint(win, &tm->hot, L.btn, TB_COUNT);
        }
        return CTRUE;
    }
    case WM_MSG_LBUTTONUP:
    case WM_MSG_MOUSELEAVE: {
        cbool redraw;
        if (tm == NULL) { return CFALSE; }
        redraw = ui_hot_release(&tm->hot);
        if (msg == WM_MSG_MOUSELEAVE && ui_hot_move(&tm->hot, -1)) {
            redraw = CTRUE;
        }
        if (redraw) {
            TmLayout L;
            tm_layout(win, &L);
            ui_hot_repaint(win, &tm->hot, L.btn, TB_COUNT);
        }
        return CTRUE;
    }
    case WM_MSG_KEYDOWN:     return tm_key(win, tm, (int)a, (int)b);
    case WM_MSG_DESTROY:
        if (tm != NULL) { sys_free(tm, (cu32)sizeof(TaskMan)); }
        return CTRUE;
    default: return CFALSE;
    }
}

void app_taskman_open(void)
{
    TaskMan *tm;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int cw = 400, ch = 380, fx, fy;

    tm = (TaskMan *)sys_calloc(1, (cu32)sizeof(TaskMan));
    if (tm == NULL) { SYS_LOGE("app", "taskman: OOM"); return; }

    plat_video_info(&vi);
    fx = (vi.width - cw) / 2 + 30;
    fy = (vi.height - ch) / 2 - 20;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, cw, ch);

    /*
     * Before wm_create, not after. wm_create dispatches WM_MSG_CREATE into
     * taskman_proc straight away, so anything the window does on creation
     * runs against whatever sys_calloc left behind -- and a zeroed Hist has a
     * ring of capacity zero. Nothing pushes a sample that early today (the
     * graphs are fed from WM_MSG_TIMER, which wm_set_animated starts further
     * down), so this is ordering that happens to be safe rather than ordering
     * that is safe. Initialising first makes it the latter.
     */
    hist_clear(&tm->mem);
    hist_clear(&tm->frame);
    w = wm_create("Task Manager", &frame, WM_STYLE_APP, taskman_proc, tm);
    if (w == NULL) { sys_free(tm, (cu32)sizeof(TaskMan)); return; }
    tm->self = w;
    wm_show(w, CTRUE);
    wm_set_animated(w, CTRUE);   /* the graphs are the point: keep them live */
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened Task Manager");
}

cbool app_taskman_mem_bar(WmWindow *win, CRect *out, long *live, long *budget)
{
    TmLayout L;
    CRect bar;
    CPoint o;
    if (win == NULL || out == NULL) { return CFALSE; }
    if (wm_user(win) == NULL) { return CFALSE; }
    tm_layout(win, &L);
    bar = tm_mem_bar(&L.info);
    o = wm_client_origin(win);
    *out = crect_offset(&bar, o.x, o.y);
    if (live != NULL)   { *live = (long)sys_mem_live_bytes(); }
    if (budget != NULL) { *budget = TM_MEM_BUDGET; }
    return CTRUE;
}
