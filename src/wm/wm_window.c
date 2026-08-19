/*
 * wm_window.c - Window pool, lifecycle, z-order, focus, and geometry.
 *
 * The manager owns a fixed pool of top-level windows (no dynamic explosion)
 * plus a z-order list of pool indices (back to front). All geometry is in
 * back-buffer/surface coordinates.
 */
#include "wm_internal.h"
#include "castalia/sys.h"

WmState g_wm;

cbool wm_on_screen(const struct WmWindow *w)
{
    return (w != NULL && w->in_use && w->visible &&
            w->desktop == g_wm.current_desktop) ? CTRUE : CFALSE;
}

/* ---- init / shutdown ------------------------------------------------- */
CResult wm_init(GfxSurface *backbuffer)
{
    int i;
    if (backbuffer == NULL) { return CE_INVALID; }
    for (i = 0; i < CASTALIA_MAX_WINDOWS; i++) {
        g_wm.pool[i].in_use = CFALSE;
        g_wm.pool[i].id = i;
    }
    g_wm.zcount = 0;
    g_wm.focus = NULL;
    g_wm.back = backbuffer;
    /* Default work area = the whole surface; the shell narrows it to exclude
     * the taskbar via wm_set_work_area(). */
    g_wm.work_area = crect_make(0, 0, backbuffer->w, backbuffer->h);
    g_wm.current_desktop = 0;
    g_wm.inited = CTRUE;
    SYS_LOGI("wm", "window manager up (max %d windows)", CASTALIA_MAX_WINDOWS);
    return CE_OK;
}

CRect wm_work_area(void) { return g_wm.work_area; }

/*
 * A frame of at most fw x fh, centred in the work area.
 *
 * Apps used to do this themselves from plat_video_info, which reports the
 * SCREEN -- and the screen includes the taskbar. Centring a fixed height in it
 * puts half the excess below the taskbar, and what lives at the bottom of a
 * window is the status bar, so the app looks fine and has quietly lost the row
 * that says what is selected or what just happened. Six windows did that at
 * 640x480: Solitaire by 102 pixels, CastaliaWrite by 46, CastaliaSheet by 16.
 *
 * Some apps clamped and some did not, which is the real problem -- each one
 * inventing its own allowance means each one can get it wrong. This is the
 * allowance, in the only place that knows what the work area is.
 */
CRect wm_place_centered(int fw, int fh)
{
    CRect a = g_wm.work_area;
    int aw = crect_w(&a), ah = crect_h(&a);
    int x, y;
    if (fw > aw) { fw = aw; }
    if (fh > ah) { fh = ah; }
    if (fw < 1) { fw = 1; }
    if (fh < 1) { fh = 1; }
    x = a.x0 + (aw - fw) / 2;
    y = a.y0 + (ah - fh) / 2;
    if (x < a.x0) { x = a.x0; }
    if (y < a.y0) { y = a.y0; }
    return crect_make(x, y, fw, fh);
}

void wm_set_work_area(const CRect *area)
{
    if (area != NULL) { g_wm.work_area = *area; }
}

void wm_shutdown(void)
{
    int i;
    if (!g_wm.inited) { return; }
    /* Destroy any remaining windows so their procs can release resources. */
    for (i = 0; i < CASTALIA_MAX_WINDOWS; i++) {
        if (g_wm.pool[i].in_use) {
            wm_send(&g_wm.pool[i], WM_MSG_DESTROY, 0, 0, NULL);
            g_wm.pool[i].in_use = CFALSE;
        }
    }
    g_wm.zcount = 0;
    g_wm.focus = NULL;
    g_wm.inited = CFALSE;
    SYS_LOGI("wm", "window manager down");
}

void wm_set_theme(const WmTheme *theme)
{
    if (theme != NULL) { g_wm.theme = *theme; }
}

/* ---- z-order helpers ------------------------------------------------- */
static int z_index_of(int pool_index)
{
    int i;
    for (i = 0; i < g_wm.zcount; i++) {
        if (g_wm.zorder[i] == pool_index) { return i; }
    }
    return -1;
}

static void z_remove(int pool_index)
{
    int zi = z_index_of(pool_index);
    int i;
    if (zi < 0) { return; }
    for (i = zi; i < g_wm.zcount - 1; i++) {
        g_wm.zorder[i] = g_wm.zorder[i + 1];
    }
    g_wm.zcount--;
}

static void z_to_front(int pool_index)
{
    z_remove(pool_index);
    if (g_wm.zcount < CASTALIA_MAX_WINDOWS) {
        g_wm.zorder[g_wm.zcount++] = pool_index;
    }
}

/* ---- create / destroy ------------------------------------------------ */
WmWindow *wm_create(const char *title, const CRect *frame,
                    unsigned style, WmProc proc, void *user)
{
    int i;
    struct WmWindow *w = NULL;
    for (i = 0; i < CASTALIA_MAX_WINDOWS; i++) {
        if (!g_wm.pool[i].in_use) { w = &g_wm.pool[i]; break; }
    }
    if (w == NULL) {
        SYS_LOGE("wm", "window pool exhausted");
        return NULL;
    }
    w->in_use = CTRUE;
    w->style = style;
    w->state = WM_STATE_NORMAL;
    w->frame = (frame != NULL) ? *frame : crect_make(0, 0, 200, 120);
    w->restore_frame = w->frame;
    w->proc = proc;
    w->user = user;
    w->icon = NULL;
    w->visible = CFALSE;
    w->animated = CFALSE;
    w->desktop = g_wm.current_desktop;
    sys_strlcpy(w->title, (title ? title : ""), sizeof(w->title));

    z_to_front(w->id);
    wm_send(w, WM_MSG_CREATE, 0, 0, NULL);
    wm_notify_life(w, WM_LIFE_OPEN);   /* app windows only (see helper) */
    SYS_LOGD("wm", "created window '%s' (id %d)", w->title, w->id);
    return w;
}

void wm_set_lifecycle_hook(WmLifecycleHook hook) { g_wm.life_hook = hook; }

/* Fire the shell's lifecycle hook, but only for app windows (has a title bar,
 * not a menu popup) -- so menus and frameless popups never animate or chime. */
void wm_notify_life(struct WmWindow *w, WmLifeEvent ev)
{
    if (g_wm.life_hook != NULL && w != NULL &&
        (w->style & WM_STYLE_TITLE) && !(w->style & WM_STYLE_POPUP)) {
        g_wm.life_hook(w, ev);
    }
}

/*
 * Hand the keyboard to the front-most window that is really on screen, and
 * REDRAW it.
 *
 * This loop existed three times -- in wm_destroy, in wm_set_desktop and in
 * wm_move_window_to_desktop -- and the three had already drifted: only two of
 * them skipped pop-ups, and none of them repainted the window that gained the
 * focus. A window's title bar depends on focus and so, since this session,
 * does its client: its caret and the colour of its selection. So the one that
 * inherited the keyboard sat there drawn as though it had not, until
 * something unrelated redrew it.
 *
 * Leaves the focus NULL when nothing is on screen, which is correct: the
 * desktop icons take the arrow keys when no window has them.
 */
static void focus_front_on_screen(void)
{
    int i;
    g_wm.focus = NULL;
    for (i = g_wm.zcount - 1; i >= 0; i--) {
        struct WmWindow *w = &g_wm.pool[g_wm.zorder[i]];
        if (wm_on_screen(w) && !(w->style & WM_STYLE_POPUP)) {
            g_wm.focus = w;
            wm_send(w, WM_MSG_FOCUS_GAINED, 0, 0, NULL);
            wm_dirty_add(&w->frame);
            break;
        }
    }
}

void wm_destroy(WmWindow *win)
{
    if (win == NULL || !win->in_use) { return; }
    wm_forget_window(win);  /* the pool reuses slots; see wm_dispatch.c */
    wm_notify_life(win, WM_LIFE_CLOSE);
    /* Repaint whatever the window covered. Doing this here (rather than at each
     * call site) keeps every close path correct -- caption-button click,
     * Alt+F4, and programmatic teardown (e.g. a dialog finishing) all leave a
     * clean hole instead of a ghost frame. */
    if (win->visible) { wm_dirty_add(&win->frame); }
    wm_send(win, WM_MSG_DESTROY, 0, 0, NULL);
    z_remove(win->id);
    /* The window is out of the z-order by now, so this cannot pick it. */
    if (g_wm.focus == win) { focus_front_on_screen(); }
    win->in_use = CFALSE;
    SYS_LOGD("wm", "destroyed window id %d", win->id);
}

/* ---- accessors ------------------------------------------------------- */
void       *wm_user(WmWindow *win)  { return win ? win->user : NULL; }
const char *wm_title(WmWindow *win) { return win ? win->title : ""; }
CRect       wm_frame_rect(WmWindow *win) { return win->frame; }
WmWindowState wm_state(WmWindow *win) { return win ? win->state : WM_STATE_NORMAL; }
unsigned wm_style(WmWindow *win) { return win ? win->style : 0u; }

void wm_set_title(WmWindow *win, const char *title)
{
    if (win == NULL) { return; }
    sys_strlcpy(win->title, (title ? title : ""), sizeof(win->title));
}

void wm_set_icon(WmWindow *win, const GfxSurface *icon)
{
    if (win != NULL) { win->icon = icon; }
}

const GfxSurface *wm_icon(WmWindow *win) { return win ? win->icon : NULL; }

CRect wm_client_rect(WmWindow *win)
{
    CRect r = win->frame;
    int bw = g_wm.theme.border_width;
    int th = g_wm.theme.title_height;
    if (win->style & WM_STYLE_POPUP) { return r; } /* frameless */
    if (win->style & WM_STYLE_BORDER) {
        r = crect_inset(&r, (bw > 0) ? bw : 2);
    }
    if (win->style & WM_STYLE_TITLE) {
        r.y0 += (th > 0) ? th : 18;
    }
    if (r.x1 < r.x0) { r.x1 = r.x0; }
    if (r.y1 < r.y0) { r.y1 = r.y0; }
    return r;
}

CPoint wm_client_origin(WmWindow *win)
{
    CRect c = wm_client_rect(win);
    CPoint p; p.x = c.x0; p.y = c.y0;
    return p;
}

/* ---- visibility / focus / z-order ------------------------------------ */
void wm_show(WmWindow *win, cbool visible)
{
    if (win == NULL) { return; }
    win->visible = visible;
    if (visible) {
        /* Showing a minimized window restores it to the normal state (its
         * frame was never changed on minimize, so no geometry restore). */
        if (win->state == WM_STATE_MINIMIZED) {
            win->state = WM_STATE_NORMAL;
            wm_notify_life(win, WM_LIFE_RESTORE);
        }
        z_to_front(win->id);
        wm_focus(win);
    } else if (g_wm.focus == win) {
        /*
         * A window that leaves the screen gives up the keyboard.
         *
         * Key events go to the focus and nothing asks whether the focus is
         * visible, so a hidden window that kept it swallowed everything
         * typed afterwards -- invisibly, with nothing on screen to say where
         * the letters had gone.
         *
         * Here rather than in wm_minimize because this is the one place a
         * window is hidden. Show Desktop hides them all with its own loop
         * and never called wm_minimize, so a fix that lived there would have
         * covered the caption button and not the taskbar's first Quick
         * Launch icon.
         */
        wm_send(win, WM_MSG_FOCUS_LOST, 0, 0, NULL);
        focus_front_on_screen();
    }
}

cbool wm_is_visible(WmWindow *win) { return win ? win->visible : CFALSE; }

void wm_focus(WmWindow *win)
{
    struct WmWindow *lost = g_wm.focus;
    if (win == NULL || g_wm.focus == win) { return; }
    if (lost != NULL) {
        wm_send(lost, WM_MSG_FOCUS_LOST, 0, 0, NULL);
    }
    g_wm.focus = win;
    wm_send(win, WM_MSG_FOCUS_GAINED, 0, 0, NULL);
    /*
     * Both windows repaint WHOLE, not just their title bars.
     *
     * This used to invalidate nothing at all, and the active/inactive title
     * treatment survived on the fact that focus normally changes because
     * somebody clicked or raised a window, which invalidates it for other
     * reasons. wm_focus() on its own left both windows drawn wrong. Now that
     * a window's CLIENT depends on focus too -- a selection goes quiet when
     * the keyboard leaves -- the whole frame is what has to come back.
     */
    if (lost != NULL) { wm_invalidate(lost, NULL); }
    wm_invalidate(win, NULL);
}

WmWindow *wm_focused(void) { return g_wm.focus; }
cbool wm_has_focus(WmWindow *win)
{
    return (win != NULL && g_wm.focus == win) ? CTRUE : CFALSE;
}

void wm_cycle_focus(void)
{
    struct WmWindow *target = NULL;
    int i;
    /* The back-most visible, non-popup window becomes the new front. */
    for (i = 0; i < g_wm.zcount; i++) {
        struct WmWindow *w = &g_wm.pool[g_wm.zorder[i]];
        if (wm_on_screen(w) && !(w->style & WM_STYLE_POPUP)) {
            target = w;
            break;
        }
    }
    if (target == NULL || target == g_wm.focus) { return; }
    wm_bring_to_front(target);   /* invalidates, if it really moved */
    wm_focus(target);
}

/*
 * Raise a window, and invalidate it BECAUSE it was raised -- not because
 * somebody clicked.
 *
 * The dispatcher used to mark the frame dirty on every mouse-down, whether or
 * not the window moved in the z-order and whether or not it already had the
 * keyboard. So every click anywhere in any window paid for a full repaint of
 * that window on top of whatever the click itself changed: pressing a scroll
 * arrow in the Log Viewer cost 206,225 pixels of a 186,816-pixel client, and
 * only 5,920 of them were the scroll. Placing a caret, pressing a toolbar
 * button, ticking a checkbox -- all of them, all the time.
 *
 * A window already at the front is already drawn over the others, so there is
 * nothing to redraw. Focus changes invalidate on their own (see wm_focus), and
 * that is the other half of what the unconditional call was standing in for.
 */
void wm_bring_to_front(WmWindow *win)
{
    if (win == NULL) { return; }
    if (g_wm.zcount > 0 && g_wm.zorder[g_wm.zcount - 1] == win->id) {
        return;                          /* already over everything */
    }
    z_to_front(win->id);
    wm_dirty_add(&win->frame);
}

/*
 * Put a window at an arbitrary frame, and leave the screen consistent: both
 * the area it left and the area it now covers are invalidated, and the proc is
 * told what happened.
 *
 * This replaced a wm_move() that set the frame, sent WM_MSG_MOVE and
 * invalidated nothing -- correct only for a caller that knew to repaint the
 * hole itself, and it had no callers at all, so nobody had ever found out.
 *
 * WM_MSG_SIZE is sent only when the size really changed. An app that rebuilds
 * its layout on WM_MSG_SIZE should not have to do it because the window slid
 * eight pixels left.
 */
void wm_set_frame(WmWindow *win, const CRect *frame)
{
    CRect oldf;
    cbool resized;
    if (win == NULL || frame == NULL) { return; }
    oldf = win->frame;
    resized = (crect_w(frame) != crect_w(&oldf) ||
               crect_h(frame) != crect_h(&oldf)) ? CTRUE : CFALSE;
    win->frame = *frame;
    if (win->state == WM_STATE_NORMAL) { win->restore_frame = *frame; }
    if (win->visible) {
        wm_dirty_add(&oldf);
        wm_dirty_add(&win->frame);
    }
    if (oldf.x0 != frame->x0 || oldf.y0 != frame->y0) {
        wm_send(win, WM_MSG_MOVE, frame->x0, frame->y0, NULL);
    }
    if (resized) {
        wm_send(win, WM_MSG_SIZE, crect_w(frame), crect_h(frame), NULL);
    }
}

void wm_set_state(WmWindow *win, WmWindowState state)
{
    CRect old_frame;
    if (win == NULL || win->state == state) { return; }
    old_frame = win->frame;
    if (state == WM_STATE_MAXIMIZED && g_wm.back != NULL) {
        win->restore_frame = win->frame;
        /* Maximize to the work area (surface minus the taskbar strip), so the
         * taskbar stays visible and clickable under a maximized window. */
        win->frame = g_wm.work_area;
    } else if (state == WM_STATE_NORMAL) {
        win->frame = win->restore_frame;
    }
    win->state = state;
    /* Repaint BOTH the vacated area and the resized frame; otherwise growing
     * (maximize) leaves desktop showing over the new area, and shrinking
     * (restore) leaves the old frame painted over the desktop. */
    if (win->visible) {
        wm_dirty_add(&old_frame);
        wm_dirty_add(&win->frame);
    }
    wm_send(win, WM_MSG_SIZE, crect_w(&win->frame), crect_h(&win->frame), NULL);
}

/*
 * Minimize: hide, mark, and tell the shell first so it can animate the window
 * down to its taskbar button while the frame is still where the user can see
 * it. The order matters, which is why the caption button and the window menu
 * share this rather than each repeating three lines in the right sequence.
 *
 * Show Desktop does NOT come through here, deliberately: it minimizes every
 * window at once and wants one sound and one repaint for the batch, not one
 * of each per window.
 */
void wm_minimize(WmWindow *win)
{
    if (win == NULL || win->state == WM_STATE_MINIMIZED) { return; }
    if (!(win->style & WM_STYLE_MINIMIZE)) { return; }
    wm_notify_life(win, WM_LIFE_MINIMIZE);
    wm_show(win, CFALSE);
    win->state = WM_STATE_MINIMIZED;
    wm_dirty_add(&win->frame);
    /* The keyboard is given up by wm_show above, which every hide goes
     * through -- see the note there. */
}

/*
 * Ask a window to close, the way its close box does: the proc sees
 * WM_MSG_CLOSE and may handle it itself (an unsaved-changes prompt, say). Only
 * if it does not does the manager destroy the window. This is deliberately not
 * wm_destroy(), which does not ask.
 */
void wm_request_close(WmWindow *win)
{
    if (win == NULL) { return; }
    wm_send(win, WM_MSG_CLOSE, 0, 0, NULL);
}

/* ---- per-frame animation tick ---------------------------------------- */
cbool wm_is_animated(WmWindow *win)
{
    return (win != NULL && win->animated) ? CTRUE : CFALSE;
}

void wm_set_animated(WmWindow *win, cbool on)
{
    if (win != NULL) { win->animated = on; }
}

void wm_tick_animations(void)
{
    int i;
    /* Snapshot the z-order first: a proc may open/close windows in response to
     * its tick, which would reshuffle g_wm.zorder mid-iteration. */
    int ids[CASTALIA_MAX_WINDOWS];
    int n = 0;
    for (i = 0; i < g_wm.zcount; i++) { ids[n++] = g_wm.zorder[i]; }
    for (i = 0; i < n; i++) {
        struct WmWindow *w = &g_wm.pool[ids[i]];
        if (w->in_use && wm_on_screen(w) && w->animated) {
            wm_send(w, WM_MSG_TIMER, 0, 0, NULL);
        }
    }
}

int       wm_window_count(void) { return g_wm.zcount; }
WmWindow *wm_window_at(int index)
{
    if (index < 0 || index >= g_wm.zcount) { return NULL; }
    return &g_wm.pool[g_wm.zorder[index]];
}

/*
 * How many open windows say they hold unsaved work.
 *
 * Asked of every window rather than of a list the shell keeps, because a list
 * is a second place to forget an app -- and the app that gets forgotten is the
 * one whose work is lost. A window that does not answer the question has
 * nothing to lose, which is the right default.
 */
int wm_unsaved_count(void)
{
    int i, n = 0;
    for (i = 0; i < g_wm.zcount; i++) {
        WmWindow *w = &g_wm.pool[g_wm.zorder[i]];
        if (w->proc != NULL &&
            w->proc(w, WM_MSG_QUERY_UNSAVED, 0, 0, NULL)) {
            n++;
        }
    }
    return n;
}

WmWindow *wm_hit_test(int x, int y)
{
    int i;
    for (i = g_wm.zcount - 1; i >= 0; i--) {
        struct WmWindow *w = &g_wm.pool[g_wm.zorder[i]];
        if (wm_on_screen(w) && crect_contains(&w->frame, x, y)) {
            return w;
        }
    }
    return NULL;
}

/* ---- virtual desktops ------------------------------------------------ */
int wm_current_desktop(void) { return g_wm.current_desktop; }
int wm_desktop_count(void)   { return WM_DESKTOP_COUNT; }

int wm_window_desktop(WmWindow *win) { return win ? win->desktop : 0; }

void wm_set_desktop(int d)
{
    int i;
    if (d < 0) { d = 0; }
    if (d >= WM_DESKTOP_COUNT) { d = WM_DESKTOP_COUNT - 1; }
    if (d == g_wm.current_desktop) { return; }

    /* The old focus loses focus; it stays on its desktop but leaves the screen. */
    if (g_wm.focus != NULL) {
        wm_send(g_wm.focus, WM_MSG_FOCUS_LOST, 0, 0, NULL);
        g_wm.focus = NULL;
    }
    /* Mark every currently-visible window's area dirty so the compositor
     * repaints where windows vanished/appeared (the shell also full-repaints). */
    for (i = 0; i < g_wm.zcount; i++) {
        struct WmWindow *w = &g_wm.pool[g_wm.zorder[i]];
        if (w->in_use && w->visible) { wm_dirty_add(&w->frame); }
    }
    g_wm.current_desktop = d;
    focus_front_on_screen();
}

void wm_move_window_to_desktop(WmWindow *win, int d)
{
    if (win == NULL) { return; }
    if (d < 0) { d = 0; }
    if (d >= WM_DESKTOP_COUNT) { d = WM_DESKTOP_COUNT - 1; }
    if (d == win->desktop) { return; }
    /* If it is leaving the current desktop, repaint the hole and refocus. */
    if (win->desktop == g_wm.current_desktop && win->visible) {
        wm_dirty_add(&win->frame);
    }
    win->desktop = d;
    if (g_wm.focus == win && d != g_wm.current_desktop) {
        wm_send(win, WM_MSG_FOCUS_LOST, 0, 0, NULL);
        focus_front_on_screen();
    }
}

int wm_window_id(WmWindow *win)
{
    if (win == NULL) { return -1; }
    return (int)(win - g_wm.pool);
}

WmWindow *wm_window_by_id(int id)
{
    WmWindow *w;
    if (id < 0 || id >= CASTALIA_MAX_WINDOWS) { return NULL; }
    w = &g_wm.pool[id];
    /* A slot is reused, so "in range" is not "alive" -- only a window still in
     * the z-order counts, or a stale id would resurrect a dead window. */
    {
        int i;
        for (i = 0; i < g_wm.zcount; i++) {
            if (&g_wm.pool[g_wm.zorder[i]] == w) { return w; }
        }
    }
    return NULL;
}
