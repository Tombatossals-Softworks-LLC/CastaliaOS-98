/*
 * wm_internal.h - Shared internals for the window manager modules.
 * Not a public header.
 */
#ifndef CASTALIA_WM_INTERNAL_H
#define CASTALIA_WM_INTERNAL_H

#include "castalia/wm.h"

struct WmWindow {
    cbool         in_use;
    unsigned      style;
    WmWindowState state;
    CRect         frame;        /* whole window incl. non-client, surface px */
    CRect         restore_frame;/* saved frame for un-maximize             */
    char          title[CASTALIA_MAX_NAME];
    const GfxSurface *icon;     /* optional title-bar / taskbar icon (or NULL) */
    WmProc        proc;
    void         *user;
    cbool         visible;
    cbool         animated;     /* opts into a per-frame WM_MSG_TIMER tick   */
    int           id;           /* stable pool index                        */
    int           desktop;      /* which virtual desktop it lives on        */
};

/* Global manager state shared across wm_*.c */
typedef struct {
    struct WmWindow pool[CASTALIA_MAX_WINDOWS];
    int             zorder[CASTALIA_MAX_WINDOWS]; /* pool indices, back->front */
    int             zcount;
    struct WmWindow *focus;
    GfxSurface     *back;
    WmTheme         theme;
    CRect           work_area;   /* usable area for maximize (minus taskbar) */
    int             current_desktop; /* active virtual desktop (0..N-1)      */
    WmLifecycleHook life_hook;   /* shell-installed open/close notification   */
    cbool           inited;
} WmState;

extern WmState g_wm;

/* On-screen == in use, visible, and on the current virtual desktop. Windows on
 * other desktops are never composited, hit-tested, or focused. */
cbool wm_on_screen(const struct WmWindow *w);
/* Fire the shell lifecycle hook for an app window (title, non-popup). */
void  wm_notify_life(struct WmWindow *w, WmLifeEvent ev);

/* Drop shadow the compositor casts to the right/bottom of every window when
 * the theme is glossy. Dirty rects are inflated by this margin (see
 * wm_dirty_add) so moving/closing a window never leaves shadow remnants. */
#define WM_SHADOW 5

/*
 * Drop every dispatcher pointer to this window (wm_dispatch.c): the drag, the
 * resize, the hover, and a pending first title-bar click. The pool reuses
 * slots, so a stale one is either a wrong comparison or -- for the drag and
 * the resize, which are written through on the next mouse move -- a write
 * into a window that is not there any more. Called from wm_destroy.
 */
void wm_forget_window(struct WmWindow *w);

/* Caption-button rectangles (close/min/max), computed from the frame. */
CRect wm_frame_titlebar(const struct WmWindow *w);
CRect wm_frame_close_btn(const struct WmWindow *w);
CRect wm_frame_min_btn(const struct WmWindow *w);
CRect wm_frame_max_btn(const struct WmWindow *w);

/* Draw the whole window (non-client + client) clipped to 'clip'. */
void  wm_draw_window(struct WmWindow *w, const CRect *clip);

/* Send a message to a window's proc (with manager default handling). */
cbool wm_send(struct WmWindow *w, WmMessage msg, long a, long b, void *param);

/* Queue an arbitrary rectangle for repaint (implemented in wm_dispatch.c).
 * Used when a window's frame changes shape/position and both the vacated and
 * the new area must be repainted. */
void  wm_dirty_add(const CRect *r);

#endif /* CASTALIA_WM_INTERNAL_H */
