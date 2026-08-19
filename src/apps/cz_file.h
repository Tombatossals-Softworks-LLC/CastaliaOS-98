/*
 * cz_file.h - Compressing and restoring a file on disk.
 *
 * The thin layer between lzss_core.c (pure, no I/O) and the File Manager. It
 * owns the two decisions that only make sense once a real filesystem is
 * involved: how much of a file it is willing to hold in memory at once, and
 * what to do when compression makes a file BIGGER.
 *
 * The size cap is not shyness. The target has four megabytes total and this
 * holds the whole input and the whole output at the same time, so a cap is
 * the difference between refusing a file and taking the shell down with it.
 *
 * When a file does not compress -- already-compressed data, mostly -- the
 * result is stored verbatim with CZ_STORED rather than written larger than it
 * started. A compressor that can only ever grow a file is worse than useless.
 */
#ifndef CASTALIA_CZ_FILE_H
#define CASTALIA_CZ_FILE_H

#include "castalia/ctypes.h"
#include "car_core.h"   /* car_list fills CarEntry records */

/* The largest file either direction will handle. Both the input and the
 * output are resident at once, so the real peak is a little over twice this. */
#define CZ_FILE_MAX  (768u * 1024u)

/*
 * Compress 'src_path' to 'dst_path'. On success *out_saved receives the bytes
 * saved (negative if the file was stored and the header cost more than the
 * compression won). Returns CE_OK, CE_NOTFOUND, CE_OVERFLOW (past
 * CZ_FILE_MAX), CE_NOMEM, or CE_IO.
 */
CResult cz_compress_file(const char *src_path, const char *dst_path,
                         long *out_saved);

/*
 * Restore a .CZ file into 'dst_dir', under the name recorded in its header.
 * The chosen name is written to out_name (which the caller shows). Returns
 * CE_OK, CE_NOTFOUND, CE_INVALID (not a .CZ, or a damaged one), CE_OVERFLOW,
 * CE_NOMEM, or CE_IO.
 */
CResult cz_decompress_file(const char *src_path, const char *dst_dir,
                           char *out_name, cu32 namesz);

/*
 * The name a .CZ would restore under, read from its header without expanding
 * anything. CFALSE if the file is not a readable .CZ.
 *
 * Restoring writes with "wb", so the recorded name is the difference between
 * getting a file back and overwriting a newer one that happens to share it.
 * A caller cannot ask "may I replace X" without knowing X first, and it
 * should not have to spend three quarters of a megabyte of RAM to find out --
 * only the 28-byte header is read.
 */
cbool cz_member_name(const char *path, char *out, cu32 outsz);

/* Read a whole file into a fresh buffer / write one out. Shared with the
 * archive code below rather than duplicated, since both need the same size cap
 * and the same all-or-nothing behaviour. */
CResult cz_slurp_public(const char *path, unsigned char **out, cu32 *out_n);
CResult cz_spit_public(const char *path, const void *data, cu32 n);

/* ---------------------------------------------------------------------- */
/* .CAR archives: a whole folder at once                                   */
/* ---------------------------------------------------------------------- */
/*
 * Back a folder up into one archive, and put one back. Only the files
 * directly in the folder are taken -- not its subfolders. That is a real
 * limit and it is deliberate: nested members would need paths inside the
 * archive, and a path inside an archive is the thing car_core.c spends most
 * of its effort refusing. A flat archive cannot be talked into writing
 * outside the folder it is extracted to, whatever it contains.
 */

/* Archive every file directly inside 'dir' into 'archive_path'. On success
 * *out_files receives how many were stored and *out_saved the bytes saved.
 * Returns CE_OK, CE_NOTFOUND, CE_OVERFLOW (too many files, or one too large),
 * CE_NOMEM or CE_IO. */
CResult car_pack_dir(const char *dir, const char *archive_path,
                     int *out_files, long *out_saved);

/* Extract every member of 'archive_path' into 'dir'. *out_files receives how
 * many were written. Returns CE_OK, CE_NOTFOUND, CE_INVALID (not a .CAR, or
 * one carrying a member this refuses to write), CE_NOMEM or CE_IO. */
CResult car_unpack(const char *archive_path, const char *dir, int *out_files);

/*
 * Read an archive's DIRECTORY without extracting anything, into caller
 * storage. Returns the number of entries written (<= max), or -1 if the file
 * is not a readable .CAR.
 *
 * Only the front of the file is read -- the header plus the fixed-size records
 * behind it -- which is what the format was shaped for. Listing a 700 KB
 * archive costs eight kilobytes and no decompression.
 *
 * Members the reader refuses (a name that could escape the folder, an offset
 * outside the file) are skipped rather than returned, so a caller cannot show
 * or act on an entry that car_unpack would decline to extract.
 */
int car_list(const char *archive_path, CarEntry *out, int max);

/*
 * How many of an archive's members already exist in 'dir' -- that is, how many
 * files extracting it would REPLACE. -1 if the archive cannot be read.
 *
 * Extraction writes members with "wb", so this is the difference between
 * restoring a backup and destroying whatever was there. The callers use it to
 * decide whether to ask first: nothing to lose, no question.
 */
int car_clashes(const char *archive_path, const char *dir);

#endif /* CASTALIA_CZ_FILE_H */
