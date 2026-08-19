/*
 * app_bench.c - CastaliaOS Benchmark Suite ("CastaliaMark").
 *
 * A compact, spectacular benchmark window that runs REAL micro-benchmarks --
 * integer CPU throughput, memory copy/fill bandwidth, software fill-rate and
 * vector-line rate, and a checksum pass -- then reveals each result as an
 * animated bar meter and folds them into one prominent composite score, the
 * CastaliaMark. The tests do genuine work against a volatile sink so the
 * compiler cannot elide them, each sized to run in a few to a few tens of
 * milliseconds on a fast host while still producing a meaningful rate.
 *
 * Everything runs synchronously on the Run All click (or Enter); the payload's
 * per-frame animation tick then grows the bars in a staggered cascade and
 * sweeps a soft sheen across the score panel.
 */
#include "apps.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#include <string.h> /* memcpy for the memory-bandwidth test */

/* ---- layout metrics -------------------------------------------------- */
#define BM_TOOLBAR_H 28
#define BM_SCORE_H   56
#define BM_ROW_H     34
#define BM_STATUS_H  16

/* ---- benchmark identity ---------------------------------------------- */
enum { BM_CPU = 0, BM_MEMCPY, BM_MEMFILL, BM_GFXFILL, BM_GFXLINE, BM_TEXT,
       BM_HASH, NBENCH };

static const char *BM_NAME[NBENCH] = {
    "Integer CPU", "Memory Copy", "Memory Fill",
    "Graphics Fill", "Vector Lines", "Text Render", "Hash / Checksum"
};
static const char *BM_UNIT[NBENCH] = {
    "Mops/s", "MB/s", "MB/s", "Mpix/s", "K lines/s", "K glyphs/s", "MB/s"
};
/* Per-test reference: a result equal to this scores ~1000 points. Chosen so a
 * modern host lands the composite CastaliaMark in the thousands. */
static const long BM_REF[NBENCH] = {
    800L, 4000L, 6000L, 700L, 700L, 9000L, 1500L
};

/* ---- workload sizing (real work; a few..tens of ms on a fast host) --- */
#define MEM_SIZE      (1024L * 1024L) /* 1 MB scratch buffer            */
#define CPU_ITERS     20000000L       /* integer ops loop iterations    */
#define MEMCPY_ITERS  200L            /* 200 MB copied total            */
#define MEMFILL_ITERS 256L            /* 256 MB filled total            */
#define GFXFILL_ITERS 800L            /* full-surface rectangle fills   */
#define GFXLINE_ITERS 60000L          /* diagonal lines drawn           */
#define TEXT_ITERS    8000L           /* text lines rendered            */
#define HASH_ITERS    100L            /* 100 MB hashed total            */
#define GFX_W 320
#define GFX_H 240

/* ---- animation / scoring tunables ------------------------------------ */
#define BENCH_ANIM_MAX    14
#define BENCH_STAGGER     2
#define BENCH_REVEAL_MAX  (BENCH_ANIM_MAX + NBENCH * BENCH_STAGGER)
#define BENCH_SHEEN_PERIOD 120
#define BENCH_BAR_FULL    6000L /* a score of 6000 fills a bar to 100%  */

typedef struct {
    long value;  /* rate in the row's unit                             */
    long score;  /* normalized score (~1000 == reference)              */
    long refmax; /* the reference constant that scores ~1000 points    */
} BenchRow;

typedef struct {
    WmWindow *self;
    cbool     ran;
    BenchRow  row[NBENCH];
    long      mark;   /* composite CastaliaMark                        */
    int       reveal; /* bar-reveal animation phase                    */
    int       sheen;  /* free-running sheen sweep phase                */
    char      status[64];
    UiHot     hot;    /* the Run All button, under the pointer or held */
} Bench;

/* A volatile sink every benchmark feeds so the optimizer keeps the work. */
static volatile long g_bench_sink;

/* ---- real micro-benchmarks ------------------------------------------- */
/* Each returns a rate in its unit; dt is guarded against zero. */

static long bench_cpu(void)
{
    volatile long acc = 123456789L;
    long i, dt, ops;
    cu32 t0;
    t0 = plat_ticks_ms();
    for (i = 0; i < CPU_ITERS; i++) {
        acc = acc * 1103515245L + 12345L;
        acc ^= (acc >> 7);
        acc += (i ^ 0x5A5AL);
        acc &= 0x7FFFFFFFL;
    }
    dt = (long)(plat_ticks_ms() - t0);
    if (dt <= 0) { dt = 1; }
    g_bench_sink += acc;
    ops = CPU_ITERS * 4L;               /* 4 arithmetic ops per iteration */
    return ops / (dt * 1000L);          /* Mops/s = ops/1000/ms           */
}

static long bench_memcpy(void)
{
    unsigned char *a, *b;
    long i, dt, mb;
    cu32 t0;
    a = (unsigned char *)sys_alloc((cu32)MEM_SIZE);
    b = (unsigned char *)sys_alloc((cu32)MEM_SIZE);
    if (a == NULL || b == NULL) {
        if (a != NULL) { sys_free(a, (cu32)MEM_SIZE); }
        if (b != NULL) { sys_free(b, (cu32)MEM_SIZE); }
        return 0;
    }
    for (i = 0; i < MEM_SIZE; i++) { b[i] = (unsigned char)(i & 0xFF); }
    t0 = plat_ticks_ms();
    for (i = 0; i < MEMCPY_ITERS; i++) {
        memcpy(a, b, (cu32)MEM_SIZE);
        b[0] = (unsigned char)(a[i & 0xFF] + 1); /* chain to defeat elision */
    }
    dt = (long)(plat_ticks_ms() - t0);
    if (dt <= 0) { dt = 1; }
    g_bench_sink += a[0] + b[0];
    mb = (MEM_SIZE / (1024L * 1024L)) * MEMCPY_ITERS;
    sys_free(a, (cu32)MEM_SIZE);
    sys_free(b, (cu32)MEM_SIZE);
    return mb * 1000L / dt;             /* MB/s */
}

static long bench_memfill(void)
{
    cu32 *buf;
    long i, k, dt, mb, words;
    cu32 t0, pat;
    buf = (cu32 *)sys_alloc((cu32)MEM_SIZE);
    if (buf == NULL) { return 0; }
    words = MEM_SIZE / (long)sizeof(cu32);
    pat = 0xA5A5A5A5u;
    t0 = plat_ticks_ms();
    for (k = 0; k < MEMFILL_ITERS; k++) {
        for (i = 0; i < words; i++) { buf[i] = pat; }
        pat += 0x01010101u;
    }
    dt = (long)(plat_ticks_ms() - t0);
    if (dt <= 0) { dt = 1; }
    g_bench_sink += (long)buf[0];
    mb = (MEM_SIZE / (1024L * 1024L)) * MEMFILL_ITERS;
    sys_free(buf, (cu32)MEM_SIZE);
    return mb * 1000L / dt;             /* MB/s */
}

static long bench_gfxfill(void)
{
    GfxSurface *surf;
    CRect r;
    long i, dt, px;
    cu32 t0;
    surf = gfx_surface_new(GFX_W, GFX_H);
    if (surf == NULL) { return 0; }
    r = crect_make(0, 0, GFX_W, GFX_H);
    t0 = plat_ticks_ms();
    for (i = 0; i < GFXFILL_ITERS; i++) {
        gfx_fill_rect(surf, &r,
                      GFX_RGB((int)(i & 0xFF), (int)((i * 3) & 0xFF),
                              (int)((i * 7) & 0xFF)));
    }
    dt = (long)(plat_ticks_ms() - t0);
    if (dt <= 0) { dt = 1; }
    g_bench_sink += (long)gfx_get_pixel(surf, 0, 0);
    px = (long)GFX_W * GFX_H * GFXFILL_ITERS;
    gfx_surface_free(surf);
    return px / (dt * 1000L);           /* Mpix/s */
}

static long bench_gfxline(void)
{
    GfxSurface *surf;
    long i, dt;
    cu32 t0;
    int x0, y0, x1, y1;
    surf = gfx_surface_new(GFX_W, GFX_H);
    if (surf == NULL) { return 0; }
    t0 = plat_ticks_ms();
    for (i = 0; i < GFXLINE_ITERS; i++) {
        x0 = (int)((i * 7L) % GFX_W);
        y0 = (int)((i * 13L) % GFX_H);
        x1 = (int)((i * 11L + 40L) % GFX_W);
        y1 = (int)((i * 17L + 30L) % GFX_H);
        gfx_line(surf, x0, y0, x1, y1,
                 GFX_RGB(0x40, (int)(i & 0xFF), 0xC0));
    }
    dt = (long)(plat_ticks_ms() - t0);
    if (dt <= 0) { dt = 1; }
    g_bench_sink += (long)gfx_get_pixel(surf, GFX_W / 2, GFX_H / 2);
    gfx_surface_free(surf);
    return GFXLINE_ITERS / dt;          /* K lines/s = lines/ms */
}

/* Text throughput: the shell's most-drawn primitive (menus, lists, headers). */
static long bench_text(void)
{
    static const char *line =
        "CastaliaOS 98 PE 0123456789 the quick brown fox";
    GfxSurface *surf;
    long i, dt, glyphs;
    int len = 0;
    cu32 t0;
    while (line[len] != '\0') { len++; }
    surf = gfx_surface_new(GFX_W, GFX_H);
    if (surf == NULL) { return 0; }
    t0 = plat_ticks_ms();
    for (i = 0; i < TEXT_ITERS; i++) {
        gfx_draw_text(surf, ((i & 7) == 0) ? GFX_FONT_BOLD : GFX_FONT_SYSTEM,
                      (int)(i % 5), (int)((i * 9L) % GFX_H), line,
                      GFX_RGB(0xE0, 0xE8, 0xF0));
    }
    dt = (long)(plat_ticks_ms() - t0);
    if (dt <= 0) { dt = 1; }
    g_bench_sink += (long)gfx_get_pixel(surf, GFX_W / 2, GFX_H / 2);
    gfx_surface_free(surf);
    glyphs = TEXT_ITERS * (long)len;
    return glyphs / dt;                 /* glyphs/ms = K glyphs/s */
}

static long bench_hash(void)
{
    unsigned char *buf;
    long i, k, dt, mb;
    cu32 t0, h;
    buf = (unsigned char *)sys_alloc((cu32)MEM_SIZE);
    if (buf == NULL) { return 0; }
    for (i = 0; i < MEM_SIZE; i++) {
        buf[i] = (unsigned char)((i * 31L + 7L) & 0xFF);
    }
    h = 2166136261u;                    /* FNV-1a 32-bit offset basis */
    t0 = plat_ticks_ms();
    for (k = 0; k < HASH_ITERS; k++) {
        for (i = 0; i < MEM_SIZE; i++) {
            h ^= buf[i];
            h *= 16777619u;             /* FNV-1a prime */
        }
    }
    dt = (long)(plat_ticks_ms() - t0);
    if (dt <= 0) { dt = 1; }
    g_bench_sink += (long)h;
    mb = (MEM_SIZE / (1024L * 1024L)) * HASH_ITERS;
    sys_free(buf, (cu32)MEM_SIZE);
    return mb * 1000L / dt;             /* MB/s */
}

static void bench_run_all(WmWindow *win, Bench *bp)
{
    int i;
    long sum = 0;
    if (bp == NULL) { return; }
    bp->row[BM_CPU].value     = bench_cpu();
    bp->row[BM_MEMCPY].value  = bench_memcpy();
    bp->row[BM_MEMFILL].value = bench_memfill();
    bp->row[BM_GFXFILL].value = bench_gfxfill();
    bp->row[BM_GFXLINE].value = bench_gfxline();
    bp->row[BM_TEXT].value    = bench_text();
    bp->row[BM_HASH].value    = bench_hash();
    for (i = 0; i < NBENCH; i++) {
        long ref = BM_REF[i];
        bp->row[i].refmax = ref;
        if (ref <= 0) { ref = 1; }
        bp->row[i].score = bp->row[i].value * 1000L / ref;
        sum += bp->row[i].score;
    }
    bp->mark = sum / NBENCH;
    bp->ran = CTRUE;
    bp->reveal = 0;
    sys_snprintf(bp->status, sizeof(bp->status),
                 "Done -- CastaliaMark %ld", bp->mark);
    wm_invalidate(win, NULL);
}

/* ---- presentation helpers -------------------------------------------- */
static CColor bench_tier_color(long score)
{
    if (score >= 3600L) { return GFX_RGB(0x38, 0xB0, 0x4A); } /* green */
    if (score >= 1800L) { return GFX_RGB(0xE0, 0xA8, 0x28); } /* amber */
    return GFX_RGB(0xCE, 0x50, 0x3A);                         /* red   */
}

static const char *bench_rating(long mark)
{
    if (mark >= 4500L) { return "Blazing"; }
    if (mark >= 3000L) { return "Fast"; }
    if (mark >= 1500L) { return "Fair"; }
    return "Slow";
}

/* Render 'text' scaled by 'scale' (nearest-neighbor) so the composite score
 * reads as a big hero number despite the fixed 8px bitmap font. Draws to a tiny
 * offscreen surface, then blows up each set pixel into a scale*scale block. */
static void bench_draw_scaled(GfxSurface *s, const char *text, int x, int y,
                              int scale, CColor col, CColor bg)
{
    GfxSurface *tmp;
    int tw, th, ix, iy;
    tw = gfx_text_width(GFX_FONT_BOLD, text);
    th = 8;
    if (tw <= 0) { return; }
    tmp = gfx_surface_new(tw + 4, th);
    if (tmp == NULL) {
        gfx_draw_text(s, GFX_FONT_BOLD, x, y, text, col);
        return;
    }
    gfx_clear(tmp, bg);
    gfx_draw_text(tmp, GFX_FONT_BOLD, 0, 0, text, col);
    for (iy = 0; iy < th; iy++) {
        for (ix = 0; ix < tw + 4; ix++) {
            CColor px;
            CRect blk;
            px = gfx_get_pixel(tmp, ix, iy);
            if (px != bg) {
                blk = crect_make(x + ix * scale, y + iy * scale, scale, scale);
                gfx_fill_rect(s, &blk, px);
            }
        }
    }
    gfx_surface_free(tmp);
}

/* Soft moving light band (a "sheen") across rect 'r', centered on 'center'. */
static void bench_sheen(GfxSurface *s, const CRect *r, int center, int halfw)
{
    int x, y, d, amt, hw;
    CColor c;
    hw = (halfw > 0) ? halfw : 1;
    for (x = center - halfw; x <= center + halfw; x++) {
        if (x < r->x0 || x >= r->x1) { continue; }
        d = x - center;
        if (d < 0) { d = -d; }
        amt = 55 - (55 * d / hw);
        if (amt <= 0) { continue; }
        for (y = r->y0; y < r->y1; y++) {
            c = gfx_get_pixel(s, x, y);
            gfx_put_pixel(s, x, y, gfx_tint(c, 0xFFFFFFUL, amt));
        }
    }
}

/* ---- layout ---------------------------------------------------------- */
typedef struct {
    CRect toolbar, runbtn, score, panel, status, row[NBENCH];
} BenchLayout;

static void bench_layout(WmWindow *win, BenchLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c);
    int y, i, panel_h, rows_h, rtop;
    L->toolbar = crect_make(0, 0, cw, BM_TOOLBAR_H);
    L->runbtn  = crect_make(cw - 92, 5, 84, BM_TOOLBAR_H - 10);
    y = BM_TOOLBAR_H;
    L->score = crect_make(4, y + 3, cw - 8, BM_SCORE_H - 6);
    y += BM_SCORE_H;
    L->status = crect_make(0, ch - BM_STATUS_H, cw, BM_STATUS_H);
    panel_h = ch - y - BM_STATUS_H;
    if (panel_h < BM_ROW_H) { panel_h = BM_ROW_H; }
    L->panel = crect_make(4, y, cw - 8, panel_h);
    rows_h = NBENCH * BM_ROW_H;
    rtop = L->panel.y0 + (panel_h - rows_h) / 2;
    if (rtop < L->panel.y0 + 4) { rtop = L->panel.y0 + 4; }
    for (i = 0; i < NBENCH; i++) {
        L->row[i] = crect_make(L->panel.x0 + 8, rtop + i * BM_ROW_H,
                               crect_w(&L->panel) - 16, BM_ROW_H - 2);
    }
}

/* ---- paint ----------------------------------------------------------- */
static void bench_paint(WmWindow *win, GfxSurface *s)
{
    Bench *bp = (Bench *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    CColor white = GFX_RGB(0xFF, 0xFF, 0xFF);
    CColor black = GFX_RGB(0, 0, 0);
    BenchLayout L;
    CRect cc, r, inner;
    int i, sheen_c;
    char num[24];

    if (bp == NULL) { return; }
    bench_layout(win, &L);

    /* Client background. */
    cc = wm_client_rect(win);
    gfx_fill_rect(s, &cc, p->face);

    /* Toolbar: glossy gradient banner + title + Run All. */
    r = crect_offset(&L.toolbar, o.x, o.y);
    gfx_vgradient3(s, &r, gfx_tint(p->accent, white, 90), p->accent,
                   gfx_tint(p->accent, black, 60), 320);
    gfx_hline(s, r.x0, r.y1 - 1, crect_w(&r), gfx_tint(p->accent, black, 100));
    gfx_draw_text_shadow(s, GFX_FONT_BOLD, r.x0 + 8, r.y0 + 10,
                         "CastaliaMark  -  Benchmark Suite",
                         white, gfx_tint(p->accent, black, 130));
    {
        CRect rb = crect_offset(&L.runbtn, o.x, o.y);
        ui_draw_button(s, &rb, "Run All",
                       ui_hot_state(&bp->hot, 0, UI_BTN_NORMAL));
    }

    /* Score panel: raised, gradient face, big composite number + sheen. */
    r = crect_offset(&L.score, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED, p->light, p->dark, GFX_NO_FILL);
    inner = crect_inset(&r, 2);
    gfx_vgradient(s, &inner, gfx_tint(p->face, white, 55),
                  gfx_tint(p->face, black, 35));
    gfx_draw_text(s, GFX_FONT_BOLD, inner.x0 + 10, inner.y0 + 7,
                  "CASTALIAMARK SCORE", gfx_tint(p->text, white, 10));
    if (bp->ran) {
        sys_snprintf(num, sizeof(num), "%ld", bp->mark);
    } else {
        sys_strlcpy(num, "----", sizeof(num));
    }
    bench_draw_scaled(s, num, inner.x0 + 10, inner.y0 + 20, 3,
                      bp->ran ? bench_tier_color(bp->mark) : p->text_disabled,
                      GFX_COLORKEY);
    {
        const char *rate = bp->ran ? bench_rating(bp->mark) : "not run";
        gfx_draw_text(s, GFX_FONT_BOLD,
                      inner.x1 - gfx_text_width(GFX_FONT_BOLD, rate) - 10,
                      inner.y0 + 22, rate, gfx_tint(p->text, white, 5));
    }
    sheen_c = inner.x0 - 20 +
              (int)((long)bp->sheen * (long)(crect_w(&inner) + 40) /
                    BENCH_SHEEN_PERIOD);
    bench_sheen(s, &inner, sheen_c, 14);

    /* Results panel: one animated row per benchmark. */
    r = crect_offset(&L.panel, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    for (i = 0; i < NBENCH; i++) {
        CRect rr = crect_offset(&L.row[i], o.x, o.y);
        CRect bar, fill;
        long sc, frac, local, ease, shown;
        int bw, fillw;
        CColor tcol;
        char vs[32];

        tcol = bp->ran ? bench_tier_color(bp->row[i].score) : p->text_disabled;

        /* Name (left) + numeric result with unit (right). */
        gfx_draw_text(s, GFX_FONT_BOLD, rr.x0 + 2, rr.y0 + 1,
                      BM_NAME[i], p->text);
        if (bp->ran) {
            sys_snprintf(vs, sizeof(vs), "%ld %s",
                         bp->row[i].value, BM_UNIT[i]);
        } else {
            sys_strlcpy(vs, "--", sizeof(vs));
        }
        gfx_draw_text(s, GFX_FONT_BOLD,
                      rr.x1 - gfx_text_width(GFX_FONT_BOLD, vs) - 2,
                      rr.y0 + 1, vs, tcol);

        /* Bar meter: sunken well + tier-colored gradient fill. */
        bar = crect_make(rr.x0, rr.y0 + 13, crect_w(&rr), 12);
        gfx_bevel(s, &bar, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark,
                  gfx_tint(p->face, black, 22));

        /* Permille of full scale. ui_meter_fill does the clamping at both
         * ends and cannot overflow, which retires the "keep the *1000 below
         * bounded" pre-clamp at 200000 that used to guard this multiply. */
        sc = bp->row[i].score;
        frac = (long)ui_meter_fill((cs32)sc, (cs32)BENCH_BAR_FULL, 1000);
        /* Staggered ease-in reveal (quadratic). */
        local = (long)bp->reveal - (long)i * BENCH_STAGGER;
        if (local < 0) { local = 0; }
        if (local > BENCH_ANIM_MAX) { local = BENCH_ANIM_MAX; }
        ease = local * local * 1000L / ((long)BENCH_ANIM_MAX * BENCH_ANIM_MAX);
        shown = bp->ran ? (frac * ease / 1000L) : 0;

        bw = crect_w(&bar) - 2;
        fillw = ui_meter_fill((cs32)shown, (cs32)1000, bw);
        if (fillw > 0) {
            fill = crect_make(bar.x0 + 1, bar.y0 + 1, fillw,
                              crect_h(&bar) - 2);
            gfx_vgradient(s, &fill, gfx_tint(tcol, white, 105), tcol);
        }
    }

    /* Status strip. */
    r = crect_offset(&L.status, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, p->face);
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 6, r.y0 + (BM_STATUS_H - 8) / 2,
                  bp->status, p->text);
}

/* ---- input / animation ----------------------------------------------- */
static cbool bench_click(WmWindow *win, Bench *bp, int x, int y)
{
    BenchLayout L;
    bench_layout(win, &L);
    if (crect_contains(&L.runbtn, x, y)) {
        (void)ui_hot_press(&bp->hot, 0);
        bench_run_all(win, bp);
        return CTRUE;
    }
    return CFALSE;
}

static void bench_tick(WmWindow *win, Bench *bp)
{
    if (bp == NULL) { return; }
    if (bp->ran && bp->reveal < BENCH_REVEAL_MAX) { bp->reveal++; }
    bp->sheen++;
    if (bp->sheen >= BENCH_SHEEN_PERIOD) { bp->sheen = 0; }
    wm_invalidate(win, NULL);
}

static cbool bench_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    Bench *bp = (Bench *)wm_user(win);
    switch (msg) {
    case WM_MSG_PAINT:
        bench_paint(win, (GfxSurface *)param);
        return CTRUE;
    case WM_MSG_LBUTTONDOWN:
        return bench_click(win, bp, (int)a, (int)b);
    case WM_MSG_MOUSEMOVE: {
        BenchLayout L;
        if (bp == NULL) { return CFALSE; }
        bench_layout(win, &L);
        if (ui_hot_move(&bp->hot,
                        crect_contains(&L.runbtn, (int)a, (int)b) ? 0 : -1)) {
            ui_hot_repaint(win, &bp->hot, &L.runbtn, 1);
        }
        return CTRUE;
    }
    case WM_MSG_LBUTTONUP:
    case WM_MSG_MOUSELEAVE:
        if (bp != NULL) {
            cbool redraw = ui_hot_release(&bp->hot);
            BenchLayout L;
            if (msg == WM_MSG_MOUSELEAVE && ui_hot_move(&bp->hot, -1)) {
                redraw = CTRUE;
            }
            bench_layout(win, &L);
            if (redraw) { ui_hot_repaint(win, &bp->hot, &L.runbtn, 1); }
        }
        return CTRUE;
    case WM_MSG_KEYDOWN:
        if ((int)a == PLAT_KEY_ENTER || b == 'r' || b == 'R') {
            bench_run_all(win, bp);
            return CTRUE;
        }
        return CFALSE;
    case WM_MSG_TIMER:
        bench_tick(win, bp);
        return CTRUE;
    case WM_MSG_DESTROY:
        if (bp != NULL) { sys_free(bp, (cu32)sizeof(Bench)); }
        return CTRUE;
    default:
        return CFALSE;
    }
}

void app_bench_open(void)
{
    Bench *b;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int cw = 460, ch = 366, fx, fy;

    b = (Bench *)sys_calloc(1, (cu32)sizeof(Bench));
    if (b == NULL) { SYS_LOGE("app", "bench: OOM"); return; }
    b->ran = CFALSE;
    b->reveal = 0;
    b->sheen = 0;
    sys_strlcpy(b->status, "Ready -- press Run All (or Enter) to begin",
                sizeof(b->status));

    plat_video_info(&vi);
    fx = (vi.width - cw) / 2;
    fy = (vi.height - ch) / 2;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, cw, ch);

    w = wm_create("Benchmark Suite", &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE |
                  WM_STYLE_BORDER, bench_proc, b);
    if (w == NULL) { sys_free(b, (cu32)sizeof(Bench)); return; }
    b->self = w;
    wm_set_animated(w, CTRUE);
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened Benchmark Suite");
}
