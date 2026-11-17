/* A host's join lock (port/linux/game/host_rules.c) never outlives the
   players it was for: it ends 10 seconds after hosting does, or after nobody
   it could matter to is in (a dedicated server's players; a player's host's
   players of other machines). A gap shorter than that (a map's change)
   keeps it, and a lock set again starts over. */
#include "harness.h"
#include "config.inc"
#define _error_silent 0
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define HOST_RULES_DELTA

struct network_game { short machine_count; short player_count; };
struct network_game_server { struct network_game game; };

static struct network_game_server host_server;
static struct network_game_server *global_server = &host_server;
static unsigned long now_milliseconds = 1000;
static int roles_told;

static struct network_game_server *global_network_game_server_get(void) { return global_server; }
static struct network_game *network_game_server_get_game(struct network_game_server *server) { return &server->game; }
static unsigned long system_milliseconds(void) { return now_milliseconds; }
static void error(int level, const char *text, ...) { (void)level; (void)text; }
static void delta_peer_game_moderation_roles_changed(void) { roles_told++; }

#include "under_test.inc"

static void wait_seconds(int seconds)
{
	int step;

	for (step = 0; step < seconds * 30; step++)
	{
		now_milliseconds += 33;
		host_rules_lock_update();
	}
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	host_server.game.machine_count = 3;
	host_server.game.player_count = 4;
	host_rules_set_joins_locked(TRUE);
	CASE("players-stay")
	{
		wait_seconds(120);
		CHECK(host_rules_joins_locked(), "the lock ended with players in");
		CHECK(!roles_told, "moderators were told of a change");
		return 0;
	}
	CASE("hosting-ends")
	{
		global_server = NULL;
		wait_seconds(9);
		CHECK(host_rules_joins_locked(), "the lock ended before 10 seconds");
		wait_seconds(2);
		CHECK(!host_rules_joins_locked(), "the lock outlived hosting");
		CHECK(roles_told == 1, "moderators were not told the lock ended");
		return 0;
	}
	CASE("empties")
	{
#ifdef HALO_SERVER
		host_server.game.player_count = 0;
		host_server.game.machine_count = 1;
#else
		host_server.game.machine_count = 1;
#endif
		wait_seconds(11);
		CHECK(!host_rules_joins_locked(), "the lock outlived the players");
		return 0;
	}
	CASE("own-players-alone")
	{
		/* (a player's host with its own players alone: nobody to keep out;
		a dedicated server with a player in is never empty) */
		host_server.game.machine_count = 1;
		host_server.game.player_count = 2;
		wait_seconds(11);
#ifdef HALO_SERVER
		CHECK(host_rules_joins_locked(), "a server with players in unlocked");
#else
		CHECK(!host_rules_joins_locked(), "the host's own players kept the lock");
#endif
		return 0;
	}
	CASE("short-gap")
	{
		global_server = NULL;
		wait_seconds(5);
		global_server = &host_server;
		wait_seconds(1);
		global_server = NULL;
		wait_seconds(8);
		CHECK(host_rules_joins_locked(), "a short gap ended the lock");
		return 0;
	}
	CASE("locked-again")
	{
		global_server = NULL;
		wait_seconds(8);
		host_rules_set_joins_locked(TRUE);
		wait_seconds(5);
		CHECK(host_rules_joins_locked(), "a lock set again kept the old wait");
		return 0;
	}
	CHECK(FALSE, "no such case: %s", case_name);
	return 0;
}
