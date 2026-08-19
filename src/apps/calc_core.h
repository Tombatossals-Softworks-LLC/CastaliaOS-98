/*
 * calc_core.h - The calculator, without the buttons.
 *
 * Every keystroke a calculator takes is a state transition over four things:
 * the display string, an accumulator, a pending operator, and whether a
 * number is currently being typed. That is the whole machine, and none of it
 * needs a window -- which is what lets tests/test_calc.c drive the sequences
 * that go wrong (an operator pressed twice, equals with nothing pending, a
 * second decimal point, a divide by zero followed by more typing) instead of
 * hoping somebody clicks them.
 *
 * The display STRING is the source of truth while typing, not a number. That
 * is deliberate and it is what makes "0." and "1.500" behave: a double cannot
 * remember a trailing point or a trailing zero, and a calculator that drops
 * them the moment you press the key feels broken.
 */
#ifndef CASTALIA_CALC_CORE_H
#define CASTALIA_CALC_CORE_H

#include "castalia/ctypes.h"

#define CALC_DISP_MAX 32

typedef struct {
    char   disp[CALC_DISP_MAX];  /* what the screen shows                   */
    double acc;                  /* the running left-hand side              */
    int    op;                   /* 0, or '+','-','*','/'                   */
    cbool  entering;             /* a number is being typed right now       */
    cbool  error;                /* division by zero, or a bad root         */
    double mem;                  /* the memory register                     */
    cbool  mem_set;              /* ...and whether anything is in it        */
} Calc;

/* Reset to a cleared calculator. Memory is NOT cleared: on every calculator
 * ever made, C clears the working state and leaves the memory alone. */
void calc_clear(Calc *c);
/* Clear everything including memory -- what a fresh window starts from. */
void calc_reset(Calc *c);

/* The value currently displayed. */
double calc_value(const Calc *c);

/* Keystrokes. Each is exactly what the button of that name does. */
void calc_digit(Calc *c, int d);        /* 0..9                            */
void calc_dot(Calc *c);
void calc_op(Calc *c, int op);          /* '+' '-' '*' '/'                 */
void calc_equals(Calc *c);
void calc_backspace(Calc *c);
void calc_negate(Calc *c);
void calc_sqrt(Calc *c);                /* of the displayed value          */
void calc_recip(Calc *c);               /* 1/x                             */
void calc_percent(Calc *c);             /* see the note in calc_core.c     */

/* Memory register: recall, store, add, clear. */
void calc_mem_recall(Calc *c);
void calc_mem_store(Calc *c);
void calc_mem_add(Calc *c);
void calc_mem_clear(Calc *c);

/*
 * Press a button by its label, which is how the window dispatches. Returns
 * CFALSE for a label this machine does not know, so a button added to the
 * grid without being wired up fails loudly instead of doing nothing.
 */
cbool calc_press(Calc *c, const char *label);

/* ---- the keypad, as cells ---------------------------------------------
 *
 * Which label sits in which cell is layout, not arithmetic -- but it is still
 * pure, and it is where a real bug lived. The grid gives '=', '+' and '0'
 * several adjacent cells so they come out as the tall and wide keys a desk
 * calculator has; the window drew a complete bevelled button in every cell, so
 * what a person saw was two '+' keys, three '=' keys and two '0' keys, each in
 * its own frame with a gap down the middle. It looked like the table had been
 * filled in wrong.
 *
 * So the SPAN lives here, in cells, where tests/test_calc.c can check the
 * property that matters -- every cell covered by exactly one key, no key
 * overlapping another -- without a window or a pixel. The window turns cells
 * into a rectangle and knows nothing else about it.
 */
#define CALC_KEY_ROWS 6
#define CALC_KEY_COLS 5

/* The label at a cell; "" when out of range. */
const char *calc_key_label(int row, int col);

/*
 * The key STARTING at this cell: how many rows and columns of identical cells
 * it covers (both at least 1). CFALSE when the cell is a continuation of a key
 * that began above it or to its left -- those cells must not be drawn at all.
 *
 * A run is taken in ONE direction, vertical first. A run that turned a corner
 * would cover cells belonging to another key, and painting a key over its
 * neighbour is worse than the bug this replaced.
 */
cbool calc_key_span(int row, int col, int *rows, int *cols);

#endif /* CASTALIA_CALC_CORE_H */
