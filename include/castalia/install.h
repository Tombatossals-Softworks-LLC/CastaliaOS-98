/*
 * install.h - Portable installer/uninstaller core for INSTALL.EXE.
 *
 * The installer is a small, dependency-light program (like CBOOT) whose real
 * work lives in the functions below so it is HOST-TESTABLE: every operation
 * takes explicit roots instead of hard-coding C:\ , so the unit tests drive a
 * full install -> uninstall round trip against a scratch directory. main()
 * (src/install/install_main.c) only parses argv and calls these.
 *
 * Design rules, from the Project Bible:
 *   - Never destroy the user's files. CONFIG.SYS / AUTOEXEC.BAT are BACKED UP
 *     (the first, pristine backup is never overwritten) before we touch them,
 *     and our boot lines go in a clearly-marked, idempotent block that the
 *     uninstaller removes exactly.
 *   - Everything is repairable from DOS with a text editor.
 *   - No unbounded allocation; bounded buffers throughout.
 *
 * Portable C89: builds under gcc/clang (CASTALIA_HOST) and Open Watcom
 * (CASTALIA_DOS). Only the directory-walk primitives differ per platform.
 */
#ifndef CASTALIA_INSTALL_H
#define CASTALIA_INSTALL_H

/* Result: 0 == success. Negative codes are installer-specific (see below). */
#define INST_OK           0
#define INST_ERR         (-1)  /* generic failure                          */
#define INST_ERR_IO      (-2)  /* file open/read/write error               */
#define INST_ERR_ARG     (-3)  /* bad/empty argument                       */
#define INST_ERR_REFUSED (-4)  /* refused for safety (e.g. remove "/" )    */

/* The marker lines that fence our managed block inside AUTOEXEC.BAT. The
 * uninstaller strips exactly the lines between them (inclusive). */
#define INST_BLOCK_BEGIN "REM === BEGIN CASTALIAOS 98 PE (managed by INSTALL.EXE) ==="
#define INST_BLOCK_END   "REM === END CASTALIAOS 98 PE ==="

/* Suffix used for the pristine backup of a replaced boot file. */
#define INST_BACKUP_SUFFIX ".CB_"

typedef struct {
    const char *install_root; /* where the tree goes, e.g. "C:\\CASTALIA"    */
    const char *sys_root;     /* holds CONFIG.SYS/AUTOEXEC.BAT, e.g. "C:\\"   */
    const char *src_root;     /* optional file tree to copy in, or NULL       */
    int         verbose;      /* echo each step                               */
} InstallOpts;

/* ---- high-level flows (what main() calls) ---------------------------- */

/* Full install: make the tree, (optionally) copy files from src_root, back up
 * the boot files, and add the managed boot block. Returns INST_OK or a code. */
int install_run(const InstallOpts *o);

/* Full uninstall: strip the managed boot block (or restore the pristine
 * backup if present) and, when remove_tree is nonzero, delete install_root.
 * User documents/backups outside the tree are never touched. */
int uninstall_run(const InstallOpts *o, int remove_tree);

/* ---- individually testable building blocks --------------------------- */

/* Create install_root and its standard subdirectories. Existing dirs are OK. */
int install_make_tree(const char *root);

/* Copy 'src' to 'src'+INST_BACKUP_SUFFIX inside the same directory, but only
 * if 'src' exists and the backup does NOT already exist (so the first backup
 * stays pristine across re-runs). Returns INST_OK on success or when there is
 * nothing to do; negative on I/O error. */
int install_backup_file(const char *src);

/* Ensure the managed boot block is present in autoexec_path exactly once.
 * Creates the file if missing. Idempotent: a second call is a no-op. The
 * block sets CASTALIA_HOME to install_root and calls CBOOT.EXE. */
int install_ensure_boot_block(const char *autoexec_path,
                              const char *install_root);

/* Remove the managed boot block (the marker lines and everything between)
 * from autoexec_path. A no-op if the block is absent. */
int install_strip_boot_block(const char *autoexec_path);

/* Restore 'backup' (path+INST_BACKUP_SUFFIX) over 'path' if the backup exists.
 * Returns INST_OK (restored or nothing to do) or a negative code. */
int install_restore_file(const char *path);

/* Recursively copy directory tree 'src' into 'dst' (creating 'dst'). */
int install_copy_tree(const char *src, const char *dst);

/* Recursively delete directory tree 'root'. Refuses obviously dangerous roots
 * (empty, "/", a bare drive like "C:\\") to avoid a catastrophic wipe. */
int install_remove_tree(const char *root);

/* Copy a single file (binary). Overwrites dst. */
int install_copy_file(const char *src, const char *dst);

#endif /* CASTALIA_INSTALL_H */
