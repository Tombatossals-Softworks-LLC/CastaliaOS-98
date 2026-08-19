/*
 * cfg.h - Layer 2 configuration service (repairable INI).
 *
 * Per the Project Bible, settings live in transparent, hand-editable INI
 * files so the system can be repaired from DOS with a text editor. This is
 * a small, forgiving INI reader/writer:
 *   - [Section] headers, key=value pairs.
 *   - ';' and '#' start comments.
 *   - Whitespace around keys/values is trimmed.
 *   - Unknown/garbage lines are skipped, not fatal (repairability).
 *   - Values are stored in insertion order and rewritten stably.
 *
 * Sizes are bounded; there is no unbounded allocation per line.
 */
#ifndef CASTALIA_CFG_H
#define CASTALIA_CFG_H

#include "castalia/ctypes.h"

#define CFG_MAX_KEY   48
#define CFG_MAX_VALUE 192
#define CFG_MAX_SECTION 48

typedef struct CfgFile CfgFile; /* opaque */

/* Create an empty config in memory. Returns NULL on OOM. */
CfgFile *cfg_new(void);

/* Load an INI file. Missing file is not an error: returns an empty CfgFile
 * so callers can proceed with defaults and later save. On a hard I/O error
 * returns NULL. 'out_existed' (may be NULL) is set to CTRUE if the file was
 * present and read. */
CfgFile *cfg_load(const char *path, cbool *out_existed);

/* Persist to 'path'. Writes atomically where the platform allows (temp +
 * rename). Returns CE_OK or an error code. */
CResult cfg_save(CfgFile *cfg, const char *path);

/* Free all memory. Safe on NULL. */
void cfg_free(CfgFile *cfg);

/* ---- Typed getters. All return a default when the key is absent. ------ */
const char *cfg_get_str(CfgFile *cfg, const char *section,
                        const char *key, const char *def);
long        cfg_get_int(CfgFile *cfg, const char *section,
                        const char *key, long def);
cbool       cfg_get_bool(CfgFile *cfg, const char *section,
                         const char *key, cbool def);
/* Parse "#RRGGBB" or "RRGGBB" into a CColor (0x00RRGGBB). Returns def on a
 * malformed value. */
CColor      cfg_get_color(CfgFile *cfg, const char *section,
                          const char *key, CColor def);

/* ---- Setters (create section/key if needed). ------------------------- */
CResult cfg_set_str(CfgFile *cfg, const char *section,
                    const char *key, const char *value);
CResult cfg_set_int(CfgFile *cfg, const char *section,
                    const char *key, long value);
CResult cfg_set_bool(CfgFile *cfg, const char *section,
                     const char *key, cbool value);
/* Write a color as "#RRGGBB" -- the counterpart of cfg_get_color, so a value
 * written here reads back identically. */
CResult cfg_set_color(CfgFile *cfg, const char *section,
                      const char *key, CColor value);

/* Does a key exist? */
cbool cfg_has(CfgFile *cfg, const char *section, const char *key);

/* ---- Enumeration (for the Control Center / diagnostics). ------------- */
int cfg_section_count(CfgFile *cfg);
const char *cfg_section_name(CfgFile *cfg, int index);
int cfg_key_count(CfgFile *cfg, const char *section);
/* Returns the key name and, via out_value, its value for a given index. */
const char *cfg_key_at(CfgFile *cfg, const char *section, int index,
                       const char **out_value);

#endif /* CASTALIA_CFG_H */
