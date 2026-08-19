/*
 * capp_loader.h - Load and run the plugin code inside a .CAPP package.
 *
 * capp.h/capp_scan.c only DISCOVER and validate packages; capp_abi.h defines
 * the *contract* a package's CAPP_SEC_CODE section targets. This loader closes
 * the loop: it resolves a package's code section to a CappEntryFn, checks the
 * plugin's ABI against the host, hands over a live CappHostApi, and drives the
 * plugin through its init/open/frame/shutdown lifecycle. It is the piece that
 * actually *executes* add-on code behind the ABI.
 *
 * Two ways a code section becomes a runnable CappEntryFn:
 *
 *   1. Builtin reference (portable, works on every backend). The code section
 *      is the tag CAPP_CODE_BUILTIN_TAG followed by a NUL-terminated builtin
 *      name; the loader looks that name up in a table of compiled-in plugins
 *      registered with capp_register_builtin(). A statically linked plugin is
 *      still a real plugin: it is reached ONLY through the CappHostApi/CappPlugin
 *      ABI, exactly as a dynamically loaded one would be. This is how the host
 *      (which has no runtime code loader) runs add-ons, and how DOS runs the
 *      add-ons that ship in the base image.
 *
 *   2. Native code (DOS, future work). A code section that is not a builtin
 *      reference holds relocatable machine code. Mapping/relocating it and
 *      resolving CAPP_ENTRY_SYMBOL is platform-specific; a backend installs a
 *      resolver with capp_loader_set_native_resolver(). Without one, native
 *      code sections are refused (CE_UNSUPPORTED) rather than trusted blindly.
 *
 * The loader itself is portable C89 and host-tested (tests/test_capp exercises
 * registry lookup, ABI rejection, and the full lifecycle with a headless test
 * plugin). Windowing is injected (capp_loader_set_window_ops) so this module
 * never depends on the window manager or gfx code -- keeping it, and the unit
 * tests, hermetic.
 */
#ifndef CASTALIA_CAPP_LOADER_H
#define CASTALIA_CAPP_LOADER_H

#include "castalia/capp.h"
#include "castalia/capp_abi.h"

/* A CAPP_SEC_CODE section that begins with this 4-byte tag is a *builtin
 * reference*: the tag is immediately followed by the NUL-terminated name of a
 * compiled-in plugin. Real native code never starts with this tag, so the two
 * are unambiguous. */
#define CAPP_CODE_BUILTIN_TAG     "CBLT"
#define CAPP_CODE_BUILTIN_TAG_LEN 4

/* Table limits (fixed pools, no dynamic explosion -- P2/DOS friendly). */
#define CAPP_MAX_BUILTINS 16
#define CAPP_MAX_RUNNING  8

/* ---- Builtin plugin registry ----------------------------------------- */
/*
 * Register a compiled-in plugin's entry function under 'name'. A package whose
 * code section is "<tag><name>" runs this plugin. Returns CTRUE on success,
 * CFALSE if the table is full, the name is empty/too long, 'entry' is NULL, or
 * the name is already registered. Names are matched case-insensitively.
 */
cbool capp_register_builtin(const char *name, CappEntryFn entry);

/* Look up a registered builtin by name (NULL if absent). Mostly for tests. */
CappEntryFn capp_find_builtin(const char *name);

/* Drop every registered builtin (used by tests to start from a clean table). */
void capp_clear_builtins(void);

/* ---- Loader configuration (injected dependencies) -------------------- */
/*
 * Windowing operations the host api forwards to. The shell installs real,
 * window-manager-backed ops (see capp_host_wm.c); left unset, open_window
 * returns NULL so a plugin degrades gracefully with no window. All handles are
 * opaque WM window ids, matching capp_abi.h.
 */
typedef struct CappWindowOps {
    void *(*open_window)(const char *title, int w, int h);
    void  (*close_window)(void *win);
    GfxSurface *(*window_surface)(void *win);
    void  (*invalidate)(void *win);
} CappWindowOps;

/* Install the windowing backend (copied). Pass NULL to clear it. */
void capp_loader_set_window_ops(const CappWindowOps *ops);

/* Point config_get at the shared INI (e.g. C:\CASTALIA\SYS\CASTALIA.INI). The
 * file is loaded lazily on first use and cached; pass NULL to disable config
 * (config_get then returns the caller's default). */
void capp_loader_set_config(const char *ini_path);

/* Install a backend resolver for NATIVE code sections. It receives the raw
 * code bytes and returns a CappEntryFn, or NULL if it cannot load them. Unset
 * by default (native sections are refused). */
typedef CappEntryFn (*CappNativeResolver)(const void *code, cu32 len);
void capp_loader_set_native_resolver(CappNativeResolver fn);

/* Release cached loader state (the config CfgFile). Does not stop plugins. */
void capp_loader_shutdown(void);

/* ---- Launch ---------------------------------------------------------- */
/*
 * Validate the package image, resolve its code section to a CappEntryFn, check
 * the plugin's abi_version against CAPP_ABI_VERSION, then call init(host) and
 * open(). On success the plugin is tracked as running (see capp_tick_running /
 * capp_shutdown_all). Returns:
 *   CE_OK          launched
 *   CE_INVALID     bad package, no code section, or bad plugin descriptor
 *   CE_UNSUPPORTED plugin/package ABI newer than the host, or native code with
 *                  no resolver
 *   CE_NOTFOUND    builtin name not registered
 *   CE_NOMEM       running table full or init() failed
 * Every outcome is logged.
 */
CResult capp_launch_image(const void *image, cu32 len);

/* Read a .CAPP file (bounded by CAPP_SCAN_MAX_BYTES) and launch it. */
CResult capp_launch_file(const char *path);

/* True if a code section could be run right now (a registered builtin, or
 * native code with a resolver installed). Quiet -- used by the indexer. */
cbool capp_code_is_runnable(const void *code, cu32 len);

/* ---- Launchable-package index (capp_loader_io.c) --------------------- */
/*
 * Scan 'dir' for valid, host-compatible, *runnable* .CAPP packages and record
 * them so the shell can offer them in the launcher. Replaces any previous
 * index. Returns the number indexed (capped at CAPP_MAX_INDEXED).
 */
#define CAPP_MAX_INDEXED 12
int capp_index_dir(const char *dir);
int capp_indexed_count(void);
/* Display name of indexed package 'i' (NULL if out of range). */
const char *capp_indexed_name(int i);
/* Launch indexed package 'i' from its recorded path. */
CResult capp_launch_indexed(int i);

/* ---- Running plugins ------------------------------------------------- */
/* Cooperative tick: call once per frame to drive each running plugin's
 * optional frame() callback. */
void capp_tick_running(void);

/* Call every running plugin's shutdown() and clear the table. */
void capp_shutdown_all(void);

/* How many plugins are currently running. */
int  capp_running_count(void);

/* The descriptor of running plugin 'index' (NULL if out of range). */
const CappPlugin *capp_running_at(int index);

/* ---- Bundled sample add-ons (capp_sample.c) -------------------------- */
/* Register the sample plugins ('hello', 'clock') as builtins so packages that
 * reference them can be launched. Called once at shell startup. */
void capp_register_builtins(void);

/* ---- Window-manager backend (capp_host_wm.c) ------------------------- */
/* Install real, window-manager-backed windowing ops for plugins (a retained
 * client surface per window, blitted by the compositor). Call after wm_init. */
void capp_host_wm_install(void);
/* Free any retained plugin-window surfaces still held (shell teardown). */
void capp_host_wm_shutdown(void);

#endif /* CASTALIA_CAPP_LOADER_H */
