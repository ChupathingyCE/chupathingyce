/*
GAME_ENGINE.H

header included in hcex build.
*/

#ifndef __GAME_ENGINE_H
#define __GAME_ENGINE_H
#pragma once

#include "math/real_math.h"

/* ---------- constants */

enum
{
	_game_engine_disable_dynamic_light_bit = 0,
	_game_engine_disable_integrated_lights_bit,
	_game_engine_5_or_more_players_bit,
};

enum
{
	_game_variant_draw_object_in_motion_sensor_bit = 0,
	_game_variant_allow_friendly_navpoints_bit,
	_game_variant_infinite_grenades_bit,
	_game_variant_no_shields_bit,
	_game_variant_always_invisible_bit,
	/* (the starting equipment: generic, else the map's) */
	_game_variant_generic_starting_equipment_bit,
};

enum game_engine_type
{
	game_engine_none = 0,
	game_engine_ctf,
	game_engine_slayer,
	game_engine_oddball,
	game_engine_king,
	game_engine_race,
	/* port: Bomb (game_engine_bomb.c), in the slot Bungie's cut terminator
	game type had; played only between ChupathingyCE machines that agree on
	Delta Peer's bomb capability (docs/bomb.md) */
	game_engine_bomb,
	game_engine_stub,
	number_of_game_engines,
	first_usable_game_engine_index = game_engine_ctf,
	last_usable_game_engine_index = game_engine_race,
};

enum get_score_type
{
	_get_score_individual = 0,
	_get_score_team,
};

/* ---------- macros */

/* ---------- structures */

struct game_globals;
struct player_starting_location;
struct weapon_datum;
union real_argb_color;
union real_point2d;
union real_point3d;
union real_rgb_color;

struct universal_variant
{
	boolean teams;
	byte pad0;
	byte pad1;
	byte pad2;
	unsigned long flags;
	long goal_radar;
	boolean odd_man_out;
	byte pad4;
	byte pad5;
	byte pad6;
	long respawn_time_growth;
	long respawn_time;
	long suicide_penalty;
	long lives;
	real health;
	long score_to_win;
	long weapon_set;
	long vehicle_set;
};

struct ctf_variant
{
	boolean assault;
	boolean reset_on_capture;
	boolean flag_must_reset;
	boolean flag_at_home_to_score;
	long single_flag_time;
};

struct slayer_variant
{
	boolean no_death_bonus;
	boolean no_kill_penalty;
	boolean kill_in_order;
};

struct king_variant
{
	boolean moving_hill;
};

/* the balls of an oddball game */
enum
{
	MAXIMUM_ODDBALLS = 16,
};

struct oddball_variant
{
	boolean random_start;
	boolean ball_spawn_delay;
	long speed_with_ball;
	long trait_with_ball;
	long trait_without_ball;
	long oddball_ball_type;
	long ball_spawn_count;
};

struct race_variant
{
	long race_type;
	long team_scoring;
};

/* port: Bomb's rules (game_engine_bomb.c) */
enum
{
	/* a goal starts a new round: every player respawns and the bomb resets */
	_bomb_new_round_on_score_bit = 0,
	/* melee only: no starting weapons, none on the map, no grenades or
	powerups; a map's player-usable energy sword, if it has one, in hand
	(game_variant_options_default) */
	_bomb_melee_only_bit,
};

/* port: the player traits a Bomb gametype starts with
(game_engine_traits_default) */
enum
{
	_bomb_traits_none = 0,
	_bomb_traits_grifball,
	/* Grifball's, everyone faster (175%), the carrier faster still (200%):
	Open Pitch, for a big map */
	_bomb_traits_open_pitch,
	NUMBER_OF_BOMB_TRAITS
};

struct bomb_variant
{
	/* seconds the carrier stands in the goal to score; 0: on contact */
	long arm_time;
	/* seconds a dropped bomb lies before it resets; 0: never */
	long reset_time;
	/* seconds a round lasts (it ends without a score); 0: no limit */
	long round_time;
	/* _bomb_*_bit */
	byte flags;
	/* _bomb_traits_* */
	byte traits;
	byte pad[10];
};

union game_engine_variant
{
	struct ctf_variant ctf;
	struct slayer_variant slayer;
	struct king_variant king;
	struct oddball_variant oddball;
	struct race_variant race;
	struct bomb_variant bomb;
};

typedef char verify_game_engine_variant_size[sizeof(union game_engine_variant) == 0x18 ? 1 : -1];

struct game_variant
{
	wchar_t human_readable_game_description[12];
	long game_engine_index;
	struct universal_variant universal_variant;
	union game_engine_variant game_engine_variant;
	word flags;
};

typedef char verify_game_variant_size[sizeof(struct game_variant) == 0x68 ? 1 : -1];

/* port: the PC version's gametype options that the Xbox's variant has not
(its gametype editor's, port/linux/game/menu_functions.c). A saved gametype
keeps them after its signature (playlist_profile.c), the network game
carries them (network_game_manager.h), and the game plays by them
(game_variant_options_get). */
enum
{
	_friendly_fire_on = 0,
	_friendly_fire_off,
	_friendly_fire_shields_only,
	_friendly_fire_explosives_only,
	NUMBER_OF_FRIENDLY_FIRE_MODES
};

enum
{
	/* other players on the motion tracker: all (the variant's draw object
	in motion sensor bit set), friends only, none */
	_radar_players_all = 0,
	_radar_players_friends,
	_radar_players_none,
	NUMBER_OF_RADAR_PLAYERS
};

enum
{
	_loadout_category = 0,
	_loadout_custom,
	NUMBER_OF_LOADOUTS
};

enum
{
	_loadout_weapon_none = 0,
	_loadout_weapon_random,
	_loadout_weapon_assault_rifle,
	_loadout_weapon_pistol,
	_loadout_weapon_shotgun,
	_loadout_weapon_sniper_rifle,
	_loadout_weapon_rocket_launcher,
	_loadout_weapon_plasma_pistol,
	_loadout_weapon_plasma_rifle,
	_loadout_weapon_needler,
	NUMBER_OF_LOADOUT_WEAPONS
};

enum
{
	_variant_vehicle_warthog = 0,
	_variant_vehicle_ghost,
	_variant_vehicle_scorpion,
	_variant_vehicle_rocket_warthog,
	_variant_vehicle_banshee,
	_variant_vehicle_gun_turret,
	NUMBER_OF_VARIANT_VEHICLES,
	MAXIMUM_VARIANT_VEHICLE_COUNT = 4,
	/* a team's vehicles: those of a vehicle set (universal_variant's
	vehicle_set values), else every one the map places, as Halo PC places
	them (PC), else its counts */
	VARIANT_VEHICLE_SET_PC = 0xFE,
	VARIANT_VEHICLE_SET_CUSTOM = 0xFF
};

/* (the starting equipment, generic or the map's, is the variant's own:
_game_variant_generic_starting_equipment_bit) */
struct game_variant_options
{
	/* minutes; 0: none */
	short time_limit;
	short friendly_fire;
	/* seconds added to a team killer's respawn */
	short friendly_fire_penalty;
	/* seconds; 0: never (the Xbox game's) */
	short vehicle_respawn_time;
	boolean auto_team_balance;
	byte radar_players;
	/* red's and blue's (free for all: red's) */
	byte vehicle_set[2];
	byte vehicle_counts[2][NUMBER_OF_VARIANT_VEHICLES];
	/* the weapons: the weapon set's (category), else each player starts
	with these (custom: _loadout_weapon_none, _random, or a weapon) */
	byte loadout;
	byte primary_weapon;
	byte secondary_weapon;
	/* no weapons spawn on the map (its grenades and powerups do): for
	a loadout of none, melee only */
	boolean no_map_weapons;
};

typedef char verify_game_variant_options_size[sizeof(struct game_variant_options) == 0x1C ? 1 : -1];

struct game_engine
{
	char const *name;
	long type;
	void (*dispose)(void);
	boolean (*initialize_for_new_map)(void);
	void (*dispose_from_old_map)(void);
	void (*player_added)(long player_index);
	void (*game_ending)(void);
	void (*game_starting)(void);
	void (*statistics_append)(long statistic);
	void (*handle_client_message)(void *message);
	void (*handle_server_message)(void *message);
	void (*pregame_post_rasterize)(void);
	void (*post_rasterize_objects)(void);
	void (*player_update_each_tick)(
		long player_index);
	void (*objective_weapon_update)(
		long item_index,
		struct weapon_datum *weapon);
	boolean (*picking_up)(
		long weapon_index,
		long player_index);
	void (*weapon_dropped)(
		long weapon_index);
	void (*update)(void);
	long (*get_player_score)(
		long player_index,
		enum get_score_type score_type);
	wchar_t *(*format_player_score)(
		long player_index,
		wchar_t *string);
	wchar_t *(*format_score_name)(
		wchar_t *string);
	wchar_t *(*format_team_name)(
		long team_index,
		wchar_t *string);
	boolean (*allow_pick_up)(
		long unit_index,
		long weapon_index);
	void (*player_damaged_player)(
		long damaging_player_index,
		long dead_player_index,
		boolean damage_type);
	void (*player_killed_player)(
		long killing_player_index,
		long killing_object_index,
		long dead_player_index,
		boolean friendly_fire);
	boolean (*format_message)(
		long player_index,
		long message,
		long message_data,
		wchar_t *buffer,
		long buffer_size);
	real (*starting_location_rating)(
		long player_index,
		struct player_starting_location const *starting_location);
	void (*prespawn_player_update)(
		long player_index);
	void (*player_update)(
		long player_index);
	void (*team_index_override)(void);
	boolean (*player_can_see_goal)(
		long player_index,
		long goal_index);
	boolean (*test_flag)(
		long flag);
	boolean (*test_trait)(
		long trait,
		long value);
	long (*did_player_win)(
		long player_index);
};
#ifndef HALO_64BIT

typedef char verify_game_engine_size[sizeof(struct game_engine) == 0x88 ? 1 : -1];
typedef char verify_game_engine_player_update_each_tick_offset[
	offsetof(struct game_engine, player_update_each_tick) == 0x34 ? 1 : -1];
typedef char verify_game_engine_objective_weapon_update_offset[
	offsetof(struct game_engine, objective_weapon_update) == 0x38 ? 1 : -1];
typedef char verify_game_engine_format_player_score_offset[
	offsetof(struct game_engine, format_player_score) == 0x4C ? 1 : -1];
typedef char verify_game_engine_format_score_name_offset[
	offsetof(struct game_engine, format_score_name) == 0x50 ? 1 : -1];
typedef char verify_game_engine_format_team_name_offset[
	offsetof(struct game_engine, format_team_name) == 0x54 ? 1 : -1];
typedef char verify_game_engine_format_message_offset[
	offsetof(struct game_engine, format_message) == 0x64 ? 1 : -1];
typedef char verify_game_engine_player_update_offset[
	offsetof(struct game_engine, player_update) == 0x70 ? 1 : -1];
#endif

/* port: player traits (docs/bomb.md, "Player traits"): percents of a
player's health, of the damage it deals and resists, and of its speed (100
changes nothing), and its shields. A Bomb game type plays by them: every
player's, and its bomb carrier's on top; its host sends them over Delta
Peer (TRAITS: delta_wire.h has the same ranges) */
enum
{
	PLAYER_TRAIT_HEALTH_MINIMUM = 10,
	PLAYER_TRAIT_HEALTH_MAXIMUM = 400,
	PLAYER_TRAIT_DAMAGE_MINIMUM = 25,
	PLAYER_TRAIT_DAMAGE_MAXIMUM = 400,
	PLAYER_TRAIT_RESISTANCE_MINIMUM = 25,
	PLAYER_TRAIT_RESISTANCE_MAXIMUM = 400,
	PLAYER_TRAIT_SPEED_MINIMUM = 50,
	PLAYER_TRAIT_SPEED_MAXIMUM = 200,
	PLAYER_TRAIT_JUMP_MINIMUM = 50,
	PLAYER_TRAIT_JUMP_MAXIMUM = 200,
	PLAYER_TRAIT_GRAVITY_MINIMUM = 25,
	PLAYER_TRAIT_GRAVITY_MAXIMUM = 200,
	PLAYER_TRAIT_MELEE_MINIMUM = 50,
	PLAYER_TRAIT_MELEE_MAXIMUM = 200,
};

enum
{
	/* the variant's (its no shields option) */
	_trait_shields_unchanged = 0,
	_trait_shields_none,
	_trait_shields_normal,
	/* charged past full, as the overshield powerup charges them (three
	times), and charged again whenever they drain */
	_trait_shields_overshield,
	NUMBER_OF_TRAIT_SHIELDS
};

struct player_traits
{
	short health;
	short damage;
	short resistance;
	short speed;
	/* _trait_shields_* */
	byte shields;
	/* drawn in a bright color of its own */
	byte highlight;
	byte pad[2];
	/* of its jump's speed, of gravity on it in the air, and of its melee
	blow's reach: only for players whose machine has Delta Peer's
	bomb_movement (game_engine_traits_jump_multiplier) */
	short jump;
	short gravity;
	short melee;
	short pad2;
};

struct game_traits
{
	struct player_traits everyone;
	struct player_traits carrier;
};

#ifdef HALO_GAME_BROWSER
/* (game_engine_bomb.c) a variant's traits (none but a Bomb game type's), and
traits kept in their ranges */
void game_engine_traits_default(struct game_variant const *variant, struct game_traits *traits);
void game_engine_traits_clean(struct game_traits *traits);
/* whether the game plays by traits now (a Bomb game type) */
boolean game_engine_traits_active(void);
/* the traits' damage multiplier (1 when none), and whether a player has
shields by them (NONE: the game type's own rule) */
real game_engine_traits_damage_multiplier(long damaging_player_index, long damaged_player_index);
long game_engine_traits_shields(long player_index);
/* the traits' jump, gravity and melee reach multipliers of a player (1 when
none, or when its machine or the host lacks them: it moves and strikes as
it always did) */
real game_engine_traits_jump_multiplier(long player_index);
real game_engine_traits_gravity_multiplier(long player_index);
real game_engine_traits_melee_multiplier(long player_index);
/* the host's: its game's traits, for Delta Peer to send (none for a game
that is no Bomb game type) */
void game_engine_bomb_host_traits(struct game_variant const *variant);
/* a melee-only Bomb game: and the map's energy sword in the unit's hands,
if the map has one players can use */
boolean game_engine_bomb_melee_only(void);
void game_engine_bomb_give_sword(long unit_index);
/* whether a weapon is a melee-only game's unseen fists (the pistol, given
where the map has no sword): not drawn, no HUD of its own, can't shoot */
boolean game_engine_bomb_hides_weapon(long definition_index);
/* a player's view's Bomb HUD (game_engine.c draws it after the HUD): the
round's clock and each team's rounds, the planted bomb's countdown, and the
carrier's arming */
void game_engine_bomb_rasterize(long player_index);
/* whether the map's scripts are off: a Bomb game on a court with a game of
its own (game_engine_bomb.c, bomb_courts) */
boolean game_engine_bomb_scripts_off(void);
/* whether a map's placed weapon is placed: a Bomb game on a court places
none of the court's own ball (the bomb takes its place) */
struct scenario_object_datum;
boolean game_engine_bomb_weapon_placement_allowed(struct scenario_object_datum const *placement,
	struct tag_block *palette);
/* whether a weapon's pickup line is not shown (the bomb's: the game's own
message says who has it) */
boolean game_engine_bomb_hides_pickup(long weapon_definition_index);
/* the game's report's Bomb members (JSON, without braces, into text): a
player's line's, and the game's; their length, 0 for none (another game
type) */
long game_engine_bomb_report_player(long player_index, char *text, long size);
long game_engine_bomb_report_game(char *text, long size);
/* a player's carry seconds, carrier kills and kills as carrier (the event
log's); FALSE when no Bomb game is running */
boolean game_engine_bomb_player_statistics(long player_index, long *carry_seconds, long *carrier_kills,
	long *kills_as_carrier);
/* the network test's (network_test.c): the bomb, a team's goal, a team's
score, the round, and the goal the bomb is planted in, arming (the host's;
NONE: none) */
long game_engine_bomb_weapon(void);
long game_engine_bomb_planted_goal(void);
boolean game_engine_bomb_goal(long team_index, union real_point3d *position);
long game_engine_bomb_team_score(long team_index);
long game_engine_bomb_round(void);
/* a goal's countdown and blast: whether a death counts nothing (the scoring
team's own in the blast) and when its player respawns, the countdown's
second now (0: none), and whether no one spawns (the blast's pause) */
boolean game_engine_bomb_silent_death(long killing_player_index, long dead_player_index, boolean friendly_fire);
long game_engine_bomb_silent_respawn_ticks(void);
long game_engine_bomb_countdown(void);
boolean game_engine_bomb_holds_spawns(void);
/* whether the game is played in rounds (a Bomb game type): a team change
waits for the player's next respawn or the next round (team_change.c) */
boolean game_engine_bomb_rounds(void);
/* whether a landing hurts: not in Grifball (one hit kills, and a fall would
be one); falling for too long still kills */
boolean game_engine_bomb_landing_damage(void);
/* a unit struck by a melee blow that didn't kill it: a carrier's fumble */
void game_engine_bomb_unit_struck(long unit_index);
#endif

/* port: the most a game type's state for the distributed netcode's clients
may take (port/linux/game/network_distributed.c's MAXIMUM_GAME_STATE_SIZE,
less the postgame state game_engine_write_network_state puts first) */
#define GAME_ENGINE_MAXIMUM_NETWORK_STATE_SIZE (0xF00 - 4)

/* ---------- prototypes/GAME_ENGINE.C */

/* port: the gametype's vehicles of each team (game_variant_options): the
map's placement of a vehicle allowed, by its type and the nearer team's
spawn points (counted from game_engine_vehicle_placement_begin) */
struct scenario_object_datum;
struct tag_block;
void game_engine_vehicle_placement_begin(
	void);
#ifdef HALO_CUSTOM_EDITION
/* port: whether a Halo PC map's vehicles are chosen by their placements'
spawn flags in this game, and whether a placement is (game_engine.c) */
boolean game_engine_ce_vehicles_by_placement(void);
boolean game_engine_ce_vehicle_placement_allowed(struct scenario_object_datum const *placement);
#endif
boolean game_engine_vehicle_placement_allowed(
	struct scenario_object_datum const *placement,
	struct tag_block *palette);
/* port: the local player whose motion sensor is drawn (FRIENDS radar) */
void game_engine_motion_sensor_viewer(
	short local_player_index);
/* port: the gametype's friendly fire on a teammate's hit of the object */
enum
{
	_friendly_damage_all = 0,
	_friendly_damage_none,
	_friendly_damage_shields
};
short game_engine_friendly_damage(
	long attacker_player_index,
	long object_index,
	boolean explosive);
/* port: the PC options of a variant that has none of its own (saved before
them, or built in): the Xbox game's play */
void game_variant_options_default(
	struct game_variant const *variant,
	struct game_variant_options *options);
void game_engine_playlist_initialize(
	void);

long game_globals_get_weapon(
	struct game_globals *game_globals,
	long weapon_list_index);

void game_engine_playlist_begin(
	void);

boolean game_engine_get_current_stage(
	struct game_variant *variant,
	char *map_name);

long list_index_to_weapon_definition_index(
	long weapon_list_index);

long weapon_definition_index_to_list_index(
	long weapon_definition_index);

void game_engine_state_message(
	long player_index,
	long state_message,
	long state_message_player_index);

void game_engine_player_depower_active_camo(
	long player_index);

long game_engine_get_team_score(
	long team_index);

long players_in_game(
	void);

real get_blink_alpha(
	void);

long game_engine_player_get_team_index(
	long player_index);

void game_engine_update_player_always_invis(
	long player_index);

boolean game_engine_player_has_flag(
	long player_index);

void game_show_score(
	long player_index,
	long score);

void get_postgame_hilite_colors(
	union real_argb_color *winner_color,
	union real_argb_color *normal_color,
	union real_argb_color *hilite_color);

boolean game_engine_running(
	void);
boolean game_engine_showing_postgame(
	void);

boolean game_engine_get_state_message(
	long player_index,
	wchar_t *message,
	long maximum_length);

boolean game_engine_force_single_screen(
	void);

void game_engine_dispose(
	void);

void game_engine_dispose_from_old_map(
	void);

void game_engine_game_ending(
	void);

void game_engine_game_starting(
	void);

void game_engine_statistics_append(
	long statistic);

void game_engine_handle_client_message(
	void *message);

void game_engine_handle_server_message(
	void *message);

void game_engine_post_rasterize_objects(
	void);

void game_engine_post_rasterize(
	void);

void game_engine_nonplayer_post_rasterize(
	void);
void game_engine_update(
	void);

void game_engine_update_non_deterministic(
	real delta_seconds);

boolean match_game_type(
	long game_type,
	long count,
	short const *game_types);
void game_engine_flag_reset(
	long weapon_index,
	union real_point3d const *position);

void game_engine_initialize(
	struct game_variant *variant);

void game_engine_initialize_for_new_map(
	void);
void game_engine_player_added(
	long player_index);

real game_engine_get_distance_rating_for_spawn(
	long player_index,
	union real_point3d const *position);

void game_engine_variant_cleanup(
	struct game_variant *variant);

boolean game_engine_display_team_indicators(
	void);

boolean game_engine_can_score(
	void);

real game_engine_get_starting_location_rating(
	long player_index,
	struct player_starting_location const *starting_location);
boolean game_engine_should_spawn_player(
	long player_index);
void game_engine_client_respawn_countdown(
	long player_index);
void game_engine_postspawn_player_update(
	long player_index);

boolean game_engine_allow_pick_up(
	long unit_index,
	long weapon_index);

boolean game_engine_picking_up(
	long unit_index,
	long weapon_index);

boolean game_engine_test_flag(
	long flag);

boolean game_engine_test_trait(
	long trait,
	long value);

void game_engine_prespawn_player_update(
	long player_index);

long game_engine_did_player_win(
	long player_index);
long game_engine_did_player_win_default(
	long player_index);

struct game_variant *game_engine_get_variant(
	void);

struct game_variant *game_engine_get_variant_by_name(
	struct game_variant *variant,
	char const *name);

boolean game_engine_get_goal_in_use(
	short goal_index);
real_point3d *game_engine_get_goal_position(
	real_point3d *position,
	short goal_index);

void game_engine_set_goal_position(
	short goal_index,
	union real_point3d const *position,
	real height,
	char const *name,
	long player_index,
	short team_index,
	long ignore_player_index);

boolean game_engine_has_teams(
	void);

boolean game_engine_allow_pause(
	void);

boolean game_engine_allow_dynamic_lighting(
	void);

boolean game_engine_infinite_grenades(
	long player_index);

boolean game_engine_has_shield(
	long player_index);

boolean game_engine_draw_object_in_motion_sensor(
	long unit_index);

boolean game_engine_hud_draw_motion_sensor(
	long player_index);

boolean game_engine_player_has_stealth_weapon(
	long player_index);

void game_engine_weapon_fired(
	long player_index);

short game_engine_player_get_custom_motion_sensor_positions(
	long player_index,
	union real_point2d *positions,
	byte *goal_indices,
	short maximum_count);

void game_engine_render_nav_points(
	short local_player_index);

union real_rgb_color *game_engine_player_get_change_color(
	union real_rgb_color *change_color,
	long player_index);

long find_netgame_flags(
	union real_point3d const *position,
	real radius,
	real height,
	short type,
	short team_index,
	long maximum_count,
	long *flag_indices);

long find_netgame_flag(
	union real_point3d const *position,
	real radius,
	real height,
	short type,
	short team_index);

boolean game_engine_should_end_game(
	void);

void game_engine_clear_goal_position(
	short goal_index);

long get_flag_definition_index(
	void);

long get_ball_definition_index(
	void);

void game_engine_override_map_name(
	char const *map_name);

void game_engine_override_game_variant(
	struct game_variant const *variant);

void game_engine_switch_to_postgame(
	void);

void game_engine_load_stage(
	char const *map_name);

void game_engine_end_game(
	void);

void ticks_to_unicode_time_string(
	long ticks,
	unsigned long character_count,
	wchar_t *string);

void game_engine_player_damaged_player(
	long damaging_player_index,
	long dead_player_index,
	boolean damage_type);

boolean game_engine_player_is_out_of_lives(
	long player_index);

boolean game_engine_man_out(
	long player_index);

boolean game_engine_hud_draw_messages(
	long player_index);

boolean game_engine_force_autopickup(
	long unit_index,
	long weapon_index);

void game_engine_play_multiplayer_sound(
	long sound_index);
#ifdef HALO_GAME_BROWSER
boolean game_engine_multiplayer_sound_exists(
	long sound_index);
#endif
void game_engine_update_multiplayer_sound(
	void);
void game_engine_intialize_queued_sounds(
	void);

long game_engine_remap_object_definition(long definition_index);

long game_engine_remap_vehicle(long vehicle_definition_index);
long game_engine_remap_equipment(long equipment_definition_index);
void game_show_score_team(
	long team_index,
	long score);
void game_show_score_you_ally_enemy(
	long player_index,
	long you_score,
	long ally_score,
	long enemy_score,
	long other_player_index);
void game_show_score_extended(
	long player_index,
	long score,
	long team_index);
long game_engine_remap_weapon(long weapon_definition_index);


boolean game_engine_allow_integrated_lights(
	long object_index);

void game_engine_player_killed(
	long killing_player_index,
	long killing_object_index,
	long dead_player_index,
	boolean friendly_fire);

/* ---------- globals */

extern struct game_engine *game_engine;

/* ---------- public code */

real game_engine_get_damage_multiplier(
	long damaging_player_index,
	long damaged_player_index);

#ifdef HALO_64BIT
long game_engine_did_player_win_default(
	long player_index);

void game_show_score_extended(
	long player_index,
	long score,
	long team_index);

#endif
#ifdef HALO_BIG_ENDIAN
/* port: a big-endian console (port/linux/game/network_distributed.h): the
game types' states the host sends (game_engine_write_network_state) are
little-endian on the wire, as the PCs and the original Xbox send them. Each
game type's turns its own between this machine's order and the wire's
(either way: their layouts are fixed), a state of another size left as it
is (its read refuses it). */
void distributed_wire_swap(void *data, long count, long size);
/* the fields from first to last of the state, numbers of size bytes each
with no padding between them */
#define GAME_ENGINE_NETWORK_STATE_SWAP(state, first, last, size) \
	distributed_wire_swap(&(state)->first, \
		(long)(((byte *)&(state)->last + sizeof((state)->last) - (byte *)&(state)->first) / (size)), (size))
void game_engine_slayer_network_state_byte_swap(byte *buffer, long size);
void game_engine_ctf_network_state_byte_swap(byte *buffer, long size);
void game_engine_oddball_network_state_byte_swap(byte *buffer, long size);
void game_engine_king_network_state_byte_swap(byte *buffer, long size);
void game_engine_race_network_state_byte_swap(byte *buffer, long size);
void game_engine_bomb_network_state_byte_swap(byte *buffer, long size);
#endif

#endif // __GAME_ENGINE_H
