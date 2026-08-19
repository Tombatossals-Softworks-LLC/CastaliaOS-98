/*
 * sh_kmove.c - Moving and resizing a window from the keyboard.
 *
 * The window menu could Restore, Minimize, Maximize, send a window to another
 * desktop and Close it -- everything except put it somewhere. Moving and
 * resizing were still mouse-only, which on a machine where the mouse is
 * optional means a window that opened in the wrong place stayed there.
 *
 * The model is the one the systems this imitates use, and it is worth being
 * explicit about why: the mode is MODAL. While it runs, the arrow keys drive
 * the window instead of reaching the app inside it, Enter keeps the result and
 * Esc puts the window back exactly where it started. A modeless version --
 * "Ctrl+Alt+arrows always nudge the focused window" -- needs a chord the DOS
 * keyboard path cannot reliably deliver, and it steals four keys from every
 * app forever. This borrows them for a few seconds and gives them back.
 *
 * The window moves for real on every press rather than dragging an outline
 * around. An outline would need its own overlay, its own dirty rectangles and
 * its own erase-on-cancel; moving the window is less code and shows the truth,
 * and Esc can put it back because the frame it started at is remembered here.
 *
 * Where a window is ALLOWED to end up is not decided here -- that is wm_move.c,
 * shared with the mouse drag and tested in tests/test_move.c. This file is the
 * mode: what a key means while it is running, and how it ends.
 */
#include "sh_internal.h"
#include "../wm/wm_move.h"
#include "castalia/sys.h"

/* Put the hint back up. Any keypress hides the tooltip (see sh_core.c), which
 * is right everywhere else and wrong here, where the keys ARE the interaction
 * -- so it is re-shown after each one. */
static void kmove_hint(void)
{
    const char *msg = (g_sh.kmode == SH_K_SIZE)
        ? "Size: arrows resize, Enter keeps it, Esc cancels"
        : "Move: arrows move it, Enter keeps it, Esc cancels";
    sh_tooltip_show(msg, g_sh.screen_w / 2, g_sh.taskbar_rect.y0 - 4);
}

void sh_kmove_begin(WmWindow *win, int mode)
{
    if (win == NULL || (mode != SH_K_MOVE && mode != SH_K_SIZE)) { return; }
    /* A maximized window has nowhere to go and no room to grow, and silently
     * un-maximizing it to make the mode work would be a surprise. */
    if (wm_state(win) != WM_STATE_NORMAL || !wm_is_visible(win)) { return; }
    g_sh.kmode = mode;
    g_sh.kwin  = wm_window_id(win);
    g_sh.korig = wm_frame_rect(win);
    kmove_hint();
    SYS_LOGD("sh", "keyboard %s on '%s'",
             (mode == SH_K_SIZE) ? "size" : "move", wm_title(win));
}

cbool sh_kmove_active(void)
{
    return (g_sh.kmode != SH_K_NONE) ? CTRUE : CFALSE;
}

int sh_kmove_window(void) { return (g_sh.kmode != SH_K_NONE) ? g_sh.kwin : -1; }

void sh_kmove_end(void)
{
    if (g_sh.kmode == SH_K_NONE) { return; }
    g_sh.kmode = SH_K_NONE;
    g_sh.kwin  = -1;
    sh_tooltip_hide();
}

cbool sh_kmove_key(int key)
{
    WmWindow *w;
    CRect f, nf;
    int dx = 0, dy = 0;

    if (g_sh.kmode == SH_K_NONE) { return CFALSE; }
    /* The window can close underneath the mode -- an app may destroy its own
     * window on a timer -- so the mode is only as alive as its subject. */
    w = wm_window_by_id(g_sh.kwin);
    if (w == NULL) { sh_kmove_end(); return CFALSE; }

    if (key == PLAT_KEY_ESC) {
        wm_set_frame(w, &g_sh.korig);
        sh_kmove_end();
        return CTRUE;
    }
    if (key == PLAT_KEY_ENTER) { sh_kmove_end(); return CTRUE; }

    switch (key) {
    case PLAT_KEY_LEFT:  dx = -SH_K_STEP; break;
    case PLAT_KEY_RIGHT: dx =  SH_K_STEP; break;
    case PLAT_KEY_UP:    dy = -SH_K_STEP; break;
    case PLAT_KEY_DOWN:  dy =  SH_K_STEP; break;
    default:
        /* Anything else means the user has moved on. Keep where the window has
         * got to and let the key through to whatever it was meant for, rather
         * than swallowing it into a mode they have forgotten they are in. */
        sh_kmove_end();
        return CFALSE;
    }

    f = wm_frame_rect(w);
    if (g_sh.kmode == SH_K_MOVE) {
        nf = wm_move_clamp(&f, f.x0 + dx, f.y0 + dy,
                           g_sh.screen_w, g_sh.screen_h);
    } else {
        /* Size grows and shrinks at the right and bottom edges, so the corner
         * the user is looking at (the top-left, where the title is) stays put.
         * Resizing from the left edge would slide the whole window sideways as
         * it grew, which reads as a move going wrong. */
        nf = wm_size_clamp(&f, crect_w(&f) + dx, crect_h(&f) + dy,
                           g_sh.screen_w, g_sh.screen_h);
    }
    wm_set_frame(w, &nf);
    kmove_hint();
    return CTRUE;
}
