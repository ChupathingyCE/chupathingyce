/*
SERVER_GAMETYPE_VARIANT.C

Game type files as the game's gametypes, and back (server_gametype_variant.h).
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "game/game_engine.h"
#include "server_config.h"
#include "server_gametype_variant.h"

#include <string.h>

/* ---------- constants */

enum
{
	/* (the game's: game_time.h) */
	GAMETYPE_TICKS_PER_SECOND = 30,
};

/* ---------- private code */

static void set_variant_flag(
	struct game_variant *variant,
	long bit,
	boolean value)
{
	SET_FLAG(variant->universal_variant.flags, bit, value);
}

/* whether a setting's file can hold a value: a whole number brought into
its range, a boolean made 0 or 1, a choice only if it is one */
static boolean field_holds(
	short field,
	long *value)
{
	struct server_gametype_field const *description = &server_gametype_fields[field];
	int index;

	switch (description->kind)
	{
	case SERVER_FIELD_BOOLEAN:
		*value = *value ? 1 : 0;
		return TRUE;
	case SERVER_FIELD_INTEGER:
		*value = PIN(*value, description->minimum, description->maximum);
		return TRUE;
	case SERVER_FIELD_CHOICE:
		for (index = 0; index < description->choice_count; index++)
		{
			if (description->choices[index].value == *value)
				return TRUE;
		}
		return FALSE;
	}
	return FALSE;
}

/* ---------- public code */

void server_gametype_variant_apply(
	struct server_gametype const *gametype,
	struct game_variant *variant,
	struct game_variant_options *options)
{
	struct universal_variant *universal = &variant->universal_variant;
	union game_engine_variant *engine = &variant->game_engine_variant;
	short field;

	for (field = 0; field < SERVER_GAMETYPE_FIELDS; field++)
	{
		long value = gametype->values[field];

		if (!gametype->set[field])
			continue;
		switch (field)
		{
		case SERVER_GAMETYPE_SCORE_LIMIT: universal->score_to_win = value; break;
		case SERVER_GAMETYPE_TIME_LIMIT: options->time_limit = (short)value; break;
		case SERVER_GAMETYPE_TEAMS: universal->teams = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_LIVES: universal->lives = value; break;
		case SERVER_GAMETYPE_HEALTH: universal->health = (real)value / 100.0f; break;
		case SERVER_GAMETYPE_SHIELDS: set_variant_flag(variant, _game_variant_no_shields_bit, !value); break;
		case SERVER_GAMETYPE_RESPAWN_TIME: universal->respawn_time = value * GAMETYPE_TICKS_PER_SECOND; break;
		case SERVER_GAMETYPE_RESPAWN_GROWTH: universal->respawn_time_growth = value * GAMETYPE_TICKS_PER_SECOND; break;
		case SERVER_GAMETYPE_SUICIDE_PENALTY: universal->suicide_penalty = value * GAMETYPE_TICKS_PER_SECOND; break;
		case SERVER_GAMETYPE_ODD_MAN_OUT: universal->odd_man_out = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_INVISIBLE: set_variant_flag(variant, _game_variant_always_invisible_bit, value != 0); break;
		case SERVER_GAMETYPE_WEAPON_SET: universal->weapon_set = value; break;
		case SERVER_GAMETYPE_STARTING_EQUIPMENT:
			set_variant_flag(variant, _game_variant_generic_starting_equipment_bit, value != 0);
			break;
		case SERVER_GAMETYPE_MAP_WEAPONS: options->no_map_weapons = value ? FALSE : TRUE; break;
		case SERVER_GAMETYPE_INFINITE_GRENADES:
			set_variant_flag(variant, _game_variant_infinite_grenades_bit, value != 0);
			break;
		case SERVER_GAMETYPE_LOADOUT: options->loadout = (byte)value; break;
		case SERVER_GAMETYPE_PRIMARY_WEAPON: options->primary_weapon = (byte)value; break;
		case SERVER_GAMETYPE_SECONDARY_WEAPON: options->secondary_weapon = (byte)value; break;
		case SERVER_GAMETYPE_OBJECTIVES: universal->goal_radar = value; break;
		case SERVER_GAMETYPE_PLAYERS_ON_RADAR:
			/* (and the Xbox's flag: other players on the tracker or not) */
			options->radar_players = (byte)value;
			set_variant_flag(variant, _game_variant_draw_object_in_motion_sensor_bit, value != _radar_players_none);
			break;
		case SERVER_GAMETYPE_FRIENDS_ON_SCREEN:
			set_variant_flag(variant, _game_variant_allow_friendly_navpoints_bit, value != 0);
			break;
		case SERVER_GAMETYPE_FRIENDLY_FIRE: options->friendly_fire = (short)value; break;
		case SERVER_GAMETYPE_FRIENDLY_FIRE_PENALTY: options->friendly_fire_penalty = (short)value; break;
		case SERVER_GAMETYPE_AUTO_BALANCE: options->auto_team_balance = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_VEHICLE_RESPAWN: options->vehicle_respawn_time = (short)value; break;
		case SERVER_GAMETYPE_RED_VEHICLES: options->vehicle_set[0] = (byte)value; break;
		case SERVER_GAMETYPE_BLUE_VEHICLES: options->vehicle_set[1] = (byte)value; break;
		case SERVER_GAMETYPE_CTF_ASSAULT: engine->ctf.assault = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_CTF_SINGLE_FLAG_TIME:
			engine->ctf.single_flag_time = value * GAMETYPE_TICKS_PER_SECOND;
			break;
		case SERVER_GAMETYPE_CTF_FLAG_MUST_RESET: engine->ctf.flag_must_reset = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_CTF_FLAG_AT_HOME_TO_SCORE: engine->ctf.flag_at_home_to_score = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_CTF_RESET_ON_CAPTURE: engine->ctf.reset_on_capture = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_SLAYER_DEATH_BONUS: engine->slayer.no_death_bonus = value ? FALSE : TRUE; break;
		case SERVER_GAMETYPE_SLAYER_KILL_PENALTY: engine->slayer.no_kill_penalty = value ? FALSE : TRUE; break;
		case SERVER_GAMETYPE_SLAYER_KILL_IN_ORDER: engine->slayer.kill_in_order = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_KING_MOVING_HILL: engine->king.moving_hill = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_ODDBALL_RANDOM_START: engine->oddball.random_start = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_ODDBALL_SPAWN_DELAY: engine->oddball.ball_spawn_delay = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_ODDBALL_SPEED_WITH_BALL: engine->oddball.speed_with_ball = value; break;
		case SERVER_GAMETYPE_ODDBALL_TRAIT_WITH_BALL: engine->oddball.trait_with_ball = value; break;
		case SERVER_GAMETYPE_ODDBALL_TRAIT_WITHOUT_BALL: engine->oddball.trait_without_ball = value; break;
		case SERVER_GAMETYPE_ODDBALL_BALL_TYPE: engine->oddball.oddball_ball_type = value; break;
		case SERVER_GAMETYPE_ODDBALL_BALLS: engine->oddball.ball_spawn_count = value; break;
		case SERVER_GAMETYPE_RACE_TYPE: engine->race.race_type = value; break;
		case SERVER_GAMETYPE_RACE_TEAM_SCORING: engine->race.team_scoring = value; break;
		case SERVER_GAMETYPE_BOMB_ARM_TIME: engine->bomb.arm_time = value; break;
		case SERVER_GAMETYPE_BOMB_RESET_TIME: engine->bomb.reset_time = value; break;
		case SERVER_GAMETYPE_BOMB_ROUND_TIME: engine->bomb.round_time = value; break;
		case SERVER_GAMETYPE_BOMB_NEW_ROUND_ON_SCORE:
			SET_FLAG(engine->bomb.flags, _bomb_new_round_on_score_bit, value != 0);
			break;
		case SERVER_GAMETYPE_BOMB_MELEE_ONLY:
			SET_FLAG(engine->bomb.flags, _bomb_melee_only_bit, value != 0);
			/* (and its loadout: none, and no weapons on the map) */
			if (value)
			{
				options->loadout = _loadout_custom;
				options->primary_weapon = _loadout_weapon_none;
				options->secondary_weapon = _loadout_weapon_none;
				options->no_map_weapons = TRUE;
			}
			break;
		case SERVER_GAMETYPE_BOMB_TRAITS: engine->bomb.traits = (byte)value; break;
		default:
			if (field >= SERVER_GAMETYPE_RED_WARTHOG && field <= SERVER_GAMETYPE_RED_GUN_TURRET)
				options->vehicle_counts[0][field - SERVER_GAMETYPE_RED_WARTHOG] = (byte)value;
			else if (field >= SERVER_GAMETYPE_BLUE_WARTHOG && field <= SERVER_GAMETYPE_BLUE_GUN_TURRET)
				options->vehicle_counts[1][field - SERVER_GAMETYPE_BLUE_WARTHOG] = (byte)value;
			break;
		}
	}
	/* (the Xbox's vehicle set: red's, if it is one of its sets, as the
	editor keeps it) */
	universal->vehicle_set = options->vehicle_set[0] <= 4 ? options->vehicle_set[0] : 0;
	/* its name in the game (else its base's) */
	if (gametype->set[SERVER_GAMETYPE_NAME])
	{
		long index;

		for (index = 0; index < (long)NUMBEROF(variant->human_readable_game_description) - 1 && gametype->title[index];
			index++)
		{
			variant->human_readable_game_description[index] = (wchar_t)(unsigned char)gametype->title[index];
		}
		for (; index < (long)NUMBEROF(variant->human_readable_game_description); index++)
			variant->human_readable_game_description[index] = 0;
	}
}

long server_gametype_variant_value(
	short field,
	struct game_variant const *variant,
	struct game_variant_options const *options)
{
	struct universal_variant const *universal = &variant->universal_variant;
	union game_engine_variant const *engine = &variant->game_engine_variant;
	unsigned long flags = universal->flags;

	switch (field)
	{
	case SERVER_GAMETYPE_SCORE_LIMIT: return universal->score_to_win;
	case SERVER_GAMETYPE_TIME_LIMIT: return options->time_limit;
	case SERVER_GAMETYPE_TEAMS: return universal->teams != 0;
	case SERVER_GAMETYPE_LIVES: return universal->lives;
	case SERVER_GAMETYPE_HEALTH: return (long)(universal->health * 100.0f + 0.5f);
	case SERVER_GAMETYPE_SHIELDS: return !TEST_FLAG(flags, _game_variant_no_shields_bit);
	case SERVER_GAMETYPE_RESPAWN_TIME: return universal->respawn_time / GAMETYPE_TICKS_PER_SECOND;
	case SERVER_GAMETYPE_RESPAWN_GROWTH: return universal->respawn_time_growth / GAMETYPE_TICKS_PER_SECOND;
	case SERVER_GAMETYPE_SUICIDE_PENALTY: return universal->suicide_penalty / GAMETYPE_TICKS_PER_SECOND;
	case SERVER_GAMETYPE_ODD_MAN_OUT: return universal->odd_man_out != 0;
	case SERVER_GAMETYPE_INVISIBLE: return TEST_FLAG(flags, _game_variant_always_invisible_bit);
	case SERVER_GAMETYPE_WEAPON_SET: return universal->weapon_set;
	case SERVER_GAMETYPE_STARTING_EQUIPMENT: return TEST_FLAG(flags, _game_variant_generic_starting_equipment_bit);
	case SERVER_GAMETYPE_MAP_WEAPONS: return !options->no_map_weapons;
	case SERVER_GAMETYPE_INFINITE_GRENADES: return TEST_FLAG(flags, _game_variant_infinite_grenades_bit);
	case SERVER_GAMETYPE_LOADOUT: return options->loadout;
	case SERVER_GAMETYPE_PRIMARY_WEAPON: return options->primary_weapon;
	case SERVER_GAMETYPE_SECONDARY_WEAPON: return options->secondary_weapon;
	case SERVER_GAMETYPE_OBJECTIVES: return universal->goal_radar;
	case SERVER_GAMETYPE_PLAYERS_ON_RADAR: return options->radar_players;
	case SERVER_GAMETYPE_FRIENDS_ON_SCREEN: return TEST_FLAG(flags, _game_variant_allow_friendly_navpoints_bit);
	case SERVER_GAMETYPE_FRIENDLY_FIRE: return options->friendly_fire;
	case SERVER_GAMETYPE_FRIENDLY_FIRE_PENALTY: return options->friendly_fire_penalty;
	case SERVER_GAMETYPE_AUTO_BALANCE: return options->auto_team_balance != 0;
	case SERVER_GAMETYPE_VEHICLE_RESPAWN: return options->vehicle_respawn_time;
	case SERVER_GAMETYPE_RED_VEHICLES: return options->vehicle_set[0];
	case SERVER_GAMETYPE_BLUE_VEHICLES: return options->vehicle_set[1];
	case SERVER_GAMETYPE_CTF_ASSAULT: return engine->ctf.assault != 0;
	case SERVER_GAMETYPE_CTF_SINGLE_FLAG_TIME: return engine->ctf.single_flag_time / GAMETYPE_TICKS_PER_SECOND;
	case SERVER_GAMETYPE_CTF_FLAG_MUST_RESET: return engine->ctf.flag_must_reset != 0;
	case SERVER_GAMETYPE_CTF_FLAG_AT_HOME_TO_SCORE: return engine->ctf.flag_at_home_to_score != 0;
	case SERVER_GAMETYPE_CTF_RESET_ON_CAPTURE: return engine->ctf.reset_on_capture != 0;
	case SERVER_GAMETYPE_SLAYER_DEATH_BONUS: return !engine->slayer.no_death_bonus;
	case SERVER_GAMETYPE_SLAYER_KILL_PENALTY: return !engine->slayer.no_kill_penalty;
	case SERVER_GAMETYPE_SLAYER_KILL_IN_ORDER: return engine->slayer.kill_in_order != 0;
	case SERVER_GAMETYPE_KING_MOVING_HILL: return engine->king.moving_hill != 0;
	case SERVER_GAMETYPE_ODDBALL_RANDOM_START: return engine->oddball.random_start != 0;
	case SERVER_GAMETYPE_ODDBALL_SPAWN_DELAY: return engine->oddball.ball_spawn_delay != 0;
	case SERVER_GAMETYPE_ODDBALL_SPEED_WITH_BALL: return engine->oddball.speed_with_ball;
	case SERVER_GAMETYPE_ODDBALL_TRAIT_WITH_BALL: return engine->oddball.trait_with_ball;
	case SERVER_GAMETYPE_ODDBALL_TRAIT_WITHOUT_BALL: return engine->oddball.trait_without_ball;
	case SERVER_GAMETYPE_ODDBALL_BALL_TYPE: return engine->oddball.oddball_ball_type;
	case SERVER_GAMETYPE_ODDBALL_BALLS: return engine->oddball.ball_spawn_count;
	case SERVER_GAMETYPE_RACE_TYPE: return engine->race.race_type;
	case SERVER_GAMETYPE_RACE_TEAM_SCORING: return engine->race.team_scoring;
	case SERVER_GAMETYPE_BOMB_ARM_TIME: return engine->bomb.arm_time;
	case SERVER_GAMETYPE_BOMB_RESET_TIME: return engine->bomb.reset_time;
	case SERVER_GAMETYPE_BOMB_ROUND_TIME: return engine->bomb.round_time;
	case SERVER_GAMETYPE_BOMB_NEW_ROUND_ON_SCORE: return TEST_FLAG(engine->bomb.flags, _bomb_new_round_on_score_bit) != 0;
	case SERVER_GAMETYPE_BOMB_MELEE_ONLY: return TEST_FLAG(engine->bomb.flags, _bomb_melee_only_bit) != 0;
	case SERVER_GAMETYPE_BOMB_TRAITS: return engine->bomb.traits;
	}
	if (field >= SERVER_GAMETYPE_RED_WARTHOG && field <= SERVER_GAMETYPE_RED_GUN_TURRET)
		return options->vehicle_counts[0][field - SERVER_GAMETYPE_RED_WARTHOG];
	if (field >= SERVER_GAMETYPE_BLUE_WARTHOG && field <= SERVER_GAMETYPE_BLUE_GUN_TURRET)
		return options->vehicle_counts[1][field - SERVER_GAMETYPE_BLUE_WARTHOG];
	return 0;
}

void server_gametype_variant_build(
	struct server_gametype const *gametype,
	struct game_variant *variant,
	struct game_variant_options *options)
{
	int base = gametype->values[SERVER_GAMETYPE_BASE];

	if (base < 0 || base >= server_builtin_count)
		base = 0;
	game_engine_get_variant_by_name(variant, server_builtins[base].name);
	game_variant_options_default(variant, options);
	server_gametype_variant_apply(gametype, variant, options);
}

int server_gametype_variant_base(
	struct game_variant const *variant)
{
	int first = -1;
	int index;

	for (index = 0; index < server_builtin_count; index++)
	{
		struct game_variant base;

		if (server_builtins[index].engine != variant->game_engine_index)
			continue;
		if (first < 0)
			first = index;
		game_engine_get_variant_by_name(&base, server_builtins[index].name);
		if (!base.universal_variant.teams == !variant->universal_variant.teams)
			return index;
	}
	return first < 0 ? 0 : first;
}

void server_gametype_variant_capture(
	struct game_variant const *variant,
	struct game_variant_options const *options,
	char const *title,
	struct server_gametype *gametype)
{
	struct game_variant base_variant;
	struct game_variant_options base_options;
	int base = server_gametype_variant_base(variant);
	int engine = server_builtins[base].engine;
	short field;
	long index;

	csmemset(gametype, 0, sizeof(*gametype));
	gametype->values[SERVER_GAMETYPE_BASE] = base;
	gametype->set[SERVER_GAMETYPE_BASE] = TRUE;
	/* its name: printable ASCII, the rest dropped */
	for (index = 0; index < SERVER_GAMETYPE_TITLE_SIZE - 1; index++)
	{
		long character = title ? (unsigned char)title[index] : variant->human_readable_game_description[index];

		if (!character)
			break;
		gametype->title[index] = character >= 0x20 && character <= 0x7E ? (char)character : '?';
	}
	gametype->title[index] = 0;
	gametype->set[SERVER_GAMETYPE_NAME] = gametype->title[0] != 0;
	game_engine_get_variant_by_name(&base_variant, server_builtins[base].name);
	game_variant_options_default(&base_variant, &base_options);
	for (field = SERVER_GAMETYPE_SCORE_LIMIT; field < SERVER_GAMETYPE_FIELDS; field++)
	{
		long value;

		if (server_gametype_fields[field].engine && server_gametype_fields[field].engine != engine)
			continue;
		value = server_gametype_variant_value(field, variant, options);
		if (value == server_gametype_variant_value(field, &base_variant, &base_options) || !field_holds(field, &value))
			continue;
		/* (capture the flag is always played in teams) */
		if (field == SERVER_GAMETYPE_TEAMS && engine == SERVER_ENGINE_CTF)
			continue;
		gametype->values[field] = (int)value;
		gametype->set[field] = TRUE;
	}
}

#endif
