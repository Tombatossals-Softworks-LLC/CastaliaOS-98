/*
 * sh_theme_io.c - The theme model: defaults, presets, quantization, and the
 * INI round trip.
 *
 * Split out of sh_theme.c so it depends on nothing but the config parser and
 * the color kernel -- which is what lets the host tests save a theme, load it
 * back, and compare every field (tests/test_theme.c). Applying a theme (which
 * talks to the UI layer and the window manager) stays in sh_theme.c.
 *
 * "Castalia Classic" is an original palette: a Mediterranean stone desktop,
 * royal-blue active title bars, antique-gold accent, graphite grays. None of
 * it copies Microsoft's color scheme or artwork. A high-contrast profile is
 * used for safe mode.
 */
#include "sh_internal.h"
#include "castalia/shell.h"
#include "castalia/cfg.h"
#include "castalia/sys.h"

void sh_theme_default(ShTheme *out)
{
    if (out == NULL) { return; }
    sys_strlcpy(out->name, "Castalia Classic", sizeof(out->name));

    out->desktop_top      = GFX_RGB(0x35, 0x4A, 0x5E); /* slate stone      */
    out->desktop_bottom   = GFX_RGB(0x1E, 0x2A, 0x38); /* deep graphite    */

    out->title_active_l   = GFX_RGB(0x1B, 0x3F, 0x7A); /* royal blue       */
    out->title_active_r   = GFX_RGB(0x3E, 0x69, 0xB0);
    out->title_inactive_l = GFX_RGB(0x5A, 0x64, 0x72); /* calm slate       */
    out->title_inactive_r = GFX_RGB(0x76, 0x80, 0x8E);
    out->title_text       = GFX_RGB(0xF3, 0xF6, 0xFB);

    out->ui.face          = GFX_RGB(0xC6, 0xCB, 0xD4);
    out->ui.light         = GFX_RGB(0xEF, 0xF2, 0xF6);
    out->ui.dark          = GFX_RGB(0x8A, 0x93, 0xA0);
    out->ui.darker        = GFX_RGB(0x3A, 0x41, 0x4B);
    out->ui.text          = GFX_RGB(0x1A, 0x1E, 0x24);
    out->ui.text_disabled = GFX_RGB(0x8A, 0x93, 0xA0);
    out->ui.accent        = GFX_RGB(0x1B, 0x3F, 0x7A);
    out->ui.accent_text   = GFX_RGB(0xF3, 0xF6, 0xFB);

    out->taskbar_face     = GFX_RGB(0xC6, 0xCB, 0xD4);
    out->title_height     = 20;
    out->border_width     = 2;
    out->colors           = 0; /* native / truecolor: no quantization */
}

/* Quantize the theme's discrete colors to its 'colors' target. Gathering every
 * color field into one array keeps this a single call into the tested gfx
 * kernel; for a 16-color target we also flatten the gradient endpoints so the
 * desktop and title bars render in true EGA colors (an interpolated gradient
 * would otherwise wander off the 16-color palette). */
void sh_theme_quantize(ShTheme *t)
{
    CColor c[15];
    if (t == NULL || t->colors == 0) { return; }

    if (t->colors == 16) {
        /* Flatten gradients to their top/left endpoint before quantizing. */
        t->desktop_bottom   = t->desktop_top;
        t->title_active_r   = t->title_active_l;
        t->title_inactive_r = t->title_inactive_l;
    }

    c[0]  = t->desktop_top;    c[1]  = t->desktop_bottom;
    c[2]  = t->title_active_l; c[3]  = t->title_active_r;
    c[4]  = t->title_inactive_l; c[5] = t->title_inactive_r;
    c[6]  = t->title_text;     c[7]  = t->ui.face;
    c[8]  = t->ui.light;       c[9]  = t->ui.dark;
    c[10] = t->ui.darker;      c[11] = t->ui.text;
    c[12] = t->ui.accent;      c[13] = t->ui.accent_text;
    c[14] = t->taskbar_face;

    gfx_quantize_colors(c, 15, t->colors);

    t->desktop_top    = c[0];  t->desktop_bottom   = c[1];
    t->title_active_l = c[2];  t->title_active_r   = c[3];
    t->title_inactive_l = c[4]; t->title_inactive_r = c[5];
    t->title_text     = c[6];  t->ui.face          = c[7];
    t->ui.light       = c[8];  t->ui.dark          = c[9];
    t->ui.darker      = c[10]; t->ui.text          = c[11];
    t->ui.accent      = c[12]; t->ui.accent_text   = c[13];
    t->taskbar_face   = c[14];
    /* Disabled text tracks 'dark' after quantization. */
    t->ui.text_disabled = t->ui.dark;
}

/* High-contrast, minimal theme for safe mode. */
static void sh_theme_safe(ShTheme *out)
{
    sh_theme_default(out);
    sys_strlcpy(out->name, "Castalia Safe", sizeof(out->name));
    out->desktop_top    = GFX_RGB(0x00, 0x00, 0x2A);
    out->desktop_bottom = GFX_RGB(0x00, 0x00, 0x2A);
    out->title_active_l = GFX_RGB(0x00, 0x00, 0x80);
    out->title_active_r = GFX_RGB(0x00, 0x00, 0x80);
    out->ui.accent      = GFX_RGB(0x00, 0x00, 0x80);
}

void sh_theme_safe_mode(ShTheme *out) { sh_theme_safe(out); }

/* Recolor the accent + title + desktop for a preset, keeping the beveled
 * control grays (which read well against any accent). */
static void theme_recolor(ShTheme *out, const char *name,
                          CColor desk_top, CColor desk_bot,
                          CColor title_l, CColor title_r, CColor accent)
{
    sys_strlcpy(out->name, name, sizeof(out->name));
    out->desktop_top    = desk_top;
    out->desktop_bottom = desk_bot;
    out->title_active_l = title_l;
    out->title_active_r = title_r;
    out->ui.accent      = accent;
    /*
     * The INACTIVE title follows the active one, desaturated and lightened.
     *
     * It used to be whatever sh_theme_default left behind -- Classic's calm
     * slate -- so Forest Green painted the focused window's title bar green
     * and every other window's the previous theme's BLUE. Two themes on one
     * screen, and nothing reported it because an inactive title bar drawn in
     * a valid colour is not an error.
     *
     * Classic and Aurora were right only because each states its own pair;
     * deriving it here means a preset cannot forget. 150/256 is about three
     * fifths of the way to a neutral grey: far enough that the focused window
     * is obvious at a glance, near enough that the hue survives.
     */
    out->title_inactive_l = gfx_tint(title_l, GFX_RGB(0x9A, 0x9A, 0x9A), 150);
    out->title_inactive_r = gfx_tint(title_r, GFX_RGB(0x9A, 0x9A, 0x9A), 150);
}

void sh_theme_preset(ShTheme *out, int preset)
{
    if (out == NULL) { return; }
    sh_theme_default(out); /* start from Classic grays/metrics */
    switch (preset) {
    case 1: /* Storm -- cool graphite/steel */
        theme_recolor(out, "Castalia Storm",
                      GFX_RGB(0x3C, 0x40, 0x48), GFX_RGB(0x20, 0x23, 0x29),
                      GFX_RGB(0x44, 0x50, 0x62), GFX_RGB(0x6A, 0x78, 0x8C),
                      GFX_RGB(0x44, 0x50, 0x62));
        break;
    case 2: /* Forest -- deep green */
        theme_recolor(out, "Castalia Forest",
                      GFX_RGB(0x2A, 0x40, 0x30), GFX_RGB(0x16, 0x24, 0x1B),
                      GFX_RGB(0x1E, 0x5A, 0x38), GFX_RGB(0x3C, 0x84, 0x58),
                      GFX_RGB(0x1E, 0x5A, 0x38));
        break;
    case 3: /* High Contrast (same as safe mode) */
        sh_theme_safe(out);
        break;
    case 4: /* Aurora -- an XP-Luna-inspired glossy blue (original palette) */
        sys_strlcpy(out->name, "Aurora", sizeof(out->name));
        out->desktop_top      = GFX_RGB(0x4E, 0x8B, 0xD8); /* sky azure       */
        out->desktop_bottom   = GFX_RGB(0x1B, 0x3E, 0x8C); /* deep blue       */
        out->title_active_l   = GFX_RGB(0x36, 0x74, 0xD6); /* bright azure    */
        out->title_active_r   = GFX_RGB(0x15, 0x3A, 0x9E); /* deep blue base  */
        out->title_inactive_l = GFX_RGB(0x8E, 0xA0, 0xC2);
        out->title_inactive_r = GFX_RGB(0x5C, 0x6E, 0x90);
        out->title_text       = GFX_RGB(0xFF, 0xFF, 0xFF);
        out->ui.face          = GFX_RGB(0xD8, 0xE0, 0xEE); /* silver-blue     */
        out->ui.light         = GFX_RGB(0xFF, 0xFF, 0xFF);
        out->ui.dark          = GFX_RGB(0x92, 0xA2, 0xBA);
        out->ui.darker        = GFX_RGB(0x40, 0x50, 0x68);
        out->ui.text          = GFX_RGB(0x14, 0x1C, 0x28);
        out->ui.text_disabled = GFX_RGB(0x92, 0xA2, 0xBA);
        out->ui.accent        = GFX_RGB(0x2A, 0x5B, 0xC8);
        out->ui.accent_text   = GFX_RGB(0xFF, 0xFF, 0xFF);
        out->taskbar_face     = GFX_RGB(0x35, 0x64, 0xC8); /* XP-blue taskbar */
        break;
    case 0:
    default:
        break; /* Classic */
    }
}

const char *sh_theme_preset_name(int preset)
{
    switch (preset) {
    case 1:  return "Storm Gray";
    case 2:  return "Forest Green";
    case 3:  return "High Contrast";
    case 4:  return "Aurora (XP)";
    default: return "Castalia Classic";
    }
}

CResult sh_theme_load(ShTheme *out, const char *ini_path)
{
    CfgFile *cfg;
    cbool existed = CFALSE;
    if (out == NULL) { return CE_INVALID; }
    sh_theme_default(out);
    cfg = cfg_load(ini_path, &existed);
    if (cfg == NULL) { return CE_IO; }
    if (!existed) { cfg_free(cfg); return CE_NOTFOUND; }

    sys_strlcpy(out->name, cfg_get_str(cfg, "Theme", "Name", out->name),
                sizeof(out->name));
    out->desktop_top    = cfg_get_color(cfg, "Colors", "DesktopTop", out->desktop_top);
    out->desktop_bottom = cfg_get_color(cfg, "Colors", "DesktopBottom", out->desktop_bottom);
    out->title_active_l = cfg_get_color(cfg, "Colors", "TitleActiveL", out->title_active_l);
    out->title_active_r = cfg_get_color(cfg, "Colors", "TitleActiveR", out->title_active_r);
    out->title_inactive_l = cfg_get_color(cfg, "Colors", "TitleInactiveL", out->title_inactive_l);
    out->title_inactive_r = cfg_get_color(cfg, "Colors", "TitleInactiveR", out->title_inactive_r);
    out->title_text     = cfg_get_color(cfg, "Colors", "TitleText", out->title_text);
    out->ui.face        = cfg_get_color(cfg, "Colors", "Face", out->ui.face);
    out->ui.light       = cfg_get_color(cfg, "Colors", "Light", out->ui.light);
    out->ui.dark        = cfg_get_color(cfg, "Colors", "Dark", out->ui.dark);
    out->ui.darker      = cfg_get_color(cfg, "Colors", "Darker", out->ui.darker);
    out->ui.text        = cfg_get_color(cfg, "Colors", "Text", out->ui.text);
    out->ui.text_disabled = cfg_get_color(cfg, "Colors", "TextDisabled",
                                          out->ui.text_disabled);
    out->ui.accent      = cfg_get_color(cfg, "Colors", "Accent", out->ui.accent);
    out->ui.accent_text = cfg_get_color(cfg, "Colors", "AccentText", out->ui.accent_text);
    out->taskbar_face   = cfg_get_color(cfg, "Colors", "Taskbar", out->taskbar_face);
    out->title_height   = (int)cfg_get_int(cfg, "Metrics", "TitleBarHeight", out->title_height);
    out->border_width   = (int)cfg_get_int(cfg, "Metrics", "BorderWidth", out->border_width);
    /* [Theme] Colors: 16 or 256 opts a theme into the low-color pipeline;
     * anything else (or absent) stays native/truecolor. */
    out->colors         = (int)cfg_get_int(cfg, "Theme", "Colors", out->colors);
    if (out->colors != 16 && out->colors != 256) { out->colors = 0; }

    cfg_free(cfg);
    return CE_OK;
}

CResult sh_theme_save(const ShTheme *t, const char *ini_path)
{
    CfgFile *cfg;
    cbool existed = CFALSE;
    CResult rc;
    if (t == NULL || ini_path == NULL) { return CE_INVALID; }
    cfg = cfg_load(ini_path, &existed);   /* an existing file is overwritten */
    if (cfg == NULL) { return CE_IO; }

    cfg_set_str(cfg, "Theme", "Name", t->name);
    cfg_set_int(cfg, "Theme", "Colors", t->colors);
    cfg_set_color(cfg, "Colors", "DesktopTop", t->desktop_top);
    cfg_set_color(cfg, "Colors", "DesktopBottom", t->desktop_bottom);
    cfg_set_color(cfg, "Colors", "TitleActiveL", t->title_active_l);
    cfg_set_color(cfg, "Colors", "TitleActiveR", t->title_active_r);
    cfg_set_color(cfg, "Colors", "TitleInactiveL", t->title_inactive_l);
    cfg_set_color(cfg, "Colors", "TitleInactiveR", t->title_inactive_r);
    cfg_set_color(cfg, "Colors", "TitleText", t->title_text);
    cfg_set_color(cfg, "Colors", "Face", t->ui.face);
    cfg_set_color(cfg, "Colors", "Light", t->ui.light);
    cfg_set_color(cfg, "Colors", "Dark", t->ui.dark);
    cfg_set_color(cfg, "Colors", "Darker", t->ui.darker);
    cfg_set_color(cfg, "Colors", "Text", t->ui.text);
    cfg_set_color(cfg, "Colors", "TextDisabled", t->ui.text_disabled);
    cfg_set_color(cfg, "Colors", "Accent", t->ui.accent);
    cfg_set_color(cfg, "Colors", "AccentText", t->ui.accent_text);
    cfg_set_color(cfg, "Colors", "Taskbar", t->taskbar_face);
    cfg_set_int(cfg, "Metrics", "TitleBarHeight", t->title_height);
    cfg_set_int(cfg, "Metrics", "BorderWidth", t->border_width);

    rc = cfg_save(cfg, ini_path);
    cfg_free(cfg);
    return rc;
}
