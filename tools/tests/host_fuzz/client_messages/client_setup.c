
/* ---------- client_messages_fuzz: a client of a game, made for each input
(compiled at the end of network_client_manager.c, which keeps the client's
structure its own: tools/tests/host_fuzz/client_messages/target.json) */

static struct network_game_client client_messages_fuzz_client;

/* a client in state (searching, joining, the lobby, in game, after it):
past joining, machine 1 of a lobby of the host's machine (0) and this one,
a player each */
struct network_game_client *client_messages_fuzz_reset(
	int state)
{
	struct network_game_client *client = &client_messages_fuzz_client;
	long index;

	csmemset(client, 0, sizeof(*client));
	network_game_invalidate(&client->game);
	client->game.maximum_players = 16;
	/* (a connection is not dereferenced here: the transport is the
	harness's) */
	client->connection = (struct network_connection *)client;
	client->state = (word)state;
	client->machine_index = NONE;
	client->seconds_to_game_start = NONE;
	if (state >= _network_game_client_state_pregame)
	{
		client->machine_index = 1;
		for (index = 0; index < 2; index++)
		{
			struct network_machine machine;
			struct network_player player;

			csmemset(&machine, 0, sizeof(machine));
			machine.name[0] = 'M';
			machine.machine_index = (char)index;
			network_game_add_machine(&client->game, &machine);
			csmemset(&player, 0, sizeof(player));
			player.name[0] = 'P';
			player.icon_index = NONE;
			player.machine_index = (char)index;
			player.team_index = (char)index;
			player.player_list_index = NONE;
			network_game_add_player(&client->game, &player);
		}
	}
	return client;
}
