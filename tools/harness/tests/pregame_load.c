/* A machine that joined the last game in progress, then a new lobby (#1049):
   the host's own code for the late join (network_game_server_start_late_joiner,
   network_game_server_late_joiner_loaded), the switch back to the pregame
   (network_game_server_reset_to_pregame), the machines' precache reports
   and the pregame load timeout (network_game_server_pregame_load_timeout,
   which network_game_server_idle_pregame_tasks calls each frame of the
   countdown). The late joiner's state is the others' once the lobby opens; a
   machine that never says it has the map loaded holds the start no longer
   than network.pregame_load_timeout seconds past the countdown, and is
   dropped; one that says so starts with the others. The connection and the
   messages are fakes that record what was sent. */
#include <stdarg.h>
#include "harness.h"
#include "config.inc"

#define MILLISECONDS_PER_SECOND 1000
#define TICKS_PER_SECOND 30
#define MAXIMUM_LOCAL_PLAYERS 4
#define MAXIMUM_NUMBER_OF_LOCAL_PLAYERS 4
#define HALO_PORT_MAXIMUM_NETWORK_MACHINES 8
#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 16
#define HALO_PORT_MAXIMUM_NETWORK_MESSAGE_SIZE 0x1000
#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define SET_FLAG(flags, bit, value) ((value) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))
#define csmemset memset
#define csmemcpy memcpy
#define csstrcmp strcmp
#define match_vassert(file, line, condition, text) ((void)0)
#define _error_silent 0

#include "enums.inc"

typedef unsigned short word;
struct network_connection { boolean active; int switches_sent; int begins_sent; };
struct network_player
{
	short machine_index, controller_index, team_index;
	char player_list_index;
	boolean valid;
};
struct network_machine { short machine_index; };
struct universal_variant { boolean teams; };
struct game_variant { struct universal_variant universal_variant; };
struct network_game
{
	struct network_player players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	struct network_machine machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	struct game_variant variant;
	long number_of_games_played;
};
struct countdown_timer { long time_remaining; unsigned long last_update_time; };
struct countdown_state
{
	boolean active, paused, adjusted_time_this_tick;
	struct countdown_timer timer;
	long last_countdown_message_time;
};
struct network_game_server_client_machine
{
	short machine_index;
	unsigned long flags;
	struct network_connection *connection;
	unsigned long last_heard_time;
	long last_received_update_sequence_number;
};
struct network_game_server
{
	short state;
	struct network_game game;
	struct network_game_server_client_machine client_machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	struct countdown_state countdown_state;
	long next_update_number;
	unsigned long time_of_first_client_loading_completion;
	boolean sent_start_game_message, queued_player_valid;
	short waiting_player_count;
};
struct network_message { word header; short type; };

#include "structures.inc"

/* ---------- the fake world */

static unsigned long now_milliseconds = 5000;
static long timeout_setting = 90;
static unsigned long settings_changes = 1;
static char server_map[64] = "putput";
static struct network_message last_message;
static int budget_resets[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
static int dropped[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
static char drop_line[256];
static short local_machine_index = 0;

static unsigned long system_milliseconds(void) { return now_milliseconds; }
static long config_integer(const char *name)
{
	if (!strcmp(name, "network.pregame_load_timeout"))
		return timeout_setting;
	if (!strcmp(name, "network.late_join_wait"))
		return 0;
	CHECK(0, "an unknown setting was read: %s", name);
	return 0;
}
static unsigned long config_changes(void) { return settings_changes; }
static long game_time_get(void) { return 19900; }
static char *main_get_multiplayer_map_name(void) { return server_map; }
static boolean network_player_is_valid(struct network_player *player) { return player->valid; }
static void network_event(const char *format, ...)
{
	char text[256];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	if (strstr(text, "pregame_load_timeout"))
		snprintf(drop_line, sizeof(drop_line), "%s", text);
}
static void error(int level, const char *format, ...) { (void)level; (void)format; }
static void *create_network_game_message(short type, void *data, long size)
{
	(void)data;
	(void)size;
	last_message.type = type;
	return &last_message;
}
static boolean network_connection_active(struct network_connection *connection)
{
	return connection && connection->active;
}
static boolean network_game_server_send_message_to_client_machine(struct network_game_server *server,
	struct network_game_server_client_machine *machine, void *message)
{
	short type = ((struct network_message *)message)->type;

	(void)server;
	if (!network_connection_active(machine->connection))
		return FALSE;
	if (type == _message_server_switch_to_pregame)
		machine->connection->switches_sent++;
	if (type == _message_server_begin_game)
		machine->connection->begins_sent++;
	return TRUE;
}
static boolean network_game_server_send_message_to_all_machines(struct network_game_server *server, void *message)
{
	(void)server;
	(void)message;
	return TRUE;
}
static boolean network_game_server_send_game_settings_to_client_machine(struct network_game_server *server,
	struct network_game_server_client_machine *machine, void const *game, long size)
{
	(void)server;
	(void)machine;
	(void)game;
	(void)size;
	return TRUE;
}
static boolean network_game_server_send_game_settings_to_all_machines(struct network_game_server *server,
	void const *game, long size)
{
	(void)server;
	(void)game;
	(void)size;
	return TRUE;
}
static boolean network_game_server_settings_waiting(struct network_game_server *server,
	struct network_game_server_client_machine *machine)
{
	(void)server;
	(void)machine;
	return FALSE;
}
static boolean network_game_server_client_machine_is_local(struct network_game_server *server,
	struct network_game_server_client_machine *machine)
{
	(void)server;
	return machine->machine_index == local_machine_index;
}
static boolean network_game_server_player_among(struct network_player const *player, struct network_player *players,
	long count)
{
	(void)player;
	(void)players;
	(void)count;
	return TRUE;
}
static void network_game_server_tell_late_joiner_of_quitters(struct network_game_server *server,
	struct network_game_server_client_machine *machine, struct network_player *started, long count)
{
	(void)server;
	(void)machine;
	(void)started;
	(void)count;
}
static void network_game_reset_for_next_round(struct network_game *game, boolean client)
{
	(void)game;
	(void)client;
}
static boolean network_game_server_setup_game_from_playlist(struct network_game_server *server)
{
	(void)server;
	return TRUE;
}
static void network_game_server_cooperative_round(struct network_game_server *server) { (void)server; }
static boolean network_game_server_handle_client_machines(struct network_game_server *server)
{
	(void)server;
	return TRUE;
}
static void network_distributed_lobby_budget_reset(long machine_index)
{
	if (machine_index >= 0 && machine_index < HALO_PORT_MAXIMUM_NETWORK_MACHINES)
		budget_resets[machine_index]++;
}

/* a machine dropped as a timed-out one is: out of the game with its players */
static boolean network_game_server_drop_client_machine(struct network_game_server *server,
	struct network_game_server_client_machine *machine)
{
	long index;

	CHECK(machine->machine_index >= 0, "a machine with no index was dropped");
	dropped[machine->machine_index]++;
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		if (server->game.players[index].machine_index == machine->machine_index)
			server->game.players[index].valid = FALSE;
	}
	machine->machine_index = NONE;
	machine->flags = 0;
	machine->connection = NULL;
	return TRUE;
}

#include "under_test.inc"

/* ---------- the session */

static struct network_game_server server;
static struct network_connection connections[HALO_PORT_MAXIMUM_NETWORK_MACHINES];

static void add_machine(short index, boolean with_player)
{
	struct network_game_server_client_machine *machine = &server.client_machines[index];

	machine->machine_index = index;
	machine->flags = FLAG(_network_client_machine_connected_bit) | FLAG(_network_client_machine_validated_bit);
	connections[index].active = TRUE;
	machine->connection = &connections[index];
	if (with_player)
	{
		struct network_player *player = &server.game.players[index];

		player->valid = TRUE;
		player->machine_index = index;
		player->team_index = index & 1;
		player->player_list_index = (char)index;
	}
}

/* the game the machines played: #0 the host's own (no player, as a dedicated
server's), #1 and #2 there from the start, #3 joined it in progress */
static void play_a_game_with_a_late_joiner(void)
{
	struct network_game_server_client_machine *late = &server.client_machines[3];

	memset(&server, 0, sizeof(server));
	for (short index = 0; index < HALO_PORT_MAXIMUM_NETWORK_MACHINES; index++)
		server.client_machines[index].machine_index = NONE;
	add_machine(0, FALSE);
	add_machine(1, TRUE);
	add_machine(2, TRUE);
	for (short index = 0; index <= 2; index++)
	{
		server.client_machines[index].flags |= FLAG(_network_client_machine_precached_bit) |
			FLAG(_network_client_machine_level_loaded_bit);
	}
	server.state = _network_game_server_state_ingame;
	add_machine(3, TRUE);
	network_game_server_start_late_joiner(&server, late);
	CHECK(connections[3].begins_sent == 1, "the late joiner was not started");
	CHECK(TEST_FLAG(late->flags, _network_client_machine_started_late_bit), "the late joiner was not marked started");
	now_milliseconds += 6000;
	network_game_server_late_joiner_loaded(&server, late);
	CHECK(TEST_FLAG(late->flags, _network_client_machine_level_loaded_bit), "the late joiner's load was refused");
	/* (a precache report it made before, of the map it joined) */
	late->flags |= FLAG(_network_client_machine_precached_bit);
	/* the game ends; its carnage report; the next game's lobby */
	now_milliseconds += 8 * 60 * 1000;
	server.state = _network_game_server_state_postgame;
	now_milliseconds += 20 * 1000;
	CHECK(network_game_server_reset_to_pregame(&server), "the server did not go back to the pregame");
	CHECK(server.state == _network_game_server_state_pregame, "the server is not in the pregame");
}

/* each machine named says it has the map loaded, as its precache report does */
static void reports(int first, int last)
{
	for (int index = first; index <= last; index++)
	{
		if (server.client_machines[index].machine_index != NONE)
			network_game_server_client_machine_is_precached(&server, &server.client_machines[index], "putput");
	}
}

/* frames of the countdown's end (held) or before it, a second at a time; TRUE
once the game would start (network_game_server_idle_pregame_tasks) */
static boolean frames(int seconds, boolean held)
{
	for (int second = 0; second < seconds; second++)
	{
		now_milliseconds += 1000;
		network_game_server_pregame_load_timeout(&server, held);
		if (held && network_game_server_have_all_machines_have_precached(&server))
			return TRUE;
	}
	return FALSE;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	play_a_game_with_a_late_joiner();

	CASE("state-reset")
	{
		/* the late joiner's state is the others' */
		for (int index = 1; index <= 3; index++)
		{
			unsigned long flags = server.client_machines[index].flags;

			CHECK(!TEST_FLAG(flags, _network_client_machine_level_loaded_bit), "machine #%d still loaded", index);
			CHECK(!TEST_FLAG(flags, _network_client_machine_started_late_bit), "machine #%d still started late", index);
			CHECK(!TEST_FLAG(flags, _network_client_machine_precached_bit),
				"machine #%d's precache report of the last lobby still counts", index);
			CHECK(budget_resets[index] == 1, "machine #%d's lobby budget was not renewed", index);
			CHECK(connections[index].switches_sent == 1, "machine #%d was not switched to the pregame", index);
		}
		CHECK(!network_game_server_late_joiner_waiting[3] && !network_game_server_late_joiner_first_added[3],
			"the late join's wait is left over");
		return 0;
	}
	CASE("late-joiner-reports")
	{
		reports(1, 2);
		CHECK(!frames(5, TRUE), "the game started before the late joiner said it had the map");
		reports(3, 3);
		CHECK(frames(1, TRUE), "the game did not start once every machine had the map");
		CHECK(!dropped[3] && !dropped[1] && !dropped[2], "a machine was dropped");
		return 0;
	}
	CASE("late-joiner-silent")
	{
		reports(1, 2);
		CHECK(!frames(timeout_setting - 1, TRUE), "the game started without the late joiner before the timeout");
		CHECK(!dropped[3], "the late joiner was dropped before the timeout");
		CHECK(frames(3, TRUE), "the lobby is still held after the timeout");
		CHECK(dropped[3] == 1, "the late joiner was not dropped");
		CHECK(!dropped[0] && !dropped[1] && !dropped[2], "a machine that had the map was dropped");
		CHECK(strstr(drop_line, "machine #3 ") != NULL, "the log does not name the machine: %s", drop_line);
		/* (an address in the log: digits, a dot, digits) */
		for (const char *at = drop_line; *at; at++)
			CHECK(!(at[0] >= '0' && at[0] <= '9' && at[1] == '.' && at[2] >= '0' && at[2] <= '9'),
				"the log has an address: %s", drop_line);
		return 0;
	}
	CASE("timeout-off")
	{
		timeout_setting = 0;
		settings_changes++;
		reports(1, 2);
		CHECK(!frames(3600, TRUE), "the game started without the late joiner");
		CHECK(!dropped[3], "the late joiner was dropped with the timeout off");
		return 0;
	}
	CASE("timeout-setting")
	{
		timeout_setting = 20;
		settings_changes++;
		reports(1, 2);
		CHECK(!frames(19, TRUE), "the game started before the timeout");
		CHECK(frames(3, TRUE) && dropped[3] == 1, "a 20-second timeout did not drop the machine");
		return 0;
	}
	CASE("countdown-not-over")
	{
		/* the wait counts only while the countdown is over: a pause starts it again */
		reports(1, 2);
		CHECK(!frames(80, TRUE), "the game started");
		CHECK(!frames(30, FALSE), "the game started while paused");
		CHECK(!frames(80, TRUE), "the game started");
		CHECK(!dropped[3], "the machine was dropped though the countdown was over for less than the timeout");
		return 0;
	}
	CASE("host-never-dropped")
	{
		/* a player's host with its own player, whose report is late too */
		server.game.players[0].valid = TRUE;
		server.game.players[0].machine_index = 0;
		reports(1, 3);
		CHECK(!frames(200, TRUE), "the game started without the host's own machine");
		CHECK(!dropped[0], "the host's own machine was dropped");
		return 0;
	}
	CHECK(0, "no such case: %s", case_name);
	return 0;
}
