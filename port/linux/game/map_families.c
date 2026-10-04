/*
MAP_FAMILIES.C

The families of maps the native builds play, by their names' suffixes, and
where each family's files are (halo_map_families.h). The names are read on
every build (the server browser names a game's map on builds that cannot
play it); the files are looked for only on the builds that play such maps
(HALO_CUSTOM_EDITION).
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "halo_map_families.h"

#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* a map file's name, at most (the menus' and the browser's) */
	MAP_FAMILY_FILE_LENGTH = 64,
	/* the file names one family's folders hold, at most, listed once each */
	MAXIMUM_LISTED_FILES = 256,
};

/* ---------- globals */

static struct
{
	char const *suffix;
	char const *badge;
	char const *folder;
} const map_families[NUMBER_OF_MAP_FAMILIES] =
{
	{ "", "", "maps" },
	{ "@ce", "HALO PC", "maps/ce" },
	{ "@md", "HALOMD", "md_maps" },
};

/* ---------- public code */

short map_family_parse(
	char const *map,
	char *file,
	long size)
{
	char const *base = map;
	char const *cursor;
	long length;
	short family;

	for (cursor = map; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			base = cursor + 1;
	}
	length = (long)strlen(base);
	for (family = NUMBER_OF_MAP_FAMILIES - 1; family > _map_family_xbox; family--)
	{
		long suffix_length = (long)strlen(map_families[family].suffix);

		if (length > suffix_length && !_stricmp(base + length - suffix_length, map_families[family].suffix))
		{
			length -= suffix_length;
			break;
		}
	}
	if (file && size > 0)
		snprintf(file, (size_t)size, "%.*s", (int)length, base);
	return family;
}

char const *map_family_suffix(
	short family)
{
	return family > _map_family_xbox && family < NUMBER_OF_MAP_FAMILIES ? map_families[family].suffix : "";
}

char const *map_family_badge(
	short family)
{
	return family > _map_family_xbox && family < NUMBER_OF_MAP_FAMILIES ? map_families[family].badge : "";
}

char const *map_family_folder(
	short family)
{
	return family >= _map_family_xbox && family < NUMBER_OF_MAP_FAMILIES ? map_families[family].folder : "maps";
}

#ifdef HALO_CUSTOM_EDITION

/* ---------- constants */

enum
{
	/* a cache file's header: 'head', its version, ..., its type (a short
	at 0x60: 1 multiplayer) */
	CACHE_HEADER_SIGNATURE = 'head',
	CACHE_HEADER_TYPE_OFFSET = 0x60,
	CACHE_TYPE_MULTIPLAYER = 1,
	CUSTOM_EDITION_CACHE_VERSION = 609,
	HALOMD_CACHE_VERSION = 7,
};

/* each family's places, in the order they are looked in: a folder (under
the maps folder, or the data root's) and the suffix its files' names have */
struct map_family_place
{
	boolean under_maps;
	char const *folder;
	char const *suffix;
};

static struct map_family_place const custom_edition_places[] =
{
	{ TRUE, "ce\\", "" },
	{ TRUE, "ce\\", "@ce" },
	{ TRUE, "", "@ce" },
};

static struct map_family_place const halomd_places[] =
{
	{ FALSE, "md_maps\\", "" },
	{ FALSE, "md_maps\\", "@md" },
	{ TRUE, "", "@md" },
	/* (where they were first played, beside Custom Edition's) */
	{ TRUE, "ce\\", "" },
	{ TRUE, "ce\\", "@md" },
};

/* ---------- prototypes */

char const *cache_files_map_directory(void);

/* ---------- private code */

static struct map_family_place const *family_places(
	short family,
	long *count)
{
	switch (family)
	{
	case _map_family_custom_edition:
		*count = NUMBEROF(custom_edition_places);
		return custom_edition_places;
	case _map_family_halomd:
		*count = NUMBEROF(halomd_places);
		return halomd_places;
	default:
		*count = 0;
		return NULL;
	}
}

static long family_cache_version(
	short family)
{
	return family == _map_family_halomd ? HALOMD_CACHE_VERSION : CUSTOM_EDITION_CACHE_VERSION;
}

/* a place's folder, as a path ending in its separator */
static void place_folder(
	struct map_family_place const *place,
	char *path,
	long size)
{
	snprintf(path, (size_t)size, "%s%s", place->under_maps ? cache_files_map_directory() : "d:\\", place->folder);
}

/* whether a file is a cache file of a version (and a multiplayer map's, if
asked) */
static boolean cache_file_is(
	char const *path,
	long version,
	boolean multiplayer)
{
	unsigned long header[(CACHE_HEADER_TYPE_OFFSET + 4) / 4];
	unsigned long bytes_read = 0;
	HANDLE file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	boolean result = FALSE;

	if (file == INVALID_HANDLE_VALUE)
		return FALSE;
	if (ReadFile(file, header, sizeof(header), &bytes_read, NULL) && bytes_read == sizeof(header))
	{
		result = header[0] == CACHE_HEADER_SIGNATURE && header[1] == (unsigned long)version &&
			(!multiplayer || (header[CACHE_HEADER_TYPE_OFFSET / 4] & 0xffff) == CACHE_TYPE_MULTIPLAYER);
	}
	CloseHandle(file);
	return result;
}

/* ---------- public code */

boolean map_family_find(
	short family,
	char const *file,
	char *path,
	long size)
{
	long count, index;
	struct map_family_place const *places = family_places(family, &count);

	if (!file[0] || strlen(file) >= MAP_FAMILY_FILE_LENGTH || strchr(file, '\\') || strchr(file, '/') ||
		strchr(file, ':'))
	{
		return FALSE;
	}
	for (index = 0; index < count; index++)
	{
		char folder[256];

		place_folder(&places[index], folder, sizeof(folder));
		snprintf(path, (size_t)size, "%s%s%s.map", folder, file, places[index].suffix);
		if (cache_file_is(path, family_cache_version(family), FALSE))
			return TRUE;
	}
	path[0] = 0;
	return FALSE;
}

void map_family_list(
	short family,
	void (*found)(char const *file, void *context),
	void *context)
{
	static char listed[MAXIMUM_LISTED_FILES][MAP_FAMILY_FILE_LENGTH];
	long listed_count = 0;
	struct map_family_place const *places;
	long count, index;

	places = family_places(family, &count);
	for (index = 0; index < count; index++)
	{
		char folder[256], pattern[288];
		WIN32_FIND_DATAA data;
		HANDLE find;
		size_t suffix_length = strlen(places[index].suffix);

		place_folder(&places[index], folder, sizeof(folder));
		snprintf(pattern, sizeof(pattern), "%s*.map", folder);
		find = FindFirstFileA(pattern, &data);
		if (find == INVALID_HANDLE_VALUE)
			continue;
		do
		{
			char file[MAP_FAMILY_FILE_LENGTH], path[384];
			size_t length = strlen(data.cFileName);
			long other;

			/* (<file><suffix>.map, its file's name not empty, and in a
			place without a suffix not one named for a family: that is
			another place's) */
			if (length < 4 + suffix_length + 1 || _stricmp(data.cFileName + length - 4, ".map"))
				continue;
			length -= 4;
			if (suffix_length && _strnicmp(data.cFileName + length - suffix_length, places[index].suffix, suffix_length))
				continue;
			length -= suffix_length;
			if (length >= MAP_FAMILY_FILE_LENGTH)
				continue;
			snprintf(file, sizeof(file), "%.*s", (int)length, data.cFileName);
			if (!suffix_length && map_family_parse(file, NULL, 0) != _map_family_xbox)
				continue;
			for (other = 0; other < listed_count && _stricmp(listed[other], file); other++)
				;
			if (other < listed_count || listed_count >= MAXIMUM_LISTED_FILES)
				continue;
			snprintf(path, sizeof(path), "%s%s", folder, data.cFileName);
			if (!cache_file_is(path, family_cache_version(family), TRUE))
				continue;
			snprintf(listed[listed_count++], MAP_FAMILY_FILE_LENGTH, "%s", file);
			found(file, context);
		}
		while (FindNextFileA(find, &data));
		CloseHandle(find);
	}
}

#endif
