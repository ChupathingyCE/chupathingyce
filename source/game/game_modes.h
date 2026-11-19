/*
GAME_MODES.H

The game modes layer (docs/modes.md): a rule set laid over a game engine, as
the gametype's further options say (the 16 bytes past the PC options,
network_rules.h: a mode byte, Hardcore, and each mode's own). The modes are
OpenCE's game types, played by their version 35 rules so that a game means
one thing on every machine: SWAT (no shields, a headshot kills) and Hardcore
(no motion sensor, no aim assist, a kill restores the killer) in this
build; Gun Game and Infection to come.

The options reach the game as the network game's variant does
(network_game_manager.c: the host's own, a client's from its host's record)
and are read here by everything that plays by them: the variant's overrides
at the game's start, the damage and kill rules, the HUD and the aim. With
no mode and no Hardcore, nothing here changes the game.
*/

#ifndef __HALO_GAME_MODES_H
#define __HALO_GAME_MODES_H

#ifdef HALO_GAME_BROWSER

struct game_variant;
struct game_variant_options;

/* the further options' size (network_rules.h's NETWORK_RULES_EXTENDED_OPTIONS_SIZE) */
#define GAME_MODE_OPTIONS_SIZE 16

/* the game's further options: set as its variant is (NULL: the defaults,
a game without them), sanitized; and what they are now */
void game_mode_options_set(byte const *options);
byte const *game_mode_options_get(void);
/* the further options of a game type that has none of its own */
void game_mode_options_default(byte *options);

/* the mode the game plays now (NETWORK_RULES_MODE_*: SWAT on the Slayer
engine; none on any other, or with no options), and whether Hardcore is on
(any engine but Bomb) */
long game_mode_active(void);
boolean game_mode_hardcore(void);
/* the modes a game of the variant and options needs of every machine
(NETWORK_RULES_MODES_* bits; network_rules_modes_needed) */
unsigned long game_mode_needed(struct game_variant const *variant, byte const *options);
/* the kind byte of a listing (version 37) for the variant and options */
long game_mode_kind(struct game_variant const *variant, byte const *options);

/* the variant's forced settings at the game's start, on every machine
(SWAT: no shields), on the game's own copy of its variant */
void game_mode_variant_overrides(struct game_variant *variant);

/* SWAT (the host): whether a shot of the damage category on a player's
head kills, as if the damage could cause headshots */
boolean game_mode_swat_headshot(short damage_category);
/* Hardcore (every machine): no motion sensor, and no aim assist (neither
autoaim nor magnetism) for any input device */
boolean game_mode_hides_motion_sensor(void);
boolean game_mode_no_aim_assist(void);
/* Hardcore (the host): a player's kill of another restores the killer
(game_engine_player_killed) */
void game_mode_player_killed(long killing_player_index, long dead_player_index, boolean friendly_fire);

/* the built-in game types of the layer (SWAT, Hardcore, HC CTF, HC Oddball,
HC King: the names game_engine_get_variant_by_name knows, "swat",
"hardcore", "hc_ctf", "hc_oddball", "hc_king"), in their order after the
Bomb game types: each one's PC options (the variant's defaults, and SWAT's
loadout) and further options */
#define NUMBER_OF_GAME_MODE_BUILTINS 5
struct game_mode_builtin
{
	char const *name;
	wchar_t const *title;
	struct game_variant *(*build)(struct game_variant *variant);
	/* (the mode and hardcore of its further options) */
	byte mode;
	byte hardcore;
};
struct game_mode_builtin const *game_mode_builtin(long index);
/* the built-in of a name (its index), or NONE */
long game_mode_builtin_named(char const *name);
/* a built-in's PC options and further options */
void game_mode_builtin_options(long index, struct game_variant const *variant, struct game_variant_options *options,
	byte *modes);
/* the further options a built-in's name gives (the defaults for a name
that is none of the layer's) */
void game_mode_options_of_builtin(char const *name, byte *modes);

#endif

#endif
