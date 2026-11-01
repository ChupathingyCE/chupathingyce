/*
MAP_ARCHIVE.H

What a Custom Edition map downloaded from the community map repositories
is checked and unpacked by (map_archive.c, for map_download.c): the name it
is asked for by, the zip archive it comes in, its cache file's header, the
hashed names other builds give shared maps, and network.map_downloads's
words. No file, network or game dependency (zlib, and Monocypher's
BLAKE2b): the tests build it by itself (tools/test_map_download.py).

Plain types only: the game's units (64-bit builds rewrite their long) and
the tests both call it.
*/

#ifndef MAP_ARCHIVE_H
#define MAP_ARCHIVE_H

enum
{
	/* the most a download, the map unpacked from it, or a map file hashed
	may be: 1 GB (community maps run past 300 MB, Bigass V3 among them; the
	loader takes up to 2 GB, and every offset here is an unsigned int, read
	and hashed in pieces) */
	MAP_ARCHIVE_MAXIMUM_SIZE = 1024 * 1024 * 1024,
	/* a cache file's header */
	MAP_ARCHIVE_HEADER_SIZE = 0x800,
	/* a map's file name, at most (its header's name field, less its
	terminator) */
	MAP_ARCHIVE_MAXIMUM_NAME_LENGTH = 31,
	/* a map's whole-file hash (map_archive_map_hash), in bytes */
	MAP_ARCHIVE_HASH_SIZE = 32,
	/* the hex digits of it a hashed name ends in */
	MAP_ARCHIVE_HASHED_NAME_DIGITS = 8,
};

/* network.map_downloads */
enum
{
	MAP_DOWNLOADS_ASK,
	MAP_DOWNLOADS_ALWAYS,
	MAP_DOWNLOADS_NEVER,
};

/* the archive's bytes [offset, offset + size) into buffer: 1 if all were
read */
typedef int (*map_archive_read_proc)(void *context, unsigned int offset, void *buffer, unsigned int size);
/* the map's next size bytes written: 1 on success */
typedef int (*map_archive_write_proc)(void *context, const void *buffer, unsigned int size);
/* whether to stop (the player cancelled): nonzero stops */
typedef int (*map_archive_stop_proc)(void *context);

struct map_archive_io
{
	void *context;
	/* the archive's size in bytes */
	unsigned int archive_size;
	map_archive_read_proc read;
	map_archive_write_proc write;
	/* (NULL: never stops) */
	map_archive_stop_proc stop;
};

/* whether name is a map's file name the repositories are asked for: 1 to
MAP_ARCHIVE_MAXIMUM_NAME_LENGTH letters, digits, '_', '-' and '.', not
starting with '.' and holding no "..": nothing a path or a URL reads as
more than a name */
int map_archive_name_valid(const char *name);

/* <name>.map unpacked from the zip archive io reads, written through io's
write (nothing else in the archive is written anywhere): 1 with its size in
*map_size, else 0 with why in error. Refused whole: an archive that is not
a zip this reads (no ZIP64, nothing split, encrypted or compressed but by
deflate), that names any entry with a path that leaves its folder ("..", an
absolute path, a drive), that has no entry <name>.map at its top or more than
one, or whose entry is larger than MAP_ARCHIVE_MAXIMUM_SIZE, lies outside the
archive, or does not unpack to its stated size and CRC-32 */
int map_archive_extract(const struct map_archive_io *io, const char *name, unsigned int *map_size, char *error,
	int error_size);

/* whether the first MAP_ARCHIVE_HEADER_SIZE bytes of a map file of
file_size bytes are a Custom Edition (cache version 609) multiplayer map's
header, as the game's loader reads it, with its own name name (any case);
version, when not 0, is the host's (its header's checksum, which the host
advertises as its game's map version) and must be the file's. 1, else 0
with why in error */
int map_archive_check_header(const unsigned char *header, unsigned int file_size, const char *name,
	unsigned int version, char *error, int error_size);

/* network.map_downloads's value (MAP_DOWNLOADS_*): "ask", "always" or
"never", in any case; anything else asks */
int map_archive_setting(const char *text);
/* ... and a value's word */
const char *map_archive_setting_word(int setting);

/* a header's checksum (the host's map version) */
unsigned int map_archive_header_checksum(const unsigned char *header);

/* ---------- the game's own maps

Halo PC's own maps and files (its multiplayer maps, its campaign's levels,
and its ui, bitmaps, sounds and loc) are Microsoft's and Gearbox's: the
player copies them from their own Halo PC or Custom Edition install, and
the downloader never asks a repository for them, though the community
repositories have copies. */

/* whether name (a map's file name, or a hashed name's base, in any case) is
one of Halo PC's own: MAP_ARCHIVE_STOCK_MAP for a map, MAP_ARCHIVE_STOCK_FILE
for one of its data files (ui, bitmaps, sounds, loc), else 0 */
enum
{
	MAP_ARCHIVE_STOCK_MAP = 1,
	MAP_ARCHIVE_STOCK_FILE = 2,
};
int map_archive_stock_name(const char *name);
/* what a player is told of one (title: "Install this map yourself"): that
<name>.map is one of Halo PC's own maps (or files) and where to copy it
from, into text (size bytes); 0 if name is not one of them */
#define MAP_ARCHIVE_STOCK_TITLE "Install this map yourself"
int map_archive_stock_message(const char *name, char *text, int size);

/* ---------- hashed names

The names other builds give the Custom Edition maps their hosts share:
<base>_<8 hex digits>, the digits the first of the map file's whole-file
hash (map_archive_map_hash), so that two versions of a map have two names.
The file is the community repositories' <base>.map, its contents unchanged:
the game finds a hashed name's map by its base name when the hash matches
(map_families.c), and the downloader asks the repositories for the base. */

/* whether name is a hashed name: a base of at least one character, '_', and
MAP_ARCHIVE_HASHED_NAME_DIGITS hex digits (either case) ending it. 1 with
the base in base (base_size bytes, NULL for none) and the digits' value in
*prefix (NULL for none), else 0 */
int map_archive_hashed_name(const char *name, char *base, int base_size, unsigned int *prefix);

/* the name a player knows a map by: a hashed name's base, in lowercase (the
repositories' file), else the name itself */
void map_archive_display_name(const char *name, char *display, int size);

/* a map file's whole-file hash, as other builds name shared maps by it:
BLAKE2b-256 of "OpenCE map 1", the file's size (4 bytes, little-endian) and
the BLAKE2b-256 of each 0x40000-byte piece of the file in turn (the last
piece shorter). The file's size bytes are read through read; stop (NULL
for never) may end it early. 1 with the hash in hash
(MAP_ARCHIVE_HASH_SIZE bytes), else 0 */
int map_archive_map_hash(map_archive_read_proc read, map_archive_stop_proc stop, void *context, unsigned int size,
	unsigned char *hash);

/* a hash's first MAP_ARCHIVE_HASHED_NAME_DIGITS hex digits, as a number
(what a hashed name ends in) */
unsigned int map_archive_hash_prefix(const unsigned char *hash);

/* ---------- map versions

Since network version 29 a host names a Custom Edition map by its own name
(custom_maps\hugeass) and puts which version of it it plays in the game
record's map version: its whole-file hash's first 32 bits (the prefix
above), or 1 for a prefix of 0, which a client takes for "any". Hosts of
version 23 to 28 put the map's header checksum there; a client accepts
either. */

/* a map of this whole-file hash's version, as a host of network version 29
sends it: never 0 */
unsigned int map_archive_hash_version(const unsigned char *hash);

/* whether a map of this header checksum and hash version (map_archive_hash_version's)
is the host's version: 1 if version is 0 (any), either of them, else 0 */
int map_archive_version_matches(unsigned int version, unsigned int checksum, unsigned int hash_version);

#endif
