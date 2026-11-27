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
map is logged once, with a "spv3:" prefix, in SPV3 mode only (debug.txt):
the survey below, the scripts the game lacks, resource misses and the
loader's corrections.

OpenSauce's formats and limits are read from its source (GPL-3.0) as facts
(cache_files_structures_yelo.hpp, blam_memory_upgrades.hpp, its group tags);
no code of it is here. tools/spv3_scan.py writes the same survey offline.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "errors.h"
#include "halo_map_packs.h"
#include "spv3_mode.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	SPV3_TAG_BASE = 0x40440000,
	SPV3_HEADER_YELO_OFFSET = 0x70,
	SPV3_TAG_INSTANCE_SIZE = 0x20,
	SPV3_SCENARIO_BSPS_OFFSET = 0x5A4,
	SPV3_SCENARIO_SYNTAX_OFFSET = 0x474,
	SPV3_SCENARIO_STRINGS_OFFSET = 0x488,
	SPV3_SCENARIO_SCRIPTS_OFFSET = 0x49C,
	SPV3_SCENARIO_GLOBALS_OFFSET = 0x4A8,
	SPV3_SCENARIO_READ_SIZE = 0x5B0,
	SPV3_BSP_REFERENCE_SIZE = 0x20,
	/* the names counted for a map's summary */
	SPV3_MAXIMUM_NAMES = 256,
	SPV3_NAME_LENGTH = 48,
	SPV3_MAXIMUM_GROUPS = 128,
	SPV3_CORRECTIONS_LOGGED = 32,
	SPV3_RESOURCE_MISSES_LOGGED = 32,
};

/* OpenSauce's header flags (cache_files_structures_yelo.hpp) */
static char const *const spv3_yelo_flag_names[] =
{
	"uses_memory_upgrades",
	"uses_mod_data_files",
	"is_protected",
	"uses_game_state_upgrades",
	"has_compression_params",
};

/* Custom Edition's own tag groups; any other is OpenSauce's or unknown */
static char const spv3_stock_groups[][5] =
{
	"actr", "actv", "ant!", "antr", "bipd", "bitm", "boom", "cdmg", "coll", "colo", "cont", "ctrl", "deca", "DeLa",
	"devc", "devi", "dobc", "effe", "elec", "eqip", "flag", "fog ", "font", "foot", "garb", "glw!", "grhi", "hmt ",
	"hud#", "hudg", "item", "itmc", "jpt!", "lens", "lifi", "ligh", "lsnd", "mach", "matg", "metr", "mgs2", "mod2",
	"mode", "mply", "ngpr", "obje", "part", "pctl", "phys", "plac", "pphy", "proj", "rain", "sbsp", "scen", "scex",
	"schi", "scnr", "senv", "sgla", "shdr", "sky ", "smet", "snd!", "snde", "soso", "sotr", "Soul", "spla", "ssce",
	"str#", "swat", "tagc", "trak", "udlg", "unhi", "unic", "unit", "ustr", "vcky", "vehi", "weap", "wind", "wphi",
};

/* OpenSauce's groups, by name, for the log */
static struct
{
	char group[5];
	char const *name;
	char const *engine;
} const spv3_opensauce_groups[] =
{
	{ "yelo", "project_yellow", "checked; read: gravity honored, the rest logged (opensauce_map.c)" },
	{ "gelo", "project_yellow_globals", "checked; read and logged (opensauce_map.c)" },
	{ "gelc", "project_yellow_globals_cv", "not supported, not read" },
	{ "shpp", "shader_postprocess", "not supported, no post-processing" },
	{ "shpg", "shader_postprocess_generic", "checked; drawn (its compiled effects translated: opensauce_postprocess.c)" },
	{ "sppg", "shader_postprocess_globals", "checked; its bloom drawn (opensauce_postprocess.c)" },
	{ "efpp", "effect_postprocess", "not supported, no post-processing" },
	{ "efpc", "effect_postprocess_collection", "checked; drawn (opensauce_postprocess.c)" },
	{ "efpg", "effect_postprocess_generic", "checked; drawn (opensauce_postprocess.c)" },
	{ "avtc", "actor_variant_transform_collection", "checked, actors never transform yet" },
	{ "avti", "actor_variant_transform_in", "checked, actors never transform yet" },
	{ "avto", "actor_variant_transform_out", "checked, actors never transform yet" },
	{ "magy", "model_animations_yelo", "not supported, not read" },
	{ "eqhi", "equipment_hud_interface", "not supported, not read" },
	{ "sidy", "string_id_yelo", "not supported, not read" },
	{ "sily", "text_value_pair_definition", "not supported, not read" },
	{ "tag+", "tag_database", "not supported, not read" },
	{ "memg", "cache_memory_gestalt", "not supported, not read" },
	{ "meml", "cache_memory_layout_table", "not supported, not read" },
};

/* ---------- globals */

int spv3_opensauce_window;

struct spv3_name_count
{
	char name[SPV3_NAME_LENGTH];
	long count;
};

static struct
{
	struct spv3_name_count functions[SPV3_MAXIMUM_NAMES];
	long function_count;
	struct spv3_name_count globals[SPV3_MAXIMUM_NAMES];
	long global_count;
	long resource_misses;
	long corrections;
	char surveyed[64];
} spv3_globals;

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

/* ---------- the survey */

static unsigned long spv3_read32(
	void const *data,
	unsigned long offset)
{
	unsigned char const *bytes = (unsigned char const *)data + offset;

	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) | ((unsigned long)bytes[2] << 16) |
		((unsigned long)bytes[3] << 24);
}

static unsigned short spv3_read16(
	void const *data,
	unsigned long offset)
{
	unsigned char const *bytes = (unsigned char const *)data + offset;

	return (unsigned short)(bytes[0] | (bytes[1] << 8));
}

/* the bytes at a tag address, or NULL outside the tag data */
static unsigned char const *spv3_at(
	void const *tags,
	unsigned long tag_data_size,
	unsigned long address,
	unsigned long size)
{
	unsigned long offset = address - SPV3_TAG_BASE;

	if (address < SPV3_TAG_BASE || offset > tag_data_size || size > tag_data_size - offset)
		return NULL;
	return (unsigned char const *)tags + offset;
}

static void spv3_group_text(
	unsigned long group,
	char text[5])
{
	long index;

	for (index = 0; index < 4; index++)
	{
		char character = (char)((group >> (24 - 8 * index)) & 0xFF);

		text[index] = character >= 0x20 && character < 0x7F ? character : '?';
	}
	text[4] = 0;
}

static boolean spv3_group_is_stock(
	char const *group)
{
	long index;

	for (index = 0; index < (long)NUMBEROF(spv3_stock_groups); index++)
	{
		if (!memcmp(spv3_stock_groups[index], group, 4))
			return TRUE;
	}
	return FALSE;
}

void spv3_mode_survey(
	char const *map_name,
	void const *cache_header,
	void const *tags,
	unsigned long tag_data_size,
	unsigned long file_size)
{
	unsigned char const *header = (unsigned char const *)cache_header;
	unsigned char const *yelo = header + SPV3_HEADER_YELO_OFFSET;
	struct
	{
		char group[5];
		long count;
	} groups[SPV3_MAXIMUM_GROUPS];
	long group_count = 0;
	unsigned long instances, scenario_index, tag_count, index;
	unsigned char const *scenario = NULL;
	char line[512];

	if (!spv3_mode_active() || !header || !tags || tag_data_size < 0x28)
		return;
	/* (once a map: checked, then loaded, it is read twice) */
	if (!_stricmp(spv3_globals.surveyed, map_name))
		return;
	snprintf(spv3_globals.surveyed, sizeof(spv3_globals.surveyed), "%s", map_name);
	spv3_globals.function_count = spv3_globals.global_count = 0;
	spv3_globals.resource_misses = spv3_globals.corrections = 0;

	spv3_mode_log("---- %s: map survey ----", map_name);
	spv3_mode_log("%s: cache version %lu, name %.32s, build %.32s, type %lu, file %lu bytes (header says %lu), "
		"tag data %lu bytes at %lu", map_name, spv3_read32(header, 4), (char const *)header + 0x20,
		(char const *)header + 0x40, spv3_read32(header, 0x60), file_size, spv3_read32(header, 8), tag_data_size,
		spv3_read32(header, 0x10));
	if (spv3_read32(yelo, 0) == 0x79656C6FUL) /* 'yelo' */
	{
		unsigned short flags = spv3_read16(yelo, 6);
		unsigned long upgrade_bits = spv3_read32(yelo, 0x0C);
		float upgrade;
		long bit;

		memcpy(&upgrade, &upgrade_bits, sizeof(upgrade));
		line[0] = 0;
		for (bit = 0; bit < (long)NUMBEROF(spv3_yelo_flag_names); bit++)
		{
			if (flags & (1 << bit))
				snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%s", line[0] ? ", " : "",
					spv3_yelo_flag_names[bit]);
		}
		spv3_mode_log("%s: OpenSauce header v%u, flags 0x%04X (%s), memory upgrade %.2f, tag versions %u/%u, "
			"mod name '%.32s', CheApe definitions %lu bytes at %lu", map_name, spv3_read16(yelo, 4), flags,
			line[0] ? line : "none", upgrade, spv3_read16(yelo, 8), spv3_read16(yelo, 0x0A), (char const *)yelo + 0x40,
			spv3_read32(yelo, 0x10), spv3_read32(yelo, 0x18));
	}
	else
		spv3_mode_log("%s: no OpenSauce header (a stock Custom Edition cache)", map_name);
	spv3_mode_log("%s: tag data %lu bytes of the game's tag cache's %lu (Custom Edition 0x01700000, OpenSauce "
		"0x%08lX)", map_name, tag_data_size, spv3_mode_tag_cache_size(0x01700000UL),
		(unsigned long)SPV3_OPENSAUCE_TAG_CACHE_SIZE);

	instances = spv3_read32(tags, 0);
	scenario_index = spv3_read32(tags, 4) & 0xFFFF;
	tag_count = spv3_read32(tags, 12);
	spv3_mode_log("%s: %lu tags (Custom Edition's limit 5120, OpenSauce's %d), tag index at 0x%08lX", map_name,
		tag_count, SPV3_OPENSAUCE_TAG_COUNT, instances);
	/* (the count against the tag data first: its product with the instance
	size would wrap in 32 bits) */
	if (tag_count > tag_data_size / SPV3_TAG_INSTANCE_SIZE ||
		!spv3_at(tags, tag_data_size, instances, tag_count * SPV3_TAG_INSTANCE_SIZE))
	{
		spv3_mode_log("%s: the tag index is outside the tag data: no more survey", map_name);
		return;
	}
	for (index = 0; index < tag_count; index++)
	{
		unsigned char const *instance = spv3_at(tags, tag_data_size, instances + index * SPV3_TAG_INSTANCE_SIZE,
			SPV3_TAG_INSTANCE_SIZE);
		char group[5];
		long found;

		if (!instance)
			break;
		spv3_group_text(spv3_read32(instance, 0), group);
		for (found = 0; found < group_count && memcmp(groups[found].group, group, 4); found++)
			;
		if (found == group_count && group_count < SPV3_MAXIMUM_GROUPS)
		{
			memcpy(groups[found].group, group, 5);
			groups[found].count = 0;
			group_count++;
		}
		if (found < group_count)
			groups[found].count++;
		if (index == scenario_index)
			scenario = spv3_at(tags, tag_data_size, spv3_read32(instance, 0x14), SPV3_SCENARIO_READ_SIZE);
	}
	{
		long stock = 0;
		long other = 0;

		line[0] = 0;
		for (index = 0; index < (unsigned long)group_count; index++)
		{
			if (spv3_group_is_stock(groups[index].group))
			{
				stock += groups[index].count;
				snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%s %ld", line[0] ? ", " : "",
					groups[index].group, groups[index].count);
			}
		}
		spv3_mode_log("%s: stock tag groups (%ld tags): %s", map_name, stock, line);
		for (index = 0; index < (unsigned long)group_count; index++)
		{
			long known;
			char const *name = "unknown";
			char const *engine = "not read";

			if (spv3_group_is_stock(groups[index].group))
				continue;
			other += groups[index].count;
			for (known = 0; known < (long)NUMBEROF(spv3_opensauce_groups); known++)
			{
				if (!memcmp(spv3_opensauce_groups[known].group, groups[index].group, 4))
				{
					name = spv3_opensauce_groups[known].name;
					engine = spv3_opensauce_groups[known].engine;
				}
			}
			spv3_mode_log("%s: tag group %s (%s): %ld tags: %s", map_name, groups[index].group, name,
				groups[index].count, engine);
		}
		spv3_mode_log("%s: %ld tags of OpenSauce's and other groups outside Halo PC's", map_name, other);
	}
	if (!scenario)
	{
		spv3_mode_log("%s: the scenario is outside the tag data: no more survey", map_name);
		return;
	}
	{
		unsigned long reference = spv3_read32(scenario, 0);
		char group[5];

		spv3_group_text(reference, group);
		spv3_mode_log("%s: the scenario's first reference (OpenSauce's yelo reference): %s", map_name,
			reference && reference != 0xFFFFFFFFUL ? group : "none");
	}
	{
		long bsp_count = (long)spv3_read32(scenario, SPV3_SCENARIO_BSPS_OFFSET);
		unsigned long bsp_address = spv3_read32(scenario, SPV3_SCENARIO_BSPS_OFFSET + 4);
		unsigned long window_top = SPV3_TAG_BASE + spv3_mode_tag_cache_size(0x01700000UL);
		long bsp;

		spv3_mode_log("%s: %ld structure BSPs (Custom Edition's limit 16, OpenSauce's 32)", map_name, bsp_count);
		for (bsp = 0; bsp_count > 0 && bsp < bsp_count && bsp < 64; bsp++)
		{
			unsigned char const *reference = spv3_at(tags, tag_data_size, bsp_address + bsp * SPV3_BSP_REFERENCE_SIZE,
				SPV3_BSP_REFERENCE_SIZE);
			unsigned long base, size;

			if (!reference)
				break;
			size = spv3_read32(reference, 4);
			base = spv3_read32(reference, 8);
			spv3_mode_log("%s: BSP %ld: %lu bytes at file %lu, linked at 0x%08lX to 0x%08lX: %s", map_name, bsp, size,
				spv3_read32(reference, 0), base, (base + size) & 0xFFFFFFFFUL,
				base >= SPV3_TAG_BASE && base <= window_top && size <= window_top - base ? "inside the tag window" :
					"OUTSIDE the tag window");
		}
	}
	{
		unsigned long syntax_size = spv3_read32(scenario, SPV3_SCENARIO_SYNTAX_OFFSET);
		unsigned long strings_size = spv3_read32(scenario, SPV3_SCENARIO_STRINGS_OFFSET);
		unsigned char const *syntax = spv3_at(tags, tag_data_size,
			spv3_read32(scenario, SPV3_SCENARIO_SYNTAX_OFFSET + 0x0C), 0x38);

		spv3_mode_log("%s: scripts: %ld scripts, %ld globals, syntax data %lu bytes (%u nodes; Custom Edition's "
			"limit 19001, OpenSauce's 28501), strings %lu bytes", map_name,
			(long)spv3_read32(scenario, SPV3_SCENARIO_SCRIPTS_OFFSET),
			(long)spv3_read32(scenario, SPV3_SCENARIO_GLOBALS_OFFSET), syntax_size,
			syntax ? (unsigned)spv3_read16(syntax, 0x20) : 0u, strings_size);
	}
}

/* ---------- counted names */

static void spv3_count(
	struct spv3_name_count *names,
	long *count,
	char const *name)
{
	long index;

	if (!spv3_mode_active() || !name)
		return;
	for (index = 0; index < *count; index++)
	{
		if (!strncmp(names[index].name, name, SPV3_NAME_LENGTH - 1))
		{
			names[index].count++;
			return;
		}
	}
	if (*count < SPV3_MAXIMUM_NAMES)
	{
		snprintf(names[*count].name, sizeof(names[*count].name), "%s", name);
		names[*count].count = 1;
		(*count)++;
	}
}

void spv3_mode_note_missing_function(
	char const *name)
{
	spv3_count(spv3_globals.functions, &spv3_globals.function_count, name);
}

void spv3_mode_note_missing_global(
	char const *name)
{
	spv3_count(spv3_globals.globals, &spv3_globals.global_count, name);
}

/* most used first */
static void spv3_sort_names(
	struct spv3_name_count *names,
	long count)
{
	long index;

	for (index = 1; index < count; index++)
	{
		struct spv3_name_count name = names[index];
		long place = index;

		while (place > 0 && names[place - 1].count < name.count)
		{
			names[place] = names[place - 1];
			place--;
		}
		names[place] = name;
	}
}

void spv3_mode_scripts_summary(
	long dropped_scripts,
	long scripts,
	long dropped_globals,
	long globals)
{
	long index;

	if (!spv3_mode_active())
		return;
	spv3_mode_log("%s: scripts: %ld of %ld scripts and %ld of %ld globals dropped; %ld functions and %ld external "
		"globals the game lacks:", spv3_globals.surveyed, dropped_scripts, scripts, dropped_globals, globals,
		spv3_globals.function_count, spv3_globals.global_count);
	spv3_sort_names(spv3_globals.functions, spv3_globals.function_count);
	spv3_sort_names(spv3_globals.globals, spv3_globals.global_count);
	for (index = 0; index < spv3_globals.function_count; index++)
		spv3_mode_log("%s: missing function %s: %ld calls: not supported, scripts calling it dropped",
			spv3_globals.surveyed, spv3_globals.functions[index].name, spv3_globals.functions[index].count);
	for (index = 0; index < spv3_globals.global_count; index++)
		spv3_mode_log("%s: missing global %s: %ld uses: not supported, scripts using it dropped",
			spv3_globals.surveyed, spv3_globals.globals[index].name, spv3_globals.globals[index].count);
	spv3_globals.function_count = spv3_globals.global_count = 0;
}

void spv3_mode_note_resource_miss(
	char const *kind,
	char const *tag_name)
{
	if (!spv3_mode_active())
		return;
	spv3_globals.resource_misses++;
	if (spv3_globals.resource_misses <= SPV3_RESOURCE_MISSES_LOGGED)
		spv3_mode_log("%s: resource miss: %s %s", spv3_globals.surveyed, kind ? kind : "?", tag_name ? tag_name : "?");
	else if (spv3_globals.resource_misses == SPV3_RESOURCE_MISSES_LOGGED + 1)
		spv3_mode_log("%s: more resource misses: no more logged", spv3_globals.surveyed);
}

void spv3_mode_note_correction(
	char const *what)
{
	if (!spv3_mode_active())
		return;
	spv3_globals.corrections++;
	if (spv3_globals.corrections <= SPV3_CORRECTIONS_LOGGED)
		spv3_mode_log("%s: corrected: %s", spv3_globals.surveyed, what ? what : "?");
	else if (spv3_globals.corrections == SPV3_CORRECTIONS_LOGGED + 1)
		spv3_mode_log("%s: more corrections: no more logged", spv3_globals.surveyed);
}
