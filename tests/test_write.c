/*
 * test_write.c - Word-processor document model (write_core.c): editing,
 * selections, character attributes, paragraph alignment across splits and
 * joins, greedy word-wrap layout, counts, and the save/load round trip.
 * Pure logic, no gfx/wm.
 */
#include "ctest.h"
#include "write_core.h"

static WriteDoc g_d;
static WrRow    g_rows[WR_MAX_ROWS];

/* Type a whole string at the caret with one attribute. */
static void type(WriteDoc *d, const char *s, unsigned char attr)
{
    int i;
    for (i = 0; s[i] != '\0'; i++) { wr_insert(d, s[i], attr); }
}

/* Copy row 'r' out as a NUL-terminated string. */
static void row_text(const WriteDoc *d, const WrRow *rows, int r, char *out)
{
    int i;
    for (i = 0; i < rows[r].len; i++) { out[i] = d->text[rows[r].start + i]; }
    out[rows[r].len] = '\0';
}

static void write_editing(void)
{
    wr_clear(&g_d);
    CHECK_EQI(wr_char_count(&g_d), 0);
    CHECK_EQI(wr_para_count(&g_d), 1);

    type(&g_d, "Hello", 0);
    CHECK_EQI(wr_char_count(&g_d), 5);
    CHECK_STR(g_d.text, "Hello");
    CHECK_EQI(g_d.caret, 5);

    /* insert in the middle */
    g_d.caret = 0;
    wr_insert(&g_d, '>', 0);
    CHECK_STR(g_d.text, ">Hello");
    CHECK_EQI(g_d.caret, 1);

    /* backspace and delete */
    wr_backspace(&g_d);
    CHECK_STR(g_d.text, "Hello");
    g_d.caret = 0;
    wr_delete(&g_d);
    CHECK_STR(g_d.text, "ello");
    /* deleting past the ends is harmless */
    g_d.caret = 0;
    wr_backspace(&g_d);
    CHECK_STR(g_d.text, "ello");
    g_d.caret = g_d.len;
    wr_delete(&g_d);
    CHECK_STR(g_d.text, "ello");
}

static void write_selection(void)
{
    wr_clear(&g_d);
    type(&g_d, "one two three", 0);
    CHECK(!wr_has_selection(&g_d));

    g_d.sel = 4; g_d.caret = 7;              /* "two" */
    CHECK(wr_has_selection(&g_d));
    CHECK_EQI(wr_sel_start(&g_d), 4);
    CHECK_EQI(wr_sel_end(&g_d), 7);

    CHECK(wr_delete_selection(&g_d));
    CHECK_STR(g_d.text, "one  three");
    CHECK_EQI(g_d.caret, 4);
    CHECK(!wr_has_selection(&g_d));
    /* a reversed selection works the same way */
    wr_clear(&g_d);
    type(&g_d, "abcdef", 0);
    g_d.caret = 1; g_d.sel = 4;
    CHECK_EQI(wr_sel_start(&g_d), 1);
    CHECK_EQI(wr_sel_end(&g_d), 4);
    wr_backspace(&g_d);                      /* deletes the selection */
    CHECK_STR(g_d.text, "aef");
}

static void write_attrs(void)
{
    wr_clear(&g_d);
    type(&g_d, "plain", 0);
    wr_apply_attr(&g_d, 0, 2, WR_BOLD, CTRUE);
    CHECK_EQI(g_d.attr[0], WR_BOLD);
    CHECK_EQI(g_d.attr[1], WR_BOLD);
    CHECK_EQI(g_d.attr[2], 0);

    /* attributes combine, and clear independently */
    wr_apply_attr(&g_d, 0, 5, WR_UNDER, CTRUE);
    CHECK_EQI(g_d.attr[0], WR_BOLD | WR_UNDER);
    CHECK_EQI(g_d.attr[4], WR_UNDER);
    wr_apply_attr(&g_d, 0, 5, WR_BOLD, CFALSE);
    CHECK_EQI(g_d.attr[0], WR_UNDER);

    /* out-of-range ranges are clamped, not crashes */
    wr_apply_attr(&g_d, -5, 999, WR_BOLD, CTRUE);
    CHECK_EQI(g_d.attr[4], WR_BOLD | WR_UNDER);

    /* italic is an independent bit that combines with the others */
    wr_clear(&g_d);
    type(&g_d, "abcd", 0);
    wr_apply_attr(&g_d, 1, 3, WR_ITALIC, CTRUE);
    wr_apply_attr(&g_d, 2, 4, WR_BOLD, CTRUE);
    CHECK_EQI(g_d.attr[1], WR_ITALIC);
    CHECK_EQI(g_d.attr[2], WR_ITALIC | WR_BOLD);
    CHECK_EQI(g_d.attr[3], WR_BOLD);

    /* typed attributes travel with inserted characters */
    wr_clear(&g_d);
    type(&g_d, "ab", WR_BOLD);
    CHECK_EQI(g_d.attr[1], WR_BOLD);
}

static void write_paragraphs(void)
{
    wr_clear(&g_d);
    type(&g_d, "first", 0);
    wr_set_align(&g_d, 0, WR_CENTER);
    CHECK_EQI(wr_get_align(&g_d, 0), WR_CENTER);

    /* splitting a paragraph carries its alignment into the new one */
    wr_insert(&g_d, '\n', 0);
    type(&g_d, "second", 0);
    CHECK_EQI(wr_para_count(&g_d), 2);
    CHECK_EQI(wr_get_align(&g_d, 0), WR_CENTER);
    CHECK_EQI(wr_get_align(&g_d, g_d.len), WR_CENTER);

    /* the two can then differ */
    wr_set_align(&g_d, g_d.len, WR_RIGHT);
    CHECK_EQI(wr_get_align(&g_d, 0), WR_CENTER);
    CHECK_EQI(wr_get_align(&g_d, g_d.len), WR_RIGHT);
    CHECK_EQI(wr_para_of(&g_d, 0), 0);
    CHECK_EQI(wr_para_of(&g_d, g_d.len), 1);

    /* joining drops the second paragraph's alignment */
    g_d.caret = 6;              /* just after the '\n' */
    wr_backspace(&g_d);
    CHECK_EQI(wr_para_count(&g_d), 1);
    CHECK_EQI(wr_get_align(&g_d, 0), WR_CENTER);
}

static void write_layout(void)
{
    int n;
    char buf[128];

    /* wrap on the last space that fits */
    wr_clear(&g_d);
    type(&g_d, "hello world foo", 0);
    n = wr_layout(&g_d, 11, g_rows, WR_MAX_ROWS);
    CHECK_EQI(n, 2);
    row_text(&g_d, g_rows, 0, buf); CHECK_STR(buf, "hello ");
    row_text(&g_d, g_rows, 1, buf); CHECK_STR(buf, "world foo");

    /* a word longer than the column breaks hard */
    wr_clear(&g_d);
    type(&g_d, "abcdefghij", 0);
    n = wr_layout(&g_d, 4, g_rows, WR_MAX_ROWS);
    CHECK_EQI(n, 3);
    row_text(&g_d, g_rows, 0, buf); CHECK_STR(buf, "abcd");
    row_text(&g_d, g_rows, 2, buf); CHECK_STR(buf, "ij");

    /* empty paragraphs still occupy a row, and rows know their paragraph */
    wr_clear(&g_d);
    type(&g_d, "a\n\nb", 0);
    n = wr_layout(&g_d, 20, g_rows, WR_MAX_ROWS);
    CHECK_EQI(n, 3);
    CHECK_EQI(g_rows[0].para, 0);
    CHECK_EQI(g_rows[1].len, 0);
    CHECK_EQI(g_rows[1].para, 1);
    CHECK_EQI(g_rows[2].para, 2);

    /* rows carry their paragraph's alignment */
    wr_set_align(&g_d, 0, WR_RIGHT);
    n = wr_layout(&g_d, 20, g_rows, WR_MAX_ROWS);
    CHECK_EQI(g_rows[0].align, WR_RIGHT);
    CHECK_EQI(g_rows[2].align, WR_LEFT);

    /* an empty document still lays out one row */
    wr_clear(&g_d);
    n = wr_layout(&g_d, 20, g_rows, WR_MAX_ROWS);
    CHECK_EQI(n, 1);
    CHECK_EQI(g_rows[0].len, 0);

    /* the caret maps to the row that contains it */
    wr_clear(&g_d);
    type(&g_d, "hello world foo", 0);
    n = wr_layout(&g_d, 11, g_rows, WR_MAX_ROWS);
    CHECK_EQI(wr_row_of(g_rows, n, 0), 0);
    CHECK_EQI(wr_row_of(g_rows, n, 3), 0);
    CHECK_EQI(wr_row_of(g_rows, n, 8), 1);
}

static void write_counts(void)
{
    wr_clear(&g_d);
    CHECK_EQI(wr_word_count(&g_d), 0);
    type(&g_d, "one two  three", 0);
    CHECK_EQI(wr_word_count(&g_d), 3);      /* runs of spaces count once */
    CHECK_EQI(wr_char_count(&g_d), 14);
    wr_clear(&g_d);
    type(&g_d, "a\nb c", 0);
    CHECK_EQI(wr_word_count(&g_d), 3);      /* newlines separate words   */
}

static void write_roundtrip(void)
{
    char buf[1024];
    WriteDoc back;

    wr_clear(&g_d);
    type(&g_d, "Hi", WR_BOLD);
    type(&g_d, " there", 0);
    wr_set_align(&g_d, 0, WR_CENTER);
    wr_insert(&g_d, '\n', 0);
    type(&g_d, "second", WR_UNDER);
    wr_set_align(&g_d, g_d.len, WR_RIGHT);

    wr_apply_attr(&g_d, 3, 8, WR_ITALIC, CTRUE);   /* "there" in italic */
    wr_serialize(&g_d, buf, (int)sizeof buf);
    CHECK(wr_parse(&back, buf, NULL));
    CHECK_STR(back.text, "Hi there\nsecond");
    CHECK_EQI(back.attr[0], WR_BOLD);
    CHECK_EQI(back.attr[1], WR_BOLD);
    CHECK_EQI(back.attr[2], 0);
    CHECK_EQI(back.attr[3], WR_ITALIC);
    CHECK_EQI(back.attr[7], WR_ITALIC);
    CHECK_EQI(back.attr[9], WR_UNDER);
    CHECK_EQI(wr_get_align(&back, 0), WR_CENTER);
    CHECK_EQI(wr_get_align(&back, back.len), WR_RIGHT);
    CHECK_EQI(wr_para_count(&back), 2);

    /* a plain text file (no header) still opens */
    CHECK(wr_parse(&back, "just text\nsecond line", NULL));
    CHECK_STR(back.text, "just text\nsecond line");
    CHECK_EQI(wr_para_count(&back), 2);

    /* ---- what did NOT fit --------------------------------------------- */
    /*
     * The model holds WR_MAX_TEXT characters. A longer file used to load its
     * first 4095, `break`, and return CTRUE -- so the app said "Opened" and
     * Save wrote those 4095 back over the whole document. The parse now says
     * how much it could not take, and CTRUE means "all of it".
     */
    {
        static char big[WR_MAX_TEXT * 2];
        int dropped = -1, i;

        /* exactly full is not overfull */
        for (i = 0; i < WR_MAX_TEXT - 1; i++) { big[i] = 'a'; }
        big[WR_MAX_TEXT - 1] = '\0';
        CHECK(wr_parse(&back, big, &dropped));
        CHECK_EQI(dropped, 0);
        CHECK_EQI(back.len, WR_MAX_TEXT - 1);

        /* ...one character more, and it is refused as whole */
        big[WR_MAX_TEXT - 1] = 'a';
        big[WR_MAX_TEXT] = '\0';
        CHECK(!wr_parse(&back, big, &dropped));
        CHECK_EQI(dropped, 1);
        CHECK_EQI(back.len, WR_MAX_TEXT - 1);   /* what fit is still there */

        /* ...and the count is the real overage, not just "some". Reporting
         * 1 for every oversize file would pass the check above. */
        for (i = 0; i < WR_MAX_TEXT + 250; i++) { big[i] = 'b'; }
        big[WR_MAX_TEXT + 250] = '\0';
        CHECK(!wr_parse(&back, big, &dropped));
        CHECK_EQI(dropped, 251);

        /* the same through the MARKUP path, which is a different loop */
        {
            static char doc[WR_MAX_TEXT * 2];
            int o = 0;
            const char *hdr = "CWRITE1\n.P 0\n";
            for (i = 0; hdr[i] != '\0'; i++) { doc[o++] = hdr[i]; }
            for (i = 0; i < WR_MAX_TEXT + 10; i++) { doc[o++] = 'c'; }
            doc[o] = '\0';
            CHECK(!wr_parse(&back, doc, &dropped));
            CHECK_EQI(dropped, 11);   /* 4106 asked for, 4095 stored */
            CHECK_EQI(back.len, WR_MAX_TEXT - 1);
            CHECK_EQI(back.text[0], 'c');
        }

        /* a document that fits reports nothing dropped through that path too */
        CHECK(wr_parse(&back, "CWRITE1\n.P 0\nshort\n", &dropped));
        CHECK_EQI(dropped, 0);

        /* NULL 'dropped' is allowed and does not change the verdict */
        CHECK(!wr_parse(&back, big, NULL));
        CHECK(wr_parse(&back, "CWRITE1\n.P 0\nshort\n", NULL));

        /* bad arguments are a refusal, and say nothing was dropped rather
         * than leaving the caller's variable as it found it */
        dropped = 77;
        CHECK(!wr_parse(NULL, "x", &dropped));
        CHECK_EQI(dropped, 0);
        dropped = 77;
        CHECK(!wr_parse(&back, NULL, &dropped));
        CHECK_EQI(dropped, 0);
    }
}

/* The worst case for serialization: every character toggles all three
 * attributes around itself, which is what makes the markup outrun a naive
 * "twice the text" buffer. WR_SERIAL_MAX must cover it, and the writer must
 * report a length the caller can trust against its buffer. */
static char g_ser[WR_SERIAL_MAX];

static void write_worst_case(void)
{
    int i, n;

    wr_clear(&g_d);
    for (i = 0; i < WR_MAX_TEXT - 1; i++) {
        /* alternate between "all attributes" and "none" every character */
        if (!wr_insert(&g_d, (i % 40 == 39) ? '\n' : 'x',
                       (unsigned char)((i & 1) ? (WR_BOLD | WR_ITALIC | WR_UNDER)
                                               : 0))) {
            break;
        }
    }
    n = wr_serialize(&g_d, g_ser, (int)sizeof g_ser);
    CHECK(n > 0);
    CHECK(n < WR_SERIAL_MAX);          /* the bound really is the worst case */
    CHECK_EQI((int)g_ser[n], 0);       /* and it is NUL-terminated in range  */

    /* A buffer that is too small must REPORT the overflow (a length >= the
     * buffer) instead of quietly pretending it fit -- callers key off this. */
    {
        char small[64];
        int need = wr_serialize(&g_d, small, (int)sizeof small);
        CHECK(need >= (int)sizeof small);
        CHECK_EQI((int)small[sizeof small - 1], 0);   /* still terminated */
    }
}

static void write_find_replace(void)
{
    wr_clear(&g_d);
    type(&g_d, "the cat sat on the mat", 0);

    /* find walks forward, then wraps back round exactly once */
    CHECK_EQI(wr_find(&g_d, "the", 0, CTRUE), 0);
    CHECK_EQI(wr_find(&g_d, "the", 1, CTRUE), 15);
    CHECK_EQI(wr_find(&g_d, "the", 16, CTRUE), 0);      /* wrapped */
    CHECK_EQI(wr_find(&g_d, "the", 16, CFALSE), -1);    /* no wrap */
    CHECK_EQI(wr_find(&g_d, "dog", 0, CTRUE), -1);
    /* case-insensitive, and an empty needle never matches */
    CHECK_EQI(wr_find(&g_d, "CAT", 0, CTRUE), 4);
    CHECK_EQI(wr_find(&g_d, "", 0, CTRUE), -1);

    /* replacing one occurrence shifts the text and leaves the caret after it */
    CHECK_EQI(wr_replace_at(&g_d, 4, 3, "dog"), 3);
    CHECK_STR(g_d.text, "the dog sat on the mat");
    CHECK_EQI(g_d.caret, 7);

    /* a longer replacement grows the document */
    wr_clear(&g_d);
    type(&g_d, "a b", 0);
    CHECK_EQI(wr_replace_at(&g_d, 2, 1, "xyz"), 3);
    CHECK_STR(g_d.text, "a xyz");

    /* the replacement inherits the formatting of what it replaced */
    wr_clear(&g_d);
    type(&g_d, "keep ", 0);
    type(&g_d, "bold", WR_BOLD);
    wr_replace_at(&g_d, 5, 4, "BOLD");
    CHECK_STR(g_d.text, "keep BOLD");
    CHECK_EQI(g_d.attr[5], WR_BOLD);
    CHECK_EQI(g_d.attr[8], WR_BOLD);
    CHECK_EQI(g_d.attr[0], 0);

    /* replace-all covers every hit and does not loop on itself */
    wr_clear(&g_d);
    type(&g_d, "the cat sat on the mat", 0);
    CHECK_EQI(wr_replace_all(&g_d, "at", "og"), 3);
    CHECK_STR(g_d.text, "the cog sog on the mog");

    /* a replacement that contains the needle must still terminate */
    wr_clear(&g_d);
    type(&g_d, "aaa", 0);
    CHECK_EQI(wr_replace_all(&g_d, "a", "aa"), 3);
    CHECK_STR(g_d.text, "aaaaaa");

    /* nothing to find changes nothing */
    wr_clear(&g_d);
    type(&g_d, "hello", 0);
    CHECK_EQI(wr_replace_all(&g_d, "zz", "y"), 0);
    CHECK_STR(g_d.text, "hello");
}

void test_write(void)
{
    printf("- write\n");
    write_editing();
    write_selection();
    write_attrs();
    write_paragraphs();
    write_layout();
    write_counts();
    write_roundtrip();
    write_worst_case();
    write_find_replace();
}
