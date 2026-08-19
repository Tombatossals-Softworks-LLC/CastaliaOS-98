/*
 * capp_abi.h - The CastaliaOS plugin ABI: the contract between the shell and
 *              the code inside a .CAPP package's CAPP_SEC_CODE section.
 *
 * This is the *contract only*. It defines two structures:
 *   - CappHostApi: the versioned table of host services a plugin may call
 *     (log, windowing, drawing surface, config). It grows append-only so a
 *     newer host stays compatible with an older plugin.
 *   - CappPlugin:  what a plugin exports -- a descriptor with its name/version
 *     and lifecycle callbacks. The plugin provides a single entry function
 *     (CAPP_ENTRY_SYMBOL) returning a pointer to a static CappPlugin.
 *
 * This header is the contract only; the loader that resolves a code section to
 * a CappEntryFn, checks its ABI, hands over a CappHostApi, and drives the
 * lifecycle lives in capp_loader.h/capp_loader.c. That loader runs compiled-in
 * ("builtin") plugins through this exact ABI on every backend today; mapping and
 * relocating *native* code on DOS is installed behind a resolver seam and is the
 * one remaining piece. Keeping the ABI here lets packages be authored and
 * versioned against a stable interface independent of how code is loaded.
 */
#ifndef CASTALIA_CAPP_ABI_H
#define CASTALIA_CAPP_ABI_H

#include "castalia/ctypes.h"
#include "castalia/gfx.h"

/* Bump when the CappHostApi/CappPlugin layout changes incompatibly. A package
 * whose header abi_version exceeds the host's is refused; equal or lower with
 * an append-only table is accepted. */
#define CAPP_ABI_VERSION 1

/* The symbol a plugin exports; the loader resolves it in the code section. */
#define CAPP_ENTRY_SYMBOL "capp_main"

/* Log levels mirror SysLogLevel without pulling sys.h into a plugin's headers. */
enum {
    CAPP_LOG_INFO = 2,
    CAPP_LOG_WARN = 3,
    CAPP_LOG_ERROR = 4
};

/*
 * Host services handed to a plugin at init. Opaque handles keep plugins off
 * the shell's internal structs. APPEND-ONLY: never reorder or remove members;
 * add new ones at the end and bump CAPP_ABI_VERSION.
 */
typedef struct CappHostApi {
    cu16 abi_version;   /* = CAPP_ABI_VERSION the host was built with        */
    cu16 reserved;

    /* Diagnostics. */
    void (*log)(int level, const char *tag, const char *message);

    /* Windowing (handles are opaque WM window ids). */
    void *(*open_window)(const char *title, int w, int h);
    void  (*close_window)(void *win);
    GfxSurface *(*window_surface)(void *win); /* client draw surface         */
    void  (*invalidate)(void *win);           /* request a repaint           */

    /* Repairable config (reads the shared CASTALIA.INI service). */
    const char *(*config_get)(const char *section, const char *key,
                              const char *def);
} CappHostApi;

/*
 * A plugin's exported descriptor. 'abi_version' must be <= the host's. The
 * lifecycle callbacks are all optional except that a plugin with no 'open' can
 * never be launched. Calls are cooperative and single-threaded, matching the
 * shell's frame loop.
 */
typedef struct CappPlugin {
    cu16 abi_version;
    cu16 reserved;
    const char *name;
    const char *version;

    int  (*init)(const CappHostApi *host); /* one-time; return 0 on success  */
    void (*open)(void);                    /* user launched the add-on       */
    void (*frame)(void);                   /* optional cooperative tick      */
    void (*shutdown)(void);                /* teardown                       */
} CappPlugin;

/* The entry function type a plugin exports under CAPP_ENTRY_SYMBOL. */
typedef const CappPlugin *(*CappEntryFn)(void);

#endif /* CASTALIA_CAPP_ABI_H */
