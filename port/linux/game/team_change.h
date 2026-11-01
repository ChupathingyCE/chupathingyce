/*
TEAM_CHANGE.H

Changing teams during a team game (team_change.c).
*/

#ifndef __TEAM_CHANGE_H
#define __TEAM_CHANGE_H

/* the host's rule for its players' own changes */
enum team_change_policy
{
	/* none: only an admin's sv_team moves a player */
	_team_change_off = 0,
	/* only while the teams stay within the margin of each other */
	_team_change_balanced,
	/* any */
	_team_change_allow,
};

/* (the host's) its rule and margin (a dedicated server's settings; a
player's host reads network.team_change and network.team_change_margin
until this is called) */
void team_change_host_set(int policy, int margin);
int team_change_host_policy(void);
int team_change_host_margin(void);

/* the pause menu's CHANGE TEAMS: the local player's ask to be put on the
other team, of the host (this machine's own rule, or over Delta Peer), and
what came of it shown to them. FALSE if it could not be asked */
boolean team_change_ask(short local_player_index);

/* whether a game map's pause menu has CHANGE TEAMS: a multiplayer game
with teams (not co-op), unless display.pause_change_teams is off */
boolean team_change_pause_offered(void);

/* (the host's) an admin moves the player in the slot of the game's player
list to a team (0 red, 1 blue), whatever the rule: enum delta_team_result,
and what happened in text */
int team_change_move(long list_index, short team, char *text, int size);

/* (the host's, from game_engine_player_killed) whether the player's death
is a team change's (nobody's kill, nothing counted, no penalty); and, for
every death, the team change waiting for it made */
boolean team_change_death(long dead_player_index);
void team_change_player_died(long dead_player_index);

/* (the host's, from a round-based game type's next round: game_engine_bomb.c)
a change waiting for the player's death is made now: the host kills their
unit as nobody's kill, and they respawn at once on the new team. TRUE if
so (the player is not sent home) */
boolean team_change_round_started(long player_index);

/* each frame (main.c): the host offers team changes over Delta Peer, and a
client shows what its host answered */
void team_change_update(void);

#endif
