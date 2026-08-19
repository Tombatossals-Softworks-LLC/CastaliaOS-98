/*
 * sh_theme.c - Applying a theme.
 *
 * The model itself -- defaults, presets, quantization and the INI round trip
 * -- lives in sh_theme_io.c, which depends on nothing the host tests cannot
 * link. This half is the part that talks to the UI layer and the window
 * manager, so it can only run inside a live shell.
 */
#include "sh_internal.h"
#include "castalia/shell.h"
#include "castalia/cfg.h"
#include "castalia/sys.h"
#include "castalia/wm.h"

static ShTheme g_active;

void sh_theme_apply(const ShTheme *theme)
{
    WmTheme wt;
    if (theme == NULL) { return; }
    g_active = *theme;

    /* Render the theme through its color target (16 / 256), if any. */
    sh_theme_quantize(&g_active);

    /* Push control colors down to the UI layer, and whether it may use the
     * glossy treatment -- the same condition the window frames use below. */
    ui_set_palette(&g_active.ui);
    ui_set_glossy(sh_glossy());

    /* Push frame colors/metrics down to the window manager. */
    wt.title_active_l   = g_active.title_active_l;
    wt.title_active_r   = g_active.title_active_r;
    wt.title_inactive_l = g_active.title_inactive_l;
    wt.title_inactive_r = g_active.title_inactive_r;
    wt.title_text       = g_active.title_text;
    wt.face             = g_active.ui.face;
    wt.light            = g_active.ui.light;
    wt.dark             = g_active.ui.dark;
    wt.darker           = g_active.ui.darker;
    wt.title_height     = g_active.title_height;
    wt.border_width     = g_active.border_width;
    /* Glossy XP chrome only in native/truecolor; the 16/256 low-color pipeline
     * and safe mode stay flat so no derived highlight wanders off the reduced
     * palette and the recovery profile keeps zero extra work. */
    wt.glossy           = sh_glossy();
    wm_set_theme(&wt);
}

const ShTheme *sh_theme_active(void) { return &g_active; }
