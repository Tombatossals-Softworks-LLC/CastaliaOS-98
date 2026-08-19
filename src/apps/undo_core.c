/*
 * undo_core.c - Undo and redo for a flat text buffer (see undo_core.h).
 */
#include "undo_core.h"

void undo_init(UndoStack *u)
{
    if (u == NULL) { return; }
    u->count = 0;
    u->at = 0;
}

void undo_record(UndoStack *u, int kind, int pos, const char *text, int len)
{
    UndoRec *r;
    int i;
    if (u == NULL || text == NULL) { return; }
    if (kind != UNDO_INSERT && kind != UNDO_DELETE) { return; }
    if (len <= 0 || pos < 0) { return; }
    if (len > UNDO_TEXT_MAX) {
        /* Cannot be remembered faithfully, so remember nothing. Half an edit
         * in the history is worse than an empty one: undoing it would eat or
         * duplicate characters and leave a document that still looks fine. */
        undo_init(u);
        return;
    }

    /*
     * A new edit forks the timeline: everything ahead of the cursor is
     * abandoned. Keeping it is how a redo puts back text the author has
     * already replaced with something else.
     */
    u->count = u->at;

    if (u->count >= UNDO_LEVELS) {
        /* Drop the oldest. A memmove of the whole array is fine at this size
         * and keeps the indices trivially correct -- the ring arithmetic that
         * would avoid it is exactly the thing this tree has got wrong before. */
        for (i = 0; i + 1 < UNDO_LEVELS; i++) { u->rec[i] = u->rec[i + 1]; }
        u->count = UNDO_LEVELS - 1;
    }
    r = &u->rec[u->count];
    r->kind = kind;
    r->pos = pos;
    r->len = len;
    for (i = 0; i < len; i++) { r->text[i] = text[i]; }
    u->count++;
    u->at = u->count;
}

cbool undo_can_undo(const UndoStack *u)
{
    return (u != NULL && u->at > 0) ? CTRUE : CFALSE;
}

cbool undo_can_redo(const UndoStack *u)
{
    return (u != NULL && u->at < u->count) ? CTRUE : CFALSE;
}

/* Put 'len' characters back at 'pos'. Returns CFALSE if they will not fit. */
static cbool undo_insert_at(char *buf, int *blen, int cap, int pos,
                            const char *text, int len)
{
    int i;
    if (pos < 0 || pos > *blen) { return CFALSE; }
    if (*blen + len > cap) { return CFALSE; }
    for (i = *blen - 1; i >= pos; i--) { buf[i + len] = buf[i]; }
    for (i = 0; i < len; i++) { buf[pos + i] = text[i]; }
    *blen += len;
    return CTRUE;
}

/* Take 'len' characters away at 'pos'. */
static cbool undo_delete_at(char *buf, int *blen, int pos, int len)
{
    int i;
    if (pos < 0 || len < 0 || pos + len > *blen) { return CFALSE; }
    for (i = pos; i + len < *blen; i++) { buf[i] = buf[i + len]; }
    *blen -= len;
    return CTRUE;
}

int undo_undo(UndoStack *u, char *buf, int *len, int cap)
{
    const UndoRec *r;
    if (u == NULL || buf == NULL || len == NULL) { return -1; }
    if (!undo_can_undo(u)) { return -1; }
    r = &u->rec[u->at - 1];
    /* The inverse of what was done. */
    if (r->kind == UNDO_INSERT) {
        if (!undo_delete_at(buf, len, r->pos, r->len)) { return -1; }
    } else {
        if (!undo_insert_at(buf, len, cap, r->pos, r->text, r->len)) {
            return -1;
        }
    }
    u->at--;
    return r->pos;
}

int undo_redo(UndoStack *u, char *buf, int *len, int cap)
{
    const UndoRec *r;
    if (u == NULL || buf == NULL || len == NULL) { return -1; }
    if (!undo_can_redo(u)) { return -1; }
    r = &u->rec[u->at];
    /* ...and doing it again. */
    if (r->kind == UNDO_INSERT) {
        if (!undo_insert_at(buf, len, cap, r->pos, r->text, r->len)) {
            return -1;
        }
    } else {
        if (!undo_delete_at(buf, len, r->pos, r->len)) { return -1; }
    }
    u->at++;
    return (r->kind == UNDO_INSERT) ? (r->pos + r->len) : r->pos;
}

/* ---- cell undo --------------------------------------------------------- */
static void cundo_copy(char *dst, const char *src, int cap)
{
    int i = 0;
    if (src != NULL) {
        while (i < cap - 1 && src[i] != '\0') { dst[i] = src[i]; i++; }
    }
    dst[i] = '\0';
}

void cundo_init(CellUndo *u)
{
    if (u == NULL) { return; }
    u->count = 0;
    u->at = 0;
}

void cundo_record(CellUndo *u, int sheet, int row, int col,
                  const char *before, const char *after)
{
    CellEdit *r;
    int i;
    if (u == NULL) { return; }
    /* Setting a cell to what it already held is not an edit. Recording it
     * would make Ctrl+Z appear to do nothing, once per wasted keystroke. */
    if (before != NULL && after != NULL) {
        int same = 1;
        for (i = 0; i < (int)sizeof r->before; i++) {
            if (before[i] != after[i]) { same = 0; break; }
            if (before[i] == '\0') { break; }
        }
        if (same) { return; }
    }
    u->count = u->at;               /* the new edit forks the timeline */
    if (u->count >= CUNDO_LEVELS) {
        for (i = 0; i + 1 < CUNDO_LEVELS; i++) { u->rec[i] = u->rec[i + 1]; }
        u->count = CUNDO_LEVELS - 1;
    }
    r = &u->rec[u->count];
    r->sheet = sheet; r->row = row; r->col = col;
    cundo_copy(r->before, before, (int)sizeof r->before);
    cundo_copy(r->after,  after,  (int)sizeof r->after);
    u->count++;
    u->at = u->count;
}

cbool cundo_can_undo(const CellUndo *u)
{
    return (u != NULL && u->at > 0) ? CTRUE : CFALSE;
}

cbool cundo_can_redo(const CellUndo *u)
{
    return (u != NULL && u->at < u->count) ? CTRUE : CFALSE;
}

const char *cundo_undo(CellUndo *u, int *sheet, int *row, int *col)
{
    const CellEdit *r;
    if (!cundo_can_undo(u)) { return NULL; }
    r = &u->rec[u->at - 1];
    if (sheet) { *sheet = r->sheet; }
    if (row)   { *row   = r->row; }
    if (col)   { *col   = r->col; }
    u->at--;
    return r->before;
}

const char *cundo_redo(CellUndo *u, int *sheet, int *row, int *col)
{
    const CellEdit *r;
    if (!cundo_can_redo(u)) { return NULL; }
    r = &u->rec[u->at];
    if (sheet) { *sheet = r->sheet; }
    if (row)   { *row   = r->row; }
    if (col)   { *col   = r->col; }
    u->at++;
    return r->after;
}

/* ---- attributed-text undo ---------------------------------------------- */
void wundo_init(WUndoStack *u)
{
    if (u == NULL) { return; }
    u->count = 0;
    u->at = 0;
}

void wundo_record(WUndoStack *u, int kind, int pos,
                  const char *text, const unsigned char *attr, int len)
{
    WUndoRec *r;
    int i;
    if (u == NULL || text == NULL) { return; }
    if (kind != UNDO_INSERT && kind != UNDO_DELETE) { return; }
    if (len <= 0 || pos < 0) { return; }
    if (len > UNDO_TEXT_MAX) { wundo_init(u); return; }

    u->count = u->at;                 /* a new edit forks the timeline */
    if (u->count >= WUNDO_LEVELS) {
        for (i = 0; i + 1 < WUNDO_LEVELS; i++) { u->rec[i] = u->rec[i + 1]; }
        u->count = WUNDO_LEVELS - 1;
    }
    r = &u->rec[u->count];
    r->kind = kind;
    r->pos = pos;
    r->len = len;
    for (i = 0; i < len; i++) {
        r->text[i] = text[i];
        /* No attributes supplied means plain text, not undefined bytes -- a
         * record half-filled from the stack would "restore" random emphasis. */
        r->attr[i] = (attr != NULL) ? attr[i] : (unsigned char)0;
    }
    u->count++;
    u->at = u->count;
}

cbool wundo_can_undo(const WUndoStack *u)
{
    return (u != NULL && u->at > 0) ? CTRUE : CFALSE;
}

cbool wundo_can_redo(const WUndoStack *u)
{
    return (u != NULL && u->at < u->count) ? CTRUE : CFALSE;
}

const WUndoRec *wundo_undo_step(WUndoStack *u)
{
    if (!wundo_can_undo(u)) { return NULL; }
    u->at--;
    return &u->rec[u->at];
}

const WUndoRec *wundo_redo_step(WUndoStack *u)
{
    const WUndoRec *r;
    if (!wundo_can_redo(u)) { return NULL; }
    r = &u->rec[u->at];
    u->at++;
    return r;
}
