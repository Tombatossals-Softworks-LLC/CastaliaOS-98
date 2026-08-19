/*
 * test_path.c - Path and listing helpers behind the file dialog (ui_path.c).
 *
 * These are the parts of "browse for a file" that go wrong quietly: a join
 * that doubles the separator, a parent that walks off the root, a filter that
 * lets ".BMPX" through, an ordering that buries the folders among the files.
 * All of it is pure, so all of it is checked here rather than by clicking.
 */
#include "ctest.h"
#include "castalia/ui.h"

static char g_buf[CASTALIA_MAX_PATH];

static void pj(const char *dir, const char *name)
{
    ui_path_join(g_buf, (cu32)sizeof g_buf, dir, name);
}

static void up_join(void)
{
    pj("C:\\CASTALIA", "DOCS");
    CHECK_STR(g_buf, "C:\\CASTALIA\\DOCS");     /* keeps the DOS separator  */
    pj("/home/user", "notes.txt");
    CHECK_STR(g_buf, "/home/user/notes.txt");
    /* A separator already there is not doubled. */
    pj("/home/user/", "notes.txt");
    CHECK_STR(g_buf, "/home/user/notes.txt");
    pj("C:\\", "AUTOEXEC.BAT");
    CHECK_STR(g_buf, "C:\\AUTOEXEC.BAT");
    /* An absolute name replaces the directory rather than being appended. */
    pj("/home/user", "/etc/motd");
    CHECK_STR(g_buf, "/etc/motd");
    pj("/home/user", "C:\\DOS\\EDIT.COM");
    CHECK_STR(g_buf, "C:\\DOS\\EDIT.COM");
    /* Empty pieces. */
    pj("", "file.txt");
    CHECK_STR(g_buf, "file.txt");
    pj("/dir", "");
    CHECK_STR(g_buf, "/dir/");
    pj(NULL, "x");
    CHECK_STR(g_buf, "x");
    pj("/dir", NULL);
    CHECK_STR(g_buf, "/dir/");
    ui_path_join(NULL, 10, "/a", "b");        /* no crash */

    /* "." is where you already are, so joining it adds nothing. Both browsers
     * fall back to "." when CASTALIA_HOME is unset, and without this every
     * path they show, store and open would carry a leading "./" that means
     * nothing -- and that the address bar would then have to climb out of. */
    pj(".", "NOTES.TXT");
    CHECK_STR(g_buf, "NOTES.TXT");
    pj(".", "DOCS");
    CHECK_STR(g_buf, "DOCS");
    /* "." with nothing to join is the empty name, not "." itself -- callers
     * test the result for emptiness to mean "nothing selected". */
    pj(".", "");
    CHECK_STR(g_buf, "");
    /* But ".." is a real directory and joins like one. */
    pj("..", "NOTES.TXT");
    CHECK_STR(g_buf, "../NOTES.TXT");
    /* And a name that merely starts with a dot is not "." either. */
    pj(".CONFIG", "A");
    CHECK_STR(g_buf, ".CONFIG/A");

    /* A join that does not fit truncates instead of running off the end. */
    {
        char small[8];
        ui_path_join(small, (cu32)sizeof small, "/verylongdirectory", "name");
        CHECK(small[7] == '\0');
    }
}

static void up_complete(void)
{
    /* A name the app may place in its own folder... */
    CHECK(!ui_path_is_complete("NOTES.TXT"));
    CHECK(!ui_path_is_complete("BUDGET.CSV"));
    /* ...versus one that already says where it goes, which must be left
     * alone -- joining it again is how a save ends up in a folder that does
     * not exist. */
    CHECK(ui_path_is_complete("/home/user/notes.txt"));
    CHECK(ui_path_is_complete("C:\\CASTALIA\\DOCS\\LETTER.DOC"));
    CHECK(ui_path_is_complete("./THEMES/DEMO.INI"));
    CHECK(ui_path_is_complete("DOCS\\LETTER.DOC"));
    CHECK(ui_path_is_complete("C:FILE.TXT"));
    CHECK(!ui_path_is_complete(""));
    CHECK(!ui_path_is_complete(NULL));
}

static void up_parent(void)
{
    sys_strlcpy(g_buf, "/home/user/docs", sizeof g_buf);
    CHECK(ui_path_up(g_buf));
    CHECK_STR(g_buf, "/home/user");
    CHECK(ui_path_up(g_buf));
    CHECK_STR(g_buf, "/home");
    CHECK(ui_path_up(g_buf));
    CHECK_STR(g_buf, "/");
    CHECK(!ui_path_up(g_buf));                /* the root is the end       */
    CHECK_STR(g_buf, "/");

    /* A trailing separator does not cost a level. */
    sys_strlcpy(g_buf, "/home/user/", sizeof g_buf);
    CHECK(ui_path_up(g_buf));
    CHECK_STR(g_buf, "/home");

    /* DOS drives stop at the drive root. */
    sys_strlcpy(g_buf, "C:\\CASTALIA\\DOCS", sizeof g_buf);
    CHECK(ui_path_up(g_buf));
    CHECK_STR(g_buf, "C:\\CASTALIA");
    CHECK(ui_path_up(g_buf));
    CHECK_STR(g_buf, "C:\\");
    CHECK(!ui_path_up(g_buf));
    sys_strlcpy(g_buf, "C:", sizeof g_buf);
    CHECK(!ui_path_up(g_buf));

    /* A bare name goes to the current directory, once. */
    sys_strlcpy(g_buf, "folder", sizeof g_buf);
    CHECK(ui_path_up(g_buf));
    CHECK_STR(g_buf, ".");
    CHECK(!ui_path_up(g_buf));

    g_buf[0] = '\0';
    CHECK(!ui_path_up(g_buf));
    CHECK(!ui_path_up(NULL));
}

/*
 * ui_path_parent_rel is NOT ui_path_up with a different signature, and the
 * difference is the whole reason both exist.
 *
 * ui_path_up serves the file dialog, which must not let a pick wander outside
 * the tree it was opened on: at "." it refuses and says so. The File Manager
 * and the console are shells -- typing "cd .." at "." is an ordinary thing to
 * do, and answering "you cannot" would be wrong. So this one climbs out into
 * "..", and keeps climbing.
 *
 * Both apps carried their own identical copy of this before; the copies are
 * gone, and the behaviour they had is pinned here.
 */
static void up_parent_rel(void)
{
    char b[CASTALIA_MAX_PATH];

    /* Absolute paths: strip one component, stopping at the root. */
    ui_path_parent_rel("/home/user/docs", b, (cu32)sizeof b);
    CHECK_STR(b, "/home/user");
    ui_path_parent_rel("/home", b, (cu32)sizeof b);
    CHECK_STR(b, "/");
    /* The root is its own parent -- climbing past it has nowhere to go. */
    ui_path_parent_rel("/", b, (cu32)sizeof b);
    CHECK_STR(b, "/");

    /* A trailing separator is not an extra level: "/a/b/" is "/a", not "/a/b". */
    ui_path_parent_rel("/home/user/", b, (cu32)sizeof b);
    CHECK_STR(b, "/home");

    /* DOS separators work the same way. */
    ui_path_parent_rel("C:\\CASTALIA\\DOCS", b, (cu32)sizeof b);
    CHECK_STR(b, "C:\\CASTALIA");
    /* Mixed separators are normal once a path has been joined on DOS. */
    ui_path_parent_rel("C:\\CASTALIA/DOCS", b, (cu32)sizeof b);
    CHECK_STR(b, "C:\\CASTALIA");

    /* The relative cases, which are where this parts company with
     * ui_path_up. "." climbs out; ui_path_up would have refused. */
    ui_path_parent_rel(".", b, (cu32)sizeof b);
    CHECK_STR(b, "..");
    /* Any other bare name sits in the current directory. */
    ui_path_parent_rel("DOCS", b, (cu32)sizeof b);
    CHECK_STR(b, ".");
    /* Including "..", which is deliberately NOT walked to "../..": the
     * shells re-derive from CASTALIA_HOME rather than tracking a depth, so
     * one level of climb is as far as this needs to be right. */
    ui_path_parent_rel("..", b, (cu32)sizeof b);
    CHECK_STR(b, ".");

    /* Degenerate input is answered, not crashed on. */
    ui_path_parent_rel("", b, (cu32)sizeof b);
    CHECK_STR(b, ".");
    ui_path_parent_rel(NULL, b, (cu32)sizeof b);
    CHECK_STR(b, ".");
    ui_path_parent_rel("/a/b", NULL, 16);       /* no crash */
    ui_path_parent_rel("/a/b", b, 0);           /* no crash, no write */

    /* A parent that does not fit truncates instead of running off the end. */
    {
        char small[8];
        ui_path_parent_rel("/averylongdirectory/name", small,
                           (cu32)sizeof small);
        CHECK(small[7] == '\0');
    }
}

/*
 * ui_path_default_dir: where a Save goes when the user only gave a name.
 *
 * The Spreadsheet, the Writer and the Theme Editor each had their own copy of
 * this, differing only in the folder. The direction is what these checks are
 * really about: a name the user picked from somewhere else has to be written
 * back THERE. Putting it in the app's folder instead would leave the file the
 * user opened untouched, and their edit would look lost.
 */
static void up_default_dir(void)
{
    char b[CASTALIA_MAX_PATH];

    /* A bare name is placed in the default folder. */
    ui_path_default_dir(b, (cu32)sizeof b, "/home/user/DOCS", "LETTER.DOC");
    CHECK_STR(b, "/home/user/DOCS/LETTER.DOC");
    ui_path_default_dir(b, (cu32)sizeof b, "C:\\CASTALIA\\THEMES", "BLUE.THM");
    CHECK_STR(b, "C:\\CASTALIA\\THEMES\\BLUE.THM");

    /* An absolute name is left exactly alone. */
    ui_path_default_dir(b, (cu32)sizeof b, "/home/user/DOCS", "/tmp/OTHER.DOC");
    CHECK_STR(b, "/tmp/OTHER.DOC");
    /* So is a drive-qualified one. */
    ui_path_default_dir(b, (cu32)sizeof b, "/home/user/DOCS", "A:\\DISK.DOC");
    CHECK_STR(b, "A:\\DISK.DOC");
    /* And so is one that merely carries a folder -- this is the case a plain
     * "is it absolute" test gets wrong, and the one the file dialog produces
     * when somebody browses into a subfolder. */
    ui_path_default_dir(b, (cu32)sizeof b, "/home/user/DOCS", "SUB/OTHER.DOC");
    CHECK_STR(b, "SUB/OTHER.DOC");
    ui_path_default_dir(b, (cu32)sizeof b, "/home/user/DOCS", "SUB\\OTHER.DOC");
    CHECK_STR(b, "SUB\\OTHER.DOC");

    /* Degenerate input is answered, not crashed on. */
    ui_path_default_dir(b, (cu32)sizeof b, "/home/user/DOCS", NULL);
    CHECK_STR(b, "/home/user/DOCS/");
    ui_path_default_dir(b, (cu32)sizeof b, "/home/user/DOCS", "");
    CHECK_STR(b, "/home/user/DOCS/");
    ui_path_default_dir(NULL, 16, "/d", "x");     /* no crash */
    ui_path_default_dir(b, 0, "/d", "x");         /* no crash, no write */
}

static void up_filter(void)
{
    CHECK(ui_path_match_ext("PICTURE.BMP", "BMP"));
    CHECK(ui_path_match_ext("picture.bmp", "BMP"));      /* case-insensitive */
    CHECK(ui_path_match_ext("PICTURE.bmp", "bmp"));
    CHECK(ui_path_match_ext("a.b.c.BMP", "BMP"));        /* the LAST dot     */
    CHECK(!ui_path_match_ext("PICTURE.BMPX", "BMP"));    /* not a prefix     */
    CHECK(!ui_path_match_ext("PICTURE.BM", "BMP"));
    CHECK(!ui_path_match_ext("BMP", "BMP"));             /* no extension     */
    CHECK(!ui_path_match_ext("PICTURE.TXT", "BMP"));
    CHECK(!ui_path_match_ext("", "BMP"));
    CHECK(!ui_path_match_ext(NULL, "BMP"));
    /* No filter means everything, including names with no dot at all. */
    CHECK(ui_path_match_ext("README", NULL));
    CHECK(ui_path_match_ext("README", ""));
    CHECK(ui_path_match_ext("a.txt", ""));
}

static void up_order(void)
{
    /* The way up always leads. */
    CHECK(ui_path_compare("..", CTRUE, "AAA", CTRUE) < 0);
    CHECK(ui_path_compare("AAA", CTRUE, "..", CTRUE) > 0);
    CHECK_EQI(ui_path_compare("..", CTRUE, "..", CTRUE), 0);
    /* Then folders, whatever their names. */
    CHECK(ui_path_compare("zzz", CTRUE, "aaa", CFALSE) < 0);
    CHECK(ui_path_compare("aaa", CFALSE, "zzz", CTRUE) > 0);
    /* Within a kind: alphabetical, ignoring case. */
    CHECK(ui_path_compare("apple", CFALSE, "banana", CFALSE) < 0);
    CHECK(ui_path_compare("Apple", CFALSE, "apple", CFALSE) == 0);
    CHECK(ui_path_compare("apple", CFALSE, "APPLE2", CFALSE) < 0);
    CHECK_EQI(ui_path_compare(NULL, CFALSE, "x", CFALSE), 0);

    /* Sorting a small listing with it puts everything where a reader looks. */
    {
        static const char *nm[6] = { "photo.bmp", "DOCS", "..", "SYS",
                                     "a.txt", "Zebra.txt" };
        static const cbool dir[6] = { CFALSE, CTRUE, CTRUE, CTRUE,
                                      CFALSE, CFALSE };
        int order[6], i, j;
        for (i = 0; i < 6; i++) { order[i] = i; }
        for (i = 1; i < 6; i++) {           /* insertion sort, same as the UI */
            int k = order[i];
            j = i - 1;
            while (j >= 0 && ui_path_compare(nm[order[j]], dir[order[j]],
                                             nm[k], dir[k]) > 0) {
                order[j + 1] = order[j];
                j--;
            }
            order[j + 1] = k;
        }
        CHECK_STR(nm[order[0]], "..");
        CHECK_STR(nm[order[1]], "DOCS");
        CHECK_STR(nm[order[2]], "SYS");
        CHECK_STR(nm[order[3]], "a.txt");
        CHECK_STR(nm[order[4]], "photo.bmp");
        CHECK_STR(nm[order[5]], "Zebra.txt");
    }
}

void test_path(void)
{
    printf("- ui path\n");
    up_join();
    up_complete();
    up_parent();
    up_parent_rel();
    up_default_dir();
    up_filter();
    up_order();

    /* ---- basename ------------------------------------------------------
     *
     * This existed four times under four names, plus a fifth inline in the
     * Hex Viewer. One copy now, so the edges are worth pinning once.
     */
    {
        CHECK_STR(ui_path_base("C:/CASTALIA/NOTES.TXT"), "NOTES.TXT");
        CHECK_STR(ui_path_base("C:\\CASTALIA\\NOTES.TXT"), "NOTES.TXT");
        /* Mixed separators are normal on DOS once a path has been joined. */
        CHECK_STR(ui_path_base("C:/CASTALIA\\SUB/FILE.BMP"), "FILE.BMP");
        /* No separator at all: the whole thing is the name. */
        CHECK_STR(ui_path_base("NOTES.TXT"), "NOTES.TXT");
        /* A trailing separator names nothing, and must not walk back to the
         * directory above -- callers use this to fill a title bar, and
         * "SUB" where the user asked for a file would be a quiet lie. */
        CHECK_STR(ui_path_base("C:/CASTALIA/SUB/"), "");
        CHECK_STR(ui_path_base("/"), "");
        CHECK_STR(ui_path_base(""), "");
        /* Deliberately NOT drive-aware: every copy this replaced behaved
         * this way, and changing behaviour during a de-duplication is how a
         * tidy-up becomes a bug. Recorded so the next reader knows it is a
         * decision rather than an oversight. */
        CHECK_STR(ui_path_base("C:FILE.TXT"), "C:FILE.TXT");
        /* A NULL is a failed lookup upstream, not a crash here. */
        CHECK_STR(ui_path_base(NULL), "");
    }
}
