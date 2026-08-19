/*
 * car_core.h - The .CAR archive directory: many files in one.
 *
 * A .CZ holds one compressed file, which is the wrong shape for the thing
 * people actually want to do on a machine this size: put a folder somewhere
 * safe before changing it. This is the container that holds several, and it is
 * only the DIRECTORY -- the bytes of each member are still lzss_core.c.
 *
 * The format is fixed-size records on purpose. A directory you can read with
 * two seeks and no allocation is worth more here than one that packs tightly,
 * because the machine reading it has four megabytes and a floppy.
 *
 *   0   'C' 'A' 'R' '1'
 *   4   u16 count
 *   6   u16 reserved (zero)
 *   8   count x 32-byte entries:
 *         0   char name[14]   NUL padded, 8.3 plus terminator
 *        14   u32 original
 *        18   u32 stored
 *        22   u32 offset      from the start of the file
 *        26   u8  method      CZ_STORED / CZ_LZSS
 *        27   u8  reserved[5]
 *   ...  the member data, in entry order
 *
 * Everything here is validation and arithmetic over a caller-owned buffer --
 * no allocation, no file I/O -- so tests/test_car.c can feed it archives no
 * writer of ours would ever produce.
 *
 * THE POINT OF THIS FILE IS REFUSING BAD ARCHIVES. An archive is a file from
 * somewhere else, and an extractor that trusts its directory will happily be
 * told to write outside the folder it was pointed at -- a member called
 * "..\\..\\AUTOEXEC.BAT" is the oldest trick there is. Names are checked for
 * separators and for being empty; offsets and lengths are checked against the
 * real size of the file rather than against what the header claims about
 * itself.
 */
#ifndef CASTALIA_CAR_CORE_H
#define CASTALIA_CAR_CORE_H

#include "castalia/ctypes.h"

#define CAR_MAGIC_LEN   4
#define CAR_HEADER      8
#define CAR_ENTRY       32
#define CAR_NAME_MAX    14
#define CAR_MAX_FILES   256   /* a folder, not a filesystem */

typedef struct {
    char name[CAR_NAME_MAX];
    cu32 original;
    cu32 stored;
    cu32 offset;
    int  method;
} CarEntry;

/* Bytes needed for an archive of 'count' members holding 'payload' bytes. */
cu32 car_size_for(int count, cu32 payload);

/* Write the fixed header. Returns CAR_HEADER, or 0 on a bad argument. */
cu32 car_write_header(int count, void *dst, cu32 cap);

/* Write entry 'index'. Returns CAR_ENTRY, or 0 if it will not fit or the
 * entry is one this code would refuse to read back (see car_read_entry). */
cu32 car_write_entry(const CarEntry *e, int index, void *dst, cu32 cap);

/* Read the count, or -1 if this is not a .CAR at all or claims more members
 * than 'total' bytes could possibly hold. */
int car_read_count(const void *src, cu32 total);

/*
 * Read entry 'index' out of an archive of 'total' bytes. Returns CFALSE --
 * and writes nothing -- for any entry this code would not be willing to
 * extract: an empty name, a name carrying a path separator or a drive letter,
 * a name that is "." or "..", an unknown method, or an offset and length that
 * do not both lie inside the file.
 */
cbool car_read_entry(const void *src, cu32 total, int index, CarEntry *out);

/* Is this a name we are willing to create on the way out? Exposed because the
 * writer should refuse the same names the reader does, rather than producing
 * archives it would later reject. */
cbool car_name_ok(const char *name);

#endif /* CASTALIA_CAR_CORE_H */
