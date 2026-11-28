/*
H2_MEDALS.H

A Halo 2 level's medals and post-game counts (h2_medals.c): Halo 2 shows no
medal in the game - the announcer and the kill lines are the game's -, the
medals are counted at each kill and shown in the postgame carnage report.
Counted on the host (and in a local game) by Halo 2's rules; a joiner of
this project's builds is sent the host's counts in the distributed netcode
(h2_medals_protocol.h), never on the crossplay wire. The report reads them
(carnage_report.c; the Halo 2 menu's own screen, h2_menu.c).
*/

#ifndef __H2_MEDALS_H
#define __H2_MEDALS_H

#include "h2_medals_protocol.h"

/* (game_statistics.c game_statistics_start) a game begins: the counts cleared */
void h2_medals_game_start(void);
/* (game_engine.c game_engine_update) a tick: time alive, who carries a flag */
void h2_medals_update(void);
/* (game_engine.c game_engine_player_killed, a Halo 2 level's) a player's
death: the killer's medals - the chain (multiple_kills, with this kill),
the spree (kills_in_a_row, with it), the kill's kind from the dead unit's
last damage category (units.c's marks: the category in the low byte, bit 8
a stuck grenade's detonation, bit 9 a melee from behind) */
void h2_medals_player_killed(long killing_player_index, long dead_player_index, boolean friendly_fire,
	short multiple_kills, short kills_in_a_row, short marks);
/* (game_engine_ctf.c) a flag taken from its stand or returned */
void h2_medals_flag_grabbed(long player_index);
void h2_medals_flag_returned(long player_index);
/* (units.c unit_enter_seat) a player's unit took a vehicle's boarding seat */
void h2_medals_boarded(long player_index);
/* (game_engine_king.c) a King kill, either player on the hill */
void h2_medals_king_kill(long killing_player_index, long dead_player_index, boolean friendly_fire, boolean killer_on_hill,
	boolean dead_on_hill);
/* (h2_territories.c) a territory's holder changed (NONE: neutral) */
void h2_medals_territory_changed(long previous_player_index, long holder_player_index);

/* whether a Halo 2 level's game is counted (or a joiner has the host's) */
boolean h2_medals_active(void);
/* a player's record (the host's on a joiner), FALSE when none */
boolean h2_medals_record(long player_index, struct h2_medal_record *record);
/* a medal's name: the player's map's (Halo 2's postgame strings, read with
the level's HUD file), else our own plain words */
char const *h2_medals_words(int medal);
/* a postgame string by its string id's name (the map's), or fallback */
char const *h2_medals_word(char const *id, char const *fallback);

/* the distributed netcode (network_distributed.c): the host's entries that
changed (refresh: a few more round them all), all of them to a machine that
has loaded, and a joiner taking the host's */
void h2_medals_network_send(boolean refresh);
void h2_medals_network_send_all(long machine_index);
void h2_medals_network_handle(void const *entries, short count, long machine_index);
unsigned short h2_medals_network_entry_size(void);

#endif
