/*
BOTS.H

port: computer-controlled multiplayer players (bots.c).
*/

#ifndef HALO_GAME_BOTS_H
#define HALO_GAME_BOTS_H
#pragma once

#include "cseries/cseries.h" /* port: standalone lobby/controller interface */

/* ---------- prototypes/BOTS.C */

/* port: override config for the next map only. Counts sum to at most 31;
   skill is recruit (0), marine (1), ODST (2), Spartan (3). */
boolean bots_prepare_local_match(short friendly_count, short enemy_count, short skill);

/* a new map: no bots, and the map's navigation graph to build again */
void bots_initialize_for_new_map(
	void);

/* each game tick, before the players' actions are taken (players.c): the
bots join or leave, then each decides the action the host sends for it in its
next update, as a local player's input is sent */
void bots_update(
	void);

/* whether a bot has played in this map's game: its results are not reported
to the game list or Delta Stats */
boolean bots_game_had_bots(
	void);

/* port: effective demand includes a private lobby override, not just config. */
boolean bots_navigation_requested(void);

/* Host-local policy override. Base owns action/aim/reaction parameters;
   active owns perception/movement/selection. IDs are recruit 0..Spartan 3.
   Invalid IDs or a non-bot player return FALSE without changing state. */
boolean bots_set_player_difficulty(long player_index, short base, short active);

/* whether the player is one of this machine's bots */
boolean bots_player_is_bot(
	long player_index);

#endif /* HALO_GAME_BOTS_H */
