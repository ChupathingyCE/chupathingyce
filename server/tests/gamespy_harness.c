/*
GAMESPY_HARNESS.C

server/platform/gamespy_listing.c alone, for tools/test_gamespy_listing.py:
the listing started on a port with a list of masters, given a made-up
server's state, until its standard input ends (then stopped, which sends
the masters the last heartbeat). Its notices go to standard output.

  gamespy_harness <port> <masters> <gamever> <players> [joins]
  gamespy_harness map <map>     (the map's name as Custom Edition has it)
  gamespy_harness keepalive     (the server's keep-alive payload, as hex)
  gamespy_harness settings      (a game settings payload, as hex: the one
                                 our capture of Custom Edition's server has)
*/

#include "../platform/gamespy_listing.h"
#include "../platform/ce_messages.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	struct gamespy_status status;
	char problem[160];
	char notice[160];
	int players;
	int index;

	if (argc == 3 && !strcmp(argv[1], "map"))
	{
		char name[GAMESPY_TEXT_SIZE];

		gamespy_map_name(argv[2], name, sizeof(name));
		printf("%s\n", name);
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "keepalive"))
	{
		struct ce_payload payload;
		unsigned char out[32];
		int length;

		ce_payload_begin(&payload, out, sizeof(out));
		ce_payload_put_server_keep_alive(&payload, 0, 0);
		length = ce_payload_end(&payload);
		for (index = 0; index < length; index++)
			printf("%02x", out[index]);
		printf("\n");
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "settings"))
	{
		static const unsigned char begin_game[] = { 1, 0, 0, 0, 0, CE_MESSAGE_BEGIN_GAME };
		struct ce_game_settings settings;
		struct ce_payload payload;
		unsigned char out[1400];
		int length;

		ce_game_settings_slayer(&settings);
		snprintf(settings.name, sizeof(settings.name), "hcetest");
		snprintf(settings.map, sizeof(settings.map), "bloodgulch");
		settings.maximum_players = 4;
		settings.player_count = 1;
		settings.random_seed = 51721;
		snprintf(settings.players[0].name, sizeof(settings.players[0].name), "Noodle");
		settings.players[0].color = 0;
		settings.players[0].machine = 0;
		settings.players[0].controller = 0;
		settings.players[0].team = 0;
		settings.players[0].list_index = 0;
		ce_payload_begin(&payload, out, sizeof(out));
		ce_payload_put_game_settings(&payload, &settings);
		ce_payload_put_message(&payload, begin_game, sizeof(begin_game));
		length = ce_payload_end(&payload);
		for (index = 0; index < length; index++)
			printf("%02x", out[index]);
		printf("\n");
		return 0;
	}
	if (argc != 5 && !(argc == 6 && !strcmp(argv[5], "joins")))
	{
		fprintf(stderr, "usage: gamespy_harness <port> <masters> <gamever> <players>\n");
		return 2;
	}
	memset(&status, 0, sizeof(status));
	snprintf(status.hostname, sizeof(status.hostname), "[C] Test\\Server");
	gamespy_map_name("levels\\test\\bloodgulch\\bloodgulch", status.mapname, sizeof(status.mapname));
	snprintf(status.gametype, sizeof(status.gametype), "Slayer");
	snprintf(status.gamevariant, sizeof(status.gamevariant), "Team Slayer");
	snprintf(status.version, sizeof(status.version), "0.0.0-test");
	status.maxplayers = 16;
	status.teamplay = 1;
	status.fraglimit = 50;
	status.playing = 1;
	players = atoi(argv[4]);
	if (players > GAMESPY_MAXIMUM_PLAYERS + 4)
		players = GAMESPY_MAXIMUM_PLAYERS + 4;
	status.numplayers = players;
	for (index = 0; index < players && index < GAMESPY_MAXIMUM_PLAYERS; index++)
	{
		struct gamespy_player *player = &status.players[index];

		snprintf(player->name, sizeof(player->name), "Player%d", index + 1);
		player->score = index * 3;
		player->ping = index == 0 ? -1 : 20 + index;
		player->team = index & 1;
		status.player_count++;
	}
	if (!gamespy_listing_start(atoi(argv[1]), argv[2], argv[3], problem, sizeof(problem)))
	{
		printf("not started: %s\n", problem);
		return 1;
	}
	gamespy_listing_publish(&status);
	if (argc == 6)
		gamespy_listing_set_joins(1);
	printf("started\n");
	fflush(stdout);
	for (;;)
	{
		struct pollfd input = { 0, POLLIN, 0 };
		char buffer[256];

		while (gamespy_listing_next_notice(notice, sizeof(notice)))
		{
			printf("%s\n", notice);
			fflush(stdout);
		}
		if (poll(&input, 1, 100) > 0)
		{
			ssize_t got = read(0, buffer, sizeof(buffer));

			if (got <= 0)
				break;
			/* (a line "change": another map, as a new game's) */
			if (!strncmp(buffer, "change", 6))
			{
				gamespy_map_name("timberland@ce", status.mapname, sizeof(status.mapname));
				gamespy_listing_publish(&status);
			}
		}
	}
	gamespy_listing_stop();
	printf("stopped\n");
	return 0;
}
