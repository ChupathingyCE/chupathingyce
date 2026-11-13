/*
MAP_SHARE_HOST.C

The host's side of map sharing (map_share.h, port/linux/README.md, "Map sharing"), behind
network.map_share (off by default; the host's call), for a dedicated
server's game and a game hosted from the menus alike:

- a request (83) is taken only over the stream of a machine that joined
  the game (in the lobby or in a game in progress, loaded or not), one a
  machine every 5 seconds (one sooner waits; only the last waiting is
  taken);
- only the game's current map is offered (84), and only a Custom Edition
  map (custom_maps\<file>, not one of Halo PC's own), only the map file
  (never a resource map), only if its version is not 0 and its file's hash
  is that version; anything else is "unavailable", and sharing off is
  "sharing is off";
- the sources list (85) is sent empty: the host is the only source, and no
  player's address goes to anyone;
- the file goes over TCP port 5153, which takes connections only from the
  addresses of the game's machines, at most 15 at once, 4 from one address,
  8 that have not said hello; a piece is read when asked for and sent in
  16 KiB blocks out of one token bucket for all of them
  (network.map_share_upload KB/s, 16 to 20480; a game hosted from the
  menus 256 unless the setting is chosen);
- a machine that is downloading (its request answered, not done or failed,
  heard from in the last 20 seconds, sent a piece's worth of bytes by this
  host in the last 80, and within its map's size at 16 KB a second plus two
  minutes from the first offer, which asking again doesn't move) is not
  refused or timed out for having no player
  (network_server_manager.c: map_share_host_downloading); at most 8
  machines download at once, 2 from one address.

Each request and each stream closed is logged, the peer's address through
log_address (a tag for a public one).
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "bungie_net/network/transport.h"
#include "bungie_net/network/transport_endpoint.h"
#include "game/game.h"
#include "networking/network_game_globals.h"
#include "halo_map_families.h"
#include "map_share.h"
#include "../src/map_share_files.h"

#include <stdio.h>
#include <string.h>

/* the platform layer's */
int config_boolean(char const *name);
long config_integer(char const *name);
int config_chosen(char const *name);
int browser_dedicated(void);
void platform_log(char const *format, ...);
const char *log_address(const unsigned char *bytes, int length, int port, char *text, int size);
int map_archive_stock_name(const char *name);
/* network_server_manager.c's */
boolean network_game_server_map_share_game(char *map_name, long size, unsigned long *version, boolean *in_progress);
boolean network_game_server_map_share_machine(long machine_index, unsigned long *address);
/* network_server_message_handler.c's */
boolean network_distributed_server_send_to_machine_reliably(long machine_index, void *message, word size);

/* ---------- constants */

enum
{
	MACHINES = HALO_PORT_MAXIMUM_NETWORK_MACHINES,
	REQUEST_INTERVAL = 5000,
	HEARD_WINDOW = 20000,
	GROWTH_WINDOW = 80000,
	/* the longest a download may keep a machine with no player: its size at
	16 KB a second, and this much more, and never past DOWNLOAD_LONGEST */
	DOWNLOAD_SLACK = 120000,
	DOWNLOAD_SLOWEST = 16 * 1024,
	DOWNLOAD_LONGEST = 30 * 60 * 1000,
	/* an address whose download ended before this host sent it the whole
	map starts no other for this long */
	ADDRESS_COOLDOWN = 15 * 60 * 1000,
	/* machines downloading at once: in all, and from one address */
	DOWNLOADERS = 8,
	DOWNLOADERS_PER_ADDRESS = 2,
	SOURCES_INTERVAL = 10000,
	/* a listen host's upload, in KB a second, unless network.map_share_upload
	is chosen (a dedicated server's default is the setting's, 1024) */
	LISTEN_HOST_UPLOAD = 256,
	LISTEN_RETRY = 10000,

	STREAMS = 15,
	STREAMS_PER_ADDRESS = 4,
	STREAMS_WITHOUT_HELLO = 8,
	HELLO_TIMEOUT = 10000,
	SILENCE_TIMEOUT = 30000,
	KEEP_ALIVE_INTERVAL = 10000,
	REQUESTS_PER_STREAM = 8,
	MAXIMUM_FILES = 4,

	INPUT_SIZE = 4 + MAP_SHARE_FRAME_MAXIMUM,
	OUTPUT_SIZE = 0x20000,
	BLOCK_FRAME_SIZE = 4 + 1 + 1 + 4 + 4 + MAP_SHARE_BLOCK_SIZE,
	BUCKET_MINIMUM = 0x8000,
};

/* ---------- structures */

struct host_machine
{
	boolean known;
	unsigned long address;
	unsigned long last_heard;
	/* a request waiting its turn (or its file's hash) */
	boolean pending;
	char pending_level[MAP_SHARE_LEVEL_SIZE];
	byte pending_wanted;
	unsigned long last_handled;
	boolean handled_once;
	/* its answer waits for the file's hash */
	boolean waiting_hash;
	/* answered with the map, and how it goes */
	boolean answered;
	boolean finished;
	unsigned long total;
	unsigned long had;
	unsigned long growth_time;
	unsigned long sources_time;
	short tenth;
	/* the first offer of the map (asking again doesn't move it), and the
	bytes this host really sent the machine's address since, as of the
	last growth */
	boolean offered_once;
	unsigned long started;
	unsigned long served;
	unsigned long served_mark;
};

struct host_cooldown
{
	unsigned long address;
	unsigned long until;
};

struct host_stream
{
	struct transport_endpoint *endpoint;
	unsigned long address;
	word port;
	unsigned long opened;
	unsigned long last_received;
	unsigned long last_sent;
	boolean said_hello;
	boolean states_sent;
	byte file_count;
	byte hashes[MAXIMUM_FILES][MAP_SHARE_HASH_SIZE];
	/* hashes asked for: the next to send, NONE for none */
	long hashes_next;
	byte hashes_file;
	/* pieces asked for, in order (the first one being sent) */
	unsigned long requests[REQUESTS_PER_STREAM];
	byte request_files[REQUESTS_PER_STREAM];
	short request_count;
	/* the first's bytes, read when its turn came, and how far it is sent */
	byte *piece;
	unsigned long piece_size;
	unsigned long piece_sent;
	boolean piece_loaded;
	unsigned long bytes_sent;
	byte input[INPUT_SIZE];
	long input_used;
	byte output[OUTPUT_SIZE];
	long output_used;
};

/* ---------- globals */

static struct
{
	struct host_machine machines[MACHINES];
	struct host_cooldown cooldowns[MACHINES];
	struct host_stream streams[STREAMS];
	struct transport_endpoint *listener;
	unsigned long listen_failed_time;
	boolean listen_failure_logged;
	/* the file served: the current map's, hashed */
	boolean serving;
	unsigned long serving_size;
	byte serving_hash[MAP_SHARE_HASH_SIZE];
	char serving_level[MAP_SHARE_LEVEL_SIZE];
	/* the bucket */
	double tokens;
	unsigned long bucket_time;
	short next_stream;
} host;

/* ---------- private code */

static unsigned long host_now(
	void)
{
	unsigned long now = system_milliseconds();

	return now ? now : 1;
}

/* a dedicated server's game, or a game hosted from the menus (a listen
host), alike: network.map_share says, off by default, with the same limits */
static boolean host_sharing(
	void)
{
	return config_boolean("network.map_share") != 0;
}

static double host_upload_rate(
	void)
{
	long kilobytes = config_integer("network.map_share_upload");

	/* (a game hosted from the menus shares the player's own upload with
	their play: LISTEN_HOST_UPLOAD unless the setting is chosen) */
	if (!browser_dedicated() && !config_chosen("network.map_share_upload"))
		kilobytes = LISTEN_HOST_UPLOAD;

	return 1024.0 * (double)(kilobytes < 16 ? 16 : kilobytes > 20480 ? 20480 : kilobytes);
}

/* an address (the transport's: IPv4, first byte highest) as the log may show it */
static char const *host_address_text(
	unsigned long address,
	long port,
	char *text,
	int size)
{
	unsigned char bytes[4];

	bytes[0] = (unsigned char)(address >> 24);
	bytes[1] = (unsigned char)(address >> 16);
	bytes[2] = (unsigned char)(address >> 8);
	bytes[3] = (unsigned char)address;
	return log_address(bytes, 4, (int)port, text, size);
}

static void host_send(
	long machine_index,
	unsigned char type,
	unsigned char const *body,
	short body_size)
{
	unsigned char message[MAP_SHARE_DISTRIBUTED_HEADER_SIZE + MAP_SHARE_OFFER_SIZE +
		MAXIMUM_FILES * MAP_SHARE_OFFER_FILE_SIZE];
	short size;

	if (body_size > (short)(sizeof(message) - MAP_SHARE_DISTRIBUTED_HEADER_SIZE))
		return;
	size = map_share_message_build(message, type, body, body_size, game_in_progress() ? game_time_get() : 0);
	network_distributed_server_send_to_machine_reliably(machine_index, message, (word)size);
}

/* the current map as the wire names it, and its version: FALSE with no game */
static boolean host_current_map(
	char *level,
	unsigned long *version,
	boolean *in_progress)
{
	char map_name[0x80];

	if (!network_game_server_map_share_game(map_name, sizeof(map_name), version, in_progress))
		return FALSE;
	map_family_wire_name(map_name, level, MAP_SHARE_LEVEL_SIZE);
	return TRUE;
}

/* the file of the current map, when it is one shared: its path */
static boolean host_current_file(
	char const *level,
	unsigned long version,
	char *path,
	long size)
{
	char file[MAP_SHARE_LEVEL_SIZE];

	if (!version || !map_share_level_file(level, file) || map_archive_stock_name(file))
		return FALSE;
	return map_family_find_version(_map_family_custom_edition, file, version, path, size);
}

static void host_offer(
	long machine_index,
	char const *level,
	byte result,
	boolean in_progress)
{
	struct host_machine *machine = &host.machines[machine_index];
	unsigned char body[MAP_SHARE_OFFER_SIZE + MAP_SHARE_OFFER_FILE_SIZE];
	short size = MAP_SHARE_OFFER_SIZE;

	csmemset(body, 0, sizeof(body));
	csstrncpy((char *)body, level, MAP_SHARE_LEVEL_SIZE - 1);
	body[0x40] = result;
	body[0x42] = (byte)(in_progress ? 1 : 0);
	if (result == MAP_SHARE_OFFER_OK)
	{
		body[0x41] = 1;
		body[0x44] = 0;
		map_share_put32(body + 0x44 + 4, host.serving_size);
		csmemcpy(body + 0x44 + 8, host.serving_hash, MAP_SHARE_HASH_SIZE);
		size += MAP_SHARE_OFFER_FILE_SIZE;
	}
	host_send(machine_index, MAP_SHARE_MESSAGE_OFFER, body, size);
	if (result == MAP_SHARE_OFFER_OK)
	{
		unsigned char sources[MAP_SHARE_SOURCES_SIZE];

		/* (the host alone: no other machine is named) */
		csmemset(sources, 0, sizeof(sources));
		sources[1] = (byte)(in_progress ? 1 : 0);
		host_send(machine_index, MAP_SHARE_MESSAGE_SOURCES, sources, sizeof(sources));
		machine->answered = TRUE;
		machine->finished = FALSE;
		machine->sources_time = host_now();
		/* (asking again restarts nothing: the time and the bytes count from
		the first offer, so a machine can't stay by asking) */
		if (!machine->offered_once || machine->total != host.serving_size)
		{
			machine->offered_once = TRUE;
			machine->total = host.serving_size;
			machine->had = 0;
			machine->tenth = 0;
			machine->started = machine->sources_time;
			machine->growth_time = machine->sources_time;
			machine->served = 0;
			machine->served_mark = 0;
		}
	}
	platform_log("map sharing: machine %ld asked for %s: %s", machine_index, level,
		result == MAP_SHARE_OFFER_OK ? "offered" : result == MAP_SHARE_OFFER_SHARING_OFF ? "sharing is off" :
		"unavailable");
}

/* ---------- downloads: the time they may take and how many at once */

static unsigned long host_download_limit(
	unsigned long size)
{
	unsigned long long limit = (unsigned long long)size * 1000 / DOWNLOAD_SLOWEST + DOWNLOAD_SLACK;

	return limit < DOWNLOAD_LONGEST ? (unsigned long)limit : DOWNLOAD_LONGEST;
}

/* whether an address is waiting out ADDRESS_COOLDOWN */
static boolean host_cooling(
	unsigned long address,
	unsigned long now)
{
	long index;

	for (index = 0; index < MACHINES; index++)
	{
		struct host_cooldown const *cooldown = &host.cooldowns[index];

		if (cooldown->until && cooldown->address == address && (long)(cooldown->until - now) > 0)
			return TRUE;
	}
	return FALSE;
}

/* a machine leaves: if its download held a slot and this host never sent
its address the whole map, the address cools down (so a few addresses
can't keep every slot by leaving and joining again) */
static void host_machine_left(
	struct host_machine const *machine,
	unsigned long now)
{
	long index;
	long oldest = 0;

	if (!machine->answered || !machine->offered_once || machine->served >= machine->total)
		return;
	for (index = 0; index < MACHINES; index++)
	{
		struct host_cooldown *cooldown = &host.cooldowns[index];

		if (!cooldown->until || (long)(cooldown->until - now) <= 0 || cooldown->address == machine->address)
		{
			oldest = index;
			break;
		}
		if ((long)(cooldown->until - host.cooldowns[oldest].until) < 0)
			oldest = index;
	}
	host.cooldowns[oldest].address = machine->address;
	host.cooldowns[oldest].until = now + ADDRESS_COOLDOWN;
	if (!host.cooldowns[oldest].until)
		host.cooldowns[oldest].until = 1;
	platform_log("map sharing: a download ended unfinished; its address waits %d minutes for another",
		ADDRESS_COOLDOWN / 60000);
}

static boolean host_machine_downloading_now(
	struct host_machine const *machine,
	unsigned long now)
{
	return machine->known && machine->answered && !machine->finished &&
		(long)(now - machine->last_heard) < HEARD_WINDOW &&
		(long)(now - machine->started) < (long)host_download_limit(machine->total) &&
		(long)(now - machine->growth_time) < GROWTH_WINDOW;
}

/* whether one more machine may start a download: DOWNLOADERS in all,
DOWNLOADERS_PER_ADDRESS from one address */
static boolean host_room_for(
	long machine_index)
{
	unsigned long now = host_now();
	long in_all = 0;
	long from_address = 0;
	long index;

	for (index = 0; index < MACHINES; index++)
	{
		struct host_machine const *other = &host.machines[index];

		if (index == machine_index || !host_machine_downloading_now(other, now))
			continue;
		in_all++;
		if (other->address == host.machines[machine_index].address)
			from_address++;
	}
	return in_all < DOWNLOADERS && from_address < DOWNLOADERS_PER_ADDRESS;
}

/* bytes sent on a stream from an address: the growth of the downloads of
the machines at that address, a piece at a time */
static void host_served(
	unsigned long address,
	unsigned long size)
{
	unsigned long now = host_now();
	long index;

	for (index = 0; index < MACHINES; index++)
	{
		struct host_machine *machine = &host.machines[index];

		if (!machine->known || machine->address != address || !machine->answered || machine->finished)
			continue;
		machine->served += size;
		if (machine->served >= machine->served_mark + MAP_SHARE_PIECE_SIZE || machine->served >= machine->total)
		{
			machine->served_mark = machine->served;
			machine->growth_time = now;
		}
	}
}

/* a machine's waiting request, in its turn: answered, or left waiting for
its file's hash */
static void host_answer(
	long machine_index)
{
	struct host_machine *machine = &host.machines[machine_index];
	char level[MAP_SHARE_LEVEL_SIZE];
	char path[256];
	unsigned long version = 0;
	boolean in_progress = FALSE;
	unsigned char hash[MAP_SHARE_HASH_SIZE];
	unsigned int size = 0;
	int hashed;

	machine->waiting_hash = FALSE;
	if (!host_current_map(level, &version, &in_progress))
		return;
	if (!host_sharing())
	{
		host_offer(machine_index, machine->pending_level, MAP_SHARE_OFFER_SHARING_OFF, in_progress);
		machine->pending = FALSE;
		return;
	}
	if (strcmp(machine->pending_level, level) || !(machine->pending_wanted & MAP_SHARE_WANT_MAP) ||
		!host_current_file(level, version, path, sizeof(path)))
	{
		host_offer(machine_index, machine->pending_level, MAP_SHARE_OFFER_UNAVAILABLE, in_progress);
		machine->pending = FALSE;
		return;
	}
	hashed = map_share_catalog_file(path, &size, hash);
	machine->waiting_hash = hashed == 0;
	if (hashed == 0)
		return;
	machine->pending = FALSE;
	if (hashed < 0 || map_share_hash_version(hash) != version)
	{
		host.serving = FALSE;
		host_offer(machine_index, level, MAP_SHARE_OFFER_UNAVAILABLE, in_progress);
		return;
	}
	host.serving = TRUE;
	host.serving_size = size;
	csmemcpy(host.serving_hash, hash, sizeof(hash));
	csstrncpy(host.serving_level, level, sizeof(host.serving_level) - 1);
	if (!machine->offered_once && (!host_room_for(machine_index) || host_cooling(machine->address, host_now())))
	{
		platform_log("map sharing: machine %ld asked, but %s", machine_index,
			host_cooling(machine->address, host_now()) ? "its address had a download end unfinished lately" :
			"too many machines are downloading (in all or from its address)");
		host_offer(machine_index, level, MAP_SHARE_OFFER_UNAVAILABLE, in_progress);
		return;
	}
	host_offer(machine_index, level, MAP_SHARE_OFFER_OK, in_progress);
}

static void host_close_stream(
	struct host_stream *stream,
	char const *reason)
{
	char text[64];

	if (!stream->endpoint)
		return;
	platform_log("map sharing: stream from %s closed (%s; %lu bytes sent)",
		host_address_text(stream->address, stream->port, text, sizeof(text)), reason, stream->bytes_sent);
	delete_transport_endpoint(stream->endpoint);
	if (stream->piece)
		debug_free(stream->piece, __FILE__, __LINE__);
	csmemset(stream, 0, sizeof(*stream));
}

static void host_close_all(
	void)
{
	short index;

	for (index = 0; index < STREAMS; index++)
		host_close_stream(&host.streams[index], "sharing stopped");
	if (host.listener)
	{
		delete_transport_endpoint(host.listener);
		host.listener = NULL;
	}
	host.serving = FALSE;
}

/* a frame into the stream's output: FALSE when it has no room now */
static boolean host_frame(
	struct host_stream *stream,
	byte type,
	void const *body,
	long size)
{
	if (stream->output_used + 5 + size > OUTPUT_SIZE)
		return FALSE;
	map_share_put32(stream->output + stream->output_used, (unsigned long)(1 + size));
	stream->output[stream->output_used + 4] = type;
	if (size)
		csmemcpy(stream->output + stream->output_used + 5, body, size);
	stream->output_used += 5 + size;
	return TRUE;
}

/* which of a stream's files is the one served */
static boolean host_file_served(
	struct host_stream *stream,
	byte file)
{
	return host.serving && file < stream->file_count &&
		!csmemcmp(stream->hashes[file], host.serving_hash, MAP_SHARE_HASH_SIZE);
}

static void host_refuse(
	struct host_stream *stream,
	byte file,
	unsigned long piece)
{
	byte body[5];

	body[0] = file;
	map_share_put32(body + 1, piece);
	host_frame(stream, MAP_SHARE_FRAME_REFUSE, body, sizeof(body));
}

/* a whole frame from a stream: FALSE closes it */
static boolean host_handle_frame(
	struct host_stream *stream,
	byte type,
	byte const *body,
	long size)
{
	if (!stream->said_hello)
	{
		long count;

		if (type != MAP_SHARE_FRAME_HELLO || size < 6 || csmemcmp(body, "MHS1", 4) ||
			body[4] != MAP_SHARE_HELLO_VERSION)
		{
			return FALSE;
		}
		count = body[5];
		if (count < 1 || count > MAXIMUM_FILES || size != 6 + count * MAP_SHARE_HASH_SIZE)
			return FALSE;
		stream->said_hello = TRUE;
		stream->file_count = (byte)count;
		csmemcpy(stream->hashes, body + 6, count * MAP_SHARE_HASH_SIZE);
		return host_frame(stream, MAP_SHARE_FRAME_HELLO, body, size);
	}
	switch (type)
	{
	case MAP_SHARE_FRAME_HASHES_REQUEST:
		if (size == 1 && host_file_served(stream, body[0]))
		{
			stream->hashes_file = body[0];
			stream->hashes_next = 0;
		}
		break;
	case MAP_SHARE_FRAME_REQUEST:
		if (size == 5)
		{
			unsigned long piece = map_share_get32(body + 1);

			if (!host_file_served(stream, body[0]) || piece >= map_share_piece_count(host.serving_size) ||
				stream->request_count >= REQUESTS_PER_STREAM)
			{
				host_refuse(stream, body[0], piece);
			}
			else
			{
				stream->requests[stream->request_count] = piece;
				stream->request_files[stream->request_count] = body[0];
				stream->request_count++;
			}
		}
		break;
	default:
		/* (state, have, refuse, block, keep-alive: nothing for the host;
		any other kind is ignored) */
		break;
	}
	return TRUE;
}

/* what a stream has sent: its whole frames handled; FALSE closes it */
static boolean host_read_stream(
	struct host_stream *stream,
	unsigned long now)
{
	for (;;)
	{
		long result;
		long offset = 0;

		if (stream->input_used < INPUT_SIZE)
		{
			result = read_endpoint(stream->endpoint, stream->input + stream->input_used,
				INPUT_SIZE - stream->input_used);
			if (result == 0)
				return FALSE;
			if (result < 0 && result != _transport_result_operation_would_block)
				return FALSE;
			if (result > 0)
				stream->input_used += result;
		}
		else
			result = 0;
		while (stream->input_used - offset >= 4)
		{
			unsigned long length = map_share_get32(stream->input + offset);

			if (length == 0 || length > MAP_SHARE_FRAME_MAXIMUM)
				return FALSE;
			if ((unsigned long)(stream->input_used - offset - 4) < length)
				break;
			stream->last_received = now;
			if (!host_handle_frame(stream, stream->input[offset + 4], stream->input + offset + 5, (long)length - 1))
				return FALSE;
			offset += 4 + (long)length;
		}
		if (offset)
		{
			csmemmove(stream->input, stream->input + offset, stream->input_used - offset);
			stream->input_used -= offset;
		}
		if (result <= 0)
			return TRUE;
	}
}

/* the next of a stream's frames that need no tokens: states, hashes */
static void host_fill_stream(
	struct host_stream *stream)
{
	if (stream->said_hello && !stream->states_sent && host.serving)
	{
		byte file;

		for (file = 0; file < stream->file_count; file++)
		{
			byte body[2];

			body[0] = file;
			body[1] = (byte)(host_file_served(stream, file) ? MAP_SHARE_STATE_ALL : MAP_SHARE_STATE_NONE);
			host_frame(stream, MAP_SHARE_FRAME_STATE, body, sizeof(body));
		}
		stream->states_sent = TRUE;
	}
	if (stream->hashes_next != NONE && stream->states_sent && host_file_served(stream, stream->hashes_file))
	{
		unsigned long count = map_share_piece_count(host.serving_size);
		byte body[7 + MAP_SHARE_HASHES_PER_FRAME * MAP_SHARE_HASH_SIZE];

		while (stream->hashes_next >= 0 && (unsigned long)stream->hashes_next < count)
		{
			unsigned long first = (unsigned long)stream->hashes_next;
			unsigned long part = MIN(count - first, (unsigned long)MAP_SHARE_HASHES_PER_FRAME);

			if (stream->output_used + 5 + 7 + (long)part * MAP_SHARE_HASH_SIZE > OUTPUT_SIZE)
				return;
			body[0] = stream->hashes_file;
			map_share_put32(body + 1, first);
			map_share_put16(body + 5, (unsigned int)part);
			if (!map_share_catalog_piece_hashes(first, part, body + 7))
			{
				stream->hashes_next = NONE;
				return;
			}
			host_frame(stream, MAP_SHARE_FRAME_HASHES, body, 7 + (long)part * MAP_SHARE_HASH_SIZE);
			stream->hashes_next = (long)(first + part);
		}
		stream->hashes_next = NONE;
	}
}

/* a stream's next block, if it has one to send and room for it: its size
in tokens, 0 if none */
static long host_block_ready(
	struct host_stream *stream)
{
	if (!stream->endpoint || !stream->request_count || !stream->states_sent ||
		stream->output_used + BLOCK_FRAME_SIZE > OUTPUT_SIZE)
	{
		return 0;
	}
	if (!stream->piece_loaded)
	{
		unsigned long piece = stream->requests[0];
		unsigned long offset = piece * MAP_SHARE_PIECE_SIZE;

		if (!host_file_served(stream, stream->request_files[0]) || offset >= host.serving_size)
		{
			host_refuse(stream, stream->request_files[0], piece);
			stream->request_count--;
			csmemmove(stream->requests, stream->requests + 1, stream->request_count * sizeof(stream->requests[0]));
			csmemmove(stream->request_files, stream->request_files + 1, stream->request_count);
			return 0;
		}
		if (!stream->piece)
			stream->piece = (byte *)debug_malloc(MAP_SHARE_PIECE_SIZE, FALSE, __FILE__, __LINE__);
		stream->piece_size = MIN(host.serving_size - offset, (unsigned long)MAP_SHARE_PIECE_SIZE);
		if (!stream->piece || !map_share_catalog_read(offset, stream->piece, stream->piece_size))
		{
			host_refuse(stream, stream->request_files[0], piece);
			stream->request_count--;
			csmemmove(stream->requests, stream->requests + 1, stream->request_count * sizeof(stream->requests[0]));
			csmemmove(stream->request_files, stream->request_files + 1, stream->request_count);
			return 0;
		}
		stream->piece_sent = 0;
		stream->piece_loaded = TRUE;
	}
	return (long)MIN(stream->piece_size - stream->piece_sent, (unsigned long)MAP_SHARE_BLOCK_SIZE);
}

static void host_send_block(
	struct host_stream *stream)
{
	long size = (long)MIN(stream->piece_size - stream->piece_sent, (unsigned long)MAP_SHARE_BLOCK_SIZE);
	byte *frame = stream->output + stream->output_used;

	map_share_put32(frame, (unsigned long)(1 + 9 + size));
	frame[4] = MAP_SHARE_FRAME_BLOCK;
	frame[5] = stream->request_files[0];
	map_share_put32(frame + 6, stream->requests[0]);
	map_share_put32(frame + 10, stream->piece_sent);
	csmemcpy(frame + 14, stream->piece + stream->piece_sent, size);
	stream->output_used += 14 + size;
	stream->piece_sent += (unsigned long)size;
	stream->bytes_sent += (unsigned long)size;
	host_served(stream->address, (unsigned long)size);
	if (stream->piece_sent >= stream->piece_size)
	{
		stream->piece_loaded = FALSE;
		stream->request_count--;
		csmemmove(stream->requests, stream->requests + 1, stream->request_count * sizeof(stream->requests[0]));
		csmemmove(stream->request_files, stream->request_files + 1, stream->request_count);
	}
}

/* what a stream's output holds, written as far as it goes: FALSE closes it */
static boolean host_write_stream(
	struct host_stream *stream,
	unsigned long now)
{
	if (!stream->output_used && stream->said_hello && (long)(now - stream->last_sent) >= KEEP_ALIVE_INTERVAL)
		host_frame(stream, MAP_SHARE_FRAME_KEEP_ALIVE, NULL, 0);
	while (stream->output_used)
	{
		long written = write_endpoint(stream->endpoint, stream->output, stream->output_used);

		if (written == _transport_result_operation_would_block)
			return TRUE;
		if (written <= 0)
			return FALSE;
		stream->last_sent = now;
		csmemmove(stream->output, stream->output + written, stream->output_used - written);
		stream->output_used -= written;
	}
	return TRUE;
}

/* whether an address is one of the game's machines' */
static boolean host_address_in_game(
	unsigned long address)
{
	long machine_index;

	for (machine_index = 0; machine_index < MACHINES; machine_index++)
	{
		unsigned long machine_address;

		if (network_game_server_map_share_machine(machine_index, &machine_address) && machine_address == address)
			return TRUE;
	}
	return FALSE;
}

static void host_accept(
	unsigned long now)
{
	while (host.listener && endpoint_readable(host.listener, 0))
	{
		struct transport_endpoint *endpoint = accept_endpoint(host.listener);
		struct transport_address address = { { { 0 } } };
		struct host_stream *stream = NULL;
		short from_address = 0;
		short without_hello = 0;
		short oldest = NONE;
		short index;
		char text[64];

		if (!endpoint)
			return;
		if (get_endpoint_address(endpoint, &address) != _transport_error_none ||
			!host_address_in_game(address.address.ipv4_address))
		{
			platform_log("map sharing: refused a stream from %s (not a machine in the game)",
				host_address_text(address.address.ipv4_address, address.port, text, sizeof(text)));
			delete_transport_endpoint(endpoint);
			continue;
		}
		for (index = 0; index < STREAMS; index++)
		{
			struct host_stream *other = &host.streams[index];

			if (!other->endpoint)
			{
				if (!stream)
					stream = other;
				continue;
			}
			if (other->address == address.address.ipv4_address)
				from_address++;
			if (!other->said_hello)
			{
				without_hello++;
				if (oldest == NONE || (long)(other->opened - host.streams[oldest].opened) < 0)
					oldest = index;
			}
		}
		if (without_hello >= STREAMS_WITHOUT_HELLO && oldest != NONE)
		{
			host_close_stream(&host.streams[oldest], "too many streams without a hello");
			if (!stream)
				stream = &host.streams[oldest];
		}
		if (!stream || from_address >= STREAMS_PER_ADDRESS ||
			set_endpoint_blocking(endpoint, FALSE) != _transport_error_none)
		{
			platform_log("map sharing: refused a stream from %s (too many streams)",
				host_address_text(address.address.ipv4_address, address.port, text, sizeof(text)));
			delete_transport_endpoint(endpoint);
			continue;
		}
		csmemset(stream, 0, sizeof(*stream));
		stream->endpoint = endpoint;
		stream->address = address.address.ipv4_address;
		stream->port = address.port;
		stream->opened = now;
		stream->last_received = now;
		stream->last_sent = now;
		stream->hashes_next = NONE;
		platform_log("map sharing: stream from %s", host_address_text(stream->address, stream->port, text,
			sizeof(text)));
	}
}

static void host_listen(
	unsigned long now)
{
	struct transport_address address = { { { 0 } } };

	if (host.listener || (host.listen_failed_time && (long)(now - host.listen_failed_time) < LISTEN_RETRY))
		return;
	host.listener = create_transport_endpoint(MAP_SHARE_ENDPOINT_TCP);
	address.address_length = IPV4_ADDRESS_LENGTH;
	address.port = MAP_SHARE_PORT;
	if (host.listener)
	{
		set_endpoint_exclusive(host.listener);
		if (bind_endpoint(host.listener, &address) != _transport_error_none ||
			listen_endpoint(host.listener) != _transport_error_none ||
			set_endpoint_blocking(host.listener, FALSE) != _transport_error_none)
		{
			delete_transport_endpoint(host.listener);
			host.listener = NULL;
		}
	}
	if (!host.listener)
	{
		host.listen_failed_time = now;
		if (!host.listen_failure_logged)
			platform_log("map sharing: port %d is taken; maps are not shared until it is free", MAP_SHARE_PORT);
		host.listen_failure_logged = TRUE;
		return;
	}
	host.listen_failed_time = 0;
	host.listen_failure_logged = FALSE;
	platform_log("map sharing: listening on port %d", MAP_SHARE_PORT);
}

/* the machines: forgotten when their slots empty or change hands; waiting
requests answered in their turn; the sources list again every 10 seconds */
static void host_update_machines(
	unsigned long now)
{
	long machine_index;
	boolean in_progress = FALSE;
	char level[MAP_SHARE_LEVEL_SIZE];
	unsigned long version = 0;

	host_current_map(level, &version, &in_progress);
	for (machine_index = 0; machine_index < MACHINES; machine_index++)
	{
		struct host_machine *machine = &host.machines[machine_index];
		unsigned long address;

		if (!machine->known)
			continue;
		if (!network_game_server_map_share_machine(machine_index, &address) || address != machine->address)
		{
			host_machine_left(machine, now);
			csmemset(machine, 0, sizeof(*machine));
			continue;
		}
		if (machine->pending &&
			(machine->waiting_hash || !machine->handled_once || (long)(now - machine->last_handled) >= REQUEST_INTERVAL))
		{
			if (!machine->waiting_hash)
			{
				machine->handled_once = TRUE;
				machine->last_handled = now;
			}
			host_answer(machine_index);
		}
		if (map_share_host_downloading(machine_index) && (long)(now - machine->sources_time) >= SOURCES_INTERVAL)
		{
			unsigned char sources[MAP_SHARE_SOURCES_SIZE];

			csmemset(sources, 0, sizeof(sources));
			sources[1] = (byte)(in_progress ? 1 : 0);
			host_send(machine_index, MAP_SHARE_MESSAGE_SOURCES, sources, sizeof(sources));
			machine->sources_time = now;
		}
	}
	/* (the map changed: its file no longer served) */
	if (host.serving && strcmp(level, host.serving_level))
		host.serving = FALSE;
}

/* ---------- public code */

void map_share_update(
	void)
{
	unsigned long now;
	char level[MAP_SHARE_LEVEL_SIZE];
	char file[MAP_SHARE_LEVEL_SIZE];
	unsigned long version = 0;
	boolean in_progress = FALSE;
	double rate;
	short index;
	short turns;

	if (map_share_fetching() || !global_network_game_server_get())
	{
		if (host.listener || host.serving)
		{
			host_close_all();
			map_share_catalog_clear();
		}
		csmemset(host.machines, 0, sizeof(host.machines));
		return;
	}
	now = host_now();
	host_update_machines(now);
	/* (the port while sharing and the game is on a Custom Edition map) */
	if (!host_sharing() || !host_current_map(level, &version, &in_progress) || !map_share_level_file(level, file))
	{
		if (host.listener)
			host_close_all();
		return;
	}
	host_listen(now);
	host_accept(now);
	for (index = 0; index < STREAMS; index++)
	{
		struct host_stream *stream = &host.streams[index];

		if (!stream->endpoint)
			continue;
		if (!host_read_stream(stream, now))
			host_close_stream(stream, "closed");
		else if (!stream->said_hello && (long)(now - stream->opened) > HELLO_TIMEOUT)
			host_close_stream(stream, "no hello");
		else if ((long)(now - stream->last_received) > SILENCE_TIMEOUT)
			host_close_stream(stream, "silent");
		else
			host_fill_stream(stream);
	}
	/* the bucket: filled at the upload rate, held to a quarter-second's
	worth (32 KiB at least), spent a block at a time round-robin */
	rate = host_upload_rate();
	if (host.bucket_time)
		host.tokens += rate * (double)(now - host.bucket_time) / 1000.0;
	host.bucket_time = now;
	if (host.tokens > MAX(rate / 4.0, (double)BUCKET_MINIMUM))
		host.tokens = MAX(rate / 4.0, (double)BUCKET_MINIMUM);
	for (turns = 0; turns < STREAMS * 64; turns++)
	{
		boolean sent = FALSE;

		for (index = 0; index < STREAMS; index++)
		{
			struct host_stream *stream = &host.streams[(host.next_stream + index) % STREAMS];
			long size = host_block_ready(stream);

			if (!size)
				continue;
			if (host.tokens < (double)size)
				break;
			host.tokens -= (double)size;
			host_send_block(stream);
			host.next_stream = (short)((host.next_stream + index + 1) % STREAMS);
			sent = TRUE;
			break;
		}
		if (!sent)
			break;
	}
	for (index = 0; index < STREAMS; index++)
	{
		struct host_stream *stream = &host.streams[index];

		if (stream->endpoint && !host_write_stream(stream, now))
			host_close_stream(stream, "closed");
	}
}

boolean map_share_host_message(
	long machine_index,
	void const *message,
	long size)
{
	unsigned char type;
	unsigned char const *body;
	long body_size;
	struct host_machine *machine;
	unsigned long address;

	if (!map_share_message_parse(message, size, &type, &body, &body_size))
		return FALSE;
	/* (from a machine of the game, to a host: 84 and 85 are a host's) */
	if (map_share_fetching() || machine_index < 0 || machine_index >= MACHINES ||
		!network_game_server_map_share_machine(machine_index, &address))
	{
		return TRUE;
	}
	machine = &host.machines[machine_index];
	if (!machine->known || machine->address != address)
	{
		csmemset(machine, 0, sizeof(*machine));
		machine->known = TRUE;
		machine->address = address;
	}
	switch (type)
	{
	case MAP_SHARE_MESSAGE_REQUEST:
		if (body_size != MAP_SHARE_REQUEST_SIZE || !map_share_level_parse(body, machine->pending_level))
			return TRUE;
		machine->pending = TRUE;
		machine->pending_wanted = body[0x44];
		machine->last_heard = host_now();
		/* (asking again does not restart the progress clock) */
		break;
	case MAP_SHARE_MESSAGE_PROGRESS:
	{
		unsigned long had;
		unsigned long now = host_now();
		short tenth;

		if (body_size != MAP_SHARE_PROGRESS_SIZE || !machine->answered || machine->finished)
			return TRUE;
		machine->last_heard = now;
		had = map_share_get32(body + 4);
		if (had > machine->total)
			had = machine->total;
		/* (what it says it has is for the log alone: its stay grows only
		with the bytes this host really sends it, host_served) */
		if (had > machine->had)
			machine->had = had;
		tenth = (short)(machine->total ? (unsigned long)((unsigned long long)had * 10 / machine->total) : 0);
		if (body[0] == MAP_SHARE_PROGRESS_DOWNLOADING && tenth > machine->tenth)
		{
			machine->tenth = tenth;
			platform_log("map sharing: machine %ld downloading, %d%%", machine_index, tenth * 10);
		}
		if (body[0] == MAP_SHARE_PROGRESS_DONE || body[0] == MAP_SHARE_PROGRESS_FAILED)
		{
			machine->finished = TRUE;
			platform_log("map sharing: machine %ld %s", machine_index,
				body[0] == MAP_SHARE_PROGRESS_DONE ? "downloaded the map" : "failed to download the map");
		}
		break;
	}
	default:
		break;
	}
	return TRUE;
}

boolean map_share_host_downloading(
	long machine_index)
{
	struct host_machine const *machine;
	unsigned long now = host_now();

	if (machine_index < 0 || machine_index >= MACHINES)
		return FALSE;
	machine = &host.machines[machine_index];
	if (!machine->known || machine->finished || !(machine->answered || machine->pending))
		return FALSE;
	if ((long)(now - machine->last_heard) >= HEARD_WINDOW)
		return FALSE;
	if (!machine->answered)
		return TRUE;
	if ((long)(now - machine->started) >= (long)host_download_limit(machine->total))
		return FALSE;
	return (long)(now - machine->growth_time) < GROWTH_WINDOW;
}

#endif /* HALO_GAME_BROWSER */
