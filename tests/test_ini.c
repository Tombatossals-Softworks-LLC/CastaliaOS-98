/*
 * test_ini.c - INI config: round-trip, typed getters, forgiving parse, and
 * color parsing. Writes to a temp file to exercise load/save.
 */
#include "ctest.h"
#include "castalia/cfg.h"

#include <stdio.h>

static const char *TMP = "build/test_cfg.ini";

void test_ini(void)
{
    CfgFile *cfg;
    cbool existed = CTRUE;

    printf("- cfg/ini\n");

    /* In-memory set/get + typed accessors. */
    cfg = cfg_new();
    CHECK(cfg != NULL);
    cfg_set_str(cfg, "Shell", "Theme", "Castalia Classic");
    cfg_set_int(cfg, "Video", "Width", 800);
    cfg_set_int(cfg, "Video", "Height", 600);
    cfg_set_bool(cfg, "Boot", "SafeMode", CFALSE);
    cfg_set_str(cfg, "Colors", "Accent", "#B68A2E");

    CHECK_STR(cfg_get_str(cfg, "Shell", "Theme", "?"), "Castalia Classic");
    CHECK_EQI(cfg_get_int(cfg, "Video", "Width", 0), 800);
    CHECK_EQI(cfg_get_bool(cfg, "Boot", "SafeMode", CTRUE), 0);
    CHECK(cfg_has(cfg, "Video", "Height"));
    CHECK(!cfg_has(cfg, "Video", "Depth"));

    /* Default fallbacks for missing keys. */
    CHECK_EQI(cfg_get_int(cfg, "Video", "Depth", 16), 16);
    CHECK_STR(cfg_get_str(cfg, "None", "Nope", "def"), "def");

    /* Color parse. */
    {
        CColor c = cfg_get_color(cfg, "Colors", "Accent", 0);
        CHECK_EQI(c, 0xB68A2EL);
        CHECK_EQI(cfg_get_color(cfg, "Colors", "Bad", 0x123456L), 0x123456L);
    }

    /* Save then reload -> values must survive. */
    CHECK_EQI(cfg_save(cfg, TMP), CE_OK);
    cfg_free(cfg);

    cfg = cfg_load(TMP, &existed);
    CHECK(cfg != NULL);
    CHECK(existed);
    CHECK_STR(cfg_get_str(cfg, "Shell", "Theme", "?"), "Castalia Classic");
    CHECK_EQI(cfg_get_int(cfg, "Video", "Height", 0), 600);
    cfg_free(cfg);

    /* Missing file loads empty, not NULL, so defaults can proceed. */
    cfg = cfg_load("build/does_not_exist.ini", &existed);
    CHECK(cfg != NULL);
    CHECK(!existed);
    CHECK_EQI(cfg_get_int(cfg, "X", "Y", 42), 42);
    cfg_free(cfg);

    remove(TMP);
}
