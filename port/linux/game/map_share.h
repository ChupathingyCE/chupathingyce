/*
MAP_SHARE.H

Map sharing (port/linux/README.md, "Map sharing"): a host hands the Custom Edition map its
game is on to a machine that joined it and asks, as OpenCE's hosts do, so a
player missing the map can get it from the game itself. The four messages
ride the game's connection as distributed messages (83 to 86); the file goes
over a TCP stream to the host's port 5153.

- map_share_host.c: the host's side, behind network.map_share (off by
  default): it answers a request (83) for its current map with an offer
  (84) and an empty sources list (85: the host alone), takes the asker's
  progress (86), and serves the file on port 5153 to its game's machines
  only.
- map_share_client.c: the fetch (debug.network_test "fetch:<host>"), the
  map bot's: a headless run joins one host as a machine with no player,
  reads the map's name and version from the game record, asks for the map
  file alone, downloads it from the host alone, writes it and a result,
  leaves and quits. The same fetch is a player's download of a missing map
  from the host (map_share_player_start), into the quarantine folder, when
  network.map_downloads_from_host allows it (port/linux/src/map_download.c,
  "From the host").
- map_share_wire.c: what both write and read.

A build that does not know 83 to 86 drops them, as ours did (network_
distributed.c): nothing here changes the network version.
*/

#ifndef __MAP_SHARE_H
#define __MAP_SHARE_H
#pragma once

/* ---------- constants */

enum
{
	/* the distributed messages (network_distributed.h's numbering) */
	MAP_SHARE_MESSAGE_REQUEST = 83,
	MAP_SHARE_MESSAGE_OFFER = 84,
	MAP_SHARE_MESSAGE_SOURCES = 85,
	MAP_SHARE_MESSAGE_PROGRESS = 86,

	MAP_SHARE_DISTRIBUTED_HEADER_SIZE = 8,
	/* the most further header bytes a layout adds (network_rules.h's
	NETWORK_RULES_HEADER_EXTRA_LIMIT) */
	MAP_SHARE_HEADER_EXTRA_LIMIT = 8,
	/* the largest message taken (an offer of every file, a short list of
	sources); a longer one is dropped */
	MAP_SHARE_RECEIVE_LIMIT = 512,
	MAP_SHARE_LEVEL_SIZE = 64,
	MAP_SHARE_REQUEST_SIZE = 72,
	MAP_SHARE_OFFER_SIZE = 68,
	MAP_SHARE_OFFER_FILE_SIZE = 40,
	MAP_SHARE_SOURCES_SIZE = 4,
	MAP_SHARE_SOURCE_SIZE = 32,
	MAP_SHARE_PROGRESS_SIZE = 12,

	/* a request's wanted files: the map, bitmaps.map, sounds.map, loc.map */
	MAP_SHARE_WANT_MAP = 1,
	/* an offer's result */
	MAP_SHARE_OFFER_OK = 0,
	MAP_SHARE_OFFER_SHARING_OFF = 1,
	MAP_SHARE_OFFER_UNAVAILABLE = 2,
	/* progress's state */
	MAP_SHARE_PROGRESS_DOWNLOADING = 0,
	MAP_SHARE_PROGRESS_DONE = 1,
	MAP_SHARE_PROGRESS_FAILED = 2,

	/* the transfer stream */
	MAP_SHARE_PORT = 5153,
	MAP_SHARE_FRAME_MAXIMUM = 0x5FFC,
	MAP_SHARE_BLOCK_SIZE = 16384,
	MAP_SHARE_HASHES_PER_FRAME = 256,
	MAP_SHARE_FRAME_HELLO = 1,
	MAP_SHARE_FRAME_STATE,
	MAP_SHARE_FRAME_HAVE,
	MAP_SHARE_FRAME_REQUEST,
	MAP_SHARE_FRAME_BLOCK,
	MAP_SHARE_FRAME_REFUSE,
	MAP_SHARE_FRAME_HASHES_REQUEST,
	MAP_SHARE_FRAME_HASHES,
	MAP_SHARE_FRAME_KEEP_ALIVE,
	MAP_SHARE_HELLO_VERSION = 1,
	MAP_SHARE_STATE_NONE = 0,
	MAP_SHARE_STATE_ALL = 1,
	MAP_SHARE_STATE_SOME = 2,

	/* a transport endpoint of the TCP kind (transport_endpoint_winsock.c's) */
	MAP_SHARE_ENDPOINT_TCP = 0x12,
};

/* ---------- map_share_wire.c */

/* numbers in the wire's order (little-endian), whatever this machine's */
void map_share_put16(unsigned char *at, unsigned int value);
void map_share_put32(unsigned char *at, unsigned long value);
unsigned int map_share_get16(unsigned char const *at);
unsigned long map_share_get32(unsigned char const *at);

/* a distributed message of body_size bytes (its 8-byte header, then the
body) built in message (MAP_SHARE_DISTRIBUTED_HEADER_SIZE + body_size
bytes): the message header word, its type, a count of 1 and game_time.
Returns its size */
short map_share_message_build(unsigned char *message, unsigned char type, unsigned char const *body, short body_size,
	long game_time);
/* a received message's type and body (message of size bytes, its message
header in this machine's order): FALSE when it is none of 83 to 86 */
boolean map_share_message_parse(void const *message, long size, unsigned char *type, unsigned char const **body,
	long *body_size);

/* the level name field of a request or offer: TRUE with it in level (64
bytes) when it holds 1 to 63 characters and its terminator */
boolean map_share_level_parse(unsigned char const *field, char *level);
/* a game record's map version of a file hash: its first 4 bytes, big-endian,
1 for 0 */
unsigned long map_share_hash_version(unsigned char const *hash);
/* a level name a Custom Edition map's (custom_maps\<file>), with a safe file
name (printable ASCII, no separators or characters file systems refuse, no
dot or space at either end, not a Windows device name): TRUE with the file
name in file (64 bytes) */
boolean map_share_level_file(char const *level, char *file);

/* ---------- network_distributed.c */

/* a message as map_share_message_build makes it (size bytes) as the game's
wire has it, with the header's further bytes of the game's layout (OpenCE
42 on: 4, the epoch), into out (room bytes): its size, 0 when it does not
fit. hosting: this machine is the game's host (its announced version),
else the host's version */
int network_distributed_share_widen(boolean hosting, void const *message, int size, void *out, int room);
/* ... and a received one back as map_share_message_parse reads it, into
out (room bytes, at least size): its size, 0 when it is too short or
long */
int network_distributed_share_narrow(boolean hosting, void const *message, int size, void *out, int room);
/* the further header bytes the game's layout adds (0 before OpenCE's 42) */
int network_distributed_share_extra(boolean hosting);
/* a received message as map_share_message_parse reads it (8-byte header),
from either form (network_rules_share_header_size): into out (room bytes),
its size, with *short_form TRUE for the old 8-byte form; 0 for neither */
int map_share_message_narrow(boolean hosting, void const *message, int size, void *out, int room,
	boolean *short_form);

/* ---------- map_share_host.c */

/* each frame (main.c): the host's listener, streams and answers */
void map_share_update(void);
/* a client machine's distributed message (network_server_message_handler.c),
from its stream, whether or not it has loaded the game: TRUE if it was one
of map sharing's (taken or dropped) */
boolean map_share_host_message(long machine_index, void const *message, long size);
/* whether a machine that joined counts as downloading the host's map: not
refused or timed out for having no player (network_server_manager.c) */
boolean map_share_host_downloading(long machine_index);

/* ---------- map_share_client.c */

/* whether this run is a fetch (debug.network_test "fetch:...") */
boolean map_share_fetching(void);
/* the fetch, each frame, from network_test.c */
void map_share_fetch_update(boolean main_menu_loaded, real seconds);
/* the host's game record (network_client_manager.c), its map as the wire
names it: TRUE when the fetch takes it, in place of loading the map */
boolean map_share_fetch_game_record(char const *wire_level, unsigned long version, short player_count);
/* the host's distributed message (network_client_message_handler.c): TRUE
if the fetch took it */
boolean map_share_client_message(void const *message, long size, boolean reliable);
/* the fetch's machine's name (network_game_manager.c) */
char const *map_share_fetch_machine_name(void);

#ifdef HALO_HOST_MAP_DOWNLOADS
/* a player's download of the map file (its file's name, the advertised
map's) of the host's version from the host (host: its invite, or its
identifier, as network_game_client_join_invite_host takes them), the
network searching and the game not joined: TRUE if it began (its progress
map_download_get's). The game is joined as a machine with no player, and
left again once the file is whole or the download failed */
boolean map_share_player_start(char const *host, char const *file, unsigned long version);
/* whether one is under way (until the game is left) */
boolean map_share_player_busy(void);
/* each frame while one is (browser_screen_host_download_update's, from the
main loop) */
void map_share_player_update(void);
#endif

#endif
