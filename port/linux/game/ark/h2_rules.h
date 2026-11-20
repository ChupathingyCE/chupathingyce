/*
H2_RULES.H

port: Halo 2's multiplayer rules on a Halo 2 level (h2_rules.c): where a
player spawns (its influences), when items and vehicles come back, what a
player starts with, the base Slayer variant.
*/

#ifndef __H2_RULES_H
#define __H2_RULES_H
#pragma once

struct game_variant;
struct scenario_object_datum;

/* a level loaded (cache_files.c): a Halo 2 one's spawn influences, zones
and defaults read from its map and shared.map */
void h2_rules_map_loaded(char const *map_name);

/* a Halo 2 level is loaded (its scenario named "h2\...") */
boolean h2_rules_level(void);
/* (perf/ce-floor-2) h2_rules_level_fast: h2_level_fast.h */
#include "h2_level_fast.h"

/* (players.c find_best_starting_location_index) the starting location a
player spawns at by Halo 2's influences: TRUE and the index (NONE: none
free this tick), FALSE when not a Halo 2 level's game */
boolean h2_rules_spawn_location(long player_index, long *location_index);

/* (game_engine.c game_engine_update_item_spawn) the netgame items and the
vehicles' respawn by Halo 2's rules: TRUE when done (a Halo 2 level) */
boolean h2_rules_update_items(void);

/* (game_engine.c game_engine_update) a Halo 2 level's teleporters for
the tick, by Halo 2's rules (teleporters.cpp): each key's first entrance
and exit, the nearest player standing within half a unit of an entrance
and riding nothing sent, a teleporter resting its recharge time after,
a filled exit holding the player with its message every 4 seconds (no
telefrag); FALSE when not a Halo 2 level's game (this engine's) */
boolean h2_rules_update_teleporters(void);

/* (game_engine.c game_engine_postspawn_player_update) the grenades a
player starts with, before the starting equipment's flags: Halo 2's
limit (2 frags, 1 in a game of 9 or more players) and no plasma; FALSE
when not a Halo 2 level */
boolean h2_rules_starting_grenades(boolean nine_or_more_players, long *fragmentation, long *plasma);

/* (game_engine.c game_engine_vehicle_placement_allowed) whether a Halo 2
level's vehicle placement is placed in the running game type */
boolean h2_rules_vehicle_placement_allowed(struct scenario_object_datum const *placement);

/* (game_engine.c game_engine_initialize_for_new_map) a Halo 2 level's
stock game type (Slayer, Team Slayer, King, Crazy King, Team King,
Oddball, Team Oddball, Juggernaut, CTF) as MCC's default Halo 2 variant of
it: its score to win, respawn and suicide penalty */
void h2_rules_variant_defaults(struct game_variant *variant);
/* the time limit in minutes a game plays to: the options' (MCC's default
Halo 2 variants have none) */
short h2_rules_time_limit(short options_time_limit);
/* (game_engine_ctf.c) the ticks a dropped CTF flag lies before it goes
home: a Halo 2 level's MCC flag reset time (30 s), else ticks */
long h2_rules_ctf_flag_return_ticks(long ticks);

/* (h2_vehicle_wrecks.c) the ticks after a vehicle of this definition at
its home died that a new one comes: its placement's Halo 2 spawn time,
else ticks (the default) */
long h2_rules_vehicle_respawn_ticks(long definition_index, real_point3d const *home, long ticks);
/* (h2_vehicle_wrecks.c) whether Halo 2 keeps the dead vehicle a second
longer as its timer runs out: someone sits in it, or a player stands near */
boolean h2_rules_vehicle_respawn_held(long vehicle_index);
/* (hud_messaging.c) a Halo 2 level's printed HUD messages' colour and
up and fade times, its HUD globals' messaging parameters; FALSE where
none are known */
boolean h2_rules_hud_message_style(real_argb_color *color, real *up_seconds, real *fade_seconds);

/* (bots.c) the tick a Halo 2 level's netgame item (by its index in the
scenario's netgame equipment) next comes: its timer's (started when the
last was picked up or moved, Halo 2's rule, not the CE engine's every
multiple of its period); NONE while none is due (the last still lies
there, or its timer has yet to start). FALSE when not a Halo 2 level's game */
boolean h2_rules_item_next_tick(long equipment_index, long *tick);

#endif
