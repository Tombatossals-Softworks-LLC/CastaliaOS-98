/*
 * undo_core.h - Undo and redo for a flat text buffer.
 *
 * Notepad, CastaliaWrite and CastaliaSheet have had no undo at all. Paint has
 * one; the editors, where Ctrl+Z is pure muscle memory, did not. Typing over
 * a selection you meant to keep is the single most-regretted keystroke in any
 * text editor, and until now this system had no answer to it.
 *
 * The design is bounded on purpose, because there is no allocator to lean on:
 * a fixed ring of EDIT RECORDS rather than snapshots of the buffer. A
 * snapshot ring of a 16 KB Notepad buffer would cost 16 KB per level; a
 * record costs the characters that actually changed. Sixty-four levels of a
 * 128-character record is 8 KB for the whole history.
 *
 * Every edit is one of two things, and each is the other's inverse:
 *
 *     INSERT n chars at p   undone by   deleting n chars at p
 *     DELETE n chars at p   undone by   inserting them back at p
 *
 * ...which is why the deleted text is kept in the record. That is the part
 * that is easy to get wrong and impossible to see: an undo stack that forgets
 * what it deleted will happily "restore" the wrong characters, and the
 * document looks perfectly normal afterwards.
 *
 * Redo is the same list walked the other way, and a NEW edit made after
 * undoing discards everything ahead of it -- the timeline forks, and keeping
 * the abandoned branch is how a redo puts back text the author has already
 * replaced.
 */
#ifndef CASTALIA_UNDO_CORE_H
#define CASTALIA_UNDO_CORE_H

#include "castalia/ctypes.h"

#define UNDO_LEVELS   64     /* edits remembered                          */
#define UNDO_TEXT_MAX 128    /* characters kept per edit                  */

enum { UNDO_NONE = 0, UNDO_INSERT, UNDO_DELETE };

typedef struct {
    int  kind;                    /* UNDO_INSERT / UNDO_DELETE            */
    int  pos;                     /* offset in the buffer                 */
    int  len;                     /* characters inserted or removed       */
    char text[UNDO_TEXT_MAX];     /* what they were (needed to undo both) */
} UndoRec;

typedef struct {
    UndoRec rec[UNDO_LEVELS];
    int     count;   /* records held, oldest first                        */
    int     at;      /* how many of them are "done" -- the cursor          */
} UndoStack;

/* Start empty. */
void undo_init(UndoStack *u);

/*
 * Record an edit that has just been APPLIED to the buffer.
 *
 * 'text' is what was inserted, or what was removed -- in both cases the
 * characters themselves, because undoing a delete needs them back and
 * undoing an insert needs to know how many to take away.
 *
 * An edit longer than UNDO_TEXT_MAX cannot be recorded faithfully, so the
 * whole history is CLEARED instead. A half-remembered edit is worse than
 * none: undoing it would corrupt the document silently, where an empty
 * history merely refuses.
 */
void undo_record(UndoStack *u, int kind, int pos, const char *text, int len);

/* Is there anything to undo / redo? */
cbool undo_can_undo(const UndoStack *u);
cbool undo_can_redo(const UndoStack *u);

/*
 * Undo (or redo) one edit against 'buf', whose length is *len and whose
 * capacity is 'cap'. Returns the caret position the editor should move to,
 * or -1 if there was nothing to do or it would not fit.
 */
int undo_undo(UndoStack *u, char *buf, int *len, int cap);
int undo_redo(UndoStack *u, char *buf, int *len, int cap);

/* ---- cell undo (spreadsheets) ------------------------------------------
 *
 * A spreadsheet edit is not an insert or a delete in a stream of characters:
 * it is "cell (sheet, row, col) held X and now holds Y". Squeezing that into
 * the text stack above would mean inventing an offset for a thing that has no
 * offset, so it gets its own small stack -- with the same two properties that
 * matter, because they are the two that are invisible when wrong:
 *
 *   - the OLD text is kept, not just the location. Restoring a cell to the
 *     right emptiness instead of the right formula is the spreadsheet version
 *     of putting back the wrong characters, and a sheet full of subtly wrong
 *     numbers still recalculates and still prints.
 *   - a new edit after undoing discards the abandoned branch.
 */
#define CUNDO_LEVELS 64

typedef struct {
    int  sheet, row, col;
    char before[32];      /* SHEET_TEXT; kept literal so this header does  */
    char after[32];       /* not have to include sheet_core.h              */
} CellEdit;

typedef struct {
    CellEdit rec[CUNDO_LEVELS];
    int      count;
    int      at;
} CellUndo;

void  cundo_init(CellUndo *u);
/* Record a change that has just been applied. */
void  cundo_record(CellUndo *u, int sheet, int row, int col,
                   const char *before, const char *after);
cbool cundo_can_undo(const CellUndo *u);
cbool cundo_can_redo(const CellUndo *u);
/*
 * Step back or forward. On success the cell to restore is written through the
 * three out-parameters, and the text to put there is returned; NULL when
 * there is nothing to do.
 */
const char *cundo_undo(CellUndo *u, int *sheet, int *row, int *col);
const char *cundo_redo(CellUndo *u, int *sheet, int *row, int *col);

/* ---- attributed-text undo (CastaliaWrite) -------------------------------
 *
 * CastaliaWrite keeps a character buffer and a PARALLEL attribute byte per
 * character -- bold, italic, underline. Undoing with the flat stack above
 * would restore the characters and lose the formatting, which is a partial
 * undo that looks like a complete one: the words are right, the emphasis is
 * gone, and the document still reads as a document. That is precisely the
 * failure this whole family of bugs is made of, so the record carries the
 * attribute bytes alongside the text.
 *
 * A separate stack rather than widening UndoRec, because Notepad has no
 * attributes and would pay 8 KB for a payload it never fills.
 *
 * This one hands the record BACK rather than applying it: CastaliaWrite
 * already has wr_insert and wr_delete_range, and undo_core has no business
 * including write_core.h to reach them.
 */
#define WUNDO_LEVELS 48

typedef struct {
    int  kind;                          /* UNDO_INSERT / UNDO_DELETE      */
    int  pos;
    int  len;
    char          text[UNDO_TEXT_MAX];
    unsigned char attr[UNDO_TEXT_MAX];  /* what the flat stack cannot keep */
} WUndoRec;

typedef struct {
    WUndoRec rec[WUNDO_LEVELS];
    int      count;
    int      at;
} WUndoStack;

void  wundo_init(WUndoStack *u);
void  wundo_record(WUndoStack *u, int kind, int pos,
                   const char *text, const unsigned char *attr, int len);
cbool wundo_can_undo(const WUndoStack *u);
cbool wundo_can_redo(const WUndoStack *u);

/*
 * The record to REVERSE (undo) or to REPLAY (redo), or NULL when there is
 * nothing to do. The caller applies it: undoing an insert is a delete of
 * 'len' at 'pos', undoing a delete is an insert of 'text'/'attr' there.
 */
const WUndoRec *wundo_undo_step(WUndoStack *u);
const WUndoRec *wundo_redo_step(WUndoStack *u);

#endif /* CASTALIA_UNDO_CORE_H */
