/*
 * sheet_core.h - Pure spreadsheet model + formula engine (no gfx/wm/platform).
 *
 * The model behind app_sheet.c ("CastaliaSheet"), kept free of UI so the host
 * unit tests can exercise it hermetically (tests/test_sheet.c) -- the same
 * split mines_core.c uses.
 *
 * A cell holds the raw text the user typed. On recalc each cell is classified
 * as empty, a number, plain text, or a formula (leading '='), and formulas are
 * evaluated by a small recursive-descent parser:
 *
 *   expr   := term (('+' | '-') term)*
 *   term   := unary (('*' | '/') unary)*
 *   unary  := ['+' | '-'] atom
 *   atom   := number | '(' expr ')' | ref | func '(' args ')'
 *   args   := arg (',' arg)*        arg := range | expr
 *   range  := ref ':' ref
 *
 * Functions: SUM, AVG, MIN, MAX, COUNT, ABS, INT, ROUND. References are
 * A1-style (A1..P48).
 * Evaluation is memoized per recalc pass and self-references are reported as
 * #CYCLE! rather than recursing forever.
 */
#ifndef CASTALIA_SHEET_CORE_H
#define CASTALIA_SHEET_CORE_H

#include "castalia/ctypes.h"

#define SHEET_COLS 16            /* columns A..P                            */
#define SHEET_ROWS 48            /* rows 1..48                              */
#define SHEET_TEXT 32            /* raw entry length (with NUL)             */

/* What a cell turned out to be after classification. */
enum {
    SHEET_EMPTY = 0,
    SHEET_NUMBER,
    SHEET_LABEL,      /* plain text                                         */
    SHEET_FORMULA
};

/* Per-cell evaluation status. */
enum {
    SHEET_OK = 0,
    SHEET_ERR_SYNTAX,
    SHEET_ERR_DIV0,
    SHEET_ERR_REF,
    SHEET_ERR_CYCLE
};

typedef struct {
    char raw[SHEET_ROWS][SHEET_COLS][SHEET_TEXT];
    double        val[SHEET_ROWS][SHEET_COLS];
    unsigned char kind[SHEET_ROWS][SHEET_COLS];
    unsigned char stat[SHEET_ROWS][SHEET_COLS];
    unsigned char seen[SHEET_ROWS][SHEET_COLS];  /* recalc memo/cycle marks */
} Sheet;

/* Empty every cell. */
void sheet_clear(Sheet *s);

/* Replace a cell's raw text and recalculate the whole sheet. Out-of-range
 * coordinates are ignored; 'text' may be NULL or "" to clear the cell. */
void sheet_set(Sheet *s, int r, int c, const char *text);

/* The raw text as typed ("" when empty). Never NULL for valid coordinates. */
const char *sheet_raw(const Sheet *s, int r, int c);

/* Reclassify and re-evaluate every cell. sheet_set() calls this for you. */
void sheet_recalc(Sheet *s);

int    sheet_kind(const Sheet *s, int r, int c);    /* SHEET_EMPTY..FORMULA */
int    sheet_status(const Sheet *s, int r, int c);  /* SHEET_OK / SHEET_ERR_* */
double sheet_value(const Sheet *s, int r, int c);   /* 0 for text/empty      */

/* The string a grid cell should display: text as typed, numbers formatted,
 * formulas as their value or an error tag ("#DIV/0!", "#CYCLE!", ...). */
void sheet_display(const Sheet *s, int r, int c, char *out, int outsz);

/* Format a number the way the grid does (trailing ".00" suppressed). */
void sheet_fmt_num(double v, char *out, int outsz);

/* "A".."P" for a column index; "A1" style label for a cell. */
void sheet_colname(int c, char *out, int outsz);
void sheet_cellname(int r, int c, char *out, int outsz);

/* Parse an A1-style reference at 't'. On success fills row/col (0-based) and
 * the consumed length, and returns CTRUE. */
cbool sheet_ref_parse(const char *t, int *out_r, int *out_c, int *out_len);

/* ---- CSV interchange -------------------------------------------------- */
/*
 * Cells are written as the RAW text you typed, so formulas survive a round
 * trip; values containing a comma or a quote are quoted the usual way ("" for
 * an embedded quote). Trailing empty rows and columns are trimmed. Returns the
 * length the CSV NEEDS, which is >= outsz when the buffer was too small and
 * the output was truncated -- callers must check before writing it out.
 * SHEET_CSV_MAX is the worst case (every cell quoted with every character
 * doubled, a separator per cell and a newline per row), so a buffer of that
 * size can never truncate.
 */
#define SHEET_CSV_MAX (SHEET_ROWS * SHEET_COLS * (SHEET_TEXT * 2 + 3) \
                       + SHEET_ROWS + 8)
int   sheet_to_csv(const Sheet *s, char *out, int outsz);

/*
 * What a CSV asked for, against what the grid can hold.
 *
 * The sheet is 48 x 16 with 31 characters per cell, and a CSV comes from
 * anywhere -- another machine, the console, a text editor. A file bigger than
 * the grid used to load its first 48 rows, report "Opened", and then Save
 * wrote those 48 rows back over all 100. Nothing said a row had been dropped,
 * because sheet_from_csv returned CTRUE whatever it had thrown away.
 *
 * So the parse counts what it could not take, and says so. One walk, not two:
 * a separate "would it fit" pass would be a second implementation of the
 * quoting rules, and the two would drift.
 */
typedef struct {
    int rows;         /* rows the SOURCE holds (not the grid)            */
    int cols;         /* widest row in the source                        */
    int dropped_rows; /* rows past SHEET_ROWS -- parsed, not stored      */
    int dropped_cols; /* columns past SHEET_COLS, at the widest row      */
    int clipped;      /* cells whose text did not fit SHEET_TEXT         */
} SheetLoad;

/*
 * Replace the sheet with parsed CSV and recalculate. Forgiving about SHAPE:
 * ragged rows and missing quotes are accepted.
 *
 * Returns CTRUE only when the sheet now holds the WHOLE file -- no row, no
 * column and no character was left behind. 'info' (may be NULL) says what was.
 * A caller that ignores both is claiming the file fits, and is how a 100-row
 * CSV becomes a 48-row one.
 */
cbool sheet_from_csv(Sheet *s, const char *src, SheetLoad *info);

#endif /* CASTALIA_SHEET_CORE_H */
