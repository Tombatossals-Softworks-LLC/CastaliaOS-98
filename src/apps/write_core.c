/*
 * write_core.c - Word-processor document model (pure logic, host-tested).
 *
 * See write_core.h. The document is a character buffer plus a parallel
 * attribute byte and one alignment per paragraph; layout is a pure greedy
 * word-wrap that the UI renders and the tests assert on.
 */
#include "write_core.h"
#include "castalia/sys.h"

static int clampi(int v, int lo, int hi)
{
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

void wr_clear(WriteDoc *d)
{
    int i;
    if (d == NULL) { return; }
    d->len = 0;
    d->text[0] = '\0';
    d->caret = 0;
    d->sel = -1;
    for (i = 0; i < WR_MAX_PARAS; i++) { d->align[i] = WR_LEFT; }
}

void wr_set_text(WriteDoc *d, const char *text)
{
    int i = 0;
    if (d == NULL) { return; }
    wr_clear(d);
    if (text == NULL) { return; }
    while (text[i] != '\0' && i < WR_MAX_TEXT - 1) {
        d->text[i] = text[i];
        d->attr[i] = 0;
        i++;
    }
    d->len = i;
    d->text[i] = '\0';
    d->caret = 0;
}

int wr_para_count(const WriteDoc *d)
{
    int i, n = 1;
    if (d == NULL) { return 1; }
    for (i = 0; i < d->len; i++) {
        if (d->text[i] == '\n') { n++; }
    }
    return n;
}

int wr_para_of(const WriteDoc *d, int pos)
{
    int i, n = 0;
    if (d == NULL) { return 0; }
    pos = clampi(pos, 0, d->len);
    for (i = 0; i < pos; i++) {
        if (d->text[i] == '\n') { n++; }
    }
    return (n < WR_MAX_PARAS) ? n : WR_MAX_PARAS - 1;
}

int wr_get_align(const WriteDoc *d, int pos)
{
    if (d == NULL) { return WR_LEFT; }
    return (int)d->align[wr_para_of(d, pos)];
}

void wr_set_align(WriteDoc *d, int pos, int align)
{
    if (d == NULL || align < WR_LEFT || align > WR_RIGHT) { return; }
    d->align[wr_para_of(d, pos)] = (unsigned char)align;
}

/* Paragraph 'k' was split in two: duplicate its alignment into the new one. */
static void align_split(WriteDoc *d, int k)
{
    int i;
    if (k < 0 || k >= WR_MAX_PARAS - 1) { return; }
    for (i = WR_MAX_PARAS - 1; i > k + 1; i--) { d->align[i] = d->align[i - 1]; }
    d->align[k + 1] = d->align[k];
}

/* Paragraphs 'k' and 'k+1' merged: drop the second one's alignment. */
static void align_join(WriteDoc *d, int k)
{
    int i;
    if (k < 0 || k >= WR_MAX_PARAS - 1) { return; }
    for (i = k + 1; i < WR_MAX_PARAS - 1; i++) { d->align[i] = d->align[i + 1]; }
    d->align[WR_MAX_PARAS - 1] = WR_LEFT;
}

cbool wr_insert(WriteDoc *d, char ch, unsigned char attr)
{
    int i;
    if (d == NULL || d->len >= WR_MAX_TEXT - 1) { return CFALSE; }
    d->caret = clampi(d->caret, 0, d->len);
    for (i = d->len; i > d->caret; i--) {
        d->text[i] = d->text[i - 1];
        d->attr[i] = d->attr[i - 1];
    }
    d->text[d->caret] = ch;
    d->attr[d->caret] = attr;
    d->len++;
    d->text[d->len] = '\0';
    if (ch == '\n') { align_split(d, wr_para_of(d, d->caret)); }
    d->caret++;
    d->sel = -1;
    return CTRUE;
}

/* Remove the single character at 'at'. */
static void remove_at(WriteDoc *d, int at)
{
    int i;
    if (at < 0 || at >= d->len) { return; }
    if (d->text[at] == '\n') { align_join(d, wr_para_of(d, at)); }
    for (i = at; i < d->len - 1; i++) {
        d->text[i] = d->text[i + 1];
        d->attr[i] = d->attr[i + 1];
    }
    d->len--;
    d->text[d->len] = '\0';
}

void wr_backspace(WriteDoc *d)
{
    if (d == NULL) { return; }
    if (wr_delete_selection(d)) { return; }
    if (d->caret <= 0) { return; }
    remove_at(d, d->caret - 1);
    d->caret--;
}

void wr_delete(WriteDoc *d)
{
    if (d == NULL) { return; }
    if (wr_delete_selection(d)) { return; }
    if (d->caret >= d->len) { return; }
    remove_at(d, d->caret);
}

cbool wr_has_selection(const WriteDoc *d)
{
    if (d == NULL || d->sel < 0) { return CFALSE; }
    return (d->sel != d->caret) ? CTRUE : CFALSE;
}
int wr_sel_start(const WriteDoc *d)
{
    if (!wr_has_selection(d)) { return d ? d->caret : 0; }
    return (d->sel < d->caret) ? d->sel : d->caret;
}
int wr_sel_end(const WriteDoc *d)
{
    if (!wr_has_selection(d)) { return d ? d->caret : 0; }
    return (d->sel > d->caret) ? d->sel : d->caret;
}

cbool wr_delete_selection(WriteDoc *d)
{
    int a, b;
    if (!wr_has_selection(d)) { return CFALSE; }
    a = wr_sel_start(d);
    b = wr_sel_end(d);
    while (b > a) { remove_at(d, b - 1); b--; }
    d->caret = a;
    d->sel = -1;
    return CTRUE;
}

void wr_apply_attr(WriteDoc *d, int from, int to, unsigned char mask, cbool on)
{
    int i;
    if (d == NULL) { return; }
    from = clampi(from, 0, d->len);
    to   = clampi(to, 0, d->len);
    for (i = from; i < to; i++) {
        if (on) { d->attr[i] |= mask; }
        else    { d->attr[i] = (unsigned char)(d->attr[i] & ~mask); }
    }
}

int wr_layout(const WriteDoc *d, int cols, WrRow *rows, int maxrows)
{
    int i = 0, n = 0, para = 0;
    if (d == NULL || rows == NULL || maxrows <= 0) { return 0; }
    if (cols < 1) { cols = 1; }
    for (;;) {
        int pend = i;
        while (pend < d->len && d->text[pend] != '\n') { pend++; }
        if (pend == i) {                        /* empty paragraph */
            if (n < maxrows) {
                rows[n].start = i; rows[n].len = 0;
                rows[n].para = para;
                rows[n].align = (int)d->align[(para < WR_MAX_PARAS) ? para : 0];
                n++;
            }
        } else {
            int j = i;
            while (j < pend && n < maxrows) {
                int take = pend - j;
                if (take > cols) {
                    int k, brk = -1;
                    take = cols;
                    for (k = j + take; k > j; k--) {
                        if (d->text[k - 1] == ' ') { brk = k; break; }
                    }
                    if (brk > j) { take = brk - j; }
                }
                rows[n].start = j; rows[n].len = take;
                rows[n].para = para;
                rows[n].align = (int)d->align[(para < WR_MAX_PARAS) ? para : 0];
                n++;
                j += take;
            }
        }
        if (pend >= d->len) { break; }
        i = pend + 1;
        para++;
        if (n >= maxrows) { break; }
    }
    if (n == 0) {
        rows[0].start = 0; rows[0].len = 0; rows[0].para = 0;
        rows[0].align = WR_LEFT;
        n = 1;
    }
    return n;
}

int wr_row_of(const WrRow *rows, int nrows, int pos)
{
    int i, best = 0;
    if (rows == NULL || nrows <= 0) { return 0; }
    for (i = 0; i < nrows; i++) {
        if (pos >= rows[i].start) { best = i; }
    }
    return best;
}

int wr_word_count(const WriteDoc *d)
{
    int i, n = 0;
    cbool in_word = CFALSE;
    if (d == NULL) { return 0; }
    for (i = 0; i < d->len; i++) {
        char c = d->text[i];
        cbool sp = (c == ' ' || c == '\n' || c == '\t') ? CTRUE : CFALSE;
        if (!sp && !in_word) { n++; in_word = CTRUE; }
        else if (sp) { in_word = CFALSE; }
    }
    return n;
}

int wr_char_count(const WriteDoc *d) { return (d == NULL) ? 0 : d->len; }

/* ---- find / replace ---------------------------------------------------- */
static char low(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Does 'needle' sit at index 'at'? */
static cbool match_at(const WriteDoc *d, const char *needle, int at)
{
    int i = 0;
    while (needle[i] != '\0') {
        if (at + i >= d->len) { return CFALSE; }
        if (low(d->text[at + i]) != low(needle[i])) { return CFALSE; }
        i++;
    }
    return CTRUE;
}

int wr_find(const WriteDoc *d, const char *needle, int from, cbool wrap)
{
    int i;
    if (d == NULL || needle == NULL || needle[0] == '\0') { return -1; }
    if (from < 0) { from = 0; }
    for (i = from; i < d->len; i++) {
        if (match_at(d, needle, i)) { return i; }
    }
    if (!wrap) { return -1; }
    for (i = 0; i < from && i < d->len; i++) {
        if (match_at(d, needle, i)) { return i; }
    }
    return -1;
}

int wr_replace_at(WriteDoc *d, int at, int len, const char *text)
{
    unsigned char keep;
    int i, n = 0;
    if (d == NULL || at < 0 || at > d->len || len < 0) { return 0; }
    if (at + len > d->len) { len = d->len - at; }
    /* The replacement inherits the formatting of what it replaces. */
    keep = (len > 0) ? d->attr[at] : (unsigned char)0;
    for (i = 0; i < len; i++) { remove_at(d, at); }
    d->caret = at;
    if (text != NULL) {
        for (i = 0; text[i] != '\0'; i++) {
            if (!wr_insert(d, text[i], keep)) { break; }
            n++;
        }
    }
    d->sel = -1;
    return n;
}

int wr_replace_all(WriteDoc *d, const char *needle, const char *text)
{
    int at = 0, n = 0, nlen = 0, guard = 0;
    if (d == NULL || needle == NULL || needle[0] == '\0') { return 0; }
    while (needle[nlen] != '\0') { nlen++; }
    for (;;) {
        int hit = wr_find(d, needle, at, CFALSE);   /* never wrap: one pass */
        int put;
        if (hit < 0) { break; }
        if (++guard > WR_MAX_TEXT) { break; }       /* paranoia, not policy */
        put = wr_replace_at(d, hit, nlen, text);
        at = hit + put;
        if (put == 0 && nlen == 0) { break; }
        n++;
    }
    return n;
}

/* ---- serialization ---------------------------------------------------- */
static int put_str(char *out, int outsz, int at, const char *s)
{
    int i = 0;
    while (s[i] != '\0') {
        if (at < outsz - 1) { out[at] = s[i]; }
        at++;
        i++;
    }
    return at;
}

int wr_serialize(const WriteDoc *d, char *out, int outsz)
{
    int i, at = 0, para = 0;
    unsigned char cur = 0;
    char pbuf[16];
    if (d == NULL || out == NULL || outsz <= 0) { return 0; }
    at = put_str(out, outsz, at, "CWRITE1\n");
    sys_snprintf(pbuf, sizeof pbuf, ".P %d\n", (int)d->align[0]);
    at = put_str(out, outsz, at, pbuf);
    for (i = 0; i < d->len; i++) {
        unsigned char want = d->attr[i];
        if (d->text[i] == '\n') {
            if (cur & WR_UNDER)  { at = put_str(out, outsz, at, "{/u}"); }
            if (cur & WR_ITALIC) { at = put_str(out, outsz, at, "{/i}"); }
            if (cur & WR_BOLD)   { at = put_str(out, outsz, at, "{/b}"); }
            cur = 0;
            at = put_str(out, outsz, at, "\n");
            para++;
            sys_snprintf(pbuf, sizeof pbuf, ".P %d\n",
                         (int)d->align[(para < WR_MAX_PARAS) ? para : 0]);
            at = put_str(out, outsz, at, pbuf);
            continue;
        }
        if ((cur & WR_BOLD) && !(want & WR_BOLD))     { at = put_str(out, outsz, at, "{/b}"); }
        if ((cur & WR_ITALIC) && !(want & WR_ITALIC)) { at = put_str(out, outsz, at, "{/i}"); }
        if ((cur & WR_UNDER) && !(want & WR_UNDER))   { at = put_str(out, outsz, at, "{/u}"); }
        if (!(cur & WR_BOLD) && (want & WR_BOLD))     { at = put_str(out, outsz, at, "{b}"); }
        if (!(cur & WR_ITALIC) && (want & WR_ITALIC)) { at = put_str(out, outsz, at, "{i}"); }
        if (!(cur & WR_UNDER) && (want & WR_UNDER))   { at = put_str(out, outsz, at, "{u}"); }
        cur = want;
        if (at < outsz - 1) { out[at] = d->text[i]; }
        at++;
    }
    if (cur & WR_UNDER)  { at = put_str(out, outsz, at, "{/u}"); }
    if (cur & WR_ITALIC) { at = put_str(out, outsz, at, "{/i}"); }
    if (cur & WR_BOLD)   { at = put_str(out, outsz, at, "{/b}"); }
    at = put_str(out, outsz, at, "\n");
    out[(at < outsz) ? at : outsz - 1] = '\0';
    return at;
}

/* Does 'p' start with 'tag'? */
static cbool starts(const char *p, const char *tag)
{
    int i = 0;
    while (tag[i] != '\0') {
        if (p[i] != tag[i]) { return CFALSE; }
        i++;
    }
    return CTRUE;
}

cbool wr_parse(WriteDoc *d, const char *src, int *dropped)
{
    unsigned char cur = 0;
    int para = 0, lost = 0;
    cbool first_para = CTRUE;
    if (dropped != NULL) { *dropped = 0; }
    if (d == NULL || src == NULL) { return CFALSE; }
    wr_clear(d);
    if (!starts(src, "CWRITE1")) {
        int had = 0;
        while (src[had] != '\0') { had++; }
        wr_set_text(d, src);          /* plain text: still openable */
        lost = had - d->len;
        if (lost < 0) { lost = 0; }
        if (dropped != NULL) { *dropped = lost; }
        return (lost == 0) ? CTRUE : CFALSE;
    }
    while (*src != '\0' && *src != '\n') { src++; }
    if (*src == '\n') { src++; }

    while (*src != '\0') {
        if (starts(src, ".P ")) {
            int a = src[3] - '0';
            if (a < WR_LEFT || a > WR_RIGHT) { a = WR_LEFT; }
            if (!first_para) {
                d->caret = d->len;
                if (!wr_insert(d, '\n', 0)) { lost++; }
                para++;
            }
            first_para = CFALSE;
            /* Past WR_MAX_PARAS the alignment has nowhere to go. The text may
             * still fit; the formatting does not, and a save would flatten
             * every paragraph after the 256th to the left. */
            if (para < WR_MAX_PARAS) { d->align[para] = (unsigned char)a; }
            else                     { lost++; }
            cur = 0;
            while (*src != '\0' && *src != '\n') { src++; }
            if (*src == '\n') { src++; }
            continue;
        }
        if (starts(src, "{b}"))  { cur |= WR_BOLD;  src += 3; continue; }
        if (starts(src, "{/b}")) { cur = (unsigned char)(cur & ~WR_BOLD);  src += 4; continue; }
        if (starts(src, "{i}"))  { cur |= WR_ITALIC; src += 3; continue; }
        if (starts(src, "{/i}")) { cur = (unsigned char)(cur & ~WR_ITALIC); src += 4; continue; }
        if (starts(src, "{u}"))  { cur |= WR_UNDER; src += 3; continue; }
        if (starts(src, "{/u}")) { cur = (unsigned char)(cur & ~WR_UNDER); src += 4; continue; }
        if (*src == '\n') { src++; continue; }   /* paragraph text ends */
        d->caret = d->len;
        /*
         * A full document used to `break` here and return CTRUE, so the app
         * said "Opened" and Save wrote the prefix back over the whole file.
         * The walk continues instead, counting exactly what did not fit --
         * the answer to "how much of this file am I looking at" has to come
         * from reading the rest of it.
         */
        if (!wr_insert(d, *src, cur)) { lost++; }
        src++;
    }
    d->caret = 0;
    d->sel = -1;
    if (dropped != NULL) { *dropped = lost; }
    return (lost == 0) ? CTRUE : CFALSE;
}
