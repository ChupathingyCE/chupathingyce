/* A rotation that names a map the host hasn't got (#1106): the host never
   sets a game on it (network_game_server_change_map_name keeps the game's
   map), the test host's map list skips it to the next installed map, and a
   dedicated server whose playlist has no installed map hosts nothing and
   says so once. */
#include "harness.h"
#include <stdarg.h>
#define _error_silent 0
#define NETWORK_SERVER_MANAGER_FILE ""
#define MAXIMUM_NETWORK_MACHINE_COUNT 4
#define NETWORK_GAME_MAP_NAME_LENGTH 128
#define SET_FLAG(flags, bit, value) ((value) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))
#define csstrncpy strncpy
#define csstrcmp strcmp
enum { _network_client_machine_precached_bit = 3 };
enum { _network_game_server_state_pregame = 0 };

#include "config.inc"

struct network_game_server_client_machine { long machine_index; unsigned long flags; };
struct network_game_map { char name[NETWORK_GAME_MAP_NAME_LENGTH]; long version; };
struct network_game { struct network_game_map map; };
struct network_game_server
{
	short state;
	struct network_game_server_client_machine client_machines[MAXIMUM_NETWORK_MACHINE_COUNT];
	struct network_game game;
};

/* the maps this machine has */
static const char *installed[8];
static int skips_logged, refusals_logged, sends;
static unsigned long now_milliseconds = 1000;
static char host_map[128];

static boolean cache_files_map_installed(const char *map_name)
{
	int index;

	for (index = 0; installed[index]; index++)
	{
		if (strstr(map_name, installed[index]))
			return TRUE;
	}
	return FALSE;
}
static unsigned long system_milliseconds(void) { return now_milliseconds; }
static unsigned long cache_files_map_choose(char *map_name, long size) { (void)map_name; (void)size; return 0; }
static void main_set_multiplayer_map_name(const char *map_name) { snprintf(host_map, sizeof(host_map), "%s", map_name); }
static void game_engine_override_map_name(const char *map_name) { (void)map_name; }
static boolean network_game_server_send_game_data_pregame(struct network_game_server *server) { (void)server; sends++; return TRUE; }
static void network_event(const char *format, ...) { (void)format; }
static void error(int level, const char *format, ...) { (void)level; (void)format; }
static void platform_log(const char *format, ...)
{
	if (strstr(format, "skipping it"))
		skips_logged++;
	if (strstr(format, "not starting the playlist"))
		refusals_logged++;
}

/* the test host's state (port/linux/game/network_test.c) */
static struct { char map_name[64]; short map_entry; short variant_index; } network_test;

/* a dedicated server's (server/src/dedicated.c) */
#define DEDICATED_ENTRIES 4
static struct
{
	long entry_count;
	char maps[DEDICATED_ENTRIES][64];
	char playlist[64];
	boolean none_playable;
	unsigned long none_playable_time;
} dedicated;
static boolean map_playable_here(const char *map, char *reason, long size)
{
	if (cache_files_map_installed(map))
		return TRUE;
	snprintf(reason, (size_t)size, "no multiplayer map %s in the maps folder", map);
	return FALSE;
}

/* (MCC maps' games at 43, the host's question before a lobby switch, and
MCC package work waiting for local admission: not this test's,
network_server_manager.c's own) */
#define network_game_server_mcc_version(map_name) ((void)(map_name))
#define network_game_server_mcc_confirm(server, map_name) ((void)(server), (void)(map_name), FALSE)
#define mcc_ui_network_defer_map(server, map_name) ((void)(server), (void)(map_name), FALSE)

#include "under_test.inc"

static void set_list(const char *maps)
{
	snprintf(network_test.map_name, sizeof(network_test.map_name), "%s", maps);
	network_test.map_entry = 0;
	network_test.variant_index = 0;
}

/* the next game's map, as the test host moves to it */
static void next_game(void)
{
	network_test.variant_index++;
	if (network_test.map_entry + 1 < network_test_map_count())
		network_test.map_entry++;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	char path[128];

	installed[0] = "bloodgulch";
	installed[1] = "hangemhigh";
	CASE("middle-skipped")
	{
		set_list("bloodgulch+dangercanyon+hangemhigh");
		CHECK(network_test_playable_map(path, sizeof(path)), "no first map");
		CHECK(!strcmp(path, "levels\\test\\bloodgulch\\bloodgulch"), "game 1 is %s", path);
		next_game();
		CHECK(network_test_playable_map(path, sizeof(path)), "no second map");
		CHECK(!strcmp(path, "levels\\test\\hangemhigh\\hangemhigh"), "game 2 is %s", path);
		CHECK(skips_logged == 1, "%d skips logged", skips_logged);
		next_game();
		CHECK(network_test_playable_map(path, sizeof(path)), "no third map");
		CHECK(!strcmp(path, "levels\\test\\hangemhigh\\hangemhigh"), "game 3 is %s", path);
		return 0;
	}
	CASE("last-missing-holds")
	{
		/* (past the list's end its last installed map, as normal play holds
		on the last map: not the list's first again) */
		set_list("bloodgulch+hangemhigh+dangercanyon");
		next_game();
		CHECK(network_test_playable_map(path, sizeof(path)), "no second map");
		CHECK(!strcmp(path, "levels\\test\\hangemhigh\\hangemhigh"), "game 2 is %s", path);
		next_game();
		CHECK(network_test_playable_map(path, sizeof(path)), "no third map");
		CHECK(!strcmp(path, "levels\\test\\hangemhigh\\hangemhigh"), "game 3 is %s", path);
		next_game();
		CHECK(network_test_playable_map(path, sizeof(path)), "no fourth map");
		CHECK(!strcmp(path, "levels\\test\\hangemhigh\\hangemhigh"), "game 4 is %s", path);
		return 0;
	}
	CASE("none-installed")
	{
		set_list("dangercanyon+deathisland@ce");
		CHECK(!network_test_playable_map(path, sizeof(path)), "a map that isn't installed was chosen: %s", path);
		return 0;
	}
	CASE("host-keeps-its-map")
	{
		static struct network_game_server server;

		strcpy(server.game.map.name, "levels\\test\\bloodgulch\\bloodgulch");
		server.client_machines[1].machine_index = 1;
		server.client_machines[1].flags = FLAG(_network_client_machine_precached_bit);
		CHECK(!network_game_server_change_map_name(&server, "levels\\test\\dangercanyon\\dangercanyon"),
			"the refusal wasn't told to the caller");
		CHECK(!strcmp(server.game.map.name, "levels\\test\\bloodgulch\\bloodgulch"), "the game's map is now %s",
			server.game.map.name);
		CHECK(!strcmp(host_map, "levels\\test\\bloodgulch\\bloodgulch"), "the host loads %s", host_map);
		CHECK(!sends, "the missing map was sent to the players");
		CHECK(TEST_FLAG(server.client_machines[1].flags, _network_client_machine_precached_bit),
			"the players were asked to precache again");
		CHECK(network_game_server_change_map_name(&server, "levels\\test\\hangemhigh\\hangemhigh"),
			"an installed map was told refused");
		CHECK(!strcmp(server.game.map.name, "levels\\test\\hangemhigh\\hangemhigh"), "an installed map was refused");
		CHECK(sends == 1, "the installed map wasn't sent");
		return 0;
	}
	CASE("playlist-none-playable")
	{
		dedicated.entry_count = 2;
		strcpy(dedicated.maps[0], "levels\\test\\dangercanyon\\dangercanyon");
		strcpy(dedicated.maps[1], "deathisland@ce");
		CHECK(!playlist_playable(), "a playlist with no installed map was started");
		CHECK(refusals_logged == 1, "%d refusals logged", refusals_logged);
		now_milliseconds += 1000;
		CHECK(!playlist_playable(), "started");
		now_milliseconds += 60000;
		CHECK(!playlist_playable(), "started");
		CHECK(refusals_logged == 1, "the refusal was said again (%d)", refusals_logged);
		/* (a map installed since) */
		strcpy(dedicated.maps[1], "levels\\test\\hangemhigh\\hangemhigh");
		CHECK(!playlist_playable(), "looked again before the wait was over");
		now_milliseconds += 60000;
		CHECK(playlist_playable(), "a playlist with an installed map was refused");
		return 0;
	}
	CASE("playlist-playable")
	{
		dedicated.entry_count = 3;
		strcpy(dedicated.maps[0], "levels\\test\\dangercanyon\\dangercanyon");
		strcpy(dedicated.maps[1], "levels\\test\\hangemhigh\\hangemhigh");
		strcpy(dedicated.maps[2], "levels\\test\\bloodgulch\\bloodgulch");
		CHECK(playlist_playable(), "a playlist with installed maps was refused");
		CHECK(!refusals_logged, "a refusal was logged");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
