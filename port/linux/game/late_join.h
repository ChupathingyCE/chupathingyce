/*
LATE_JOIN.H

Late joiners waiting for the next game (late_join.c; docs/delta.md, Host
rules): a host option, off by default.
*/

#ifndef __LATE_JOIN_H
#define __LATE_JOIN_H

#ifdef HALO_GAME_BROWSER

/* (the host's: network_game_manager.c, as a player is made for the game)
a player added to a game in progress, so held until the next */
void late_join_player_added(
	long player_index);

/* (the host's: game_engine_should_spawn_player) whether a player waits for
the next game: they joined this one in progress, the host's late joiners
wait, and someone else is playing */
boolean late_join_holds(
	long player_index);

/* (any machine's: director.c, follow_camera.c, hud.c) whether a local
player is waiting for the next game, as this machine sees it: no unit yet,
no death, a while into the game, and the host's rule on */
boolean late_join_waiting(
	long player_index);

#else

/* (a build without the game list and the server, a console's: no host
rules there, so nobody waits; late_join.c builds nothing) */
#define late_join_player_added(player_index) ((void)(player_index))
#define late_join_holds(player_index) ((void)(player_index), FALSE)
#define late_join_waiting(player_index) ((void)(player_index), FALSE)

#endif

#endif
