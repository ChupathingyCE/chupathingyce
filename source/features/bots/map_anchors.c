/*
MAP_ANCHORS.C

port: a map's tactical anchors (map_anchors.h). Everything here is read from
the loaded scenario's tag blocks, whose element counts are checked before each
read, and placed with the walkable-surface projection the bots route with. An
anchor that does not project (a flag in the air, an item on a ledge too narrow
to stand on) is kept, with no polygon, so the semantic layer can see it.
Host-local and derived: nothing here is sent to another machine.
*/

/* ---------- headers */

#include "cseries.h"
#include "game.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_files.h"
#include "cache/cache_files.h"
#include "navigation_world.h"
#include "map_anchors.h"
#include <stdio.h>
#include <string.h>

/* port/linux/src/platform.h's and port_config.c's */
void platform_log(char const *format, ...);
char const *config_string(char const *name);

#ifdef HALO_FEATURE_BOTS

/* ---------- constants */

enum
{
	MAP_ANCHORS_MAXIMUM = 512,
	MAP_ANCHORS_PLACED_PER_TICK = 16,
};

/* (an airborne flag or an item resting on a crate still marks the floor below) */
#define MAP_ANCHORS_GROUND_DEPTH 3.f

enum map_anchor_kind
{
	_map_anchor_starting_location,
	_map_anchor_netgame_flag,
	_map_anchor_netgame_equipment,
};

/* ---------- structures */

struct map_anchor
{
	short kind;
	short type; /* netgame flag type; NONE otherwise */
	short team_index;
	short spawn_time; /* equipment, in seconds; 0 is the game's default */
	short game_types[4];
	real_point3d position;
	real facing;
	long tag_index; /* equipment's item collection */
	uint32_t polygon;
	uint32_t component;
	struct sn_point ground;
};

static struct
{
	struct map_anchor anchors[MAP_ANCHORS_MAXIMUM];
	short count;
	short placed;
	boolean collected;
	boolean reported;
	uint64_t resource_revision;
} map_anchors_globals;

/* ---------- private prototypes */

static void map_anchors_collect(void);
static void map_anchors_add(struct map_anchor const *anchor);
static char const *map_anchor_kind_name(struct map_anchor const *anchor);
static void map_anchors_report(void);
static void map_anchors_write_json_string(FILE *file, char const *text);

/* ---------- public code */

void map_anchors_reset(
	void)
{
	csmemset(&map_anchors_globals, 0, sizeof(map_anchors_globals));
}

void map_anchors_update(
	void)
{
	struct sn_resource const *resource = navigation_world_resource();
	short budget;

	if (!resource)
		return;
	if (!map_anchors_globals.collected || map_anchors_globals.resource_revision != resource->revision)
	{
		map_anchors_reset();
		map_anchors_collect();
		map_anchors_globals.collected = TRUE;
		map_anchors_globals.resource_revision = resource->revision;
	}
	for (budget = 0; budget < MAP_ANCHORS_PLACED_PER_TICK &&
		map_anchors_globals.placed < map_anchors_globals.count; budget++)
	{
		struct map_anchor *anchor = &map_anchors_globals.anchors[map_anchors_globals.placed++];
		struct sn_point point;

		point.x = anchor->position.x;
		point.y = anchor->position.y;
		point.z = anchor->position.z;
		if (sn_project_below(resource, point, MAP_ANCHORS_GROUND_DEPTH,
			&anchor->polygon, &anchor->ground) == SN_FOUND)
		{
			anchor->component = sn_component(resource, anchor->polygon);
		}
		else
		{
			anchor->polygon = SN_NONE;
			anchor->component = SN_NONE;
		}
	}
	if (!map_anchors_globals.reported && map_anchors_globals.placed == map_anchors_globals.count)
	{
		map_anchors_report();
		map_anchors_globals.reported = TRUE;
	}
}

boolean map_anchors_team_base(
	long team_index,
	real_point3d *position)
{
	struct scenario *scenario = global_scenario_get();
	long index;

	if (!scenario || !position)
		return FALSE;
	for (index = 0; index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag const *flag = TAG_BLOCK_GET_ELEMENT(
			&scenario->netgame_flags, index, struct scenario_netgame_flag);

		if (flag->type == _netgame_flag_ctf_flag && flag->team_index == team_index &&
			valid_real_point3d(&flag->position))
		{
			*position = flag->position;
			return TRUE;
		}
	}
	return FALSE;
}

/* ---------- private code */

static void map_anchors_collect(
	void)
{
	struct scenario *scenario = global_scenario_get();
	struct map_anchor anchor;
	long index;

	if (!scenario)
		return;
	for (index = 0; index < scenario->players.count; index++)
	{
		struct player_starting_location const *location = TAG_BLOCK_GET_ELEMENT(
			&scenario->players, index, struct player_starting_location);

		csmemset(&anchor, 0, sizeof(anchor));
		anchor.kind = _map_anchor_starting_location;
		anchor.type = NONE;
		anchor.team_index = location->team_index;
		csmemcpy(anchor.game_types, location->game_types, sizeof(anchor.game_types));
		anchor.position = location->position;
		anchor.facing = location->facing;
		anchor.tag_index = NONE;
		map_anchors_add(&anchor);
	}
	for (index = 0; index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag const *flag = TAG_BLOCK_GET_ELEMENT(
			&scenario->netgame_flags, index, struct scenario_netgame_flag);

		csmemset(&anchor, 0, sizeof(anchor));
		anchor.kind = _map_anchor_netgame_flag;
		anchor.type = flag->type;
		anchor.team_index = flag->team_index;
		anchor.position = flag->position;
		anchor.facing = flag->facing;
		anchor.tag_index = NONE;
		map_anchors_add(&anchor);
	}
	for (index = 0; index < scenario->netgame_equipment.count; index++)
	{
		struct scenario_netgame_equipment const *equipment = TAG_BLOCK_GET_ELEMENT(
			&scenario->netgame_equipment, index, struct scenario_netgame_equipment);

		csmemset(&anchor, 0, sizeof(anchor));
		anchor.kind = _map_anchor_netgame_equipment;
		anchor.type = NONE;
		anchor.team_index = equipment->team_index;
		anchor.spawn_time = equipment->spawn_time;
		csmemcpy(anchor.game_types, equipment->game_type, sizeof(anchor.game_types));
		anchor.position = equipment->position;
		anchor.facing = equipment->facing;
		anchor.tag_index = equipment->item_collection.index;
		map_anchors_add(&anchor);
	}
}

static void map_anchors_add(
	struct map_anchor const *anchor)
{
	if (!valid_real_point3d(&anchor->position))
		return;
	if (map_anchors_globals.count >= MAP_ANCHORS_MAXIMUM)
	{
		platform_log("anchors: more than %d anchors; the rest are not listed", MAP_ANCHORS_MAXIMUM);
		return;
	}
	map_anchors_globals.anchors[map_anchors_globals.count] = *anchor;
	map_anchors_globals.anchors[map_anchors_globals.count].polygon = SN_NONE;
	map_anchors_globals.anchors[map_anchors_globals.count].component = SN_NONE;
	map_anchors_globals.count++;
}

static char const *map_anchor_kind_name(
	struct map_anchor const *anchor)
{
	static char const *const flag_names[] = { "ctf_flag", "ctf_vehicle", "oddball_ball_spawn",
		"race_track", "race_vehicle", "vegas_bank", "teleporter_source", "teleporter_target", "hill" };

	switch (anchor->kind)
	{
	case _map_anchor_starting_location: return "starting_location";
	case _map_anchor_netgame_equipment: return "equipment";
	default:
		return anchor->type >= 0 && anchor->type < (short)NUMBEROF(flag_names) ?
			flag_names[anchor->type] : "netgame_flag";
	}
}

/* (a tag name, or empty for none; the tag table bounds the index) */
static char const *map_anchor_item_name(
	struct map_anchor const *anchor)
{
	char const *name;

	if (anchor->tag_index == NONE)
		return "";
	name = tag_get_name(anchor->tag_index);
	return name ? name : "";
}

static void map_anchors_report(
	void)
{
	char const *path = config_string("debug.map_anchors_dump");
	char temporary_path[528];
	FILE *file = NULL;
	size_t length;
	boolean failed;
	short index;
	short placed = 0;

	for (index = 0; index < map_anchors_globals.count; index++)
	{
		struct map_anchor const *anchor = &map_anchors_globals.anchors[index];

		if (anchor->polygon != SN_NONE)
			placed++;
		platform_log("anchors: %s type %d team %d at (%.2f %.2f %.2f) facing %.2f polygon %ld "
			"component %ld %s", map_anchor_kind_name(anchor), anchor->type, anchor->team_index,
			anchor->position.x, anchor->position.y, anchor->position.z, anchor->facing,
			anchor->polygon == SN_NONE ? -1L : (long)anchor->polygon,
			anchor->component == SN_NONE ? -1L : (long)anchor->component,
			map_anchor_item_name(anchor));
	}
	platform_log("anchors: %d anchors, %d on walkable polygons", map_anchors_globals.count, placed);

	if (!path || !path[0])
		return;
	length = strlen(path);
	if (length < 5 || length >= 512 || strcmp(path + length - 5, ".json"))
	{
		platform_log("anchors: dump filename must end in .json (never overwrite a map)");
		return;
	}
	snprintf(temporary_path, sizeof(temporary_path), "%s.partial", path);
	file = fopen(temporary_path, "w");
	if (!file)
	{
		platform_log("anchors: cannot open the dump");
		return;
	}
	fprintf(file, "{\n  \"schema_version\": \"0.1.0\",\n  \"source\": \"scenario tag blocks; "
		"walkable BSP polygons\",\n  \"map\": ");
	map_anchors_write_json_string(file, cache_file_loaded_map_name());
	fprintf(file, ",\n  \"coordinates\": \"halo_ce_world_units\",\n  \"anchors\": [\n");
	for (index = 0; index < map_anchors_globals.count; index++)
	{
		struct map_anchor const *anchor = &map_anchors_globals.anchors[index];

		fprintf(file, "    {\"kind\": \"%s\", \"team\": %d, \"position\": [%.4f, %.4f, %.4f], "
			"\"facing\": %.4f, \"game_types\": [%d, %d, %d, %d], ", map_anchor_kind_name(anchor),
			anchor->team_index, anchor->position.x, anchor->position.y, anchor->position.z,
			anchor->facing, anchor->game_types[0], anchor->game_types[1], anchor->game_types[2],
			anchor->game_types[3]);
		if (anchor->kind == _map_anchor_netgame_equipment)
		{
			fprintf(file, "\"spawn_time\": %d, \"item_collection\": ", anchor->spawn_time);
			map_anchors_write_json_string(file, map_anchor_item_name(anchor));
			fprintf(file, ", ");
		}
		if (anchor->polygon == SN_NONE)
			fprintf(file, "\"walkable\": null}");
		else
		{
			fprintf(file, "\"walkable\": {\"polygon\": %lu, \"component\": %lu, "
				"\"ground\": [%.4f, %.4f, %.4f]}}", (unsigned long)anchor->polygon,
				(unsigned long)anchor->component, anchor->ground.x, anchor->ground.y, anchor->ground.z);
		}
		fprintf(file, index + 1 < map_anchors_globals.count ? ",\n" : "\n");
	}
	fprintf(file, "  ]\n}\n");
	failed = ferror(file) != 0;
	failed = fclose(file) != 0 || failed;
	if (failed || rename(temporary_path, path))
		platform_log("anchors: dump publication failed");
	else
		platform_log("anchors: wrote %s", path);
}

static void map_anchors_write_json_string(
	FILE *file,
	char const *text)
{
	short index;

	fputc('"', file);
	for (index = 0; text && index < 256 && text[index]; index++)
	{
		unsigned char c = (unsigned char)text[index];

		if (c == '"' || c == '\\')
			fprintf(file, "\\%c", c);
		else if (c < 0x20 || c >= 0x7f)
			fprintf(file, "\\u%04x", c);
		else
			fputc(c, file);
	}
	fputc('"', file);
}

#endif /* HALO_FEATURE_BOTS */
