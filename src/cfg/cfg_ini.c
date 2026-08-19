/*
 * cfg_ini.c - Forgiving, repairable INI reader/writer.
 *
 * Design goals (from the Project Bible): settings must be editable from DOS
 * with a text editor and survive corruption gracefully. So:
 *   - Unparseable lines are skipped, never fatal.
 *   - A missing file loads as empty (defaults win, we can save later).
 *   - Values keep insertion order and are rewritten stably.
 *   - Everything is bounded; no per-line unbounded allocation.
 *
 * Uses stdio directly so the parser is testable on the host without the
 * platform layer; under Open Watcom, stdio maps onto INT 21h file services.
 */
#include "castalia/cfg.h"
#include "castalia/sys.h"

#include <stdio.h>

typedef struct CfgPair {
    char             key[CFG_MAX_KEY];
    char             value[CFG_MAX_VALUE];
    struct CfgPair  *next;
} CfgPair;

typedef struct CfgSection {
    char                name[CFG_MAX_SECTION];
    CfgPair            *first;
    CfgPair            *last;
    int                 count;
    struct CfgSection  *next;
} CfgSection;

struct CfgFile {
    CfgSection *first;
    CfgSection *last;
    int         count;
};

/* ---- construction / teardown ----------------------------------------- */

CfgFile *cfg_new(void)
{
    CfgFile *cfg = (CfgFile *)sys_calloc(1, (cu32)sizeof(CfgFile));
    return cfg; /* NULL on OOM */
}

void cfg_free(CfgFile *cfg)
{
    CfgSection *s;
    if (cfg == NULL) { return; }
    s = cfg->first;
    while (s != NULL) {
        CfgSection *nexts = s->next;
        CfgPair *p = s->first;
        while (p != NULL) {
            CfgPair *nextp = p->next;
            sys_free(p, (cu32)sizeof(CfgPair));
            p = nextp;
        }
        sys_free(s, (cu32)sizeof(CfgSection));
        s = nexts;
    }
    sys_free(cfg, (cu32)sizeof(CfgFile));
}

/* ---- internal lookup / insert ---------------------------------------- */

static CfgSection *find_section(CfgFile *cfg, const char *name)
{
    CfgSection *s;
    if (name == NULL) { name = ""; }
    for (s = cfg->first; s != NULL; s = s->next) {
        if (sys_stricmp(s->name, name) == 0) { return s; }
    }
    return NULL;
}

static CfgSection *ensure_section(CfgFile *cfg, const char *name)
{
    CfgSection *s = find_section(cfg, name);
    if (s != NULL) { return s; }
    s = (CfgSection *)sys_calloc(1, (cu32)sizeof(CfgSection));
    if (s == NULL) { return NULL; }
    sys_strlcpy(s->name, (name ? name : ""), sizeof(s->name));
    if (cfg->last != NULL) { cfg->last->next = s; } else { cfg->first = s; }
    cfg->last = s;
    cfg->count++;
    return s;
}

static CfgPair *find_pair(CfgSection *s, const char *key)
{
    CfgPair *p;
    for (p = s->first; p != NULL; p = p->next) {
        if (sys_stricmp(p->key, key) == 0) { return p; }
    }
    return NULL;
}

static CResult set_pair(CfgFile *cfg, const char *section,
                        const char *key, const char *value)
{
    CfgSection *s;
    CfgPair    *p;
    if (cfg == NULL || key == NULL) { return CE_INVALID; }
    s = ensure_section(cfg, section);
    if (s == NULL) { return CE_NOMEM; }
    p = find_pair(s, key);
    if (p == NULL) {
        p = (CfgPair *)sys_calloc(1, (cu32)sizeof(CfgPair));
        if (p == NULL) { return CE_NOMEM; }
        sys_strlcpy(p->key, key, sizeof(p->key));
        if (s->last != NULL) { s->last->next = p; } else { s->first = p; }
        s->last = p;
        s->count++;
    }
    sys_strlcpy(p->value, (value ? value : ""), sizeof(p->value));
    return CE_OK;
}

/* ---- parsing --------------------------------------------------------- */

CfgFile *cfg_load(const char *path, cbool *out_existed)
{
    CfgFile *cfg = cfg_new();
    FILE    *f;
    char     line[256];
    char     cur_section[CFG_MAX_SECTION];

    if (out_existed != NULL) { *out_existed = CFALSE; }
    if (cfg == NULL) { return NULL; }

    cur_section[0] = '\0';
    f = fopen((path ? path : ""), "r");
    if (f == NULL) {
        /* Not an error: proceed with an empty config. */
        return cfg;
    }
    if (out_existed != NULL) { *out_existed = CTRUE; }

    while (fgets(line, (int)sizeof(line), f) != NULL) {
        char *s = line;
        /* Strip a trailing newline. */
        char *nl = s;
        while (*nl != '\0') {
            if (*nl == '\n' || *nl == '\r') { *nl = '\0'; break; }
            nl++;
        }
        sys_strtrim(s);
        if (s[0] == '\0' || s[0] == ';' || s[0] == '#') {
            continue; /* blank or comment */
        }
        if (s[0] == '[') {
            /* Section header: [name]. Tolerate a missing ']'. */
            char *end = s + 1;
            char *w = cur_section;
            cu32 room = (cu32)sizeof(cur_section) - 1;
            while (*end != '\0' && *end != ']') {
                if (room > 0) { *w++ = *end; room--; }
                end++;
            }
            *w = '\0';
            sys_strtrim(cur_section);
            continue;
        }
        /* key = value */
        {
            char *eq = s;
            while (*eq != '\0' && *eq != '=') { eq++; }
            if (*eq == '=') {
                char key[CFG_MAX_KEY];
                *eq = '\0';
                sys_strtrim(s);
                sys_strtrim(eq + 1);
                sys_strlcpy(key, s, sizeof(key));
                if (key[0] != '\0') {
                    set_pair(cfg, cur_section, key, eq + 1);
                }
            }
            /* Lines without '=' are ignored (repairability). */
        }
    }
    fclose(f);
    return cfg;
}

/* Write to a temp file then rename, so an interrupted save cannot corrupt
 * the existing config. Falls back to a direct write if rename is unsupported. */
CResult cfg_save(CfgFile *cfg, const char *path)
{
    char  tmp[264];
    FILE *f;
    CfgSection *s;

    if (cfg == NULL || path == NULL) { return CE_INVALID; }

    sys_strlcpy(tmp, path, sizeof(tmp));
    sys_strlcat(tmp, ".tmp", sizeof(tmp));

    f = fopen(tmp, "w");
    if (f == NULL) {
        /* Try writing directly as a fallback. */
        f = fopen(path, "w");
        if (f == NULL) { return CE_IO; }
        tmp[0] = '\0';
    }

    fprintf(f, "; CastaliaOS 98 PE configuration\n");
    fprintf(f, "; This file is safe to edit with any text editor.\n\n");
    for (s = cfg->first; s != NULL; s = s->next) {
        CfgPair *p;
        if (s->name[0] != '\0') {
            fprintf(f, "[%s]\n", s->name);
        }
        for (p = s->first; p != NULL; p = p->next) {
            fprintf(f, "%s=%s\n", p->key, p->value);
        }
        fprintf(f, "\n");
    }
    if (fclose(f) != 0) { return CE_IO; }

    if (tmp[0] != '\0') {
        remove(path);           /* rename() fails if target exists on DOS */
        if (rename(tmp, path) != 0) {
            return CE_IO;
        }
    }
    return CE_OK;
}

/* ---- getters --------------------------------------------------------- */

const char *cfg_get_str(CfgFile *cfg, const char *section,
                        const char *key, const char *def)
{
    CfgSection *s;
    CfgPair    *p;
    if (cfg == NULL) { return def; }
    s = find_section(cfg, section);
    if (s == NULL) { return def; }
    p = find_pair(s, key);
    if (p == NULL) { return def; }
    return p->value;
}

static long parse_long(const char *s, long def)
{
    long sign = 1;
    long v = 0;
    cbool any = CFALSE;
    if (s == NULL) { return def; }
    while (*s == ' ' || *s == '\t') { s++; }
    if (*s == '+') { s++; }
    else if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        any = CTRUE;
        s++;
    }
    if (!any) { return def; }
    return v * sign;
}

long cfg_get_int(CfgFile *cfg, const char *section, const char *key, long def)
{
    const char *v = cfg_get_str(cfg, section, key, NULL);
    if (v == NULL) { return def; }
    return parse_long(v, def);
}

cbool cfg_get_bool(CfgFile *cfg, const char *section, const char *key, cbool def)
{
    const char *v = cfg_get_str(cfg, section, key, NULL);
    if (v == NULL) { return def; }
    if (sys_stricmp(v, "1") == 0 || sys_stricmp(v, "true") == 0 ||
        sys_stricmp(v, "yes") == 0 || sys_stricmp(v, "on") == 0) {
        return CTRUE;
    }
    if (sys_stricmp(v, "0") == 0 || sys_stricmp(v, "false") == 0 ||
        sys_stricmp(v, "no") == 0 || sys_stricmp(v, "off") == 0) {
        return CFALSE;
    }
    return def;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

CColor cfg_get_color(CfgFile *cfg, const char *section, const char *key, CColor def)
{
    const char *v = cfg_get_str(cfg, section, key, NULL);
    int h[6];
    int i;
    if (v == NULL) { return def; }
    if (*v == '#') { v++; }
    for (i = 0; i < 6; i++) {
        int hv = hexval((unsigned char)v[i]);
        if (hv < 0) { return def; }
        h[i] = hv;
    }
    /* Ensure exactly 6 hex digits consumed (7th char must terminate). */
    if (v[6] != '\0' && v[6] != ' ' && v[6] != '\t') { return def; }
    return (CColor)(((cu32)(h[0] * 16 + h[1]) << 16) |
                    ((cu32)(h[2] * 16 + h[3]) << 8)  |
                     (cu32)(h[4] * 16 + h[5]));
}

/* ---- setters --------------------------------------------------------- */

CResult cfg_set_str(CfgFile *cfg, const char *section, const char *key,
                    const char *value)
{
    return set_pair(cfg, section, key, value);
}

CResult cfg_set_int(CfgFile *cfg, const char *section, const char *key, long value)
{
    char buf[24];
    sys_snprintf(buf, sizeof(buf), "%ld", value);
    return set_pair(cfg, section, key, buf);
}

CResult cfg_set_bool(CfgFile *cfg, const char *section, const char *key, cbool value)
{
    return set_pair(cfg, section, key, value ? "1" : "0");
}

CResult cfg_set_color(CfgFile *cfg, const char *section, const char *key,
                      CColor value)
{
    char buf[10];
    sys_snprintf(buf, sizeof(buf), "#%02X%02X%02X",
                 (int)((value >> 16) & 0xFF), (int)((value >> 8) & 0xFF),
                 (int)(value & 0xFF));
    return cfg_set_str(cfg, section, key, buf);
}

cbool cfg_has(CfgFile *cfg, const char *section, const char *key)
{
    CfgSection *s;
    if (cfg == NULL) { return CFALSE; }
    s = find_section(cfg, section);
    if (s == NULL) { return CFALSE; }
    return find_pair(s, key) != NULL;
}

/* ---- enumeration ----------------------------------------------------- */

int cfg_section_count(CfgFile *cfg)
{
    return (cfg != NULL) ? cfg->count : 0;
}

const char *cfg_section_name(CfgFile *cfg, int index)
{
    CfgSection *s;
    int i = 0;
    if (cfg == NULL) { return NULL; }
    for (s = cfg->first; s != NULL; s = s->next) {
        if (i == index) { return s->name; }
        i++;
    }
    return NULL;
}

int cfg_key_count(CfgFile *cfg, const char *section)
{
    CfgSection *s;
    if (cfg == NULL) { return 0; }
    s = find_section(cfg, section);
    return (s != NULL) ? s->count : 0;
}

const char *cfg_key_at(CfgFile *cfg, const char *section, int index,
                       const char **out_value)
{
    CfgSection *s;
    CfgPair    *p;
    int i = 0;
    if (out_value != NULL) { *out_value = NULL; }
    if (cfg == NULL) { return NULL; }
    s = find_section(cfg, section);
    if (s == NULL) { return NULL; }
    for (p = s->first; p != NULL; p = p->next) {
        if (i == index) {
            if (out_value != NULL) { *out_value = p->value; }
            return p->key;
        }
        i++;
    }
    return NULL;
}
