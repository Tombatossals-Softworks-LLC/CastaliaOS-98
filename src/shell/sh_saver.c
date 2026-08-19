/*
 * sh_saver.c - Screensavers (stateless, function of a frame counter).
 *
 * Everything is a pure function of the frame, so the shell can kick a saver in
 * after an idle timeout, advance it each frame, and drop it the instant input
 * arrives -- no saved state to unwind. Original art, integer math only. Four
 * modes: the drifting Castalia castle, a warp starfield, a plasma field, and a
 * Mystify-style bouncing polyline. The active mode is chosen in the Control
 * Center; sh_saver_draw() reads it from settings, sh_saver_draw_mode() renders a
 * specific one (used by the Control Center's live preview).
 */
#include "castalia/shell.h"
#include "castalia/gfx.h"
#include "castalia/settings.h"

#define SAVER_STARS 90

/* 256-entry sine LUT, values 0..255 == 128 + 127*sin(2*pi*i/256). */
static const unsigned char SIN[256] = {
    128, 131, 134, 137, 140, 144, 147, 150, 153, 156, 159, 162, 165, 168, 171, 174,
    177, 179, 182, 185, 188, 191, 193, 196, 199, 201, 204, 206, 209, 211, 213, 216,
    218, 220, 222, 224, 226, 228, 230, 232, 234, 235, 237, 239, 240, 241, 243, 244,
    245, 246, 248, 249, 250, 250, 251, 252, 253, 253, 254, 254, 254, 255, 255, 255,
    255, 255, 255, 255, 254, 254, 254, 253, 253, 252, 251, 250, 250, 249, 248, 246,
    245, 244, 243, 241, 240, 239, 237, 235, 234, 232, 230, 228, 226, 224, 222, 220,
    218, 216, 213, 211, 209, 206, 204, 201, 199, 196, 193, 191, 188, 185, 182, 179,
    177, 174, 171, 168, 165, 162, 159, 156, 153, 150, 147, 144, 140, 137, 134, 131,
    128, 125, 122, 119, 116, 112, 109, 106, 103, 100,  97,  94,  91,  88,  85,  82,
     79,  77,  74,  71,  68,  65,  63,  60,  57,  55,  52,  50,  47,  45,  43,  40,
     38,  36,  34,  32,  30,  28,  26,  24,  22,  21,  19,  17,  16,  15,  13,  12,
     11,  10,   8,   7,   6,   6,   5,   4,   3,   3,   2,   2,   2,   1,   1,   1,
      1,   1,   1,   1,   2,   2,   2,   3,   3,   4,   5,   6,   6,   7,   8,  10,
     11,  12,  13,  15,  16,  17,  19,  21,  22,  24,  26,  28,  30,  32,  34,  36,
     38,  40,  43,  45,  47,  50,  52,  55,  57,  60,  63,  65,  68,  71,  74,  77,
     79,  82,  85,  88,  91,  94,  97, 100, 103, 106, 109, 112, 116, 119, 122, 125
};
static int isin(int a) { return (int)SIN[(unsigned)a & 255u] - 128; }  /* -127..127 */

/* A cheap deterministic hash so star positions are stable across frames. */
static unsigned saver_hash(unsigned n)
{
    n = (n ^ 61u) ^ (n >> 16);
    n = n + (n << 3);
    n = n ^ (n >> 4);
    n = n * 0x27d4eb2du;
    n = n ^ (n >> 15);
    return n;
}

/* Reflect t into 0..range and back (triangle wave) for the bounce. */
static int saver_bounce(int t, int range)
{
    int p;
    if (range <= 0) { return 0; }
    p = t % (2 * range);
    return (p < range) ? p : (2 * range - p);
}

/* A small gold castle at (x,y), top-left origin (~44x30). */
static void saver_castle(GfxSurface *s, int x, int y)
{
    CColor lit  = GFX_RGB(0xF2, 0xD2, 0x78);
    CColor body = GFX_RGB(0xD2, 0xA4, 0x40);
    CColor dark = GFX_RGB(0x5A, 0x42, 0x12);
    CColor gate = GFX_RGB(0x18, 0x12, 0x04);
    CRect r;
    int i;
    /* Curtain wall + towers + keep. */
    r = crect_make(x + 8, y + 12, 28, 18);  gfx_fill_rect(s, &r, body);
    r = crect_make(x, y + 6, 10, 24);        gfx_fill_rect(s, &r, body);
    r = crect_make(x + 34, y + 6, 10, 24);   gfx_fill_rect(s, &r, body);
    r = crect_make(x + 15, y, 14, 30);       gfx_fill_rect(s, &r, body);
    /* Lit top faces. */
    gfx_hline(s, x, y + 6, 10, lit);
    gfx_hline(s, x + 34, y + 6, 10, lit);
    gfx_hline(s, x + 15, y, 14, lit);
    /* Merlons. */
    for (i = 0; i < 3; i++) {
        r = crect_make(x + i * 5, y + 2, 3, 4);      gfx_fill_rect(s, &r, body);
        r = crect_make(x + 34 + i * 5, y + 2, 3, 4); gfx_fill_rect(s, &r, body);
        r = crect_make(x + 15 + i * 6, y - 4, 4, 4);  gfx_fill_rect(s, &r, body);
    }
    /* Windows + gate + shaded right edges. */
    gfx_vline(s, x + 9, y + 30, -24 + 24, dark);
    gfx_vline(s, x + 43, y + 6, 24, dark);
    gfx_vline(s, x + 28, y, 30, dark);
    r = crect_make(x + 19, y + 20, 6, 10); gfx_fill_rect(s, &r, gate);
    gfx_vline(s, x + 4, y + 12, 6, gate);
    gfx_vline(s, x + 39, y + 12, 6, gate);
}

/* Mode 0: the drifting Castalia castle over a twinkling starfield. */
static void saver_scene_castle(GfxSurface *s, int frame)
{
    int w, h, i, cw = 44, ch = 34, cx, cy;
    CRect full;
    w = s->w; h = s->h;
    full = crect_make(0, 0, w, h);
    gfx_fill_rect(s, &full, GFX_RGB(0x02, 0x03, 0x08));

    /* Starfield: stable positions, brightness twinkling with the frame. */
    for (i = 0; i < SAVER_STARS; i++) {
        unsigned ha = saver_hash((unsigned)i);
        unsigned hb = saver_hash((unsigned)i * 2654435761u + 1u);
        int sx = (int)(ha % (unsigned)w);
        int sy = (int)(hb % (unsigned)h);
        int tw = (frame / 3 + i * 5) & 63;
        int b = 90 + ((tw < 32) ? tw : (63 - tw)) * 5;   /* 90..245 */
        CColor c = GFX_RGB(b, b, b > 255 - 20 ? 255 : b + 20);
        gfx_put_pixel(s, sx, sy, c);
        if ((i & 7) == 0) {   /* a few brighter cross-stars */
            gfx_put_pixel(s, sx - 1, sy, c);
            gfx_put_pixel(s, sx + 1, sy, c);
            gfx_put_pixel(s, sx, sy - 1, c);
            gfx_put_pixel(s, sx, sy + 1, c);
        }
    }

    /* The bouncing castle + a soft glow behind it. */
    cx = saver_bounce(frame * 2, w - cw);
    cy = saver_bounce(frame * 3 / 2, h - ch);
    {
        int gx = cx + cw / 2, gy = cy + ch / 2, rx = cw, ry = ch, ex, ey;
        for (ey = -ry; ey <= ry; ey++) {
            for (ex = -rx; ex <= rx; ex++) {
                long e = (long)ex * ex * 256 / ((long)rx * rx) +
                         (long)ey * ey * 256 / ((long)ry * ry);
                if (e < 256) {
                    CColor c = gfx_get_pixel(s, gx + ex, gy + ey);
                    gfx_put_pixel(s, gx + ex, gy + ey,
                                  gfx_tint(c, GFX_RGB(0x30, 0x24, 0x08),
                                           (int)((256 - e) * 90 / 256)));
                }
            }
        }
    }
    saver_castle(s, cx, cy);
}

/* Mode 1: a warp starfield -- stars streak outward from the center. */
static void saver_scene_starfield(GfxSurface *s, int frame)
{
    int w = s->w, h = s->h, cx = w / 2, cy = h / 2, i;
    CRect full = crect_make(0, 0, w, h);
    int period = 200;
    gfx_fill_rect(s, &full, GFX_RGB(0x00, 0x00, 0x02));
    for (i = 0; i < 220; i++) {
        unsigned seed = saver_hash((unsigned)i * 2654435761u + 7u);
        int dx = (int)(seed % 2000u) - 1000;
        int dy = (int)((seed >> 11) % 2000u) - 1000;
        int zt = (frame * 3 + (int)(seed % (unsigned)period)) % period;
        int z0 = period - zt;              /* near (small) .. far (large)   */
        int z1 = z0 + 6;
        int px0, py0, px1, py1, b;
        if (z0 < 1) { z0 = 1; }
        px0 = cx + dx * 220 / (z0 * 10 + 1);
        py0 = cy + dy * 220 / (z0 * 10 + 1);
        px1 = cx + dx * 220 / (z1 * 10 + 1);
        py1 = cy + dy * 220 / (z1 * 10 + 1);
        if (px0 < 0 || px0 >= w || py0 < 0 || py0 >= h) { continue; }
        b = 60 + (period - z0) * 195 / period;
        if (b > 255) { b = 255; }
        gfx_line(s, px1, py1, px0, py0, GFX_RGB(b, b, (b > 235) ? 255 : b + 20));
    }
}

/* Mode 2: an integer plasma field, drawn in 2x2 blocks for speed. */
static void saver_scene_plasma(GfxSurface *s, int frame)
{
    int w = s->w, h = s->h, x, y;
    for (y = 0; y < h; y += 2) {
        for (x = 0; x < w; x += 2) {
            int v = isin(x / 3 + frame)
                  + isin(y / 4 - frame)
                  + isin((x + y) / 5 + frame / 2)
                  + isin((x - y) / 6 - frame / 3);   /* -508..508 */
            int t = (v + 512) & 255;                 /* wrap into 0..255    */
            CColor c = GFX_RGB(128 + isin(t) , 128 + isin(t + 85),
                               128 + isin(t + 170));
            CRect blk = crect_make(x, y, 2, 2);
            gfx_fill_rect(s, &blk, c);
        }
    }
}

/* Mode 3: a Mystify-style bouncing polyline with a fading trail. */
static void saver_scene_mystify(GfxSurface *s, int frame)
{
    int w = s->w, h = s->h, k, v;
    CRect full = crect_make(0, 0, w, h);
    /* Four vertices, each bouncing with its own speed/phase. */
    static const int vx[4] = { 3, 4, 5, 3 };
    static const int vy[4] = { 2, 3, 2, 4 };
    static const int ph[4] = { 0, 137, 61, 200 };
    gfx_fill_rect(s, &full, GFX_RGB(0x04, 0x04, 0x0C));
    for (k = 10; k >= 0; k--) {                 /* trailing copies, dim..bright */
        int f = frame - k * 3;
        int px[4], py[4];
        int bright = 255 - k * 20;
        CColor col;
        if (bright < 40) { bright = 40; }
        col = GFX_RGB((128 + isin(f)) * bright / 255,
                      (128 + isin(f + 85)) * bright / 255,
                      (128 + isin(f + 170)) * bright / 255);
        for (v = 0; v < 4; v++) {
            px[v] = saver_bounce(f * vx[v] + ph[v], w - 1);
            py[v] = saver_bounce(f * vy[v] + ph[v] * 2, h - 1);
        }
        for (v = 0; v < 4; v++) {
            gfx_line(s, px[v], py[v], px[(v + 1) & 3], py[(v + 1) & 3], col);
        }
    }
}

void sh_saver_draw_mode(GfxSurface *s, int frame, int mode)
{
    if (s == NULL) { return; }
    gfx_reset_clip(s);
    switch (mode) {
    case 1:  saver_scene_starfield(s, frame); break;
    case 2:  saver_scene_plasma(s, frame);    break;
    case 3:  saver_scene_mystify(s, frame);   break;
    default: saver_scene_castle(s, frame);    break;
    }
}

void sh_saver_draw(GfxSurface *s, int frame)
{
    sh_saver_draw_mode(s, frame, settings_get()->screensaver);
}
