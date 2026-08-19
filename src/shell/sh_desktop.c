/*
 * sh_desktop.c - The desktop layer: stone gradient, castle motif, desktop
 *                icons with labels, and brand watermark.
 *
 * All icon artwork is drawn procedurally from primitives (original silhouettes
 * built around the Castalia castle/shield identity -- no Windows iconography).
 * The desktop is repainted only within the frame's dirty region.
 */
#include "sh_internal.h"
#include "castalia/castalia.h"
#include "castalia/cfg.h"
#include "castalia/gfx.h"
#include "castalia/settings.h"
#include "castalia/sys.h"
#include "icon_nav.h"
#include "trash_core.h"       /* TRASH.IDX is not a file somebody deleted */
#include <stdlib.h>
#include <string.h>

/* Widest screen the stretch table covers. 1600 is past anything a VESA 2.0
 * card of this era offers at a depth the presenter can drive; a wider one
 * still works, just without the table. */
#define SH_MAX_SCREEN_W 1600

enum { IK_MONITOR, IK_FOLDER, IK_GEAR, IK_DOC, IK_TRASH, IK_MEDIA, IK_CLOCK };

#define DESK_ICON_COUNT 7
#define ICON_SZ  40
#define ICON_LABEL_H 12

typedef struct {
    const char *label;
    int         cmd;
    int         kind;
    CRect       hit;   /* icon+label hit rect */
} DeskIcon;

static DeskIcon g_icons[DESK_ICON_COUNT];

/* Selection + double-click state (Win98: single-click selects, double opens).
 * The window itself is settings_get()->dblclick_ms -- it used to be a 400
 * here and a separate 400 in wm_dispatch.c, so the icons and the title bars
 * each had their own idea of what a double-click is. */
static int  g_sel_icon = -1;
static cu32 g_icon_click_ms = 0;
static int  g_icon_click_idx = -1;

/* Icon drag: armed on button-down over an icon, activated once the pointer
 * moves past a small threshold, and dropped (repositioned) on button-up. A
 * plain click never crosses the threshold, so select / double-click still
 * work unchanged. */
#define ICON_DRAG_THRESH 4
static int  g_drag_icon   = -1;    /* armed/active icon index, or -1        */
static cbool g_drag_active = CFALSE;
static int  g_drag_off_x, g_drag_off_y; /* cursor offset within the hit rect */
static int  g_down_x, g_down_y;    /* button-down position                  */
static int  g_drag_x, g_drag_y;    /* current cursor position               */
static CRect g_ghost_rect;         /* last-drawn floating ghost bounds      */
static CRect drag_ghost_rect(void); /* defined with the drag handlers below  */

/* Cached "the Recycle Bin has files" state, so the bin icon can show full vs
 * empty without stat-ing the filesystem on every repaint. Refreshed at startup
 * and whenever Trash changes (sh_recyclebin_touch). */
static cbool g_trash_full = CFALSE;

/*
 * Whether the landscape skips what it is about to paint over: each hill layer
 * stopping where the layer in front of it starts, and the haze band stopping
 * where the hills are guaranteed to cover it.
 *
 * Off means every band fills to the bottom of the screen the way they all used
 * to -- the same picture, painted the slow way. Being able to render it BOTH
 * ways is what lets --vale-demo prove they ARE the same picture, rather than a
 * colour heuristic guessing at what a hole would look like.
 */
static cbool g_paint_only_visible = CTRUE;

void sh_desktop_init(void)
{
    static const char *labels[DESK_ICON_COUNT] = {
        "This Machine", "Documents", "Control Center", "Log Viewer",
        "Media Player", "Clock", "Recycle Bin"
    };
    static const int cmds[DESK_ICON_COUNT] = {
        SH_CMD_SYSINFO, SH_CMD_FILEMANAGER, SH_CMD_CONTROLCENTER,
        SH_CMD_LOGVIEWER, SH_CMD_MEDIA, SH_CMD_CLOCK, SH_CMD_RECYCLEBIN
    };
    static const int kinds[DESK_ICON_COUNT] = {
        IK_MONITOR, IK_FOLDER, IK_GEAR, IK_DOC, IK_MEDIA, IK_CLOCK, IK_TRASH
    };
    int i;
    int x = 22, y = 20, step = 74;
    /* Point at an optional BMP icon pack; empty path keeps the procedural art. */
    sh_iconpack_set_dir(settings_get()->icons_dir);
    for (i = 0; i < DESK_ICON_COUNT; i++) {
        g_icons[i].label = labels[i];
        g_icons[i].cmd   = cmds[i];
        g_icons[i].kind  = kinds[i];
        g_icons[i].hit   = crect_make(x, y, 76, ICON_SZ + ICON_LABEL_H + 4);
        y += step;
    }
    sh_desktop_build_cache();
    sh_recyclebin_touch();   /* set the bin icon's initial empty/full state */
}

/* Does the Recycle Bin (CASTALIA_HOME/TRASH) hold at least one file?
 * TRASH.IDX is the bin's own bookkeeping and does not count -- otherwise the
 * icon would stay full forever after the last file is restored. */
static cbool trash_has_files(void)
{
    char trash[CASTALIA_MAX_PATH];
    PlatDir *d;
    PlatDirEntry e;
    cbool any = CFALSE;
    sys_home_path(trash, (cu32)sizeof(trash), "TRASH");
    d = plat_opendir(trash);
    if (d == NULL) { return CFALSE; }
    while (plat_readdir(d, &e)) {
        if (e.name[0] == '.' || e.is_dir) { continue; }
        if (trash_is_index(e.name)) { continue; }
        any = CTRUE;
        break;
    }
    plat_closedir(d);
    return any;
}

cbool sh_recyclebin_full(void)
{
    return g_trash_full;
}

void sh_recyclebin_touch(void)
{
    cbool full = trash_has_files();
    int i;
    if (full == g_trash_full) { return; }
    g_trash_full = full;
    /* Repaint just the bin icon. */
    for (i = 0; i < DESK_ICON_COUNT; i++) {
        if (g_icons[i].cmd == SH_CMD_RECYCLEBIN) { sh_mark_dirty(&g_icons[i].hit); }
    }
}

/* Persist the current icon positions to the [Desktop] section of CASTALIA.INI
 * so a dragged layout survives a restart. Preserves the rest of the file. */
void sh_desktop_save_positions(void)
{
    const char *path = settings_ini_path();
    CfgFile *cfg;
    int i;
    char key[16];
    if (path == NULL || path[0] == '\0') { return; }
    cfg = cfg_load(path, NULL);
    if (cfg == NULL) { cfg = cfg_new(); }
    if (cfg == NULL) { return; }
    for (i = 0; i < DESK_ICON_COUNT; i++) {
        sys_snprintf(key, sizeof(key), "Icon%dX", i);
        cfg_set_int(cfg, "Desktop", key, g_icons[i].hit.x0);
        sys_snprintf(key, sizeof(key), "Icon%dY", i);
        cfg_set_int(cfg, "Desktop", key, g_icons[i].hit.y0);
    }
    cfg_save(cfg, path);
    cfg_free(cfg);
}

/* Overlay any saved [Desktop] icon positions onto the default grid (call once
 * at startup, after sh_desktop_init). A missing key keeps the grid default; a
 * value is clamped into the visible desktop so a bad INI can't hide an icon. */
void sh_desktop_load_positions(void)
{
    const char *path = settings_ini_path();
    CfgFile *cfg;
    int i;
    char key[16];
    if (path == NULL || path[0] == '\0') { return; }
    cfg = cfg_load(path, NULL);
    if (cfg == NULL) { return; }
    for (i = 0; i < DESK_ICON_COUNT; i++) {
        int w = crect_w(&g_icons[i].hit), h = crect_h(&g_icons[i].hit);
        int x, y;
        sys_snprintf(key, sizeof(key), "Icon%dX", i);
        x = (int)cfg_get_int(cfg, "Desktop", key, g_icons[i].hit.x0);
        sys_snprintf(key, sizeof(key), "Icon%dY", i);
        y = (int)cfg_get_int(cfg, "Desktop", key, g_icons[i].hit.y0);
        if (g_sh.screen_w > 0) {
            if (x < 0) { x = 0; } else if (x > g_sh.screen_w - w) { x = g_sh.screen_w - w; }
            if (y < 0) { y = 0; } else if (y > g_sh.screen_h - h) { y = g_sh.screen_h - h; }
        }
        g_icons[i].hit = crect_make(x, y, w, h);
    }
    cfg_free(cfg);
}

/* ---- procedural icon art --------------------------------------------- */
static void icon_monitor(GfxSurface *s, int x, int y)
{
    const UiPalette *p = ui_palette();
    CRect body = crect_make(x + 2, y, ICON_SZ - 4, 28);
    CRect screen = crect_make(x + 5, y + 3, ICON_SZ - 10, 20);
    CRect glyph = crect_make(x + 9, y + 13, ICON_SZ - 18, 7);
    CRect stand = crect_make(x + ICON_SZ / 2 - 5, y + 28, 10, 5);
    CRect base = crect_make(x + ICON_SZ / 2 - 10, y + 33, 20, 4);
    gfx_bevel(s, &body, GFX_BEVEL_RAISED, p->light, p->dark, p->face);
    gfx_bevel(s, &screen, GFX_BEVEL_SUNKEN, p->light, p->dark, GFX_RGB(0x18,0x40,0x30));
    /* tiny castle on the screen */
    gfx_fill_rect(s, &glyph, GFX_RGB(0x9F,0xC8,0xB4));
    gfx_fill_rect(s, &stand, p->dark);
    gfx_bevel(s, &base, GFX_BEVEL_RAISED, p->light, p->dark, p->face);
}
static void icon_folder(GfxSurface *s, int x, int y)
{
    CColor manila = GFX_RGB(0xE2, 0xC1, 0x6A);
    CColor edge   = GFX_RGB(0x9A, 0x7C, 0x2E);
    CRect tab = crect_make(x + 4, y + 6, 16, 6);
    CRect body = crect_make(x + 4, y + 10, ICON_SZ - 8, 22);
    gfx_fill_rect(s, &tab, manila);
    gfx_frame_rect(s, &tab, edge);
    gfx_fill_rect(s, &body, manila);
    gfx_frame_rect(s, &body, edge);
    gfx_hline(s, body.x0 + 2, body.y0 + 4, crect_w(&body) - 4, GFX_RGB(0xF0,0xD9,0x92));
}
static void icon_gear(GfxSurface *s, int x, int y)
{
    CColor steel = GFX_RGB(0xB8, 0xBE, 0xC8);
    CColor dark  = GFX_RGB(0x70, 0x78, 0x84);
    int cx = x + ICON_SZ / 2, cy = y + 16;
    CRect body = crect_make(cx - 12, cy - 12, 24, 24);
    CRect hole = crect_make(cx - 4, cy - 4, 8, 8);
    CRect tooth_v = crect_make(cx - 3, cy - 16, 6, 32);
    CRect tooth_h = crect_make(cx - 16, cy - 3, 32, 6);
    /* teeth */
    gfx_fill_rect(s, &tooth_v, dark);
    gfx_fill_rect(s, &tooth_h, dark);
    gfx_fill_rect(s, &body, steel);
    gfx_frame_rect(s, &body, dark);
    gfx_fill_rect(s, &hole, GFX_RGB(0x3A,0x41,0x4B));
}
static void icon_doc(GfxSurface *s, int x, int y)
{
    CColor paper = GFX_RGB(0xF4, 0xF6, 0xFA);
    CColor edge  = GFX_RGB(0x7A, 0x83, 0x90);
    CRect body = crect_make(x + 8, y + 2, ICON_SZ - 18, 32);
    int i;
    gfx_fill_rect(s, &body, paper);
    gfx_frame_rect(s, &body, edge);
    /* folded corner */
    gfx_line(s, body.x1 - 8, body.y0, body.x1 - 1, body.y0 + 7, edge);
    for (i = 0; i < 5; i++) {
        gfx_hline(s, body.x0 + 3, body.y0 + 6 + i * 5, crect_w(&body) - 6,
                  GFX_RGB(0x9A, 0xA3, 0xB0));
    }
}
static void icon_trash(GfxSurface *s, int x, int y)
{
    CColor metal = GFX_RGB(0x9A, 0xA3, 0xB0);
    CColor dark  = GFX_RGB(0x5A, 0x63, 0x70);
    CRect lid = crect_make(x + 8, y + 4, ICON_SZ - 16, 4);
    CRect bin = crect_make(x + 10, y + 8, ICON_SZ - 20, 26);
    CRect knob = crect_make(x + ICON_SZ / 2 - 3, y + 1, 6, 3);
    int i;
    gfx_fill_rect(s, &lid, metal);
    gfx_frame_rect(s, &lid, dark);
    gfx_fill_rect(s, &knob, metal);
    gfx_fill_rect(s, &bin, metal);
    gfx_frame_rect(s, &bin, dark);
    for (i = 0; i < 3; i++) {
        gfx_vline(s, bin.x0 + 4 + i * 6, bin.y0 + 3, crect_h(&bin) - 6, dark);
    }
}
static void icon_media(GfxSurface *s, int x, int y)
{
    /* A green "play" disc with a music note -- the Media Player. */
    int cx = x + ICON_SZ / 2, cy = y + 18, i;
    CColor rim = GFX_RGB(0x14, 0x4A, 0x24), note = GFX_RGB(0x20, 0xE0, 0x40);
    gfx_fill_circle(s, cx, cy, 16, rim);
    gfx_fill_circle(s, cx, cy, 14, GFX_RGB(0x08, 0x14, 0x0A));
    for (i = 0; i < 8; i++) {                 /* play triangle */
        gfx_vline(s, cx - 4 + i, cy - (8 - i), 2 * (8 - i), note);
    }
    gfx_vline(s, x + ICON_SZ - 6, y + 2, 12, note);   /* note stem */
    gfx_fill_circle(s, x + ICON_SZ - 8, y + 14, 3, note);
}
static void icon_clock(GfxSurface *s, int x, int y)
{
    int cx = x + ICON_SZ / 2, cy = y + 18;
    CColor rim = GFX_RGB(0x60, 0x6A, 0x7A);
    gfx_fill_circle(s, cx, cy, 16, rim);
    gfx_fill_circle(s, cx, cy, 14, GFX_RGB(0xF4, 0xF6, 0xF0));
    gfx_put_pixel(s, cx, cy - 12, GFX_RGB(0x30,0x38,0x46));   /* 12 o'clock tick */
    gfx_put_pixel(s, cx + 12, cy, GFX_RGB(0x30,0x38,0x46));   /* 3 */
    gfx_put_pixel(s, cx, cy + 12, GFX_RGB(0x30,0x38,0x46));   /* 6 */
    gfx_put_pixel(s, cx - 12, cy, GFX_RGB(0x30,0x38,0x46));   /* 9 */
    gfx_line(s, cx, cy, cx + 6, cy - 3, GFX_RGB(0x20,0x28,0x36));  /* minute */
    gfx_line(s, cx, cy, cx, cy - 8, GFX_RGB(0x20,0x28,0x36));      /* hour */
    gfx_line(s, cx, cy, cx - 4, cy + 5, GFX_RGB(0xC0,0x20,0x20));  /* second */
    gfx_fill_circle(s, cx, cy, 2, GFX_RGB(0x30,0x38,0x46));
}

/*
 * 'live' is whether the desktop is where the keyboard is -- which it is only
 * when no window is focused, since that is the exact condition under which
 * sh_core routes the arrow keys here. A selected icon behind an open window
 * kept its accent-blue plate and its white ring, saying the arrows would move
 * it when they would go to the window instead.
 */
static void draw_icon(GfxSurface *s, const DeskIcon *ic, cbool selected,
                      cbool live)
{
    static const char *const kind_name[DESK_ICON_COUNT] = {
        "computer", "folder", "settings", "document", "trash", "media", "clock"
    };
    int ix = ic->hit.x0 + (crect_w(&ic->hit) - ICON_SZ) / 2;
    int iy = ic->hit.y0;
    /* An icon pack overrides the procedural art. The IK_* kinds are 0..4 and
     * line up with kind_name[]; a NULL icon (no pack, or a missing file) falls
     * through to the original procedural drawing below. */
    const GfxSurface *pic = (ic->kind >= 0 && ic->kind < DESK_ICON_COUNT)
                          ? sh_iconpack_icon(kind_name[ic->kind]) : NULL;
    if (pic != NULL) {
        CRect src = crect_make(0, 0, pic->w, pic->h);
        int bx = ix + (ICON_SZ - pic->w) / 2;
        int by = iy + (ICON_SZ - pic->h) / 2;
        gfx_blit(s, bx, by, pic, &src, GFX_BLIT_KEYED);
    } else {
        switch (ic->kind) {
        case IK_MONITOR: icon_monitor(s, ix, iy); break;
        case IK_FOLDER:  icon_folder(s, ix, iy);  break;
        case IK_GEAR:    icon_gear(s, ix, iy);    break;
        case IK_DOC:     icon_doc(s, ix, iy);     break;
        case IK_TRASH:   icon_trash(s, ix, iy);   break;
        case IK_MEDIA:   icon_media(s, ix, iy);   break;
        case IK_CLOCK:   icon_clock(s, ix, iy);   break;
        default: break;
        }
    }
    /* The Recycle Bin shows crumpled paper poking out when it holds files. */
    if (ic->cmd == SH_CMD_RECYCLEBIN && g_trash_full) {
        CColor paper = GFX_RGB(0xF4, 0xF1, 0xE2);
        CColor edge  = GFX_RGB(0x9A, 0x94, 0x80);
        CRect p1 = crect_make(ix + ICON_SZ / 2 - 8, iy + 1, 9, 8);
        CRect p2 = crect_make(ix + ICON_SZ / 2 + 1, iy + 3, 8, 7);
        gfx_fill_rect(s, &p1, paper); gfx_frame_rect(s, &p1, edge);
        gfx_fill_rect(s, &p2, paper); gfx_frame_rect(s, &p2, edge);
        gfx_hline(s, p1.x0 + 2, p1.y0 + 4, 5, edge);
        gfx_hline(s, p2.x0 + 2, p2.y0 + 3, 4, edge);
    }
    {
        int lw = gfx_text_width(GFX_FONT_SYSTEM, ic->label);
        int cx = ic->hit.x0 + crect_w(&ic->hit) / 2;
        CRect lr = crect_make(ic->hit.x0, iy + ICON_SZ + 2, crect_w(&ic->hit),
                              ICON_LABEL_H);
        CColor label_col = GFX_RGB(0xEC, 0xF0, 0xF6);
        if (selected) {
            /* Highlight the label with the accent color + a focus rectangle.
             * Both go quiet when the keyboard is somewhere else: a plate the
             * wallpaper shows through, and no ring at all. */
            CRect hl = crect_make(cx - lw / 2 - 2, lr.y0, lw + 4, ICON_LABEL_H);
            gfx_fill_rect(s, &hl, live ? g_sh.theme.ui.accent
                                       : gfx_tint(g_sh.theme.ui.accent,
                                                  GFX_RGB(0x60,0x66,0x70), 170));
            if (live) { gfx_frame_rect(s, &hl, GFX_RGB(0xFF, 0xFF, 0xFF)); }
            label_col = g_sh.theme.ui.accent_text;
            gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &lr, ic->label, label_col,
                               GFX_ALIGN_HCENTER | GFX_ALIGN_TOP);
        } else {
            /* XP-style label: white text with a 1px dark drop shadow so it
             * stays readable over any wallpaper or gradient. */
            gfx_draw_text_shadow(s, GFX_FONT_SYSTEM, cx - lw / 2, lr.y0 + 2,
                                 ic->label, label_col, GFX_RGB(0x10, 0x1C, 0x2C));
        }
    }
}

/* A quiet crenellated castle silhouette across the lower desktop. */
static void draw_castle(GfxSurface *s)
{
    int base_y = g_sh.screen_h - g_sh.taskbar_h - 60;
    int cx = g_sh.screen_w / 2;
    CColor stone = GFX_RGB(0x2A, 0x38, 0x48);
    CColor gate_c = GFX_RGB(0x18, 0x22, 0x2E);
    int i;
    CRect keep = crect_make(cx - 70, base_y, 140, 60);
    CRect merlon, tower_l, tower_r, gate;
    gfx_fill_rect(s, &keep, stone);
    /* battlements */
    for (i = 0; i < 7; i++) {
        merlon = crect_make(keep.x0 + i * 20, base_y - 8, 12, 8);
        gfx_fill_rect(s, &merlon, stone);
    }
    /* two towers */
    tower_l = crect_make(cx - 92, base_y - 14, 20, 74);
    tower_r = crect_make(cx + 72, base_y - 14, 20, 74);
    gfx_fill_rect(s, &tower_l, stone);
    gfx_fill_rect(s, &tower_r, stone);
    /* gate */
    gate = crect_make(cx - 10, base_y + 30, 20, 30);
    gfx_fill_rect(s, &gate, gate_c);
}

/* ---- wallpaper ------------------------------------------------------- */
static GfxSurface *g_wallpaper = NULL;
static char        g_wall_path[CASTALIA_MAX_PATH] = "";
static int         g_wall_mode = WALLPAPER_NONE;

/* (Re)load the wallpaper BMP if the configured path changed. */
static void wallpaper_sync(void)
{
    const CastaliaSettings *st = settings_get();
    g_wall_mode = st->wallpaper_mode;
    if (sys_stricmp(st->wallpaper, g_wall_path) == 0) { return; }  /* unchanged */
    if (g_wallpaper != NULL) { gfx_surface_free(g_wallpaper); g_wallpaper = NULL; }
    sys_strlcpy(g_wall_path, st->wallpaper, sizeof(g_wall_path));
    if (g_wall_path[0] != '\0') {
        g_wallpaper = gfx_bmp_load(g_wall_path);
        if (g_wallpaper == NULL) {
            SYS_LOGW("sh", "wallpaper load failed: %s", g_wall_path);
        } else {
            SYS_LOGI("sh", "wallpaper %dx%d loaded from %s",
                     g_wallpaper->w, g_wallpaper->h, g_wall_path);
        }
    }
}

static cbool wallpaper_active(void)
{
    return (g_wallpaper != NULL && g_wall_mode != WALLPAPER_NONE) ? CTRUE : CFALSE;
}

static void draw_wallpaper(GfxSurface *s)
{
    GfxSurface *wp = g_wallpaper;
    if (!wallpaper_active()) { return; }
    if (g_wall_mode == WALLPAPER_CENTER) {
        CRect src = crect_make(0, 0, wp->w, wp->h);
        gfx_blit(s, (s->w - wp->w) / 2, (s->h - wp->h) / 2, wp, &src, GFX_BLIT_COPY);
    } else if (g_wall_mode == WALLPAPER_TILE) {
        int x, y;
        for (y = 0; y < s->h; y += wp->h) {
            for (x = 0; x < s->w; x += wp->w) {
                CRect src = crect_make(0, 0, wp->w, wp->h);
                gfx_blit(s, x, y, wp, &src, GFX_BLIT_COPY);
            }
        }
    } else { /* WALLPAPER_STRETCH: nearest-neighbor to fill the screen */
        /*
         * The source column for a given output column is the same on every
         * row, so it is computed once per column rather than once per pixel:
         * 800 divides for an 800x600 screen instead of 480,000. Same integer
         * source coordinates, so the picture is identical.
         *
         * The table is bounded by the screen width and lives on the static
         * side, because this runs while the background is being baked and the
         * DOS stack is not the place for a few kilobytes.
         */
        static int cols[SH_MAX_SCREEN_W];
        int dx, dy, ncols = s->w;
        if (ncols > SH_MAX_SCREEN_W) { ncols = SH_MAX_SCREEN_W; }
        gfx_scale_map(cols, ncols, wp->w);
        for (dy = 0; dy < s->h; dy++) {
            int sy = dy * wp->h / s->h;
            const CColor *srow = wp->pixels + (long)sy * wp->pitch;
            CColor *drow = s->pixels + (long)dy * s->pitch;
            for (dx = 0; dx < ncols; dx++) { drow[dx] = srow[cols[dx]]; }
            /* A screen wider than the table falls back to the divide rather
             * than dropping columns; nothing this system sets is that wide. */
            for (dx = ncols; dx < s->w; dx++) {
                drow[dx] = srow[dx * wp->w / s->w];
            }
        }
    }
}

/* Darken toward the edges for depth (a soft radial vignette). Applied once when
 * baking the cache -- never per frame -- so the per-pixel cost is paid once.
 *
 * Once, but on every pixel of the screen at every boot and every theme change,
 * and the obvious form of it costs a LONG DIVISION per pixel: nearly half a
 * million divides at around forty cycles each is most of a second on the 386
 * this targets. Two observations remove it exactly, with no approximation and
 * no change to a single output pixel.
 *
 * The shade factor only takes a hundred and one distinct values, and it is
 * monotone in the squared distance -- so instead of dividing, precompute the
 * hundred and one distances at which it steps and walk that boundary forward
 * as the distance grows. And a row is symmetric about the centre column, so
 * one distance serves the pixel on each side of it.
 */
static void apply_vignette(GfxSurface *s)
{
    /* thresh[j] = the smallest squared distance whose shade step is j. */
    static long thresh[102];
    int w = s->w, h = s->h, x, y, i, j, half;
    int cx = w / 2, cy = h / 2;
    long maxd = (long)cx * cx + (long)cy * cy;
    if (maxd <= 0) { return; }
    for (j = 0; j <= 101; j++) { thresh[j] = ((long)j * maxd + 99) / 100; }

    half = (cx > w - 1 - cx) ? cx : w - 1 - cx;
    for (y = 0; y < h; y++) {
        CColor *row = s->pixels + (long)y * s->pitch;
        long dy = y - cy;
        long dy2 = dy * dy;
        long i2 = 0;              /* i*i, stepped rather than multiplied */
        int  k = 0;
        for (i = 0; i <= half; i++) {
            long d = i2 + dy2;
            int f;
            while (k < 100 && d >= thresh[k + 1]) { k++; }
            f = 256 - k;
            x = cx + i;
            if (x < w) { CColor c = row[x]; row[x] = GFX_SCALE_RGB(c, f); }
            x = cx - i;
            if (i > 0 && x >= 0) {
                CColor c = row[x]; row[x] = GFX_SCALE_RGB(c, f);
            }
            i2 += 2 * (long)i + 1;
        }
    }
}

/* ---- the "Vale de Castalia" factory landscape ------------------------- */
/*
 * The default desktop in glossy mode: an original, procedurally painted
 * landscape (sky, sun, clouds, rolling hills, a sunlit castle) in the spirit of
 * XP's pastoral wallpaper -- but ours, drawn from primitives, no assets. It is
 * rendered ONCE into the background cache, so its per-pixel cost is paid a
 * single time; steady-state repaints stay a plain blit.
 */

/* Integer sine: amplitude 256, angle in 0..1023 for a full circle. */
static int land_sin(int a)
{
    static const int Q[17] = { 0, 25, 50, 74, 98, 120, 142, 162, 180, 197,
                               212, 225, 236, 244, 250, 254, 256 };
    int t, i, f, v;
    a &= 1023;
    t = a & 255;
    if (a & 256) { t = 255 - t; }
    i = t >> 4;
    f = t & 15;
    v = Q[i] + ((Q[i + 1] - Q[i]) * f) / 16;
    return (a & 512) ? -v : v;
}

/* Tint the pixels inside an ellipse toward 'toward', strongest at the center
 * (quadratic falloff). The soft brush behind clouds and the sun glow.
 *
 * The falloff term separates: e = (dx^2 * 256)/rx^2 + (dy^2 * 256)/ry^2, and
 * each half depends on one axis only. Computed inline that is two long
 * multiplies and TWO DIVIDES per pixel, and a 386 pays about forty cycles for
 * a divide. Precomputing one term per column and one per row leaves an add.
 * This runs across most of an 800x600 wallpaper at every boot and every theme
 * change, so it is worth the table. */
static void land_glow(GfxSurface *s, int cx, int cy, int rx, int ry,
                      CColor toward, int amt)
{
    static int ex[2048];
    int x, y, x0, x1, y0, y1;
    if (rx <= 0 || ry <= 0) { return; }
    x0 = cx - rx; x1 = cx + rx;
    y0 = cy - ry; y1 = cy + ry;
    if (x0 < 0) { x0 = 0; }
    if (y0 < 0) { y0 = 0; }
    if (x1 > s->w - 1) { x1 = s->w - 1; }
    if (y1 > s->h - 1) { y1 = s->h - 1; }
    if (x1 - x0 >= (int)(sizeof(ex) / sizeof(ex[0]))) { return; }
    for (x = x0; x <= x1; x++) {
        long dx = x - cx;
        ex[x - x0] = (int)((dx * dx * 256) / ((long)rx * rx));
    }
    for (y = y0; y <= y1; y++) {
        long dy = y - cy;
        int ey = (int)((dy * dy * 256) / ((long)ry * ry));
        CColor *row = s->pixels + (long)y * s->pitch;
        if (ey >= 256) { continue; }
        for (x = x0; x <= x1; x++) {
            int e = ex[x - x0] + ey;
            if (e < 256) {
                row[x] = gfx_tint(row[x], toward, ((256 - e) * amt) / 256);
            }
        }
    }
}

/* One layer of rolling hills: fill below a sine-shaped crest, shading darker
 * toward the bottom, with a brighter line along the crest.
 *
 * The shade at a pixel depends only on how far down the slope it is, and that
 * value runs 0..200 -- two hundred and one distinct colours for a quarter of a
 * million pixels. Mixing each one fresh (a tint is three multiplies, three
 * shifts and three clamps, behind a call) is the single most expensive thing
 * the wallpaper does. The ramp is built once per layer instead, and the inner
 * loop becomes a table read and a store stepping down the column. */
/* The crest height of a hill layer at every column, into 'out'. Split out so a
 * layer can be told where the layer IN FRONT of it starts -- see land_hills. */
static void land_crests(int w, int base_y, int amp, int wavelen, int phase,
                        int *out)
{
    int x;
    if (wavelen <= 0) { return; }
    for (x = 0; x < w; x++) {
        out[x] = base_y - (amp * land_sin((x * 1024) / wavelen + phase)) / 256;
    }
}

/*
 * One hill layer. 'limit' is the crest of the layer in front of it, per column,
 * or NULL for the frontmost layer, which fills to the bottom of the screen.
 *
 * Every layer used to fill from its own crest all the way down, and then the
 * next layer painted over almost all of it. Three layers over an 800x600
 * desktop is about half a million pixels written to draw the two hundred
 * thousand that end up visible -- the back layer's entire body is covered by
 * the middle one, and most of the middle one by the front. The picture is
 * identical either way; the difference is only in how much of it is painted
 * twice, and this is baked on every boot.
 */
static void land_hills(GfxSurface *s, int base_y, int amp, int wavelen,
                       int phase, CColor top, CColor deep, const int *limit)
{
    static CColor ramp[201];
    int x, y, i;
    CColor crest_ink;
    if (wavelen <= 0) { return; }
    for (i = 0; i <= 200; i++) { ramp[i] = gfx_tint(top, deep, i); }
    /* The lit crest line is the same colour for every column, so it is mixed
     * once rather than eight hundred times. */
    crest_ink = gfx_tint(top, GFX_RGB(0xFF, 0xFF, 0xF0), 70);
    for (x = 0; x < s->w; x++) {
        int crest = base_y - (amp * land_sin((x * 1024) / wavelen + phase)) / 256;
        int span = s->h - crest;
        int start = crest;
        int stop = s->h;
        int t, rem, tstep, tmod;
        CColor *px;
        if (span < 1) { span = 1; }
        if (start < 0) { start = 0; }
        /*
         * Where the layer in front takes over. The gradient still runs over
         * the layer's WHOLE height (span is unchanged), so every pixel that
         * does get written is the exact colour it was before -- only the ones
         * that were about to be painted over are skipped.
         */
        if (limit != NULL) {
            stop = limit[x];
            if (stop > s->h) { stop = s->h; }
        }
        px = s->pixels + (long)start * s->pitch + x;
        /*
         * t = ((y - crest) * 200) / span, walked instead of divided.
         *
         * That divide ran once per pixel, and three hill layers cover most of
         * the screen -- about six hundred thousand divides every time the
         * background is baked, which is every boot and every theme change. A
         * divide is one instruction but twenty to forty cycles on the machines
         * this targets, so an instruction-count profile barely shows it.
         *
         * y - crest is never negative (start is the larger of crest and 0), so
         * C's truncation is a floor here and stepping the remainder gives
         * exactly the same t for every pixel -- verified byte for byte against
         * the previous output, not assumed.
         */
        {
            int num = (start - crest) * 200;
            t = num / span;
            rem = num - t * span;
        }
        tstep = 200 / span;
        tmod  = 200 % span;
        for (y = start; y < stop; y++) {
            int tt = t;
            if (tt < 0) { tt = 0; }
            if (tt > 200) { tt = 200; }
            *px = ramp[tt];
            px += s->pitch;
            t += tstep;
            rem += tmod;
            if (rem >= span) { rem -= span; t++; }
        }
        gfx_put_pixel(s, x, crest, crest_ink);
    }
}

/*
 * The sunlit castle on the near hill: towers, keep, walls, merlons, windows,
 * gate, and a gold pennant.
 *
 * It takes the SUN's x, and everything about the light follows from it: which
 * face of each mass is lit, which edge carries the shadow, and which way the
 * castle's shadow falls across the grass. That used to be baked in -- every
 * shaded edge on the right -- and it was backwards. The sun in this scene is
 * high and to the RIGHT (78% of the width) while the castle stands at 30%, so
 * the light comes from the castle's right and it was shading the wrong faces
 * of every tower. Passing the sun in is what stops the two drifting again:
 * move the sun and the stone follows it.
 *
 * The stone is graded top to bottom as well. The sky, the hills and the haze
 * in this scene are all gradients; flat fill is what made the castle read as
 * grey cardboard propped on a painting.
 */
static void land_castle(GfxSurface *s, int cx, int ground_y, int sun_x)
{
    CColor top   = GFX_RGB(0xCC, 0xC3, 0xAE);   /* sun on the parapets   */
    CColor base  = GFX_RGB(0x9C, 0x92, 0x7E);   /* stone toward the grass */
    CColor shade = GFX_RGB(0x6E, 0x66, 0x58);
    CColor edge  = GFX_RGB(0xE4, 0xDC, 0xC6);
    CColor dark  = GFX_RGB(0x3C, 0x36, 0x2C);
    CColor gold  = GFX_RGB(0xE8, 0xC0, 0x4C);
    int lit_right = (sun_x > cx) ? 1 : 0;
    CRect r;
    int i;
    int wall_y = ground_y - 26;

    /* One block of stone: graded down its height, a shadowed band on the face
     * away from the sun and a bright rim on the face toward it. */
#define STONE(RX, RY, RW, RH)                                                 \
    do {                                                                      \
        CRect q = crect_make((RX), (RY), (RW), (RH));                         \
        gfx_vgradient(s, &q, top, base);                                      \
        if (lit_right) {                                                      \
            gfx_vline(s, (RX), (RY), (RH), shade);                            \
            gfx_vline(s, (RX) + 1, (RY), (RH), gfx_tint(base, shade, 110));   \
            gfx_vline(s, (RX) + (RW) - 1, (RY), (RH), edge);                  \
        } else {                                                              \
            gfx_vline(s, (RX) + (RW) - 1, (RY), (RH), shade);                 \
            gfx_vline(s, (RX) + (RW) - 2, (RY), (RH),                         \
                      gfx_tint(base, shade, 110));                            \
            gfx_vline(s, (RX), (RY), (RH), edge);                             \
        }                                                                     \
    } while (0)

    /* The shadow it throws on the grass, away from the sun. Drawn first so the
     * stone lands on top of it. */
    {
        int dir = lit_right ? -1 : 1;
        CColor cast = gfx_tint(GFX_RGB(0x3C, 0x6E, 0x2C),
                               GFX_RGB(0x18, 0x30, 0x14), 120);
        for (i = 0; i < 5; i++) {
            int wide = 68 - i * 4;
            /* Centred on the castle and leaning away from the sun, so it
             * mirrors when the light does instead of sliding off the base. */
            gfx_hline(s, cx - wide / 2 + dir * (6 + i * 4),
                      ground_y - 1 + i / 2, wide, cast);
        }
    }

    /* Curtain wall between the towers. */
    STONE(cx - 26, wall_y, 52, ground_y - wall_y);
    for (i = 0; i < 6; i++) {   /* wall merlons */
        STONE(cx - 26 + i * 9, wall_y - 4, 5, 4);
    }
    /* Side towers. */
    STONE(cx - 34, wall_y - 14, 12, ground_y - wall_y + 14);
    STONE(cx + 22, wall_y - 14, 12, ground_y - wall_y + 14);
    for (i = 0; i < 3; i++) {   /* tower merlons */
        STONE(cx - 34 + i * 5, wall_y - 18, 3, 4);
        STONE(cx + 22 + i * 5, wall_y - 18, 3, 4);
    }
    /* Central keep, tallest, with merlons and the pennant. */
    STONE(cx - 10, wall_y - 30, 20, ground_y - wall_y + 30);
    for (i = 0; i < 3; i++) {
        STONE(cx - 10 + i * 7, wall_y - 34, 4, 4);
    }
#undef STONE

    gfx_vline(s, cx, wall_y - 46, 12, dark);          /* flag pole */
    /* A pennant with a swallow tail rather than a rectangle. It streams the
     * same way the shadow falls -- not because light blows cloth about, but
     * because one direction across the whole scene reads as deliberate and
     * two read as a mistake. */
    {
        int dir = lit_right ? -1 : 1;
        for (i = 0; i < 4; i++) {
            int len = 8 - (i > 1 ? (i - 1) * 2 : 0);
            gfx_hline(s, (dir > 0) ? cx + 1 : cx - len,
                      wall_y - 46 + i, len, gold);
        }
        gfx_hline(s, (dir > 0) ? cx + 1 : cx - 4, wall_y - 42, 4,
                  gfx_tint(gold, dark, 60));
    }
    /* Windows (dark slits) + the gate arch. */
    gfx_vline(s, cx - 29, wall_y - 6, 5, dark);
    gfx_vline(s, cx + 27, wall_y - 6, 5, dark);
    gfx_vline(s, cx - 3, wall_y - 22, 6, dark);
    gfx_vline(s, cx + 3, wall_y - 22, 6, dark);
    r = crect_make(cx - 4, ground_y - 10, 8, 10);
    gfx_fill_rect(s, &r, dark);
    /* A lit arch stone over the gate, so the doorway is a doorway and not a
     * hole punched in the wall. */
    gfx_hline(s, cx - 5, ground_y - 11, 10, edge);
}

/* Paint the whole landscape (covers every pixel; vignette is applied after). */
static void draw_vale(GfxSurface *s)
{
    int w = s->w, h = s->h;
    int horizon = h * 52 / 100;
    CRect sky = crect_make(0, 0, w, horizon);
    CRect haze = crect_make(0, horizon, w, h - horizon);
    int castle_x = w * 30 / 100;
    /* One sun, named once. The castle shades itself from this, so the light on
     * the stone cannot disagree with the light in the sky. */
    int sun_x = w * 78 / 100;
    int sun_y = h * 12 / 100;
    int crest;
    /*
     * The three hill crests, computed up front because the HAZE band needs
     * them too -- see below. NULL when the screen is wider than the tables,
     * which falls back to painting everything the old way: slower, and still
     * right, which is the correct way round for a fallback.
     */
    static int crest_far[SH_MAX_SCREEN_W];
    static int crest_mid[SH_MAX_SCREEN_W];
    static int crest_near[SH_MAX_SCREEN_W];
    cbool have_crests = (w <= SH_MAX_SCREEN_W && g_paint_only_visible) ? CTRUE : CFALSE;
    if (have_crests) {
        land_crests(w, h * 56 / 100, h * 3 / 100, w * 3 / 4, 200, crest_far);
        land_crests(w, h * 64 / 100, h * 5 / 100, w * 3 / 5, 620, crest_mid);
        land_crests(w, h * 76 / 100, h * 7 / 100, w * 9 / 10, 60, crest_near);
    }

    /* Sky: deep azure -> bright -> pale horizon, then a haze band under it. */
    gfx_vgradient3(s, &sky, GFX_RGB(0x1E, 0x4E, 0xA8), GFX_RGB(0x6F, 0xA8, 0xE0),
                   GFX_RGB(0xC8, 0xE2, 0xF4), 640);
    /*
     * The haze band used to run from the horizon to the bottom of the screen,
     * and the hills then covered all but the top forty of its two hundred and
     * eighty rows. Below the LOWEST crest of the back layer, the three layers
     * between them cover every column, so there is nothing down there the haze
     * can be seen through -- it stops there.
     */
    if (have_crests) {
        int lowest = crest_far[0], x2;
        for (x2 = 1; x2 < w; x2++) {
            if (crest_far[x2] > lowest) { lowest = crest_far[x2]; }
        }
        if (lowest + 1 < haze.y1) { haze.y1 = lowest + 1; }
    }
    gfx_fill_rect(s, &haze, GFX_RGB(0xC8, 0xE2, 0xF4));

    /* Sun glow high right, then a few puffy clouds (two blobs each). */
    land_glow(s, sun_x, sun_y, w / 7, h / 9,
              GFX_RGB(0xFF, 0xF2, 0xC8), 170);
    land_glow(s, w * 22 / 100, h * 15 / 100, w * 9 / 100, h * 3 / 100,
              GFX_RGB(0xFF, 0xFF, 0xFF), 230);
    land_glow(s, w * 26 / 100, h * 13 / 100, w * 5 / 100, h * 2 / 100,
              GFX_RGB(0xFF, 0xFF, 0xFF), 240);
    land_glow(s, w * 55 / 100, h * 24 / 100, w * 11 / 100, h * 3 / 100,
              GFX_RGB(0xFF, 0xFF, 0xFF), 210);
    land_glow(s, w * 59 / 100, h * 22 / 100, w * 5 / 100, h * 2 / 100,
              GFX_RGB(0xFF, 0xFF, 0xFF), 235);

    /*
     * Three hill layers, back (hazy) to front (saturated).
     *
     * Each is told where the layer in FRONT of it starts, so it stops there
     * instead of filling to the bottom of the screen and being painted over.
     * The picture is identical; about three hundred thousand of the five
     * hundred thousand pixels this used to write were covered by the next
     * layer before anybody saw them, and this is baked on every boot.
     *
     * A screen wider than the crest tables falls back to the old behaviour --
     * slower, and still right, which is the correct way round for a fallback.
     */
    {
        const int *lim_far = have_crests ? crest_mid  : NULL;
        const int *lim_mid = have_crests ? crest_near : NULL;
        land_hills(s, h * 56 / 100, h * 3 / 100, w * 3 / 4, 200,
                   GFX_RGB(0x8A, 0xB2, 0x84), GFX_RGB(0x6E, 0x96, 0x6E), lim_far);
        land_hills(s, h * 64 / 100, h * 5 / 100, w * 3 / 5, 620,
                   GFX_RGB(0x58, 0x96, 0x40), GFX_RGB(0x3C, 0x6E, 0x2C), lim_mid);
        land_hills(s, h * 76 / 100, h * 7 / 100, w * 9 / 10, 60,
                   GFX_RGB(0x3E, 0x80, 0x2A), GFX_RGB(0x22, 0x50, 0x18), NULL);
    }

    /* The castle stands on the near hill's crest. */
    crest = h * 76 / 100 -
            (h * 7 / 100 * land_sin((castle_x * 1024) / (w * 9 / 10) + 60)) / 256;
    land_castle(s, castle_x, crest + 6, sun_x);
}

/* Render the STATIC desktop background (landscape/wallpaper + watermark) --
 * everything that does not change frame to frame. Drawn once into the cache
 * (which also applies the vignette) and used as the live fallback. */
static void desk_render_background(GfxSurface *s)
{
    CRect full = crect_make(0, 0, g_sh.screen_w, g_sh.screen_h);
    char  brand[64];
    if (wallpaper_active()) {
        gfx_vgradient(s, &full, g_sh.theme.desktop_top, g_sh.theme.desktop_bottom);
        draw_wallpaper(s);
    } else if (sh_glossy()) {
        /* The factory landscape (fully covers the screen). */
        draw_vale(s);
    } else {
        /* Low-color / safe mode: the flat theme gradient + castle silhouette,
         * staying inside the reduced palette. */
        gfx_vgradient(s, &full, g_sh.theme.desktop_top, g_sh.theme.desktop_bottom);
        draw_castle(s);
    }
    sys_snprintf(brand, sizeof(brand), "%s  %s", CASTALIA_NAME, CASTALIA_VER_STRING);
    gfx_draw_text(s, GFX_FONT_SYSTEM,
                  g_sh.screen_w - gfx_text_width(GFX_FONT_SYSTEM, brand) - 10,
                  g_sh.screen_h - g_sh.taskbar_h - 16, brand,
                  GFX_RGB(0x7C, 0x8A, 0x9C));
}

/*
 * Desktop background cache. The gradient + castle + watermark are static, so
 * recomputing the per-pixel gradient on every dirty rectangle (every cursor
 * move, drag, and animation frame) is wasted work on a P2. Instead we paint the
 * background once into an offscreen surface and blit the dirty area from it --
 * a per-row memcpy instead of a per-pixel lerp. Rebuilt on theme/size change.
 */
static GfxSurface *g_desk_cache      = NULL;
static cbool       g_desk_cache_ok   = CFALSE;
static cbool       g_desk_use_cache  = CTRUE;   /* toggled off by the benchmark */

/*
 * What the cached image actually depends on.
 *
 * Rebuilding it costs about 24 million instructions -- the sky gradient, five
 * soft glows, three hill layers and a vignette, every pixel of an 800x600
 * surface several times over. That is most of the shell's startup, and it was
 * being paid again on EVERY theme apply and every settings apply, including
 * the ones where the picture does not change at all.
 *
 * And it usually does not. The factory landscape is drawn from hardcoded
 * colours: it depends on the screen size and on nothing else, so switching
 * from Aurora to Forest Green repaints the window frames and produces a
 * pixel-identical desktop behind them. On the target that is a visible stall
 * every time somebody clicks a theme in the Control Center, for no change.
 *
 * So the key names, per branch, exactly what that branch reads -- and the vale
 * branch names almost nothing, which is the point. Fields a branch does not
 * use stay zero rather than being filled in "just in case": a key that
 * mentions the theme colours in a mode that ignores them would rebuild on
 * every theme change and this would all be for nothing.
 *
 * What the branches read, checked rather than assumed: draw_vale() is entirely
 * hardcoded colours over s->w/s->h; draw_castle() is hardcoded colours placed
 * from screen_w/screen_h/taskbar_h; the brand watermark is a compile-time
 * string in a hardcoded colour at a position from the same three; and
 * apply_vignette() reads only the surface. taskbar_h itself comes from
 * screen_h alone (sh_taskbar_layout), so no theme can move it out from under a
 * key that was taken before the taskbar was laid out again.
 */
typedef struct {
    int    w, h, taskbar_h;
    int    mode;               /* 0 wallpaper, 1 the vale, 2 flat + castle */
    CColor top, bottom;        /* modes 0 and 2 only                       */
    int    wall_mode, wall_w, wall_h;   /* mode 0 only                     */
    char   wall[CASTALIA_MAX_PATH];     /* mode 0 only                     */
} DeskCacheKey;

/*
 * The cached background, or NULL when it has not been built (a size change, or
 * the benchmark's no-cache mode). This is the wallpaper ALONE -- the desktop
 * icons are composited on top of a blit from it -- which is what makes it the
 * right thing to measure: a check that samples the finished screen is looking
 * at the wallpaper plus whatever happens to be drawn over it, and the first
 * version of --vale-demo duly located the "sun" inside the Documents folder
 * icon, which is the warmest thing on the left of the screen.
 */
const GfxSurface *sh_desktop_background(void)
{
    return g_desk_cache_ok ? g_desk_cache : NULL;
}

static DeskCacheKey g_cache_key;
static cbool        g_cache_key_ok = CFALSE;
static long         g_cache_builds = 0;

static void desk_cache_key(DeskCacheKey *k)
{
    memset(k, 0, sizeof(*k));
    k->w = g_sh.screen_w;
    k->h = g_sh.screen_h;
    k->taskbar_h = g_sh.taskbar_h;      /* the watermark sits above the bar */
    if (wallpaper_active()) {
        k->mode = 0;
        k->top = g_sh.theme.desktop_top;
        k->bottom = g_sh.theme.desktop_bottom;
        k->wall_mode = g_wall_mode;
        k->wall_w = g_wallpaper->w;
        k->wall_h = g_wallpaper->h;
        sys_strlcpy(k->wall, g_wall_path, sizeof(k->wall));
    } else if (sh_glossy()) {
        k->mode = 1;            /* the vale reads the screen size and no more */
    } else {
        k->mode = 2;
        k->top = g_sh.theme.desktop_top;
        k->bottom = g_sh.theme.desktop_bottom;
    }
}

long sh_desktop_cache_builds(void) { return g_cache_builds; }

void sh_desktop_build_cache(void)
{
    DeskCacheKey key;

    if (g_desk_cache != NULL &&
        (g_desk_cache->w != g_sh.screen_w || g_desk_cache->h != g_sh.screen_h)) {
        gfx_surface_free(g_desk_cache);
        g_desk_cache = NULL;
        g_cache_key_ok = CFALSE;
    }
    /* Must run before the key is taken: it is what loads or drops the
     * wallpaper surface the key then describes. */
    wallpaper_sync();
    desk_cache_key(&key);
    if (g_desk_cache != NULL && g_desk_cache_ok && g_cache_key_ok &&
        memcmp(&key, &g_cache_key, sizeof(key)) == 0) {
        return;                 /* the same picture as the one already there */
    }

    g_desk_cache_ok = CFALSE;
    if (g_desk_cache == NULL) {
        g_desk_cache = gfx_surface_new(g_sh.screen_w, g_sh.screen_h);
        if (g_desk_cache == NULL) { return; }   /* live-render fallback */
    }
    gfx_reset_clip(g_desk_cache);
    desk_render_background(g_desk_cache);
    apply_vignette(g_desk_cache);   /* one-time; keeps steady-state repaint cheap */
    g_cache_key = key;
    g_cache_key_ok = CTRUE;
    g_cache_builds++;
    g_desk_cache_ok = CTRUE;
}

void sh_desktop_free_cache(void)
{
    if (g_desk_cache != NULL) { gfx_surface_free(g_desk_cache); g_desk_cache = NULL; }
    if (g_wallpaper != NULL)  { gfx_surface_free(g_wallpaper);  g_wallpaper = NULL; }
    g_wall_path[0] = '\0';
    g_desk_cache_ok = CFALSE;
    g_cache_key_ok = CFALSE;   /* nothing to compare against any more */
    sh_iconpack_free();
}

void sh_desktop_set_cache_enabled(cbool on) { g_desk_use_cache = on; }

void sh_desktop_set_hill_limits(cbool on)
{
    if (on == g_paint_only_visible) { return; }
    g_paint_only_visible = on;
    /* The cached picture was baked the other way, and the cache key describes
     * the SCENE rather than how it was drawn -- so it would not notice. */
    g_cache_key_ok = CFALSE;
    g_desk_cache_ok = CFALSE;
}

/* A checksum of the cached background, so two bakes can be compared without
 * holding two copies of an 800x600 surface. */
cu32 sh_desktop_cache_sum(void)
{
    const GfxSurface *s = sh_desktop_background();
    cu32 sum = 2166136261u;
    int x, y;
    if (s == NULL) { return 0u; }
    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            sum = (sum ^ (cu32)s->pixels[(long)y * s->pitch + x]) * 16777619u;
        }
    }
    return sum;
}

/* Time 'iters' full-screen desktop repaints with the cache on (blit) and off
 * (live per-pixel gradient), so the win is measurable. Restores the cache. */
void sh_desktop_bench(int iters, unsigned long *cached_ms, unsigned long *live_ms)
{
    CRect full = crect_make(0, 0, g_sh.screen_w, g_sh.screen_h);
    cu32 t0;
    int  k;
    if (iters < 1) { iters = 1; }
    g_desk_use_cache = CTRUE;
    for (k = 0; k < 20; k++) { sh_desktop_paint(&full); }   /* warm caches */
    t0 = plat_ticks_ms();
    for (k = 0; k < iters; k++) { sh_desktop_paint(&full); }
    if (cached_ms != NULL) { *cached_ms = (unsigned long)(plat_ticks_ms() - t0); }
    g_desk_use_cache = CFALSE;
    t0 = plat_ticks_ms();
    for (k = 0; k < iters; k++) { sh_desktop_paint(&full); }
    if (live_ms != NULL) { *live_ms = (unsigned long)(plat_ticks_ms() - t0); }
    g_desk_use_cache = CTRUE;
}

void sh_desktop_paint(const CRect *clip)
{
    GfxSurface *s = g_sh.back;
    CRect full = crect_make(0, 0, g_sh.screen_w, g_sh.screen_h);
    CRect area = (clip != NULL) ? crect_intersect(&full, clip) : full;
    int i;

    if (crect_empty(&area)) { return; }
    gfx_set_clip(s, &area);

    if (g_desk_use_cache && g_desk_cache_ok) {
        gfx_blit(s, area.x0, area.y0, g_desk_cache, &area, GFX_BLIT_COPY);
    } else {
        desk_render_background(s);
    }

    /* Icons are drawn live (their selection highlight changes). */
    for (i = 0; i < DESK_ICON_COUNT; i++) {
        if (crect_overlaps(&g_icons[i].hit, &area)) {
            draw_icon(s, &g_icons[i], (i == g_sel_icon) ? CTRUE : CFALSE,
                      (wm_focused() == NULL) ? CTRUE : CFALSE);
        }
    }
    /* While dragging, a floating ghost of the icon follows the cursor. */
    if (g_drag_active && g_drag_icon >= 0) {
        CRect gr = drag_ghost_rect();
        if (crect_overlaps(&gr, &area)) {
            DeskIcon ghost = g_icons[g_drag_icon];
            ghost.hit = gr;
            draw_icon(s, &ghost, CTRUE, CTRUE);  /* being dragged: live */
        }
    }

    gfx_reset_clip(s);
}

cbool sh_desktop_handle_click(int x, int y, int buttons)
{
    int i;
    CASTALIA_UNUSED(buttons);
    for (i = 0; i < DESK_ICON_COUNT; i++) {
        if (crect_contains(&g_icons[i].hit, x, y)) {
            cu32 now = plat_ticks_ms();
            cbool dbl = (i == g_icon_click_idx &&
                         (now - g_icon_click_ms) < (cu32)settings_get()->dblclick_ms) ? CTRUE : CFALSE;
            /* Repaint the old and new selection. */
            if (g_sel_icon != i) {
                if (g_sel_icon >= 0) { sh_mark_dirty(&g_icons[g_sel_icon].hit); }
                g_sel_icon = i;
                sh_mark_dirty(&g_icons[i].hit);
            }
            /* Arm a drag from this icon (activates only if the pointer moves). */
            g_drag_icon = i;
            g_drag_active = CFALSE;
            g_down_x = x; g_down_y = y;
            g_drag_x = x; g_drag_y = y;
            g_drag_off_x = x - g_icons[i].hit.x0;
            g_drag_off_y = y - g_icons[i].hit.y0;
            if (dbl) {
                /* Double-click opens (Win98 behavior). */
                g_icon_click_idx = -1; /* consume the pair */
                if (g_icons[i].cmd != SH_CMD_NONE) { sh_dispatch_command(g_icons[i].cmd); }
            } else {
                g_icon_click_idx = i;
                g_icon_click_ms = now;
            }
            return CTRUE;
        }
    }
    /* Click on empty desktop clears the selection. */
    if (g_sel_icon >= 0) {
        sh_mark_dirty(&g_icons[g_sel_icon].hit);
        g_sel_icon = -1;
    }
    return CFALSE;
}

cbool sh_desktop_is_dragging(void)
{
    return (g_drag_icon >= 0) ? CTRUE : CFALSE;
}

/*
 * Arrow keys across the icons, and Enter to open one.
 *
 * Reached only when nothing else wants the key -- no window focused, no menu
 * or launcher open, no keyboard move running -- which is exactly the state the
 * desktop is the only thing on screen. Until this existed the icons could be
 * reached with a mouse and in no other way, on a system whose own hardware
 * verification was carried out with a keyboard alone.
 *
 * Where an arrow goes is icon_nav.c's business (pure, tested against desktops
 * laid out by hand), because the icons are draggable: after somebody has
 * rearranged them, "the next one" has to mean the next one on the SCREEN, and
 * the order they happen to sit in this array says nothing about that.
 */
cbool sh_desktop_handle_key(int key)
{
    static const int DIR[4][2] = {
        { PLAT_KEY_LEFT,  ICON_NAV_LEFT  },
        { PLAT_KEY_RIGHT, ICON_NAV_RIGHT },
        { PLAT_KEY_UP,    ICON_NAV_UP    },
        { PLAT_KEY_DOWN,  ICON_NAV_DOWN  }
    };
    CRect hits[DESK_ICON_COUNT];
    int i;

    if (key == PLAT_KEY_ENTER) {
        if (g_sel_icon < 0 || g_sel_icon >= DESK_ICON_COUNT) { return CFALSE; }
        if (g_icons[g_sel_icon].cmd == SH_CMD_NONE) { return CTRUE; }
        sh_dispatch_command(g_icons[g_sel_icon].cmd);
        return CTRUE;
    }
    for (i = 0; i < DESK_ICON_COUNT; i++) { hits[i] = g_icons[i].hit; }
    for (i = 0; i < 4; i++) {
        int next;
        if (key != DIR[i][0]) { continue; }
        next = icon_nav_next(hits, DESK_ICON_COUNT, g_sel_icon, DIR[i][1]);
        /* Nothing that way: keep the selection where it is. An arrow at the
         * edge of the desktop should do nothing rather than jump. */
        if (next < 0 || next == g_sel_icon) { return CTRUE; }
        if (g_sel_icon >= 0) { sh_mark_dirty(&g_icons[g_sel_icon].hit); }
        g_sel_icon = next;
        sh_mark_dirty(&g_icons[next].hit);
        return CTRUE;
    }
    return CFALSE;
}

/* Where an icon is, for the headless driver: the double-click scene has to
 * click real pixels on a real icon, and the layout moves with the screen. */
cbool sh_desktop_icon_rect(int i, CRect *out)
{
    if (i < 0 || i >= DESK_ICON_COUNT || out == NULL) { return CFALSE; }
    *out = g_icons[i].hit;
    return CTRUE;
}

int sh_desktop_selected(void) { return g_sel_icon; }

void sh_desktop_mark_icons(void)
{
    int i;
    for (i = 0; i < DESK_ICON_COUNT; i++) {
        CRect r = g_icons[i].hit;
        r.y1 += ICON_LABEL_H;      /* the label plate hangs below the hit box */
        sh_mark_dirty(&r);
    }
}

cbool sh_desktop_icon_context(int x, int y)
{
    int i;
    for (i = 0; i < DESK_ICON_COUNT; i++) {
        if (crect_contains(&g_icons[i].hit, x, y)) {
            if (g_sel_icon != i) {
                if (g_sel_icon >= 0) { sh_mark_dirty(&g_icons[g_sel_icon].hit); }
                g_sel_icon = i;
                sh_mark_dirty(&g_icons[i].hit);
            }
            sh_context_open_icon(x, y, g_icons[i].cmd,
                                 (g_icons[i].cmd == SH_CMD_RECYCLEBIN)
                                 ? CTRUE : CFALSE, g_trash_full);
            return CTRUE;
        }
    }
    return CFALSE;
}

/* Where the dragged icon's floating ghost sits for the current cursor. */
static CRect drag_ghost_rect(void)
{
    const CRect *h = &g_icons[g_drag_icon].hit;
    return crect_make(g_drag_x - g_drag_off_x, g_drag_y - g_drag_off_y,
                      crect_w(h), crect_h(h));
}

cbool sh_desktop_handle_motion(int x, int y)
{
    if (g_drag_icon < 0) { return CFALSE; }
    g_drag_x = x; g_drag_y = y;
    if (!g_drag_active) {
        int dx = x - g_down_x, dy = y - g_down_y;
        if (dx < 0) { dx = -dx; }
        if (dy < 0) { dy = -dy; }
        if (dx <= ICON_DRAG_THRESH && dy <= ICON_DRAG_THRESH) {
            return CTRUE;   /* still just a click in progress */
        }
        g_drag_active = CTRUE;
        g_ghost_rect = g_icons[g_drag_icon].hit;
    }
    /* Repaint the old ghost location (erase) and the new one (draw). */
    sh_mark_dirty(&g_ghost_rect);
    g_ghost_rect = drag_ghost_rect();
    sh_mark_dirty(&g_ghost_rect);
    return CTRUE;
}

cbool sh_desktop_handle_up(int x, int y)
{
    cbool was_active = g_drag_active;
    if (g_drag_icon < 0) { return CFALSE; }
    if (g_drag_active) {
        int i = g_drag_icon;
        int nx = x - g_drag_off_x, ny = y - g_drag_off_y;
        int w = crect_w(&g_icons[i].hit), h = crect_h(&g_icons[i].hit);
        int maxx = g_sh.screen_w - w;
        int maxy = g_sh.taskbar_rect.y0 - h;
        if (nx < 2) { nx = 2; } else if (nx > maxx) { nx = maxx; }
        if (ny < 2) { ny = 2; } else if (ny > maxy) { ny = maxy; }
        sh_mark_dirty(&g_icons[i].hit);   /* erase the old home */
        sh_mark_dirty(&g_ghost_rect);     /* erase the last ghost */
        g_icons[i].hit = crect_make(nx, ny, w, h);
        sh_mark_dirty(&g_icons[i].hit);   /* draw at the new home */
        SYS_LOGI("sh", "desktop icon '%s' moved to %d,%d",
                 g_icons[i].label, nx, ny);
        sh_desktop_save_positions();      /* the new layout persists */
    }
    g_drag_icon = -1;
    g_drag_active = CFALSE;
    return was_active;   /* consumed the up only if a drag was in progress */
}
