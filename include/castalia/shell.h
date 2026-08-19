/*
 * shell.h - Layer 4 desktop shell: desktop, taskbar, launcher, cursor, and
 *           the theme that colors the whole environment.
 *
 * The shell owns the top-level session loop. It paints the desktop layer
 * beneath the window manager and the taskbar + software cursor above it,
 * runs the launcher menu, and dispatches the "Programs / Run / Shutdown"
 * commands. It is the piece a user experiences as "CastaliaOS".
 *
 * File manager, control center, run dialog, and shutdown dialog are wired
 * as launcher commands; their full implementations land across Phases 2-3
 * and are guarded so the MVP boots to a usable desktop today.
 */
#ifndef CASTALIA_SHELL_H
#define CASTALIA_SHELL_H

#include "castalia/ctypes.h"
#include "castalia/gfx.h"
#include "castalia/rect.h"
#include "castalia/ui.h"

/* ---- Theme ------------------------------------------------------------ */
/*
 * The theme is loaded from an INI file (see assets/themes/) and drives every
 * color in the shell. Defaults describe "Castalia Classic": Mediterranean
 * stone desktop, royal-blue active titles, antique-gold accent. All original.
 */
typedef struct {
    char   name[CASTALIA_MAX_NAME];
    /* Desktop */
    CColor desktop_top;      /* desktop gradient top                      */
    CColor desktop_bottom;   /* desktop gradient bottom                   */
    /* Window frames */
    CColor title_active_l;   /* active title gradient left                */
    CColor title_active_r;   /* active title gradient right               */
    CColor title_inactive_l;
    CColor title_inactive_r;
    CColor title_text;
    /* Controls / shared UI palette */
    UiPalette ui;
    /* Taskbar */
    CColor taskbar_face;
    /* Title bar metric for the active mode (px). */
    int    title_height;
    int    border_width;
    /* Color target: 0 = native/truecolor (no quantization), 16 or 256 select
     * the low-color pipeline (theme rendered in the EGA-16 / 3-3-2 palette). */
    int    colors;
} ShTheme;

/* Fill 'out' with the built-in "Castalia Classic" theme (no file needed). */
void sh_theme_default(ShTheme *out);
/* Fill 'out' with a built-in preset (SETTINGS_THEME_*): Classic, Storm,
 * Forest, or High Contrast. Unknown ids fall back to Classic. */
void sh_theme_preset(ShTheme *out, int preset);
/* The human-readable name of a preset (for the Control Center). */
const char *sh_theme_preset_name(int preset);
/* Load a theme from an INI; missing values fall back to the default. */
CResult sh_theme_load(ShTheme *out, const char *ini_path);
/* Write a theme to an INI. Every field is written, so a theme saved here
 * loads back identical (host-tested in tests/test_theme.c). */
CResult sh_theme_save(const ShTheme *t, const char *ini_path);
/* Quantize the theme's colors in place to its 'colors' target (16 or 256).
 * A no-op when colors is 0 (native). For a 16-color target it also flattens
 * the gradient endpoints so the desktop/title bars stay within the 16-color
 * palette (no interpolated off-palette midtones). */
void sh_theme_quantize(ShTheme *t);
/* Make 'theme' the active theme (updates ui_palette + metrics). Applies the
 * theme's color target first. */
void sh_theme_apply(const ShTheme *theme);
/* Apply a theme AND refresh everything that caches its colors -- the desktop
 * background, the taskbar (which re-renders the Start orb), and every window.
 * This is what the Theme Editor's Apply button needs; sh_theme_apply alone
 * only swaps the palette. */
void sh_theme_apply_live(const ShTheme *theme);
const ShTheme *sh_theme_active(void);

/* ---- Shell lifecycle -------------------------------------------------- */
/*
 * Initialize the shell on top of an already-initialized platform + wm.
 * Loads the theme (from cfg or default), builds the desktop and taskbar,
 * and installs the crash handler. 'safe_mode' selects the reduced,
 * high-contrast, no-frills profile.
 */
CResult sh_init(cbool safe_mode);
void    sh_shutdown(void);

/* One cooperative frame: drain input, update, composite, present. Returns
 * CFALSE when the user chose to exit the session (Exit to DOS / Shutdown). */
cbool   sh_run_frame(void);

/* Force a full desktop repaint (theme/mode switch, recovery). */
void    sh_invalidate_all(void);

/* Paint the crash screen. Separated from the crash handler so it can be drawn
 * and inspected without ending the process -- sys_fatal exits, so a handler
 * that painted inline could never be checked. */
void    sh_crash_screen(GfxSurface *s, const char *subsystem, CResult code,
                        const char *file, int line, const char *message);

/* Open or close the launcher menu (also bound to the launcher button and a
 * keyboard shortcut). Exposed so the headless demo/tests can pop it. */
void    sh_open_launcher(cbool open);
cbool   sh_launcher_is_open(void);

/* ---- Launcher command ids (also used as WM_MSG_COMMAND ids) ---------- */
#define SH_CMD_NONE          0
#define SH_CMD_PROGRAMS      100
#define SH_CMD_NOTEPAD       101
#define SH_CMD_CALCULATOR    102
#define SH_CMD_SYSINFO       103
#define SH_CMD_LOGVIEWER     104
#define SH_CMD_FILEMANAGER   105
#define SH_CMD_CONTROLCENTER 106
#define SH_CMD_RUN           107
#define SH_CMD_HELP          108
#define SH_CMD_RESTART_SHELL 109
#define SH_CMD_EXIT_TO_DOS   110
#define SH_CMD_SHUTDOWN      111
#define SH_CMD_DOSLAUNCHER   112
#define SH_CMD_ABOUT         113
#define SH_CMD_REFRESH       114  /* redraw the whole desktop               */
#define SH_CMD_ARRANGE       115  /* line up desktop icons                  */
#define SH_CMD_TASKMANAGER   116  /* open the Task Manager                  */
#define SH_CMD_PAINT         117  /* open Paint                             */
#define SH_CMD_VIEWER        118  /* open the image/hex Viewer              */
#define SH_CMD_MINES         119  /* open the Mines game                    */
#define SH_CMD_SHOWDESKTOP   120  /* minimize every window (Quick Launch)   */
#define SH_CMD_MEDIA         121  /* open the Media Player                  */
#define SH_CMD_BENCHMARK     122  /* open the Benchmark Suite               */
#define SH_CMD_CLOCK         123  /* open the Clock & Calendar              */
#define SH_CMD_CHARMAP       124  /* open the Character Map                 */
#define SH_CMD_SOLITAIRE     125  /* open Solitaire                         */
#define SH_CMD_FREECELL      152  /* open FreeCell                          */
#define SH_CMD_REVERSI       153  /* open Reversi                           */
#define SH_CMD_CONSOLE       126  /* open the Console (command shell)       */
#define SH_CMD_WELCOME       127  /* open the Welcome first-run tour        */
#define SH_CMD_RECYCLEBIN    128  /* open the Recycle Bin (Trash folder)    */
#define SH_CMD_EMPTYTRASH    129  /* empty the Recycle Bin (delete Trash)   */
#define SH_CMD_DISPLAYPROPS  130  /* Control Center at the Desktop category */
#define SH_CMD_SHEET         131  /* open CastaliaSheet (spreadsheet)       */
#define SH_CMD_WRITE         132  /* open CastaliaWrite (word processor)    */
#define SH_CMD_NETWORK       133  /* open Network (adapter, ARP, ping)      */
#define SH_CMD_THEMEEDITOR   134  /* open the Theme Editor                  */
#define SH_CMD_CAPTURE       135  /* save the screen to PHOTOS as a BMP     */
#define SH_CMD_DISKUSAGE     136  /* open Disk Usage (treemap of a folder)  */

/*
 * The window menu (Alt+Space, or a right-click on a title bar / task button).
 * These act on the window the menu was raised over rather than opening
 * anything, so unlike every command above them they need a subject: the shell
 * remembers which window that was while the menu is up.
 *
 * IDS 137..150 ARE THIS RANGE AND NOTHING ELSE MAY USE THEM. The dispatcher
 * answers the whole span before it looks at the table of things to open, so an
 * app given an id in here is not merely wrong -- it silently does nothing at
 * all. FreeCell and Reversi were handed 145 and 146 and were unreachable from
 * the Start menu for as long as they have existed: every scene that tested
 * them called app_freecell_open() directly, which says nothing about whether
 * the menu entry arrives. --launch-demo dispatches every entry on the Start
 * menu and requires a window to appear, which is the check that catches this.
 */
#define SH_CMD_WIN_RESTORE   137  /* un-maximize, or bring a minimized one back */
#define SH_CMD_WIN_MOVE      138  /* arrow keys move the window                */
#define SH_CMD_WIN_SIZE      139  /* arrow keys resize it                      */
#define SH_CMD_WIN_MINIMIZE  140
#define SH_CMD_WIN_MAXIMIZE  141
#define SH_CMD_WIN_CLOSE     142
/* Send the window to virtual desktop N: BASE + 0 .. BASE + WM_DESKTOP_COUNT-1.
 * Reserve the whole span even if fewer desktops are configured, so the ids
 * below never depend on how many there are. */
#define SH_CMD_WIN_DESKTOP   143
#define SH_CMD_WIN_DESKTOP_N 8

/* Back to ordinary commands, after the desktop span above. */
#define SH_CMD_COMPARE       151  /* open File Compare (what changed)       */

/* Launchable .CAPP add-ons occupy a contiguous command range; the launcher
 * adds one entry per indexed package (SH_CMD_ADDON_BASE + index). */
#define SH_CMD_ADDON_BASE    200

/* Dispatch a launcher/menu command. Some open windows; the "exit" family
 * ends the session (sh_run_frame then returns CFALSE). */
void sh_dispatch_command(int cmd);

/* Re-check the Recycle Bin's contents and update its desktop icon (empty vs
 * full). Apps that add or remove Trash files (e.g. the File Manager's
 * delete-to-Trash) call this so the bin reflects reality at once. */
void sh_recyclebin_touch(void);

/* Which of the two bin icons the desktop is showing. The bin keeps its own
 * bookkeeping file in TRASH, so "is there a file in there" and "does the user
 * have anything in the bin" are different questions -- this answers the second
 * one, which is the one the icon is claiming. */
cbool sh_recyclebin_full(void);

/* Save what is on screen right now to CASTALIA_HOME\PHOTOS as the next free
 * SHOTnnnn.BMP, and say where it went. The shell does this rather than the
 * platform layer because the shell owns the composited back buffer -- which
 * is what makes it work on DOS as well as on the host. */
void sh_capture_screen(void);

/* Re-apply the active settings live (theme preset, clock format) and repaint.
 * Called by the Control Center after the user changes and applies settings.
 * Safe mode still pins the high-contrast theme. */
void sh_apply_settings(void);

/* Switch to virtual desktop 'd' (0-based): closes overlays, tells the window
 * manager, and forces a full repaint of the desktop that scrolls into view. */
void sh_switch_desktop(int d);

/* Enable per-frame logging of the window open/close zoom outline (host demo /
 * verification only; off in normal use). */
void sh_anim_set_debug(cbool on);
/* The outline the first running animation is drawing now, for the headless
 * driver; CFALSE when nothing is animating. */
cbool sh_anim_bounds(CRect *out);

/* Benchmark the desktop background repaint: 'iters' full-screen paints with the
 * cache (blit) vs without (live per-pixel gradient), returning each duration in
 * ms. Verification aid for the background-cache optimization. */
void sh_desktop_bench(int iters, unsigned long *cached_ms, unsigned long *live_ms);

/* Draw the boot splash (crest, product name, progress bar at progress/256) into
 * a surface. main.c presents it during interactive startup; --splash-demo
 * screenshots it headless. Stateless. */
void sh_splash_draw(GfxSurface *s, int progress);

/* The classic end-of-session screen ("It is now safe to turn off your
 * computer") with the castle crest. Drawn while video is still up, before
 * poweroff. Stateless. */
void sh_shutdown_screen(GfxSurface *s);

/* One frame of the active screensaver ('frame' advances over time). Stateless
 * (everything is a function of 'frame'), so it composites without owning state.
 * sh_saver_draw() uses the mode selected in settings; sh_saver_draw_mode()
 * renders a specific mode (0 castle, 1 starfield, 2 plasma, 3 mystify) -- used
 * by the Control Center's live preview. */
void sh_saver_draw(GfxSurface *s, int frame);
void sh_saver_draw_mode(GfxSurface *s, int frame, int mode);

/* Is the glossy XP chrome active (truecolor theme, not safe mode)? Exposed so
 * the cursor and other chrome can match the look. */
cbool sh_glossy(void);

/* Reason the last session loop ended (for main.c to act on). */
typedef enum {
    SH_EXIT_RUNNING = 0,
    SH_EXIT_TO_DOS,
    SH_EXIT_RESTART_SHELL,
    SH_EXIT_SHUTDOWN,
    SH_EXIT_REBOOT
} ShExitReason;
ShExitReason sh_exit_reason(void);

/* ---- Software cursor (drawn last, over everything) ------------------- */
void sh_cursor_init(void);
void sh_cursor_shutdown(void);
void sh_cursor_set_pos(int x, int y);
/* Draw the arrow. Called after compositing, before present. There is no
 * matching erase: the compositor repaints the cursor's previous rectangle
 * every frame, because that rectangle has to be in the present list anyway.
 * See the comment at the top of sh_cursor.c. */
void sh_cursor_draw(GfxSurface *s);
/* The rectangle the cursor currently occupies (for dirty tracking). */
CRect sh_cursor_bounds(void);

/* ---- Icon pack (optional themed icons, sh_iconpack.c) ---------------- */
/* Return the pack icon named 'name' (e.g. "computer", "tb-copy", "ql-notepad"),
 * or NULL if no pack is configured or the pack lacks that icon -- callers then
 * fall back to their procedural/text drawing. Icons are magenta-keyed
 * (GFX_COLORKEY) and cached for the session. The active pack is chosen by
 * [Assets] Icons= (see assets/icons/README.md). App-facing, like ui_palette(). */
const GfxSurface *sh_iconpack_icon(const char *name);

#endif /* CASTALIA_SHELL_H */
