/*
 * test_assoc.c - The file-association table (assoc.c).
 *
 * Getting this wrong is quiet and annoying: a double-click opens the wrong
 * app, or the Type column says one thing while Properties says another. The
 * checks below pin the extension matching, the case-insensitivity, the
 * fallback, and the awkward names that look like they have an extension and
 * do not.
 */
#include "ctest.h"
#include "assoc.h"

void test_assoc(void)
{
    printf("- associations\n");

    /* The common cases, in both cases. */
    CHECK_EQI((int)assoc_for("NOTES.TXT"), ASSOC_TEXT);
    CHECK_EQI((int)assoc_for("notes.txt"), ASSOC_TEXT);
    CHECK_EQI((int)assoc_for("PHOTO.BMP"), ASSOC_IMAGE);
    CHECK_EQI((int)assoc_for("Budget.csv"), ASSOC_SHEET);
    CHECK_EQI((int)assoc_for("LETTER.DOC"), ASSOC_WRITE);
    CHECK_EQI((int)assoc_for("SONG.WAV"), ASSOC_AUDIO);
    CHECK_EQI((int)assoc_for("CASTALIA.INI"), ASSOC_THEME);
    CHECK_EQI((int)assoc_for("EDIT.COM"), ASSOC_PROGRAM);
    CHECK_EQI((int)assoc_for("SETUP.EXE"), ASSOC_PROGRAM);
    CHECK_EQI((int)assoc_for("GAME.CAPP"), ASSOC_PACKAGE);
    CHECK_EQI((int)assoc_for("NOTES.CZ"), ASSOC_ARCHIVE);
    CHECK_STR(assoc_type_name("NOTES.CZ"), "Compressed File");

    /* A full path is judged by its name, not by the folders above it. */
    CHECK_EQI((int)assoc_for("C:\\CASTALIA\\DOCS\\LETTER.DOC"), ASSOC_WRITE);
    CHECK_EQI((int)assoc_for("/home/user/photo.bmp"), ASSOC_IMAGE);

    /* Nobody claims these, so the Viewer gets them -- it can always show
     * something, even if only a hex dump. */
    CHECK_EQI((int)assoc_for("README"), ASSOC_UNKNOWN);
    CHECK_EQI((int)assoc_for("ARCHIVE.ZIP"), ASSOC_UNKNOWN);
    CHECK_EQI((int)assoc_for(""), ASSOC_UNKNOWN);
    CHECK_EQI((int)assoc_for(NULL), ASSOC_UNKNOWN);

    /* Names that look like they have an extension and do not: a dot in a
     * directory above the file, and a name that simply ends in one. */
    CHECK_EQI((int)assoc_for("/etc/my.dir/README"), ASSOC_UNKNOWN);
    CHECK_EQI((int)assoc_for("NOTES."), ASSOC_UNKNOWN);
    CHECK_EQI((int)assoc_for(".TXT"), ASSOC_TEXT);      /* a dotfile, still text */

    /* The last extension wins, the way every shell has always done it. */
    CHECK_EQI((int)assoc_for("BACKUP.TXT.BMP"), ASSOC_IMAGE);

    /* An extension that only starts like a known one is not that one. */
    CHECK_EQI((int)assoc_for("THING.TXTX"), ASSOC_UNKNOWN);
    CHECK_EQI((int)assoc_for("THING.BM"), ASSOC_UNKNOWN);

    /* The names the UI prints. */
    CHECK_STR(assoc_type_name("PHOTO.BMP"), "Bitmap Image");
    CHECK_STR(assoc_type_name("notes.txt"), "Text Document");
    CHECK_STR(assoc_type_name("SONG.WAV"), "Sound");
    CHECK_STR(assoc_type_name("README"), "File");
    CHECK_STR(assoc_type_name(NULL), "File");

    /* ---- an extension the table does not know --------------------------- *
     *
     * This used to be `(a_ext(name) != NULL) ? "File" : "File"` -- both arms
     * the same, so every unknown extension read "File" and a Type column full
     * of them said nothing the file name did not already say. The shape of
     * the ternary is what showed the distinction was meant to be there;
     * -Wduplicated-branches is what found it.
     *
     * "XYZ File" is what the table itself already does for the rows it has
     * ("Log File", "Batch File"), so the unknown case now matches the known
     * ones instead of collapsing.
     */
    CHECK_STR(assoc_type_name("SETUP.XYZ"), "XYZ File");
    CHECK_STR(assoc_type_name("DATA.DAT"), "DAT File");
    /* Lower case is raised, the way an extension is written in this column. */
    CHECK_STR(assoc_type_name("archive.zip"), "ZIP File");
    CHECK_STR(assoc_type_name("Mixed.TaR"), "TAR File");
    /* A single-character extension still names itself. */
    CHECK_STR(assoc_type_name("PROGRAM.Q"), "Q File");
    /* A path, not just a name -- the extension is still the last one. */
    CHECK_STR(assoc_type_name("A:\\SUB\\FILE.QQQ"), "QQQ File");
    /* A dot with nothing after it is not an extension, so it is not a type. */
    CHECK_STR(assoc_type_name("TRAILING."), "File");
    /* A directory that contains a dot must not lend its extension to a
     * file that has none -- a_ext restarts at every separator. */
    CHECK_STR(assoc_type_name("A:\\V1.2\\README"), "File");
    /* An absurdly long extension is truncated rather than overrunning the
     * buffer it is built in. */
    {
        const char *t =
            assoc_type_name("X.ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
        /* Whatever it says, it says it inside the buffer and ends in File. */
        CHECK(test_strcmp(t, "ABCDEFGHIJKL File") == 0);
    }
    CHECK_STR(assoc_app_name(ASSOC_IMAGE), "Paint");
    CHECK_STR(assoc_app_name(ASSOC_TEXT), "Notepad");
    CHECK(assoc_app_name(ASSOC_UNKNOWN)[0] != '\0');
}
