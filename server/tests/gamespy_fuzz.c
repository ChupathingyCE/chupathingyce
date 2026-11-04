/*
GAMESPY_FUZZ.C

libFuzzer's entry for what anyone on the internet can send the dedicated
server's GameSpy port (server/platform/gamespy_listing.c, built into this
file so its master packet reader, which is not exported, is run too), as
tools/test_host_fuzz.py builds it. The input's first byte says how much of
what follows is the server's state (a status, its text fields made
terminated as the server's own are); the rest is a query or master packet.
*/

#include "../platform/gamespy_listing.c"

#include <stdint.h>

static void make_text(char *text, size_t size)
{
	text[size - 1] = 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct gamespy_status status;
	unsigned char reply[GAMESPY_PACKET_SIZE];
	unsigned char query[GAMESPY_PACKET_SIZE];
	unsigned char instance[4] = {1, 2, 3, 4};
	char text[256];
	char response[200];
	struct sockaddr_in from;
	size_t status_size, query_size;
	int index;

	if (size < 1)
		return 0;
	status_size = (size_t)data[0] * sizeof(status) / 255;
	if (status_size > size - 1)
		status_size = size - 1;
	memset(&status, 0, sizeof(status));
	memcpy(&status, data + 1, status_size < sizeof(status) ? status_size : sizeof(status));
	make_text(status.hostname, sizeof(status.hostname));
	make_text(status.mapname, sizeof(status.mapname));
	make_text(status.gametype, sizeof(status.gametype));
	make_text(status.gamevariant, sizeof(status.gamevariant));
	make_text(status.version, sizeof(status.version));
	for (index = 0; index < GAMESPY_MAXIMUM_PLAYERS; index++)
		make_text(status.players[index].name, sizeof(status.players[index].name));
	query_size = size - 1 - status_size;
	if (query_size > sizeof(query))
		query_size = sizeof(query);
	memcpy(query, data + 1 + status_size, query_size);

	gamespy_reply(&status, "01.00.10.0621", query, (int)query_size, reply, sizeof(reply));
	gamespy_reply(&status, "01.00.10.0621", query, (int)query_size, reply, 64);
	gamespy_reply(&status, NULL, query, (int)query_size / 2, reply, sizeof(reply));
	gamespy_heartbeat(&status, "01.00.10.0621", instance, (int)(query_size % 5) - 1, query_size ? "127.0.0.1" : NULL,
		(int)query_size, reply, sizeof(reply));
	gamespy_challenge_response(query, (int)query_size, response, sizeof(response));
	gamespy_challenge_response(query, (int)query_size, response, 5);

	/* (the text a map's name is made from, ended as the server's is) */
	memcpy(text, query, query_size < sizeof(text) - 1 ? query_size : sizeof(text) - 1);
	text[query_size < sizeof(text) - 1 ? query_size : sizeof(text) - 1] = 0;
	gamespy_map_name(text, response, sizeof(response));
	gamespy_map_name(text, response, 8);

	/* (a master's packet, from the master's address) */
	memset(&listing, 0, sizeof(listing));
	listing.socket = -1;
	memcpy(listing.instance, instance, 4);
	listing.master_count = 1;
	listing.masters[0].resolved = 1;
	memset(&from, 0, sizeof(from));
	from.sin_family = AF_INET;
	from.sin_port = 2303;
	listing.masters[0].address = from;
	master_packet(query, (int)query_size, &from);
	if (query_size >= 7)
	{
		unsigned char packet[GAMESPY_PACKET_SIZE];

		memcpy(packet, query, query_size);
		packet[0] = 0xfe;
		packet[1] = 0xfd;
		memcpy(packet + 3, instance, 4);
		master_packet(packet, (int)query_size, &from);
	}
	return 0;
}
