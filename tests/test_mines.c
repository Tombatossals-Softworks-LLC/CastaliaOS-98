/*
 * test_mines.c - Mines game logic (mines_core.c): setup, adjacency counts,
 * flood reveal, flags, win/lose transitions, first-click safety, and RNG
 * determinism. Pure logic, no gfx/wm.
 */
#include "ctest.h"
#include "mines_core.h"

/* A 5x5 layout with two mines:            adjacency ground truth:
 *   . . . . .        1 1 1 0 0
 *   . M . . .        1 M 1 0 0
 *   . . . . .        1 1 2 1 1
 *   . . . M .        0 0 1 M 1
 *   . . . . .        0 0 1 1 1   */
static const unsigned char LAYOUT_5x5[25] = {
    0,0,0,0,0,
    0,1,0,0,0,
    0,0,0,0,0,
    0,0,0,1,0,
    0,0,0,0,0
};

static void mines_setup(void)
{
    MinesGame g;
    mines_init(&g, 9, 9, 10, 42u);
    CHECK_EQI(g.w, 9);
    CHECK_EQI(g.h, 9);
    CHECK_EQI(g.mines, 10);
    CHECK_EQI((int)g.status, (int)MINES_PLAYING);
    CHECK_EQI(mines_flags_left(&g), 10);
    CHECK(!mines_is_open(&g, 0, 0));
    CHECK(!mines_is_mine(&g, 0, 0));   /* nothing placed until first reveal */

    /* Bounds clamping: oversized boards and absurd mine counts are tamed. */
    mines_init(&g, 99, 99, 5000, 1u);
    CHECK_EQI(g.w, MINES_MAX_W);
    CHECK_EQI(g.h, MINES_MAX_H);
    CHECK_EQI(g.mines, MINES_MAX_W * MINES_MAX_H - 1);
    mines_init(&g, 0, 0, 0, 1u);
    CHECK_EQI(g.w, 2);
    CHECK_EQI(g.h, 2);
    CHECK_EQI(g.mines, 1);
}

static void mines_adjacency(void)
{
    MinesGame g;
    mines_init(&g, 5, 5, 2, 1u);
    mines_set_layout(&g, LAYOUT_5x5);
    CHECK_EQI(g.mines, 2);
    CHECK(mines_is_mine(&g, 1, 1));
    CHECK(mines_is_mine(&g, 3, 3));
    CHECK_EQI(mines_adjacent(&g, 0, 0), 1);
    CHECK_EQI(mines_adjacent(&g, 2, 0), 1);
    CHECK_EQI(mines_adjacent(&g, 3, 0), 0);
    CHECK_EQI(mines_adjacent(&g, 2, 2), 2);   /* touches both mines */
    CHECK_EQI(mines_adjacent(&g, 4, 4), 1);
    CHECK_EQI(mines_adjacent(&g, 0, 3), 0);
    /* Out-of-range reads are inert. */
    CHECK_EQI(mines_adjacent(&g, -1, 0), 0);
    CHECK_EQI(mines_adjacent(&g, 0, 99), 0);
}

static void mines_flood(void)
{
    MinesGame g;
    mines_init(&g, 5, 5, 2, 1u);
    mines_set_layout(&g, LAYOUT_5x5);

    /* Reveal the far zero corner: the zero region spanning the right/bottom
     * opens along with its numbered boundary; the mines stay covered. */
    CHECK(mines_reveal(&g, 4, 0));
    CHECK_EQI((int)g.status, (int)MINES_PLAYING);
    CHECK(mines_is_open(&g, 4, 0));
    CHECK(mines_is_open(&g, 3, 0));   /* zero region */
    CHECK(mines_is_open(&g, 2, 0));   /* boundary number 1 */
    CHECK(mines_is_open(&g, 4, 2));   /* boundary number 1 */
    CHECK(!mines_is_open(&g, 1, 1));  /* mine stays covered */
    CHECK(!mines_is_open(&g, 0, 0));  /* disconnected from that zero region */

    /* A numbered cell reveals only itself. */
    CHECK(mines_reveal(&g, 0, 0));
    CHECK(mines_is_open(&g, 0, 0));
    CHECK(!mines_is_open(&g, 0, 1));

    /* Revealing an already-open cell is a harmless no-op. */
    CHECK(mines_reveal(&g, 0, 0));
}

static void mines_flags(void)
{
    MinesGame g;
    mines_init(&g, 5, 5, 2, 1u);
    mines_set_layout(&g, LAYOUT_5x5);

    mines_toggle_flag(&g, 1, 1);
    CHECK(mines_is_flag(&g, 1, 1));
    CHECK_EQI(mines_flags_left(&g), 1);
    /* A flagged cell refuses to reveal (misclick guard). */
    CHECK(mines_reveal(&g, 1, 1));
    CHECK_EQI((int)g.status, (int)MINES_PLAYING);
    CHECK(!mines_is_open(&g, 1, 1));
    /* Toggle off. */
    mines_toggle_flag(&g, 1, 1);
    CHECK(!mines_is_flag(&g, 1, 1));
    CHECK_EQI(mines_flags_left(&g), 2);
    /* Can't flag an opened cell. */
    CHECK(mines_reveal(&g, 0, 0));
    mines_toggle_flag(&g, 0, 0);
    CHECK(!mines_is_flag(&g, 0, 0));
}

static void mines_lose(void)
{
    MinesGame g;
    mines_init(&g, 5, 5, 2, 1u);
    mines_set_layout(&g, LAYOUT_5x5);
    CHECK(!mines_reveal(&g, 1, 1));   /* stepped on it */
    CHECK_EQI((int)g.status, (int)MINES_LOST);
    CHECK_EQI(g.burst, 1 * 5 + 1);
    /* All mines are exposed for the post-mortem. */
    CHECK(mines_is_open(&g, 3, 3));
    /* The game is over: further input is inert. */
    CHECK(mines_reveal(&g, 0, 0));
    CHECK(!mines_is_open(&g, 0, 0));
    mines_toggle_flag(&g, 0, 0);
    CHECK(!mines_is_flag(&g, 0, 0));
}

static void mines_win(void)
{
    MinesGame g;
    int x, y;
    mines_init(&g, 5, 5, 2, 1u);
    mines_set_layout(&g, LAYOUT_5x5);
    for (y = 0; y < 5; y++) {
        for (x = 0; x < 5; x++) {
            if (!mines_is_mine(&g, x, y)) { CHECK(mines_reveal(&g, x, y)); }
        }
    }
    CHECK_EQI((int)g.status, (int)MINES_WON);
    /* Victory auto-flags the mines and zeroes the counter. */
    CHECK(mines_is_flag(&g, 1, 1));
    CHECK(mines_is_flag(&g, 3, 3));
    CHECK_EQI(mines_flags_left(&g), 0);
}

static void mines_first_click_safe(void)
{
    unsigned seed;
    for (seed = 1u; seed <= 25u; seed++) {
        MinesGame g;
        int x, y, count = 0;
        mines_init(&g, 9, 9, 10, seed);
        CHECK(mines_reveal(&g, 4, 4));            /* never a mine */
        CHECK(mines_is_open(&g, 4, 4));
        CHECK_EQI((int)g.status == (int)MINES_LOST ? 1 : 0, 0);
        for (y = 0; y < 9; y++) {
            for (x = 0; x < 9; x++) {
                if (mines_is_mine(&g, x, y)) { count++; }
            }
        }
        CHECK_EQI(count, 10);                     /* exactly 10 placed */
    }
}

static void mines_deterministic(void)
{
    MinesGame a, b;
    int x, y, same = 1;
    mines_init(&a, 9, 9, 10, 1234u);
    mines_init(&b, 9, 9, 10, 1234u);
    mines_reveal(&a, 0, 0);
    mines_reveal(&b, 0, 0);
    for (y = 0; y < 9; y++) {
        for (x = 0; x < 9; x++) {
            if (mines_is_mine(&a, x, y) != mines_is_mine(&b, x, y)) { same = 0; }
        }
    }
    CHECK_EQI(same, 1);   /* same seed + same first click => same board */
}

void test_mines(void)
{
    printf("- mines\n");
    mines_setup();
    mines_adjacency();
    mines_flood();
    mines_flags();
    mines_lose();
    mines_win();
    mines_first_click_safe();
    mines_deterministic();
}
