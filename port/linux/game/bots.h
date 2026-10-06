/*
BOTS.H

Multiplayer bots (bots.c): players the host drives itself. In a system link
or internet game they are players of the lobby, on machines of their own
that no connection owns (network_server_manager.c): every machine has them
in the game's settings, and the host gives each its input every tick, as it
takes a client's (player_queues_new.c). The distributed netcode then treats
them as it does the host's own players.

How many, and how good: the settings bots.count and bots.skill
(port_config.c), or the console's bots and bot_skill.
*/

#ifndef __BOTS_H
#define __BOTS_H

struct player_action;

/* how many bots a game this machine hosts has (0: none) */
long bots_configured(void);
/* ... set for the games to come (and this lobby); skill 1 to 5 */
void bots_set_count(long count);
/* ... or how many players they fill a game up to with the people in it,
making way for people who join (0: off) */
long bots_fill(void);
void bots_set_fill(long fill);
void bots_set_skill(long skill);
long bots_get_skill(void);
void bots_set_skill_spread(long spread);
long bots_get_skill_spread(void);
/* a bot's name: a different one for each slot */
char const *bot_name(long slot);
/* a player at this absolute index is a bot from now on: none of the last
one's goals and habits */
void bot_reset(long player_absolute_index);
/* a bot player's input for the tick about to run */
void bot_think(long player_absolute_index, struct player_action *action);
/* a map has loaded, its objects placed: its navigation now, on a machine
that hosts with bots */
void bots_map_loaded(void);
/* the structure BSP the navigation in hand was worked out for (-1: none) */
long bots_navigation_bsp(void);
/* baking the navigation of every multiplayer map, with no window
   (HALO_BAKE_NAVIGATION: bot_bake.c) */
boolean bot_bake_enabled(void);
void bot_bake_update(boolean main_menu_loaded, real seconds);
/* the map is gone: its navigation, and what the bots knew of the game */
void bots_map_disposed(void);
#endif /* __BOTS_H */
