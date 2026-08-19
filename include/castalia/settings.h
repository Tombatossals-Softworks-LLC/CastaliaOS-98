/*
 * settings.h - User settings model, persisted to CASTALIA.INI.
 *
 * A tiny, typed view over the repairable INI (cfg_*). The shell loads it once
 * at startup, applies it (theme, clock format), and the Control Center edits
 * and saves it. The Boot/SafeMode key is shared with CBOOT, so "Safe Mode on
 * next boot" set here is honored by the launcher.
 *
 * There is exactly one active settings object (settings_get()); it lives for
 * the session and holds the path it was loaded from so settings_save() is a
 * no-argument call from the UI.
 */
#ifndef CASTALIA_SETTINGS_H
#define CASTALIA_SETTINGS_H

#include "castalia/ctypes.h"

/* Theme presets (see sh_theme_preset). Keep the count in sync with the shell. */
#define SETTINGS_THEME_COUNT 5
enum {
    SETTINGS_THEME_CLASSIC = 0, /* royal blue        */
    SETTINGS_THEME_STORM   = 1, /* cool graphite     */
    SETTINGS_THEME_FOREST  = 2, /* deep green        */
    SETTINGS_THEME_CONTRAST= 3, /* high contrast     */
    SETTINGS_THEME_AURORA  = 4  /* XP-Luna glossy blue */
};

typedef struct {
    int   theme_preset;    /* [Theme]   Preset  (0..SETTINGS_THEME_COUNT-1) */
    int   theme_colors;    /* [Theme]   Colors  (0 native, 16, or 256)      */
    cbool clock_seconds;   /* [Taskbar] ClockSeconds                        */
    cbool safe_next_boot;  /* [Boot]    SafeMode  (shared with CBOOT)        */
    cbool sound_enabled;   /* [Sound]   Enabled                             */
    cbool animations;      /* [Shell]   Animations  (window/menu motion)     */
    /*
     * [Shell] DragOutline -- drag a window by its OUTLINE rather than
     * carrying its contents along.
     *
     * Moving a window with its contents repaints two places on every motion
     * event: the area it left and the area it now covers. Measured at 118,201
     * pixels for four pixels of travel on a 400x280 window -- 105% of the
     * frame, per event, for as long as the mouse is moving. An outline is
     * four thin lines, about two per cent of that, and it is what the systems
     * this desktop is modelled on did by default on hardware this old.
     *
     * Off by default: the compositor here is fast enough to carry the
     * contents, and watching the window itself move is the better experience
     * when the machine can afford it. Forced on in safe mode, where the video
     * path is the fallback one and nothing else is animated either.
     */
    cbool drag_outline;
    int   wallpaper_mode;  /* [Shell]   WallpaperMode (0 none,1 center,2 tile,3 stretch) */
    char  wallpaper[CASTALIA_MAX_PATH]; /* [Shell] Wallpaper (BMP path, empty = none) */
    int   screensaver;     /* [Shell]   ScreenSaver (0..SETTINGS_SAVER_COUNT-1) */
    cbool welcome_startup; /* [Shell]   Welcome (show the tour at startup)      */
    char  icons_dir[CASTALIA_MAX_PATH]; /* [Assets] Icons (icon-pack dir, empty = procedural art) */
    int   font_face;       /* [Assets] Font (0 = system 8x8, 1 = Spleen 5x8)  */
    /*
     * [Mouse] DoubleClickMs -- how long a second click may arrive after the
     * first and still count as a double.
     *
     * This lived as a 400 in sh_desktop.c and, independently, as another 400
     * in wm_dispatch.c: the desktop icons and the title bars each had their
     * own idea of what a double-click is, agreeing only because nobody had
     * touched either. It is one number now, and a setting, because it is one
     * of the few that people genuinely differ on -- and because a machine
     * this old is often driven by somebody whose hands are not steady.
     */
    int   dblclick_ms;
    /* [Keyboard] RepeatDelayMs / RepeatCps -- applied to the BIOS at startup
     * and whenever the panel changes them. Only the steps the BIOS actually
     * has; see plat_set_key_repeat. */
    int   key_delay_ms;
    int   key_cps;
} CastaliaSettings;

/* What the Mouse panel offers, and the bounds anything else must respect.
 * Below the floor a deliberate double-click becomes unachievable; above the
 * ceiling two separate clicks a second apart start merging into one. */
#define SETTINGS_DBLCLICK_MIN 200
#define SETTINGS_DBLCLICK_MAX 900

/* The delays and rates the BIOS has, in the order the Keyboard panel offers
 * them. Anything else would round to one of these inside INT 16h. */
#define SETTINGS_KEY_DELAY_COUNT 4
#define SETTINGS_KEY_CPS_COUNT   8

/* Wallpaper display modes. */
enum {
    WALLPAPER_NONE    = 0,
    WALLPAPER_CENTER  = 1,
    WALLPAPER_TILE    = 2,
    WALLPAPER_STRETCH = 3
};

/* Screensaver modes (see sh_saver_draw_mode). */
#define SETTINGS_SAVER_COUNT 4
enum {
    SETTINGS_SAVER_CASTLE    = 0,
    SETTINGS_SAVER_STARFIELD = 1,
    SETTINGS_SAVER_PLASMA    = 2,
    SETTINGS_SAVER_MYSTIFY   = 3
};

/* The single active settings object (valid after settings_load). */
CastaliaSettings *settings_get(void);

/* Reset to built-in defaults (does not touch disk). */
void settings_defaults(void);

/* Load from an INI file into the active object, remembering the path for
 * settings_save(). Missing file / missing keys fall back to defaults. */
void settings_load(const char *ini_path);

/* Write the active object back to the remembered path. Preserves any other
 * sections/keys already in the file. Returns CE_OK or an error. */
CResult settings_save(void);

/* The remembered INI path (empty string if settings_load never set one). Lets
 * other subsystems persist their own sections in the same repairable file. */
const char *settings_ini_path(void);

#endif /* CASTALIA_SETTINGS_H */
