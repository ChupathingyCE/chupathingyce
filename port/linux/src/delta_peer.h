/*
DELTA_PEER.H

Delta Peer (docs/delta.md): ChupathingyCE's own messages between the
machines of one game, beside OpenCE's game protocol, which stays as it is.

- A host that speaks it listens on DELTA_PEER_PORT (the game's port + 10)
  and sets DELTA_ADVERTISED_FLAG in its advertisement.
- A client that joined such a host (the game's join, unchanged) sends HELLO;
  the host answers WELCOME, or LEGACY for another major. No answer within
  DELTA_PEER_HANDSHAKE_TIME means legacy only: the game never waits.
- Each pair uses the capabilities both have; the host sends every Delta
  client a ROSTER of every machine's claims (its platform key, its profile)
  and of what the whole room shares.
- Either side with a newer signed legacy table than the other's sends it
  (TABLE, in pieces; TABLE_HAVE says what each has): delta.c checks it.

Its sockets are the game's own Winsock layer's (xnet.c), so network.address
and the invite tunnel (p2p.c) carry them as they do the game's: no new path.

Two layers here: the sessions (struct delta_peer, delta_peer.c: no sockets,
no platform; the tests drive it directly) and the game's (delta_peer_game.c:
one session over real sockets, called from the shared source's hooks).
Nothing is trusted: a peer's claims are shown as claims and never decide
game state; its limits only make a host take fewer players.
*/

#ifndef HALO_DELTA_PEER_H
#define HALO_DELTA_PEER_H

#include "delta_wire.h"

enum
{
	/* a game's machines and players (HALO_PORT_MAXIMUM_NETWORK_MACHINES and
	_PLAYERS) */
	DELTA_PEER_MAXIMUM_MACHINES = 128,
	DELTA_PEER_MAXIMUM_PLAYERS = 128,
	/* a room limit nothing sets */
	DELTA_PEER_NO_LIMIT = 255,

	/* milliseconds: HELLO is sent again this often until answered, and the
	client gives up (legacy only) after the handshake time */
	DELTA_PEER_HELLO_INTERVAL = 500,
	DELTA_PEER_HANDSHAKE_TIME = 4000,
	/* the host sends its roster this often, and at once (no more often than
	the gap) when it changes */
	DELTA_PEER_ROSTER_INTERVAL = 5000,
	DELTA_PEER_ROSTER_GAP = 250,
	/* a client forgets a machine the roster has not named this long */
	DELTA_PEER_ROSTER_EXPIRY = 12000,
	/* a host takes a new session (a HELLO of a new session) from a machine
	at most this often */
	DELTA_PEER_NEW_SESSION_GAP = 1000,

	/* the messages a host takes from one machine, a second (and at once);
	a client from its host */
	DELTA_PEER_HOST_RATE = 10,
	DELTA_PEER_HOST_BURST = 20,
	DELTA_PEER_CLIENT_RATE = 50,
	DELTA_PEER_CLIENT_BURST = 100,
	/* the datagrams read in one frame */
	DELTA_PEER_FRAME_DATAGRAMS = 64,

	/* the legacy table's relay (milliseconds): a session's first pass waits
	this long after the handshake; a piece is sent to a machine at most this
	often (5 a second: under the host's DELTA_PEER_HOST_RATE from a machine,
	with room for the session's own messages); passes to one machine, at
	most this many a session, this far apart; a received table is checked
	(its signature) at most once a minute a machine; a piece-by-piece table
	from one machine is given up after this long without a piece; a host
	sends to at most this many machines at once */
	DELTA_PEER_TABLE_DELAY = 2000,
	DELTA_PEER_TABLE_GAP = 200,
	DELTA_PEER_TABLE_PASSES = 3,
	DELTA_PEER_TABLE_PASS_GAP = 60000,
	DELTA_PEER_TABLE_CHECK_GAP = 60000,
	DELTA_PEER_TABLE_STALE = 5000,
	DELTA_PEER_TABLE_SENDS = 4,
	/* (and says TABLE_HAVE to a machine at most once a second) */
	DELTA_PEER_TABLE_HAVE_GAP = 1000,
	/* the game's map (MAP) is sent to a client at once when it changes, and
	again this often (a datagram lost) */
	DELTA_PEER_MAP_INTERVAL = 5000,
	/* ... and the game's player traits (TRAITS) the same way */
	DELTA_PEER_TRAITS_INTERVAL = 5000,

	/* moderation (milliseconds where a time): the host takes MOD_ACTION
	from a machine at most this often (and at once); checks a machine's
	signatures (PROOF, BIND_ANSWER) at most this often; says MOD_CHALLENGE
	again (to a machine that has not signed in), MOD_STATE (to one that
	has) and a waiting MOD_BIND with the roster; a bind waits this long for
	its answer; a client keeps a notice or result to show this long */
	DELTA_PEER_MODERATION_ACTION_RATE = 1,
	DELTA_PEER_MODERATION_ACTION_BURST = 3,
	DELTA_PEER_MODERATION_SIGNATURE_RATE = 2,
	DELTA_PEER_MODERATION_SIGNATURE_BURST = 2,
	DELTA_PEER_MODERATION_BIND_TIME = 120000,

	/* chat (docs/delta.md, Chat): the host takes CHAT_SAY from a machine at
	most this often (and at once), says CHAT_CONFIG again this often (and on
	a change, no sooner than the roster's gap); a client takes CHAT_LINE at
	most this often (and at once), and keeps the last lines it heard */
	DELTA_PEER_CHAT_SAY_RATE = 1,
	DELTA_PEER_CHAT_SAY_BURST = 3,
	/* past the rate, a machine's lines refused this long (its chat paused;
	delta_peer_set_chat_cooldown) */
	DELTA_PEER_CHAT_COOLDOWN = 5000,
	DELTA_PEER_CHAT_CONFIG_INTERVAL = 10000,
	DELTA_PEER_CHAT_LINE_RATE = 10,
	DELTA_PEER_CHAT_LINE_BURST = 20,
	DELTA_PEER_CHAT_LINES = 32,

	/* team changes: TEAM_REQUEST a host takes from one machine (the host's
	own rules space them out further: team_change.c) */
	DELTA_PEER_TEAM_RATE = 1,
	DELTA_PEER_TEAM_BURST = 3
};

/* the room's limits (delta_peer_room_limit) */
enum delta_peer_limit
{
	/* the most players the game may have: the host's host_players and every
	Delta machine's join limit (join_players_opt_in for one opted in) */
	_delta_peer_limit_players = 0
};

/* a client's handshake */
enum delta_peer_client_state
{
	_delta_peer_client_off = 0,
	/* HELLO sent, no answer yet */
	_delta_peer_client_waiting,
	_delta_peer_client_delta,
	/* the host answered LEGACY, or nothing, or said BYE: the legacy
	protocol alone until the client leaves the game */
	_delta_peer_client_legacy
};

/* what a session asks of the machine it runs on */
struct delta_peer_env
{
	void *context;
	/* a datagram to ipv4 (as in_addr's s_addr: compared, never read) and
	port (host order) */
	void (*send_datagram)(void *context, delta_u32 ipv4, unsigned short port, const unsigned char *data, int size);
	/* a line for the log (never an address) */
	void (*log_line)(void *context, const char *text);
	/* a random number (sessions) */
	delta_u32 (*random_number)(void *context);
	/* (may be NULL: no relay) the legacy table's relay (delta.c's
	delta_legacy_serial, _signed and _offer): this machine's serial now (0
	the built-in table; DELTA_WIRE_TABLE_NONE: it takes no tables and
	sends none); its signed table into buffer (its size, 0 for none); a
	signed table from another machine, checked and taken if newer (1) */
	delta_u32 (*legacy_table_serial)(void *context);
	int (*legacy_table_signed)(void *context, unsigned char *buffer, int size);
	int (*legacy_table_offer)(void *context, const unsigned char *table, int size);
	/* (may be NULL: none) whether the legacy table's kill switch turns a
	capability off (delta_capability_disabled): never offered, agreed or
	used while it does, whatever local.capabilities has */
	int (*capability_disabled)(void *context, int capability);
	/* (may be NULL: delta.h's defaults) the platform policy's row for a
	platform, which the signed legacy table may tune
	(delta_peer_platform_policy): key holds the defaults on the way in */
	void (*platform_policy)(void *context, int platform, struct delta_platform_key *key);
	/* (a client's; may be NULL: it never signs) the moderator key's
	signature of message, and the key: 1, else 0 (no player key). Called
	only when the player acts (signs in, answers a bind) */
	int (*moderation_sign)(void *context, const unsigned char *message, int size,
		unsigned char key[DELTA_WIRE_MODERATION_KEY_SIZE], unsigned char signature[DELTA_WIRE_MODERATION_SIGNATURE_SIZE]);
	/* (a client's; may be NULL: none needed) the binding a challenge from
	the host at ipv4 must carry: 1 and it in binding (the invite's host
	identity this machine joined through), 0 if any will do (a LAN host),
	-1 if this machine cannot tell and signs nothing */
	int (*moderation_binding)(void *context, delta_u32 host_ipv4, char *binding, int size);
};

/* a host's moderation (the dedicated server's: delta_moderation.h's
struct, the session's view); a host with none does not offer moderation */
struct delta_peer_moderation_host
{
	void *context;
	/* a machine proved its moderator key this session (key), or it is
	forgotten (key NULL: the session ended, the machine left) */
	void (*machine_key)(void *context, int machine_index, const unsigned char *key);
	/* the role a key has now (enum delta_moderation_role), its permissions
	and its longest timed ban */
	int (*key_role)(void *context, const unsigned char *key, delta_u32 *permissions, delta_u32 *ban_minutes);
	/* a signed action of a machine's key (its signature, sequence and kind
	checked; its permission is the host's to check): 1 if done, and what to
	tell the player in result */
	int (*action)(void *context, int machine_index, const unsigned char *key, int action, int target_machine,
		int minutes, const char *reason, char *result, int result_size);
	/* a bind's answer, signed by key (for the request the host sent) */
	void (*bind_answer)(void *context, int machine_index, delta_u32 request, int accepted, const unsigned char *key);
};

/* a host's chat (the game's: delta_peer_game.c routes lines by team and
names their speakers); a host with none takes no CHAT_SAY */
struct delta_peer_chat_host
{
	void *context;
	/* a machine's player said a line (its session's sequence, rate, repeat,
	text, the host's mode and the machine's mute checked): 0 if it is
	relayed (the host sends it on with delta_peer_chat_line), else why not
	(enum delta_chat_refusal) */
	int (*say)(void *context, int machine_index, int scope, const char *text, int length);
};

/* a host's team changes (the game's: port/linux/game/team_change.c); a
host with none does not offer team_change */
struct delta_peer_team_host
{
	void *context;
	/* a machine asks for its player (the game's player list index) to
	change to a team (0, 1, or DELTA_WIRE_TEAM_OTHER): its answer (enum
	delta_team_result), the player's team now in team, and with too soon the
	seconds to wait in seconds */
	int (*request)(void *context, int machine_index, int player, int team, int *team_now, int *seconds);
};

/* the host's chat with a client's session */
struct delta_peer_host_chat
{
	/* the last CHAT_SAY's sequence */
	delta_u32 sequence;
	struct delta_rate rate;
	/* the last line relayed: its fingerprint and when */
	int has_last;
	delta_u32 last_fingerprint;
	delta_u32 last_time;
	/* its lines refused until then, once it said too much too fast */
	int paused;
	delta_u32 paused_until;
	/* CHAT_CONFIG as last sent, and when */
	int config_sent;
	struct delta_wire_chat_config config;
	delta_u32 config_time;
};

/* a client's chat with its host */
struct delta_peer_client_chat
{
	delta_u32 sequence;
	/* CHAT_CONFIG as heard (has_config: some heard) */
	int has_config;
	struct delta_wire_chat_config config;
	delta_u32 config_time;
	struct delta_rate rate;
	/* the lines heard, the last DELTA_PEER_CHAT_LINES of them: line n
	(from 0) is at n % DELTA_PEER_CHAT_LINES while n >= received - that */
	delta_u32 received;
	struct delta_wire_chat_line lines[DELTA_PEER_CHAT_LINES];
	/* the last refusal, and how many have come */
	delta_u32 refused_count;
	struct delta_wire_chat_refused refused;
};

/* the host's moderation with a client's session */
struct delta_peer_host_moderation
{
	/* the challenge said (its nonce and binding) */
	int challenged;
	unsigned char nonce[DELTA_WIRE_MODERATION_NONCE_SIZE];
	int binding_length;
	char binding[DELTA_WIRE_MODERATION_BINDING_SIZE + 1];
	/* its key proved */
	int verified;
	unsigned char key[DELTA_WIRE_MODERATION_KEY_SIZE];
	/* the last action's sequence */
	delta_u32 sequence;
	struct delta_rate action_rate;
	struct delta_rate signature_rate;
	/* MOD_STATE as last sent */
	int state_sent;
	struct delta_wire_mod_state state;
	/* a bind waiting for its answer */
	int binding_request;
	struct delta_wire_mod_bind bind;
	delta_u32 bind_time;
};

/* a client's moderation with its host */
struct delta_peer_client_moderation
{
	int challenged;
	unsigned char nonce[DELTA_WIRE_MODERATION_NONCE_SIZE];
	int binding_length;
	char binding[DELTA_WIRE_MODERATION_BINDING_SIZE + 1];
	/* PROOF sent (the player signed in); MOD_STATE heard */
	int signed_in;
	int has_state;
	struct delta_wire_mod_state state;
	delta_u32 sequence;
	/* the last result and notice heard (count: how many so far) */
	delta_u32 result_count;
	struct delta_wire_mod_result result;
	delta_u32 notice_count;
	struct delta_wire_mod_notice notice;
	/* a bind waiting for the player's answer */
	int bind_waiting;
	struct delta_wire_mod_bind bind;
	delta_u32 bind_time;
};

/* this machine */
struct delta_peer_local
{
	delta_u32 capabilities;
	unsigned short legacy_version;
	/* (with no legacy_table_serial in the env: this, which never changes) */
	delta_u32 legacy_table_serial;
	struct delta_platform_key key;
	char build[DELTA_WIRE_BUILD_SIZE + 1];
	int has_profile;
	struct delta_wire_profile profile;
	/* a host that applies no platform caveats (network.host_platform_limits
	= false: for testing) */
	int ignore_platform_limits;
};

/* a machine of the game, as the game says (the host's view) */
struct delta_peer_game_machine
{
	unsigned char machine_index;
	/* the host's own */
	unsigned char local;
	delta_u32 ipv4;
};

/* what is known of a machine: the host's from the handshakes, a client's
from the roster */
struct delta_peer_machine
{
	int known;
	/* DELTA_ROSTER_* */
	unsigned char flags;
	delta_u32 capabilities;
	struct delta_platform_key key;
	struct delta_wire_profile profile;
	delta_u32 seen_time;
};

/* the legacy table's relay with one machine (the host's with each client,
a client's with its host) */
struct delta_peer_relay
{
	/* its serial, from the handshake and TABLE_HAVE */
	delta_u32 their_serial;
	/* the session's start (passes wait DELTA_PEER_TABLE_DELAY) */
	delta_u32 start_time;
	/* sending to it: the serial, the next piece's offset, when the last
	piece went */
	int sending;
	delta_u32 sending_serial;
	delta_u32 offset;
	delta_u32 piece_time;
	/* passes begun, and when the last began */
	int passes;
	delta_u32 pass_time;
	/* a table from it checked, and when */
	int checked;
	delta_u32 check_time;
	/* TABLE_HAVE said to it, and when */
	int said;
	delta_u32 said_time;
	/* the serial last said to it in TABLE_HAVE (this machine's changed:
	said again) */
	delta_u32 said_serial;
};

/* the host's view of a client machine's session */
struct delta_peer_host_peer
{
	int used;
	delta_u32 ipv4;
	unsigned short port;
	delta_u32 session;
	delta_u32 session_time;
	delta_u32 agreed;
	struct delta_wire_hello hello;
	int has_profile;
	struct delta_wire_profile profile;
	struct delta_peer_relay relay;
	/* the game's map as last sent to it (the host's map_generation; 0:
	none), and when */
	delta_u32 map_sent_generation;
	delta_u32 map_time;
	/* the game's traits as last sent to it (traits_generation), and when */
	delta_u32 traits_sent_generation;
	delta_u32 traits_time;
	struct delta_peer_host_moderation moderation;
	struct delta_peer_host_chat chat;
	/* team changes: the last TEAM_REQUEST's sequence, and their rate */
	delta_u32 team_sequence;
	struct delta_rate team_rate;
};

/* a signed legacy table coming in, piece by piece: one at a time, from one
machine */
struct delta_peer_table_in
{
	int active;
	/* its machine (the host's client), or -1 (a client's host) */
	int from;
	delta_u32 serial;
	delta_u32 total;
	/* the pieces in (bit n: the piece at n * DELTA_WIRE_TABLE_CHUNK) */
	delta_u32 pieces;
	delta_u32 time;
	unsigned char data[DELTA_LEGACY_SIGNED_SIZE];
};

struct delta_peer
{
	struct delta_peer_env env;
	struct delta_peer_local local;
	/* 0 off, 1 client, 2 host */
	int mode;

	/* the host's */
	struct delta_peer_game_machine game_machines[DELTA_PEER_MAXIMUM_MACHINES];
	int game_machine_count;
	struct delta_peer_host_peer peers[DELTA_PEER_MAXIMUM_MACHINES];
	struct delta_rate rates[DELTA_PEER_MAXIMUM_MACHINES];
	/* a table from each machine checked, and when (as the rates, the
	machine's for the game: a new session keeps it) */
	unsigned char table_checked[DELTA_PEER_MAXIMUM_MACHINES];
	delta_u32 table_check_time[DELTA_PEER_MAXIMUM_MACHINES];
	int roster_dirty;
	delta_u32 roster_time;
	int roster_sent;
	/* the game's map (delta_peer_set_map), and a number raised each time it
	changes (0: none set) */
	struct delta_wire_map map;
	delta_u32 map_generation;
	/* the game's player traits (delta_peer_set_traits), and a number raised
	each time they change (0: none set) */
	struct delta_wire_traits traits;
	delta_u32 traits_generation;
	/* each game machine's address and when the host first saw it in the
	game with it (0 address: not in the game): its handshake's window starts
	there (delta_peer_machine_lacks) */
	delta_u32 machine_seen_ipv4[DELTA_PEER_MAXIMUM_MACHINES];
	delta_u32 machine_seen_time[DELTA_PEER_MAXIMUM_MACHINES];
	unsigned char machine_seen[DELTA_PEER_MAXIMUM_MACHINES];

	/* the client's */
	int client_state;
	delta_u32 host_ipv4;
	unsigned short host_port;
	delta_u32 session;
	delta_u32 handshake_start;
	delta_u32 hello_time;
	unsigned char machine_index;
	struct delta_wire_welcome welcome;
	delta_u32 agreed;
	struct delta_rate host_rate;
	struct delta_peer_relay host_relay;
	/* the host's map (MAP), and a number raised each time it changes (0:
	none heard) */
	struct delta_wire_map host_map;
	delta_u32 host_map_generation;
	/* the host's traits (TRAITS), and a number raised each time they change
	(0: none heard) */
	struct delta_wire_traits host_traits;
	delta_u32 host_traits_generation;

	/* both: the legacy table relayed; this machine's signed table as it
	goes out (its serial: 0 not read yet), and one coming in */
	delta_u32 table_out_serial;
	int table_out_size;
	unsigned char table_out[DELTA_LEGACY_SIGNED_SIZE];
	struct delta_peer_table_in table_in;

	/* both: what is known of each machine, the room, and each player's
	machine (-1: none) */
	struct delta_peer_machine machines[DELTA_PEER_MAXIMUM_MACHINES];
	delta_u32 room_capabilities;
	unsigned char room_players;
	signed char player_machines[DELTA_PEER_MAXIMUM_PLAYERS];

	/* moderation: the host's (NULL: none, and not offered), and the
	client's with its host */
	const struct delta_peer_moderation_host *moderation_host;
	int moderation_binding_length;
	char moderation_binding[DELTA_WIRE_MODERATION_BINDING_SIZE + 1];
	int moderation_roles_dirty;
	delta_u32 moderation_time;
	struct delta_peer_client_moderation client_moderation;

	/* chat: the host's (NULL: none, and no CHAT_SAY taken), its mode and
	flags (CHAT_CONFIG's), each machine's mute (until the game drops the
	machine: chat_muted[machine] set, and chat_mute_until its end, 0 for
	none) and the lines it has sent; the client's with its host */
	const struct delta_peer_chat_host *chat_host;
	unsigned char chat_mode;
	unsigned char chat_flags;
	/* (milliseconds a machine's chat pauses past the rate; 0: none) */
	delta_u32 chat_cooldown;
	int chat_dirty;
	unsigned char chat_muted[DELTA_PEER_MAXIMUM_MACHINES];
	delta_u32 chat_mute_until[DELTA_PEER_MAXIMUM_MACHINES];
	delta_u32 chat_lines_sent;
	struct delta_peer_client_chat client_chat;

	/* team changes: the host's (NULL: none, not offered); the client's last
	TEAM_REQUEST's sequence, and the results heard (how many, the last) */
	const struct delta_peer_team_host *team_host;
	delta_u32 team_sequence;
	delta_u32 team_results;
	struct delta_wire_team_result team_result;

	/* counted for the tests and the log */
	unsigned long dropped;
};

/* ---------- sessions (delta_peer.c) */

void delta_peer_initialize(struct delta_peer *peer, const struct delta_peer_env *env,
	const struct delta_peer_local *local);

/* each frame while hosting: the game's machines and each player's machine
(player_machines[player]: -1 for none). Sends what is due */
void delta_peer_host_frame(struct delta_peer *peer, delta_u32 now, const struct delta_peer_game_machine *machines,
	int count, const signed char *player_machines);
/* each frame while a client of another machine's game: joined (in its
pregame, game or postgame), the host's address and Delta port, whether its
advertisement has DELTA_ADVERTISED_FLAG, this machine's index in the game
(DELTA_WIRE_NO_MACHINE: not yet) and each player's machine */
void delta_peer_client_frame(struct delta_peer *peer, delta_u32 now, int joined, delta_u32 host_ipv4,
	unsigned short host_port, int host_speaks_delta, unsigned char machine_index, const signed char *player_machines);
/* leaving: BYE to whoever has a session, and everything forgotten */
void delta_peer_stop(struct delta_peer *peer);
/* a datagram that came to the Delta socket */
void delta_peer_receive(struct delta_peer *peer, delta_u32 now, delta_u32 ipv4, unsigned short port,
	const unsigned char *data, int size);

/* the host's: the game's map, sent to each client that agreed to ce_maps
(NULL: none, nothing sent). Called each frame; a map the same as the last
changes nothing */
void delta_peer_set_map(struct delta_peer *peer, const struct delta_wire_map *map);
/* the client's: the host's map as MAP said it last, and the number raised
each time it changed (0 if none heard: a host without Delta, or that did not
agree to ce_maps) */
delta_u32 delta_peer_host_map(const struct delta_peer *peer, struct delta_wire_map *map);

/* the host's: the game's player traits, sent to each client that agreed to
bomb (NULL: none, nothing sent). Called each frame; traits the same as the
last change nothing */
void delta_peer_set_traits(struct delta_peer *peer, const struct delta_wire_traits *traits);
/* the client's: the host's traits as TRAITS said them last, and the number
raised each time they changed (0 if none heard) */
delta_u32 delta_peer_host_traits(const struct delta_peer *peer, struct delta_wire_traits *traits);
/* the host's: whether a machine of its game lacks a capability: it speaks
Delta and did not agree to it (at once), or it has said nothing for the
handshake's window (DELTA_PEER_HANDSHAKE_TIME) since the host first saw it
in the game (an OpenCE machine, one of another major, or one whose Delta
cannot reach the host). 0 for one that has it, one still in its window, the
host's own, or a machine not in the game; a capability this host does not
offer itself is lacked by every other machine */
int delta_peer_machine_lacks(const struct delta_peer *peer, delta_u32 now, int machine_index, int capability);
/* ... and whether one is still in its window: in the game, not the host's,
no session yet and its window not over (a game that needs the capability
waits for it) */
int delta_peer_machine_pending(const struct delta_peer *peer, delta_u32 now, int machine_index);

/* ---------- moderation (docs/delta.md, Moderation)

The host's: a client that agreed to moderation is sent a challenge (a nonce
of its session, and the host's binding); it signs in by signing it with its
moderator key; its actions are signed the same way, each with a sequence
above the last. The host's moderation (set_moderation_host) decides what a
key may do. */

/* the host's moderation (NULL: none; moderation is then not offered to
clients that say HELLO from now) */
void delta_peer_set_moderation_host(struct delta_peer *peer, const struct delta_peer_moderation_host *host);
/* the host's binding: what its challenges carry (none: a LAN host) */
void delta_peer_set_moderation_binding(struct delta_peer *peer, const char *binding);
/* (host) a machine's key proved this session: 1 and it in key */
int delta_peer_moderation_key(const struct delta_peer *peer, int machine_index, unsigned char *key);
/* (host) whether a machine's session agreed to moderation */
int delta_peer_moderation_capable(const struct delta_peer *peer, int machine_index);
/* (host) a notice to a machine's player (kind: _delta_moderation_notice_*):
1 if sent */
int delta_peer_moderation_notice(struct delta_peer *peer, int machine_index, int kind, const char *text);
/* (host) asks a machine's player to link the game to an account (the
server's panel's) on a server: 1 if sent; the answer comes to bind_answer */
int delta_peer_moderation_bind(struct delta_peer *peer, delta_u32 now, int machine_index, delta_u32 request,
	const char *account, const char *server);
/* (host) the roles changed: MOD_STATE said again to every machine signed in */
void delta_peer_moderation_roles_changed(struct delta_peer *peer);

/* (client) whether the host agreed to moderation, and has said its
challenge */
int delta_peer_client_moderation_ready(const struct delta_peer *peer);
/* (client) the player signs in: PROOF sent, 1; 0 if not ready, no key, or
the challenge's binding is not the host this machine joined (a relay) */
int delta_peer_client_moderation_sign_in(struct delta_peer *peer);
/* (client) an action signed and sent: its sequence, 0 if not */
delta_u32 delta_peer_client_moderation_action(struct delta_peer *peer, int action, int target_machine, int minutes,
	const char *reason);
/* (client) the player's answer to the waiting bind: 1 if sent */
int delta_peer_client_moderation_bind_answer(struct delta_peer *peer, int accepted);

/* ---------- chat (docs/delta.md, Chat)

Player lines need chat agreed by both sides; the host's own lines (server
and system kinds) need server_messages. The host checks each CHAT_SAY's
session, sequence, rate, repeat and text, its mode and the machine's mute,
then asks its chat (set_chat_host) to route the line; lines go out with
delta_peer_chat_line. */

/* (host) its chat (NULL: none, no CHAT_SAY taken) */
void delta_peer_set_chat_host(struct delta_peer *peer, const struct delta_peer_chat_host *host);
/* (host) what it relays (enum delta_chat_mode) and CHAT_CONFIG's flags
(DELTA_CHAT_CONFIG_FILTERED); each client told on a change */
void delta_peer_set_chat_mode(struct delta_peer *peer, int mode, int flags);
/* (host) the seconds a machine's chat pauses once it says too much too fast
(past DELTA_PEER_CHAT_SAY_RATE and _BURST): its lines refused, CHAT_REFUSED
saying how long; 0: each line past the rate refused alone */
void delta_peer_set_chat_cooldown(struct delta_peer *peer, int seconds);
/* (host) a machine's lines not relayed: for minutes (0: until it leaves
the game), or relayed again (mute 0) */
void delta_peer_chat_mute(struct delta_peer *peer, delta_u32 now, int machine_index, int mute, int minutes);
/* (host) whether a machine is muted now */
int delta_peer_chat_muted(const struct delta_peer *peer, delta_u32 now, int machine_index);
/* (host) whether a machine's session takes a line of the kind (chat
agreed for a player's, server_messages for the host's own) */
int delta_peer_chat_capable(const struct delta_peer *peer, int machine_index, int kind);
/* (host) a line to a machine (its line number is the host's): 1 if sent */
int delta_peer_chat_line(struct delta_peer *peer, int machine_index, const struct delta_wire_chat_line *line);

/* (client) whether the host agreed to chat (player lines may be said) */
int delta_peer_client_chat_ready(const struct delta_peer *peer);
/* (client) a line its player says (UTF-8; checked as the host will):
its sequence, 0 if not sent */
delta_u32 delta_peer_client_chat_say(struct delta_peer *peer, int scope, const char *text, int length);
/* (client) the lines heard so far (a count that only grows), and line
number index of them: 1 if it is still kept */
delta_u32 delta_peer_client_chat_received(const struct delta_peer *peer);
int delta_peer_client_chat_get(const struct delta_peer *peer, delta_u32 index, struct delta_wire_chat_line *line);

/* ---------- team changes (docs/delta.md, Team changes)

A client asks for its player to change teams (TEAM_REQUEST, both agreed to
team_change); the host checks the session, the rising sequence and its
budget, then asks its team host, and answers TEAM_RESULT. The change itself
reaches every machine through the game. */

/* (host) its team changes (NULL: none; team_change is then not offered to
clients that say HELLO from now) */
void delta_peer_set_team_host(struct delta_peer *peer, const struct delta_peer_team_host *host);
/* (client) whether the host agreed to team_change */
int delta_peer_client_team_ready(const struct delta_peer *peer);
/* (client) its player (the game's player list index) asks for a team (0,
1, DELTA_WIRE_TEAM_OTHER): the request's sequence, 0 if not sent */
delta_u32 delta_peer_client_team_request(struct delta_peer *peer, int player, int team);
/* (client) the results heard so far (a count that only grows), and the
last in result */
delta_u32 delta_peer_client_team_result(const struct delta_peer *peer, struct delta_wire_team_result *result);

/* what is known of a machine (0 if nothing) */
int delta_peer_machine(const struct delta_peer *peer, int machine_index, struct delta_peer_machine *machine);
/* whether every machine of the game has the capability now (room-wide
capabilities are on only while this is so) */
int delta_peer_room_has(const struct delta_peer *peer, int capability);
/* the room's limit (enum delta_peer_limit); DELTA_PEER_NO_LIMIT if none */
int delta_peer_room_limit(const struct delta_peer *peer, int limit);
/* whether the host's game may be network co-op: no machine of it (its
own, or a Delta machine's platform) is of a platform the policy keeps out
of co-op (delta_platform_policy_coop); 1 when not hosting, or for a host
that ignores the platform caveats (network.host_platform_limits = false) */
int delta_peer_room_coop(const struct delta_peer *peer);
/* the most players a game with this platform key's machine may have
without harm to it: the platform policy's join limit for its platform, or
the key's own if lower; no limit if its player opted in
(DELTA_PLATFORM_KEY_OPTED_IN: network.platform_limits = "off") */
int delta_peer_key_join_limit(const struct delta_peer *peer, const struct delta_platform_key *key);

/* ---------- the game's (delta_peer_game.c): one session over the game's
sockets. The hooks are called from the shared source (network_*.c) under
HALO_GAME_BROWSER; everything else reads them. */

/* network.protocol: "auto" (the default), "delta" or "opence" */
enum delta_peer_protocol
{
	_delta_peer_protocol_auto = 0,
	/* (for now as auto: only marks the intent; Delta-only rooms and their
	listing come later) */
	_delta_peer_protocol_delta,
	/* Delta Peer off: no socket, no flag, no handshake */
	_delta_peer_protocol_opence
};
int delta_peer_protocol(void);

/* the host's advertisement flags to add (DELTA_ADVERTISED_FLAG, or 0) */
unsigned char delta_peer_advertised_flags(void);
/* the game wire after them (DELTA_ADVERTISED_WIRE_OFFSET), in the
advertisement's reserved bytes (size of them): zeros unless the flag is set */
void delta_peer_advertised_wire(unsigned char *reserved, int size);
/* whether a host's advertisement (its reserved bytes) is of this machine's
game wire: 1 when both speak Delta, of one major and one wire (the OpenCE
numbers then don't matter), 0 when the host's wire is another (its wire
into theirs, of theirs_size), -1 when either side doesn't say (this machine
on the legacy protocol, a host without Delta, of another major, or built
before the wire was advertised): the OpenCE numbers decide */
int delta_peer_advertised_wire_matches(const unsigned char *reserved, int size, char *theirs, int theirs_size);
/* the hosted game's player limit: DELTA_PEER_NO_LIMIT unless a Delta
machine (or this host's own key) takes fewer */
int delta_peer_host_player_limit(void);
/* whether this host offers network co-op now: not while a machine of a
platform without it (the original Xbox) is in its game (1 when not hosting
with Delta) */
int delta_peer_host_coop(void);

/* the hooks: the host's game each frame (machines and players as above);
the client each frame (not while this machine hosts: its own client is the
host's machine); the host's game or the client gone (host: which) */
void delta_peer_game_host_frame(const struct delta_peer_game_machine *machines, int count,
	const signed char *player_machines);
void delta_peer_game_client_frame(int joined, delta_u32 host_ipv4, int host_speaks_delta, int machine_index,
	const signed char *player_machines);
void delta_peer_game_stop(int host);

/* the platform policy's row for a platform (delta.h's defaults, in key on
the way in). The signed legacy table's "platform_policy" section is to tune
it here; for now the defaults stand */
void delta_peer_platform_policy(int platform, struct delta_platform_key *key);

/* this machine's platform key; a game's machine's (0 if not known) */
void delta_peer_local_key(struct delta_platform_key *key);
int delta_peer_machine_key(int machine_index, struct delta_platform_key *key);
/* a player's platform (enum delta_platform; unknown if its machine did not
say, or did not agree to platform): for the scoreboard's icons */
int delta_peer_player_platform(int player_index);
/* a machine's profile claim: its player ID and revision (0 if none) */
int delta_peer_machine_profile(int machine_index, unsigned char *player_id, delta_u32 *revision);
/* Bomb game types (docs/bomb.md): the host's game's player traits, sent to
the clients that agreed to bomb (NULL: none: the game is no Bomb game
type); the client's: the host's traits, and the number raised each time
they changed (0: none heard, or not a client); whether a machine of the
hosted game lacks a capability (delta_peer_machine_lacks: every other
machine does while this host has no Delta), and whether it is still in its
handshake's window */
void delta_peer_game_set_traits(const struct delta_wire_traits *traits);
delta_u32 delta_peer_game_host_traits(struct delta_wire_traits *traits);
int delta_peer_game_machine_lacks(int machine_index, int capability);
/* whether a player's machine agreed to a capability with the host, as this
machine knows it (its own, the host's, the roster's); none for a player
without one */
int delta_peer_game_player_has(int player_index, int capability);
int delta_peer_game_machine_pending(int machine_index);
/* the game's room: delta_peer_room_has and _room_limit of the session */
int delta_peer_game_room_has(int capability);
int delta_peer_game_room_limit(int limit);
/* the client's handshake (enum delta_peer_client_state) */
int delta_peer_game_client_state(void);

/* team changes (port/linux/game/team_change.c): the host's team host (NULL:
none); the client's request for its player (the game's player list index)
and the results heard (a count that grows, and the last) */
void delta_peer_game_set_team_host(const struct delta_peer_team_host *host);
int delta_peer_game_team_ready(void);
unsigned int delta_peer_game_team_request(int player, int team);
unsigned int delta_peer_game_team_result(struct delta_wire_team_result *result);

/* ---------- what Delta List says of a hosted game (browser.c's
announcement; docs/delta.md, Delta List) */

/* a game's machines by platform: the registry's numbers, then those
without Delta (OpenCE's) */
#define DELTA_PEER_MACHINE_LEGACY NUMBER_OF_DELTA_PLATFORMS
#define DELTA_PEER_MACHINE_KINDS (NUMBER_OF_DELTA_PLATFORMS + 1)

struct delta_peer_host_summary
{
	/* hosting with Delta now (its socket open: the advertisement's
	DELTA_ADVERTISED_FLAG); 0 for a game hosted on the legacy protocol
	alone (network.protocol = "opence", or its port taken) */
	int delta;
	/* this machine's platform key and the capabilities it offers */
	struct delta_platform_key key;
	delta_u32 capabilities;
	/* the game's machines by platform, as Delta Peer knows them: a Delta
	machine that does not share its platform is unknown; a dedicated
	server leaves itself out (it has no players). Counts only, and only
	while delta */
	unsigned char machines[DELTA_PEER_MACHINE_KINDS];
};

/* the hosted game's summary (on the game's thread, as the hooks); 0 if
this machine is not hosting */
int delta_peer_game_host_summary(struct delta_peer_host_summary *summary);
/* a platform's registry name ("pc_linux"; "unknown" for a number this
build does not know), and a name's number (-1 for none) */
const char *delta_peer_platform_name(int platform);
int delta_peer_platform_number(const char *name);


#endif
