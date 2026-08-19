/*
 * sh_cursor.c - Software mouse cursor.
 *
 * The cursor is drawn last, over the composited scene. The arrow is original
 * Castalia art.
 *
 * There is deliberately NO save-under, which is the usual way to do this and
 * was how this file worked for a long time. It saved the pixels under the
 * arrow and put them back at the start of the next frame -- and every one of
 * those restored pixels was then painted over by the compositor, in the same
 * frame, before anything could see them.
 *
 * That is structural, not a coincidence of the current layout. sh_core.c adds
 * the cursor's previous rectangle to the repaint region, and it has no choice:
 * that region is also the list handed to plat_present(), so a rectangle left
 * out of it is a rectangle the physical framebuffer never hears about -- the
 * old arrow would stay on the screen no matter what the back buffer said.
 * Being in the region means being re-composited from the desktop up. So the
 * restore could never be the thing that erased anything.
 *
 * Removing it saves 504,221 instructions over a 40-frame run -- about 12,600
 * a frame, against a steady-state frame of roughly 50,100 -- spent producing
 * pixels nobody ever read. And it leaves every screenshot byte-for-byte
 * identical, which is what makes the reasoning above a measurement rather
 * than an argument.
 */
#include "castalia/shell.h"
#include "castalia/gfx.h"
#include "castalia/sys.h"

#define CUR_W 12
#define CUR_H 17
#define CUR_SH 3               /* drop-shadow offset (glossy mode)             */
#define CUR_BW (CUR_W + CUR_SH) /* occupied width incl. the shadow             */
#define CUR_BH (CUR_H + CUR_SH) /* occupied height                             */

/* ' ' transparent, 'X' outline (dark), '.' fill (light). */
static const char *g_arrow[CUR_H] = {
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X.....XXXXX ",
    "X..X..X     ",
    "X.X X..X    ",
    "XX  X..X    ",
    "X    X..X   ",
    "     X..X   ",
    "      XX    "
};

static int g_x = 0, g_y = 0;

/* The arrow baked into a keyed sprite, built once (see sh_cursor_draw). */
static CColor     g_art_px[CUR_W * CUR_H];
static GfxSurface g_art;
static cbool      g_art_ready = CFALSE;

static void cursor_build_shadow(void);   /* defined with the shadow, below */

static void cursor_build_art(CColor outline, CColor fill)
{
    int row, col;
    gfx_surface_wrap(&g_art, g_art_px, CUR_W, CUR_H, CUR_W);
    for (row = 0; row < CUR_H; row++) {
        const char *line = g_arrow[row];
        for (col = 0; col < CUR_W; col++) {
            char c = line[col];
            g_art_px[row * CUR_W + col] =
                (c == 'X') ? outline : ((c == '.') ? fill : GFX_COLORKEY);
        }
    }
    g_art_ready = CTRUE;
}

void sh_cursor_init(void)     { g_x = 0; g_y = 0; cursor_build_shadow(); }
void sh_cursor_shutdown(void) { }

void sh_cursor_set_pos(int x, int y) { g_x = x; g_y = y; }

CRect sh_cursor_bounds(void)
{
    /* The shadow margin is included: this rectangle is what the compositor
     * repaints and what plat_present() pushes, and both have to cover every
     * pixel the arrow touched or the last frame's cursor stays on screen. */
    return crect_make(g_x, g_y, CUR_BW, CUR_BH);
}

/*
 * The drop shadow, precomputed.
 *
 * Drawn the obvious way -- three darkening steps per inked cell of the arrow,
 * each reading a pixel, tinting it and writing it back -- this was 28,535
 * instructions a frame, measured: 76% of everything an idle desktop does. The
 * cost is not the arithmetic, it is doing it 300 times through gfx_get_pixel
 * and gfx_put_pixel, each of which bounds-checks and recomputes a row pointer.
 *
 * Neighbouring cells overlap, so a pixel could be darkened up to three times
 * in sequence. That is not a bug to fix -- it is what gives the shadow its
 * falloff -- but it does mean the answer for a given pixel depends only on
 * WHICH steps land on it and in what order, and that is fixed by the arrow
 * art. So it is worked out once: every pixel of the shadow box gets a slot
 * number, and every slot gets a 256-entry table mapping a channel value to
 * its final darkened value. Drawing is then one read, three table lookups and
 * one write per pixel, roughly 150 pixels instead of 300 read-modify-writes.
 *
 * The tables replay the ORIGINAL steps in the ORIGINAL order rather than
 * folding them into a single multiplier, which was the first attempt: folding
 * loses up to 2 per channel to truncation, and while nobody could see that in
 * a drop shadow, it would have cost the byte-for-byte screenshot comparison
 * that is the only real evidence a pure-speed change did not change anything.
 *
 * Result, measured the same way as the cost: the shadow drops from 28,535
 * instructions a frame to 9,872, and an idle frame from 37,380 to 18,717 --
 * twice as fast for doing exactly the same thing, with every screenshot
 * byte-for-byte identical. The tables cost 30,461 instructions to build, once.
 */
#define CUR_SH_SLOTS 16        /* geometry allows at most 9; see below */
#define CUR_SH_MAXSEQ 3

static unsigned char g_sh_lut[CUR_SH_SLOTS][256];
static unsigned char g_sh_slot[CUR_BH][CUR_BW];
static int   g_sh_slots = 1;               /* slot 0 means "leave this pixel" */
static cbool g_sh_ready = CFALSE;

/* The slot holding exactly this sequence of darkening steps, creating it if
 * this is the first pixel to need it.
 *
 * Slots are matched on the sequence itself, not on the table it produces.
 * That is stricter than it needs to be -- two different orders of the same
 * steps sometimes agree on all 256 inputs and would then get a slot each --
 * but comparing three bytes beats comparing two 256-entry tables by enough to
 * matter: the table-matching version cost about a million instructions to
 * build, which is nothing on a host and forty milliseconds of startup on the
 * 25 MHz machine this targets. Sixteen slots is room to spare for nine. */
static int cursor_shadow_slot(const unsigned char *seq, int n)
{
    static unsigned char known[CUR_SH_SLOTS][CUR_SH_MAXSEQ];
    static unsigned char known_n[CUR_SH_SLOTS];
    int i, k, v;

    for (i = 1; i < g_sh_slots; i++) {
        int same = ((int)known_n[i] == n);
        for (k = 0; same && k < n; k++) {
            if (known[i][k] != seq[k]) { same = 0; }
        }
        if (same) { return i; }
    }
    if (g_sh_slots >= CUR_SH_SLOTS) {
        /* Unreachable with this art: each cell contributes its 110 step to one
         * pixel and its two 55 steps to two others, so no pixel can collect
         * more than one of each kind and there are at most nine orderings.
         * Said out loud anyway, because a silently dropped shadow pixel is a
         * strange-looking cursor nobody would think to blame on a table. */
        SYS_LOGW("shell", "cursor shadow needs more than %d slots; "
                 "some pixels will not be darkened", CUR_SH_SLOTS);
        return 0;
    }
    i = g_sh_slots++;
    known_n[i] = (unsigned char)n;
    for (k = 0; k < n; k++) { known[i][k] = seq[k]; }
    for (v = 0; v < 256; v++) {
        int x = v;
        for (k = 0; k < n; k++) { x = (x * (256 - (int)seq[k])) >> 8; }
        g_sh_lut[i][v] = (unsigned char)x;
    }
    return i;
}

static void cursor_build_shadow(void)
{
    /* On the stack, not in BSS: this runs once and 1.2 KB of permanently
     * reserved scratch is not worth saving a moment of startup. */
    unsigned char seq[CUR_BH][CUR_BW][CUR_SH_MAXSEQ];
    unsigned char cnt[CUR_BH][CUR_BW];
    int row, col, i, j;

    for (row = 0; row < CUR_BH; row++) {
        for (col = 0; col < CUR_BW; col++) { cnt[row][col] = 0; }
    }

    /* Walk the cells in the same order the drawing loop used to, so the steps
     * land on each pixel in the same sequence and the tables reproduce it. */
    for (row = 0; row < CUR_H; row++) {
        const char *line = g_arrow[row];
        for (col = 0; col < CUR_W; col++) {
            int px[CUR_SH_MAXSEQ], py[CUR_SH_MAXSEQ], pa[CUR_SH_MAXSEQ];
            if (line[col] == ' ') { continue; }
            px[0] = col + CUR_SH;     py[0] = row + CUR_SH;     pa[0] = 110;
            px[1] = col + CUR_SH - 1; py[1] = row + CUR_SH;     pa[1] = 55;
            px[2] = col + CUR_SH;     py[2] = row + CUR_SH - 1; pa[2] = 55;
            for (i = 0; i < CUR_SH_MAXSEQ; i++) {
                int n;
                if (px[i] < 0 || px[i] >= CUR_BW ||
                    py[i] < 0 || py[i] >= CUR_BH) { continue; }
                n = (int)cnt[py[i]][px[i]];
                if (n < CUR_SH_MAXSEQ) {
                    seq[py[i]][px[i]][n] = (unsigned char)pa[i];
                    cnt[py[i]][px[i]] = (unsigned char)(n + 1);
                }
            }
        }
    }

    for (i = 0; i < CUR_BH; i++) {
        for (j = 0; j < CUR_BW; j++) {
            g_sh_slot[i][j] = (cnt[i][j] == 0)
                ? 0
                : (unsigned char)cursor_shadow_slot(seq[i][j], (int)cnt[i][j]);
        }
    }
    g_sh_ready = CTRUE;
}

void sh_cursor_draw(GfxSurface *s)
{
    const CColor outline = GFX_RGB(20, 22, 28);
    const CColor fill    = GFX_RGB(245, 247, 250);
    cbool glossy = sh_glossy();
    int row;

    /* Soft drop shadow first (glossy mode), offset down-right, so the arrow
     * draws crisply on top. */
    if (glossy) {
        if (!g_sh_ready) { cursor_build_shadow(); }
        for (row = 0; row < CUR_BH; row++) {
            int col;
            for (col = 0; col < CUR_BW; col++) {
                int slot = (int)g_sh_slot[row][col];
                CColor c;
                const unsigned char *lut;
                if (slot == 0) { continue; }
                lut = g_sh_lut[slot];
                c = gfx_get_pixel(s, g_x + col, g_y + row);
                gfx_put_pixel(s, g_x + col, g_y + row,
                              GFX_RGB(lut[GFX_R(c)], lut[GFX_G(c)],
                                      lut[GFX_B(c)]));
            }
        }
    }

    /*
     * The arrow itself, as one keyed blit rather than 204 gfx_put_pixel calls.
     *
     * It is a fixed two-colour sprite with a transparent background, which is
     * precisely what GFX_BLIT_KEYED already does -- row-wise, with the clip
     * worked out once instead of per pixel. Baking it costs 204 stores, once.
     * Reaching for the existing primitive rather than a hand-rolled loop also
     * means the arrow is covered by tests/test_blit.c, which checks the keyed
     * path in both directions.
     */
    if (!g_art_ready) { cursor_build_art(outline, fill); }
    gfx_blit(s, g_x, g_y, &g_art, NULL, GFX_BLIT_KEYED);
}
