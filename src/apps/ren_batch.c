/*
 * ren_batch.c - Running a wildcard rename over a real folder (see the header).
 */
#include "ren_batch.h"
#include "ren_core.h"
#include "castalia/ui.h"
#include "castalia/plat.h"
#include "castalia/sys.h"
#include "castalia/castalia.h"

cbool ren_batch_is_pattern(const char *p)
{
    int i;
    if (p == NULL) { return CFALSE; }
    for (i = 0; p[i] != '\0'; i++) {
        if (p[i] == '*' || p[i] == '?') { return CTRUE; }
    }
    return CFALSE;
}

cbool ren_batch_one(const char *dir, const char *from, const char *to,
                    char *why, cu32 whysz)
{
    char src[CASTALIA_MAX_PATH], dst[CASTALIA_MAX_PATH];
    if (why != NULL && whysz > 0) { why[0] = '\0'; }
    if (dir == NULL || from == NULL || to == NULL ||
        from[0] == '\0' || to[0] == '\0') {
        if (why != NULL) { sys_strlcpy(why, "Nothing to rename.", whysz); }
        return CFALSE;
    }
    ui_path_join(src, sizeof(src), dir, from);
    ui_path_join(dst, sizeof(dst), dir, to);
    if (sys_stricmp(from, to) != 0 && plat_file_size(dst) >= 0) {
        if (why != NULL) {
            sys_snprintf(why, whysz, "'%s' already exists. Nothing renamed.", to);
        }
        return CFALSE;
    }
    if (plat_file_rename(src, dst) == CE_OK) { return CTRUE; }
    if (why != NULL) {
        sys_snprintf(why, whysz, "Could not rename '%s'.", from);
    }
    return CFALSE;
}

cbool ren_batch_run(const char *dir, const char *from, const char *to,
                    RenBatch *out)
{
    /*
     * Static because REN_BATCH_MAX names is a few kilobytes and this runs on
     * a machine whose stack is measured in kilobytes. Only one batch is ever
     * in flight -- both callers are modal at the point they ask for one.
     */
    static char plan_from[REN_BATCH_MAX][CASTALIA_MAX_NAME];
    static char plan_to[REN_BATCH_MAX][REN_NAME_MAX];
    char src[CASTALIA_MAX_PATH], dst[CASTALIA_MAX_PATH];
    PlatDir *d;
    PlatDirEntry e;
    int n = 0, i, bad;
    RenBatch r;

    r.matched = 0; r.renamed = 0; r.why[0] = '\0';
    if (out != NULL) { *out = r; }
    if (dir == NULL || from == NULL || to == NULL ||
        from[0] == '\0' || to[0] == '\0') {
        sys_strlcpy(r.why, "Nothing to rename.", sizeof r.why);
        if (out != NULL) { *out = r; }
        return CFALSE;
    }

    d = plat_opendir(dir);
    if (d == NULL) {
        sys_strlcpy(r.why, "Cannot read this folder.", sizeof r.why);
        if (out != NULL) { *out = r; }
        return CFALSE;
    }
    while (plat_readdir(d, &e)) {
        if (e.is_dir) { continue; }          /* folders are not in scope */
        if (!ren_match(e.name, from)) { continue; }
        if (n >= REN_BATCH_MAX) { n++; break; }
        sys_strlcpy(plan_from[n], e.name, (cu32)CASTALIA_MAX_NAME);
        n++;
    }
    plat_closedir(d);

    r.matched = n;
    if (n == 0) {
        sys_snprintf(r.why, sizeof r.why, "No files match '%s'.", from);
        if (out != NULL) { *out = r; }
        return CFALSE;
    }
    if (n > REN_BATCH_MAX) {
        sys_snprintf(r.why, sizeof r.why,
                     "More than %d files match -- narrow the pattern.",
                     REN_BATCH_MAX);
        if (out != NULL) { *out = r; }
        return CFALSE;
    }

    /* 1. every new name must BE a name */
    for (i = 0; i < n; i++) {
        if (!ren_apply(plan_from[i], from, to, plan_to[i], (cu32)REN_NAME_MAX)) {
            sys_snprintf(r.why, sizeof r.why,
                         "'%s' under '%s' is not a valid name. Nothing renamed.",
                         plan_from[i], to);
            if (out != NULL) { *out = r; }
            return CFALSE;
        }
    }
    /* 2. ...and must be its own */
    bad = ren_first_collision(plan_to[0], n, (cu32)REN_NAME_MAX);
    if (bad >= 0) {
        sys_snprintf(r.why, sizeof r.why,
                     "'%s' and another file both become '%s'. Nothing renamed.",
                     plan_from[bad], plan_to[bad]);
        if (out != NULL) { *out = r; }
        return CFALSE;
    }
    /*
     * 3. ...and must not land on a file that is already there. Refused even
     *    when that file is itself part of the batch: A->B with B->C is
     *    order-dependent, and the order that works is not the order a
     *    directory happens to enumerate in.
     */
    for (i = 0; i < n; i++) {
        if (sys_stricmp(plan_from[i], plan_to[i]) == 0) { continue; }
        ui_path_join(dst, sizeof(dst), dir, plan_to[i]);
        if (plat_file_size(dst) >= 0) {
            sys_snprintf(r.why, sizeof r.why,
                         "'%s' would overwrite '%s'. Nothing renamed.",
                         plan_from[i], plan_to[i]);
            if (out != NULL) { *out = r; }
            return CFALSE;
        }
    }

    for (i = 0; i < n; i++) {
        if (sys_stricmp(plan_from[i], plan_to[i]) == 0) { continue; }
        ui_path_join(src, sizeof(src), dir, plan_from[i]);
        ui_path_join(dst, sizeof(dst), dir, plan_to[i]);
        if (plat_file_rename(src, dst) == CE_OK) { r.renamed++; }
    }
    if (out != NULL) { *out = r; }
    return CTRUE;
}
