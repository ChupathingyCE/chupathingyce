/*
GAME_MODES.C

The game modes layer (game_modes.h, docs/modes.md): what the gametype's
further options make of a game, on every machine alike.

The rules are OpenCE's version 35 rules, written from our own
specification of them (nothing of theirs was read for this), so that our
players join their SWAT and Hardcore games and their players join ours, and
one game means the same on both. SWAT and Hardcore were first made by
OpenCE's contributors; the credit is in docs/modes.md.

Where each rule lives:
- the variant's overrides at the game's start: game_mode_variant_overrides,
  from network_game_manager.c as the game's variant is set;
- SWAT's headshots: objects/damage.c, as damage is dealt (the host alone
  deals damage);
- Hardcore's motion sensor: game_engine.c's HUD gates;
- Hardcore's aim: aim_assist.c (the controller) and
  port/linux/game/network_distributed.c (the mouse, with the host's policy);
- Hardcore's restore: game_engine_player_killed.
*/

/* ---------- headers */

#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "game_engine.h"
#include "game_engine_playlist.h"
#include "game.h"
#include "players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "networking/network_game_globals.h"
#include "../../port/linux/game/network_rules.h"

#include "game_modes.h"

#ifdef HALO_GAME_BROWSER

/* ---------- globals */

/* the game's further options, as the network game gave them (the defaults
until it does) */
static byte game_mode_options[GAME_MODE_OPTIONS_SIZE] = { 0 };
static boolean game_mode_options_given = FALSE;

/* ---------- private code */

static byte const *options_now(void)
{
	if (!game_mode_options_given)
	{
		network_rules_extended_options_default(game_mode_options);
		game_mode_options_given = TRUE;
	}
	return game_mode_options;
}

/* the modes the game plays now, by its engine's own variant */
static unsigned needed_now(void)
{
	struct game_variant const *variant = game_engine_get_variant();

	if (!variant || !variant->game_engine_index)
		return 0;
	return network_rules_modes_needed(options_now(), variant->game_engine_index, variant->universal_variant.teams != 0);
}

/* the host of the game: the game's server, or a game played alone */
static boolean host(void)
{
	return !network_game_distributed_client();
}

/* ---------- public code */

void game_mode_options_set(
	byte const *options)
{
	if (options)
		csmemcpy(game_mode_options, options, GAME_MODE_OPTIONS_SIZE);
	else
		network_rules_extended_options_default(game_mode_options);
	network_rules_extended_options_sanitize(game_mode_options);
	game_mode_options_given = TRUE;
}

byte const *game_mode_options_get(
	void)
{
	return options_now();
}

void game_mode_options_default(
	byte *options)
{
	network_rules_extended_options_default(options);
}

long game_mode_active(
	void)
{
	int hardcore;

	return network_rules_modes_mode(needed_now(), &hardcore);
}

boolean game_mode_hardcore(
	void)
{
	return (needed_now() & NETWORK_RULES_MODES_HARDCORE) != 0;
}

unsigned long game_mode_needed(
	struct game_variant const *variant,
	byte const *options)
{
	byte sanitized[GAME_MODE_OPTIONS_SIZE];

	if (!variant || !variant->game_engine_index)
		return 0;
	if (options)
		csmemcpy(sanitized, options, sizeof(sanitized));
	else
		network_rules_extended_options_default(sanitized);
	network_rules_extended_options_sanitize(sanitized);
	return network_rules_modes_needed(sanitized, variant->game_engine_index, variant->universal_variant.teams != 0);
}

long game_mode_kind(
	struct game_variant const *variant,
	byte const *options)
{
	int hardcore;
	int mode = network_rules_modes_mode((unsigned)game_mode_needed(variant, options), &hardcore);

	return network_rules_kind_byte(mode, hardcore);
}

void game_mode_variant_overrides(
	struct game_variant *variant)
{
	unsigned needed;

	if (!variant || !variant->game_engine_index)
		return;
	needed = network_rules_modes_needed(options_now(), variant->game_engine_index,
		variant->universal_variant.teams != 0);
	/* SWAT: no shields, whatever the variant says */
	if (needed & NETWORK_RULES_MODES_SWAT)
		SET_FLAG(variant->universal_variant.flags, _game_variant_no_shields_bit, TRUE);
}

boolean game_mode_swat_headshot(
	short damage_category)
{
	/* (the categories are objects/damage.c's own: it asks whether SWAT
	plays and tests them itself; this says whether SWAT's rule is this
	machine's to apply) */
	(void)damage_category;
	return host() && game_engine_running() && game_mode_active() == NETWORK_RULES_MODE_SWAT;
}

boolean game_mode_hides_motion_sensor(
	void)
{
	return game_mode_hardcore();
}

boolean game_mode_no_aim_assist(
	void)
{
	return game_engine_running() && game_mode_hardcore();
}

void game_mode_player_killed(
	long killing_player_index,
	long dead_player_index,
	boolean friendly_fire)
{
	struct player_datum *killer;
	struct unit_datum *unit;

	if (!host() || !game_mode_hardcore() || friendly_fire || killing_player_index == NONE ||
		killing_player_index == dead_player_index || dead_player_index == NONE)
	{
		return;
	}
	killer = player_try_and_get(killing_player_index);
	if (!killer || killer->unit_index == NONE)
		return;
	unit = unit_try_and_get(killer->unit_index);
	if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return;
	/* the killer's body to full; shields to full when below it, no longer
	depleted or charging (an overshield past full is kept) */
	unit->object.body_vitality = unit->object.maximum_body_vitality > 0.0f ? 1.0f : unit->object.body_vitality;
	if (unit->object.maximum_shield_vitality > 0.0f && unit->object.shield_vitality < 1.0f)
	{
		unit->object.shield_vitality = 1.0f;
		SET_FLAG(unit->object.damage_flags, _object_shield_depleted_bit, FALSE);
		SET_FLAG(unit->object.damage_flags, _object_shield_charging_bit, FALSE);
	}
}

/* ---------- the built-ins */

static struct game_mode_builtin const game_mode_builtins[NUMBER_OF_GAME_MODE_BUILTINS] =
{
	{ "swat", L"SWAT", build_game_variant_swat, NETWORK_RULES_MODE_SWAT, 0 },
	{ "hardcore", L"Hardcore", build_game_variant_hardcore, NETWORK_RULES_MODE_NONE, 1 },
	{ "hc_ctf", L"HC CTF", build_game_variant_hc_ctf, NETWORK_RULES_MODE_NONE, 1 },
	{ "hc_oddball", L"HC Oddball", build_game_variant_hc_oddball, NETWORK_RULES_MODE_NONE, 1 },
	{ "hc_king", L"HC King", build_game_variant_hc_king, NETWORK_RULES_MODE_NONE, 1 },
};

struct game_mode_builtin const *game_mode_builtin(
	long index)
{
	if (index < 0 || index >= NUMBER_OF_GAME_MODE_BUILTINS)
		return NULL;
	return &game_mode_builtins[index];
}

long game_mode_builtin_named(
	char const *name)
{
	long index;

	if (!name)
		return NONE;
	for (index = 0; index < NUMBER_OF_GAME_MODE_BUILTINS; index++)
	{
		if (!csstrcmp(name, game_mode_builtins[index].name))
			return index;
	}
	return NONE;
}

void game_mode_builtin_options(
	long index,
	struct game_variant const *variant,
	struct game_variant_options *options,
	byte *modes)
{
	struct game_mode_builtin const *builtin = game_mode_builtin(index);

	game_variant_options_default(variant, options);
	network_rules_extended_options_default(modes);
	if (!builtin)
		return;
	network_rules_mode_options_builtin(builtin->mode, builtin->hardcore, modes);
	/* SWAT: each player starts with a pistol and an assault rifle, and
	the map places no weapons; nobody on the motion tracker */
	if (builtin->mode == NETWORK_RULES_MODE_SWAT)
	{
		options->loadout = _loadout_custom;
		options->primary_weapon = _loadout_weapon_pistol;
		options->secondary_weapon = _loadout_weapon_assault_rifle;
		options->no_map_weapons = TRUE;
		options->radar_players = _radar_players_none;
	}
	/* Hardcore: friendly fire on, nobody on the motion tracker (which is
	not drawn) */
	if (builtin->hardcore)
	{
		options->friendly_fire = _friendly_fire_on;
		options->radar_players = _radar_players_none;
	}
}

void game_mode_options_of_builtin(
	char const *name,
	byte *modes)
{
	long index = game_mode_builtin_named(name);

	network_rules_extended_options_default(modes);
	if (index != NONE)
		network_rules_mode_options_builtin(game_mode_builtins[index].mode, game_mode_builtins[index].hardcore, modes);
}

#endif /* HALO_GAME_BROWSER */
