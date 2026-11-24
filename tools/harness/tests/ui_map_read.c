/*
UI_MAP_READ.C (test)

The real ui_map_read (port/linux/game/menu_tags.c) over made-up cache files:
it reads ui.map's copy in the cache for the settings' pictures in a game
map, and only from a file whose header names "ui" and has the checksum the
main menu's build found, and only bytes that lie past the header and within
the length the header gives. The file layer's calls are stand-ins over a
file the case writes. test_ui_map_read.py takes the code (under_test.inc).
*/

#include "harness.h"

typedef void *HANDLE;
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFUL
#define GENERIC_READ 0x80000000UL
#define FILE_SHARE_READ 1
#define OPEN_EXISTING 3
#define FILE_BEGIN 0

static const char *file_path;

static HANDLE CreateFileA(const char *name, unsigned long access, unsigned long share, void *security,
	unsigned long disposition, unsigned long flags, HANDLE template_file)
{
	FILE *file;

	(void)name, (void)access, (void)share, (void)security, (void)disposition, (void)flags, (void)template_file;
	file = file_path ? fopen(file_path, "rb") : NULL;
	return file ? (HANDLE)file : INVALID_HANDLE_VALUE;
}

static int ReadFile(HANDLE handle, void *buffer, unsigned long size, unsigned long *done, void *overlapped)
{
	(void)overlapped;
	*done = (unsigned long)fread(buffer, 1, size, (FILE *)handle);
	return 1;
}

static unsigned long SetFilePointer(HANDLE handle, long offset, long *high, unsigned long method)
{
	(void)high, (void)method;
	return fseek((FILE *)handle, offset, SEEK_SET) ? INVALID_SET_FILE_POINTER : (unsigned long)offset;
}

static int CloseHandle(HANDLE handle)
{
	fclose((FILE *)handle);
	return 1;
}

#include "under_test.inc"

#define CHECKSUM 0xCDC1A39AUL
#define LENGTH 0x4000

/* a cache file of the given length (its header says header_length), named
name, with the checksum; its bytes past the header count up */
static void cache_file(const char *path, long length, long header_length, const char *name, unsigned long checksum)
{
	FILE *file = fopen(path, "wb");
	byte header[UI_MAP_HEADER_SIZE];
	long index;

	memset(header, 0, sizeof(header));
	memcpy(header, "daeh", 4);
	memcpy(header + UI_MAP_HEADER_LENGTH_OFFSET, &header_length, sizeof(header_length));
	strncpy((char *)header + UI_MAP_HEADER_NAME_OFFSET, name, UI_MAP_HEADER_NAME_SIZE);
	memcpy(header + UI_MAP_HEADER_CHECKSUM_OFFSET, &checksum, sizeof(checksum));
	fwrite(header, 1, length < (long)sizeof(header) ? length : (long)sizeof(header), file);
	for (index = sizeof(header); index < length; index++)
		fputc((int)(index & 0xFF), file);
	fclose(file);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	char path[512];
	byte buffer[256];

	snprintf(path, sizeof(path), "%s.cache", argv[0]);
	file_path = path;

	CASE("reads-ui-map")
	{
		cache_file(path, LENGTH, LENGTH, "ui", CHECKSUM);
		memset(buffer, 0, sizeof(buffer));
		CHECK(ui_map_read(CHECKSUM, 0x1000, 64, buffer), "the read was refused");
		CHECK(buffer[0] == 0x00 && buffer[1] == 0x01 && buffer[63] == 0x3F, "wrong bytes %02x %02x %02x",
			buffer[0], buffer[1], buffer[63]);
		CHECK(ui_map_read(CHECKSUM, LENGTH - 64, 64, buffer), "the file's last bytes were refused");
		return 0;
	}
	CASE("another-map-or-checksum")
	{
		cache_file(path, LENGTH, LENGTH, "bloodgulch", CHECKSUM);
		CHECK(!ui_map_read(CHECKSUM, 0x1000, 64, buffer), "read from a cache file of another map");
		cache_file(path, LENGTH, LENGTH, "ui", CHECKSUM + 1);
		CHECK(!ui_map_read(CHECKSUM, 0x1000, 64, buffer), "read from another ui.map");
		/* (a name that fills its field, with no end: not "ui") */
		cache_file(path, LENGTH, LENGTH, "uiuiuiuiuiuiuiuiuiuiuiuiuiuiuiui", CHECKSUM);
		CHECK(!ui_map_read(CHECKSUM, 0x1000, 64, buffer), "read from a cache file with an unended name");
		return 0;
	}
	CASE("range-outside-the-map")
	{
		/* (more in the file than the map's length: what follows it is not
		ui.map's) */
		cache_file(path, LENGTH + 0x1000, LENGTH, "ui", CHECKSUM);
		CHECK(!ui_map_read(CHECKSUM, 0x10, 64, buffer), "read inside the header");
		CHECK(!ui_map_read(CHECKSUM, -64, 64, buffer), "read at a negative offset");
		CHECK(!ui_map_read(CHECKSUM, LENGTH - 32, 64, buffer), "read past the map's end");
		CHECK(!ui_map_read(CHECKSUM, 0x1000, 0, buffer), "read of nothing");
		CHECK(!ui_map_read(CHECKSUM, 0x1000, -1, buffer), "read of a negative size");
		CHECK(!ui_map_read(CHECKSUM, 0x7FFFFFF0, 64, buffer), "read near the largest offset");
		return 0;
	}
	CASE("header-longer-than-file")
	{
		/* (the header says more than the file has: the read comes up short) */
		cache_file(path, 0x1000, LENGTH, "ui", CHECKSUM);
		CHECK(!ui_map_read(CHECKSUM, 0x2000, 64, buffer), "read past the file's end");
		/* (a file shorter than a header) */
		cache_file(path, 0x100, LENGTH, "ui", CHECKSUM);
		CHECK(!ui_map_read(CHECKSUM, 0x1000, 64, buffer), "read from a file without a whole header");
		/* (no file) */
		remove(path);
		CHECK(!ui_map_read(CHECKSUM, 0x1000, 64, buffer), "read with no file");
		return 0;
	}
	return 2;
}
