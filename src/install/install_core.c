/*
 * install_core.c - Portable installer/uninstaller core (see install.h).
 *
 * Only the directory primitives (mkdir/rmdir/enumerate) differ per platform:
 *   - CASTALIA_DOS: Open Watcom <direct.h> + <dos.h> _dos_find* .
 *   - host:         POSIX <sys/stat.h> + <dirent.h>.
 * Everything else -- backups, the managed boot block, the recursive copy and
 * delete drivers -- is shared portable C89 and is exercised by the host unit
 * tests (tests/test_install.c).
 */
#include "castalia/install.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Path separator differs by platform so the host tests can use POSIX roots. */
#ifdef CASTALIA_DOS
#  include <direct.h>   /* mkdir, rmdir                                      */
#  include <dos.h>      /* _dos_findfirst / _dos_findnext / struct find_t    */
#  define INST_SEP '\\'
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <dirent.h>
#  include <errno.h>
#  include <unistd.h>   /* rmdir */
#  define INST_SEP '/'
#endif

#define INST_PATH_MAX 320

/* The standard installed subdirectory layout (mirrors dist/cdroot/CASTALIA). */
static const char *const INST_SUBDIRS[] = {
    "BIN", "SYS", "LOGS", "THEMES", "ICONS", "FONTS",
    "HELP", "APPS", "DRV", "TEMP", "TRASH"
};
#define INST_SUBDIR_COUNT ((int)(sizeof(INST_SUBDIRS) / sizeof(INST_SUBDIRS[0])))

/* The subset of the layout that belongs to the user rather than to the media.
 * PHOTOS and DOCS are not in INST_SUBDIRS because the shell creates them on
 * demand; they are listed here because that makes them no less the user's. */
static const char *const INST_USER_DIRS[] = {
    "SYS", "LOGS", "TEMP", "TRASH", "THEMES", "PHOTOS", "DOCS"
};
#define INST_USER_DIR_COUNT \
    ((int)(sizeof(INST_USER_DIRS) / sizeof(INST_USER_DIRS[0])))

/* ---------------------------------------------------------------------- */
/* Small path + file helpers                                              */
/* ---------------------------------------------------------------------- */

static void inst_join(char *dst, size_t dstsz, const char *a, const char *b)
{
    size_t n;
    if (dstsz == 0) { return; }
    dst[0] = '\0';
    n = strlen(a);
    if (n >= dstsz) { n = dstsz - 1; }
    memcpy(dst, a, n);
    dst[n] = '\0';
    /* Add a separator unless 'a' already ends in one. */
    if (n > 0 && dst[n - 1] != '\\' && dst[n - 1] != '/') {
        if (n + 1 < dstsz) { dst[n] = INST_SEP; dst[n + 1] = '\0'; n++; }
    }
    if (b != NULL && *b != '\0') {
        size_t m = strlen(b);
        if (n + m >= dstsz) { m = dstsz - 1 - n; }
        memcpy(dst + n, b, m);
        dst[n + m] = '\0';
    }
}

static int inst_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* Case-insensitive compare of exactly n characters. C89 has no strncasecmp,
 * and Watcom's stricmp is not portable back to the host build. */
static int inst_ci_eq_n(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) { return 0; }
        if (ca == '\0') { return 1; }
    }
    return 1;
}

int install_is_user_state(const char *rel)
{
    const char *p;
    size_t n;
    int i;

    if (rel == NULL) { return 0; }
    while (*rel == '\\' || *rel == '/') { rel++; }

    /* The first component, and only the first: everything below SYS\ is as
     * much the user's as SYS\CASTALIA.INI is. */
    p = rel;
    while (*p != '\0' && *p != '\\' && *p != '/') { p++; }
    n = (size_t)(p - rel);
    if (n == 0) { return 0; }

    for (i = 0; i < INST_USER_DIR_COUNT; i++) {
        /* Compare the WHOLE component. Matching a prefix would make SYSTEM\
         * the user's because SYS is, and a name is not a namespace. */
        if (strlen(INST_USER_DIRS[i]) == n &&
            inst_ci_eq_n(rel, INST_USER_DIRS[i], n)) {
            return 1;
        }
    }
    return 0;
}

int install_copy_file(const char *src, const char *dst)
{
    FILE *in, *out;
    char buf[4096];
    size_t n;

    in = fopen(src, "rb");
    if (in == NULL) { return INST_ERR_IO; }
    out = fopen(dst, "wb");
    if (out == NULL) { fclose(in); return INST_ERR_IO; }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in); fclose(out); return INST_ERR_IO;
        }
    }
    fclose(in);
    if (fclose(out) != 0) { return INST_ERR_IO; }
    return INST_OK;
}

/* ---------------------------------------------------------------------- */
/* Platform directory primitives                                          */
/* ---------------------------------------------------------------------- */

/* Create one directory. Returns INST_OK whether it was created or already
 * present; negative on a real error. */
static int inst_mkdir_one(const char *path)
{
#ifdef CASTALIA_DOS
    /* 0 => created; nonzero almost always means "already exists", which is
     * fine. A genuinely un-creatable path surfaces later as a file-write
     * error, where we can report it precisely. */
    mkdir(path);
    return INST_OK;
#else
    if (mkdir(path, 0777) == 0) { return INST_OK; }
    if (errno == EEXIST) { return INST_OK; }
    return INST_ERR_IO;
#endif
}

static int inst_rmdir_one(const char *path)
{
#ifdef CASTALIA_DOS
    return (rmdir(path) == 0) ? INST_OK : INST_ERR_IO;
#else
    return (rmdir(path) == 0) ? INST_OK : INST_ERR_IO;
#endif
}

/*
 * Enumerate the immediate children of 'dir'. For each entry that is not "."
 * or "..", call fn(fullpath, is_dir, user). Stops and returns the first
 * negative code fn yields; otherwise INST_OK.
 */
typedef int (*inst_walk_fn)(const char *fullpath, int is_dir, void *user);

static int inst_walk_dir(const char *dir, inst_walk_fn fn, void *user)
{
#ifdef CASTALIA_DOS
    struct find_t fi;
    char pattern[INST_PATH_MAX];
    char full[INST_PATH_MAX];
    unsigned rc;

    inst_join(pattern, sizeof(pattern), dir, "*.*");
    rc = _dos_findfirst(pattern, _A_SUBDIR | _A_HIDDEN | _A_SYSTEM | _A_RDONLY,
                        &fi);
    while (rc == 0) {
        if (strcmp(fi.name, ".") != 0 && strcmp(fi.name, "..") != 0) {
            int is_dir = (fi.attrib & _A_SUBDIR) ? 1 : 0;
            int r;
            inst_join(full, sizeof(full), dir, fi.name);
            r = fn(full, is_dir, user);
            if (r < 0) { _dos_findclose(&fi); return r; }
        }
        rc = _dos_findnext(&fi);
    }
    _dos_findclose(&fi);
    return INST_OK;
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    if (d == NULL) { return INST_ERR_IO; }
    while ((e = readdir(d)) != NULL) {
        char full[INST_PATH_MAX];
        struct stat st;
        int is_dir;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        inst_join(full, sizeof(full), dir, e->d_name);
        if (stat(full, &st) != 0) { continue; }
        is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
        {
            int r = fn(full, is_dir, user);
            if (r < 0) { closedir(d); return r; }
        }
    }
    closedir(d);
    return INST_OK;
#endif
}

/* ---------------------------------------------------------------------- */
/* Tree creation                                                          */
/* ---------------------------------------------------------------------- */

int install_make_tree(const char *root)
{
    int i;
    char sub[INST_PATH_MAX];
    if (root == NULL || root[0] == '\0') { return INST_ERR_ARG; }
    if (inst_mkdir_one(root) < 0) { return INST_ERR_IO; }
    for (i = 0; i < INST_SUBDIR_COUNT; i++) {
        inst_join(sub, sizeof(sub), root, INST_SUBDIRS[i]);
        if (inst_mkdir_one(sub) < 0) { return INST_ERR_IO; }
    }
    return INST_OK;
}

/* ---------------------------------------------------------------------- */
/* Recursive copy                                                         */
/* ---------------------------------------------------------------------- */

/*
 * 'rel' is where we are relative to the destination ROOT, which is the only
 * thing install_is_user_state can be asked about -- the absolute destination
 * path cannot answer it, because the install root may be called anything.
 */
typedef struct {
    const char *dst;
    const char *rel;       /* "" at the root                                */
    int         keep_user; /* leave a file the user owns exactly as it is   */
} CopyCtx;

static int inst_copy_tree_rel(const char *src, const char *dst,
                              const char *rel, int keep_user);

static int copy_entry(const char *src_full, int is_dir, void *user)
{
    CopyCtx *ctx = (CopyCtx *)user;
    const char *name = src_full;
    const char *p;
    char dst_full[INST_PATH_MAX];
    char child_rel[INST_PATH_MAX];

    /* basename of src_full */
    for (p = src_full; *p; p++) {
        if (*p == '\\' || *p == '/') { name = p + 1; }
    }
    inst_join(dst_full, sizeof(dst_full), ctx->dst, name);

    if (ctx->rel[0] == '\0') {
        size_t m = strlen(name);
        if (m >= sizeof(child_rel)) { m = sizeof(child_rel) - 1; }
        memcpy(child_rel, name, m);
        child_rel[m] = '\0';
    } else {
        inst_join(child_rel, sizeof(child_rel), ctx->rel, name);
    }

    if (is_dir) {
        return inst_copy_tree_rel(src_full, dst_full, child_rel,
                                  ctx->keep_user);
    }
    /* Absent is not the same as theirs: a first install has no CASTALIA.INI
     * yet, and refusing to write one would leave the desktop with no config
     * at all. Only an EXISTING user file is protected. */
    if (ctx->keep_user && install_is_user_state(child_rel) &&
        inst_exists(dst_full)) {
        return INST_OK;
    }
    return install_copy_file(src_full, dst_full);
}

static int inst_copy_tree_rel(const char *src, const char *dst,
                              const char *rel, int keep_user)
{
    CopyCtx ctx;
    if (src == NULL || dst == NULL || src[0] == '\0' || dst[0] == '\0') {
        return INST_ERR_ARG;
    }
    if (inst_mkdir_one(dst) < 0) { return INST_ERR_IO; }
    ctx.dst = dst;
    ctx.rel = rel;
    ctx.keep_user = keep_user;
    return inst_walk_dir(src, copy_entry, &ctx);
}

int install_copy_tree(const char *src, const char *dst)
{
    return inst_copy_tree_rel(src, dst, "", 0);
}

int install_copy_tree_keep_user(const char *src, const char *dst)
{
    return inst_copy_tree_rel(src, dst, "", 1);
}

/* ---------------------------------------------------------------------- */
/* Recursive delete                                                       */
/* ---------------------------------------------------------------------- */

static int inst_dangerous_root(const char *root)
{
    size_t n;
    if (root == NULL) { return 1; }
    n = strlen(root);
    if (n == 0) { return 1; }
    if (strcmp(root, "/") == 0 || strcmp(root, "\\") == 0) { return 1; }
    /* A bare drive: "C:", "C:\", "C:/" -- never wipe a whole drive. */
    if (n <= 3 && n >= 2 && root[1] == ':') { return 1; }
    return 0;
}

static int delete_entry(const char *full, int is_dir, void *user)
{
    (void)user;
    if (is_dir) {
        int r = install_remove_tree(full);
        if (r < 0) { return r; }
        return INST_OK;
    }
    return (remove(full) == 0) ? INST_OK : INST_ERR_IO;
}

int install_remove_tree(const char *root)
{
    int r;
    if (inst_dangerous_root(root)) { return INST_ERR_REFUSED; }
    if (!inst_exists(root)) {
        /* Might still be a directory that fopen() can't open; try to walk it,
         * and if the walk fails treat as "nothing to remove". */
    }
    r = inst_walk_dir(root, delete_entry, NULL);
    if (r < 0 && r != INST_ERR_IO) { return r; }
    /* Remove the (now-empty) directory itself. Ignore failure if it's gone. */
    inst_rmdir_one(root);
    return INST_OK;
}

/* ---------------------------------------------------------------------- */
/* Boot-file backup / restore                                             */
/* ---------------------------------------------------------------------- */

int install_backup_file(const char *src)
{
    char backup[INST_PATH_MAX];
    if (src == NULL || src[0] == '\0') { return INST_ERR_ARG; }
    if (!inst_exists(src)) { return INST_OK; } /* nothing to back up */

    if ((size_t)snprintf(backup, sizeof(backup), "%s%s", src,
                         INST_BACKUP_SUFFIX) >= sizeof(backup)) {
        return INST_ERR_ARG;
    }
    /* Keep the first, pristine backup: never overwrite an existing one. */
    if (inst_exists(backup)) { return INST_OK; }
    return install_copy_file(src, backup);
}

int install_restore_file(const char *path)
{
    char backup[INST_PATH_MAX];
    if (path == NULL || path[0] == '\0') { return INST_ERR_ARG; }
    if ((size_t)snprintf(backup, sizeof(backup), "%s%s", path,
                         INST_BACKUP_SUFFIX) >= sizeof(backup)) {
        return INST_ERR_ARG;
    }
    if (!inst_exists(backup)) { return INST_OK; } /* nothing to restore */
    return install_copy_file(backup, path);
}

/* ---------------------------------------------------------------------- */
/* Managed boot block                                                     */
/* ---------------------------------------------------------------------- */

/* Does the file contain the BEGIN marker? */
static int has_boot_block(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    int found = 0;
    if (f == NULL) { return 0; }
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, INST_BLOCK_BEGIN) != NULL) { found = 1; break; }
    }
    fclose(f);
    return found;
}

int install_ensure_boot_block(const char *autoexec_path,
                              const char *install_root)
{
    FILE *f;
    if (autoexec_path == NULL || autoexec_path[0] == '\0' ||
        install_root == NULL || install_root[0] == '\0') {
        return INST_ERR_ARG;
    }
    if (has_boot_block(autoexec_path)) { return INST_OK; } /* idempotent */

    /* Append (create if missing) -- we never rewrite the user's lines above. */
    f = fopen(autoexec_path, "a");
    if (f == NULL) { return INST_ERR_IO; }
    fprintf(f, "\n%s\n", INST_BLOCK_BEGIN);
    fprintf(f, "SET CASTALIA_HOME=%s\n", install_root);
    fprintf(f, "SET PATH=%s\\BIN;%%PATH%%\n", install_root);
    fprintf(f, "SET DOS4GVM=1\n");
    fprintf(f, "CD %s\\BIN\n", install_root);
    fprintf(f, "REM Load a DOS mouse driver here for INT 33h (e.g. CTMOUSE.EXE /P).\n");
    fprintf(f, "CBOOT.EXE\n");
    fprintf(f, "CD \\\n");
    fprintf(f, "%s\n", INST_BLOCK_END);
    if (fclose(f) != 0) { return INST_ERR_IO; }
    return INST_OK;
}

int install_strip_boot_block(const char *autoexec_path)
{
    FILE *in, *out;
    char tmp[INST_PATH_MAX];
    char line[256];
    int in_block = 0;

    if (autoexec_path == NULL || autoexec_path[0] == '\0') {
        return INST_ERR_ARG;
    }
    if (!has_boot_block(autoexec_path)) { return INST_OK; }

    if ((size_t)snprintf(tmp, sizeof(tmp), "%s.CBT", autoexec_path)
            >= sizeof(tmp)) {
        return INST_ERR_ARG;
    }
    in = fopen(autoexec_path, "r");
    if (in == NULL) { return INST_ERR_IO; }
    out = fopen(tmp, "w");
    if (out == NULL) { fclose(in); return INST_ERR_IO; }

    while (fgets(line, sizeof(line), in)) {
        if (!in_block && strstr(line, INST_BLOCK_BEGIN) != NULL) {
            in_block = 1;
            continue;
        }
        if (in_block) {
            if (strstr(line, INST_BLOCK_END) != NULL) { in_block = 0; }
            continue; /* drop every line inside the block, markers included */
        }
        fputs(line, out);
    }
    fclose(in);
    if (fclose(out) != 0) { remove(tmp); return INST_ERR_IO; }

    /* Replace the original with the stripped copy. */
    if (remove(autoexec_path) != 0) { remove(tmp); return INST_ERR_IO; }
    if (rename(tmp, autoexec_path) != 0) { return INST_ERR_IO; }
    return INST_OK;
}

/* ---------------------------------------------------------------------- */
/* High-level flows                                                       */
/* ---------------------------------------------------------------------- */

static void inst_say(const InstallOpts *o, const char *msg)
{
    if (o->verbose) { printf("  %s\n", msg); }
}

int install_is_present(const InstallOpts *o)
{
    char path[INST_PATH_MAX];
    char dir[INST_PATH_MAX];

    if (o == NULL) { return 0; }
    if (o->install_root != NULL && o->install_root[0] != '\0') {
        inst_join(dir, sizeof(dir), o->install_root, "SYS");
        inst_join(path, sizeof(path), dir, "CASTALIA.INI");
        if (inst_exists(path)) { return 1; }
        inst_join(dir, sizeof(dir), o->install_root, "BIN");
        inst_join(path, sizeof(path), dir, "CBOOT.EXE");
        if (inst_exists(path)) { return 1; }
    }
    if (o->sys_root != NULL) {
        inst_join(path, sizeof(path), o->sys_root, "AUTOEXEC.BAT");
        if (has_boot_block(path)) { return 1; }
    }
    return 0;
}

int install_run(const InstallOpts *o)
{
    return install_run_mode(o, INST_MODE_INSTALL);
}

int install_run_mode(const InstallOpts *o, InstallMode mode)
{
    char autoexec[INST_PATH_MAX];
    char config[INST_PATH_MAX];
    int rc;

    if (o == NULL || o->install_root == NULL || o->install_root[0] == '\0') {
        return INST_ERR_ARG;
    }
    if (o->sys_root == NULL) { return INST_ERR_ARG; }

    /* An upgrade is the one mode that can be asked for something impossible,
     * so it is the one mode that checks before it touches anything. */
    if (mode == INST_MODE_UPGRADE) {
        if (o->src_root == NULL || o->src_root[0] == '\0') {
            return INST_ERR_ARG;
        }
        if (!install_is_present(o)) { return INST_ERR_NOTHING; }
    }

    inst_say(o, (mode == INST_MODE_INSTALL)
                ? "Creating the CASTALIA directory tree..."
                : "Checking the CASTALIA directory tree...");
    rc = install_make_tree(o->install_root);
    if (rc < 0) { return rc; }

    if (o->src_root != NULL && o->src_root[0] != '\0') {
        inst_say(o, (mode == INST_MODE_UPGRADE)
                    ? "Replacing program files; settings and documents kept..."
                    : "Copying files from installation media...");
        rc = install_copy_tree_keep_user(o->src_root, o->install_root);
        if (rc < 0) { return rc; }
    }

    inst_join(config,   sizeof(config),   o->sys_root, "CONFIG.SYS");
    inst_join(autoexec, sizeof(autoexec), o->sys_root, "AUTOEXEC.BAT");

    inst_say(o, "Backing up CONFIG.SYS and AUTOEXEC.BAT...");
    rc = install_backup_file(config);
    if (rc < 0) { return rc; }
    rc = install_backup_file(autoexec);
    if (rc < 0) { return rc; }

    inst_say(o, "Adding the CastaliaOS boot entry...");
    rc = install_ensure_boot_block(autoexec, o->install_root);
    if (rc < 0) { return rc; }

    return INST_OK;
}

int uninstall_run(const InstallOpts *o, int remove_tree)
{
    char autoexec[INST_PATH_MAX];
    int rc;

    if (o == NULL || o->sys_root == NULL) { return INST_ERR_ARG; }

    inst_join(autoexec, sizeof(autoexec), o->sys_root, "AUTOEXEC.BAT");

    /* Prefer restoring the pristine backup; if there is none, surgically strip
     * just our managed block so the user's other lines are preserved. */
    {
        char backup[INST_PATH_MAX + 8];
        snprintf(backup, sizeof(backup), "%s%s", autoexec, INST_BACKUP_SUFFIX);
        if (inst_exists(backup)) {
            inst_say(o, "Restoring AUTOEXEC.BAT from backup...");
            rc = install_restore_file(autoexec);
        } else {
            inst_say(o, "Removing the CastaliaOS boot entry...");
            rc = install_strip_boot_block(autoexec);
        }
        if (rc < 0) { return rc; }
    }

    /* CONFIG.SYS is only restored (we never appended to it, but a future build
     * might); restoring a nonexistent backup is a safe no-op. */
    {
        char config[INST_PATH_MAX];
        inst_join(config, sizeof(config), o->sys_root, "CONFIG.SYS");
        rc = install_restore_file(config);
        if (rc < 0) { return rc; }
    }

    if (remove_tree && o->install_root != NULL && o->install_root[0] != '\0') {
        inst_say(o, "Removing the installed files...");
        rc = install_remove_tree(o->install_root);
        if (rc < 0) { return rc; }
    }
    return INST_OK;
}
