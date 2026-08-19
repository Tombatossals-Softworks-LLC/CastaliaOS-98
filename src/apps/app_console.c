/*
 * app_console.c - CastaliaOS Console (a small command shell in a window).
 *
 * A DOS-flavored terminal: a scrollback buffer, a prompt showing the current
 * directory, a blinking cursor, command history (Up/Down), and a set of real
 * built-in commands that touch the actual filesystem through the platform API
 * (dir / cd / type / del ... ) plus system info (ver / mem / date / time). It is
 * deliberately self-contained -- no external process launch -- so it behaves the
 * same on the host and on DOS.
 */
#include "apps.h"
#include "diff_core.h"
#include "ren_batch.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"
#include "castalia/castalia.h"

#include <string.h>
#include "ring_core.h"
#include <stdlib.h>

#define CON_LINES 240          /* scrollback ring capacity                 */
#define CON_COLS  100          /* max chars per stored line                */
#define CON_HIST  16           /* command history depth                    */
#define CON_ROW_H 10

typedef struct {
    char  lines[CON_LINES][CON_COLS];
    Ring  scroll;              /* which of those slots hold output          */
    int   view;                /* lines scrolled up from the bottom        */

    char  cwd[CASTALIA_MAX_PATH];
    char  input[CON_COLS];
    int   input_len;

    char  hist[CON_HIST][CON_COLS];
    Ring  hring;               /* which history slots hold a command       */
    int   hist_pos;            /* -1 = editing a fresh line                */

    int   blink;
    CRect cur_rect;   /* where the caret was last drawn (screen coords) */
} Console;

/* ---- output ---------------------------------------------------------- */
static void con_add(Console *c, const char *text)
{
    sys_strlcpy(c->lines[ring_push(&c->scroll)], (text ? text : ""), CON_COLS);
    c->view = 0;   /* any new output jumps back to the bottom */
}

static void con_addf(Console *c, const char *fmt, const char *a)
{
    char buf[CON_COLS];
    sys_snprintf(buf, sizeof(buf), fmt, a);
    con_add(c, buf);
}

/* Get displayed line 'i', 0 == oldest still held. The wrapping lives in
 * ring_core.c now; this used to be
 * `(line_head - line_count + i + CON_LINES * 2) % CON_LINES`, which is
 * correct and was one of four separate correct spellings in this tree. */
static const char *con_line(const Console *c, int i)
{
    int at = ring_at(&c->scroll, i);
    return (at >= 0) ? c->lines[at] : "";
}

static int con_count(const Console *c) { return ring_count(&c->scroll); }

/* Paths are joined and climbed by ui_path_join / ui_path_parent_rel in
 * ui_path.c -- the same two the File Manager uses, so CD here and a
 * double-click there cannot drift apart. */

/* DOS-style prompt: uppercase, backslashes, e.g. "C:\CASTALIA>". */
static void con_prompt(const Console *c, char *dst, cu32 dstsz)
{
    int i;
    char p[CASTALIA_MAX_PATH];
    sys_snprintf(p, sizeof(p), "C:%s>", c->cwd);
    for (i = 0; p[i] != '\0'; i++) {
        if (p[i] == '/') { p[i] = '\\'; }
        else if (p[i] >= 'a' && p[i] <= 'z') { p[i] = (char)(p[i] - 'a' + 'A'); }
    }
    sys_strlcpy(dst, p, dstsz);
}

/* ---- commands -------------------------------------------------------- */
/*
 * A whole file into 'buf', resolved against the console's own directory.
 * Returns bytes read, or -1 having said why. A file too large is REFUSED
 * rather than truncated: comparing the first 32K of two files and reporting
 * them identical is the one answer this command must never give.
 */
static long con_read_file(Console *c, const char *name, char *buf, cu32 cap)
{
    char path[CASTALIA_MAX_PATH];
    PlatFile *f;
    long size;
    cu32 got;
    ui_path_join(path, sizeof(path), c->cwd, name);
    size = plat_file_size(path);
    if (size < 0) { con_addf(c, "Cannot open '%s'.", name); return -1; }
    if ((cu32)size > cap) {
        char msg[CON_COLS];
        sys_snprintf(msg, sizeof(msg),
                     "'%s' is %ldK -- fc reads at most %luK per file.",
                     name, size / 1024L, (unsigned long)(cap / 1024u));
        con_add(c, msg);
        return -1;
    }
    f = plat_fopen(path, "rb");
    if (f == NULL) { con_addf(c, "Cannot open '%s'.", name); return -1; }
    got = plat_fread(f, buf, cap);
    plat_fclose(f);
    return (long)got;
}

/*
 * fc FILE1 FILE2 -- what changed between two text files, the way DOS's own FC
 * answered it. The comparison is diff_core.c, the same one the File Compare
 * window uses, so the console and the window can never disagree about what
 * "different" means.
 */
static void cmd_fc(Console *c, const char *arg)
{
    static char buf_a[32768];
    static char buf_b[32768];
    static DiffResult res;
    char p1[CASTALIA_MAX_PATH], p2[CASTALIA_MAX_PATH];
    char line[CON_COLS];
    long n1, n2;
    int i = 0, k = 0, h;

    /* Two names, split on the first run of spaces. A quoted path is out of
     * scope here for the same reason it is everywhere else in this console:
     * DOS names have no spaces in them. */
    while (arg[i] == ' ') { i++; }
    while (arg[i] != '\0' && arg[i] != ' ' && k + 1 < (int)sizeof(p1)) {
        p1[k++] = arg[i++];
    }
    p1[k] = '\0';
    while (arg[i] == ' ') { i++; }
    k = 0;
    while (arg[i] != '\0' && arg[i] != ' ' && k + 1 < (int)sizeof(p2)) {
        p2[k++] = arg[i++];
    }
    p2[k] = '\0';
    if (p1[0] == '\0' || p2[0] == '\0') {
        con_add(c, "Usage: fc FILE1 FILE2");
        return;
    }

    n1 = con_read_file(c, p1, buf_a, (cu32)sizeof(buf_a));
    if (n1 < 0) { return; }
    n2 = con_read_file(c, p2, buf_b, (cu32)sizeof(buf_b));
    if (n2 < 0) { return; }

    diff_compare(buf_a, (cu32)n1, buf_b, (cu32)n2, &res);
    diff_summary(&res, line, sizeof(line));
    con_add(c, line);
    for (h = 0; h < res.count; h++) {
        const DiffHunk *d = &res.hunk[h];
        char text[CON_COLS];
        int j;
        sys_snprintf(line, sizeof(line), "  line %d:", d->a_start);
        con_add(c, line);
        for (j = 0; j < d->a_count; j++) {
            if (diff_line(buf_a, (cu32)n1, d->a_start + j, text, sizeof(text))) {
                sys_snprintf(line, sizeof(line), "  - %s", text);
                con_add(c, line);
            }
        }
        for (j = 0; j < d->b_count; j++) {
            if (diff_line(buf_b, (cu32)n2, d->b_start + j, text, sizeof(text))) {
                sys_snprintf(line, sizeof(line), "  + %s", text);
                con_add(c, line);
            }
        }
    }
}

static void cmd_help(Console *c)
{
    con_add(c, "Built-in commands:");
    con_add(c, "  help          this list          ver     version + build");
    con_add(c, "  dir [path]    list a directory   cd DIR  change directory");
    con_add(c, "  tree [path]   the folder tree    pwd     where you are");
    con_add(c, "  type FILE     print a text file  cls     clear the screen");
    con_add(c, "  echo TEXT     print text         mem     memory usage");
    con_add(c, "  date / time   wall clock         about   product info");
    con_add(c, "  del FILE      delete a file      mkdir DIR   make a folder");
    con_add(c, "  copy A B      copy a file        ren A B     rename A to B");
    con_add(c, "                ...ren takes wildcards: ren *.TXT *.BAK");
    con_add(c, "  fc A B        what changed between two text files");
    con_add(c, "  exit          close the console");
}

/* Split 'arg' into two space-separated tokens (either may be empty). */
static void split2(const char *arg, char *a, cu32 asz, char *b, cu32 bsz)
{
    int i = 0, k = 0;
    while (arg[i] == ' ') { i++; }
    while (arg[i] != '\0' && arg[i] != ' ' && k < (int)asz - 1) { a[k++] = arg[i++]; }
    a[k] = '\0';
    while (arg[i] == ' ') { i++; }
    k = 0;
    while (arg[i] != '\0' && k < (int)bsz - 1) { b[k++] = arg[i++]; }
    b[k] = '\0';
}

static void cmd_mkdir(Console *c, const char *arg)
{
    char path[CASTALIA_MAX_PATH];
    CResult r;
    if (arg == NULL || arg[0] == '\0') { con_add(c, "Usage: mkdir DIR"); return; }
    ui_path_join(path, sizeof(path), c->cwd, arg);
    r = plat_mkdir(path);
    if (r == CE_OK)        { con_addf(c, "Created folder '%s'.", arg); }
    else if (r == CE_BUSY) { con_addf(c, "'%s' already exists.", arg); }
    else                   { con_addf(c, "Could not create '%s'.", arg); }
}

static void cmd_copy(Console *c, const char *arg)
{
    char a[CON_COLS], b[CON_COLS], src[CASTALIA_MAX_PATH], dst[CASTALIA_MAX_PATH];
    PlatFile *in, *out;
    char buf[2048];
    cu32 n;
    cbool ok = CTRUE;
    split2(arg, a, sizeof(a), b, sizeof(b));
    if (a[0] == '\0' || b[0] == '\0') { con_add(c, "Usage: copy SRC DST"); return; }
    ui_path_join(src, sizeof(src), c->cwd, a);
    ui_path_join(dst, sizeof(dst), c->cwd, b);
    /*
     * Copying ONTO an existing file destroyed it and said "1 file copied":
     * plat_fopen(dst, "wb") truncates whatever is there. The same hole `ren`
     * had, in the command next to it -- found by asking where else the shape
     * lives rather than by hitting it.
     *
     * Refused rather than prompted, matching `ren`: a console with no modal
     * confirm should say no and say why, not guess.
     */
    if (sys_stricmp(a, b) != 0 && plat_file_size(dst) >= 0) {
        char msg[CON_COLS];
        sys_snprintf(msg, sizeof(msg),
                     "'%s' already exists. Delete it first, or copy to "
                     "another name.", b);
        con_add(c, msg);
        return;
    }
    in = plat_fopen(src, "rb");
    if (in == NULL) { con_addf(c, "Cannot open '%s'.", a); return; }
    out = plat_fopen(dst, "wb");
    if (out == NULL) { plat_fclose(in); con_addf(c, "Cannot write '%s'.", b); return; }
    while ((n = plat_fread(in, buf, (cu32)sizeof(buf))) > 0) {
        if (plat_fwrite(out, buf, n) != n) { ok = CFALSE; break; }
    }
    plat_fclose(in); plat_fclose(out);
    con_addf(c, ok ? "1 file copied to '%s'." : "Copy of '%s' failed.", b);
}

/*
 * ren OLD NEW -- one file, or a whole folder when either side has a wildcard.
 *
 * `ren *.TXT *.BAK` is the first thing anyone types here, and it used to try
 * to rename a single file literally called "*.TXT". The rules and the safety
 * are ren_batch.c, which the File Manager's Rename Many also uses, so the
 * console and the window can never disagree about which batches are safe.
 */
static void cmd_ren(Console *c, const char *arg)
{
    char a[CON_COLS], b[CON_COLS], why[112];
    split2(arg, a, sizeof(a), b, sizeof(b));
    if (a[0] == '\0' || b[0] == '\0') { con_add(c, "Usage: ren OLD NEW"); return; }

    if (!ren_batch_is_pattern(a) && !ren_batch_is_pattern(b)) {
        if (ren_batch_one(c->cwd, a, b, why, sizeof why)) {
            con_addf(c, "Renamed to '%s'.", b);
        } else {
            con_add(c, why);
        }
        return;
    }
    {
        RenBatch r;
        char msg[CON_COLS];
        if (!ren_batch_run(c->cwd, a, b, &r)) { con_add(c, r.why); return; }
        sys_snprintf(msg, sizeof(msg), "%d file(s) renamed.", r.renamed);
        con_add(c, msg);
    }
}

static void cmd_ver(Console *c)
{
    char b[CON_COLS];
    sys_snprintf(b, sizeof(b), "%s  %s", CASTALIA_NAME, CASTALIA_EDITION);
    con_add(c, b);
    sys_snprintf(b, sizeof(b), "Version %s (%s)  -  built %s",
                 CASTALIA_VER_STRING, CASTALIA_VER_STAGE, __DATE__);
    con_add(c, b);
}

/*
 * tree - the directory below here, drawn.
 *
 * DOS TREE with /A: ASCII elbows, because the font this system ships is
 * 32..126 and a box-drawing character would come out as the missing-glyph
 * box. The prefix is carried down as a string so a deep child still lines up
 * under its parent's rail.
 *
 * BOUNDED on two axes, and both bounds are load-bearing rather than tidiness:
 *
 *   - DEPTH, because a directory tree on a real disk can be deeper than this
 *     console's scrollback and the useful part is the top of it;
 *   - LINES, because the walk is recursive and a pathological tree would
 *     otherwise push every other line out of a ring the user is reading.
 *
 * Truncation is ANNOUNCED. A tree that silently stops looks exactly like a
 * tree that ended, which would have the reader believe a folder is empty.
 */
#define CON_TREE_DEPTH 6
#define CON_TREE_LINES 200

static void tree_walk(Console *c, const char *dir, const char *prefix,
                      int depth, int *lines, int *cut)
{
    PlatDir *d;
    PlatDirEntry e;
    char names[64][CASTALIA_MAX_NAME];
    unsigned char isdir[64];
    int n = 0, i;

    if (depth > CON_TREE_DEPTH) { *cut = 1; return; }
    d = plat_opendir(dir);
    if (d == NULL) { return; }
    /* Collected first: the elbow of the LAST entry differs, and that cannot
     * be known while still reading. */
    while (plat_readdir(d, &e) && n < 64) {
        if (strcmp(e.name, ".") == 0 || strcmp(e.name, "..") == 0) { continue; }
        sys_strlcpy(names[n], e.name, sizeof(names[n]));
        isdir[n] = e.is_dir ? 1u : 0u;
        n++;
    }
    plat_closedir(d);

    for (i = 0; i < n; i++) {
        char row[CON_COLS];
        char child_prefix[CON_COLS];
        cbool last = (i == n - 1) ? CTRUE : CFALSE;
        if (*lines >= CON_TREE_LINES) { *cut = 1; return; }
        {
            char nm[CASTALIA_MAX_NAME + 2];
            sys_snprintf(nm, sizeof(nm), "%s%s", names[i],
                         isdir[i] ? "/" : "");
            sys_snprintf(row, sizeof(row), "%s%s%s", prefix,
                         last ? "\\---" : "+---", nm);
        }
        con_add(c, row);
        (*lines)++;
        if (isdir[i]) {
            char child[CASTALIA_MAX_PATH];
            ui_path_join(child, sizeof(child), dir, names[i]);
            sys_snprintf(child_prefix, sizeof(child_prefix), "%s%s", prefix,
                         last ? "    " : "|   ");
            tree_walk(c, child, child_prefix, depth + 1, lines, cut);
        }
    }
}

static void cmd_tree(Console *c, const char *arg)
{
    char path[CASTALIA_MAX_PATH];
    int lines = 0, cut = 0;
    if (arg != NULL && arg[0] != '\0') {
        ui_path_join(path, sizeof(path), c->cwd, arg);
    } else {
        sys_strlcpy(path, c->cwd, sizeof(path));
    }
    {
        PlatDir *probe = plat_opendir(path);
        if (probe == NULL) {
            con_addf(c, "Cannot open '%s'.", arg && arg[0] ? arg : path);
            return;
        }
        plat_closedir(probe);
    }
    con_addf(c, " Folder tree of %s", path);
    tree_walk(c, path, "", 1, &lines, &cut);
    if (cut) {
        char msg[CON_COLS];
        sys_snprintf(msg, sizeof(msg),
                     "... stopped at %d lines / %d levels -- there is more.",
                     CON_TREE_LINES, CON_TREE_DEPTH);
        con_add(c, msg);
    }
    {
        char msg[CON_COLS];
        sys_snprintf(msg, sizeof(msg), " %d entr%s shown.", lines,
                     (lines == 1) ? "y" : "ies");
        con_add(c, msg);
    }
}

static void cmd_dir(Console *c, const char *arg)
{
    char path[CASTALIA_MAX_PATH];
    PlatDir *d;
    PlatDirEntry e;
    int files = 0, dirs = 0;
    long bytes = 0;
    if (arg != NULL && arg[0] != '\0') { ui_path_join(path, sizeof(path), c->cwd, arg); }
    else { sys_strlcpy(path, c->cwd, sizeof(path)); }
    d = plat_opendir(path);
    if (d == NULL) { con_addf(c, "Cannot open '%s'.", arg && arg[0] ? arg : path); return; }
    con_addf(c, " Directory of %s", path);
    con_add(c, "");
    while (plat_readdir(d, &e)) {
        char row[CON_COLS];
        if (strcmp(e.name, ".") == 0) { continue; }
        if (e.is_dir) {
            sys_snprintf(row, sizeof(row), "  %-40s <DIR>", e.name);
            dirs++;
        } else {
            char num[16];
            sys_snprintf(num, sizeof(num), "%ld", e.size >= 0 ? e.size : 0L);
            sys_snprintf(row, sizeof(row), "  %-40s %12s", e.name, num);
            files++;
            if (e.size > 0) { bytes += e.size; }
        }
        con_add(c, row);
    }
    plat_closedir(d);
    {
        char tot[CON_COLS];
        sys_snprintf(tot, sizeof(tot), " %d file(s), %ld bytes   %d dir(s)",
                     files, bytes, dirs);
        con_add(c, "");
        con_add(c, tot);
    }
}

static void cmd_cd(Console *c, const char *arg)
{
    char np[CASTALIA_MAX_PATH];
    PlatDir *d;
    if (arg == NULL || arg[0] == '\0') { con_add(c, c->cwd); return; }
    /* An absolute argument needs no special case here: ui_path_join lets a
     * complete name replace the directory, and knows about "C:\..." as well
     * as "/...", which the branch that used to sit here did not. */
    if (strcmp(arg, "..") == 0) { ui_path_parent_rel(c->cwd, np, sizeof(np)); }
    else { ui_path_join(np, sizeof(np), c->cwd, arg); }
    d = plat_opendir(np);
    if (d == NULL) { con_addf(c, "The system cannot find '%s'.", arg); return; }
    plat_closedir(d);
    sys_strlcpy(c->cwd, np, sizeof(c->cwd));
}

static void cmd_type(Console *c, const char *arg)
{
    char path[CASTALIA_MAX_PATH];
    PlatFile *f;
    char buf[512];
    char line[CON_COLS];
    int li = 0, printed = 0;
    cu32 n;
    if (arg == NULL || arg[0] == '\0') { con_add(c, "Usage: type FILE"); return; }
    ui_path_join(path, sizeof(path), c->cwd, arg);
    f = plat_fopen(path, "rb");
    if (f == NULL) { con_addf(c, "Cannot open '%s'.", arg); return; }
    while ((n = plat_fread(f, buf, (cu32)sizeof(buf))) > 0 && printed < 400) {
        cu32 i;
        for (i = 0; i < n; i++) {
            char ch = buf[i];
            if (ch == '\n' || li >= CON_COLS - 1) {
                line[li] = '\0'; con_add(c, line); li = 0; printed++;
                if (printed >= 400) { break; }
                if (ch != '\n') { line[li++] = ch; }
            } else if (ch != '\r') {
                line[li++] = (ch >= 32 || ch == '\t') ? (ch == '\t' ? ' ' : ch) : '.';
            }
        }
    }
    if (li > 0) { line[li] = '\0'; con_add(c, line); }
    plat_fclose(f);
    if (printed >= 400) { con_add(c, "-- output truncated --"); }
}

static void cmd_del(Console *c, const char *arg)
{
    char path[CASTALIA_MAX_PATH];
    if (arg == NULL || arg[0] == '\0') { con_add(c, "Usage: del FILE"); return; }
    ui_path_join(path, sizeof(path), c->cwd, arg);
    if (plat_file_remove(path) == CE_OK) { con_addf(c, "Deleted '%s'.", arg); }
    else { con_addf(c, "Could not delete '%s'.", arg); }
}

static void cmd_mem(Console *c)
{
    char b[CON_COLS];
    sys_snprintf(b, sizeof(b), "Live   : %lu KB",
                 (unsigned long)(sys_mem_live_bytes() / 1024));
    con_add(c, b);
    sys_snprintf(b, sizeof(b), "Peak   : %lu KB",
                 (unsigned long)(sys_mem_peak_bytes() / 1024));
    con_add(c, b);
    sys_snprintf(b, sizeof(b), "Blocks : %lu live allocation(s)",
                 (unsigned long)sys_mem_alloc_count());
    con_add(c, b);
}

static void cmd_datetime(Console *c, cbool date)
{
    char b[CON_COLS];
    if (date) {
        int y = 0, mo = 0, dd = 0, wd = 0;
        static const char *W[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
        plat_wall_date(&y, &mo, &dd, &wd);
        sys_snprintf(b, sizeof(b), "Current date: %s %04d-%02d-%02d",
                     (wd >= 0 && wd < 7) ? W[wd] : "?", y, mo, dd);
    } else {
        int h = 0, m = 0, s = 0;
        plat_wall_clock(&h, &m, &s);
        sys_snprintf(b, sizeof(b), "Current time: %02d:%02d:%02d", h, m, s);
    }
    con_add(c, b);
}

/* Execute one entered command line. Returns CTRUE to keep the window open. */
static cbool con_exec(WmWindow *win, Console *c, const char *raw)
{
    char cmd[CON_COLS];
    const char *arg;
    int i = 0;
    while (raw[i] == ' ') { i++; }
    { int k = 0; while (raw[i] != '\0' && raw[i] != ' ' && k < CON_COLS - 1) { cmd[k++] = raw[i++]; } cmd[k] = '\0'; }
    while (raw[i] == ' ') { i++; }
    arg = raw + i;

    if (cmd[0] == '\0') { return CTRUE; }
    if (sys_stricmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) { cmd_help(c); }
    else if (sys_stricmp(cmd, "ver") == 0) { cmd_ver(c); }
    else if (sys_stricmp(cmd, "cls") == 0 || sys_stricmp(cmd, "clear") == 0) {
            /* cls clears the SCREEN. It does not clear the command
             * history -- that is what a shell does, and briefly this
             * cleared both, because the history ring's init was pasted
             * in here by a careless match instead of into the
             * constructor where it belonged. */
            ring_init(&c->scroll, CON_LINES); c->view = 0;
    }
    else if (sys_stricmp(cmd, "dir") == 0 || sys_stricmp(cmd, "ls") == 0) { cmd_dir(c, arg); }
    else if (sys_stricmp(cmd, "tree") == 0) { cmd_tree(c, arg); }
    else if (sys_stricmp(cmd, "fc") == 0 ||
             sys_stricmp(cmd, "diff") == 0) { cmd_fc(c, arg); }
    else if (sys_stricmp(cmd, "cd") == 0 || sys_stricmp(cmd, "chdir") == 0) { cmd_cd(c, arg); }
    else if (sys_stricmp(cmd, "type") == 0 || sys_stricmp(cmd, "cat") == 0) { cmd_type(c, arg); }
    else if (sys_stricmp(cmd, "del") == 0 || sys_stricmp(cmd, "rm") == 0) { cmd_del(c, arg); }
    else if (sys_stricmp(cmd, "mkdir") == 0 || sys_stricmp(cmd, "md") == 0) { cmd_mkdir(c, arg); }
    else if (sys_stricmp(cmd, "copy") == 0 || sys_stricmp(cmd, "cp") == 0) { cmd_copy(c, arg); }
    else if (sys_stricmp(cmd, "ren") == 0 || sys_stricmp(cmd, "rename") == 0 ||
             sys_stricmp(cmd, "move") == 0 || sys_stricmp(cmd, "mv") == 0) { cmd_ren(c, arg); }
    else if (sys_stricmp(cmd, "echo") == 0) { con_add(c, arg); }
    else if (sys_stricmp(cmd, "mem") == 0) { cmd_mem(c); }
    else if (sys_stricmp(cmd, "date") == 0) { cmd_datetime(c, CTRUE); }
    else if (sys_stricmp(cmd, "time") == 0) { cmd_datetime(c, CFALSE); }
    else if (sys_stricmp(cmd, "about") == 0) { app_about_open(); }
    else if (sys_stricmp(cmd, "pwd") == 0) { con_add(c, c->cwd); }
    else if (sys_stricmp(cmd, "exit") == 0 || sys_stricmp(cmd, "quit") == 0) {
        wm_destroy(win); return CFALSE;
    }
    else { con_addf(c, "'%s' is not recognized. Type 'help'.", cmd); }
    return CTRUE;
}

/* ---- input ----------------------------------------------------------- */
static void con_history_push(Console *c, const char *line)
{
    int n = ring_count(&c->hring);
    if (line[0] == '\0') { return; }
    /* Do not record the same command twice in a row. The newest entry is
     * index count-1; this used to be `(hist_count - 1) % CON_HIST` over a
     * count that never capped, which is a third spelling of the same idea. */
    if (n > 0 && strcmp(c->hist[ring_at(&c->hring, n - 1)], line) == 0) {
        return;
    }
    sys_strlcpy(c->hist[ring_push(&c->hring)], line, CON_COLS);
}

static void con_submit(WmWindow *win, Console *c)
{
    char prompt[CASTALIA_MAX_PATH];
    char echo[CON_COLS + CASTALIA_MAX_PATH];
    con_prompt(c, prompt, sizeof(prompt));
    sys_snprintf(echo, sizeof(echo), "%s%s", prompt, c->input);
    con_add(c, echo);
    con_history_push(c, c->input);
    if (!con_exec(win, c, c->input)) { return; } /* window destroyed */
    c->input[0] = '\0'; c->input_len = 0; c->hist_pos = -1;
}

int app_console_line_count(WmWindow *win)
{
    Console *c = (Console *)wm_user(win);
    return (c != NULL) ? ring_count(&c->scroll) : 0;
}

/* One scrollback line, oldest first, or NULL past the end. The count alone
 * cannot tell a tree from a stack trace: --console-demo runs `tree` and has to
 * see the elbows and the folder names, not merely that output appeared. */
const char *app_console_line(WmWindow *win, int i)
{
    Console *c = (win != NULL) ? (Console *)wm_user(win) : NULL;
    int slot;
    if (c == NULL || i < 0 || i >= ring_count(&c->scroll)) { return NULL; }
    slot = ring_at(&c->scroll, i);
    if (slot < 0 || slot >= CON_LINES) { return NULL; }
    return c->lines[slot];
}

int app_console_hist_count(WmWindow *win)
{
    Console *c = (Console *)wm_user(win);
    return (c != NULL) ? ring_count(&c->hring) : 0;
}

/*
 * Which scrollback lines are on screen, and where the input line sits.
 *
 * Paint and the invalidate below both ask here rather than each doing the
 * arithmetic. The input line is NOT simply the last row of the window: the
 * scrollback is drawn from the top, so with four lines of output in a
 * twenty-row window the prompt sits on row five. Two copies of that would
 * eventually disagree, and the way it shows is a prompt that does not
 * repaint while you type into it.
 */
static void con_rows(WmWindow *win, Console *c, int *first, int *bottom,
                     int *inp_y)
{
    CPoint o = wm_client_origin(win);
    CRect cr = wm_client_rect(win);
    int rows = (crect_h(&cr) - 6) / CON_ROW_H;
    int body, b, f, shown;
    if (rows < 2) { rows = 2; }
    body = rows - 1;
    b = con_count(c) - c->view;
    f = b - body;
    if (f < 0) { f = 0; }
    shown = b - f;
    if (shown < 0) { shown = 0; }
    if (first != NULL)  { *first = f; }
    if (bottom != NULL) { *bottom = b; }
    if (inp_y != NULL)  { *inp_y = o.y + 4 + shown * CON_ROW_H; }
}

/*
 * Repaint the input line, and only that.
 *
 * Typing at the prompt repainted the whole window -- every line of
 * scrollback, none of which had moved. The scrollback only changes when a
 * command runs or the view scrolls, and both of those still repaint whole.
 */
static void con_invalidate_input(WmWindow *win, Console *c)
{
    CPoint o = wm_client_origin(win);
    CRect cr = wm_client_rect(win);
    CRect row;
    int y = 0;
    con_rows(win, c, (int *)0, (int *)0, &y);
    row = crect_make(o.x, y - 1, crect_w(&cr), CON_ROW_H + 2);
    wm_invalidate(win, &row);
}

static void con_key(WmWindow *win, Console *c, int key, int ch)
{
    int visible;
    CRect cr = wm_client_rect(win);
    visible = (crect_h(&cr) - 8) / CON_ROW_H - 1;
    if (visible < 1) { visible = 1; }

    if (key == PLAT_KEY_ENTER) { con_submit(win, c); wm_invalidate(win, NULL); return; }
    if (key == PLAT_KEY_BACKSP) {
        if (c->input_len > 0) {
            c->input[--c->input_len] = '\0';
            con_invalidate_input(win, c);
        }
        return;
    }
    if (key == PLAT_KEY_UP) {
        int total = ring_count(&c->hring);
        if (total > 0) {
            if (c->hist_pos < 0) { c->hist_pos = total - 1; }
            else if (c->hist_pos > 0) { c->hist_pos--; }
            /* hist_pos indexes the ring directly: 0 is the oldest command
             * still remembered. The `base = hist_count - total` arithmetic
             * this replaces existed only because the count never capped. */
            sys_strlcpy(c->input, c->hist[ring_at(&c->hring, c->hist_pos)],
                        CON_COLS);
            c->input_len = (int)sys_strnlen(c->input, CON_COLS);
            con_invalidate_input(win, c);
        }
        return;
    }
    if (key == PLAT_KEY_DOWN) {
        int total = ring_count(&c->hring);
        if (c->hist_pos >= 0 && c->hist_pos < total - 1) {
            c->hist_pos++;
            sys_strlcpy(c->input, c->hist[ring_at(&c->hring, c->hist_pos)],
                        CON_COLS);
            c->input_len = (int)sys_strnlen(c->input, CON_COLS);
        } else { c->hist_pos = -1; c->input[0] = '\0'; c->input_len = 0; }
        con_invalidate_input(win, c);
        return;
    }
    if (key == PLAT_KEY_PGUP) {
        c->view += visible - 1;
        if (c->view > con_count(c) - 1) { c->view = con_count(c) - 1; }
        if (c->view < 0) { c->view = 0; }
        wm_invalidate(win, NULL); return;
    }
    if (key == PLAT_KEY_PGDN) {
        c->view -= visible - 1; if (c->view < 0) { c->view = 0; }
        wm_invalidate(win, NULL); return;
    }
    if (ch >= 32 && ch < 127 && c->input_len < CON_COLS - 1) {
        c->input[c->input_len++] = (char)ch;
        c->input[c->input_len] = '\0';
        con_invalidate_input(win, c);
    }
}

/* ---- paint ----------------------------------------------------------- */
#define CON_BG   GFX_RGB(0x08, 0x10, 0x0A)
#define CON_FG   GFX_RGB(0xC8, 0xD8, 0xC8)
#define CON_HI   GFX_RGB(0x66, 0xEE, 0x88)

static void con_paint(WmWindow *win, GfxSurface *s)
{
    Console *c = (Console *)wm_user(win);
    CPoint o = wm_client_origin(win);
    CRect cr = wm_client_rect(win);
    CRect well;
    int cw = crect_w(&cr), ch = crect_h(&cr);
    int rows, i, y, first;
    if (c == NULL) { return; }
    well = crect_make(o.x, o.y, cw, ch);
    gfx_fill_rect(s, &well, CON_BG);

    rows = (ch - 6) / CON_ROW_H;
    if (rows < 2) { rows = 2; }
    y = o.y + 4;
    /* The input line follows the scrollback rather than sitting at the
     * bottom of the window; con_rows owns that arithmetic for both this and
     * the invalidate that repaints the prompt while you type. */
    {
        int bottom = 0;
        con_rows(win, c, &first, &bottom, (int *)0);
        for (i = first; i < bottom && i < con_count(c); i++) {
            gfx_draw_text(s, GFX_FONT_SYSTEM, o.x + 5, y, con_line(c, i), CON_FG);
            y += CON_ROW_H;
        }
    }
    /* Input line with prompt + blinking cursor (only when at the bottom). */
    if (c->view == 0) {
        char prompt[CASTALIA_MAX_PATH];
        int px;
        con_prompt(c, prompt, sizeof(prompt));
        gfx_draw_text(s, GFX_FONT_SYSTEM, o.x + 5, y, prompt, CON_HI);
        px = o.x + 5 + gfx_text_width(GFX_FONT_SYSTEM, prompt);
        gfx_draw_text(s, GFX_FONT_SYSTEM, px, y, c->input, CON_FG);
        px += gfx_text_width(GFX_FONT_SYSTEM, c->input);
        /*
         * Recorded on every paint, drawn on half of them: the blink timer
         * below invalidates this rect alone rather than the whole window.
         *
         * And only in the window that has the keyboard. A caret is a promise
         * that what you type lands here; an unfocused console blinking one
         * was making that promise while the keys went somewhere else -- and
         * charging for it, because the blink kept invalidating forever
         * whether or not anybody was looking at it. cur_rect is cleared so
         * the timer below has nothing to invalidate.
         */
        if (wm_has_focus(win)) {
            c->cur_rect = crect_make(px + 1, y, 6, 8);
            if ((c->blink / 16) % 2 == 0) {
                gfx_fill_rect(s, &c->cur_rect, CON_HI);
            }
        } else {
            c->cur_rect = crect_make(0, 0, 0, 0);
        }
    } else {
        c->cur_rect = crect_make(0, 0, 0, 0);   /* scrolled up: no caret */
        gfx_draw_text(s, GFX_FONT_SYSTEM, o.x + 5, y,
                      "-- scrolled up (PgDn to return) --", CON_HI);
    }
}

static cbool console_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    Console *c = (Console *)wm_user(win);
    switch (msg) {
    case WM_MSG_PAINT:   con_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_MOUSEWHEEL:
        /*
         * The console counts its scroll the other way round: 'view' is how
         * many lines UP from the bottom the window is, so a notch away from
         * the user increases it. ui_scroll_wheel is not used here for that
         * reason -- bending it to an inverted axis would make the shared
         * rule harder to read than the four lines it saves.
         */
        if (c != NULL) {
            int was = c->view;
            int max = con_count(c) - 1;
            c->view += (int)a * UI_WHEEL_LINES;
            if (c->view > max) { c->view = max; }
            if (c->view < 0)   { c->view = 0; }
            if (c->view != was) { wm_invalidate(win, NULL); }
        }
        return CTRUE;
    case WM_MSG_KEYDOWN: con_key(win, c, (int)a, (int)b); return CTRUE;
    case WM_MSG_TIMER:
        if (c != NULL) {
            c->blink++;
            /* A caret is six pixels wide. Repainting a 600x400 console around
             * it twice a second cost 600,000 filled pixels a blink; the caret
             * itself costs the area of a caret. */
            if (c->blink % 16 == 0 && !crect_empty(&c->cur_rect)) {
                CRect r = crect_inset(&c->cur_rect, -1);
                wm_invalidate(win, &r);
            }
        }
        return CTRUE;
    case WM_MSG_DESTROY:
        if (c != NULL) { sys_free(c, (cu32)sizeof(Console)); }
        return CTRUE;
    default: return CFALSE;
    }
}

const char *app_console_cwd(WmWindow *win)
{
    Console *c = (win != NULL) ? (Console *)wm_user(win) : NULL;
    return (c != NULL) ? c->cwd : NULL;
}

void app_console_open(void)
{
    Console *c;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int cw = 580, ch = 360, fx, fy;

    c = (Console *)sys_calloc(1, (cu32)sizeof(Console));
    if (c == NULL) { SYS_LOGE("app", "console: OOM"); return; }
    c->hist_pos = -1;
    /* The struct arrives zeroed from sys_calloc, and a zeroed Ring has a
     * capacity of ZERO -- which ring_push quietly rounds up to one rather
     * than dividing by it. That is a one-line console, and it is exactly what
     * this window became when the ring was first wired in: the banner and the
     * directory listing scrolled off instantly, leaving a single line. It was
     * caught by opening the window and looking at it. */
    ring_init(&c->scroll, CON_LINES);
    ring_init(&c->hring, CON_HIST);
    sys_strlcpy(c->cwd, sys_home(), sizeof(c->cwd));

    con_add(c, "CastaliaOS Console  [Version " CASTALIA_VER_STRING "]");
    con_add(c, "(c) Castalia Project. Type 'help' for a list of commands.");
    con_add(c, "");

    plat_video_info(&vi);
    fx = (vi.width - cw) / 2; fy = (vi.height - ch) / 2;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, cw, ch);

    w = wm_create("Console", &frame, WM_STYLE_APP, console_proc, c);
    if (w == NULL) { sys_free(c, (cu32)sizeof(Console)); return; }
    wm_show(w, CTRUE);
    wm_set_animated(w, CTRUE);   /* blinking cursor */
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened Console");
}
