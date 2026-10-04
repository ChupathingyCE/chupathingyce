/*
PROBE.C

The game list's probe (server/README.md): with HALO_PROBE naming an
invite (its digits, as halo://join/ is followed by), the game reads the
game that invite leads to and quits. halo.milenko.org lists games hosted
by copies without the game list this way: a signed-in player gives their
invite, the site probes it to show what it leads to, and probes it again
while it is listed. Built into the game browser's builds (configure.py
--game-browser); without HALO_PROBE it does nothing.

It runs without a window, sound or a player, as the dedicated server
(browser_headless). Each frame (main.c) it:
  - waits for the main menu, and starts the network searching for games,
    as System Link's list does;
  - joins the invite's tunnel (p2p.c), through which the host advertises
    its game as on a LAN; it never joins the game itself, so it takes no
    place in it;
  - once the game is advertised, prints it on standard output as one line,
    "probe: " and a JSON object, and quits (0);
  - if nothing is advertised in time, or the invite is not one, prints why
    the same way and quits (1).

The line: {"ok": true, "name": ..., "map": ..., "engine": "slayer",
"players": 3, "maximum_players": 12, "open": true, "teams": false,
"network_version": 10, "compatible": true, "machines": 3, "score_limit":
25, "in_progress": true}, or {"ok": false, "error": ...}.

That is all a game's advertisement holds: no player's name or score. Those
travel only to machines that have joined the game, which the probe never
does.
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "networking/network_client_manager.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* how long the host has to advertise its game, from the tunnel's join */
	PROBE_TIMEOUT_MILLISECONDS = 20000,
	/* frames, as the dedicated server's: the rest of the frame waits */
	PROBE_FRAME_MILLISECONDS = 1000 / 30,
	/* "halo://join/" and the longest invite */
	PROBE_LINK_LENGTH = 12 + 64 + 1,
};

/* ---------- prototypes */

char const *browser_probe(void);
int p2p_join_invite(char const *text);
boolean create_global_network_game_client(void);
void dispose_global_network_game_client(void);
void dispose_global_network_game_server(void);
void game_connection_set(short connection);
boolean main_menu_is_active(void);
boolean bink_playback_active(void);

/* ---------- globals */

static struct
{
	boolean initialized;
	boolean active;
	boolean joined;
	unsigned long join_time;
	unsigned long frame_time;
	char invite[65];
} probe;

/* ---------- private code */

static void initialize(
	void)
{
	char const *invite = browser_probe();
	long length = invite ? (long)strlen(invite) : 0;

	probe.initialized = TRUE;
	if (!invite)
		return;
	probe.active = TRUE;
	if (length >= (long)sizeof(probe.invite))
		length = (long)sizeof(probe.invite) - 1;
	csmemcpy(probe.invite, invite, (unsigned long)length);
	probe.invite[length] = 0;
}

/* the result, printed (text: already JSON's), and the game quit */
static void finish(
	char const *json,
	int status)
{
	printf("probe: %s\n", json);
	fflush(stdout);
	exit(status);
}

static void fail(
	char const *why)
{
	char json[256];

	snprintf(json, sizeof(json), "{\"ok\": false, \"error\": \"%s\"}", why);
	finish(json, 1);
}

/* text into a JSON string's body: quotes, backslashes and controls escaped */
static long json_text(
	char *out,
	long size,
	char const *text)
{
	long used = 0;

	for (; *text && used < size - 7; text++)
	{
		unsigned char character = (unsigned char)*text;

		if (character == '"' || character == '\\')
		{
			out[used++] = '\\';
			out[used++] = (char)character;
		}
		else if (character < 0x20)
		{
			used += snprintf(out + used, (size_t)(size - used), "\\u%04x", character);
		}
		else
		{
			out[used++] = (char)character;
		}
	}
	out[used] = 0;
	return used;
}

/* the game's name (UTF-16, as the advertisement has it) as UTF-8 */
static void utf8_name(
	wchar_t const *name,
	long length,
	char *text,
	long size)
{
	long used = 0;
	long index;

	for (index = 0; index < length && name[index] && used < size - 4; index++)
	{
		unsigned int code = (unsigned short)name[index];

		if (code < 0x80)
		{
			text[used++] = (char)code;
		}
		else if (code < 0x800)
		{
			text[used++] = (char)(0xC0 | (code >> 6));
			text[used++] = (char)(0x80 | (code & 0x3F));
		}
		else
		{
			text[used++] = (char)(0xE0 | (code >> 12));
			text[used++] = (char)(0x80 | ((code >> 6) & 0x3F));
			text[used++] = (char)(0x80 | (code & 0x3F));
		}
	}
	text[used] = 0;
}

static char const *engine_name(
	short engine_type)
{
	static char const *const names[] = { "none", "ctf", "slayer", "oddball", "king", "race" };

	return engine_type >= 0 && engine_type < (short)NUMBEROF(names) ? names[engine_type] : "unknown";
}

static void report(
	struct network_invite_advertisement const *advertisement)
{
	char name[NUMBEROF(advertisement->game_name) * 3 + 1];
	char escaped_name[sizeof(name) * 2];
	char escaped_map[0x80 * 2];
	char const *map = advertisement->map_name;
	char const *cursor;
	char json[1024];

	/* (the map's name, without its path: levels\test\bloodgulch\bloodgulch) */
	for (cursor = advertisement->map_name; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			map = cursor + 1;
	}
	utf8_name(advertisement->game_name, NUMBEROF(advertisement->game_name), name, sizeof(name));
	json_text(escaped_name, sizeof(escaped_name), name);
	json_text(escaped_map, sizeof(escaped_map), map);
	snprintf(json, sizeof(json),
		"{\"ok\": true, \"name\": \"%s\", \"map\": \"%s\", \"engine\": \"%s\", \"players\": %d, "
		"\"maximum_players\": %d, \"open\": %s, \"teams\": %s, \"network_version\": %u, \"compatible\": %s, "
		"\"machines\": %d, \"score_limit\": %d, \"in_progress\": %s}",
		escaped_name, escaped_map, engine_name(advertisement->engine_type), advertisement->player_count,
		advertisement->maximum_player_count, advertisement->open ? "true" : "false",
		advertisement->has_teams ? "true" : "false", (unsigned int)advertisement->network_version,
		advertisement->compatible ? "true" : "false", advertisement->machine_count, advertisement->score_limit,
		advertisement->in_progress ? "true" : "false");
	finish(json, 0);
}

/* ---------- public code */

boolean probe_active(
	void)
{
	if (!probe.initialized)
		initialize();
	return probe.active;
}

void probe_update(
	void)
{
	struct network_invite_advertisement advertisement;
	long found;

	if (!probe_active())
		return;

	/* (the rest of the frame waits: the probe spins otherwise) */
	{
		unsigned long now = system_milliseconds();
		unsigned long elapsed = now - probe.frame_time;

		if (probe.frame_time && elapsed < PROBE_FRAME_MILLISECONDS)
			Sleep(PROBE_FRAME_MILLISECONDS - elapsed);
		probe.frame_time = system_milliseconds();
	}

	if (!probe.joined)
	{
		char link[PROBE_LINK_LENGTH];

		if (!main_menu_is_active() || bink_playback_active())
			return;
		/* the network searching for games, as System Link's list starts it,
		then the invite's tunnel, through which its host advertises */
		dispose_global_network_game_client();
		dispose_global_network_game_server();
		if (!create_global_network_game_client())
			fail("the network could not start");
		game_connection_set(_game_connection_network_client);
		snprintf(link, sizeof(link), "halo://join/%s", probe.invite);
		if (!p2p_join_invite(link))
			fail("not an invite");
		probe.joined = TRUE;
		probe.join_time = system_milliseconds();
		return;
	}

	found = network_game_client_invite_host_advertisement(probe.invite, &advertisement);
	if (found < 0)
		fail("not an invite");
	if (found > 0)
		report(&advertisement);
	if (system_milliseconds() - probe.join_time > PROBE_TIMEOUT_MILLISECONDS)
		fail("no answer from the host");
}

#endif
