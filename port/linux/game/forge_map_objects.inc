/*
FORGE_MAP_OBJECTS.INC

The map's own objects in a Forge layout (included by forge.c, for its
statics): the scenario's placed objects (its scenery, vehicles, weapons,
machines and the rest) and a multiplayer map's item spawns, which the
editor moves and deletes like the layout's own. The map's file is never
changed: a layout lists the placements it takes away (forge_files.h,
version 11: each by its category and its index in the scenario's block),
and a game of the layout does not make them.

- Deleted: the placement is taken away. A new round, the layout saved and
  loaded again, or played, has it not.
- Moved, turned, or only grabbed: the placement is taken away, and the
  object it made becomes one of the layout's (forge_placed: the budget
  counts it, its properties can be set, the layout saves it where the
  editor let it go). Loaded or played, the map does not make it and the
  layout places it there.

The scenario's objects are all made by object_new_from_scenario
(objects.c): it asks forge_map_placement_skipped first, and tells
forge_map_placement_made which object it made. A multiplayer map's item
spawns (game_engine.c game_engine_update_item_spawn) ask
forge_map_item_spawn_skipped and tell forge_map_item_spawn_made. Every
test is one global read (forge_map_objects_active) outside a Forge game or
a layout played, so nothing of any other game changes. A layout without the
block (versions 1 to 7) takes nothing away.

Which object a placement made is kept for the editor only (a Forge game,
not a layout played), in a fixed table: an object the engine makes
otherwise (a vehicle it respawns on its pad, a flag, a ball) is no
placement's, and stays as before (the editor moves it for this game only).
A vehicle the editor takes is no longer the engine's to put back on its
pad (game_engine_vehicle_home_forget).
*/

/* ---------- constants */

/* the objects made from placements that the editor can take (a
multiplayer map has some 100 to 300) */
#define FORGE_MAP_MADE_MOST 1024

/* ---------- structures */

struct forge_map_made
{
	long object_index;
	unsigned short index;
	unsigned char category;
};

/* ---------- prototypes */

/* game_engine.c's (port): the map's vehicle no longer put back on its pad */
void game_engine_vehicle_home_forget(long object_index);

/* ---------- globals */

/* (objects.c, game_engine.c) a Forge game or a layout played is up: the
hooks below are asked */
int forge_map_objects_active = 0;

static struct
{
	/* what the session takes away (the layout's version 11 block, its
	entries ascending) */
	struct forge_file_canvas canvas;
	/* (the editor's) the objects the placements made */
	struct forge_map_made made[FORGE_MAP_MADE_MOST];
	short made_count;
	boolean made_full_logged;
	/* (the log's) the placements not made this map, a category each */
	long skipped[NUMBER_OF_FORGE_CANVAS_CATEGORIES];
} forge_map_objects;

/* each category's object type (the items' none: the netgame equipment) */
static short const forge_map_object_types[NUMBER_OF_FORGE_CANVAS_CATEGORIES] =
{
	_object_type_scenery, _object_type_biped, _object_type_vehicle, _object_type_weapon, _object_type_equipment,
	_object_type_machine, _object_type_control, _object_type_light_fixture, _object_type_sound_scenery, NONE,
};

static char const *const forge_map_category_log_names[NUMBER_OF_FORGE_CANVAS_CATEGORIES] =
{
	"scenery", "bipeds", "vehicles", "weapons", "equipment", "machines", "controls", "light fixtures", "sound scenery",
	"item spawns",
};

/* ---------- private code */

/* where (category, index) is among the entries, or where it would go:
TRUE when it is there */
static boolean forge_map_canvas_search(
	short category,
	long index,
	short *at)
{
	short low = 0;
	short high = (short)PIN(forge_map_objects.canvas.entry_count, 0, FORGE_FILE_MAXIMUM_CANVAS_ENTRIES);

	while (low < high)
	{
		short middle = (short)((low + high) / 2);
		struct forge_file_canvas_entry const *entry = &forge_map_objects.canvas.entries[middle];
		long key = (long)entry->category * (FORGE_FILE_CANVAS_MAXIMUM_INDEX + 1) + entry->index;
		long wanted = (long)category * (FORGE_FILE_CANVAS_MAXIMUM_INDEX + 1) + index;

		if (key == wanted)
		{
			*at = middle;
			return TRUE;
		}
		if (key < wanted)
			low = (short)(middle + 1);
		else
			high = middle;
	}
	*at = low;
	return FALSE;
}

/* whether the session takes a placement away: of a category taken away
whole unless kept, else when listed */
static boolean forge_map_canvas_removed(
	short category,
	long index)
{
	short at;
	boolean listed;

	if (category < 0 || category >= NUMBER_OF_FORGE_CANVAS_CATEGORIES || index < 0 ||
		index > FORGE_FILE_CANVAS_MAXIMUM_INDEX)
	{
		return FALSE;
	}
	listed = forge_map_canvas_search(category, index, &at);
	return ((forge_map_objects.canvas.categories >> category) & 1) ? !listed : listed;
}

/* a placement taken away (a kept one of a category taken away whole: its
entry out; another: an entry in, in order); FALSE when there is no room */
static boolean forge_map_canvas_take_away(
	short category,
	long index)
{
	struct forge_file_canvas *canvas = &forge_map_objects.canvas;
	short at;
	boolean listed;

	if (forge_map_canvas_removed(category, index))
		return TRUE;
	listed = forge_map_canvas_search(category, index, &at);
	if (listed)
	{
		csmemmove(&canvas->entries[at], &canvas->entries[at + 1], sizeof(canvas->entries[0]) * (size_t)(canvas->entry_count - at - 1));
		canvas->entry_count--;
		return TRUE;
	}
	if (canvas->entry_count >= FORGE_FILE_MAXIMUM_CANVAS_ENTRIES)
		return FALSE;
	csmemmove(&canvas->entries[at + 1], &canvas->entries[at], sizeof(canvas->entries[0]) * (size_t)(canvas->entry_count - at));
	canvas->entries[at].category = (unsigned char)category;
	canvas->entries[at].kept = 0;
	canvas->entries[at].index = (unsigned short)index;
	canvas->entry_count++;
	return TRUE;
}

/* the category and index of one of the scenario's object placements */
static boolean forge_map_placement_find(
	struct scenario_object_datum const *placement,
	short *category,
	long *index)
{
	struct scenario *scenario = global_scenario_index != NONE ? global_scenario_get() : NULL;
	short at;

	if (!scenario || !placement)
		return FALSE;
	for (at = 0; at < NUMBER_OF_FORGE_CANVAS_CATEGORIES; at++)
	{
		long element_size;
		struct tag_block *block;
		char const *first;
		long offset;

		if (forge_map_object_types[at] == NONE)
			continue;
		block = scenario_get_object_type_scenario_datums(scenario, forge_map_object_types[at], &element_size);
		if (!block || block->count <= 0 || element_size <= 0)
			continue;
		first = (char const *)tag_block_get_element_with_size(block, 0, element_size);
		if ((char const *)placement < first)
			continue;
		offset = (long)((char const *)placement - first);
		if (offset % element_size || offset / element_size >= block->count)
			continue;
		*category = at;
		*index = offset / element_size;
		return *index <= FORGE_FILE_CANVAS_MAXIMUM_INDEX;
	}
	return FALSE;
}

/* the editor's table: which placement made an object (NONE: none) */
static short forge_map_made_of_object(
	long object_index)
{
	short at;

	if (object_index == NONE)
		return NONE;
	for (at = 0; at < forge_map_objects.made_count; at++)
	{
		if (forge_map_objects.made[at].object_index == object_index)
			return forge_object_alive(object_index) ? at : NONE;
	}
	return NONE;
}

static void forge_map_made_add(
	short category,
	long index,
	long object_index)
{
	short at;
	short free_at = NONE;

	if (!forge_globals.game_active || forge_globals.playing || object_index == NONE)
		return;
	/* (a slot of an object gone is used again) */
	for (at = 0; at < forge_map_objects.made_count; at++)
	{
		if (forge_map_objects.made[at].object_index == object_index || !object_try_and_get(forge_map_objects.made[at].object_index))
		{
			free_at = at;
			break;
		}
	}
	if (free_at == NONE)
	{
		if (forge_map_objects.made_count >= FORGE_MAP_MADE_MOST)
		{
			if (!forge_map_objects.made_full_logged)
				platform_log("forge: more than %d of the map's objects: the rest are moved for this game only", FORGE_MAP_MADE_MOST);
			forge_map_objects.made_full_logged = TRUE;
			return;
		}
		free_at = forge_map_objects.made_count++;
	}
	forge_map_objects.made[free_at].object_index = object_index;
	forge_map_objects.made[free_at].category = (unsigned char)category;
	forge_map_objects.made[free_at].index = (unsigned short)index;
}

/* (a map load, a new round: every object goes) the table emptied */
static void forge_map_made_clear(
	void)
{
	forge_map_objects.made_count = 0;
	forge_map_objects.made_full_logged = FALSE;
}

/* (forge_map_loaded) what the session takes away: the layout's, when one
is played or edited, else nothing */
static void forge_map_objects_start(
	struct forge_file_layout const *layout)
{
	csmemset(&forge_map_objects.canvas, 0, sizeof(forge_map_objects.canvas));
	csmemset(forge_map_objects.skipped, 0, sizeof(forge_map_objects.skipped));
	forge_map_made_clear();
	if (layout && (layout->flags & FORGE_FILE_FLAG_CANVAS))
	{
		forge_map_objects.canvas = layout->canvas;
		forge_map_objects.canvas.entry_count = (short)PIN(forge_map_objects.canvas.entry_count, 0,
			FORGE_FILE_MAXIMUM_CANVAS_ENTRIES);
		platform_log("forge: the layout takes %d of the map's objects away", forge_map_objects.canvas.entry_count);
	}
	forge_map_objects_active = forge_globals.game_active;
}

/* (forge_variant_capture) what the session takes away, in the layout */
static void forge_map_objects_capture(
	struct forge_file_layout *layout)
{
	if (!forge_map_objects.canvas.entry_count && !forge_map_objects.canvas.categories)
		return;
	layout->canvas = forge_map_objects.canvas;
	layout->flags |= FORGE_FILE_FLAG_CANVAS;
}

/* an item spawn's time between items (game_engine.c's rule), seconds */
static unsigned short forge_map_item_spawn_seconds(
	long equipment_index)
{
	struct scenario *scenario = global_scenario_get();
	struct scenario_netgame_equipment const *equipment;
	short seconds;

	if (equipment_index < 0 || equipment_index >= scenario->netgame_equipment.count)
		return FORGE_DEFAULT_RESPAWN_SECONDS;
	equipment = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_equipment, equipment_index, struct scenario_netgame_equipment);
	seconds = equipment->spawn_time;
	if (seconds == 0 && equipment->item_collection.index != NONE)
		seconds = ((struct item_collection_definition *)tag_get(ITEM_COLLECTION_DEFINITION_TAG,
			equipment->item_collection.index))->spawn_time;
	return (unsigned short)(seconds > 0 ? MIN(seconds, FORGE_FILE_MAXIMUM_RESPAWN_SECONDS) : FORGE_DEFAULT_RESPAWN_SECONDS);
}

/* (the editor's delete, its grab, its turn) one of the map's own objects
taken out of the map's: its placement taken away, and kept one of the
layout's (keep) at once, the budget's then. TRUE when it is no map
object's (nothing to do) or it was taken; FALSE, said on the screen, when it
could not be (the budget full, too many of the map's taken) */
static boolean forge_map_object_take(
	long object_index,
	boolean keep)
{
	short made = forge_map_made_of_object(object_index);
	short category;
	long index;
	char name[FORGE_NAME_SIZE];
	struct object_datum *object;

	if (made == NONE || forge_placed_of_object(object_index))
		return TRUE;
	object = object_get(object_index);
	category = forge_map_objects.made[made].category;
	index = forge_map_objects.made[made].index;
	forge_object_name(object_index, name, sizeof(name));
	if (keep)
	{
		forge_placed_update();
		if (forge_objects_counted() >= FORGE_MAXIMUM_OBJECTS || forge_globals.placed_count >= FORGE_MAXIMUM_OBJECTS)
		{
			forge_notice("Budget full: %d objects. Delete one to move this one.", FORGE_MAXIMUM_OBJECTS);
			return FALSE;
		}
	}
	if (!forge_map_canvas_take_away(category, index))
	{
		forge_notice("Can't change %s: the layout already changes %d of the map's objects", name,
			FORGE_FILE_MAXIMUM_CANVAS_ENTRIES);
		return FALSE;
	}
	forge_map_objects.made[made].object_index = NONE;
	if (object->object.type == _object_type_vehicle)
		game_engine_vehicle_home_forget(object_index);
	if (keep)
	{
		struct forge_placed *placed;

		forge_placed_add(object_index, object->definition_index);
		placed = forge_placed_of_object(object_index);
		if (placed && category == _forge_canvas_items)
			placed->respawn_seconds = forge_map_item_spawn_seconds(index);
	}
	platform_log("forge: the map's %s (%s %ld) %s", name, forge_map_category_log_names[category], index,
		keep ? "is the layout's now" : "taken away");
	return TRUE;
}

/* ---------- public code (the engine's hooks) */

/* (objects.c object_new_from_scenario) whether the session takes this
placement away: it is not made */
boolean forge_map_placement_skipped(
	struct scenario_object_datum const *placement)
{
	short category;
	long index;

	if (!forge_map_objects_active || (!forge_map_objects.canvas.entry_count && !forge_map_objects.canvas.categories))
		return FALSE;
	if (!forge_map_placement_find(placement, &category, &index) || !forge_map_canvas_removed(category, index))
		return FALSE;
	forge_map_objects.skipped[category]++;
	return TRUE;
}

/* (objects.c object_new_from_scenario) the object a placement made */
void forge_map_placement_made(
	struct scenario_object_datum const *placement,
	long object_index)
{
	short category;
	long index;

	if (!forge_map_objects_active || !forge_globals.game_active || forge_globals.playing)
		return;
	if (forge_map_placement_find(placement, &category, &index))
		forge_map_made_add(category, index, object_index);
}

/* (game_engine.c game_engine_update_item_spawn) whether the session takes
this item spawn away: it makes no item */
boolean forge_map_item_spawn_skipped(
	long equipment_index)
{
	if (!forge_map_objects_active || !forge_map_canvas_removed(_forge_canvas_items, equipment_index))
		return FALSE;
	forge_map_objects.skipped[_forge_canvas_items]++;
	return TRUE;
}

/* (game_engine.c game_engine_update_item_spawn) the item a spawn made */
void forge_map_item_spawn_made(
	long equipment_index,
	long object_index)
{
	if (!forge_map_objects_active || equipment_index < 0 || equipment_index > FORGE_FILE_CANVAS_MAXIMUM_INDEX)
		return;
	forge_map_made_add(_forge_canvas_items, equipment_index, object_index);
}

/* (the console's "forge map [category]") the map's own objects the
editor can take (of a category: its log name's first word), and what the
session takes away, in the log */
static void forge_map_objects_log(
	char const *category_word)
{
	short at;
	short listed = 0;
	char line[256];
	int length = 0;

	for (at = 0; at < forge_map_objects.made_count; at++)
	{
		struct forge_map_made const *made = &forge_map_objects.made[at];
		struct object_datum *object;
		char name[FORGE_NAME_SIZE];

		if (!forge_object_alive(made->object_index))
			continue;
		if (category_word && category_word[0] &&
			strncmp(forge_map_category_log_names[made->category], category_word, strlen(category_word)))
		{
			continue;
		}
		object = object_get(made->object_index);
		forge_object_name(made->object_index, name, sizeof(name));
		platform_log("forge: map object %s %d: %s at %.2f %.2f %.2f", forge_map_category_log_names[made->category],
			(int)made->index, name, object->object.position.x, object->object.position.y, object->object.position.z);
		listed++;
	}
	line[0] = 0;
	for (at = 0; at < forge_map_objects.canvas.entry_count && length < (int)sizeof(line) - 40; at++)
	{
		struct forge_file_canvas_entry const *entry = &forge_map_objects.canvas.entries[at];

		length += snprintf(line + length, sizeof(line) - (size_t)length, "%s%s %d%s", at ? ", " : "",
			forge_map_category_log_names[PIN(entry->category, 0, NUMBER_OF_FORGE_CANVAS_CATEGORIES - 1)], (int)entry->index,
			entry->kept ? " kept" : "");
	}
	platform_log("forge: map objects listed: %d; taken away: %d (%s)", listed, forge_map_objects.canvas.entry_count,
		line[0] ? line : "none");
	line[0] = 0;
	length = 0;
	for (at = 0; at < NUMBER_OF_FORGE_CANVAS_CATEGORIES; at++)
	{
		if (forge_map_objects.skipped[at] && length < (int)sizeof(line) - 40)
		{
			length += snprintf(line + length, sizeof(line) - (size_t)length, "%s%s %ld", length ? ", " : "",
				forge_map_category_log_names[at], forge_map_objects.skipped[at]);
		}
	}
	platform_log("forge: map objects not made this map: %s", line[0] ? line : "none");
}
