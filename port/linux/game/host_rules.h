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

/* the host's own rule (this machine's, whether or not it hosts now) */
boolean host_rules_host_player_names(void);
boolean host_rules_host_follow_camera(void);

/* whether this machine's players may show names above other players' heads
(display.player_names): its own rule in a game it hosts or plays alone; its
host's in a game it joined, FALSE if the host said nothing (OpenCE's, an
older build: the Xbox's rule, a name only on the player aimed at) */
boolean host_rules_player_names(void);

/* whether this machine's players may watch a follow camera while they wait
to respawn (display.follow_camera), by the same rule: FALSE where the host
said nothing (the Xbox's death camera alone) */
boolean host_rules_follow_camera(void);

/* each frame (main.c): a host says its rules to its Chupathingy clients
over Delta Peer */
void host_rules_update(void);

#endif
