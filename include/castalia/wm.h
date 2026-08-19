/*
 * wm.h - Layer 3 window manager: top-level windows, z-order, focus,
 *        non-client frames, and message dispatch.
 *
 * The window manager owns a fixed pool of top-level windows (no dynamic
 * window explosion), tracks their z-order and invalid regions, draws the
 * classic beveled frame + title bar + caption buttons, and routes input to
 * a per-window WmProc. The desktop and taskbar are drawn by the shell as
 * background/foreground layers; everything else the user opens is a WmWindow.
 *
 * Messages mirror the Bible's message model but with a Castalia-native,
 * deliberately small surface (no Win32 emulation).
 */
#ifndef CASTALIA_WM_H
#define CASTALIA_WM_H

#include "castalia/ctypes.h"
#include "castalia/rect.h"
#include "castalia/gfx.h"

typedef enum {
    WM_MSG_CREATE = 0,
    WM_MSG_DESTROY,
    WM_MSG_PAINT,        /* param: GfxSurface* (already clipped)          */
    WM_MSG_SIZE,
    WM_MSG_MOVE,
    WM_MSG_MOUSEMOVE,    /* a=x, b=y (client coords)                      */
    /*
     * The pointer left this window for another one, or for the desktop.
     *
     * A window only ever hears where the pointer IS, and once it is somewhere
     * else that message goes to somebody else -- so a button lit under the
     * pointer stayed lit after the pointer had gone. Its own message rather
     * than a MOUSEMOVE with impossible coordinates, because half the windows
     * here use MOUSEMOVE to drag something and would have had to learn to
     * recognise one.
     *
     * Not sent while a mouse button is held: a drag that wanders off the
     * window is still that window's drag.
     */
    WM_MSG_MOUSELEAVE,
    WM_MSG_LBUTTONDOWN,  /* a=x, b=y                                      */
    WM_MSG_LBUTTONUP,    /* a=x, b=y                                      */
    WM_MSG_RBUTTONDOWN,  /* a=x, b=y                                      */
    /*
     * The wheel turned over this window. a = notches (positive is away from
     * the user), b = 0.
     *
     * It goes to the window under the POINTER, not the one with focus. Both
     * are defensible -- the systems this desktop is modelled on sent it to
     * the focused window -- but this one has a hover treatment on every
     * button and a leave message to go with it, so the pointer already means
     * "the thing I am working with" everywhere else, and a wheel that
     * scrolled a window the pointer was nowhere near would be the one place
     * it did not.
     *
     * Delivering it does NOT raise or focus the window: scrolling something
     * to read it is not the same as choosing it.
     */
    WM_MSG_MOUSEWHEEL,
    WM_MSG_KEYDOWN,      /* a=keycode, b=ascii, param=const int* mods      */
    WM_MSG_KEYUP,        /* a=keycode, b=ascii, param=const int* mods      */
    WM_MSG_COMMAND,      /* a=control/menu id                             */
    WM_MSG_TIMER,        /* a=timer id                                    */
    WM_MSG_FOCUS_GAINED,
    WM_MSG_FOCUS_LOST,
    WM_MSG_CLOSE,        /* user pressed the close box                    */
    /*
     * "Do you hold work nobody has saved?" An app answers by RETURNING CTRUE
     * from its proc; one that does not know the question returns CFALSE like
     * any other unhandled message, which is the right answer for a window
     * with nothing to lose.
     *
     * It exists because the close box is not the only way out. Shutting down
     * ends the session loop without closing a single window, so every
     * WM_MSG_CLOSE guard in the system is bypassed and a modified document
     * goes with it -- the same loss through a door none of the guards watch.
     */
    WM_MSG_QUERY_UNSAVED
} WmMessage;

typedef struct WmWindow WmWindow;

/*
 * Window procedure. Return CTRUE if the message was handled; CFALSE lets
 * the manager apply default behavior (e.g. destroy on WM_MSG_CLOSE).
 * For WM_MSG_PAINT, 'param' is a GfxSurface* whose clip is set to the
 * window's client rect (in surface coordinates); draw in client coords via
 * the helpers in wm_client_origin().
 */
typedef cbool (*WmProc)(WmWindow *win, WmMessage msg, long a, long b, void *param);

/* Window style flags. */
#define WM_STYLE_TITLE     0x01  /* has a title bar                       */
#define WM_STYLE_CLOSE     0x02  /* has a close box                       */
#define WM_STYLE_MINIMIZE  0x04
#define WM_STYLE_MAXIMIZE  0x08
#define WM_STYLE_BORDER    0x10  /* has a resizable/moveable frame        */
#define WM_STYLE_MODAL     0x20  /* blocks input to its owner             */
#define WM_STYLE_POPUP     0x40  /* frameless (menus): no NC area, no task */

/* A convenient default for ordinary app windows. */
#define WM_STYLE_APP (WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE | \
                      WM_STYLE_MAXIMIZE | WM_STYLE_BORDER)

typedef enum {
    WM_STATE_NORMAL = 0,
    WM_STATE_MINIMIZED,
    WM_STATE_MAXIMIZED
} WmWindowState;

/* Frame theme, supplied by the shell so the manager stays independent of the
 * shell's theme representation (Layer 3 must not depend on Layer 4). */
typedef struct {
    CColor title_active_l, title_active_r;
    CColor title_inactive_l, title_inactive_r;
    CColor title_text;
    CColor face, light, dark, darker;
    int    title_height;
    int    border_width;
    cbool  glossy;   /* draw XP-style sheen (off in 16-color / safe mode)      */
} WmTheme;

/* Bring the manager up/down. Requires gfx + a back buffer. */
CResult wm_init(GfxSurface *backbuffer);
void    wm_shutdown(void);

/* Install the frame colors/metrics (called by the shell at theme apply). */
void    wm_set_theme(const WmTheme *theme);

/* Reserve the usable work area (surface minus the taskbar strip). Maximized
 * windows fill this rather than the whole surface, so the taskbar stays
 * visible. The shell sets it at layout; defaults to the full back buffer. */
void    wm_set_work_area(const CRect *area);
/* The usable area: the surface minus the taskbar strip. */
CRect   wm_work_area(void);
/*
 * A frame of at most fw x fh, centred in that area -- what an app wants when
 * it opens a window. Sizing from plat_video_info instead measures the SCREEN,
 * which includes the taskbar, so a window taller than the work area puts its
 * own status bar underneath it and looks fine while having lost a row.
 */
CRect   wm_place_centered(int fw, int fh);

/*
 * Create a top-level window. 'title' is copied. 'proc' may be NULL for a
 * passive window. 'user' is stored for the proc to retrieve. Returns NULL
 * if the window pool is exhausted (CASTALIA_MAX_WINDOWS).
 */
WmWindow *wm_create(const char *title, const CRect *frame,
                    unsigned style, WmProc proc, void *user);

/* Destroy a window (sends WM_MSG_DESTROY, removes from z-order, invalidates
 * the area it covered). Safe on NULL. */
void wm_destroy(WmWindow *win);

/*
 * Lifecycle hook: the shell installs one callback the manager fires when a
 * top-level app window (has a title bar, not a menu popup) is created or
 * destroyed. It keeps window-open/close policy -- sound cues and open/close
 * animations -- in the shell without every app module wiring it up, and without
 * the manager depending on the shell or the sound layer. NULL clears it.
 */
typedef enum {
    WM_LIFE_OPEN = 0,   /* window just created                              */
    WM_LIFE_CLOSE,      /* window about to be destroyed                     */
    WM_LIFE_MINIMIZE,   /* window minimized (hidden to the taskbar)         */
    WM_LIFE_RESTORE     /* window restored from minimized                   */
} WmLifeEvent;
typedef void (*WmLifecycleHook)(WmWindow *win, WmLifeEvent ev);
void wm_set_lifecycle_hook(WmLifecycleHook hook);

/* Accessors. */
void       *wm_user(WmWindow *win);
CRect       wm_frame_rect(WmWindow *win);   /* whole window incl. NC       */
CRect       wm_client_rect(WmWindow *win);  /* client area, surface coords */
CPoint      wm_client_origin(WmWindow *win);/* client (0,0) in surface px  */
const char *wm_title(WmWindow *win);
void        wm_set_title(WmWindow *win, const char *title);
/* Optional title-bar / taskbar icon (a shell icon-pack surface, or NULL). The
 * WM only stores and blits it; the shell sets it (by window title) on open. */
void        wm_set_icon(WmWindow *win, const GfxSurface *icon);
const GfxSurface *wm_icon(WmWindow *win);
WmWindowState wm_state(WmWindow *win);
unsigned    wm_style(WmWindow *win);   /* WM_STYLE_* flags */

/* Visibility, focus, z-order. */
void wm_show(WmWindow *win, cbool visible);
cbool wm_is_visible(WmWindow *win);
void wm_focus(WmWindow *win);
WmWindow *wm_focused(void);
/* Cycle the window stack: raise the back-most visible window to the front and
 * focus it (the Alt+Tab switcher). Repeated calls rotate through all windows. */
void wm_cycle_focus(void);

/* ---- Virtual desktops ------------------------------------------------- */
/*
 * Each window lives on one of WM_DESKTOP_COUNT virtual desktops; only windows on
 * the current desktop are composited, hit-tested, and listed. New windows are
 * created on the current desktop. Switching desktops is instant -- windows keep
 * their state and geometry, they just leave/enter the screen. Something Windows
 * 98 SE never had.
 */
#define WM_DESKTOP_COUNT 4

int  wm_current_desktop(void);
int  wm_desktop_count(void);
/* Switch to desktop 'd' (clamped). Refocuses the front-most window there. */
void wm_set_desktop(int d);
/* Which desktop a window is on. */
int  wm_window_desktop(WmWindow *win);
/* Move a window to desktop 'd' (e.g. "send to desktop N"). */
void wm_move_window_to_desktop(WmWindow *win, int d);
void wm_bring_to_front(WmWindow *win);
/* Put a window at an arbitrary frame: invalidates both the area it left and
 * the area it now covers, and sends WM_MSG_MOVE / WM_MSG_SIZE for whichever
 * actually changed. A window in its normal state also takes the new frame as
 * the one a later restore returns to. */
void wm_set_frame(WmWindow *win, const CRect *frame);
void wm_set_state(WmWindow *win, WmWindowState state);
/* Minimize: hide the window, mark it minimized, and fire the lifecycle hook
 * first so the shell can animate it down to its taskbar button while the frame
 * is still on screen. Ignored for a window without WM_STYLE_MINIMIZE. */
void wm_minimize(WmWindow *win);
/* Ask a window to close, exactly as its close box does: its proc receives
 * WM_MSG_CLOSE and may handle it (prompt, veto), and only if it does not does
 * the manager destroy it. Not wm_destroy(), which does not ask. */
void wm_request_close(WmWindow *win);

/* How many open windows say they hold unsaved work (see
 * WM_MSG_QUERY_UNSAVED). Zero when nothing would be lost. */
int wm_unsaved_count(void);

/*
 * Mark part of a window as needing repaint, or all of it with NULL.
 *
 * The rectangle is in SCREEN coordinates, not client ones -- it is clipped
 * against the window's frame, which is where the window is. Passing a
 * layout rect straight from an app's own geometry (which is client-relative)
 * intersects two different coordinate spaces and usually invalidates
 * nothing at all; offset it by wm_client_origin() first. The parameter used
 * to be called client_rect_or_null, which is how that happens.
 */
void wm_invalidate(WmWindow *win, const CRect *screen_rect_or_null);

/* ---- Per-frame animation tick ---------------------------------------- */
/*
 * A window that opts in with wm_set_animated(win, CTRUE) receives one
 * WM_MSG_TIMER per shell frame; its proc updates state and re-invalidates to
 * self-animate (the media player's visualizer, the benchmark's growing bars).
 * Off by default, so a still window costs nothing. The shell calls
 * wm_tick_animations() once per frame.
 */
void wm_set_animated(WmWindow *win, cbool on);
/* ...and whether it did. A window that repaints itself every frame cannot be
 * compared across two of them, so the checks that do that skip it and say so
 * rather than reporting its animation as a repaint fault. */
cbool wm_is_animated(WmWindow *win);
void wm_tick_animations(void);

/* Feed a raw input event to the manager (called by the shell loop). Returns
 * CTRUE if a window consumed it. */
struct PlatEvent; /* fwd */
cbool wm_handle_event(const struct PlatEvent *ev);

/*
 * Dirty-rect compositing is split so the shell can sequence layers
 * correctly (desktop under, windows, then taskbar/cursor over):
 *   1. wm_collect_dirty() moves pending window invalidations into the shell's
 *      frame region and clears the manager's internal list. The shell then
 *      repaints the desktop under that region.
 *   2. wm_paint() draws every visible window (back to front) clipped to the
 *      region the shell hands back.
 */
void wm_collect_dirty(CRegion *region);
void wm_paint(const CRegion *region);

/*
 * Does this window have the keyboard?
 *
 * Asked at PAINT time, by anything that draws a selection. A window that is
 * not focused should not be drawing the one thing that says "your typing
 * lands here" -- a list whose highlight stays a live blue while the keys go
 * somewhere else is telling the user something untrue about their own
 * machine, and it is the reason Windows greyed an unfocused selection.
 */
cbool wm_has_focus(WmWindow *win);

/* Enumerate windows for the taskbar (front-to-back or creation order). */
int       wm_window_count(void);
WmWindow *wm_window_at(int index);

/* A small stable number for a window, unique among the windows alive at the
 * same time. The shell uses it to remember an order across frames without
 * holding pointers to windows that may be destroyed under it. Returns -1 for
 * NULL. It is a slot number, so it IS reused once a window closes -- fine for
 * "which window is this right now", wrong for anything that must outlive the
 * window. */
int       wm_window_id(WmWindow *win);
/* The live window with that id, or NULL if it has since closed. */
WmWindow *wm_window_by_id(int id);
/* Hit-test the topmost visible window at a point (NULL if none / desktop). */
WmWindow *wm_hit_test(int x, int y);
/* Is the point on this window's title bar, clear of the caption buttons? The
 * shell asks before raising the window menu on a right-click, so it does not
 * have to know the frame metrics. CFALSE for a window with no title bar. */
cbool     wm_titlebar_hit(WmWindow *win, int x, int y);

/* Modal support: a visible window with WM_STYLE_MODAL captures all input until
 * it closes. The shell checks wm_has_modal() so clicks on the taskbar/desktop
 * are blocked while a dialog is up. */
cbool     wm_has_modal(void);
WmWindow *wm_topmost_modal(void);

/* ---- Window snapping (drag a title bar to a screen edge) ------------- */
/*
 * Dragging a window's title bar so the pointer reaches a work-area edge snaps
 * the window: the left/right edges tile it to that half, the top edge maximizes
 * it -- the desktop-tiling gesture Windows 98 SE never had. The zone/rect math
 * is pure and host-tested (tests/test_snap.c).
 */
typedef enum {
    WM_SNAP_NONE = 0,
    WM_SNAP_LEFT,   /* left half of the work area   */
    WM_SNAP_RIGHT,  /* right half                   */
    WM_SNAP_TOP     /* whole work area (maximize)   */
} WmSnapZone;

#define WM_SNAP_EDGE 12   /* px from an edge that arms a snap */

/* Which snap zone the point (px,py) is in for the given work area, or
 * WM_SNAP_NONE. */
WmSnapZone wm_snap_zone(const CRect *work, int px, int py, int edge);
/* The target frame for a zone within the work area. */
CRect      wm_snap_rect(const CRect *work, WmSnapZone zone);

/* While a title-bar drag hovers a snap edge, returns CTRUE and fills '*out'
 * with the target snap rectangle so the shell can draw a live preview outline;
 * CFALSE when no snap is currently armed. */
cbool      wm_drag_snap_preview(CRect *out);

/*
 * The rubber band of an outline drag, when one is in progress ([Shell]
 * DragOutline). The shell draws it over the scene; the window itself has not
 * moved and will not until the button comes up.
 */
cbool      wm_drag_outline(CRect *out);

#endif /* CASTALIA_WM_H */
