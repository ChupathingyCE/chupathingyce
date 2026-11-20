/* MAP_LOADERS.C
	(port) the map loaders, one per map file format, told apart by the
	header alone: every format starts 'daeh' (little-endian 'head') and its
	version; Halo 1's and the Xbox Halo 2's header is 0x800 bytes ('toof'
	at 0x7FC), MCC Halo 2's 0x380 ('toof' at 0x37C), MCC Halo 3's 0x4000
	('toof' at 0x3FFC). See map_loaders.h. */

#include "cseries.h"
#include "cseries_windows.h"
#include "map_loaders.h"
#include "h2_level.h"
#include "h3_level.h"

#ifndef _WIN32
#include <dirent.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOADER_ALIGN __attribute__((force_align_arg_pointer))

/* ---------- constants */

enum
{
	HEADER_SIGNATURE = 0x68656164,  /* 'head' */
	FOOTER_SIGNATURE = 0x666F6F74,  /* 'foot' */
	HEADER_BYTES = 0x4000,
};

/* ---------- private code */

static unsigned long le32(unsigned char const *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* the file's first bytes (up to HEADER_BYTES), how many */
LOADER_ALIGN static long header_read(char const *path, unsigned char *header)
{
	FILE *file = fopen(path, "rb");
	long n;

	if (!file)
		return 0;
	n = (long)fread(header, 1, HEADER_BYTES, file);
	fclose(file);
	return n;
}

LOADER_ALIGN static short format_of_header(unsigned char const *h, long n)
{
	unsigned long version;

	if (n < 0x800 || le32(h) != HEADER_SIGNATURE)
		return _map_format_unknown;
	version = le32(h + 4);
	if (version == 8 && le32(h + 0x7FC) == FOOTER_SIGNATURE)
		return _map_format_halo2_xbox;
	if ((version == 5 || version == 7 || version == 609 || version == 13) && le32(h + 0x7FC) == FOOTER_SIGNATURE)
		return _map_format_halo1;
	if (version == 13 && le32(h + 0x37C) == FOOTER_SIGNATURE)
		return _map_format_halo2_mcc;
	if (version == 13 && n >= HEADER_BYTES && le32(h + 0x3FFC) == FOOTER_SIGNATURE)
		return _map_format_halo3_mcc;
	return _map_format_unknown;
}

/* a string field of the header, its own copy */
static void header_string(unsigned char const *h, long n, long offset, long size, char *out, long out_size)
{
	long i;

	for (i = 0; i < size && i < out_size - 1 && offset + i < n && h[offset + i]; i++)
		out[i] = (char)h[offset + i];
	out[i] = 0;
}

LOADER_ALIGN static boolean describe_as(char const *path, struct map_loader_entry *entry, short format,
	long name_offset, long scenario_offset)
{
	static unsigned char header[HEADER_BYTES];
	long n = header_read(path, header);

	if (format_of_header(header, n) != format)
		return FALSE;
	memset(entry, 0, sizeof(*entry));
	entry->format = format;
	header_string(header, n, name_offset, 32, entry->name, sizeof(entry->name));
	if (scenario_offset >= 0)
		header_string(header, n, scenario_offset, 256, entry->scenario, sizeof(entry->scenario));
	return TRUE;
}

/* Halo 1: the name at 0x20; played as it is */
LOADER_ALIGN static boolean halo1_describe(char const *path, struct map_loader_entry *entry)
{
	return describe_as(path, entry, _map_format_halo1, 0x20, -1);
}

LOADER_ALIGN static boolean halo1_resolve(char const *maps_directory, char const *name, char *path, long path_size,
	char *why, long why_size)
{
	(void)why;
	(void)why_size;
	return snprintf(path, path_size, "%s%s.map", maps_directory, name) < path_size;
}

/* Halo 2 (Xbox: the name at 0x198, the scenario at 0x1BC; MCC: at 0xB0
and 0xD0): a cache file made from it (h2_level.c) */
LOADER_ALIGN static boolean halo2_xbox_describe(char const *path, struct map_loader_entry *entry)
{
	return describe_as(path, entry, _map_format_halo2_xbox, 0x198, 0x1BC);
}

LOADER_ALIGN static boolean halo2_mcc_describe(char const *path, struct map_loader_entry *entry)
{
	return describe_as(path, entry, _map_format_halo2_mcc, 0xB0, 0xD0);
}

LOADER_ALIGN static boolean halo2_resolve(char const *maps_directory, char const *name, char *path, long path_size,
	char *why, long why_size)
{
	struct h2_level_build_result result;

	if (!h2_level_name_plain(name))
	{
		snprintf(why, why_size, "its name is not a plain file name");
		return FALSE;
	}
	/* (MCC's maps without textures.dat: nothing of Halo 2 can be made - not
	the emblems at the first start, not a level -, refused here with the
	reason, h2_level_textures_missing) */
	if (h2_level_textures_missing())
	{
		snprintf(why, why_size, "%s", H2_LEVEL_NO_TEXTURES);
		return FALSE;
	}
	/* (what every Halo 2 level shares - the levels folder, the emblems' pack -
	made once per card if missing, before the first level: h2_level.h) */
	h2_level_first_start(maps_directory, name);
	/* (HALO_H2_DIRECT: no level file - the level made as it loads from the
	MCC map, h2_level.h; the path is the map's own) */
	if (h2_level_direct_enabled())
		return h2_level_direct_prepare(maps_directory, name, path, path_size, why, why_size);
	if (h2_level_cache_path(maps_directory, name, path, path_size, &result))
		return TRUE;
	snprintf(why, why_size, "%s", result.why);
	return FALSE;
}

/* Halo 3 (MCC): named (0xC0: the name; 0xE0: the scenario); on PC, with the
test switch HALO_H3_LEVELS, a direct level - made in memory as it loads from
the MCC map and shared.map, no level file (h3_level.h); no menu lists it yet */
LOADER_ALIGN static boolean halo3_mcc_describe(char const *path, struct map_loader_entry *entry)
{
	return describe_as(path, entry, _map_format_halo3_mcc, 0xC0, 0xE0);
}

LOADER_ALIGN static boolean halo3_resolve(char const *maps_directory, char const *name, char *path, long path_size,
	char *why, long why_size)
{
	if (h3_level_direct_enabled())
		return h3_level_direct_prepare(maps_directory, name, path, path_size, why, why_size);
	(void)maps_directory;
	(void)name;
	(void)path;
	(void)path_size;
	snprintf(why, why_size, "Halo 3 maps are not loaded in the engine yet");
	return FALSE;
}

static struct map_loader const loaders[] =
{
	{ _map_format_halo1, "Halo", halo1_describe, halo1_resolve },
	{ _map_format_halo2_xbox, "Halo 2 (Xbox)", halo2_xbox_describe, halo2_resolve },
	{ _map_format_halo2_mcc, "Halo 2", halo2_mcc_describe, halo2_resolve },
	{ _map_format_halo3_mcc, "Halo 3", halo3_mcc_describe, halo3_resolve },
};

/* ---------- public code */

LOADER_ALIGN short map_format_of_file(char const *path)
{
	static unsigned char header[HEADER_BYTES];

	return format_of_header(header, header_read(path, header));
}

LOADER_ALIGN struct map_loader const *map_loader_get(short format)
{
	long i;

	for (i = 0; i < (long)(sizeof(loaders) / sizeof(loaders[0])); i++)
	{
		if (loaders[i].format == format)
			return &loaders[i];
	}
	return NULL;
}

LOADER_ALIGN struct map_loader const *map_loader_for_file(char const *path, struct map_loader_entry *entry)
{
	struct map_loader const *loader = map_loader_get(map_format_of_file(path));
	struct map_loader_entry ignored;

	if (loader && !loader->describe(path, entry ? entry : &ignored))
		loader = NULL;
	return loader;
}

#ifdef _WIN32
/* (the windows build: FindFirstFileA, as there is no dirent.h) */
LOADER_ALIGN long map_loaders_list(char const *directory,
	void (*found)(struct map_loader_entry const *entry, char const *path, void *context), void *context)
{
	char pattern[1024];
	WIN32_FIND_DATAA data;
	HANDLE find;
	long count = 0;

	if (snprintf(pattern, sizeof(pattern), "%s*.map", directory) >= (int)sizeof(pattern))
		return 0;
	find = FindFirstFileA(pattern, &data);
	if (find == INVALID_HANDLE_VALUE)
		return 0;
	do
	{
		char path[1024];
		struct map_loader_entry entry;
		char const *name = data.cFileName;

		if (!strcmp(name, "shared.map") || !strcmp(name, "single_player_shared.map") ||
			!strcmp(name, "mainmenu.map") || !strcmp(name, "campaign.map"))
		{
			continue;
		}
		if (snprintf(path, sizeof(path), "%s%s", directory, name) >= (int)sizeof(path))
			continue;
		if (map_loader_for_file(path, &entry))
		{
			if (found)
				found(&entry, path, context);
			count++;
		}
	} while (FindNextFileA(find, &data));
	CloseHandle(find);
	return count;
}
#else
LOADER_ALIGN long map_loaders_list(char const *directory,
	void (*found)(struct map_loader_entry const *entry, char const *path, void *context), void *context)
{
	DIR *dir = opendir(directory);
	struct dirent *e;
	long count = 0;

	if (!dir)
		return 0;
	while ((e = readdir(dir)) != NULL)
	{
		char path[1024];
		struct map_loader_entry entry;
		size_t length = strlen(e->d_name);

		/* (the shared maps hold no scenario of their own) */
		if (length < 5 || strcmp(e->d_name + length - 4, ".map") != 0 ||
			!strcmp(e->d_name, "shared.map") || !strcmp(e->d_name, "single_player_shared.map") ||
			!strcmp(e->d_name, "mainmenu.map") || !strcmp(e->d_name, "campaign.map"))
		{
			continue;
		}
		if (snprintf(path, sizeof(path), "%s%s", directory, e->d_name) >= (int)sizeof(path))
			continue;
		if (map_loader_for_file(path, &entry))
		{
			if (found)
				found(&entry, path, context);
			count++;
		}
	}
	closedir(dir);
	return count;
}
#endif

LOADER_ALIGN boolean map_loaders_resolve(char const *maps_directory, char const *name, char *path, long path_size,
	char *why, long why_size)
{
	char file[1024];
	struct map_loader const *loader;

	snprintf(file, sizeof(file), "%s%s.map", maps_directory, name);
	loader = map_loader_for_file(file, NULL);
	if (!loader)
	{
		snprintf(why, why_size, "it is no map this engine knows");
		return FALSE;
	}
	return loader->resolve(maps_directory, name, path, path_size, why, why_size);
}
