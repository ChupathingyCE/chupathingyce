/*
BOT_MANAGER.H

port: the bots' blackboard (bot_manager.c). The game mode's adapter writes
what matters for scoring (ambitions); bots read it, and write what their team
should know (sightings of enemies). docs/bot_decisions.md has the design.
*/

#ifndef HALO_BOT_MANAGER_H
#define HALO_BOT_MANAGER_H
#pragma once

/* ---------- headers */

#include "cseries/cseries.h"
#include "math/real_math.h"

/* ---------- constants */

enum
{
	BOT_AMBITIONS_MAXIMUM = 16,
	/* team games have two teams; free for all shares nothing */
	BOT_MANAGER_TEAMS = 2,
	/* seats a vehicle's claims cover (bots.c considers no more) */
	BOT_MANAGER_SEATS = 16,
};

/* what a bot may do with an ambition */
enum bot_ambition_kind
{
	_bot_ambition_none = 0,
	_bot_ambition_take, /* pick the object up (a flag, a ball) */
	_bot_ambition_deliver, /* bring a carried object here (a flag stand) */
	_bot_ambition_guard, /* stay near it against enemies (a flag at home) */
	_bot_ambition_hold, /* stand inside it (a hill) */
	_bot_ambition_hunt, /* kill whoever carries it (an enemy flag carrier) */
	_bot_ambition_reach, /* touch it (a race flag) */
	_bot_ambition_return, /* touch one's dropped flag; no inventory acquisition */
	NUMBER_OF_BOT_AMBITION_KINDS
};

/* ---------- structures */

struct bot_ambition
{
	short kind;
	short owner_team_index; /* NONE: nobody's */
	short acting_team_index; /* port: the team assigned this objective duty */
	/* the teams that may use it, a bit for each team; in free for all every
	player is allowed */
	unsigned long team_mask;
	long object_index; /* NONE: an area only */
	long carrier_player_index; /* NONE: not carried */
	real_point3d position;
	real radius;
	/* how much it is worth to the mode, 0 to 1, before the bot's own scoring */
	real value;
};

/* ---------- prototypes/BOT_MANAGER.C */

/* a new map or game: no ambitions, no sightings */
void bot_manager_reset(
	void);

/* each tick before the bots think: old sightings go, and the game mode's
adapter writes the ambitions again */
void bot_manager_update(
	void);

short bot_manager_ambition_count(
	void);

/* port: standard CTF goal selection. One claimant per team pursues each loose
flag; other bots guard. Carriers always deliver, regardless of shared contacts. */
boolean bot_manager_ctf_goal(long team_index, long player_index, short bot_number,
	long unavailable_object_index, struct bot_ambition *goal);

/* NULL past the count */
struct bot_ambition const *bot_manager_ambition_get(
	short ambition_index);

/* whether a player of the team may use the ambition */
boolean bot_manager_ambition_allowed(
	struct bot_ambition const *ambition,
	long team_index);

/* port: a sighting by a callout-capable bot or a local human (reporter 0);
ignored outside team games and for teams out of range. No hidden information. */
void bot_manager_report_sighting(
	long team_index,
	long enemy_player_index,
	short reporter_bot_number,
	real_point3d const *position);

/* the team's freshest sighting nearest to the point, no older than
maximum_age_ticks, of an enemy still alive; FALSE if none */
boolean bot_manager_nearest_sighting(
	long team_index,
	real_point3d const *from,
	long maximum_age_ticks,
	long *enemy_player_index,
	real_point3d *position);

/* Item claims (engine_ai/claims.h): one bot per team at a time goes for a
weapon or powerup. Opposing teams use independent scopes, and claims never
stop a human from racing the bot to an item. FFA bots share one scope. */
boolean bot_manager_claim_item(
	long team_index,
	long object_index,
	short bot_number,
	long duration_ticks);

boolean bot_manager_item_claimed_by_other(
	long team_index,
	long object_index,
	short bot_number);

void bot_manager_release_item(
	long team_index,
	long object_index,
	short bot_number);

/* Vehicle seat claims: one bot per team heads for each seat (below
BOT_MANAGER_SEATS), so a team's bots do not all run for one driver's seat.
As with items, a claim never stops a human taking the seat. */
boolean bot_manager_claim_seat(
	long team_index,
	long vehicle_index,
	short seat_index,
	short bot_number,
	long duration_ticks);

boolean bot_manager_seat_claimed_by_other(
	long team_index,
	long vehicle_index,
	short seat_index,
	short bot_number);

/* whether a teammate bot is on its way to any of the vehicle's seats */
boolean bot_manager_vehicle_claimed_by_other(
	long team_index,
	long vehicle_index,
	short bot_number);

void bot_manager_release_seat(
	long team_index,
	long vehicle_index,
	short seat_index,
	short bot_number);

/* every item and seat claim the bot holds */
void bot_manager_release_bot(
	long team_index,
	short bot_number);

#endif /* HALO_BOT_MANAGER_H */
