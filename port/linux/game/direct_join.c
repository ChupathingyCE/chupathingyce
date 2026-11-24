/*
DIRECT_JOIN.C

Joining a game by its address (direct_join.h): the PC version's Direct IP,
kept with System Link, as LAN parties and the old direct ways of playing
had it. The Custom Edition menus' DIRECT IP screen (menu_functions.c), the
Chupathingy menus' X on System Link (which opens that screen), the
console's connect and the command line's --connect (HALO_CONNECT) all come
here, and go on as the System Link list does:

- the address is parsed (port/linux/src/direct_address.h): an IPv4 address,
  a host name (looked up on a thread of its own, IPv4: the game's network
  is), or an IPv6 address, which the game's network can't reach yet (said
  so); then an optional :port, the host's system link port (5150, the
  game's own, if none);
- the network searches as System Link's list does
  (ui_online_games_start_network), and also sends its search to the
  address (network_game_client_search_address): a host on another network,
  or behind a router that forwards the port, answers it there (an older
  host or OpenCE's answers by its broadcast only, which reaches the same
  LAN alone; ours answers a search from the internet only with its
  network.direct_ip_host on, network_server_message_handler.c);
- once the host's advertisement is heard, its game is joined as picking
  it in the list joins it (network_game_client_join_address): the same
  checks of the version and the game, the same words when it can't be
  joined (halo_join_reasons.h, network_game_client_refusal);
- a host not heard from in DIRECT_JOIN_AFTER is joined without its
  advertisement, as the Xbox's console.join does (an OpenCE or retail host
  across the internet, whose answer is broadcast): its own refusal says
  what is wrong; a host that never answers is given up on after
  JOIN_TIMEOUT ("The host didn't answer.");
- joined, the lobby opens over the menus, as Online Games opens it.

Privacy (never_show_ips): the address is input only. The screen's field
shows it as stars once entered (a host name as it is), lists name a saved
one by its player's name or "Saved server <n>", and nothing else shows it:
the HUD, chat, the kill feed, stats and Discord know the game by its name,
as any system link game. The log writes a public address as log_address's
tag and a host name as log_address_name's; the console echoes connect
without it, and its history leaves the line out.

The last DIRECT_JOIN_SAVED_MOST addresses are kept in the save folder's
direct_ip.txt (an address and a name a line, a tab between), on this
machine alone, for the screen's list.

Passwords: the game's join has none (server/src/server_commands.c: a
system link join carries no password), so neither has this; a game with a
password is joined from Online Games or its invite, which ask for it. A
host refuses a direct join to a game with a password from the internet
(_rejection_code_bad_password), and the player is told to join from the
invite (JOIN_REASON_PASSWORD_INVITE, network_game_client_rejected_by_game).
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "game/game.h"
#include "interface/player_ui.h"
#include "interface/ui_widget.h"
#include "interface/terminal.h"
#include "saved games/player_profile.h"
#include "saved games/saved_game_files.h"
#include "networking/network_game_globals.h"
#include "networking/network_client_manager.h"
#include "halo_join_reasons.h"
#include "direct_join.h"
#include "lobby_screen.h"
#include "../src/direct_address.h"
#include "../src/log_address.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* the host not heard from this long: joined without its advertisement */
	DIRECT_JOIN_AFTER = 5000,
	/* a host name not looked up in this long: given up */
	LOOKUP_TIMEOUT = 30000,
	/* ... and not joined this long after the start: given up */
	JOIN_TIMEOUT = 15000,
};

#define STATUS_LOOKING "Looking for the game..."
#define STATUS_JOINING "Joining..."
#define STATUS_EMPTY "Enter the host's address or name."
#define STATUS_BAD "That isn't an address."
#define STATUS_BAD_PORT "The port goes from 1 to 65535."
#define STATUS_IPV6 "IPv6 can't be joined yet. Use an IPv4 address or name."
#define STATUS_NOT_FOUND "Couldn't find that host name."
#define STATUS_IN_GAME "Leave the game first."
#define STATUS_NO_NETWORK "Could not start the network."
#define STATUS_BUSY "Already joining a game."

enum direct_join_state
{
	_direct_join_idle,
	/* a host name looked up (the lookup thread) */
	_direct_join_resolving,
	/* the host's advertisement waited for (and, after DIRECT_JOIN_AFTER,
	the host joined without it) */
	_direct_join_looking,
};

/* ---------- the platform layer's and the engine's */

void platform_log(char const *format, ...);
boolean ui_online_games_start_network(void);
boolean create_global_network_game_client(void);
void game_connection_set(short connection);
boolean main_menu_is_active(void);
boolean game_in_progress(void);
char const *pc_menus_screen(char const *name);

/* ---------- globals */

static struct
{
	enum direct_join_state state;
	struct direct_address address;
	/* IPv4, first byte highest, once known */
	unsigned long ipv4;
	unsigned long started;
	boolean direct_tried;
	/* started by the console: its result said there too */
	boolean console;
	/* started by the DIRECT IP screen: given up once it has gone */
	boolean from_screen;
	char status[DIRECT_JOIN_STATUS_SIZE];
	/* as the log may name it */
	char shown[LOG_ADDRESS_SIZE + 8];
} direct_join;

/* when the DIRECT IP screen was last up (direct_join_screen_alive) */
static unsigned long screen_alive_time;

static struct
{
	boolean read;
	short count;
	char text[DIRECT_JOIN_SAVED_MOST][DIRECT_JOIN_TEXT_SIZE];
	char name[DIRECT_JOIN_SAVED_MOST][DIRECT_JOIN_NAME_SIZE];
} saved;

/* ---------- private code */

static void status_set(char const *text)
{
	snprintf(direct_join.status, sizeof(direct_join.status), "%s", text);
	if (direct_join.console && text[0])
		terminal_shell_output("connect: %s", text);
	/* (the console's join said its last) */
	if (direct_join.state == _direct_join_idle)
		direct_join.console = FALSE;
}

/* the log's words for the address: log_address's tag for a public one */
static void shown_set(void)
{
	char text[LOG_ADDRESS_SIZE];

	if (direct_join.address.kind == _direct_address_name)
		log_address_name(direct_join.address.host, text, sizeof(text));
	else
	{
		unsigned char bytes[4];

		bytes[0] = (unsigned char)(direct_join.ipv4 >> 24);
		bytes[1] = (unsigned char)(direct_join.ipv4 >> 16);
		bytes[2] = (unsigned char)(direct_join.ipv4 >> 8);
		bytes[3] = (unsigned char)direct_join.ipv4;
		log_address(bytes, 4, -1, text, sizeof(text));
	}
	if (direct_join.address.port)
		snprintf(direct_join.shown, sizeof(direct_join.shown), "%s port %u", text, direct_join.address.port);
	else
		snprintf(direct_join.shown, sizeof(direct_join.shown), "%s", text);
}

/* (direct_join_platform.c: the save folder's file, by its own path, and a
host name looked up on a thread: 0 looking, 1 found, -1 not) */
int direct_saved_read(char *text, int size);
int direct_saved_write(char const *text);
int direct_lookup_start(char const *host);
int direct_command_line_address(char *text, int size);
int direct_lookup_result(unsigned char *address);
void direct_lookup_cancel(void);

static void saved_read(void)
{
	static char text[DIRECT_JOIN_SAVED_MOST * (DIRECT_JOIN_TEXT_SIZE + DIRECT_JOIN_NAME_SIZE + 2) + 1];
	char *line, *next;

	if (saved.read)
		return;
	saved.read = TRUE;
	saved.count = 0;
	if (!direct_saved_read(text, sizeof(text)))
		return;
	for (line = text; line && *line && saved.count < DIRECT_JOIN_SAVED_MOST; line = next)
	{
		struct direct_address address;
		char *tab;

		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		line[strcspn(line, "\r")] = 0;
		tab = strchr(line, '\t');
		if (tab)
			*tab++ = 0;
		/* (only what parses as an address is kept) */
		if (strlen(line) >= DIRECT_JOIN_TEXT_SIZE || direct_address_parse(line, &address) != _direct_address_ok)
			continue;
		snprintf(saved.text[saved.count], sizeof(saved.text[0]), "%s", line);
		snprintf(saved.name[saved.count], sizeof(saved.name[0]), "%s", tab ? tab : "");
		saved.count++;
	}
	csmemset(text, 0, sizeof(text));
}

static void saved_write(void)
{
	static char text[DIRECT_JOIN_SAVED_MOST * (DIRECT_JOIN_TEXT_SIZE + DIRECT_JOIN_NAME_SIZE + 2) + 1];
	size_t used = 0;
	short index;

	text[0] = 0;
	for (index = 0; index < saved.count; index++)
	{
		int length = snprintf(text + used, sizeof(text) - used, "%s\t%s\n", saved.text[index], saved.name[index]);

		if (length < 0 || (size_t)length >= sizeof(text) - used)
			break;
		used += (size_t)length;
	}
	if (!direct_saved_write(text))
		platform_log("direct join: could not keep the address in the save folder");
	csmemset(text, 0, sizeof(text));
}

/* the address joined, first in the list (its name kept, if it had one) */
static void saved_add(char const *text)
{
	char name[DIRECT_JOIN_NAME_SIZE] = "";
	short index, found = NONE;

	saved_read();
	for (index = 0; index < saved.count; index++)
	{
		if (!strcmp(saved.text[index], text))
			found = index;
	}
	if (found != NONE)
	{
		snprintf(name, sizeof(name), "%s", saved.name[found]);
		for (index = found; index > 0; index--)
		{
			memcpy(saved.text[index], saved.text[index - 1], sizeof(saved.text[0]));
			memcpy(saved.name[index], saved.name[index - 1], sizeof(saved.name[0]));
		}
	}
	else
	{
		if (saved.count < DIRECT_JOIN_SAVED_MOST)
			saved.count++;
		for (index = (short)(saved.count - 1); index > 0; index--)
		{
			memcpy(saved.text[index], saved.text[index - 1], sizeof(saved.text[0]));
			memcpy(saved.name[index], saved.name[index - 1], sizeof(saved.name[0]));
		}
	}
	snprintf(saved.text[0], sizeof(saved.text[0]), "%s", text);
	snprintf(saved.name[0], sizeof(saved.name[0]), "%s", name);
	saved_write();
}

/* the first player in, with the profile System Link's Start would pick (as
Online Games' join: browser_screen.c's join_first_player) */
static void join_first_player(void)
{
	struct player_profile profile;
	long profile_index = player_ui_get_player1_last_used_profile_index();

	player_ui_local_player_joined_multiplayer_game(0);
	if (profile_index == NONE || !TEST_FLAG(profile_index, _saved_game_file_index_valid_bit))
	{
		long profile_indices[100];
		word profile_count = NUMBEROF(profile_indices);

		player_profiles_enumerate_available_to_local_player_index(0, &profile_count, profile_indices, FALSE);
		profile_index = profile_count ? profile_indices[0] : NONE;
	}
	if (profile_index != NONE && TEST_FLAG(profile_index, _saved_game_file_index_valid_bit) &&
		player_profile_get(profile_index, &profile))
	{
		player_ui_set_active_player_profile(0, profile_index, &profile);
	}
}

static void finish(void)
{
	network_game_client_search_address(0, 0);
	direct_join.state = _direct_join_idle;
}

/* the address known (looked up, or typed): the network searching for it */
static void look_for_host(void)
{
	shown_set();
	if (!ui_online_games_start_network() && !global_network_game_client_get())
	{
		if (!create_global_network_game_client())
		{
			status_set(STATUS_NO_NETWORK);
			finish();
			return;
		}
		game_connection_set(_game_connection_network_client);
	}
	network_game_client_search_address(direct_join.ipv4, direct_join.address.port);
	join_first_player();
	direct_join.state = _direct_join_looking;
	direct_join.started = system_milliseconds();
	direct_join.direct_tried = FALSE;
	status_set(STATUS_LOOKING);
	platform_log("direct join: looking for the game at %s", direct_join.shown);
}

/* the join under way: the lobby up, as Online Games opens it */
static void joined(void)
{
	finish();
	status_set(STATUS_JOINING);
	ui_widgets_close_all();
	lobby_screen_joined_from_list();
	ui_widget_load_by_name_or_tag(
		pc_menus_screen("ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen"),
		NONE, NULL, NONE, NONE, NONE, NONE);
}

/* ---------- public code */

boolean direct_join_start(
	char const *text)
{
	struct direct_address address;
	short client_state = NONE;
	void *client = global_network_game_client_get();

	direct_join.status[0] = 0;
	if (direct_join.state != _direct_join_idle)
	{
		status_set(STATUS_BUSY);
		return FALSE;
	}
	if (client && network_game_client_get_state(client, NULL) != 0)
		client_state = network_game_client_get_state(client, NULL);
	if (global_network_game_server_get() || (client_state != NONE && client_state != 0) ||
		(game_in_progress() && !main_menu_is_active()))
	{
		status_set(STATUS_IN_GAME);
		return FALSE;
	}
	switch (direct_address_parse(text, &address))
	{
	case _direct_address_ok:
		break;
	case _direct_address_empty:
		status_set(STATUS_EMPTY);
		return FALSE;
	case _direct_address_bad_port:
		status_set(STATUS_BAD_PORT);
		return FALSE;
	default:
		status_set(STATUS_BAD);
		return FALSE;
	}
	if (address.kind == _direct_address_ipv6)
	{
		status_set(STATUS_IPV6);
		return FALSE;
	}
	direct_join.address = address;
	direct_join.from_screen = screen_alive_time && system_milliseconds() - screen_alive_time < 500;
	saved_add(text);
	/* (localhost is this machine, RFC 6761: no lookup) */
	if (address.kind == _direct_address_name && !strcmp(address.host, "localhost"))
	{
		address.kind = _direct_address_ipv4;
		address.ipv4 = 0x7F000001UL;
		direct_join.address = address;
	}
	if (address.kind == _direct_address_ipv4)
	{
		direct_join.ipv4 = address.ipv4;
		look_for_host();
		return direct_join.state != _direct_join_idle;
	}
	/* a host name: looked up on the lookup thread */
	{
		char tag[LOG_ADDRESS_SIZE];

		if (!direct_lookup_start(address.host))
		{
			status_set(STATUS_NOT_FOUND);
			return FALSE;
		}
		direct_join.state = _direct_join_resolving;
		direct_join.started = system_milliseconds();
		status_set(STATUS_LOOKING);
		platform_log("direct join: looking up %s", log_address_name(address.host, tag, sizeof(tag)));
	}
	return TRUE;
}

boolean direct_join_saved_start(
	short index)
{
	char text[DIRECT_JOIN_TEXT_SIZE];

	saved_read();
	if (index < 0 || index >= saved.count)
		return FALSE;
	snprintf(text, sizeof(text), "%s", saved.text[index]);
	return direct_join_start(text);
}

short direct_join_saved_count(
	void)
{
	saved_read();
	return saved.count;
}

boolean direct_join_saved_name(
	short index,
	char *name,
	int size)
{
	saved_read();
	if (index < 0 || index >= saved.count)
		return FALSE;
	direct_address_saved_name(saved.name[index], saved.text[index], index + 1, name, (size_t)size);
	return TRUE;
}

boolean direct_join_active(
	void)
{
	return direct_join.state != _direct_join_idle;
}

char const *direct_join_status(
	void)
{
	return direct_join.status;
}

void direct_join_screen_alive(
	void)
{
	screen_alive_time = system_milliseconds();
	if (!screen_alive_time)
		screen_alive_time = 1;
}

void direct_join_cancel(
	void)
{
	if (direct_join.state == _direct_join_idle)
		return;
	direct_lookup_cancel();
	finish();
	direct_join.status[0] = 0;
	platform_log("direct join: given up");
}

/* the command line's --connect <address> (or HALO_CONNECT), once the main
menu is up */
static void command_line_connect(void)
{
	static boolean done = FALSE;
	char text[DIRECT_JOIN_TEXT_SIZE];

	if (done || !main_menu_is_active())
		return;
	done = TRUE;
	if (!direct_command_line_address(text, sizeof(text)))
		return;
	platform_log("direct join: an address from the command line");
	if (!direct_join_start(text))
		platform_log("direct join: not joined (%s)", direct_join.status);
	csmemset(text, 0, sizeof(text));
}

void direct_join_update(
	void)
{
	unsigned long now;

	/* (first: a join it starts is timed from now on) */
	command_line_connect();
	now = system_milliseconds();
	switch (direct_join.state)
	{
	case _direct_join_idle:
		return;
	default:
		break;
	}
	/* (the screen that started it closed: B backed out of it) */
	if (direct_join.from_screen && now - screen_alive_time > 1000)
	{
		direct_join_cancel();
		return;
	}
	switch (direct_join.state)
	{
	case _direct_join_resolving:
	{
		unsigned char bytes[4];
		int result = direct_lookup_result(bytes);

		if (!result)
		{
			if (now - direct_join.started > LOOKUP_TIMEOUT)
			{
				direct_lookup_cancel();
				finish();
				status_set(STATUS_NOT_FOUND);
				platform_log("direct join: the host name's lookup took too long");
			}
			return;
		}
		if (result < 0)
		{
			finish();
			status_set(STATUS_NOT_FOUND);
			platform_log("direct join: the host name was not found");
			return;
		}
		direct_join.ipv4 = (unsigned long)bytes[0] << 24 | (unsigned long)bytes[1] << 16 |
			(unsigned long)bytes[2] << 8 | (unsigned long)bytes[3];
		look_for_host();
		return;
	}
	case _direct_join_looking:
	{
		void *client = global_network_game_client_get();
		boolean heard = network_game_client_address_advertised(direct_join.ipv4);
		boolean direct = !heard && !direct_join.direct_tried && now - direct_join.started > DIRECT_JOIN_AFTER;

		if (!client)
		{
			finish();
			status_set(STATUS_NO_NETWORK);
			return;
		}
		if (network_game_client_join_address(direct_join.ipv4, direct_join.address.port, direct))
		{
			platform_log("direct join: joining the game at %s%s", direct_join.shown,
				direct ? " (it sent no advertisement: joined directly)" : "");
			joined();
			return;
		}
		if (direct)
			direct_join.direct_tried = TRUE;
		/* (heard, and refused by the checks a listed game has: their words) */
		if (heard && network_game_client_refusal()[0])
		{
			finish();
			status_set(network_game_client_refusal());
			platform_log("direct join: the game at %s can't be joined: %s", direct_join.shown,
				network_game_client_refusal());
			return;
		}
		if (now - direct_join.started > JOIN_TIMEOUT)
		{
			finish();
			status_set(JOIN_REASON_NO_ANSWER);
			platform_log("direct join: no answer from %s", direct_join.shown);
		}
		return;
	}
	default:
		finish();
		return;
	}
}

void direct_join_console(
	char const *arguments)
{
	char text[DIRECT_JOIN_TEXT_SIZE];
	size_t length;

	while (*arguments == ' ' || *arguments == '\t')
		arguments++;
	length = strcspn(arguments, " \t");
	if (!length)
	{
		terminal_shell_output("usage: connect <address>[:port]");
		return;
	}
	if (length >= sizeof(text))
		length = sizeof(text) - 1;
	memcpy(text, arguments, length);
	text[length] = 0;
	arguments += length;
	while (*arguments == ' ' || *arguments == '\t')
		arguments++;
	/* (Halo PC's connect took a password after the address: a system link
	join has none to give) */
	if (*arguments)
		terminal_shell_note("connect: a direct join has no password; a game with one is joined from Online Games");
	direct_join.console = TRUE;
	direct_join_start(text);
	direct_join.console = direct_join.state != _direct_join_idle;
	memset(text, 0, sizeof(text));
}

#endif
