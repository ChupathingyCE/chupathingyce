/*
GAME_LIST_CLAIMS.C

The game list's confirmed players (configure.py --game-browser,
HALO_GAME_BROWSER; port/linux/src/browser.c): when a game this machine
plays in ends, on the host and on every machine that joined it, this
machine's players confirm their lines in the game's carnage report with its
player key. Called each frame from the main loop (main.c).
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "../src/browser.h"

enum
{
	MAXIMUM_CLAIMED_PLAYERS = 4,
};

static boolean game_over_seen;

void game_list_claims_update(
	void)
{
	boolean over;

	/* a network game that has ended (scoring is over) */
	over = game_engine_running() && !game_engine_can_score() &&
		(global_network_game_server_get() || global_network_game_client_get());
	if (over && !game_over_seen)
	{
		unsigned short names[MAXIMUM_CLAIMED_PLAYERS][BROWSER_PLAYER_NAME_LENGTH];
		struct data_iterator iterator;
		struct player_datum *player;
		int count = 0;

		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL &&
			count < MAXIMUM_CLAIMED_PLAYERS)
		{
			if (player->local_player_index != NONE)
			{
				csmemcpy(names[count], player->name, sizeof(names[count]));
				count++;
			}
		}
		browser_claim_game((const unsigned short (*)[BROWSER_PLAYER_NAME_LENGTH])names, count);
	}
	game_over_seen = over;
}

#endif
