/*
SERVER_BROWSER.C

The PC menus' Server Browser's games on Halo PC maps (halo_server_browser.h):
a game whose map is announced as <file>@ce (Custom Edition) or <file>@md
(HaloMD) is named as the menus' map list names the map (ui_map_list.c), and
joined only with the map in its family's folders (halo_map_families.h), on
a build with Halo PC map support (HALO_CUSTOM_EDITION), as the game list's
Online Games screen joins one (browser_screen.c's words). A Custom Edition
map that may be downloaded (map_download.c) does not stop the join: the
join is left when the host names it, and Online Games offers it over the
main menu, then joins that host again (browser_screen_map_missing).
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "main/main.h"
#include "text/unicode.h"
#include "halo_map_families.h"
#include "halo_server_browser.h"
#include "halo_ui_map_list.h"
#include "../src/map_download.h"

#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* a map's file name, at most */
	MAP_FILE_LENGTH = 64,
	/* whether a family's folders have a game's map, asked again this often */
	CE_MAP_CHECK_INTERVAL = 1000,
};

/* whether a game's map can be played here */
enum
{
	_ce_map_none,
	_ce_map_present,
	_ce_map_missing,
	_ce_map_unsupported,
};

/* ---------- globals */

#ifdef HALO_CUSTOM_EDITION
/* the last map asked of its family's folders, and the answer */
static struct
{
	char map[MAP_FILE_LENGTH + 4];
	short answer;
	unsigned long time;
} ce_map_check;
#endif

/* ---------- private code */

static void text_from_ascii(char const *ascii, wchar_t *text, short length)
{
	short index;

	for (index = 0; ascii[index] && index < length - 1; index++)
		text[index] = (wchar_t)(unsigned char)ascii[index];
	text[index] = 0;
}

/* whether a game's map can be played here: an Xbox map, or a Halo PC map
in its family's folders, missing, or on a build without them (the folders
asked again for another map, or now and then) */
static short ce_map_state(char const *map)
{
	char file[MAP_FILE_LENGTH];
	short family = map_family_parse_listed(map, file, sizeof(file));

	if (family == _map_family_xbox)
		return _ce_map_none;
#ifdef HALO_CUSTOM_EDITION
	if (strcmp(ce_map_check.map, map) || system_milliseconds() - ce_map_check.time > CE_MAP_CHECK_INTERVAL)
	{
		csstrncpy(ce_map_check.map, map, sizeof(ce_map_check.map) - 1);
		ce_map_check.map[sizeof(ce_map_check.map) - 1] = 0;
		ce_map_check.answer = ui_map_list_map_present(map) ? _ce_map_present : _ce_map_missing;
		ce_map_check.time = system_milliseconds();
	}
	return ce_map_check.answer;
#else
	return _ce_map_unsupported;
#endif
}

/* ---------- public code */

short server_browser_map_family(char const *map, wchar_t *text, short length)
{
	char file[MAP_FILE_LENGTH];
	short family = map_family_parse_listed(map, file, sizeof(file));

	if (family == _map_family_xbox)
		return family;
	ui_map_list_map_name(map, text, length);
	if (!text[0])
		text_from_ascii("Unknown map", text, length);
	return family;
}

boolean server_browser_map_downloadable(char const *map)
{
	char file[MAP_FILE_LENGTH];

	return map_family_parse_listed(map, file, sizeof(file)) == _map_family_custom_edition &&
		ce_map_state(map) == _ce_map_missing && map_download_possible(file);
}

boolean server_browser_map_blocked(char const *map, wchar_t *message, short length)
{
	char file[MAP_FILE_LENGTH], display[MAP_FILE_LENGTH], text[MAP_FILE_LENGTH + 192];
	short family = map_family_parse_listed(map, file, sizeof(file));

	switch (ce_map_state(map))
	{
	case _ce_map_missing:
		/* (a hashed name by the file the player knows: map_archive.h) */
		map_archive_display_name(file, display, (int)sizeof(display));
		/* (one of Halo PC's own maps: never downloaded, map_archive.h) */
		if (family == _map_family_custom_edition && map_archive_stock_name(file))
		{
			char first[96], second[160];

			map_family_stock_need(file, 0, first, sizeof(first), second, sizeof(second), text, sizeof(text));
		}
		/* (a pack's map: its pack's folder, maps_<pack>) */
		else if (family == _map_family_pack)
		{
			char pack[MAP_PACK_ID_LENGTH + 1];

			map_family_pack_id(map, pack, sizeof(pack));
			snprintf(text, sizeof(text), "Needs maps_%s/%s.map to join", pack, file);
		}
		else
			snprintf(text, sizeof(text), server_browser_map_downloadable(map) ?
				"Needs %s/%s.map: join to download it" : "Needs %s/%s.map to join",
				map_family_folder(family), display);
		break;
	case _ce_map_unsupported:
		snprintf(text, sizeof(text), "%s maps aren't supported in this build.",
			family == _map_family_halomd ? "HaloMD" : "Halo PC");
		break;
	default:
		return FALSE;
	}
	text_from_ascii(text, message, length);
	return TRUE;
}
