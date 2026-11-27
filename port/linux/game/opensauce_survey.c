/*
OPENSAUCE_SURVEY.C

The log of an OpenSauce map (opensauce_survey.h), for any map with
OpenSauce's header and, in SPV3 mode (spv3_mode.c), for every Custom Edition
map, as SPV3 mode always logged them: everything about a map is said once,
in debug.txt, with an "opensauce:" prefix ("spv3:" in SPV3 mode, so SPV3's
reports and tools/spv3_scan.py read as before):

- the survey of a map as it is checked (ce_map_checks.c): its header and
  OpenSauce's, its tags' groups (OpenSauce's named, with what the engine
  does with each), its structure BSPs against the tag window and the size of
  its scripts;
- the functions and globals its scripts name that the game lacks
  (hs_compile.c), counted and summed up once its scripts are compiled;
- the resources a tag asks a resource map for that it does not have
  (ce_resources.c), and the corrections the game makes to what a map asks
  for, the first of each logged.

A map without OpenSauce's header logs nothing outside SPV3 mode.

OpenSauce's formats and limits are read from its source (GPL-3.0) as facts
(cache_files_structures_yelo.hpp, blam_memory_upgrades.hpp, its group tags);
no code of it is here. tools/spv3_scan.py writes the same survey offline.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "errors.h"
#include "opensauce.h"
#include "opensauce_maps.h"
#include "opensauce_survey.h"
#include "spv3_mode.h"

#ifdef OPENSAUCE_SURVEY

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	SURVEY_TAG_BASE = 0x40440000,
	SURVEY_HEADER_YELO_OFFSET = 0x70,
	SURVEY_TAG_INSTANCE_SIZE = 0x20,
	SURVEY_SCENARIO_BSPS_OFFSET = 0x5A4,
	SURVEY_SCENARIO_SYNTAX_OFFSET = 0x474,
	SURVEY_SCENARIO_STRINGS_OFFSET = 0x488,
	SURVEY_SCENARIO_SCRIPTS_OFFSET = 0x49C,
	SURVEY_SCENARIO_GLOBALS_OFFSET = 0x4A8,
	SURVEY_SCENARIO_READ_SIZE = 0x5B0,
	SURVEY_BSP_REFERENCE_SIZE = 0x20,
	/* the names counted for a map's summary */
	SURVEY_MAXIMUM_NAMES = 256,
	SURVEY_NAME_LENGTH = 48,
	SURVEY_MAXIMUM_GROUPS = 128,
	SURVEY_CORRECTIONS_LOGGED = 32,
	SURVEY_RESOURCE_MISSES_LOGGED = 32,
	/* 'yelo', as the header's bytes read little-endian */
	SURVEY_YELO_SIGNATURE = 0x79656C6F,
};

/* OpenSauce's header flags (cache_files_structures_yelo.hpp) */
static char const *const survey_yelo_flag_names[] =
{
	"uses_memory_upgrades",
	"uses_mod_data_files",
	"is_protected",
	"uses_game_state_upgrades",
	"has_compression_params",
};

/* Custom Edition's own tag groups; any other is OpenSauce's or unknown */
static char const survey_stock_groups[][5] =
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
} const survey_opensauce_groups[] =
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
	{ "avtc", "actor_variant_transform_collection", "checked; actors transform (opensauce_transforms.c)" },
	{ "avti", "actor_variant_transform_in", "checked; read by the transforms (opensauce_transforms.c)" },
	{ "avto", "actor_variant_transform_out", "checked; read by the transforms (opensauce_transforms.c)" },
	{ "magy", "model_animations_yelo", "not supported, not read" },
	{ "eqhi", "equipment_hud_interface", "not supported, not read" },
	{ "sidy", "string_id_yelo", "not supported, not read" },
	{ "sily", "text_value_pair_definition", "not supported, not read" },
	{ "tag+", "tag_database", "not supported, not read" },
	{ "memg", "cache_memory_gestalt", "not supported, not read" },
	{ "meml", "cache_memory_layout_table", "not supported, not read" },
};

/* ---------- globals */

struct survey_name_count
{
	char name[SURVEY_NAME_LENGTH];
	long count;
};

static struct
{
	struct survey_name_count functions[SURVEY_MAXIMUM_NAMES];
	long function_count;
	struct survey_name_count globals[SURVEY_MAXIMUM_NAMES];
	long global_count;
	long resource_misses;
	long corrections;
	char surveyed[64];
	/* the map being checked has OpenSauce's header (opensauce_survey_map),
	until its checks are done (opensauce_survey_checked) */
	boolean checking;
} survey_globals;

static unsigned long survey_read32(
	void const *data,
	unsigned long offset);

/* ---------- public code */

boolean opensauce_survey_active(
	void)
{
	return spv3_mode_active() || survey_globals.checking || opensauce_map_active();
}

void opensauce_survey_log(
	char const *format,
	...)
{
	char text[768];
	va_list arguments;

	if (!opensauce_survey_active())
		return;
	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	error(_error_silent, "%s: %s", spv3_mode_active() ? "spv3" : "opensauce", text);
}

boolean opensauce_survey_wanted(
	void const *cache_header)
{
	if (spv3_mode_active())
		return TRUE;
	return cache_header &&
		survey_read32((unsigned char const *)cache_header + SURVEY_HEADER_YELO_OFFSET, 0) == SURVEY_YELO_SIGNATURE;
}

void opensauce_survey_checked(
	char const *map_name,
	char const *refusal)
{
	if (refusal)
		opensauce_survey_log("%s: REFUSED by the map checks: %s", map_name, refusal);
	survey_globals.checking = FALSE;
}

/* ---------- the survey */

static unsigned long survey_read32(
	void const *data,
	unsigned long offset)
{
	unsigned char const *bytes = (unsigned char const *)data + offset;

	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) | ((unsigned long)bytes[2] << 16) |
		((unsigned long)bytes[3] << 24);
}

static unsigned short survey_read16(
	void const *data,
	unsigned long offset)
{
	unsigned char const *bytes = (unsigned char const *)data + offset;

	return (unsigned short)(bytes[0] | (bytes[1] << 8));
}

/* the bytes at a tag address, or NULL outside the tag data */
static unsigned char const *survey_at(
	void const *tags,
	unsigned long tag_data_size,
	unsigned long address,
	unsigned long size)
{
	unsigned long offset = address - SURVEY_TAG_BASE;

	if (address < SURVEY_TAG_BASE || offset > tag_data_size || size > tag_data_size - offset)
		return NULL;
	return (unsigned char const *)tags + offset;
}

static void survey_group_text(
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

static boolean survey_group_is_stock(
	char const *group)
{
	long index;

	for (index = 0; index < (long)NUMBEROF(survey_stock_groups); index++)
	{
		if (!memcmp(survey_stock_groups[index], group, 4))
			return TRUE;
	}
	return FALSE;
}

void opensauce_survey_map(
	char const *map_name,
	void const *cache_header,
	void const *tags,
	unsigned long tag_data_size,
	unsigned long file_size)
{
	unsigned char const *header = (unsigned char const *)cache_header;
	unsigned char const *yelo = header + SURVEY_HEADER_YELO_OFFSET;
	struct
	{
		char group[5];
		long count;
	} groups[SURVEY_MAXIMUM_GROUPS];
	long group_count = 0;
	unsigned long instances, scenario_index, tag_count, index;
	unsigned char const *scenario = NULL;
	char line[512];

	if (!header || !opensauce_survey_wanted(cache_header))
	{
		/* (a map without OpenSauce's header, outside SPV3 mode: not logged,
		and the last one's survey over) */
		survey_globals.surveyed[0] = 0;
		return;
	}
	survey_globals.checking = survey_read32(yelo, 0) == SURVEY_YELO_SIGNATURE;
	if (!tags || tag_data_size < 0x28)
		return;
	/* (once a map: checked, then loaded, it is read twice) */
	if (!_stricmp(survey_globals.surveyed, map_name))
		return;
	snprintf(survey_globals.surveyed, sizeof(survey_globals.surveyed), "%s", map_name);
	survey_globals.function_count = survey_globals.global_count = 0;
	survey_globals.resource_misses = survey_globals.corrections = 0;

	opensauce_survey_log("---- %s: map survey ----", map_name);
	opensauce_survey_log("%s: cache version %lu, name %.32s, build %.32s, type %lu, file %lu bytes (header says %lu), "
		"tag data %lu bytes at %lu", map_name, survey_read32(header, 4), (char const *)header + 0x20,
		(char const *)header + 0x40, survey_read32(header, 0x60), file_size, survey_read32(header, 8), tag_data_size,
		survey_read32(header, 0x10));
	if (survey_read32(yelo, 0) == SURVEY_YELO_SIGNATURE)
	{
		unsigned short flags = survey_read16(yelo, 6);
		unsigned long upgrade_bits = survey_read32(yelo, 0x0C);
		float upgrade;
		long bit;

		memcpy(&upgrade, &upgrade_bits, sizeof(upgrade));
		line[0] = 0;
		for (bit = 0; bit < (long)NUMBEROF(survey_yelo_flag_names); bit++)
		{
			if (flags & (1 << bit))
				snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%s", line[0] ? ", " : "",
					survey_yelo_flag_names[bit]);
		}
		opensauce_survey_log("%s: OpenSauce header v%u, flags 0x%04X (%s), memory upgrade %.2f, tag versions %u/%u, "
			"mod name '%.32s', CheApe definitions %lu bytes at %lu", map_name, survey_read16(yelo, 4), flags,
			line[0] ? line : "none", upgrade, survey_read16(yelo, 8), survey_read16(yelo, 0x0A), (char const *)yelo + 0x40,
			survey_read32(yelo, 0x10), survey_read32(yelo, 0x18));
	}
	else
		opensauce_survey_log("%s: no OpenSauce header (a stock Custom Edition cache)", map_name);
	opensauce_survey_log("%s: tag data %lu bytes of the game's tag cache's %lu (Custom Edition 0x01700000, OpenSauce "
		"0x%08lX)", map_name, tag_data_size, opensauce_maps_header_tag_cache_size(cache_header, 0x01700000UL),
		(unsigned long)OPENSAUCE_TAG_CACHE_SIZE);

	instances = survey_read32(tags, 0);
	scenario_index = survey_read32(tags, 4) & 0xFFFF;
	tag_count = survey_read32(tags, 12);
	opensauce_survey_log("%s: %lu tags (Custom Edition's limit 5120, OpenSauce's %d), tag index at 0x%08lX", map_name,
		tag_count, OPENSAUCE_MAXIMUM_TAG_COUNT, instances);
	/* (the count against the tag data first: its product with the instance
	size would wrap in 32 bits) */
	if (tag_count > tag_data_size / SURVEY_TAG_INSTANCE_SIZE ||
		!survey_at(tags, tag_data_size, instances, tag_count * SURVEY_TAG_INSTANCE_SIZE))
	{
		opensauce_survey_log("%s: the tag index is outside the tag data: no more survey", map_name);
		return;
	}
	for (index = 0; index < tag_count; index++)
	{
		unsigned char const *instance = survey_at(tags, tag_data_size, instances + index * SURVEY_TAG_INSTANCE_SIZE,
			SURVEY_TAG_INSTANCE_SIZE);
		char group[5];
		long found;

		if (!instance)
			break;
		survey_group_text(survey_read32(instance, 0), group);
		for (found = 0; found < group_count && memcmp(groups[found].group, group, 4); found++)
			;
		if (found == group_count && group_count < SURVEY_MAXIMUM_GROUPS)
		{
			memcpy(groups[found].group, group, 5);
			groups[found].count = 0;
			group_count++;
		}
		if (found < group_count)
			groups[found].count++;
		if (index == scenario_index)
			scenario = survey_at(tags, tag_data_size, survey_read32(instance, 0x14), SURVEY_SCENARIO_READ_SIZE);
	}
	{
		long stock = 0;
		long other = 0;

		line[0] = 0;
		for (index = 0; index < (unsigned long)group_count; index++)
		{
			if (survey_group_is_stock(groups[index].group))
			{
				stock += groups[index].count;
				snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%s %ld", line[0] ? ", " : "",
					groups[index].group, groups[index].count);
			}
		}
		opensauce_survey_log("%s: stock tag groups (%ld tags): %s", map_name, stock, line);
		for (index = 0; index < (unsigned long)group_count; index++)
		{
			long known;
			char const *name = "unknown";
			char const *engine = "not read";

			if (survey_group_is_stock(groups[index].group))
				continue;
			other += groups[index].count;
			for (known = 0; known < (long)NUMBEROF(survey_opensauce_groups); known++)
			{
				if (!memcmp(survey_opensauce_groups[known].group, groups[index].group, 4))
				{
					name = survey_opensauce_groups[known].name;
					engine = survey_opensauce_groups[known].engine;
				}
			}
			opensauce_survey_log("%s: tag group %s (%s): %ld tags: %s", map_name, groups[index].group, name,
				groups[index].count, engine);
		}
		opensauce_survey_log("%s: %ld tags of OpenSauce's and other groups outside Halo PC's", map_name, other);
	}
	if (!scenario)
	{
		opensauce_survey_log("%s: the scenario is outside the tag data: no more survey", map_name);
		return;
	}
	{
		unsigned long reference = survey_read32(scenario, 0);
		char group[5];

		survey_group_text(reference, group);
		opensauce_survey_log("%s: the scenario's first reference (OpenSauce's yelo reference): %s", map_name,
			reference && reference != 0xFFFFFFFFUL ? group : "none");
	}
	{
		long bsp_count = (long)survey_read32(scenario, SURVEY_SCENARIO_BSPS_OFFSET);
		unsigned long bsp_address = survey_read32(scenario, SURVEY_SCENARIO_BSPS_OFFSET + 4);
		unsigned long window_top = SURVEY_TAG_BASE + opensauce_maps_header_tag_cache_size(cache_header, 0x01700000UL);
		long bsp;

		opensauce_survey_log("%s: %ld structure BSPs (Custom Edition's limit 16, OpenSauce's 32)", map_name, bsp_count);
		for (bsp = 0; bsp_count > 0 && bsp < bsp_count && bsp < 64; bsp++)
		{
			unsigned char const *reference = survey_at(tags, tag_data_size, bsp_address + bsp * SURVEY_BSP_REFERENCE_SIZE,
				SURVEY_BSP_REFERENCE_SIZE);
			unsigned long base, size;

			if (!reference)
				break;
			size = survey_read32(reference, 4);
			base = survey_read32(reference, 8);
			opensauce_survey_log("%s: BSP %ld: %lu bytes at file %lu, linked at 0x%08lX to 0x%08lX: %s", map_name, bsp, size,
				survey_read32(reference, 0), base, (base + size) & 0xFFFFFFFFUL,
				base >= SURVEY_TAG_BASE && base <= window_top && size <= window_top - base ? "inside the tag window" :
					"OUTSIDE the tag window");
		}
	}
	{
		unsigned long syntax_size = survey_read32(scenario, SURVEY_SCENARIO_SYNTAX_OFFSET);
		unsigned long strings_size = survey_read32(scenario, SURVEY_SCENARIO_STRINGS_OFFSET);
		unsigned char const *syntax = survey_at(tags, tag_data_size,
			survey_read32(scenario, SURVEY_SCENARIO_SYNTAX_OFFSET + 0x0C), 0x38);

		opensauce_survey_log("%s: scripts: %ld scripts, %ld globals, syntax data %lu bytes (%u nodes; Custom Edition's "
			"limit 19001, OpenSauce's 28501), strings %lu bytes", map_name,
			(long)survey_read32(scenario, SURVEY_SCENARIO_SCRIPTS_OFFSET),
			(long)survey_read32(scenario, SURVEY_SCENARIO_GLOBALS_OFFSET), syntax_size,
			syntax ? (unsigned)survey_read16(syntax, 0x20) : 0u, strings_size);
	}
}

/* ---------- counted names */

static void survey_count(
	struct survey_name_count *names,
	long *count,
	char const *name)
{
	long index;

	if (!opensauce_survey_active() || !name)
		return;
	for (index = 0; index < *count; index++)
	{
		if (!strncmp(names[index].name, name, SURVEY_NAME_LENGTH - 1))
		{
			names[index].count++;
			return;
		}
	}
	if (*count < SURVEY_MAXIMUM_NAMES)
	{
		snprintf(names[*count].name, sizeof(names[*count].name), "%s", name);
		names[*count].count = 1;
		(*count)++;
	}
}

void opensauce_survey_note_missing_function(
	char const *name)
{
	survey_count(survey_globals.functions, &survey_globals.function_count, name);
}

void opensauce_survey_note_missing_global(
	char const *name)
{
	survey_count(survey_globals.globals, &survey_globals.global_count, name);
}

/* most used first */
static void survey_sort_names(
	struct survey_name_count *names,
	long count)
{
	long index;

	for (index = 1; index < count; index++)
	{
		struct survey_name_count name = names[index];
		long place = index;

		while (place > 0 && names[place - 1].count < name.count)
		{
			names[place] = names[place - 1];
			place--;
		}
		names[place] = name;
	}
}

void opensauce_survey_scripts_summary(
	long dropped_scripts,
	long scripts,
	long dropped_globals,
	long globals)
{
	long index;

	if (!opensauce_survey_active())
		return;
	opensauce_survey_log("%s: scripts: %ld of %ld scripts and %ld of %ld globals dropped; %ld functions and %ld external "
		"globals the game lacks:", survey_globals.surveyed, dropped_scripts, scripts, dropped_globals, globals,
		survey_globals.function_count, survey_globals.global_count);
	survey_sort_names(survey_globals.functions, survey_globals.function_count);
	survey_sort_names(survey_globals.globals, survey_globals.global_count);
	for (index = 0; index < survey_globals.function_count; index++)
		opensauce_survey_log("%s: missing function %s: %ld calls: not supported, scripts calling it dropped",
			survey_globals.surveyed, survey_globals.functions[index].name, survey_globals.functions[index].count);
	for (index = 0; index < survey_globals.global_count; index++)
		opensauce_survey_log("%s: missing global %s: %ld uses: not supported, scripts using it dropped",
			survey_globals.surveyed, survey_globals.globals[index].name, survey_globals.globals[index].count);
	survey_globals.function_count = survey_globals.global_count = 0;
}

void opensauce_survey_note_resource_miss(
	char const *kind,
	char const *tag_name)
{
	if (!opensauce_survey_active())
		return;
	survey_globals.resource_misses++;
	if (survey_globals.resource_misses <= SURVEY_RESOURCE_MISSES_LOGGED)
		opensauce_survey_log("%s: resource miss: %s %s", survey_globals.surveyed, kind ? kind : "?", tag_name ? tag_name : "?");
	else if (survey_globals.resource_misses == SURVEY_RESOURCE_MISSES_LOGGED + 1)
		opensauce_survey_log("%s: more resource misses: no more logged", survey_globals.surveyed);
}

void opensauce_survey_note_correction(
	char const *what)
{
	if (!opensauce_survey_active())
		return;
	survey_globals.corrections++;
	if (survey_globals.corrections <= SURVEY_CORRECTIONS_LOGGED)
		opensauce_survey_log("%s: corrected: %s", survey_globals.surveyed, what ? what : "?");
	else if (survey_globals.corrections == SURVEY_CORRECTIONS_LOGGED + 1)
		opensauce_survey_log("%s: more corrections: no more logged", survey_globals.surveyed);
}

#endif
