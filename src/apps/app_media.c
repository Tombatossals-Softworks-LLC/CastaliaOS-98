/*
 * app_media.c - CastaliaOS Media Player (a Winamp-style WAV player).
 *
 * A dark-skinned player deck with a green LCD, a live visualizer (oscilloscope
 * or spectrum-style bars driven by the real decoded samples), transport
 * controls, a seek bar, volume/balance sliders, and a scrollable playlist. It
 * can add a single WAV or scan a whole folder (recursively) for WAVs.
 *
 * Audio is best-effort through the snd_pcm seam: on the host/null backend it is
 * silent, but the deck still plays -- the position advances on the wall clock
 * and the visualizer animates from the decoded PCM -- so the whole experience
 * is real and verifiable. WAV decoding + downsampling lives in wav.c.
 */
#include "apps.h"
#include "wav.h"
#include "eq_core.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/snd.h"
#include "castalia/sys.h"

#include <string.h>
#include <stdlib.h>

#define MP_MAX_TRACKS  128
#define MP_BARS        20
#define MP_SCAN_DEPTH  4
#define MP_DBLCLICK_MS 400
#define MP_PL_ROW_H    13
#define MP_TARGET_RATE 8000
#define MP_MAX_SAMPLES 1600000L   /* ~200 s at 8 kHz mono (~1.6 MB cap)     */

enum { MP_STOP = 0, MP_PLAY, MP_PAUSE };
/* Visualizer modes, cycled by clicking the panel. All are driven by the same
 * decoded samples -- none of them is a canned animation. */
enum { VIZ_BARS = 0, VIZ_MIRROR, VIZ_DOTS, VIZ_SCOPE, VIZ_COUNT };
static const char *const VIZ_NAME[VIZ_COUNT] = {
    "Bars", "Mirror", "Dots", "Scope"
};
enum { TB_PREV = 0, TB_PLAY, TB_PAUSE, TB_STOP, TB_NEXT, TB_OPEN, TB_N };
enum { PL_ADDFILE = 0, PL_ADDDIR, PL_REMOVE, PL_CLEAR, PL_N };

typedef struct {
    char  path[CASTALIA_MAX_PATH];
    char  name[64];
    cu32  ms;
    cbool probed;
    cbool ok;
    int   rate, channels, bits;
} MpTrack;

typedef struct {
    MpTrack   track[MP_MAX_TRACKS];
    int       count;
    int       sel;          /* selected playlist row                       */
    int       top;          /* first visible playlist row                  */
    int       cur;          /* loaded/playing track index, -1 = none       */
    int       state;        /* MP_STOP / MP_PLAY / MP_PAUSE                 */
    WavClip  *clip;         /* decoded current track                       */
    cu32      base_ms;      /* elapsed offset captured at 'origin_ms'       */
    cu32      origin_ms;    /* wall clock at the last play/seek            */
    int       viz;          /* VIZ_*                                        */
    int       volume;       /* 0..100                                      */
    int       balance;      /* -100..100                                   */
    cbool     show_remain;  /* LCD time shows remaining (-M:SS) not elapsed */
    cbool     shuffle;      /* random next-track order                      */
    int       repeat;       /* 0 none, 1 all, 2 one                         */
    cu32      rng;          /* simple LCG state for shuffle                 */
    int       frame;        /* animation tick counter                      */
    int       scroll;       /* LCD title scroll offset (px)                */
    cbool     title_scrolls;/* set by the painter: the name did not fit    */
    int       bar[MP_BARS]; /* smoothed bar heights                        */
    int       peak[MP_BARS];/* falling peak caps                           */
    cu32      last_click_ms;
    int       last_click_row;
    /* Playlist drag-to-reorder: arm on a row press, activate past a small
     * threshold, drop between rows on release. */
    int       drag_row;     /* grabbed source row, or -1                    */
    cbool     drag_active;  /* moved past the threshold                     */
    int       drag_down_y;  /* button-down y (client)                       */
    int       drag_y;       /* current cursor y (client)                    */
    char      status[80];
    int       eq[EQ_BANDS];   /* band gains; EQ_UNITY each = flat          */
    signed char *eq_buf;      /* the filtered clip actually handed to the
                               * mixer; NULL while the EQ is flat          */
    cu32      eq_alloc;
    WmWindow *self;
    UiHot hot;    /* playlist toolbar button under the pointer */
} Media;

/* ---- palette --------------------------------------------------------- */
#define MP_BG        GFX_RGB(0x20, 0x22, 0x2C)
#define MP_BG_HI     GFX_RGB(0x34, 0x36, 0x44)
#define MP_BG_LO     GFX_RGB(0x0E, 0x0E, 0x14)
#define MP_LCD_BG    GFX_RGB(0x07, 0x11, 0x09)
#define MP_LCD       GFX_RGB(0x33, 0xF0, 0x52)
#define MP_LCD_DIM   GFX_RGB(0x11, 0x5A, 0x24)
/*
 * The deck's own labels (VOL, BAL, LO/MID/HI) are NOT the LCD's dim green.
 *
 * They were, and MP_LCD_DIM is right where it is used inside the readout --
 * against (7,17,9), near black. The deck face behind these labels is
 * (35,37,48), and against that the same green measures 111 on the perceptual
 * scale tools/icon_contrast.py uses, where 110 is the line below which a
 * pixel stops reading as ink. The playlist text next to them measures 534.
 * One colour, two backgrounds, and only one of them was checked.
 *
 * Bright enough to read on the deck, still dimmer than the readout so the
 * LCD stays the thing your eye goes to.
 */
#define MP_DECK_LABEL GFX_RGB(0x2A, 0xB0, 0x40)
#define MP_PL_BG     GFX_RGB(0x0C, 0x0E, 0x16)
#define MP_PL_TEXT   GFX_RGB(0x66, 0xE0, 0x80)
#define MP_PL_SELBG  GFX_RGB(0x21, 0x3A, 0x6E)

/* ---- small helpers --------------------------------------------------- */
static void fmt_time(char *b, cu32 sz, cu32 ms)
{
    cu32 s = ms / 1000u;
    sys_snprintf(b, sz, "%lu:%02lu", (unsigned long)(s / 60u),
                 (unsigned long)(s % 60u));
}

static cu32 mp_duration(const Media *m)
{
    if (m->clip != NULL) { return m->clip->ms; }
    if (m->cur >= 0 && m->cur < m->count) { return m->track[m->cur].ms; }
    return 0;
}

static cu32 mp_elapsed(const Media *m)
{
    cu32 dur = mp_duration(m);
    cu32 e;
    if (m->state == MP_PLAY) { e = m->base_ms + (sys_now_ms() - m->origin_ms); }
    else                     { e = m->base_ms; }
    if (dur != 0 && e > dur) { e = dur; }
    return e;
}

/* ---- playlist management --------------------------------------------- */
static void mp_add_path(Media *m, const char *path)
{
    MpTrack *t;
    WavInfo wi;
    if (m->count >= MP_MAX_TRACKS) { return; }
    if (!wav_is_wav_name(path)) { return; }
    t = &m->track[m->count];
    sys_strlcpy(t->path, path, sizeof(t->path));
    sys_strlcpy(t->name, ui_path_base(path), sizeof(t->name));
    t->probed = wav_probe(path, &wi);
    t->ok = wi.valid;
    t->ms = wi.ms;
    t->rate = wi.rate; t->channels = wi.channels; t->bits = wi.bits;
    m->count++;
}

static void mp_scan_dir(Media *m, const char *dir, int depth)
{
    PlatDir *d;
    PlatDirEntry e;
    if (depth > MP_SCAN_DEPTH || m->count >= MP_MAX_TRACKS) { return; }
    d = plat_opendir(dir);
    if (d == NULL) { return; }
    while (plat_readdir(d, &e) && m->count < MP_MAX_TRACKS) {
        char child[CASTALIA_MAX_PATH];
        cu32 n;
        if (strcmp(e.name, ".") == 0 || strcmp(e.name, "..") == 0) { continue; }
        sys_strlcpy(child, dir, sizeof(child));
        n = sys_strnlen(child, sizeof(child));
        if (n == 0 || (child[n - 1] != '/' && child[n - 1] != '\\')) {
            sys_strlcat(child, "/", sizeof(child));
        }
        sys_strlcat(child, e.name, sizeof(child));
        if (e.is_dir) { mp_scan_dir(m, child, depth + 1); }
        else if (wav_is_wav_name(e.name)) { mp_add_path(m, child); }
    }
    plat_closedir(d);
}

/* ---- transport ------------------------------------------------------- */
static void mp_free_clip(Media *m)
{
    if (m->clip != NULL) { wav_free(m->clip); m->clip = NULL; }
}

static void mp_load(Media *m, int idx)
{
    mp_free_clip(m);
    m->cur = idx;
    if (idx < 0 || idx >= m->count) { return; }
    m->clip = wav_load_clip(m->track[idx].path, MP_TARGET_RATE,
                            (cu32)MP_MAX_SAMPLES);
    if (m->clip != NULL && m->track[idx].ms == 0) {
        m->track[idx].ms = m->clip->info.ms;
    }
}

static void mp_reset_viz(Media *m)
{
    int i;
    for (i = 0; i < MP_BARS; i++) { m->bar[i] = 0; m->peak[i] = 0; }
}

/* Free any filtered copy of the clip. */
static void mp_eq_drop(Media *m)
{
    if (m->eq_buf != NULL) {
        sys_free(m->eq_buf, m->eq_alloc);
        m->eq_buf = NULL;
        m->eq_alloc = 0;
    }
}

/*
 * The samples the mixer should play: the decoded clip when the equalizer is
 * flat, otherwise a filtered copy. Filtering a copy (rather than in place)
 * keeps the decode intact, so moving a slider re-filters from the original
 * instead of compounding the previous setting.
 */
static const signed char *mp_playable(Media *m)
{
    EqState st;
    if (m->clip == NULL) { return NULL; }
    if (eq_is_flat(m->eq)) { mp_eq_drop(m); return m->clip->mono; }
    if (m->eq_buf == NULL || m->eq_alloc < m->clip->count) {
        mp_eq_drop(m);
        m->eq_buf = (signed char *)sys_alloc(m->clip->count);
        if (m->eq_buf == NULL) { return m->clip->mono; }  /* no EQ, no lie */
        m->eq_alloc = m->clip->count;
    }
    eq_reset(&st);
    eq_process(&st, m->clip->mono, m->eq_buf, m->clip->count, m->eq);
    return m->eq_buf;
}

static void mp_play_index(Media *m, int idx)
{
    if (idx < 0 || idx >= m->count) { return; }
    if (m->cur != idx || m->clip == NULL) { mp_load(m, idx); }
    m->base_ms = 0;
    m->origin_ms = sys_now_ms();
    m->state = MP_PLAY;
    m->sel = idx;
    mp_reset_viz(m);
    if (m->clip != NULL) {
        snd_pcm_play(mp_playable(m), m->clip->count, m->clip->rate, 1, 8);
        sys_snprintf(m->status, sizeof(m->status), "Playing: %s",
                     m->track[idx].name);
    } else {
        sys_snprintf(m->status, sizeof(m->status), "Cannot decode: %s",
                     m->track[idx].name);
        m->state = MP_STOP;
    }
}

static void mp_play(Media *m)
{
    if (m->state == MP_PAUSE) {           /* resume */
        m->origin_ms = sys_now_ms();
        m->state = MP_PLAY;
        return;
    }
    if (m->count == 0) { return; }
    mp_play_index(m, (m->cur >= 0) ? m->cur : (m->sel >= 0 ? m->sel : 0));
}

static void mp_pause(Media *m)
{
    if (m->state == MP_PLAY) {
        m->base_ms = mp_elapsed(m);
        m->state = MP_PAUSE;
    } else if (m->state == MP_PAUSE) {
        m->origin_ms = sys_now_ms();
        m->state = MP_PLAY;
    }
}

static void mp_stop(Media *m)
{
    m->state = MP_STOP;
    m->base_ms = 0;
    snd_pcm_stop();
    mp_reset_viz(m);
}

/* A small LCG for shuffle (Math.random-free, deterministic and portable). */
static int mp_rand(Media *m, int n)
{
    if (n <= 1) { return 0; }
    m->rng = m->rng * 1103515245u + 12345u;
    return (int)((m->rng >> 16) % (cu32)n);
}

/* The track that follows 'from' given shuffle + wrap. */
static int mp_pick_next(Media *m, int from, cbool wrap)
{
    if (m->count == 0) { return -1; }
    if (m->shuffle && m->count > 1) {
        int r = mp_rand(m, m->count);
        if (r == from) { r = (r + 1) % m->count; }
        return r;
    }
    if (from + 1 < m->count) { return from + 1; }
    return wrap ? 0 : -1;
}

static void mp_step(Media *m, int dir)
{
    int base = (m->cur >= 0 ? m->cur : m->sel);
    int idx;
    if (m->count == 0) { return; }
    if (dir > 0)      { idx = mp_pick_next(m, base, CTRUE); }
    else if (m->shuffle) { idx = mp_rand(m, m->count); }
    else              { idx = (base > 0) ? base - 1 : m->count - 1; }
    if (idx < 0) { idx = 0; }
    if (m->state == MP_PLAY) { mp_play_index(m, idx); }
    else { m->sel = idx; m->cur = idx; mp_load(m, idx); m->base_ms = 0; }
}

/* ---- visualizer update (called each animation tick while playing) ----- */
/* Is the visualizer still settling? After a stop the bars and their peak caps
 * fall for a moment; until they are all down, the panel is still animating. */
static cbool mp_viz_active(const Media *m)
{
    int b;
    for (b = 0; b < MP_BARS; b++) {
        if (m->bar[b] > 0 || m->peak[b] > 0) { return CTRUE; }
    }
    return CFALSE;
}

static void mp_update_viz(Media *m, int viz_h)
{
    int b;
    if (m->state == MP_PLAY && m->clip != NULL && m->clip->count > 0) {
        cu32 idx = (mp_elapsed(m) / 1000u) * (cu32)m->clip->rate +
                   ((mp_elapsed(m) % 1000u) * (cu32)m->clip->rate) / 1000u;
        int win = m->clip->rate / 24; /* ~1/24 s window */
        int slice;
        if (win < MP_BARS) { win = MP_BARS; }
        slice = win / MP_BARS;
        if (slice < 1) { slice = 1; }
        for (b = 0; b < MP_BARS; b++) {
            int k, maxa = 0, h;
            for (k = 0; k < slice; k++) {
                cu32 si = idx + (cu32)(b * slice + k);
                int a;
                if (si >= m->clip->count) { break; }
                a = m->clip->mono[si];
                if (a < 0) { a = -a; }
                if (a > maxa) { maxa = a; }
            }
            h = (maxa * viz_h * (m->volume + 8)) / (128 * 108);
            if (h > viz_h) { h = viz_h; }
            /* fast attack, slow release */
            if (h >= m->bar[b]) { m->bar[b] = h; }
            else { m->bar[b] -= 1 + m->bar[b] / 10; if (m->bar[b] < h) m->bar[b] = h; }
            if (m->bar[b] > m->peak[b]) { m->peak[b] = m->bar[b]; }
            else if (m->peak[b] > 0) { m->peak[b] -= 1; }
        }
    } else {
        for (b = 0; b < MP_BARS; b++) {
            if (m->bar[b] > 0) { m->bar[b] -= 1 + m->bar[b] / 8; }
            if (m->bar[b] < 0) { m->bar[b] = 0; }
            if (m->peak[b] > m->bar[b]) { m->peak[b] -= 1; }
            if (m->peak[b] < 0) { m->peak[b] = 0; }
        }
    }
}

/* ---- layout ---------------------------------------------------------- */
typedef struct {
    CRect deck;         /* whole player deck                              */
    CRect lcd;          /* black display panel                           */
    CRect viz;          /* visualizer sub-rect (inside lcd)              */
    CRect timebox;      /* LCD time readout (click toggles remaining)     */
    CRect seek;         /* seek bar                                       */
    CRect tb[TB_N];     /* transport buttons                             */
    CRect shuf, rep;    /* shuffle / repeat toggles                       */
    CRect vol, bal;     /* sliders                                        */
    CRect eq[EQ_BANDS]; /* the three equalizer bands                      */
    CRect pltool;       /* playlist toolbar                              */
    CRect plbtn[PL_N];
    CRect list;         /* playlist well                                 */
    CRect status;
    int   visible;
} MpLayout;

static void mp_layout(WmWindow *win, MpLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c);
    int i, x, deck_h = 128, tool_y, list_y, list_h;
    L->deck = crect_make(0, 0, cw, deck_h);
    L->lcd  = crect_make(8, 8, cw - 16, 44);
    L->viz  = crect_make(L->lcd.x0 + 4, L->lcd.y0 + 4, 84, L->lcd.y1 - L->lcd.y0 - 8);
    L->timebox = crect_make(L->viz.x1 + 8, L->lcd.y0 + 4, 60, 16);
    L->seek = crect_make(8, 56, cw - 16, 9);
    x = 8;
    for (i = 0; i < TB_N; i++) {
        int w = (i == TB_OPEN) ? 30 : 26;
        if (i == TB_OPEN) { x += 8; }
        L->tb[i] = crect_make(x, 70, w, 22);
        x += w + 2;
    }
    L->shuf = crect_make(x + 6, 70, 42, 22);
    L->rep  = crect_make(x + 6 + 45, 70, 42, 22);
    L->vol = crect_make(38, 100, 100, 9);
    L->bal = crect_make(cw - 96, 100, 92, 9);
    {   /* LO / MID / HI, centred between the two existing sliders */
        int ex = 148, i;
        for (i = 0; i < EQ_BANDS; i++) {
            L->eq[i] = crect_make(ex + i * 30, 100, 24, 9);
        }
    }
    tool_y = deck_h;
    L->pltool = crect_make(0, tool_y, cw, 24);
    x = 4;
    {
        static const int W[PL_N] = { 62, 74, 58, 46 };
        for (i = 0; i < PL_N; i++) {
            L->plbtn[i] = crect_make(x, tool_y + 3, W[i], 18);
            x += W[i] + 3;
        }
    }
    list_y = tool_y + 24;
    list_h = ch - list_y - 18;
    if (list_h < MP_PL_ROW_H) { list_h = MP_PL_ROW_H; }
    L->list = crect_make(6, list_y, cw - 12, list_h);
    L->status = crect_make(0, ch - 18, cw, 18);
    L->visible = (crect_h(&L->list) - 2) / MP_PL_ROW_H;
}

/* ---- glyphs ---------------------------------------------------------- */
/*
 * One solid triangle, five columns wide, apex pointing right (dir > 0) or
 * left (dir < 0).
 *
 * This exists because the direction used to be open-coded three times as
 * `1 + 2 * i` -- a height that GROWS with x, so the apex lands on the LEFT --
 * and two of those three wanted to point the other way. The play button was
 * a backwards triangle and Next was drawn identically to Prev, both plainly
 * visible on screen for as long as the deck has existed. With the direction
 * as an argument there is one place to be right.
 */
static void mp_tri(GfxSurface *s, int x, int cy, int dir, CColor col)
{
    CRect t = crect_make(x, cy - 4, 5, 9);
    ui_draw_tri(s, &t, (dir > 0) ? UI_TRI_RIGHT : UI_TRI_LEFT, col);
}

static void mp_glyph(GfxSurface *s, const CRect *r, int id, CColor col)
{
    int cx = (r->x0 + r->x1) / 2, cy = (r->y0 + r->y1) / 2;
    switch (id) {
    case TB_PREV:                       /* |<<  bar on the left */
        gfx_vline(s, cx - 6, cy - 4, 8, col);
        mp_tri(s, cx - 4, cy, -1, col);
        mp_tri(s, cx + 1, cy, -1, col);
        break;
    case TB_NEXT:                       /* >>|  bar on the right */
        mp_tri(s, cx - 6, cy, +1, col);
        mp_tri(s, cx - 1, cy, +1, col);
        gfx_vline(s, cx + 5, cy - 4, 8, col);
        break;
    case TB_PLAY:                       /* >    centred on cx */
        mp_tri(s, cx - 2, cy, +1, col);
        break;
    case TB_PAUSE: {
        CRect a = crect_make(cx - 4, cy - 4, 3, 8);
        CRect b = crect_make(cx + 1, cy - 4, 3, 8);
        gfx_fill_rect(s, &a, col);
        gfx_fill_rect(s, &b, col);
        break;
    }
    case TB_STOP: {
        CRect sq = crect_make(cx - 4, cy - 4, 8, 8);
        gfx_fill_rect(s, &sq, col);
        break;
    }
    case TB_OPEN: {
        int i;
        for (i = 0; i < 5; i++) { gfx_hline(s, cx - i, cy - 3 + i, 1 + 2 * i, col); }
        gfx_hline(s, cx - 5, cy + 4, 11, col);
        break;
    }
    default: break;
    }
}

/* ---- painting -------------------------------------------------------- */
static void mp_draw_lcd(GfxSurface *s, Media *m, const MpLayout *L, int ox, int oy)
{
    CRect lcd = crect_offset(&L->lcd, ox, oy);
    CRect viz = crect_offset(&L->viz, ox, oy);
    cu32 el = mp_elapsed(m), dur = mp_duration(m);
    char tbuf[16], ibuf[40], nbuf[96];
    int tx, i, vh = crect_h(&viz), vw = crect_w(&viz);

    gfx_bevel(s, &lcd, GFX_BEVEL_SUNKEN, GFX_RGB(0x40,0x44,0x50),
              GFX_RGB(0x02,0x06,0x03), MP_LCD_BG);

    /* Visualizer panel. */
    gfx_frame_rect(s, &viz, GFX_RGB(0x0A,0x22,0x0E));
    if (m->viz == VIZ_BARS) {
        int bw = vw / MP_BARS;
        if (bw < 1) { bw = 1; }
        for (i = 0; i < MP_BARS; i++) {
            int h = m->bar[i]; int pk = m->peak[i];
            int bx = viz.x0 + i * bw;
            CRect col;
            if (h > vh) { h = vh; }
            if (h > 0) {
                col = crect_make(bx, viz.y1 - h, bw - 1 > 0 ? bw - 1 : 1, h);
                gfx_vgradient(s, &col, MP_LCD, GFX_RGB(0x14,0x74,0x24));
            }
            if (pk > vh) { pk = vh; }
            if (pk > 0) { gfx_hline(s, bx, viz.y1 - pk, bw - 1 > 0 ? bw - 1 : 1,
                                    GFX_RGB(0x9C,0xFF,0xB0)); }
        }
    } else if (m->viz == VIZ_MIRROR) {
        /* The same band levels, mirrored around the panel's midline. */
        int bw = vw / MP_BARS, mid = viz.y0 + vh / 2;
        if (bw < 1) { bw = 1; }
        for (i = 0; i < MP_BARS; i++) {
            int h = m->bar[i] / 2, bx = viz.x0 + i * bw;
            int w = (bw - 1 > 0) ? bw - 1 : 1;
            CRect col;
            if (h > vh / 2) { h = vh / 2; }
            if (h <= 0) { continue; }
            col = crect_make(bx, mid - h, w, h);
            gfx_vgradient(s, &col, GFX_RGB(0x14,0x74,0x24), MP_LCD);
            col = crect_make(bx, mid, w, h);
            gfx_vgradient(s, &col, MP_LCD, GFX_RGB(0x0C,0x44,0x16));
        }
        gfx_hline(s, viz.x0, mid, vw, GFX_RGB(0x0A,0x30,0x12));
    } else if (m->viz == VIZ_DOTS) {
        /* A dot-matrix column per band, plus the floating peak dot. */
        int bw = vw / MP_BARS, cell = 4;
        if (bw < 1) { bw = 1; }
        for (i = 0; i < MP_BARS; i++) {
            int h = m->bar[i], pk = m->peak[i];
            int bx = viz.x0 + i * bw, y;
            if (h > vh) { h = vh; }
            if (pk > vh) { pk = vh; }
            for (y = 0; y < h; y += cell) {
                CRect d = crect_make(bx, viz.y1 - y - (cell - 1),
                                     (bw - 2 > 0) ? bw - 2 : 1, cell - 2);
                gfx_fill_rect(s, &d, (y > (vh * 7) / 10)
                              ? GFX_RGB(0xD0,0xE0,0x40) : MP_LCD);
            }
            if (pk > 0) {
                CRect d = crect_make(bx, viz.y1 - pk - 1,
                                     (bw - 2 > 0) ? bw - 2 : 1, 2);
                gfx_fill_rect(s, &d, GFX_RGB(0x9C,0xFF,0xB0));
            }
        }
    } else {
        /* Oscilloscope: plot samples around the playhead. */
        int prevy = viz.y0 + vh / 2;
        if (m->clip != NULL && m->clip->count > 0) {
            cu32 idx = (el / 1000u) * (cu32)m->clip->rate +
                       ((el % 1000u) * (cu32)m->clip->rate) / 1000u;
            for (i = 0; i < vw; i++) {
                cu32 si = idx + (cu32)i;
                int val = (si < m->clip->count) ? m->clip->mono[si] : 0;
                int y = viz.y0 + vh / 2 - (val * (vh / 2) * (m->volume + 8)) / (128 * 108);
                if (y < viz.y0) { y = viz.y0; }
                if (y >= viz.y1) { y = viz.y1 - 1; }
                if (i > 0) { gfx_line(s, viz.x0 + i - 1, prevy, viz.x0 + i, y, MP_LCD); }
                prevy = y;
            }
        } else {
            gfx_hline(s, viz.x0, viz.y0 + vh / 2, vw, MP_LCD_DIM);
        }
    }

    /* Big time readout (elapsed, or -remaining when toggled). */
    if (m->show_remain && dur > el) {
        char tmp[12];
        fmt_time(tmp, sizeof(tmp), dur - el);
        sys_snprintf(tbuf, sizeof(tbuf), "-%s", tmp);
    } else {
        fmt_time(tbuf, sizeof(tbuf), el);
    }
    tx = viz.x1 + 10;
    gfx_draw_text(s, GFX_FONT_BOLD, tx, lcd.y0 + 6, tbuf, MP_LCD);

    /* Format / status line (with derived kbps). */
    if (m->cur >= 0 && m->cur < m->count && m->track[m->cur].ok) {
        MpTrack *t = &m->track[m->cur];
        int kbps = (t->rate * t->bits * t->channels) / 1000;
        sys_snprintf(ibuf, sizeof(ibuf), "%d kbps  %d Hz  %d-bit  %s", kbps,
                     t->rate, t->bits, t->channels == 2 ? "stereo" : "mono");
    } else {
        const char *st = (m->state == MP_PLAY) ? "playing"
                       : (m->state == MP_PAUSE) ? "paused" : "stopped";
        sys_snprintf(ibuf, sizeof(ibuf), "-- kbps  %s", st);
    }
    gfx_draw_text(s, GFX_FONT_SYSTEM, tx, lcd.y0 + 18, ibuf, MP_LCD_DIM);

    /* Scrolling title. */
    if (m->cur >= 0 && m->cur < m->count) {
        sys_snprintf(nbuf, sizeof(nbuf), "%d. %s", m->cur + 1, m->track[m->cur].name);
    } else {
        sys_strlcpy(nbuf, "CastaliaOS Media Player", sizeof(nbuf));
    }
    {
        int tw = gfx_text_width(GFX_FONT_SYSTEM, nbuf);
        int avail = lcd.x1 - tx - 6;
        CRect clip = crect_make(tx, lcd.y0 + 30, avail, 10);
        CRect old = gfx_clip_narrow(s, &clip);
        m->title_scrolls = (tw > avail) ? CTRUE : CFALSE;
        if (tw <= avail) {
            gfx_draw_text(s, GFX_FONT_SYSTEM, tx, lcd.y0 + 31, nbuf, MP_LCD);
        } else {
            int off = m->scroll % (tw + 24);
            gfx_draw_text(s, GFX_FONT_SYSTEM, tx - off, lcd.y0 + 31, nbuf, MP_LCD);
            gfx_draw_text(s, GFX_FONT_SYSTEM, tx - off + tw + 24, lcd.y0 + 31, nbuf, MP_LCD);
        }
        gfx_set_clip(s, &old);
    }
    (void)dur;
}

static void mp_draw_slider(GfxSurface *s, const CRect *r, int val, int lo, int hi)
{
    int span = hi - lo, w = crect_w(r);
    int tx = r->x0 + ((val - lo) * (w - 8)) / (span > 0 ? span : 1);
    CRect groove = crect_make(r->x0, (r->y0 + r->y1) / 2 - 1, w, 3);
    CRect thumb;
    gfx_bevel(s, &groove, GFX_BEVEL_SUNKEN_THIN, MP_BG_HI, MP_BG_LO, GFX_RGB(0x12,0x14,0x1C));
    thumb = crect_make(tx, r->y0, 8, crect_h(r));
    gfx_bevel(s, &thumb, GFX_BEVEL_RAISED, MP_BG_HI, MP_BG_LO, GFX_RGB(0x60,0x66,0x78));
}

/* Defined with the input handlers below; used by the playlist paint. */
static int mp_drop_index(Media *m, const MpLayout *L, int py);

static void mp_paint(WmWindow *win, GfxSurface *s)
{
    Media *m = (Media *)wm_user(win);
    CPoint o = wm_client_origin(win);
    CRect client = wm_client_rect(win);
    MpLayout L;
    CRect r;
    int i;
    if (m == NULL) { return; }
    mp_layout(win, &L);

    /* Deck background. */
    r = crect_offset(&client, 0, 0);
    r.x1 = r.x0 + crect_w(&client); r.y1 = r.y0 + crect_h(&client);
    gfx_fill_rect(s, &r, MP_BG);
    r = crect_offset(&L.deck, o.x, o.y);
    gfx_vgradient(s, &r, MP_BG_HI, MP_BG);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, MP_BG_HI, MP_BG_LO, GFX_NO_FILL);

    mp_draw_lcd(s, m, &L, o.x, o.y);

    /* Seek bar. */
    r = crect_offset(&L.seek, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, MP_BG_HI, MP_BG_LO, GFX_RGB(0x10,0x14,0x1A));
    {
        cu32 dur = mp_duration(m), el = mp_elapsed(m);
        if (dur > 0) {
            int fw = (int)(((long)el * (crect_w(&r) - 4)) / (long)dur);
            CRect fill = crect_make(r.x0 + 2, r.y0 + 2, fw, crect_h(&r) - 4);
            CRect knob;
            if (fw > 0) { gfx_vgradient(s, &fill, GFX_RGB(0x4C,0xB0,0xF0),
                                        GFX_RGB(0x1E,0x64,0xC4)); }
            knob = crect_make(r.x0 + 2 + fw - 2, r.y0, 4, crect_h(&r));
            gfx_bevel(s, &knob, GFX_BEVEL_RAISED_THIN, MP_BG_HI, MP_BG_LO,
                      GFX_RGB(0x80,0x88,0x9A));
        }
    }

    /* Transport buttons. */
    for (i = 0; i < TB_N; i++) {
        CRect b = crect_offset(&L.tb[i], o.x, o.y);
        int pressed = ((i == TB_PLAY && m->state == MP_PLAY) ||
                       (i == TB_PAUSE && m->state == MP_PAUSE));
        gfx_bevel(s, &b, pressed ? GFX_BEVEL_SUNKEN : GFX_BEVEL_RAISED,
                  MP_BG_HI, MP_BG_LO, GFX_RGB(0x3A,0x3E,0x4C));
        mp_glyph(s, &b, i, GFX_RGB(0xC8,0xEC,0xD0));
    }

    /* Shuffle + Repeat toggles (lit green when active). */
    {
        CRect sh = crect_offset(&L.shuf, o.x, o.y);
        CRect rp = crect_offset(&L.rep, o.x, o.y);
        char rl[8];
        gfx_bevel(s, &sh, m->shuffle ? GFX_BEVEL_SUNKEN : GFX_BEVEL_RAISED,
                  MP_BG_HI, MP_BG_LO, GFX_RGB(0x30,0x34,0x42));
        gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &sh, "SHUF",
                           m->shuffle ? MP_LCD : GFX_RGB(0x8A,0x92,0xA2),
                           GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
        gfx_bevel(s, &rp, (m->repeat != 0) ? GFX_BEVEL_SUNKEN : GFX_BEVEL_RAISED,
                  MP_BG_HI, MP_BG_LO, GFX_RGB(0x30,0x34,0x42));
        sys_snprintf(rl, sizeof(rl), "REP%s", m->repeat == 2 ? "1" :
                                              m->repeat == 1 ? "*" : "");
        gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &rp, rl,
                           (m->repeat != 0) ? MP_LCD : GFX_RGB(0x8A,0x92,0xA2),
                           GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
    }

    /* Volume + balance sliders, with labels to the left. */
    r = crect_offset(&L.vol, o.x, o.y);
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 - 30, r.y0 + 1, "VOL", MP_DECK_LABEL);
    mp_draw_slider(s, &r, m->volume, 0, 100);
    r = crect_offset(&L.bal, o.x, o.y);
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 - 30, r.y0 + 1, "BAL", MP_DECK_LABEL);
    mp_draw_slider(s, &r, m->balance, -100, 100);

    /* The equalizer: three bands between the volume and balance sliders.
     * A band at the centre is unity, so a flat row is a flat response. */
    {
        static const char *const EQ_LABEL[EQ_BANDS] = { "LO", "MID", "HI" };
        int b;
        for (b = 0; b < EQ_BANDS; b++) {
            CRect e = crect_offset(&L.eq[b], o.x, o.y);
            /*
             * Eight pixels up, not ten. The transport buttons occupy rows 70
             * to 91 and the sliders start at 100, leaving exactly eight rows
             * for an eight-pixel label -- at -10 the tops of LO, MID and HI
             * were drawn over the bottom of the SHUF and REP buttons. Two
             * rows of green inside a grey button is not something anyone
             * would report; it just makes the deck look slightly soft.
             */
            gfx_draw_text(s, GFX_FONT_SYSTEM, e.x0 + 2, e.y0 - 8,
                          EQ_LABEL[b], MP_DECK_LABEL);
            mp_draw_slider(s, &e, m->eq[b], 0, EQ_GAIN_MAX);
        }
    }

    /* Playlist toolbar. */
    r = crect_offset(&L.pltool, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, MP_BG_HI, MP_BG_LO, MP_BG);
    {
        static const char *PL_LABEL[PL_N] = { "Add File", "Add Folder",
                                              "Remove", "Clear" };
        for (i = 0; i < PL_N; i++) {
            CRect b = crect_offset(&L.plbtn[i], o.x, o.y);
            ui_draw_button(s, &b, PL_LABEL[i],
                           ui_hot_state(&m->hot, i, UI_BTN_NORMAL));
        }
    }

    /* Playlist. */
    r = crect_offset(&L.list, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN, MP_BG_HI, MP_BG_LO, MP_PL_BG);
    for (i = 0; i < L.visible; i++) {
        int idx = m->top + i;
        int ry = r.y0 + 2 + i * MP_PL_ROW_H;
        MpTrack *t;
        CColor tcol = MP_PL_TEXT;
        char row[96], dur[12];
        if (idx >= m->count) { break; }
        t = &m->track[idx];
        if (idx == m->sel) {
            CRect hl = crect_make(r.x0 + 1, ry - 1, crect_w(&r) - 2, MP_PL_ROW_H);
            gfx_fill_rect(s, &hl, MP_PL_SELBG);
            tcol = GFX_RGB(0xE6,0xF2,0xFF);
        }
        if (idx == m->cur) {
            gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 3, ry,
                          (m->state == MP_PLAY) ? ">" : "=", MP_LCD);
        }
        sys_snprintf(row, sizeof(row), "%d. %s", idx + 1, t->name);
        gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 14, ry, row,
                      t->ok ? tcol : GFX_RGB(0x99,0x55,0x55));
        if (t->ms > 0) {
            fmt_time(dur, sizeof(dur), t->ms);
            gfx_draw_text(s, GFX_FONT_SYSTEM, r.x1 - 34, ry, dur, tcol);
        }
    }
    if (m->count == 0) {
        gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 8, r.y0 + 8,
                      "Playlist empty -- use Add File or Add Folder.",
                      GFX_RGB(0x55,0x77,0x60));
    }
    /* Drag-to-reorder: an insertion line at the drop gap plus a floating ghost
     * of the grabbed row's name following the cursor. */
    if (m->drag_active && m->drag_row >= 0 && m->drag_row < m->count) {
        int to = mp_drop_index(m, &L, m->drag_y);
        int liney = r.y0 + 2 + (to - m->top) * MP_PL_ROW_H - 1;
        int gy = r.y0 + m->drag_y - L.list.y0 - MP_PL_ROW_H / 2;
        CRect gbar;
        char gname[96];
        if (liney < r.y0 + 1) { liney = r.y0 + 1; }
        if (liney > r.y1 - 2) { liney = r.y1 - 2; }
        gfx_hline(s, r.x0 + 2, liney, crect_w(&r) - 4, MP_LCD);
        gfx_hline(s, r.x0 + 2, liney + 1, crect_w(&r) - 4, MP_LCD);
        if (gy < r.y0 + 1) { gy = r.y0 + 1; }
        if (gy > r.y1 - MP_PL_ROW_H - 1) { gy = r.y1 - MP_PL_ROW_H - 1; }
        gbar = crect_make(r.x0 + 2, gy, crect_w(&r) - 4, MP_PL_ROW_H);
        gfx_fill_rect(s, &gbar, MP_PL_SELBG);
        gfx_frame_rect(s, &gbar, MP_LCD);
        sys_snprintf(gname, sizeof(gname), "%d. %s", m->drag_row + 1,
                     m->track[m->drag_row].name);
        gfx_draw_text(s, GFX_FONT_SYSTEM, gbar.x0 + 12, gbar.y0 + 3, gname,
                      GFX_RGB(0xE6,0xF2,0xFF));
    }

    /* Status bar. */
    r = crect_offset(&L.status, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, MP_BG_HI, MP_BG_LO, MP_BG);
    {
        CRect tr = r; char cnt[24];
        tr.x0 += 6;
        gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &tr, m->status,
                           GFX_RGB(0xB0,0xC0,0xC8), GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);
        sys_snprintf(cnt, sizeof(cnt), "%d track(s)", m->count);
        gfx_draw_text(s, GFX_FONT_SYSTEM, r.x1 - 68, r.y0 + 5, cnt,
                      GFX_RGB(0x80,0x90,0x98));
    }
}

/* ---- dialog callbacks ------------------------------------------------ */
static void on_add_file(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    Media *m = (Media *)wm_user(win);
    if (ok && m != NULL && text[0] != '\0') {
        int before = m->count;
        mp_add_path(m, text);
        if (m->count > before) {
            sys_snprintf(m->status, sizeof(m->status), "Added %s",
                         ui_path_base(text));
        } else {
            sys_snprintf(m->status, sizeof(m->status), "Not a WAV: %s",
                         ui_path_base(text));
        }
        wm_invalidate(win, NULL);
    }
}

static void on_add_dir(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    Media *m = (Media *)wm_user(win);
    if (ok && m != NULL && text[0] != '\0') {
        int before = m->count;
        mp_scan_dir(m, text, 0);
        sys_snprintf(m->status, sizeof(m->status), "Added %d WAV(s) from %s",
                     m->count - before, ui_path_base(text));
        wm_invalidate(win, NULL);
    }
}

static void mp_pl_action(WmWindow *win, Media *m, int id)
{
    const char *home;
    switch (id) {
    case PL_ADDFILE:
        ui_file_dialog("Add File", NULL, "WAV", CFALSE, "", on_add_file, win);
        break;
    case PL_ADDDIR:
        home = sys_home();
        ui_prompt("Add Folder", "Folder to scan for WAVs:", home, on_add_dir, win);
        break;
    case PL_REMOVE:
        if (m->sel >= 0 && m->sel < m->count) {
            int i;
            if (m->sel == m->cur) { mp_stop(m); mp_free_clip(m); m->cur = -1; }
            for (i = m->sel; i < m->count - 1; i++) { m->track[i] = m->track[i + 1]; }
            m->count--;
            if (m->cur > m->sel) { m->cur--; }
            if (m->sel >= m->count) { m->sel = m->count - 1; }
            wm_invalidate(win, NULL);
        }
        break;
    case PL_CLEAR:
        mp_stop(m); mp_free_clip(m);
        m->count = 0; m->sel = 0; m->top = 0; m->cur = -1;
        sys_strlcpy(m->status, "Playlist cleared", sizeof(m->status));
        wm_invalidate(win, NULL);
        break;
    default: break;
    }
}

/* ---- input ----------------------------------------------------------- */
static void mp_click(WmWindow *win, Media *m, int px, int py)
{
    MpLayout L;
    int i;
    mp_layout(win, &L);

    for (i = 0; i < TB_N; i++) {
        if (crect_contains(&L.tb[i], px, py)) {
            switch (i) {
            case TB_PREV:  mp_step(m, -1); break;
            case TB_PLAY:  mp_play(m); break;
            case TB_PAUSE: mp_pause(m); break;
            case TB_STOP:  mp_stop(m); break;
            case TB_NEXT:  mp_step(m, +1); break;
            case TB_OPEN:  mp_pl_action(win, m, PL_ADDFILE); return;
            default: break;
            }
            wm_invalidate(win, NULL);
            return;
        }
    }
    for (i = 0; i < PL_N; i++) {
        if (crect_contains(&L.plbtn[i], px, py)) {
            (void)ui_hot_press(&m->hot, i);
            mp_pl_action(win, m, i);
            return;
        }
    }
    if (crect_contains(&L.shuf, px, py)) {
        m->shuffle = !m->shuffle;
        sys_snprintf(m->status, sizeof(m->status), "Shuffle %s",
                     m->shuffle ? "on" : "off");
        wm_invalidate(win, NULL);
        return;
    }
    if (crect_contains(&L.rep, px, py)) {
        m->repeat = (m->repeat + 1) % 3;
        sys_snprintf(m->status, sizeof(m->status), "Repeat: %s",
                     m->repeat == 2 ? "one" : m->repeat == 1 ? "all" : "off");
        wm_invalidate(win, NULL);
        return;
    }
    if (crect_contains(&L.timebox, px, py)) {
        m->show_remain = !m->show_remain;
        wm_invalidate(win, NULL);
        return;
    }
    if (crect_contains(&L.viz, px, py)) {
        m->viz = (m->viz + 1) % VIZ_COUNT;
        /* Say which one you landed on -- otherwise clicking the panel is a
         * mystery box. */
        sys_snprintf(m->status, sizeof(m->status), "Visualizer: %s",
                     VIZ_NAME[m->viz]);
        wm_invalidate(win, NULL);
        return;
    }
    if (crect_contains(&L.seek, px, py)) {
        cu32 dur = mp_duration(m);
        if (dur > 0) {
            int rel = px - L.seek.x0 - 2, w = crect_w(&L.seek) - 4;
            if (rel < 0) { rel = 0; }
            if (rel > w) { rel = w; }
            m->base_ms = (cu32)(((long)rel * (long)dur) / (w > 0 ? w : 1));
            m->origin_ms = sys_now_ms();
        }
        wm_invalidate(win, NULL);
        return;
    }
    if (crect_contains(&L.vol, px, py)) {
        int rel = px - L.vol.x0, w = crect_w(&L.vol);
        m->volume = (rel * 100) / (w > 0 ? w : 1);
        if (m->volume < 0) { m->volume = 0; }
        if (m->volume > 100) { m->volume = 100; }
        wm_invalidate(win, NULL);
        return;
    }
    {   /* the equalizer bands */
        int b;
        for (b = 0; b < EQ_BANDS; b++) {
            if (crect_contains(&L.eq[b], px, py)) {
                int rel = px - L.eq[b].x0, w = crect_w(&L.eq[b]);
                m->eq[b] = (rel * EQ_GAIN_MAX) / (w > 0 ? w : 1);
                if (m->eq[b] < 0) { m->eq[b] = 0; }
                if (m->eq[b] > EQ_GAIN_MAX) { m->eq[b] = EQ_GAIN_MAX; }
                /* Re-filter from the decode and hand the mixer the result, so
                 * the change is audible now rather than at the next track. */
                if (m->state == MP_PLAY && m->clip != NULL) {
                    snd_pcm_play(mp_playable(m), m->clip->count,
                                 m->clip->rate, 1, 8);
                }
                sys_snprintf(m->status, sizeof(m->status),
                             eq_is_flat(m->eq) ? "Equalizer: flat"
                                               : "Equalizer: %s %d%%",
                             (b == 0) ? "low" : (b == 1) ? "mid" : "high",
                             (m->eq[b] * 100) / EQ_UNITY);
                wm_invalidate(win, NULL);
                return;
            }
        }
    }
    if (crect_contains(&L.bal, px, py)) {
        int rel = px - L.bal.x0, w = crect_w(&L.bal);
        m->balance = (rel * 200) / (w > 0 ? w : 1) - 100;
        if (m->balance < -100) { m->balance = -100; }
        if (m->balance > 100) { m->balance = 100; }
        wm_invalidate(win, NULL);
        return;
    }
    if (crect_contains(&L.list, px, py)) {
        int row = m->top + (py - L.list.y0 - 2) / MP_PL_ROW_H;
        if (row >= 0 && row < m->count) {
            cu32 now = sys_now_ms();
            cbool dbl = (row == m->last_click_row &&
                         (now - m->last_click_ms) < MP_DBLCLICK_MS) ? CTRUE : CFALSE;
            m->sel = row;
            m->last_click_row = row;
            m->last_click_ms = now;
            /* Arm a drag-to-reorder from this row (activates only on motion). */
            m->drag_row = row;
            m->drag_active = CFALSE;
            m->drag_down_y = py;
            m->drag_y = py;
            if (dbl) { mp_play_index(m, row); }
            wm_invalidate(win, NULL);
        }
        return;
    }
}

/* Row index (0..count) the cursor y currently points between, for a drop. */
static int mp_drop_index(Media *m, const MpLayout *L, int py)
{
    int rel = py - (L->list.y0 + 2) + MP_PL_ROW_H / 2;
    int idx = m->top + (rel / MP_PL_ROW_H);
    if (idx < 0) { idx = 0; }
    if (idx > m->count) { idx = m->count; }
    return idx;
}

static void mp_pl_motion(WmWindow *win, Media *m, int py)
{
    if (m->drag_row < 0) { return; }
    m->drag_y = py;
    if (!m->drag_active) {
        int dy = py - m->drag_down_y;
        if (dy < 0) { dy = -dy; }
        if (dy <= 4) { return; }
        m->drag_active = CTRUE;
    }
    wm_invalidate(win, NULL);
}

static void mp_pl_up(WmWindow *win, Media *m, int py)
{
    MpLayout L;
    if (m->drag_row < 0) { return; }
    if (m->drag_active) {
        int from = m->drag_row, to;
        mp_layout(win, &L);
        to = mp_drop_index(m, &L, py);
        /* Removing 'from' shifts everything after it up by one. */
        if (to > from) { to--; }
        if (to != from && from >= 0 && from < m->count &&
            to >= 0 && to < m->count) {
            MpTrack moved = m->track[from];
            int i;
            if (to > from) {
                for (i = from; i < to; i++) { m->track[i] = m->track[i + 1]; }
            } else {
                for (i = from; i > to; i--) { m->track[i] = m->track[i - 1]; }
            }
            m->track[to] = moved;
            /* Keep the playing and selected indices pinned to their tracks. */
            if (m->cur == from) { m->cur = to; }
            else if (from < m->cur && to >= m->cur) { m->cur--; }
            else if (from > m->cur && to <= m->cur) { m->cur++; }
            m->sel = to;
            sys_snprintf(m->status, sizeof(m->status), "Moved track to #%d",
                         to + 1);
        }
        wm_invalidate(win, NULL);
    }
    m->drag_row = -1;
    m->drag_active = CFALSE;
}

static void mp_scroll_to_sel(Media *m, int visible)
{
    if (visible < 1) { visible = 1; }
    if (m->sel < m->top) { m->top = m->sel; }
    if (m->sel >= m->top + visible) { m->top = m->sel - visible + 1; }
    if (m->top < 0) { m->top = 0; }
}

static cbool mp_key(WmWindow *win, Media *m, int key, int ch)
{
    MpLayout L;
    mp_layout(win, &L);
    switch (key) {
    case PLAT_KEY_UP:    if (m->sel > 0) { m->sel--; } mp_scroll_to_sel(m, L.visible); break;
    case PLAT_KEY_DOWN:  if (m->sel < m->count - 1) { m->sel++; } mp_scroll_to_sel(m, L.visible); break;
    case PLAT_KEY_ENTER: mp_play_index(m, m->sel); break;
    case PLAT_KEY_DELETE: mp_pl_action(win, m, PL_REMOVE); return CTRUE;
    default:
        if (ch == ' ') { mp_pause(m); break; }
        if (ch == 'z' || ch == 'Z') { mp_step(m, -1); break; }
        if (ch == 'b' || ch == 'B') { mp_step(m, +1); break; }
        if (ch == 'x' || ch == 'X') { mp_play(m); break; }
        if (ch == 'v' || ch == 'V') { mp_stop(m); break; }
        if (ch == 's' || ch == 'S') { m->shuffle = !m->shuffle; break; }
        if (ch == 'r' || ch == 'R') { m->repeat = (m->repeat + 1) % 3; break; }
        return CFALSE;
    }
    wm_invalidate(win, NULL);
    return CTRUE;
}

/* ---- animation tick -------------------------------------------------- */
static void mp_tick(WmWindow *win, Media *m)
{
    MpLayout L;
    mp_layout(win, &L);
    m->frame++;
    m->scroll += 1;
    /* Auto-advance at end of track, honoring repeat + shuffle. */
    if (m->state == MP_PLAY) {
        cu32 dur = mp_duration(m);
        if (dur > 0 && mp_elapsed(m) >= dur) {
            if (m->repeat == 2) {           /* repeat one */
                mp_play_index(m, m->cur);
            } else {
                int nx = mp_pick_next(m, m->cur, (m->repeat == 1) ? CTRUE : CFALSE);
                if (nx >= 0) { mp_play_index(m, nx); }
                else { mp_stop(m); sys_strlcpy(m->status, "End of playlist",
                                               sizeof(m->status)); }
            }
        }
    }
    mp_update_viz(m, crect_h(&L.viz) - 4);

    /*
     * Only the deck's display moves on a tick: the visualizer, the scrolling
     * title, the elapsed readout and the seek head all live between the top
     * of the LCD and the bottom of the seek bar. The transport buttons, the
     * sliders, the equalizer and the playlist below them do not move at all,
     * and repainting them sixty times a second to animate a strip of bars
     * cost about 620,000 filled pixels per frame -- more than the whole
     * screen -- whether or not anything was playing.
     *
     * And when nothing is moving, nothing is invalidated. Stopped, with the
     * bars decayed to zero and a title short enough to sit still, the window
     * is genuinely static and should cost what a static window costs.
     */
    if (m->state != MP_PLAY && !m->title_scrolls && !mp_viz_active(m)) {
        return;
    }
    {
        CPoint o = wm_client_origin(win);
        CRect display = crect_make_xyxy(L.lcd.x0, L.lcd.y0, L.lcd.x1, L.seek.y1);
        display = crect_inset(&display, -2);
        display = crect_offset(&display, o.x, o.y);
        wm_invalidate(win, &display);
    }
}

static cbool media_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    Media *m = (Media *)wm_user(win);
    switch (msg) {
    case WM_MSG_PAINT:       mp_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: mp_click(win, m, (int)a, (int)b); return CTRUE;
    case WM_MSG_MOUSEMOVE: {
        MpLayout L;
        int i, hit = -1;
        mp_pl_motion(win, m, (int)b);
        if (m == NULL) { return CTRUE; }
        mp_layout(win, &L);
        for (i = 0; i < PL_N; i++) {
            if (crect_contains(&L.plbtn[i], (int)a, (int)b)) { hit = i; }
        }
        if (ui_hot_move(&m->hot, hit)) {
            ui_hot_repaint(win, &m->hot, L.plbtn, PL_N);
        }
        return CTRUE;
    }
    case WM_MSG_LBUTTONUP:
        mp_pl_up(win, m, (int)b);
        if (m != NULL && ui_hot_release(&m->hot)) {
            MpLayout L;
            mp_layout(win, &L);
            ui_hot_repaint(win, &m->hot, L.plbtn, PL_N);
        }
        return CTRUE;
    case WM_MSG_MOUSELEAVE:
        if (m != NULL) {
            cbool redraw = ui_hot_release(&m->hot);
            MpLayout L;
            if (ui_hot_move(&m->hot, -1)) { redraw = CTRUE; }
            mp_layout(win, &L);
            if (redraw) { ui_hot_repaint(win, &m->hot, L.plbtn, PL_N); }
        }
        return CTRUE;
    case WM_MSG_MOUSEWHEEL: {
        MpLayout L;
        if (m == NULL) { return CFALSE; }
        mp_layout(win, &L);
        if (ui_scroll_wheel(&m->top, (int)a, m->count, L.visible)) {
            CPoint o = wm_client_origin(win);
            CRect r = crect_offset(&L.list, o.x, o.y);
            wm_invalidate(win, &r);
        }
        return CTRUE;
    }
    case WM_MSG_KEYDOWN:     return mp_key(win, m, (int)a, (int)b);
    case WM_MSG_TIMER:       mp_tick(win, m); return CTRUE;
    case WM_MSG_DESTROY:
        if (m != NULL) {
            mp_eq_drop(m);
            mp_free_clip(m);
            sys_free(m, (cu32)sizeof(Media));
        }
        return CTRUE;
    default: return CFALSE;
    }
}

static WmWindow *mp_open_window(void)
{
    Media *m;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int cw = 380, ch = 430;

    m = (Media *)sys_calloc(1, (cu32)sizeof(Media));
    if (m == NULL) { SYS_LOGE("app", "media: OOM"); return NULL; }
    m->cur = -1;
    m->drag_row = -1;
    m->sel = 0;
    m->last_click_row = -1;
    m->volume = 78;
    m->balance = 0;
    m->viz = VIZ_BARS;
    { int i; for (i = 0; i < EQ_BANDS; i++) { m->eq[i] = EQ_UNITY; } }
    m->rng = sys_now_ms() | 1u;
    sys_strlcpy(m->status, "Ready", sizeof(m->status));

    /* Seed the playlist from a MEDIA folder under CASTALIA_HOME, if present. */
    {
        char media[CASTALIA_MAX_PATH];
        sys_home_path(media, (cu32)sizeof(media), "MEDIA");
        mp_scan_dir(m, media, 0);
        if (m->count > 0) {
            sys_snprintf(m->status, sizeof(m->status), "%d track(s) in MEDIA",
                         m->count);
        }
    }

    plat_video_info(&vi);
    frame = wm_place_centered(cw, ch);   /* the work area, not the screen */

    w = wm_create("Media Player", &frame, WM_STYLE_APP, media_proc, m);
    if (w == NULL) { sys_free(m, (cu32)sizeof(Media)); return NULL; }
    m->self = w;
    wm_show(w, CTRUE);
    wm_set_animated(w, CTRUE);   /* live visualizer + seek + scroll */
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened Media Player (%d tracks)", m->count);
    return w;
}

void app_media_open(void) { (void)mp_open_window(); }

/* Open the player on one sound and start it -- what a double-click on a WAV
 * does. The file joins the playlist rather than replacing it. */
void app_media_open_file(const char *path)
{
    WmWindow *w = mp_open_window();
    Media *m = (w != NULL) ? (Media *)wm_user(w) : NULL;
    int before;
    if (m == NULL || path == NULL || path[0] == '\0') { return; }
    before = m->count;
    mp_add_path(m, path);
    if (m->count > before) {
        m->cur = m->count - 1;
        mp_play(m);
        sys_snprintf(m->status, sizeof(m->status), "Playing %s",
                     ui_path_base(path));
    } else {
        sys_snprintf(m->status, sizeof(m->status), "Not a WAV: %s",
                     ui_path_base(path));
    }
    wm_invalidate(w, NULL);
}

/*
 * The transport button band in CLIENT coordinates, for --repaint-demo.
 *
 * The equalizer labels were drawn two pixel rows into the bottom of the SHUF
 * and REP buttons. Nobody would report that -- it reads as the deck looking
 * slightly soft -- and it is invisible in a side-by-side comparison, which is
 * how it survived being looked at. So the scene counts label-coloured pixels
 * inside this band instead, where the answer is a number.
 */
cbool app_media_transport_rect(WmWindow *win, CRect *out)
{
    MpLayout L;
    if (win == NULL || out == NULL || wm_user(win) == NULL) { return CFALSE; }
    mp_layout(win, &L);
    *out = crect_make(L.tb[0].x0, L.tb[0].y0,
                      L.rep.x1 - L.tb[0].x0, crect_h(&L.tb[0]));
    return CTRUE;
}

/* The colour the deck draws its dim labels in, so the scene can look for it
 * without a second copy of the constant. */
/*
 * The colour the DECK's labels are drawn in -- which --repaint-demo hunts for
 * inside the transport buttons. It must track whatever mp_paint actually uses:
 * when the labels moved off MP_LCD_DIM this still answered MP_LCD_DIM, and the
 * scene went looking for a colour nothing was drawn in any more. It reported
 * zero spill, which is what it reports when everything is right.
 */
CColor app_media_label_color(void) { return MP_DECK_LABEL; }
/* ...and the face they are drawn ON, so a scene can ask whether the two are
 * far enough apart to read rather than trusting that somebody looked. */
CColor app_media_deck_color(void) { return MP_BG; }
