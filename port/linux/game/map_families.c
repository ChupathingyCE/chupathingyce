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
#include "../src/map_archive.h"
#include "../src/map_file_hash.h"
#include "map_stock_pairs.h"

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
	/* the short tag on a listed game's map, what the tag stands for, and
	its color (map_family_tag) */
	char const *tag;
	char const *title;
	unsigned long color;
} const map_families[NUMBER_OF_MAP_FAMILIES] =
{
	{ "", "", "maps", "Xbox", "XBX", "Xbox map", 0x8CCB7A },
	{ "@ce", "HALO PC", "maps_ce", "Custom Edition", "HCE", "Custom Edition map", 0x6FB2FF },
	{ "@md", "HALOMD", "maps_md", "HaloMD", "HMD", "HaloMD map", 0xB59BFF },
	{ "@pc", "HALO PC RETAIL", "maps_pc", "Halo PC", "GBX", "Halo PC map", 0xD9B45A },
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

/* the maps the Xbox plays, by their file names (a name past these, with no
folder or suffix, is a Halo PC map: OpenCE's hosts announce one by its file
name alone) */
static char const *const xbox_map_files[] =
{
	"beavercreek", "bloodgulch", "boardingaction", "carousel", "chillout", "damnation", "hangemhigh", "longest",
	"prisoner", "putput", "ratrace", "sidewinder", "wizard",
	"a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40", "ui",
};

short map_family_parse_listed(
	char const *map,
	char *file,
	long size)
{
	short family = map_family_parse(map, file, size);
	long index;

	if (family != _map_family_xbox || !map[0] || strpbrk(map, "\\/@"))
		return family;
	for (index = 0; index < NUMBEROF(xbox_map_files); index++)
	{
		if (!_stricmp(map, xbox_map_files[index]))
			return family;
	}
	return _map_family_custom_edition;
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

char const *map_family_tag(
	short family)
{
	return family >= _map_family_xbox && family < NUMBER_OF_MAP_FAMILIES ? map_families[family].tag : "";
}

char const *map_family_title(
	short family)
{
	return family >= _map_family_xbox && family < NUMBER_OF_MAP_FAMILIES ? map_families[family].title : "";
}

unsigned long map_family_color(
	short family)
{
	return family >= _map_family_xbox && family < NUMBER_OF_MAP_FAMILIES ? map_families[family].color :
		MAP_FAMILY_COLOR_MCC;
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
void platform_log(char const *format, ...);

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

/* a hashed name's map (<base>_<8 hex digits>, the naming other builds use
for the maps their hosts share: port/linux/src/map_archive.h) found by its
base name: the first <base>.map in the places whose whole-file hash begins
with the name's digits. The game plays it under the hashed name, which the
host's game names it by */
static boolean hashed_name_find(
	struct map_family_place const *places,
	long count,
	char const *file,
	char *path,
	long size)
{
	static char logged[MAP_FAMILY_FILE_LENGTH + 256];
	char base[MAP_FAMILY_FILE_LENGTH];
	char line[sizeof(logged)];
	unsigned int prefix, found;
	long index;

	if (!map_archive_hashed_name(file, base, sizeof(base), &prefix))
		return FALSE;
	for (index = 0; index < count; index++)
	{
		char folder[256];

		place_folder(&places[index], folder, sizeof(folder));
		snprintf(path, (size_t)size, "%s%s%s.map", folder, base, places[index].suffix);
		if (!cache_file_is(path, CUSTOM_EDITION_CACHE_VERSION, FALSE) || !map_file_hash_prefix(path, &found) ||
			found != prefix)
		{
			continue;
		}
		/* (said once, not at every look for it) */
		snprintf(line, sizeof(line), "%s %s", file, path);
		if (strcmp(line, logged))
		{
			snprintf(logged, sizeof(logged), "%s", line);
			platform_log("map: %s is %s (hash matches)", file, strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path);
		}
		return TRUE;
	}
	return FALSE;
}

/* the network game's map and the version of it the host plays
(map_family_set_game_version), whose file map_family_find gives */
static struct
{
	short family;
	char file[MAP_FAMILY_FILE_LENGTH];
	unsigned long version;
} map_family_game;

/* whether the map file at path is of a host's version (cache_files_map_version
on the host: since network version 29 its whole-file hash's first 32 bits,
before then its header's checksum; 0 for any). The checksum first: no file
is hashed for a host that sends it */
static boolean version_matches(
	char const *path,
	unsigned long version)
{
	unsigned char header[MAP_ARCHIVE_HEADER_SIZE];
	unsigned long bytes_read = 0;
	unsigned int checksum = 0;
	unsigned int hash_version = 0;
	HANDLE file;

	if (!version)
		return TRUE;
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return FALSE;
	if (ReadFile(file, header, sizeof(header), &bytes_read, NULL) && bytes_read == sizeof(header))
		checksum = map_archive_header_checksum(header);
	CloseHandle(file);
	if (map_archive_version_matches((unsigned int)version, checksum, 0))
		return TRUE;
	return map_file_hash_version(path, &hash_version) &&
		map_archive_version_matches((unsigned int)version, checksum, hash_version);
}

/* the first file of a map's name in the family's places (or, for a Custom
Edition map's hashed name, its base's whose hash is the name's) */
static boolean place_file(
	short family,
	char const *file,
	char *path,
	long size)
{
	long count, index;
	struct map_family_place const *places = family_places(family, &count);

	for (index = 0; index < count; index++)
	{
		char folder[256];

		place_folder(&places[index], folder, sizeof(folder));
		snprintf(path, (size_t)size, "%s%s%s.map", folder, file, places[index].suffix);
		if (cache_file_is(path, family_cache_version(family), FALSE))
			return TRUE;
	}
	return FALSE;
}

static boolean find_any(
	short family,
	char const *file,
	char *path,
	long size)
{
	long count, index;
	struct map_family_place const *places = family_places(family, &count);

	if (place_file(family, file, path, size))
		return TRUE;
	/* (a Custom Edition map by a hashed name another build's host gave it:
	its base name's file, in the same places, if its hash is the name's) */
	if (family == _map_family_custom_edition && hashed_name_find(places, count, file, path, size))
		return TRUE;
	/* (one of Halo PC's own maps, which a Custom Edition game names
	custom_maps\<name> whichever edition its host has: the Halo PC file will
	do for presence; whether it is the host's version is find_version's) */
	if (family == _map_family_custom_edition && map_archive_stock_name(file) == MAP_ARCHIVE_STOCK_MAP)
		return find_any(_map_family_halo_pc, file, path, size);
	path[0] = 0;
	return FALSE;
}

/* a stock map's file of the host's version in the other edition's places, or
checked to be the same as it (map_stock_pairs): the player's file of either
edition whose version is the host's, or is the pair of it */
static boolean stock_find(
	char const *file,
	unsigned long version,
	char *path,
	long size)
{
	short edition;
	long pair;

	for (edition = _map_family_custom_edition; edition <= _map_family_halo_pc; edition += _map_family_halo_pc - _map_family_custom_edition)
	{
		long count, index;
		struct map_family_place const *places = family_places(edition, &count);

		for (index = 0; index < count; index++)
		{
			char folder[256];
			unsigned int local;

			place_folder(&places[index], folder, sizeof(folder));
			snprintf(path, (size_t)size, "%s%s%s.map", folder, file, places[index].suffix);
			if (!cache_file_is(path, family_cache_version(edition), FALSE))
				continue;
			if (version_matches(path, version))
				return TRUE;
			if (!map_file_hash_version(path, &local))
				continue;
			for (pair = 0; map_stock_pairs[pair].file; pair++)
			{
				unsigned long here = edition == _map_family_halo_pc ? map_stock_pairs[pair].halo_pc_version :
					map_stock_pairs[pair].custom_edition_version;
				unsigned long there = edition == _map_family_halo_pc ? map_stock_pairs[pair].custom_edition_version :
					map_stock_pairs[pair].halo_pc_version;

				if (!_stricmp(file, map_stock_pairs[pair].file) && local == here && (version & 0xffffffffUL) == there)
					return TRUE;
			}
		}
	}
	path[0] = 0;
	return FALSE;
}

static boolean find_version(
	short family,
	char const *file,
	unsigned long version,
	char *path,
	long size)
{
	long count, index;
	struct map_family_place const *places = family_places(family, &count);

	for (index = 0; index < count; index++)
	{
		char folder[256];

		place_folder(&places[index], folder, sizeof(folder));
		snprintf(path, (size_t)size, "%s%s%s.map", folder, file, places[index].suffix);
		if (cache_file_is(path, family_cache_version(family), FALSE) && version_matches(path, version))
			return TRUE;
	}
	if (family == _map_family_custom_edition)
	{
		/* (another version of the map beside the one of its name, named
		for its hash: <file>_<8 hex digits>.map, the version's, as a
		download beside it is named: map_download.c) */
		for (index = 0; index < count; index++)
		{
			char folder[256];

			place_folder(&places[index], folder, sizeof(folder));
			snprintf(path, (size_t)size, "%s%s_%08lx%s.map", folder, file, version & 0xffffffffUL,
				places[index].suffix);
			if (cache_file_is(path, CUSTOM_EDITION_CACHE_VERSION, FALSE) && version_matches(path, version))
				return TRUE;
		}
		/* (a hashed name an older host gave the map: its base's file) */
		if (hashed_name_find(places, count, file, path, size) && version_matches(path, version))
			return TRUE;
		/* (one of Halo PC's own maps: the host's edition of it may be the
		Halo PC file, or the other edition's file that is checked to play
		the same: map_stock_pairs) */
		if (map_archive_stock_name(file) == MAP_ARCHIVE_STOCK_MAP && stock_find(file, version, path, size))
			return TRUE;
	}
	path[0] = 0;
	return FALSE;
}

/* a map file's name a map can be found by: no folder in it */
static boolean file_name_valid(
	char const *file)
{
	return file[0] && strlen(file) < MAP_FAMILY_FILE_LENGTH && !strchr(file, '\\') && !strchr(file, '/') &&
		!strchr(file, ':');
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

long map_family_stock_need(
	char const *file,
	unsigned long version,
	char *first,
	long first_size,
	char *second,
	long second_size,
	char *blocker,
	long blocker_size)
{
	short edition;
	boolean held[NUMBER_OF_MAP_FAMILIES] = { FALSE };
	char display[MAP_FAMILY_FILE_LENGTH];
	long needed = _map_family_xbox;

	map_archive_display_name(file, display, (int)sizeof(display));
	for (edition = _map_family_custom_edition; edition <= _map_family_halo_pc; edition += _map_family_halo_pc - _map_family_custom_edition)
	{
		char path[256];

		held[edition] = place_file(edition, display, path, sizeof(path));
	}
	/* (a file of one edition that is not the host's: the host plays the other) */
	if (version && held[_map_family_halo_pc] && !held[_map_family_custom_edition])
		needed = _map_family_custom_edition;
	else if (version && held[_map_family_custom_edition] && !held[_map_family_halo_pc])
		needed = _map_family_halo_pc;
	if (needed == _map_family_custom_edition)
	{
		snprintf(first, (size_t)first_size, "%s.map: this game needs the Custom Edition file.", display);
		snprintf(second, (size_t)second_size, "Copy it from the Custom Edition installer\ninto maps_ce, then join again.");
		snprintf(blocker, (size_t)blocker_size, "Needs the Custom Edition %s.map in maps_ce to join", display);
	}
	else if (needed == _map_family_halo_pc)
	{
		snprintf(first, (size_t)first_size, "%s.map: this game needs the Halo PC file.", display);
		snprintf(second, (size_t)second_size, "Copy it from your Halo PC install\ninto maps_pc, then join again.");
		snprintf(blocker, (size_t)blocker_size, "Needs the Halo PC %s.map in maps_pc to join", display);
	}
	else
	{
		snprintf(first, (size_t)first_size, "%s.map comes with Halo PC.", display);
		snprintf(second, (size_t)second_size, "Copy it from Halo PC (maps_pc) or\nCustom Edition (maps_ce), then join again.");
		snprintf(blocker, (size_t)blocker_size, "%s.map comes with Halo PC: copy it into maps_pc or maps_ce to join", display);
	}
	return needed;
}

boolean map_family_find(
	short family,
	char const *file,
	char *path,
	long size)
{
	if (!file_name_valid(file))
		return FALSE;
	/* (the network game's map: the file of the host's version, if there is
	one; any other is found as before, and cache_files_map_present has told
	the player it is not the host's) */
	if (map_family_game.version && map_family_game.family == family && !_stricmp(map_family_game.file, file) &&
		find_version(family, file, map_family_game.version, path, size))
	{
		return TRUE;
	}
	return find_any(family, file, path, size);
}

boolean map_family_find_version(
	short family,
	char const *file,
	unsigned long version,
	char *path,
	long size)
{
	if (!file_name_valid(file))
		return FALSE;
	return version ? find_version(family, file, version, path, size) : find_any(family, file, path, size);
}

void map_family_set_game_version(
	char const *map,
	unsigned long version)
{
	char file[MAP_FAMILY_FILE_LENGTH];
	short family = map_family_parse(map ? map : "", file, sizeof(file));

	if (!map || family == _map_family_xbox || !version)
	{
		map_family_game.version = 0;
		return;
	}
	map_family_game.family = family;
	snprintf(map_family_game.file, sizeof(map_family_game.file), "%s", file);
	map_family_game.version = version;
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
it so (map_downloaded_remember: Custom Edition maps from the community map
repositories, port/linux/src/map_download.c, listed in
maps_ce/downloaded_maps.txt for the next runs), or if game.downloaded_maps
names it (or is "*", every map). */

enum
{
#ifdef HALO_XBOX_CONSOLE
	/* (the console downloads none: only a game.downloaded_maps' names) */
	MAXIMUM_DOWNLOADED_MAPS = 8,
#else
	MAXIMUM_DOWNLOADED_MAPS = 512,
#endif
	DOWNLOADED_MAP_NAME_LENGTH = 64,
};

/* the maps the downloader put in maps_ce, one name a line (map_download.c):
they stay downloaded maps when the game starts again */
#define DOWNLOADED_MAPS_LIST "d:\\maps_ce\\downloaded_maps.txt"

static char map_downloaded_names[MAXIMUM_DOWNLOADED_MAPS][DOWNLOADED_MAP_NAME_LENGTH];
static long map_downloaded_count;
static boolean map_downloaded_list_read;

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

/* the list of the maps the downloader put in maps_ce, read once */
static void map_downloaded_read_list(
	void)
{
#ifdef HALO_XBOX_CONSOLE
	/* (the console downloads no maps: no list, and no 32 KB to read one in) */
	map_downloaded_list_read = TRUE;
}
#else
	static char text[MAXIMUM_DOWNLOADED_MAPS * DOWNLOADED_MAP_NAME_LENGTH];
	unsigned long bytes_read = 0;
	HANDLE file;
	char *line;

	if (map_downloaded_list_read)
		return;
	map_downloaded_list_read = TRUE;
	file = CreateFileA(DOWNLOADED_MAPS_LIST, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return;
	if (!ReadFile(file, text, sizeof(text) - 1, &bytes_read, NULL))
		bytes_read = 0;
	CloseHandle(file);
	text[bytes_read] = 0;
	for (line = text; *line;)
	{
		size_t length = strcspn(line, "\r\n");
		char name[DOWNLOADED_MAP_NAME_LENGTH];

		if (length && length < sizeof(name))
		{
			snprintf(name, sizeof(name), "%.*s", (int)length, line);
			map_downloaded_mark(name);
		}
		line += length;
		line += strspn(line, "\r\n");
	}
}
#endif

void map_downloaded_remember(
	char const *map_name)
{
	unsigned long written = 0;
	char line[DOWNLOADED_MAP_NAME_LENGTH + 2];
	HANDLE file;

	if (!map_name || !*map_name || strlen(map_name) >= DOWNLOADED_MAP_NAME_LENGTH)
		return;
	map_downloaded_read_list();
	if (map_is_downloaded(map_name))
		return;
	map_downloaded_mark(map_name);
	file = CreateFileA(DOWNLOADED_MAPS_LIST, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return;
	SetFilePointer(file, 0, NULL, FILE_END);
	snprintf(line, sizeof(line), "%s\n", map_name);
	WriteFile(file, line, (unsigned long)strlen(line), &written, NULL);
	CloseHandle(file);
}

void map_downloaded_mark(
	char const *map_name)
{
	long index;

	map_downloaded_read_list();
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
	map_downloaded_read_list();
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
