/*
PARTY_TEAMS.H

Keeping a party on one team in team games this machine hosts
(party_teams.c; docs/delta.md, Party teams).
*/

#ifndef __PARTY_TEAMS_H
#define __PARTY_TEAMS_H

struct network_game;

/* (the host's) whether it keeps parties together: a dedicated server's
setting (keep_parties_together, off by default); a player's host reads
network.keep_parties_together (on) until this is called */
void party_teams_host_set(boolean keep);
boolean party_teams_host_on(void);

/* (the host's, from the pregame lobby's tasks: network_server_manager.c)
the lobby's teams brought together where a party is split, once each time
its players or their parties change: TRUE if a team changed (the lobby is
sent again) */
boolean party_teams_pregame(struct network_game *game);

/* each frame (main.c): in a team game this machine hosts, a party member
who joined late and has not spawned yet goes to the party's team, where the
teams stay within one of each other */
void party_teams_update(void);

#endif
