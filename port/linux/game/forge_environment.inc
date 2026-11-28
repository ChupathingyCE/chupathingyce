/*
FORGE_ENVIRONMENT.INC

Forge's sky and fog (included by forge.c, for its statics): a layout may
show another sky of the map's in place of its own - any sky tag the loaded
map has, the scenario's first - and a fog of its own outdoors: its colour,
its density, where it starts and where it is whole. The Forge menu's
SKY AND FOG tab sets them (SKY, FOG, FOG COLOR, FOG DENSITY, FOG START,
FOG END);
they are saved with the layout as its version 15 record (forge_files.h) and
drawn the same for every player of a Forge game or of the layout played.

Nothing of the map is changed: the scenario keeps its skies; where the
game asks which sky a part of the map shows (scenario_get_sky_definition_
index: the sky drawn, its lights, its fog) the layout's is answered, and
the outdoor fog the game draws (scenario_get_atmospheric_fog) is the
layout's. The indoors, the map's fog planes and every other game are as
ever. Only drawing: nothing of the game's simulation reads a sky.
*/

/* ---------- constants */

/* the most skies offered (the map's sky tags) */
#define FORGE_ENVIRONMENT_MOST_SKIES 16

/* FOG COLOR's choices */
static struct
{
	char const *name;
	unsigned char rgb[3];
} const forge_fog_colors[] =
{
	{ "GREY", { 128, 128, 132 } },
	{ "WHITE", { 225, 228, 232 } },
	{ "BLUE", { 88, 126, 196 } },
	{ "TEAL", { 70, 150, 150 } },
	{ "GREEN", { 92, 150, 84 } },
	{ "SAND", { 196, 164, 112 } },
	{ "ORANGE", { 214, 124, 58 } },
	{ "RED", { 176, 52, 40 } },
	{ "PURPLE", { 116, 72, 166 } },
	{ "BLACK", { 12, 12, 14 } },
};

/* FOG DENSITY's, FOG START's and FOG END's choices (percent; world units) */
static unsigned char const forge_fog_densities[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
static unsigned char const forge_fog_starts[] = { 0, 5, 10, 20, 40, 80, 120 };
static unsigned short const forge_fog_ends[] = { 20, 40, 60, 100, 160, 250, 400, 510 };

/* ---------- globals */

/* the layout's sky (its tag; NONE: the map's own) and fog (its colour,
density percent, start and end in world units) */
static struct
{
	long sky_tag_index;
	boolean fog;
	unsigned char color[3];
	unsigned char density;
	unsigned char start;
	unsigned short end;
	/* the fog as the game reads it (forge_outdoor_fog) */
	struct sky_atmospheric_fog outdoor_fog;
} forge_environment = { NONE };

/* ---------- private code */

/* the map's skies: the scenario's first, then the other sky tags loaded;
how many (at most FORGE_ENVIRONMENT_MOST_SKIES) */
static short forge_environment_skies(
	long *skies)
{
	struct tag_iterator iterator;
	long tag_index;
	short count = 0;
	short index;

	if (global_scenario_index == NONE)
		return 0;
	tag_iterator_new(&iterator, SKY_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE && count < FORGE_ENVIRONMENT_MOST_SKIES)
	{
		if (tag_get_group_tag(tag_index) != SKY_DEFINITION_TAG)
			continue;
		skies[count++] = tag_index;
	}
	/* (the scenario's first sky first: the map's own) */
	for (index = 1; index < count; index++)
	{
		if (skies[index] == scenario_get_sky_definition_index(0))
		{
			long first = skies[0];

			skies[0] = skies[index];
			skies[index] = first;
		}
	}
	return count;
}

/* the fog the game reads, from the layout's */
static void forge_environment_fog_update(
	void)
{
	struct sky_atmospheric_fog *fog = &forge_environment.outdoor_fog;

	csmemset(fog, 0, sizeof(*fog));
	fog->color.red = forge_environment.color[0] / 255.f;
	fog->color.green = forge_environment.color[1] / 255.f;
	fog->color.blue = forge_environment.color[2] / 255.f;
	fog->maximum_density = forge_environment.density / 100.f;
	fog->start_distance = (real)forge_environment.start;
	fog->opaque_distance = (real)forge_environment.end;
}

/* the fog drawn taken from the layout at once: the game eases the fog
toward a new one only as the camera moves (scenario_get_atmospheric_fog),
and the monitor may not move while it changes the fog */
static void forge_environment_fog_restart(
	void)
{
	short index;

	if (!scenario_globals)
		return;
	for (index = 0; index < (short)NUMBEROF(scenario_globals->atmospheric_fog); index++)
		scenario_globals->atmospheric_fog[index].valid = FALSE;
}

static void forge_environment_clear(
	void)
{
	forge_environment.sky_tag_index = NONE;
	forge_environment.fog = FALSE;
	forge_environment.color[0] = forge_fog_colors[0].rgb[0];
	forge_environment.color[1] = forge_fog_colors[0].rgb[1];
	forge_environment.color[2] = forge_fog_colors[0].rgb[2];
	forge_environment.density = 40;
	forge_environment.start = 10;
	forge_environment.end = 160;
	forge_environment_fog_update();
}

static void forge_environment_log(
	char const *what)
{
	platform_log("forge: %s: sky %s, fog %s (colour %d %d %d, density %d%%, %d to %d world units)", what,
		forge_environment.sky_tag_index != NONE ? tag_get_name(forge_environment.sky_tag_index) : "the map's",
		forge_environment.fog ? "the layout's" : "the map's", forge_environment.color[0], forge_environment.color[1],
		forge_environment.color[2], forge_environment.density, forge_environment.start, forge_environment.end);
}

/* (the map loaded for a Forge game or a layout played) the layout's sky
and fog: a version 15 file's (checked by the validator); a sky the map
does not have is the map's own */
static void forge_environment_from_layout(
	struct forge_file_layout const *layout)
{
	short index;

	if (forge_file_layout_version(layout) < FORGE_FILE_ENVIRONMENT_VERSION)
		return;
	for (index = 0; index < layout->record_count && index < FORGE_FILE_MAXIMUM_RECORDS; index++)
	{
		struct forge_file_record const *record = &layout->records[index];
		unsigned char const *params = record->params;

		if (record->kind != _forge_record_environment)
			continue;
		if ((params[FORGE_FILE_ENVIRONMENT_PARAM_SETS] & FORGE_FILE_ENVIRONMENT_SKY) && record->path_index < layout->path_count)
		{
			long tag_index = tag_loaded(SKY_DEFINITION_TAG, layout->paths[record->path_index].path);

			forge_environment.sky_tag_index = tag_index != NONE && tag_get_group_tag(tag_index) == SKY_DEFINITION_TAG ?
				tag_index : NONE;
			if (forge_environment.sky_tag_index == NONE)
				platform_log("forge: the layout's sky %s is not on this map: the map's own", layout->paths[record->path_index].path);
		}
		if (params[FORGE_FILE_ENVIRONMENT_PARAM_SETS] & FORGE_FILE_ENVIRONMENT_FOG)
		{
			forge_environment.fog = TRUE;
			forge_environment.color[0] = params[FORGE_FILE_ENVIRONMENT_PARAM_COLOR];
			forge_environment.color[1] = params[FORGE_FILE_ENVIRONMENT_PARAM_COLOR + 1];
			forge_environment.color[2] = params[FORGE_FILE_ENVIRONMENT_PARAM_COLOR + 2];
			forge_environment.density = params[FORGE_FILE_ENVIRONMENT_PARAM_DENSITY];
			forge_environment.start = params[FORGE_FILE_ENVIRONMENT_PARAM_START];
			forge_environment.end = (unsigned short)(params[FORGE_FILE_ENVIRONMENT_PARAM_OPAQUE] * FORGE_FILE_FOG_OPAQUE_UNIT);
		}
		forge_environment_fog_update();
		forge_environment_log("the layout's sky and fog");
		break;
	}
}

/* (forge_variant_capture, a save) the sky and fog as the layout's version 15
record, when it has either; how many could not be (the file full) */
static short forge_environment_capture(
	struct forge_file_layout *layout)
{
	struct forge_file_record *record;
	unsigned char sets = 0;
	short path = NONE;

	if (forge_environment.sky_tag_index != NONE)
		sets |= FORGE_FILE_ENVIRONMENT_SKY;
	if (forge_environment.fog)
		sets |= FORGE_FILE_ENVIRONMENT_FOG;
	if (!sets)
		return 0;
	if (layout->record_count >= FORGE_FILE_MAXIMUM_RECORDS)
		return 1;
	if (sets & FORGE_FILE_ENVIRONMENT_SKY)
	{
		char const *name = tag_get_name(forge_environment.sky_tag_index);

		if (!name || !forge_file_path_valid(name, (int)strlen(name)))
		{
			sets &= (unsigned char)~FORGE_FILE_ENVIRONMENT_SKY;
		}
		else
		{
			for (path = 0; path < layout->path_count; path++)
			{
				if (layout->paths[path].group_tag == FORGE_FILE_SKY_GROUP && !strcmp(layout->paths[path].path, name))
					break;
			}
			if (path == layout->path_count)
			{
				if (layout->path_count >= FORGE_FILE_MAXIMUM_PATHS)
					return 1;
				layout->paths[path].group_tag = FORGE_FILE_SKY_GROUP;
				snprintf(layout->paths[path].path, sizeof(layout->paths[path].path), "%s", name);
				layout->path_count++;
			}
		}
		if (!sets)
			return 1;
	}
	record = &layout->records[layout->record_count++];
	csmemset(record, 0, sizeof(*record));
	record->kind = _forge_record_environment;
	record->path_index = (sets & FORGE_FILE_ENVIRONMENT_SKY) ? (unsigned short)path : FORGE_FILE_NO_PATH;
	record->params[FORGE_FILE_ENVIRONMENT_PARAM_SETS] = sets;
	if (sets & FORGE_FILE_ENVIRONMENT_FOG)
	{
		record->params[FORGE_FILE_ENVIRONMENT_PARAM_COLOR] = forge_environment.color[0];
		record->params[FORGE_FILE_ENVIRONMENT_PARAM_COLOR + 1] = forge_environment.color[1];
		record->params[FORGE_FILE_ENVIRONMENT_PARAM_COLOR + 2] = forge_environment.color[2];
		record->params[FORGE_FILE_ENVIRONMENT_PARAM_DENSITY] = (unsigned char)PIN(forge_environment.density, 1,
			FORGE_FILE_MAXIMUM_FOG_DENSITY);
		record->params[FORGE_FILE_ENVIRONMENT_PARAM_START] = (unsigned char)MIN(forge_environment.start,
			FORGE_FILE_MAXIMUM_FOG_START);
		record->params[FORGE_FILE_ENVIRONMENT_PARAM_OPAQUE] = (unsigned char)PIN((forge_environment.end + 1) / 2,
			forge_environment.start / 2 + 1, 255);
	}
	layout->flags |= FORGE_FILE_FLAG_NETGAME_SPAWNS;
	return 0;
}

/* the next choice of a list of bytes after (or before) the one at or past
value */
static short forge_environment_choice(
	short count,
	long value,
	unsigned char const *bytes,
	unsigned short const *shorts)
{
	short index;

	for (index = 0; index < count - 1; index++)
	{
		if ((bytes ? (long)bytes[index] : (long)shorts[index]) >= value)
			break;
	}
	return index;
}

/* an option of the sky and fog stepped (SKY AND FOG's rows; the console's forge
environment) */
static void forge_environment_step(
	short row,
	short step)
{
	short index;
	short count;

	switch (row)
	{
	case _forge_environment_sky:
	{
		long skies[FORGE_ENVIRONMENT_MOST_SKIES];
		short sky_count = forge_environment_skies(skies);

		/* (the map's own, then each other sky) */
		for (index = 0; index < sky_count && skies[index] != forge_environment.sky_tag_index; index++)
			;
		if (forge_environment.sky_tag_index == NONE || index >= sky_count)
			index = 0;
		index = (short)((index + step + sky_count) % MAX(sky_count, 1));
		forge_environment.sky_tag_index = index == 0 || index >= sky_count ? NONE : skies[index];
		platform_log("forge: the map has %d skies", sky_count);
		break;
	}
	case _forge_environment_fog:
		forge_environment.fog = !forge_environment.fog;
		break;
	case _forge_environment_color:
		count = (short)NUMBEROF(forge_fog_colors);
		for (index = 0; index < count; index++)
		{
			if (!memcmp(forge_fog_colors[index].rgb, forge_environment.color, 3))
				break;
		}
		index = index >= count ? 0 : (short)((index + step + count) % count);
		csmemcpy(forge_environment.color, forge_fog_colors[index].rgb, 3);
		forge_environment.fog = TRUE;
		break;
	case _forge_environment_density:
		count = (short)NUMBEROF(forge_fog_densities);
		index = forge_environment_choice(count, forge_environment.density, forge_fog_densities, NULL);
		forge_environment.density = forge_fog_densities[(index + step + count) % count];
		forge_environment.fog = TRUE;
		break;
	case _forge_environment_start:
		count = (short)NUMBEROF(forge_fog_starts);
		index = forge_environment_choice(count, forge_environment.start, forge_fog_starts, NULL);
		forge_environment.start = forge_fog_starts[(index + step + count) % count];
		/* (the end past the start) */
		while (forge_environment.end <= forge_environment.start + 1)
		{
			short end = forge_environment_choice((short)NUMBEROF(forge_fog_ends), forge_environment.start + 2, NULL,
				forge_fog_ends);

			forge_environment.end = forge_fog_ends[end];
		}
		forge_environment.fog = TRUE;
		break;
	case _forge_environment_end:
		count = (short)NUMBEROF(forge_fog_ends);
		index = forge_environment_choice(count, forge_environment.end, NULL, forge_fog_ends);
		for (;;)
		{
			index = (short)((index + step + count) % count);
			if (forge_fog_ends[index] > forge_environment.start + 1)
				break;
		}
		forge_environment.end = forge_fog_ends[index];
		forge_environment.fog = TRUE;
		break;
	default:
		return;
	}
	forge_environment_fog_update();
	forge_environment_fog_restart();
	forge_environment_log("sky and fog");
}

/* ---------- public code */

/* whether the layout's sky and fog are the game's now: a Forge game, or a
layout played */
static boolean forge_environment_active(
	void)
{
	return forge_globals.game_active && !forge_globals.round_resetting;
}

long forge_sky_definition_index(
	long definition_index)
{
	if (definition_index == NONE || forge_environment.sky_tag_index == NONE || !forge_environment_active())
		return definition_index;
	return forge_environment.sky_tag_index;
}

struct sky_atmospheric_fog const *forge_outdoor_fog(
	struct sky_atmospheric_fog const *fog)
{
	if (!forge_environment.fog || !forge_environment_active())
		return fog;
	return &forge_environment.outdoor_fog;
}

boolean forge_environment_text(
	short row,
	char *value,
	int size)
{
	short index;

	value[0] = 0;
	switch (row)
	{
	case _forge_environment_sky:
		if (forge_environment.sky_tag_index == NONE)
		{
			snprintf(value, (size_t)size, "%s", "MAP'S");
		}
		else
		{
			char name[FORGE_NAME_SIZE];

			forge_tag_readable_name(forge_environment.sky_tag_index, name, sizeof(name));
			snprintf(value, (size_t)size, "%s", name);
			for (index = 0; value[index]; index++)
			{
				if (value[index] >= 'a' && value[index] <= 'z')
					value[index] = (char)(value[index] - 'a' + 'A');
			}
		}
		return TRUE;
	case _forge_environment_fog:
		snprintf(value, (size_t)size, "%s", forge_environment.fog ? "CUSTOM" : "MAP'S");
		return TRUE;
	case _forge_environment_color:
		for (index = 0; index < (short)NUMBEROF(forge_fog_colors); index++)
		{
			if (!memcmp(forge_fog_colors[index].rgb, forge_environment.color, 3))
				break;
		}
		if (index < (short)NUMBEROF(forge_fog_colors))
			snprintf(value, (size_t)size, "%s", forge_fog_colors[index].name);
		else
			snprintf(value, (size_t)size, "%d %d %d", forge_environment.color[0], forge_environment.color[1],
				forge_environment.color[2]);
		return TRUE;
	case _forge_environment_density:
		snprintf(value, (size_t)size, "%d%%", forge_environment.density);
		return TRUE;
	case _forge_environment_start:
		snprintf(value, (size_t)size, "%.0f M", forge_environment.start * FORGE_METRES_PER_WORLD_UNIT);
		return TRUE;
	case _forge_environment_end:
		snprintf(value, (size_t)size, "%.0f M", forge_environment.end * FORGE_METRES_PER_WORLD_UNIT);
		return TRUE;
	}
	return FALSE;
}
