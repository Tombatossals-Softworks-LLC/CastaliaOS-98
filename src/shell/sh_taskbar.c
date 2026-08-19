/*
 * sh_taskbar.c - The taskbar: launcher button, running-task buttons, tray,
 *                and clock. Anchored to the bottom edge (Bible v1 scope).
 */
#include "sh_internal.h"
#include "castalia/gfx.h"
#include "castalia/sys.h"
#include "castalia/settings.h"
#include "castalia/snd.h"
#include "castalia/net.h"
#include "castalia/clip.h"

#define CLOCK_W 72   /* wide enough for HH:MM:SS when seconds are shown */
#define TASK_BTN_MAX_W 132
#define PAGER_CELL 15
#define PAGER_W  (PAGER_CELL * WM_DESKTOP_COUNT)
#define TRAY_CELL 14
#define TRAY_N   3
#define TRAY_W   (TRAY_CELL * TRAY_N + 4)

/* Quick Launch: the small one-click icon strip beside the Start button (the
 * Windows-98-SE taskbar signature). Fixed, first-party entries for v1. */
#define QUICK_CELL 20
#define QUICK_N    4
#define QUICK_W    (QUICK_CELL * QUICK_N + 4)

static CRect g_clock_well;
static CRect g_tray_rect;
static CRect g_pager_rect;
static CRect g_quick_rect;
static int   g_task_x1;   /* right boundary of the task-button strip */

static const int QL_CMD[QUICK_N] = {
    SH_CMD_SHOWDESKTOP, SH_CMD_FILEMANAGER, SH_CMD_NOTEPAD, SH_CMD_MINES
};
static const char *QL_TIP[QUICK_N] = {
    "Show Desktop", "File Manager", "Notepad", "Mines"
};

/* Hover state (which taskbar element the pointer is over): -1 none, >=0 a task
 * button index; g_hover_start is the launcher button; g_hover_quick a Quick
 * Launch cell. Repaint on change gives the buttons a live "hot" highlight. */
static int   g_hover_task  = -1;
static cbool g_hover_start = CFALSE;
static int   g_hover_quick = -1;

/* Tooltip timing: the encoded hover target (0 none, 1 Start, 2 clock,
 * 100+i task, 200+j tray, 300+k Quick Launch), when it was acquired, and
 * whether its tooltip has been raised. The tooltip shows after the pointer
 * rests SH_TIP_DELAY_MS on one target and hides the moment it moves off. */
#define SH_TIP_DELAY_MS 600
static int   g_tip_target = 0;
static cu32  g_tip_since  = 0;
static cbool g_tip_shown  = CFALSE;

static cbool taskbar_glossy(void) { return sh_glossy(); }

/* ---- the Start orb ---------------------------------------------------- */
/*
 * In glossy (Aurora) mode the launcher button is a spherical ORB that overhangs
 * the taskbar's top edge, bearing the Castalia castle -- the XP/Vista start-orb
 * flourish. It is expensive to shade, so all three states (up / hover / pressed)
 * are rendered ONCE into small keyed sprites when the taskbar lays out or the
 * theme changes, and painting is a single keyed blit.
 */
enum { ORB_UP = 0, ORB_HOT, ORB_DOWN, ORB_STATES };
#define ORB_OVERHANG 9   /* how far the orb rises above the taskbar edge */

static GfxSurface *g_orb[ORB_STATES];

/*
 * Render one launcher-button state: a shaded sphere with the Castalia mark
 * inset on it, on a keyed background so it blits over whatever is behind the
 * overhang. Hover brightens it and pressed darkens it -- the artwork itself
 * never changes, which is what keeps the brand consistent.
 *
 * The SPHERE is drawn here rather than borrowing sh_logo_draw_plated's pale
 * disc, and it is what makes this a button. That call used to be the whole of
 * this function, and once the mark became a pack image it stopped drawing any
 * plate at all (an image is a finished mark that needs no disc behind it on
 * the launcher's header, which is what the plate was for). On the taskbar that
 * left no button whatsoever: the bare castle, drawn at the full 38px of a
 * button that deliberately overhangs the bar, so the artwork floated over the
 * wallpaper with a hard clipped edge, nothing to press, and no way to tell
 * hover from pressed except that a picture changed colour.
 *
 * So the mark sits INSIDE the sphere at 58%, which is also what stops it
 * spilling: the button's size is a taskbar measurement, not an artwork one.
 */
static GfxSurface *orb_render(int d, int state)
{
    GfxSurface *o = gfx_surface_new(d, d);
    const UiPalette *p = ui_palette();
    CColor top, bot, rim, spec;
    /* R is one short of half the box: with R = d/2 the topmost and leftmost
     * points land exactly on the boundary while the opposite sides fall one
     * pixel short of it, and the circle grows a single-pixel nub at 12 and 9
     * o'clock that reads as a speck of dirt on the wallpaper. */
    int cx = d / 2, cy = d / 2, R = d / 2 - 1, m;
    int x, y;

    if (o == NULL) { return NULL; }
    gfx_clear(o, GFX_COLORKEY);

    /*
     * Pale, not accent-coloured: the taskbar IS the accent, and an orb in the
     * same blue would be a smudge on it. Tinting the theme's own accent toward
     * white keeps it in the family while staying far enough from the bar to
     * read as a raised object -- and it follows a theme change for free.
     */
    /* gfx_tint's amount is out of 256, not out of 100. */
    top  = gfx_tint(p->accent, GFX_RGB(0xFF, 0xFF, 0xFF), 228);
    bot  = gfx_tint(p->accent, GFX_RGB(0xFF, 0xFF, 0xFF), 110);
    rim  = gfx_tint(p->accent, GFX_RGB(0x00, 0x00, 0x00), 110);
    spec = GFX_RGB(0xFF, 0xFF, 0xFF);
    if (state == ORB_HOT) {
        top = gfx_tint(top, GFX_RGB(0xFF, 0xFF, 0xFF), 90);
        bot = gfx_tint(bot, GFX_RGB(0xFF, 0xFF, 0xFF), 90);
    } else if (state == ORB_DOWN) {
        top = gfx_tint(top, GFX_RGB(0x00, 0x00, 0x00), 70);
        bot = gfx_tint(bot, GFX_RGB(0x00, 0x00, 0x00), 70);
        rim = gfx_tint(rim, GFX_RGB(0x00, 0x00, 0x00), 60);
    }

    for (y = 0; y < d; y++) {
        for (x = 0; x < d; x++) {
            long dx = x - cx, dy = y - cy;
            long r2 = dx * dx + dy * dy;
            CColor c;
            if (r2 > (long)R * R) { continue; }
            /* Vertical gradient for the body, one darker ring at the edge so
             * the circle has a boundary against a light wallpaper as well as a
             * dark one. */
            c = gfx_tint(top, bot, (y * 256) / (d > 0 ? d : 1));
            if (r2 > (long)(R - 1) * (R - 1)) { c = rim; }
            gfx_put_pixel(o, x, y, c);
        }
    }

    /* One specular cap across the top third -- the whole of the "glossy" here.
     * Pressed loses it, because a sphere you have pushed in does not catch the
     * light the same way, and that is the difference a person actually sees. */
    if (state != ORB_DOWN) {
        for (y = 2; y < d / 3; y++) {
            for (x = 0; x < d; x++) {
                long dx = x - cx, dy = y - cy;
                long ex = (dx * 100) / (R > 0 ? R : 1);
                long ey = ((dy + R / 2) * 170) / (R > 0 ? R : 1);
                if (dx * dx + dy * dy > (long)(R - 2) * (R - 2)) { continue; }
                if (ex * ex + ey * ey > 10000L) { continue; }
                gfx_put_pixel(o, x, y,
                              gfx_tint(gfx_get_pixel(o, x, y), spec, 130));
            }
        }
    }

    /* The mark, centred and inset. Nudged down by the same amount the
     * specular cap occupies, so it sits in the sphere rather than on its
     * horizon. */
    m = (d * 58) / 100;
    if (m >= 8) {
        sh_logo_draw(o, cx - m / 2, cy - m / 2 + d / 16, m);
    }
    return o;
}

void sh_taskbar_free_orb(void)
{
    int i;
    for (i = 0; i < ORB_STATES; i++) {
        if (g_orb[i] != NULL) { gfx_surface_free(g_orb[i]); g_orb[i] = NULL; }
    }
}

void sh_taskbar_build_orb(void)
{
    int i, d = crect_w(&g_sh.launcher_button);
    sh_taskbar_free_orb();
    if (!taskbar_glossy() || d < 20) { return; }
    for (i = 0; i < ORB_STATES; i++) { g_orb[i] = orb_render(d, i); }
}

/* Mark the taskbar INCLUDING the orb overhang dirty (state changes must
 * repaint the part of the orb that rises above the bar). */
void sh_taskbar_dirty(void)
{
    CRect r = g_sh.taskbar_rect;
    if (g_sh.launcher_button.y0 < r.y0) { r.y0 = g_sh.launcher_button.y0; }
    sh_mark_dirty(&r);
}

void sh_taskbar_layout(void)
{
    int th = (g_sh.screen_h >= 600) ? 30 : 26;
    int y  = g_sh.screen_h - th + 4;
    int rx = g_sh.screen_w - 4;
    g_sh.taskbar_h = th;
    g_sh.taskbar_rect = crect_make(0, g_sh.screen_h - th, g_sh.screen_w, th);
    if (sh_glossy()) {
        /* The Start orb: a circle one overhang taller than the bar, rising
         * above the taskbar edge (the sprite is keyed, so the square's corners
         * show the desktop/windows behind). */
        int d = th + ORB_OVERHANG - 1;
        g_sh.launcher_button = crect_make(4, g_sh.screen_h - d - 1, d, d);
    } else {
        g_sh.launcher_button = crect_make(4, g_sh.taskbar_rect.y0 + 3, 88, th - 6);
    }

    /* Right-anchored blocks, laid out right to left: clock, tray, pager. */
    g_clock_well = crect_make(rx - CLOCK_W, y, CLOCK_W, th - 8);  rx -= CLOCK_W + 5;
    g_tray_rect  = crect_make(rx - TRAY_W, y, TRAY_W, th - 8);    rx -= TRAY_W + 5;
    g_pager_rect = crect_make(rx - PAGER_W, y, PAGER_W, th - 8);  rx -= PAGER_W + 5;
    g_task_x1 = rx;  /* task buttons fill the space up to here */

    /* Quick Launch sits between the Start button and the task strip. */
    g_quick_rect = crect_make(g_sh.launcher_button.x1 + 6, y, QUICK_W, th - 8);

    /* Reserve the work area above the taskbar so maximized windows stop at it. */
    {
        CRect work = crect_make(0, 0, g_sh.screen_w, g_sh.taskbar_rect.y0);
        wm_set_work_area(&work);
    }

    /* (Re)render the Start orb sprites for the current metrics/theme. */
    sh_taskbar_build_orb();
}

/* A small antique-gold shield, the Castalia mark for the launcher button. */
static void draw_shield(GfxSurface *s, int x, int y)
{
    CColor gold = GFX_RGB(0xC7, 0x9A, 0x3C);
    CColor dk   = GFX_RGB(0x7E, 0x5F, 0x1C);
    CColor hi   = GFX_RGB(0xE8, 0xC7, 0x74);
    static const char *art[12] = {
        " XXXXXXX ",
        "XX.....XX",
        "X..XXX..X",
        "X.X...X.X",
        "X.X...X.X",
        "X..XXX..X",
        "X.......X",
        "X.......X",
        " X.....X ",
        "  X...X  ",
        "   X.X   ",
        "    X    "
    };
    int r;
    for (r = 0; r < 12; r++) {
        const char *ln = art[r];
        int c;
        for (c = 0; ln[c] != '\0'; c++) {
            if (ln[c] == 'X') { gfx_put_pixel(s, x + c, y + r, dk); }
            else if (ln[c] == '.') {
                gfx_put_pixel(s, x + c, y + r, (r < 5) ? hi : gold);
            }
        }
    }
}

static void draw_launcher_button(GfxSurface *s)
{
    const UiPalette *p = ui_palette();
    CRect r = g_sh.launcher_button;
    int off = g_sh.launcher_open ? 1 : 0;

    if (taskbar_glossy() && g_orb[ORB_UP] != NULL) {
        /* The Castalia orb: pick the state sprite and blit it keyed, so the
         * circle overhangs the bar and the corners show what is behind. */
        int st = g_sh.launcher_open ? ORB_DOWN : (g_hover_start ? ORB_HOT : ORB_UP);
        GfxSurface *o = (g_orb[st] != NULL) ? g_orb[st] : g_orb[ORB_UP];
        CRect src = crect_make(0, 0, o->w, o->h);
        gfx_blit(s, r.x0, r.y0, o, &src, GFX_BLIT_KEYED);
        return;
    }
    gfx_bevel(s, &r, g_sh.launcher_open ? GFX_BEVEL_SUNKEN : GFX_BEVEL_RAISED,
              p->light, p->dark, p->face);
    draw_shield(s, r.x0 + 6 + off, r.y0 + (crect_h(&r) - 12) / 2 + off);
    {
        CRect tr = r;
        tr.x0 += 20 + off; tr.y0 += off;
        gfx_draw_text_rect(s, GFX_FONT_BOLD, &tr, "Castalia", p->text,
                           GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);
    }
}

/* Collect windows that deserve a taskbar button: not menus (POPUP) and not
 * transient modal dialogs, and only on the current virtual desktop. Returns the
 * count written to 'out'. */
static int taskbar_windows(WmWindow **out, int max)
{
    int i, n = wm_window_count(), c = 0;
    int cur = wm_current_desktop();
    for (i = 0; i < n && c < max; i++) {
        WmWindow *w = wm_window_at(i);
        if (w == NULL) { continue; }
        if (wm_style(w) & (WM_STYLE_POPUP | WM_STYLE_MODAL)) { continue; }
        if (wm_window_desktop(w) != cur) { continue; }
        out[c++] = w;
    }
    return c;
}

/* ---- desktop pager --------------------------------------------------- */
static void draw_pager(GfxSurface *s)
{
    const UiPalette *p = ui_palette();
    int n = wm_desktop_count(), cur = wm_current_desktop(), i;
    int cw = crect_w(&g_pager_rect) / n;
    for (i = 0; i < n; i++) {
        CRect c = crect_make(g_pager_rect.x0 + i * cw, g_pager_rect.y0,
                             cw - 1, crect_h(&g_pager_rect));
        cbool on = (i == cur) ? CTRUE : CFALSE;
        char num[4];
        gfx_bevel(s, &c, on ? GFX_BEVEL_SUNKEN : GFX_BEVEL_RAISED,
                  p->light, p->dark, on ? p->accent : p->face);
        sys_snprintf(num, sizeof(num), "%d", i + 1);
        gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &c, num,
                           on ? p->accent_text : p->text,
                           GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
    }
}

/* Blit a tray pack icon centered in its cell; returns CFALSE if the pack has no
 * such icon (caller then draws the procedural glyph). */
static cbool tray_pack_icon(GfxSurface *s, const char *name, int cellx, int cy)
{
    const GfxSurface *ic = sh_iconpack_icon(name);
    CRect src;
    if (ic == NULL) { return CFALSE; }
    src = crect_make(0, 0, ic->w, ic->h);
    gfx_blit(s, cellx + (TRAY_CELL - ic->w) / 2, cy - ic->h / 2, ic, &src,
             GFX_BLIT_KEYED);
    return CTRUE;
}

/* ---- system tray ----------------------------------------------------- */
static void draw_tray(GfxSurface *s)
{
    const UiPalette *p = ui_palette();
    CColor grn = GFX_RGB(0x2E, 0xA0, 0x44);
    CColor gry = GFX_RGB(0x80, 0x86, 0x90);
    CColor red = GFX_RGB(0xC0, 0x20, 0x20);
    int x = g_tray_rect.x0 + 3;
    int cy = g_tray_rect.y0 + crect_h(&g_tray_rect) / 2;

    gfx_bevel(s, &g_tray_rect, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, p->face);

    /* Sound: a small speaker; green when on, slashed red when muted. */
    if (!tray_pack_icon(s, snd_enabled() ? "tray-sound-on" : "tray-sound-off",
                        x, cy)) {
        cbool on = snd_enabled();
        CColor col = on ? grn : gry;
        CRect box = crect_make(x, cy - 2, 3, 5);
        gfx_fill_rect(s, &box, col);
        gfx_line(s, x + 3, cy - 2, x + 6, cy - 5, col);
        gfx_line(s, x + 3, cy + 2, x + 6, cy + 5, col);
        gfx_vline(s, x + 6, cy - 5, 11, col);
        if (!on) { gfx_line(s, x, cy - 6, x + 8, cy + 5, red); }
    }
    /* Network: two nodes + a link; green when a driver/NIC is up. */
    if (!tray_pack_icon(s, net_available() ? "tray-net-on" : "tray-net-off",
                        x + TRAY_CELL, cy)) {
        int nx = x + TRAY_CELL;
        CColor col = net_available() ? grn : gry;
        CRect a = crect_make(nx, cy - 3, 3, 3);
        CRect b = crect_make(nx + 5, cy + 1, 3, 3);
        gfx_fill_rect(s, &a, col);
        gfx_fill_rect(s, &b, col);
        gfx_line(s, nx + 2, cy - 1, nx + 5, cy + 2, col);
    }
    /* Clipboard: an outlined page, filled with accent when it holds text. */
    if (!tray_pack_icon(s, clip_has_text() ? "tray-clip-on" : "tray-clip-off",
                        x + 2 * TRAY_CELL, cy)) {
        int cx = x + 2 * TRAY_CELL;
        cbool has = clip_has_text();
        CColor col = has ? p->accent : gry;
        CRect r = crect_make(cx, cy - 4, 7, 9);
        gfx_frame_rect(s, &r, col);
        if (has) { CRect f = crect_make(cx + 2, cy - 2, 3, 5); gfx_fill_rect(s, &f, col); }
    }
}

/* ---- Quick Launch ----------------------------------------------------- */
static CRect quick_cell_rect(int i)
{
    return crect_make(g_quick_rect.x0 + 2 + i * QUICK_CELL, g_quick_rect.y0,
                      QUICK_CELL - 2, crect_h(&g_quick_rect));
}

/* Original mini-icons, drawn with primitives (like the tray glyphs). An icon
 * pack (ql-showdesktop / ql-fileman / ql-notepad / ql-mines) overrides them;
 * a missing entry falls through to the procedural glyph. */
static void quick_icon(GfxSurface *s, int which, int cx, int cy)
{
    static const char *const ql_name[QUICK_N] = {
        "ql-showdesktop", "ql-fileman", "ql-notepad", "ql-mines"
    };
    const UiPalette *p = ui_palette();
    if (which >= 0 && which < QUICK_N) {
        const GfxSurface *pic = sh_iconpack_icon(ql_name[which]);
        if (pic != NULL) {
            CRect src = crect_make(0, 0, pic->w, pic->h);
            gfx_blit(s, cx - pic->w / 2, cy - pic->h / 2, pic, &src, GFX_BLIT_KEYED);
            return;
        }
    }
    switch (which) {
    case 0: {
        /*
         * Show Desktop: a monitor with a lit screen.
         *
         * Drawn with the same visual weight as a pack icon -- a bezel, a
         * graded screen and a highlight -- because these glyphs sit BESIDE
         * pack art in the shipped default: the Tango set has ql-fileman and
         * ql-notepad but no ql-showdesktop or ql-mines, so two flat
         * monochrome shapes were sitting between two shaded colour icons and
         * read as icons that had failed to load. Matching the weight is the
         * fix that works for every pack, including none at all.
         */
        CColor bez_hi = GFX_RGB(0xE8, 0xE8, 0xE4);
        CColor bez    = GFX_RGB(0xB4, 0xB4, 0xB0);
        CColor bez_lo = GFX_RGB(0x58, 0x5C, 0x64);
        CRect body = crect_make(cx - 7, cy - 6, 14, 11);
        CRect glass = crect_make(body.x0 + 2, body.y0 + 2, 10, 7);
        gfx_fill_rect(s, &body, bez);
        gfx_hline(s, body.x0, body.y0, 14, bez_hi);
        gfx_vline(s, body.x0, body.y0, 11, bez_hi);
        gfx_hline(s, body.x0, body.y1 - 1, 14, bez_lo);
        gfx_vline(s, body.x1 - 1, body.y0, 11, bez_lo);
        gfx_vgradient(s, &glass, gfx_tint(p->accent, 0xFFFFFF, 90), p->accent);
        gfx_frame_rect(s, &glass, bez_lo);
        /* a stand and a foot, so it reads as a monitor and not a window */
        gfx_hline(s, cx - 2, cy + 5, 5, bez_lo);
        gfx_hline(s, cx - 4, cy + 6, 9, bez);
        gfx_hline(s, cx - 4, cy + 7, 9, bez_lo);
        break;
    }
    case 1: {
        /* File Manager: a folder, at the same size and shading as the other
         * three. All four are redrawn together on purpose -- raising two of
         * them and leaving these flat would only move the mismatch from the
         * pack configuration to the bare one. */
        CColor gold_hi = GFX_RGB(0xF0, 0xCE, 0x74);
        CColor gold    = GFX_RGB(0xD2, 0xA4, 0x40);
        CColor dk      = GFX_RGB(0x7E, 0x5F, 0x1C);
        CRect tab  = crect_make(cx - 7, cy - 5, 6, 2);
        CRect body = crect_make(cx - 7, cy - 3, 14, 9);
        gfx_fill_rect(s, &tab, gold);
        gfx_hline(s, tab.x0, tab.y0, 6, gold_hi);
        gfx_vgradient(s, &body, gold_hi, gold);
        gfx_frame_rect(s, &body, dk);
        gfx_hline(s, tab.x0, tab.y0, 6, dk);
        gfx_vline(s, tab.x0, tab.y0, 2, dk);
        break;
    }
    case 2: {
        /* Notepad: a page with a folded corner and ruled lines. */
        CColor paper = GFX_RGB(0xF8, 0xF8, 0xF4);
        CColor edge  = GFX_RGB(0x50, 0x54, 0x5C);
        CColor rule  = GFX_RGB(0x86, 0x8A, 0x92);
        CRect page = crect_make(cx - 5, cy - 6, 11, 13);
        gfx_fill_rect(s, &page, paper);
        gfx_frame_rect(s, &page, edge);
        /* the fold: a notch out of the top-right, with its own shadow */
        gfx_hline(s, page.x1 - 4, page.y0, 4, GFX_RGB(0xD0, 0xD2, 0xD6));
        gfx_hline(s, page.x1 - 3, page.y0 + 1, 3, GFX_RGB(0xD0, 0xD2, 0xD6));
        gfx_put_pixel(s, page.x1 - 4, page.y0 + 1, edge);
        gfx_hline(s, cx - 3, cy - 3, 6, rule);
        gfx_hline(s, cx - 3, cy - 1, 6, rule);
        gfx_hline(s, cx - 3, cy + 1, 6, rule);
        gfx_hline(s, cx - 3, cy + 3, 4, rule);
        break;
    }
    default: {
        /* Mines: a sea mine with a lit shoulder and a red cap, so it reads as
         * a round object rather than a black cross. Same reasoning as the
         * monitor above -- it sits next to shaded pack art. */
        CColor k    = GFX_RGB(0x1A, 0x1A, 0x20);
        CColor k_hi = GFX_RGB(0x60, 0x64, 0x70);
        CColor spike = GFX_RGB(0x38, 0x3C, 0x44);
        /* spikes first, so the body draws over their roots */
        gfx_hline(s, cx - 6, cy, 13, spike);
        gfx_vline(s, cx, cy - 6, 13, spike);
        gfx_put_pixel(s, cx - 4, cy - 4, spike);
        gfx_put_pixel(s, cx + 4, cy - 4, spike);
        gfx_put_pixel(s, cx - 4, cy + 4, spike);
        gfx_put_pixel(s, cx + 4, cy + 4, spike);
        gfx_fill_circle(s, cx, cy, 4, k);
        /* a highlight up and left, and the detonator cap on top */
        gfx_fill_circle(s, cx - 1, cy - 1, 1, k_hi);
        gfx_put_pixel(s, cx, cy - 5, GFX_RGB(0xC8, 0x30, 0x28));
        break;
    }
    }
}

static void draw_quicklaunch(GfxSurface *s)
{
    const UiPalette *p = ui_palette();
    int i;
    for (i = 0; i < QUICK_N; i++) {
        CRect c = quick_cell_rect(i);
        /* Flat until hovered, then a raised thin bevel -- the classic
         * cool-bar toolbar feel. */
        if (i == g_hover_quick) {
            gfx_bevel(s, &c, GFX_BEVEL_RAISED_THIN, p->light, p->dark,
                      gfx_tint(p->face, 0xFFFFFF, 60));
        }
        quick_icon(s, i, (c.x0 + c.x1) / 2, (c.y0 + c.y1) / 2);
    }
    /* An etched divider between Quick Launch and the task strip. */
    {
        int dx = g_quick_rect.x1 + 4;
        gfx_vline(s, dx,     g_quick_rect.y0 + 1, crect_h(&g_quick_rect) - 2, p->dark);
        gfx_vline(s, dx + 1, g_quick_rect.y0 + 1, crect_h(&g_quick_rect) - 2, p->light);
    }
}

/* Shared task-strip geometry: returns the window count and, when the strip is
 * wide enough to draw, the left edge x0 and per-button width bw (bw==0 means
 * the strip is too narrow to show buttons). */
static int task_strip(WmWindow **wins, int max, int *out_x0, int *out_bw)
{
    int n = taskbar_windows(wins, max);
    int x0 = g_quick_rect.x1 + 8;   /* after the Quick Launch strip */
    int avail = g_task_x1 - x0;
    int bw = 0;
    if (n > 0 && avail >= 40) {
        bw = avail / n;
        if (bw > TASK_BTN_MAX_W) { bw = TASK_BTN_MAX_W; }
    }
    *out_x0 = x0;
    *out_bw = bw;
    return n;
}

static CRect task_btn_rect(int i, int x0, int bw)
{
    return crect_make(x0 + i * bw + 1, g_sh.taskbar_rect.y0 + 4,
                      bw - 3, g_sh.taskbar_h - 8);
}

/* The taskbar button rectangle for window 'w', if it currently has one (used as
 * the minimize/restore animation anchor). Returns CFALSE if 'w' is not on the
 * strip (no room, or on another desktop). */
cbool sh_taskbar_button_rect(WmWindow *w, CRect *out)
{
    WmWindow *wins[CASTALIA_MAX_WINDOWS];
    int x0, bw, i;
    int n = task_strip(wins, CASTALIA_MAX_WINDOWS, &x0, &bw);
    if (bw <= 0 || w == NULL || out == NULL) { return CFALSE; }
    for (i = 0; i < n; i++) {
        if (wins[i] == w) { *out = task_btn_rect(i, x0, bw); return CTRUE; }
    }
    return CFALSE;
}

static void draw_task_buttons(GfxSurface *s)
{
    const UiPalette *p = ui_palette();
    WmWindow *wins[CASTALIA_MAX_WINDOWS];
    int x0, bw, i;
    int n = task_strip(wins, CASTALIA_MAX_WINDOWS, &x0, &bw);
    if (bw <= 0) { return; }

    for (i = 0; i < n; i++) {
        WmWindow *w = wins[i];
        CRect br = task_btn_rect(i, x0, bw);
        cbool active = (w == wm_focused() && wm_is_visible(w)) ? CTRUE : CFALSE;
        cbool hot = (i == g_hover_task && !active) ? CTRUE : CFALSE;
        GfxBevel bevel = active ? GFX_BEVEL_SUNKEN : GFX_BEVEL_RAISED;
        CColor face = active ? p->light : (hot ? gfx_tint(p->face, 0xFFFFFF, 70)
                                               : p->face);
        CRect tr;
        gfx_bevel(s, &br, bevel, p->light, p->dark, face);
        if (hot) { /* a bright top sheen line on the hovered button */
            gfx_hline(s, br.x0 + 2, br.y0 + 1, crect_w(&br) - 4,
                      gfx_tint(p->face, 0xFFFFFF, 150));
        }
        tr = br; tr.x0 += 5; tr.x1 -= 3;
        if (active) { tr.x0 += 1; tr.y0 += 1; }
        {
            const GfxSurface *ic = wm_icon(w);
            if (ic != NULL && crect_w(&tr) > 20) {
                int isz = 16;
                int iw = (ic->w < isz) ? ic->w : isz;
                int ih = (ic->h < isz) ? ic->h : isz;
                CRect src = crect_make(0, 0, iw, ih);
                gfx_blit(s, tr.x0, tr.y0 + (crect_h(&tr) - ih) / 2, ic, &src,
                         GFX_BLIT_KEYED);
                tr.x0 += iw + 3;
            }
        }
        {
            /*
             * Fitted to the button. Button width is `avail / n` with a maximum
             * but no MINIMUM, so the buttons keep shrinking as windows are
             * opened -- around twenty of them they are near 25 px, and an
             * unfitted title simply runs across its neighbours. The taskbar's
             * own clip keeps it on the taskbar; it does nothing to keep a
             * label inside its own button.
             */
            char tt[64];
            ui_text_fit(tt, sizeof tt, wm_title(w), crect_w(&tr) - 4,
                        GFX_FONT_SYSTEM);
            gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &tr, tt, p->text,
                               GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);
        }
    }
}

/* Compose the tooltip line for an encoded hover target (see g_tip_target).
 * Empty out = no tooltip for that target. */
static void tip_text_for(int target, char *out, cu32 outsz)
{
    static const char *WD[7] = {
        "Sunday", "Monday", "Tuesday", "Wednesday",
        "Thursday", "Friday", "Saturday"
    };
    static const char *MO[12] = {
        "January", "February", "March", "April", "May", "June", "July",
        "August", "September", "October", "November", "December"
    };
    out[0] = '\0';
    if (target == 1) {
        sys_strlcpy(out, "Open the Castalia menu", outsz);
    } else if (target == 2) {
        int yy = 0, mm = 1, dd = 1, wd = 0;
        plat_wall_date(&yy, &mm, &dd, &wd);
        if (wd < 0 || wd > 6) { wd = 0; }
        if (mm < 1 || mm > 12) { mm = 1; }
        sys_snprintf(out, outsz, "%s, %s %d, %d", WD[wd], MO[mm - 1], dd, yy);
    } else if (target >= 100 && target < 200) {
        WmWindow *wins[CASTALIA_MAX_WINDOWS];
        int x0, bw;
        int n = task_strip(wins, CASTALIA_MAX_WINDOWS, &x0, &bw);
        int i = target - 100;
        if (i >= 0 && i < n) { sys_strlcpy(out, wm_title(wins[i]), outsz); }
    } else if (target >= 200 && target < 300) {
        int j = target - 200;
        if (j == 0) {
            sys_strlcpy(out, snd_enabled() ? "Sound: on (click to mute)"
                                           : "Sound: muted (click to enable)",
                        outsz);
        } else if (j == 1) {
            sys_strlcpy(out, net_available() ? "Network: connected"
                                             : "Network: not detected", outsz);
        } else {
            sys_strlcpy(out, clip_has_text() ? "Clipboard: text ready"
                                             : "Clipboard: empty", outsz);
        }
    } else if (target >= 300 && target < 300 + QUICK_N) {
        sys_strlcpy(out, QL_TIP[target - 300], outsz);
    }
}

/* Recompute which taskbar element the pointer is over and, if it changed, mark
 * the taskbar dirty so the hover highlight updates. Called each frame, so it
 * also runs the tooltip clock: rest on one element for SH_TIP_DELAY_MS and its
 * tooltip pops just above the bar; move off and it hides at once. */
void sh_taskbar_update_hover(int mx, int my)
{
    int nh = -1;
    cbool st = CFALSE;
    int nq = -1;
    int target = 0;
    if (crect_contains(&g_sh.taskbar_rect, mx, my)) {
        if (crect_contains(&g_sh.launcher_button, mx, my)) {
            st = CTRUE;
            target = 1;
        } else if (crect_contains(&g_quick_rect, mx, my)) {
            int i;
            for (i = 0; i < QUICK_N; i++) {
                CRect c = quick_cell_rect(i);
                if (crect_contains(&c, mx, my)) { nq = i; target = 300 + i; break; }
            }
        } else if (crect_contains(&g_clock_well, mx, my)) {
            target = 2;
        } else if (crect_contains(&g_tray_rect, mx, my)) {
            int j = (mx - g_tray_rect.x0) / TRAY_CELL;
            if (j < 0) { j = 0; }
            if (j >= TRAY_N) { j = TRAY_N - 1; }
            target = 200 + j;
        } else {
            WmWindow *wins[CASTALIA_MAX_WINDOWS];
            int x0, bw, i;
            int n = task_strip(wins, CASTALIA_MAX_WINDOWS, &x0, &bw);
            if (bw > 0) {
                for (i = 0; i < n; i++) {
                    CRect br = task_btn_rect(i, x0, bw);
                    if (crect_contains(&br, mx, my)) { nh = i; target = 100 + i; break; }
                }
            }
        }
    }
    if (nh != g_hover_task || st != g_hover_start || nq != g_hover_quick) {
        g_hover_task = nh;
        g_hover_start = st;
        g_hover_quick = nq;
        sh_taskbar_dirty();
    }

    /* Tooltip clock. Retargeting restarts the delay; popups/modals veto. */
    if (target != g_tip_target) {
        g_tip_target = target;
        g_tip_since = plat_ticks_ms();
        if (g_tip_shown) { sh_tooltip_hide(); g_tip_shown = CFALSE; }
    } else if (target != 0 && !g_tip_shown &&
               !g_sh.launcher_open && !sh_context_is_open() &&
               !wm_has_modal() &&
               plat_ticks_ms() - g_tip_since >= SH_TIP_DELAY_MS) {
        char txt[96];
        tip_text_for(target, txt, sizeof(txt));
        if (txt[0] != '\0') {
            sh_tooltip_show(txt, mx, g_sh.taskbar_rect.y0 - 2);
        }
        g_tip_shown = CTRUE;   /* raised (or vetoed): don't retry each frame */
    }
}

static void draw_clock(GfxSurface *s)
{
    const UiPalette *p = ui_palette();
    char t[12];
    gfx_bevel(s, &g_clock_well, GFX_BEVEL_SUNKEN, p->light, p->dark, p->face);
    sys_format_clock_ex(t, sizeof(t), settings_get()->clock_seconds);
    gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &g_clock_well, t, p->text,
                       GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
}

void sh_taskbar_paint(const CRect *clip)
{
    GfxSurface *s = g_sh.back;
    const UiPalette *p = ui_palette();
    CRect tb = g_sh.taskbar_rect;
    CRect ub = tb;   /* paint bounds: the bar plus the orb overhang */
    CRect area;
    if (g_sh.launcher_button.y0 < ub.y0) { ub.y0 = g_sh.launcher_button.y0; }
    if (clip != NULL && !crect_overlaps(&ub, clip)) { return; }
    area = (clip != NULL) ? crect_intersect(&ub, clip) : ub;
    if (crect_empty(&area)) { return; }

    gfx_set_clip(s, &area);
    if (taskbar_glossy()) {
        /* A glossy XP bar: bright sheen near the top fading to a darker base,
         * with a bright specular line along the very top edge. */
        CColor base = g_sh.theme.taskbar_face;
        gfx_vgradient3(s, &tb, gfx_tint(base, 0xFFFFFF, 70), base,
                       gfx_tint(base, 0x000000, 55), 200);
        gfx_hline(s, tb.x0, tb.y0, crect_w(&tb), gfx_tint(base, 0xFFFFFF, 160));
    } else {
        gfx_fill_rect(s, &tb, g_sh.theme.taskbar_face);
        /* top highlight edge */
        gfx_hline(s, tb.x0, tb.y0, crect_w(&tb), p->light);
        gfx_hline(s, tb.x0, tb.y0 + 1, crect_w(&tb), g_sh.theme.taskbar_face);
    }

    draw_quicklaunch(s);
    draw_task_buttons(s);
    draw_pager(s);
    draw_tray(s);
    draw_clock(s);
    /* The orb last: it overlaps the bar's top edge and everything behind the
     * overhang (bar layers repaint first, then the sphere lands on top). */
    draw_launcher_button(s);
    gfx_reset_clip(s);
}

/*
 * The orb again, over whatever has since been painted on top of it.
 *
 * The launcher panel's bottom edge sits on the taskbar, and the panel is
 * composited after the bar -- so an open Start menu covered the orb's
 * overhang and sliced the sphere flat with a grey band across its top. The
 * button you are pressing is the one thing that should not disappear while
 * you press it, so it goes back on last, the way the orb sits over the menu's
 * corner on the systems this is quoting.
 */
void sh_taskbar_paint_orb(void)
{
    GfxSurface *s = g_sh.back;
    CRect r = g_sh.launcher_button;
    if (s == NULL || crect_empty(&r)) { return; }
    gfx_set_clip(s, &r);
    draw_launcher_button(s);
    gfx_reset_clip(s);
}

cbool sh_taskbar_handle_click(int x, int y, int buttons)
{
    if (!crect_contains(&g_sh.taskbar_rect, x, y)) { return CFALSE; }

    if (crect_contains(&g_sh.launcher_button, x, y)) {
        sh_open_launcher(!g_sh.launcher_open);
        return CTRUE;
    }
    /* Quick Launch: one click fires the cell's command. */
    if (crect_contains(&g_quick_rect, x, y)) {
        int i;
        for (i = 0; i < QUICK_N; i++) {
            CRect c = quick_cell_rect(i);
            if (crect_contains(&c, x, y)) {
                snd_click();
                sh_dispatch_command(QL_CMD[i]);
                return CTRUE;
            }
        }
        return CTRUE;
    }
    /* Desktop pager: click a cell to switch virtual desktops. */
    if (crect_contains(&g_pager_rect, x, y)) {
        int n = wm_desktop_count();
        int cw = crect_w(&g_pager_rect) / n;
        int idx = (x - g_pager_rect.x0) / (cw > 0 ? cw : 1);
        if (idx >= 0 && idx < n) { sh_switch_desktop(idx); }
        return CTRUE;
    }
    /* System tray: the sound cell toggles mute (persisted). */
    if (crect_contains(&g_tray_rect, x, y)) {
        if (x < g_tray_rect.x0 + TRAY_CELL) {
            cbool en = !snd_enabled();
            snd_set_enabled(en);
            settings_get()->sound_enabled = en;
            settings_save();
            sh_taskbar_dirty();
        }
        return CTRUE;
    }
    /* Clock: click opens the Clock & Calendar app (like double-clicking the
     * Windows tray clock). The hover tooltip still shows the full date. */
    if (crect_contains(&g_clock_well, x, y)) {
        snd_click();
        sh_dispatch_command(SH_CMD_CLOCK);
        return CTRUE;
    }
    /* Task buttons. */
    {
        WmWindow *wins[CASTALIA_MAX_WINDOWS];
        int x0, bw, i;
        int n = task_strip(wins, CASTALIA_MAX_WINDOWS, &x0, &bw);
        if (bw > 0) {
            for (i = 0; i < n; i++) {
                CRect br = task_btn_rect(i, x0, bw);
                if (crect_contains(&br, x, y)) {
                    WmWindow *w = wins[i];
                    if (w != NULL && (buttons & PLAT_MB_RIGHT)) {
                        /* Right-click raises the window menu at the button;
                         * ctx_raise lifts it clear of the taskbar, so it
                         * opens upward over the desktop. This is the only
                         * route to a MINIMIZED window's menu -- there is no
                         * title bar to right-click while it is down, and it
                         * does not hold the focus for Alt+Space. */
                        sh_context_open_window(w, br.x0, br.y0);
                        return CTRUE;
                    }
                    if (w != NULL) {
                        /*
                         * The button of the window you are ALREADY in puts it
                         * away again. That is what these buttons have done
                         * since the taskbar was invented, and without it the
                         * only way down is the caption button -- so a window
                         * you reached from the taskbar had to be dismissed
                         * somewhere else.
                         *
                         * Only when it is both visible and focused: clicking
                         * the button of a window that is merely BEHIND
                         * another one means "bring it here", not "hide it".
                         */
                        if (wm_is_visible(w) && wm_focused() == w &&
                            (wm_style(w) & WM_STYLE_MINIMIZE)) {
                            wm_minimize(w);
                            sh_taskbar_dirty();
                            return CTRUE;
                        }
                        if (!wm_is_visible(w)) { wm_show(w, CTRUE); }
                        wm_focus(w);
                        wm_bring_to_front(w);
                        /* The window's visibility/z-order changed but only the
                         * taskbar was dirty; invalidate the window's frame so a
                         * restored or raised window is actually redrawn. */
                        wm_invalidate(w, NULL);
                        sh_taskbar_dirty();
                    }
                    return CTRUE;
                }
            }
        }
    }
    return CTRUE; /* clicks on empty taskbar are consumed */
}
