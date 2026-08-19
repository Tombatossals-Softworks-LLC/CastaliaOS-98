/*
 * ren_core.h - Renaming MANY files at once, the way REN always did it.
 *
 * `ren *.TXT *.BAK` is the first thing anyone types at a DOS prompt when they
 * want to rename a folder full of files. This console answered it by trying to
 * rename one file literally called "*.TXT", failing, and saying so -- which is
 * honest and useless. The File Manager's answer was F2, once per file. On a
 * disk of a few hundred photos that is the job a computer should be doing.
 *
 * The rules are MS-DOS's, because they are the ones people already know. The
 * name is split into base and extension and each is matched and rewritten
 * independently, which is why `*.TXT -> *.BAK` keeps the base and replaces the
 * extension without either pattern having to mention the other.
 *
 *   ren_match("NOTES.TXT", "*.TXT")            -> true
 *   ren_apply("NOTES.TXT", "*.TXT", "*.BAK")   -> "NOTES.BAK"
 *   ren_apply("REPORT1.DOC", "*", "OLD_*")     -> "OLD_REPO.DOC"  (8.3 clips)
 *
 * In a TO pattern, '*' copies the rest of that component from the source, '?'
 * copies one character from the same position, and anything else is a literal.
 * In a FROM pattern they are wildcards: '*' matches the rest, '?' matches one
 * character -- or none at the end of a component, so "A?.TXT" matches "A.TXT"
 * exactly as DOS does.
 *
 * Everything here is pure, so tests/test_ren.c drives the cases that go wrong
 * without touching a disk: patterns that overflow 8.3, patterns that produce
 * an empty name, and -- the one that matters -- two different files that come
 * out with the SAME new name.
 *
 * THE COLLISION IS THE POINT. A batch rename that renames half a folder and
 * then stops on a clash has already destroyed the file it overwrote and left
 * the folder in a state nobody asked for. So the caller is expected to build
 * the whole plan, call ren_first_collision() on it, and refuse the batch --
 * not perform it and report at the end.
 */
#ifndef CASTALIA_REN_CORE_H
#define CASTALIA_REN_CORE_H

#include "castalia/ctypes.h"

/* The DOS name shape these rules are written against. */
#define REN_BASE_MAX 8
#define REN_EXT_MAX  3
#define REN_NAME_MAX 13   /* 8 + '.' + 3 + NUL */

/*
 * Does 'name' match 'pat'? Case-insensitive, base and extension matched
 * separately. A pattern with no '.' matches against the base alone, so "*"
 * matches every file rather than only the ones without an extension.
 */
cbool ren_match(const char *name, const char *pat);

/*
 * The new name for 'name' under 'from' -> 'to'. Returns CFALSE and leaves
 * 'out' empty when the result would not be a name: empty, longer than 8.3, or
 * carrying a path separator. Refusing is deliberate -- a silently clipped
 * rename is how two files become one.
 *
 * 'from' is accepted for symmetry with the command line and is not consulted:
 * DOS applies the TO pattern positionally against the SOURCE NAME, not against
 * whatever the FROM pattern happened to capture. `ren A*.TXT B*.TXT` renames
 * ABC.TXT to BBC.TXT, not to BABC.TXT.
 */
cbool ren_apply(const char *name, const char *from, const char *to,
                char *out, cu32 cap);

/*
 * The first entry in 'names' that repeats an earlier one, case-insensitively,
 * or -1 when they are all distinct. 'names' is n entries of 'stride' bytes --
 * a flat array rather than an array of pointers, so a caller on DOS can keep
 * the whole plan in one allocation.
 */
int ren_first_collision(const char *names, int n, cu32 stride);

#endif /* CASTALIA_REN_CORE_H */
