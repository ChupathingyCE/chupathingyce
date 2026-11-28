/*
NET_MONITOR.C

The server monitor (net_monitor.h; port/linux/README.md, "Server
monitor"). debug.network_test "monitor:<listed name>", headless
(SDL_VIDEO_DRIVER=dummy), run by tools/server_monitor.py:

- once the main menu is up it opens the Online Games screen, as the
  Multiplayer menu does, and looks for the game listed under that name (the
  same name, else the same name in any case, else the one game whose name
  holds it); none in a minute, it quits;
- it joins only a game with room to spare: one with no password, open to
  joins, of a version this build plays, and with debug.monitor_free_slots
  free after its own player (2 unless set); else it quits without joining;
- it joins as A on it does, once, as a player named debug.monitor_name
  ("Chupa" unless set), and plays nothing: no input (no controller, no
  keyboard), no chat, no voice (the tool turns voice and profile sharing
  off);
- every ten seconds a line of JSON goes to debug.monitor_output: the host's
  ticks as they arrived (new ticks a second, the gaps between them, their
  spread, the ticks skipped, the host's clock against this machine's), the
  corrections this machine made by what they moved (its own player, other
  players, vehicles, AI) with how far and how many were snaps, the
  distributed messages each way by kind, the datagrams and bytes each way,
  the chat lines (the server's and the players', counted: the server's by
  their channel, length and a hash), the players, the bots (on a dedicated
  server, players sharing one machine: it plays no one on its own, so its
  bots share its machine; a player's game's split screen is no bots) and
  how often the host told this machine about each kind,
  this machine's own frames (the slow ones are this machine's, not the
  host's), and the warnings;
- other lines say what happened: listed, joined, each map and game type,
  each game's end, and why it left;
- it leaves when the time is up (debug.monitor_minutes) and the game under
  way has ended, or after debug.monitor_games games, or at once when the
  game has fewer than debug.monitor_free_slots free, or 20 minutes past
  its time whatever the game; when the host drops it, it does not join
  again. Leaving is the map bot's way (map_share_client.c): the game's
  client let go, a second, then the program quits.

Nothing it writes names an address or a player: a game goes by its listed
name (a player's own game only counted), a server's line by its hash (its
text, which may name players, only with debug.monitor_keep_text, in a local
file of its own beside the output). xinput_sdl.c shuts this machine's
gamepads, keyboard and mouse out of a monitor's run.
*/

#include "net_monitor.h"

#ifdef NET_MONITOR_BUILT

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "../src/browser.h"
#include "../src/map_share_files.h"
#include "network_distributed.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* the platform layer's (port/linux/src/port_config.c, platform) */
const char *config_string(char const *name);
double config_real(char const *name);
long config_integer(char const *name);
int config_boolean(char const *name);
void platform_log(char const *format, ...);
unsigned long system_milliseconds(void);
/* xnet.c's: the datagrams the game's sockets sent and received, and their
bytes */
void xnet_datagram_counts(unsigned long *sent, unsigned long *sent_bytes, unsigned long *received,
	unsigned long *received_bytes);
/* xnet.c's: the streams' reads and writes (the game's reliable connection
is TCP), and their bytes */
void xnet_stream_counts(unsigned long *writes, unsigned long *written_bytes, unsigned long *reads,
	unsigned long *read_bytes);
/* delta_peer_game.c's: the datagrams Delta Peer's socket read */
void delta_peer_game_received_counts(unsigned long *datagrams, unsigned long *bytes, unsigned long *voice_datagrams,
	unsigned long *voice_bytes);
/* browser_screen.c's */
void browser_screen_open(void);
boolean browser_screen_active(void);
boolean browser_screen_debug_listing(long index, struct browser_game *game);
void browser_screen_debug_join(long index);

/* (network_client_manager.c's states, which it keeps to itself) */
enum
{
	_client_searching,
	_client_joining,
	_client_pregame,
	_client_ingame,
	_client_postgame
};

enum
{
	WINDOW_MILLISECONDS = 10000,
	/* the gaps between the host's new ticks, by their upper bounds (ms) */
	NUMBER_OF_GAP_BUCKETS = 9,
	MAXIMUM_KINDS = 256,
	/* the game connection's message types counted (message_header's) */
	MAXIMUM_MESSAGE_TYPES = 8,
	MAXIMUM_PLAYERS = 128,
	MAXIMUM_SERVER_LINES = 16,
	SERVER_LINE_LENGTH = 96,
	FIND_SECONDS = 60,
	JOIN_SECONDS = 60,
	LEAVE_DELAY = 1000,
	GRACE_MINUTES = 20
};

static short const gap_bounds[NUMBER_OF_GAP_BUCKETS] = { 40, 50, 70, 100, 150, 250, 500, 1000, 32767 };

enum
{
	_monitor_off,
	_monitor_menu,
	_monitor_finding,
	_monitor_joining,
	_monitor_joined,
	_monitor_leaving
};

/* (what a window counts, from nothing at each window's start) */
struct monitor_window
{
	unsigned long started;
	/* in a game every frame of it (not loading, not the scores) */
	boolean whole_game;
	/* the host's ticks: new ones, the ticks skipped between them, the gaps
	between their arrivals (ms: sum, sum of squares, largest, buckets), and
	the host's clock (its first and last tick in the window) */
	long new_ticks;
	long skipped_ticks;
	long gaps;
	double gap_sum;
	double gap_squares;
	long gap_largest;
	long gap_buckets[NUMBER_OF_GAP_BUCKETS];
	long first_tick;
	long last_tick;
	unsigned long first_tick_time;
	unsigned long last_tick_time;
	/* messages by kind: count and bytes, each way */
	long in_count[MAXIMUM_KINDS];
	long in_bytes[MAXIMUM_KINDS];
	long out_count[MAXIMUM_KINDS];
	long out_bytes[MAXIMUM_KINDS];
	/* corrections by kind: count, snaps, sum and largest distance */
	long corrections[NUMBER_OF_NET_MONITOR_CORRECTIONS];
	long snaps[NUMBER_OF_NET_MONITOR_CORRECTIONS];
	double correction_sum[NUMBER_OF_NET_MONITOR_CORRECTIONS];
	float correction_largest[NUMBER_OF_NET_MONITOR_CORRECTIONS];
	/* the host's word on each player's unit */
	long unit_states[MAXIMUM_PLAYERS];
	/* chat lines: the server's (Delta's), the system's (a notice shown in
	the chat) and the players', and their bytes; and the host's notices */
	long server_lines;
	long server_bytes;
	long system_lines;
	long system_bytes;
	long player_lines;
	long player_bytes;
	long notices;
	long notice_bytes;
	long warnings[NUMBER_OF_NET_MONITOR_WARNINGS];
	long warning_bytes[NUMBER_OF_NET_MONITOR_WARNINGS];
	/* the distributed messages' bytes: as they came (alone, or batches
	whole), the batches', and the messages in batches */
	long distributed_bytes;
	long layout_bytes;
	long batch_bytes;
	long batched_bytes;
	/* every message the game's connection read, by message type and a
	packet's type, datagram [0] or stream [1]: count and bytes */
	long game_count[MAXIMUM_MESSAGE_TYPES][256][2];
	long game_bytes[MAXIMUM_MESSAGE_TYPES][256][2];
	/* Delta Peer's socket's counts at the window's start, and the streams' */
	unsigned long delta_peer[4];
	unsigned long streams[4];
	/* this machine's frames: count, longest, over 50 and 100 ms */
	long frames;
	long frame_longest;
	long frames_over_50;
	long frames_over_100;
	/* the datagram counts at the window's start */
	unsigned long datagrams[4];
};

static struct
{
	boolean checked;
	short state;
	char target[64];
	char name[16];
	char output[512];
	/* debug.monitor_keep_text: the server's lines' text, to <output>.text
	alone (a local file, never the report) */
	boolean keep_text;
	/* the listed game is a dedicated server's (its bots share a machine) */
	boolean dedicated;
	boolean write_failed;
	unsigned long started;
	unsigned long state_time;
	real menu_seconds;
	real minutes;
	long games;
	long free_slots;
	unsigned long find_tried;
	/* the game joined, as listed */
	char listed_name[64];
	char listed_map[64];
	/* what it has seen of the game: in a game (its tick running), the map and
	game type shown last, the games ended */
	boolean in_game;
	boolean was_connected;
	char map[128];
	char variant[32];
	long games_ended;
	short client_state;
	/* (phase_names) */
	short phase;
	unsigned long host_tick_time;
	long host_tick;
	struct monitor_window window;
	/* the server's distinct lines (their text and count), over the run */
	char server_lines[MAXIMUM_SERVER_LINES][SERVER_LINE_LENGTH];
	unsigned long server_line_hashes[MAXIMUM_SERVER_LINES];
	long server_line_counts[MAXIMUM_SERVER_LINES];
	short server_line_count;
	int exit_code;
} monitor;

static char const *correction_names[NUMBER_OF_NET_MONITOR_CORRECTIONS] = {
	"own_player", "remote_player", "own_vehicle", "remote_vehicle", "object", "actor", "host"
};
static char const *warning_names[NUMBER_OF_NET_MONITOR_WARNINGS] = {
	"unknown_kind", "out_of_window", "stale", "malformed", "not_in_game", "unsynchronized", "wrong_sender", "mcc", "epoch"
};
static char const *phase_names[] = { "joining", "lobby", "loading", "game", "postgame" };

/* ---------- writing */

/* text for JSON: printable ASCII, quotes and backslashes escaped, no more
than fits */
static void monitor_json_text(
	char *out,
	int size,
	char const *text)
{
	int length = 0;

	for (; text && *text && length < size - 3; text++)
	{
		unsigned char c = (unsigned char)*text;

		if (c == '"' || c == '\\')
			out[length++] = '\\';
		out[length++] = c >= 32 && c < 127 ? (char)c : '?';
	}
	out[length] = 0;
}

static void monitor_wide_text(
	char *out,
	int size,
	wchar_t const *text,
	int maximum)
{
	int index;

	for (index = 0; index < maximum && index < size - 1 && text[index]; index++)
		out[index] = text[index] >= 32 && text[index] < 127 ? (char)text[index] : '?';
	out[index] = 0;
}

/* (a path of this computer's, not a game path: the platform's file) */
static void monitor_write(
	char const *line)
{
	if (!map_share_append_line(monitor.output[0] ? monitor.output : "monitor.jsonl", line) && !monitor.write_failed)
	{
		monitor.write_failed = TRUE;
		platform_log("server monitor: its output could not be written");
	}
}

static double monitor_seconds(
	void)
{
	return (double)(system_milliseconds() - monitor.started) / 1000.0;
}

/* an event: its name and its fields (JSON, without braces, or "") */
static void monitor_event(
	char const *event,
	char const *fields)
{
	char line[1024];

	snprintf(line, sizeof(line), "{\"t\": %.1f, \"event\": \"%s\"%s%s}", monitor_seconds(), event,
		fields && fields[0] ? ", " : "", fields ? fields : "");
	monitor_write(line);
	platform_log("server monitor: %s %s", event, fields ? fields : "");
}

/* ---------- the window */

static void monitor_window_reset(
	void)
{
	struct monitor_window *window = &monitor.window;

	csmemset(window, 0, sizeof(*window));
	window->started = system_milliseconds();
	window->whole_game = monitor.in_game;
	window->first_tick = NONE;
	window->last_tick = NONE;
	xnet_datagram_counts(&window->datagrams[0], &window->datagrams[1], &window->datagrams[2],
		&window->datagrams[3]);
	delta_peer_game_received_counts(&window->delta_peer[0], &window->delta_peer[1], &window->delta_peer[2],
		&window->delta_peer[3]);
	xnet_stream_counts(&window->streams[0], &window->streams[1], &window->streams[2], &window->streams[3]);
}

static void monitor_append(
	char *line,
	int size,
	int *length,
	char const *format,
	...)
{
	va_list arguments;
	int written;

	if (*length >= size - 1)
		return;
	va_start(arguments, format);
	written = vsnprintf(line + *length, (size_t)(size - *length), format, arguments);
	va_end(arguments);
	if (written > 0)
		*length = MIN(*length + written, size - 1);
}

/* the players as this machine has them: everyone, this machine's own, and
the bots (players sharing a machine with another: a dedicated server's own
machine plays no one, so its bots share it), with how often the host told
this machine of each kind's units in the window */
static void monitor_players(
	char *line,
	int size,
	int *length,
	real seconds)
{
	struct network_game *game = network_game_get_game();
	short machine_players[256];
	struct data_iterator iterator;
	struct player_datum *player;
	long humans = 0, bots = 0, own = 0;
	long human_states = 0, bot_states = 0, own_states = 0;
	long most_on_a_machine = 0;

	csmemset(machine_players, 0, sizeof(machine_players));
	if (game_in_progress() && player_data)
	{
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
			machine_players[(unsigned char)player->network_player_data.machine_index]++;
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
			long states = index >= 0 && index < MAXIMUM_PLAYERS ? monitor.window.unit_states[index] : 0;
			short sharing = machine_players[(unsigned char)player->network_player_data.machine_index];

			most_on_a_machine = MAX(most_on_a_machine, sharing);
			if (player->local_player_index != NONE)
			{
				own++;
				own_states += states;
			}
			/* (a player's own game's split screen shares a machine too: no bots
			there, they are a dedicated server's alone) */
			else if (sharing > 1 && monitor.dedicated)
			{
				bots++;
				bot_states += states;
			}
			else
			{
				humans++;
				human_states += states;
			}
		}
	}
	monitor_append(line, size, length,
		", \"players\": {\"listed\": %d, \"maximum\": %d, \"own\": %ld, \"people\": %ld, \"bots\": %ld, "
		"\"most_on_one_machine\": %ld, \"unit_states_per_second\": {\"own\": %.1f, \"person\": %.1f, \"bot\": %.1f}}",
		game ? (int)game->player_count : 0, game ? (int)game->maximum_players : 0, own, humans, bots, most_on_a_machine,
		own && seconds > 0.0f ? own_states / own / seconds : 0.0,
		humans && seconds > 0.0f ? human_states / humans / seconds : 0.0,
		bots && seconds > 0.0f ? bot_states / bots / seconds : 0.0);
}

static void monitor_window_write(
	void)
{
	struct monitor_window *window = &monitor.window;
	unsigned long now = system_milliseconds();
	real seconds = (real)(now - window->started) / 1000.0f;
	unsigned long datagrams[4];
	static char line[16384];
	int length = 0;
	int kind;
	int index;
	boolean first;

	xnet_datagram_counts(&datagrams[0], &datagrams[1], &datagrams[2], &datagrams[3]);
	monitor_append(line, sizeof(line), &length,
		"{\"t\": %.1f, \"window\": %.1f, \"state\": \"%s\", \"whole_game\": %s, \"map\": \"%s\", "
		"\"variant\": \"%s\"",
		monitor_seconds(), (double)seconds,
		phase_names[monitor.phase],
		window->whole_game && monitor.in_game ? "true" : "false", monitor.map, monitor.variant);
	/* the host's ticks */
	{
		double mean = window->gaps ? window->gap_sum / window->gaps : 0.0;
		double spread = window->gaps > 1 ?
			sqrt(MAX(0.0, (window->gap_squares - window->gap_sum * mean) / (window->gaps - 1))) : 0.0;
		double span = (double)(window->last_tick_time - window->first_tick_time) / 1000.0;
		double clock = span > 1.0 && window->first_tick != NONE ?
			(double)(window->last_tick - window->first_tick) / (span * TICKS_PER_SECOND) : 0.0;

		monitor_append(line, sizeof(line), &length,
			", \"host\": {\"new_ticks\": %ld, \"ticks_per_second\": %.2f, \"skipped_ticks\": %ld, "
			"\"gap_mean_ms\": %.1f, \"gap_spread_ms\": %.1f, \"gap_largest_ms\": %ld, \"clock\": %.3f, \"gaps\": [",
			window->new_ticks, seconds > 0.0f ? window->new_ticks / seconds : 0.0, window->skipped_ticks, mean, spread,
			window->gap_largest, clock);
		for (index = 0; index < NUMBER_OF_GAP_BUCKETS; index++)
			monitor_append(line, sizeof(line), &length, "%s%ld", index ? ", " : "", window->gap_buckets[index]);
		monitor_append(line, sizeof(line), &length, "]}");
	}
	/* corrections */
	monitor_append(line, sizeof(line), &length, ", \"corrections\": {");
	for (index = 0; index < NUMBER_OF_NET_MONITOR_CORRECTIONS; index++)
	{
		monitor_append(line, sizeof(line), &length, "%s\"%s\": [%ld, %ld, %.3f, %.3f]", index ? ", " : "",
			correction_names[index], window->corrections[index], window->snaps[index],
			window->corrections[index] ? window->correction_sum[index] / window->corrections[index] : 0.0,
			(double)window->correction_largest[index]);
	}
	monitor_append(line, sizeof(line), &length, "}");
	/* messages each way, by kind: [count, bytes] */
	monitor_append(line, sizeof(line), &length, ", \"messages_in\": {");
	for (kind = 0, first = TRUE; kind < MAXIMUM_KINDS; kind++)
	{
		if (!window->in_count[kind])
			continue;
		monitor_append(line, sizeof(line), &length, "%s\"%d\": [%ld, %ld]", first ? "" : ", ", kind,
			window->in_count[kind], window->in_bytes[kind]);
		first = FALSE;
	}
	monitor_append(line, sizeof(line), &length, "}, \"messages_out\": {");
	for (kind = 0, first = TRUE; kind < MAXIMUM_KINDS; kind++)
	{
		if (!window->out_count[kind])
			continue;
		monitor_append(line, sizeof(line), &length, "%s\"%d\": [%ld, %ld]", first ? "" : ", ", kind,
			window->out_count[kind], window->out_bytes[kind]);
		first = FALSE;
	}
	monitor_append(line, sizeof(line), &length,
		"}, \"datagrams\": {\"out\": %lu, \"out_bytes\": %lu, \"in\": %lu, \"in_bytes\": %lu}",
		datagrams[0] - window->datagrams[0], datagrams[1] - window->datagrams[1], datagrams[2] - window->datagrams[2],
		datagrams[3] - window->datagrams[3]);
	{
		unsigned long streams[4];

		xnet_stream_counts(&streams[0], &streams[1], &streams[2], &streams[3]);
		monitor_append(line, sizeof(line), &length,
			", \"streams\": {\"writes\": %lu, \"out_bytes\": %lu, \"reads\": %lu, \"in_bytes\": %lu}",
			streams[0] - window->streams[0], streams[1] - window->streams[1], streams[2] - window->streams[2],
			streams[3] - window->streams[3]);
	}
	/* every message the game's connection read: "<message type>.<packet
	type>.<d|s>": [count, bytes] */
	monitor_append(line, sizeof(line), &length, ", \"game_messages\": {");
	{
		int type, packet, stream;

		for (type = 0, first = TRUE; type < MAXIMUM_MESSAGE_TYPES; type++)
		{
			for (packet = 0; packet < 256; packet++)
			{
				for (stream = 0; stream < 2; stream++)
				{
					if (!window->game_count[type][packet][stream])
						continue;
					monitor_append(line, sizeof(line), &length, "%s\"%d.%d.%c\": [%ld, %ld]", first ? "" : ", ", type,
						packet, stream ? 's' : 'd', window->game_count[type][packet][stream],
						window->game_bytes[type][packet][stream]);
					first = FALSE;
				}
			}
		}
	}
	{
		unsigned long delta_peer[4];

		delta_peer_game_received_counts(&delta_peer[0], &delta_peer[1], &delta_peer[2], &delta_peer[3]);
		monitor_append(line, sizeof(line), &length,
			"}, \"distributed_bytes\": %ld, \"layout_bytes\": %ld, \"batch_bytes\": %ld, \"batched_bytes\": %ld, "
			"\"delta_peer\": {\"in\": %lu, \"in_bytes\": %lu, \"voice_in\": %lu, \"voice_in_bytes\": %lu}",
			window->distributed_bytes, window->layout_bytes, window->batch_bytes, window->batched_bytes,
			delta_peer[0] - window->delta_peer[0], delta_peer[1] - window->delta_peer[1],
			delta_peer[2] - window->delta_peer[2], delta_peer[3] - window->delta_peer[3]);
	}
	monitor_append(line, sizeof(line), &length,
		", \"chat\": {\"server_lines\": %ld, \"server_bytes\": %ld, \"system_lines\": %ld, \"system_bytes\": %ld, "
		"\"player_lines\": %ld, \"player_bytes\": %ld, \"notices\": %ld, \"notice_bytes\": %ld}",
		window->server_lines, window->server_bytes, window->system_lines, window->system_bytes, window->player_lines,
		window->player_bytes, window->notices, window->notice_bytes);
	monitor_players(line, sizeof(line), &length, seconds);
	monitor_append(line, sizeof(line), &length, ", \"warnings\": {");
	for (index = 0; index < NUMBER_OF_NET_MONITOR_WARNINGS; index++)
	{
		monitor_append(line, sizeof(line), &length, "%s\"%s\": %ld", index ? ", " : "", warning_names[index],
			window->warnings[index]);
	}
	monitor_append(line, sizeof(line), &length, "}, \"warning_bytes\": {");
	for (index = 0; index < NUMBER_OF_NET_MONITOR_WARNINGS; index++)
	{
		monitor_append(line, sizeof(line), &length, "%s\"%s\": %ld", index ? ", " : "", warning_names[index],
			window->warning_bytes[index]);
	}
	monitor_append(line, sizeof(line), &length,
		"}, \"client\": {\"frames\": %ld, \"frame_longest_ms\": %ld, \"frames_over_50ms\": %ld, "
		"\"frames_over_100ms\": %ld}}",
		window->frames, window->frame_longest, window->frames_over_50, window->frames_over_100);
	monitor_write(line);
	monitor_window_reset();
}

/* ---------- the hooks */

int net_monitor_active(
	void)
{
	return monitor.state != _monitor_off;
}

/* (counting from the join on: what the host sends as the machine joins
too) */
static boolean monitor_counting(
	void)
{
	return monitor.state == _monitor_joining || monitor.state == _monitor_joined;
}

void net_monitor_message_in(
	int type,
	int size,
	long game_time,
	int in_batch,
	int tick_sample)
{
	struct monitor_window *window = &monitor.window;

	if (!monitor_counting())
		return;
	/* (a batch's own bytes hold its messages': counted as it came, and its
	messages each by their kind) */
	if (!in_batch)
		window->distributed_bytes += size;
	if (!in_batch && type == _distributed_message_batch)
		window->batch_bytes += size;
	if (in_batch)
		window->batched_bytes += size;
	if (type >= 0 && type < MAXIMUM_KINDS)
	{
		window->in_count[type]++;
		window->in_bytes[type] += size;
	}
	if (monitor.state != _monitor_joined)
		return;
	/* (a datagram's newest tick: the host's tick as it arrived) */
	if (tick_sample && game_time > monitor.host_tick)
	{
		unsigned long now = system_milliseconds();

		if (monitor.host_tick != NONE && monitor.host_tick_time)
		{
			long gap = (long)(now - monitor.host_tick_time);
			short bucket;

			window->gaps++;
			window->gap_sum += gap;
			window->gap_squares += (double)gap * gap;
			window->gap_largest = MAX(window->gap_largest, gap);
			for (bucket = 0; bucket < NUMBER_OF_GAP_BUCKETS - 1 && gap >= gap_bounds[bucket]; bucket++)
				;
			window->gap_buckets[bucket]++;
			if (game_time - monitor.host_tick > 1)
				window->skipped_ticks += game_time - monitor.host_tick - 1;
		}
		if (window->first_tick == NONE)
		{
			window->first_tick = game_time;
			window->first_tick_time = now;
		}
		window->last_tick = game_time;
		window->last_tick_time = now;
		window->new_ticks++;
		monitor.host_tick = game_time;
		monitor.host_tick_time = now;
	}
}

void net_monitor_message_out(
	int type,
	int size)
{
	if (monitor.state != _monitor_joined || type < 0 || type >= MAXIMUM_KINDS)
		return;
	monitor.window.out_count[type]++;
	monitor.window.out_bytes[type] += size;
}

void net_monitor_correction(
	int kind,
	float distance,
	int snap)
{
	struct monitor_window *window = &monitor.window;

	if (monitor.state != _monitor_joined || kind < 0 || kind >= NUMBER_OF_NET_MONITOR_CORRECTIONS)
		return;
	window->corrections[kind]++;
	if (snap)
		window->snaps[kind]++;
	if (distance == distance)
	{
		window->correction_sum[kind] += distance;
		window->correction_largest[kind] = MAX(window->correction_largest[kind], distance);
	}
}

void net_monitor_unit_state(
	long player_index)
{
	if (monitor.state == _monitor_joined && player_index >= 0 && player_index < MAXIMUM_PLAYERS)
		monitor.window.unit_states[player_index]++;
}

/* the server's distinct lines, by channel: an event for each new one, the
first ones kept and counted */
/* (FNV-1a: a line told from another without its text) */
static unsigned long monitor_hash(
	char const *text,
	int length)
{
	unsigned long hash = 2166136261UL;
	int index;

	for (index = 0; index < length; index++)
		hash = ((hash ^ (unsigned char)text[index]) * 16777619UL) & 0xFFFFFFFFUL;
	return hash;
}

/* the server's distinct lines, by channel: an event for each new one, its
channel, length and hash alone (a line may name players: its text never goes
in the lines the report reads; with debug.monitor_keep_text, to a local file
of its own), and how often each came */
static void monitor_server_line(
	char const *channel,
	char const *text,
	int length)
{
	unsigned long hash;
	short index;

	length = MAX(0, MIN(length, SERVER_LINE_LENGTH - 1));
	hash = monitor_hash(text ? text : "", text ? length : 0);
	for (index = 0; index < monitor.server_line_count; index++)
	{
		if (monitor.server_line_hashes[index] == hash && !strcmp(monitor.server_lines[index], channel))
		{
			monitor.server_line_counts[index]++;
			return;
		}
	}
	if (monitor.server_line_count < MAXIMUM_SERVER_LINES)
	{
		char fields[256];

		snprintf(monitor.server_lines[monitor.server_line_count], SERVER_LINE_LENGTH, "%s", channel);
		monitor.server_line_hashes[monitor.server_line_count] = hash;
		monitor.server_line_counts[monitor.server_line_count++] = 1;
		snprintf(fields, sizeof(fields), "\"channel\": \"%s\", \"hash\": \"%08lx\", \"length\": %d, \"phase\": \"%s\"",
			channel, hash, length, phase_names[monitor.phase]);
		monitor_event("server_line", fields);
		if (monitor.keep_text && text)
		{
			char path[600];
			char line[SERVER_LINE_LENGTH + 64];
			char text_json[2 * SERVER_LINE_LENGTH];
			char kept[SERVER_LINE_LENGTH];

			snprintf(kept, sizeof(kept), "%.*s", length, text);
			monitor_json_text(text_json, sizeof(text_json), kept);
			snprintf(path, sizeof(path), "%s.text", monitor.output[0] ? monitor.output : "monitor.jsonl");
			snprintf(line, sizeof(line), "{\"hash\": \"%08lx\", \"text\": \"%s\"}", hash, text_json);
			map_share_append_line(path, line);
		}
	}
}

void net_monitor_chat(
	int channel,
	int length,
	char const *text)
{
	struct monitor_window *window = &monitor.window;

	if (!monitor_counting())
		return;
	if (channel == 0)
	{
		window->player_lines++;
		window->player_bytes += length;
		return;
	}
	if (channel == 1)
	{
		window->server_lines++;
		window->server_bytes += length;
	}
	else
	{
		window->system_lines++;
		window->system_bytes += length;
	}
	monitor_server_line(channel == 1 ? "delta chat" : "chat system line", text, length);
}

void net_monitor_notice(
	char const *text,
	int length)
{
	if (!monitor_counting())
		return;
	monitor.window.notices++;
	monitor.window.notice_bytes += length;
	/* (a blank one, a screen's padding, counted alone) */
	while (length > 0 && text && *text == ' ')
	{
		text++;
		length--;
	}
	if (length > 0)
		monitor_server_line("notice", text, length);
}

void net_monitor_layout_bytes(
	int size)
{
	if (monitor_counting())
		monitor.window.layout_bytes += size;
}

void net_monitor_game_message(
	int message_type,
	int packet_type,
	int size,
	int stream)
{
	struct monitor_window *window = &monitor.window;

	if (!monitor_counting() || message_type < 0 || packet_type < 0 || packet_type > 255)
		return;
	message_type = MIN(message_type, MAXIMUM_MESSAGE_TYPES - 1);
	window->game_count[message_type][packet_type][stream != 0]++;
	window->game_bytes[message_type][packet_type][stream != 0] += size;
}

void net_monitor_warning(
	int warning,
	int type,
	int size)
{
	if (monitor_counting() && warning >= 0 && warning < NUMBER_OF_NET_MONITOR_WARNINGS)
	{
		monitor.window.warnings[warning]++;
		monitor.window.warning_bytes[warning] += size;
	}
	(void)type;
}

void net_monitor_player_name(
	wchar_t *name,
	int size)
{
	int index;

	/* (an automated network test's joining machine: debug.network_test_name,
	UTF-8, for a test of what a host does with a name) */
	if (monitor.state == _monitor_off && config_string("debug.network_test")[0] &&
		config_string("debug.network_test_name")[0])
	{
		unsigned char const *cursor = (unsigned char const *)config_string("debug.network_test_name");

		for (index = 0; index < size - 1 && *cursor; index++)
		{
			unsigned int character = *cursor++;
			int more = character >= 0xE0 ? 2 : character >= 0xC0 ? 1 : 0;

			if (more)
				character &= 0x3F >> more;
			for (; more && (*cursor & 0xC0) == 0x80; more--)
				character = (character << 6) | (*cursor++ & 0x3F);
			name[index] = (wchar_t)character;
		}
		name[index] = 0;
		return;
	}
	if (monitor.state == _monitor_off || !monitor.name[0])
		return;
	for (index = 0; index < size - 1 && monitor.name[index]; index++)
		name[index] = (wchar_t)monitor.name[index];
	name[index] = 0;
}

/* ---------- the run */

static void monitor_state(
	short state)
{
	monitor.state = state;
	monitor.state_time = system_milliseconds();
}

static void monitor_leave(
	char const *reason,
	int exit_code)
{
	char fields[256];
	short index;

	if (monitor.state == _monitor_leaving)
		return;
	if (monitor.state == _monitor_joined)
		monitor_window_write();
	/* (the server's distinct lines: how often each came, by hash) */
	for (index = 0; index < monitor.server_line_count; index++)
	{
		char line_fields[128];

		snprintf(line_fields, sizeof(line_fields), "\"channel\": \"%s\", \"hash\": \"%08lx\", \"count\": %ld",
			monitor.server_lines[index], monitor.server_line_hashes[index], monitor.server_line_counts[index]);
		monitor_event("server_line_total", line_fields);
	}
	snprintf(fields, sizeof(fields), "\"reason\": \"%s\", \"games_ended\": %ld", reason, monitor.games_ended);
	monitor_event("leave", fields);
	monitor.exit_code = exit_code;
	monitor_state(_monitor_leaving);
}

static void monitor_read_settings(
	void)
{
	char const *setting = config_string("debug.network_test");

	monitor.checked = TRUE;
	if (strncmp(setting, "monitor:", 8) || !setting[8])
		return;
	snprintf(monitor.target, sizeof(monitor.target), "%s", setting + 8);
	snprintf(monitor.name, sizeof(monitor.name), "%s", config_string("debug.monitor_name"));
	if (!monitor.name[0])
		snprintf(monitor.name, sizeof(monitor.name), "Chupa");
	snprintf(monitor.output, sizeof(monitor.output), "%s", config_string("debug.monitor_output"));
	monitor.minutes = (real)config_real("debug.monitor_minutes");
	monitor.games = config_integer("debug.monitor_games");
	monitor.free_slots = MAX(1, config_integer("debug.monitor_free_slots"));
	monitor.keep_text = config_boolean("debug.monitor_keep_text") != 0;
	monitor.started = system_milliseconds();
	monitor.host_tick = NONE;
	monitor_state(_monitor_menu);
}

/* whether the text holds the word, in any case */
static boolean monitor_holds(
	char const *text,
	char const *word)
{
	size_t length = strlen(word);

	for (; *text; text++)
	{
		size_t index;

		for (index = 0; index < length && text[index] &&
			tolower((unsigned char)text[index]) == tolower((unsigned char)word[index]); index++)
			;
		if (index == length)
			return TRUE;
	}
	return FALSE;
}

/* the listed game's name, as ASCII */
static void monitor_listed_name(
	struct browser_game const *game,
	char *name,
	int size)
{
	int index;

	for (index = 0; index < BROWSER_NAME_LENGTH && index < size - 1 && game->name[index]; index++)
		name[index] = game->name[index] >= 32 && game->name[index] < 127 ? (char)game->name[index] : '?';
	name[index] = 0;
}

/* the screen's game listed under the target's name: the same, else the same
in any case, else the one whose name holds it; NONE for none (or, for the
last, more than one) */
static long monitor_find(
	struct browser_game *found)
{
	struct browser_game game;
	char name[BROWSER_NAME_LENGTH + 1];
	long index;
	long pass;

	for (pass = 0; pass < 3; pass++)
	{
		long match = NONE;
		long matches = 0;

		for (index = 0; browser_screen_debug_listing(index, &game); index++)
		{
			boolean same;

			monitor_listed_name(&game, name, sizeof(name));
			same = pass == 0 ? !strcmp(name, monitor.target) :
				pass == 1 ? strlen(name) == strlen(monitor.target) && monitor_holds(name, monitor.target) :
				monitor_holds(name, monitor.target);
			if (same)
			{
				if (match == NONE)
				{
					match = index;
					*found = game;
				}
				matches++;
			}
		}
		if (match != NONE && (pass < 2 || matches == 1))
			return match;
	}
	return NONE;
}

static void monitor_finding(
	unsigned long now)
{
	struct browser_game game;
	long index;
	char name[BROWSER_NAME_LENGTH + 1];
	char name_json[2 * BROWSER_NAME_LENGTH + 2];
	char map_json[2 * BROWSER_MAP_LENGTH + 2];
	char fields[512];
	int room;

	/* (the list given a few seconds to fill: a name that only holds the
	target's must be the one game that does in the whole list) */
	if (now - monitor.find_tried < 2000 || now - monitor.state_time < 8000)
		return;
	monitor.find_tried = now;
	index = monitor_find(&game);
	if (index == NONE)
	{
		if (now - monitor.state_time >= FIND_SECONDS * 1000)
		{
			char listed[1024];
			int length = 0;

			long players_games = 0;

			/* (the servers it had, by their listed names; a player's own game,
			often named after the player, only counted) */
			listed[0] = 0;
			for (index = 0; browser_screen_debug_listing(index, &game) && length < (int)sizeof(listed) - 40; index++)
			{
				if (game.hosting != BROWSER_HOSTING_DEDICATED && game.hosting != BROWSER_HOSTING_OFFICIAL &&
					!game.opence_server)
				{
					players_games++;
					continue;
				}
				monitor_listed_name(&game, name, sizeof(name));
				monitor_json_text(name_json, sizeof(name_json), name);
				monitor_append(listed, sizeof(listed), &length, "%s\"%s\"", length ? ", " : "", name_json);
			}
			snprintf(fields, sizeof(fields), "\"servers\": [%.440s], \"players_games\": %ld", listed, players_games);
			monitor_event("not_listed", fields);
			monitor_leave("not listed", 3);
		}
		return;
	}
	/* (a game between maps, not open to joins: waited for, as long as a
	listing is) */
	if (!game.open)
	{
		if (now - monitor.state_time >= FIND_SECONDS * 1000)
			monitor_leave("not open to joins", 4);
		return;
	}
	monitor_listed_name(&game, name, sizeof(name));
	monitor_json_text(name_json, sizeof(name_json), name);
	monitor_json_text(map_json, sizeof(map_json), game.map);
	snprintf(monitor.listed_name, sizeof(monitor.listed_name), "%s", name);
	monitor.dedicated = game.hosting == BROWSER_HOSTING_DEDICATED || game.hosting == BROWSER_HOSTING_OFFICIAL ||
		game.opence_server;
	snprintf(monitor.listed_map, sizeof(monitor.listed_map), "%s", game.map);
	room = game.maximum_players - game.players - 1;
	snprintf(fields, sizeof(fields),
		"\"name\": \"%s\", \"map\": \"%s\", \"players\": %d, \"maximum\": %d, \"version\": %u, \"password\": %d, "
		"\"joins_locked\": %d, \"ping\": %d, \"region\": \"%.7s\", \"opence_server\": %d, \"hosting\": %d",
		name_json, map_json, game.players, game.maximum_players, (unsigned)game.version, game.locked,
		game.joins_locked, game.ping, game.region, game.opence_server, (int)game.hosting);
	monitor_event("listed", fields);
	/* (the good guest's rules: only a game with room to spare, open to
	anyone, that this build plays) */
	if (game.locked)
		monitor_leave("password", 4);
	else if (game.joins_locked)
		monitor_leave("joins locked", 4);
	else if (browser_game_version_state(&game) != 0)
		monitor_leave("another version", 4);
	else if (room < monitor.free_slots)
		monitor_leave("near full", 5);
	else
	{
		monitor_event("join", "");
		monitor_state(_monitor_joining);
		monitor.phase = 0;
		monitor_window_reset();
		browser_screen_debug_join(index);
	}
}

/* the game as it goes: the map and game type, a game's start and end */
static void monitor_follow_game(
	void)
{
	struct network_game *game = network_game_get_game();
	struct network_game_client *client = global_network_game_client_get();
	short state_data;
	boolean in_game = game_in_progress() && game_engine_running() && game_engine_can_score();
	short client_state = client ? network_game_client_get_state(client, &state_data) : _client_searching;

	monitor.client_state = client_state;
	monitor.phase = in_game ? 3 : client_state == _client_postgame || (game_in_progress() && game_engine_running()) ? 4 :
		game_in_progress() ? 2 : 1;
	if (game)
	{
		char map[128];
		char variant_text[16];
		char variant[32];

		monitor_json_text(map, sizeof(map), game->map.name);
		monitor_wide_text(variant_text, sizeof(variant_text), game->variant.human_readable_game_description,
			NUMBEROF(game->variant.human_readable_game_description));
		monitor_json_text(variant, sizeof(variant), variant_text);
		if (strcmp(map, monitor.map) || strcmp(variant, monitor.variant))
		{
			char fields[256];

			snprintf(monitor.map, sizeof(monitor.map), "%s", map);
			snprintf(monitor.variant, sizeof(monitor.variant), "%s", variant);
			snprintf(fields, sizeof(fields), "\"map\": \"%s\", \"variant\": \"%s\", \"players\": %d, \"maximum\": %d",
				map, variant, (int)game->player_count, (int)game->maximum_players);
			monitor_event("map", fields);
		}
		/* (the good guest: room for others kept) */
		if (game->maximum_players > 0 && game->maximum_players - game->player_count < monitor.free_slots)
		{
			char fields[96];

			snprintf(fields, sizeof(fields), "\"players\": %d, \"maximum\": %d", (int)game->player_count,
				(int)game->maximum_players);
			monitor_event("near_full", fields);
			monitor_leave("near full", 0);
			return;
		}
	}
	if (in_game != monitor.in_game)
	{
		monitor.in_game = in_game;
		if (in_game)
			monitor_event("game_start", "");
		else
		{
			monitor.games_ended++;
			monitor_event("game_end", "");
		}
		/* (the host's ticks start over with each game) */
		monitor.host_tick = NONE;
	}
}

static void monitor_joined(
	unsigned long now,
	boolean main_menu_loaded,
	real seconds)
{
	struct monitor_window *window = &monitor.window;
	struct network_game_client *client = global_network_game_client_get();
	double elapsed_minutes = (double)(now - monitor.started) / 60000.0;
	long frame = (long)(seconds * 1000.0f + 0.5f);
	short state_data;

	/* this machine's own frames */
	window->frames++;
	window->frame_longest = MAX(window->frame_longest, frame);
	if (frame > 50)
		window->frames_over_50++;
	if (frame > 100)
		window->frames_over_100++;
	/* (dropped by the host, or the game gone: not joined again) */
	if (!client || (monitor.was_connected && main_menu_loaded && !game_in_progress() &&
		network_game_client_get_state(client, &state_data) == _client_searching))
	{
		monitor_leave("dropped by the host", 7);
		return;
	}
	monitor.was_connected = TRUE;
	monitor_follow_game();
	if (monitor.state != _monitor_joined)
		return;
	if (!monitor.in_game)
		window->whole_game = FALSE;
	if (now - window->started >= WINDOW_MILLISECONDS)
		monitor_window_write();
	if (monitor.games > 0 && monitor.games_ended >= monitor.games)
		monitor_leave("games done", 0);
	else if (monitor.minutes > 0.0f && elapsed_minutes >= monitor.minutes && !monitor.in_game)
		monitor_leave("time up", 0);
	else if (monitor.minutes > 0.0f && elapsed_minutes >= monitor.minutes + GRACE_MINUTES)
		monitor_leave("time up, game still going", 0);
}

void net_monitor_update(
	int main_menu_loaded,
	float seconds)
{
	unsigned long now = system_milliseconds();

	if (!monitor.checked)
		monitor_read_settings();
	switch (monitor.state)
	{
	case _monitor_menu:
		/* (a run with no end, neither minutes nor games, is not started) */
		if (monitor.minutes <= 0.0f && monitor.games <= 0)
		{
			platform_log("server monitor: debug.monitor_minutes or debug.monitor_games must be set; not joining");
			exit(2);
		}
		if (!main_menu_loaded)
			return;
		monitor.menu_seconds += seconds;
		if (monitor.menu_seconds >= 3.0f)
		{
			char fields[256];
			char target[128];
			char name[2 * sizeof(monitor.name)];

			monitor_json_text(target, sizeof(target), monitor.target);
			monitor_json_text(name, sizeof(name), monitor.name);
			snprintf(fields, sizeof(fields), "\"target\": \"%s\", \"player\": \"%s\", \"minutes\": %.1f, \"games\": %ld",
				target, name, (double)monitor.minutes, monitor.games);
			monitor_event("start", fields);
			browser_screen_open();
			monitor_state(_monitor_finding);
		}
		break;
	case _monitor_finding:
		if (browser_screen_active())
			monitor_finding(now);
		else if (now - monitor.state_time >= FIND_SECONDS * 1000)
			monitor_leave("the game list did not open", 3);
		break;
	case _monitor_joining:
	{
		struct network_game_client *client = global_network_game_client_get();
		short state_data;
		short state = client ? network_game_client_get_state(client, &state_data) : _client_searching;

		if (state >= _client_pregame)
		{
			/* (what came while joining, its own window) */
			monitor_window_write();
			monitor_event("joined", "");
			monitor_state(_monitor_joined);
		}
		else if (now - monitor.state_time >= JOIN_SECONDS * 1000)
			monitor_leave("join failed", 6);
		break;
	}
	case _monitor_joined:
		monitor_joined(now, main_menu_loaded, seconds);
		break;
	case _monitor_leaving:
		if (now - monitor.state_time >= LEAVE_DELAY)
		{
			dispose_global_network_game_client();
			platform_log("server monitor: left");
			exit(monitor.exit_code);
		}
		break;
	default:
		break;
	}
}

#endif /* NET_MONITOR_BUILT */
