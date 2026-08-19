/*
 * test_calc.c - The calculator's state machine (calc_core.c).
 *
 * A calculator is not arithmetic. Adding two numbers is the easy part and it
 * was never where these go wrong; what goes wrong is the SEQUENCE -- an
 * operator pressed twice, equals with nothing pending, a second decimal
 * point, backspacing a number down to nothing, dividing by zero and then
 * carrying on typing. Every one of those is a keystroke a person really
 * presses, and until this file existed not one of them was checked.
 *
 * The display string is the source of truth while typing, so the checks are
 * against the STRING wherever the string is the point ("0." must survive) and
 * against the value elsewhere.
 */
#include "ctest.h"
#include "../src/apps/calc_core.h"

/* Type a whole sequence of button labels, one per string. */
static void press(Calc *c, const char *const *keys, int n)
{
    int i;
    for (i = 0; i < n; i++) { calc_press(c, keys[i]); }
}

#define SEQ(c, ...) do { \
    static const char *const k_[] = { __VA_ARGS__ }; \
    press((c), k_, (int)(sizeof k_ / sizeof k_[0])); \
} while (0)

void test_calc(void)
{
    Calc c;

    printf("- calculator\n");

    /* ---- the ordinary case -------------------------------------------- */
    calc_reset(&c);
    CHECK_STR(c.disp, "0");
    SEQ(&c, "1", "2", "+", "3", "=");
    CHECK_STR(c.disp, "15");
    SEQ(&c, "C");
    CHECK_STR(c.disp, "0");

    /* Each of the four operators. */
    calc_reset(&c); SEQ(&c, "9", "-", "4", "="); CHECK_STR(c.disp, "5");
    calc_reset(&c); SEQ(&c, "6", "*", "7", "="); CHECK_STR(c.disp, "42");
    calc_reset(&c); SEQ(&c, "8", "/", "2", "="); CHECK_STR(c.disp, "4");

    /* ---- typing ------------------------------------------------------- */
    /* A leading zero is replaced, not appended to. */
    calc_reset(&c); SEQ(&c, "0", "5"); CHECK_STR(c.disp, "5");
    /* A decimal point with nothing typed starts "0.", and it must SURVIVE --
     * a double cannot hold a trailing point, which is why the display string
     * is what the machine keeps. */
    calc_reset(&c); SEQ(&c, "."); CHECK_STR(c.disp, "0.");
    SEQ(&c, "5"); CHECK_STR(c.disp, "0.5");
    /* A second point is refused rather than making a nonsense string. */
    calc_reset(&c); SEQ(&c, "1", ".", "5", ".", "2"); CHECK_STR(c.disp, "1.52");
    /* Trailing zeros after a point survive too. */
    calc_reset(&c); SEQ(&c, "1", ".", "5", "0", "0");
    CHECK_STR(c.disp, "1.500");

    /* ---- backspace ---------------------------------------------------- */
    calc_reset(&c); SEQ(&c, "1", "2", "3", "<"); CHECK_STR(c.disp, "12");
    SEQ(&c, "<", "<"); CHECK_STR(c.disp, "0");
    /* Backspacing past the start leaves 0 and stops entering, so the next
     * digit replaces rather than appends. */
    SEQ(&c, "7"); CHECK_STR(c.disp, "7");
    /* Backspace does nothing to a computed result: it is not being typed. */
    calc_reset(&c); SEQ(&c, "2", "+", "2", "=", "<"); CHECK_STR(c.disp, "4");

    /* ---- the sequences that go wrong ----------------------------------- */
    /* An operator pressed twice must not apply anything twice; the second
     * one replaces the first. 5 + - 3 = is 2, not 8. */
    calc_reset(&c); SEQ(&c, "5", "+", "-", "3", "="); CHECK_STR(c.disp, "2");
    /* Equals with nothing pending leaves the display alone. */
    calc_reset(&c); SEQ(&c, "7", "="); CHECK_STR(c.disp, "7");
    /* Equals twice does not re-apply the operator. */
    calc_reset(&c); SEQ(&c, "2", "+", "3", "=", "="); CHECK_STR(c.disp, "5");
    /* Chaining: each operator commits the one before it. */
    calc_reset(&c); SEQ(&c, "2", "+", "3", "+", "4", "=");
    CHECK_STR(c.disp, "9");
    /* Left to right, with no precedence -- which is what a four-function
     * calculator does and what a person pressing these keys expects. */
    calc_reset(&c); SEQ(&c, "2", "+", "3", "*", "4", "=");
    CHECK_STR(c.disp, "20");

    /* ---- divide by zero, and carrying on afterwards -------------------- */
    calc_reset(&c); SEQ(&c, "5", "/", "0", "=");
    CHECK_STR(c.disp, "Error");
    CHECK(c.error);
    /* An operator on an error state is refused rather than compounding it. */
    SEQ(&c, "+"); CHECK_STR(c.disp, "Error");
    /* ...but typing a digit clears it and starts over, so the machine is not
     * stuck: a calculator you have to close and reopen is a broken one. */
    SEQ(&c, "8"); CHECK_STR(c.disp, "8"); CHECK(!c.error);
    /* C clears it too. */
    calc_reset(&c); SEQ(&c, "1", "/", "0", "=", "C");
    CHECK_STR(c.disp, "0"); CHECK(!c.error);

    /* ---- negate -------------------------------------------------------- */
    calc_reset(&c); SEQ(&c, "5", "+/-"); CHECK_STR(c.disp, "-5");
    SEQ(&c, "+/-"); CHECK_STR(c.disp, "5");
    /* Negating mid-entry keeps you in entry, so more digits still append. */
    calc_reset(&c); SEQ(&c, "1", "2", "+/-", "3");
    CHECK_STR(c.disp, "-123");

    /* ---- square root --------------------------------------------------- */
    calc_reset(&c); SEQ(&c, "9", "sqrt");       CHECK_STR(c.disp, "3");
    calc_reset(&c); SEQ(&c, "2", "5", "sqrt");  CHECK_STR(c.disp, "5");
    calc_reset(&c); SEQ(&c, "0", "sqrt");       CHECK_STR(c.disp, "0");
    calc_reset(&c); SEQ(&c, "1", "sqrt");       CHECK_STR(c.disp, "1");
    /* A big one, to show the iteration converges rather than stopping early. */
    calc_reset(&c); SEQ(&c, "1", "0", "0", "0", "0", "0", "0", "sqrt");
    CHECK_STR(c.disp, "1000");
    /* An imperfect root lands where the display can show it. The display is
     * "%g", which is six significant digits -- so this pins the precision the
     * calculator actually offers rather than a precision it does not. */
    calc_reset(&c); SEQ(&c, "2", "sqrt");
    CHECK_STR(c.disp, "1.41421");
    calc_reset(&c); SEQ(&c, "3", "sqrt");
    CHECK_STR(c.disp, "1.73205");
    /* A negative root is an error, not a silent zero or a NaN on the screen. */
    calc_reset(&c); SEQ(&c, "4", "+/-", "sqrt");
    CHECK_STR(c.disp, "Error");

    /* ---- reciprocal ----------------------------------------------------- */
    calc_reset(&c); SEQ(&c, "4", "1/x");   CHECK_STR(c.disp, "0.25");
    calc_reset(&c); SEQ(&c, "0", "1/x");   CHECK_STR(c.disp, "Error");
    /* 1/x twice returns where it started. */
    calc_reset(&c); SEQ(&c, "8", "1/x", "1/x"); CHECK_STR(c.disp, "8");

    /* ---- percent -------------------------------------------------------- */
    /* Percent OF the pending left-hand side, which is what the key has meant
     * on desk calculators forever: 200 + 10% is 200 + 20. */
    calc_reset(&c); SEQ(&c, "2", "0", "0", "+", "1", "0", "%");
    CHECK_STR(c.disp, "20");
    SEQ(&c, "="); CHECK_STR(c.disp, "220");
    /* With nothing pending there is nothing to take a percentage OF, so it
     * is a plain divide by a hundred. */
    calc_reset(&c); SEQ(&c, "5", "0", "%"); CHECK_STR(c.disp, "0.5");

    /* ---- memory --------------------------------------------------------- */
    calc_reset(&c);
    CHECK(!c.mem_set);
    SEQ(&c, "4", "2", "MS");
    CHECK(c.mem_set);
    SEQ(&c, "C");
    /* C clears the working state and leaves memory alone -- every calculator
     * ever made behaves this way, and one that forgot would be maddening. */
    CHECK(c.mem_set);
    SEQ(&c, "MR"); CHECK_STR(c.disp, "42");
    /* M+ accumulates. */
    calc_reset(&c);
    SEQ(&c, "1", "0", "MS", "5", "M+", "C", "MR");
    CHECK_STR(c.disp, "15");
    /* MC empties it. */
    SEQ(&c, "MC");
    CHECK(!c.mem_set);
    SEQ(&c, "C", "MR"); CHECK_STR(c.disp, "0");
    /* A recalled value is editable: it is a number you just entered. */
    calc_reset(&c); SEQ(&c, "1", "2", "MS", "C", "MR", "3");
    CHECK_STR(c.disp, "123");
    /* And it takes part in arithmetic. */
    calc_reset(&c); SEQ(&c, "7", "MS", "C", "MR", "+", "3", "=");
    CHECK_STR(c.disp, "10");

    /* ---- an unknown label is refused, loudly ---------------------------- */
    /* A button added to the grid and never wired up would otherwise do
     * nothing at all and look like a dead key. */
    calc_reset(&c);
    CHECK(calc_press(&c, "7"));
    CHECK(calc_press(&c, "sqrt"));
    CHECK(!calc_press(&c, "xyzzy"));
    CHECK(!calc_press(&c, ""));
    CHECK(!calc_press(&c, "MX"));

    /* ---- refusals ------------------------------------------------------- */
    calc_press(NULL, "1");          /* no crash */
    calc_digit(NULL, 1);
    calc_clear(NULL);
    calc_reset(NULL);
    calc_sqrt(NULL);
    calc_mem_add(NULL);
    CHECK(calc_value(NULL) == 0.0);
    /* A digit outside 0..9 is not a keystroke any button produces. */
    calc_reset(&c); calc_digit(&c, 10); CHECK_STR(c.disp, "0");
    calc_digit(&c, -1); CHECK_STR(c.disp, "0");

    /* ---- the keypad, as cells ------------------------------------------ *
     *
     * The grid gives '=', '+' and '0' several adjacent cells so they come out
     * as the tall and wide keys a desk calculator has. The window drew a
     * complete bevelled button in EVERY cell, so a person saw two '+' keys,
     * three '=' keys and two '0' keys, each in its own frame with a gap down
     * the middle -- it looked like the table had been filled in wrong.
     *
     * The property that makes that impossible is a tiling one: every cell
     * belongs to exactly one key, and no key covers a cell that is not its
     * own. Checked here rather than by looking at pixels, because it is not
     * about pixels -- and because a span that turned a corner would silently
     * paint one key over its neighbour.
     */
    {
        int cover[CALC_KEY_ROWS][CALC_KEY_COLS];
        int r, c2, keys = 0, cells = 0;
        for (r = 0; r < CALC_KEY_ROWS; r++) {
            for (c2 = 0; c2 < CALC_KEY_COLS; c2++) { cover[r][c2] = 0; }
        }
        for (r = 0; r < CALC_KEY_ROWS; r++) {
            for (c2 = 0; c2 < CALC_KEY_COLS; c2++) {
                int rs = -1, cs = -1, i, j;
                if (!calc_key_span(r, c2, &rs, &cs)) { continue; }
                keys++;
                CHECK(rs >= 1 && cs >= 1);
                /* A span never leaves the grid... and if it claims to, the
                 * claim is clamped before it is used as an index: a lying
                 * span would otherwise walk this test's own array off its end
                 * and report as a memory fault instead of as the wrong
                 * answer it is. */
                CHECK(r + rs <= CALC_KEY_ROWS);
                CHECK(c2 + cs <= CALC_KEY_COLS);
                if (r + rs > CALC_KEY_ROWS) { rs = CALC_KEY_ROWS - r; }
                if (c2 + cs > CALC_KEY_COLS) { cs = CALC_KEY_COLS - c2; }
                for (i = 0; i < rs; i++) {
                    for (j = 0; j < cs; j++) {
                        /* ...and never lands on a cell another key already
                         * covers, which is what "no key is drawn over its
                         * neighbour" means. */
                        CHECK_EQI(cover[r + i][c2 + j], 0);
                        cover[r + i][c2 + j] = 1;
                        /* Every cell a key covers carries that key's label:
                         * a bounding box around an L-shaped run would pick up
                         * a cell belonging to something else. */
                        CHECK_STR(calc_key_label(r + i, c2 + j),
                                  calc_key_label(r, c2));
                    }
                }
            }
        }
        /* Every cell covered exactly once. */
        for (r = 0; r < CALC_KEY_ROWS; r++) {
            for (c2 = 0; c2 < CALC_KEY_COLS; c2++) {
                CHECK_EQI(cover[r][c2], 1);
                cells++;
            }
        }
        CHECK_EQI(cells, 30);
        /* Twenty-six keys in thirty cells: four cells are continuations --
         * one for '0', one for '+', two for '='. A count of 30 here means
         * nothing is being merged and the duplicates are back. */
        CHECK_EQI(keys, 26);

        /* ...and the three that span, by name, so the shapes are pinned and
         * not merely self-consistent. */
        {
            int rs = 0, cs = 0;
            CHECK(calc_key_span(3, 4, &rs, &cs));       /* '=' */
            CHECK_STR(calc_key_label(3, 4), "=");
            CHECK_EQI(rs, 3); CHECK_EQI(cs, 1);
            CHECK(calc_key_span(3, 3, &rs, &cs));       /* '+' */
            CHECK_STR(calc_key_label(3, 3), "+");
            CHECK_EQI(rs, 2); CHECK_EQI(cs, 1);
            CHECK(calc_key_span(5, 0, &rs, &cs));       /* '0' */
            CHECK_STR(calc_key_label(5, 0), "0");
            CHECK_EQI(rs, 1); CHECK_EQI(cs, 2);
            /* The cells those three swallow are continuations. */
            CHECK(!calc_key_span(4, 4, &rs, &cs));
            CHECK(!calc_key_span(5, 4, &rs, &cs));
            CHECK(!calc_key_span(4, 3, &rs, &cs));
            CHECK(!calc_key_span(5, 1, &rs, &cs));
        }

        /* Every key on the grid is one the machine answers to -- the check
         * that caught a grid with no '=' at all. */
        for (r = 0; r < CALC_KEY_ROWS; r++) {
            for (c2 = 0; c2 < CALC_KEY_COLS; c2++) {
                Calc k;
                calc_reset(&k);
                CHECK(calc_press(&k, calc_key_label(r, c2)));
            }
        }
        /* Out of range is empty, not a crash or a stray pointer. */
        CHECK_STR(calc_key_label(-1, 0), "");
        CHECK_STR(calc_key_label(0, -1), "");
        CHECK_STR(calc_key_label(CALC_KEY_ROWS, 0), "");
        CHECK_STR(calc_key_label(0, CALC_KEY_COLS), "");
        CHECK(!calc_key_span(-1, 0, NULL, NULL));
        CHECK(!calc_key_span(CALC_KEY_ROWS, CALC_KEY_COLS, NULL, NULL));
    }
}
