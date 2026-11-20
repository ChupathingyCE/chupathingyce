/*
EMBLEM_NET.H

Halo 2 emblems between this project's builds (h2_emblem_protocol.h says what
is sent and how it is checked): each machine tells the host its players'
emblems (_message_client_emblem), the host checks them, keeps them per machine
and player and tells every machine (_message_server_emblem). An extra of our
own: an older build or another PC build drops the messages as unknown packet
types and sees no emblems; never on the crossplay wire (Halo CE crossplay with
PC servers is unchanged), the network version unchanged.
*/

#ifndef HALO_EMBLEM_NET_H
#define HALO_EMBLEM_NET_H

#include "h2_emblem_protocol.h"

struct network_game_client;
struct network_game_server;
struct network_game_server_client_machine;

/* (the message handlers) a host's message to this joiner, a joiner's to this
host */
void emblem_net_client_handle_message(struct network_game_client *client, unsigned short *message, short message_size);
void emblem_net_server_handle_message(struct network_game_server *server,
	struct network_game_server_client_machine *machine, unsigned short *message, short message_size);
/* (friends_game.c, each frame) this machine's players' sent when they
change, a host's table sent on */
void emblem_net_update(void);
/* a machine's player's emblem this game knows; NULL none */
struct h2_emblem const *emblem_net_player(int machine, int controller);

#endif
