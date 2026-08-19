/*
 * sh_splash.c - The boot splash: castle crest, product name, progress bar.
 *
 * Drawn straight into the back buffer and presented full-screen for a moment
 * at interactive startup (main.c runs a short progress loop before the desktop
 * appears), in the tradition of the Win9x/XP boot screens -- but original art.
 * It is a pure draw function with no state, so the headless build can render
 * and screenshot it for verification (--splash-demo).
 */
#include "sh_internal.h"
#include "castalia/castalia.h"
#include "castalia/gfx.h"
#include "castalia/sys.h"

void sh_splash_draw(GfxSurface *s, int progress)
{
    CRect full, track, fill;
    char line[64];
    int w, h, cx, bar_w, bar_x, bar_y, fw;

    if (s == NULL) { return; }
    if (progress < 0)   { progress = 0; }
    if (progress > 256) { progress = 256; }
    w = s->w; h = s->h; cx = w / 2;
    gfx_reset_clip(s);

    /* Deep navy stage with a subtle brighter core. */
    full = crect_make(0, 0, w, h);
    gfx_vgradient3(s, &full, GFX_RGB(0x06, 0x0C, 0x24), GFX_RGB(0x12, 0x2A, 0x5E),
                   GFX_RGB(0x04, 0x08, 0x1A), 420);

    /* The crest, glowing. */
    sh_logo_draw(s, cx - 40, h * 32 / 100 - 40, 80);

    /* Product name + edition, centered. */
    sys_snprintf(line, sizeof(line), "%s", CASTALIA_NAME);
    gfx_draw_text_shadow(s, GFX_FONT_BOLD,
                         cx - gfx_text_width(GFX_FONT_BOLD, line) / 2,
                         h * 38 / 100 + 34, line,
                         GFX_RGB(0xFF, 0xFF, 0xFF), GFX_RGB(0x04, 0x0A, 0x1E));
    sys_snprintf(line, sizeof(line), "%s  %s", CASTALIA_EDITION,
                 CASTALIA_VER_STRING);
    gfx_draw_text(s, GFX_FONT_SYSTEM,
                  cx - gfx_text_width(GFX_FONT_SYSTEM, line) / 2,
                  h * 38 / 100 + 48, line, GFX_RGB(0x9A, 0xB2, 0xD8));

    /* Progress bar: sunken track, gold fill with a bright top line. */
    bar_w = 240;
    bar_x = cx - bar_w / 2;
    bar_y = h * 62 / 100;
    track = crect_make(bar_x, bar_y, bar_w, 14);
    gfx_fill_rect(s, &track, GFX_RGB(0x02, 0x06, 0x14));
    gfx_frame_rect(s, &track, GFX_RGB(0x3A, 0x54, 0x8C));
    /* Shared with every other meter in the system, so the clamping is in
     * one place rather than in the four that used to have their own. */
    fw = ui_meter_fill((cs32)progress, (cs32)256, bar_w - 4);
    if (fw > 0) {
        fill = crect_make(bar_x + 2, bar_y + 2, fw, 10);
        gfx_vgradient3(s, &fill, GFX_RGB(0xF2, 0xDA, 0x8A), GFX_RGB(0xD2, 0xA4, 0x40),
                       GFX_RGB(0x8A, 0x66, 0x1E), 300);
        gfx_hline(s, bar_x + 2, bar_y + 2, fw, GFX_RGB(0xFF, 0xF2, 0xC0));
    }
    gfx_draw_text(s, GFX_FONT_SYSTEM,
                  cx - gfx_text_width(GFX_FONT_SYSTEM, "Starting CastaliaOS...") / 2,
                  bar_y - 16, "Starting CastaliaOS...", GFX_RGB(0xC8, 0xD6, 0xEE));
}

void sh_shutdown_screen(GfxSurface *s)
{
    CRect full;
    const char *l1 = "It is now safe to turn off";
    const char *l2 = "your computer.";
    int w, h, cx;

    if (s == NULL) { return; }
    w = s->w; h = s->h; cx = w / 2;
    gfx_reset_clip(s);

    /* The amber-on-black of the era's shutdown screen, gently graded. */
    full = crect_make(0, 0, w, h);
    gfx_vgradient3(s, &full, GFX_RGB(0x08, 0x06, 0x02), GFX_RGB(0x16, 0x10, 0x04),
                   GFX_RGB(0x06, 0x04, 0x01), 440);

    sh_logo_draw(s, cx - 40, h * 30 / 100 - 40, 80);

    gfx_draw_text_shadow(s, GFX_FONT_BOLD,
                         cx - gfx_text_width(GFX_FONT_BOLD, l1) / 2, h * 52 / 100,
                         l1, GFX_RGB(0xFF, 0xC8, 0x40), GFX_RGB(0x1A, 0x0E, 0x00));
    gfx_draw_text_shadow(s, GFX_FONT_BOLD,
                         cx - gfx_text_width(GFX_FONT_BOLD, l2) / 2, h * 52 / 100 + 16,
                         l2, GFX_RGB(0xFF, 0xC8, 0x40), GFX_RGB(0x1A, 0x0E, 0x00));
    gfx_draw_text(s, GFX_FONT_SYSTEM,
                  cx - gfx_text_width(GFX_FONT_SYSTEM, CASTALIA_NAME) / 2,
                  h * 52 / 100 + 40, CASTALIA_NAME, GFX_RGB(0x8A, 0x70, 0x30));
}
