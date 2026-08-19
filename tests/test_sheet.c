/*
 * test_sheet.c - Spreadsheet model + formula engine (sheet_core.c):
 * classification, arithmetic and precedence, cell references, range functions,
 * error propagation (#DIV/0!, #REF!, #CYCLE!), and display formatting.
 * Pure logic, no gfx/wm.
 */
#include "ctest.h"
#include "sheet_core.h"
#include "castalia/sys.h"

static Sheet g_sh;

/* Doubles compare within a small epsilon. */
static void check_near(double a, double b, const char *what)
{
    double d = a - b;
    if (d < 0.0) { d = -d; }
    g_checks++;
    if (!(d < 1e-9)) {
        g_fails++;
        fprintf(stderr, "  FAIL %s:%d  %s  (%.6f != %.6f)\n",
                __FILE__, __LINE__, what, a, b);
    }
}

/* Put a formula in A10 and return its value / status. */
static double eval1(const char *formula, int *out_status)
{
    sheet_set(&g_sh, 9, 0, formula);
    if (out_status != NULL) { *out_status = sheet_status(&g_sh, 9, 0); }
    return sheet_value(&g_sh, 9, 0);
}

static void sheet_classify(void)
{
    sheet_clear(&g_sh);
    CHECK_EQI(sheet_kind(&g_sh, 0, 0), SHEET_EMPTY);

    sheet_set(&g_sh, 0, 0, "42");
    CHECK_EQI(sheet_kind(&g_sh, 0, 0), SHEET_NUMBER);
    check_near(sheet_value(&g_sh, 0, 0), 42.0, "A1 == 42");

    sheet_set(&g_sh, 0, 1, "-3.5");
    CHECK_EQI(sheet_kind(&g_sh, 0, 1), SHEET_NUMBER);
    check_near(sheet_value(&g_sh, 0, 1), -3.5, "B1 == -3.5");

    sheet_set(&g_sh, 0, 2, "Total");
    CHECK_EQI(sheet_kind(&g_sh, 0, 2), SHEET_LABEL);
    check_near(sheet_value(&g_sh, 0, 2), 0.0, "label has no value");

    sheet_set(&g_sh, 0, 3, "=1+1");
    CHECK_EQI(sheet_kind(&g_sh, 0, 3), SHEET_FORMULA);

    /* Clearing a cell empties it again. */
    sheet_set(&g_sh, 0, 0, "");
    CHECK_EQI(sheet_kind(&g_sh, 0, 0), SHEET_EMPTY);

    /* The raw text is preserved exactly as typed. */
    sheet_set(&g_sh, 1, 0, "=2*3");
    CHECK_STR(sheet_raw(&g_sh, 1, 0), "=2*3");
}

static void sheet_arith(void)
{
    int st;
    sheet_clear(&g_sh);
    check_near(eval1("=1+2", &st), 3.0, "1+2");
    CHECK_EQI(st, SHEET_OK);
    check_near(eval1("=10-4", NULL), 6.0, "10-4");
    check_near(eval1("=6*7", NULL), 42.0, "6*7");
    check_near(eval1("=9/2", NULL), 4.5, "9/2");
    /* precedence: * and / bind tighter than + and - */
    check_near(eval1("=2+3*4", NULL), 14.0, "2+3*4");
    check_near(eval1("=(2+3)*4", NULL), 20.0, "(2+3)*4");
    check_near(eval1("=100/10/2", NULL), 5.0, "left-assoc /");
    check_near(eval1("=10-3-2", NULL), 5.0, "left-assoc -");
    /* unary signs and whitespace */
    check_near(eval1("=-5+2", NULL), -3.0, "unary minus");
    check_near(eval1("=  7  *  2 ", NULL), 14.0, "whitespace");
    check_near(eval1("=2.5*4", NULL), 10.0, "decimals");
}

static void sheet_refs(void)
{
    int st;
    sheet_clear(&g_sh);
    sheet_set(&g_sh, 0, 0, "10");     /* A1 */
    sheet_set(&g_sh, 1, 0, "32");     /* A2 */
    check_near(eval1("=A1+A2", &st), 42.0, "A1+A2");
    CHECK_EQI(st, SHEET_OK);
    /* lower-case references work too */
    check_near(eval1("=a1*2", NULL), 20.0, "a1*2");

    /* a chain of dependent formulas resolves in one recalc */
    sheet_clear(&g_sh);
    sheet_set(&g_sh, 0, 0, "2");      /* A1 = 2      */
    sheet_set(&g_sh, 1, 0, "=A1*3");  /* A2 = 6      */
    sheet_set(&g_sh, 2, 0, "=A2+A1"); /* A3 = 8      */
    sheet_set(&g_sh, 3, 0, "=A3*A2"); /* A4 = 48     */
    check_near(sheet_value(&g_sh, 1, 0), 6.0, "A2");
    check_near(sheet_value(&g_sh, 2, 0), 8.0, "A3");
    check_near(sheet_value(&g_sh, 3, 0), 48.0, "A4");

    /* editing the root re-flows the whole chain */
    sheet_set(&g_sh, 0, 0, "5");
    check_near(sheet_value(&g_sh, 3, 0), 300.0, "A4 after edit");

    /* an empty or text cell reads as zero */
    sheet_clear(&g_sh);
    sheet_set(&g_sh, 0, 1, "hello");
    check_near(eval1("=B1+7", NULL), 7.0, "text ref is 0");
}

static void sheet_functions(void)
{
    int st;
    sheet_clear(&g_sh);
    sheet_set(&g_sh, 0, 0, "1");
    sheet_set(&g_sh, 1, 0, "2");
    sheet_set(&g_sh, 2, 0, "3");
    sheet_set(&g_sh, 3, 0, "4");

    check_near(eval1("=SUM(A1:A4)", &st), 10.0, "SUM range");
    CHECK_EQI(st, SHEET_OK);
    check_near(eval1("=AVG(A1:A4)", NULL), 2.5, "AVG range");
    check_near(eval1("=MIN(A1:A4)", NULL), 1.0, "MIN range");
    check_near(eval1("=MAX(A1:A4)", NULL), 4.0, "MAX range");
    check_near(eval1("=COUNT(A1:A4)", NULL), 4.0, "COUNT range");
    /* case-insensitive names, argument lists, mixed refs and literals */
    check_near(eval1("=sum(A1:A4)", NULL), 10.0, "lower-case SUM");
    check_near(eval1("=SUM(A1,A2,10)", NULL), 13.0, "SUM arg list");
    check_near(eval1("=SUM(A1:A2)*2+1", NULL), 7.0, "SUM in a bigger expr");
    /* ranges skip empty and text cells */
    sheet_set(&g_sh, 4, 0, "note");
    check_near(eval1("=COUNT(A1:A8)", NULL), 4.0, "COUNT skips text/empty");
    check_near(eval1("=SUM(A1:A8)", NULL), 10.0, "SUM skips text/empty");
    /* a rectangular range spans columns too */
    sheet_clear(&g_sh);
    sheet_set(&g_sh, 0, 0, "1"); sheet_set(&g_sh, 0, 1, "2");
    sheet_set(&g_sh, 1, 0, "3"); sheet_set(&g_sh, 1, 1, "4");
    check_near(eval1("=SUM(A1:B2)", NULL), 10.0, "rectangular SUM");
    /* nested calls */
    check_near(eval1("=SUM(A1:A2)+MAX(B1:B2)", NULL), 8.0, "nested funcs");

    /* A realistic two-call formula must survive storage intact -- a cell that
     * is too short silently truncates the closing paren into a syntax error. */
    sheet_clear(&g_sh);
    for (st = 0; st < 6; st++) {
        char n[8];
        sys_snprintf(n, sizeof n, "%d", (st + 1) * 10);
        sheet_set(&g_sh, st + 1, 3, n);          /* D2..D7 = 10..60 */
    }
    sheet_set(&g_sh, 8, 5, "=SUM(D2:D7)/COUNT(D2:D7)");
    CHECK_STR(sheet_raw(&g_sh, 8, 5), "=SUM(D2:D7)/COUNT(D2:D7)");
    CHECK_EQI(sheet_status(&g_sh, 8, 5), SHEET_OK);
    check_near(sheet_value(&g_sh, 8, 5), 35.0, "SUM/COUNT");
}

static void sheet_errors(void)
{
    int st;
    sheet_clear(&g_sh);

    eval1("=1/0", &st);
    CHECK_EQI(st, SHEET_ERR_DIV0);

    eval1("=Z9", &st);                    /* outside a 16-column sheet */
    CHECK_EQI(st, SHEET_ERR_REF);

    eval1("=1+", &st);
    CHECK_EQI(st, SHEET_ERR_SYNTAX);
    eval1("=(1+2", &st);
    CHECK_EQI(st, SHEET_ERR_SYNTAX);
    eval1("=NOPE(1)", &st);
    CHECK_EQI(st, SHEET_ERR_SYNTAX);

    /* a direct self-reference is a cycle, not a hang */
    sheet_clear(&g_sh);
    sheet_set(&g_sh, 0, 0, "=A1");
    CHECK_EQI(sheet_status(&g_sh, 0, 0), SHEET_ERR_CYCLE);

    /* ...and so is a mutual one */
    sheet_clear(&g_sh);
    sheet_set(&g_sh, 0, 0, "=B1+1");
    sheet_set(&g_sh, 0, 1, "=A1+1");
    CHECK(sheet_status(&g_sh, 0, 0) == SHEET_ERR_CYCLE ||
          sheet_status(&g_sh, 0, 1) == SHEET_ERR_CYCLE);

    /* errors propagate to dependents */
    sheet_clear(&g_sh);
    sheet_set(&g_sh, 0, 0, "=1/0");
    sheet_set(&g_sh, 1, 0, "=A1+1");
    CHECK_EQI(sheet_status(&g_sh, 1, 0), SHEET_ERR_DIV0);

    /* a recovered cell clears the error */
    sheet_set(&g_sh, 0, 0, "4");
    CHECK_EQI(sheet_status(&g_sh, 1, 0), SHEET_OK);
    check_near(sheet_value(&g_sh, 1, 0), 5.0, "A2 recovered");
}

static void sheet_format(void)
{
    char buf[32];

    sheet_fmt_num(42.0, buf, (int)sizeof buf);
    CHECK_STR(buf, "42");
    sheet_fmt_num(-7.0, buf, (int)sizeof buf);
    CHECK_STR(buf, "-7");
    sheet_fmt_num(2.5, buf, (int)sizeof buf);
    CHECK_STR(buf, "2.5");
    sheet_fmt_num(3.14159, buf, (int)sizeof buf);
    CHECK_STR(buf, "3.14");
    sheet_fmt_num(0.05, buf, (int)sizeof buf);
    CHECK_STR(buf, "0.05");
    sheet_fmt_num(0.0, buf, (int)sizeof buf);
    CHECK_STR(buf, "0");

    /* display: labels verbatim, numbers formatted, errors tagged */
    sheet_clear(&g_sh);
    sheet_set(&g_sh, 0, 0, "Widgets");
    sheet_display(&g_sh, 0, 0, buf, (int)sizeof buf);
    CHECK_STR(buf, "Widgets");

    sheet_set(&g_sh, 0, 1, "=3/2");
    sheet_display(&g_sh, 0, 1, buf, (int)sizeof buf);
    CHECK_STR(buf, "1.5");

    sheet_set(&g_sh, 0, 2, "=1/0");
    sheet_display(&g_sh, 0, 2, buf, (int)sizeof buf);
    CHECK_STR(buf, "#DIV/0!");

    sheet_display(&g_sh, 5, 5, buf, (int)sizeof buf);
    CHECK_STR(buf, "");

    /* names */
    sheet_colname(0, buf, (int)sizeof buf);
    CHECK_STR(buf, "A");
    sheet_colname(SHEET_COLS - 1, buf, (int)sizeof buf);
    CHECK_STR(buf, "P");
    sheet_cellname(0, 0, buf, (int)sizeof buf);
    CHECK_STR(buf, "A1");
    sheet_cellname(11, 2, buf, (int)sizeof buf);
    CHECK_STR(buf, "C12");
}

static void sheet_ref_parsing(void)
{
    int r, c, len;
    CHECK(sheet_ref_parse("A1", &r, &c, &len));
    CHECK_EQI(r, 0); CHECK_EQI(c, 0); CHECK_EQI(len, 2);

    CHECK(sheet_ref_parse("c12+1", &r, &c, &len));
    CHECK_EQI(r, 11); CHECK_EQI(c, 2); CHECK_EQI(len, 3);

    CHECK(sheet_ref_parse("P48", &r, &c, &len));
    CHECK_EQI(r, SHEET_ROWS - 1); CHECK_EQI(c, SHEET_COLS - 1);

    CHECK(!sheet_ref_parse("Z9", &r, &c, &len));    /* column out of range */
    CHECK(!sheet_ref_parse("A99", &r, &c, &len));   /* row out of range    */
    CHECK(!sheet_ref_parse("A0", &r, &c, &len));    /* rows are 1-based    */
    CHECK(!sheet_ref_parse("AA1", &r, &c, &len));   /* single letter only  */
    CHECK(!sheet_ref_parse("42", &r, &c, &len));
    CHECK(!sheet_ref_parse("", &r, &c, &len));
}

/* Out-of-range coordinates must never write outside the sheet. */
static void sheet_bounds(void)
{
    sheet_clear(&g_sh);
    sheet_set(&g_sh, -1, 0, "x");
    sheet_set(&g_sh, 0, SHEET_COLS, "x");
    sheet_set(&g_sh, SHEET_ROWS, 0, "x");
    CHECK_EQI(sheet_kind(&g_sh, 0, 0), SHEET_EMPTY);
    CHECK_STR(sheet_raw(&g_sh, -1, -1), "");
    CHECK_EQI(sheet_kind(&g_sh, 999, 999), SHEET_EMPTY);
    check_near(sheet_value(&g_sh, 999, 999), 0.0, "oob value is 0");
}

static void sheet_mathfuncs(void)
{
    sheet_clear(&g_sh);
    check_near(eval1("=ABS(-7.5)", NULL), 7.5, "ABS negative");
    check_near(eval1("=ABS(3)", NULL), 3.0, "ABS positive");
    check_near(eval1("=INT(4.9)", NULL), 4.0, "INT truncates");
    check_near(eval1("=INT(-4.9)", NULL), -4.0, "INT toward zero");
    check_near(eval1("=ROUND(2.4)", NULL), 2.0, "ROUND down");
    check_near(eval1("=ROUND(2.5)", NULL), 3.0, "ROUND up");
    check_near(eval1("=ROUND(-2.5)", NULL), -3.0, "ROUND negative");
    /* they compose with the rest of the language */
    sheet_set(&g_sh, 0, 0, "-9.6");
    check_near(eval1("=ABS(A1)+ROUND(1.5)", NULL), 11.6, "ABS+ROUND");
}

static void sheet_csv(void)
{
    static char buf[4096];
    int n;

    /* formulas survive a round trip, and commas inside them are quoted */
    sheet_clear(&g_sh);
    sheet_set(&g_sh, 0, 0, "Item");
    sheet_set(&g_sh, 0, 1, "Qty");
    sheet_set(&g_sh, 1, 0, "Bolt, hex");        /* embedded comma */
    sheet_set(&g_sh, 1, 1, "4");
    sheet_set(&g_sh, 2, 1, "=SUM(B2,B2,1)");    /* commas in a formula */
    n = sheet_to_csv(&g_sh, buf, (int)sizeof buf);
    CHECK(n > 0);

    sheet_clear(&g_sh);
    CHECK_EQI(sheet_kind(&g_sh, 0, 0), SHEET_EMPTY);
    CHECK(sheet_from_csv(&g_sh, buf, NULL));
    CHECK_STR(sheet_raw(&g_sh, 0, 0), "Item");
    CHECK_STR(sheet_raw(&g_sh, 1, 0), "Bolt, hex");
    CHECK_STR(sheet_raw(&g_sh, 2, 1), "=SUM(B2,B2,1)");
    CHECK_EQI(sheet_kind(&g_sh, 2, 1), SHEET_FORMULA);
    check_near(sheet_value(&g_sh, 2, 1), 9.0, "formula recalculated on load");

    /* a hand-written CSV (ragged rows, no quotes) still loads */
    CHECK(sheet_from_csv(&g_sh, "1,2,3\n4,5\n", NULL));
    check_near(sheet_value(&g_sh, 0, 2), 3.0, "ragged CSV");
    check_near(sheet_value(&g_sh, 1, 1), 5.0, "ragged CSV row 2");
    CHECK_EQI(sheet_kind(&g_sh, 1, 2), SHEET_EMPTY);

    /* an embedded quote round-trips */
    sheet_clear(&g_sh);
    sheet_set(&g_sh, 0, 0, "say \"hi\"");
    sheet_to_csv(&g_sh, buf, (int)sizeof buf);
    sheet_from_csv(&g_sh, buf, NULL);
    CHECK_STR(sheet_raw(&g_sh, 0, 0), "say \"hi\"");

    /* ---- what did NOT fit --------------------------------------------- */
    /*
     * The sheet is 48 x 16. A CSV bigger than that used to load its first 48
     * rows and return CTRUE, so the app said "Opened" and Save wrote 48 rows
     * back over 100. These are the cases that decide whether a file is safe
     * to save over, so each is checked for BOTH answers -- what fit, and what
     * was left behind.
     */
    {
        SheetLoad li;
        int i;

        /* fits exactly: nothing dropped, and it says so */
        sheet_clear(&g_sh);
        CHECK(sheet_from_csv(&g_sh, "a,b\nc,d\n", &li));
        CHECK_EQI(li.rows, 2);
        CHECK_EQI(li.cols, 2);
        CHECK_EQI(li.dropped_rows, 0);
        CHECK_EQI(li.dropped_cols, 0);
        CHECK_EQI(li.clipped, 0);

        /* An empty file is zero rows, not one. Off by one here and every
         * "N rows" the app reports is off by one. */
        sheet_clear(&g_sh);
        CHECK(sheet_from_csv(&g_sh, "", &li));
        CHECK_EQI(li.rows, 0);

        /* ...and a file with no trailing newline still has its last row. */
        CHECK(sheet_from_csv(&g_sh, "a\nb", &li));
        CHECK_EQI(li.rows, 2);
        /* A trailing newline does NOT invent a row after it. */
        CHECK(sheet_from_csv(&g_sh, "a\nb\n", &li));
        CHECK_EQI(li.rows, 2);
        /* CRLF is one break, not two. */
        CHECK(sheet_from_csv(&g_sh, "a\r\nb\r\n", &li));
        CHECK_EQI(li.rows, 2);

        /* exactly full is not overfull */
        {
            static char big[SHEET_CSV_MAX];
            int o = 0;
            for (i = 0; i < SHEET_ROWS; i++) {
                big[o++] = 'x'; big[o++] = '\n';
            }
            big[o] = '\0';
            CHECK(sheet_from_csv(&g_sh, big, &li));
            CHECK_EQI(li.rows, SHEET_ROWS);
            CHECK_EQI(li.dropped_rows, 0);

            /* ...one more row, and it is refused as whole */
            big[o++] = 'x'; big[o++] = '\n'; big[o] = '\0';
            CHECK(!sheet_from_csv(&g_sh, big, &li));
            CHECK_EQI(li.rows, SHEET_ROWS + 1);
            CHECK_EQI(li.dropped_rows, 1);
            /* and what DID fit is still there and still right */
            CHECK_STR(sheet_raw(&g_sh, 0, 0), "x");
            CHECK_STR(sheet_raw(&g_sh, SHEET_ROWS - 1, 0), "x");
        }

        /* too WIDE, at the right count -- a row is dropped by columns even
         * though every row fits vertically */
        {
            static char wide[SHEET_CSV_MAX];
            int o = 0;
            for (i = 0; i < SHEET_COLS + 3; i++) {
                if (i > 0) { wide[o++] = ','; }
                wide[o++] = 'y';
            }
            wide[o++] = '\n'; wide[o] = '\0';
            CHECK(!sheet_from_csv(&g_sh, wide, &li));
            CHECK_EQI(li.rows, 1);
            CHECK_EQI(li.cols, SHEET_COLS + 3);
            CHECK_EQI(li.dropped_cols, 3);
            CHECK_EQI(li.dropped_rows, 0);
            CHECK_STR(sheet_raw(&g_sh, 0, SHEET_COLS - 1), "y");
        }

        /* a cell too long is CLIPPED, and that counts as not whole -- the
         * text is gone as surely as a dropped row, just less visibly */
        {
            static char longcell[SHEET_TEXT * 3];
            for (i = 0; i < SHEET_TEXT + 4; i++) { longcell[i] = 'z'; }
            longcell[SHEET_TEXT + 4] = '\0';
            CHECK(!sheet_from_csv(&g_sh, longcell, &li));
            CHECK_EQI(li.clipped, 1);
            CHECK_EQI(li.dropped_rows, 0);
            CHECK_EQI(li.dropped_cols, 0);
            /* what fit is the PREFIX, at exactly the documented length */
            CHECK_EQI((int)sys_strnlen(sheet_raw(&g_sh, 0, 0), 999u),
                      SHEET_TEXT - 1);
        }

        /* ...and a cell of exactly the length that fits is not clipped. The
         * check above would pass with an off-by-one that clips everything. */
        {
            static char fitcell[SHEET_TEXT * 3];
            for (i = 0; i < SHEET_TEXT - 1; i++) { fitcell[i] = 'z'; }
            fitcell[SHEET_TEXT - 1] = '\0';
            CHECK(sheet_from_csv(&g_sh, fitcell, &li));
            CHECK_EQI(li.clipped, 0);
        }

        /* a quoted cell counts its clipping too -- the quoted branch is a
         * separate loop, and it used to drop characters without counting */
        {
            static char q[SHEET_TEXT * 3];
            int o = 0;
            q[o++] = '"';
            for (i = 0; i < SHEET_TEXT + 4; i++) { q[o++] = 'w'; }
            q[o++] = '"'; q[o] = '\0';
            CHECK(!sheet_from_csv(&g_sh, q, &li));
            CHECK_EQI(li.clipped, 1);
        }

        /* NULL info is allowed, and the verdict is the same without it */
        CHECK(sheet_from_csv(&g_sh, "a,b\n", NULL));
        CHECK(!sheet_from_csv(&g_sh, "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\n"
                                     "n\no\np\nq\nr\ns\nt\nu\nv\nw\nx\ny\nz\n"
                                     "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\n"
                                     "n\no\np\nq\nr\ns\nt\nu\nv\nw\nx\ny\nz\n",
                              NULL));
        CHECK(!sheet_from_csv(NULL, "a\n", &li));
        CHECK(!sheet_from_csv(&g_sh, NULL, &li));
    }
}

/* Same guarantee for the spreadsheet: a sheet where every cell is full of
 * characters that CSV has to quote and double must still fit SHEET_CSV_MAX,
 * and a short buffer must report the overflow rather than under-report it. */
static char g_csv[SHEET_CSV_MAX];

static void sheet_worst_case(void)
{
    char cell[SHEET_TEXT];
    int r, c, n, i;

    for (i = 0; i < SHEET_TEXT - 1; i++) { cell[i] = (i & 1) ? '"' : ','; }
    cell[SHEET_TEXT - 1] = '\0';

    sheet_clear(&g_sh);
    for (r = 0; r < SHEET_ROWS; r++) {
        for (c = 0; c < SHEET_COLS; c++) {
            sys_strlcpy(g_sh.raw[r][c], cell, (cu32)SHEET_TEXT);
        }
    }
    sheet_recalc(&g_sh);

    n = sheet_to_csv(&g_sh, g_csv, (int)sizeof g_csv);
    CHECK(n > 0);
    CHECK(n < SHEET_CSV_MAX);          /* the bound really is the worst case */
    CHECK_EQI((int)g_csv[n], 0);

    {
        char small[64];
        int need = sheet_to_csv(&g_sh, small, (int)sizeof small);
        CHECK(need >= (int)sizeof small);
        CHECK_EQI((int)small[sizeof small - 1], 0);
    }
    sheet_clear(&g_sh);
}

void test_sheet(void)
{
    printf("- sheet\n");
    sheet_classify();
    sheet_arith();
    sheet_refs();
    sheet_functions();
    sheet_errors();
    sheet_format();
    sheet_ref_parsing();
    sheet_bounds();
    sheet_mathfuncs();
    sheet_csv();
    sheet_worst_case();
}
