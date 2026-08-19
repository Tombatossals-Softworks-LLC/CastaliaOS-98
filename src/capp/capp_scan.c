/*
 * capp_scan.c - Discover and validate .CAPP packages in a directory.
 *
 * The shell calls this at startup on C:\CASTALIA\APPS so add-on packages are
 * found, integrity-checked, and reported in the log -- WITHOUT executing any
 * package code (loading is future work). It is portable: it uses only the
 * platform file API and the runtime services, so it builds and runs on both
 * the host and DOS backends and is exercised by `make run`.
 */
#include "castalia/capp.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#include <string.h>

/* Case-insensitive test for a ".CAPP" suffix. */
static cbool has_capp_ext(const char *name)
{
    cu32 n = (cu32)strlen(name);
    if (n < 5) { return CFALSE; }
    return (sys_stricmp(name + n - 5, ".CAPP") == 0) ? CTRUE : CFALSE;
}

int capp_scan_dir(const char *dir)
{
    PlatDir *d;
    PlatDirEntry ent;
    int count = 0;

    if (dir == NULL || dir[0] == '\0') { return 0; }
    d = plat_opendir(dir);
    if (d == NULL) {
        SYS_LOGI("capp", "no add-on directory at %s", dir);
        return 0;
    }

    while (plat_readdir(d, &ent)) {
        char path[CASTALIA_MAX_PATH];
        long sz;
        void *buf;
        PlatFile *f;

        if (ent.is_dir || !has_capp_ext(ent.name)) { continue; }
        sys_snprintf(path, sizeof(path), "%s/%s", dir, ent.name);
        sz = plat_file_size(path);
        if (sz <= 0 || (cu32)sz > CAPP_SCAN_MAX_BYTES) {
            SYS_LOGW("capp", "skipping %s (size %ld)", ent.name, sz);
            continue;
        }
        buf = sys_alloc((cu32)sz);
        if (buf == NULL) { continue; }

        f = plat_fopen(path, "rb");
        if (f != NULL) {
            cu32 got = plat_fread(f, buf, (cu32)sz);
            plat_fclose(f);
            if (got == (cu32)sz) {
                CappInfo info;
                CResult rc = capp_parse(buf, (cu32)sz, &info);
                if (rc == CE_OK) {
                    count++;
                    SYS_LOGI("capp", "package '%s' v%s by %s (%d section(s), abi %u)",
                             info.name, info.version, info.author,
                             info.section_count, info.abi_version);
                } else {
                    SYS_LOGW("capp", "invalid package %s (code %d)",
                             ent.name, (int)rc);
                }
            }
        }
        sys_free(buf, (cu32)sz);
    }

    plat_closedir(d);
    SYS_LOGI("capp", "scanned %s: %d valid add-on package(s)", dir, count);
    return count;
}
