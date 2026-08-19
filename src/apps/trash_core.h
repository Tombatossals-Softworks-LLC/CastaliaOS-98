/*
 * trash_core.h - The Recycle Bin's bookkeeping: unique names, and the index
 * that remembers where a deleted file came from.
 *
 * Two problems, both invisible until they cost somebody a file.
 *
 * 1. COLLISIONS. Deleting is a rename into one flat TRASH folder, so two files
 *    called NOTES.TXT deleted from two different folders want the same slot.
 *    The File Manager used to resolve that by deleting whatever was already
 *    there -- so the second delete destroyed the first file, permanently,
 *    while the status line said "Moved to Trash" and Help promised that
 *    deleting "moves to the Recycle Bin rather than destroying anything".
 *    That is the worst shape a bug can have: a data-loss path that announces
 *    success.
 *
 * 2. PROVENANCE. A bin you cannot restore from is a folder called TRASH. To
 *    put a file back you have to know where it was, and nothing recorded it.
 *
 * So: a name is made unique on the way in, and an INDEX file remembers where
 * every item came from. The index is plain text -- one line per item,
 * 'stored|original path' -- because every other stored format in this system
 * is repairable with a text editor and a recycle bin is exactly where you want
 * that property. The separator is '|', which no DOS filename may contain.
 *
 * What is recorded is the WHOLE original path, not just the folder, because
 * the slot name is not always the original name: NOTES.TXT deleted second is
 * stored as NOTES~1.TXT, and putting that back as NOTES~1.TXT is not a
 * restore. The tag cannot be stripped back off either -- a file genuinely
 * named NOTES~1.TXT exists and would be restored under someone else's name.
 * The only way to give a file its name back is to have written it down.
 *
 * These functions are pure string work over a caller-supplied buffer: no
 * filesystem, so tests/test_trash.c can drive the cases that matter without
 * one, and the caller does the probing (which name is free) and the I/O.
 */
#ifndef CASTALIA_TRASH_CORE_H
#define CASTALIA_TRASH_CORE_H

#include "castalia/ctypes.h"

/*
 * The n'th candidate name for 'name' inside the bin. n = 0 is the name
 * itself; n >= 1 appends a ~n tag, shortening the BASE (never the extension)
 * so the result still fits 8.3 on the target:
 *
 *     n=0  LONGNAME.TXT  ->  LONGNAME.TXT
 *     n=1  LONGNAME.TXT  ->  LONGN~1.TXT
 *     n=1  A.TXT         ->  A~1.TXT
 *     n=1  NOEXT         ->  NOEXT~1
 *
 * The extension is preserved because it is what the shell opens the file
 * with: a restored NOTES~1.TX would no longer be a text file.
 *
 * The caller probes the bin and increments n until the name is free.
 */
void trash_candidate(char *out, cu32 cap, const char *name, int n);

/*
 * The index, as a text buffer the caller loads and saves whole. It is small
 * -- one short line per item in the bin -- so rewriting it entirely on each
 * change costs nothing and cannot leave a half-updated file behind.
 */

/* Record that 'stored' (the name inside the bin) came from 'orig' (its whole
 * original path). Any prior line for 'stored' is replaced. CFALSE if it will
 * not fit. */
cbool trash_idx_add(char *buf, cu32 cap, const char *stored, const char *orig);

/* Where 'stored' came from, as a whole path. CFALSE when the index does not
 * know it -- which is a normal answer: a file dragged into the bin by hand has
 * no entry, and that is exactly when the caller must ask the user where to put
 * it rather than guess. */
cbool trash_idx_find(const char *buf, const char *stored,
                     char *out, cu32 outcap);

/* Forget 'stored' (after a restore, or when the bin is emptied). */
void trash_idx_remove(char *buf, cu32 cap, const char *stored);

/* How many items the index knows about. */
int trash_idx_count(const char *buf);

/*
 * The index file's own name inside the bin.
 *
 * It lives in TRASH alongside the deleted items, so everything that asks "what
 * is in the bin" has to know not to count it: without this the desktop's bin
 * icon stays full forever after the last file is restored, "Empty Recycle Bin"
 * offers to delete one item from a bin you can see is empty, and Restore is
 * offered on the bookkeeping itself.
 */
#define TRASH_IDX_NAME "TRASH.IDX"
cbool trash_is_index(const char *name);

#endif /* CASTALIA_TRASH_CORE_H */
