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

OpenSauce's formats and limits are read from its source (GPL-3.0) as facts
(cache_files_structures_yelo.hpp, blam_memory_upgrades.hpp); no code of it
is here.
*/

#include "cseries.h"
#include "errors.h"
#include "opensauce.h"
#include "opensauce_maps.h"
#include "opensauce_survey.h"
#include "spv3_mode.h"

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

#endif
