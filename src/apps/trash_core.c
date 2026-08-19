/*
 * trash_core.c - Recycle Bin bookkeeping (see trash_core.h).
 */
#include "trash_core.h"
#include "castalia/sys.h"

#define TRASH_SEP '|'

/* Where the extension starts, or -1. The LAST dot, and never a leading one:
 * ".INI" is a name, not an empty base with an extension. */
static int trash_ext_at(const char *name)
{
    int i, dot = -1;
    for (i = 0; name[i] != '\0'; i++) {
        if (name[i] == '.' && i > 0) { dot = i; }
    }
    return dot;
}

void trash_candidate(char *out, cu32 cap, const char *name, int n)
{
    int dot, blen, i, k = 0;
    char tag[8];
    int taglen;

    if (out == NULL || cap == 0) { return; }
    out[0] = '\0';
    if (name == NULL) { return; }
    if (n <= 0) { sys_strlcpy(out, name, cap); return; }

    sys_snprintf(tag, sizeof tag, "~%d", n);
    taglen = (int)sys_strnlen(tag, sizeof tag);

    dot = trash_ext_at(name);
    blen = (dot >= 0) ? dot : (int)sys_strnlen(name, 64);

    /*
     * The base is trimmed so base + tag fits 8 characters. Trimming the BASE
     * rather than the whole name is the point: the extension decides which
     * app opens the file, so a restored NOTES~1.TX would stop being text.
     */
    if (blen + taglen > 8) { blen = 8 - taglen; }
    if (blen < 1) { blen = 1; }

    for (i = 0; i < blen && (cu32)(k + 1) < cap; i++) { out[k++] = name[i]; }
    for (i = 0; i < taglen && (cu32)(k + 1) < cap; i++) { out[k++] = tag[i]; }
    if (dot >= 0) {
        for (i = dot; name[i] != '\0' && (cu32)(k + 1) < cap; i++) {
            out[k++] = name[i];
        }
    }
    out[k] = '\0';
}

/* The start of the line whose stored-name is 'stored', or -1. */
static int trash_line_of(const char *buf, const char *stored)
{
    int i = 0;
    if (buf == NULL || stored == NULL || stored[0] == '\0') { return -1; }
    while (buf[i] != '\0') {
        int start = i, j = 0;
        while (stored[j] != '\0' && buf[i + j] == stored[j]) { j++; }
        if (stored[j] == '\0' && buf[i + j] == TRASH_SEP) { return start; }
        while (buf[i] != '\0' && buf[i] != '\n') { i++; }
        if (buf[i] == '\n') { i++; }
    }
    return -1;
}

/* Drop the line starting at 'start'. */
static void trash_cut_line(char *buf, int start)
{
    int end = start;
    int i;
    while (buf[end] != '\0' && buf[end] != '\n') { end++; }
    if (buf[end] == '\n') { end++; }
    for (i = 0; buf[end + i] != '\0'; i++) { buf[start + i] = buf[end + i]; }
    buf[start + i] = '\0';
}

void trash_idx_remove(char *buf, cu32 cap, const char *stored)
{
    int at;
    CASTALIA_UNUSED(cap);
    if (buf == NULL) { return; }
    at = trash_line_of(buf, stored);
    if (at >= 0) { trash_cut_line(buf, at); }
}

cbool trash_idx_add(char *buf, cu32 cap, const char *stored, const char *orig)
{
    cu32 len, need;
    if (buf == NULL || stored == NULL || orig == NULL) { return CFALSE; }
    if (stored[0] == '\0') { return CFALSE; }
    trash_idx_remove(buf, cap, stored);        /* replace, never duplicate */
    len = sys_strnlen(buf, cap);
    need = len + sys_strnlen(stored, cap) + 1u + sys_strnlen(orig, cap) + 2u;
    if (need > cap) { return CFALSE; }
    sys_strlcat(buf, stored, cap);
    {
        char sep[2];
        sep[0] = TRASH_SEP; sep[1] = '\0';
        sys_strlcat(buf, sep, cap);
    }
    sys_strlcat(buf, orig, cap);
    sys_strlcat(buf, "\n", cap);
    return CTRUE;
}

cbool trash_is_index(const char *name)
{
    return (name != NULL && sys_stricmp(name, TRASH_IDX_NAME) == 0)
         ? CTRUE : CFALSE;
}

cbool trash_idx_find(const char *buf, const char *stored,
                     char *out, cu32 outcap)
{
    int at, i, k = 0;
    if (out != NULL && outcap > 0) { out[0] = '\0'; }
    if (buf == NULL) { return CFALSE; }
    at = trash_line_of(buf, stored);
    if (at < 0) { return CFALSE; }
    i = at;
    while (buf[i] != '\0' && buf[i] != TRASH_SEP) { i++; }
    if (buf[i] != TRASH_SEP) { return CFALSE; }
    i++;
    if (out == NULL || outcap == 0) { return CTRUE; }
    while (buf[i] != '\0' && buf[i] != '\n' && (cu32)(k + 1) < outcap) {
        out[k++] = buf[i++];
    }
    out[k] = '\0';
    /* An entry with an empty path is not an answer: restoring to "" would
     * drop the file at the root of nowhere. */
    return (k > 0) ? CTRUE : CFALSE;
}

int trash_idx_count(const char *buf)
{
    int i = 0, n = 0;
    if (buf == NULL) { return 0; }
    while (buf[i] != '\0') {
        int has = 0;
        while (buf[i] != '\0' && buf[i] != '\n') { has = 1; i++; }
        if (has) { n++; }
        if (buf[i] == '\n') { i++; }
    }
    return n;
}
