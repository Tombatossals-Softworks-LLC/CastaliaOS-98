/*
 * capp_loader_io.c - File wrapper over the portable capp loader.
 *
 * Split from capp_loader.c (which stays free of platform I/O so the unit tests
 * can link it hermetically, mirroring the gfx_bmp / gfx_bmp_io split). This
 * reads a .CAPP off disk through the platform file API and hands the image to
 * capp_launch_image.
 */
#include "castalia/capp_loader.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#include <string.h>

CResult capp_launch_file(const char *path)
{
    long      sz;
    void     *buf;
    PlatFile *f;
    CResult   rc = CE_NOTFOUND;

    if (path == NULL || path[0] == '\0') { return CE_INVALID; }
    sz = plat_file_size(path);
    if (sz <= 0 || (cu32)sz > CAPP_SCAN_MAX_BYTES) {
        SYS_LOGW("capp", "cannot launch %s (size %ld)", path, sz);
        return CE_INVALID;
    }
    buf = sys_alloc((cu32)sz);
    if (buf == NULL) { return CE_NOMEM; }

    f = plat_fopen(path, "rb");
    if (f != NULL) {
        cu32 got = plat_fread(f, buf, (cu32)sz);
        plat_fclose(f);
        if (got == (cu32)sz) { rc = capp_launch_image(buf, (cu32)sz); }
        else { rc = CE_FAIL; }
    }
    sys_free(buf, (cu32)sz);
    return rc;
}

/* ---- launchable-package index ---------------------------------------- */
typedef struct {
    char name[CAPP_NAME_MAX];
    char path[CASTALIA_MAX_PATH];
} IndexEnt;

static IndexEnt g_index[CAPP_MAX_INDEXED];
static int      g_index_count = 0;

int capp_indexed_count(void) { return g_index_count; }

const char *capp_indexed_name(int i)
{
    if (i < 0 || i >= g_index_count) { return NULL; }
    return g_index[i].name;
}

CResult capp_launch_indexed(int i)
{
    if (i < 0 || i >= g_index_count) { return CE_INVALID; }
    return capp_launch_file(g_index[i].path);
}

/* Case-insensitive ".CAPP" suffix test (mirrors capp_scan.c). */
static cbool has_capp_ext(const char *name)
{
    cu32 n = (cu32)strlen(name);
    if (n < 5) { return CFALSE; }
    return (sys_stricmp(name + n - 5, ".CAPP") == 0) ? CTRUE : CFALSE;
}

/* Read a package and, if it is valid, host-compatible, and its code section is
 * runnable now, append it to the index. */
static void index_one(const char *dir, const char *fname)
{
    char      path[CASTALIA_MAX_PATH];
    long      sz;
    void     *buf;
    PlatFile *f;

    if (g_index_count >= CAPP_MAX_INDEXED) { return; }
    sys_snprintf(path, sizeof(path), "%s/%s", dir, fname);
    sz = plat_file_size(path);
    if (sz <= 0 || (cu32)sz > CAPP_SCAN_MAX_BYTES) { return; }
    buf = sys_alloc((cu32)sz);
    if (buf == NULL) { return; }

    f = plat_fopen(path, "rb");
    if (f != NULL) {
        cu32 got = plat_fread(f, buf, (cu32)sz);
        plat_fclose(f);
        if (got == (cu32)sz) {
            CappInfo info;
            if (capp_parse(buf, (cu32)sz, &info) == CE_OK &&
                info.abi_version <= CAPP_ABI_VERSION) {
                const void *code; cu32 code_len;
                if (capp_find_section(buf, &info, CAPP_SEC_CODE, &code, &code_len) &&
                    capp_code_is_runnable(code, code_len)) {
                    IndexEnt *e = &g_index[g_index_count++];
                    sys_strlcpy(e->name, info.name, sizeof(e->name));
                    sys_strlcpy(e->path, path, sizeof(e->path));
                }
            }
        }
    }
    sys_free(buf, (cu32)sz);
}

int capp_index_dir(const char *dir)
{
    PlatDir      *d;
    PlatDirEntry  ent;

    g_index_count = 0;
    if (dir == NULL || dir[0] == '\0') { return 0; }
    d = plat_opendir(dir);
    if (d == NULL) { return 0; }
    while (plat_readdir(d, &ent) && g_index_count < CAPP_MAX_INDEXED) {
        if (ent.is_dir || !has_capp_ext(ent.name)) { continue; }
        index_one(dir, ent.name);
    }
    plat_closedir(d);
    if (g_index_count > 0) {
        SYS_LOGI("capp", "indexed %d launchable add-on(s) in %s", g_index_count, dir);
    }
    return g_index_count;
}
