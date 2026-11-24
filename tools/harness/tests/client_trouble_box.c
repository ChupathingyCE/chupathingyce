/* The network trouble box on a client in a game (network_game_client_idle_ingame,
   network_game_client_listened_milliseconds): it is for a host gone quiet,
   not for this machine's own long frame (a level or BSP loading), after which
   what the host sent meanwhile is still waiting to be read. Frames run on a
   fake clock; the connection keeps network_connection_idle's order: the
   going-stale flag is set from the silence before what waits is read. */
#include "harness.h"
#include "config.inc"
#define _error_silent 0
#define MILLISECONDS_PER_SECOND 1000

struct network_connection
{
	unsigned long last_heard; /* when it last read anything from its host */
	unsigned long last_read; /* when it last looked */
	boolean stale;
};
struct network_game_client
{
	struct network_connection *connection;
	boolean connection_silent;
};
struct network_game_server { int unused; };

static unsigned long now = 100000;
/* the host sends every tick but while it is silent, from silent_from to silent_until */
static unsigned long silent_from = UNSIGNED_LONG_MAX, silent_until = UNSIGNED_LONG_MAX;
static int boxes;
static unsigned long first_box;

static unsigned long system_milliseconds(void) { return now; }
static struct network_game_server *global_network_game_server_get(void) { return NULL; }
static boolean network_game_client_server_has_started_game(struct network_game_client *client) { return TRUE; }
static boolean network_connection_active(struct network_connection *connection) { return TRUE; }
static boolean network_connection_connected(struct network_connection *connection) { return TRUE; }
static boolean network_connection_going_stale(struct network_connection *connection) { return connection->stale; }
static boolean network_game_is_splitscreen_local(void) { return FALSE; }
static boolean network_game_client_network_lost(void) { return FALSE; }
static short local_player_get_next(short index) { return index == NONE ? 0 : NONE; }
static void display_error(short error, short local_player_index, boolean a, boolean b)
{
	if (error == _error_network_trouble_is_brewing && !boxes++)
		first_box = now;
}
static void display_error_when_main_menu_loaded(short error) { }
static void network_event(const char *text, ...) { }
static void error(int level, const char *text, ...) { }
static boolean network_game_client_process_incoming_messages(struct network_game_client *client) { return TRUE; }
static long network_game_client_connection_timeout(long timeout) { return timeout; }
static boolean network_connection_idle(struct network_connection *connection, long timeout, void *new_connection)
{
	/* (the flag from the silence so far, then what came since the last look
	read: anything the host sent after it) */
	connection->stale = now - connection->last_heard > MILLISECONDS_PER_SECOND * 5;
	if (connection->last_read < silent_from || now >= silent_until)
		connection->last_heard = now;
	connection->last_read = now;
	return TRUE;
}

#include "under_test.inc"

static struct network_connection connection;
static struct network_game_client client = { &connection, FALSE };

/* frames of this many milliseconds until the clock reaches end */
static void frames(unsigned long length, unsigned long end)
{
	while (now < end)
	{
		network_game_client_idle_ingame(&client);
		now += length;
	}
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	unsigned long start = now;

	connection.last_heard = connection.last_read = now;
	CASE("listening")
	{
		frames(33, start + 60000);
		CHECK(!boxes, "a box with the host heard every frame");
	}
	CASE("host-quiet-then-own-frame")
	{
		/* the host stops for 3 s (its own BSP load), then this machine for 3 s
		(its own), the host talking again meanwhile: 6 s without reading
		anything, but what the host sent is waiting */
		frames(33, start + 10000);
		silent_from = now;
		silent_until = now + 3000;
		frames(33, silent_until - 33);
		frames(3000, now + 1);
		frames(33, start + 30000);
		CHECK(!boxes, "this machine's own frame showed the box (%d times)", boxes);
	}
	CASE("own-load")
	{
		frames(33, start + 10000);
		frames(11000, now + 1);
		frames(33, start + 40000);
		CHECK(!boxes, "this machine's own load showed the box (%d times)", boxes);
	}
	CASE("host-gone")
	{
		unsigned long gone;

		frames(33, start + 10000);
		gone = silent_from = now;
		frames(33, start + 30000);
		CHECK(boxes == 1, "a host gone quiet showed %d boxes", boxes);
		CHECK(first_box - gone > 5000 && first_box - gone < 6000, "the box came %lu ms after the host went quiet",
			first_box - gone);
	}
	CASE("host-gone-in-own-frame")
	{
		/* the host goes quiet as this machine's 3 s frame starts: the box
		once this machine has listened 5 s */
		unsigned long back;

		frames(33, start + 10000);
		silent_from = now;
		frames(3000, now + 1);
		back = now;
		frames(33, start + 40000);
		CHECK(boxes == 1, "a host gone quiet showed %d boxes", boxes);
		CHECK(first_box - back > 5000 && first_box - back < 6000, "the box came %lu ms after this machine listened again",
			first_box - back);
	}
	CASE("slow-machine-host-gone")
	{
		/* every frame of this machine's 700 ms: a host gone quiet is still seen */
		frames(700, start + 10000);
		silent_from = now;
		frames(700, start + 40000);
		CHECK(boxes == 1, "a slow machine showed %d boxes for a host gone quiet", boxes);
	}
	return 0;
}
