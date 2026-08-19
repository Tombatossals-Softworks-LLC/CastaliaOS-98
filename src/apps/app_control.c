/*
 * app_control.c - CastaliaOS Control Center (the "Control Panel").
 *
 * A categorized settings window: a listbox of categories on the left and a
 * panel on the right that swaps per category. Edits are staged into a local
 * copy of the settings; [Apply] writes them to CASTALIA.INI (via the settings
 * model) and re-applies the theme/clock live; [OK] applies and closes.
 *
 * Uses the ui_controls toolkit (listbox, radio, checkbox). The window owns a
 * heap payload freed on WM_MSG_DESTROY.
 */
#include "apps.h"
#include "../sys/cal_core.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/settings.h"
#include "castalia/shell.h"
#include "castalia/sys.h"

#include <stdlib.h>   /* getenv */

/* Column-map table bound: the preview bezel is far smaller. A wider one still draws, just
 * without the table. */
#define CC_MAX_PREVIEW_W 256

#define CC_SIDEBAR_W  120
#define CC_ROW_H      18
#define CC_BTN_H      22
#define CC_BTN_W      64

enum { CC_APPEARANCE = 0, CC_DESKTOP, CC_SAVER, CC_CLOCK, CC_DATETIME,
       CC_MOUSE, CC_KEYBOARD, CC_SOUND, CC_STARTUP, CC_ABOUT, CC_CAT_COUNT };

/* Double-click speeds offered, in the order shown. The middle one is the
 * value both call sites used to hard-code. */
#define CC_DC_COUNT 4
static const struct { const char *label; int ms; } CC_DC[CC_DC_COUNT] = {
    { "Slow    (900 ms)",   900 },
    { "Relaxed (600 ms)",   600 },
    { "Normal  (400 ms)",   400 },
    { "Fast    (250 ms)",   250 }
};

/* Typematic steps the BIOS actually has -- offering anything between them
 * would be a control that rounds silently. */
#define CC_KD_COUNT 4
static const struct { const char *label; int ms; } CC_KD[CC_KD_COUNT] = {
    { "Long   (1000 ms)", 1000 }, { "Medium  (750 ms)", 750 },
    { "Short   (500 ms)",  500 }, { "Shortest (250 ms)", 250 }
};
#define CC_KR_COUNT 4
static const struct { const char *label; int cps; } CC_KR[CC_KR_COUNT] = {
    { "Slow   (3 / sec)",  3 }, { "Medium (10 / sec)", 10 },
    { "Fast   (20 / sec)", 20 }, { "Fastest (30 / sec)", 30 }
};

/*
 * Date & Time.
 *
 * Not a setting -- an action against the hardware clock, which is why this
 * panel has its own Set button rather than riding on OK/Apply. It is here
 * because of the machine this system targets: a 1999 box that has sat in a
 * cupboard comes up in 1980 with a flat coin cell, and until the clock is
 * right every file it saves is stamped wrong and the Agenda is useless.
 *
 * The fields are stepped rather than typed. Six spin rows need no text-entry
 * control, cannot be left holding half a number, and cannot express a date
 * that does not exist -- the day clamps into the month through
 * cal_clamp_day() the moment the year or month moves.
 */
#define CC_DT_FIELDS 6
enum { CC_DT_YEAR = 0, CC_DT_MONTH, CC_DT_DAY,
       CC_DT_HOUR, CC_DT_MIN, CC_DT_SEC };
static const char *CC_DT_LABEL[CC_DT_FIELDS] = {
    "Year", "Month", "Day", "Hour", "Minute", "Second"
};

static const char *CC_SAVER_NAME[SETTINGS_SAVER_COUNT] = {
    "Castalia Castle", "Starfield Warp", "Plasma", "Mystify"
};

/* Bundled wallpapers offered by the Desktop category (relative to CASTALIA_HOME).
 * Index 0 is "no wallpaper" (the theme's desktop gradient shows through). */
static const struct { const char *label; const char *file; } CC_WALL[] = {
    { "None (desktop gradient)", 0 },
    { "Castalia Hills",          "WALLS/HILLS.BMP" },
    { "Azure",                   "WALLS/AZURE.BMP" },
    { "Sunset",                  "WALLS/SUNSET.BMP" },
    { "Nebula",                  "WALLS/NEBULA.BMP" },
    { "Bubbles",                 "WALLS/BUBBLES.BMP" }
};
#define CC_WALL_COUNT ((int)(sizeof(CC_WALL) / sizeof(CC_WALL[0])))

/* Does 'path' end with 'suffix' (case-insensitive, path-separator agnostic)? */
static cbool cc_path_ends(const char *path, const char *suffix)
{
    cu32 pl = sys_strnlen(path, CASTALIA_MAX_PATH);
    cu32 sl = sys_strnlen(suffix, CASTALIA_MAX_PATH);
    if (sl == 0 || sl > pl) { return CFALSE; }
    return (sys_stricmp(path + (pl - sl), suffix) == 0) ? CTRUE : CFALSE;
}

/* Which wallpaper option the staged settings currently select. */
static int cc_wall_selected(const CastaliaSettings *st)
{
    int i;
    if (st->wallpaper[0] == '\0' || st->wallpaper_mode == WALLPAPER_NONE) {
        return 0;
    }
    for (i = 1; i < CC_WALL_COUNT; i++) {
        if (cc_path_ends(st->wallpaper, CC_WALL[i].file)) { return i; }
    }
    return 0;
}

/* Nearest-neighbor blit of 'src' scaled to fill 'dst' (clip-safe). */
static void cc_blit_scaled(GfxSurface *s, const CRect *dst, const GfxSurface *src)
{
    int dw = crect_w(dst), dh = crect_h(dst), x, y;
    if (src == NULL || dw <= 0 || dh <= 0) { return; }
    {
        static int cols[CC_MAX_PREVIEW_W];
        int ncols = (dw > CC_MAX_PREVIEW_W) ? CC_MAX_PREVIEW_W : dw;
        gfx_scale_map(cols, ncols, src->w);
        for (y = 0; y < dh; y++) {
            int sy = y * src->h / dh;
            const CColor *srow = src->pixels + (long)sy * src->pitch;
            for (x = 0; x < ncols; x++) {
                gfx_put_pixel(s, dst->x0 + x, dst->y0 + y, srow[cols[x]]);
            }
            for (x = ncols; x < dw; x++) {
                gfx_put_pixel(s, dst->x0 + x, dst->y0 + y,
                              srow[x * src->w / dw]);
            }
        }
    }
}
enum { CC_BTN_OK = 0, CC_BTN_APPLY, CC_BTN_CLOSE, CC_BTN_COUNT };
static const char *CC_BTN_LABEL[CC_BTN_COUNT] = { "OK", "Apply", "Close" };

typedef struct {
    UiList           cats;         /* category list on the left            */
    CastaliaSettings edit;         /* staged (unsaved) settings            */
    int              hot_row;      /* which panel row the mouse is pressing */
    GfxSurface      *preview;      /* cached wallpaper thumbnail (Desktop)  */
    int              preview_idx;  /* which wallpaper option is loaded (-1) */
    GfxSurface      *saver_surf;   /* small offscreen for the saver preview */
    int              saver_frame;  /* advances the live saver preview       */
    int              dt[CC_DT_FIELDS]; /* staged date/time, not yet applied */
    char             dt_status[72];
    /*
     * Keyboard focus.
     *
     * The Control Center could browse its categories from the keyboard and
     * change NOTHING: every one of its fifteen controls answered the mouse
     * alone. On a DOS box booted without a mouse driver that made the
     * settings app unreachable in the only sense that matters -- and the
     * Mouse and Keyboard panels are IN it, so the one place you would go to
     * sort the problem out was behind the problem.
     */
    cbool            panel_focus;  /* Tab moved focus off the category list */
    int              panel_sel;    /* highlighted panel row while it has it */
    UiHot            hot;          /* OK / Apply / Close under the pointer  */
    UiHot            hot_dt;       /* ...and the Date & Time spinners        */
} Control;

/* ---- date & time ------------------------------------------------------ */
/* Read the machine's clock into the staged fields. */
static void cc_dt_reload(Control *cc)
{
    int y = 1980, mo = 1, d = 1, h = 0, mi = 0, sec = 0;
    plat_wall_date(&y, &mo, &d, NULL);
    plat_wall_clock(&h, &mi, &sec);
    cc->dt[CC_DT_YEAR]  = y;
    cc->dt[CC_DT_MONTH] = mo;
    cc->dt[CC_DT_DAY]   = d;
    cc->dt[CC_DT_HOUR]  = h;
    cc->dt[CC_DT_MIN]   = mi;
    cc->dt[CC_DT_SEC]   = sec;
    /* Whatever the clock said, the day has to be one that exists -- a machine
     * with a flat battery can report 31 February and this panel must not
     * offer it back as though it were a date. */
    cc->dt[CC_DT_DAY] = cal_clamp_day(y, mo, d);
    if (cc->dt[CC_DT_DAY] < 1) { cc->dt[CC_DT_DAY] = 1; }
    sys_strlcpy(cc->dt_status, "Reading the machine clock.",
                sizeof(cc->dt_status));
}

/* Step one field, wrapping at its own ends, then put the day back inside the
 * month -- which is the whole reason this is one function and not six. */
static void cc_dt_step(Control *cc, int field, int delta)
{
    static const int LO[CC_DT_FIELDS] = { CAL_YEAR_MIN, 1, 1, 0, 0, 0 };
    static const int HI[CC_DT_FIELDS] = { CAL_YEAR_MAX, 12, 31, 23, 59, 59 };
    int v, lo, hi;

    if (field < 0 || field >= CC_DT_FIELDS) { return; }
    lo = LO[field];
    hi = HI[field];
    if (field == CC_DT_DAY) {
        hi = cal_days_in_month(cc->dt[CC_DT_YEAR], cc->dt[CC_DT_MONTH]);
        if (hi < 1) { hi = 31; }
    }
    v = cc->dt[field] + delta;
    if (v < lo) { v = hi; }
    else if (v > hi) { v = lo; }
    cc->dt[field] = v;

    if (field == CC_DT_YEAR || field == CC_DT_MONTH) {
        cc->dt[CC_DT_DAY] = cal_clamp_day(cc->dt[CC_DT_YEAR],
                                          cc->dt[CC_DT_MONTH],
                                          cc->dt[CC_DT_DAY]);
    }
    sys_strlcpy(cc->dt_status, "Not set yet -- press Set the clock.",
                sizeof(cc->dt_status));
}

/* Push the staged values at the hardware. Each half is reported on its own:
 * a machine can take the time and refuse the date, and "it worked" covering
 * both would be a guess. */
static void cc_dt_apply(Control *cc)
{
    CResult rd, rt;
    if (!cal_valid(cc->dt[CC_DT_YEAR], cc->dt[CC_DT_MONTH],
                   cc->dt[CC_DT_DAY])) {
        sys_strlcpy(cc->dt_status, "That is not a real date.",
                    sizeof(cc->dt_status));
        return;
    }
    rd = plat_set_wall_date(cc->dt[CC_DT_YEAR], cc->dt[CC_DT_MONTH],
                            cc->dt[CC_DT_DAY]);
    rt = plat_set_wall_clock(cc->dt[CC_DT_HOUR], cc->dt[CC_DT_MIN],
                             cc->dt[CC_DT_SEC]);
    if (rd == CE_OK && rt == CE_OK) {
        sys_strlcpy(cc->dt_status, "Clock set.", sizeof(cc->dt_status));
    } else if (rd == CE_UNSUPPORTED || rt == CE_UNSUPPORTED) {
        /* The host says so plainly rather than reporting a success it did not
         * have; on DOS this branch does not happen. */
        sys_strlcpy(cc->dt_status, "This platform will not let a program set "
                    "its clock.", sizeof(cc->dt_status));
    } else {
        sys_snprintf(cc->dt_status, sizeof(cc->dt_status),
                     "The clock refused it (date %s, time %s).",
                     (rd == CE_OK) ? "ok" : "failed",
                     (rt == CE_OK) ? "ok" : "failed");
    }
    SYS_LOGI("app", "control: set clock %04d-%02d-%02d %02d:%02d:%02d -> %s",
             cc->dt[CC_DT_YEAR], cc->dt[CC_DT_MONTH], cc->dt[CC_DT_DAY],
             cc->dt[CC_DT_HOUR], cc->dt[CC_DT_MIN], cc->dt[CC_DT_SEC],
             cc->dt_status);
}

/* The [-] and [+] boxes for a field row, and the Set button. Shared by the
 * painter and the hit test so the two cannot drift apart -- a button drawn in
 * one place and clicked in another is the classic way a settings panel goes
 * subtly dead. */
#define CC_DT_BOX 18
static CRect cc_dt_minus(const CRect *panel, int field);
static CRect cc_dt_plus(const CRect *panel, int field);
static CRect cc_dt_setbtn(const CRect *panel);

/* Test hooks. The headless driver needs the Date and Time buttons' positions
 * so it can click the REAL ones -- computing them a second time in the scene
 * would test the scene's arithmetic, and driving cc_dt_step() directly would
 * skip the hit test, which is where a settings panel usually goes dead. */
/* The Date & Time hooks are declared in apps.h; they were repeated here. */

/* ---- layout ---------------------------------------------------------- */
typedef struct {
    CRect sidebar;   /* category listbox        */
    CRect panel;     /* right-hand content area */
    CRect btn[CC_BTN_COUNT];
} CcLayout;

static void cc_layout(WmWindow *win, CcLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c);
    int by = ch - CC_BTN_H - 6, bx, i;
    L->sidebar = crect_make(6, 6, CC_SIDEBAR_W, ch - CC_BTN_H - 18);
    L->panel   = crect_make(CC_SIDEBAR_W + 14, 6,
                            cw - CC_SIDEBAR_W - 20, ch - CC_BTN_H - 18);
    /* Buttons anchored bottom-right: OK, Apply, Close. */
    bx = cw - (CC_BTN_W + 6) * CC_BTN_COUNT - 4;
    for (i = 0; i < CC_BTN_COUNT; i++) {
        L->btn[i] = crect_make(bx + i * (CC_BTN_W + 6), by, CC_BTN_W, CC_BTN_H);
    }
}

/* A row rectangle inside the panel (row index, full width). */
static CRect cc_row(const CRect *panel, int row)
{
    return crect_make(panel->x0 + 10, panel->y0 + 12 + row * CC_ROW_H,
                      crect_w(panel) - 20, CC_ROW_H);
}

static CRect cc_dt_minus(const CRect *panel, int field)
{
    CRect r = cc_row(panel, field + 1);
    return crect_make(r.x0 + 120, r.y0 - 1, CC_DT_BOX, CC_DT_BOX);
}

static CRect cc_dt_plus(const CRect *panel, int field)
{
    CRect r = cc_row(panel, field + 1);
    return crect_make(r.x0 + 120 + CC_DT_BOX + 3, r.y0 - 1,
                      CC_DT_BOX, CC_DT_BOX);
}

static CRect cc_dt_setbtn(const CRect *panel)
{
    CRect r = cc_row(panel, CC_DT_FIELDS + 2);
    return crect_make(r.x0, r.y0, 120, CC_BTN_H);
}

/* Load the wallpaper thumbnail for option 'idx' into cc->preview (cached). */
static void cc_load_preview(Control *cc, int idx)
{
    char path[CASTALIA_MAX_PATH];
    if (cc->preview_idx == idx) { return; }
    if (cc->preview != NULL) { gfx_surface_free(cc->preview); cc->preview = NULL; }
    cc->preview_idx = idx;
    if (idx <= 0 || CC_WALL[idx].file == 0) { return; }
    sys_home_path(path, (cu32)sizeof(path), CC_WALL[idx].file);
    cc->preview = gfx_bmp_load(path);
}

/* Draw an XP "Display Properties"-style monitor bezel centered in 'area';
 * returns the inner screen rect (surface coords) for the caller to fill. */
static CRect cc_monitor_bezel(GfxSurface *s, const CRect *area)
{
    const UiPalette *p = ui_palette();
    int mw = 130, mh = 100;
    int mx = area->x0 + (crect_w(area) - mw) / 2, my = area->y0;
    CRect body   = crect_make(mx, my, mw, mh);
    CRect screen = crect_make(mx + 9, my + 8, mw - 18, mh - 28);
    CRect stand  = crect_make(mx + mw / 2 - 13, my + mh - 14, 26, 6);
    CRect base   = crect_make(mx + mw / 2 - 22, my + mh - 8, 44, 5);
    gfx_bevel(s, &body, GFX_BEVEL_RAISED, p->light, p->dark, GFX_RGB(0xCF,0xD4,0xDC));
    gfx_bevel(s, &stand, GFX_BEVEL_RAISED_THIN, p->light, p->dark, GFX_RGB(0xBF,0xC5,0xCE));
    gfx_bevel(s, &base, GFX_BEVEL_RAISED_THIN, p->light, p->dark, GFX_RGB(0xBF,0xC5,0xCE));
    gfx_bevel(s, &screen, GFX_BEVEL_SUNKEN, p->light, p->dark, GFX_NO_FILL);
    return crect_inset(&screen, 2);
}

/* The wallpaper preview: monitor + the selected wallpaper (or theme gradient). */
static void cc_draw_monitor(GfxSurface *s, const CRect *area, Control *cc, int sel)
{
    CRect inner = cc_monitor_bezel(s, area);
    cc_load_preview(cc, sel);
    if (sel > 0 && cc->preview != NULL) {
        cc_blit_scaled(s, &inner, cc->preview);
    } else {
        const ShTheme *t = sh_theme_active();
        gfx_vgradient(s, &inner, t->desktop_top, t->desktop_bottom);
    }
}

/* The screensaver preview: monitor + a live frame of the chosen saver. */
static void cc_draw_saver_monitor(GfxSurface *s, const CRect *area, Control *cc,
                                  int saver)
{
    CRect inner = cc_monitor_bezel(s, area);
    if (cc->saver_surf == NULL) { cc->saver_surf = gfx_surface_new(140, 88); }
    if (cc->saver_surf != NULL) {
        sh_saver_draw_mode(cc->saver_surf, cc->saver_frame, saver);
        cc_blit_scaled(s, &inner, cc->saver_surf);
    } else {
        gfx_fill_rect(s, &inner, GFX_RGB(0x08, 0x08, 0x10));
    }
}

/* ---- paint ----------------------------------------------------------- */
/*
 * A panel's explanatory caption: fitted to the panel's width, and placed on
 * the row grid one line below the last option row.
 *
 * Both halves were wrong, in five places, and both only became visible when
 * all ten categories were rendered side by side and looked at:
 *
 *   - The Screen Saver caption is 45 characters, which is 270 pixels in a
 *     276-pixel panel starting 12 pixels in. It ran straight through the
 *     panel's etched border and out over the window frame. gfx_draw_text
 *     neither fits nor clips, which is the same hole that once let every
 *     button in the system paint outside itself.
 *   - Four of the five sat flush against the bottom edge of the last option
 *     row with no gap at all, while the Mouse panel's had a full row of air.
 *     Nothing overlapped, but the inconsistency is visible the moment two
 *     panels are seen together.
 */
static void cc_caption(GfxSurface *s, const CRect *panel, int row,
                       const char *text)
{
    const UiPalette *p = ui_palette();
    char fit[96];
    int avail = crect_w(panel) - 24;
    /*
     * The captions are also written short enough to fit at the default size,
     * so this ellipsizes nothing in practice -- two of them did, and a
     * caption cut to "...zoom + slide effe..." has lost the sentence it
     * existed to say. The fit stays because the window is resizable and a
     * theme may carry a wider face: fitting is what keeps the text readable,
     * clipping is what keeps it inside the panel, and they answer different
     * questions.
     */
    gfx_text_fit(fit, (cu32)sizeof fit, text, avail, GFX_FONT_SYSTEM);
    gfx_draw_text(s, GFX_FONT_SYSTEM, panel->x0 + 12,
                  cc_row(panel, row).y0 + 3, fit, p->text_disabled);
}

/* The panel's rows, defined below with the click and key handlers that walk
 * them; cc_paint draws the focus ring from the same model. */
static int   cc_panel_rows(int cat);
static CRect cc_panel_row_rect(const CRect *panel, int cat, int i);

static void cc_paint(WmWindow *win, GfxSurface *s)
{
    Control *cc = (Control *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    CcLayout L;
    CRect r;
    int cat, i;

    if (cc == NULL) { return; }
    cc_layout(win, &L);

    /* Category list. */
    r = crect_offset(&L.sidebar, o.x, o.y);
    /* Live only while the list itself has the keyboard. Tab moves focus to
     * the panel and draws a ring on the row it lands on; two live
     * highlights on one window is one too many. */
    ui_list_draw(s, &r, &cc->cats,
                 (!cc->panel_focus && wm_has_focus(win)) ? CTRUE : CFALSE);

    /* Right panel well + a title. */
    r = crect_offset(&L.panel, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_ETCHED, p->light, p->dark, GFX_NO_FILL);
    cat = ui_list_sel(&cc->cats);
    {
        CRect pr = r; /* panel in surface coords */
        switch (cat) {
        case CC_APPEARANCE:
            gfx_draw_text(s, GFX_FONT_BOLD, pr.x0 + 10, pr.y0 + 6,
                          "Desktop Theme", p->text);
            for (i = 0; i < SETTINGS_THEME_COUNT; i++) {
                CRect row = cc_row(&pr, i + 1);
                ui_draw_radio(s, &row, sh_theme_preset_name(i),
                              (cc->edit.theme_preset == i), UI_BTN_NORMAL);
            }
            break;
        case CC_DESKTOP: {
            int sel = cc_wall_selected(&cc->edit);
            gfx_draw_text(s, GFX_FONT_BOLD, pr.x0 + 10, pr.y0 + 6,
                          "Desktop Wallpaper", p->text);
            for (i = 0; i < CC_WALL_COUNT; i++) {
                CRect row = cc_row(&pr, i + 1);
                ui_draw_radio(s, &row, CC_WALL[i].label, (sel == i), UI_BTN_NORMAL);
            }
            cc_caption(s, &pr, CC_WALL_COUNT + 1,
                       "Stretched to fit the screen.");
            {
                CRect marea = crect_make(pr.x0 + 10,
                                         pr.y0 + 12 + (CC_WALL_COUNT + 2) * CC_ROW_H,
                                         crect_w(&pr) - 20, 100);
                cc_draw_monitor(s, &marea, cc, sel);
            }
            break;
        }
        case CC_SAVER: {
            int sv = cc->edit.screensaver;
            gfx_draw_text(s, GFX_FONT_BOLD, pr.x0 + 10, pr.y0 + 6,
                          "Screen Saver", p->text);
            for (i = 0; i < SETTINGS_SAVER_COUNT; i++) {
                CRect row = cc_row(&pr, i + 1);
                ui_draw_radio(s, &row, CC_SAVER_NAME[i], (sv == i), UI_BTN_NORMAL);
            }
            cc_caption(s, &pr, SETTINGS_SAVER_COUNT + 1,
                       "Shown after ~45s idle; any key wakes it.");
            {
                CRect marea = crect_make(pr.x0 + 10,
                                         pr.y0 + 12 + (SETTINGS_SAVER_COUNT + 2) * CC_ROW_H,
                                         crect_w(&pr) - 20, 100);
                cc_draw_saver_monitor(s, &marea, cc, sv);
            }
            break;
        }
        case CC_CLOCK: {
            CRect row;
            gfx_draw_text(s, GFX_FONT_BOLD, pr.x0 + 10, pr.y0 + 6,
                          "Taskbar Clock", p->text);
            row = cc_row(&pr, 1);
            ui_draw_check(s, &row, "Show seconds (HH:MM:SS)",
                          cc->edit.clock_seconds, UI_BTN_NORMAL);
            break;
        }
        case CC_DATETIME: {
            int i;
            char v[16];
            CRect b;
            gfx_draw_text(s, GFX_FONT_BOLD, pr.x0 + 10, pr.y0 + 6,
                          "Date & Time", p->text);
            for (i = 0; i < CC_DT_FIELDS; i++) {
                CRect row = cc_row(&pr, i + 1);
                gfx_draw_text(s, GFX_FONT_SYSTEM, row.x0 + 2, row.y0 + 3,
                              CC_DT_LABEL[i], p->text);
                if (i == CC_DT_YEAR) {
                    sys_snprintf(v, sizeof(v), "%4d", cc->dt[i]);
                } else {
                    sys_snprintf(v, sizeof(v), "%02d", cc->dt[i]);
                }
                gfx_draw_text(s, GFX_FONT_BOLD, row.x0 + 78, row.y0 + 3, v,
                              p->text);
                b = cc_dt_minus(&pr, i);
                ui_draw_button(s, &b, "-",
                               ui_hot_state(&cc->hot_dt, i * 2,
                                            UI_BTN_NORMAL));
                b = cc_dt_plus(&pr, i);
                ui_draw_button(s, &b, "+",
                               ui_hot_state(&cc->hot_dt, i * 2 + 1,
                                            UI_BTN_NORMAL));
            }
            b = cc_dt_setbtn(&pr);
            ui_draw_button(s, &b, "Set the clock",
                           ui_hot_state(&cc->hot_dt, CC_DT_FIELDS * 2,
                                        UI_BTN_NORMAL));
            gfx_draw_text(s, GFX_FONT_SYSTEM, pr.x0 + 12,
                          pr.y0 + 12 + (CC_DT_FIELDS + 4) * CC_ROW_H,
                          cc->dt_status, p->text_disabled);
            break;
        }
        case CC_MOUSE: {
            int i;
            gfx_draw_text(s, GFX_FONT_BOLD, pr.x0 + 10, pr.y0 + 6,
                          "Mouse", p->text);
            gfx_draw_text(s, GFX_FONT_SYSTEM, pr.x0 + 12, pr.y0 + 8 + CC_ROW_H,
                          "How long a second click may take to arrive:",
                          p->text_disabled);
            for (i = 0; i < CC_DC_COUNT; i++) {
                CRect row = cc_row(&pr, i + 2);
                ui_draw_radio(s, &row, CC_DC[i].label,
                              cc->edit.dblclick_ms == CC_DC[i].ms,
                              UI_BTN_NORMAL);
            }
            cc_caption(s, &pr, CC_DC_COUNT + 3,
                       "Applies to desktop icons and title bars alike.");
            break;
        }
        case CC_KEYBOARD: {
            int i;
            gfx_draw_text(s, GFX_FONT_BOLD, pr.x0 + 10, pr.y0 + 6,
                          "Keyboard", p->text);
            /*
             * Headings anchored to the ROW GRID rather than to hand-computed
             * offsets. The second one used pr.y0 + 8 + (CC_KD_COUNT + 2) *
             * CC_ROW_H, which lands four pixels ABOVE the bottom of the last
             * radio row -- "Then how fast:" was printed through the tail of
             * "Shortest (250 ms)", with the T clipping into its radio button.
             *
             * It had presumably always done that. Nothing failed, no check
             * covers a panel's pixels, and the collision only came to light
             * when this panel was rendered and looked at for the focus ring.
             */
            gfx_draw_text(s, GFX_FONT_SYSTEM, pr.x0 + 12,
                          cc_row(&pr, 1).y0 + 3,
                          "Wait before a held key repeats:", p->text_disabled);
            for (i = 0; i < CC_KD_COUNT; i++) {
                CRect row = cc_row(&pr, i + 2);
                ui_draw_radio(s, &row, CC_KD[i].label,
                              cc->edit.key_delay_ms == CC_KD[i].ms,
                              UI_BTN_NORMAL);
            }
            gfx_draw_text(s, GFX_FONT_SYSTEM, pr.x0 + 12,
                          cc_row(&pr, CC_KD_COUNT + 2).y0 + 3,
                          "Then how fast:", p->text_disabled);
            for (i = 0; i < CC_KR_COUNT; i++) {
                CRect row = cc_row(&pr, CC_KD_COUNT + 3 + i);
                ui_draw_radio(s, &row, CC_KR[i].label,
                              cc->edit.key_cps == CC_KR[i].cps, UI_BTN_NORMAL);
            }
            break;
        }
        case CC_SOUND: {
            CRect row;
            gfx_draw_text(s, GFX_FONT_BOLD, pr.x0 + 10, pr.y0 + 6,
                          "Sound & Effects", p->text);
            row = cc_row(&pr, 1);
            ui_draw_check(s, &row, "Enable sounds (PC speaker)",
                          cc->edit.sound_enabled, UI_BTN_NORMAL);
            row = cc_row(&pr, 2);
            ui_draw_check(s, &row, "Enable window & menu animations",
                          cc->edit.animations, UI_BTN_NORMAL);
            row = cc_row(&pr, 3);
            ui_draw_check(s, &row, "Drag windows by outline only",
                          cc->edit.drag_outline, UI_BTN_NORMAL);
            cc_caption(s, &pr, 4,
                       "Chimes on open/close; zoom and slide. Dragging by");
            cc_caption(s, &pr, 5,
                       "outline moves a rubber band instead of the window:");
            cc_caption(s, &pr, 6,
                       "a tenth of the work moving, a fifteenth resizing.");
            break;
        }
        case CC_STARTUP: {
            CRect row;
            gfx_draw_text(s, GFX_FONT_BOLD, pr.x0 + 10, pr.y0 + 6,
                          "Startup", p->text);
            row = cc_row(&pr, 1);
            ui_draw_check(s, &row, "Start in Safe Mode on next boot",
                          cc->edit.safe_next_boot, UI_BTN_NORMAL);
            cc_caption(s, &pr, 2, "CBOOT reads this from CASTALIA.INI.");
            break;
        }
        case CC_ABOUT:
        default:
            gfx_draw_text(s, GFX_FONT_BOLD, pr.x0 + 10, pr.y0 + 6,
                          "About", p->text);
            gfx_draw_text(s, GFX_FONT_SYSTEM, pr.x0 + 12, pr.y0 + 12 + CC_ROW_H,
                          "CastaliaOS 98 PE Control Center", p->text);
            gfx_draw_text(s, GFX_FONT_SYSTEM, pr.x0 + 12, pr.y0 + 12 + 2 * CC_ROW_H,
                          "Settings are stored in SYS\\CASTALIA.INI", p->text);
            gfx_draw_text(s, GFX_FONT_SYSTEM, pr.x0 + 12, pr.y0 + 12 + 3 * CC_ROW_H,
                          "and can be repaired with any text editor.", p->text);
            break;
        }
    }

    /*
     * The keyboard focus ring, drawn once for every category rather than
     * nine times inside the switch above -- the row model already knows
     * where each row is.
     *
     * A dotted rectangle, which is what this era used and what survives the
     * 16-colour pipeline: a tinted fill would vanish into the panel on a
     * high-contrast theme, and safe mode is exactly when somebody is driving
     * this window without a mouse.
     */
    if (cc->panel_focus) {
        CRect pr = crect_offset(&L.panel, o.x, o.y);
        int n = cc_panel_rows(cat);
        if (n > 0 && cc->panel_sel >= 0 && cc->panel_sel < n) {
            CRect fr = cc_panel_row_rect(&pr, cat, cc->panel_sel);
            fr = crect_inset(&fr, -1);   /* just outside the row */
            gfx_focus_rect(s, &fr, p->text);
        }
    }

    /* Buttons. */
    for (i = 0; i < CC_BTN_COUNT; i++) {
        CRect br = crect_offset(&L.btn[i], o.x, o.y);
        ui_draw_button(s, &br, CC_BTN_LABEL[i],
                       ui_hot_state(&cc->hot, i, UI_BTN_NORMAL));
    }
}

/* ---- actions --------------------------------------------------------- */
static void cc_apply(Control *cc)
{
    *settings_get() = cc->edit;   /* commit staged edits */
    settings_save();
    sh_apply_settings();          /* live theme/clock refresh */
}

/* Handle a click inside the current category's panel (radios / checkboxes). */
/*
 * ---- the panel as a LIST OF ROWS ---------------------------------------
 *
 * The panel used to be a pile of per-category click tests, which is why it
 * was mouse-only: there was no notion of "row 3 of this panel" for a key to
 * move to. Naming the rows gives the keyboard something to walk, and gives
 * the mouse and the keyboard one activation each rather than two.
 *
 * Date & Time is the odd one: its rows are spinners, not choices. Rows 0..4
 * are the five fields (Left/Right step them, and a click still hits the -/+
 * boxes) and the last row is Set.
 */
static int cc_panel_rows(int cat)
{
    switch (cat) {
    case CC_APPEARANCE: return SETTINGS_THEME_COUNT;
    case CC_DESKTOP:    return CC_WALL_COUNT;
    case CC_SAVER:      return SETTINGS_SAVER_COUNT;
    case CC_CLOCK:      return 1;
    case CC_DATETIME:   return CC_DT_FIELDS + 1;   /* fields, then Set */
    case CC_MOUSE:      return CC_DC_COUNT;
    case CC_KEYBOARD:   return CC_KD_COUNT + CC_KR_COUNT;
    case CC_SOUND:      return 3;
    case CC_STARTUP:    return 1;
    default:            return 0;                  /* About: nothing to set */
    }
}

/* Where row 'i' of the current category is drawn. */
static CRect cc_panel_row_rect(const CRect *panel, int cat, int i)
{
    switch (cat) {
    case CC_DATETIME:
        if (i >= CC_DT_FIELDS) { return cc_dt_setbtn(panel); }
        return cc_row(panel, i + 1);
    case CC_MOUSE:
        return cc_row(panel, i + 2);
    case CC_KEYBOARD:
        /* Two groups with a heading between them, so the second group's rows
         * are offset by one extra line -- the same arithmetic the click test
         * used, in one place now instead of two. */
        if (i < CC_KD_COUNT) { return cc_row(panel, i + 2); }
        return cc_row(panel, CC_KD_COUNT + 3 + (i - CC_KD_COUNT));
    default:
        return cc_row(panel, i + 1);
    }
}

/*
 * Do what row 'i' does. 'step' is -1 or +1 for the Date & Time spinners and
 * ignored everywhere else -- every other row is a choice or a toggle, and a
 * choice has no direction.
 */
static void cc_panel_activate(Control *cc, int i, int step)
{
    int cat = ui_list_sel(&cc->cats);
    if (i < 0 || i >= cc_panel_rows(cat)) { return; }
    switch (cat) {
    case CC_APPEARANCE: cc->edit.theme_preset = i; break;
    case CC_DESKTOP:
        if (i == 0 || CC_WALL[i].file == 0) {
            cc->edit.wallpaper[0] = '\0';
            cc->edit.wallpaper_mode = WALLPAPER_NONE;
        } else {
            sys_home_path(cc->edit.wallpaper,
                          (cu32)sizeof(cc->edit.wallpaper), CC_WALL[i].file);
            cc->edit.wallpaper_mode = WALLPAPER_STRETCH;
        }
        break;
    case CC_SAVER:    cc->edit.screensaver = i; break;
    case CC_CLOCK:    cc->edit.clock_seconds = !cc->edit.clock_seconds; break;
    case CC_DATETIME:
        if (i >= CC_DT_FIELDS) { cc_dt_apply(cc); }
        else                   { cc_dt_step(cc, i, step); }
        break;
    case CC_MOUSE:    cc->edit.dblclick_ms = CC_DC[i].ms; break;
    case CC_KEYBOARD:
        if (i < CC_KD_COUNT) { cc->edit.key_delay_ms = CC_KD[i].ms; }
        else { cc->edit.key_cps = CC_KR[i - CC_KD_COUNT].cps; }
        break;
    case CC_SOUND:
        if (i == 0)      { cc->edit.sound_enabled = !cc->edit.sound_enabled; }
        else if (i == 1) { cc->edit.animations    = !cc->edit.animations; }
        else             { cc->edit.drag_outline  = !cc->edit.drag_outline; }
        break;
    case CC_STARTUP:
        cc->edit.safe_next_boot = !cc->edit.safe_next_boot;
        break;
    default: break;
    }
}

static cbool cc_panel_click(WmWindow *win, Control *cc, const CRect *panel,
                            int x, int y)
{
    int cat = ui_list_sel(&cc->cats);
    int n = cc_panel_rows(cat), i;

    /* Date & Time's spinners are two buttons per row, so they are hit-tested
     * before the row itself: a click on "-" is not a click on the field. */
    if (cat == CC_DATETIME) {
        for (i = 0; i < CC_DT_FIELDS; i++) {
            CRect b = cc_dt_minus(panel, i);
            if (crect_contains(&b, x, y)) {
                cc->panel_sel = i;
                cc_panel_activate(cc, i, -1);
                wm_invalidate(win, NULL);
                return CTRUE;
            }
            b = cc_dt_plus(panel, i);
            if (crect_contains(&b, x, y)) {
                cc->panel_sel = i;
                cc_panel_activate(cc, i, +1);
                wm_invalidate(win, NULL);
                return CTRUE;
            }
        }
    }
    for (i = 0; i < n; i++) {
        CRect row = cc_panel_row_rect(panel, cat, i);
        if (crect_contains(&row, x, y)) {
            /*
             * The click moves the keyboard's PLACE to this row, so a later
             * Tab resumes where the mouse left off -- but deliberately does
             * NOT take focus.
             *
             * The first version did take focus, and that quietly changed
             * what Enter means: with the panel focused, Enter activates the
             * highlighted row instead of pressing OK. The Mouse panel scene
             * caught it immediately -- it clicks a row, presses Enter to
             * apply, and found the double-click window still at its default,
             * because the setting had been chosen and never applied. Enter
             * is the dialog's default action until the user asks for it not
             * to be, and Tab is that asking.
             */
            cc->panel_sel = i;
            cc_panel_activate(cc, i, +1);
            wm_invalidate(win, NULL);
            return CTRUE;
        }
    }
    return CFALSE;
}

static cbool cc_click(WmWindow *win, Control *cc, int x, int y)
{
    CcLayout L;
    int i;
    cc_layout(win, &L);
    if (ui_list_click(&cc->cats, &L.sidebar, x, y)) {
        wm_invalidate(win, NULL);
        return CTRUE;
    }
    if (crect_contains(&L.panel, x, y)) {
        return cc_panel_click(win, cc, &L.panel, x, y);
    }
    for (i = 0; i < CC_BTN_COUNT; i++) {
        if (crect_contains(&L.btn[i], x, y)) {
            (void)ui_hot_press(&cc->hot, i);
            if (i == CC_BTN_OK)         { cc_apply(cc); wm_destroy(win); }
            else if (i == CC_BTN_APPLY) { cc_apply(cc); }
            else if (i == CC_BTN_CLOSE) { wm_destroy(win); }
            return CTRUE;
        }
    }
    return CFALSE;
}

static cbool cc_key(WmWindow *win, Control *cc, int key)
{
    CcLayout L;
    int cat, n;
    cc_layout(win, &L);
    cat = ui_list_sel(&cc->cats);
    n = cc_panel_rows(cat);

    /* Tab moves between the category list and the panel -- and refuses to
     * move onto a panel with nothing in it, because a focus ring sitting on
     * About with no row to land on is a dead end the user has to guess their
     * way out of. */
    if (key == PLAT_KEY_TAB) {
        if (!cc->panel_focus && n > 0) {
            cc->panel_focus = CTRUE;
            if (cc->panel_sel < 0 || cc->panel_sel >= n) { cc->panel_sel = 0; }
        } else {
            cc->panel_focus = CFALSE;
        }
        wm_invalidate(win, NULL);
        return CTRUE;
    }

    if (cc->panel_focus && n > 0) {
        switch (key) {
        case PLAT_KEY_UP:
            cc->panel_sel = (cc->panel_sel > 0) ? cc->panel_sel - 1 : n - 1;
            wm_invalidate(win, NULL);
            return CTRUE;
        case PLAT_KEY_DOWN:
            cc->panel_sel = (cc->panel_sel + 1 < n) ? cc->panel_sel + 1 : 0;
            wm_invalidate(win, NULL);
            return CTRUE;
        case PLAT_KEY_LEFT:
        case PLAT_KEY_RIGHT:
            /* Only Date & Time has anything a direction means; everywhere
             * else Left/Right would silently pick a neighbouring option. */
            if (cat == CC_DATETIME && cc->panel_sel < CC_DT_FIELDS) {
                cc_panel_activate(cc, cc->panel_sel,
                                  (key == PLAT_KEY_RIGHT) ? +1 : -1);
                wm_invalidate(win, NULL);
                return CTRUE;
            }
            return CFALSE;
        case PLAT_KEY_ENTER:
        case PLAT_KEY_SPACE:
            cc_panel_activate(cc, cc->panel_sel, +1);
            wm_invalidate(win, NULL);
            return CTRUE;
        case PLAT_KEY_ESC:
            cc->panel_focus = CFALSE;
            wm_invalidate(win, NULL);
            return CTRUE;
        default: break;
        }
        return CFALSE;
    }

    if (ui_list_key(&cc->cats, &L.sidebar, key)) {
        /* A different category has different rows; keeping the old index
         * would put the focus ring on row 7 of a three-row panel. */
        cc->panel_sel = 0;
        wm_invalidate(win, NULL);
        return CTRUE;
    }
    if (key == PLAT_KEY_ENTER) { cc_apply(cc); return CTRUE; }
    return CFALSE;
}

static cbool control_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    Control *cc = (Control *)wm_user(win);
    switch (msg) {
    case WM_MSG_PAINT:       cc_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: return cc_click(win, cc, (int)a, (int)b);
    case WM_MSG_MOUSEMOVE: {
        CcLayout L;
        int i, hit = -1, dt = -1;
        CPoint mo = wm_client_origin(win);
        if (cc == NULL) { return CFALSE; }
        cc_layout(win, &L);
        for (i = 0; i < CC_BTN_COUNT; i++) {
            if (crect_contains(&L.btn[i], (int)a, (int)b)) { hit = i; }
        }
        /* The two buttons that changed, not the window. A pointer crossing
         * OK / Apply / Close used to cost three full repaints of a
         * 124,000-pixel client, one per button it passed over. */
        if (ui_hot_move(&cc->hot, hit)) {
            ui_hot_repaint(win, &cc->hot, L.btn, CC_BTN_COUNT);
        }
        /* The Date & Time panel's spinners, when that is the panel showing.
         * They are the most-clicked controls in the window and the smallest,
         * which is exactly where knowing what is under the pointer helps. */
        if (ui_list_sel(&cc->cats) == CC_DATETIME) {
            CRect r2;
            for (i = 0; i < CC_DT_FIELDS && dt < 0; i++) {
                r2 = cc_dt_minus(&L.panel, i);
                if (crect_contains(&r2, (int)a, (int)b)) { dt = i * 2; }
                r2 = cc_dt_plus(&L.panel, i);
                if (crect_contains(&r2, (int)a, (int)b)) { dt = i * 2 + 1; }
            }
            r2 = cc_dt_setbtn(&L.panel);
            if (dt < 0 && crect_contains(&r2, (int)a, (int)b)) {
                dt = CC_DT_FIELDS * 2;
            }
        }
        if (ui_hot_move(&cc->hot_dt, dt)) {
            /* The spinners are two rows of tiny boxes; the panel they sit in
             * is the cheapest rectangle that certainly contains both. */
            CRect pr2 = crect_offset(&L.panel, mo.x, mo.y);
            wm_invalidate(win, &pr2);
        }
        return CFALSE;
    }
    case WM_MSG_LBUTTONUP:
    case WM_MSG_MOUSELEAVE:
        if (cc != NULL) {
            cbool redraw = ui_hot_release(&cc->hot);
            cbool dt_redraw = ui_hot_release(&cc->hot_dt);
            CcLayout L;
            if (msg == WM_MSG_MOUSELEAVE) {
                if (ui_hot_move(&cc->hot, -1))    { redraw = CTRUE; }
                if (ui_hot_move(&cc->hot_dt, -1)) { dt_redraw = CTRUE; }
            }
            cc_layout(win, &L);
            if (redraw) {
                ui_hot_repaint(win, &cc->hot, L.btn, CC_BTN_COUNT);
            }
            if (dt_redraw) {
                CPoint po = wm_client_origin(win);
                CRect pr2 = crect_offset(&L.panel, po.x, po.y);
                wm_invalidate(win, &pr2);
            }
        }
        return CTRUE;
    case WM_MSG_KEYDOWN:     return cc_key(win, cc, (int)a);
    case WM_MSG_TIMER:
        if (cc != NULL) {
            cc->saver_frame++;
            /* Only the Screen Saver tab has a live preview; repaint just then. */
            if (ui_list_sel(&cc->cats) == CC_SAVER) { wm_invalidate(win, NULL); }
        }
        return CTRUE;
    case WM_MSG_DESTROY:
        if (cc != NULL) {
            if (cc->preview != NULL) { gfx_surface_free(cc->preview); }
            if (cc->saver_surf != NULL) { gfx_surface_free(cc->saver_surf); }
            sys_free(cc, (cu32)sizeof(Control));
        }
        return CTRUE;
    default: return CFALSE;
    }
}

void app_control_open(void) { app_control_open_cat(CC_APPEARANCE); }

/* Open the Control Center with 'cat' pre-selected (e.g. CC_DESKTOP when raised
 * as the desktop's "Properties"). Out-of-range falls back to Appearance. */
cbool app_control_dt_rect(WmWindow *win, int field, int which, CRect *out)
{
    Control *cc = (win != NULL) ? (Control *)wm_user(win) : NULL;
    CcLayout L;
    if (cc == NULL || out == NULL) { return CFALSE; }
    cc_layout(win, &L);
    if (which == 2) { *out = cc_dt_setbtn(&L.panel); return CTRUE; }
    if (field < 0 || field >= CC_DT_FIELDS) { return CFALSE; }
    *out = (which == 0) ? cc_dt_minus(&L.panel, field)
                        : cc_dt_plus(&L.panel, field);
    return CTRUE;
}

/* A panel row's rectangle, in client coordinates -- the same cc_row() every
 * category lays its controls out with, so a scene clicks the row the panel
 * drew rather than one it computed for itself. */
cbool app_control_btn_rect(WmWindow *win, int i, CRect *out)
{
    CcLayout L;
    CPoint o;
    if (win == NULL || out == NULL || i < 0 || i >= CC_BTN_COUNT) {
        return CFALSE;
    }
    cc_layout(win, &L);
    o = wm_client_origin(win);
    *out = crect_offset(&L.btn[i], o.x, o.y);
    return CTRUE;
}

cbool app_control_row_rect(WmWindow *win, int row, CRect *out)
{
    CcLayout L;
    if (win == NULL || out == NULL) { return CFALSE; }
    cc_layout(win, &L);
    *out = cc_row(&L.panel, row);
    return CTRUE;
}

int app_control_dt_field(WmWindow *win, int field)
{
    Control *cc = (win != NULL) ? (Control *)wm_user(win) : NULL;
    if (cc == NULL || field < 0 || field >= CC_DT_FIELDS) { return -1; }
    return cc->dt[field];
}

const char *app_control_dt_status(WmWindow *win)
{
    Control *cc = (win != NULL) ? (Control *)wm_user(win) : NULL;
    return (cc != NULL) ? cc->dt_status : NULL;
}

void app_control_open_cat(int cat)
{
    Control *cc;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int cw = 420, ch = 322, fx, fy;

    cc = (Control *)sys_calloc(1, (cu32)sizeof(Control));
    if (cc == NULL) { SYS_LOGE("app", "control: OOM"); return; }
    cc->edit = *settings_get();
    cc->preview = NULL;
    cc->preview_idx = -1;
    cc->saver_surf = NULL;
    cc->saver_frame = 0;
    cc_dt_reload(cc);
    ui_list_clear(&cc->cats);
    ui_list_add(&cc->cats, "Appearance");
    ui_list_add(&cc->cats, "Desktop");
    ui_list_add(&cc->cats, "Screen Saver");
    ui_list_add(&cc->cats, "Clock");
    ui_list_add(&cc->cats, "Date & Time");
    ui_list_add(&cc->cats, "Mouse");
    ui_list_add(&cc->cats, "Keyboard");
    ui_list_add(&cc->cats, "Sound");
    ui_list_add(&cc->cats, "Startup");
    ui_list_add(&cc->cats, "About");
    if (cat < 0 || cat >= CC_CAT_COUNT) { cat = CC_APPEARANCE; }
    cc->cats.sel = cat;

    plat_video_info(&vi);
    fx = (vi.width - cw) / 2; fy = (vi.height - ch) / 2;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, cw, ch);

    w = wm_create("Control Center", &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE |
                  WM_STYLE_BORDER, control_proc, cc);
    if (w == NULL) { sys_free(cc, (cu32)sizeof(Control)); return; }
    wm_show(w, CTRUE);
    wm_set_animated(w, CTRUE);   /* drives the live Screen Saver preview */
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened Control Center");
}

/* The staged (not yet applied) key-repeat delay, for --mousekey-demo. A
 * keyboard-only pick has no visible side effect until Apply, so the scene
 * reads the staged value rather than guessing from the drawing. */
int app_control_key_delay(WmWindow *win)
{
    Control *cc = (win != NULL) ? (Control *)wm_user(win) : NULL;
    return (cc != NULL) ? cc->edit.key_delay_ms : -1;
}

/* The right-hand panel's rectangle in CLIENT coordinates, for the containment
 * check in --mousekey-demo. Nothing a panel draws may cross it. */
cbool app_control_panel_rect(WmWindow *win, CRect *out)
{
    CcLayout L;
    if (win == NULL || out == NULL) { return CFALSE; }
    if (wm_user(win) == NULL) { return CFALSE; }
    cc_layout(win, &L);
    *out = L.panel;
    return CTRUE;
}
