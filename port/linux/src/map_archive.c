/*
MAP_ARCHIVE.C

A Custom Edition map from the community map repositories (map_archive.h,
map_download.c): its name checked before it is asked for, the zip archive
the repositories send it in unpacked, and its cache file's header checked.

Nothing in a download is trusted. The archive is read by its central
directory, every offset and size checked against the archive before it is
read; one entry alone is unpacked, the map's own (<name>.map, at the
archive's top), and its bytes go only where the caller writes them: no
name from the archive is ever a path. An archive naming any entry with a
path that leaves its folder is refused whole (no honest repository sends
one). The entry unpacks to no more than its stated size and
MAP_ARCHIVE_MAXIMUM_SIZE, and to exactly its stated size and CRC-32.

The header is then checked as the game's loader reads it (cache_files.c,
map_families.c): a Custom Edition multiplayer map, its ranges within the
file, named for the map asked for, and the host's version of it. The game's
own checks (port/linux/game/ce_map_checks.c) still read the whole map
before it is played.
*/

#include "map_archive.h"

#include "monocypher.h"
#include "zlib_prefixed.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	ZIP_LOCAL_SIGNATURE = 0x04034b50,
	ZIP_CENTRAL_SIGNATURE = 0x02014b50,
	ZIP_END_SIGNATURE = 0x06054b50,
	ZIP_LOCAL_SIZE = 30,
	ZIP_CENTRAL_SIZE = 46,
	ZIP_END_SIZE = 22,
	/* (the end record's comment, at most) */
	ZIP_MAXIMUM_COMMENT = 0xffff,
	/* entries read, at most (a map's archive holds one, or a few beside it) */
	ZIP_MAXIMUM_ENTRIES = 256,
	ZIP_MAXIMUM_NAME = 512,
	ZIP_FLAG_ENCRYPTED = 0x0001,
	ZIP_METHOD_STORED = 0,
	ZIP_METHOD_DEFLATED = 8,

	/* what is read and written at a time */
	CHUNK_SIZE = 64 * 1024,

	/* the cache file header's fields (cache_files.c's struct
	cache_file_header) */
	HEADER_SIGNATURE = 0x68656164, /* 'head' */
	FOOTER_SIGNATURE = 0x666f6f74, /* 'foot' */
	HEADER_VERSION_OFFSET = 0x4,
	HEADER_FILE_LENGTH_OFFSET = 0x8,
	HEADER_TAG_DATA_OFFSET = 0x10,
	HEADER_TAG_DATA_SIZE_OFFSET = 0x14,
	HEADER_NAME_OFFSET = 0x20,
	HEADER_NAME_SIZE = 0x20,
	HEADER_TYPE_OFFSET = 0x60,
	HEADER_CHECKSUM_OFFSET = 0x64,
	HEADER_FOOTER_OFFSET = 0x7fc,
	CUSTOM_EDITION_VERSION = 609,
	MULTIPLAYER_TYPE = 1,
};

/* ---------- private code */

static unsigned int read_16(const unsigned char *bytes)
{
	return (unsigned int)bytes[0] | (unsigned int)bytes[1] << 8;
}

static unsigned int read_32(const unsigned char *bytes)
{
	return read_16(bytes) | read_16(bytes + 2) << 16;
}

static int refuse(char *error, int error_size, const char *format, ...)
{
	va_list arguments;

	if (error && error_size > 0)
	{
		va_start(arguments, format);
		vsnprintf(error, (size_t)error_size, format, arguments);
		va_end(arguments);
	}
	return 0;
}

static int lower(int character)
{
	return character >= 'A' && character <= 'Z' ? character + ('a' - 'A') : character;
}

/* two names the same in any case: length bytes of a against all of b */
static int same_name(const char *a, size_t length, const char *b)
{
	size_t index;

	if (strlen(b) != length)
		return 0;
	for (index = 0; index < length; index++)
	{
		if (lower((unsigned char)a[index]) != lower((unsigned char)b[index]))
			return 0;
	}
	return 1;
}

/* whether [offset, offset + size) lies within [0, limit), without overflow */
static int within(unsigned int offset, unsigned int size, unsigned int limit)
{
	return offset <= limit && size <= limit - offset;
}

/* whether an entry's name (length bytes, not terminated) leaves its folder:
"..", an absolute path, a drive, or a NUL in it */
static int name_escapes(const char *name, unsigned int length)
{
	unsigned int index, start = 0;

	if (!length || name[0] == '/' || name[0] == '\\')
		return 1;
	for (index = 0; index <= length; index++)
	{
		if (index < length && !name[index])
			return 1;
		if (index < length && name[index] == ':')
			return 1;
		if (index == length || name[index] == '/' || name[index] == '\\')
		{
			if (index - start == 2 && name[start] == '.' && name[start + 1] == '.')
				return 1;
			start = index + 1;
		}
	}
	return 0;
}

/* the end of central directory record: the central directory's offset,
size and entry count; 0 if it is not a zip this reads */
static int find_end(const struct map_archive_io *io, unsigned int *directory_offset, unsigned int *directory_size,
	unsigned int *entries, char *error, int error_size)
{
	unsigned int tail_size = io->archive_size < ZIP_END_SIZE + ZIP_MAXIMUM_COMMENT ? io->archive_size :
		ZIP_END_SIZE + ZIP_MAXIMUM_COMMENT;
	unsigned char *tail;
	unsigned int index;
	int found = 0;

	if (io->archive_size < ZIP_END_SIZE)
		return refuse(error, error_size, "the download is not a zip archive");
	tail = (unsigned char *)malloc(tail_size);
	if (!tail)
		return refuse(error, error_size, "out of memory");
	if (!io->read(io->context, io->archive_size - tail_size, tail, tail_size))
	{
		free(tail);
		return refuse(error, error_size, "the download could not be read");
	}
	/* (the last record whose comment ends the archive) */
	for (index = tail_size - ZIP_END_SIZE + 1; index-- > 0 && !found;)
	{
		if (read_32(tail + index) == ZIP_END_SIGNATURE &&
			index + ZIP_END_SIZE + read_16(tail + index + 20) == tail_size)
		{
			const unsigned char *end = tail + index;

			if (read_16(end + 4) || read_16(end + 6) || read_16(end + 8) != read_16(end + 10))
			{
				free(tail);
				return refuse(error, error_size, "the archive is split, which this does not read");
			}
			*entries = read_16(end + 10);
			*directory_size = read_32(end + 12);
			*directory_offset = read_32(end + 16);
			/* (ZIP64's markers: this reads none, and no map needs it) */
			if (*entries == 0xffff || *directory_size == 0xffffffffu || *directory_offset == 0xffffffffu)
			{
				free(tail);
				return refuse(error, error_size, "the archive is ZIP64, which this does not read");
			}
			if (!within(*directory_offset, *directory_size, io->archive_size - tail_size + index))
			{
				free(tail);
				return refuse(error, error_size, "the archive's directory lies outside it");
			}
			found = 1;
		}
	}
	free(tail);
	if (!found)
		return refuse(error, error_size, "the download is not a zip archive");
	if (*entries > ZIP_MAXIMUM_ENTRIES)
		return refuse(error, error_size, "the archive has %u entries (at most %d)", *entries, ZIP_MAXIMUM_ENTRIES);
	return 1;
}

/* the entry (stored or deflated) at data_offset, compressed_size bytes,
unpacked through io's write: exactly size bytes whose CRC-32 is crc */
static int unpack_entry(const struct map_archive_io *io, int method, unsigned int data_offset,
	unsigned int compressed_size, unsigned int size, unsigned int crc, char *error, int error_size)
{
	unsigned char *input = (unsigned char *)malloc(CHUNK_SIZE);
	unsigned char *output = (unsigned char *)malloc(CHUNK_SIZE);
	unsigned int read_offset = 0, written = 0;
	uLong check = z_crc32(0L, Z_NULL, 0);
	z_stream stream;
	int ended = 0;
	int ok = 1;

	if (!input || !output)
	{
		free(input);
		free(output);
		return refuse(error, error_size, "out of memory");
	}
	memset(&stream, 0, sizeof(stream));
	if (method == ZIP_METHOD_DEFLATED && inflateInit2(&stream, -MAX_WBITS) != Z_OK)
	{
		free(input);
		free(output);
		return refuse(error, error_size, "could not start inflating");
	}
	while (ok && !ended)
	{
		unsigned int count = compressed_size - read_offset < CHUNK_SIZE ? compressed_size - read_offset : CHUNK_SIZE;

		if (io->stop && io->stop(io->context))
		{
			ok = refuse(error, error_size, "cancelled");
			break;
		}
		if (count && !io->read(io->context, data_offset + read_offset, input, count))
		{
			ok = refuse(error, error_size, "the download could not be read");
			break;
		}
		read_offset += count;
		if (method == ZIP_METHOD_STORED)
		{
			if (count && !io->write(io->context, input, count))
				ok = refuse(error, error_size, "the map could not be written");
			check = z_crc32(check, input, count);
			written += count;
			ended = read_offset == compressed_size;
			continue;
		}
		/* (all of it read, and the stream not ended: it is cut short) */
		if (!count)
		{
			ok = refuse(error, error_size, "the map's data ends early");
			break;
		}
		stream.next_in = input;
		stream.avail_in = count;
		do
		{
			unsigned int produced;
			int result;

			if (io->stop && io->stop(io->context))
			{
				ok = refuse(error, error_size, "cancelled");
				break;
			}
			stream.next_out = output;
			stream.avail_out = CHUNK_SIZE;
			result = inflate(&stream, Z_NO_FLUSH);
			if (result != Z_OK && result != Z_STREAM_END && result != Z_BUF_ERROR)
			{
				ok = refuse(error, error_size, "the map's data is damaged (inflate: %d)", result);
				break;
			}
			produced = CHUNK_SIZE - stream.avail_out;
			/* (never past its stated size: an archive that would unpack to
			more stops here) */
			if (produced > size - written)
			{
				ok = refuse(error, error_size, "the map unpacks to more than its stated %u bytes", size);
				break;
			}
			if (produced && !io->write(io->context, output, produced))
			{
				ok = refuse(error, error_size, "the map could not be written");
				break;
			}
			check = z_crc32(check, output, produced);
			written += produced;
			if (result == Z_STREAM_END)
				ended = 1;
		}
		while (ok && !ended && stream.avail_out == 0);
	}
	if (method == ZIP_METHOD_DEFLATED)
		inflateEnd(&stream);
	free(input);
	free(output);
	if (!ok)
		return 0;
	if (written != size)
		return refuse(error, error_size, "the map unpacked to %u bytes, not its stated %u", written, size);
	if ((unsigned int)check != crc)
		return refuse(error, error_size, "the map's CRC-32 is %08x, not its stated %08x", (unsigned int)check, crc);
	return 1;
}

/* ---------- public code */

int map_archive_name_valid(const char *name)
{
	size_t length, index;

	if (!name)
		return 0;
	length = strlen(name);
	if (!length || length > MAP_ARCHIVE_MAXIMUM_NAME_LENGTH || name[0] == '.' || strstr(name, ".."))
		return 0;
	for (index = 0; index < length; index++)
	{
		char character = name[index];

		if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == '_' || character == '-' || character == '.'))
		{
			return 0;
		}
	}
	return 1;
}

int map_archive_extract(const struct map_archive_io *io, const char *name, unsigned int *map_size, char *error,
	int error_size)
{
	char wanted[MAP_ARCHIVE_MAXIMUM_NAME_LENGTH + 5];
	unsigned int directory_offset, directory_size, entries, entry, offset;
	unsigned int found_local = 0, found_compressed = 0, found_size = 0, found_crc = 0;
	int found_method = 0, found = 0;

	if (error && error_size > 0)
		error[0] = 0;
	*map_size = 0;
	if (!map_archive_name_valid(name))
		return refuse(error, error_size, "not a map name this asks for");
	if (io->archive_size > MAP_ARCHIVE_MAXIMUM_SIZE)
		return refuse(error, error_size, "the download is over %d MB", MAP_ARCHIVE_MAXIMUM_SIZE >> 20);
	snprintf(wanted, sizeof(wanted), "%s.map", name);
	if (!find_end(io, &directory_offset, &directory_size, &entries, error, error_size))
		return 0;
	offset = directory_offset;
	for (entry = 0; entry < entries; entry++)
	{
		unsigned char header[ZIP_CENTRAL_SIZE];
		char entry_name[ZIP_MAXIMUM_NAME];
		unsigned int name_length, extra_length, comment_length;

		if (!within(offset, ZIP_CENTRAL_SIZE, directory_offset + directory_size) ||
			!io->read(io->context, offset, header, ZIP_CENTRAL_SIZE) || read_32(header) != ZIP_CENTRAL_SIGNATURE)
		{
			return refuse(error, error_size, "the archive's directory is damaged");
		}
		name_length = read_16(header + 28);
		extra_length = read_16(header + 30);
		comment_length = read_16(header + 32);
		if (name_length >= sizeof(entry_name) ||
			!within(offset + ZIP_CENTRAL_SIZE, name_length + extra_length + comment_length,
				directory_offset + directory_size) ||
			!io->read(io->context, offset + ZIP_CENTRAL_SIZE, entry_name, name_length))
		{
			return refuse(error, error_size, "the archive's directory is damaged");
		}
		/* (any entry's name that leaves its folder: the archive refused,
		though no entry's name is ever written to) */
		if (name_escapes(entry_name, name_length))
			return refuse(error, error_size, "the archive names a path outside its folder");
		if (same_name(entry_name, name_length, wanted))
		{
			if (found)
				return refuse(error, error_size, "the archive holds %s more than once", wanted);
			found = 1;
			if (read_16(header + 8) & ZIP_FLAG_ENCRYPTED)
				return refuse(error, error_size, "the archive's map is encrypted");
			found_method = (int)read_16(header + 10);
			found_crc = read_32(header + 16);
			found_compressed = read_32(header + 20);
			found_size = read_32(header + 24);
			found_local = read_32(header + 42);
		}
		offset += ZIP_CENTRAL_SIZE + name_length + extra_length + comment_length;
	}
	if (!found)
		return refuse(error, error_size, "the archive has no %s", wanted);
	if (found_method != ZIP_METHOD_STORED && found_method != ZIP_METHOD_DEFLATED)
		return refuse(error, error_size, "the archive's map is compressed by method %d, which this does not read",
			found_method);
	if (found_size > MAP_ARCHIVE_MAXIMUM_SIZE)
		return refuse(error, error_size, "the map is %u MB unpacked (at most %d MB)", found_size >> 20,
			MAP_ARCHIVE_MAXIMUM_SIZE >> 20);
	if (found_method == ZIP_METHOD_STORED && found_compressed != found_size)
		return refuse(error, error_size, "the archive's map has two sizes");
	if (found_size < MAP_ARCHIVE_HEADER_SIZE)
		return refuse(error, error_size, "the map is too small to be one (%u bytes)", found_size);
	/* the local header, and the data after it, within the archive before
	its directory */
	{
		unsigned char local[ZIP_LOCAL_SIZE];
		unsigned int data_offset;

		if (!within(found_local, ZIP_LOCAL_SIZE, directory_offset) ||
			!io->read(io->context, found_local, local, ZIP_LOCAL_SIZE) || read_32(local) != ZIP_LOCAL_SIGNATURE)
		{
			return refuse(error, error_size, "the archive's map entry is damaged");
		}
		if (!within(found_local + ZIP_LOCAL_SIZE, read_16(local + 26) + read_16(local + 28), directory_offset))
			return refuse(error, error_size, "the archive's map entry is damaged");
		data_offset = found_local + ZIP_LOCAL_SIZE + read_16(local + 26) + read_16(local + 28);
		if (!within(data_offset, found_compressed, directory_offset))
			return refuse(error, error_size, "the archive's map lies outside it");
		if (!unpack_entry(io, found_method, data_offset, found_compressed, found_size, found_crc, error, error_size))
			return 0;
	}
	*map_size = found_size;
	return 1;
}

unsigned int map_archive_header_checksum(const unsigned char *header)
{
	return read_32(header + HEADER_CHECKSUM_OFFSET);
}

int map_archive_check_header(const unsigned char *header, unsigned int file_size, const char *name,
	unsigned int version, char *error, int error_size)
{
	unsigned int file_length = read_32(header + HEADER_FILE_LENGTH_OFFSET);
	unsigned int tag_data_offset = read_32(header + HEADER_TAG_DATA_OFFSET);
	unsigned int tag_data_size = read_32(header + HEADER_TAG_DATA_SIZE_OFFSET);
	const char *map_name = (const char *)header + HEADER_NAME_OFFSET;
	const char *terminator = (const char *)memchr(map_name, 0, HEADER_NAME_SIZE);

	if (error && error_size > 0)
		error[0] = 0;
	if (file_size < MAP_ARCHIVE_HEADER_SIZE || file_size > MAP_ARCHIVE_MAXIMUM_SIZE)
		return refuse(error, error_size, "its size (%u bytes) is not a map's", file_size);
	if (read_32(header) != HEADER_SIGNATURE || read_32(header + HEADER_FOOTER_OFFSET) != FOOTER_SIGNATURE)
		return refuse(error, error_size, "it is not a cache file");
	if (read_32(header + HEADER_VERSION_OFFSET) != CUSTOM_EDITION_VERSION)
	{
		return refuse(error, error_size, "it is a cache file of version %u, not Custom Edition's %d",
			read_32(header + HEADER_VERSION_OFFSET), CUSTOM_EDITION_VERSION);
	}
	/* (0 from some of Invader's maps: the loader takes the file's own) */
	if (file_length && (file_length < MAP_ARCHIVE_HEADER_SIZE || file_length > file_size))
		return refuse(error, error_size, "its header says %u bytes, the file has %u", file_length, file_size);
	if (tag_data_offset < MAP_ARCHIVE_HEADER_SIZE || !within(tag_data_offset, tag_data_size, file_size))
		return refuse(error, error_size, "its tag data lies outside the file");
	if (read_16(header + HEADER_TYPE_OFFSET) != MULTIPLAYER_TYPE)
		return refuse(error, error_size, "it is not a multiplayer map");
	if (!terminator)
		return refuse(error, error_size, "its name does not end within its header");
	if (!same_name(map_name, (size_t)(terminator - map_name), name))
		return refuse(error, error_size, "it is named %s inside, not %s", map_name, name);
	if (version && map_archive_header_checksum(header) != version)
	{
		return refuse(error, error_size, "its checksum is %08x, the host's %08x: another version of the map",
			map_archive_header_checksum(header), version);
	}
	return 1;
}

int map_archive_setting(const char *text)
{
	if (text && same_name(text, strlen(text), "always"))
		return MAP_DOWNLOADS_ALWAYS;
	if (text && same_name(text, strlen(text), "never"))
		return MAP_DOWNLOADS_NEVER;
	return MAP_DOWNLOADS_ASK;
}

const char *map_archive_setting_word(int setting)
{
	return setting == MAP_DOWNLOADS_ALWAYS ? "always" : setting == MAP_DOWNLOADS_NEVER ? "never" : "ask";
}

/* ---------- hashed names (the naming other builds use for shared maps) */

int map_archive_hashed_name(const char *name, char *base, int base_size, unsigned int *prefix)
{
	size_t length, index;
	unsigned int value = 0;

	if (!name)
		return 0;
	length = strlen(name);
	if (length < MAP_ARCHIVE_HASHED_NAME_DIGITS + 2 || name[length - MAP_ARCHIVE_HASHED_NAME_DIGITS - 1] != '_')
		return 0;
	for (index = length - MAP_ARCHIVE_HASHED_NAME_DIGITS; index < length; index++)
	{
		int character = lower((unsigned char)name[index]);

		if (character >= '0' && character <= '9')
			value = (value << 4) | (unsigned int)(character - '0');
		else if (character >= 'a' && character <= 'f')
			value = (value << 4) | (unsigned int)(character - 'a' + 10);
		else
			return 0;
	}
	if (base && base_size > 0)
		snprintf(base, (size_t)base_size, "%.*s", (int)(length - MAP_ARCHIVE_HASHED_NAME_DIGITS - 1), name);
	if (prefix)
		*prefix = value;
	return 1;
}

/* Halo PC's own maps and files, by their file names */
static const char *const stock_maps[] =
{
	/* multiplayer */
	"beavercreek", "bloodgulch", "boardingaction", "carousel", "chillout", "damnation", "dangercanyon", "deathisland",
	"gephyrophobia", "hangemhigh", "icefields", "infinity", "longest", "prisoner", "putput", "ratrace", "sidewinder",
	"timberland", "wizard",
	/* the campaign */
	"a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40",
};
static const char *const stock_files[] = { "ui", "bitmaps", "sounds", "loc" };

int map_archive_stock_name(const char *name)
{
	char base[64];
	size_t index;

	if (!name || !*name)
		return 0;
	/* (a hashed name's base: map_archive_display_name, lowercase) */
	map_archive_display_name(name, base, (int)sizeof(base));
	for (index = 0; base[index]; index++)
		base[index] = (char)lower((unsigned char)base[index]);
	for (index = 0; index < sizeof(stock_maps) / sizeof(stock_maps[0]); index++)
	{
		if (!strcmp(base, stock_maps[index]))
			return MAP_ARCHIVE_STOCK_MAP;
	}
	for (index = 0; index < sizeof(stock_files) / sizeof(stock_files[0]); index++)
	{
		if (!strcmp(base, stock_files[index]))
			return MAP_ARCHIVE_STOCK_FILE;
	}
	return 0;
}

int map_archive_stock_message(const char *name, char *text, int size)
{
	char base[64];
	int stock = map_archive_stock_name(name);
	size_t index;

	if (!stock || size <= 0)
		return 0;

	map_archive_display_name(name, base, (int)sizeof(base));
	/* (the install's file names, lowercase) */
	for (index = 0; base[index]; index++)
		base[index] = (char)lower((unsigned char)base[index]);
	snprintf(text, (size_t)size, "%s.map comes with Halo PC. "
		"Copy it from Halo PC (maps_pc) or Custom Edition (maps_ce), then join again.", base);
	return 1;
}

void map_archive_display_name(const char *name, char *display, int size)
{
	size_t index;

	if (size <= 0)
		return;
	if (!map_archive_hashed_name(name, display, size, NULL))
	{
		snprintf(display, (size_t)size, "%s", name ? name : "");
		return;
	}
	for (index = 0; display[index]; index++)
		display[index] = (char)lower((unsigned char)display[index]);
}

int map_archive_map_hash(map_archive_read_proc read, map_archive_stop_proc stop, void *context, unsigned int size,
	unsigned char *hash)
{
	enum
	{
		PIECE_SIZE = 0x40000,
	};
	static const char tag[] = "OpenCE map 1";
	unsigned char size_bytes[4];
	unsigned char piece_hash[MAP_ARCHIVE_HASH_SIZE];
	crypto_blake2b_ctx whole;
	unsigned char *piece = (unsigned char *)malloc(PIECE_SIZE);
	unsigned int offset;
	int ok = piece != NULL;

	size_bytes[0] = (unsigned char)size;
	size_bytes[1] = (unsigned char)(size >> 8);
	size_bytes[2] = (unsigned char)(size >> 16);
	size_bytes[3] = (unsigned char)(size >> 24);
	crypto_blake2b_init(&whole, MAP_ARCHIVE_HASH_SIZE);
	crypto_blake2b_update(&whole, (const unsigned char *)tag, sizeof(tag) - 1);
	crypto_blake2b_update(&whole, size_bytes, sizeof(size_bytes));
	for (offset = 0; ok && offset < size; offset += PIECE_SIZE)
	{
		unsigned int count = size - offset < PIECE_SIZE ? size - offset : PIECE_SIZE;

		ok = (!stop || !stop(context)) && read(context, offset, piece, count);
		if (ok)
		{
			crypto_blake2b(piece_hash, sizeof(piece_hash), piece, count);
			crypto_blake2b_update(&whole, piece_hash, sizeof(piece_hash));
		}
	}
	crypto_blake2b_final(&whole, hash);
	free(piece);
	return ok;
}

unsigned int map_archive_hash_prefix(const unsigned char *hash)
{
	/* (the digits in the order they are written: the bytes big-endian) */
	return (unsigned int)hash[0] << 24 | (unsigned int)hash[1] << 16 | (unsigned int)hash[2] << 8 |
		(unsigned int)hash[3];
}

unsigned int map_archive_hash_version(const unsigned char *hash)
{
	unsigned int version = map_archive_hash_prefix(hash);

	/* (0 is the version a client checks nothing for) */
	return version ? version : 1;
}

int map_archive_version_matches(unsigned int version, unsigned int checksum, unsigned int hash_version)
{
	return !version || version == hash_version || version == checksum;
}
