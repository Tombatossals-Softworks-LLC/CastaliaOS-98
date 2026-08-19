/*
 * sheet_core.c - Spreadsheet model + formula engine (pure logic, host-tested).
 *
 * See sheet_core.h for the grammar. Evaluation is a recursive-descent parse of
 * the cell's text, memoized per recalc pass through the 'seen' marks so a sheet
 * of dependent formulas costs one pass, and a reference cycle is reported as
 * #CYCLE! instead of recursing forever.
 */
#include "sheet_core.h"
#include "castalia/sys.h"

#define SHEET_MAX_DEPTH 24   /* nested parens / call depth guard */

/* ---- tiny character helpers (no <ctype.h>, no locale) ----------------- */
static cbool ch_digit(char c) { return (c >= '0' && c <= '9') ? CTRUE : CFALSE; }
static cbool ch_alpha(char c)
{
    return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) ? CTRUE : CFALSE;
}
static char ch_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}
static cbool in_range(int r, int c)
{
    return (r >= 0 && r < SHEET_ROWS && c >= 0 && c < SHEET_COLS) ? CTRUE : CFALSE;
}

/* Parse an unsigned decimal (with optional fraction) at 'p'. */
static cbool parse_number(const char *p, double *out, int *len)
{
    int i = 0;
    double v = 0.0;
    cbool any = CFALSE;
    while (ch_digit(p[i])) { v = v * 10.0 + (double)(p[i] - '0'); i++; any = CTRUE; }
    if (p[i] == '.') {
        double f = 0.1;
        i++;
        while (ch_digit(p[i])) {
            v += (double)(p[i] - '0') * f;
            f *= 0.1;
            i++;
            any = CTRUE;
        }
    }
    if (!any) { return CFALSE; }
    *out = v;
    *len = i;
    return CTRUE;
}

cbool sheet_ref_parse(const char *t, int *out_r, int *out_c, int *out_len)
{
    int i, col, row = 0;
    cbool anyd = CFALSE;
    if (t == NULL || !ch_alpha(t[0])) { return CFALSE; }
    col = ch_upper(t[0]) - 'A';
    i = 1;
    if (ch_alpha(t[i])) { return CFALSE; }   /* single-letter columns only */
    while (ch_digit(t[i])) { row = row * 10 + (t[i] - '0'); i++; anyd = CTRUE; }
    if (!anyd) { return CFALSE; }
    if (!in_range(row - 1, col)) { return CFALSE; }
    if (out_r != NULL) { *out_r = row - 1; }
    if (out_c != NULL) { *out_c = col; }
    if (out_len != NULL) { *out_len = i; }
    return CTRUE;
}

/* Does 't' at least LOOK like a reference (letters then digits)? Used to
 * report an out-of-sheet reference as #REF! rather than a syntax error. */
static cbool looks_like_ref(const char *t)
{
    int i = 0;
    if (!ch_alpha(t[i])) { return CFALSE; }
    while (ch_alpha(t[i])) { i++; }
    return ch_digit(t[i]) ? CTRUE : CFALSE;
}

/* ---- parser state ----------------------------------------------------- */
typedef struct {
    const char *p;
    Sheet      *s;
    int         err;     /* SHEET_OK or the first SHEET_ERR_* seen */
    int         depth;
} SP;

typedef struct {
    int    n;
    double sum, mn, mx;
} Acc;

static void acc_add(Acc *a, double v)
{
    if (a->n == 0) { a->mn = v; a->mx = v; }
    else {
        if (v < a->mn) { a->mn = v; }
        if (v > a->mx) { a->mx = v; }
    }
    a->sum += v;
    a->n++;
}

static void fail(SP *ps, int err)
{
    if (ps->err == SHEET_OK) { ps->err = err; }
}
static void skip_ws(SP *ps)
{
    while (*ps->p == ' ' || *ps->p == '\t') { ps->p++; }
}

static void ensure_cell(Sheet *s, int r, int c);
static double p_expr(SP *ps);

/* Read a cell's value, propagating its error (#DIV/0!, #CYCLE!, ...). */
static double ref_value(SP *ps, int r, int c)
{
    ensure_cell(ps->s, r, c);
    if (ps->s->stat[r][c] != SHEET_OK) { fail(ps, ps->s->stat[r][c]); }
    return ps->s->val[r][c];
}

/* Feed every numeric cell of the rectangle r0..r1 x c0..c1 into 'a'. */
static void acc_range(SP *ps, Acc *a, int r0, int c0, int r1, int c1)
{
    int r, c, t;
    if (r0 > r1) { t = r0; r0 = r1; r1 = t; }
    if (c0 > c1) { t = c0; c0 = c1; c1 = t; }
    for (r = r0; r <= r1; r++) {
        for (c = c0; c <= c1; c++) {
            int k;
            ensure_cell(ps->s, r, c);
            k = ps->s->kind[r][c];
            if (k != SHEET_NUMBER && k != SHEET_FORMULA) { continue; }
            if (ps->s->stat[r][c] != SHEET_OK) { fail(ps, ps->s->stat[r][c]); }
            acc_add(a, ps->s->val[r][c]);
        }
    }
}

/* Parse one function argument: a range (A1:B4) or a plain expression. */
static void p_arg(SP *ps, Acc *a)
{
    const char *save;
    int r0, c0, len;
    skip_ws(ps);
    save = ps->p;
    if (sheet_ref_parse(ps->p, &r0, &c0, &len)) {
        const char *q = ps->p + len;
        while (*q == ' ' || *q == '\t') { q++; }
        if (*q == ':') {
            int r1, c1, len2;
            q++;
            while (*q == ' ' || *q == '\t') { q++; }
            if (sheet_ref_parse(q, &r1, &c1, &len2)) {
                ps->p = q + len2;
                acc_range(ps, a, r0, c0, r1, c1);
                return;
            }
            fail(ps, SHEET_ERR_REF);
            return;
        }
    }
    ps->p = save;
    acc_add(a, p_expr(ps));
}

/* SUM / AVG / MIN / MAX / COUNT over the argument list (the '(' is eaten). */
static double p_call(SP *ps, const char *id)
{
    Acc a;
    a.n = 0; a.sum = 0.0; a.mn = 0.0; a.mx = 0.0;
    skip_ws(ps);
    if (*ps->p != ')') {
        for (;;) {
            p_arg(ps, &a);
            skip_ws(ps);
            if (*ps->p != ',') { break; }
            ps->p++;
        }
    }
    skip_ws(ps);
    if (*ps->p == ')') { ps->p++; } else { fail(ps, SHEET_ERR_SYNTAX); }

    if (sys_stricmp(id, "SUM") == 0)   { return a.sum; }
    if (sys_stricmp(id, "COUNT") == 0) { return (double)a.n; }
    if (sys_stricmp(id, "AVG") == 0) {
        if (a.n == 0) { fail(ps, SHEET_ERR_DIV0); return 0.0; }
        return a.sum / (double)a.n;
    }
    if (sys_stricmp(id, "MIN") == 0) { return (a.n > 0) ? a.mn : 0.0; }
    if (sys_stricmp(id, "MAX") == 0) { return (a.n > 0) ? a.mx : 0.0; }
    if (sys_stricmp(id, "ABS") == 0) { return (a.sum < 0.0) ? -a.sum : a.sum; }
    if (sys_stricmp(id, "INT") == 0) { return (double)(long)a.sum; }
    if (sys_stricmp(id, "ROUND") == 0) {
        return (a.sum >= 0.0) ? (double)(long)(a.sum + 0.5)
                              : -(double)(long)(-a.sum + 0.5);
    }
    fail(ps, SHEET_ERR_SYNTAX);
    return 0.0;
}

static double p_atom(SP *ps)
{
    double v = 0.0;
    int len;
    skip_ws(ps);
    if (ps->depth > SHEET_MAX_DEPTH) { fail(ps, SHEET_ERR_SYNTAX); return 0.0; }

    if (*ps->p == '(') {
        ps->p++;
        ps->depth++;
        v = p_expr(ps);
        ps->depth--;
        skip_ws(ps);
        if (*ps->p == ')') { ps->p++; } else { fail(ps, SHEET_ERR_SYNTAX); }
        return v;
    }
    if (ch_digit(*ps->p) || (*ps->p == '.' && ch_digit(ps->p[1]))) {
        if (parse_number(ps->p, &v, &len)) { ps->p += len; return v; }
        fail(ps, SHEET_ERR_SYNTAX);
        return 0.0;
    }
    if (ch_alpha(*ps->p)) {
        const char *save = ps->p;
        char id[10];
        int n = 0;
        while (ch_alpha(*ps->p)) {
            if (n < (int)sizeof(id) - 1) { id[n++] = ch_upper(*ps->p); }
            ps->p++;
        }
        id[n] = '\0';
        skip_ws(ps);
        if (*ps->p == '(') {
            ps->p++;
            ps->depth++;
            v = p_call(ps, id);
            ps->depth--;
            return v;
        }
        ps->p = save;
        {
            int r, c;
            if (sheet_ref_parse(ps->p, &r, &c, &len)) {
                ps->p += len;
                return ref_value(ps, r, c);
            }
        }
        fail(ps, looks_like_ref(ps->p) ? SHEET_ERR_REF : SHEET_ERR_SYNTAX);
        return 0.0;
    }
    fail(ps, SHEET_ERR_SYNTAX);
    return 0.0;
}

static double p_unary(SP *ps)
{
    skip_ws(ps);
    if (*ps->p == '-') { ps->p++; return -p_unary(ps); }
    if (*ps->p == '+') { ps->p++; return p_unary(ps); }
    return p_atom(ps);
}

static double p_term(SP *ps)
{
    double v = p_unary(ps);
    for (;;) {
        skip_ws(ps);
        if (*ps->p == '*') { ps->p++; v = v * p_unary(ps); }
        else if (*ps->p == '/') {
            double d;
            ps->p++;
            d = p_unary(ps);
            if (d == 0.0) { fail(ps, SHEET_ERR_DIV0); return 0.0; }
            v = v / d;
        } else { break; }
    }
    return v;
}

static double p_expr(SP *ps)
{
    double v = p_term(ps);
    for (;;) {
        skip_ws(ps);
        if (*ps->p == '+') { ps->p++; v = v + p_term(ps); }
        else if (*ps->p == '-') { ps->p++; v = v - p_term(ps); }
        else { break; }
    }
    return v;
}

/* ---- classification + recalc ------------------------------------------ */
/* Is the whole string a plain number (with optional sign and spaces)? */
static cbool whole_number(const char *t, double *out)
{
    int i = 0, len;
    int neg = 0;
    double v;
    while (t[i] == ' ' || t[i] == '\t') { i++; }
    if (t[i] == '-') { neg = 1; i++; }
    else if (t[i] == '+') { i++; }
    if (!parse_number(t + i, &v, &len)) { return CFALSE; }
    i += len;
    while (t[i] == ' ' || t[i] == '\t') { i++; }
    if (t[i] != '\0') { return CFALSE; }
    *out = neg ? -v : v;
    return CTRUE;
}

static void ensure_cell(Sheet *s, int r, int c)
{
    if (!in_range(r, c)) { return; }
    if (s->seen[r][c] == 2) { return; }
    if (s->seen[r][c] == 1) {                 /* referenced while computing */
        s->stat[r][c] = SHEET_ERR_CYCLE;
        s->val[r][c] = 0.0;
        s->seen[r][c] = 2;
        return;
    }
    if (s->kind[r][c] != SHEET_FORMULA) { s->seen[r][c] = 2; return; }

    s->seen[r][c] = 1;
    {
        SP ps;
        double v;
        ps.p = s->raw[r][c] + 1;   /* skip '=' */
        ps.s = s;
        ps.err = SHEET_OK;
        ps.depth = 0;
        v = p_expr(&ps);
        skip_ws(&ps);
        if (ps.err == SHEET_OK && *ps.p != '\0') { ps.err = SHEET_ERR_SYNTAX; }
        s->stat[r][c] = (unsigned char)ps.err;
        s->val[r][c] = (ps.err == SHEET_OK) ? v : 0.0;
    }
    s->seen[r][c] = 2;
}

void sheet_recalc(Sheet *s)
{
    int r, c;
    if (s == NULL) { return; }
    for (r = 0; r < SHEET_ROWS; r++) {
        for (c = 0; c < SHEET_COLS; c++) {
            const char *t = s->raw[r][c];
            double v = 0.0;
            s->stat[r][c] = SHEET_OK;
            s->seen[r][c] = 0;
            s->val[r][c] = 0.0;
            if (t[0] == '\0')      { s->kind[r][c] = SHEET_EMPTY; }
            else if (t[0] == '=')  { s->kind[r][c] = SHEET_FORMULA; }
            else if (whole_number(t, &v)) {
                s->kind[r][c] = SHEET_NUMBER;
                s->val[r][c] = v;
            } else                 { s->kind[r][c] = SHEET_LABEL; }
        }
    }
    for (r = 0; r < SHEET_ROWS; r++) {
        for (c = 0; c < SHEET_COLS; c++) { ensure_cell(s, r, c); }
    }
}

void sheet_clear(Sheet *s)
{
    int r, c;
    if (s == NULL) { return; }
    for (r = 0; r < SHEET_ROWS; r++) {
        for (c = 0; c < SHEET_COLS; c++) {
            s->raw[r][c][0] = '\0';
            s->val[r][c] = 0.0;
            s->kind[r][c] = SHEET_EMPTY;
            s->stat[r][c] = SHEET_OK;
            s->seen[r][c] = 0;
        }
    }
}

void sheet_set(Sheet *s, int r, int c, const char *text)
{
    if (s == NULL || !in_range(r, c)) { return; }
    if (text == NULL) { s->raw[r][c][0] = '\0'; }
    else { sys_strlcpy(s->raw[r][c], text, (cu32)SHEET_TEXT); }
    sheet_recalc(s);
}

const char *sheet_raw(const Sheet *s, int r, int c)
{
    if (s == NULL || !in_range(r, c)) { return ""; }
    return s->raw[r][c];
}
int sheet_kind(const Sheet *s, int r, int c)
{
    if (s == NULL || !in_range(r, c)) { return SHEET_EMPTY; }
    return (int)s->kind[r][c];
}
int sheet_status(const Sheet *s, int r, int c)
{
    if (s == NULL || !in_range(r, c)) { return SHEET_OK; }
    return (int)s->stat[r][c];
}
double sheet_value(const Sheet *s, int r, int c)
{
    if (s == NULL || !in_range(r, c)) { return 0.0; }
    return s->val[r][c];
}

void sheet_fmt_num(double v, char *out, int outsz)
{
    int neg = 0;
    long scaled, ip, frac;
    if (out == NULL || outsz <= 0) { return; }
    if (v < 0.0) { neg = 1; v = -v; }
    if (!(v < 20000000.0)) {          /* also catches NaN */
        sys_strlcpy(out, "#NUM!", (cu32)outsz);
        return;
    }
    scaled = (long)(v * 100.0 + 0.5);
    ip = scaled / 100;
    frac = scaled % 100;
    if (frac == 0) {
        sys_snprintf(out, (cu32)outsz, "%s%ld", neg ? "-" : "", ip);
    } else if (frac % 10 == 0) {
        sys_snprintf(out, (cu32)outsz, "%s%ld.%ld", neg ? "-" : "", ip, frac / 10);
    } else if (frac < 10) {
        sys_snprintf(out, (cu32)outsz, "%s%ld.0%ld", neg ? "-" : "", ip, frac);
    } else {
        sys_snprintf(out, (cu32)outsz, "%s%ld.%ld", neg ? "-" : "", ip, frac);
    }
}

void sheet_display(const Sheet *s, int r, int c, char *out, int outsz)
{
    int k, st;
    if (out == NULL || outsz <= 0) { return; }
    out[0] = '\0';
    if (s == NULL || !in_range(r, c)) { return; }
    k = (int)s->kind[r][c];
    st = (int)s->stat[r][c];
    if (k == SHEET_EMPTY) { return; }
    if (k == SHEET_LABEL) { sys_strlcpy(out, s->raw[r][c], (cu32)outsz); return; }
    if (st != SHEET_OK) {
        const char *tag = (st == SHEET_ERR_DIV0)  ? "#DIV/0!"
                        : (st == SHEET_ERR_REF)   ? "#REF!"
                        : (st == SHEET_ERR_CYCLE) ? "#CYCLE!" : "#ERR!";
        sys_strlcpy(out, tag, (cu32)outsz);
        return;
    }
    sheet_fmt_num(s->val[r][c], out, outsz);
}

void sheet_colname(int c, char *out, int outsz)
{
    if (out == NULL || outsz < 2) { return; }
    if (c < 0 || c >= SHEET_COLS) { out[0] = '?'; out[1] = '\0'; return; }
    out[0] = (char)('A' + c);
    out[1] = '\0';
}

void sheet_cellname(int r, int c, char *out, int outsz)
{
    if (out == NULL || outsz <= 0) { return; }
    if (!in_range(r, c)) { sys_strlcpy(out, "--", (cu32)outsz); return; }
    sys_snprintf(out, (cu32)outsz, "%c%d", (char)('A' + c), r + 1);
}

/* ---- CSV interchange -------------------------------------------------- */
/* Append 's' to out[at..], honoring the buffer limit; returns the new length. */
static int csv_put(char *out, int outsz, int at, const char *t)
{
    int i = 0;
    while (t[i] != '\0') {
        if (at < outsz - 1) { out[at] = t[i]; }
        at++;
        i++;
    }
    return at;
}

static int csv_putc(char *out, int outsz, int at, char c)
{
    if (at < outsz - 1) { out[at] = c; }
    return at + 1;
}

int sheet_to_csv(const Sheet *s, char *out, int outsz)
{
    int r, c, at = 0, lastr = -1, lastc = -1;
    if (s == NULL || out == NULL || outsz <= 0) { return 0; }
    for (r = 0; r < SHEET_ROWS; r++) {
        for (c = 0; c < SHEET_COLS; c++) {
            if (s->raw[r][c][0] != '\0') {
                if (r > lastr) { lastr = r; }
                if (c > lastc) { lastc = c; }
            }
        }
    }
    for (r = 0; r <= lastr; r++) {
        for (c = 0; c <= lastc; c++) {
            const char *t = s->raw[r][c];
            int i, quote = 0;
            for (i = 0; t[i] != '\0'; i++) {
                if (t[i] == ',' || t[i] == '"' || t[i] == '\n') { quote = 1; break; }
            }
            if (quote) {
                at = csv_putc(out, outsz, at, '"');
                for (i = 0; t[i] != '\0'; i++) {
                    if (t[i] == '"') { at = csv_putc(out, outsz, at, '"'); }
                    at = csv_putc(out, outsz, at, t[i]);
                }
                at = csv_putc(out, outsz, at, '"');
            } else {
                at = csv_put(out, outsz, at, t);
            }
            if (c < lastc) { at = csv_putc(out, outsz, at, ','); }
        }
        at = csv_putc(out, outsz, at, '\n');
    }
    out[(at < outsz) ? at : outsz - 1] = '\0';
    return at;
}

cbool sheet_from_csv(Sheet *s, const char *src, SheetLoad *info)
{
    int r = 0, c = 0;
    SheetLoad li;

    li.rows = 0; li.cols = 0;
    li.dropped_rows = 0; li.dropped_cols = 0; li.clipped = 0;
    if (info != NULL) { *info = li; }
    if (s == NULL || src == NULL) { return CFALSE; }
    sheet_clear(s);
    /*
     * The walk runs to the END of the source, not to the end of the grid.
     * Stopping at row 48 is what made "how many rows did this file have"
     * unanswerable -- and unanswered, it read as 48.
     */
    while (*src != '\0') {
        char cell[SHEET_TEXT];
        int n = 0, over = 0;
        if (*src == '"') {                       /* quoted field */
            src++;
            while (*src != '\0') {
                if (*src == '"' && src[1] == '"') {
                    if (n < SHEET_TEXT - 1) { cell[n++] = '"'; } else { over++; }
                    src += 2;
                    continue;
                }
                if (*src == '"') { src++; break; }
                if (n < SHEET_TEXT - 1) { cell[n++] = *src; } else { over++; }
                src++;
            }
        } else {
            while (*src != '\0' && *src != ',' && *src != '\n' && *src != '\r') {
                if (n < SHEET_TEXT - 1) { cell[n++] = *src; } else { over++; }
                src++;
            }
        }
        cell[n] = '\0';
        if (over > 0) { li.clipped++; }
        if (r < SHEET_ROWS && c < SHEET_COLS) {
            sys_strlcpy(s->raw[r][c], cell, (cu32)SHEET_TEXT);
        }
        if (c + 1 > li.cols) { li.cols = c + 1; }
        if (*src == ',') { src++; c++; continue; }
        if (*src == '\r') { src++; }
        if (*src == '\n') { src++; }
        r++;
        c = 0;
    }
    li.rows = r;
    if (li.rows > SHEET_ROWS) { li.dropped_rows = li.rows - SHEET_ROWS; }
    if (li.cols > SHEET_COLS) { li.dropped_cols = li.cols - SHEET_COLS; }
    sheet_recalc(s);
    if (info != NULL) { *info = li; }
    return (li.dropped_rows == 0 && li.dropped_cols == 0 && li.clipped == 0)
           ? CTRUE : CFALSE;
}
