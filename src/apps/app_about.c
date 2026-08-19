/*
 * app_about.c - CastaliaOS "About CastaliaOS 98 PE" window.
 *
 * A spectacular, information-dense about box: a tall glossy royal-blue banner
 * with a hand-drawn gold castle crest and the product name, then a row of tabs
 * -- General, Hardware, Statistics, Credits -- over a sunken content panel.
 * Every value is real and live: identity comes from the product macros, the
 * codebase figures from buildstats.h, and the hardware/memory/clock readings
 * from the platform and system runtimes. A subtle sheen sweeps the banner each
 * frame. The window owns a heap payload freed on WM_MSG_DESTROY.
 *
 * Structure mirrors app_taskman.c (payload + layout struct + paint + click +
 * destroy); the tab model follows app_control.c.
 */
#include "apps.h"
#include "castalia/castalia.h"
#include "castalia/buildstats.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"

/* The shared Castalia mark (src/shell/sh_logo.c) -- one brand, one drawing. */
void sh_logo_draw_plated(GfxSurface *s, int x, int y, int size);
#include "castalia/plat.h"
#include "castalia/sys.h"
#include "castalia/net.h"
#include "castalia/snd.h"

#define AB_BANNER_H 64
#define AB_TAB_H    22
#define AB_FOOTER_H 34
#define AB_PAD      10
#define AB_BTN_W    72
#define AB_BTN_H    22
#define AB_MEM_BUDGET (8L * 1024L * 1024L)  /* the 8 MB idle budget */

enum { AB_GENERAL = 0, AB_HARDWARE, AB_STATS, AB_CREDITS, AB_TAB_COUNT };
static const char *AB_TAB_LABEL[AB_TAB_COUNT] = {
    "General", "Hardware", "Statistics", "Credits"
};

typedef struct {
    int   tab;     /* current tab (AB_*)                         */
    int   phase;   /* banner-sheen animation phase (px offset)   */
    UiHot hot;     /* the OK button, under the pointer or held   */
} About;

/* ---- layout ---------------------------------------------------------- */
typedef struct {
    CRect banner;
    CRect tab[AB_TAB_COUNT];
    CRect panel;
    CRect ok;
} AbLayout;

static void ab_layout(WmWindow *win, AbLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c);
    int panel_y = AB_BANNER_H + AB_TAB_H + AB_PAD;
    int tw = cw / AB_TAB_COUNT;
    int i, x;

    L->banner = crect_make(0, 0, cw, AB_BANNER_H);
    x = 0;
    for (i = 0; i < AB_TAB_COUNT; i++) {
        int w = (i == AB_TAB_COUNT - 1) ? (cw - x) : tw;
        L->tab[i] = crect_make(x, AB_BANNER_H, w, AB_TAB_H);
        x += w;
    }
    L->panel = crect_make(AB_PAD, panel_y, cw - 2 * AB_PAD,
                          ch - panel_y - AB_FOOTER_H);
    L->ok = crect_make(cw - AB_BTN_W - AB_PAD,
                       ch - AB_FOOTER_H + (AB_FOOTER_H - AB_BTN_H) / 2,
                       AB_BTN_W, AB_BTN_H);
}

/* ---- small helpers --------------------------------------------------- */
/* Group a long integer with thousands separators, e.g. 21251 -> "21,251". */
static void ab_commafy(long v, char *dst, cu32 dstsz)
{
    char digits[24];
    char out[32];
    int n = 0, oi = 0, i, cnt;
    unsigned long uv;

    if (dstsz == 0) { return; }
    uv = (v < 0) ? (unsigned long)(-v) : (unsigned long)v;
    if (uv == 0) { digits[n++] = '0'; }
    while (uv > 0 && n < (int)sizeof(digits)) {
        digits[n++] = (char)('0' + (int)(uv % 10UL));
        uv /= 10UL;
    }
    if (v < 0 && oi < (int)sizeof(out) - 1) { out[oi++] = '-'; }
    cnt = 0;
    for (i = n - 1; i >= 0; i--) {
        if (cnt > 0 && (i + 1) % 3 == 0 && oi < (int)sizeof(out) - 1) {
            out[oi++] = ',';
        }
        if (oi < (int)sizeof(out) - 1) { out[oi++] = digits[i]; }
        cnt++;
    }
    out[oi] = '\0';
    sys_strlcpy(dst, out, dstsz);
}

/* A label:value table row. Label in the muted color, value in normal text. */
static void ab_kv(GfxSurface *s, int x, int y, int vcol,
                  const char *k, const char *v, const UiPalette *p)
{
    gfx_draw_text(s, GFX_FONT_SYSTEM, x, y, k, p->text_disabled);
    gfx_draw_text(s, GFX_FONT_SYSTEM, x + vcol, y, v, p->text);
}

/* A labeled horizontal bar meter (value scaled against maxv). */
static void ab_bar(GfxSurface *s, int x, int y, int labw, int barw,
                   const char *label, long value, long maxv,
                   const UiPalette *p)
{
    CRect well;
    char num[24];

    gfx_draw_text(s, GFX_FONT_SYSTEM, x, y + 2, label, p->text);
    well = crect_make(x + labw, y, barw, 12);
    ui_draw_meter(s, &well, (cs32)value, (cs32)maxv, p->accent);
    ab_commafy(value, num, sizeof num);
    gfx_draw_text(s, GFX_FONT_SYSTEM, well.x1 + 6, y + 2, num, p->text);
}

/* A moving highlight band sweeping the banner (cheap read-modify-write). */
static void ab_sheen(GfxSurface *s, const CRect *b, int phase)
{
    int bw = crect_w(b), bh = crect_h(b);
    int band = 26, period, sx, col, row;

    if (bw <= 0 || bh <= 0) { return; }
    period = bw + band;
    sx = (phase % period) - band;
    for (col = 0; col < band; col++) {
        int x = b->x0 + sx + col;
        int dx = col - band / 2;
        int amt;
        if (x < b->x0 || x >= b->x1) { continue; }
        if (dx < 0) { dx = -dx; }
        amt = 70 - (dx * 70) / (band / 2);
        if (amt <= 0) { continue; }
        for (row = 0; row < bh; row++) {
            int y = b->y0 + row;
            CColor c = gfx_get_pixel(s, x, y);
            gfx_put_pixel(s, x, y, gfx_tint(c, 0xFFFFFF, amt));
        }
    }
}

/* ---- banner ---------------------------------------------------------- */
static void ab_paint_banner(GfxSurface *s, const CRect *r, int phase)
{
    CColor top   = GFX_RGB(0x1A, 0x3C, 0x9A);
    CColor mid   = GFX_RGB(0x3C, 0x66, 0xCC);
    CColor bot   = GFX_RGB(0x0A, 0x1E, 0x5E);
    CColor white = GFX_RGB(0xFF, 0xFF, 0xFF);
    CColor sub   = GFX_RGB(0xCF, 0xDC, 0xF7);
    CColor shadow = GFX_RGB(0x06, 0x12, 0x38);
    char buf[96];
    int tx = r->x0 + AB_PAD + 52;

    gfx_vgradient3(s, r, top, mid, bot, 300);
    gfx_hline(s, r->x0, r->y0, crect_w(r), gfx_tint(top, 0xFFFFFF, 150));
    gfx_hline(s, r->x0, r->y1 - 1, crect_w(r), GFX_RGB(0x04, 0x0C, 0x2E));
    ab_sheen(s, r, phase);

    sh_logo_draw_plated(s, r->x0 + AB_PAD, r->y0 + (crect_h(r) - 44) / 2, 44);

    gfx_draw_text_shadow(s, GFX_FONT_BOLD, tx, r->y0 + 12,
                         CASTALIA_NAME, white, shadow);
    sys_snprintf(buf, sizeof buf, "%s  -  Version %s  (%s)",
                 CASTALIA_EDITION, CASTALIA_VER_STRING, CASTALIA_VER_STAGE);
    gfx_draw_text(s, GFX_FONT_SYSTEM, tx, r->y0 + 28, buf, sub);
    sys_snprintf(buf, sizeof buf, "%s  -  Codename \"%s\"",
                 CASTALIA_VENDOR, CASTALIA_CODENAME);
    gfx_draw_text(s, GFX_FONT_SYSTEM, tx, r->y0 + 42, buf, sub);
}

/* ---- tabs ------------------------------------------------------------ */
static void ab_paint_tabs(GfxSurface *s, const AbLayout *L, CPoint o,
                          int cur, const UiPalette *p)
{
    int i;
    for (i = 0; i < AB_TAB_COUNT; i++) {
        CRect tr = crect_offset(&L->tab[i], o.x, o.y);
        if (i == cur) {
            CRect strip;
            gfx_bevel(s, &tr, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
            strip = crect_make(tr.x0 + 2, tr.y0 + 2, crect_w(&tr) - 4, 2);
            gfx_fill_rect(s, &strip, p->accent);
            gfx_draw_text_rect(s, GFX_FONT_BOLD, &tr, AB_TAB_LABEL[i],
                               p->text, GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
        } else {
            CColor recessed = gfx_tint(p->face, p->dark, 48);
            gfx_fill_rect(s, &tr, recessed);
            gfx_hline(s, tr.x0, tr.y1 - 1, crect_w(&tr), p->dark);
            gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &tr, AB_TAB_LABEL[i],
                               ui_text_dim(),
                               GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
        }
    }
}

/* ---- panels ---------------------------------------------------------- */
static void ab_panel_general(GfxSurface *s, const CRect *pr, const UiPalette *p)
{
    int x = pr->x0 + 6;
    int y = pr->y0 + 6;
    int lh = gfx_font_height(GFX_FONT_SYSTEM) + 4;
    int vcol = 84;
    int hh = 0, mm = 0, ssec = 0;
    cu32 up;
    char buf[96];
    CRect div;

    gfx_draw_text(s, GFX_FONT_BOLD, x, y, "Product Identity", p->text);
    y += lh + 2;
    ab_kv(s, x, y, vcol, "Product", CASTALIA_NAME, p);       y += lh;
    ab_kv(s, x, y, vcol, "Edition", CASTALIA_EDITION, p);    y += lh;
    ab_kv(s, x, y, vcol, "Codename", CASTALIA_CODENAME, p);  y += lh;
    ab_kv(s, x, y, vcol, "Version", CASTALIA_VER_STRING, p); y += lh;
    ab_kv(s, x, y, vcol, "Stage", CASTALIA_VER_STAGE, p);    y += lh;
    ab_kv(s, x, y, vcol, "Vendor", CASTALIA_VENDOR, p);      y += lh;
    sys_snprintf(buf, sizeof buf, "%s  %s", __DATE__, __TIME__);
    ab_kv(s, x, y, vcol, "Built", buf, p);                  y += lh;
    ab_kv(s, x, y, vcol, "License", "MIT (see LEGAL.md)", p); y += lh + 4;

    div = crect_make(pr->x0 + 4, y, crect_w(pr) - 8, 2);
    gfx_bevel(s, &div, GFX_BEVEL_ETCHED, p->light, p->dark, GFX_NO_FILL);
    y += 8;

    gfx_draw_text(s, GFX_FONT_SYSTEM, x, y,
                  "An original, Windows 9x-inspired desktop", p->text);
    y += lh;
    gfx_draw_text(s, GFX_FONT_SYSTEM, x, y,
                  "environment written from scratch in C. It", p->text);
    y += lh;
    gfx_draw_text(s, GFX_FONT_SYSTEM, x, y,
                  "contains no Microsoft code, assets, or", p->text);
    y += lh;
    gfx_draw_text(s, GFX_FONT_SYSTEM, x, y,
                  "branding -- just a love letter to the era.", p->text);
    y += lh + 6;

    plat_wall_clock(&hh, &mm, &ssec);
    up = sys_now_ms() / 1000u;
    sys_snprintf(buf, sizeof buf,
                 "Clock %02d:%02d:%02d      Uptime %lu:%02lu:%02lu",
                 hh, mm, ssec,
                 (unsigned long)(up / 3600u),
                 (unsigned long)((up / 60u) % 60u),
                 (unsigned long)(up % 60u));
    gfx_draw_text(s, GFX_FONT_BOLD, x, y, buf, p->accent);
}

static void ab_panel_hardware(GfxSurface *s, const CRect *pr, const UiPalette *p)
{
    int x = pr->x0 + 6;
    int y = pr->y0 + 6;
    int lh = gfx_font_height(GFX_FONT_SYSTEM) + 4;
    int vcol = 74;
    PlatVideoInfo vi;
    NetDeviceInfo ni;
    char buf[96];
    char mac[24];
    long live, peak;
    CRect well;

    plat_video_info(&vi);
    net_device_info(&ni);

    gfx_draw_text(s, GFX_FONT_BOLD, x, y, "System Devices", p->text);
    y += lh + 2;

    ab_kv(s, x, y, vcol, "Host", plat_identity(), p); y += lh;
    sys_snprintf(buf, sizeof buf, "%d x %d   %d bpp", vi.width, vi.height, vi.bpp);
    ab_kv(s, x, y, vcol, "Video", buf, p); y += lh;
    ab_kv(s, x, y, vcol, "Driver",
          vi.driver_name ? vi.driver_name : "(unknown)", p); y += lh;
    ab_kv(s, x, y, vcol, "Sound", snd_device_name(), p); y += lh;
    ab_kv(s, x, y, vcol, "Network",
          ni.available ? ni.name : "Not present (offline)", p); y += lh;
    if (ni.available) {
        net_mac_format(ni.mac, mac, sizeof mac);
        ab_kv(s, x, y, vcol, "MAC", mac, p);
    } else {
        ab_kv(s, x, y, vcol, "MAC", "--", p);
    }
    y += lh + 4;

    /* Memory readout + a live usage gauge against the 8 MB budget. */
    live = (long)sys_mem_live_bytes();
    peak = (long)sys_mem_peak_bytes();
    sys_snprintf(buf, sizeof buf, "%lu KB live   %lu KB peak   %lu block(s)",
                 (unsigned long)(live / 1024L),
                 (unsigned long)(peak / 1024L),
                 (unsigned long)sys_mem_alloc_count());
    gfx_draw_text(s, GFX_FONT_SYSTEM, x, y, "Memory", p->text_disabled);
    gfx_draw_text(s, GFX_FONT_SYSTEM, x + vcol, y, buf, p->text);
    y += lh + 2;

    /* The About window has its OWN memory gauge, separate from ab_bar and
     * from the Task Manager's, and it carried the same overflow: live is in
     * BYTES, so live * width passed 2^31 at 5.31 MB of an 8 MB budget and the
     * bar read empty. Three copies of one widget, one of them wrong twice
     * over. */
    well = crect_make(x, y, crect_w(pr) - 12, 12);
    ui_draw_meter(s, &well, (cs32)live, (cs32)AB_MEM_BUDGET, p->accent);
    y += 16;
    sys_snprintf(buf, sizeof buf, "Live memory vs. 8 MB idle budget");
    gfx_draw_text(s, GFX_FONT_SYSTEM, x, y, buf, p->text_disabled);
}

static void ab_panel_stats(GfxSurface *s, const CRect *pr, const UiPalette *p)
{
    int x = pr->x0 + 6;
    int y = pr->y0 + 6;
    int lh = gfx_font_height(GFX_FONT_SYSTEM) + 4;
    int barw, labw = 62;
    char num[24];
    char buf[96];
    long maxv;

    gfx_draw_text(s, GFX_FONT_BOLD, x, y, "This Build", p->text);
    y += lh + 2;

    /* Headline: total lines of code. */
    ab_commafy(CASTALIA_STAT_TOTAL_LOC, num, sizeof num);
    sys_snprintf(buf, sizeof buf, "%s", num);
    gfx_draw_text(s, GFX_FONT_BOLD, x, y, buf, p->accent);
    gfx_draw_text(s, GFX_FONT_SYSTEM,
                  x + gfx_text_width(GFX_FONT_BOLD, buf) + 8, y,
                  "total lines (source + tests)", p->text);
    y += lh + 6;

    /* Comparative bars, scaled against the largest (code). */
    barw = crect_w(pr) - labw - 60;
    if (barw < 40) { barw = 40; }
    maxv = CASTALIA_STAT_CODE_LOC;
    ab_bar(s, x, y, labw, barw, "Code (.c)",
           CASTALIA_STAT_CODE_LOC, maxv, p);   y += lh + 4;
    ab_bar(s, x, y, labw, barw, "Headers",
           CASTALIA_STAT_HEADER_LOC, maxv, p); y += lh + 4;
    ab_bar(s, x, y, labw, barw, "Tests",
           CASTALIA_STAT_TEST_LOC, maxv, p);   y += lh + 8;

    /* The rest of the breakdown as a compact table. */
    ab_commafy(CASTALIA_STAT_LOC, num, sizeof num);
    sys_snprintf(buf, sizeof buf, "%s lines in src/ + include/", num);
    ab_kv(s, x, y, 78, "Source", buf, p); y += lh;
    sys_snprintf(buf, sizeof buf, "%d  (%d .c  +  %d .h)",
                 CASTALIA_STAT_FILES, CASTALIA_STAT_CFILES,
                 CASTALIA_STAT_HFILES);
    ab_kv(s, x, y, 78, "Files", buf, p); y += lh;
    sys_snprintf(buf, sizeof buf, "%d across %d modules",
                 CASTALIA_STAT_FUNCS, CASTALIA_STAT_MODULES);
    ab_kv(s, x, y, 78, "Functions", buf, p); y += lh;
    sys_snprintf(buf, sizeof buf, "%d TODO/FIXME marker(s) left",
                 CASTALIA_STAT_TODOS);
    ab_kv(s, x, y, 78, "Honesty", buf, p);
}

static void ab_panel_credits(GfxSurface *s, const CRect *pr, const UiPalette *p)
{
    static const char *layers[7][2] = {
        { "Platform",   "host / DOS-VESA seam" },
        { "System",     "log, memory, strings" },
        { "Graphics",   "software XRGB renderer" },
        { "Window Mgr", "frames, focus, z-order" },
        { "UI",         "controls & menus" },
        { "Shell",      "desktop & taskbar" },
        { "Apps",       "this About, and friends" }
    };
    int x = pr->x0 + 6;
    int y = pr->y0 + 6;
    int lh = gfx_font_height(GFX_FONT_SYSTEM) + 4;
    int i;

    gfx_draw_text(s, GFX_FONT_BOLD, x, y, "The Castalia Project", p->text);
    y += lh;
    gfx_draw_text(s, GFX_FONT_SYSTEM, x, y,
                  "by Dave Abellan & Claudio di Castello", p->accent);
    y += lh;
    gfx_draw_text(s, GFX_FONT_SYSTEM, x, y,
                  "Tombatossals Softworks  -  Built by hand, layer over layer:",
                  p->text_disabled);
    y += lh + 2;
    for (i = 0; i < 7; i++) {
        char idx[6];
        sys_snprintf(idx, sizeof idx, "%d", i + 1);
        gfx_draw_text(s, GFX_FONT_SYSTEM, x, y, idx, p->accent);
        gfx_draw_text(s, GFX_FONT_BOLD, x + 12, y, layers[i][0], p->text);
        gfx_draw_text(s, GFX_FONT_SYSTEM, x + 92, y, layers[i][1],
                      p->text_disabled);
        y += lh;
    }
    y += 4;
    gfx_draw_text(s, GFX_FONT_SYSTEM, x, y,
                  "Font: Spleen 5x8 by Frederic Cambus (BSD-2).", p->text);
    y += lh;
    gfx_draw_text(s, GFX_FONT_SYSTEM, x, y,
                  "Icons inspired by the Tango icon set.", p->text);
    y += lh + 2;
    gfx_draw_text(s, GFX_FONT_SYSTEM, x, y,
                  "Built with original code -- see LEGAL.md", p->text_disabled);
    y += lh;
    gfx_draw_text(s, GFX_FONT_SYSTEM, x, y,
                  "and THIRD_PARTY_NOTICES.md. Thank you!", p->text_disabled);
}

/* ---- paint ----------------------------------------------------------- */
static void ab_paint(WmWindow *win, GfxSurface *s)
{
    About *ab = (About *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    CColor white = GFX_RGB(0xFF, 0xFF, 0xFF);
    AbLayout L;
    CRect r, pr, footer;
    CRect c;
    int ch;

    if (ab == NULL) { return; }
    ab_layout(win, &L);
    c = wm_client_rect(win);
    ch = crect_h(&c);

    /* Body background. */
    r = crect_offset(&c, 0, 0);
    gfx_fill_rect(s, &r, p->face);

    /* Banner. */
    r = crect_offset(&L.banner, o.x, o.y);
    ab_paint_banner(s, &r, ab->phase);

    /* Tabs. */
    ab_paint_tabs(s, &L, o, ab->tab, p);

    /* Content well. */
    r = crect_offset(&L.panel, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);
    pr = crect_inset(&r, 6);
    switch (ab->tab) {
    case AB_HARDWARE: ab_panel_hardware(s, &pr, p); break;
    case AB_STATS:    ab_panel_stats(s, &pr, p);    break;
    case AB_CREDITS:  ab_panel_credits(s, &pr, p);  break;
    case AB_GENERAL:
    default:          ab_panel_general(s, &pr, p);  break;
    }

    /* Footer: etched divider + OK button. */
    footer = crect_make(o.x, o.y + ch - AB_FOOTER_H, crect_w(&c), 2);
    gfx_bevel(s, &footer, GFX_BEVEL_ETCHED, p->light, p->dark, GFX_NO_FILL);
    r = crect_offset(&L.ok, o.x, o.y);
    ui_draw_button(s, &r, "OK", ui_hot_state(&ab->hot, 0, UI_BTN_NORMAL));
}

/* ---- input ----------------------------------------------------------- */
static cbool ab_click(WmWindow *win, About *ab, int x, int y)
{
    AbLayout L;
    int i;

    ab_layout(win, &L);
    if (crect_contains(&L.ok, x, y)) {
        wm_destroy(win);
        return CTRUE;
    }
    for (i = 0; i < AB_TAB_COUNT; i++) {
        if (crect_contains(&L.tab[i], x, y)) {
            if (ab->tab != i) {
                ab->tab = i;
                wm_invalidate(win, NULL);
            }
            return CTRUE;
        }
    }
    return CFALSE;
}

static cbool ab_key(WmWindow *win, About *ab, int key, int ch)
{
    switch (key) {
    case PLAT_KEY_ESC:
    case PLAT_KEY_ENTER:
        wm_destroy(win);
        return CTRUE;
    case PLAT_KEY_LEFT:
        if (ab->tab > 0) { ab->tab--; wm_invalidate(win, NULL); }
        return CTRUE;
    case PLAT_KEY_RIGHT:
        if (ab->tab < AB_TAB_COUNT - 1) { ab->tab++; wm_invalidate(win, NULL); }
        return CTRUE;
    case PLAT_KEY_TAB:
        ab->tab = (ab->tab + 1) % AB_TAB_COUNT;
        wm_invalidate(win, NULL);
        return CTRUE;
    default:
        if (ch >= '1' && ch <= '4') {
            ab->tab = ch - '1';
            wm_invalidate(win, NULL);
            return CTRUE;
        }
        return CFALSE;
    }
}

static cbool about_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    About *ab = (About *)wm_user(win);
    switch (msg) {
    case WM_MSG_PAINT:       ab_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: return ab_click(win, ab, (int)a, (int)b);
    case WM_MSG_MOUSEMOVE: {
        AbLayout L;
        if (ab == NULL) { return CFALSE; }
        ab_layout(win, &L);
        (void)ui_hot_move(&ab->hot,
                          crect_contains(&L.ok, (int)a, (int)b) ? 0 : -1);
        return CTRUE;   /* the banner sheen repaints this window anyway */
    }
    case WM_MSG_MOUSELEAVE:
        if (ab != NULL) { (void)ui_hot_move(&ab->hot, -1); }
        return CTRUE;
    case WM_MSG_KEYDOWN:     return ab_key(win, ab, (int)a, (int)b);
    case WM_MSG_TIMER:
        if (ab != NULL) {
            ab->phase += 5;
            if (ab->phase > 0x3FFFFFF) { ab->phase = 0; }
            wm_invalidate(win, NULL);
        }
        return CTRUE;
    case WM_MSG_DESTROY:
        if (ab != NULL) { sys_free(ab, (cu32)sizeof(About)); }
        return CTRUE;
    default:
        return CFALSE;
    }
}

void app_about_open(void)
{
    About *ab;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int cw = 484, ch = 384, fx, fy;

    ab = (About *)sys_calloc(1, (cu32)sizeof(About));
    if (ab == NULL) { SYS_LOGE("app", "about: OOM"); return; }
    ab->tab = AB_GENERAL;

    plat_video_info(&vi);
    fx = (vi.width - cw) / 2;
    fy = (vi.height - ch) / 2;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, cw, ch);

    w = wm_create("About CastaliaOS 98 PE", &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE |
                  WM_STYLE_BORDER, about_proc, ab);
    if (w == NULL) { sys_free(ab, (cu32)sizeof(About)); return; }
    wm_set_animated(w, CTRUE);
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened About");
}
