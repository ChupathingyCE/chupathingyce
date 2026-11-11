/*
MAP_SHARE_CLIENT.C

The map bot's fetch (map_share.h, port/linux/README.md, "Map sharing"), and a
player's download of a missing map from the game's host (below); the bot's
is a headless run with
debug.network_test = "fetch:<host>" joins one host's game, takes its
current Custom Edition map from it, writes it and a result, leaves and
quits. It takes the least it can from the host:

- it joins as a machine and never adds a player (no player slot); its
  machine is named "Chupathingy map bot";
- it never loads the game: the game record's map name and version are all
  it reads of it;
- a version it is told it holds (debug.map_fetch_held), or a map other than
  the one it came for (debug.map_fetch_name), it leaves at once without
  asking;
- it asks for the map file alone, never a resource map;
- it downloads from the host alone, over the host's port 5153: the sources
  list (85, other players' addresses) is dropped unread, and it listens on
  no port;
- it reports its progress every 2 seconds, then done or failed, and leaves.

<host> is a system link address (a.b.c.d, or a.b.c.d:port for a host not on
the game's port) or an internet invite (internet play, network.online).

The file goes in debug.map_fetch_folder as <name>.map.part, renamed to
<name>.map once every piece and the whole file's hash check. The result, a
JSON object, goes in debug.map_fetch_result (else result.json in the
folder): "result" fetched, held or failed; "reason" in the map repository's
words for a fetch that failed (refused, full, sharing off, unavailable, not
shared, timed out, too large, bad data, not reachable, failed); the level,
the version, the size and the file's hash (hex) when known; the players the
host's game record listed. The exit code: 0 fetched, 10 held, 1 any other.

A player's download from the host (map_share_player_start, HALO_HOST_MAP_
DOWNLOADS: the desktops and Android) is the same fetch run by the Online
Games screen (browser_screen.c) for a map the repositories didn't give,
only as network.map_downloads_from_host allows (map_download.c, "From the
host"). It joins the host the player was joining as a machine with no
player, under the player's own machine name, asks for the map named in the
offer alone, and leaves for the game to be joined again once it is done.
On top of the bot's checks: the offered map hash is held to the blacklist
before anything is downloaded; the size cap is the player's
(network.map_downloads_from_host_limit); the file goes into the quarantine
folder (maps_quarantine), never into maps_ce, and map_download.c checks it
and moves it in. Its progress is map_download_get's (the screen's bar), and
the player's cancel (map_download_cancel) ends it within a frame. Nothing
it logs names an address.
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "bungie_net/network/transport.h"
#include "bungie_net/network/transport_endpoint.h"
#include "game/game.h"
#include "networking/network_game_globals.h"
#include "networking/network_client_manager.h"
#include "map_share.h"
#include "../src/map_share_files.h"
#ifdef HALO_HOST_MAP_DOWNLOADS
#include "../src/map_download.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the platform layer's */
const char *config_string(char const *name);
long config_integer(char const *name);
void platform_log(char const *format, ...);
int p2p_join_invite(const char *text);
/* network_client_manager.c's */
long network_game_client_map_fetch_state(void);
short network_game_client_map_fetch_rejection(void);
boolean network_game_client_join_address(unsigned long address, word port, boolean direct);
long network_game_client_join_invite_host(char const *invite);
/* network_game_globals.c's */
boolean network_distributed_client_send_reliably(void *message, word size);
void game_connection_set(short connection);
#ifdef HALO_HOST_MAP_DOWNLOADS
/* (ui_widget_event_handler_functions.c's: the network searching anew, the
host's game left; platform.h's) */
boolean ui_online_games_start_network(void);
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);
#endif

/* ---------- constants */

#define MAP_FETCH_MACHINE_NAME "Chupathingy map bot"

enum
{
	JOIN_TIMEOUT = 20000,
	DIRECT_JOIN_AFTER = 5000,
	RECORD_TIMEOUT = 15000,
	OFFER_TIMEOUT = 30000,
	SILENCE_TIMEOUT = 30000,
	PIECE_TIMEOUT = 60000,
	PROGRESS_INTERVAL = 2000,
	KEEP_ALIVE_INTERVAL = 10000,
	LEAVE_DELAY = 1000,
	OUTSTANDING = 2,
	MAXIMUM_HELD = 64,
	/* the most a map may be (the clients' cap, 768 MiB) */
	MAP_LIMIT = 0x30000000,
	INPUT_SIZE = 4 + MAP_SHARE_FRAME_MAXIMUM,
	EXIT_FETCHED = 0,
	EXIT_FAILED = 1,
	EXIT_HELD = 10,
};

enum
{
	_fetch_set_up,
	_fetch_joining,
	_fetch_waiting_for_record,
	_fetch_asked,
	_fetch_streaming,
	_fetch_leaving,
};

/* ---------- globals */

static struct
{
	short fetching;
	/* a player's download from the host (map_share_player_start), not the
	bot's */
	boolean player;
	boolean cancelled;
	/* (the blacklist refused the map offered) */
	boolean blocked;
	short state;
	unsigned long state_time;
	unsigned long started;
	real menu_seconds;
	char target[160];
	boolean invite;
	unsigned long address;
	word port;
	boolean direct_tried;
	boolean joined;
	unsigned long held[MAXIMUM_HELD];
	short held_count;
	unsigned long maximum;
	char folder[512];
	char result_path[600];
	/* the map come for (its file's name), empty for any */
	char wanted[MAP_SHARE_LEVEL_SIZE];
	/* the game record */
	boolean record;
	boolean record_changed;
	char level[MAP_SHARE_LEVEL_SIZE];
	char file[MAP_SHARE_LEVEL_SIZE];
	unsigned long version;
	short players;
	/* the offer */
	boolean offered;
	unsigned long size;
	byte hash[MAP_SHARE_HASH_SIZE];
	/* the stream */
	struct transport_endpoint *endpoint;
	byte input[INPUT_SIZE];
	long input_used;
	unsigned long last_received;
	unsigned long last_sent;
	unsigned long last_piece;
	unsigned long last_progress;
	boolean hello_answered;
	boolean have_all;
	byte *hashes;
	unsigned long hash_count;
	unsigned long piece_count;
	boolean hashes_checked;
	unsigned long next_piece;
	struct
	{
		boolean used;
		unsigned long piece;
		unsigned long received;
		byte *data;
	} slots[OUTSTANDING];
	unsigned long had;
	void *part;
	char part_path[600];
	char final_path[600];
	/* the end */
	int exit_code;
} fetch;

/* ---------- private code */

static void fetch_joined_update(unsigned long now);

static unsigned long fetch_now(
	void)
{
	unsigned long now = system_milliseconds();

	return now ? now : 1;
}

static void fetch_state(
	short state)
{
	fetch.state = state;
	fetch.state_time = fetch_now();
}

static void fetch_send(
	unsigned char type,
	unsigned char const *body,
	short body_size)
{
	unsigned char message[MAP_SHARE_DISTRIBUTED_HEADER_SIZE + MAP_SHARE_REQUEST_SIZE];
	short size;

	if (body_size > (short)(sizeof(message) - MAP_SHARE_DISTRIBUTED_HEADER_SIZE))
		return;
	/* (no game runs on this machine: its time 0) */
	size = map_share_message_build(message, type, body, body_size, 0);
	network_distributed_client_send_reliably(message, (word)size);
}

static void fetch_progress(
	byte state)
{
	unsigned char body[MAP_SHARE_PROGRESS_SIZE];

	csmemset(body, 0, sizeof(body));
	body[0] = state;
	map_share_put32(body + 4, fetch.had);
	map_share_put32(body + 8, fetch.size);
	fetch_send(MAP_SHARE_MESSAGE_PROGRESS, body, sizeof(body));
	fetch.last_progress = fetch_now();
}

/* text as a JSON string's insides */
static void fetch_json_text(
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
		else if (character < 0x20 || character > 0x7E)
			used += snprintf(out + used, (size_t)(size - used), "\\u%04x", character);
		else
			out[used++] = (char)character;
	}
	out[used] = 0;
}

static void fetch_release(
	void)
{
	short index;

	if (fetch.endpoint)
	{
		delete_transport_endpoint(fetch.endpoint);
		fetch.endpoint = NULL;
	}
	if (fetch.part)
	{
		map_share_part_close(fetch.part, fetch.part_path, NULL, 0);
		fetch.part = NULL;
	}
	for (index = 0; index < OUTSTANDING; index++)
	{
		if (fetch.slots[index].data)
			free(fetch.slots[index].data);
		fetch.slots[index].data = NULL;
	}
	if (fetch.hashes)
		free(fetch.hashes);
	fetch.hashes = NULL;
}

/* the end: the result written, the host told if it was answering, and the
game left a moment later */
static void fetch_finish(
	char const *result,
	char const *reason,
	char const *detail)
{
	char json[2048];
	char level[160];
	char detail_text[400];
	char hash[2 * MAP_SHARE_HASH_SIZE + 1];
	char version[16];
	short index;

	if (fetch.state == _fetch_leaving)
		return;
	if (fetch.offered && strcmp(result, "fetched"))
		fetch_progress(MAP_SHARE_PROGRESS_FAILED);
	fetch_release();
#ifdef HALO_HOST_MAP_DOWNLOADS
	/* (a player's: map_download.c takes the file, or why it failed) */
	if (fetch.player)
	{
		if (!strcmp(result, "fetched"))
		{
			platform_log("map download: %s.map from the host is whole (%lu bytes, every piece checked): checking it",
				fetch.wanted, fetch.size);
			map_download_host_received(fetch.hash);
		}
		else
			map_download_host_failed(detail && detail[0] ? detail : "the host did not give the map", fetch.cancelled,
				fetch.blocked);
		fetch_state(_fetch_leaving);
		return;
	}
#endif
	hash[0] = 0;
	if (fetch.offered)
	{
		for (index = 0; index < MAP_SHARE_HASH_SIZE; index++)
			snprintf(hash + 2 * index, 3, "%02x", fetch.hash[index]);
	}
	version[0] = 0;
	if (fetch.record)
		snprintf(version, sizeof(version), "%08lx", fetch.version);
	fetch_json_text(level, sizeof(level), fetch.level);
	fetch_json_text(detail_text, sizeof(detail_text), detail ? detail : "");
	snprintf(json, sizeof(json),
		"{\"result\": \"%s\", \"reason\": \"%s\", \"detail\": \"%s\", \"level\": \"%s\", \"name\": \"%s\", "
		"\"version\": \"%s\", \"size\": %lu, \"map_hash\": \"%s\", \"file\": \"%s%s\", \"players\": %d, "
		"\"seconds\": %.1f, \"machine\": \"%s\"}\n",
		result, reason ? reason : "", detail_text, level, fetch.file, version, fetch.offered ? fetch.size : 0UL, hash,
		!strcmp(result, "fetched") ? fetch.file : "", !strcmp(result, "fetched") ? ".map" : "", (int)fetch.players,
		(double)(fetch_now() - fetch.started) / 1000.0, MAP_FETCH_MACHINE_NAME);
	if (!map_share_write_text(fetch.result_path, json))
		platform_log("map fetch: the result could not be written");
	platform_log("map fetch: %s%s%s%s", result, reason && reason[0] ? " (" : "", reason ? reason : "",
		reason && reason[0] ? ")" : "");
	if (detail && detail[0])
		platform_log("map fetch: %s", detail);
	fetch.exit_code = !strcmp(result, "fetched") ? EXIT_FETCHED : !strcmp(result, "held") ? EXIT_HELD : EXIT_FAILED;
	fetch_state(_fetch_leaving);
}

static void fetch_read_settings(
	void)
{
	char const *setting = config_string("debug.network_test");
	char const *held = config_string("debug.map_fetch_held");
	char const *folder = config_string("debug.map_fetch_folder");
	char const *result = config_string("debug.map_fetch_result");
	char const *wanted = config_string("debug.map_fetch_name");
	long maximum = config_integer("debug.map_fetch_maximum");
	unsigned int parts[4];
	unsigned int port = 0;
	char tail;

	snprintf(fetch.target, sizeof(fetch.target), "%s", setting + 6);
	snprintf(fetch.folder, sizeof(fetch.folder), "%s", folder);
	snprintf(fetch.wanted, sizeof(fetch.wanted), "%s", wanted);
	if (result[0])
		snprintf(fetch.result_path, sizeof(fetch.result_path), "%s", result);
	else
		snprintf(fetch.result_path, sizeof(fetch.result_path), "%s/result.json", folder[0] ? folder : ".");
	fetch.maximum = (unsigned long)(maximum <= 0 ? 512 : MIN(maximum, 512)) << 20;
	if (fetch.maximum > MAP_LIMIT)
		fetch.maximum = MAP_LIMIT;
	/* a.b.c.d[:port], else an invite */
	if ((sscanf(fetch.target, "%u.%u.%u.%u%c", &parts[0], &parts[1], &parts[2], &parts[3], &tail) == 4 ||
			(sscanf(fetch.target, "%u.%u.%u.%u:%u%c", &parts[0], &parts[1], &parts[2], &parts[3], &port, &tail) == 5 &&
				port > 0 && port < 65536)) &&
		parts[0] < 256 && parts[1] < 256 && parts[2] < 256 && parts[3] < 256)
	{
		fetch.address = ((unsigned long)parts[0] << 24) | ((unsigned long)parts[1] << 16) |
			((unsigned long)parts[2] << 8) | (unsigned long)parts[3];
		fetch.port = (word)port;
	}
	else
		fetch.invite = TRUE;
	/* the versions held: 8 hex digits each, any separator between them */
	while (*held && fetch.held_count < MAXIMUM_HELD)
	{
		char *end;
		unsigned long version;

		if (!((*held >= '0' && *held <= '9') || (*held >= 'a' && *held <= 'f') || (*held >= 'A' && *held <= 'F')))
		{
			held++;
			continue;
		}
		version = strtoul(held, &end, 16);
		if (end == held)
			break;
		fetch.held[fetch.held_count++] = version;
		held = end;
	}
}

static boolean fetch_held(
	unsigned long version)
{
	short index;

	for (index = 0; index < fetch.held_count; index++)
	{
		if (fetch.held[index] == version)
			return TRUE;
	}
	return FALSE;
}

static void fetch_join(
	unsigned long now)
{
	long state = network_game_client_map_fetch_state();
	short rejection = network_game_client_map_fetch_rejection();

	if (rejection)
	{
		fetch_finish("failed", rejection == 1 ? "full" : "refused",
			rejection == 1 ? "the host's game is full" : "the host refused the join");
		return;
	}
	if (state >= 2)
	{
		fetch.joined = TRUE;
		platform_log("map fetch: joined the host's game (no player)");
		fetch_state(_fetch_waiting_for_record);
		return;
	}
	if ((long)(now - fetch.started) > JOIN_TIMEOUT)
	{
		fetch_finish("failed", "not reachable", "the host did not take the join in 20 seconds");
		return;
	}
	if (state != 0)
		return;
	if (fetch.invite)
	{
		long joined = network_game_client_join_invite_host(fetch.target);

		if (joined < 0)
			fetch_finish("failed", "refused", "the invite's game cannot be joined");
	}
	else
	{
		boolean direct = !fetch.direct_tried && (long)(now - fetch.started) > DIRECT_JOIN_AFTER;

		if (network_game_client_join_address(fetch.address, fetch.port, direct))
			platform_log("map fetch: joining the host");
		if (direct)
			fetch.direct_tried = TRUE;
	}
}

static void fetch_ask(
	void)
{
	unsigned char body[MAP_SHARE_REQUEST_SIZE];

	if (!map_share_level_file(fetch.level, fetch.file))
	{
		fetch.file[0] = 0;
		fetch_finish("failed", "not shared", "the host's map is not a Custom Edition map");
		return;
	}
	if (fetch.wanted[0] && csstrcasecmp(fetch.file, fetch.wanted))
	{
		fetch_finish("failed", "unavailable", "the host is on another map now: left without asking");
		return;
	}
	if (!fetch.version)
	{
		fetch_finish("failed", "not shared", "the host's map has version 0");
		return;
	}
	if (fetch_held(fetch.version))
	{
		fetch_finish("held", "", "that version is held: left without asking");
		return;
	}
#ifdef HALO_HOST_MAP_DOWNLOADS
	/* (a player's: into the quarantine folder, by the name the offer was
	for, map_download.c having emptied its files) */
	if (fetch.player)
	{
		char part[300], whole[300];

		map_download_host_paths(fetch.wanted, part, whole, (int)sizeof(part));
		platform_translate_path(part, fetch.part_path, sizeof(fetch.part_path));
		platform_translate_path(whole, fetch.final_path, sizeof(fetch.final_path));
	}
	else
#endif
	{
		if (!fetch.folder[0])
		{
			fetch_finish("failed", "failed", "no folder for the map (debug.map_fetch_folder)");
			return;
		}
		snprintf(fetch.final_path, sizeof(fetch.final_path), "%s/%s.map", fetch.folder, fetch.file);
		snprintf(fetch.part_path, sizeof(fetch.part_path), "%s/%s.map.part", fetch.folder, fetch.file);
	}
	if (map_share_file_exists(fetch.final_path))
	{
		fetch_finish("failed", "failed", "a file of the map's name is in the folder already");
		return;
	}
	csmemset(body, 0, sizeof(body));
	csstrncpy((char *)body, fetch.level, MAP_SHARE_LEVEL_SIZE - 1);
	map_share_put32(body + 0x40, fetch.version);
	body[0x44] = MAP_SHARE_WANT_MAP;
	fetch_send(MAP_SHARE_MESSAGE_REQUEST, body, sizeof(body));
	platform_log("map fetch: asked for %s (version %08lx)", fetch.level, fetch.version);
	fetch_state(_fetch_asked);
}

static boolean fetch_frame(
	byte type,
	void const *body,
	long size)
{
	byte frame[5 + 64];
	long written = 0;

	if (size > (long)sizeof(frame) - 5)
		return FALSE;
	map_share_put32(frame, (unsigned long)(1 + size));
	frame[4] = type;
	if (size)
		csmemcpy(frame + 5, body, size);
	/* (the frames sent are small: a blocking moment at most) */
	while (written < 5 + size)
	{
		long result = write_endpoint(fetch.endpoint, frame + written, 5 + size - written);

		if (result == _transport_result_operation_would_block)
			continue;
		if (result <= 0)
			return FALSE;
		written += result;
	}
	fetch.last_sent = fetch_now();
	return TRUE;
}

/* the stream to the host's port 5153 */
static void fetch_connect(
	void)
{
	struct network_game_client *client = global_network_game_client_get();
	struct transport_address address = { { { 0 } } };
	byte hello[6 + MAP_SHARE_HASH_SIZE];

	if (!client)
	{
		fetch_finish("failed", "failed", "the game connection is gone");
		return;
	}
	network_game_client_get_remote_server_address(client, &address);
	address.port = MAP_SHARE_PORT;
	fetch.endpoint = create_transport_endpoint(MAP_SHARE_ENDPOINT_TCP);
	if (!fetch.endpoint || connect_endpoint(fetch.endpoint, &address) != _transport_error_none ||
		set_endpoint_blocking(fetch.endpoint, FALSE) != _transport_error_none)
	{
		fetch_finish("failed", "not reachable", "the host's map sharing port did not answer");
		return;
	}
	fetch.last_received = fetch_now();
	fetch.last_piece = fetch.last_received;
	csmemcpy(hello, "MHS1", 4);
	hello[4] = MAP_SHARE_HELLO_VERSION;
	hello[5] = 1;
	csmemcpy(hello + 6, fetch.hash, MAP_SHARE_HASH_SIZE);
	if (!fetch_frame(MAP_SHARE_FRAME_HELLO, hello, sizeof(hello)))
	{
		fetch_finish("failed", "not reachable", "the host closed its map sharing port");
		return;
	}
	platform_log("map fetch: connected to the host's map sharing port");
	fetch_state(_fetch_streaming);
}

static void fetch_ask_pieces(
	void)
{
	short index;

	for (index = 0; index < OUTSTANDING && fetch.next_piece < fetch.piece_count; index++)
	{
		byte body[5];

		if (fetch.slots[index].used)
			continue;
		if (!fetch.slots[index].data)
			fetch.slots[index].data = (byte *)malloc(MAP_SHARE_PIECE_SIZE);
		if (!fetch.slots[index].data)
		{
			fetch_finish("failed", "failed", "out of memory");
			return;
		}
		body[0] = 0;
		map_share_put32(body + 1, fetch.next_piece);
		if (!fetch_frame(MAP_SHARE_FRAME_REQUEST, body, sizeof(body)))
		{
			fetch_finish("failed", "failed", "the host closed the stream");
			return;
		}
		fetch.slots[index].used = TRUE;
		fetch.slots[index].piece = fetch.next_piece++;
		fetch.slots[index].received = 0;
	}
}

/* a block of a piece asked for: FALSE ends the fetch (said) */
static boolean fetch_block(
	byte const *body,
	long size)
{
	unsigned long piece;
	unsigned long offset;
	unsigned long piece_size;
	long length = size - 9;
	short index;

	if (size < 10 || body[0] != 0)
		return TRUE;
	piece = map_share_get32(body + 1);
	offset = map_share_get32(body + 5);
	for (index = 0; index < OUTSTANDING; index++)
	{
		if (fetch.slots[index].used && fetch.slots[index].piece == piece)
			break;
	}
	if (index == OUTSTANDING || length > MAP_SHARE_BLOCK_SIZE)
	{
		fetch_finish("failed", "bad data", "the host sent a block that was not asked for");
		return FALSE;
	}
	piece_size = MIN(fetch.size - piece * MAP_SHARE_PIECE_SIZE, (unsigned long)MAP_SHARE_PIECE_SIZE);
	if (offset != fetch.slots[index].received || offset + (unsigned long)length > piece_size)
	{
		fetch_finish("failed", "bad data", "the host sent a block out of order");
		return FALSE;
	}
	csmemcpy(fetch.slots[index].data + offset, body + 9, length);
	fetch.slots[index].received += (unsigned long)length;
	if (fetch.slots[index].received < piece_size)
		return TRUE;
	{
		byte hash[MAP_SHARE_HASH_SIZE];

		map_share_piece_hash(fetch.slots[index].data, piece_size, hash);
		if (csmemcmp(hash, fetch.hashes + piece * MAP_SHARE_HASH_SIZE, MAP_SHARE_HASH_SIZE))
		{
			fetch_finish("failed", "bad data", "a piece did not match its hash");
			return FALSE;
		}
	}
	if (!map_share_part_write(fetch.part, piece * MAP_SHARE_PIECE_SIZE, fetch.slots[index].data, piece_size))
	{
		fetch_finish("failed", "failed", "the map could not be written");
		return FALSE;
	}
	fetch.slots[index].used = FALSE;
	fetch.had += piece_size;
	fetch.last_piece = fetch_now();
#ifdef HALO_HOST_MAP_DOWNLOADS
	if (fetch.player)
		map_download_host_progress(MAP_DOWNLOAD_RECEIVING, fetch.had, fetch.size);
#endif
	if (fetch.had >= fetch.size)
	{
		void *part = fetch.part;

		fetch.part = NULL;
		if (!map_share_part_close(part, fetch.part_path, fetch.final_path, 1))
		{
			fetch_finish("failed", "failed", "the map could not be renamed into place");
			return FALSE;
		}
		fetch_progress(MAP_SHARE_PROGRESS_DONE);
		fetch_finish("fetched", "", "");
		return FALSE;
	}
	fetch_ask_pieces();
	return fetch.state == _fetch_streaming;
}

/* a whole frame from the host: FALSE ends the fetch (said) */
static boolean fetch_handle_frame(
	byte type,
	byte const *body,
	long size)
{
	if (!fetch.hello_answered)
	{
		if (type != MAP_SHARE_FRAME_HELLO || size != 6 + MAP_SHARE_HASH_SIZE || csmemcmp(body, "MHS1", 4) ||
			body[4] != MAP_SHARE_HELLO_VERSION || body[5] != 1 || csmemcmp(body + 6, fetch.hash, MAP_SHARE_HASH_SIZE))
		{
			fetch_finish("failed", "failed", "the host's stream did not answer as map sharing does");
			return FALSE;
		}
		fetch.hello_answered = TRUE;
		return TRUE;
	}
	switch (type)
	{
	case MAP_SHARE_FRAME_STATE:
		if (size >= 2 && body[0] == 0 && !fetch.have_all)
		{
			byte request = 0;

			if (body[1] != MAP_SHARE_STATE_ALL)
			{
				fetch_finish("failed", "unavailable", "the host does not have the whole map");
				return FALSE;
			}
			fetch.have_all = TRUE;
			if (!fetch_frame(MAP_SHARE_FRAME_HASHES_REQUEST, &request, 1))
			{
				fetch_finish("failed", "failed", "the host closed the stream");
				return FALSE;
			}
		}
		break;
	case MAP_SHARE_FRAME_HASHES:
	{
		unsigned long first;
		unsigned long count;

		if (size < 7 || body[0] != 0 || fetch.hashes_checked)
			break;
		first = map_share_get32(body + 1);
		count = map_share_get16(body + 5);
		if (first != fetch.hash_count || !count || count > MAP_SHARE_HASHES_PER_FRAME ||
			size != 7 + (long)count * MAP_SHARE_HASH_SIZE || first + count > fetch.piece_count)
		{
			fetch_finish("failed", "bad data", "the host's piece hashes were out of order");
			return FALSE;
		}
		csmemcpy(fetch.hashes + first * MAP_SHARE_HASH_SIZE, body + 7, count * MAP_SHARE_HASH_SIZE);
		fetch.hash_count += count;
		if (fetch.hash_count == fetch.piece_count)
		{
			byte whole[MAP_SHARE_HASH_SIZE];

			map_share_file_hash(fetch.size, fetch.hashes, fetch.piece_count, whole);
			if (csmemcmp(whole, fetch.hash, MAP_SHARE_HASH_SIZE))
			{
				fetch_finish("failed", "bad data", "the piece hashes did not make the offered hash");
				return FALSE;
			}
			fetch.hashes_checked = TRUE;
			fetch.part = map_share_part_open(fetch.part_path);
			if (!fetch.part)
			{
				fetch_finish("failed", "failed", "the part file could not be made");
				return FALSE;
			}
			platform_log("map fetch: %lu piece hashes check; downloading %lu bytes", fetch.piece_count, fetch.size);
#ifdef HALO_HOST_MAP_DOWNLOADS
			if (fetch.player)
				map_download_host_progress(MAP_DOWNLOAD_RECEIVING, 0, fetch.size);
#endif
			fetch_ask_pieces();
			return fetch.state == _fetch_streaming;
		}
		break;
	}
	case MAP_SHARE_FRAME_BLOCK:
		if (fetch.hashes_checked)
			return fetch_block(body, size);
		break;
	case MAP_SHARE_FRAME_REFUSE:
		fetch_finish("failed", "refused", "the host refused a piece");
		return FALSE;
	default:
		/* (have, keep-alive, and kinds unknown: ignored) */
		break;
	}
	return TRUE;
}

static void fetch_stream(
	unsigned long now)
{
	for (;;)
	{
		long result = read_endpoint(fetch.endpoint, fetch.input + fetch.input_used, INPUT_SIZE - fetch.input_used);
		long offset = 0;

		if (result == 0 || (result < 0 && result != _transport_result_operation_would_block))
		{
			fetch_finish("failed", "failed", "the host closed the stream");
			return;
		}
		if (result > 0)
			fetch.input_used += result;
		while (fetch.input_used - offset >= 4)
		{
			unsigned long length = map_share_get32(fetch.input + offset);

			if (!length || length > MAP_SHARE_FRAME_MAXIMUM)
			{
				fetch_finish("failed", "bad data", "the host sent a frame out of range");
				return;
			}
			if ((unsigned long)(fetch.input_used - offset - 4) < length)
				break;
			fetch.last_received = now;
			if (!fetch_handle_frame(fetch.input[offset + 4], fetch.input + offset + 5, (long)length - 1))
				return;
			offset += 4 + (long)length;
		}
		if (offset)
		{
			csmemmove(fetch.input, fetch.input + offset, fetch.input_used - offset);
			fetch.input_used -= offset;
		}
		if (result < 0)
			break;
	}
	/* (blocks handled above may have come after now: the time again) */
	now = fetch_now();
	if ((long)(now - fetch.last_received) > SILENCE_TIMEOUT)
		fetch_finish("failed", "timed out", "the host's stream went silent for 30 seconds");
	else if ((long)(now - fetch.last_piece) > PIECE_TIMEOUT)
		fetch_finish("failed", "timed out", "no piece of the map came for a minute");
	else if ((long)(now - fetch.last_sent) >= KEEP_ALIVE_INTERVAL && !fetch_frame(MAP_SHARE_FRAME_KEEP_ALIVE, NULL, 0))
		fetch_finish("failed", "failed", "the host closed the stream");
}

/* ---------- public code */

/* whether this run is the bot's fetch */
static boolean fetch_bot(
	void)
{
	if (!fetch.fetching)
		fetch.fetching = (short)(!strncmp(config_string("debug.network_test"), "fetch:", 6) ? 1 : -1);
	return fetch.fetching > 0;
}

boolean map_share_fetching(
	void)
{
	return fetch_bot() || fetch.player;
}

char const *map_share_fetch_machine_name(
	void)
{
	/* (a player's download keeps the player's machine name) */
	return fetch_bot() ? MAP_FETCH_MACHINE_NAME : NULL;
}

void map_share_fetch_update(
	boolean main_menu_loaded,
	real seconds)
{
	unsigned long now = fetch_now();

	switch (fetch.state)
	{
	case _fetch_set_up:
		if (!main_menu_loaded)
			return;
		/* (the main menu settling first) */
		fetch.menu_seconds += seconds;
		if (fetch.menu_seconds < 2.0f)
			return;
		fetch.started = now;
		fetch_read_settings();
		dispose_global_network_game_client();
		dispose_global_network_game_server();
		if (!create_global_network_game_client())
		{
			fetch_finish("failed", "failed", "the game's network client could not start");
			return;
		}
		game_connection_set(_game_connection_network_client);
		if (fetch.invite && !p2p_join_invite(fetch.target))
		{
			fetch_finish("failed", "not reachable", "that is not an address or an invite");
			return;
		}
		platform_log("map fetch: looking for the host (%s)", fetch.invite ? "an invite" : "a system link address");
		fetch_state(_fetch_joining);
		break;
	case _fetch_joining:
		fetch_join(now);
		break;
	case _fetch_leaving:
		if ((long)(now - fetch.state_time) >= LEAVE_DELAY)
		{
			dispose_global_network_game_client();
			platform_log("map fetch: left");
			exit(fetch.exit_code);
		}
		break;
	default:
		fetch_joined_update(now);
		break;
	}
}

/* ---------- private code: joined */

/* each frame once joined: dropped by the host, the map changed, or a wait
run out, else the next step */
static void fetch_joined_update(
	unsigned long now)
{
	if (network_game_client_map_fetch_state() < 2)
	{
		short rejection = network_game_client_map_fetch_rejection();

		fetch_finish("failed", rejection == 1 ? "full" : "refused",
			fetch.state == _fetch_streaming ? "the host dropped the machine during the download" :
			"the host dropped the machine");
		return;
	}
	if (fetch.record_changed && fetch.state >= _fetch_asked)
	{
		fetch_finish("failed", "unavailable", "the host changed maps");
		return;
	}
	if (fetch.state == _fetch_waiting_for_record)
	{
		if (fetch.record)
			fetch_ask();
		else if ((long)(now - fetch.state_time) > RECORD_TIMEOUT)
			fetch_finish("failed", "timed out", "the host sent no game record");
	}
	else if (fetch.state == _fetch_asked)
	{
		if (fetch.offered)
			fetch_connect();
		else if ((long)(now - fetch.state_time) > OFFER_TIMEOUT)
			fetch_finish("failed", "timed out", "the host did not answer the request in 30 seconds");
	}
	else if (fetch.state == _fetch_streaming)
		fetch_stream(now);
	if ((fetch.state == _fetch_asked || fetch.state == _fetch_streaming) && fetch.offered &&
		(long)(fetch_now() - fetch.last_progress) >= PROGRESS_INTERVAL)
	{
		fetch_progress(MAP_SHARE_PROGRESS_DOWNLOADING);
	}
}

/* ---------- public code: a player's download from the host */

#ifdef HALO_HOST_MAP_DOWNLOADS
boolean map_share_player_start(
	char const *host,
	char const *file,
	unsigned long version)
{
	short fetching;

	if (fetch.player || map_share_fetching() || !host || !host[0])
		return FALSE;
	/* (the network searching, as the screen's join has it: no game joined) */
	if (network_game_client_map_fetch_state() < 0)
		ui_online_games_start_network();
	if (network_game_client_map_fetch_state() != 0)
	{
		platform_log("map download: the host can't be asked for %s.map: a game is joined", file);
		return FALSE;
	}
	if (!map_download_host_begin(file, (unsigned int)version))
		return FALSE;
	fetching = fetch.fetching;
	csmemset(&fetch, 0, sizeof(fetch));
	fetch.fetching = fetching;
	fetch.player = TRUE;
	fetch.invite = TRUE;
	snprintf(fetch.target, sizeof(fetch.target), "%s", host);
	snprintf(fetch.wanted, sizeof(fetch.wanted), "%s", file);
	fetch.maximum = map_download_host_limit();
	fetch.started = fetch_now();
	/* (the rejection of a join before this one forgotten) */
	network_game_client_map_fetch_rejection();
	platform_log("map download: joining the host's game to ask it for %s.map (no player)", file);
	fetch_state(_fetch_joining);
	return TRUE;
}

boolean map_share_player_busy(
	void)
{
	return fetch.player;
}

void map_share_player_update(
	void)
{
	unsigned long now;

	if (!fetch.player)
		return;
	now = fetch_now();
	if (fetch.state != _fetch_leaving && map_download_host_cancelled())
	{
		fetch.cancelled = TRUE;
		fetch_finish("failed", "cancelled", "cancelled");
	}
	switch (fetch.state)
	{
	case _fetch_joining:
		fetch_join(now);
		break;
	case _fetch_leaving:
		/* (a moment for the last progress to reach the host, then the game
		left: the network searching again, for the game to be joined) */
		if ((long)(now - fetch.state_time) >= LEAVE_DELAY)
		{
			short fetching = fetch.fetching;

			ui_online_games_start_network();
			platform_log("map download: left the host's game");
			csmemset(&fetch, 0, sizeof(fetch));
			fetch.fetching = fetching;
		}
		break;
	default:
		fetch_joined_update(now);
		break;
	}
}
#endif

boolean map_share_fetch_game_record(
	char const *wire_level,
	unsigned long version,
	short player_count)
{
	char level[MAP_SHARE_LEVEL_SIZE];

	if (!map_share_fetching())
		return FALSE;
	csstrncpy(level, wire_level, sizeof(level) - 1);
	level[sizeof(level) - 1] = 0;
	if (fetch.record && (strcmp(level, fetch.level) || version != fetch.version))
		fetch.record_changed = TRUE;
	if (!fetch.record)
	{
		csmemcpy(fetch.level, level, sizeof(level));
		fetch.version = version;
		fetch.record = TRUE;
		platform_log("map fetch: the host's game is on %s (version %08lx), %d players", level, version,
			(int)player_count);
	}
	fetch.players = player_count;
	return TRUE;
}

boolean map_share_client_message(
	void const *message,
	long size,
	boolean reliable)
{
	unsigned char type;
	unsigned char const *body;
	long body_size;

	if (!map_share_fetching())
		return FALSE;
	if (!map_share_message_parse(message, size, &type, &body, &body_size))
		return FALSE;
	/* (over the host's stream alone; one waited for alone) */
	if (!reliable || fetch.state != _fetch_asked)
		return TRUE;
	if (type == MAP_SHARE_MESSAGE_SOURCES)
	{
		/* (its length checked; its entries, other players' addresses, never
		read, logged or kept) */
		return TRUE;
	}
	if (type != MAP_SHARE_MESSAGE_OFFER || fetch.offered)
		return TRUE;
	{
		char level[MAP_SHARE_LEVEL_SIZE];
		byte result;
		byte count;

		if (body_size < MAP_SHARE_OFFER_SIZE || !map_share_level_parse(body, level) || strcmp(level, fetch.level))
			return TRUE;
		result = body[0x40];
		count = body[0x41];
		if (count > 4 || body_size != MAP_SHARE_OFFER_SIZE + count * MAP_SHARE_OFFER_FILE_SIZE)
		{
			fetch_finish("failed", "failed", "the host's offer was malformed");
			return TRUE;
		}
		if (result == MAP_SHARE_OFFER_SHARING_OFF)
		{
			fetch_finish("failed", "sharing off", "the host does not share its maps");
			return TRUE;
		}
		if (result != MAP_SHARE_OFFER_OK)
		{
			fetch_finish("failed", "unavailable", "the host did not offer the map");
			return TRUE;
		}
		if (count != 1 || body[MAP_SHARE_OFFER_SIZE] != 0)
		{
			fetch_finish("failed", "failed", "the host's offer was not the map file alone");
			return TRUE;
		}
		fetch.size = map_share_get32(body + MAP_SHARE_OFFER_SIZE + 4);
		csmemcpy(fetch.hash, body + MAP_SHARE_OFFER_SIZE + 8, MAP_SHARE_HASH_SIZE);
		if (fetch.size > MAP_LIMIT)
		{
			char text[160];

			snprintf(text, sizeof(text), "the map is %lu MB, more than the %d MB any map may be", fetch.size >> 20,
				MAP_LIMIT >> 20);
			fetch_finish("failed", "too large", text);
			return TRUE;
		}
		if (!fetch.size || map_share_hash_version(fetch.hash) != fetch.version)
		{
			fetch_finish("failed", "failed", "the host's offer was not of this map");
			return TRUE;
		}
		fetch.offered = TRUE;
#ifdef HALO_HOST_MAP_DOWNLOADS
		/* (a player's: the blacklist, by the map hash offered, before a byte
		of it comes; and the player's own size cap) */
		if (fetch.player)
		{
			char reason[160], text[240];

			if (map_download_host_refused(fetch.hash, reason, (int)sizeof(reason)))
			{
				snprintf(text, sizeof(text), "%s.map is on the blacklist: %s", fetch.wanted, reason);
				fetch.blocked = TRUE;
				fetch_finish("failed", "blocked", text);
				return TRUE;
			}
			if (fetch.size > fetch.maximum)
			{
				snprintf(text, sizeof(text), "the map is %lu MB, more than the %lu MB you allow", fetch.size >> 20,
					fetch.maximum >> 20);
				fetch_finish("failed", "too large", text);
				return TRUE;
			}
		}
#endif
		if (fetch.size > fetch.maximum)
		{
			fetch_finish("failed", "too large", "the map is larger than the size cap");
			return TRUE;
		}
		fetch.piece_count = map_share_piece_count(fetch.size);
		fetch.hashes = (byte *)malloc((size_t)fetch.piece_count * MAP_SHARE_HASH_SIZE);
		if (!fetch.hashes)
		{
			fetch_finish("failed", "failed", "out of memory");
			return TRUE;
		}
		platform_log("map fetch: the host offered %lu bytes", fetch.size);
		fetch_progress(MAP_SHARE_PROGRESS_DOWNLOADING);
	}
	return TRUE;
}

#endif /* HALO_GAME_BROWSER */
