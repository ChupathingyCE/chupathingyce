/*
BOT_MANAGER.C

port: the bots' blackboard, after Halo Infinite's BotManager (GDC 2022,
"Thinking Like Players"; docs/bot_decisions.md). It holds:

  - ambitions: the objects and areas that score in the game mode, each with
    the teams that may use it. The game engine stays the authority: an
    adapter for each mode reads its state and writes the ambitions, and a bot
    acts on them only as a player can (walking, picking up, standing in);
  - sightings: each team's last sighting of each enemy, written by the bots
    whose skill shares what they see, read by their teammates. port: local
    humans also publish sustained camera/LOS contacts through human_callouts.c;
    incoming-damage cues are separate bearings, never visual sightings.

Everything here is host-local and derived: it is rebuilt from the game's
state and never sent to another machine.
*/

/* ---------- headers */

#include "cseries.h"
#include "game.h"
#include "game_engine.h"
#include "players.h"
#include "units/units.h"
#include "engine_ai/claims.h"
#include "bot_manager.h"
#include "human_callouts.h" /* port: implicit local-human team awareness */
#include "halo_bot_objectives.h" /* port: game-owned copied flag state */
int config_boolean(char const *name);

/* port/linux/src/platform.h's */
void platform_log(char const *format, ...);

#ifdef HALO_FEATURE_BOTS

/* At most four duties per team; publication cannot exhaust the goal table. */
typedef char bot_manager_ctf_goals_fit[BOT_AMBITIONS_MAXIMUM >= 4 * HALO_BOT_CTF_TEAMS ? 1 : -1];

/* ---------- structures */

struct bot_sighting
{
	long player_index; /* NONE: no sighting */
	long time;
	long last_callout_time;
	real_point3d position;
};

/* ---------- globals */

static struct
{
	short ambition_count;
	struct bot_ambition ambitions[BOT_AMBITIONS_MAXIMUM];
	struct bot_sighting sightings[BOT_MANAGER_TEAMS][HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	struct engine_ai_claims item_claims;
	/* port: vehicle seats, keyed by bot_manager_seat_key */
	struct engine_ai_claims seat_claims;
	/* port: transition-only evidence of actual engine ownership and scoring. */
	boolean ctf_observed;
	struct halo_bot_ctf_snapshot ctf_previous;
} bot_manager_globals;

/* ---------- private prototypes */

static void bot_manager_publish_ambitions(void);

/* ---------- public code */

void bot_manager_reset(
	void)
{
	short team_index;
	short player_index;

	csmemset(&bot_manager_globals, 0, sizeof(bot_manager_globals));
	engine_ai_claims_reset(&bot_manager_globals.item_claims);
	engine_ai_claims_reset(&bot_manager_globals.seat_claims);
	human_callouts_reset(); /* port: no contacts survive map/game resets */
	for (team_index = 0; team_index < BOT_MANAGER_TEAMS; team_index++)
	{
		for (player_index = 0; player_index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; player_index++)
		{
			bot_manager_globals.sightings[team_index][player_index].player_index = NONE;
			bot_manager_globals.sightings[team_index][player_index].last_callout_time = NONE;
		}
	}
}

void bot_manager_update(
	void)
{
	bot_manager_publish_ambitions();
	human_callouts_update(); /* port: human POV contacts before teammates think */
}

short bot_manager_ambition_count(
	void)
{
	return bot_manager_globals.ambition_count;
}

struct bot_ambition const *bot_manager_ambition_get(
	short ambition_index)
{
	if (ambition_index < 0 || ambition_index >= bot_manager_globals.ambition_count)
		return NULL;
	return &bot_manager_globals.ambitions[ambition_index];
}

boolean bot_manager_ambition_allowed(
	struct bot_ambition const *ambition,
	long team_index)
{
	if (!ambition || ambition->kind == _bot_ambition_none)
		return FALSE;
	if (!game_engine_has_teams())
		return TRUE;
	if (team_index < 0 || team_index >= 32)
		return FALSE;
	return (ambition->team_mask & (1UL << team_index)) != 0;
}

void bot_manager_report_sighting(
	long team_index,
	long enemy_player_index,
	short reporter_bot_number,
	real_point3d const *position)
{
	struct bot_sighting *sighting;
	long absolute_index;
	long now;
	boolean callout;

	if (!game_engine_has_teams() || team_index < 0 || team_index >= BOT_MANAGER_TEAMS ||
		enemy_player_index == NONE || !position)
	{
		return;
	}
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(enemy_player_index);
	if (absolute_index < 0 || absolute_index >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
		return;
	sighting = &bot_manager_globals.sightings[team_index][absolute_index];
	now = game_time_get();
	callout = sighting->player_index != enemy_player_index ||
		sighting->last_callout_time == NONE || now < sighting->last_callout_time ||
		now - sighting->last_callout_time >= 4 * TICKS_PER_SECOND ||
		distance3d(position, &sighting->position) >= 8.f;
	if (callout)
	{
		/* port: zero denotes an implicit human callout, not Bot 0. */
		if (reporter_bot_number == 0)
			platform_log("bots: human called out enemy %ld last seen at (%.1f %.1f %.1f)",
				absolute_index, position->x, position->y, position->z);
		else
			platform_log("bots: bot %d called out enemy %ld last seen at (%.1f %.1f %.1f)",
				reporter_bot_number, absolute_index, position->x, position->y, position->z);
		sighting->last_callout_time = now;
	}
	sighting->player_index = enemy_player_index;
	sighting->time = now;
	sighting->position = *position;
}

boolean bot_manager_nearest_sighting(
	long team_index,
	real_point3d const *from,
	long maximum_age_ticks,
	long *enemy_player_index,
	real_point3d *position)
{
	long now = game_time_get();
	long best_index = NONE;
	real best_distance = 0.f;
	short absolute_index;

	if (!game_engine_has_teams() || team_index < 0 || team_index >= BOT_MANAGER_TEAMS || !from)
		return FALSE;
	for (absolute_index = 0; absolute_index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; absolute_index++)
	{
		struct bot_sighting *sighting = &bot_manager_globals.sightings[team_index][absolute_index];
		struct player_datum *player;
		struct unit_datum *unit;
		real distance;

		if (sighting->player_index == NONE)
			continue;
		player = player_try_and_get(sighting->player_index);
		unit = player && player->unit_index != NONE ? unit_try_and_get(player->unit_index) : NULL;
		/* (stale, gone, dead, or now a teammate) */
		if (now - sighting->time > maximum_age_ticks || now < sighting->time || !unit ||
			TEST_FLAG(unit->object.damage_flags, _object_dead_bit) || player->team_index == team_index)
		{
			sighting->player_index = NONE;
			continue;
		}
		distance = distance3d(from, &sighting->position);
		if (best_index == NONE || distance < best_distance)
		{
			best_index = absolute_index;
			best_distance = distance;
		}
	}
	if (best_index == NONE)
		return FALSE;
	if (enemy_player_index)
		*enemy_player_index = bot_manager_globals.sightings[team_index][best_index].player_index;
	if (position)
		*position = bot_manager_globals.sightings[team_index][best_index].position;
	return TRUE;
}

static boolean bot_manager_claim_scope(
	long team_index,
	uint32_t *claim_scope)
{
	assert(claim_scope);
	if (!game_engine_has_teams())
	{
		*claim_scope = 0;
		return TRUE;
	}
	if (team_index < 0 || team_index >= BOT_MANAGER_TEAMS)
		return FALSE;
	*claim_scope = (uint32_t)team_index;
	return TRUE;
}

/* Claims are scoped to a team and keyed by the object's salted datum index. */
boolean bot_manager_claim_item(
	long team_index,
	long object_index,
	short bot_number,
	long duration_ticks)
{
	long now = game_time_get();
	uint32_t claim_scope;

	if (object_index == NONE || bot_number <= 0 || duration_ticks <= 0 || now < 0 ||
		!bot_manager_claim_scope(team_index, &claim_scope))
		return FALSE;
	return engine_ai_claims_acquire(&bot_manager_globals.item_claims, claim_scope,
		(uint32_t)object_index, (uint32_t)bot_number, (uint64_t)now, (uint64_t)duration_ticks);
}

boolean bot_manager_item_claimed_by_other(
	long team_index,
	long object_index,
	short bot_number)
{
	long now = game_time_get();
	uint32_t claim_scope;

	if (object_index == NONE || bot_number <= 0 || now < 0 ||
		!bot_manager_claim_scope(team_index, &claim_scope))
		return FALSE;
	return engine_ai_claims_held_by_other(&bot_manager_globals.item_claims, claim_scope,
		(uint32_t)object_index, (uint32_t)bot_number, (uint64_t)now);
}

void bot_manager_release_item(
	long team_index,
	long object_index,
	short bot_number)
{
	uint32_t claim_scope;

	if (object_index == NONE || bot_number <= 0 ||
		!bot_manager_claim_scope(team_index, &claim_scope))
		return;
	engine_ai_claims_release(&bot_manager_globals.item_claims, claim_scope,
		(uint32_t)object_index, (uint32_t)bot_number);
}

void bot_manager_release_bot(
	long team_index,
	short bot_number)
{
	uint32_t claim_scope;

	if (bot_number <= 0 || !bot_manager_claim_scope(team_index, &claim_scope))
		return;
	engine_ai_claims_release_owner(&bot_manager_globals.item_claims, claim_scope,
		(uint32_t)bot_number);
	engine_ai_claims_release_owner(&bot_manager_globals.seat_claims, claim_scope,
		(uint32_t)bot_number);
}

/* A seat's claim key: the vehicle's absolute index and the seat (below
BOT_MANAGER_SEATS). The salt is not in it: a claim outlives its vehicle by at
most its short duration, and a replacement in the same place is the same
seat to a teammate. */
static boolean bot_manager_seat_key(
	long vehicle_index,
	short seat_index,
	uint32_t *key)
{
	assert(key);
	if (vehicle_index == NONE || seat_index < 0 || seat_index >= BOT_MANAGER_SEATS)
		return FALSE;
	*key = (uint32_t)DATUM_INDEX_TO_ABSOLUTE_INDEX(vehicle_index) * BOT_MANAGER_SEATS + (uint32_t)seat_index;
	return TRUE;
}

boolean bot_manager_claim_seat(
	long team_index,
	long vehicle_index,
	short seat_index,
	short bot_number,
	long duration_ticks)
{
	long now = game_time_get();
	uint32_t claim_scope;
	uint32_t key;

	if (bot_number <= 0 || duration_ticks <= 0 || now < 0 ||
		!bot_manager_seat_key(vehicle_index, seat_index, &key) ||
		!bot_manager_claim_scope(team_index, &claim_scope))
		return FALSE;
	return engine_ai_claims_acquire(&bot_manager_globals.seat_claims, claim_scope,
		key, (uint32_t)bot_number, (uint64_t)now, (uint64_t)duration_ticks);
}

boolean bot_manager_seat_claimed_by_other(
	long team_index,
	long vehicle_index,
	short seat_index,
	short bot_number)
{
	long now = game_time_get();
	uint32_t claim_scope;
	uint32_t key;

	if (bot_number <= 0 || now < 0 ||
		!bot_manager_seat_key(vehicle_index, seat_index, &key) ||
		!bot_manager_claim_scope(team_index, &claim_scope))
		return FALSE;
	return engine_ai_claims_held_by_other(&bot_manager_globals.seat_claims, claim_scope,
		key, (uint32_t)bot_number, (uint64_t)now);
}

boolean bot_manager_vehicle_claimed_by_other(
	long team_index,
	long vehicle_index,
	short bot_number)
{
	short seat_index;

	/* (BOT_MANAGER_SEATS lookups) */
	for (seat_index = 0; seat_index < BOT_MANAGER_SEATS; seat_index++)
		if (bot_manager_seat_claimed_by_other(team_index, vehicle_index, seat_index, bot_number))
			return TRUE;
	return FALSE;
}

void bot_manager_release_seat(
	long team_index,
	long vehicle_index,
	short seat_index,
	short bot_number)
{
	uint32_t claim_scope;
	uint32_t key;

	if (bot_number <= 0 || !bot_manager_seat_key(vehicle_index, seat_index, &key) ||
		!bot_manager_claim_scope(team_index, &claim_scope))
		return;
	engine_ai_claims_release(&bot_manager_globals.seat_claims, claim_scope, key, (uint32_t)bot_number);
}

/* port: Goals are rebuilt every tick, but identity is kind/team/salted datum,
not their current array offset. Losing a claim leaves another bot free to act. */
boolean bot_manager_ctf_goal(long team_index, long player_index, short bot_number,
	long unavailable_object_index, struct bot_ambition *goal)
{
	short index;
	struct bot_ambition const *best = NULL;
	if (!goal || team_index < 0 || team_index >= BOT_MANAGER_TEAMS ||
		player_index == NONE || bot_number <= 0) return FALSE;
	for (index = 0; index < bot_manager_globals.ambition_count; index++)
	{
		struct bot_ambition const *candidate = &bot_manager_globals.ambitions[index];
		if (!bot_manager_ambition_allowed(candidate, team_index)) continue;
		if (candidate->kind == _bot_ambition_deliver &&
			candidate->carrier_player_index != player_index) continue;
		if ((candidate->kind == _bot_ambition_take || candidate->kind == _bot_ambition_return) &&
			(candidate->object_index == unavailable_object_index ||
			bot_manager_item_claimed_by_other(team_index, candidate->object_index, bot_number))) continue;
		if (!best || candidate->value > best->value) best = candidate;
	}
	if (!best) return FALSE;
	if ((best->kind == _bot_ambition_take || best->kind == _bot_ambition_return) &&
		!bot_manager_claim_item(team_index, best->object_index, bot_number, 4 * TICKS_PER_SECOND))
		return FALSE;
	*goal = *best;
	return TRUE;
}

/* ---------- private code: the game modes' adapters */

/* port: Two teams publish at most four goals each, below the fixed capacity. */
typedef char bot_ctf_goal_capacity_check[BOT_AMBITIONS_MAXIMUM >= 4 * HALO_BOT_CTF_TEAMS ? 1 : -1];

static void bot_manager_ctf_add(short kind, short team, struct halo_bot_ctf_flag const *flag,
	real_point3d const *position, real radius, real value, long carrier)
{
	struct bot_ambition *goal;
	assert(bot_manager_globals.ambition_count < BOT_AMBITIONS_MAXIMUM);
	assert(team >= 0 && team < HALO_BOT_CTF_TEAMS);
	goal = &bot_manager_globals.ambitions[bot_manager_globals.ambition_count++];
	csmemset(goal, 0, sizeof(*goal));
	goal->kind = kind;
	goal->owner_team_index = kind == _bot_ambition_take || kind == _bot_ambition_deliver ? 1 - team : team;
	goal->acting_team_index = team;
	goal->team_mask = 1UL << team;
	goal->object_index = flag ? flag->object_index : NONE;
	goal->carrier_player_index = carrier;
	goal->position = *position;
	goal->radius = radius;
	goal->value = value;
}

/* port: Two flags, at most six transition logs per tick; no carrier positions. */
static void bot_manager_ctf_observe(struct halo_bot_ctf_snapshot const *snapshot)
{
	short team;
	for (team = 0; team < HALO_BOT_CTF_TEAMS; team++)
	{
		struct halo_bot_ctf_flag const *flag = &snapshot->flags[team];
		struct halo_bot_ctf_flag const *previous = &bot_manager_globals.ctf_previous.flags[team];
		if (!bot_manager_globals.ctf_observed || flag->state != previous->state ||
			flag->object_index != previous->object_index ||
			flag->carrier_player_index != previous->carrier_player_index)
			platform_log("bots: CTF flag %d state %d carrier %ld", team, flag->state, flag->carrier_player_index);
		if (bot_manager_globals.ctf_observed &&
			snapshot->scores[team] > bot_manager_globals.ctf_previous.scores[team])
			platform_log("bots: CTF team %d captures %ld", team, snapshot->scores[team]);
		/* Event counters wrap; 255 identifies the engine's automatic return. */
		if (bot_manager_globals.ctf_observed && snapshot->returners[team] != 255 &&
			snapshot->return_events[team] != bot_manager_globals.ctf_previous.return_events[team])
			platform_log("bots: CTF team %d touch return player-slot %u", team,
				(unsigned int)snapshot->returners[team]);
	}
	bot_manager_globals.ctf_previous = *snapshot;
	bot_manager_globals.ctf_observed = TRUE;
}

static void bot_manager_publish_ctf(void)
{
	struct halo_bot_ctf_snapshot snapshot;
	short team;
	if (!game_engine_ctf_bot_snapshot(&snapshot)) return;
	bot_manager_ctf_observe(&snapshot);
	if (!snapshot.can_score) return;
	for (team = 0; team < HALO_BOT_CTF_TEAMS; team++)
	{
		struct halo_bot_ctf_flag const *own = &snapshot.flags[team];
		struct halo_bot_ctf_flag const *enemy = &snapshot.flags[1 - team];
		/* A carrier waits at the stand if the own flag must be returned first.
		   The game's actual player-update predicate, not the bot, awards score. */
		if (enemy->state == _halo_bot_flag_carried)
			bot_manager_ctf_add(_bot_ambition_deliver, team, enemy, &own->stand,
				0.55f, 1.f, enemy->carrier_player_index);
		/* Search the enemy's public scoring stand when our flag is carried.
		   This breaks a two-carrier stalemate without reading hidden positions;
		   actual engagement still requires ordinary perception. */
		if (own->state == _halo_bot_flag_carried)
			bot_manager_ctf_add(_bot_ambition_hunt, team, own, &enemy->stand,
				1.5f, 0.985f, own->carrier_player_index);
		if (own->state == _halo_bot_flag_dropped)
			bot_manager_ctf_add(_bot_ambition_return, team, own, &own->position,
				0.45f, 0.99f, NONE);
		if (enemy->state == _halo_bot_flag_home || enemy->state == _halo_bot_flag_dropped)
			bot_manager_ctf_add(_bot_ambition_take, team, enemy, &enemy->position,
				0.45f, 0.97f, NONE);
		bot_manager_ctf_add(_bot_ambition_guard, team, NULL, &own->stand, 3.f, 0.65f, NONE);
	}
}

/* port: Standard CTF reads the engine's state. Other objective modes keep
bots out until their rules and execution have adapters of their own. */
static void bot_manager_publish_ambitions(
	void)
{
	bot_manager_globals.ambition_count = 0;
	if (!game_engine_get_variant())
		return;
	switch (game_engine_get_variant()->game_engine_index)
	{
	case game_engine_ctf:
		if (config_boolean("bots.ctf")) bot_manager_publish_ctf();
		break;
	case game_engine_slayer:
	default:
		break;
	}
}

#endif /* HALO_FEATURE_BOTS */
