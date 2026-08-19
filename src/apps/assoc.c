/*
 * assoc.c - The association table (pure, host-tested).
 *
 * See assoc.h. Extensions are compared case-insensitively against one table,
 * so adding a type means adding one row and nothing else.
 */
#include "assoc.h"
#include "castalia/sys.h"

static const struct {
    const char *ext;
    AssocKind   kind;
    const char *type;
} A_TABLE[] = {
    { "TXT",  ASSOC_TEXT,    "Text Document" },
    { "LOG",  ASSOC_TEXT,    "Log File" },
    { "INI",  ASSOC_THEME,   "Configuration" },
    { "MD",   ASSOC_TEXT,    "Text Document" },
    { "C",    ASSOC_TEXT,    "C Source" },
    { "H",    ASSOC_TEXT,    "C Header" },
    { "BAT",  ASSOC_PROGRAM, "Batch File" },
    { "COM",  ASSOC_PROGRAM, "DOS Program" },
    { "EXE",  ASSOC_PROGRAM, "DOS Program" },
    { "BMP",  ASSOC_IMAGE,   "Bitmap Image" },
    { "CSV",  ASSOC_SHEET,   "Spreadsheet" },
    { "DOC",  ASSOC_WRITE,   "Document" },
    { "WRI",  ASSOC_WRITE,   "Document" },
    { "WAV",  ASSOC_AUDIO,   "Sound" },
    { "CAPP", ASSOC_PACKAGE, "CastaliaOS Package" },
    { "CZ",   ASSOC_ARCHIVE, "Compressed File" },
    { "CAR",  ASSOC_ARCHIVE, "Castalia Archive" }
};
#define A_COUNT (int)(sizeof(A_TABLE) / sizeof(A_TABLE[0]))

/* The extension, without the dot, or NULL when there is none. A dot that is
 * part of the path ("./name") does not count, and neither does a trailing one. */
static const char *a_ext(const char *name)
{
    const char *dot = NULL;
    int i;
    if (name == NULL) { return NULL; }
    for (i = 0; name[i] != '\0'; i++) {
        if (name[i] == '/' || name[i] == '\\') { dot = NULL; continue; }
        if (name[i] == '.') { dot = name + i; }
    }
    if (dot == NULL || dot[1] == '\0') { return NULL; }
    return dot + 1;
}

static int a_row(const char *name)
{
    const char *ext = a_ext(name);
    int i;
    if (ext == NULL) { return -1; }
    for (i = 0; i < A_COUNT; i++) {
        if (sys_stricmp(ext, A_TABLE[i].ext) == 0) { return i; }
    }
    return -1;
}

AssocKind assoc_for(const char *name)
{
    int r = a_row(name);
    return (r < 0) ? ASSOC_UNKNOWN : A_TABLE[r].kind;
}

const char *assoc_type_name(const char *name)
{
    /*
     * An extension the table does not know still says something: ".LOG" is a
     * "LOG File", which is how this era named a type it had no better word
     * for -- and it is what the table itself already does for the rows it has
     * ("Log File", "Batch File").
     *
     * This line used to read `(a_ext(name) != NULL) ? "File" : "File"`, both
     * arms the same, so a Type column full of unknown extensions said "File"
     * over and over and told the reader nothing they could not see from the
     * name. -Wduplicated-branches found it; the shape of the ternary is what
     * says the distinction was meant to be there.
     */
    static char buf[24];
    char up[13];
    const char *ext;
    int i, n = 0;
    int r = a_row(name);
    if (r >= 0) { return A_TABLE[r].type; }
    ext = a_ext(name);
    if (ext == NULL) { return "File"; }
    for (i = 0; i < (int)sizeof(up) - 1 && ext[i] != '\0'; i++) {
        char c = ext[i];
        if (c >= 'a' && c <= 'z') { c = (char)(c - 'a' + 'A'); }
        up[n++] = c;
    }
    up[n] = '\0';
    if (n == 0) { return "File"; }
    sys_snprintf(buf, sizeof(buf), "%s File", up);
    return buf;
}

const char *assoc_app_name(AssocKind kind)
{
    switch (kind) {
    case ASSOC_TEXT:    return "Notepad";
    case ASSOC_IMAGE:   return "Paint";
    case ASSOC_SHEET:   return "CastaliaSheet";
    case ASSOC_WRITE:   return "CastaliaWrite";
    case ASSOC_AUDIO:   return "Media Player";
    case ASSOC_THEME:   return "the Theme Editor";
    case ASSOC_PROGRAM: return "DOS";
    case ASSOC_PACKAGE: return "the package loader";
    case ASSOC_ARCHIVE: return "the File Manager";   /* it expands it */
    default:            return "the Viewer";
    }
}
