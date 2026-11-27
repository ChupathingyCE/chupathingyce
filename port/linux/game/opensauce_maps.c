/*
OPENSAUCE_MAPS.C

How the game reads an OpenSauce map (opensauce_maps.h): any Custom Edition
cache with OpenSauce's header (opensauce_map.c), SPV3's among them.

OpenSauce's header in the cache header's padding (at 0x70) may ask for its
memory upgrades: a tag cache 1.5 times Custom Edition's (0x02280000 at
0x40440000), which the map's structure BSPs are linked to the top of, and
1.5 times the scripts' syntax nodes. With the larger window mapped (a 64-bit
desktop build, port/linux/src/xbox_heap.c), such a map is read so: the tag
cache is OpenSauce's size for that map alone (cache_files.c,
ce_map_checks.c), and its scripts' larger node array is read in place
(hs.c). Every other map keeps Custom Edition's size, so nothing of theirs
moves. In SPV3 mode every map is read with the larger size, as that mode
always read them (spv3_mode.c).

Any other flag of the header (mod data files, protection, game state
upgrades, compression) is refused, and said: those maps need what the game
doesn't have.

Where they are: maps_os, beside maps_ce, holds any OpenSauce maps,
<name>.yelo (or <name>.map) played as <name>@ce, found after Custom
Edition's own places so that no map there changes (map_families.c); its
maps read Custom Edition's resource maps, and a ui there is never anyone's
menus. An OpenSauce mod with its own menus (maps_<pack>\ui.yelo, and no
ui.map) is played as its own mode, as any map pack is (map_packs.c,
launcher.c): its ui and levels from its folder first, and its own resource
maps. Its scripts may do no more than any map's (hs.c: no map_name); SPV3,
the one mod with more, is SPV3 mode's.

OpenSauce's formats and limits are read from its source (GPL-3.0) as facts
(cache_files_structures_yelo.hpp, blam_memory_upgrades.hpp); no code of it
is here.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "errors.h"
#include "halo_map_packs.h"
#include "opensauce.h"
#include "opensauce_maps.h"
#include "opensauce_survey.h"
#include "spv3_mode.h"

#include <stdio.h>
#include <string.h>

/* ---------- globals */

int opensauce_tag_window;

#ifdef OPENSAUCE_MAPS

static struct
{
	/* a map's checks are running (opensauce_maps_check_started), and whether
	the map they check is read with the memory upgrades */
	boolean checking;
	boolean checking_upgrades;
} opensauce_maps_globals;

/* ---------- private code */

/* whether a header's flags ask for the memory upgrades */
static boolean opensauce_maps_flags_upgrade(
	unsigned short flags)
{
	return (flags & (1 << _opensauce_header_uses_memory_upgrades_bit)) ? TRUE : FALSE;
}

/* ---------- public code */

boolean opensauce_maps_window_mapped(
	void)
{
#ifdef OPENSAUCE_MAPS_MEMORY_UPGRADES
	return opensauce_tag_window ? TRUE : FALSE;
#else
	return FALSE;
#endif
}

boolean opensauce_maps_accepts_header(
	char const *map_name,
	unsigned short flags)
{
	short bit;
	boolean accepted = TRUE;

	for (bit = 0; bit < 16; bit++)
	{
		if (!(flags & (1 << bit)))
			continue;
		if (bit == _opensauce_header_uses_memory_upgrades_bit)
		{
			if (!opensauce_maps_window_mapped())
			{
				opensauce_survey_log("%s: OpenSauce's memory upgrades need the larger tag window, which is not "
					"mapped (a 64-bit desktop build with maps_spv3 in the data folder at start): "
					"refused", map_name);
				accepted = FALSE;
			}
			else
			{
				opensauce_survey_log("%s: OpenSauce flag uses_memory_upgrades: read with a tag cache of 0x%08lX",
					map_name, (unsigned long)OPENSAUCE_TAG_CACHE_SIZE);
			}
		}
		else
		{
			opensauce_survey_log("%s: OpenSauce flag %s: not supported, refused", map_name,
				opensauce_header_flag_name(bit));
			accepted = FALSE;
		}
	}
	if (opensauce_maps_globals.checking)
		opensauce_maps_globals.checking_upgrades = accepted && opensauce_maps_flags_upgrade(flags);
	return accepted;
}

void opensauce_maps_check_started(
	void)
{
	opensauce_maps_globals.checking = TRUE;
	opensauce_maps_globals.checking_upgrades = FALSE;
}

void opensauce_maps_check_done(
	void)
{
	opensauce_maps_globals.checking = FALSE;
	opensauce_maps_globals.checking_upgrades = FALSE;
}

boolean opensauce_maps_memory_upgrades(
	void)
{
	struct opensauce_header const *header;

	if (!opensauce_maps_window_mapped())
		return FALSE;
	if (spv3_mode_active())
		return TRUE;
	if (opensauce_maps_globals.checking)
		return opensauce_maps_globals.checking_upgrades;
	header = opensauce_map_header();
	return header && opensauce_maps_flags_upgrade(header->flags);
}

unsigned long opensauce_maps_tag_cache_size(
	unsigned long custom_edition_size)
{
	return opensauce_maps_memory_upgrades() ? (unsigned long)OPENSAUCE_TAG_CACHE_SIZE : custom_edition_size;
}

unsigned long opensauce_maps_header_tag_cache_size(
	void const *cache_header,
	unsigned long custom_edition_size)
{
	struct opensauce_header header;

	if (!opensauce_maps_window_mapped())
		return custom_edition_size;
	if (spv3_mode_active() ||
		(cache_header && opensauce_header_read(cache_header, &header) && opensauce_maps_flags_upgrade(header.flags)))
	{
		return (unsigned long)OPENSAUCE_TAG_CACHE_SIZE;
	}
	return custom_edition_size;
}

boolean opensauce_maps_session_is_online(
	struct opensauce_maps_session const *session)
{
	if (!session)
		return FALSE;
	if (session->hosting)
		return session->hosting_internet;
	return session->joined && !session->host_on_lan;
}

boolean opensauce_maps_online_allows(
	char const *map_name,
	unsigned short flags,
	struct opensauce_maps_session const *session)
{
	if (!flags || spv3_mode_active() || !opensauce_maps_session_is_online(session))
		return TRUE;
	opensauce_survey_log("%s: an OpenSauce map that asks for flags 0x%04X, in an online game (%s): refused; "
		"they play offline and over system link for now", map_name ? map_name : "?", (unsigned int)flags,
		session->hosting ? "hosted for the internet" : "joined from the internet");
	return FALSE;
}

boolean opensauce_maps_header_online_allows(
	char const *map_name,
	void const *cache_header)
{
	unsigned char const *yelo = (unsigned char const *)cache_header + OPENSAUCE_HEADER_OFFSET;
	struct opensauce_maps_session session;

	if (!cache_header || memcmp(yelo, "oley", 4) || !(yelo[6] | yelo[7]))
		return TRUE;
	opensauce_maps_session_now(&session);
	return opensauce_maps_online_allows(map_name, (unsigned short)(yelo[6] | (yelo[7] << 8)), &session);
}

boolean opensauce_maps_header_local_only(
	void const *cache_header)
{
	unsigned char const *yelo = (unsigned char const *)cache_header + OPENSAUCE_HEADER_OFFSET;

	return cache_header && !memcmp(yelo, "oley", 4) && (yelo[6] || yelo[7]);
}

/* ---------- where its maps are */

static boolean opensauce_maps_file_exists(
	char const *path)
{
	HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (file == INVALID_HANDLE_VALUE)
		return FALSE;
	CloseHandle(file);
	return TRUE;
}

/* a map's file name: no folder, family or pack in it */
static boolean opensauce_maps_file_valid(
	char const *file)
{
	return file && file[0] && strlen(file) < 64 && !strpbrk(file, "\\/:@");
}

/* d:\<folder>\<file>.yelo, else .map, into path */
static boolean opensauce_maps_file_path(
	char const *folder,
	char const *file,
	char *path,
	long size)
{
	static char const *const endings[] = { ".yelo", ".map" };
	long index;

	if (size <= 0)
		return FALSE;
	for (index = 0; index < (long)NUMBEROF(endings); index++)
	{
		int length = snprintf(path, (size_t)size, "d:\\%s\\%s%s", folder, file, endings[index]);

		if (length > 0 && length < (int)size && opensauce_maps_file_exists(path))
			return TRUE;
	}
	path[0] = 0;
	return FALSE;
}

boolean opensauce_maps_folder_map_path(
	char const *file,
	char *path,
	long size)
{
	if (!opensauce_maps_file_valid(file) || !_stricmp(file, "ui"))
		return FALSE;
	return opensauce_maps_file_path(OPENSAUCE_MAPS_FOLDER, file, path, size);
}

boolean opensauce_maps_pack_present(
	char const *pack)
{
#ifdef OPENSAUCE_MAPS_MEMORY_UPGRADES
	char path[256];

	/* (SPV3's folder is SPV3 mode's; maps_os is no mod's) */
	if (!pack || !opensauce_maps_file_valid(pack) || !_stricmp(pack, SPV3_MODE_PACK_ID) || !_stricmp(pack, "os"))
		return FALSE;
	snprintf(path, sizeof(path), "d:\\maps_%s\\ui.map", pack);
	if (opensauce_maps_file_exists(path))
		return FALSE;
	snprintf(path, sizeof(path), "d:\\maps_%s\\ui.yelo", pack);
	return opensauce_maps_file_exists(path);
#else
	(void)pack;
	return FALSE;
#endif
}

char const *opensauce_maps_pack(
	void)
{
	static int asked;
	static char pack[32];

	/* (map_pack_menus is read once too: it takes effect after a restart) */
	if (!asked)
	{
		char const *menus = map_pack_menus();

		asked = 1;
		pack[0] = 0;
		if (menus && strlen(menus) < sizeof(pack) && opensauce_maps_pack_present(menus))
		{
			snprintf(pack, sizeof(pack), "%s", menus);
			error(_error_silent, "OpenSauce maps: maps_%s is an OpenSauce mod's: its menus and maps are played "
				"from there", pack);
		}
	}
	return pack[0] ? pack : NULL;
}

boolean opensauce_maps_pack_is_ui(
	char const *map_name)
{
	return map_name && !_stricmp(map_name, "ui") && opensauce_maps_pack();
}

boolean opensauce_maps_pack_map_path(
	char const *file,
	char *path,
	long size)
{
	char const *pack = opensauce_maps_pack();
	char folder[48];

	if (!pack || !opensauce_maps_file_valid(file))
		return FALSE;
	snprintf(folder, sizeof(folder), "maps_%s", pack);
	return opensauce_maps_file_path(folder, file, path, size);
}

boolean opensauce_maps_pack_resource(
	char const *name,
	char *path,
	long size)
{
	char const *pack = opensauce_maps_pack();
	int length;

	if (!pack || !opensauce_maps_file_valid(name) || size <= 0)
		return FALSE;
	length = snprintf(path, (size_t)size, "d:\\maps_%s\\%s.map", pack, name);
	if (length > 0 && length < (int)size && opensauce_maps_file_exists(path))
		return TRUE;
	path[0] = 0;
	return FALSE;
}

#endif
