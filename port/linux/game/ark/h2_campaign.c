/*
H2_CAMPAIGN.C

port: a Halo 2 campaign level's structure BSPs and its objects' places
in them (h2_campaign.h).
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"
#include "h2_rules.h"
#include "h2_campaign.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

boolean h2_campaign_placement_bsps(
	struct scenario_object_datum const *placement,
	unsigned short *bsps)
{
	byte const *bytes = (byte const *)placement;

	if (!placement || bytes[H2_CAMPAIGN_PLACEMENT_MARK_OFFSET] != H2_CAMPAIGN_PLACEMENT_MARK || !h2_rules_level())
		return FALSE;
	memcpy(bsps, bytes + H2_CAMPAIGN_PLACEMENT_BSPS_OFFSET, sizeof(*bsps));

	return TRUE;
}

short h2_campaign_structure_bsp_index(
	char const *name)
{
	struct scenario *scenario = global_scenario_index != NONE ? global_scenario_get() : NULL;
	short index;

	if (!scenario || !name || !*name)
		return NONE;
	/* (a script's string may come quoted) */
	if (*name == '"')
		name++;
	for (index = 0; index < scenario->structure_bsp_references.count && index < MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO; index++)
	{
		struct scenario_structure_bsp_reference *reference = TAG_BLOCK_GET_ELEMENT(
			&scenario->structure_bsp_references, index, struct scenario_structure_bsp_reference);
		char const *tag_name = reference->structure_bsp.index != NONE ? tag_get_name(reference->structure_bsp.index) : NULL;
		char const *last;
		size_t length = strlen(name);

		if (!tag_name)
			continue;
		last = strrchr(tag_name, '\\');
		last = last ? last + 1 : tag_name;
		if (length && name[length - 1] == '"')
			length--;
		if (strlen(last) == length && !strncasecmp(last, name, length))
			return index;
	}

	return NONE;
}

boolean h2_campaign_switch_bsp_by_name(
	char const *name)
{
	short index = h2_campaign_structure_bsp_index(name);

	if (index == NONE)
	{
		error(_error_silent, "switch_bsp_by_name: this level has no structure bsp named '%s'", name ? name : "");
		return FALSE;
	}
	scenario_switch_structure_bsp(index);

	return TRUE;
}

/* ---------- a campaign level's scripts' functions (h2_scripts.c calls
them by name: h2_campaign_natives) */

#include "h2_scripts.h"
#include "h2_hud.h"
#include "h2_cache.h"
#include "game/game.h"
#include "game/players.h"
#include "objects/objects.h"
#include "hs/hs.h"
#include "memory/data.h"

#include <stdio.h>
#include <stdlib.h>

#define H2_ALIGN __attribute__((force_align_arg_pointer))

void platform_log(char const *format, ...);
long *hs_runtime_arguments_evaluate_all(long thread_index, short maximum, short *count, boolean initialize);
void hs_return(long thread_index, long value);
void hud_activate_nav_point_with_object(short nav_index, long player_index, long object_index, float vertical_offset);
void hud_activate_nav_point_with_flag(short nav_index, long player_index, short flag_index, float vertical_offset);
void hud_deactivate_nav_point_with_object(long player_index, long object_index);
void hud_deactivate_nav_point_with_flag(long player_index, short flag_index);
void hud_unit_activate_nav_point_with_object(short nav_index, long unit_index, long object_index, float vertical_offset);
void hud_unit_activate_nav_point_with_flag(short nav_index, long unit_index, short flag_index, float vertical_offset);
void hud_unit_deactivate_nav_point_with_object(long unit_index, long object_index);
void hud_unit_deactivate_nav_point_with_flag(long unit_index, short flag_index);
void hud_port_activate_nav_point_with_position(short nav_index, long position_index, float vertical_offset);
void hud_port_deactivate_nav_point_with_position(long position_index);

enum
{
	MAXIMUM_NAV_POINT_POSITIONS = 64,
};

/* the points scripts' nav points stand at (breadcrumbs_activate_team_nav_
point_position), by name: a nav point's reference is its index here. Kept
for the level (not game state: a name always stands for the same point,
so a revert finds its nav points' points here) */
static struct
{
	long scenario_index;
	short count;
	struct { char name[32]; real_point3d point; } points[MAXIMUM_NAV_POINT_POSITIONS];
	short logged;
} h2_nav;

union h2_campaign_value
{
	long l;
	short s;
	boolean b;
	real r;
};

static short value_short(long a) { union h2_campaign_value v; v.l = a; return v.s; }
static real value_real(long a) { union h2_campaign_value v; v.l = a; return v.r; }
static boolean value_boolean(long a) { union h2_campaign_value v; v.l = a; return v.b; }

/* (render_epoch.c: changes with every map and game state replaced) */
extern unsigned long halo_map_generation;

static void nav_points_for_level(void)
{
	static unsigned long generation;

	/* (the map's generation too: consecutive maps' scenarios can have the same tag index) */
	if (h2_nav.scenario_index != global_scenario_index || generation != halo_map_generation)
	{
		memset(&h2_nav, 0, sizeof(h2_nav));
		h2_nav.scenario_index = global_scenario_index;
		generation = halo_map_generation;
	}
}

boolean h2_campaign_nav_point_position(long index, real_point3d *position)
{
	nav_points_for_level();
	if (index < 0 || index >= h2_nav.count)
		return FALSE;
	*position = h2_nav.points[index].point;
	return TRUE;
}

/* a navpoint argument (Halo 2's: the HUD globals' waypoint arrow's index,
or its name's string id) as the level's HUD file's waypoint arrow */
static short navpoint_index(long value, char const *function)
{
	short count = h2_hud_waypoint_count(), index = NONE;
	short s = value_short(value);

	if (s >= 0 && s < count && (value & 0xFFFF0000UL) == 0)
		index = s;
	else
	{
		struct h2_cache *cache = h2_scripts_cache();
		char name[64];

		if (cache && h2_cache_string(cache, (unsigned long)value, name, sizeof(name)))
			index = h2_hud_waypoint_index(name);
	}
	if (h2_nav.logged++ < 16 || getenv("HALO_H2_NAV_LOG"))
		platform_log("h2 nav point: %s navpoint %08lx -> waypoint arrow %d (of %d)", function, (unsigned long)value, (int)index, (int)count);
	return index != NONE ? index : 0;
}

/* the point named name's index, added if new (NONE: no room) */
static long nav_point_position_index(char const *name, real_point3d const *point)
{
	short i;

	nav_points_for_level();
	for (i = 0; i < h2_nav.count; i++)
	{
		if (!strncmp(h2_nav.points[i].name, name, sizeof(h2_nav.points[i].name) - 1))
		{
			if (point)
				h2_nav.points[i].point = *point;
			return i;
		}
	}
	if (!point || h2_nav.count >= MAXIMUM_NAV_POINT_POSITIONS)
		return NONE;
	snprintf(h2_nav.points[h2_nav.count].name, sizeof(h2_nav.points[0].name), "%s", name);
	h2_nav.points[h2_nav.count].point = *point;
	return h2_nav.count++;
}

/* whether the scripts' nav points are shown: not, as the Xbox's Halo 2 never
shows one in its campaign (its comparison, every level: no waypoint, no
marker); HALO_H2_NAV_POINTS=1 shows them (tests) - logged once a level */
static boolean nav_points_shown(char const *function)
{
	static long logged_scenario = NONE;
	char const *setting = getenv("HALO_H2_NAV_POINTS");

	if (setting && !strcmp(setting, "1"))
		return TRUE;
	if (logged_scenario != global_scenario_index)
	{
		logged_scenario = global_scenario_index;
		platform_log("h2 campaign: %s: nav points not shown, as the Xbox's (HALO_H2_NAV_POINTS=1 shows them)", function);
	}
	return FALSE;
}

/* (activate_team_nav_point_object, breadcrumbs_activate_team_nav_point_object
<navpoint> <team> <object> <real>; _flag the same with a cutscene flag): the
local players' (a campaign's players are its one team: every local
player's, whatever team the script names) */
static void activate_team_nav_point(long thread_index, boolean initialize, boolean flag, char const *name)
{
	short count;
	long *a = hs_runtime_arguments_evaluate_all(thread_index, 4, &count, initialize);

	if (!a)
		return;
	if (count == 4 && nav_points_shown(name))
	{
		short nav = navpoint_index(a[0], name);

		struct data_iterator iterator;
		struct player_datum *player;

		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (player->local_player_index == NONE)
				continue;
			if (flag)
			{
				if (value_short(a[2]) >= 0)
					hud_activate_nav_point_with_flag(nav, iterator.datum_index, value_short(a[2]), value_real(a[3]));
			}
			else if (a[2] != NONE)
				hud_activate_nav_point_with_object(nav, iterator.datum_index, a[2], value_real(a[3]));
		}
	}
	hs_return(thread_index, 0);
}

H2_ALIGN static void activate_team_nav_point_object_evaluate(short function_index, long thread_index, boolean initialize)
{
	activate_team_nav_point(thread_index, initialize, FALSE, "activate_team_nav_point_object");
	(void)function_index;
}

H2_ALIGN static void activate_team_nav_point_flag_evaluate(short function_index, long thread_index, boolean initialize)
{
	activate_team_nav_point(thread_index, initialize, TRUE, "activate_team_nav_point_flag");
	(void)function_index;
}

/* (deactivate_team_nav_point_object / _flag, breadcrumbs_deactivate_... <team>
<object or flag>) */
static void deactivate_team_nav_point(long thread_index, boolean initialize, boolean flag)
{
	short count;
	long *a = hs_runtime_arguments_evaluate_all(thread_index, 2, &count, initialize);

	if (!a)
		return;
	if (count == 2)
	{
		struct data_iterator iterator;
		struct player_datum *player;

		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (player->local_player_index == NONE)
				continue;
			if (flag)
				hud_deactivate_nav_point_with_flag(iterator.datum_index, value_short(a[1]));
			else if (a[1] != NONE)
				hud_deactivate_nav_point_with_object(iterator.datum_index, a[1]);
		}
	}
	hs_return(thread_index, 0);
}

H2_ALIGN static void deactivate_team_nav_point_object_evaluate(short function_index, long thread_index, boolean initialize)
{
	deactivate_team_nav_point(thread_index, initialize, FALSE);
	(void)function_index;
}

H2_ALIGN static void deactivate_team_nav_point_flag_evaluate(short function_index, long thread_index, boolean initialize)
{
	deactivate_team_nav_point(thread_index, initialize, TRUE);
	(void)function_index;
}

/* (activate_nav_point_object / _flag <navpoint> <unit> <object or flag>
<real>; deactivate_nav_point_object / _flag <unit> <object or flag>): a
player's unit's */
static void unit_nav_point(long thread_index, boolean initialize, boolean activate, boolean flag)
{
	short count;
	long *a = hs_runtime_arguments_evaluate_all(thread_index, activate ? 4 : 2, &count, initialize);

	if (!a)
		return;
	if (activate && count == 4 && a[1] != NONE && nav_points_shown("activate_nav_point"))
	{
		short nav = navpoint_index(a[0], "activate_nav_point");

		if (flag)
			hud_unit_activate_nav_point_with_flag(nav, a[1], value_short(a[2]), value_real(a[3]));
		else if (a[2] != NONE)
			hud_unit_activate_nav_point_with_object(nav, a[1], a[2], value_real(a[3]));
	}
	else if (!activate && count == 2 && a[0] != NONE)
	{
		if (flag)
			hud_unit_deactivate_nav_point_with_flag(a[0], value_short(a[1]));
		else if (a[1] != NONE)
			hud_unit_deactivate_nav_point_with_object(a[0], a[1]);
	}
	hs_return(thread_index, 0);
}

H2_ALIGN static void activate_nav_point_object_evaluate(short function_index, long thread_index, boolean initialize)
{
	unit_nav_point(thread_index, initialize, TRUE, FALSE);
	(void)function_index;
}

H2_ALIGN static void activate_nav_point_flag_evaluate(short function_index, long thread_index, boolean initialize)
{
	unit_nav_point(thread_index, initialize, TRUE, TRUE);
	(void)function_index;
}

H2_ALIGN static void deactivate_nav_point_object_evaluate(short function_index, long thread_index, boolean initialize)
{
	unit_nav_point(thread_index, initialize, FALSE, FALSE);
	(void)function_index;
}

H2_ALIGN static void deactivate_nav_point_flag_evaluate(short function_index, long thread_index, boolean initialize)
{
	unit_nav_point(thread_index, initialize, FALSE, TRUE);
	(void)function_index;
}

/* (breadcrumbs_activate_team_nav_point_position <navpoint> <team> <x> <y>
<z> <name> <real>) */
H2_ALIGN static void activate_team_nav_point_position_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = hs_runtime_arguments_evaluate_all(thread_index, 7, &count, initialize);

	if (!a)
		return;
	if (count == 7 && a[5] && nav_points_shown("breadcrumbs_activate_team_nav_point_position"))
	{
		real_point3d point;
		long index;

		point.x = value_real(a[2]);
		point.y = value_real(a[3]);
		point.z = value_real(a[4]);
		index = nav_point_position_index((char const *)a[5], &point);
		if (index != NONE)
			hud_port_activate_nav_point_with_position(navpoint_index(a[0], "breadcrumbs_activate_team_nav_point_position"), index,
				value_real(a[6]));
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (breadcrumbs_deactivate_team_nav_point <team> <name>) */
H2_ALIGN static void deactivate_team_nav_point_position_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = hs_runtime_arguments_evaluate_all(thread_index, 2, &count, initialize);

	if (!a)
		return;
	if (count == 2 && a[1])
	{
		long index = nav_point_position_index((char const *)a[1], NULL);

		if (index != NONE)
			hud_port_deactivate_nav_point_with_position(index);
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (unit_add_equipment <unit> <starting profile> <reset> <primary>) a
player's unit given the starting profile's weapons and grenades
(players.c player_add_equipment: the level's profiles are Halo 2's, in its
order - h2_level.c); a unit not a player's is left as it is */
H2_ALIGN static void unit_add_equipment_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = hs_runtime_arguments_evaluate_all(thread_index, 4, &count, initialize);

	if (!a)
		return;
	if (count >= 3 && a[0] != NONE && global_scenario_index != NONE)
	{
		short profile = value_short(a[1]);
		struct scenario *scenario = global_scenario_get();
		struct object_datum *object = object_try_and_get_and_verify_type(a[0], _object_mask_unit);

		if (object && profile >= 0 && profile < scenario->starting_profiles.count &&
			player_index_from_unit_index(a[0]) != NONE)
		{
			player_add_equipment(a[0], profile, value_boolean(a[2]));
			platform_log("h2 campaign: unit_add_equipment: starting profile %d given (reset %d)", (int)profile, (int)value_boolean(a[2]));
		}
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

static struct h2_scripts_native const campaign_natives[] =
{
	{ "unit_add_equipment", _hs_type_void, unit_add_equipment_evaluate, NONE, { 0 } },
	{ "activate_team_nav_point_object", _hs_type_void, activate_team_nav_point_object_evaluate, NONE, { 0 } },
	{ "activate_team_nav_point_flag", _hs_type_void, activate_team_nav_point_flag_evaluate, NONE, { 0 } },
	{ "breadcrumbs_activate_team_nav_point_object", _hs_type_void, activate_team_nav_point_object_evaluate, NONE, { 0 } },
	{ "breadcrumbs_activate_team_nav_point_flag", _hs_type_void, activate_team_nav_point_flag_evaluate, NONE, { 0 } },
	{ "deactivate_team_nav_point_object", _hs_type_void, deactivate_team_nav_point_object_evaluate, NONE, { 0 } },
	{ "deactivate_team_nav_point_flag", _hs_type_void, deactivate_team_nav_point_flag_evaluate, NONE, { 0 } },
	{ "breadcrumbs_deactivate_team_nav_point_object", _hs_type_void, deactivate_team_nav_point_object_evaluate, NONE, { 0 } },
	{ "breadcrumbs_deactivate_team_nav_point_flag", _hs_type_void, deactivate_team_nav_point_flag_evaluate, NONE, { 0 } },
	{ "activate_nav_point_object", _hs_type_void, activate_nav_point_object_evaluate, NONE, { 0 } },
	{ "activate_nav_point_flag", _hs_type_void, activate_nav_point_flag_evaluate, NONE, { 0 } },
	{ "deactivate_nav_point_object", _hs_type_void, deactivate_nav_point_object_evaluate, NONE, { 0 } },
	{ "deactivate_nav_point_flag", _hs_type_void, deactivate_nav_point_flag_evaluate, NONE, { 0 } },
	{ "breadcrumbs_activate_team_nav_point_position", _hs_type_void, activate_team_nav_point_position_evaluate, NONE, { 0 } },
	{ "breadcrumbs_deactivate_team_nav_point", _hs_type_void, deactivate_team_nav_point_position_evaluate, NONE, { 0 } },
};

H2_ALIGN struct h2_scripts_native const *h2_campaign_natives(
	long *count)
{
	*count = (long)NUMBEROF(campaign_natives);
	return campaign_natives;
}

/* Halo 2's campaign in order (its maps' names: no 02, which Halo 2 cut; Uprising
(08a) comes before High Charity (07b)) */
static char const *const h2_campaign_levels[] =
{
	"00a_introduction", "01a_tutorial", "01b_spacestation", "03a_oldmombasa", "03b_newmombasa", "04a_gasgiant",
	"04b_floodlab", "05a_deltaapproach", "05b_deltatowers", "06a_sentinelwalls", "06b_floodzone", "07a_highcharity",
	"08a_deltacliffs", "07b_forerunnership", "08b_deltacontrol",
};

long h2_campaign_level_count(
	void)
{
	return (long)NUMBEROF(h2_campaign_levels);
}

char const *h2_campaign_level_name(
	long index)
{
	return index >= 0 && index < (long)NUMBEROF(h2_campaign_levels) ? h2_campaign_levels[index] : NULL;
}

long h2_campaign_level_index(
	char const *map_name)
{
	char const *last;
	size_t length;
	short k;

	if (!map_name)
		return NONE;
	/* (a scenario's path, a tag's name - "h2\\03a_oldmombasa" -, or the level's name) */
	last = strrchr(map_name, '\\');
	last = last ? last + 1 : map_name;
	length = strlen(last);
	for (k = 0; k < (short)NUMBEROF(h2_campaign_levels); k++)
	{
		if (strlen(h2_campaign_levels[k]) == length && !strncasecmp(last, h2_campaign_levels[k], length))
			return k;
	}

	return NONE;
}

char const *h2_campaign_next_level(
	char const *map_name)
{
	static char next[128];
	char const *last;
	size_t length;
	short k;

	if (!map_name)
		return NULL;
	last = strrchr(map_name, '\\');
	last = last ? last + 1 : map_name;
	length = strlen(last);
	for (k = 0; k + 1 < (short)NUMBEROF(h2_campaign_levels); k++)
	{
		if (strlen(h2_campaign_levels[k]) == length && !strncasecmp(last, h2_campaign_levels[k], length))
		{
			/* (as the Halo 2 menus start a level: its scenario's path) */
			snprintf(next, sizeof(next), "scenarios\\solo\\%s\\%s", h2_campaign_levels[k + 1], h2_campaign_levels[k + 1]);
			return next;
		}
	}

	return NULL;
}

/* (scenario.c) a Halo 2 trigger volume attached to an object's node (h2_level.c
writes the object name at its unused 0x26 and the node name's string id at
0x28): that node's matrix now, NULL while the object is not there. The node
is found by name in the object's model once a map */
#include "h2_cache.h"
#include "h2_scripts.h"
#include "models/model_definitions.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"

real_matrix4x3 const *h2_trigger_volume_node_matrix(
	short trigger_volume_index)
{
	enum { MAXIMUM_VOLUMES = 1024 };
	static void const *scenario_seen;
	static short nodes[MAXIMUM_VOLUMES];
	struct scenario *scenario = global_scenario_get();
	struct scenario_trigger_volume const *volume;
	short name_index;
	long object_index;

	if (!scenario || trigger_volume_index < 0 || trigger_volume_index >= scenario->trigger_volumes.count ||
		trigger_volume_index >= MAXIMUM_VOLUMES)
	{
		return NULL;
	}
	if (scenario_seen != scenario)
	{
		short k;

		scenario_seen = scenario;
		for (k = 0; k < MAXIMUM_VOLUMES; k++)
			nodes[k] = -2;
	}
	volume = TAG_BLOCK_GET_ELEMENT(&scenario->trigger_volumes, trigger_volume_index, struct scenario_trigger_volume);
	name_index = *(short const *)(volume->unused + 2);
	if (name_index < 0 || name_index >= scenario->object_names.count ||
		(object_index = object_index_from_name_index(name_index)) == NONE || !object_try_and_get(object_index))
	{
		return NULL;
	}
	if (nodes[trigger_volume_index] == -2)
	{
		struct object_definition *definition = object_definition_get(object_get(object_index)->definition_index);
		struct model *model = definition->object.model.index != NONE ? model_definition_get(definition->object.model.index) : NULL;
		char node_name[64];
		short n;

		nodes[trigger_volume_index] = 0;
		if (model && h2_scripts_cache() &&
			h2_cache_string(h2_scripts_cache(), *(unsigned long const *)(volume->unused + 4), node_name, sizeof(node_name)))
		{
			for (n = 0; n < model->nodes.count; n++)
			{
				if (!strcasecmp(TAG_BLOCK_GET_ELEMENT(&model->nodes, n, struct model_node)->name, node_name))
					nodes[trigger_volume_index] = n;
			}
		}
	}
	return object_get_node_matrix(object_index, nodes[trigger_volume_index]);
}
