/*
 * assoc.h - File associations: which app opens what, and what to call it.
 *
 * One table, consulted by the File Manager (double-click, the Properties
 * dialog's "Type" line and the Details column) so those three can never
 * disagree about what a file is. Pure string work, host-tested in
 * tests/test_assoc.c -- no window, no filesystem.
 *
 * A file with no extension, or one nobody claims, is a document the Viewer
 * can always show: it falls back to a hex dump rather than refusing.
 */
#ifndef CASTALIA_ASSOC_H
#define CASTALIA_ASSOC_H

#include "castalia/ctypes.h"

typedef enum {
    ASSOC_UNKNOWN = 0,   /* the Viewer (image if it can, else hex)         */
    ASSOC_TEXT,          /* Notepad                                        */
    ASSOC_IMAGE,         /* Paint                                          */
    ASSOC_SHEET,         /* CastaliaSheet                                  */
    ASSOC_WRITE,         /* CastaliaWrite                                  */
    ASSOC_AUDIO,         /* Media Player                                   */
    ASSOC_THEME,         /* Theme Editor                                   */
    ASSOC_PROGRAM,       /* a DOS executable: run it                       */
    ASSOC_PACKAGE,       /* a .CAPP add-on                                 */
    ASSOC_ARCHIVE        /* a .CZ compressed file: the File Manager expands */
} AssocKind;

/* What opens 'name' (a file name or a full path; only the extension counts). */
AssocKind assoc_for(const char *name);

/*
 * "Bitmap Image", "Text Document", ... -- the File Manager's Type column and
 * the Properties dialog both print this, so they always agree.
 *
 * An extension the table does not know gives "LOG File", "XYZ File"; a name
 * with no extension at all gives "File". That unknown-extension answer lives
 * in a single static buffer, so it is valid until the next call -- every
 * caller draws or copies it immediately, which is the only use this needs to
 * support. The known-extension answers are string literals and always valid.
 */
const char *assoc_type_name(const char *name);

/* "Paint", "Notepad", ... for the status line ("Opened X in Paint"). */
const char *assoc_app_name(AssocKind kind);

#endif /* CASTALIA_ASSOC_H */
