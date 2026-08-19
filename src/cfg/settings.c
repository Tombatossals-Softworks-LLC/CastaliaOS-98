/*
 * settings.c - Active user settings, persisted to CASTALIA.INI via cfg_*.
 *
 * settings_save() re-loads the file first and rewrites only our keys, so a
 * hand-edited INI (comments, extra sections) survives a save from the UI --
 * the Bible's repairability rule.
 */
#include "castalia/settings.h"
#include "castalia/cfg.h"
#include "castalia/sys.h"

static CastaliaSettings g_settings;
static char             g_path[CASTALIA_MAX_PATH];
static cbool            g_have_path = CFALSE;

CastaliaSettings *settings_get(void) { return &g_settings; }

void settings_defaults(void)
{
    g_settings.theme_preset   = SETTINGS_THEME_AURORA; /* boot into the XP look */
    g_settings.theme_colors   = 0; /* native / truecolor */
    g_settings.clock_seconds  = CFALSE;
    g_settings.dblclick_ms    = 400;
    g_settings.key_delay_ms   = 500;
    g_settings.key_cps        = 10;
    g_settings.safe_next_boot = CFALSE;
    g_settings.sound_enabled  = CTRUE;
    g_settings.animations     = CTRUE;
    g_settings.drag_outline   = CFALSE;
    g_settings.wallpaper_mode = WALLPAPER_STRETCH;
    g_settings.wallpaper[0]   = '\0';   /* none by default -> gradient desktop */
    g_settings.screensaver    = SETTINGS_SAVER_CASTLE;
    g_settings.welcome_startup = CTRUE;  /* first boot greets; dismissible */
    g_settings.icons_dir[0]   = '\0';   /* none by default -> procedural icons */
    g_settings.font_face      = 1;      /* Spleen 5x8 by default (0 = original) */
}

void settings_load(const char *ini_path)
{
    CfgFile *cfg;
    int preset;

    settings_defaults();
    if (ini_path != NULL && ini_path[0] != '\0') {
        sys_strlcpy(g_path, ini_path, sizeof(g_path));
        g_have_path = CTRUE;
    } else {
        g_have_path = CFALSE;
    }
    if (!g_have_path) { return; }

    cfg = cfg_load(g_path, NULL);
    if (cfg == NULL) { return; } /* keep defaults */

    preset = (int)cfg_get_int(cfg, "Theme", "Preset", g_settings.theme_preset);
    if (preset < 0 || preset >= SETTINGS_THEME_COUNT) { preset = SETTINGS_THEME_CLASSIC; }
    g_settings.theme_preset   = preset;
    g_settings.theme_colors   = (int)cfg_get_int(cfg, "Theme", "Colors",
                                                 g_settings.theme_colors);
    if (g_settings.theme_colors != 16 && g_settings.theme_colors != 256) {
        g_settings.theme_colors = 0;
    }
    g_settings.clock_seconds  = cfg_get_bool(cfg, "Taskbar", "ClockSeconds",
                                             g_settings.clock_seconds);
    g_settings.dblclick_ms    = (int)cfg_get_int(cfg, "Mouse", "DoubleClickMs",
                                                 g_settings.dblclick_ms);
    /* A hand-edited INI is a supported way to configure this system, so a
     * number from one is clamped rather than trusted: 0 would make every
     * double-click impossible and there would be nothing on screen to say
     * why. */
    if (g_settings.dblclick_ms < SETTINGS_DBLCLICK_MIN) {
        g_settings.dblclick_ms = SETTINGS_DBLCLICK_MIN;
    }
    if (g_settings.dblclick_ms > SETTINGS_DBLCLICK_MAX) {
        g_settings.dblclick_ms = SETTINGS_DBLCLICK_MAX;
    }
    g_settings.key_delay_ms = (int)cfg_get_int(cfg, "Keyboard", "RepeatDelayMs",
                                               g_settings.key_delay_ms);
    g_settings.key_cps      = (int)cfg_get_int(cfg, "Keyboard", "RepeatCps",
                                               g_settings.key_cps);
    if (g_settings.key_delay_ms < 250)  { g_settings.key_delay_ms = 250; }
    if (g_settings.key_delay_ms > 1000) { g_settings.key_delay_ms = 1000; }
    if (g_settings.key_cps < 2)  { g_settings.key_cps = 2; }
    if (g_settings.key_cps > 30) { g_settings.key_cps = 30; }
    g_settings.safe_next_boot = cfg_get_bool(cfg, "Boot", "SafeMode",
                                             g_settings.safe_next_boot);
    g_settings.sound_enabled  = cfg_get_bool(cfg, "Sound", "Enabled",
                                             g_settings.sound_enabled);
    g_settings.animations     = cfg_get_bool(cfg, "Shell", "Animations",
                                             g_settings.animations);
    g_settings.drag_outline   = cfg_get_bool(cfg, "Shell", "DragOutline",
                                             g_settings.drag_outline);
    g_settings.wallpaper_mode = (int)cfg_get_int(cfg, "Shell", "WallpaperMode",
                                                 g_settings.wallpaper_mode);
    if (g_settings.wallpaper_mode < WALLPAPER_NONE ||
        g_settings.wallpaper_mode > WALLPAPER_STRETCH) {
        g_settings.wallpaper_mode = WALLPAPER_NONE;
    }
    g_settings.screensaver    = (int)cfg_get_int(cfg, "Shell", "ScreenSaver",
                                                 g_settings.screensaver);
    if (g_settings.screensaver < 0 ||
        g_settings.screensaver >= SETTINGS_SAVER_COUNT) {
        g_settings.screensaver = SETTINGS_SAVER_CASTLE;
    }
    g_settings.welcome_startup = cfg_get_bool(cfg, "Shell", "Welcome",
                                              g_settings.welcome_startup);
    sys_strlcpy(g_settings.wallpaper,
                cfg_get_str(cfg, "Shell", "Wallpaper", g_settings.wallpaper),
                sizeof(g_settings.wallpaper));
    sys_strlcpy(g_settings.icons_dir,
                cfg_get_str(cfg, "Assets", "Icons", g_settings.icons_dir),
                sizeof(g_settings.icons_dir));
    {
        const char *f = cfg_get_str(cfg, "Assets", "Font", "");
        if (sys_stricmp(f, "spleen") == 0)      { g_settings.font_face = 1; }
        else if (sys_stricmp(f, "system") == 0) { g_settings.font_face = 0; }
        /* anything else (incl. empty): keep the default */
    }
    cfg_free(cfg);
    SYS_LOGI("cfg", "settings loaded (theme=%d, colors=%d, clockSec=%d, safeNext=%d, sound=%d)",
             g_settings.theme_preset, g_settings.theme_colors,
             (int)g_settings.clock_seconds, (int)g_settings.safe_next_boot,
             (int)g_settings.sound_enabled);
}

CResult settings_save(void)
{
    CfgFile *cfg;
    CResult rc;
    if (!g_have_path) { return CE_INVALID; }

    /* Re-load so unrelated keys/comments are preserved, then overwrite ours. */
    cfg = cfg_load(g_path, NULL);
    if (cfg == NULL) { cfg = cfg_new(); }
    if (cfg == NULL) { return CE_NOMEM; }

    cfg_set_int (cfg, "Theme",   "Preset",       g_settings.theme_preset);
    cfg_set_int (cfg, "Theme",   "Colors",       g_settings.theme_colors);
    cfg_set_bool(cfg, "Taskbar", "ClockSeconds", g_settings.clock_seconds);
    cfg_set_int(cfg, "Mouse", "DoubleClickMs", g_settings.dblclick_ms);
    cfg_set_int(cfg, "Keyboard", "RepeatDelayMs", g_settings.key_delay_ms);
    cfg_set_int(cfg, "Keyboard", "RepeatCps", g_settings.key_cps);
    cfg_set_bool(cfg, "Boot",    "SafeMode",     g_settings.safe_next_boot);
    cfg_set_bool(cfg, "Sound",   "Enabled",      g_settings.sound_enabled);
    cfg_set_bool(cfg, "Shell",   "Animations",   g_settings.animations);
    cfg_set_bool(cfg, "Shell",   "DragOutline",  g_settings.drag_outline);
    cfg_set_int (cfg, "Shell",   "WallpaperMode", g_settings.wallpaper_mode);
    cfg_set_str (cfg, "Shell",   "Wallpaper",     g_settings.wallpaper);
    cfg_set_int (cfg, "Shell",   "ScreenSaver",   g_settings.screensaver);
    cfg_set_bool(cfg, "Shell",   "Welcome",       g_settings.welcome_startup);
    cfg_set_str (cfg, "Assets",  "Icons",         g_settings.icons_dir);
    cfg_set_str (cfg, "Assets",  "Font",          g_settings.font_face == 1 ? "spleen" : "system");

    rc = cfg_save(cfg, g_path);
    cfg_free(cfg);
    if (rc == CE_OK) {
        SYS_LOGI("cfg", "settings saved to %s", g_path);
    } else {
        SYS_LOGW("cfg", "settings save failed (%d)", (int)rc);
    }
    return rc;
}

const char *settings_ini_path(void)
{
    return g_have_path ? g_path : "";
}
