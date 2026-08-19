/*
 * sh_logo.c - The Castalia mark: a castle on green hills inside a blue "C".
 *
 * One drawing routine renders the brand everywhere it appears -- the Start
 * button, the launcher header, the boot splash, the About crest -- so the
 * identity can never drift between them. The ring and the hills are drawn
 * mathematically (they must stay round at any size); the castle is a pixel-art
 * sprite scaled by whole pixels so it keeps its crisp retro edges instead of
 * turning to mush.
 *
 * All original artwork, drawn with the engine's own primitives.
 */
#include "sh_internal.h"
#include "castalia/gfx.h"
#include "castalia/sys.h"

/* The palette of the mark. */
#define LG_INK    GFX_RGB(0x14, 0x2E, 0x5E)   /* navy outline              */
#define LG_BLUE   GFX_RGB(0x2E, 0x6D, 0xB4)   /* the C ring                */
#define LG_BLUE_HI GFX_RGB(0x7F, 0xB2, 0xE4)  /* dithered ring highlight   */
#define LG_WALL   GFX_RGB(0x4C, 0x84, 0xC6)   /* castle stone              */
#define LG_WALL_D GFX_RGB(0x33, 0x62, 0xA2)   /* castle shading            */
#define LG_ROOF   GFX_RGB(0xE0, 0x4A, 0x2F)   /* conical roofs             */
#define LG_GOLD   GFX_RGB(0xF0, 0xA8, 0x1E)   /* windows and the gate      */
#define LG_HILL   GFX_RGB(0x5F, 0xA8, 0x3C)   /* hills                     */
#define LG_HILL_HI GFX_RGB(0x9A, 0xCF, 0x4F)  /* dithered hill highlight   */

/*
 * The castle, 15 x 16 cells:
 *   W wall stone   R roof   G gold (window / gate)   . transparent
 * A tall crenellated keep in the middle, a battlemented tower on the left and
 * a red-roofed turret either side, over a wall with an arched gate.
 */
#define LG_CW 15
#define LG_CH 16
static const char *const LG_CASTLE[LG_CH] = {
    "......W.W......",
    "......WWW......",
    "...R..WWW......",
    "..RRR.WWW..R...",
    ".RRRRRWWW.RRR..",
    ".WWWWWWWW RRRRR",
    ".W.G.W WW.WW.WW",
    ".WWWWWWWW.WWWWW",
    ".WWWWWWWW.WWWWW",
    ".WWWWWWWWWWWWWW",
    "WWWWWWWWWWWWWWW",
    "WW.G.WWWWW.G.WW",
    "WWWWWWWWWWWWWWW",
    "WWWWW.GGG.WWWWW",
    "WWWW.GGGGG.WWWW",
    "WWWW.GGGGG.WWWW"
};

/*
 * At small sizes the detailed castle collapses into noise, so the mark has a
 * second, bolder cut -- fewer, chunkier shapes -- the way any logo needs a
 * simplified form for a favicon. 9 x 9 cells.
 */
#define LG_SW 9
#define LG_SH 9
static const char *const LG_CASTLE_S[LG_SH] = {
    "..R...R..",
    ".RRR.RRR.",
    "WWWWWWWWW",
    "W.W.W.W.W",
    "WWWWWWWWW",
    "WW.G.G.WW",
    "WWWWWWWWW",
    "WWW.G.WWW",
    "WWW.G.WWW"
};

/* A 2x2 ordered dither: CTRUE where the highlight tone should land. */
static cbool lg_dither(int x, int y)
{
    return (((x + y) & 1) == 0) ? CTRUE : CFALSE;
}

/*
 * ...but only where there is room for it to read as texture.
 *
 * A 2x2 checkerboard over a twenty-pixel hill is not a highlight, it is
 * noise: half the pixels alternate, the two greens average into one muddy
 * tone, and the mark looks like a failed screenshot. The dither earns its
 * place on the splash at 80 px and on the About crest; below that the flat
 * tone is simply better. Sixty-four is where it stops looking like static.
 */
#define LG_DITHER_MIN 64
static cbool lg_dither_at(int size, int x, int y)
{
    return (size >= LG_DITHER_MIN) ? lg_dither(x, y) : CFALSE;
}

/* Fill a scaled sprite cell. */
static void lg_cell(GfxSurface *s, int x, int y, int k, CColor c)
{
    CRect r = crect_make(x, y, k, k);
    gfx_fill_rect(s, &r, c);
}

/*
 * ---- the mark as an IMAGE ----------------------------------------------
 *
 * The brand is drawn from primitives below, which is what lets it exist at
 * all with no assets. But a hand-authored mark beats a procedural one, and a
 * 9x9 sprite cannot carry a real logo at 22 pixels. So if the active icon
 * pack supplies `logo-16/24/32/48/64/128`, that art wins and the drawing
 * below becomes the fallback for a bare install.
 *
 * Baked at several sizes rather than scaled from one, because a mark reduced
 * from 128 to 22 by nearest-neighbour loses the strokes that make it legible;
 * the sizes are produced by tools/png2bmp.py, which area-averages with
 * premultiplied alpha. The size chosen here is the smallest baked one that is
 * at least as large as the request, so any scaling left is a REDUCTION.
 */
static const int LG_BAKED[6] = { 16, 24, 32, 48, 64, 128 };

static const GfxSurface *lg_pack_image(int size)
{
    const GfxSurface *best = NULL;
    int i;
    for (i = 0; i < 6; i++) {
        const GfxSurface *g;
        char name[16];
        sys_snprintf(name, sizeof name, "logo-%d", LG_BAKED[i]);
        g = sh_iconpack_icon(name);
        if (g == NULL) { continue; }
        best = g;                      /* the largest seen so far */
        if (LG_BAKED[i] >= size) { return g; }
    }
    return best;                       /* none big enough: use the biggest */
}

/* Nearest-neighbour, keyed, into a size x size box. The column map is shared
 * with every other scaler in the system (gfx_scale_map) and lives in BSS
 * rather than on the stack, which is small on the target. */
static int g_lg_cols[512];

static void lg_blit_scaled(GfxSurface *s, int x, int y, int size,
                           const GfxSurface *img)
{
    int ix, iy, row;
    if (img == NULL || img->w <= 0 || img->h <= 0) { return; }
    if (size > (int)(sizeof g_lg_cols / sizeof g_lg_cols[0])) {
        size = (int)(sizeof g_lg_cols / sizeof g_lg_cols[0]);
    }
    gfx_scale_map(g_lg_cols, size, img->w);
    for (iy = 0; iy < size; iy++) {
        row = (iy * img->h) / size;
        for (ix = 0; ix < size; ix++) {
            CColor c = img->pixels[(long)row * img->pitch + g_lg_cols[ix]];
            /* The shared constant, not a literal: sh_iconpack quantizes pack
             * art onto the theme palette in 16- and 256-colour modes and
             * leaves this exact value alone so keyed transparency survives.
             * A second spelling of it here is a second thing to keep in step. */
            if (c == GFX_COLORKEY) { continue; }
            gfx_put_pixel(s, x + ix, y + iy, c);
        }
    }
}

void sh_logo_draw(GfxSurface *s, int x, int y, int size)
{
    int cx, cy, r_out, r_in, k, gx, gy, px, py;
    long ro2, ri2;

    if (s == NULL || size < 8) { return; }
    {
        const GfxSurface *img = lg_pack_image(size);
        if (img != NULL) { lg_blit_scaled(s, x, y, size, img); return; }
    }
    /* The mark is wider than its ring: the hills break out to the right, so
     * the circle sits left of centre and everything stays inside the box the
     * caller gave us (a sprite that draws outside its own bounds gets clipped
     * by whatever it is blitted into). */
    cx = x + (size * 44) / 100;
    cy = y + (size * 46) / 100;
    r_out = (size * 43) / 100;
    r_in  = (r_out * 62) / 100;
    ro2 = (long)r_out * r_out;
    ri2 = (long)r_in * r_in;

    /* ---- the "C": an annulus with a wedge opened on the right ---------- */
    for (py = -r_out; py <= r_out; py++) {
        for (px = -r_out; px <= r_out; px++) {
            long d2 = (long)px * px + (long)py * py;
            int ay = (py < 0) ? -py : py;
            CColor c;
            if (d2 > ro2 || d2 < ri2) { continue; }
            /* The opening: a sector on the right, roughly +/-38 degrees. */
            if (px > 0 && (long)ay * 100 < (long)px * 78) { continue; }
            /* Highlight the upper-right of the ring with a dithered tone. */
            c = LG_BLUE;
            /* A dithered sheen on the OUTER upper-right of the ring only. */
            if (py < 0 && px > -r_out / 3 &&
                d2 > (ri2 + ro2) / 2 && lg_dither_at(size, cx + px, cy + py)) {
                c = LG_BLUE_HI;
            }
            /* A navy edge on the inner and outer rims defines the shape. */
            if (d2 > (long)(r_out - 1) * (r_out - 1) ||
                d2 < (long)(r_in + 1) * (r_in + 1)) {
                c = LG_INK;
            }
            gfx_put_pixel(s, cx + px, cy + py, c);
        }
    }

    /* ---- the hills ----------------------------------------------------
     * Two wide, shallow domes fill the bottom of the ring. The BOTTOM edge is
     * the ring itself -- the green is clipped to the circle, not to the domes
     * -- which is what keeps it reading as ground inside the mark rather than
     * two balls stuck on it. Only a modest tongue escapes through the C's
     * opening, and it is bounded so it cannot hang below the mark.
     */
    {
        int m1x = cx - (r_out * 30) / 100, m1y = cy + (r_out * 148) / 100;
        int m2x = cx + (r_out * 72) / 100, m2y = cy + (r_out * 140) / 100;
        int m1r = (r_out * 105) / 100, m2r = (r_out * 88) / 100;
        int tongue_x = x + size - 1;
        int tongue_y = cy + (r_out * 88) / 100;
        for (py = cy; py < y + size; py++) {
            for (px = x; px <= tongue_x; px++) {
                long d1 = (long)(px - m1x) * (px - m1x) +
                          (long)(py - m1y) * (py - m1y);
                long dm2 = (long)(px - m2x) * (px - m2x) +
                           (long)(py - m2y) * (py - m2y);
                long dc = (long)(px - cx) * (px - cx) +
                          (long)(py - cy) * (py - cy);
                cbool in1 = (d1 <= (long)m1r * m1r) ? CTRUE : CFALSE;
                cbool in2 = (dm2 <= (long)m2r * m2r) ? CTRUE : CFALSE;
                cbool inside = (dc <= ro2) ? CTRUE : CFALSE;
                if (!in1 && !in2) { continue; }
                if (!inside) {
                    /* the deliberate overhang, and nothing more */
                    if (!(in2 && px > cx && py <= tongue_y)) { continue; }
                }
                gfx_put_pixel(s, px, py,
                              lg_dither_at(size, px, py) ? LG_HILL_HI : LG_HILL);
            }
        }
    }

    /* ---- the castle ---------------------------------------------------- */
    {
        const char *const *art = LG_CASTLE;
        int aw = LG_CW, ah = LG_CH;
        /*
         * The bold cut only below ~30 px. Whole-pixel scaling means a 9-cell
         * sprite can be 9 px or 18 px and nothing between, and in a 34 px
         * mark 9 is a speck while 18 swamps the ring the castle is supposed
         * to sit inside. The 15-cell cut at scale 1 is 15 px -- the size that
         * was missing -- so the middle range uses the detailed art at 1:1,
         * which is the density it was drawn for.
         */
        if (size < 30) { art = LG_CASTLE_S; aw = LG_SW; ah = LG_SH; }
        /* Sized against the ring's HOLE, not the whole mark: the castle may
         * overlap the ring a little (it does in the mark) but must not swamp
         * it. Its base then sits on the hill line rather than floating. */
        /*
         * ROUNDED, not truncated. As written this was
         * ((r_in * 165) / 100) / aw with integer division and aw = 9, which
         * floors to 1 for every size from 20 to 43 px -- so the castle stayed
         * nine pixels across whether the mark was 22 px or 43 px, and the
         * Start orb and the launcher header are both in that range. A
         * nine-pixel castle inside a thirty-four-pixel mark is a speck in a
         * white hole, which is exactly how it looked. Rounding puts the
         * second step where it belongs, at about 32 px.
         */
        k = (((r_in * 185) / 100) + aw / 2) / aw;
        if (k < 1) { k = 1; }
        gx = cx - (aw * k) / 2;
        gy = cy + (r_out * 48) / 100 - ah * k + k;
    for (py = 0; py < ah; py++) {
        for (px = 0; px < aw; px++) {
            char ch = art[py][px];
            int dx = gx + px * k, dy = gy + py * k;
            switch (ch) {
            case 'W':
                /* right-hand cells shade, giving the stone some volume */
                lg_cell(s, dx, dy, k, (px > aw / 2 + 2) ? LG_WALL_D : LG_WALL);
                break;
            case 'R': lg_cell(s, dx, dy, k, LG_ROOF); break;
            case 'G': lg_cell(s, dx, dy, k, LG_GOLD); break;
            default: break;
            }
        }
    }
    /* Outline the castle: any solid cell with a transparent neighbour gets a
     * navy edge, which is what makes the sprite read against the ring. */
    for (py = 0; py < ah; py++) {
        for (px = 0; px < aw; px++) {
            char ch = art[py][px];
            int dx = gx + px * k, dy = gy + py * k;
            if (ch == '.' || ch == ' ') { continue; }
            if (py == 0 || art[py - 1][px] == '.' || art[py - 1][px] == ' ') {
                gfx_hline(s, dx, dy, k, LG_INK);
            }
            if (px == 0 || art[py][px - 1] == '.' || art[py][px - 1] == ' ') {
                gfx_vline(s, dx, dy, k, LG_INK);
            }
            if (px == aw - 1 || art[py][px + 1] == '.' ||
                art[py][px + 1] == ' ') {
                gfx_vline(s, dx + k - 1, dy, k, LG_INK);
            }
        }
    }
    }

}

/*
 * The mark on a pale disc.
 *
 * The ring is blue, and so are the taskbar and the launcher's header band --
 * dropped straight onto either, the C simply vanishes. The plate is a
 * PRESENTATION choice for those surfaces, kept out of sh_logo_draw so the
 * artwork itself stays exactly as designed wherever it lands on white.
 */
void sh_logo_draw_plated(GfxSurface *s, int x, int y, int size)
{
    int R = (size * 46) / 100;
    int ccx = x + (size * 44) / 100, ccy = y + (size * 46) / 100;
    int px, py;
    if (s == NULL || size < 8) { return; }
    /*
     * The plate is a pale disc that gives the PROCEDURAL mark something to sit
     * on over dark chrome, and its geometry -- centre at 44/46%, radius 46% --
     * is tuned to that drawing, where the ring sits left of centre and the
     * hills break out to the right.
     *
     * A pack image fills its own box and carries its own transparency, so the
     * same disc pokes out from behind it as a pale blob on one side. An image
     * needs no plate: it is already a finished mark.
     */
    if (lg_pack_image(size) != NULL) {
        sh_logo_draw(s, x, y, size);
        return;
    }
    for (py = ccy - R; py <= ccy + R; py++) {
        for (px = ccx - R; px <= ccx + R; px++) {
            long dx = px - ccx, dy = py - ccy;
            if (dx * dx + dy * dy <= (long)R * R) {
                gfx_put_pixel(s, px, py, GFX_RGB(0xF4, 0xF8, 0xFF));
            }
        }
    }
    sh_logo_draw(s, x, y, size);
}
