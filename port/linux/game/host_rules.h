/*
HOST_RULES.H

The host's rules for its players' extras (host_rules.c; docs/delta.md, Host
rules): what a player may turn on beyond the Xbox's game, as the host of
the game allows it.
*/

#ifndef __HOST_RULES_H
#define __HOST_RULES_H

/* (a dedicated server's, from its settings: a player's host reads
config.toml's network.* until this is called) whether its players may show
names above heads */
void host_rules_set_player_names(boolean allowed);
/* ... and whether they may watch a follow camera while they wait to respawn */
void host_rules_set_follow_camera(boolean allowed);

/* ... whether its late joiners wait for the next game (late_join.c) */
void host_rules_set_late_join_hold(boolean wait);
/* whether the host takes new machines (sv_lock, a moderator's LOCK, the
host's own Moderation screen): locked, none join until it is unlocked.
Not saved: a restart unlocks; and it unlocks by itself 10 seconds after
hosting ends or nobody it could matter to is in (host_rules_update) */
void host_rules_set_joins_locked(boolean locked);
boolean host_rules_joins_locked(void);
/* whether anyone a lock is for is in: a dedicated server's players, or a
player's host's players of other machines (FALSE when not hosting) */
boolean host_rules_lock_has_players(void);
/* (the same, plain: browser.c's announcement) */
int host_rules_joins_locked_now(void);

/* the host's own rule (this machine's, whether or not it hosts now) */
boolean host_rules_host_player_names(void);
boolean host_rules_host_follow_camera(void);
boolean host_rules_host_late_join_hold(void);

/* whether this machine's players may show names above other players' heads
(display.player_names): its own rule in a game it hosts or plays alone; its
host's in a game it joined, FALSE if the host said nothing (OpenCE's, an
older build: the Xbox's rule, a name only on the player aimed at) */
boolean host_rules_player_names(void);

/* whether this machine's players may watch a follow camera while they wait
to respawn (display.follow_camera), by the same rule: FALSE where the host
said nothing (the Xbox's death camera alone) */
boolean host_rules_follow_camera(void);

/* whether a player who joins a game in progress waits for the next game
(late_join.c): its own rule in a game it hosts, its host's in a game it
joined (FALSE if the host said nothing) */
boolean host_rules_late_join_hold(void);

/* (a client's) whether its host said its joins are locked */
boolean host_rules_host_says_locked(void);

/* (a client's) its host's team change rule as HOST_RULES said it
(team_change.h's policy): FALSE if the host said none */
boolean host_rules_team_change(int *policy);

/* each frame (main.c): a host says its rules to its Chupathingy clients
over Delta Peer */
void host_rules_update(void);

#endif
