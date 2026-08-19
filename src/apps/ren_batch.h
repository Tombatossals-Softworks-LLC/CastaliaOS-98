/*
 * ren_batch.h - Running a wildcard rename over a real folder.
 *
 * ren_core.c decides what a pattern MEANS; this decides what to do about a
 * disk. It is split out because two places need it -- the console's `ren` and
 * the File Manager's Rename Many -- and a batch rename is not something to
 * have two implementations of. The console and the window would eventually
 * disagree about which batches are safe, and only one of them would be right.
 * (diff_core has the same arrangement with `fc` and the File Compare window.)
 *
 * THE WHOLE BATCH IS PLANNED BEFORE ANYTHING MOVES. A rename that runs half a
 * folder and stops on a clash has already overwritten a file, and there is no
 * undo for that. So every new name is computed, checked against the others and
 * checked against what is already on the disk, and any ONE problem refuses the
 * batch entirely -- with a sentence naming the file that caused it, because a
 * refusal nobody can read is indistinguishable from nothing happening.
 */
#ifndef CASTALIA_REN_BATCH_H
#define CASTALIA_REN_BATCH_H

#include "castalia/ctypes.h"

/* Files one batch may touch. Past this it refuses rather than doing part. */
#define REN_BATCH_MAX 256

typedef struct {
    int  matched;    /* files the FROM pattern selected                     */
    int  renamed;    /* files actually renamed (0 unless it went ahead)     */
    char why[112];   /* empty on success; otherwise why nothing happened    */
} RenBatch;

/*
 * Rename every file in 'dir' matching 'from' according to 'to'.
 *
 * Returns CTRUE only when the batch ran. On CFALSE nothing on the disk was
 * touched and 'out->why' says which file made it impossible. Directories are
 * never renamed -- a wildcard batch is about files, and a folder caught by
 * "*" would be a surprise nobody typed.
 */
cbool ren_batch_run(const char *dir, const char *from, const char *to,
                    RenBatch *out);

/* Does 'p' carry a wildcard -- i.e. is this a batch rather than one file? */
cbool ren_batch_is_pattern(const char *p);

/*
 * Rename ONE file, refusing to overwrite an existing one.
 *
 * plat_file_rename goes straight to the C library, which overwrites without
 * complaint: `ren A B` with B already there destroyed B and reported success.
 * Comparing case-insensitively is what lets a pure change of case still work,
 * since on a filesystem that does not distinguish them the target "already
 * exists" and is the very file being renamed.
 */
cbool ren_batch_one(const char *dir, const char *from, const char *to,
                    char *why, cu32 whysz);

#endif /* CASTALIA_REN_BATCH_H */
