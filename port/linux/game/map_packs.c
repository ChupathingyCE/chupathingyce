/*
MAP_PACKS.C

Community packs of Xbox maps, each in its maps_<pack> folder
(halo_map_packs.h): their files' fingerprints, their names (the library of
known Xbox mod maps, then the pack's maplist.txt, then the file's name), the
multiplayer maps in the folders, and a pack's own menus (game.pack_menus).
Built on every build that has the menus' map list (HALO_MAP_LIST), the
original Xbox's among them: it reads files as the game does, with no more
than a few KB kept.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "halo_map_families.h"
#include "halo_map_packs.h"
#include "xbox_mod_maps.h"

#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* the fingerprint's samples (halo_map_packs.h) */
	FINGERPRINT_SAMPLES = 16,
	FINGERPRINT_SAMPLE_SIZE = 0x10000,
	FINGERPRINT_CHUNK = 2048,
	/* the files whose fingerprints are kept for the run */
	FINGERPRINTS_KEPT = 24,
	/* a pack's map's name, <file>@<pack>, at most: its cache slot's name, in
	the header's 32 bytes (cache_files_windows.c) */
	MAP_PACK_MAP_NAME_LENGTH = 32,
	MAP_PACK_FILE_LENGTH = MAP_PACK_MAP_NAME_LENGTH - 2,
	/* a maplist.txt, at most */
	MAPLIST_SIZE = 8192,
	/* a cache file's header, as far as it is read: its signature ('head',
	read as bytes), version and type (a short at 0x60: 1 multiplayer) */
	CACHE_HEADER_READ = 0x64,
	CACHE_HEADER_TYPE_OFFSET = 0x60,
	CACHE_TYPE_MULTIPLAYER = 1,
	XBOX_CACHE_VERSION = 5,
	/* the pack folders listed, at most */
	MAXIMUM_PACKS = 16,
};

/* ---------- prototypes */

char const *config_string(char const *name);
char const *cache_files_map_directory(void);

/* ---------- globals */

static struct
{
	char pack[MAP_PACK_ID_LENGTH + 1];
	char file[MAP_PACK_FILE_LENGTH];
	unsigned long size;
	unsigned long time_low;
	unsigned long time_high;
	unsigned long fingerprint;
} map_pack_fingerprints[FINGERPRINTS_KEPT];
static long map_pack_fingerprint_count;
static long map_pack_fingerprint_next;

/* a pack's maplist.txt, the last one asked for, kept until a list of the
maps reads them anew */
static struct
{
	boolean read;
	char pack[MAP_PACK_ID_LENGTH + 1];
	char text[MAPLIST_SIZE];
} map_pack_maplist;

/* ---------- private code */

static unsigned long fnv1a(
	unsigned long value,
	unsigned char const *bytes,
	unsigned long count)
{
	unsigned long index;

	for (index = 0; index < count; index++)
		value = ((value ^ bytes[index]) * 16777619UL) & 0xFFFFFFFFUL;
	return value;
}

static boolean pack_valid(
	char const *pack)
{
	return pack && map_pack_id_valid(pack, (long)strlen(pack));
}

/* a file's name a pack's map can have: no folder, no family's suffix, short
enough for its cache slot's name with its pack's */
static boolean file_name_valid(
	char const *pack,
	char const *file)
{
	return pack_valid(pack) && file && file[0] && strlen(file) + 1 + strlen(pack) < MAP_PACK_MAP_NAME_LENGTH &&
		!strpbrk(file, "\\/:@");
}

static boolean file_path(
	char const *pack,
	char const *file,
	char *path,
	long size)
{
	int length;

	if (!file_name_valid(pack, file))
		return FALSE;
	length = snprintf(path, (size_t)size, "d:\\maps_%s\\%s.map", pack, file);
	return length > 0 && length < size;
}

static boolean file_exists(
	char const *path)
{
	HANDLE handle = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (handle == INVALID_HANDLE_VALUE)
		return FALSE;
	CloseHandle(handle);
	return TRUE;
}

static int handle_read(
	void *context,
	unsigned long offset,
	void *buffer,
	unsigned long size)
{
	HANDLE file = *(HANDLE *)context;
	unsigned long bytes_read = 0;

	if (SetFilePointer(file, (long)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		return 0;
	return ReadFile(file, buffer, size, &bytes_read, NULL) && bytes_read == size;
}

/* a pack's maplist.txt ("" without one) */
static char const *maplist_text(
	char const *pack)
{
	if (!map_pack_maplist.read || _stricmp(map_pack_maplist.pack, pack))
	{
		char path[64];
		HANDLE file;
		unsigned long bytes_read = 0;

		map_pack_maplist.read = TRUE;
		snprintf(map_pack_maplist.pack, sizeof(map_pack_maplist.pack), "%s", pack);
		map_pack_maplist.text[0] = 0;
		snprintf(path, sizeof(path), "d:\\maps_%s\\maplist.txt", pack);
		file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file != INVALID_HANDLE_VALUE)
		{
			if (!ReadFile(file, map_pack_maplist.text, sizeof(map_pack_maplist.text) - 1, &bytes_read, NULL))
				bytes_read = 0;
			map_pack_maplist.text[bytes_read] = 0;
			CloseHandle(file);
		}
	}
	return map_pack_maplist.text;
}

/* a file's name made readable (hugeass_v2: Hugeass V2) */
static void tidy_name(
	char const *file,
	char *name,
	long size)
{
	long length = 0;
	boolean space = FALSE;

	for (; *file && length < size - 1; file++)
	{
		char character = *file;

		if (character == '_' || character == '-' || character == '.' || character == ' ')
		{
			space = length > 0;
			continue;
		}
		if (space && length < size - 2)
		{
			name[length++] = ' ';
			space = FALSE;
		}
		if ((length == 0 || name[length - 1] == ' ') && character >= 'a' && character <= 'z')
			character = (char)(character - 'a' + 'A');
		name[length++] = character;
	}
	name[length] = 0;
}

/* a pack's multiplayer maps, passed to found */
static void list_pack(
	char const *pack,
	void (*found)(char const *pack, char const *file, void *context),
	void *context)
{
	WIN32_FIND_DATAA data;
	char pattern[64];
	HANDLE find;

	snprintf(pattern, sizeof(pattern), "d:\\maps_%s\\*.map", pack);
	find = FindFirstFileA(pattern, &data);
	if (find == INVALID_HANDLE_VALUE)
		return;
	do
	{
		char file[MAP_PACK_FILE_LENGTH], path[256];
		unsigned char header[CACHE_HEADER_READ];
		size_t length = strlen(data.cFileName);
		unsigned long bytes_read = 0;
		HANDLE handle;
		boolean multiplayer = FALSE;

		if (length <= 4 || length - 4 >= sizeof(file) || _stricmp(data.cFileName + length - 4, ".map"))
			continue;
		snprintf(file, sizeof(file), "%.*s", (int)(length - 4), data.cFileName);
		if (!_stricmp(file, "ui") || !file_path(pack, file, path, sizeof(path)))
			continue;
		handle = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (handle == INVALID_HANDLE_VALUE)
			continue;
		/* (an Xbox cache file's header, as bytes: 'head' and the version
		little endian, as the file has them on every machine) */
		if (ReadFile(handle, header, sizeof(header), &bytes_read, NULL) && bytes_read == sizeof(header))
		{
			multiplayer = !memcmp(header, "daeh", 4) && header[4] == XBOX_CACHE_VERSION && !header[5] && !header[6] &&
				!header[7] && header[CACHE_HEADER_TYPE_OFFSET] == CACHE_TYPE_MULTIPLAYER &&
				!header[CACHE_HEADER_TYPE_OFFSET + 1];
		}
		CloseHandle(handle);
		if (multiplayer)
			found(pack, file, context);
	}
	while (FindNextFileA(find, &data));
	CloseHandle(find);
}

/* ---------- public code */

unsigned long map_pack_fingerprint_read(
	unsigned long size,
	map_pack_read_proc read,
	void *context)
{
	unsigned char bytes[FINGERPRINT_CHUNK];
	unsigned long value = 2166136261UL;
	long sample;

	bytes[0] = (unsigned char)(size & 0xFF);
	bytes[1] = (unsigned char)((size >> 8) & 0xFF);
	bytes[2] = (unsigned char)((size >> 16) & 0xFF);
	bytes[3] = (unsigned char)((size >> 24) & 0xFF);
	value = fnv1a(value, bytes, 4);
	for (sample = 0; sample < FINGERPRINT_SAMPLES; sample++)
	{
		unsigned long offset = size / FINGERPRINT_SAMPLES * (unsigned long)sample;
		unsigned long left = size - offset < FINGERPRINT_SAMPLE_SIZE ? size - offset : FINGERPRINT_SAMPLE_SIZE;

		while (left)
		{
			unsigned long count = left < FINGERPRINT_CHUNK ? left : FINGERPRINT_CHUNK;

			if (!read(context, offset, bytes, count))
				return 0;
			value = fnv1a(value, bytes, count);
			offset += count;
			left -= count;
		}
	}
	return value ? value : 1;
}

unsigned long map_pack_fingerprint(
	char const *pack,
	char const *file)
{
	char path[256];
	FILETIME written;
	unsigned long size, fingerprint;
	long index;
	HANDLE handle;

	if (!file_path(pack, file, path, sizeof(path)))
		return 0;
	handle = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
		return 0;
	size = GetFileSize(handle, NULL);
	memset(&written, 0, sizeof(written));
	GetFileTime(handle, NULL, NULL, &written);
	for (index = 0; index < map_pack_fingerprint_count; index++)
	{
		if (!_stricmp(map_pack_fingerprints[index].pack, pack) && !_stricmp(map_pack_fingerprints[index].file, file) &&
			map_pack_fingerprints[index].size == size &&
			map_pack_fingerprints[index].time_low == (unsigned long)written.dwLowDateTime &&
			map_pack_fingerprints[index].time_high == (unsigned long)written.dwHighDateTime)
		{
			CloseHandle(handle);
			return map_pack_fingerprints[index].fingerprint;
		}
	}
	fingerprint = size == INVALID_FILE_SIZE ? 0 : map_pack_fingerprint_read(size, handle_read, &handle);
	CloseHandle(handle);
	if (!fingerprint)
		return 0;
	if (map_pack_fingerprint_count < FINGERPRINTS_KEPT)
		index = map_pack_fingerprint_count++;
	else
	{
		index = map_pack_fingerprint_next;
		map_pack_fingerprint_next = (map_pack_fingerprint_next + 1) % FINGERPRINTS_KEPT;
	}
	snprintf(map_pack_fingerprints[index].pack, sizeof(map_pack_fingerprints[index].pack), "%s", pack);
	snprintf(map_pack_fingerprints[index].file, sizeof(map_pack_fingerprints[index].file), "%s", file);
	map_pack_fingerprints[index].size = size;
	map_pack_fingerprints[index].time_low = (unsigned long)written.dwLowDateTime;
	map_pack_fingerprints[index].time_high = (unsigned long)written.dwHighDateTime;
	map_pack_fingerprints[index].fingerprint = fingerprint;
	return fingerprint;
}

boolean map_pack_present(
	char const *pack,
	char const *file)
{
	char path[256];

	return file_path(pack, file, path, sizeof(path)) && file_exists(path);
}

boolean map_pack_map_name(
	char const *pack,
	char const *file,
	char *map,
	long size)
{
	int length;

	if (size > 0)
		map[0] = 0;
	if (!file_name_valid(pack, file) || size <= 0)
		return FALSE;
	length = snprintf(map, (size_t)size, "%s@%s", file, pack);
	if (length < 0 || length >= size)
	{
		map[0] = 0;
		return FALSE;
	}
	return TRUE;
}

struct xbox_mod_pack const *map_pack_library_pack(
	char const *pack)
{
	long index;

	for (index = 0; pack && index < (long)NUMBEROF(xbox_mod_packs); index++)
	{
		if (!_stricmp(xbox_mod_packs[index].id, pack))
			return &xbox_mod_packs[index];
	}
	return NULL;
}

void map_pack_folder(
	char const *pack,
	char *folder,
	long size)
{
	snprintf(folder, (size_t)size, "maps_%s", pack ? pack : "");
}

void map_pack_title(
	char const *pack,
	char *title,
	long size)
{
	struct xbox_mod_pack const *known = map_pack_library_pack(pack);

	if (known)
		snprintf(title, (size_t)size, "%s", known->name);
	else
		snprintf(title, (size_t)size, "the maps_%s pack", pack ? pack : "");
}

void map_pack_tag(
	char const *pack,
	char *tag,
	long size)
{
	struct xbox_mod_pack const *known = map_pack_library_pack(pack);
	long index;

	if (known)
	{
		snprintf(tag, (size_t)size, "%s", known->tag);
		return;
	}
	for (index = 0; pack && pack[index] && index < size - 1; index++)
		tag[index] = (char)(pack[index] >= 'a' && pack[index] <= 'z' ? pack[index] - 'a' + 'A' : pack[index]);
	if (size > 0)
		tag[index] = 0;
}

struct xbox_mod_map const *map_pack_library_find(
	unsigned long fingerprint)
{
	long index;

	for (index = 0; fingerprint && index < (long)NUMBEROF(xbox_mod_maps); index++)
	{
		if (xbox_mod_maps[index].fingerprint == fingerprint)
			return &xbox_mod_maps[index];
	}
	return NULL;
}

boolean map_pack_maplist_find(
	char const *text,
	char const *file,
	char *name,
	long size)
{
	size_t file_length = strlen(file);

	while (text && *text)
	{
		size_t line_length = strcspn(text, "\r\n");
		char const *dash = memchr(text, '-', line_length);

		if (dash)
		{
			char const *start = text;
			char const *end = dash;

			while (start < end && (*start == ' ' || *start == '\t'))
				start++;
			while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
				end--;
			if ((size_t)(end - start) == file_length && file_length && !_strnicmp(start, file, file_length))
			{
				start = dash + 1;
				end = text + line_length;
				while (start < end && (*start == ' ' || *start == '\t'))
					start++;
				while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
					end--;
				if (end > start && size > 0)
				{
					snprintf(name, (size_t)size, "%.*s", (int)(end - start), start);
					return TRUE;
				}
			}
		}
		text += line_length;
		text += strspn(text, "\r\n");
	}
	return FALSE;
}

short map_pack_names(
	char const *pack,
	char const *file,
	unsigned long fingerprint,
	char *name,
	long name_size,
	char *pack_name,
	long pack_size)
{
	struct xbox_mod_map const *known;

	if (!fingerprint)
		fingerprint = map_pack_fingerprint(pack, file);
	known = map_pack_library_find(fingerprint);
	/* (the library's pack's name, else the folder's) */
	if (pack_size > 0)
		map_pack_title(known ? known->pack : pack, pack_name, pack_size);
	if (known && !_stricmp(known->file, file) && !_stricmp(known->pack, pack))
	{
		snprintf(name, (size_t)name_size, "%s", known->name);
		return _map_pack_name_library;
	}
	if (pack_size > 0)
		map_pack_title(pack, pack_name, pack_size);
	if (pack_valid(pack) && map_pack_maplist_find(maplist_text(pack), file, name, name_size))
		return _map_pack_name_maplist;
	tidy_name(file, name, name_size);
	return _map_pack_name_file;
}

void map_pack_list(
	void (*found)(char const *pack, char const *file, void *context),
	void *context)
{
	char packs[MAXIMUM_PACKS][MAP_PACK_ID_LENGTH + 1];
	long pack_count = 0, index;
	WIN32_FIND_DATAA data;
	HANDLE find;

	/* (the names asked after this list are the maplist.txts' as they are now) */
	map_pack_maplist.read = FALSE;
	/* (the folders first, the find closed before any other is opened) */
	find = FindFirstFileA("d:\\maps_*", &data);
	if (find != INVALID_HANDLE_VALUE)
	{
		do
		{
			char const *pack = data.cFileName + 5;
			long length = (long)strlen(data.cFileName) - 5;

			if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || _strnicmp(data.cFileName, "maps_", 5) ||
				!map_pack_id_valid(pack, length) || pack_count >= MAXIMUM_PACKS)
			{
				continue;
			}
			for (index = 0; index < length; index++)
			{
				char character = pack[index];

				packs[pack_count][index] = (char)(character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character);
			}
			packs[pack_count][length] = 0;
			pack_count++;
		}
		while (FindNextFileA(find, &data));
		CloseHandle(find);
	}
	for (index = 0; index < pack_count; index++)
		list_pack(packs[index], found, context);
}

/* ---------- a pack's own menus

game.pack_menus names a pack ("cex") whose ui.map is played in place of
the game's, as the pack's own release does: its menus' multiplayer maps are
the Xbox's thirteen names, which the menus' map list gives the pack's maps
of those names (ui_map_list.c), and its scripts may switch to a level of
the pack's (main_set_map_name: map_pack_menus_map_name). Read once, at the
first ask: it takes effect after a restart. */

char const *map_pack_menus(
	void)
{
	static boolean asked;
	static char pack[MAP_PACK_ID_LENGTH + 1];

	if (!asked)
	{
		char const *value = config_string("game.pack_menus");
		char path[64];
		long index;

		asked = TRUE;
		pack[0] = 0;
		if (value && map_pack_id_valid(value, (long)strlen(value)))
		{
			for (index = 0; value[index]; index++)
				pack[index] = (char)(value[index] >= 'A' && value[index] <= 'Z' ? value[index] - 'A' + 'a' : value[index]);
			pack[index] = 0;
			snprintf(path, sizeof(path), "d:\\maps_%s\\ui.map", pack);
			if (!file_exists(path))
				pack[0] = 0;
		}
	}
	return pack[0] ? pack : NULL;
}

boolean map_pack_menus_known(
	void)
{
	char const *pack = map_pack_menus();
	struct xbox_mod_map const *known;

	if (!pack)
		return FALSE;
	known = map_pack_library_find(map_pack_fingerprint(pack, "ui"));
	return known && !_stricmp(known->pack, pack) && !_stricmp(known->file, "ui");
}

void map_pack_menus_map_name(
	char const *map_name,
	char *name,
	long size)
{
	char path[256];
	char const *pack = map_pack_menus();
	char const *file = map_name;
	char const *cursor;

	snprintf(name, (size_t)size, "%s", map_name ? map_name : "");
	if (!map_name || !pack)
		return;
	for (cursor = map_name; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			file = cursor + 1;
	}
	if (!map_pack_present(pack, file))
		return;
	/* (a level the game's maps have is theirs: the pack's menus name the
	campaign's as the game's own do) */
	snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), file);
	if (file_exists(path))
		return;
	{
		char pack_map[64];

		if (map_pack_map_name(pack, file, pack_map, sizeof(pack_map)))
			snprintf(name, (size_t)size, "%s", pack_map);
	}
}
