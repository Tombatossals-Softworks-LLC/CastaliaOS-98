/*
 * cboot.c - CBOOT.EXE: the safe launcher that AUTOEXEC.BAT calls.
 *
 * CBOOT is a tiny text-mode gatekeeper in front of the graphical shell:
 *   - Reads the boot-dirty flag left by a crashed session and, after two
 *     unclean boots, offers Safe Mode (Bible: "If boot fails twice, offer
 *     safe mode").
 *   - Honors an explicit Safe Mode request: `CBOOT /safe`, or a SafeMode line
 *     in CASTALIA.INI.
 *   - Verifies the shell executable exists before launching.
 *   - Launches CASTALIA.EXE (optionally with --safe) and passes its exit
 *     code back to DOS.
 *
 * IMPORTANT: CBOOT runs after AUTOEXEC.BAT does `CD \CASTALIA\BIN`, but the
 * shell writes its state (boot-dirty flag) under CASTALIA_HOME and the config
 * lives at CASTALIA_HOME\SYS. CBOOT therefore resolves ALL of its state files
 * from CASTALIA_HOME too -- not relative to the current directory -- so the two
 * halves agree on the same absolute files. (An earlier version used paths
 * relative to BIN, which silently defeated the crash / Safe Mode flow.)
 *
 * Dependency-light and repairable. Built for DOS with Open Watcom; the launch
 * step uses the C runtime's system(), so its logic is testable on a host too.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CB_MAX_FAILS   2
#define CB_PATH_MAX    260

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* Join home + "\\" + rel into dst (bounded, always terminated). */
static void join_home(char *dst, size_t dstsz, const char *home, const char *rel)
{
    if (dstsz == 0) { return; }
    snprintf(dst, dstsz, "%s\\%s", home, rel);
    dst[dstsz - 1] = '\0';
}

static int read_fail_count(const char *path)
{
    int n = 0;
    FILE *f = fopen(path, "r");
    if (f) { if (fscanf(f, "%d", &n) != 1) { n = 0; } fclose(f); }
    return n;
}

static void write_fail_count(const char *path, int n)
{
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%d\n", n); fclose(f); }
}

/* Very small INI probe: is "SafeMode" set truthy under [Boot]? Kept trivial so
 * CBOOT stays tiny; the full INI service lives in the shell. */
static int ini_wants_safe(const char *ini_path)
{
    FILE *f = fopen(ini_path, "r");
    char line[128];
    int in_boot = 0, want = 0;
    if (!f) { return 0; }
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') { p++; }
        if (*p == '[') { in_boot = (strncmp(p, "[Boot]", 6) == 0); continue; }
        if (in_boot) {
            if (strncmp(p, "SafeMode", 8) == 0 || strncmp(p, "safemode", 8) == 0) {
                if (strchr(p, '1') || strstr(p, "true") || strstr(p, "yes") ||
                    strstr(p, "TRUE") || strstr(p, "YES")) {
                    want = 1;
                }
            }
        }
    }
    fclose(f);
    return want;
}

int main(int argc, char **argv)
{
    const char *home;
    char  shell_path[CB_PATH_MAX];
    char  dirty_path[CB_PATH_MAX];
    char  failc_path[CB_PATH_MAX];
    char  ini_path[CB_PATH_MAX];
    char  cmd[CB_PATH_MAX + 16];
    int   safe = 0;
    int   fails;
    int   rc;
    int   i;

    printf("CastaliaOS 98 PE - CBOOT launcher\n");
    printf("---------------------------------\n");

    /* Resolve everything from CASTALIA_HOME (set by AUTOEXEC.BAT). Default to
     * the conventional install root if it is somehow unset. */
    home = getenv("CASTALIA_HOME");
    if (home == NULL || home[0] == '\0') { home = "C:\\CASTALIA"; }
    join_home(shell_path, sizeof(shell_path), home, "BIN\\CASTALIA.EXE");
    join_home(dirty_path, sizeof(dirty_path), home, "SYS\\bootdirty.flg");
    join_home(failc_path, sizeof(failc_path), home, "SYS\\bootfail.cnt");
    join_home(ini_path,   sizeof(ini_path),   home, "SYS\\CASTALIA.INI");

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "/safe") == 0 || strcmp(argv[i], "-safe") == 0 ||
            strcmp(argv[i], "--safe") == 0) {
            safe = 1;
        }
    }

    /* Repeated unclean boots -> force Safe Mode. */
    if (file_exists(dirty_path)) {
        fails = read_fail_count(failc_path) + 1;
        write_fail_count(failc_path, fails);
        printf("Notice: the previous session did not shut down cleanly (%d).\n",
               fails);
        if (fails >= CB_MAX_FAILS) {
            printf("Starting in SAFE MODE to allow repair.\n");
            safe = 1;
        }
    } else {
        write_fail_count(failc_path, 0);
    }

    if (!safe && ini_wants_safe(ini_path)) {
        printf("CASTALIA.INI requests Safe Mode.\n");
        safe = 1;
    }

    if (!file_exists(shell_path)) {
        fprintf(stderr, "ERROR: %s not found. Cannot start the desktop.\n",
                shell_path);
        fprintf(stderr, "Run the installer or see docs/RECOVERY.md.\n");
        return 1;
    }

    if (safe) {
        printf("Launching desktop (Safe Mode)...\n");
        snprintf(cmd, sizeof(cmd), "%s --safe", shell_path);
    } else {
        printf("Launching desktop...\n");
        snprintf(cmd, sizeof(cmd), "%s", shell_path);
    }
    cmd[sizeof(cmd) - 1] = '\0';

    rc = system(cmd);

    /* A clean shell exit removes the dirty flag itself; reset the counter. */
    if (!file_exists(dirty_path)) { write_fail_count(failc_path, 0); }

    if (rc != 0) {
        fprintf(stderr, "Desktop exited with code %d. See CASTALIA\\LOGS.\n", rc);
    }
    return rc;
}
