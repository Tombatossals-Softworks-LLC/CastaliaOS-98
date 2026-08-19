/*
 * sh_context.c - Right-click context menu overlay.
 *
 * A shell-owned popup that reuses the ui_menu control (like the launcher) but
 * is raised at an arbitrary point. Today it serves the desktop context menu;
 * the same machinery will back window/taskbar context menus. Selecting an item
 * routes through sh_dispatch_command, so context entries share the launcher's
 * command space.
 */
#include "sh_internal.h"
#include "castalia/gfx.h"
#include "castalia/snd.h"
#include "castalia/sys.h"

/* ---- open / close ---------------------------------------------------- */
/* Position the just-built ctx_menu at the click, clamped so the whole menu
 * stays on screen and clear of the taskbar, then raise it. 'target' is the
 * window a window-menu acts on (-1 for the menus that act on nothing); it is
 * a parameter rather than something each opener remembers to set, because a
 * stale target from the previous menu would act on the wrong window. */
static void ctx_raise(int x, int y, int target)
{
    UiMenu *m = &g_sh.ctx_menu;
    int w, h;
    m->highlight = -1;
    g_sh.ctx_win = target;
    /* The context menu and launcher are mutually exclusive popups. */
    sh_open_launcher(CFALSE);
    ui_menu_measure(m, GFX_FONT_SYSTEM, &w, &h);
    if (x + w > g_sh.screen_w) { x = g_sh.screen_w - w; }
    if (y + h > g_sh.taskbar_rect.y0) { y = g_sh.taskbar_rect.y0 - h; }
    if (x < 0) { x = 0; }
    if (y < 0) { y = 0; }
    g_sh.ctx_x = x; g_sh.ctx_y = y;
    g_sh.ctx_rect = crect_make(x, y, w, h);
    g_sh.ctx_open = CTRUE;
    snd_menu();
    sh_mark_dirty(&g_sh.ctx_rect);
}

void sh_context_open_desktop(int x, int y)
{
    UiMenu *m = &g_sh.ctx_menu;
    ui_menu_clear(m);
    ui_menu_add(m, SH_CMD_REFRESH,      "Refresh",             CTRUE);
    ui_menu_set_last_icon(m, sh_iconpack_icon("m-restart"));
    ui_menu_add(m, SH_CMD_ARRANGE,      "Line up Icons",       CTRUE);
    ui_menu_set_last_icon(m, sh_iconpack_icon("m-app"));
    ui_menu_add(m, SH_CMD_CAPTURE,      "Capture Screen",      CTRUE);
    ui_menu_set_last_icon(m, sh_iconpack_icon("m-app"));
    ui_menu_add_separator(m);
    ui_menu_add(m, SH_CMD_SYSINFO,      "System Information",  CTRUE);
    ui_menu_set_last_icon(m, sh_iconpack_icon("m-sysinfo"));
    ui_menu_add(m, SH_CMD_TASKMANAGER,  "Task Manager",        CTRUE);
    ui_menu_set_last_icon(m, sh_iconpack_icon("m-taskman"));
    ui_menu_add(m, SH_CMD_CONTROLCENTER,"Control Center",      CTRUE);
    ui_menu_set_last_icon(m, sh_iconpack_icon("m-control"));
    ui_menu_add_separator(m);
    ui_menu_add(m, SH_CMD_DISPLAYPROPS, "Properties",          CTRUE);
    ui_menu_set_last_icon(m, sh_iconpack_icon("m-control"));
    ui_menu_add(m, SH_CMD_ABOUT,        "About CastaliaOS...", CTRUE);
    ui_menu_set_last_icon(m, sh_iconpack_icon("m-help"));
    ctx_raise(x, y, -1);
}

void sh_context_open_icon(int x, int y, int open_cmd, cbool is_bin,
                          cbool bin_has_files)
{
    UiMenu *m = &g_sh.ctx_menu;
    ui_menu_clear(m);
    ui_menu_add(m, open_cmd, "Open", CTRUE);
    ui_menu_set_last_icon(m, sh_iconpack_icon("m-fileman"));
    if (is_bin) {
        ui_menu_add_separator(m);
        ui_menu_add(m, SH_CMD_EMPTYTRASH, "Empty Recycle Bin", bin_has_files);
        ui_menu_set_last_icon(m, sh_iconpack_icon("m-restart"));
    }
    ctx_raise(x, y, -1);
}

/* ---- the window menu -------------------------------------------------- */
/*
 * Everything a window can be told to do, in one place. It exists for the
 * keyboard: Alt+F4 was the only window-level key binding, so without this a
 * keyboard-only user -- which is how this system was verified on real
 * hardware -- could open and close windows but never minimize, maximize or
 * restore one, and could not move a window to another virtual desktop at all,
 * since nothing anywhere called wm_move_window_to_desktop().
 *
 * Items that do not apply are DISABLED rather than dropped. A menu that
 * changes length as a window changes state is a menu whose items move under
 * the pointer between one press and the next.
 */
void sh_context_open_window(WmWindow *win, int x, int y)
{
    UiMenu *m = &g_sh.ctx_menu;
    unsigned style;
    WmWindowState st;
    int here, i, n;
    char label[24];

    if (win == NULL) { return; }
    style = wm_style(win);
    st    = wm_state(win);
    here  = wm_window_desktop(win);
    n     = wm_desktop_count();
    if (n > SH_CMD_WIN_DESKTOP_N) { n = SH_CMD_WIN_DESKTOP_N; }

    ui_menu_clear(m);
    /* Restore covers both directions a window can be away from normal: back
     * down from maximized, and back out of the taskbar from minimized. */
    ui_menu_add(m, SH_CMD_WIN_RESTORE,  "Restore",
                (st != WM_STATE_NORMAL) ? CTRUE : CFALSE);
    /* Move and Size hand the arrow keys to the window for a moment. Both need
     * a window that is on screen at its normal size: there is nowhere to move
     * a maximized window to, and nothing to see while one is minimized. */
    ui_menu_add(m, SH_CMD_WIN_MOVE, "Move",
                (st == WM_STATE_NORMAL && wm_is_visible(win)) ? CTRUE : CFALSE);
    ui_menu_add(m, SH_CMD_WIN_SIZE, "Size",
                (st == WM_STATE_NORMAL && wm_is_visible(win) &&
                 (style & WM_STYLE_BORDER)) ? CTRUE : CFALSE);
    ui_menu_add(m, SH_CMD_WIN_MINIMIZE, "Minimize",
                ((style & WM_STYLE_MINIMIZE) && st != WM_STATE_MINIMIZED)
                    ? CTRUE : CFALSE);
    ui_menu_add(m, SH_CMD_WIN_MAXIMIZE, "Maximize",
                ((style & WM_STYLE_MAXIMIZE) && st != WM_STATE_MAXIMIZED)
                    ? CTRUE : CFALSE);
    if (n > 1) {
        ui_menu_add_separator(m);
        for (i = 0; i < n; i++) {
            sys_snprintf(label, sizeof(label), "Send to Desktop %d", i + 1);
            /* The desktop it is already on is listed and disabled: leaving it
             * out would renumber the rest as the window moves around. */
            ui_menu_add(m, SH_CMD_WIN_DESKTOP + i, label,
                        (i != here) ? CTRUE : CFALSE);
        }
    }
    ui_menu_add_separator(m);
    /* No icons on this menu. The icon pack has names for the things the
     * launcher opens, not for what a window can be told to do, and borrowing
     * a near-enough one (a restart glyph beside "Close") would be a small lie
     * repeated every time the menu is opened. */
    ui_menu_add(m, SH_CMD_WIN_CLOSE, "Close",
                (style & WM_STYLE_CLOSE) ? CTRUE : CFALSE);
    ctx_raise(x, y, wm_window_id(win));
}

cbool sh_context_window_command(int cmd)
{
    WmWindow *w;
    if (cmd < SH_CMD_WIN_RESTORE ||
        cmd >= SH_CMD_WIN_DESKTOP + SH_CMD_WIN_DESKTOP_N) { return CFALSE; }
    /* The window may have closed between the menu opening and this arriving --
     * an app can destroy its own window on a timer -- and a wm id whose slot
     * has been reused belongs to somebody else now. wm_window_by_id() answers
     * both questions; a NULL is a command with nothing left to act on. */
    w = wm_window_by_id(g_sh.ctx_win);
    if (w == NULL) { return CTRUE; }
    switch (cmd) {
    case SH_CMD_WIN_RESTORE:
        if (wm_state(w) == WM_STATE_MINIMIZED) {
            wm_show(w, CTRUE);            /* also clears MINIMIZED + refocuses */
            wm_invalidate(w, NULL);
            sh_taskbar_dirty();
        } else {
            wm_set_state(w, WM_STATE_NORMAL);
        }
        break;
    case SH_CMD_WIN_MOVE:
        sh_kmove_begin(w, SH_K_MOVE);
        break;
    case SH_CMD_WIN_SIZE:
        if (wm_style(w) & WM_STYLE_BORDER) { sh_kmove_begin(w, SH_K_SIZE); }
        break;
    case SH_CMD_WIN_MINIMIZE:
        wm_minimize(w);                   /* no-op without WM_STYLE_MINIMIZE */
        sh_taskbar_dirty();
        break;
    case SH_CMD_WIN_MAXIMIZE:
        if (wm_style(w) & WM_STYLE_MAXIMIZE) {
            /* Maximizing something that is currently down in the taskbar has
             * to bring it back up first, or it would be maximized and still
             * invisible -- the window equivalent of tidying a room nobody can
             * get into. */
            if (wm_state(w) == WM_STATE_MINIMIZED) {
                wm_show(w, CTRUE);
                sh_taskbar_dirty();
            }
            wm_set_state(w, WM_STATE_MAXIMIZED);
            wm_invalidate(w, NULL);
        }
        break;
    case SH_CMD_WIN_CLOSE:
        if (wm_style(w) & WM_STYLE_CLOSE) { wm_request_close(w); }
        break;
    default:
        wm_move_window_to_desktop(w, cmd - SH_CMD_WIN_DESKTOP);
        sh_taskbar_dirty();
        break;
    }
    return CTRUE;
}

void sh_context_close(void)
{
    CRect sr;
    if (!g_sh.ctx_open) { return; }
    g_sh.ctx_open = CFALSE;
    g_sh.ctx_menu.highlight = -1;
    /* Erase the menu AND its drop shadow. */
    sr = g_sh.ctx_rect;
    if (sh_glossy()) { sr.x1 += SH_SHADOW; sr.y1 += SH_SHADOW; }
    sh_mark_dirty(&sr);
}

cbool sh_context_is_open(void) { return g_sh.ctx_open; }

/* ---- paint ----------------------------------------------------------- */
void sh_context_paint(const CRect *clip)
{
    CRect sr;
    if (!g_sh.ctx_open) { return; }
    sr = g_sh.ctx_rect;
    if (sh_glossy()) { sr.x1 += SH_SHADOW; sr.y1 += SH_SHADOW; }
    if (clip != NULL && !crect_overlaps(&sr, clip)) { return; }
    if (clip != NULL) { gfx_set_clip(g_sh.back, clip); }
    if (sh_glossy()) { gfx_drop_shadow(g_sh.back, &g_sh.ctx_rect, SH_SHADOW, 90); }
    ui_menu_draw(g_sh.back, &g_sh.ctx_menu, g_sh.ctx_x, g_sh.ctx_y,
                 GFX_FONT_SYSTEM);
    gfx_reset_clip(g_sh.back);
}

/* ---- input ----------------------------------------------------------- */
/*
 * Bring back the two rows a highlight moved between, rather than the panel.
 *
 * This is ui_menu_repaint() with the shell's marking instead of a window's:
 * the geometry helper is shared, only the destination differs, because the
 * desktop menu is a shell layer in SCREEN coordinates and has no window to
 * invalidate against. Same rule as the window menus, same rule as the Start
 * panel; the third spelling of it, and the last one left.
 */
static void ctx_mark_rows(int prev, int now)
{
    int which[2], k;
    which[0] = prev;
    which[1] = now;
    for (k = 0; k < 2; k++) {
        CRect r;
        if (!ui_menu_row_rect(&g_sh.ctx_menu, g_sh.ctx_x, g_sh.ctx_y,
                              GFX_FONT_SYSTEM, which[k], &r)) { continue; }
        /* One pixel out for the selection's own frame, which is drawn on the
         * rectangle's edge rather than inside it. */
        r = crect_inset(&r, -1);
        sh_mark_dirty(&r);
    }
}

void sh_context_handle_motion(int x, int y)
{
    int hit;
    if (!g_sh.ctx_open) { return; }
    hit = ui_menu_hit(&g_sh.ctx_menu, g_sh.ctx_x, g_sh.ctx_y, GFX_FONT_SYSTEM, x, y);
    if (hit != g_sh.ctx_menu.highlight) {
        int prev = g_sh.ctx_menu.highlight;
        g_sh.ctx_menu.highlight = hit;
        ctx_mark_rows(prev, hit);
    }
}

cbool sh_context_handle_click(int x, int y, int buttons)
{
    int hit;
    CASTALIA_UNUSED(buttons);
    if (!g_sh.ctx_open) { return CFALSE; }
    if (!crect_contains(&g_sh.ctx_rect, x, y)) {
        sh_context_close();   /* click outside dismisses */
        return CFALSE;        /* let the click also reach whatever is under it */
    }
    hit = ui_menu_hit(&g_sh.ctx_menu, g_sh.ctx_x, g_sh.ctx_y, GFX_FONT_SYSTEM, x, y);
    if (hit >= 0) {
        int cmd = g_sh.ctx_menu.items[hit].id;
        sh_context_close();
        sh_dispatch_command(cmd);
    }
    return CTRUE;
}

/* Is item i a landable target (enabled, not a separator)? */

static void ctx_step(int dir)
{
    int prev = g_sh.ctx_menu.highlight;
    /* The arrow keys move the same highlight the pointer does, so they cost
     * the same two rows. A menu walked with the keyboard is not a cheaper
     * menu than one walked with the mouse, and it used to be the same panel. */
    if (ui_menu_step(&g_sh.ctx_menu, dir)) {
        ctx_mark_rows(prev, g_sh.ctx_menu.highlight);
    }
}

cbool sh_context_handle_key(int key)
{
    UiMenu *m = &g_sh.ctx_menu;
    if (!g_sh.ctx_open) { return CFALSE; }
    if (key == PLAT_KEY_UP)   { ctx_step(-1); return CTRUE; }
    if (key == PLAT_KEY_DOWN) { ctx_step(+1); return CTRUE; }
    if (key == PLAT_KEY_ENTER) {
        if (ui_menu_selectable(m, m->highlight)) {
            int cmd = m->items[m->highlight].id;
            sh_context_close();
            sh_dispatch_command(cmd);
        }
        return CTRUE;
    }
    return CTRUE; /* menu owns the keyboard while open (Esc handled by caller) */
}
