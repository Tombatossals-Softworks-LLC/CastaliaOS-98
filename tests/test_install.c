/*
 * test_install.c - Host unit tests for the installer core (install_core.c).
 *
 * These drive a full install -> uninstall round trip against a scratch tree
 * under build/, so the create-tree / backup / idempotent-boot-block /
 * strip / copy / remove logic is verified on the host and in CI -- the same
 * code path that runs on DOS, minus the platform mkdir/enumerate primitives.
 *
 * Host-only (run_tests is never built by the DOS makefile), so POSIX headers
 * are fine here. Path buffers are tiered (base < derived < backup) so the
 * concatenations stay provably in-bounds under -Wformat-truncation.
 */
#include "ctest.h"
#include "castalia/install.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define SCRATCH "build/tinst"

static void mk(const char *p) { mkdir(p, 0777); }

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f) { fputs(text, f); fclose(f); }
}

static void append_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "ab");
    if (f) { fputs(text, f); fclose(f); }
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static int dir_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
}

/* Whole-file substring occurrence count. */
static int count_occ(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    char buf[8192];
    size_t n, nl = strlen(needle);
    int count = 0;
    if (!f) { return 0; }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    if (nl == 0) { return 0; }
    {
        const char *p = buf;
        while ((p = strstr(p, needle)) != NULL) { count++; p += nl; }
    }
    return count;
}

static int file_contains(const char *path, const char *needle)
{
    return count_occ(path, needle) > 0;
}

void test_install(void)
{
    char instroot[200], sysroot[200], src[200], dst[200];
    char inst2[200], sys2[200];
    char autoexec[256], config[256], ae2[256], p[256];
    char backup[320];

    /* Clean slate. */
    install_remove_tree(SCRATCH);
    mk("build");
    mk(SCRATCH);

    /* ---- make_tree creates the standard layout ---- */
    snprintf(instroot, sizeof(instroot), "%s/CASTALIA", SCRATCH);
    CHECK_EQI(install_make_tree(instroot), INST_OK);
    CHECK(dir_exists(instroot));
    snprintf(p, sizeof(p), "%s/BIN",   instroot); CHECK(dir_exists(p));
    snprintf(p, sizeof(p), "%s/SYS",   instroot); CHECK(dir_exists(p));
    snprintf(p, sizeof(p), "%s/TRASH", instroot); CHECK(dir_exists(p));

    /* ---- backup keeps the first, pristine copy ---- */
    snprintf(sysroot, sizeof(sysroot), "%s/sysroot", SCRATCH);
    mk(sysroot);
    snprintf(autoexec, sizeof(autoexec), "%s/AUTOEXEC.BAT", sysroot);
    snprintf(config,   sizeof(config),   "%s/CONFIG.SYS",   sysroot);
    write_file(autoexec, "@ECHO OFF\nPROMPT $P$G\n");
    write_file(config,   "FILES=40\n");

    CHECK_EQI(install_backup_file(autoexec), INST_OK);
    snprintf(backup, sizeof(backup), "%s%s", autoexec, INST_BACKUP_SUFFIX);
    CHECK(file_exists(backup));
    /* Change the live file, re-run backup: the pristine backup must NOT move. */
    append_file(autoexec, "SET FOO=changed\n");
    CHECK_EQI(install_backup_file(autoexec), INST_OK);
    CHECK(!file_contains(backup, "changed"));

    /* ---- boot block is added exactly once (idempotent) ---- */
    CHECK_EQI(install_ensure_boot_block(autoexec, instroot), INST_OK);
    CHECK_EQI(count_occ(autoexec, INST_BLOCK_BEGIN), 1);
    CHECK(file_contains(autoexec, "CBOOT.EXE"));
    CHECK(file_contains(autoexec, "CASTALIA_HOME"));
    CHECK_EQI(install_ensure_boot_block(autoexec, instroot), INST_OK);
    CHECK_EQI(count_occ(autoexec, INST_BLOCK_BEGIN), 1); /* still one */
    CHECK(file_contains(autoexec, "PROMPT $P$G"));       /* user line kept */

    /* ---- strip removes exactly our block, preserving user lines ---- */
    CHECK_EQI(install_strip_boot_block(autoexec), INST_OK);
    CHECK_EQI(count_occ(autoexec, INST_BLOCK_BEGIN), 0);
    CHECK(!file_contains(autoexec, "CBOOT.EXE"));
    CHECK(file_contains(autoexec, "PROMPT $P$G"));
    /* stripping again is a no-op */
    CHECK_EQI(install_strip_boot_block(autoexec), INST_OK);

    /* ---- recursive copy ---- */
    snprintf(src, sizeof(src), "%s/src", SCRATCH);
    mk(src);
    snprintf(p, sizeof(p), "%s/HELLO.TXT", src); write_file(p, "hi");
    snprintf(p, sizeof(p), "%s/SUB", src);       mk(p);
    snprintf(p, sizeof(p), "%s/SUB/A.TXT", src); write_file(p, "aaa");
    snprintf(dst, sizeof(dst), "%s/dst", SCRATCH);
    CHECK_EQI(install_copy_tree(src, dst), INST_OK);
    snprintf(p, sizeof(p), "%s/HELLO.TXT", dst); CHECK(file_exists(p));
    snprintf(p, sizeof(p), "%s/SUB/A.TXT", dst); CHECK(file_exists(p));

    /* ---- remove_tree, and its dangerous-root guard ---- */
    CHECK_EQI(install_remove_tree(dst), INST_OK);
    CHECK(!dir_exists(dst));
    CHECK_EQI(install_remove_tree(""),      INST_ERR_REFUSED);
    CHECK_EQI(install_remove_tree("/"),     INST_ERR_REFUSED);
    CHECK_EQI(install_remove_tree("C:\\"),  INST_ERR_REFUSED);

    /* ---- full install -> uninstall round trip ---- */
    snprintf(inst2, sizeof(inst2), "%s/inst2", SCRATCH);
    snprintf(sys2,  sizeof(sys2),  "%s/sys2",  SCRATCH);
    snprintf(ae2,   sizeof(ae2),   "%s/AUTOEXEC.BAT", sys2);
    mk(sys2);
    write_file(ae2, "@ECHO OFF\n");
    {
        InstallOpts o;
        memset(&o, 0, sizeof(o));
        o.install_root = inst2;
        o.sys_root     = sys2;
        o.src_root     = NULL;
        o.verbose      = 0;
        CHECK_EQI(install_run(&o), INST_OK);
        snprintf(p, sizeof(p), "%s/BIN", inst2); CHECK(dir_exists(p));
        CHECK(file_contains(ae2, "CASTALIA_HOME"));
        /* uninstall restores boot files from backup and removes the tree */
        CHECK_EQI(uninstall_run(&o, 1), INST_OK);
        CHECK(!dir_exists(inst2));
        CHECK(!file_contains(ae2, "CASTALIA_HOME"));
    }

    /* Tidy up the scratch tree. */
    install_remove_tree(SCRATCH);
}
