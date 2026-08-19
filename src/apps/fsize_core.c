/*
 * fsize_core.c - Totalling file sizes (see fsize_core.h).
 */
#include "fsize_core.h"
#include "../sys/mach_core.h"
#include "castalia/sys.h"

/* Below this the exact byte count is more useful than a rounded label. */
#define FSIZE_EXACT_BELOW 10240L

void fsize_begin(FsizeSum *s)
{
    if (s == NULL) { return; }
    s->total = (cs32)0;
    s->files = 0;
    s->over  = CFALSE;
}

void fsize_add(FsizeSum *s, long bytes)
{
    if (s == NULL || bytes <= 0) { return; }
    s->files++;
    /*
     * Saturate rather than wrap. The subtraction is the safe form of the
     * question "would total + bytes exceed the limit" -- adding them first to
     * find out is the overflow itself, which is precisely the bug this file
     * exists to fix.
     */
    if (bytes > FSIZE_MAX || (long)s->total > FSIZE_MAX - bytes) {
        s->total = (cs32)FSIZE_MAX;
        s->over  = CTRUE;
        return;
    }
    s->total = (cs32)((long)s->total + bytes);
}

void fsize_label(const FsizeSum *s, char *dst, cu32 dstsz)
{
    char sz[24];
    const char *plural;
    if (dst == NULL || dstsz == 0u) { return; }
    if (s == NULL || s->files == 0) {
        sys_strlcpy(dst, "No files here", dstsz);
        return;
    }
    plural = (s->files == 1) ? "" : "s";
    if (!s->over && (long)s->total < FSIZE_EXACT_BELOW) {
        sys_snprintf(dst, dstsz, "%ld bytes in %d file%s",
                     (long)s->total, s->files, plural);
        return;
    }
    mach_kb_label((long)s->total / 1024L, sz, sizeof(sz));
    if (s->over) {
        sys_snprintf(dst, dstsz, "more than %s in %d file%s",
                     sz, s->files, plural);
    } else {
        sys_snprintf(dst, dstsz, "%s in %d file%s", sz, s->files, plural);
    }
}
