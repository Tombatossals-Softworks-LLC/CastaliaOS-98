/*
 * ren_core.c - Renaming many files at once (see ren_core.h).
 */
#include "ren_core.h"
#include "castalia/sys.h"

static int rc_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

/*
 * Split a name into base and extension at the LAST dot.
 *
 * "ARCHIVE.TAR.GZ" is base "ARCHIVE.TAR", ext "GZ" -- which is what DOS does
 * and is also the only split that survives a round trip. A leading dot is not
 * a separator: ".PROFILE" is a base, not an empty name with an extension.
 */
static void rc_split(const char *name, char *base, cu32 bcap,
                     char *ext, cu32 ecap)
{
    int last = -1, i;
    base[0] = '\0';
    ext[0] = '\0';
    if (name == NULL) { return; }
    for (i = 0; name[i] != '\0'; i++) {
        if (name[i] == '.' && i > 0) { last = i; }
    }
    if (last < 0) {
        sys_strlcpy(base, name, bcap);
        return;
    }
    {
        cu32 n = (cu32)last + 1u;
        if (n > bcap) { n = bcap; }
        sys_strlcpy(base, name, n);
        sys_strlcpy(ext, name + last + 1, ecap);
    }
}

/*
 * One component against one pattern component.
 *
 * '?' matches one character, or NONE when the source has run out -- "A?" has
 * to match "A", because that is what DOS does and because `ren REPORT?.DOC`
 * over a folder holding REPORT.DOC would otherwise skip exactly the file the
 * person was looking at.
 */
static cbool rc_match_part(const char *s, const char *p)
{
    while (*p != '\0') {
        if (*p == '*') { return CTRUE; }          /* takes the rest */
        if (*p == '?') {
            if (*s != '\0') { s++; }
            p++;
            continue;
        }
        if (*s == '\0') { return CFALSE; }
        if (rc_lower((unsigned char)*s) != rc_lower((unsigned char)*p)) {
            return CFALSE;
        }
        s++; p++;
    }
    return (*s == '\0') ? CTRUE : CFALSE;
}

cbool ren_match(const char *name, const char *pat)
{
    char nb[CASTALIA_MAX_NAME], ne[CASTALIA_MAX_NAME];
    char pb[CASTALIA_MAX_NAME], pe[CASTALIA_MAX_NAME];
    cbool has_dot = CFALSE;
    int i;

    if (name == NULL || pat == NULL || name[0] == '\0' || pat[0] == '\0') {
        return CFALSE;
    }
    rc_split(name, nb, sizeof nb, ne, sizeof ne);
    rc_split(pat, pb, sizeof pb, pe, sizeof pe);
    for (i = 0; pat[i] != '\0'; i++) { if (pat[i] == '.' && i > 0) { has_dot = CTRUE; } }

    if (!rc_match_part(nb, pb)) { return CFALSE; }
    /*
     * A pattern with no dot says nothing about the extension, so it matches
     * whatever is there. "*" meaning "every file without an extension" would
     * make the commonest pattern of all the least useful one.
     */
    if (!has_dot) { return CTRUE; }
    return rc_match_part(ne, pe);
}

/*
 * Build one component of the new name from the TO pattern and the SOURCE.
 * Returns the length written, or -1 if it would not fit 'lim'.
 */
static int rc_build_part(const char *src, const char *to, char *out, int lim)
{
    int n = 0, i = 0;
    int slen = (int)sys_strnlen(src, (cu32)CASTALIA_MAX_NAME);
    /*
     * The copy is BOUNDED BY THE SOURCE LENGTH, which is the part that was
     * wrong: it walked `while (src[n] != '\0')` with n the OUTPUT position, so
     * "ARCHIVED*" over "REPORT1" put the star at position 8 against a
     * seven-character name and read whatever followed it on the stack.
     *
     * The source is indexed by the PATTERN position. With '*' terminal (below)
     * that stays in lockstep with the output position, so the two are
     * interchangeable here -- it is the bound that matters, not the choice.
     */
    while (to[i] != '\0') {
        if (to[i] == '*') {
            int k = i;                  /* the source position the star sits at */
            while (k < slen) {
                if (n >= lim) { return -1; }
                out[n] = src[k];
                n++; k++;
            }
            /*
             * A '*' ENDS its component. Whatever follows it has nowhere to go
             * -- the star has already taken the rest of the source -- and DOS
             * stops there too. Carrying on would make `*X*` mean "the name,
             * then X, then part of the name again", which is not something
             * anyone types on purpose.
             */
            break;
        }
        if (to[i] == '?') {
            /* the source character at this position, if there is one */
            if (i < slen) {
                if (n >= lim) { return -1; }
                out[n] = src[i];
                n++;
            }
            i++;
            continue;
        }
        if (n >= lim) { return -1; }
        out[n] = to[i];
        n++;
        i++;
    }
    out[n] = '\0';
    return n;
}

cbool ren_apply(const char *name, const char *from, const char *to,
                char *out, cu32 cap)
{
    char nb[CASTALIA_MAX_NAME], ne[CASTALIA_MAX_NAME];
    char tb[CASTALIA_MAX_NAME], te[CASTALIA_MAX_NAME];
    char ob[REN_BASE_MAX + 1], oe[REN_EXT_MAX + 1];
    cbool has_dot = CFALSE;
    int nbase, next_, i;

    (void)from;   /* see ren_core.h: DOS applies TO against the SOURCE name */
    if (out == NULL || cap == 0) { return CFALSE; }
    out[0] = '\0';
    if (name == NULL || to == NULL || name[0] == '\0' || to[0] == '\0') {
        return CFALSE;
    }
    /* A rename that could move a file elsewhere is not a rename. */
    for (i = 0; to[i] != '\0'; i++) {
        if (to[i] == '/' || to[i] == '\\' || to[i] == ':') { return CFALSE; }
    }
    rc_split(name, nb, sizeof nb, ne, sizeof ne);
    /*
     * A leading dot in a TO pattern is a separator, unlike in a NAME where
     * ".PROFILE" is a base. `ren *.TXT .BAK` asks for an empty base, and an
     * empty base is refused below -- rather than, as it did once, producing
     * ".BAK.TXT" by treating the whole thing as a literal base and then
     * appending the extension the file already had.
     */
    if (to[0] == '.') {
        tb[0] = '\0';
        sys_strlcpy(te, to + 1, sizeof te);
        has_dot = CTRUE;
    } else {
        rc_split(to, tb, sizeof tb, te, sizeof te);
        for (i = 0; to[i] != '\0'; i++) {
            if (to[i] == '.' && i > 0) { has_dot = CTRUE; }
        }
    }

    nbase = rc_build_part(nb, tb, ob, REN_BASE_MAX);
    if (nbase < 0) { return CFALSE; }
    if (has_dot) {
        next_ = rc_build_part(ne, te, oe, REN_EXT_MAX);
        if (next_ < 0) { return CFALSE; }
    } else {
        /* No dot in the TO pattern keeps the extension the file already had,
         * so `ren *.TXT BACKUP` does not silently strip ".TXT". */
        next_ = (int)sys_strnlen(ne, (cu32)REN_EXT_MAX + 1u);
        if (next_ > REN_EXT_MAX) { return CFALSE; }
        sys_strlcpy(oe, ne, sizeof oe);
    }
    if (nbase == 0) { return CFALSE; }            /* ".TXT" is not a name */

    if ((cu32)(nbase + (next_ > 0 ? next_ + 1 : 0)) >= cap) { return CFALSE; }
    sys_strlcpy(out, ob, cap);
    if (next_ > 0) {
        cu32 at = (cu32)nbase;
        out[at] = '.';
        out[at + 1u] = '\0';
        sys_strlcpy(out + at + 1u, oe, cap - at - 1u);
    }
    return CTRUE;
}

int ren_first_collision(const char *names, int n, cu32 stride)
{
    int i, j;
    if (names == NULL || stride == 0) { return -1; }
    for (i = 1; i < n; i++) {
        for (j = 0; j < i; j++) {
            if (sys_stricmp(names + (cu32)i * stride,
                            names + (cu32)j * stride) == 0) {
                return i;
            }
        }
    }
    return -1;
}
