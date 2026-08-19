/*
 * sh_internal.h - Shared state and helpers for the shell modules.
 * Not a public header.
 */
#ifndef CASTALIA_SH_INTERNAL_H
#define CASTALIA_SH_INTERNAL_H

#include "castalia/shell.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/plat.h"

typedef struct {
    GfxSurface  *back;
    ShTheme      theme;
    cbool        safe_mode;

    int          screen_w, screen_h;

    /* Per-frame repaint accumulation (desktop/taskbar/launcher invalidations). */
    CRegion      frame_dirty;
    cbool        full_repaint;

    /* Launcher menu overlay -- an XP-style two-column Start panel: the left
     * column lists programs, the right column lists places & system tools, and
     * a green footer band carries the session buttons (Log Off / Shut Down). */
    cbool        launcher_open;
    UiMenu       launcher_menu;     /* left column: programs                 */
    UiMenu       launcher_menu2;    /* right column: places & system         */
    int          launcher_focus;    /* keyboard focus: 0 left, 1 right col   */
    int          launcher_foot_hl;  /* hovered footer button: -1/0/1         */
    CRect        launcher_button;   /* on the taskbar                       */
    CRect        launcher_rect;     /* menu popup bounds when open           */
    int          launcher_reveal;   /* slide-up progress (0..SH_MENU_REVEAL) */
    /*
     * How far each column is scrolled, in PIXELS from the top of its content.
     *
     * A column taller than the work area used to have its extra rows dropped:
     * not drawn and not clickable, which is indistinguishable from never
     * having been added. The shipped lists fit at 640x480 and --launch-demo
     * fails if they stop fitting, but .CAPP add-ons are discovered at RUN
     * time and no build-time check can see them -- so the overflow has to be
     * survivable rather than merely detected.
     */
    int          launcher_scroll[2];
    int          launcher_bar_hot;  /* held scroll-bar part, 0 = none        */
    int          launcher_bar_col;  /* which column that part belongs to     */

    /* Context (right-click) menu overlay. */
    cbool        ctx_open;
    UiMenu       ctx_menu;
    int          ctx_x, ctx_y;      /* top-left where the menu was raised    */
    CRect        ctx_rect;          /* menu popup bounds when open           */
    /* The window a window-menu acts on, as a wm id rather than a pointer: the
     * menu outlives a frame, and a window can be destroyed while it is up (by
     * its own timer, or by whatever the app is doing). -1 = not a window menu. */
    int          ctx_win;

    /* Keyboard move / size mode (the window menu's Move and Size entries).
     * While one is running the arrow keys drive the window instead of reaching
     * it, Enter keeps the result and Esc puts it back where it started. */
    int          kmode;          /* SH_K_NONE / SH_K_MOVE / SH_K_SIZE       */
    int          kwin;           /* the window being driven, as a wm id     */
    CRect        korig;          /* its frame when the mode began (for Esc) */

    /* Taskbar. */
    CRect        taskbar_rect;
    int          taskbar_h;

    /* Tooltip overlay (hover hint; sh_tooltip.c). */
    cbool        tip_open;
    CRect        tip_rect;
    char         tip_text[96];

    /* Input snapshot. */
    int          mouse_x, mouse_y, mouse_b;

    ShExitReason exit_reason;
    cbool        running;
} ShState;

extern ShState g_sh;

/* Frames for the launcher menu slide-up reveal (0 = closed, this = full). */
#define SH_MENU_REVEAL 5

/* Keyboard move/size modes, and how far one arrow press goes. Eight pixels is
 * a compromise found by using it: one is unusable (a window takes a hundred
 * presses to cross the screen), thirty-two overshoots everything worth lining
 * up against. */
#define SH_K_NONE 0
#define SH_K_MOVE 1
#define SH_K_SIZE 2
#define SH_K_STEP 8

/* Keyboard move/size (sh_kmove.c). Begin a mode on 'win'; feed it keys while
 * it runs. sh_kmove_key returns CTRUE when it consumed the key. */
void  sh_kmove_begin(WmWindow *win, int mode);
cbool sh_kmove_active(void);
cbool sh_kmove_key(int key);
/* End the mode, keeping where the window has got to. Called when anything
 * else happens: a click, a window closing, the shell shutting down. */
void  sh_kmove_end(void);
/* The window being driven, or -1. */
int   sh_kmove_window(void);

/* Drop-shadow margin the shell's overlays (menus) cast, matching WM_SHADOW. */
#define SH_SHADOW 5

/* sh_glossy() is public and lives in castalia/shell.h. It was declared here
 * as well, so every file including both got the same prototype twice. */

/* Add a rectangle to the current frame's repaint region. */
void sh_mark_dirty(const CRect *r);
/* sh_switch_desktop is public (shell.h). */
/* Is the Alt+Tab panel on screen? For the scenes that measure it. */
cbool sh_switch_showing(void);

/* Safe-mode (high-contrast, minimal) theme, defined in sh_theme.c. */
void sh_theme_safe_mode(ShTheme *out);

/* Optional icon pack (sh_iconpack.c). The shell draws icons procedurally by
 * default; a BMP pack pointed to by [Assets] Icons= overrides them by NAME.
 * Desktop, Quick Launch, and the File Manager toolbar request icons by name
 * (see assets/icons/README.md for the slot names); a missing icon returns NULL
 * and the caller falls back to its procedural/text drawing. Icons load lazily
 * and are cached (misses too) for the session. */
void sh_iconpack_set_dir(const char *dir);   /* NULL/empty -> procedural art  */
void sh_iconpack_free(void);
/* sh_iconpack_icon(name) is public (shell.h) so apps can use themed icons. */

/* Desktop layer. */
void sh_desktop_init(void);
void sh_desktop_paint(const CRect *clip);
cbool sh_desktop_handle_click(int x, int y, int buttons);
/* Desktop-icon drag: motion/up consume the event only while an icon is being
 * dragged (returns CTRUE), otherwise the shell routes it on to the wm. */
cbool sh_desktop_handle_motion(int x, int y);
cbool sh_desktop_handle_up(int x, int y);
cbool sh_desktop_is_dragging(void);
/* Arrow keys move the icon selection, Enter opens it. Only reached when the
 * desktop is the only thing on screen -- no window focused, no menu up.
 * CTRUE when the key was consumed. */
cbool sh_desktop_handle_key(int key);
/* The selected icon index, or -1. For the headless driver. */
int   sh_desktop_selected(void);
/* Repaint every desktop icon next frame. The selection is drawn live only
 * while the desktop HAS the arrow keys -- which is only while no window is
 * focused -- so opening or closing the last window changes how it looks
 * without touching a pixel of it. */
void  sh_desktop_mark_icons(void);
/* Pixels composited on the last frame: the area of the region that was
 * actually repainted and presented. "What did that keystroke cost" is not a
 * question a screenshot can answer, and on the target it is the number that
 * decides whether an editor feels instant. */
long  sh_last_frame_px(void);
cbool sh_desktop_icon_rect(int i, CRect *out);
/* Persist / restore desktop icon positions in CASTALIA.INI's [Desktop] section
 * so a dragged layout survives a restart. Restore runs once at startup. */
void sh_desktop_save_positions(void);
void sh_desktop_load_positions(void);
/* Right-click on a desktop icon: select it and raise its context menu. Returns
 * CTRUE if an icon was hit (caller then skips the empty-desktop menu). */
cbool sh_desktop_icon_context(int x, int y);
/* Static-background cache (gradient + castle + watermark). Rebuild on theme or
 * screen-size change; free at shutdown. The blit path replaces a per-pixel
 * gradient recompute on every dirty rectangle. */
void sh_desktop_build_cache(void);
/* How many times the cache has actually been rebuilt. A call that finds its
 * inputs unchanged does not count -- which is the whole point, and is what
 * --deskcache-demo checks. */
long sh_desktop_cache_builds(void);
void sh_desktop_free_cache(void);
void sh_desktop_set_cache_enabled(cbool on);   /* benchmark hook */

/* Taskbar layer. */
void sh_taskbar_layout(void);
void sh_taskbar_paint(const CRect *clip);
/* Repaint just the Start orb, over anything composited after the bar. */
void sh_taskbar_paint_orb(void);

/* The cached desktop background -- the wallpaper alone, without the icons that
 * are composited over it. NULL when no cache has been built. */
const GfxSurface *sh_desktop_background(void);

/* Draw the landscape whole (CFALSE) instead of skipping the parts that are
 * about to be painted over -- the hill layers below the next layer's crest,
 * and the haze band below the hills. Same picture, painted the slow way, for
 * --vale-demo, which bakes it both ways and requires the two to match. */
void sh_desktop_set_hill_limits(cbool on);
/* A checksum over every pixel of the cached background. */
cu32 sh_desktop_cache_sum(void);
cbool sh_taskbar_handle_click(int x, int y, int buttons);
/* Update the hovered taskbar element from the pointer; repaints on change. */
void sh_taskbar_update_hover(int mx, int my);
/* The taskbar button rect for a window (minimize/restore anim anchor). */
cbool sh_taskbar_button_rect(WmWindow *w, CRect *out);
/* Mark the taskbar dirty INCLUDING the Start orb's overhang above the bar. */
void sh_taskbar_dirty(void);
/* (Re)build / free the cached Start-orb sprites (built at layout/theme). */
void sh_taskbar_build_orb(void);
void sh_taskbar_free_orb(void);

/* Launcher overlay. */
void  sh_launcher_build(void);
void  sh_launcher_paint(const CRect *clip);
/* CTRUE when the last launcher layout was too tall for the work area and had
 * to drop rows -- which makes those entries undrawable and unclickable. */
cbool sh_launcher_clamped(void);
cbool sh_launcher_handle_click(int x, int y, int buttons);
void  sh_launcher_handle_motion(int x, int y);
/* Keyboard navigation while the menu is open: Up/Down move the highlight,
 * Enter activates. Returns CTRUE when the menu consumed the key. */
cbool sh_launcher_handle_key(int key);
/* The wheel over an open panel scrolls the column under the pointer. CTRUE
 * when the panel took it (including "over the panel, nothing to scroll" --
 * the notch must not fall through to the window behind). */
cbool sh_launcher_handle_wheel(int x, int y, int notches);
/* Let go of a held scroll-bar part. */
void  sh_launcher_release(void);
/* For the scenes: what a click at (x,y) would launch (-1 for nothing), and
 * whether a column carries a scroll bar. An entry scrolled off the bottom
 * looks exactly like one that was never added; only a hit test tells them
 * apart. */
int   sh_launcher_command_at(int x, int y);
cbool sh_launcher_bar_rect(int which, CRect *out);

/* Context (right-click) menu overlay. A shell-owned popup that reuses ui_menu;
 * opened at a point with a caller-built menu and dispatches like the launcher. */
void  sh_context_open_desktop(int x, int y);
/* Raise a desktop-icon context menu: "Open" (dispatches open_cmd) plus, for the
 * Recycle Bin, "Empty Recycle Bin" (disabled when the bin is already empty). */
void  sh_context_open_icon(int x, int y, int open_cmd, cbool is_bin,
                           cbool bin_has_files);
/* Raise the window menu for 'win' at (x,y): Restore / Minimize / Maximize,
 * "Send to Desktop N", and Close. Items the window's style or state rules out
 * are shown disabled rather than hidden, so the menu keeps its shape. */
void  sh_context_open_window(WmWindow *win, int x, int y);
/* Act on the window a window menu was raised over. CTRUE if 'cmd' was one of
 * the window commands (whether or not the window is still alive). */
cbool sh_context_window_command(int cmd);
void  sh_context_close(void);
cbool sh_context_is_open(void);
void  sh_context_paint(const CRect *clip);
void  sh_context_handle_motion(int x, int y);
/* Returns CTRUE if the click was consumed by the menu. */
cbool sh_context_handle_click(int x, int y, int buttons);
/* Keyboard nav while open (arrows/Enter); CTRUE if consumed. */
cbool sh_context_handle_key(int key);

/* Tooltip overlay (sh_tooltip.c). Shown centered on 'cx' with its bottom edge
 * at 'bottom', clamped on-screen; hover timing lives in sh_taskbar.c. */
void  sh_tooltip_show(const char *text, int cx, int bottom);
void  sh_tooltip_hide(void);
cbool sh_tooltip_is_open(void);
void  sh_tooltip_paint(const CRect *clip);

/* The Win98-style "Shut Down" modal (radio options -> exit reason). */
void  sh_shutdown_dialog(void);
/* End the session, asking first if any window holds unsaved work. The one
 * place that stops the loop -- see the note in sh_core.c. */
void  sh_end_session(ShExitReason reason);

/* UI animations (sh_anim.c). Window open/close "zoom" overlays, frame-budgeted
 * and dirty-rect friendly. */
/* The Castalia mark (castle on hills inside a blue C), drawn at any size.
 * Shared by the Start button, launcher header, splash and About crest. */
void  sh_logo_draw(GfxSurface *s, int x, int y, int size);
/* The same mark on a pale disc, for the blue surfaces (taskbar, launcher
 * header) where a blue ring would otherwise disappear. */
void  sh_logo_draw_plated(GfxSurface *s, int x, int y, int size);

void  sh_anim_window(const CRect *frame, cbool opening);
/* Zoom an outline between two arbitrary rects (minimize/restore to a taskbar
 * button). 'restoring' only labels the debug log. */
void  sh_anim_zoom(const CRect *from, const CRect *to, cbool restoring);
cbool sh_anim_active(void);
void  sh_anim_collect(CRegion *region);   /* add outline bounds to repaint     */
void  sh_anim_draw(GfxSurface *s);        /* draw outlines, then advance       */
void  sh_anim_set_enabled(cbool en);
void  sh_anim_clear(void);
/* sh_anim_set_debug is public (shell.h) so the host demo can enable it. */

/* Cursor (save-under software cursor). */
/* declared in shell.h: sh_cursor_* */

#endif /* CASTALIA_SH_INTERNAL_H */
