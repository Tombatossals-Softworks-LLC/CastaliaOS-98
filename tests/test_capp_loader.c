/*
 * test_capp_loader.c - Host unit tests for the .CAPP plugin loader.
 *
 * Exercises the loader (capp_loader.c) hermetically: a headless test plugin
 * (no window) is registered as a builtin, packaged into a .CAPP whose code
 * section is a builtin reference, and run through the full lifecycle. Then the
 * rejection paths -- unregistered builtin, missing code section, native code
 * with no resolver, and both package- and plugin-level ABI mismatches -- are
 * checked. Also verifies config_get reads the injected INI, and that the
 * frame() tick and shutdown() are actually invoked.
 */
#include "ctest.h"
#include "castalia/capp.h"
#include "castalia/capp_loader.h"
#include "castalia/cfg.h"

#include <string.h>
#include <stdio.h>

/* ---- a minimal headless plugin whose lifecycle we can observe --------- */
static int tp_init_calls, tp_open_calls, tp_frame_calls, tp_shutdown_calls;
static const CappHostApi *tp_host;
static char tp_cfg_theme[32];

static int tp_init(const CappHostApi *host)
{
    const char *v;
    tp_init_calls++;
    tp_host = host;
    host->log(CAPP_LOG_INFO, "tplug", "test plugin init");
    v = host->config_get("Theme", "Name", "fallback");
    strncpy(tp_cfg_theme, (v ? v : ""), sizeof(tp_cfg_theme) - 1);
    tp_cfg_theme[sizeof(tp_cfg_theme) - 1] = '\0';
    return 0;
}
static void tp_open(void)     { tp_open_calls++; }
static void tp_frame(void)    { tp_frame_calls++; }
static void tp_shutdown(void) { tp_shutdown_calls++; }

static const CappPlugin TP_PLUGIN = {
    CAPP_ABI_VERSION, 0, "TestPlugin", "0.1.0",
    tp_init, tp_open, tp_frame, tp_shutdown
};
static const CappPlugin *tp_entry(void) { return &TP_PLUGIN; }

/* A plugin whose descriptor claims a newer ABI than the host supports. */
static const CappPlugin TP_FUTURE = {
    (cu16)(CAPP_ABI_VERSION + 1), 0, "FuturePlugin", "9.9.9",
    NULL, NULL, NULL, NULL
};
static const CappPlugin *tp_future_entry(void) { return &TP_FUTURE; }

static void tp_reset(void)
{
    tp_init_calls = tp_open_calls = tp_frame_calls = tp_shutdown_calls = 0;
    tp_host = NULL;
    tp_cfg_theme[0] = '\0';
}

/* Build a package whose sole CODE section is a builtin reference to 'name'
 * (or, if 'name' is NULL, a package with no code section at all). Returns the
 * image length, or <0 on failure. */
static int build_pkg(unsigned char *out, cu32 cap, cu16 abi,
                     const char *name, const void *raw, cu32 raw_len)
{
    CappInfo meta;
    cu16  types[1], flags[1];
    const void *data[1];
    cu32  sizes[1];
    unsigned char code[64];
    cu32  clen;

    memset(&meta, 0, sizeof(meta));
    meta.abi_version = abi;
    strcpy(meta.name, "TestPkg");
    strcpy(meta.version, "1.0.0");

    if (raw != NULL) {                 /* caller-supplied raw code bytes */
        if (raw_len > sizeof(code)) { return -1; }
        memcpy(code, raw, raw_len); clen = raw_len;
    } else if (name != NULL) {          /* builtin reference "<tag>name\0" */
        memcpy(code, CAPP_CODE_BUILTIN_TAG, CAPP_CODE_BUILTIN_TAG_LEN);
        clen = (cu32)CAPP_CODE_BUILTIN_TAG_LEN;
        strcpy((char *)code + clen, name);
        clen += (cu32)strlen(name) + 1;
    } else {                            /* no code section */
        return capp_build(out, cap, &meta, types, flags, data, sizes, 0);
    }
    types[0] = CAPP_SEC_CODE; flags[0] = 0;
    data[0] = code; sizes[0] = clen;
    return capp_build(out, cap, &meta, types, flags, data, sizes, 1);
}

void test_capp_loader(void)
{
    unsigned char pkg[1024];
    int len;

    /* Start from a clean registry / running table. */
    capp_clear_builtins();
    capp_shutdown_all();
    capp_loader_set_native_resolver(NULL);
    capp_loader_set_window_ops(NULL);

    /* ---- registry basics ---- */
    CHECK(capp_register_builtin("tplug", tp_entry));
    CHECK(!capp_register_builtin("tplug", tp_entry));   /* duplicate refused  */
    CHECK(!capp_register_builtin("", tp_entry));        /* empty name refused */
    CHECK(!capp_register_builtin("x", NULL));           /* NULL entry refused */
    CHECK(capp_find_builtin("TPLUG") == tp_entry);      /* case-insensitive   */
    CHECK(capp_find_builtin("nope") == NULL);

    /* ---- config service reads the injected INI ---- */
    {
        CfgFile *c = cfg_new();
        cfg_set_str(c, "Theme", "Name", "Forest");
        CHECK_EQI(cfg_save(c, "build/test_capp.ini"), CE_OK);
        cfg_free(c);
        capp_loader_set_config("build/test_capp.ini");
    }

    /* ---- happy path: launch the builtin, run its lifecycle ---- */
    tp_reset();
    len = build_pkg(pkg, sizeof(pkg), CAPP_ABI_VERSION, "tplug", NULL, 0);
    CHECK(len > 0);
    CHECK_EQI(capp_launch_image(pkg, (cu32)len), CE_OK);
    CHECK_EQI(capp_running_count(), 1);
    CHECK_EQI(tp_init_calls, 1);
    CHECK_EQI(tp_open_calls, 1);
    CHECK(capp_running_at(0) == &TP_PLUGIN);
    CHECK_STR(tp_cfg_theme, "Forest");    /* config_get went through the INI  */

    /* frame tick drives the plugin's frame() */
    capp_tick_running();
    capp_tick_running();
    CHECK_EQI(tp_frame_calls, 2);

    /* shutdown_all invokes shutdown() and clears the table */
    capp_shutdown_all();
    CHECK_EQI(tp_shutdown_calls, 1);
    CHECK_EQI(capp_running_count(), 0);

    /* ---- rejection: unregistered builtin name ---- */
    len = build_pkg(pkg, sizeof(pkg), CAPP_ABI_VERSION, "ghost", NULL, 0);
    CHECK_EQI(capp_launch_image(pkg, (cu32)len), CE_NOTFOUND);
    CHECK_EQI(capp_running_count(), 0);

    /* ---- rejection: no code section ---- */
    len = build_pkg(pkg, sizeof(pkg), CAPP_ABI_VERSION, NULL, NULL, 0);
    CHECK(len > 0);
    CHECK_EQI(capp_launch_image(pkg, (cu32)len), CE_INVALID);

    /* ---- rejection: native code, no resolver installed ---- */
    {
        const unsigned char native[] = { 0xB8, 0x00, 0x00, 0x00, 0x00, 0xC3 };
        len = build_pkg(pkg, sizeof(pkg), CAPP_ABI_VERSION, NULL, native,
                        (cu32)sizeof(native));
        CHECK(len > 0);
        CHECK_EQI(capp_launch_image(pkg, (cu32)len), CE_UNSUPPORTED);
    }

    /* ---- rejection: package targets a newer ABI than the host ---- */
    len = build_pkg(pkg, sizeof(pkg), (cu16)(CAPP_ABI_VERSION + 1), "tplug", NULL, 0);
    CHECK(len > 0);
    CHECK_EQI(capp_launch_image(pkg, (cu32)len), CE_UNSUPPORTED);

    /* ---- rejection: plugin descriptor targets a newer ABI ---- */
    CHECK(capp_register_builtin("future", tp_future_entry));
    len = build_pkg(pkg, sizeof(pkg), CAPP_ABI_VERSION, "future", NULL, 0);
    CHECK(len > 0);
    CHECK_EQI(capp_launch_image(pkg, (cu32)len), CE_UNSUPPORTED);
    CHECK_EQI(capp_running_count(), 0);

    /* Clean up loader state so later suites start fresh. */
    capp_loader_set_config(NULL);
    capp_clear_builtins();
    remove("build/test_capp.ini");
}
