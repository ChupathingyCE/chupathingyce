/*
FORGE_EFFECTS.INC

Forge's effects and decals (included by forge.c, for its statics): the
Forge menu's EFFECTS places one of the loaded map's effects (its effect
tags: fire, smoke, sparks, steam, a teleporter's shimmer...) where the
monitor puts it, started again whenever it ends (LOOP) or every so many
seconds (REPEAT); DECALS lays one of the map's decals (scorch marks,
blood, oil, signs) on the ground under where it is put, at a size.

Each is one of the layout's markers (forge_spawns.inc: placed, carried,
let go, its properties opened from it, deleted; drawn for the monitor
alone as a small marker where it stands) and saved as a version 16 record
of forge_files.h, its tag by path. What it shows is the game's own effect
or decal, seen by everyone in a Forge game and in a layout played: the
effect started a tick at a time (game.c's game_tick, forge_effects_tick),
harmless (effects_port_harmless: an explosion's effect hurts nobody), one
at a time a marker; a decal laid once a round. A layout holds at most 16
effects and 32 decals (forge_files.h), so what they cost stays small.

The tags offered are the loaded map's own. A Forge game of a multiplayer
map also has the tags Forge brought in from the campaign maps
(forge_ai_tags.c: the AI characters, the campaign vehicles and objects);
their effects and decals are offered only while the layout has a character,
vehicle or object brought in from the same mission, so that a saved layout
never names an effect or decal that isn't there when it is played without
them (it would be skipped). Saved by path, either kind.
*/

/* ---------- constants */

/* the most effect and decal tags the menu offers */
#define FORGE_EFFECTS_MOST_TAGS 512
#define FORGE_DECALS_MOST_TAGS 128
/* the REPEAT row's choices (seconds; 0: again as soon as it ends) and SIZE's
(tenths) */
static unsigned char const forge_effect_repeats[] = { 0, 1, 2, 3, 5, 10, 20, 30, 60 };
static unsigned char const forge_decal_sizes[] = { 3, 5, 8, 10, 15, 20, 30, 40 };
#define FORGE_DECAL_DEFAULT_SIZE 10
/* a decal is laid on the ground this far under its marker at most */
#define FORGE_DECAL_REACH 3.f
/* the most decal datums one decal tag lays (its chain) */
#define FORGE_DECAL_MOST_PIECES 4
/* the decals a map holds that are looked through (laid ones found) */
#define FORGE_DECALS_MOST_SCANNED 2048
/* the most campaign missions the brought-in tags come from (forge_ai_tags.c's
FORGE_AI_MAXIMUM_SOURCES, with room) */
#define FORGE_EFFECTS_MOST_SOURCES 8

/* ---------- structures */

struct forge_effect_entry
{
	long tag_index;
	char name[FORGE_NAME_SIZE];
};

/* ---------- globals */

extern struct data_array *effect_data;
extern struct data_array *global_decal_data;

static struct
{
	boolean built;
	struct forge_effect_entry effects[FORGE_EFFECTS_MOST_TAGS];
	short effect_count;
	struct forge_effect_entry decals[FORGE_DECALS_MOST_TAGS];
	short decal_count;
	/* the brought-in missions the layout used as it was made (the campaign
	maps' effects and decals offered: theirs alone) */
	char const *sources[FORGE_EFFECTS_MOST_SOURCES];
	short source_count;
	short brought_effects;
	short brought_decals;
} forge_effect_palette;

/* (a layout's effects and decals, as the map loads) one of them was not
found while the campaign maps' tags were still to be brought in
(forge_ai_tags.c): looked for again once they are, as the layout's objects
are placed (forge_effects_from_layout_brought_in) */
static boolean forge_effect_records_waiting;

/* ---------- private code */

static boolean forge_spawn_is_effect(
	struct forge_spawn const *spawn)
{
	return spawn && (spawn->kind == _forge_spawn_effect || spawn->kind == _forge_spawn_decal);
}

static short forge_effects_count(
	short kind)
{
	short index;
	short count = 0;

	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
		count += forge_globals.spawns[index].kind == kind ? 1 : 0;
	return count;
}

static int forge_effect_entry_compare(
	void const *a,
	void const *b)
{
	return strcmp(((struct forge_effect_entry const *)a)->name, ((struct forge_effect_entry const *)b)->name);
}

/* the brought-in missions (forge_ai_tags.c) the layout's objects and
characters are from, each once, in sources: how many */
static short forge_effect_used_sources(
	char const **sources,
	short most)
{
	short count = 0;
	short index;

	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		char const *source = forge_ai_tags_source_map(forge_globals.placed[index].tag_index);
		short seen;

		if (!source)
			continue;
		for (seen = 0; seen < count && strcmp(sources[seen], source); seen++)
			;
		if (seen == count && count < most)
			sources[count++] = source;
	}
	return count;
}

/* whether a tag is offered: the map's own, or brought in from a mission the
layout uses (forge_effect_palette.sources) */
static boolean forge_effect_tag_offered(
	long tag_index)
{
	char const *source = forge_ai_tags_source_map(tag_index);
	short index;

	if (!source)
		return TRUE;
	for (index = 0; index < forge_effect_palette.source_count; index++)
	{
		if (!strcmp(forge_effect_palette.sources[index], source))
			return TRUE;
	}
	return FALSE;
}

/* a group's tags, by name ("<name> 2" for the second of one name);
*brought: how many of them were brought in from a campaign map */
static short forge_effect_palette_group(
	unsigned long group_tag,
	struct forge_effect_entry *entries,
	short most,
	short *brought)
{
	struct tag_iterator iterator;
	long tag_index;
	short count = 0;
	short index;

	*brought = 0;
	tag_iterator_new(&iterator, group_tag);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE && count < most)
	{
		char const *name = tag_get_name(tag_index);

		if (tag_get_group_tag(tag_index) != group_tag || !name || !forge_file_path_valid(name, (int)strlen(name)) ||
			!forge_effect_tag_offered(tag_index))
		{
			continue;
		}
		*brought += forge_ai_tags_source_map(tag_index) ? 1 : 0;
		entries[count].tag_index = tag_index;
		forge_tag_readable_name(tag_index, entries[count].name, sizeof(entries[count].name));
		count++;
	}
	qsort(entries, (size_t)count, sizeof(entries[0]), forge_effect_entry_compare);
	{
		short first = 0;
		short repeat = 1;

		for (index = 1; index < count; index++)
		{
			if (!strcmp(entries[index].name, entries[first].name))
			{
				size_t length = strlen(entries[index].name);

				repeat++;
				if (length + 4 < sizeof(entries[index].name))
					snprintf(entries[index].name + length, sizeof(entries[index].name) - length, " %d", repeat);
			}
			else
			{
				first = index;
				repeat = 1;
			}
		}
	}
	return count;
}

/* (a new map) the palette made again when next asked for */
static void forge_effect_palette_reset(
	void)
{
	forge_effect_palette.built = FALSE;
	forge_effect_records_waiting = FALSE;
}

/* (each time the menu asks) the palette made again when the missions the
layout uses have changed (a character placed, the last one of a mission's
deleted) */
static void forge_effect_palette_ready(
	void)
{
	char const *sources[FORGE_EFFECTS_MOST_SOURCES];
	short source_count = forge_effect_used_sources(sources, FORGE_EFFECTS_MOST_SOURCES);

	if (forge_effect_palette.built && source_count == forge_effect_palette.source_count &&
		!csmemcmp(sources, forge_effect_palette.sources, (size_t)source_count * sizeof(sources[0])))
	{
		return;
	}
	csmemcpy(forge_effect_palette.sources, sources, (size_t)source_count * sizeof(sources[0]));
	forge_effect_palette.source_count = source_count;
	forge_effect_palette.effect_count = forge_effect_palette_group(EFFECT_DEFINITION_TAG, forge_effect_palette.effects,
		FORGE_EFFECTS_MOST_TAGS, &forge_effect_palette.brought_effects);
	forge_effect_palette.decal_count = forge_effect_palette_group(DECAL_GROUP_TAG, forge_effect_palette.decals,
		FORGE_DECALS_MOST_TAGS, &forge_effect_palette.brought_decals);
	forge_effect_palette.built = TRUE;
	platform_log("forge: %d effects and %d decals to place (%d and %d of them from the %d campaign maps the layout uses)",
		forge_effect_palette.effect_count, forge_effect_palette.decal_count, forge_effect_palette.brought_effects,
		forge_effect_palette.brought_decals, source_count);
}

/* the menu's EFFECTS or DECALS (category): its items, each one's name and
how many the layout has */
static short forge_effect_category_count(
	short category)
{
	forge_effect_palette_ready();
	return category == _forge_category_effects ? forge_effect_palette.effect_count :
		category == _forge_category_decals ? forge_effect_palette.decal_count : 0;
}

static boolean forge_effect_category_item(
	short category,
	short item_index,
	struct forge_item *item)
{
	struct forge_effect_entry const *entry;
	short kind = category == _forge_category_decals ? _forge_spawn_decal : _forge_spawn_effect;
	short index;

	if (item_index < 0 || item_index >= forge_effect_category_count(category))
		return FALSE;
	entry = category == _forge_category_decals ? &forge_effect_palette.decals[item_index] :
		&forge_effect_palette.effects[item_index];
	snprintf(item->name, sizeof(item->name), "%s", entry->name);
	item->placed = 0;
	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
	{
		if (forge_globals.spawns[index].kind == kind && forge_globals.spawns[index].effect_tag_index == entry->tag_index)
			item->placed++;
	}
	return TRUE;
}

/* a marker's name: its tag's */
static void forge_effect_name(
	struct forge_spawn const *spawn,
	char *name,
	int size)
{
	forge_tag_readable_name(spawn->effect_tag_index, name, size);
}

static void forge_effect_log(
	char const *what,
	struct forge_spawn const *spawn)
{
	platform_log("forge: %s the %s #%ld (%s, %s %d) at %.2f %.2f %.2f facing %.2f %.2f", what,
		forge_spawn_kind_log_names[spawn->kind % NUMBER_OF_FORGE_MARKER_KINDS], spawn->id,
		spawn->effect_tag_index != NONE ? tag_get_name(spawn->effect_tag_index) : "?",
		spawn->kind == _forge_spawn_decal ? "size" : "repeat", spawn->effect_value, spawn->home.position.x,
		spawn->home.position.y, spawn->home.position.z, spawn->home.forward.i, spawn->home.forward.j);
}

/* what an effect or decal shows taken away: its effect stopped, its decal
lifted (laid again as it is let go, or never, deleted) */
static void forge_effect_release(
	struct forge_spawn *spawn)
{
	short piece;

	if (spawn->effect_index != NONE && effect_data && datum_try_and_get(effect_data, spawn->effect_index))
		effect_delete(spawn->effect_index);
	spawn->effect_index = NONE;
	for (piece = 0; piece < spawn->decal_piece_count && piece < FORGE_DECAL_MOST_PIECES; piece++)
	{
		if (global_decal_data && datum_try_and_get(global_decal_data, spawn->decal_pieces[piece]))
			decal_delete(spawn->decal_pieces[piece]);
	}
	spawn->decal_piece_count = 0;
	spawn->effect_started = FALSE;
}

/* a new effect or decal marker's: its tag, its repeat or size, nothing shown
yet */
static void forge_effect_set(
	struct forge_spawn *spawn,
	long tag_index,
	unsigned char value)
{
	spawn->effect_tag_index = tag_index;
	spawn->effect_value = value;
	spawn->effect_index = NONE;
	spawn->decal_piece_count = 0;
	spawn->effect_started = FALSE;
	spawn->effect_round = forge_globals.round_count;
}

/* (the menu's EFFECTS or DECALS) the item chosen placed, held */
static void forge_effect_choose(
	short category,
	short item_index)
{
	struct forge_effect_entry const *entry;
	short kind = category == _forge_category_decals ? _forge_spawn_decal : _forge_spawn_effect;
	struct forge_spawn *spawn;

	if (item_index < 0 || item_index >= forge_effect_category_count(category))
		return;
	entry = category == _forge_category_decals ? &forge_effect_palette.decals[item_index] :
		&forge_effect_palette.effects[item_index];
	forge_spawn_choose(kind);
	spawn = forge_spawn_of_id(forge_globals.held_marker_id);
	if (!spawn || spawn->kind != kind)
		return;
	forge_effect_set(spawn, entry->tag_index, (unsigned char)(kind == _forge_spawn_decal ? FORGE_DECAL_DEFAULT_SIZE : 0));
	{
		char name[FORGE_NAME_SIZE];

		forge_effect_name(spawn, name, sizeof(name));
		forge_notice(("Placed %s"), name);
	}
	forge_effect_log("placed", spawn);
}

/* (the console's forge effect|decal <name>) the first of the map's tags
with name in its readable name or its path, placed, held */
static boolean forge_effect_choose_named(
	short category,
	char const *name)
{
	short count = forge_effect_category_count(category);
	short index;

	for (index = 0; index < count; index++)
	{
		struct forge_effect_entry const *entry = category == _forge_category_decals ? &forge_effect_palette.decals[index] :
			&forge_effect_palette.effects[index];
		char const *path = tag_get_name(entry->tag_index);

		if (strstr(entry->name, name) || (path && strstr(path, name)))
		{
			forge_effect_choose(category, index);
			return forge_globals.held_marker_id != 0;
		}
	}
	platform_log("forge: no %s named %s on this map", category == _forge_category_decals ? "decal" : "effect", name);
	return FALSE;
}

/* a version 16 record (checked by the validator) as a marker: its tag
found by path on this map (none: skipped); TRUE if placed */
static boolean forge_effect_from_record(
	struct forge_file_layout const *layout,
	struct forge_file_record const *record)
{
	struct forge_transform home;
	struct forge_spawn *spawn;
	float angles[3];
	long tag_index;
	boolean decal = record->kind == _forge_record_decal;

	if (record->path_index >= layout->path_count)
		return FALSE;
	tag_index = tag_loaded(decal ? DECAL_GROUP_TAG : EFFECT_DEFINITION_TAG, layout->paths[record->path_index].path);
	if (tag_index == NONE || tag_get_group_tag(tag_index) != (decal ? DECAL_GROUP_TAG : EFFECT_DEFINITION_TAG))
	{
		/* (one of the campaign maps', maybe: looked for again once they are
		brought in) */
		if (forge_ai_tags_pending())
		{
			forge_effect_records_waiting = TRUE;
			return FALSE;
		}
		platform_log("forge: the layout's %s is not on this map: skipped", layout->paths[record->path_index].path);
		return FALSE;
	}
	home.position.x = record->position[0];
	home.position.y = record->position[1];
	home.position.z = record->position[2];
	angles[0] = record->angles[0];
	angles[1] = angles[2] = 0.f;
	forge_facing_from_angles(angles, &home.forward, &home.up);
	spawn = forge_spawn_add(0, decal ? _forge_spawn_decal : _forge_spawn_effect, NONE, 0, 0, &home);
	if (!spawn)
		return FALSE;
	forge_effect_set(spawn, tag_index, record->params[0]);
	return TRUE;
}

/* (forge_variant_apply, the layout placed: the campaign maps' tags brought
in by now) the layout's effects and decals that waited for them, the ones
found at the map's load left as they are */
static void forge_effects_from_layout_brought_in(
	struct forge_file_layout const *layout)
{
	short index;
	short placed = 0;

	if (!forge_effect_records_waiting)
		return;
	forge_effect_records_waiting = FALSE;
	if (forge_file_layout_version(layout) < FORGE_FILE_EFFECTS_VERSION)
		return;
	for (index = 0; index < layout->record_count && index < FORGE_FILE_MAXIMUM_RECORDS; index++)
	{
		struct forge_file_record const *record = &layout->records[index];
		long tag_index;

		if ((record->kind != _forge_record_effect && record->kind != _forge_record_decal) ||
			record->path_index >= layout->path_count)
		{
			continue;
		}
		tag_index = tag_loaded(record->kind == _forge_record_decal ? DECAL_GROUP_TAG : EFFECT_DEFINITION_TAG,
			layout->paths[record->path_index].path);
		/* (the map's own: placed as it loaded) */
		if (tag_index != NONE && !forge_ai_tags_source_map(tag_index))
			continue;
		if (forge_effect_from_record(layout, record))
			placed++;
	}
	if (placed)
		platform_log("forge: %d of the layout's effects and decals from the campaign maps placed", placed);
}

/* a marker as its version 16 record (its place and yaw the caller's); FALSE
when its path could not be added */
static boolean forge_effect_to_record(
	struct forge_spawn const *spawn,
	struct forge_file_layout *layout,
	struct forge_file_record *record)
{
	boolean decal = spawn->kind == _forge_spawn_decal;
	unsigned int group_tag = decal ? FORGE_FILE_DECAL_GROUP : FORGE_FILE_EFFECT_GROUP;
	char const *name = spawn->effect_tag_index != NONE ? tag_get_name(spawn->effect_tag_index) : NULL;
	short path;

	if (!name || !forge_file_path_valid(name, (int)strlen(name)))
		return FALSE;
	for (path = 0; path < layout->path_count; path++)
	{
		if (layout->paths[path].group_tag == group_tag && !strcmp(layout->paths[path].path, name))
			break;
	}
	if (path == layout->path_count)
	{
		if (layout->path_count >= FORGE_FILE_MAXIMUM_PATHS)
			return FALSE;
		layout->paths[path].group_tag = group_tag;
		snprintf(layout->paths[path].path, sizeof(layout->paths[path].path), "%s", name);
		layout->path_count++;
	}
	record->kind = decal ? _forge_record_decal : _forge_record_effect;
	record->path_index = (unsigned short)path;
	record->params[0] = decal ? (unsigned char)PIN(spawn->effect_value, 1, FORGE_FILE_MAXIMUM_DECAL_SIZE) :
		(unsigned char)MIN(spawn->effect_value, FORGE_FILE_MAXIMUM_EFFECT_REPEAT);
	return TRUE;
}

/* ---- the properties: REPEAT (an effect's) or SIZE (a decal's), then YAW */

static short forge_effect_property_count(
	void)
{
	return 2;
}

/* a row of the properties, mapped to the marker's: its first row (team's)
REPEAT or SIZE, its second (respawn's) YAW */
static void forge_effect_property_apply(
	struct forge_spawn *spawn,
	short property,
	short step,
	boolean held)
{
	unsigned char const *choices = spawn->kind == _forge_spawn_decal ? forge_decal_sizes : forge_effect_repeats;
	short count = (short)(spawn->kind == _forge_spawn_decal ? NUMBEROF(forge_decal_sizes) : NUMBEROF(forge_effect_repeats));
	short index;

	if (property == _forge_property_team)
	{
		for (index = 0; index < count - 1; index++)
		{
			if (choices[index] >= spawn->effect_value)
				break;
		}
		spawn->effect_value = choices[(index + step + count) % count];
		/* (shown again with its new setting) */
		forge_effect_release(spawn);
	}
	else if (property == _forge_property_respawn || property == _forge_property_yaw ||
		property == _forge_property_reset_rotation)
	{
		forge_spawn_yaw_step(spawn, property == _forge_property_reset_rotation ? property : _forge_property_yaw, step, held);
		forge_effect_release(spawn);
	}
	forge_effect_log("properties of", spawn);
}

static void forge_effect_property_text(
	struct forge_properties const *properties,
	short row,
	char *label,
	int label_size,
	char *value,
	int value_size)
{
	boolean decal = properties->spawn_kind == _forge_spawn_decal;

	if (row == 0 && decal)
	{
		snprintf(label, (size_t)label_size, "%s", "SIZE");
		snprintf(value, (size_t)value_size, "%d%%", properties->effect_value * 10);
	}
	else if (row == 0)
	{
		snprintf(label, (size_t)label_size, "%s", "REPEAT");
		if (properties->effect_value)
			snprintf(value, (size_t)value_size, "%d S", properties->effect_value);
		else
			snprintf(value, (size_t)value_size, "%s", "LOOP");
	}
	else
	{
		snprintf(label, (size_t)label_size, "%s", "YAW");
		snprintf(value, (size_t)value_size, "%d", properties->yaw);
	}
}

/* ---- what they show */

/* a decal laid on the ground under a marker: the decals it laid found (the
decal datums that were not there before), at most FORGE_DECAL_MOST_PIECES */
static void forge_decal_lay(
	struct forge_spawn *spawn)
{
	static long before[FORGE_DECALS_MOST_SCANNED];
	short before_count = 0;
	struct data_iterator iterator;
	real_point3d origin = spawn->home.position;
	real_vector3d velocity;

	spawn->decal_piece_count = 0;
	if (!global_decal_data || !decals_enabled || spawn->effect_tag_index == NONE)
		return;
	data_iterator_new(&iterator, global_decal_data);
	while (data_iterator_next(&iterator) && before_count < FORGE_DECALS_MOST_SCANNED)
		before[before_count++] = iterator.datum_index;
	origin.z += 0.5f;
	velocity.i = 0.f;
	velocity.j = 0.f;
	velocity.k = -(FORGE_DECAL_REACH + 0.5f);
	decal_new(spawn->effect_tag_index, &origin, &velocity, spawn->effect_value / 10.f, TRUE, NONE, NULL);
	data_iterator_new(&iterator, global_decal_data);
	while (data_iterator_next(&iterator) && spawn->decal_piece_count < FORGE_DECAL_MOST_PIECES)
	{
		short index;

		for (index = 0; index < before_count && before[index] != iterator.datum_index; index++)
			;
		if (index == before_count)
			spawn->decal_pieces[spawn->decal_piece_count++] = iterator.datum_index;
	}
}

/* an effect started at a marker, harmless, facing where it faces */
static void forge_effect_start(
	struct forge_spawn *spawn)
{
	real_point3d point = spawn->home.position;
	real_vector3d forward = spawn->home.forward;
	boolean harmless = effects_port_harmless;

	if (spawn->effect_index != NONE && effect_data && datum_try_and_get(effect_data, spawn->effect_index))
		effect_delete(spawn->effect_index);
	effects_port_harmless = TRUE;
	spawn->effect_index = effect_new_unattached_from_markers(spawn->effect_tag_index, NONE, global_zero_vector3d, 1, NULL,
		&point, &forward, 1.f, 1.f, NULL, NULL, TRUE);
	effects_port_harmless = harmless;
	if (!spawn->effect_started)
		platform_log("forge: the effect #%ld started (%s, tick %ld)", spawn->id, spawn->effect_index != NONE ? "made" : "none free",
			(long)game_time_get());
	spawn->effect_started = TRUE;
	spawn->effect_start_tick = game_time_get();
}

/* ---------- public code */

/* (game.c's game_tick, before the effects update) each effect of a Forge
game or a layout played started again when it is due - as soon as it ends
(LOOP) or every REPEAT seconds -, each decal laid once a round; what is held
shows nothing until it is let go. A tick's, from the layout alone */
void forge_effects_tick(
	void)
{
	short index;

	if (!forge_globals.game_active || forge_globals.round_resetting)
		return;
	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
	{
		struct forge_spawn *spawn = &forge_globals.spawns[index];

		if (!forge_spawn_is_effect(spawn) || spawn->effect_tag_index == NONE)
			continue;
		/* (a new round: the map's effects and decals went with it) */
		if (spawn->effect_round != forge_globals.round_count)
		{
			spawn->effect_index = NONE;
			spawn->decal_piece_count = 0;
			spawn->effect_started = FALSE;
			spawn->effect_round = forge_globals.round_count;
		}
		if (spawn->id == forge_globals.held_marker_id)
		{
			if (spawn->effect_started)
				forge_effect_release(spawn);
			continue;
		}
		if (spawn->kind == _forge_spawn_decal)
		{
			if (!spawn->effect_started)
			{
				forge_decal_lay(spawn);
				platform_log("forge: the decal #%ld laid (%d pieces)", spawn->id, spawn->decal_piece_count);
				spawn->effect_started = TRUE;
			}
			continue;
		}
		if (!spawn->effect_started ||
			(spawn->effect_value ? game_time_get() - spawn->effect_start_tick >= (long)spawn->effect_value * TICKS_PER_SECOND :
				!(effect_data && datum_try_and_get(effect_data, spawn->effect_index))))
		{
			forge_effect_start(spawn);
		}
	}
}
