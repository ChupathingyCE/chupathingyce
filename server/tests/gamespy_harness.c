/*
GAMESPY_HARNESS.C

server/platform/gamespy_listing.c alone, for tools/test_gamespy_listing.py:
the listing started on a port with a list of masters, given a made-up
server's state, until its standard input ends (then stopped, which sends
the masters the last heartbeat). Its notices go to standard output.

  gamespy_harness <port> <masters> <gamever> <players>
  gamespy_harness map <map>     (the map's name as Custom Edition has it)
*/

#include "../platform/gamespy_listing.h"

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
	if (argc != 5)
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
