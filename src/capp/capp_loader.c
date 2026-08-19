/*
 * capp_loader.c - Resolve, launch, and run .CAPP plugin code behind the ABI.
 *
 * See capp_loader.h for the design. This file is deliberately free of any
 * window-manager or gfx *code* dependency: the host api's windowing members
 * forward to ops injected by the shell (capp_loader_set_window_ops), and its
 * log/config members use the portable runtime + cfg services. That keeps the
 * loader -- and the unit tests that link it -- hermetic.
 */
#include "castalia/capp_loader.h"
#include "castalia/sys.h"
#include "castalia/cfg.h"

#include <string.h>

/* ---- builtin registry ------------------------------------------------ */
typedef struct {
    char        name[CAPP_NAME_MAX];
    CappEntryFn entry;
} BuiltinRec;

static BuiltinRec g_builtins[CAPP_MAX_BUILTINS];
static int        g_builtin_count = 0;

cbool capp_register_builtin(const char *name, CappEntryFn entry)
{
    int i;
    if (name == NULL || name[0] == '\0' || entry == NULL) { return CFALSE; }
    if ((cu32)strlen(name) >= CAPP_NAME_MAX) { return CFALSE; }
    if (g_builtin_count >= CAPP_MAX_BUILTINS) {
        SYS_LOGW("capp", "builtin table full; '%s' not registered", name);
        return CFALSE;
    }
    for (i = 0; i < g_builtin_count; i++) {
        if (sys_stricmp(g_builtins[i].name, name) == 0) { return CFALSE; }
    }
    sys_strlcpy(g_builtins[g_builtin_count].name, name, CAPP_NAME_MAX);
    g_builtins[g_builtin_count].entry = entry;
    g_builtin_count++;
    return CTRUE;
}

CappEntryFn capp_find_builtin(const char *name)
{
    int i;
    if (name == NULL) { return NULL; }
    for (i = 0; i < g_builtin_count; i++) {
        if (sys_stricmp(g_builtins[i].name, name) == 0) { return g_builtins[i].entry; }
    }
    return NULL;
}

void capp_clear_builtins(void)
{
    g_builtin_count = 0;
}

/* ---- injected dependencies ------------------------------------------- */
static CappWindowOps      g_winops;         /* zero => no windowing         */
static cbool              g_has_winops = CFALSE;
static CappNativeResolver g_native_resolver = NULL;

static char     g_cfg_path[CASTALIA_MAX_PATH];
static cbool    g_cfg_have_path = CFALSE;
static CfgFile *g_cfg = NULL;                /* lazily loaded                */
static cbool    g_cfg_tried = CFALSE;

void capp_loader_set_window_ops(const CappWindowOps *ops)
{
    if (ops == NULL) { g_has_winops = CFALSE; memset(&g_winops, 0, sizeof(g_winops)); return; }
    g_winops = *ops;
    g_has_winops = CTRUE;
}

void capp_loader_set_config(const char *ini_path)
{
    if (g_cfg != NULL) { cfg_free(g_cfg); g_cfg = NULL; }
    g_cfg_tried = CFALSE;
    if (ini_path == NULL || ini_path[0] == '\0') { g_cfg_have_path = CFALSE; return; }
    sys_strlcpy(g_cfg_path, ini_path, sizeof(g_cfg_path));
    g_cfg_have_path = CTRUE;
}

void capp_loader_set_native_resolver(CappNativeResolver fn)
{
    g_native_resolver = fn;
}

void capp_loader_shutdown(void)
{
    if (g_cfg != NULL) { cfg_free(g_cfg); g_cfg = NULL; }
    g_cfg_tried = CFALSE;
}

/* ---- host api (handed to every plugin) ------------------------------- */
static void host_log(int level, const char *tag, const char *message)
{
    SysLogLevel lv = SYS_LOG_INFO;
    if (level == CAPP_LOG_WARN)  { lv = SYS_LOG_WARN; }
    else if (level == CAPP_LOG_ERROR) { lv = SYS_LOG_ERROR; }
    sys_log_write(lv, (tag != NULL) ? tag : "capp", "capp_loader.c", 0,
                  "%s", (message != NULL) ? message : "");
}

static void *host_open_window(const char *title, int w, int h)
{
    if (!g_has_winops || g_winops.open_window == NULL) {
        SYS_LOGW("capp", "plugin requested a window but no windowing backend is installed");
        return NULL;
    }
    return g_winops.open_window(title, w, h);
}

static void host_close_window(void *win)
{
    if (g_has_winops && g_winops.close_window != NULL) { g_winops.close_window(win); }
}

static GfxSurface *host_window_surface(void *win)
{
    if (g_has_winops && g_winops.window_surface != NULL) { return g_winops.window_surface(win); }
    return NULL;
}

static void host_invalidate(void *win)
{
    if (g_has_winops && g_winops.invalidate != NULL) { g_winops.invalidate(win); }
}

static const char *host_config_get(const char *section, const char *key,
                                   const char *def)
{
    if (!g_cfg_have_path) { return def; }
    if (!g_cfg_tried) {
        g_cfg_tried = CTRUE;
        g_cfg = cfg_load(g_cfg_path, NULL);   /* NULL if the file is absent */
    }
    if (g_cfg == NULL) { return def; }
    return cfg_get_str(g_cfg, section, key, def);
}

static const CappHostApi g_host_api = {
    CAPP_ABI_VERSION, 0,
    host_log,
    host_open_window, host_close_window, host_window_surface, host_invalidate,
    host_config_get
};

/* ---- running-plugin table -------------------------------------------- */
static const CappPlugin *g_running[CAPP_MAX_RUNNING];
static int               g_running_count = 0;

int capp_running_count(void) { return g_running_count; }

const CappPlugin *capp_running_at(int index)
{
    if (index < 0 || index >= g_running_count) { return NULL; }
    return g_running[index];
}

void capp_tick_running(void)
{
    int i;
    for (i = 0; i < g_running_count; i++) {
        const CappPlugin *p = g_running[i];
        if (p != NULL && p->frame != NULL) { p->frame(); }
    }
}

void capp_shutdown_all(void)
{
    int i;
    for (i = 0; i < g_running_count; i++) {
        const CappPlugin *p = g_running[i];
        if (p != NULL && p->shutdown != NULL) { p->shutdown(); }
    }
    g_running_count = 0;
}

/* ---- code-section resolution ----------------------------------------- */
/* If 'code' is a builtin reference, copy its name into 'name_out' (>= CAPP_NAME_MAX)
 * and return CTRUE; otherwise return CFALSE (it is native code). */
static cbool code_builtin_name(const void *code, cu32 len, char *name_out)
{
    const char *p = (const char *)code;
    cu32 i;
    if (len <= (cu32)CAPP_CODE_BUILTIN_TAG_LEN) { return CFALSE; }
    if (memcmp(p, CAPP_CODE_BUILTIN_TAG, CAPP_CODE_BUILTIN_TAG_LEN) != 0) { return CFALSE; }
    /* The name must be NUL-terminated within the section and fit CAPP_NAME_MAX. */
    for (i = (cu32)CAPP_CODE_BUILTIN_TAG_LEN; i < len; i++) {
        if (p[i] == '\0') {
            cu32 nlen = i - (cu32)CAPP_CODE_BUILTIN_TAG_LEN;
            if (nlen == 0 || nlen >= CAPP_NAME_MAX) { return CFALSE; }
            memcpy(name_out, p + CAPP_CODE_BUILTIN_TAG_LEN, nlen);
            name_out[nlen] = '\0';
            return CTRUE;
        }
    }
    return CFALSE;
}

/* Quiet predicate: could this code section actually be run right now? (A
 * registered builtin, or native code with a resolver installed.) Used by the
 * directory indexer to list only launchable packages. */
cbool capp_code_is_runnable(const void *code, cu32 len)
{
    char name[CAPP_NAME_MAX];
    if (code_builtin_name(code, len, name)) {
        return (capp_find_builtin(name) != NULL) ? CTRUE : CFALSE;
    }
    return (g_native_resolver != NULL) ? CTRUE : CFALSE;
}

/* Resolve a code section to a CappEntryFn, or NULL. '*out_rc' carries the
 * classified reason on failure. */
static CappEntryFn resolve_entry(const void *code, cu32 len, CResult *out_rc)
{
    char name[CAPP_NAME_MAX];
    if (code_builtin_name(code, len, name)) {
        CappEntryFn fn = capp_find_builtin(name);
        if (fn == NULL) {
            SYS_LOGW("capp", "builtin plugin '%s' is not registered", name);
            *out_rc = CE_NOTFOUND;
        }
        return fn;
    }
    /* Native code section: needs a platform resolver. */
    if (g_native_resolver != NULL) {
        CappEntryFn fn = g_native_resolver(code, len);
        if (fn == NULL) {
            SYS_LOGW("capp", "native code section could not be loaded");
            *out_rc = CE_FAIL;
        }
        return fn;
    }
    SYS_LOGW("capp", "native code loading is not available on this platform");
    *out_rc = CE_UNSUPPORTED;
    return NULL;
}

/* ---- launch ---------------------------------------------------------- */
CResult capp_launch_image(const void *image, cu32 len)
{
    CappInfo info;
    CResult  rc;
    const void *code = NULL;
    cu32     code_len = 0;
    CappEntryFn entry;
    const CappPlugin *plug;

    rc = capp_parse(image, len, &info);
    if (rc != CE_OK) {
        SYS_LOGW("capp", "refusing to launch: package invalid (code %d)", (int)rc);
        return CE_INVALID;
    }
    if (info.abi_version > CAPP_ABI_VERSION) {
        SYS_LOGW("capp", "package '%s' targets ABI %u, host is %u -- refused",
                 info.name, info.abi_version, (unsigned)CAPP_ABI_VERSION);
        return CE_UNSUPPORTED;
    }
    if (!capp_find_section(image, &info, CAPP_SEC_CODE, &code, &code_len)) {
        SYS_LOGW("capp", "package '%s' has no code section -- nothing to run", info.name);
        return CE_INVALID;
    }

    rc = CE_FAIL;
    entry = resolve_entry(code, code_len, &rc);
    if (entry == NULL) { return rc; }

    plug = entry();
    if (plug == NULL) {
        SYS_LOGW("capp", "plugin in '%s' returned no descriptor", info.name);
        return CE_INVALID;
    }
    if (plug->abi_version > CAPP_ABI_VERSION) {
        SYS_LOGW("capp", "plugin '%s' targets ABI %u, host is %u -- refused",
                 (plug->name ? plug->name : info.name), plug->abi_version,
                 (unsigned)CAPP_ABI_VERSION);
        return CE_UNSUPPORTED;
    }
    if (g_running_count >= CAPP_MAX_RUNNING) {
        SYS_LOGW("capp", "too many add-ons running; '%s' not launched",
                 (plug->name ? plug->name : info.name));
        return CE_NOMEM;
    }

    if (plug->init != NULL) {
        int irc = plug->init(&g_host_api);
        if (irc != 0) {
            SYS_LOGW("capp", "plugin '%s' init failed (%d)",
                     (plug->name ? plug->name : info.name), irc);
            return CE_NOMEM;
        }
    }
    g_running[g_running_count++] = plug;
    if (plug->open != NULL) { plug->open(); }

    SYS_LOGI("capp", "launched add-on '%s' v%s (%d running)",
             (plug->name ? plug->name : info.name),
             (plug->version ? plug->version : "?"), g_running_count);
    return CE_OK;
}
