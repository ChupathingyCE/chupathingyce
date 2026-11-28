/*
OPENSAUCE_SESSION.C

The network session a map loads in, for OpenSauce's maps
(opensauce_maps_session_now, opensauce_maps.h): whether this machine hosts a
game the internet may join (Online Games, online co-op, a dedicated server:
p2p_set_hosting_allowed), or has joined a host on the internet (a public
address, or an internet peer reached through the tunnel, whose game address
is a virtual one: p2p_peer_public_address, which takes it in the network's
byte order, as a socket holds it, where the game's transport keeps it in
this machine's). System link and offline play are neither. Without the
game list (--no-game-browser) there is no internet play, so no session is
an online one.
*/

#include "cseries.h"
#include "opensauce_maps.h"

#if defined(OPENSAUCE_MAPS) && defined(HALO_GAME_BROWSER)

#include "cseries/cseries_windows.h"
#include "bungie_net/network/transport.h"
#include "memory/byte_swapping.h"
#include "networking/network_game_globals.h"
#include "networking/network_client_manager.h"
#include "network_rules.h"
#include "../src/p2p.h"

#include <string.h>

void opensauce_maps_session_now(
	struct opensauce_maps_session *session)
{
	struct network_game_server *server = global_network_game_server_get();
	struct network_game_client *client = global_network_game_client_get();

	memset(session, 0, sizeof(*session));
	if (server)
	{
		session->hosting = TRUE;
		session->hosting_internet = p2p_hosting_internet() ? TRUE : FALSE;
	}
	else if (client)
	{
		struct transport_address host;
		unsigned long address;

		memset(&host, 0, sizeof(host));
		network_game_client_get_remote_server_address(client, &host);
		address = host.address.ipv4_address;
		session->joined = TRUE;
		session->host_on_lan = network_rules_address_joins_as_lan((uint32_t)address) &&
			!p2p_peer_public_address(NETWORK_SWAP4(address));
	}
}

#elif defined(OPENSAUCE_MAPS)

void opensauce_maps_session_now(
	struct opensauce_maps_session *session)
{
	session->hosting = session->hosting_internet = session->joined = session->host_on_lan = FALSE;
}

#endif
