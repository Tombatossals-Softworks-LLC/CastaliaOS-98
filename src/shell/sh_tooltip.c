/*
 * sh_tooltip.c - Hover tooltip overlay: the little info-yellow label that
 * appears when the pointer rests on a taskbar element (task buttons, the
 * Start orb, Quick Launch, the tray, the clock).
 *
 * A shell overlay like the context menu: state lives in ShState, painting
 * happens between the popups and the cursor in sh_run_frame, and show/hide
 * mark the covered rect dirty so the layers beneath repaint cleanly. Timing
 * (the hover delay) is owned by the hover tracker in sh_taskbar.c.
 */
#include "sh_internal.h"
#include "castalia/gfx.h"
#include "castalia/sys.h"

#define TIP_PAD_X 5
#define TIP_PAD_Y 3

/* Classic tooltip colors: a pale info-yellow card with near-black ink. Kept
 * local (not themed): the palette has no info color, and these read correctly
 * on every preset; the low-color pipelines quantize them at present time. */
#define TIP_FACE   GFX_RGB(0xFF, 0xFF, 0xE1)
#define TIP_BORDER GFX_RGB(0x20, 0x20, 0x20)
#define TIP_INK    GFX_RGB(0x10, 0x10, 0x10)

void sh_tooltip_show(const char *text, int cx, int bottom)
{
    int w, h, x, y;
    if (text == NULL || text[0] == '\0') { sh_tooltip_hide(); return; }
    sys_strlcpy(g_sh.tip_text, text, sizeof(g_sh.tip_text));
    w = gfx_text_width(GFX_FONT_SYSTEM, g_sh.tip_text) + TIP_PAD_X * 2;
    h = gfx_font_height(GFX_FONT_SYSTEM) + TIP_PAD_Y * 2;

    /* Center on the anchor, clamped fully on-screen, sitting just above the
     * given bottom edge (the taskbar top for taskbar tooltips). */
    x = cx - w / 2;
    if (x + w > g_sh.screen_w - 2) { x = g_sh.screen_w - 2 - w; }
    if (x < 2) { x = 2; }
    y = bottom - h;
    if (y < 2) { y = 2; }

    if (g_sh.tip_open) { sh_mark_dirty(&g_sh.tip_rect); }  /* erase the old */
    g_sh.tip_rect = crect_make(x, y, w, h);
    g_sh.tip_open = CTRUE;
    sh_mark_dirty(&g_sh.tip_rect);
}

void sh_tooltip_hide(void)
{
    if (!g_sh.tip_open) { return; }
    g_sh.tip_open = CFALSE;
    sh_mark_dirty(&g_sh.tip_rect);
}

cbool sh_tooltip_is_open(void) { return g_sh.tip_open; }

void sh_tooltip_paint(const CRect *clip)
{
    GfxSurface *s = g_sh.back;
    CRect r = g_sh.tip_rect;
    if (!g_sh.tip_open) { return; }
    if (clip != NULL && !crect_overlaps(&r, clip)) { return; }
    if (clip != NULL) { gfx_set_clip(s, clip); }
    gfx_fill_rect(s, &r, TIP_FACE);
    gfx_frame_rect(s, &r, TIP_BORDER);
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + TIP_PAD_X, r.y0 + TIP_PAD_Y,
                  g_sh.tip_text, TIP_INK);
    gfx_reset_clip(s);
}
