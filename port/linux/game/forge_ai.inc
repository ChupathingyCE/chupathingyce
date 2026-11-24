/*
FORGE_AI.INC

AI characters in Forge (included by forge.c, for its statics).

The maps: a multiplayer map has no AI characters in its tags; they are
brought in from the player's own campaign maps as a Forge game of one
loads (forge_ai_tags.c). The campaign missions have their own: the ten of
them are Forge's maps too, after the multiplayer ones (forge_maps' campaign entries), played from the player's own copy, as
the lobby lists every map whose file is installed. A Forge game, or a
layout played, on a mission is the same local game as on a multiplayer
map (forge_game_ask), but the mission stays out of it (forge_mission_off):
its scripts do not run (hs_runtime.c) and its encounters are not created
(encounters.c), so no cinematic plays and none of its own characters walk
in. The players spawn at the mission's own starting locations, which name
no game type (forge_campaign_spawn_location), unless the layout has spawn
points of its own; the structure bsp the start is in is loaded first, as the
mission's startup script would (forge_campaign_start_bsp_loaded). A mission's globals have no multiplayer block (the
multiplayer unit, the flag, the ball, the announcer): the reads of it are
guarded, so the campaign's player unit plays and no announcer speaks.

The characters: the AI category of the Forge menu's OBJECTS is the map's
actor variants that name their unit (not a vehicle's crew). One placed is
its unit, given its variant's weapon, grenades, vitality and camouflage
(actor_customize_unit, as a mission's own are) and a free actor of that
variant (ai_attach_free's ai_scripting_attach_free), or a swarm of its own
for a swarm's variant (a Flood infection form). It fights as a mission's
characters do. In a Forge game it is active wherever it is (the scripts'
ai_force_active), since the player's unit waits where the monitor took off
and the engine puts actors out of the players' sight to sleep; in a layout
played it sleeps out of the players' sight as a mission's own do. Its
properties: its AI TEAM (DEFAULT: its unit's own, or
PLAYER, HUMAN, COVENANT, FLOOD, SENTINEL), its AI (on, or off: it stands
as placed, the engine's actor_braindead), its RESPAWN (never, or after 10,
30, 60 or 120 seconds dead), whether it is placed at the start, and its
yaw. In a Forge game the humans are the player's allies, as every
mission's script makes them.

The counts: the HUD and the Forge menu show the characters alive, dead
and waiting. At most forge_ai_maximum() are alive at once (game.forge_ai:
128 on a desktop build, 32 with the console's limits); past it a
respawn, a new round's or a layout's character waits until one alive is
fewer. A dead one counts nowhere in the budget, but stays the layout's
(the next round brings it back, a saved layout keeps it); its body goes
after FORGE_BODY_SECONDS, the oldest first past FORGE_MAXIMUM_BODIES. A
Flood combat form feigning death is not dead. What the game spawns itself
(a carrier's infection forms) is never the layout's, nor counted. The
Forge menu's CLEAR DEAD AI takes the dead out of the layout, and CLEAR ALL
AI (pressed twice) every character, alive or dead.

The layouts: the format already holds them (forge_files.h, versions 2 and
3: the AI team, the AI off, the respawn). A layout of a multiplayer map
with such records still reads; their tags are not on the map, so they are
skipped as before.

Online: none of this. Forge is a local game only (forge_session_update),
so the AI runs on the one machine that plays it.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

/* ---------- constants */

#define FORGE_BODY_SECONDS 45
#define FORGE_MAXIMUM_BODIES 32
/* (actors.c's _actor_mode_braindead: an actor's state.mode while its AI is
off) */
#define FORGE_ACTOR_MODE_BRAINDEAD 0
/* (actors.c's actor_default_state_alert and _none) */
#define FORGE_ACTOR_DEFAULT_STATE_NONE 0
#define FORGE_ACTOR_DEFAULT_STATE_ALERT 2

/* a character's respawn: never, or this long after death */
static short const forge_ai_respawn_choices[] = { 0, 10, 30, 60, 120 };

/* the properties' words for the AI teams, and the log's */
static char const *const forge_ai_team_names[NUMBER_OF_FORGE_AI_TEAMS] =
{
	"DEFAULT", "PLAYER", "HUMAN", "COVENANT", "FLOOD", "SENTINEL",
};
static char const *const forge_ai_team_log_names[NUMBER_OF_FORGE_AI_TEAMS] =
{
	"default", "player", "human", "covenant", "flood", "sentinel",
};

/* ---------- private code */

/* ---- the campaign missions */

/* whether the loaded map is one of Forge's campaign missions */
static boolean forge_campaign_map_loaded(
	void)
{
	short map_index;

	if (global_scenario_index == NONE || !tag_get_name(global_scenario_index))
		return FALSE;
	map_index = forge_map_of_level(tag_get_name(global_scenario_index));
	return map_index != NONE && forge_maps[map_index].campaign;
}

/* whether a start is inside the structure bsp loaded (in one of its
clusters, at its feet or a little over them: a start on the floor can be
in no leaf) */
static boolean forge_campaign_point_in_bsp(
	real_point3d const *point)
{
	struct location location;
	real_point3d raised = *point;

	if (global_structure_bsp_index == NONE)
		return FALSE;
	scenario_location_from_point(&location, point);
	if (location.cluster_index != NONE)
		return TRUE;
	raised.z += 0.5f;
	scenario_location_from_point(&location, &raised);
	return location.cluster_index != NONE;
}

/* (a mission's start, before a player spawns there) the structure bsp the
start is in, loaded. A mission begins in its first bsp and its startup
script switches to the one its start is in (switch_bsp); the scripts don't
run in Forge, and the starting locations' own bsp index is 0 in every
retail mission, so the bsps are tried in turn (Assault on the Control
Room's and Two Betrayals' start is not in the first), once a map: FALSE if
none holds it (the bsp loaded as it was) */
static short forge_campaign_start_bsp = NONE;
static long forge_campaign_start_bsp_scenario = NONE;

static boolean forge_campaign_start_bsp_loaded(
	real_point3d const *point)
{
	short bsp;
	short was = global_structure_bsp_index;
	struct scenario *scenario = global_scenario_get();

	if (forge_campaign_point_in_bsp(point))
		return TRUE;
	if (!scenario)
		return FALSE;
	if (forge_campaign_start_bsp_scenario != global_scenario_index)
	{
		forge_campaign_start_bsp = NONE;
		forge_campaign_start_bsp_scenario = global_scenario_index;
	}
	/* (the one found before, then each other) */
	if (forge_campaign_start_bsp != NONE && forge_campaign_start_bsp != global_structure_bsp_index &&
		scenario_switch_structure_bsp(forge_campaign_start_bsp) && forge_campaign_point_in_bsp(point))
	{
		platform_log("forge: the mission's start is in structure bsp %d: loaded again", forge_campaign_start_bsp);
		return TRUE;
	}
	for (bsp = 0; bsp < scenario->structure_bsp_references.count && bsp < MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO; bsp++)
	{
		if (bsp == global_structure_bsp_index || !scenario_switch_structure_bsp(bsp))
			continue;
		if (forge_campaign_point_in_bsp(point))
		{
			forge_campaign_start_bsp = bsp;
			platform_log("forge: the mission's start is in structure bsp %d of %ld: loaded", bsp,
				scenario->structure_bsp_references.count);
			return TRUE;
		}
	}
	platform_log("forge: no structure bsp holds the mission's start: bsp %d kept", was);
	if (was != NONE && was != global_structure_bsp_index)
		scenario_switch_structure_bsp(was);
	return FALSE;
}

/* (forge_spawn_location, no layout spawn point fits) a mission's own
starting locations name no game type, so the engine rates none of them for
a Forge game's: the player's by its number, any type, in the structure bsp
it is in (forge_campaign_start_bsp_loaded). NONE: the map's own choice */
static short forge_campaign_spawn_location(
	long player_index,
	struct player_starting_location *location)
{
	struct scenario *scenario;
	short absolute;
	struct player_starting_location const *start;

	if (!forge_globals.campaign || !forge_globals.game_active || player_index == NONE)
		return _forge_spawn_location_map;
	scenario = global_scenario_get();
	if (!scenario || scenario->players.count <= 0)
		return _forge_spawn_location_map;
	absolute = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	start = TAG_BLOCK_GET_ELEMENT(&scenario->players, absolute % scenario->players.count, struct player_starting_location);
	/* (its bsp loaded first; the player spawns at the next tick, in it: a
	unit made in the tick the bsp changes is not placed there) */
	if (!forge_campaign_point_in_bsp(&start->position))
	{
		short was = global_structure_bsp_index;

		if (forge_campaign_start_bsp_loaded(&start->position) && global_structure_bsp_index != was)
			return _forge_spawn_location_wait;
	}
	csmemset(location, 0, sizeof(*location));
	location->position = start->position;
	location->facing = start->facing;
	location->team_index = (short)player_get(player_index)->team_index;
	location->game_types[0] = (short)(game_engine_running() ? game_engine_get_variant()->game_engine_index : 0);
	platform_log("forge: player %d spawns at the mission's start %d of %d, at %.2f %.2f %.2f", absolute,
		absolute % scenario->players.count, scenario->players.count, start->position.x, start->position.y, start->position.z);
	return _forge_spawn_location_found;
}

/* ---- the characters */

/* whether a placed character is dead (its body there or gone; a Flood
combat form feigning death is not: it gets back up as the same unit) */
static boolean forge_placed_dead(
	struct forge_placed const *placed)
{
	struct unit_datum *unit;

	if (!placed->ai)
		return FALSE;
	if (!forge_object_alive(placed->object_index))
		return TRUE;
	unit = (struct unit_datum *)object_get(placed->object_index);
	if (!TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return FALSE;
	return !(FLAG(unit->object.type) & _object_mask_unit) || unit->unit.feign_death_timer <= 0;
}

/* (forge_placed_update) the layout's characters counted: all, the dead
or waiting among them, the waiting */
static void forge_ai_count(
	void)
{
	short index;

	forge_globals.ai_count = 0;
	forge_globals.ai_dead_count = 0;
	forge_globals.ai_waiting_count = 0;
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed *placed = &forge_globals.placed[index];

		if (!placed->ai)
			continue;
		forge_globals.ai_count++;
		if (placed->waiting && placed->object_index == NONE)
			forge_globals.ai_waiting_count++;
		else
			placed->waiting = FALSE;
		if (forge_placed_dead(placed))
			forge_globals.ai_dead_count++;
	}
}

/* the characters alive (the dead and the waiting not) */
static short forge_ai_alive(
	void)
{
	return (short)(forge_globals.ai_count - forge_globals.ai_dead_count);
}

/* the dead (not the waiting) */
static short forge_ai_dead_only(
	void)
{
	return (short)(forge_globals.ai_dead_count - forge_globals.ai_waiting_count);
}

/* the engine's team (game.h _game_team_*) an AI team is for a unit: the
player's own, an AI team, or the unit's own default */
static short forge_ai_team_index(
	short ai_team,
	long unit_index)
{
	struct object_datum *object;

	switch (ai_team)
	{
	case FORGE_AI_TEAM_PLAYER:
	{
		long player_index = local_player_get_player_index(0);

		return player_index != NONE ? (short)player_get(player_index)->team_index : (short)_game_team_player;
	}
	case FORGE_AI_TEAM_HUMAN:
		return _game_team_human;
	case FORGE_AI_TEAM_COVENANT:
		return _game_team_covenant;
	case FORGE_AI_TEAM_FLOOD:
		return _game_team_flood;
	case FORGE_AI_TEAM_SENTINEL:
		return _game_team_sentinel;
	}
	/* (its unit's own; one of no team of the campaign's fights everyone
	but its own kind: a team no one else has) */
	object = object_try_and_get(unit_index);
	if (object)
	{
		short team = unit_definition_get(object->definition_index)->unit.default_team;

		if (team > _game_team_default && team < NUMBER_OF_SOLO_CAMPAIGN_TEAMS)
			return team;
	}
	return _game_team_unused6;
}

/* a unit's actor: its own, or its swarm's (a Flood infection form's), or
NONE */
static long forge_ai_actor(
	long unit_index)
{
	struct object_datum *object = object_try_and_get(unit_index);
	struct unit_datum *unit;

	if (!object || !(FLAG(object->object.type) & _object_mask_unit))
		return NONE;
	unit = (struct unit_datum *)object;
	return unit->unit.actor_index != NONE ? unit->unit.actor_index : unit->unit.swarm_actor_index;
}

/* a placed character's AI settings given to its unit and actor (a dead
one has none to give): the unit's and the actor's team, as the scripts'
ai_set_team gives them (ai_update_team_status then has every actor see
friend and foe again), and its brain on or off, as ai_braindead does */
static void forge_ai_apply(
	struct forge_placed const *placed)
{
	struct unit_datum *unit;
	long actor_index;
	short team;

	if (!placed || !placed->ai || !forge_object_alive(placed->object_index))
		return;
	unit = unit_get(placed->object_index);
	if (TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return;
	team = forge_ai_team_index(placed->ai_team, placed->object_index);
	unit->object.owner_team_index = team;
	actor_index = forge_ai_actor(placed->object_index);
	if (actor_index != NONE)
	{
		struct actor_datum *actor = actor_get(actor_index);

		/* (a swarm's: every unit of it) */
		actor_set_team(actor_index, team);
		actor->meta.team_index = team;
		actor_braindead(actor_index, (placed->flags & FORGE_RECORD_AI_OFF) != 0);
		/* (in a Forge game, active wherever it is, as the scripts'
		ai_force_active makes a character: the actors near no player's
		unit sleep, and the player's unit waits where the monitor took off.
		In a layout played, as the campaign's: asleep out of the players'
		sight) */
		if (actor->meta.encounterless)
			actor->meta.force_active = !forge_globals.playing;
	}
	ai_update_team_status();
}

/* (forge_place) a character's unit, just made, given its variant's
equipment and an actor: a free one, or a swarm of its own for a swarm's
variant (ai_attach_free does not take those: a Flood infection form), as a
carrier's infection forms are made a swarm */
static void forge_ai_attach(
	long object_index,
	long actor_variant_index)
{
	long actor_definition_index = actor_variant_definition_get(actor_variant_index)->actor_reference.index;

	/* (its variant's weapon, grenades, vitality, colors and camouflage, as
	a mission's own characters are given them as they are placed) */
	if (actor_definition_index != NONE)
		actor_customize_unit(actor_variant_index, object_index);
	if (actor_definition_index != NONE &&
		TEST_FLAG(actor_definition_get(actor_definition_index)->flags, _actor_definition_swarm_actor_bit))
	{
		actor_create_for_unit(TRUE, object_index, actor_variant_index, NONE, 0, FALSE, NONE, FALSE,
			FORGE_ACTOR_DEFAULT_STATE_ALERT, FORGE_ACTOR_DEFAULT_STATE_NONE, NONE, 0);
	}
	else
	{
		ai_scripting_attach_free(object_index, actor_variant_index);
	}
}

/* every frame: a placed character with its AI off kept as it stands (its
actor braindead again if anything woke it), facing where the editor turned
it - a braindead unit keeps the facing it had when its AI went off and
would turn back to it -; in a Forge game, the humans the player's allies */
static void forge_ai_hold(
	void)
{
	short index;

	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed const *placed = &forge_globals.placed[index];
		struct unit_datum *unit;
		long actor_index;
		real_vector3d forward;

		if (!placed->ai || !(placed->flags & FORGE_RECORD_AI_OFF) || !forge_object_alive(placed->object_index))
			continue;
		unit = unit_get(placed->object_index);
		actor_index = forge_ai_actor(placed->object_index);
		if (actor_index == NONE || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			continue;
		if (actor_get(actor_index)->state.mode != FORGE_ACTOR_MODE_BRAINDEAD)
			actor_braindead(actor_index, TRUE);
		forward = unit->object.forward;
		forward.k = 0.f;
		if (normalize3d(&forward) < 0.0001f)
			continue;
		if (dot_product3d(&forward, &unit->unit.desired_facing_vector) < 0.9999f)
		{
			struct unit_control_data control;

			csmemset(&control, 0, sizeof(control));
			control.animation_state = _unit_animation_state_alert;
			control.throttle = *global_zero_vector3d;
			control.aiming_speed = 1;
			control.weapon_index = NONE;
			control.grenade_index = NONE;
			control.zoom_level = NONE;
			control.facing_vector = forward;
			control.aiming_vector = forward;
			control.looking_vector = forward;
			unit_control(placed->object_index, &control);
		}
	}
	/* (the humans the local player's allies, as every mission's script
	makes them (ai_allegiance player human), again after a new round, which
	starts the allegiances over) */
	if (forge_globals.ai_count > 0)
	{
		long player_index = local_player_get_player_index(0);
		short team = player_index != NONE ? (short)player_get(player_index)->team_index : NONE;

		if (team >= 0 && team < NUMBER_OF_SOLO_CAMPAIGN_TEAMS && team != _game_team_human &&
			game_team_is_enemy(team, _game_team_human))
		{
			ai_scripting_allegiance(team, _game_team_human);
			ai_update_team_status();
			platform_log("forge: the humans are the player's allies (team %d)", team);
		}
	}
}

/* (CLEAR DEAD AI, CLEAR ALL AI) the layout's dead characters, or every
one of them alive or dead, taken out of the layout and the world: they do
not come back with the next round, a layout saved after has them not, and
the steps of the undo history that name them go. What the game spawned is
the game's, left. The characters cleared, counted */
static short forge_ai_clear(
	boolean all)
{
	short index;
	short cleared = 0;
	long cleared_ids[FORGE_MAXIMUM_OBJECTS];

	forge_placed_update();
	for (index = (short)(forge_globals.placed_count - 1); index >= 0; index--)
	{
		struct forge_placed *placed = &forge_globals.placed[index];

		if (!placed->ai || (!all && (!forge_placed_dead(placed) || placed->waiting)))
			continue;
		if (placed->object_index != NONE && placed->object_index == forge_globals.held_object_index)
		{
			forge_globals.held_object_index = NONE;
			forge_globals.held_spawned = FALSE;
		}
		if (forge_object_alive(placed->object_index))
			object_delete(placed->object_index);
		cleared_ids[cleared++] = placed->id;
		forge_placed_remove(index);
	}
	forge_undo_forget(cleared_ids, cleared);
	forge_placed_update();
	platform_log("forge: cleared %d %s AI; budget %d/%d objects, AI %d alive, %d dead, %d waiting", cleared,
		all ? "placed" : "dead", forge_objects_counted(), forge_objects_budget(), forge_ai_alive(), forge_ai_dead_only(),
		forge_globals.ai_waiting_count);
	if (!cleared)
		forge_menu_notice("%s", all ? "No AI to clear." : "No dead AI to clear.");
	else if (all)
		forge_menu_notice("Cleared all AI: %d.", cleared);
	else
		forge_menu_notice("Cleared dead AI: %d.", cleared);
	return cleared;
}

/* (each update: a Forge game's and a layout played) the layout's
characters' lives: one that respawns, seen dead its respawn time of game
time, is placed again where the editor put it, its properties and its id
kept, its body taken away; one waiting for room is placed as soon as one
alive is fewer; a body is taken away FORGE_BODY_SECONDS after death, and
the oldest first past FORGE_MAXIMUM_BODIES (the character stays the
layout's). Never what the monitor holds */
static void forge_ai_life_update(
	void)
{
	long now;
	short index;
	short bodies = 0;

	if (!forge_globals.game_active || forge_globals.game_requested || forge_globals.round_pending ||
		forge_globals.round_resetting || !game_in_progress() || forge_globals.ai_count <= 0)
	{
		return;
	}
	now = game_time_get();
	forge_placed_update();
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed *placed = &forge_globals.placed[index];
		struct forge_placed kept;
		long object_index;
		short refusal;
		boolean held;

		if (!placed->ai)
			continue;
		if (!placed->waiting && !forge_placed_dead(placed))
		{
			placed->dead_tick = 0;
			continue;
		}
		held = placed->object_index != NONE && placed->object_index == forge_globals.held_object_index;
		if (!placed->waiting)
		{
			if (!placed->dead_tick)
				placed->dead_tick = now + 1;
			/* (its body: gone after a while) */
			if (placed->object_index != NONE && !held &&
				now + 1 - placed->dead_tick >= (long)FORGE_BODY_SECONDS * FORGE_TICKS_PER_SECOND)
			{
				platform_log("forge: %s (#%ld): its body cleared after %d s", forge_tag_leaf(placed->tag_index), placed->id,
					FORGE_BODY_SECONDS);
				object_delete(placed->object_index);
				placed->object_index = NONE;
			}
			if (placed->object_index != NONE)
				bodies++;
			if (!(placed->flags & FORGE_RECORD_AI_RESPAWN) || !placed->has_home ||
				now + 1 - placed->dead_tick < (long)placed->respawn_seconds * FORGE_TICKS_PER_SECOND)
			{
				continue;
			}
		}
		if (held || !placed->has_home)
			continue;
		/* (no room: waiting, its body gone, until one alive is fewer) */
		forge_placed_update();
		if (forge_ai_alive() >= forge_ai_maximum())
		{
			if (!placed->waiting)
			{
				if (forge_object_alive(placed->object_index))
					object_delete(placed->object_index);
				placed->object_index = NONE;
				placed->waiting = TRUE;
				platform_log("forge: %s (#%ld) waits to respawn: %d characters alive", forge_tag_leaf(placed->tag_index),
					placed->id, forge_ai_maximum());
			}
			continue;
		}
		kept = *placed;
		if (forge_object_alive(placed->object_index))
			object_delete(placed->object_index);
		forge_placed_remove(index);
		object_index = forge_place(kept.tag_index, &kept.home.position, &kept.home.forward, &kept.home.up, TRUE, &refusal);
		if (object_index == NONE || forge_globals.placed_count <= 0 ||
			forge_globals.placed[forge_globals.placed_count - 1].object_index != object_index)
		{
			/* (not placed: the entry kept, waiting, tried again) */
			if (forge_globals.placed_count < FORGE_MAXIMUM_OBJECTS)
			{
				kept.object_index = NONE;
				kept.waiting = TRUE;
				forge_globals.placed[forge_globals.placed_count++] = kept;
				forge_placed_update();
			}
			platform_log("forge: %s (#%ld) could not respawn (%d)", forge_tag_leaf(kept.tag_index), kept.id, (int)refusal);
			continue;
		}
		{
			struct forge_placed *entry = &forge_globals.placed[forge_globals.placed_count - 1];

			entry->id = kept.id;
			entry->team = kept.team;
			entry->flags = kept.flags;
			entry->respawn_seconds = kept.respawn_seconds;
			entry->ai_team = kept.ai_team;
			entry->home = kept.home;
			entry->has_home = TRUE;
			forge_ai_apply(entry);
		}
		if (kept.waiting)
			platform_log("forge: %s (#%ld) placed: room for it at %.2f %.2f %.2f", forge_tag_leaf(kept.tag_index), kept.id,
				kept.home.position.x, kept.home.position.y, kept.home.position.z);
		else
			platform_log("forge: %s (#%ld) respawned after %d s at %.2f %.2f %.2f", forge_tag_leaf(kept.tag_index), kept.id,
				(int)kept.respawn_seconds, kept.home.position.x, kept.home.position.y, kept.home.position.z);
		/* (the entry moved to the end: this index holds the next one) */
		index--;
	}
	/* (past FORGE_MAXIMUM_BODIES bodies: the oldest taken away) */
	while (bodies > FORGE_MAXIMUM_BODIES)
	{
		short oldest = NONE;

		for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
		{
			struct forge_placed const *placed = &forge_globals.placed[index];

			if (!placed->ai || placed->waiting || !placed->dead_tick || !forge_object_alive(placed->object_index) ||
				placed->object_index == forge_globals.held_object_index)
			{
				continue;
			}
			if (oldest == NONE || placed->dead_tick < forge_globals.placed[oldest].dead_tick)
				oldest = index;
		}
		if (oldest == NONE)
			break;
		platform_log("forge: %s (#%ld): its body cleared, %d bodies past %d",
			forge_tag_leaf(forge_globals.placed[oldest].tag_index), forge_globals.placed[oldest].id, bodies,
			FORGE_MAXIMUM_BODIES);
		object_delete(forge_globals.placed[oldest].object_index);
		forge_globals.placed[oldest].object_index = NONE;
		bodies--;
	}
}

/* (forge_property_apply) a character's AI properties stepped: its AI team,
its AI, its respawn (never, or a time) */
static boolean forge_ai_property_apply(
	struct forge_placed *placed,
	short property,
	short step)
{
	short index;
	short count = (short)NUMBEROF(forge_ai_respawn_choices);

	switch (property)
	{
	case _forge_property_ai_team:
		placed->ai_team = (unsigned char)((placed->ai_team + step + NUMBER_OF_FORGE_AI_TEAMS) % NUMBER_OF_FORGE_AI_TEAMS);
		break;
	case _forge_property_ai:
		placed->flags ^= FORGE_RECORD_AI_OFF;
		break;
	case _forge_property_respawn:
		for (index = 0; index < count - 1; index++)
		{
			if ((placed->flags & FORGE_RECORD_AI_RESPAWN) ? forge_ai_respawn_choices[index] >= placed->respawn_seconds :
				forge_ai_respawn_choices[index] == 0)
			{
				break;
			}
		}
		index = (short)((index + step + count) % count);
		if (forge_ai_respawn_choices[index])
		{
			placed->flags |= FORGE_RECORD_AI_RESPAWN;
			placed->respawn_seconds = (unsigned short)forge_ai_respawn_choices[index];
		}
		else
		{
			placed->flags &= (unsigned char)~FORGE_RECORD_AI_RESPAWN;
		}
		placed->dead_tick = 0;
		break;
	default:
		return FALSE;
	}
	forge_ai_apply(placed);
	platform_log("forge: #%ld's AI: team %s (engine team %d), AI %s, respawn %s%d s", placed->id,
		forge_ai_team_log_names[placed->ai_team % NUMBER_OF_FORGE_AI_TEAMS],
		forge_ai_team_index(placed->ai_team, placed->object_index), (placed->flags & FORGE_RECORD_AI_OFF) ? "off" : "on",
		(placed->flags & FORGE_RECORD_AI_RESPAWN) ? "after " : "never, ",
		(placed->flags & FORGE_RECORD_AI_RESPAWN) ? (int)placed->respawn_seconds : 0);
	return TRUE;
}

/* (the console's "forge ai") the layout's characters in the log */
static void forge_ai_log(
	void)
{
	short index;
	short listed = 0;

	forge_placed_update();
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed const *placed = &forge_globals.placed[index];
		struct unit_datum *unit;
		long actor_index;

		if (!placed->ai)
			continue;
		unit = forge_object_alive(placed->object_index) ? unit_get(placed->object_index) : NULL;
		actor_index = forge_ai_actor(placed->object_index);
		platform_log("forge: ai %d: %s (id %ld) %s at %.2f %.2f %.2f, AI team %s, engine team %d, AI %s, actor %s "
			"(active %d, dormant %d, mode %d, action %d, combat %d), vitality %.2f, respawn %d s", listed,
			forge_tag_leaf(placed->tag_index), placed->id,
			placed->waiting ? "waiting" : forge_placed_dead(placed) ? "dead" : "alive",
			unit ? unit->object.position.x : placed->home.position.x, unit ? unit->object.position.y : placed->home.position.y,
			unit ? unit->object.position.z : placed->home.position.z,
			forge_ai_team_log_names[placed->ai_team % NUMBER_OF_FORGE_AI_TEAMS], unit ? (int)unit->object.owner_team_index : -1,
			(placed->flags & FORGE_RECORD_AI_OFF) ? "off" : "on", actor_index != NONE ? "yes" : "none",
			actor_index != NONE ? (int)actor_get(actor_index)->meta.active : -1,
			actor_index != NONE ? (int)actor_get(actor_index)->meta.dormant : -1,
			actor_index != NONE ? (int)actor_get(actor_index)->state.mode : -1,
			actor_index != NONE ? (int)actor_get(actor_index)->state.action : -1,
			actor_index != NONE ? (int)actor_get(actor_index)->state.combat_status : -1,
			unit ? unit->object.body_vitality : 0.f,
			(placed->flags & FORGE_RECORD_AI_RESPAWN) ? (int)placed->respawn_seconds : 0);
		listed++;
	}
	{
		struct data_iterator iterator;
		short actors = 0;

		data_iterator_new(&iterator, actor_data);
		while (data_iterator_next(&iterator))
			actors++;
		platform_log("forge: ai listed: %d (alive %d, dead %d, waiting %d); the game's actors: %d, props %d of %d", listed,
			forge_ai_alive(), forge_ai_dead_only(), forge_globals.ai_waiting_count, actors, (int)prop_data->actual_count,
			(int)prop_data->maximum_count);
	}
}

/* ---------- public code */

boolean forge_mission_off(
	void)
{
	return forge_globals.campaign && forge_globals.game_active && game_connection() == _game_connection_local;
}
