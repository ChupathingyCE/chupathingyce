/*
SPV3_MODE.C

An early first look at SPV3 on this engine: SPV3 is a campaign mod for Halo
Custom Edition by the SPV3 team, played on OpenSauce (Kornner Studios). It
is played as a map pack whose folder is maps_spv3, holding the files SPV3's
installer puts in its maps folder (its missions' .yelo files, ui.yelo, and
its own bitmaps.map, sounds.map and loc.map), with game.pack_menus = "spv3".
Bring your own copy: none of its files ship.

SPV3's maps are Custom Edition caches (version 609) built with OpenSauce's
tools: OpenSauce's header in the cache header's padding (at 0x70) asks for
its memory upgrades, a tag cache 1.5 times Custom Edition's (0x02280000 at
0x40440000), and their structure BSPs are linked to that cache's top. With
this mode on, and the larger window mapped (a 64-bit desktop build with an
SPV3 install: xbox_heap.c), the Custom Edition loader reads them so:

- maps_spv3\<name>.yelo is <name>@ce (map_families.c), and ui.yelo the menus;
- maps_spv3's resource maps are read before maps_ce's (map_families.c);
- the tag cache is OpenSauce's size (cache_files.c, ce_map_checks.c);
- a map asking OpenSauce for memory upgrades alone is read; any other flag
  is refused, and said (spv3_mode_accepts_opensauce).

What OpenSauce adds that the engine has no code for is passed over as Halo
PC's engine would pass over it, and logged: its tag groups are never looked
up, its extensions in stock tags' padding are never read, and scripts that
call its functions are dropped one by one (hs_compile.c). Everything about a
map is logged once, with a "spv3:" prefix in SPV3 mode (debug.txt), by
OpenSauce's map log (opensauce_survey.c), which logs any OpenSauce map: the
survey, the scripts the game lacks, resource misses and the loader's
corrections.

OpenSauce's formats and limits are read from its source (GPL-3.0) as facts
(cache_files_structures_yelo.hpp, blam_memory_upgrades.hpp); no code of it
is here.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "errors.h"
#include "halo_map_packs.h"
#include "opensauce_survey.h"
#include "spv3_mode.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------- constants */

/* OpenSauce's header flags (cache_files_structures_yelo.hpp) */
static char const *const spv3_yelo_flag_names[] =
{
	"uses_memory_upgrades",
	"uses_mod_data_files",
	"is_protected",
	"uses_game_state_upgrades",
	"has_compression_params",
};

/* ---------- globals */

int spv3_opensauce_window;

/* ---------- public code */

boolean spv3_mode_active(
	void)
{
/* (only the 64-bit desktop builds map the larger tag window its maps need,
as the launcher offers the mode there alone: xbox_heap.c, launcher.c. The
consoles, the original Xbox among them, have map packs but never this) */
#if !defined(HALO_MAP_LIST) || !defined(HALO_64BIT) || defined(HALO_WINDOW_TRANSLATED) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_CONSOLE)
	return FALSE;
#else
	static int active = -1;

	if (active < 0)
	{
		char const *pack = map_pack_menus();

		active = pack && !_stricmp(pack, SPV3_MODE_PACK_ID);
	}
	return active ? TRUE : FALSE;
#endif
}

void spv3_mode_log(
	char const *format,
	...)
{
	char text[768];
	va_list arguments;

	if (!spv3_mode_active())
		return;
	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	error(_error_silent, "spv3: %s", text);
}

static boolean spv3_file_exists(
	char const *path)
{
	HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (file == INVALID_HANDLE_VALUE)
		return FALSE;
	CloseHandle(file);
	return TRUE;
}

boolean spv3_mode_pack_present(
	char const *pack)
{
	char path[256];

	if (!pack || _stricmp(pack, SPV3_MODE_PACK_ID))
		return FALSE;
	snprintf(path, sizeof(path), "d:\\%s\\ui.yelo", SPV3_MODE_FOLDER);
	return spv3_file_exists(path);
}

unsigned long spv3_mode_tag_cache_size(
	unsigned long custom_edition_size)
{
	return spv3_opensauce_window && spv3_mode_active() ? SPV3_OPENSAUCE_TAG_CACHE_SIZE : custom_edition_size;
}

boolean spv3_mode_map_path(
	char const *file,
	char *path,
	long size)
{
	static char const *const endings[] = { ".yelo", ".map" };
	long index;

	if (!spv3_mode_active() || !file || !file[0] || strpbrk(file, "\\/:@"))
		return FALSE;
	for (index = 0; index < (long)NUMBEROF(endings); index++)
	{
		if (snprintf(path, (size_t)size, "d:\\%s\\%s%s", SPV3_MODE_FOLDER, file, endings[index]) < (int)size &&
			spv3_file_exists(path))
		{
			return TRUE;
		}
	}
	path[0] = 0;
	return FALSE;
}

boolean spv3_mode_is_ui(
	char const *map_name)
{
	return map_name && !_stricmp(map_name, "ui") && spv3_mode_active();
}

/* (whether the ui open now is SPV3's own file, maps_spv3\ui.yelo, and not
another ui the loader fell back to: spv3_mode_note_map_opened) */
static boolean spv3_own_ui_open;

void spv3_mode_note_map_opened(
	char const *map_name,
	char const *path)
{
	char own[256];

	if (!spv3_mode_is_ui(map_name))
		return;
	snprintf(own, sizeof(own), "d:\\%s\\ui.yelo", SPV3_MODE_FOLDER);
	spv3_own_ui_open = path && !_stricmp(path, own);
	if (!spv3_own_ui_open)
		spv3_mode_log("ui: %s is not %s: its scripts may not change the level", path ? path : "(none)", own);
}

boolean spv3_mode_own_ui_open(
	char const *map_name)
{
	return spv3_mode_is_ui(map_name) && spv3_own_ui_open;
}

boolean spv3_mode_level_name(
	char const *file,
	char *name,
	long size)
{
	static char dropped[16][32];
	static long dropped_count;
	char path[256];
	size_t length = file ? strlen(file) : 0;
	size_t index;
	boolean valid = length > 0 && length < sizeof(dropped[0]) && _stricmp(file, "ui");
	long logged;

	if (size > 0)
		name[0] = 0;
	/* (a level's name: letters, digits and underscores, which no path,
	family or pack suffix is made of) */
	for (index = 0; valid && index < length; index++)
	{
		char c = file[index];

		valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
	}
	if (valid && spv3_mode_active() &&
		snprintf(path, sizeof(path), "d:\\%s\\%s.yelo", SPV3_MODE_FOLDER, file) < (int)sizeof(path) &&
		spv3_file_exists(path) &&
		snprintf(name, (size_t)size, "%s@ce", file) < (int)size)
	{
		return TRUE;
	}
	if (size > 0)
		name[0] = 0;
	/* (each name dropped is said once) */
	for (logged = 0; logged < dropped_count; logged++)
	{
		if (!strncmp(dropped[logged], file ? file : "", sizeof(dropped[0]) - 1))
			return FALSE;
	}
	if (dropped_count < (long)NUMBEROF(dropped))
	{
		snprintf(dropped[dropped_count], sizeof(dropped[0]), "%s", file ? file : "");
		dropped_count++;
	}
	spv3_mode_log("ui: map_name %.64s is not a level in %s: dropped", file ? file : "", SPV3_MODE_FOLDER);
	return FALSE;
}

boolean spv3_mode_accepts_opensauce(
	char const *map_name,
	unsigned short flags)
{
	long bit;
	boolean accepted = TRUE;

	if (!spv3_mode_active())
		return FALSE;
	for (bit = 0; bit < 16; bit++)
	{
		if (!(flags & (1 << bit)))
			continue;
		if (bit == 0)
		{
			if (!spv3_opensauce_window)
			{
				spv3_mode_log("%s: OpenSauce's memory upgrades need the larger tag window, which is not mapped "
					"(a 64-bit desktop build with maps_spv3 in the data folder at start): refused", map_name);
				accepted = FALSE;
			}
			else
				spv3_mode_log("%s: OpenSauce flag uses_memory_upgrades: read with a tag cache of 0x%08lX",
					map_name, (unsigned long)SPV3_OPENSAUCE_TAG_CACHE_SIZE);
		}
		else
		{
			spv3_mode_log("%s: OpenSauce flag %s: not supported, refused", map_name,
				bit < (long)NUMBEROF(spv3_yelo_flag_names) ? spv3_yelo_flag_names[bit] : "(unknown)");
			accepted = FALSE;
		}
	}
	return accepted;
}

/* ---------- the log of its maps */

void spv3_mode_note_correction(
	char const *what)
{
	if (spv3_mode_active())
		opensauce_survey_note_correction(what);
}
