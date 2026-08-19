/*
 * calc_core.c - The calculator, without the buttons (see calc_core.h).
 */
#include "calc_core.h"
#include "castalia/sys.h"

#include <stdlib.h>   /* atof */
#include <string.h>   /* strchr, strcmp */

/* ---- value helpers ---------------------------------------------------- */
double calc_value(const Calc *c)
{
    return (c != NULL) ? atof(c->disp) : 0.0;
}

static void calc_show(Calc *c, double v)
{
    sys_snprintf(c->disp, sizeof(c->disp), "%g", v);
    c->entering = CFALSE;
}

static void calc_fail(Calc *c)
{
    c->error = CTRUE;
    sys_strlcpy(c->disp, "Error", sizeof(c->disp));
}

static double calc_apply(double a, double b, int op, cbool *err)
{
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/':
        if (b == 0.0) { *err = CTRUE; return 0.0; }
        return a / b;
    default:  return b;
    }
}

/* ---- state ------------------------------------------------------------ */
void calc_clear(Calc *c)
{
    if (c == NULL) { return; }
    sys_strlcpy(c->disp, "0", sizeof(c->disp));
    c->acc = 0.0;
    c->op = 0;
    c->entering = CFALSE;
    c->error = CFALSE;
    /* Memory survives C, as it does on every calculator ever made. */
}

void calc_reset(Calc *c)
{
    if (c == NULL) { return; }
    calc_clear(c);
    c->mem = 0.0;
    c->mem_set = CFALSE;
}

void calc_digit(Calc *c, int d)
{
    int n;
    if (c == NULL || d < 0 || d > 9) { return; }
    if (c->error) { calc_clear(c); }
    if (!c->entering) {
        c->disp[0] = (char)('0' + d);
        c->disp[1] = '\0';
        c->entering = CTRUE;
        return;
    }
    n = (int)sys_strnlen(c->disp, sizeof(c->disp));
    if (n == 1 && c->disp[0] == '0') { c->disp[0] = (char)('0' + d); }
    else if (n < CALC_DISP_MAX - 1) {
        c->disp[n] = (char)('0' + d);
        c->disp[n + 1] = '\0';
    }
}

void calc_dot(Calc *c)
{
    if (c == NULL) { return; }
    if (c->error) { calc_clear(c); }
    if (!c->entering) {
        sys_strlcpy(c->disp, "0.", sizeof(c->disp));
        c->entering = CTRUE;
    } else if (strchr(c->disp, '.') == NULL) {
        int n = (int)sys_strnlen(c->disp, sizeof(c->disp));
        if (n < CALC_DISP_MAX - 1) { c->disp[n] = '.'; c->disp[n + 1] = '\0'; }
    }
}

void calc_op(Calc *c, int op)
{
    double cur;
    if (c == NULL || c->error) { return; }
    cur = calc_value(c);
    if (c->op != 0 && c->entering) {
        cbool err = CFALSE;
        c->acc = calc_apply(c->acc, cur, c->op, &err);
        if (err) { calc_fail(c); return; }
        calc_show(c, c->acc);
    } else {
        c->acc = cur;
    }
    c->op = op;
    c->entering = CFALSE;
}

void calc_equals(Calc *c)
{
    cbool err = CFALSE;
    double cur;
    if (c == NULL || c->error || c->op == 0) { return; }
    cur = calc_value(c);
    c->acc = calc_apply(c->acc, cur, c->op, &err);
    if (err) { calc_fail(c); return; }
    calc_show(c, c->acc);
    c->op = 0;
}

void calc_backspace(Calc *c)
{
    int n;
    if (c == NULL) { return; }
    if (c->error) { calc_clear(c); return; }
    if (!c->entering) { return; }
    n = (int)sys_strnlen(c->disp, sizeof(c->disp));
    if (n > 1) { c->disp[n - 1] = '\0'; }
    else { sys_strlcpy(c->disp, "0", sizeof(c->disp)); c->entering = CFALSE; }
}

void calc_negate(Calc *c)
{
    if (c == NULL || c->error) { return; }
    calc_show(c, -calc_value(c));
    c->entering = CTRUE;
}

/* ---- the single-value functions --------------------------------------- */
void calc_sqrt(Calc *c)
{
    double v, r, prev;
    int i;
    if (c == NULL || c->error) { return; }
    v = calc_value(c);
    if (v < 0.0) { calc_fail(c); return; }   /* not an error to hide */
    if (v == 0.0) { calc_show(c, 0.0); return; }
    /*
     * Newton's method rather than <math.h>. The DOS build links no floating
     * point library beyond what the compiler emits, and pulling one in for a
     * single button is a poor trade; sqrt converges here in well under twenty
     * iterations for any value this display can hold, and the loop stops as
     * soon as it stops moving.
     */
    r = v;
    for (i = 0; i < 40; i++) {
        prev = r;
        r = 0.5 * (r + v / r);
        if (r == prev) { break; }
    }
    calc_show(c, r);
}

void calc_recip(Calc *c)
{
    double v;
    if (c == NULL || c->error) { return; }
    v = calc_value(c);
    if (v == 0.0) { calc_fail(c); return; }
    calc_show(c, 1.0 / v);
}

void calc_percent(Calc *c)
{
    /*
     * Percent of the PENDING left-hand side, which is what the key has meant
     * on desk calculators since long before it was on a screen: "200 + 10 %"
     * is 200 + 20, not 200 + 0.1. With nothing pending it is a plain divide
     * by a hundred, because there is nothing to take a percentage of.
     */
    double v;
    if (c == NULL || c->error) { return; }
    v = calc_value(c);
    if (c->op != 0) { calc_show(c, c->acc * v / 100.0); }
    else { calc_show(c, v / 100.0); }
    c->entering = CTRUE;
}

/* ---- memory ----------------------------------------------------------- */
void calc_mem_recall(Calc *c)
{
    if (c == NULL || c->error) { return; }
    calc_show(c, c->mem);
    c->entering = CTRUE;
}

void calc_mem_store(Calc *c)
{
    if (c == NULL || c->error) { return; }
    c->mem = calc_value(c);
    c->mem_set = CTRUE;
    c->entering = CFALSE;
}

void calc_mem_add(Calc *c)
{
    if (c == NULL || c->error) { return; }
    c->mem += calc_value(c);
    c->mem_set = CTRUE;
    c->entering = CFALSE;
}

void calc_mem_clear(Calc *c)
{
    if (c == NULL) { return; }
    c->mem = 0.0;
    c->mem_set = CFALSE;
}

/* ---- dispatch --------------------------------------------------------- */
cbool calc_press(Calc *c, const char *label)
{
    if (c == NULL || label == NULL) { return CFALSE; }
    if (label[0] >= '0' && label[0] <= '9' && label[1] == '\0') {
        calc_digit(c, label[0] - '0');
        return CTRUE;
    }
    /*
     * label[0] FIRST, and the order is the whole point. Written the other way
     * round -- `label[1] == '\0' && (label[0] == '+' || ...)` -- an empty
     * label reads label[1], which is one past the end of a one-byte string.
     * Testing label[0] first means it is only reached when label[0] is not
     * the terminator, and a string whose first byte is not NUL always has a
     * second byte to read.
     *
     * The digit test above happens to be safe for the same reason, by having
     * been written in the right order rather than by anyone choosing it.
     */
    if ((label[0] == '+' || label[0] == '-' ||
         label[0] == '*' || label[0] == '/') && label[1] == '\0') {
        calc_op(c, label[0]);
        return CTRUE;
    }
    if (strcmp(label, ".")    == 0) { calc_dot(c);        return CTRUE; }
    if (strcmp(label, "C")    == 0) { calc_clear(c);      return CTRUE; }
    if (strcmp(label, "<")    == 0) { calc_backspace(c);  return CTRUE; }
    if (strcmp(label, "+/-")  == 0) { calc_negate(c);     return CTRUE; }
    if (strcmp(label, "=")    == 0) { calc_equals(c);     return CTRUE; }
    if (strcmp(label, "sqrt") == 0) { calc_sqrt(c);       return CTRUE; }
    if (strcmp(label, "1/x")  == 0) { calc_recip(c);      return CTRUE; }
    if (strcmp(label, "%")    == 0) { calc_percent(c);    return CTRUE; }
    if (strcmp(label, "MR")   == 0) { calc_mem_recall(c); return CTRUE; }
    if (strcmp(label, "MS")   == 0) { calc_mem_store(c);  return CTRUE; }
    if (strcmp(label, "M+")   == 0) { calc_mem_add(c);    return CTRUE; }
    if (strcmp(label, "MC")   == 0) { calc_mem_clear(c);  return CTRUE; }
    return CFALSE;
}

/* ---- the keypad, as cells (see calc_core.h) --------------------------- */
/*
 * Twenty-six keys in thirty cells: the memory row across the top, then a
 * conventional keypad, with '=', '+' and '0' each given the adjacent cells
 * that make them the tall and wide keys of a desk calculator.
 *
 * An earlier version of this grid was 5x5 -- twenty-five cells for twenty-six
 * keys. The one that did not fit was '=', so the calculator had no equals
 * button at all and only the keyboard could finish a sum.
 */
static const char *const CALC_KEYS[CALC_KEY_ROWS * CALC_KEY_COLS] = {
    "MC",  "MR", "MS",  "M+", "1/x",
    "C",   "<",  "sqrt","%",  "/",
    "7",   "8",  "9",   "*",  "-",
    "4",   "5",  "6",   "+",  "=",
    "1",   "2",  "3",   "+",  "=",
    "0",   "0",  "+/-", ".",  "="
};

const char *calc_key_label(int row, int col)
{
    if (row < 0 || row >= CALC_KEY_ROWS || col < 0 || col >= CALC_KEY_COLS) {
        return "";
    }
    return CALC_KEYS[row * CALC_KEY_COLS + col];
}

/* Do two cells carry the same label? Out-of-range is never the same. */
static cbool key_same(int r1, int c1, int r2, int c2)
{
    const char *a = calc_key_label(r1, c1);
    const char *b = calc_key_label(r2, c2);
    if (a[0] == '\0' || b[0] == '\0') { return CFALSE; }
    return (strcmp(a, b) == 0) ? CTRUE : CFALSE;
}

cbool calc_key_span(int row, int col, int *rows, int *cols)
{
    int k;
    if (rows != NULL) { *rows = 1; }
    if (cols != NULL) { *cols = 1; }
    if (calc_key_label(row, col)[0] == '\0') { return CFALSE; }
    /* A continuation of the cell above or to the left is not a key. */
    if (key_same(row, col, row - 1, col) || key_same(row, col, row, col - 1)) {
        return CFALSE;
    }
    if (key_same(row, col, row + 1, col)) {
        k = row;
        while (key_same(row, col, k + 1, col)) { k++; }
        if (rows != NULL) { *rows = k - row + 1; }
    } else if (key_same(row, col, row, col + 1)) {
        k = col;
        while (key_same(row, col, row, k + 1)) { k++; }
        if (cols != NULL) { *cols = k - col + 1; }
    }
    return CTRUE;
}
