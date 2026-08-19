/*
 * wm_dispatch.c - Message default handling, input routing, invalidation,
 *                 and window compositing.
 *
 * Input model: the shell feeds raw PlatEvents to wm_handle_event(). The
 * manager resolves focus, title-bar dragging, and caption-button clicks,
 * and forwards client-area mouse/keyboard messages to the target window's
 * proc in client coordinates.
 */
#include "wm_internal.h"
#include "wm_move.h"
#include "castalia/sys.h"
#include "castalia/plat.h"
#include "castalia/settings.h"

/* Pending window invalidations for the next frame. */
static CRegion g_dirty;
/* Title-bar drag state. */
static struct WmWindow *g_drag = NULL;
static int g_drag_dx = 0, g_drag_dy = 0;
/* The window the pointer was over last, so the one it LEAVES can be told.
 * A pool slot rather than owned memory, so a closed window is recognised by
 * its in_use flag rather than by a pointer that has gone bad. */
static struct WmWindow *g_hover = NULL;
static WmSnapZone g_drag_snap = WM_SNAP_NONE; /* live snap-preview zone */
/*
 * Outline dragging ([Shell] DragOutline, forced in safe mode).
 *
 * The window stays where it is and a rubber band follows the pointer; the
 * move happens once, on release. Four thin lines instead of two whole window
 * repaints per motion event -- see the note on the setting.
 *
 * g_drag_out is where the band is now; g_drag_outline says the drag is being
 * done this way at all, captured when the drag STARTS so that changing the
 * setting mid-drag cannot leave a band on screen with nothing tracking it.
 */
static cbool g_drag_outline = CFALSE;
static CRect g_drag_out;
/* Declared here because wm_drag_outline answers for a resize too, and the
 * resize state lives further down with the border-grip code. */
static struct WmWindow *g_resize;

/*
 * Mark a band's four EDGES dirty -- not its bounding box.
 *
 * The bounding box is the whole window, which is the entire point of not
 * drawing one: the first version of this called crect_inset(r, -3) and handed
 * that single rectangle to wm_dirty_add, so an "outline" drag cost 122,425
 * pixels against 118,201 for carrying the contents. It was slower than the
 * thing it replaced, and it looked right on screen the whole time.
 *
 * Four strips, each a few pixels thick, overlapping at the corners. The
 * corners being counted twice costs a few dozen pixels and saves having to
 * think about them.
 */
/*
 * Two pixels of band and one of slack either side: the strips are four thick,
 * and that number is the whole cost. Eight thick -- the first guess -- makes a
 * 400x280 drag cost 24,732 pixels a frame instead of 12,000, for nothing: the
 * outline is drawn at the frame's edge and one pixel out, and the extra was
 * covering desktop that never changed.
 */
#define WM_DRAG_BAND 2

/*
 * Straight into the region, WITHOUT the drop-shadow margin wm_dirty_add adds
 * to everything else.
 *
 * That margin exists so a rectangle that is a window's frame also repaints
 * the shadow beyond it, and no call site has to know about shadows. A drag
 * band is not a window and casts nothing: on four strips four pixels thick it
 * more than doubles each one -- 18,812 pixels a frame instead of 11,008,
 * measured -- and every one of the extra is desktop that did not change.
 */
static void dirty_add_exact(const CRect *r)
{
    if (r != NULL) { cregion_add(&g_dirty, r); }
}

static void drag_out_mark(const CRect *r)
{
    int w = crect_w(r), h = crect_h(r);
    int t = WM_DRAG_BAND;
    CRect e;
    if (w <= 0 || h <= 0) { return; }
    e = crect_make(r->x0 - t, r->y0 - t, w + t * 2, t * 2);   /* top    */
    dirty_add_exact(&e);
    e = crect_make(r->x0 - t, r->y1 - t, w + t * 2, t * 2);   /* bottom */
    dirty_add_exact(&e);
    e = crect_make(r->x0 - t, r->y0 - t, t * 2, h + t * 2);   /* left   */
    dirty_add_exact(&e);
    e = crect_make(r->x1 - t, r->y0 - t, t * 2, h + t * 2);   /* right  */
    dirty_add_exact(&e);
}

/* Clear any active snap-preview zone, invalidating its outline so it erases. */
static void clear_drag_snap(void)
{
    if (g_drag_snap != WM_SNAP_NONE) {
        CRect pv = wm_snap_rect(&g_wm.work_area, g_drag_snap);
        wm_dirty_add(&pv);
        g_drag_snap = WM_SNAP_NONE;
    }
}

cbool wm_drag_outline(CRect *out)
{
    if (!g_drag_outline || (g_drag == NULL && g_resize == NULL)) {
        return CFALSE;
    }
    if (out != NULL) { *out = g_drag_out; }
    return CTRUE;
}

cbool wm_drag_snap_preview(CRect *out)
{
    if (g_drag == NULL || g_drag_snap == WM_SNAP_NONE) { return CFALSE; }
    if (out != NULL) { *out = wm_snap_rect(&g_wm.work_area, g_drag_snap); }
    return CTRUE;
}

/* ---- default message handling ---------------------------------------- */
cbool wm_send(struct WmWindow *w, WmMessage msg, long a, long b, void *param)
{
    cbool handled = CFALSE;
    if (w == NULL) { return CFALSE; }
    if (w->proc != NULL) {
        handled = w->proc(w, msg, a, b, param);
    }
    if (!handled) {
        /* Manager defaults. */
        switch (msg) {
        case WM_MSG_CLOSE:
            wm_destroy(w);
            handled = CTRUE;
            break;
        default:
            break;
        }
    }
    return handled;
}

/* ---- invalidation ---------------------------------------------------- */
void wm_invalidate(WmWindow *win, const CRect *client_rect_or_null)
{
    CRect r;
    if (win == NULL || !win->visible) { return; }
    if (client_rect_or_null != NULL) {
        r = crect_intersect(&win->frame, client_rect_or_null);
    } else {
        r = win->frame;
    }
    wm_dirty_add(&r);
}

void wm_dirty_add(const CRect *r)
{
    CRect d;
    if (r == NULL) { return; }
    d = *r;
    /* Glossy windows cast a drop shadow beyond their frame; inflating every
     * dirty rect by that margin keeps move/close/z-order changes remnant-free
     * without each call site knowing about shadows. Over-invalidating a few
     * pixels is far cheaper than hunting ghosts. */
    if (g_wm.theme.glossy) { d.x1 += WM_SHADOW; d.y1 += WM_SHADOW; }
    cregion_add(&g_dirty, &d);
}

void wm_collect_dirty(CRegion *region)
{
    int i;
    if (region == NULL) { return; }
    for (i = 0; i < g_dirty.count; i++) {
        cregion_add(region, &g_dirty.rects[i]);
    }
    cregion_clear(&g_dirty);
}

/* ---- compositing ----------------------------------------------------- */
void wm_paint(const CRegion *region)
{
    int zi, ri;
    if (region == NULL) { return; }
    for (zi = 0; zi < g_wm.zcount; zi++) {
        struct WmWindow *w = &g_wm.pool[g_wm.zorder[zi]];
        CRect fb;
        if (!wm_on_screen(w)) { continue; }
        /* Overlap against the frame + shadow margin, so a region rect that
         * covers only the shadow strip still repaints it. */
        fb = w->frame;
        if (g_wm.theme.glossy) { fb.x1 += WM_SHADOW; fb.y1 += WM_SHADOW; }
        for (ri = 0; ri < region->count; ri++) {
            if (crect_overlaps(&fb, &region->rects[ri])) {
                wm_draw_window(w, &region->rects[ri]);
            }
        }
    }
}

/* ---- input routing --------------------------------------------------- */
static cbool point_in(const CRect *r, int x, int y) { return crect_contains(r, x, y); }

static void begin_drag(struct WmWindow *w, int x, int y)
{
    g_drag = w;
    g_drag_dx = x - w->frame.x0;
    g_drag_dy = y - w->frame.y0;
    /* Decided once, here: a setting that changed mid-drag would otherwise
     * leave a band on screen with nothing tracking it, or move a window that
     * had been showing one. */
    g_drag_outline = settings_get()->drag_outline;
    g_drag_out = w->frame;
    if (g_drag_outline) { drag_out_mark(&g_drag_out); }
}

/* Snap the just-dropped window to a work-area edge (left/right half, or top =
 * maximize). Saves the pre-snap frame as the restore frame so a later
 * maximize/restore returns to the snapped size. */
static void apply_snap(struct WmWindow *w, WmSnapZone zone)
{
    CRect nf;
    if (w == NULL || zone == WM_SNAP_NONE) { return; }
    if (zone == WM_SNAP_TOP) {
        wm_set_state(w, WM_STATE_MAXIMIZED); /* handles restore_frame + repaint */
        return;
    }
    nf = wm_snap_rect(&g_wm.work_area, zone);
    /* Normal first, so wm_set_frame records the snapped frame as the one a
     * later restore returns to -- a window tiled to a half should stay there
     * when it is un-maximized. */
    w->state = WM_STATE_NORMAL;
    wm_set_frame(w, &nf);
}

static void do_drag(int x, int y)
{
    CRect oldf;
    int nx, ny;
    if (g_drag == NULL) { return; }
    oldf = g_drag_outline ? g_drag_out : g_drag->frame;
    nx = x - g_drag_dx;
    ny = y - g_drag_dy;
    /* Keep the title bar reachable on screen -- the same rule the keyboard
     * move applies, out of the same function (wm_move.c). */
    {
        CRect want;
        if (g_wm.back != NULL) {
            want = wm_move_clamp(&oldf, nx, ny, g_wm.back->w, g_wm.back->h);
        } else {
            want = crect_make(nx, ny, crect_w(&oldf), crect_h(&oldf));
        }
        if (g_drag_outline) {
            /* The window does not move yet. Only the band's old and new
             * edges have to come back -- two thin frames, not two windows. */
            g_drag_out = want;
            drag_out_mark(&oldf);
            drag_out_mark(&want);
        } else {
            g_drag->frame = want;
            /* Invalidate both the vacated and the new area. */
            wm_dirty_add(&oldf);
            wm_dirty_add(&g_drag->frame);
        }
    }
    /* Live snap preview: if the pointer is near a work-area edge (and this
     * window can maximize), arm the target zone; invalidate on any change so
     * the outline follows or clears. */
    {
        WmSnapZone z = (g_drag->style & WM_STYLE_MAXIMIZE)
                     ? wm_snap_zone(&g_wm.work_area, x, y, WM_SNAP_EDGE)
                     : WM_SNAP_NONE;
        if (z != g_drag_snap) {
            clear_drag_snap();          /* erase the previous outline */
            g_drag_snap = z;
            if (z != WM_SNAP_NONE) {
                CRect nw = wm_snap_rect(&g_wm.work_area, z);
                wm_dirty_add(&nw);
            }
        }
    }
}

/* ---- border resize --------------------------------------------------- */
#define RS_L 0x1
#define RS_R 0x2
#define RS_T 0x4
#define RS_B 0x8
#define WM_GRIP  4     /* how close to an edge counts as a resize grip     */
/* WM_MIN_W / WM_MIN_H live in wm_move.h, shared with the keyboard path. */

static int   g_resize_edges = 0;
static CRect g_resize_start;
static int   g_resize_ax, g_resize_ay;

/*
 * Title-bar double-click detection (maximize/restore). The window is the one
 * setting shared with the desktop icons; see settings.h.
 *
 * WHERE the first press landed matters as well as when. Two presses at
 * opposite ends of a title bar are not a double-click in any system, and
 * without the position test this had a second failure that is easier to hit:
 * close a window, open another, click its title bar inside the double-click
 * time, and the pool hands out the same slot -- so the pointer compares equal
 * to a window that no longer exists and the new one maximizes itself. Found
 * by a scene that opened two File Managers in a row.
 */
#define WM_DBLCLICK_SLOP 6
static struct WmWindow *g_tb_click_win = NULL;
static cu32  g_tb_click_ms = 0;
static int   g_tb_click_x = 0, g_tb_click_y = 0;

/*
 * Forget everything the dispatcher is holding about a window, because it is
 * being destroyed.
 *
 * Four pointers here outlive a window: the one being dragged, the one being
 * resized, the one the pointer is over, and the one a first title-bar click
 * landed on. The pool hands slots out again, so none of them can be compared
 * to a live window afterwards -- and two of them are worse than a wrong
 * comparison. g_drag and g_resize are WRITTEN THROUGH on the next mouse move:
 * a window closed with Alt+F4 while its title bar is held (which needs
 * nothing but two hands) left the next motion event setting the frame of a
 * slot that is not in use, or of whatever window has been given it since.
 *
 * The click half of this was found by a scene that opened two File Managers
 * in a row and watched the second maximize itself; the rest is the same
 * question asked of the other three.
 */
void wm_forget_window(struct WmWindow *w)
{
    if (w == NULL) { return; }
    if (g_tb_click_win == w) { g_tb_click_win = NULL; }
    if (g_hover == w)        { g_hover = NULL; }
    if (g_drag == w || g_resize == w) {
        /* A band left on the desktop with nothing tracking it is worse than
         * no band: it never goes away. */
        if (g_drag_outline) { drag_out_mark(&g_drag_out); }
        g_drag_outline = CFALSE;
        if (g_drag == w) { clear_drag_snap(); g_drag = NULL; }
        if (g_resize == w) { g_resize = NULL; }
    }
}

/* Which edges (if any) the point grips. Only movable (BORDER, non-maximized)
 * windows resize. */
static int resize_hit(const struct WmWindow *w, int x, int y)
{
    const CRect *f = &w->frame;
    int e = 0;
    if (!(w->style & WM_STYLE_BORDER) || w->state != WM_STATE_NORMAL) { return 0; }
    if (x < f->x0 - WM_GRIP || x > f->x1 + WM_GRIP ||
        y < f->y0 - WM_GRIP || y > f->y1 + WM_GRIP) { return 0; }
    if (x <= f->x0 + WM_GRIP) { e |= RS_L; }
    if (x >= f->x1 - WM_GRIP) { e |= RS_R; }
    if (y <= f->y0 + WM_GRIP) { e |= RS_T; }
    if (y >= f->y1 - WM_GRIP) { e |= RS_B; }
    return e;
}

static void begin_resize(struct WmWindow *w, int edges, int x, int y)
{
    g_resize = w;
    g_resize_edges = edges;
    g_resize_start = w->frame;
    g_resize_ax = x;
    g_resize_ay = y;
    /* Same decision as begin_drag, and for the same reason: taken once, so a
     * setting changed mid-gesture cannot strand a band on the screen. */
    g_drag_outline = settings_get()->drag_outline;
    g_drag_out = w->frame;
    if (g_drag_outline) { drag_out_mark(&g_drag_out); }
}

static void do_resize(int x, int y)
{
    CRect oldf, nf;
    int dx, dy, x0, y0, x1, y1;
    if (g_resize == NULL) { return; }
    oldf = g_drag_outline ? g_drag_out : g_resize->frame;
    dx = x - g_resize_ax;
    dy = y - g_resize_ay;
    x0 = g_resize_start.x0; y0 = g_resize_start.y0;
    x1 = g_resize_start.x1; y1 = g_resize_start.y1;
    if (g_resize_edges & RS_L) { x0 += dx; }
    if (g_resize_edges & RS_R) { x1 += dx; }
    if (g_resize_edges & RS_T) { y0 += dy; }
    if (g_resize_edges & RS_B) { y1 += dy; }
    /* Enforce a minimum size by pushing back the edge the user is dragging. */
    if (x1 - x0 < WM_MIN_W) {
        if (g_resize_edges & RS_L) { x0 = x1 - WM_MIN_W; } else { x1 = x0 + WM_MIN_W; }
    }
    if (y1 - y0 < WM_MIN_H) {
        if (g_resize_edges & RS_T) { y0 = y1 - WM_MIN_H; } else { y1 = y0 + WM_MIN_H; }
    }
    if (y0 < 0) { y0 = 0; } /* keep the title bar on screen */
    nf = crect_make_xyxy(x0, y0, x1, y1);
    if (g_drag_outline) {
        /*
         * The band again, and here it saves more than it does on a move: a
         * resize also sends WM_MSG_SIZE, so the window rebuilds its whole
         * layout and repaints itself on every motion event as well as the
         * two areas the manager marks. Deferring it to the release means one
         * relayout for the gesture instead of one per pixel of travel.
         */
        g_drag_out = nf;
        drag_out_mark(&oldf);
        drag_out_mark(&nf);
        return;
    }
    g_resize->frame = nf;
    wm_dirty_add(&oldf);
    wm_dirty_add(&nf);
    wm_send(g_resize, WM_MSG_SIZE, crect_w(&nf), crect_h(&nf), NULL);
}

WmWindow *wm_topmost_modal(void)
{
    int i;
    for (i = g_wm.zcount - 1; i >= 0; i--) {
        struct WmWindow *w = &g_wm.pool[g_wm.zorder[i]];
        if (wm_on_screen(w) && (w->style & WM_STYLE_MODAL)) {
            return w;
        }
    }
    return NULL;
}

cbool wm_has_modal(void) { return (wm_topmost_modal() != NULL) ? CTRUE : CFALSE; }

cbool wm_handle_event(const PlatEvent *ev)
{
    struct WmWindow *modal;
    if (ev == NULL) { return CFALSE; }

    /* While a modal dialog is up it captures all input: keyboard goes to it,
     * and mouse presses outside its frame are swallowed (not routed to the
     * owner window, taskbar, or desktop). */
    modal = wm_topmost_modal();
    if (modal != NULL) {
        if (ev->type == PLAT_EV_KEY_DOWN) {
            if (ev->key == PLAT_KEY_CLOSE) {
                return wm_send(modal, WM_MSG_CLOSE, 0, 0, NULL);
            }
            return wm_send(modal, WM_MSG_KEYDOWN, ev->key, ev->ch,
                           (void *)&ev->mods);
        }
        if (ev->type == PLAT_EV_KEY_UP) {
            return wm_send(modal, WM_MSG_KEYUP, ev->key, ev->ch,
                           (void *)&ev->mods);
        }
        if (ev->type == PLAT_EV_MOUSE_DOWN || ev->type == PLAT_EV_MOUSE_UP ||
            ev->type == PLAT_EV_MOUSE_MOVE) {
            if (!crect_contains(&modal->frame, ev->mouse_x, ev->mouse_y)) {
                if (ev->type == PLAT_EV_MOUSE_DOWN) {
                    wm_focus(modal);
                    wm_bring_to_front(modal);
                }
                return CTRUE; /* swallow: input outside the modal is blocked */
            }
            /* inside the modal: fall through to normal routing (it is topmost) */
        }
    }

    switch (ev->type) {
    case PLAT_EV_MOUSE_DOWN: {
        struct WmWindow *w;
        if (!(ev->buttons & PLAT_MB_LEFT) && !(ev->buttons & PLAT_MB_RIGHT)) {
            /* fallthrough not needed; only act on primary/secondary press */
        }
        w = wm_hit_test(ev->mouse_x, ev->mouse_y);
        if (w == NULL) { return CFALSE; } /* desktop handles it */
        wm_focus(w);            /* invalidates both, if focus really moved */
        wm_bring_to_front(w);   /* invalidates, if it really was raised     */

        /* Caption buttons. */
        if (ev->buttons & PLAT_MB_LEFT) {
            CRect cb = wm_frame_close_btn(w);
            CRect mb = wm_frame_max_btn(w);
            CRect nb = wm_frame_min_btn(w);
            if (point_in(&cb, ev->mouse_x, ev->mouse_y)) {
                wm_send(w, WM_MSG_CLOSE, 0, 0, NULL);
                return CTRUE;
            }
            if (point_in(&mb, ev->mouse_x, ev->mouse_y)) {
                wm_set_state(w, (w->state == WM_STATE_MAXIMIZED)
                                ? WM_STATE_NORMAL : WM_STATE_MAXIMIZED);
                return CTRUE;
            }
            if (point_in(&nb, ev->mouse_x, ev->mouse_y)) {
                wm_minimize(w); /* minimize == hide; the taskbar restores it */
                return CTRUE;
            }
            /* Border/corner resize grips (checked before title-bar drag so the
             * top edge resizes and the rest of the bar drags). */
            {
                int edges = resize_hit(w, ev->mouse_x, ev->mouse_y);
                if (edges) {
                    begin_resize(w, edges, ev->mouse_x, ev->mouse_y);
                    return CTRUE;
                }
            }
            /* Title bar: double-click toggles maximize/restore, otherwise drag. */
            {
                CRect tb = wm_frame_titlebar(w);
                if (point_in(&tb, ev->mouse_x, ev->mouse_y)) {
                    cu32 now = plat_ticks_ms();
                    int ddx = ev->mouse_x - g_tb_click_x;
                    int ddy = ev->mouse_y - g_tb_click_y;
                    if (ddx < 0) { ddx = -ddx; }
                    if (ddy < 0) { ddy = -ddy; }
                    if (w == g_tb_click_win &&
                        (now - g_tb_click_ms) < (cu32)settings_get()->dblclick_ms &&
                        ddx <= WM_DBLCLICK_SLOP && ddy <= WM_DBLCLICK_SLOP &&
                        (w->style & WM_STYLE_MAXIMIZE)) {
                        wm_set_state(w, (w->state == WM_STATE_MAXIMIZED)
                                        ? WM_STATE_NORMAL : WM_STATE_MAXIMIZED);
                        g_tb_click_win = NULL; /* consume the pair */
                        return CTRUE;
                    }
                    g_tb_click_win = w;
                    g_tb_click_ms = now;
                    g_tb_click_x = ev->mouse_x;
                    g_tb_click_y = ev->mouse_y;
                    begin_drag(w, ev->mouse_x, ev->mouse_y);
                    return CTRUE;
                }
            }
        }
        /* Client-area press -> forward in client coords. */
        {
            CPoint o = wm_client_origin(w);
            WmMessage m = (ev->buttons & PLAT_MB_RIGHT) ? WM_MSG_RBUTTONDOWN
                                                        : WM_MSG_LBUTTONDOWN;
            wm_send(w, m, ev->mouse_x - o.x, ev->mouse_y - o.y, NULL);
        }
        return CTRUE;
    }
    case PLAT_EV_MOUSE_UP:
        if (g_resize != NULL) {
            struct WmWindow *rw = g_resize;
            g_resize = NULL;
            if (g_drag_outline) {
                /* One relayout for the whole gesture, here, where the band
                 * ended up -- and the band's last position is erased by the
                 * same invalidate that draws the window at its new size. */
                CRect dest = g_drag_out;
                g_drag_outline = CFALSE;
                drag_out_mark(&dest);
                wm_set_frame(rw, &dest);
            }
            return CTRUE;
        }
        if (g_drag != NULL) {
            /* Snap to a work-area edge if the pointer ended near one (only
             * windows that can maximize participate -- not dialogs/popups). */
            struct WmWindow *w = g_drag;
            WmSnapZone z = wm_snap_zone(&g_wm.work_area, ev->mouse_x,
                                        ev->mouse_y, WM_SNAP_EDGE);
            clear_drag_snap();   /* erase the live preview outline */
            if (g_drag_outline) {
                /* The move happens ONCE, here, where the band ended up --
                 * and the band's last position is erased by the same
                 * invalidate that draws the window in its new place. */
                CRect dest = g_drag_out;
                g_drag_outline = CFALSE;
                g_drag = NULL;
                drag_out_mark(&dest);      /* erase the band where it ended */
                wm_set_frame(w, &dest);
                if (z != WM_SNAP_NONE && (w->style & WM_STYLE_MAXIMIZE)) {
                    apply_snap(w, z);
                }
                return CTRUE;
            }
            g_drag = NULL;
            if (z != WM_SNAP_NONE && (w->style & WM_STYLE_MAXIMIZE)) {
                apply_snap(w, z);
            }
            return CTRUE;
        }
        {
            struct WmWindow *w = wm_hit_test(ev->mouse_x, ev->mouse_y);
            if (w != NULL) {
                CPoint o = wm_client_origin(w);
                wm_send(w, WM_MSG_LBUTTONUP,
                        ev->mouse_x - o.x, ev->mouse_y - o.y, NULL);
                return CTRUE;
            }
        }
        return CFALSE;
    case PLAT_EV_MOUSE_MOVE:
        if (g_resize != NULL) { do_resize(ev->mouse_x, ev->mouse_y); return CTRUE; }
        if (g_drag != NULL) { do_drag(ev->mouse_x, ev->mouse_y); return CTRUE; }
        {
            struct WmWindow *w = wm_hit_test(ev->mouse_x, ev->mouse_y);
            /*
             * Tell the window the pointer just LEFT that it left. Nothing
             * else would: the only message sent for a move goes to wherever
             * the pointer is now, so a button lit under it stayed lit after
             * the pointer had crossed onto another window.
             *
             * Held buttons are exempt -- a drag that wanders off the window
             * is still that window's drag, and it goes on hearing about it.
             */
            if (g_hover != NULL && g_hover != w && ev->buttons == 0) {
                if (g_hover->in_use) {
                    wm_send(g_hover, WM_MSG_MOUSELEAVE, 0, 0, NULL);
                }
                g_hover = NULL;
            }
            if (w != NULL) {
                CPoint o = wm_client_origin(w);
                if (ev->buttons == 0) { g_hover = w; }
                wm_send(w, WM_MSG_MOUSEMOVE,
                        ev->mouse_x - o.x, ev->mouse_y - o.y, NULL);
                return CTRUE;
            }
        }
        return CFALSE;
    case PLAT_EV_MOUSE_WHEEL: {
        /*
         * To the window under the POINTER, and without raising or focusing
         * it -- scrolling something to read it is not choosing it. A wheel
         * over the desktop or the taskbar is not ours; answering CFALSE lets
         * the shell have it.
         *
         * Nothing here is exempted while a button is held: a wheel turned in
         * the middle of a drag belongs to whatever is being dragged, and the
         * window it is over is that window.
         */
        struct WmWindow *w = wm_hit_test(ev->mouse_x, ev->mouse_y);
        if (w == NULL || ev->wheel == 0) { return CFALSE; }
        return wm_send(w, WM_MSG_MOUSEWHEEL, (long)ev->wheel, 0, NULL);
    }
    case PLAT_EV_KEY_DOWN:
        if (g_wm.focus != NULL) {
            /* Alt+F4 closes the focused window (keyboard-only path, since the
             * mouse is optional on the target hardware). */
            if (ev->key == PLAT_KEY_CLOSE) {
                return wm_send(g_wm.focus, WM_MSG_CLOSE, 0, 0, NULL);
            }
            return wm_send(g_wm.focus, WM_MSG_KEYDOWN, ev->key, ev->ch,
                           (void *)&ev->mods);
        }
        return CFALSE;
    case PLAT_EV_KEY_UP:
        if (g_wm.focus != NULL) {
            return wm_send(g_wm.focus, WM_MSG_KEYUP, ev->key, ev->ch,
                           (void *)&ev->mods);
        }
        return CFALSE;
    default:
        return CFALSE;
    }
}
