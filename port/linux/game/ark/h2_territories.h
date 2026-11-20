/*
H2_TERRITORIES.H

Halo 2's Territories (h2_territories.c): the calls of the port's own files
past game_engine.h's (the Halo 2 rules', the menus', the network tests').
*/

#ifndef HALO_H2_TERRITORIES_H
#define HALO_H2_TERRITORIES_H

#include "game/game_engine.h"
#include "math/real_math.h"

struct h2_cache;

/* (h2_rules_map_loaded) a Halo 2 level's territories, from its scenario's
netgame flags (type 10, territory i: identifier i); none for another map */
void h2_territories_map_loaded(
	struct h2_cache *cache,
	char const *map_name);
void h2_territories_map_cleared(
	void);

/* the built-in game types: MCC's "3 Plots" (team), and the same free for all */
struct game_variant *build_game_variant_territories(
	struct game_variant *variant);
struct game_variant *build_game_variant_ffa_territories(
	struct game_variant *variant);

/* (h2_hud.c) the territories in play for the HUD's bars: each one's
holder, the contender of a neutral one, its bar's fraction; the player's
own capture progress (his charge): the count, 0 if not a Territories game */
long h2_territories_hud(
	long player_index,
	long *holders,
	long *contenders,
	real *fractions,
	long maximum,
	real *charge);

/* (h2_rules.c, a player's spawn) the middles of the territories the
player's side holds; how many */
long h2_territories_held(
	long player_index,
	real_point3d *centers,
	long maximum);

#endif
