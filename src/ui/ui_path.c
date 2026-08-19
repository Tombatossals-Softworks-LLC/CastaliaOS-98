/*
 * ui_path.c - Path and listing helpers for the file dialog.
 *
 * The fiddly half of "browse for a file": joining a directory to a name
 * without doubling the separator, walking up without falling off the root,
 * matching an extension filter, and ordering a listing the way a reader
 * expects (parent, then folders, then files, each alphabetically).
 *
 * It is separated from the dialog itself (ui_filedlg.c) because none of it
 * needs a window -- which is what lets the host tests drive every edge case
 * (tests/test_path.c) instead of hoping the dialog behaves.
 *
 * Both separators are accepted on input, since DOS paths use '\\' and the
 * host build is handed '/'; output uses whichever the path already carries.
 */
#include "castalia/ui.h"
#include "castalia/sys.h"

static cbool up_sep(char c) { return (c == '/' || c == '\\') ? CTRUE : CFALSE; }

static char up_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* The separator a path is already written with, so a join does not mix them. */
static char up_sep_of(const char *path)
{
    int i;
    if (path == NULL) { return '/'; }
    for (i = 0; path[i] != '\0'; i++) {
        if (path[i] == '\\') { return '\\'; }
        if (path[i] == '/')  { return '/'; }
    }
    return '/';
}

void ui_path_join(char *out, cu32 outsz, const char *dir, const char *name)
{
    cu32 n;
    char sep;
    if (out == NULL || outsz == 0) { return; }
    out[0] = '\0';
    if (name == NULL) { name = ""; }
    if (dir == NULL || dir[0] == '\0') {
        sys_strlcpy(out, name, outsz);
        return;
    }
    /* "." is where you already are, so joining it adds nothing: "." + "A.TXT"
     * is "A.TXT", not "./A.TXT". Both browsers fall back to "." when
     * CASTALIA_HOME is unset, and every path they showed would otherwise
     * carry a "./" that means nothing. */
    if (sys_stricmp(dir, ".") == 0) {
        sys_strlcpy(out, name, outsz);
        return;
    }
    /* An absolute name ignores the directory, the way a typed path should. */
    if (up_sep(name[0]) || (name[0] != '\0' && name[1] == ':')) {
        sys_strlcpy(out, name, outsz);
        return;
    }
    sep = up_sep_of(dir);
    sys_strlcpy(out, dir, outsz);
    n = sys_strnlen(out, outsz);
    if (n > 0 && !up_sep(out[n - 1]) && n + 1 < outsz) {
        out[n] = sep;
        out[n + 1] = '\0';
        n++;
    }
    if (name[0] != '\0') {
        sys_strlcpy(out + n, name, outsz - n);
    }
}

const char *ui_path_base(const char *path)
{
    const char *b;
    if (path == NULL) { return ""; }
    b = path;
    while (*path != '\0') {
        if (*path == '/' || *path == '\\') { b = path + 1; }
        path++;
    }
    return b;
}

void ui_path_default_dir(char *out, cu32 outsz, const char *dir,
                         const char *name)
{
    if (out == NULL || outsz == 0) { return; }
    /* A name that already says where it goes -- absolute, drive-qualified, or
     * carrying a folder -- is used exactly as given. Only a bare name is
     * placed in the default folder.
     *
     * The direction matters: getting it backwards would take a file the user
     * picked from somewhere else and quietly write it into the app's own
     * folder instead, leaving the original untouched and the change apparently
     * lost. */
    if (name != NULL && ui_path_is_complete(name)) {
        sys_strlcpy(out, name, outsz);
        return;
    }
    ui_path_join(out, outsz, dir, name);
}

cbool ui_path_is_complete(const char *name)
{
    int i;
    if (name == NULL || name[0] == '\0') { return CFALSE; }
    if (up_sep(name[0])) { return CTRUE; }              /* absolute        */
    if (name[1] == ':') { return CTRUE; }               /* drive-qualified */
    for (i = 0; name[i] != '\0'; i++) {
        if (up_sep(name[i])) { return CTRUE; }          /* has a folder    */
    }
    return CFALSE;
}

void ui_path_parent_rel(const char *dir, char *dst, cu32 dstsz)
{
    int i, last = -1;
    if (dst == NULL || dstsz == 0) { return; }
    if (dir == NULL) { dir = ""; }
    sys_strlcpy(dst, dir, dstsz);
    /* Drop a trailing separator, but keep a lone root. */
    i = (int)sys_strnlen(dst, dstsz);
    if (i > 1 && up_sep(dst[i - 1])) { dst[i - 1] = '\0'; }
    for (i = 0; dst[i] != '\0'; i++) {
        if (up_sep(dst[i])) { last = i; }
    }
    if (last < 0) {
        /* No separator at all. From "." the parent is "..", and from any
         * other bare name it is the directory that name sits in. */
        sys_strlcpy(dst, (sys_stricmp(dir, ".") == 0) ? ".." : ".", dstsz);
    } else if (last == 0) {
        sys_strlcpy(dst, "/", dstsz);
    } else {
        dst[last] = '\0';
    }
}

cbool ui_path_up(char *path)
{
    int i, last = -1;
    cu32 n;
    if (path == NULL) { return CFALSE; }
    n = sys_strnlen(path, (cu32)CASTALIA_MAX_PATH);
    if (n == 0) { return CFALSE; }

    /* Roots have nowhere to go, and are left exactly as they were: a caller
     * that gets CFALSE must find its path untouched. */
    if (n == 1 && (up_sep(path[0]) || path[0] == '.')) { return CFALSE; }
    if (n == 2 && path[1] == ':') { return CFALSE; }
    if (n == 3 && path[1] == ':' && up_sep(path[2])) { return CFALSE; }

    /* Ignore a trailing separator so "A/B/" goes to "A", not to "A/B". */
    if (n > 1 && up_sep(path[n - 1])) { path[n - 1] = '\0'; n--; }
    for (i = 0; path[i] != '\0'; i++) {
        if (up_sep(path[i])) { last = i; }
    }
    if (last < 0) {                        /* a bare name: the CWD is up   */
        sys_strlcpy(path, ".", (cu32)CASTALIA_MAX_PATH);
        return CTRUE;
    }
    if (last == 0) {                       /* "/x" -> "/"                  */
        path[1] = '\0';
        return CTRUE;
    }
    if (last == 2 && path[1] == ':') {     /* "C:\x" -> "C:\"              */
        path[3] = '\0';
        return CTRUE;
    }
    path[last] = '\0';
    return CTRUE;
}

cbool ui_path_match_ext(const char *name, const char *ext)
{
    const char *dot = NULL;
    int i;
    if (name == NULL) { return CFALSE; }
    if (ext == NULL || ext[0] == '\0') { return CTRUE; }   /* no filter      */
    for (i = 0; name[i] != '\0'; i++) {
        if (name[i] == '.') { dot = name + i; }
    }
    if (dot == NULL) { return CFALSE; }
    dot++;
    for (i = 0; ext[i] != '\0'; i++) {
        if (up_lower(dot[i]) != up_lower(ext[i])) { return CFALSE; }
    }
    return (dot[i] == '\0') ? CTRUE : CFALSE;   /* ".BMP" but not ".BMPX"   */
}

int ui_path_compare(const char *a, cbool a_dir, const char *b, cbool b_dir)
{
    int i;
    if (a == NULL || b == NULL) { return 0; }
    /* The way up comes first, whatever it is called. */
    if (a[0] == '.' && a[1] == '.' && a[2] == '\0') {
        return (b[0] == '.' && b[1] == '.' && b[2] == '\0') ? 0 : -1;
    }
    if (b[0] == '.' && b[1] == '.' && b[2] == '\0') { return 1; }
    if (a_dir != b_dir) { return a_dir ? -1 : 1; }
    for (i = 0; ; i++) {
        char ca = up_lower(a[i]), cb = up_lower(b[i]);
        if (ca != cb) { return (ca < cb) ? -1 : 1; }
        if (ca == '\0') { return 0; }
    }
}
