/*
COMPETITIVE.H

Match rules and the competitive display options (competitive.c): no bloom,
the start countdown, item respawns, the host watching, and this machine's
own scoreboard, death camera and colors. Every one is off unless set: the
retail game's behavior stays the default.
*/

#ifndef __COMPETITIVE_H
#define __COMPETITIVE_H

/* the host's rules (a dedicated server's settings; a player's host reads
network.no_bloom, network.start_countdown, network.item_respawn and
network.host_spectates until this is called): countdown and item respawn in
seconds, 0 for none */
void competitive_host_set(int no_bloom, int countdown, int item_respawn, int host_spectates);
void competitive_host_get(int *no_bloom, int *countdown, int *item_respawn, int *host_spectates);

/* each frame (main.c): the host works out the rules its game plays by and
sends them over Delta Peer; a client takes its host's, and each tells its
player what they are */
void competitive_update(void);

/* (weapons.c) whether a shot of this weapon (its definition) is fired with
no bloom: the pistol, and the sniper rifle unzoomed, while the game plays
no bloom */
boolean competitive_no_bloom(long weapon_definition_index, boolean zoomed);

/* (player_control.c) whether the start countdown holds this machine's
players still now; (damage.c) whether the host deals no damage now (the
countdown's) */
boolean competitive_countdown_frozen(void);
boolean competitive_damage_blocked(void);

/* (game_engine.c, the host's) the ticks between weapon and powerup
respawns, 0 for the map's own */
long competitive_item_respawn_ticks(void);

/* (game_engine.c, director.c, follow_camera.c) whether the player is one of
this host's own that watch and never spawn */
boolean competitive_player_spectates(long player_index);

/* (game_engine.c) whether the scoreboard shows for the local player without
its button held (display.auto_scoreboard) */
boolean competitive_scoreboard_shown(short local_player_index);

/* (dead_camera.c) whether the death camera moves on to other players
(display.death_camera: retail's, unless "body") */
boolean competitive_death_camera_switches(void);

/* (render_objects.c) the change colors to draw an object with: its own, or
red for another player's unit in a game without teams
(display.simple_colors) */
real_rgb_color const *competitive_change_colors(long object_index, real_rgb_color const *colors);

/* (hud.c) the start countdown, over the local player's view */
void competitive_draw(short local_player_index);

#endif
