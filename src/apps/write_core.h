/*
 * write_core.h - Pure word-processor document model (no gfx/wm/platform).
 *
 * The model behind app_write.c ("CastaliaWrite"), kept free of UI so the host
 * unit tests can exercise it hermetically (tests/test_write.c) -- the same
 * split mines_core.c and sheet_core.c use.
 *
 * A document is a flat character buffer with a parallel attribute byte per
 * character (bold / underline), plus one alignment per paragraph. Paragraphs
 * are '\n'-separated; inserting or removing a break splits or joins the
 * paragraph alignment list so formatting follows the text.
 *
 * Layout is a pure function: given a column width in characters it produces
 * the visual rows a greedy word-wrap would draw, which is what the UI renders
 * and what the tests assert on.
 */
#ifndef CASTALIA_WRITE_CORE_H
#define CASTALIA_WRITE_CORE_H

#include "castalia/ctypes.h"

#define WR_MAX_TEXT  4096    /* document characters (plus a NUL)          */
#define WR_MAX_PARAS 256     /* paragraphs                                 */
#define WR_MAX_ROWS  512     /* laid-out visual rows                       */

/* Character attributes (bit mask). */
#define WR_BOLD      0x01
#define WR_UNDER     0x02
#define WR_ITALIC    0x04

/* Paragraph alignment. */
enum { WR_LEFT = 0, WR_CENTER, WR_RIGHT };

typedef struct {
    char          text[WR_MAX_TEXT];
    unsigned char attr[WR_MAX_TEXT];
    int           len;
    unsigned char align[WR_MAX_PARAS];
    int           caret;        /* 0..len                                  */
    int           sel;          /* selection anchor, or -1 when none       */
} WriteDoc;

/* One laid-out visual row. */
typedef struct {
    int start;                  /* first character index                   */
    int len;                    /* characters on the row (no '\n')         */
    int para;                   /* owning paragraph index                  */
    int align;                  /* that paragraph's alignment              */
} WrRow;

/* Empty document: one empty left-aligned paragraph, caret at 0. */
void wr_clear(WriteDoc *d);

/* Replace the whole document with plain text (paragraph breaks on '\n').
 * Attributes reset to normal, alignment to left. */
void wr_set_text(WriteDoc *d, const char *text);

/* Insert one character at the caret with 'attr' (advances the caret).
 * '\n' splits the paragraph. Returns CFALSE when the buffer is full. */
cbool wr_insert(WriteDoc *d, char ch, unsigned char attr);

/* Delete the character before / at the caret. Removing a '\n' joins the two
 * paragraphs (the first one's alignment wins). */
void wr_backspace(WriteDoc *d);
void wr_delete(WriteDoc *d);

/* Delete the current selection (if any) and clear it. Returns CTRUE if it
 * removed anything; the caret lands at the start of the removed span. */
cbool wr_delete_selection(WriteDoc *d);

/* Selection helpers. sel < 0 means "no selection". */
cbool wr_has_selection(const WriteDoc *d);
int   wr_sel_start(const WriteDoc *d);
int   wr_sel_end(const WriteDoc *d);

/* Turn attribute bits on or off across [from, to). Clamped to the document. */
void wr_apply_attr(WriteDoc *d, int from, int to, unsigned char mask, cbool on);

/* The paragraph index that contains character position 'pos'. */
int  wr_para_of(const WriteDoc *d, int pos);
int  wr_para_count(const WriteDoc *d);
/* Alignment of the paragraph at 'pos' / set it. */
int  wr_get_align(const WriteDoc *d, int pos);
void wr_set_align(WriteDoc *d, int pos, int align);

/* Greedy word-wrap into 'cols' characters. Fills up to 'maxrows' rows and
 * returns how many were produced (always at least 1). */
int  wr_layout(const WriteDoc *d, int cols, WrRow *rows, int maxrows);

/* The row index holding 'pos' (the later row when pos sits on a wrap seam). */
int  wr_row_of(const WrRow *rows, int nrows, int pos);

/* ---- find / replace ---------------------------------------------------- */
/* Index of the next occurrence of 'needle' at or after 'from', or -1. The
 * search wraps to the start of the document once, so repeatedly calling it
 * with the previous hit + 1 walks every match and comes back round.
 * Case-insensitive; an empty needle never matches. */
int  wr_find(const WriteDoc *d, const char *needle, int from, cbool wrap);

/* Replace the 'len' characters at 'at' with 'text', keeping the attributes of
 * the first replaced character so a replacement inherits its run's formatting.
 * Returns the length actually inserted. */
int  wr_replace_at(WriteDoc *d, int at, int len, const char *text);

/* Replace every occurrence, returning how many were replaced. */
int  wr_replace_all(WriteDoc *d, const char *needle, const char *text);

/* Counts for the status bar. */
int  wr_word_count(const WriteDoc *d);
int  wr_char_count(const WriteDoc *d);

/* ---- serialization ---------------------------------------------------- */
/*
 * A plain, repairable text format so a document can be fixed with any editor:
 *
 *   CWRITE1
 *   .P 0
 *   Plain, {b}bold{/b}, {i}italic{/i} and {u}underlined{/u} text.
 *
 * '.P n' opens a paragraph with alignment n; the inline tags toggle runs.
 * Writes at most 'outsz' bytes and returns the length the document NEEDS,
 * which is >= outsz when the buffer was too small and the output was
 * truncated -- callers must check before writing it out. WR_SERIAL_MAX is the
 * worst case (every character toggling all three attributes on and off around
 * itself, plus a '.P n' header per paragraph), so a buffer of that size can
 * never truncate. Parsing is forgiving: unknown lines become text.
 */
#define WR_SERIAL_MAX (WR_MAX_TEXT * 22 + WR_MAX_PARAS * 5 + 16)
int   wr_serialize(const WriteDoc *d, char *out, int outsz);
/*
 * Parse a document into 'd'. Returns CTRUE only when the WHOLE source fits --
 * the model holds WR_MAX_TEXT characters and WR_MAX_PARAS paragraphs, and a
 * file with more than that used to load its first 4095 characters and report
 * success, after which Save wrote those 4095 back over the original.
 *
 * 'dropped' (may be NULL) receives the count of source characters that could
 * not be stored, plus one per paragraph whose alignment was lost.
 */
cbool wr_parse(WriteDoc *d, const char *src, int *dropped);

#endif /* CASTALIA_WRITE_CORE_H */
