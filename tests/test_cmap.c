/*
 * test_cmap.c - The Character Map's grid (cmap_core.c).
 *
 * The whole reason this file exists is the LAST section. Everything above it
 * is ordinary grid arithmetic; the bottom is a range that is not 32..126, and
 * that is the case no test using the real font could ever have produced,
 * because the real font is 32..126 and so was the constant it was compared
 * against. The two agreed by coincidence, so the bug was invisible.
 */
#include "ctest.h"
#include "../src/apps/cmap_core.h"

void test_cmap(void)
{
    CmapGrid g;
    int row, col;

    printf("- character map grid\n");

    /* ---- the shape of today's map -------------------------------------- */
    cmap_grid_init(&g, 32, 126, 16);
    CHECK_EQI(cmap_count(&g), 95);
    /* 95 codes in rows of 16 is five full rows and a partial sixth. Dropping
     * that partial row is how the last fifteen characters of a font become
     * unreachable, so it is pinned. */
    CHECK_EQI(g.rows, 6);
    CHECK_EQI(cmap_code_at(&g, 0, 0), 32);
    CHECK_EQI(cmap_code_at(&g, 0, 15), 47);
    CHECK_EQI(cmap_code_at(&g, 1, 0), 48);
    CHECK_EQI(cmap_code_at(&g, 5, 14), 126);
    /* The one cell past the end of the partial last row has no character. */
    CHECK_EQI(cmap_code_at(&g, 5, 15), -1);
    /* ...and neither does anything off the grid entirely. */
    CHECK_EQI(cmap_code_at(&g, 6, 0), -1);
    CHECK_EQI(cmap_code_at(&g, 0, 16), -1);
    CHECK_EQI(cmap_code_at(&g, -1, 0), -1);
    CHECK_EQI(cmap_code_at(&g, 0, -1), -1);

    /* Every code in range has a cell, and it is the cell that names it. */
    {
        int c;
        for (c = 32; c <= 126; c++) {
            CHECK(cmap_cell_of(&g, c, &row, &col));
            CHECK_EQI(cmap_code_at(&g, row, col), c);
        }
    }
    CHECK(!cmap_cell_of(&g, 31, &row, &col));
    CHECK(!cmap_cell_of(&g, 127, &row, &col));

    /* ---- moving the highlight ------------------------------------------- */
    CHECK_EQI(cmap_move(&g, 65, 1, 0), 66);
    CHECK_EQI(cmap_move(&g, 65, -1, 0), 64);
    CHECK_EQI(cmap_move(&g, 65, 0, 1), 81);
    CHECK_EQI(cmap_move(&g, 65, 0, -1), 49);
    /* At the edges the move is refused, not clamped somewhere else. */
    CHECK_EQI(cmap_move(&g, 32, -1, 0), 32);
    CHECK_EQI(cmap_move(&g, 32, 0, -1), 32);
    CHECK_EQI(cmap_move(&g, 126, 1, 0), 126);
    CHECK_EQI(cmap_move(&g, 126, 0, 1), 126);
    /*
     * The interesting edge: 111 sits in the second-to-last row at the column
     * where the last row has already run out. Moving down would land on 127,
     * which is not a character, so it must not move at all -- a highlight on
     * an empty cell is a highlight on nothing.
     */
    CHECK_EQI(cmap_code_at(&g, 4, 15), 111);
    CHECK_EQI(cmap_move(&g, 111, 0, 1), 111);
    /* One column left of it, though, the last row does have a cell. */
    CHECK_EQI(cmap_move(&g, 110, 0, 1), 126);

    CHECK_EQI(cmap_home(&g), 32);
    CHECK_EQI(cmap_end(&g), 126);

    /* ---- clicks ---------------------------------------------------------- */
    CHECK_EQI(cmap_hit(&g, 0, 0, 21, 16), 32);
    CHECK_EQI(cmap_hit(&g, 20, 15, 21, 16), 32);   /* still the first cell   */
    CHECK_EQI(cmap_hit(&g, 21, 0, 21, 16), 33);    /* one pixel further, next */
    CHECK_EQI(cmap_hit(&g, 0, 16, 21, 16), 48);    /* second row             */
    /* The empty cell of the partial last row is not a character to click. */
    CHECK_EQI(cmap_hit(&g, 15 * 21, 5 * 16, 21, 16), -1);
    CHECK_EQI(cmap_hit(&g, 14 * 21, 5 * 16, 21, 16), 126);
    /* Outside, and degenerate cells. */
    CHECK_EQI(cmap_hit(&g, -1, 0, 21, 16), -1);
    CHECK_EQI(cmap_hit(&g, 0, -1, 21, 16), -1);
    CHECK_EQI(cmap_hit(&g, 16 * 21, 0, 21, 16), -1);
    CHECK_EQI(cmap_hit(&g, 0, 0, 0, 16), -1);
    CHECK_EQI(cmap_hit(&g, 0, 0, 21, 0), -1);

    /* ---- a font that is NOT 32..126 -------------------------------------- *
     *
     * This is the section that matters. gfx_font_range() exists so the map
     * follows the font, and its own comment says what happens otherwise:
     * "the day a face arrives with more in it is the day a hard-coded 126
     * somewhere else becomes a bug nobody is looking for". That hard-coded
     * 126 was in the Character Map's key handler one file away, where a typed
     * character jumped the highlight to `ch` whenever `ch <= 126` regardless
     * of what the font held.
     *
     * With today's font the two agree exactly, so nothing misbehaves and no
     * test built on the real font could tell. Widen the range and the bug
     * appears immediately: a character above 126 that the font DOES have is
     * unreachable by typing, and narrow it and a character the font does NOT
     * have becomes the selection.
     */
    cmap_grid_init(&g, 32, 255, 16);
    CHECK_EQI(cmap_count(&g), 224);
    CHECK_EQI(g.rows, 14);
    CHECK_EQI(cmap_code_at(&g, 13, 15), 255);
    /* A code the wider font really has must be reachable by typing it. */
    CHECK_EQI(cmap_jump(&g, 200), 200);
    CHECK_EQI(cmap_jump(&g, 255), 255);
    /* And the highlight must be able to walk all the way to it. */
    CHECK_EQI(cmap_move(&g, 239, 0, 1), 255);
    CHECK_EQI(cmap_move(&g, 255, 1, 0), 255);

    /* A font NARROWER than ASCII: now a printable character is one the font
     * cannot draw, and jumping to it must be refused rather than selecting a
     * cell that does not exist. */
    cmap_grid_init(&g, 32, 90, 16);
    CHECK_EQI(cmap_jump(&g, 65), 65);
    CHECK_EQI(cmap_jump(&g, 122), -1);   /* 'z', beyond this font */
    CHECK_EQI(cmap_jump(&g, 126), -1);
    CHECK_EQI(cmap_jump(&g, 31), -1);
    /* A font that does not start at 32 either. */
    cmap_grid_init(&g, 48, 57, 16);
    CHECK_EQI(cmap_count(&g), 10);
    CHECK_EQI(g.rows, 1);
    CHECK_EQI(cmap_jump(&g, 32), -1);    /* space, which this font lacks */
    CHECK_EQI(cmap_jump(&g, 53), 53);
    CHECK_EQI(cmap_code_at(&g, 0, 9), 57);
    CHECK_EQI(cmap_code_at(&g, 0, 10), -1);

    /* ---- degenerate grids ------------------------------------------------ */
    /* One character. It is still a row, and it is still clickable. */
    cmap_grid_init(&g, 65, 65, 16);
    CHECK_EQI(cmap_count(&g), 1);
    CHECK_EQI(g.rows, 1);
    CHECK_EQI(cmap_code_at(&g, 0, 0), 65);
    CHECK_EQI(cmap_code_at(&g, 0, 1), -1);
    CHECK_EQI(cmap_move(&g, 65, 1, 0), 65);
    CHECK_EQI(cmap_move(&g, 65, 0, 1), 65);
    /* A reversed range is not a negative count. */
    cmap_grid_init(&g, 90, 40, 16);
    CHECK_EQI(cmap_count(&g), 1);
    CHECK(g.rows >= 1);
    /* No columns is not a division by zero. */
    cmap_grid_init(&g, 32, 126, 0);
    CHECK(g.cols >= 1);
    CHECK(g.rows >= 1);
    CHECK_EQI(cmap_code_at(&g, 0, 0), 32);

    /* ---- refusals -------------------------------------------------------- */
    cmap_grid_init(NULL, 32, 126, 16);       /* no crash */
    CHECK_EQI(cmap_count(NULL), 0);
    CHECK_EQI(cmap_code_at(NULL, 0, 0), -1);
    CHECK(!cmap_cell_of(NULL, 65, &row, &col));
    CHECK_EQI(cmap_move(NULL, 65, 1, 0), 65);
    CHECK_EQI(cmap_jump(NULL, 65), -1);
    CHECK_EQI(cmap_hit(NULL, 0, 0, 21, 16), -1);
    CHECK_EQI(cmap_home(NULL), 0);
    CHECK_EQI(cmap_end(NULL), 0);
    /* A cell lookup that wants neither output still answers the question. */
    cmap_grid_init(&g, 32, 126, 16);
    CHECK(cmap_cell_of(&g, 65, NULL, NULL));
    /* Moving a code that is not on the map leaves it alone. */
    CHECK_EQI(cmap_move(&g, 200, -1, 0), 200);
}
