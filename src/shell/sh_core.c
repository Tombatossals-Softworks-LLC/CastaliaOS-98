/*
 * sh_core.c - Shell session loop, input routing, layered compositing, command
 *             dispatch, and the crash boundary.
 *
 * Frame model (dirty-rectangle, layered bottom to top):
 *   1. Drain input -> route to launcher / taskbar / window manager / desktop.
 *   2. Erase the software cursor (restore save-under).
 *   3. Build the frame's repaint region from window + shell invalidations.
 *   4. Repaint desktop under the region, then windows, then taskbar, then the
 *      launcher overlay, then the cursor.
 *   5. Present exactly the touched rectangles.
 * This keeps idle frames cheap and avoids the full-screen repaints the Bible
 * warns against.
 */
#include "sh_internal.h"
#include "sw_core.h"
#include "apps.h"
#include "trash_core.h"       /* what in TRASH is an item and what is not */
#include "castalia/castalia.h"
#include "castalia/capp_loader.h"
#include "castalia/clip.h"
#include "castalia/gfx.h"
#include "castalia/settings.h"
#include "castalia/snd.h"
#include "castalia/sys.h"
#include <stdlib.h>

ShState g_sh;

/* Taskbar change detection so we only repaint it when needed. */
static char g_last_clock[12] = "";
static int  g_last_taskcount = -1;
static void *g_last_focus = NULL;
static int  g_last_clip = -1;   /* clipboard-has-text, for the tray indicator */

static CRect g_prev_cursor;
/* Pixels composited on the last frame (see sh_last_frame_px). */
static long g_frame_px = 0;
long sh_last_frame_px(void) { return g_frame_px; }

/* ---- dirty helpers --------------------------------------------------- */
void sh_mark_dirty(const CRect *r)
{
    if (r != NULL) { cregion_add(&g_sh.frame_dirty, r); }
}

void sh_invalidate_all(void)
{
    g_sh.full_repaint = CTRUE;
}

/* ---- Alt+Tab: most-recently-used order, and the panel that shows it ---- */
/*
 * Alt+Tab used to rotate the z-order, which meant pressing it twice left you
 * two windows away from where you started. Every system of this era walked
 * most-recently-used order instead, so that Alt+Tab returns you to the window
 * you were just in and pressing it again brings you back -- which is what the
 * key is mostly used for. The order itself is sw_core.c; this holds the state
 * and draws the panel.
 *
 * The panel stays up for a short while after the last press rather than while
 * a modifier is held, because the DOS keyboard path delivers Alt+Tab as a
 * single key event and never tells us when Alt is released. Focus therefore
 * follows every press immediately -- the panel shows where you have got to, it
 * does not gate the switch.
 */
#define SH_SWITCH_MS   900          /* how long the panel lingers            */
#define SH_SWITCH_ROW  18
#define SH_SWITCH_W    236

static SwList g_mru;
static int    g_prev_focus_id = -1;
static cu32   g_switch_until  = 0;   /* 0 = not showing                      */
static CRect  g_switch_rect;

/* Follow focus wherever it came from -- a click, a taskbar button, a new
 * window -- so the order reflects use and not just what Alt+Tab did. */
static void sh_mru_follow_focus(void)
{
    int id = wm_window_id(wm_focused());
    if (id >= 0 && id != g_prev_focus_id) {
        sw_touch(&g_mru, id);
        g_prev_focus_id = id;
    } else if (id < 0) {
        g_prev_focus_id = -1;
    }
}

static void sh_switch_layout(void)
{
    int n = sw_count(&g_mru);
    int h = n * SH_SWITCH_ROW + 10;
    if (h < SH_SWITCH_ROW + 10) { h = SH_SWITCH_ROW + 10; }
    g_switch_rect = crect_make((g_sh.screen_w - SH_SWITCH_W) / 2,
                               (g_sh.screen_h - h) / 2 - 20, SH_SWITCH_W, h);
}

/* The panel plus the margin its shadow needs -- the unit it is dirtied,
 * painted and measured in. */
static CRect sh_switch_area(void)
{
    return crect_inset(&g_switch_rect, -6);
}

static void sh_switch_draw(GfxSurface *s, const CRect *clip)
{
    const UiPalette *p = ui_palette();
    CRect r = g_switch_rect;
    int i, n = sw_count(&g_mru), y;
    if (g_switch_until == 0u) { return; }
    if (clip != NULL) {
        CRect area = sh_switch_area();
        if (!crect_overlaps(&area, clip)) { return; }
        gfx_set_clip(s, clip);
    }
    gfx_drop_shadow(s, &r, 4, 90);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED, p->light, p->dark, p->face);
    y = r.y0 + 5;
    for (i = 0; i < n; i++) {
        WmWindow *w = wm_window_by_id(sw_at(&g_mru, i));
        CRect row = crect_make(r.x0 + 4, y, crect_w(&r) - 8, SH_SWITCH_ROW - 2);
        CColor ink = p->text;
        const GfxSurface *ic;
        if (w == NULL) { y += SH_SWITCH_ROW; continue; }
        if (wm_focused() == w) {
            gfx_fill_rect(s, &row, p->accent);
            ink = p->accent_text;
        }
        ic = wm_icon(w);
        if (ic != NULL) {
            CRect src = crect_make(0, 0, ic->w, ic->h);
            gfx_blit(s, row.x0 + 2, row.y0 + (SH_SWITCH_ROW - 2 - ic->h) / 2,
                     ic, &src, GFX_BLIT_KEYED);
        }
        gfx_draw_text(s, GFX_FONT_SYSTEM, row.x0 + 22, row.y0 + 4,
                      wm_title(w), ink);
        y += SH_SWITCH_ROW;
    }
    gfx_reset_clip(s);
}

/* Is the switcher panel on screen right now? For the scenes that measure what
 * it costs while it lingers. */
cbool sh_switch_showing(void) { return (g_switch_until != 0u) ? CTRUE : CFALSE; }

/* One Alt+Tab: move 'steps' along the order, focus that window, and put the
 * panel up (or keep it up). */
static void sh_switch_step(int steps)
{
    int id;
    WmWindow *w;
    sh_mru_follow_focus();
    id = sw_pick(&g_mru, steps);
    w = wm_window_by_id(id);
    /* An id that no longer resolves means the window closed while the panel
     * was up; drop it and try the next one rather than doing nothing. */
    if (w == NULL && id >= 0) {
        sw_remove(&g_mru, id);
        id = sw_pick(&g_mru, steps);
        w = wm_window_by_id(id);
    }
    if (w != NULL) {
        wm_focus(w);
        sw_touch(&g_mru, id);
        g_prev_focus_id = id;
    }
    sh_switch_layout();
    g_switch_until = sys_now_ms() + SH_SWITCH_MS;
    { CRect d = sh_switch_area(); sh_mark_dirty(&d); }
}


cbool sh_glossy(void)
{
    return (g_sh.theme.colors == 0 && !g_sh.safe_mode) ? CTRUE : CFALSE;
}

/* A rect inflated by the overlay drop-shadow margin (no-op when flat). */
static CRect shadow_rect(const CRect *r)
{
    CRect s = *r;
    if (sh_glossy()) { s.x1 += SH_SHADOW; s.y1 += SH_SHADOW; }
    return s;
}

/* ---- launcher toggle ------------------------------------------------- */
void sh_open_launcher(cbool open)
{
    if (open == g_sh.launcher_open) { return; }
    /* The launcher and the context menu are mutually exclusive popups. */
    if (open) { sh_context_close(); }
    /* Invalidate the menu area + its shadow (erase on close / draw on open),
     * and the button. */
    {
        CRect lr = shadow_rect(&g_sh.launcher_rect);
        sh_mark_dirty(&lr);
    }
    sh_mark_dirty(&g_sh.launcher_button);
    g_sh.launcher_open = open;
    g_sh.launcher_menu.highlight = -1;
    g_sh.launcher_menu2.highlight = -1;
    g_sh.launcher_focus = 0;
    g_sh.launcher_foot_hl = -1;
    if (open) {
        /* geometry is recomputed at paint; mark a generous area. */
        sh_taskbar_dirty();
        /* Slide the menu up unless animations are off or safe mode is on. */
        g_sh.launcher_reveal =
            (settings_get()->animations && !g_sh.safe_mode) ? 0 : SH_MENU_REVEAL;
        snd_menu();
    }
}
cbool sh_launcher_is_open(void) { return g_sh.launcher_open; }

ShExitReason sh_exit_reason(void) { return g_sh.exit_reason; }

/* ---- Run dialog ------------------------------------------------------ */
static void sh_on_run(cbool ok, const char *text, void *user)
{
    int code = 0;
    CASTALIA_UNUSED(user);
    if (!ok || text == NULL || text[0] == '\0') { return; }
    SYS_LOGI("sh", "Run: %s", text);
    /* On DOS this suspends the desktop, runs the program, and restores video;
     * on the host backend it is a logged no-op. Repaint afterwards. */
    plat_run_program(text, "", NULL, &code);
    sh_invalidate_all();
}

/* ---- live settings apply --------------------------------------------- */
void sh_theme_apply_live(const ShTheme *theme)
{
    if (theme == NULL) { return; }
    g_sh.theme = *theme;
    sh_theme_apply(&g_sh.theme);
    /* The background cache and the taskbar's orb sprites are baked from the
     * theme's colors, so swapping the palette is not enough on its own. */
    sh_desktop_build_cache();
    sh_taskbar_layout();
    sh_invalidate_all();
}

void sh_apply_settings(void)
{
    if (!g_sh.safe_mode) {
        sh_theme_preset(&g_sh.theme, settings_get()->theme_preset);
        g_sh.theme.colors = settings_get()->theme_colors;
        sh_theme_quantize(&g_sh.theme);
        sh_theme_apply(&g_sh.theme);
    }
    snd_set_enabled(settings_get()->sound_enabled);
    sh_anim_set_enabled(settings_get()->animations);
    /* The typematic rate goes to the BIOS, not to a variable this system
     * reads back: a held key repeats because the keyboard controller says so,
     * and nothing above the platform layer is in that loop. Applying it here
     * is what makes the Keyboard panel a setting rather than a picture of
     * one. The host declines and says so in the log. */
    plat_set_key_repeat(settings_get()->key_delay_ms, settings_get()->key_cps);
    /* The desktop colors may have changed -- rebuild the background cache, and
     * re-lay-out the taskbar (which re-renders the Start orb sprites and
     * switches between orb and classic button with the theme). */
    sh_desktop_build_cache();
    sh_taskbar_layout();
    /* Repaint everything: theme colors and the clock format may have changed. */
    sh_invalidate_all();
}

/* ---- Screen capture ---------------------------------------------------- */
/*
 * The back buffer IS what the screen shows, so a capture is one BMP write --
 * no hardware readback, and identical on every backend. Names are SHOT0001.BMP
 * upward; the first free number wins, so a capture never overwrites an earlier
 * one and the numbering survives files being deleted out of the middle.
 */
void sh_capture_screen(void)
{
    char dir[CASTALIA_MAX_PATH], path[CASTALIA_MAX_PATH], msg[CASTALIA_MAX_PATH];
    int n;

    if (g_sh.back == NULL) { return; }
    sys_home_path(dir, (cu32)sizeof(dir), "PHOTOS");
    plat_mkdir(dir);

    for (n = 1; n <= 9999; n++) {
        sys_snprintf(path, sizeof(path), "%s/SHOT%04d.BMP", dir, n);
        if (plat_file_size(path) < 0) { break; }   /* the first free number */
    }
    if (n > 9999) {
        ui_msgbox("Capture Screen",
                  "PHOTOS is full of captures (SHOT0001..SHOT9999).",
                  UI_MB_OK, NULL, NULL);
        return;
    }
    if (gfx_bmp_save(g_sh.back, path) != CE_OK) {
        SYS_LOGW("shell", "capture failed: %s", path);
        ui_msgbox("Capture Screen", "The capture could not be written.",
                  UI_MB_OK, NULL, NULL);
        return;
    }
    SYS_LOGI("shell", "captured screen to %s (%dx%d)", path,
             g_sh.back->w, g_sh.back->h);
    sys_snprintf(msg, sizeof(msg),
                 "Saved PHOTOS\\SHOT%04d.BMP\n\n%dx%d. Open it from the File "
                 "Manager -- \nthe Icons view shows it as a thumbnail.",
                 n, g_sh.back->w, g_sh.back->h);
    ui_msgbox("Capture Screen", msg, UI_MB_OK, NULL, NULL);
}

/* ---- Show Desktop (Quick Launch) -------------------------------------- */
/* Minimize every ordinary window on the current desktop in one stroke. The
 * taskbar buttons stay, so each window is one click away from coming back.
 * Hides directly (one bulk action, not N zoom animations) but still moves
 * each window to WM_STATE_MINIMIZED, matching the caption-button minimize
 * path -- so the Task Manager reports them as minimized and a taskbar
 * restore fires WM_LIFE_RESTORE (zoom + cue) exactly like a single-window
 * restore. */
static void sh_show_desktop(void)
{
    int i, n = wm_window_count();
    int cur = wm_current_desktop();
    int hidden = 0;
    for (i = 0; i < n; i++) {
        WmWindow *w = wm_window_at(i);
        if (w == NULL || !wm_is_visible(w)) { continue; }
        if (wm_style(w) & (WM_STYLE_POPUP | WM_STYLE_MODAL)) { continue; }
        if (wm_window_desktop(w) != cur) { continue; }
        wm_show(w, CFALSE);
        wm_set_state(w, WM_STATE_MINIMIZED);
        hidden++;
    }
    if (hidden > 0) {
        snd_close();
        sh_invalidate_all();
        SYS_LOGI("sh", "show desktop: minimized %d window(s)", hidden);
    }
}

/* ---- virtual desktops ------------------------------------------------ */
void sh_switch_desktop(int d)
{
    if (d == wm_current_desktop()) { return; }
    if (g_sh.launcher_open) { sh_open_launcher(CFALSE); }
    if (sh_context_is_open()) { sh_context_close(); }
    wm_set_desktop(d);
    sh_invalidate_all(); /* desktop bg + windows that entered/left the screen */
    SYS_LOGI("sh", "switched to desktop %d", d + 1);
}

/* Open the Recycle Bin: the File Manager pointed at CASTALIA_HOME/TRASH, where
 * safe-deleted files are moved. Ensures the folder exists so it opens even
 * before anything has been deleted. */
static void sh_open_recyclebin(void)
{
    char trash[CASTALIA_MAX_PATH];
    sys_home_path(trash, (cu32)sizeof(trash), "TRASH");
    plat_mkdir(trash);   /* harmless (CE_BUSY) if it already exists */
    app_fileman_open_path(trash);
}

/* Fill 'out' with CASTALIA_HOME/TRASH. */
static void trash_path(char *out, cu32 outsz)
{
    sys_home_path(out, outsz, "TRASH");
}

/* Count everything in the Recycle Bin. Deleted FOLDERS live here too (the
 * File Manager renames them into TRASH), so counting only files would report
 * a bin holding a folder as "already empty" and strand it forever. */
static int trash_count_in(const char *dir, int depth)
{
    PlatDir *d;
    PlatDirEntry e;
    int n = 0;
    if (depth > 8) { return 0; }          /* refuse to chase a deep tree */
    d = plat_opendir(dir);
    if (d == NULL) { return 0; }
    while (plat_readdir(d, &e)) {
        char sub[CASTALIA_MAX_PATH];
        if (e.name[0] == '.') { continue; }
        /* The bin's index is bookkeeping, not something anybody deleted:
         * counting it offers to empty a bin you can see is empty. Only at the
         * top -- a file of that name inside a deleted folder is a real file. */
        if (depth == 0 && trash_is_index(e.name)) { continue; }
        n++;
        if (e.is_dir) {
            sys_snprintf(sub, sizeof(sub), "%s/%s", dir, e.name);
            n += trash_count_in(sub, depth + 1);
        }
    }
    plat_closedir(d);
    return n;
}

static int trash_file_count(void)
{
    char trash[CASTALIA_MAX_PATH];
    trash_path(trash, sizeof(trash));
    return trash_count_in(trash, 0);
}

/* Empty one directory: files go first, sub-folders are emptied then removed.
 * Names are collected before deleting so we never delete under the open
 * directory cursor, and the recursion is depth-bounded. Returns items removed;
 * 'rmself' also removes the (now empty) directory itself. */
static int empty_dir_do(const char *dir, int depth, cbool rmself)
{
    char path[CASTALIA_MAX_PATH];
    char names[64][CASTALIA_MAX_NAME];
    unsigned char isdir[64];
    PlatDir *d;
    PlatDirEntry e;
    int count = 0, i, removed = 0;
    if (depth > 8) { return 0; }
    d = plat_opendir(dir);
    if (d == NULL) { return 0; }
    while (count < 64 && plat_readdir(d, &e)) {
        if (e.name[0] == '.') { continue; }
        /* Leave the bin's index to the caller. This sweep stops at 64 names,
         * so deleting the index here could throw away where the REMAINING
         * items came from -- provenance for files that are still in the bin. */
        if (depth == 0 && trash_is_index(e.name)) { continue; }
        sys_strlcpy(names[count], e.name, sizeof(names[count]));
        isdir[count] = e.is_dir ? 1 : 0;
        count++;
    }
    plat_closedir(d);
    for (i = 0; i < count; i++) {
        sys_snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if (isdir[i]) {
            removed += empty_dir_do(path, depth + 1, CTRUE);
        } else if (plat_file_remove(path) == CE_OK) {
            removed++;
        }
    }
    if (rmself && plat_dir_remove(dir) == CE_OK) { removed++; }
    return removed;
}

static void empty_trash_do(void)
{
    char trash[CASTALIA_MAX_PATH];
    int removed;
    trash_path(trash, sizeof(trash));
    removed = empty_dir_do(trash, 0, CFALSE);   /* keep TRASH itself */
    /* ...and the index goes only once the bin really is empty. A sweep that
     * hit its 64-name limit leaves items behind, and those items still need
     * to know where they came from. */
    if (trash_count_in(trash, 0) == 0) {
        char idx[CASTALIA_MAX_PATH];
        sys_snprintf(idx, sizeof(idx), "%s/%s", trash, TRASH_IDX_NAME);
        plat_file_remove(idx);
    }
    SYS_LOGI("sh", "emptied Recycle Bin: removed %d item(s)", removed);
    sh_recyclebin_touch();   /* the bin icon is now empty */
    sh_invalidate_all();
}

static void empty_trash_confirmed(UiDialogResult r, void *user)
{
    CASTALIA_UNUSED(user);
    if (r == UI_DR_YES) { empty_trash_do(); }
}

/* Empty the Recycle Bin, but confirm first (files are gone for good). */
static void sh_empty_trash(void)
{
    int n = trash_file_count();
    char msg[96];
    if (n <= 0) {
        ui_msgbox("Recycle Bin", "The Recycle Bin is already empty.",
                  UI_MB_OK, NULL, NULL);
        return;
    }
    sys_snprintf(msg, sizeof(msg),
                 "Permanently delete %d item(s) from the Recycle Bin?", n);
    ui_msgbox("Recycle Bin", msg, UI_MB_YESNO, empty_trash_confirmed, NULL);
}

/* ---- command dispatch ------------------------------------------------ */
/*
 * The ONE place a session ends.
 *
 * There were four: Shut Down's dialog, Restart Shell, Exit to DOS, and the
 * host's quit event. Teaching the editors to guard their close box did
 * nothing for three of them -- ending the loop does not close a window, so
 * every WM_MSG_CLOSE guard is bypassed and the unsaved document goes with the
 * session. Three doors, and the guard was on the fourth.
 *
 * So they all come through here, and the question is asked once. Not
 * "close every window and see what happens": that is a chain of modal
 * dialogs with a cancel in the middle, and a half-shut-down desktop is not
 * a state anybody asked for.
 */
static ShExitReason g_pending_exit;

static void sh_end_confirm(UiDialogResult result, void *user)
{
    (void)user;
    if (result != UI_DR_YES) { return; }   /* stay logged in */
    g_sh.exit_reason = g_pending_exit;
    g_sh.running = CFALSE;
}

void sh_end_session(ShExitReason reason)
{
    int dirty = wm_unsaved_count();
    if (dirty > 0) {
        char msg[160];
        sys_snprintf(msg, sizeof(msg),
                     "%d open %s changes nobody has saved.\n\n"
                     "End the session and lose them?",
                     dirty, (dirty == 1) ? "window has" : "windows have");
        g_pending_exit = reason;
        ui_msgbox("CastaliaOS", msg, UI_MB_YESNO, sh_end_confirm, NULL);
        return;
    }
    g_sh.exit_reason = reason;
    g_sh.running = CFALSE;
}

void sh_dispatch_command(int cmd)
{
    /* The window menu's commands act on the window it was raised over rather
     * than opening anything, so they are answered before the table below --
     * which is a table of things to open. */
    if (sh_context_window_command(cmd)) { return; }
    switch (cmd) {
    case SH_CMD_SYSINFO:       app_sysinfo_open(); break;
    case SH_CMD_LOGVIEWER:     app_logview_open(); break;
    case SH_CMD_FILEMANAGER:   app_fileman_open(); break;
    case SH_CMD_RECYCLEBIN:    sh_open_recyclebin(); break;
    case SH_CMD_EMPTYTRASH:    sh_empty_trash(); break;
    case SH_CMD_DISPLAYPROPS:  app_control_open_cat(APP_CC_DESKTOP); break;
    case SH_CMD_CONTROLCENTER: app_control_open(); break;
    case SH_CMD_TASKMANAGER:   app_taskman_open(); break;
    case SH_CMD_DISKUSAGE:     app_diskuse_open(); break;
    case SH_CMD_COMPARE:       app_compare_open(NULL, NULL); break;
    case SH_CMD_PAINT:         app_paint_open(); break;
    case SH_CMD_VIEWER:        app_view_open(); break;
    case SH_CMD_NOTEPAD:       app_notepad_open(); break;
    case SH_CMD_CALCULATOR:    app_calc_open(); break;
    case SH_CMD_MINES:         app_mines_open(); break;
    case SH_CMD_MEDIA:         app_media_open(); break;
    case SH_CMD_BENCHMARK:     app_bench_open(); break;
    case SH_CMD_CLOCK:         app_clock_open(); break;
    case SH_CMD_CHARMAP:       app_charmap_open(); break;
    case SH_CMD_SOLITAIRE:     app_solitaire_open(); break;
    case SH_CMD_FREECELL:      app_freecell_open(); break;
    case SH_CMD_REVERSI:       app_reversi_open(); break;
    case SH_CMD_SHEET:         app_sheet_open(); break;
    case SH_CMD_WRITE:         app_write_open(); break;
    case SH_CMD_NETWORK:       app_net_open(); break;
    case SH_CMD_THEMEEDITOR:   app_theme_open(); break;
    case SH_CMD_CAPTURE:       sh_capture_screen(); break;
    case SH_CMD_CONSOLE:       app_console_open(); break;
    case SH_CMD_WELCOME:       app_welcome_open(); break;
    case SH_CMD_SHOWDESKTOP:   sh_show_desktop(); break;
    case SH_CMD_RUN:
        ui_prompt("Run", "Type a program name to run:", "", sh_on_run, NULL);
        break;
    case SH_CMD_DOSLAUNCHER:
        ui_prompt("DOS Program", "Program to run (path and args):", "",
                  sh_on_run, NULL);
        break;
    case SH_CMD_HELP:          app_help_open(); break;
    case SH_CMD_ABOUT:         app_about_open(); break;
    case SH_CMD_REFRESH:       sh_invalidate_all(); break;
    case SH_CMD_ARRANGE:       sh_desktop_init(); sh_desktop_save_positions();
                               sh_invalidate_all(); break;
    case SH_CMD_RESTART_SHELL: sh_end_session(SH_EXIT_RESTART_SHELL); break;
    case SH_CMD_EXIT_TO_DOS:   sh_end_session(SH_EXIT_TO_DOS); break;
    case SH_CMD_SHUTDOWN:
        sh_shutdown_dialog(); break;
    default:
        if (cmd >= SH_CMD_ADDON_BASE &&
            cmd < SH_CMD_ADDON_BASE + CAPP_MAX_INDEXED) {
            /* Launch a discovered .CAPP add-on: its own code runs behind the
             * ABI and opens whatever window(s) it needs. */
            CResult rc = capp_launch_indexed(cmd - SH_CMD_ADDON_BASE);
            if (rc != CE_OK) {
                SYS_LOGW("sh", "add-on launch failed (cmd %d, rc %d)", cmd, (int)rc);
            }
            break;
        }
        SYS_LOGD("sh", "unhandled command %d", cmd);
        break;
    }
}

/* ---- screensaver idle state ------------------------------------------ */
#define SH_IDLE_FRAMES 2700     /* ~45s at 60 Hz before the screensaver kicks in */
static int   g_idle = 0;
static cbool g_saver_on = CFALSE;
static int   g_saver_frame = 0;
static cbool g_had_input = CFALSE;

/* ---- input routing --------------------------------------------------- */
static void handle_event(const PlatEvent *ev)
{
    if (ev->type == PLAT_EV_MOUSE_MOVE || ev->type == PLAT_EV_MOUSE_DOWN ||
        ev->type == PLAT_EV_MOUSE_UP || ev->type == PLAT_EV_KEY_DOWN ||
        ev->type == PLAT_EV_KEY_UP) {
        g_had_input = CTRUE;   /* any user input dismisses / resets the saver */
    }
    if (ev->type == PLAT_EV_MOUSE_MOVE || ev->type == PLAT_EV_MOUSE_DOWN ||
        ev->type == PLAT_EV_MOUSE_UP) {
        g_sh.mouse_x = ev->mouse_x;
        g_sh.mouse_y = ev->mouse_y;
        g_sh.mouse_b = ev->buttons;
    }
    /* A click or a keypress dismisses any tooltip (it reappears only after
     * the pointer settles on a fresh target). */
    if (ev->type == PLAT_EV_MOUSE_DOWN || ev->type == PLAT_EV_KEY_DOWN) {
        sh_tooltip_hide();
    }

    /* A modal dialog captures everything: bypass launcher/taskbar/desktop and
     * let the window manager route the event to the modal window. */
    if (wm_has_modal()) {
        wm_handle_event(ev);
        return;
    }

    switch (ev->type) {
    case PLAT_EV_QUIT:
        sh_end_session(SH_EXIT_TO_DOS);
        return;

    case PLAT_EV_MOUSE_DOWN:
        /* Reaching for the mouse ends a keyboard move: the window keeps where
         * it has got to, and the click does whatever it would have done. */
        if (sh_kmove_active()) { sh_kmove_end(); }
        if (g_sh.ctx_open) {
            if (sh_context_handle_click(ev->mouse_x, ev->mouse_y, ev->buttons)) {
                return; /* consumed inside the context menu */
            }
            /* click was outside -> menu closed; fall through to route it */
        }
        if (g_sh.launcher_open) {
            if (sh_launcher_handle_click(ev->mouse_x, ev->mouse_y, ev->buttons)) {
                return; /* consumed inside the menu */
            }
            /* click was outside -> menu closed; fall through to route it */
        }
        if (crect_contains(&g_sh.taskbar_rect, ev->mouse_x, ev->mouse_y)) {
            sh_taskbar_handle_click(ev->mouse_x, ev->mouse_y, ev->buttons);
            return;
        }
        {
            WmWindow *hit = wm_hit_test(ev->mouse_x, ev->mouse_y);
            if (hit != NULL) {
                /* Right-click on the title bar raises the window menu. The
                 * window is focused and raised first, as any press on it
                 * would be -- the menu acts on it, so it should be the one in
                 * front while you read it. */
                if ((ev->buttons & PLAT_MB_RIGHT) &&
                    wm_titlebar_hit(hit, ev->mouse_x, ev->mouse_y)) {
                    wm_focus(hit);
                    wm_bring_to_front(hit);
                    wm_invalidate(hit, NULL);
                    sh_context_open_window(hit, ev->mouse_x, ev->mouse_y);
                    return;
                }
                wm_handle_event(ev);
                return;
            }
        }
        /* Right-click: an icon raises its own menu (Open / Empty Recycle Bin),
         * otherwise the empty desktop menu. */
        if (ev->buttons & PLAT_MB_RIGHT) {
            if (!sh_desktop_icon_context(ev->mouse_x, ev->mouse_y)) {
                sh_context_open_desktop(ev->mouse_x, ev->mouse_y);
            }
            return;
        }
        sh_desktop_handle_click(ev->mouse_x, ev->mouse_y, ev->buttons);
        return;

    case PLAT_EV_MOUSE_MOVE:
        if (g_sh.ctx_open) { sh_context_handle_motion(ev->mouse_x, ev->mouse_y); }
        if (g_sh.launcher_open) { sh_launcher_handle_motion(ev->mouse_x, ev->mouse_y); }
        /* A desktop-icon drag owns the pointer until the button is released. */
        if (sh_desktop_handle_motion(ev->mouse_x, ev->mouse_y)) { return; }
        wm_handle_event(ev);
        return;

    case PLAT_EV_MOUSE_UP:
        /* Whatever the launcher was holding is let go of first: a scroll-bar
         * part drawn held after the button came up is the same defect three
         * apps had in their own bars. */
        sh_launcher_release();
        if (sh_desktop_handle_up(ev->mouse_x, ev->mouse_y)) { return; }
        wm_handle_event(ev);
        return;

    case PLAT_EV_MOUSE_WHEEL:
        /*
         * An open popup takes the wheel before the windows do. Without this
         * the notch went straight to wm_handle_event, which routes by hit
         * test -- so spinning the wheel over an open Start menu scrolled the
         * WINDOW BEHIND IT, which is both useless and invisible, since the
         * menu is covering the thing that moved.
         */
        if (g_sh.launcher_open &&
            sh_launcher_handle_wheel(ev->mouse_x, ev->mouse_y, ev->wheel)) {
            return;
        }
        wm_handle_event(ev);
        return;

    case PLAT_EV_KEY_DOWN:
        /* A keyboard move/size owns the arrows, Enter and Esc while it runs --
         * before anything else looks at them, since Esc means "put the window
         * back" here and "open the launcher" a few lines down. Any other key
         * ends the mode and is then handled normally. */
        if (sh_kmove_active()) {
            if (sh_kmove_key(ev->key)) { return; }
        }
        /* An open context menu owns the keyboard first: Esc closes it, arrows
         * move the highlight, Enter activates. */
        if (g_sh.ctx_open) {
            /* Esc closes it, and so does the key that opened it -- Alt+Space
             * twice should leave the screen as it found it. */
            if (ev->key == PLAT_KEY_ESC || ev->key == PLAT_KEY_SYSMENU) {
                sh_context_close();
                return;
            }
            if (sh_context_handle_key(ev->key)) { return; }
        }
        if (ev->key == PLAT_KEY_ESC) {
            /* Esc closes an open launcher; on an idle desktop (no focused
             * window) it opens the launcher -- a keyboard path to the menu,
             * like Ctrl+Esc on the systems this is inspired by. When a window
             * is focused, Esc is forwarded to it instead. */
            if (g_sh.launcher_open) { sh_open_launcher(CFALSE); return; }
            if (wm_focused() == NULL) { sh_open_launcher(CTRUE); return; }
        }
        /* While the launcher is open it owns the keyboard: arrows move the
         * highlight, Enter launches. This is the full keyboard path to every
         * app (Esc / Ctrl+Esc opens it, arrows + Enter pick an entry). */
        if (g_sh.launcher_open) {
            if (sh_launcher_handle_key(ev->key)) { return; }
        }
        /* Alt+Space opens the focused window's menu, at the top-left of its
         * client area -- where the system box sits on the machines this is
         * modelled on. Until this existed, Alt+F4 was the only thing the
         * keyboard could tell a window to do. */
        if (ev->key == PLAT_KEY_SYSMENU) {
            WmWindow *f = wm_focused();
            if (f != NULL && (wm_style(f) & WM_STYLE_TITLE)) {
                CPoint o = wm_client_origin(f);
                sh_context_open_window(f, o.x, o.y);
            }
            return;
        }
        /* Alt+Tab rotates the window stack (works from anywhere on the desktop). */
        if (ev->key == PLAT_KEY_NEXTWIN) {
            sh_switch_step((ev->mods & PLAT_MOD_SHIFT) ? -1 : 1);
            return;
        }
        /*
         * With nothing else on screen wanting them, the arrows walk the
         * desktop icons and Enter opens one. This is the last place the
         * keyboard could not reach: the icons were mouse-only, on a system
         * whose own hardware verification was done with a keyboard alone.
         *
         * Deliberately last among the key bindings, so it can only ever get
         * what nothing else claimed -- no focused window, no launcher, no
         * menu, no keyboard move. Ctrl+arrows still belong to the virtual
         * desktops below, which is why the modifier is excluded here.
         */
        if (wm_focused() == NULL && !(ev->mods & PLAT_MOD_CTRL) &&
            !g_sh.launcher_open && !g_sh.ctx_open) {
            if (sh_desktop_handle_key(ev->key)) { return; }
        }
        /* Ctrl+Left / Ctrl+Right cycle virtual desktops (wrapping). */
        if ((ev->mods & PLAT_MOD_CTRL) &&
            (ev->key == PLAT_KEY_LEFT || ev->key == PLAT_KEY_RIGHT)) {
            int nd = wm_desktop_count();
            int d = wm_current_desktop() + (ev->key == PLAT_KEY_RIGHT ? 1 : -1);
            if (d < 0) { d = nd - 1; }
            if (d >= nd) { d = 0; }
            sh_switch_desktop(d);
            return;
        }
        wm_handle_event(ev);
        return;

    default:
        wm_handle_event(ev);
        return;
    }
}

/* ---- taskbar tick ---------------------------------------------------- */
static void taskbar_tick(void)
{
    char now[12];
    int  n = wm_window_count();
    void *f = (void *)wm_focused();
    int  clip = clip_has_text() ? 1 : 0;
    sh_taskbar_update_hover(g_sh.mouse_x, g_sh.mouse_y);
    sys_format_clock_ex(now, sizeof(now), settings_get()->clock_seconds);
    if (sys_stricmp(now, g_last_clock) != 0 || n != g_last_taskcount ||
        f != g_last_focus || clip != g_last_clip) {
        sys_strlcpy(g_last_clock, now, sizeof(g_last_clock));
        g_last_taskcount = n;
        g_last_focus = f;
        g_last_clip = clip;
        sh_taskbar_dirty();
    }
}

/* Draw the Aero-style snap preview: a thick rubber-band outline at the target
 * snap rectangle while a window is dragged near a work-area edge. Outline only
 * (no alpha) so it stays cheap and true to the Win9x rubber-band look. */
/*
 * The rubber band of an outline drag: a two-pixel frame in the active title
 * colour, and nothing inside it.
 *
 * Deliberately not the snap preview's four-ring treatment. The two mean
 * different things -- this one says "the window is here", that one says "let
 * go and it will fill this" -- and a drag into a snap zone shows both at
 * once, so they have to be told apart at a glance.
 */
static void sh_draw_drag_outline(GfxSurface *s, const CRect *r)
{
    CColor edge = gfx_tint(g_sh.theme.title_active_l, 0xFFFFFF, 40);
    CRect a = *r;
    gfx_frame_rect(s, &a, edge);
    a.x0++; a.y0++; a.x1--; a.y1--;
    if (crect_w(&a) > 0 && crect_h(&a) > 0) { gfx_frame_rect(s, &a, edge); }
}

static void sh_draw_snap_preview(GfxSurface *s, const CRect *r)
{
    CColor edge = gfx_tint(g_sh.theme.title_active_l, 0xFFFFFF, 60);
    CColor glow = gfx_tint(g_sh.theme.title_active_l, 0xFFFFFF, 150);
    CRect a = *r, b;
    gfx_frame_rect(s, &a, edge);
    a.x0++; a.y0++; a.x1--; a.y1--; gfx_frame_rect(s, &a, edge);
    a.x0++; a.y0++; a.x1--; a.y1--; gfx_frame_rect(s, &a, glow);
    b = *r; b.x0 += 6; b.y0 += 6; b.x1 -= 6; b.y1 -= 6;
    if (crect_w(&b) > 0 && crect_h(&b) > 0) { gfx_frame_rect(s, &b, glow); }
}

/* ---- one frame ------------------------------------------------------- */
cbool sh_run_frame(void)
{
    GfxSurface *s = g_sh.back;
    PlatEvent   ev;
    CRegion     R;
    CRect       oldc, newc;
    cbool       cursor_moved;
    cbool       cursor_redraw;
    int         i;

    /* 1. input */
    while (plat_poll_event(&ev)) { handle_event(&ev); }
    if (!g_sh.running) { return CFALSE; }

    /* Screensaver: after an idle stretch, take over the screen; any input
     * dismisses it and forces a clean repaint of the desktop underneath. */
    if (g_had_input) {
        g_had_input = CFALSE;
        g_idle = 0;
        if (g_saver_on) { g_saver_on = CFALSE; sh_invalidate_all(); }
    } else if (!g_saver_on && !g_sh.safe_mode && ++g_idle >= SH_IDLE_FRAMES) {
        g_saver_on = CTRUE;
        g_saver_frame = 0;
    }
    if (g_saver_on) {
        CRect full = crect_make(0, 0, g_sh.screen_w, g_sh.screen_h);
        sh_saver_draw(s, g_saver_frame++);
        plat_present(&full, 1);
        return g_sh.running;
    }

    /* Cooperative tick for running .CAPP add-ons (drives their frame()). */
    capp_tick_running();

    /* Per-frame tick for self-animating app windows (media visualizer, the
     * benchmark's animated bars). Only windows that opted in are notified. */
    wm_tick_animations();

    taskbar_tick();
    sh_mru_follow_focus();

    /*
     * The desktop's icons draw their selection live only while the DESKTOP
     * has the arrow keys, which is only while no window is focused. Opening
     * the first window or closing the last one therefore changes how they
     * look without touching a pixel of them, so nothing would repaint them.
     * Watched here rather than pushed from wm_focus: focus also becomes NULL
     * inside wm_destroy, which has no business knowing about desktop icons.
     */
    {
        static cbool prev_desk_keys = CTRUE;
        cbool now = (wm_focused() == NULL) ? CTRUE : CFALSE;
        if (now != prev_desk_keys) {
            sh_desktop_mark_icons();
            prev_desk_keys = now;
        }
    }

    /* 2. move the cursor. Nothing erases the old one here: adding its
     * rectangle to the region below is what does that, by repainting the
     * scene over it. */
    oldc = g_prev_cursor;
    sh_cursor_set_pos(g_sh.mouse_x, g_sh.mouse_y);
    cursor_moved = (oldc.x0 != g_sh.mouse_x || oldc.y0 != g_sh.mouse_y)
                       ? CTRUE : CFALSE;
    newc = sh_cursor_bounds();

    /* 3. build repaint region */
    cregion_clear(&R);
    if (g_sh.full_repaint) {
        CRect full = crect_make(0, 0, g_sh.screen_w, g_sh.screen_h);
        cregion_add(&R, &full);
        g_sh.full_repaint = CFALSE;
    } else {
        wm_collect_dirty(&R);
        for (i = 0; i < g_sh.frame_dirty.count; i++) {
            cregion_add(&R, &g_sh.frame_dirty.rects[i]);
        }
        /* These two lines ARE the cursor's erase and its present. The old
         * rectangle gets the scene painted back over the arrow; the new one
         * gets the arrow onto the physical framebuffer. sh_cursor.c has no
         * save-under and relies on this -- neither line is optional, and
         * dropping the first leaves a trail of arrows across the desktop.
         * --cursor-demo fails if either goes.
         *
         * Only when the pointer actually moved, though. A still pointer over
         * a still desktop needs neither: the arrow is already on the back
         * buffer and already on the glass, and repainting the region under it
         * so it can be drawn again is the entire cost of an idle frame. */
        if (cursor_moved) {
            cregion_add(&R, &oldc);
            cregion_add(&R, &newc);
        }
    }
    cregion_clear(&g_sh.frame_dirty);
    /* Advance the launcher slide-up while it is still rolling out. */
    if (g_sh.launcher_open && g_sh.launcher_reveal < SH_MENU_REVEAL) {
        CRect lr = shadow_rect(&g_sh.launcher_rect);
        g_sh.launcher_reveal++;
        cregion_add(&R, &lr);
    }
    /* Animation outlines: mark their old + new bounds so the layers below
     * repaint (erasing the previous outline) before we redraw them on top. */
    sh_anim_collect(&R);

    /*
     * The Alt+Tab panel expires on a timer, and its last frame has to erase
     * it -- marking the area dirty as it goes lets the layers below repaint
     * over it.
     *
     * Only that last frame. It used to add its rectangle on EVERY frame it
     * was up, and it is up for 900 ms after the last press -- about
     * fifty-four frames -- for the express purpose of being read. Sixty-three
     * thousand pixels a frame, to show a list that is not changing. Each
     * press already marks the area itself, which is the frame where the
     * highlight actually moves.
     */
    if (g_switch_until != 0u && sys_now_ms() >= g_switch_until) {
        CRect d = sh_switch_area();
        g_switch_until = 0u;
        cregion_add(&R, &d);
    }

    /*
     * The pointer, decided BEFORE anything is composited, because deciding it
     * afterwards was wrong in a way nothing could see.
     *
     * sh_cursor_draw's shadow DARKENS what is already on the buffer: it reads
     * each pixel, runs it through a falloff table and writes it back. That is
     * what gives the arrow a soft edge with no alpha buffer, and it means
     * drawing the cursor twice over the same pixels is not the same as
     * drawing it once. The rule here used to be "redraw it whenever anything
     * at all was repainted", so a still pointer over a window somebody was
     * typing in got its shadow darkened again on every keystroke -- and since
     * nothing ever erased it, the arrow slowly grew a black halo. Sixty-two
     * pixels of it, found by --stale-demo asking every window whether a full
     * repaint would disagree with what was on screen.
     *
     * So: redraw only when the pointer moved or when something actually
     * repainted UNDER it -- and in that second case put the cursor's whole
     * rectangle into the region first, so the scene comes back over all of
     * the old arrow rather than the part that happened to overlap. A pointer
     * nobody painted near is already correct on both buffers and is left
     * alone, which is also what makes an idle frame free.
     */
    {
        cbool touched = CFALSE;
        for (i = 0; i < R.count; i++) {
            if (crect_overlaps(&newc, &R.rects[i])) { touched = CTRUE; break; }
        }
        cursor_redraw = (cursor_moved || touched) ? CTRUE : CFALSE;
        if (cursor_redraw) { cregion_add(&R, &newc); }
    }

    /*
     * Anything painted ONCE PER REGION RECT has to survive a pixel that is in
     * two of them, and cregion_add does not promise it will not be: it merges
     * an overlapping neighbour only when the union wastes little area, and
     * appends it otherwise. Two long thin rects that cross -- a window's
     * scroll bar and its status line, say -- are exactly the shape it
     * declines to merge, and exactly the shape that overlaps.
     *
     * Almost everything painted that way is idempotent: a fill, a blit, a
     * glyph. A drop SHADOW is not. gfx_drop_shadow reads each pixel, runs it
     * through a falloff and writes it back, which is how the popups get a
     * soft edge with no alpha buffer -- and a pixel darkened twice comes out
     * too dark. It is the same read-modify-write the mouse pointer's shadow
     * turned black on, met from the other side.
     *
     * So where a shadowed popup is open, the rectangles covering it are made
     * to not overlap each other first. Merging costs area, which is why
     * cregion_add declines to do it in general; here it is paid only on the
     * frames where an overlap actually exists, and correctness is not
     * negotiable against it.
     */
    if (g_sh.launcher_open || g_sh.ctx_open || g_switch_until != 0u) {
        if (g_switch_until != 0u) {
            CRect a = sh_switch_area();
            cregion_disjoin_over(&R, &a);
        }
        if (g_sh.launcher_open) {
            CRect a = shadow_rect(&g_sh.launcher_rect);
            cregion_disjoin_over(&R, &a);
        }
        if (g_sh.ctx_open) {
            CRect a = shadow_rect(&g_sh.ctx_rect);
            cregion_disjoin_over(&R, &a);
        }
    }

    /* 4. layered repaint within R */
    for (i = 0; i < R.count; i++) { sh_desktop_paint(&R.rects[i]); }
    wm_paint(&R);
    {
        cbool tb_hit = CFALSE;
        CRect tbu = g_sh.taskbar_rect;   /* bar + the Start orb's overhang */
        if (g_sh.launcher_button.y0 < tbu.y0) { tbu.y0 = g_sh.launcher_button.y0; }
        for (i = 0; i < R.count; i++) {
            if (crect_overlaps(&tbu, &R.rects[i])) { tb_hit = CTRUE; break; }
        }
        if (tb_hit) { sh_taskbar_paint(&tbu); }
    }
    /*
     * The two open menus, painted like the taskbar above them: only where
     * the region says something changed, and clipped to that.
     *
     * They used to paint WHOLE and add their whole rectangle to the region,
     * on every frame, for as long as they were open. An open Start menu
     * therefore composited its entire 134,268-pixel panel sixty times a
     * second whether or not anything moved -- the exact opposite of the idle
     * frame costing nothing -- and it made narrowing the highlight repaint
     * pointless, because the panel was coming back anyway.
     *
     * Painting per REGION RECT rather than once with the whole panel as the
     * clip is what makes the narrowing pay: a highlight that moved between
     * two rows draws two rows.
     */
    if (g_sh.launcher_open) {
        CRect lr = shadow_rect(&g_sh.launcher_rect);
        cbool lr_hit = CFALSE;
        for (i = 0; i < R.count; i++) {
            if (crect_overlaps(&lr, &R.rects[i])) {
                CRect part = crect_intersect(&lr, &R.rects[i]);
                sh_launcher_paint(&part);
                lr_hit = CTRUE;
            }
        }
        /* ...and the orb back on top of it: the panel's bottom edge lands on
         * the taskbar, so it covers the overhang and cuts the sphere flat. */
        if (lr_hit) { sh_taskbar_paint_orb(); }
    }
    if (g_sh.ctx_open) {
        CRect cr = shadow_rect(&g_sh.ctx_rect);
        for (i = 0; i < R.count; i++) {
            if (crect_overlaps(&cr, &R.rects[i])) {
                CRect part = crect_intersect(&cr, &R.rects[i]);
                sh_context_paint(&part);
            }
        }
    }
    /*
     * Tooltip: over windows/taskbar/menus, under the anim outlines + cursor,
     * and on the same terms as the two menus above -- only where the region
     * says something changed.
     *
     * It had the same unconditional paint, and it is the overlay that hangs
     * around the longest: a tooltip appears BECAUSE the pointer stopped
     * moving, and it stays until the pointer moves again. Every frame it was
     * up cost its whole card, which is the cheapest possible thing to get
     * wrong and the most reliably wrong, because "the pointer is resting" is
     * the definition of an idle desktop. sh_tooltip_show / _hide already mark
     * their own rectangle, so nothing here was keeping it on screen.
     */
    if (g_sh.tip_open) {
        for (i = 0; i < R.count; i++) {
            if (crect_overlaps(&g_sh.tip_rect, &R.rects[i])) {
                CRect part = crect_intersect(&g_sh.tip_rect, &R.rects[i]);
                sh_tooltip_paint(&part);
            }
        }
    }

    /* 4b. window open/close zoom outlines, over the scene, under the cursor. */
    if (sh_anim_active()) { sh_anim_draw(s); }
    /* Live window-snap preview outline (Aero-style), over the scene. */
    {
        CRect pv;
        if (wm_drag_snap_preview(&pv)) { sh_draw_snap_preview(s, &pv); }
    }
    /*
     * The rubber band of an outline drag, under the snap preview so that a
     * window being dragged into a snap zone shows where it will LAND rather
     * than where the pointer happens to be.
     */
    {
        CRect ob;
        if (wm_drag_outline(&ob)) { sh_draw_drag_outline(s, &ob); }
    }

    /* The Alt+Tab panel sits above every window and below the cursor, and on
     * the same terms as the popups: only where the region says so. */
    if (g_switch_until != 0u) {
        CRect sa = sh_switch_area();
        for (i = 0; i < R.count; i++) {
            if (crect_overlaps(&sa, &R.rects[i])) {
                CRect part = crect_intersect(&sa, &R.rects[i]);
                sh_switch_draw(s, &part);
            }
        }
    }

    /* 5. cursor on top, then present exactly what changed. Whether it is
     * drawn at all was decided above, before the scene went down under it. */
    if (cursor_redraw) { sh_cursor_draw(s); }
    g_prev_cursor = newc;

    /* An empty region means present NOTHING. It emphatically does not mean
     * present everything, which is what plat_present does with a count of
     * zero -- on the DOS backend that is the whole 800x600 framebuffer pushed
     * over the bus, every frame, for a desktop where nothing happened. Before
     * the cursor stopped adding its rectangle unconditionally this could not
     * arise, so the guard is new and load-bearing. */
    /*
     * What that frame cost, in pixels actually composited and pushed. On the
     * DOS target this is the number that decides whether typing feels
     * instant or feels like a modem: every one of these crosses the ISA bus.
     */
    {
        int q;
        g_frame_px = 0;
        for (q = 0; q < R.count; q++) {
            g_frame_px += (long)crect_w(&R.rects[q]) *
                          (long)crect_h(&R.rects[q]);
        }
    }
    if (R.count > 0) { plat_present(R.rects, R.count); }
    return g_sh.running;
}

/* ---- crash boundary -------------------------------------------------- */
/*
 * The crash screen.
 *
 * The old one named the fault, said "reboot into Safe Mode to repair", slept
 * two and a half seconds and dropped to DOS. Everything on it was true and
 * almost none of it was actionable: it did not say WHICH log, it went away
 * before it could be read, and it did not mention that there is now a copy of
 * the last configuration that booted cleanly sitting next to the live one.
 *
 * Painting is separated from handling because sys_fatal exits: a screen drawn
 * inside the handler could never be inspected by anything.
 */
void sh_crash_screen(GfxSurface *s, const char *subsystem, CResult code,
                     const char *file, int line, const char *message)
{
    CRect scr;
    char buf[128];
    char bak[CASTALIA_MAX_PATH];
    int y;

    if (s == NULL) { return; }
    scr = crect_make(0, 0, g_sh.screen_w, g_sh.screen_h);
    gfx_reset_clip(s);
    gfx_fill_rect(s, &scr, GFX_RGB(0x00, 0x00, 0x60));

    gfx_draw_text(s, GFX_FONT_BOLD, 24, 24,
                  "CastaliaOS has stopped to protect your data.",
                  GFX_RGB(0xFF, 0xFF, 0xFF));
    sys_snprintf(buf, sizeof(buf), "%s error (%d) at %s:%d",
                 (subsystem ? subsystem : "?"), (int)code,
                 (file ? file : "?"), line);
    gfx_draw_text(s, GFX_FONT_SYSTEM, 24, 48, buf, GFX_RGB(0xD8, 0xE0, 0xF0));
    gfx_draw_text(s, GFX_FONT_SYSTEM, 24, 62, (message ? message : ""),
                  GFX_RGB(0xD8, 0xE0, 0xF0));

    y = 92;
    gfx_draw_text(s, GFX_FONT_BOLD, 24, y, "What to do",
                  GFX_RGB(0xFF, 0xFF, 0xFF));
    y += 20;
    /* Name the file. "See the logs" is not advice anyone can follow. */
    sys_snprintf(buf, sizeof(buf), "1. The crash was recorded in  %s",
                 sys_crash_log_path());
    gfx_draw_text(s, GFX_FONT_SYSTEM, 24, y, buf, GFX_RGB(0xC8, 0xD4, 0xE8));
    y += 16;
    gfx_draw_text(s, GFX_FONT_SYSTEM, 24, y,
                  "2. Reboot and start with  CBOOT /safe  -- plain settings, "
                  "640x480, no add-ons.", GFX_RGB(0xC8, 0xD4, 0xE8));
    y += 16;

    /* Only mention the known-good copy if there actually is one; advice that
     * points at a file the machine does not have is worse than silence. */
    sys_home_path(bak, (cu32)sizeof(bak), "SYS/CASTALIA.BAK");
    if (plat_file_size(bak) > 0) {
        gfx_draw_text(s, GFX_FONT_SYSTEM, 24, y,
                      "3. Your last configuration that booted cleanly is kept "
                      "as  SYS\\CASTALIA.BAK", GFX_RGB(0xC8, 0xD4, 0xE8));
        y += 16;
        gfx_draw_text(s, GFX_FONT_SYSTEM, 24, y,
                      "   and is restored automatically if CASTALIA.INI is "
                      "unusable.", GFX_RGB(0xC8, 0xD4, 0xE8));
    } else {
        gfx_draw_text(s, GFX_FONT_SYSTEM, 24, y,
                      "3. No known-good configuration has been kept yet; a "
                      "clean session saves one.", GFX_RGB(0xC8, 0xD4, 0xE8));
    }
    y += 28;
    gfx_draw_text(s, GFX_FONT_SYSTEM, 24, y,
                  "Full recovery steps: docs/RECOVERY.md",
                  GFX_RGB(0x98, 0xA8, 0xC0));

    gfx_draw_text(s, GFX_FONT_SYSTEM, 24, g_sh.screen_h - 30,
                  "Press a key to return to DOS.",
                  GFX_RGB(0xFF, 0xE8, 0xA0));
}

static void crash_handler(const char *subsystem, CResult code,
                          const char *file, int line, const char *message,
                          void *user)
{
    cu32 until;
    CASTALIA_UNUSED(user);
    snd_error(); /* audible cue at the crash boundary (silent if no device) */
    sh_crash_screen(g_sh.back, subsystem, code, file, line, message);
    plat_present(NULL, 0);

    /* Wait for a key, but not forever: an unattended machine has to be able to
     * finish returning to DOS on its own. */
    until = sys_now_ms() + 30000u;
    for (;;) {
        PlatEvent ev;
        while (plat_poll_event(&ev)) {
            if (ev.type == PLAT_EV_KEY_DOWN || ev.type == PLAT_EV_QUIT) {
                return;
            }
        }
        if (sys_now_ms() >= until) { return; }
        plat_sleep_ms(20);
    }
}

/* ---- window open/close cues (installed on the wm lifecycle hook) ------ */
/*
 * Map a window title to an icon-pack slot for the title bar + taskbar button.
 *
 * Matched as a SUBSTRING, most specific first, because a prefix cannot answer
 * the titles this system actually uses. Two entries were quietly dead:
 *
 *   - the table said "Paint" and the window is called "CastaliaPaint", so the
 *     prefix never matched and Paint had no icon at all -- an entry that looks
 *     alive in the source and does nothing on screen.
 *   - the office apps put the DOCUMENT first ("Book1 - CastaliaSheet",
 *     "Document1 - CastaliaWrite"), which no prefix can ever reach.
 *
 * Order carries meaning here: "Hex Viewer" and "Log Viewer" must be tested
 * before the bare "Viewer", or both would take the generic icon.
 */
static cbool sh_title_has(const char *hay, const char *needle)
{
    int i, j;
    if (hay == NULL || needle == NULL) { return CFALSE; }
    for (i = 0; hay[i] != '\0'; i++) {
        for (j = 0; needle[j] != '\0' && hay[i + j] == needle[j]; j++) { }
        if (needle[j] == '\0') { return CTRUE; }
    }
    return CFALSE;
}

static const char *sh_icon_for_title(const char *title)
{
    static const struct { const char *key; const char *icon; } map[] = {
        /* the two-word names first: they contain the one-word ones */
        { "Hex Viewer",     "m-viewer"  },
        { "Log Viewer",     "m-logview" },
        { "System Info",    "m-sysinfo" },
        { "File Manager",   "m-fileman" },
        { "File Compare",   "m-compare" },
        { "Control Center", "m-control" },
        { "Task Manager",   "m-taskman" },
        { "Character Map",  "m-charmap" },
        { "Media Player",   "m-media"   },
        { "Theme Editor",   "m-theme"   },
        { "Disk Usage",     "m-diskuse" },
        { "Benchmark",      "m-bench"   },
        { "CastaliaSheet",  "m-sheet"   },
        { "CastaliaWrite",  "m-write"   },
        { "CastaliaPaint",  "m-paint"   },
        { "Notepad",        "m-notepad" },
        { "Calculator",     "m-calc"    },
        { "Mines",          "m-mines"   },
        { "Solitaire",      "m-cards"   },
        { "FreeCell",       "m-cards"   },
        { "Reversi",        "m-cards"   },
        { "Console",        "m-terminal"},
        { "Network",        "m-network" },
        { "Clock",          "m-clock"   },
        { "Archive",        "m-app"     },
        { "Welcome",        "m-help"    },
        { "Help",           "m-help"    },
        { "About",          "m-help"    },
        { "Viewer",         "m-viewer"  }
    };
    int i;
    if (title == NULL) { return NULL; }
    for (i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++) {
        if (sh_title_has(title, map[i].key)) { return map[i].icon; }
    }
    return NULL;
}

static void sh_window_lifecycle(WmWindow *win, WmLifeEvent ev)
{
    CRect fr = wm_frame_rect(win);
    CRect btn;
    switch (ev) {
    case WM_LIFE_OPEN: {
        const char *nm = sh_icon_for_title(wm_title(win));
        if (nm != NULL) { wm_set_icon(win, sh_iconpack_icon(nm)); }
        snd_open();  sh_anim_window(&fr, CTRUE);
        break;
    }
    case WM_LIFE_CLOSE:
        sw_remove(&g_mru, wm_window_id(win));
        if (g_prev_focus_id == wm_window_id(win)) { g_prev_focus_id = -1; }
        /* A window menu cannot outlive the window it acts on. Window ids are
         * slot numbers and slots are reused, so a menu left up over a closed
         * window would eventually be pointing at whoever moved in -- and then
         * "Close" would close a window the user never opened the menu on.
         * Taking the menu down with its subject removes the possibility
         * rather than narrowing it. */
        if (g_sh.ctx_win == wm_window_id(win)) {
            sh_context_close();
            g_sh.ctx_win = -1;
        }
        /* Same for a keyboard move in progress, for the same reason. */
        if (sh_kmove_window() == wm_window_id(win)) { sh_kmove_end(); }
        snd_close(); sh_anim_window(&fr, CFALSE);
        break;
    case WM_LIFE_MINIMIZE:
        snd_close();
        if (sh_taskbar_button_rect(win, &btn)) { sh_anim_zoom(&fr, &btn, CFALSE); }
        break;
    case WM_LIFE_RESTORE:
        snd_open();
        if (sh_taskbar_button_rect(win, &btn)) { sh_anim_zoom(&btn, &fr, CTRUE); }
        break;
    default: break;
    }
}

/* ---- lifecycle ------------------------------------------------------- */
CResult sh_init(cbool safe_mode)
{
    PlatVideoInfo vi;
    g_sh.back = plat_backbuffer();
    if (g_sh.back == NULL) { return CE_FAIL; }
    /* "No window menu is up" has to be said out loud: g_sh starts zeroed, and
     * zero is a perfectly good window id. Same for the keyboard move. */
    g_sh.ctx_win = -1;
    g_sh.kmode   = SH_K_NONE;
    g_sh.kwin    = -1;
    plat_video_info(&vi);
    g_sh.screen_w = vi.width;
    g_sh.screen_h = vi.height;
    g_sh.safe_mode = safe_mode;
    g_sh.running = CTRUE;
    g_sh.exit_reason = SH_EXIT_RUNNING;
    g_sh.launcher_open = CFALSE;
    cregion_clear(&g_sh.frame_dirty);

    /*
     * Safe mode drags by OUTLINE whatever the setting says. It is the mode
     * for a machine whose video path has already gone wrong once, and
     * carrying a window's contents along behind the pointer is the most
     * expensive thing this desktop does with the mouse -- 105% of the frame
     * per motion event. Nothing else is animated in safe mode either.
     */
    if (safe_mode) { settings_get()->drag_outline = CTRUE; }

    /* Safe mode forces the high-contrast theme; otherwise honor the user's
     * chosen preset (loaded from CASTALIA.INI at startup). */
    if (safe_mode) { sh_theme_safe_mode(&g_sh.theme); }
    else           { sh_theme_preset(&g_sh.theme, settings_get()->theme_preset);
                     g_sh.theme.colors = settings_get()->theme_colors; }
    /* Quantize the shell's own copy too: the desktop layer reads g_sh.theme
     * directly for its background fill, so the low-color pipeline must reach it
     * as well as the g_active copy sh_theme_apply pushes to ui/wm. */
    sh_theme_quantize(&g_sh.theme);
    sh_theme_apply(&g_sh.theme);

    sh_taskbar_layout();
    sh_desktop_init();
    /*
     * The Start orb is BAKED into cached sprites by sh_taskbar_layout, and
     * sh_desktop_init is what tells the icon pack where it lives -- one line
     * later. So the orb was always rendered before the pack existed. That did
     * not matter while the mark was drawn from primitives; it does now that a
     * pack can supply the artwork, because the button would keep the fallback
     * for the whole session while every other surface showed the real logo.
     *
     * Rebuilt here rather than by swapping the two calls: taskbar_layout also
     * sets the WM work area, which the desktop wants already in place.
     */
    sh_taskbar_build_orb();
    sh_desktop_load_positions();   /* restore a saved drag layout, if any */
    sh_launcher_build();
    sh_cursor_init();
    g_prev_cursor = crect_make(g_sh.screen_w / 2, g_sh.screen_h / 2, 1, 1);

    /* Window open/close cues + zoom animations (safe mode disables the zoom). */
    wm_set_lifecycle_hook(sh_window_lifecycle);
    sh_anim_set_enabled(settings_get()->animations);

    /* Push the saved typematic rate at the BIOS on the way up, not only when
     * the Control Center applies. Otherwise the setting saved last session
     * does nothing until somebody opens the panel again -- which looks
     * exactly like the panel not working. */
    plat_set_key_repeat(settings_get()->key_delay_ms, settings_get()->key_cps);

    sys_crash_set_handler(crash_handler, NULL);
    g_sh.full_repaint = CTRUE;

    SYS_LOGI("sh", "shell up (%dx%d, %s%s)", g_sh.screen_w, g_sh.screen_h,
             g_sh.theme.name, safe_mode ? ", SAFE MODE" : "");
    return CE_OK;
}

void sh_shutdown(void)
{
    wm_set_lifecycle_hook(NULL);
    sh_anim_clear();
    sh_desktop_free_cache();
    sh_taskbar_free_orb();
    sys_crash_set_handler(NULL, NULL);
    sh_cursor_shutdown();
    SYS_LOGI("sh", "shell down");
}
