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
	char const *kind;
} const map_families[NUMBER_OF_MAP_FAMILIES] =
{
	{ "", "", "maps", "Xbox" },
	{ "@ce", "HALO PC", "maps_ce", "Custom Edition" },
	{ "@md", "HALOMD", "maps_md", "HaloMD" },
	{ "@pc", "HALO PC RETAIL", "maps_pc", "Halo PC" },
};

/* the names a game's map has in the game's protocol (its settings and its
advertisement), for each family past the Xbox's: a folder before the file's
name and an ending after it. A Custom Edition map's is OpenCE's
(custom_maps\<name>, since its build-145), which its clients look for in
their custom_maps folder; HaloMD's and Halo PC retail's are ChupathingyCE's
alone, named so that OpenCE's clients find no map of theirs by them and tell
their players the map is missing (their names take letters, digits, _ - .
and spaces) */
static struct
{
	char const *folder;
	char const *ending;
} const map_family_wire_names[NUMBER_OF_MAP_FAMILIES] =
{
	{ "", "" },
	{ "custom_maps\\", "" },
	{ "maps_md\\", ".md" },
	{ "maps_pc\\", ".pc" },
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

char const *map_family_kind(
	short family)
{
	return family >= _map_family_xbox && family < NUMBER_OF_MAP_FAMILIES ? map_families[family].kind : "unknown";
}

void map_family_wire_name(
	char const *map,
	char *wire,
	long size)
{
	char file[MAP_FAMILY_FILE_LENGTH];
	short family = map_family_parse(map, file, sizeof(file));

	if (size <= 0)
		return;
	if (family == _map_family_xbox)
		snprintf(wire, (size_t)size, "%s", map);
	else
		snprintf(wire, (size_t)size, "%s%s%s", map_family_wire_names[family].folder, file,
			map_family_wire_names[family].ending);
}

short map_family_from_wire_name(
	char const *wire,
	char *map,
	long size)
{
	short family;

	for (family = _map_family_custom_edition; family < NUMBER_OF_MAP_FAMILIES; family++)
	{
		char const *folder = map_family_wire_names[family].folder;
		char const *ending = map_family_wire_names[family].ending;
		size_t folder_length = strlen(folder);
		size_t ending_length = strlen(ending);
		size_t length = strlen(wire);
		char const *file = wire + folder_length;
		size_t file_length;

		if (length <= folder_length + ending_length || _strnicmp(wire, folder, folder_length) ||
			_stricmp(wire + length - ending_length, ending))
		{
			continue;
		}
		file_length = length - folder_length - ending_length;
		/* (a file's name alone: no folder in it, nor a family's suffix) */
		if (file_length >= MAP_FAMILY_FILE_LENGTH || memchr(file, '\\', file_length) || memchr(file, '/', file_length) ||
			memchr(file, '@', file_length))
		{
			continue;
		}
		if (size > 0)
			snprintf(map, (size_t)size, "%.*s%s", (int)file_length, file, map_families[family].suffix);
		return family;
	}
	if (size > 0)
		snprintf(map, (size_t)size, "%s", wire);
	return map_family_parse(wire, NULL, 0);
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
	HALO_PC_CACHE_VERSION = 7,
};

/* each family's places, in the order they are looked in: a folder (under
the maps folder, or the data root's) and the suffix its files' names have.
Each family's own folder beside maps first; then the older places, read as
they were: maps\\ce (ChupathingyCE 0.6 and 0.7.0b's Custom Edition maps, and
HaloMD's first), md_maps (their HaloMD maps), OpenCE's custom_maps (its
Custom Edition maps, since build-145), and a file named for its family in
maps itself (<name>@ce.map). The older folders are offered a move into the
new ones (platform_old_map_folders, xbox_files.c) */
struct map_family_place
{
	boolean under_maps;
	char const *folder;
	char const *suffix;
};

static struct map_family_place const custom_edition_places[] =
{
	{ FALSE, "maps_ce\\", "" },
	{ FALSE, "maps_ce\\", "@ce" },
	{ TRUE, "ce\\", "" },
	{ TRUE, "ce\\", "@ce" },
	{ FALSE, "custom_maps\\", "" },
	{ TRUE, "", "@ce" },
};

static struct map_family_place const halomd_places[] =
{
	{ FALSE, "maps_md\\", "" },
	{ FALSE, "maps_md\\", "@md" },
	{ FALSE, "md_maps\\", "" },
	{ FALSE, "md_maps\\", "@md" },
	{ TRUE, "", "@md" },
	/* (where they were first played, beside Custom Edition's) */
	{ FALSE, "maps_ce\\", "" },
	{ FALSE, "maps_ce\\", "@md" },
	{ TRUE, "ce\\", "" },
	{ TRUE, "ce\\", "@md" },
};

static struct map_family_place const halo_pc_places[] =
{
	{ FALSE, "maps_pc\\", "" },
	{ FALSE, "maps_pc\\", "@pc" },
	{ TRUE, "", "@pc" },
};

/* where Custom Edition's resource maps (bitmaps.map, sounds.map, loc.map)
and Halo PC's ui.map are, for every family past the Xbox's: Custom
Edition's maps folder, then its older places */
static struct map_family_place const resource_places[] =
{
	{ FALSE, "maps_ce\\", "" },
	{ TRUE, "ce\\", "" },
	{ FALSE, "custom_maps\\", "" },
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
	case _map_family_halo_pc:
		*count = NUMBEROF(halo_pc_places);
		return halo_pc_places;
	default:
		*count = 0;
		return NULL;
	}
}

static long family_cache_version(
	short family)
{
	return family == _map_family_custom_edition ? CUSTOM_EDITION_CACHE_VERSION : HALO_PC_CACHE_VERSION;
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

long map_family_cache_version(
	short family)
{
	return family_cache_version(family);
}

boolean map_family_resource(
	char const *name,
	char *path,
	long size)
{
	long index;

	for (index = 0; index < NUMBEROF(resource_places); index++)
	{
		char folder[256];
		HANDLE file;

		place_folder(&resource_places[index], folder, sizeof(folder));
		snprintf(path, (size_t)size, "%s%s.map", folder, name);
		file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file != INVALID_HANDLE_VALUE)
		{
			CloseHandle(file);
			return TRUE;
		}
	}
	/* (where it goes: the first place) */
	snprintf(path, (size_t)size, "d:\\%s%s.map", resource_places[0].folder, name);
	return FALSE;
}

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

/* ---------- downloaded maps

Maps that came from elsewhere than the player's own disc or folders (a map
download, once there is one) are played as any other, but their scripts are
held to tighter rules (hs.c, hs_scenario_functions_check): they may not call
what changes the player's settings or other players' games, nor set the
globals that outlive the map. A map is downloaded if the downloader marked
it so (map_downloaded_mark), or if game.downloaded_maps names it (or is "*",
every map), which lets the rules be tried before there is a downloader. */

enum
{
	MAXIMUM_DOWNLOADED_MAPS = 32,
	DOWNLOADED_MAP_NAME_LENGTH = 64,
};

static char map_downloaded_names[MAXIMUM_DOWNLOADED_MAPS][DOWNLOADED_MAP_NAME_LENGTH];
static long map_downloaded_count;

const char *config_string(const char *name);

/* (a name compared as the game names maps: its file's, any case) */
static boolean map_downloaded_name_is(
	char const *name,
	long length,
	char const *map_name)
{
	char const *stripped = strrchr(map_name, '\\');

	stripped = stripped ? stripped + 1 : map_name;
	return length > 0 && (long)strlen(stripped) == length && !_strnicmp(name, stripped, length);
}

void map_downloaded_mark(
	char const *map_name)
{
	long index;

	if (!map_name || !*map_name || map_is_downloaded(map_name))
		return;
	index = map_downloaded_count < MAXIMUM_DOWNLOADED_MAPS ? map_downloaded_count++ : MAXIMUM_DOWNLOADED_MAPS - 1;
	snprintf(map_downloaded_names[index], DOWNLOADED_MAP_NAME_LENGTH, "%s", map_name);
}

boolean map_is_downloaded(
	char const *map_name)
{
	char const *setting = config_string("game.downloaded_maps");
	long index;

	if (!map_name || !*map_name)
		return FALSE;
	for (index = 0; index < map_downloaded_count; index++)
	{
		if (map_downloaded_name_is(map_downloaded_names[index], (long)strlen(map_downloaded_names[index]), map_name))
			return TRUE;
	}
	/* (game.downloaded_maps: names separated by commas or spaces, or "*") */
	while (setting && *setting)
	{
		long length = (long)strcspn(setting, ", ");

		if ((length == 1 && *setting == '*') || map_downloaded_name_is(setting, length, map_name))
			return TRUE;
		setting += length;
		setting += strspn(setting, ", ");
	}
	return FALSE;
}
