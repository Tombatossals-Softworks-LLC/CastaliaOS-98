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

    /* ---- what belongs to the user -------------------------------------
     *
     * A pure decision over a string, so every case a DOS path can take is
     * walked here rather than the two that occur to somebody at the keyboard.
     */
    CHECK(install_is_user_state("SYS\\CASTALIA.INI"));
    CHECK(install_is_user_state("SYS/CASTALIA.INI"));   /* either separator  */
    CHECK(install_is_user_state("sys\\castalia.ini"));  /* DOS is caseless   */
    CHECK(install_is_user_state("SyS\\Deep\\Nested.TXT"));
    CHECK(install_is_user_state("SYS"));                /* the dir itself    */
    CHECK(install_is_user_state("\\SYS\\CASTALIA.INI"));/* leading separator */
    CHECK(install_is_user_state("//TRASH/TRASH.IDX"));
    CHECK(install_is_user_state("LOGS\\CASTALIA.LOG"));
    CHECK(install_is_user_state("TEMP\\X.TMP"));
    CHECK(install_is_user_state("THEMES\\MINE.INI"));
    CHECK(install_is_user_state("PHOTOS\\SHOT0001.BMP"));
    CHECK(install_is_user_state("DOCS\\LETTER.DOC"));

    /* Program files the media owns. */
    CHECK(!install_is_user_state("BIN\\CASTALIA.EXE"));
    CHECK(!install_is_user_state("ICONS\\TANGO\\CLOCK.BMP"));
    CHECK(!install_is_user_state("APPS\\HELLO.CAPP"));
    CHECK(!install_is_user_state("FONTS\\X"));
    CHECK(!install_is_user_state("HELP\\X"));
    CHECK(!install_is_user_state("DRV\\X"));
    CHECK(!install_is_user_state("README.TXT"));        /* at the root       */

    /* A name is not a namespace: matching a prefix would hand SYSTEM\ to the
     * user because SYS is theirs, and would keep an upgrade from ever
     * replacing anything inside it. */
    CHECK(!install_is_user_state("SYSTEM\\X"));
    CHECK(!install_is_user_state("SY\\X"));
    CHECK(!install_is_user_state("LOGSX\\X"));
    CHECK(!install_is_user_state("DOCSX"));
    CHECK(!install_is_user_state("BINSYS\\X"));

    /* Degenerate input answers "not the user's" rather than crashing. */
    CHECK(!install_is_user_state(NULL));
    CHECK(!install_is_user_state(""));
    CHECK(!install_is_user_state("\\"));
    CHECK(!install_is_user_state("/"));

    /* ---- a copy that keeps the user's files ---------------------------- */
    {
        char media[128], tree[128], f[192], d[192];

        snprintf(media, sizeof(media), "%s/media", SCRATCH);
        snprintf(tree,  sizeof(tree),  "%s/tree",  SCRATCH);
        mk(media);
        snprintf(d, sizeof(d), "%s/SYS", media);  mk(d);
        snprintf(f, sizeof(f), "%s/SYS/CASTALIA.INI", media);
        write_file(f, "shipped\n");
        snprintf(d, sizeof(d), "%s/BIN", media);  mk(d);
        snprintf(f, sizeof(f), "%s/BIN/CBOOT.EXE", media);
        write_file(f, "new-program\n");
        snprintf(d, sizeof(d), "%s/THEMES", media); mk(d);
        snprintf(f, sizeof(f), "%s/THEMES/CLASSIC.INI", media);
        write_file(f, "shipped-theme\n");

        /* First copy onto nothing: the user has no config yet, so withholding
         * one would leave the desktop with no settings at all. */
        CHECK_EQI(install_copy_tree_keep_user(media, tree), INST_OK);
        snprintf(f, sizeof(f), "%s/SYS/CASTALIA.INI", tree);
        CHECK(file_contains(f, "shipped"));

        /* The user then edits their config and their theme. */
        write_file(f, "MINE\n");
        snprintf(f, sizeof(f), "%s/THEMES/CLASSIC.INI", tree);
        write_file(f, "MY-THEME\n");
        /* ...and the program files go stale. */
        snprintf(f, sizeof(f), "%s/BIN/CBOOT.EXE", tree);
        write_file(f, "old-program\n");

        /* Second copy: this is an upgrade. */
        CHECK_EQI(install_copy_tree_keep_user(media, tree), INST_OK);
        snprintf(f, sizeof(f), "%s/SYS/CASTALIA.INI", tree);
        CHECK(file_contains(f, "MINE"));           /* kept                  */
        CHECK(!file_contains(f, "shipped"));
        snprintf(f, sizeof(f), "%s/THEMES/CLASSIC.INI", tree);
        CHECK(file_contains(f, "MY-THEME"));       /* kept                  */
        snprintf(f, sizeof(f), "%s/BIN/CBOOT.EXE", tree);
        CHECK(file_contains(f, "new-program"));    /* replaced              */
        CHECK(!file_contains(f, "old-program"));

        /* The plain copy is what it always was, and that is the difference
         * the whole feature turns on: it takes the user's config with it. */
        snprintf(f, sizeof(f), "%s/SYS/CASTALIA.INI", tree);
        write_file(f, "MINE\n");
        CHECK_EQI(install_copy_tree(media, tree), INST_OK);
        CHECK(file_contains(f, "shipped"));

        install_remove_tree(tree);
        install_remove_tree(media);
    }

    /* ---- repair and upgrade -------------------------------------------- */
    {
        char inst3[128], sys3[128], media[128];
        char ae3[192], ini[192], f[192], d[192];
        InstallOpts o;

        snprintf(inst3, sizeof(inst3), "%s/inst3", SCRATCH);
        snprintf(sys3,  sizeof(sys3),  "%s/sys3",  SCRATCH);
        snprintf(media, sizeof(media), "%s/media3", SCRATCH);
        snprintf(ae3,   sizeof(ae3),   "%s/AUTOEXEC.BAT", sys3);
        snprintf(ini,   sizeof(ini),   "%s/SYS/CASTALIA.INI", inst3);
        mk(sys3);
        write_file(ae3, "@ECHO OFF\n");

        mk(media);
        snprintf(d, sizeof(d), "%s/BIN", media); mk(d);
        snprintf(f, sizeof(f), "%s/BIN/CBOOT.EXE", media);
        write_file(f, "v2\n");
        snprintf(d, sizeof(d), "%s/SYS", media); mk(d);
        snprintf(f, sizeof(f), "%s/SYS/CASTALIA.INI", media);
        write_file(f, "defaults\n");

        memset(&o, 0, sizeof(o));
        o.install_root = inst3;
        o.sys_root     = sys3;
        o.src_root     = NULL;
        o.verbose      = 0;

        /* Nothing is installed yet. */
        CHECK(!install_is_present(&o));

        /* An upgrade with no media is refused, and refused BEFORE the tree is
         * created -- a refusal that left a half-built C:\CASTALIA behind would
         * be worse than the mistake it is refusing. */
        CHECK_EQI(install_run_mode(&o, INST_MODE_UPGRADE), INST_ERR_ARG);
        CHECK(!dir_exists(inst3));

        /* An upgrade of a machine with nothing on it says so. */
        o.src_root = media;
        CHECK_EQI(install_run_mode(&o, INST_MODE_UPGRADE), INST_ERR_NOTHING);
        CHECK(!dir_exists(inst3));

        /* Install for real. */
        CHECK_EQI(install_run_mode(&o, INST_MODE_INSTALL), INST_OK);
        CHECK(install_is_present(&o));
        CHECK(file_contains(ini, "defaults"));
        CHECK(file_contains(ae3, "CASTALIA_HOME"));

        /* The user settles in, and then the disk eats a directory. */
        write_file(ini, "THEIRS\n");
        snprintf(d, sizeof(d), "%s/THEMES", inst3);
        install_remove_tree(d);
        CHECK(!dir_exists(d));

        /* Repair without media puts the directory back and keeps the config;
         * it must not need the CD to fix a missing folder. */
        o.src_root = NULL;
        CHECK_EQI(install_run_mode(&o, INST_MODE_REPAIR), INST_OK);
        CHECK(dir_exists(d));
        CHECK(file_contains(ini, "THEIRS"));

        /* Somebody deletes the boot entry by hand; repair restores exactly
         * one, not a second copy stacked on the first. */
        write_file(ae3, "@ECHO OFF\n");
        CHECK(!file_contains(ae3, "CASTALIA_HOME"));
        CHECK_EQI(install_run_mode(&o, INST_MODE_REPAIR), INST_OK);
        CHECK_EQI(count_occ(ae3, INST_BLOCK_BEGIN), 1);
        CHECK_EQI(install_run_mode(&o, INST_MODE_REPAIR), INST_OK);
        CHECK_EQI(count_occ(ae3, INST_BLOCK_BEGIN), 1);

        /* Now upgrade from the media: new program files, same settings. */
        snprintf(f, sizeof(f), "%s/BIN/CBOOT.EXE", inst3);
        write_file(f, "v1\n");
        o.src_root = media;
        CHECK_EQI(install_run_mode(&o, INST_MODE_UPGRADE), INST_OK);
        CHECK(file_contains(f, "v2"));
        CHECK(file_contains(ini, "THEIRS"));

        /* And a plain re-install -- how most people "repair" a DOS program --
         * is no longer a way to lose your settings. */
        CHECK_EQI(install_run_mode(&o, INST_MODE_INSTALL), INST_OK);
        CHECK(file_contains(ini, "THEIRS"));

        CHECK_EQI(uninstall_run(&o, 1), INST_OK);
        install_remove_tree(media);
    }

    /* Tidy up the scratch tree. */
    install_remove_tree(SCRATCH);
}
