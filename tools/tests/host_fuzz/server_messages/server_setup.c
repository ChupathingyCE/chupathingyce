
/* ---------- server_messages_fuzz: a host's game, made for each input
(compiled at the end of network_server_manager.c, which keeps the server's
structure its own: tools/tests/host_fuzz/server_messages/target.json) */

static struct network_game_server server_messages_fuzz_server;

/* a host's server in state (0 pregame, 1 in game, 2 postgame), its lobby
of four machines: the host's own (0), two joined with a player each (1, 2;
loaded in game), and one connected that has not joined (3) */
struct network_game_server *server_messages_fuzz_reset(
	int state,
	boolean start_sent,
	boolean teams)
{
	struct network_game_server *server = &server_messages_fuzz_server;
	long index;

	csmemset(server, 0, sizeof(*server));
	network_game_invalidate(&server->game);
	server->state = (word)state;
	server->sent_start_game_message = start_sent;
	server->game.maximum_players = 16;
	server->game.variant.universal_variant.teams = teams;
	for (index = 0; index < MAXIMUM_NETWORK_MACHINE_COUNT; index++)
	{
		server->client_machines[index].machine_index = NONE;
		network_game_server_client_machine_addresses[index] = 0;
	}
	for (index = 0; index < 4; index++)
	{
		struct network_game_server_client_machine *machine = &server->client_machines[index];

		machine->machine_index = (short)index;
		/* (a connection is not dereferenced here: the transport is the
		harness's) */
		machine->connection = (struct network_connection *)(server->client_machines + index);
		SET_FLAG(machine->flags, _network_client_machine_connected_bit, TRUE);
		network_game_server_client_machine_addresses[index] = 0x0A000001 + index;
		if (index < 3)
		{
			struct network_machine description;

			csmemset(&description, 0, sizeof(description));
			description.name[0] = 'M';
			description.machine_index = (char)index;
			network_game_add_machine(&server->game, &description);
			SET_FLAG(machine->flags, _network_client_machine_validated_bit, TRUE);
			if (state == _network_game_server_state_ingame)
				SET_FLAG(machine->flags, _network_client_machine_level_loaded_bit, TRUE);
		}
		if (index > 0 && index < 3)
		{
			struct network_player player;

			csmemset(&player, 0, sizeof(player));
			player.name[0] = 'P';
			player.name[1] = (wchar_t)('0' + index);
			player.primary_color_index = (short)index;
			player.icon_index = NONE;
			player.machine_index = (char)index;
			player.controller_index = 0;
			player.team_index = (char)(index & 1);
			player.player_list_index = NONE;
			network_game_add_player(&server->game, &player);
		}
	}
	return server;
}

struct network_game_server_client_machine *server_messages_fuzz_machine(
	long index)
{
	return &server_messages_fuzz_server.client_machines[index];
}

/* what a run must leave as it found it: the counts the game keeps match
the slots */
boolean server_messages_fuzz_consistent(
	void)
{
	struct network_game_server *server = &server_messages_fuzz_server;
	long index;
	long players = 0;

	for (index = 0; index < (long)NUMBEROF(server->game.players); index++)
	{
		struct network_player *player = &server->game.players[index];

		if (player->player_list_index != NONE)
		{
			players++;
			if (player->player_list_index != index || !network_player_is_valid(player))
				return FALSE;
		}
	}
	return server->game.player_count == players && server->waiting_player_count >= 0 &&
		server->waiting_player_count <= MAXIMUM_NETWORK_PLAYER_COUNT;
}
