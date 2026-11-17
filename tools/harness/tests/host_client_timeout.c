/* A client's frame in a game (network_game_client_idle_ingame) after the
   machine's frame stopped for a while: the host's own client plays on (its
   server, on the same thread, was silent only because the frame was), a
   client of another machine's host still times out. The connection is a fake
   that keeps network_connection_idle's timing: silent longer than a non-zero
   timeout fails it, a zero timeout never does. */
#include "harness.h"
#include "config.inc"
#define _error_silent 0

struct network_connection
{
	unsigned long silence; /* milliseconds since it last heard its host */
	long timeout; /* what the client asked for */
	boolean active, connected;
};
struct network_game_client
{
	struct network_connection *connection;
	boolean connection_silent;
};
struct network_game_server { int unused; };

static struct network_game_server host_server;
static struct network_game_server *global_server;
static boolean started = TRUE;
static int errors_shown, messages_processed;
static short shown_error = NONE;

static struct network_game_server *global_network_game_server_get(void) { return global_server; }
static boolean network_game_client_server_has_started_game(struct network_game_client *client) { return started; }
static boolean network_connection_active(struct network_connection *connection) { return connection->active; }
static boolean network_connection_connected(struct network_connection *connection) { return connection->connected; }
static boolean network_connection_going_stale(struct network_connection *connection) { return FALSE; }
static boolean network_game_is_splitscreen_local(void) { return FALSE; }
static boolean network_game_client_network_lost(void) { return FALSE; }
static short local_player_get_next(short index) { return NONE; }
static void display_error(short error, short local_player_index, boolean a, boolean b) { errors_shown++; }
static void display_error_when_main_menu_loaded(short error) { shown_error = error; }
static void network_event(const char *text, ...) { }
static void error(int level, const char *text, ...) { }
static boolean network_game_client_process_incoming_messages(struct network_game_client *client)
{
	messages_processed++;
	return TRUE;
}
static boolean network_connection_idle(struct network_connection *connection, long timeout, void *new_connection)
{
	connection->timeout = timeout;
	if (timeout && connection->silence > (unsigned long)timeout)
		return FALSE;
	return TRUE;
}

#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	struct network_connection connection = { 0, -1, TRUE, TRUE };
	struct network_game_client client = { &connection, FALSE };
	boolean expected = TRUE;

	CASE("host-hitch") { global_server = &host_server; connection.silence = 20000; }
	CASE("host-loading") { global_server = &host_server; started = FALSE; connection.silence = 90000; }
	CASE("client-hitch") { connection.silence = 20000; expected = FALSE; }
	CASE("client-loading") { started = FALSE; connection.silence = 60000; }
	CASE("client-host-gone") { started = FALSE; connection.silence = 90000; expected = FALSE; }
	CASE("client-heard") { connection.silence = 1000; }
	CHECK(network_game_client_idle_ingame(&client) == expected, "the frame %s the game",
		expected ? "left" : "did not leave");
	CHECK(shown_error == NONE && !errors_shown, "an error was shown");
	CHECK(messages_processed == (expected ? 1 : 0), "the host's messages were %sread", expected ? "not " : "");
	if (global_server)
		CHECK(connection.timeout == 0, "the host's own client timed its server out after %ld ms", connection.timeout);
	else
		CHECK(connection.timeout == (started ? 15000 : 75000), "a client's timeout changed: %ld ms", connection.timeout);
	return 0;
}
