/*
DELTA_WIRE.H

Delta Peer's wire format (docs/delta.md, "Wire format"): its datagrams'
header and messages, encoded and decoded. Every decoder takes the bytes as
hostile input: sizes are checked before anything is read, counts capped,
text kept to printable ASCII, and nothing is allocated. Trailing bytes after
the fields a message has are ignored, so a later version may append fields
without raising the major.

Plain C89 with no platform dependency (no stdint.h, no snprintf), so the
fuzz harness (port/linux/tests/delta_fuzz.c), the dedicated server and
Warthog's old toolchain build it as it is. Numbers are little-endian.
*/

#ifndef HALO_DELTA_WIRE_H
#define HALO_DELTA_WIRE_H

#include "delta.h"

/* (32 bits on every target the game has: ILP32, LP64 and LLP64) */
typedef unsigned int delta_u32;

enum
{
	/* "DP" */
	DELTA_WIRE_MAGIC_0 = 0x44,
	DELTA_WIRE_MAGIC_1 = 0x50,
	/* magic (2), major, type, payload length (2), reserved (2), session (4):
	this layout is the same in every major, so a machine of another major
	can always be answered with LEGACY */
	DELTA_WIRE_HEADER_SIZE = 12,
	/* a whole datagram, header included: under the game's own (1264) and
	the invite tunnel's (1400) */
	DELTA_WIRE_MAXIMUM_DATAGRAM = 1200,
	DELTA_WIRE_MAXIMUM_PAYLOAD = DELTA_WIRE_MAXIMUM_DATAGRAM - DELTA_WIRE_HEADER_SIZE,

	/* a build's version string, as sent (printable ASCII) */
	DELTA_WIRE_BUILD_SIZE = 31,
	/* a profile's player ID (raw bytes; 32 hex digits as the site shows it) */
	DELTA_WIRE_PLAYER_ID_SIZE = 16,

	DELTA_WIRE_HELLO_SIZE = 20,
	DELTA_WIRE_WELCOME_SIZE = 24,
	DELTA_WIRE_ROSTER_SIZE = 12,
	DELTA_WIRE_PROFILE_SIZE = 20,
	DELTA_WIRE_ROSTER_ENTRY_SIZE = 36,
	/* (32) */
	DELTA_WIRE_MAXIMUM_ROSTER_ENTRIES =
		(DELTA_WIRE_MAXIMUM_PAYLOAD - DELTA_WIRE_ROSTER_SIZE) / DELTA_WIRE_ROSTER_ENTRY_SIZE,

	/* TABLE: its fields, then a piece of the signed legacy table of at most
	this many bytes; every piece but the last is that long, at an offset
	a multiple of it */
	DELTA_WIRE_TABLE_SIZE = 16,
	DELTA_WIRE_TABLE_CHUNK = 1024,
	/* (17: a whole signed table, DELTA_LEGACY_SIGNED_SIZE) */
	DELTA_WIRE_TABLE_CHUNKS = (DELTA_LEGACY_SIGNED_SIZE + DELTA_WIRE_TABLE_CHUNK - 1) / DELTA_WIRE_TABLE_CHUNK,
	DELTA_WIRE_TABLE_HAVE_SIZE = 4,

	/* MAP: its fixed fields, then the map file's name (at most this long:
	letters, digits, '_', '-', '.' and ' ') */
	DELTA_WIRE_MAP_SIZE = 44,
	DELTA_WIRE_MAP_NAME_SIZE = 63,
	/* the map file's hash: BLAKE2b-256 of every byte of it */
	DELTA_WIRE_MAP_HASH_SIZE = 32,

	/* TRAITS: its fields (the game's kind, reserved), then everyone's and the
	bomb carrier's player traits */
	DELTA_WIRE_TRAITS_SIZE = 28,
	DELTA_WIRE_PLAYER_TRAITS_SIZE = 12,
	/* ... and after them, from bomb_movement's builds on (earlier builds
	read the first 28 bytes alone): a version (1), 3 reserved, then
	everyone's and the carrier's movement traits */
	DELTA_WIRE_TRAITS_MOVEMENT_SIZE = 48,
	DELTA_WIRE_TRAITS_MOVEMENT_VERSION = 1,
	DELTA_WIRE_PLAYER_MOVEMENT_SIZE = 8,
	/* a trait's range, in percent (the game's own: game_engine.h's
	player_traits); a value past one is refused, never mended */
	DELTA_WIRE_TRAIT_HEALTH_MINIMUM = 10,
	DELTA_WIRE_TRAIT_HEALTH_MAXIMUM = 400,
	DELTA_WIRE_TRAIT_DAMAGE_MINIMUM = 25,
	DELTA_WIRE_TRAIT_DAMAGE_MAXIMUM = 400,
	DELTA_WIRE_TRAIT_RESISTANCE_MINIMUM = 25,
	DELTA_WIRE_TRAIT_RESISTANCE_MAXIMUM = 400,
	DELTA_WIRE_TRAIT_SPEED_MINIMUM = 50,
	DELTA_WIRE_TRAIT_SPEED_MAXIMUM = 200,
	/* (0: unchanged, as a host before them sends it) */
	DELTA_WIRE_TRAIT_JUMP_MINIMUM = 50,
	DELTA_WIRE_TRAIT_JUMP_MAXIMUM = 200,
	DELTA_WIRE_TRAIT_GRAVITY_MINIMUM = 25,
	DELTA_WIRE_TRAIT_GRAVITY_MAXIMUM = 200,
	DELTA_WIRE_TRAIT_MELEE_MINIMUM = 50,
	DELTA_WIRE_TRAIT_MELEE_MAXIMUM = 200,

	/* a machine index none has (a dedicated server's own machine, or a
	client not yet given one) */
	DELTA_WIRE_NO_MACHINE = 0xFF,

	/* moderation (docs/delta.md, Moderation): Ed25519's keys, signatures,
	and the challenge's nonce */
	DELTA_WIRE_MODERATION_KEY_SIZE = 32,
	DELTA_WIRE_MODERATION_SIGNATURE_SIZE = 64,
	DELTA_WIRE_MODERATION_NONCE_SIZE = 32,
	/* a challenge's binding: the host's internet identity as its invite
	starts (its key's hash, 32 hex digits), or none on a LAN */
	DELTA_WIRE_MODERATION_BINDING_SIZE = 64,
	/* an action's reason, a result's and a notice's text, a bind's account
	and server names (printable ASCII; their bytes, without an end) */
	DELTA_WIRE_MODERATION_REASON_SIZE = 63,
	DELTA_WIRE_MODERATION_TEXT_SIZE = 127,
	DELTA_WIRE_MODERATION_NAME_SIZE = 31,
	/* the messages' fixed parts (the text follows) */
	DELTA_WIRE_MOD_CHALLENGE_SIZE = 36,
	DELTA_WIRE_MOD_PROOF_SIZE = 96,
	DELTA_WIRE_MOD_STATE_SIZE = 12,
	DELTA_WIRE_MOD_ACTION_SIZE = 76,
	DELTA_WIRE_MOD_RESULT_SIZE = 8,
	DELTA_WIRE_MOD_NOTICE_SIZE = 4,
	DELTA_WIRE_MOD_BIND_SIZE = 8,
	DELTA_WIRE_MOD_BIND_ANSWER_SIZE = 104,
	/* a signed message's bytes, at most (moderation_*_message) */
	DELTA_WIRE_MODERATION_MESSAGE_SIZE = 256,

	/* chat (docs/delta.md, Chat): a player's line, at most this many bytes
	of UTF-8 and characters; the host's own (server and system lines) at
	most this many bytes; a speaker's name as the host gives it */
	DELTA_WIRE_CHAT_SAY_TEXT_SIZE = 120,
	DELTA_WIRE_CHAT_SAY_CHARACTERS = 80,
	DELTA_WIRE_CHAT_TEXT_SIZE = 200,
	DELTA_WIRE_CHAT_NAME_SIZE = 47,
	/* the messages' fixed parts (the text follows) */
	DELTA_WIRE_CHAT_CONFIG_SIZE = 8,
	DELTA_WIRE_CHAT_SAY_SIZE = 8,
	DELTA_WIRE_CHAT_LINE_SIZE = 12,
	DELTA_WIRE_CHAT_REFUSED_SIZE = 8,

	/* team changes (docs/delta.md, Team changes) */
	DELTA_WIRE_TEAM_REQUEST_SIZE = 8,
	DELTA_WIRE_TEAM_RESULT_SIZE = 8,
	/* TEAM_REQUEST's team: whichever team the player is not on */
	DELTA_WIRE_TEAM_OTHER = 0xFF,

	/* object sync (docs/delta.md, Object sync): UNIT_VITALS's count and
	reserved bytes, then 12-byte entries, as many as a datagram holds */
	DELTA_WIRE_UNIT_VITALS_SIZE = 4,
	DELTA_WIRE_UNIT_VITALS_ENTRY_SIZE = 12,
	/* (98) */
	DELTA_WIRE_MAXIMUM_UNIT_VITALS =
		(DELTA_WIRE_MAXIMUM_PAYLOAD - DELTA_WIRE_UNIT_VITALS_SIZE) / DELTA_WIRE_UNIT_VITALS_ENTRY_SIZE,
	/* ... OBJECT_NAMES's total, first and count, then 12-byte entries: the
	host's named objects and the names it removed (at most a scenario's
	names) in as many datagrams as it takes */
	DELTA_WIRE_OBJECT_NAMES_SIZE = 8,
	DELTA_WIRE_OBJECT_NAMES_ENTRY_SIZE = 12,
	/* (98) */
	DELTA_WIRE_MAXIMUM_OBJECT_NAMES_ENTRIES =
		(DELTA_WIRE_MAXIMUM_PAYLOAD - DELTA_WIRE_OBJECT_NAMES_SIZE) / DELTA_WIRE_OBJECT_NAMES_ENTRY_SIZE,
	/* (the game's MAXIMUM_OBJECT_NAMES_PER_SCENARIO) */
	DELTA_WIRE_MAXIMUM_OBJECT_NAMES = 512
};

/* UNIT_VITALS's entry flags: the unit's shield, as the game's own unit
states say it */
enum
{
	DELTA_WIRE_VITALS_SHIELD_DEPLETED = 0x01,
	DELTA_WIRE_VITALS_SHIELD_CHARGING = 0x02,
	DELTA_WIRE_VITALS_SHIELD_OVER_CHARGING = 0x04,
	DELTA_WIRE_VITALS_FLAGS = 0x07
};

/* UNIT_VITALS's own flags (its second byte) */
enum
{
	/* the last datagram of a full pass: every unit the host's vitals
	cover, sent when a client asks for the host's objects */
	DELTA_WIRE_UNIT_VITALS_PASS = 0x01
};

/* TEAM_RESULT's results; a number is never reused */
enum delta_team_result
{
	/* the player is on the new team (dead, back on it at their respawn) */
	_delta_team_changed = 0,
	/* the change waits for the player's next respawn (a round-based game
	type: Bomb) */
	_delta_team_changes_at_respawn = 1,
	/* this host takes no team changes */
	_delta_team_refused_off = 2,
	/* the teams would be uneven */
	_delta_team_refused_uneven = 3,
	/* a game without teams */
	_delta_team_refused_no_teams = 4,
	/* too soon after the player's last change (TEAM_RESULT's seconds: how
	long until the next) */
	_delta_team_refused_too_soon = 5,
	/* not while no game is being played (the lobby, the carnage report) */
	_delta_team_refused_not_now = 6,
	/* no such player of the machine */
	_delta_team_refused_player = 7,
	/* the player is on that team already */
	_delta_team_refused_same = 8,
	/* the player is the last of their team (a team game with everyone on
	one team is over) */
	_delta_team_refused_last = 9
};

/* CHAT_CONFIG's text mode: what the host relays */
enum delta_chat_mode
{
	_delta_chat_mode_off = 0,
	_delta_chat_mode_team = 1,
	_delta_chat_mode_on = 2
};

/* CHAT_CONFIG's flags */
enum
{
	/* this player's lines are not relayed (a moderator's mute) */
	DELTA_CHAT_CONFIG_MUTED = 0x01,
	/* the host refuses lines with blocked words */
	DELTA_CHAT_CONFIG_FILTERED = 0x02
};

/* CHAT_SAY's and CHAT_LINE's scope */
enum delta_chat_scope
{
	_delta_chat_scope_all = 0,
	_delta_chat_scope_team = 1,

	NUMBER_OF_DELTA_CHAT_SCOPES
};

/* CHAT_LINE's kinds: who speaks */
enum delta_chat_kind
{
	/* a player */
	_delta_chat_kind_player = 0,
	/* a player signed in as a moderator of the server */
	_delta_chat_kind_moderator = 1,
	/* the server (its admin: sv_say, a notice) */
	_delta_chat_kind_server = 2,
	/* the game itself (needs server_messages, as server lines) */
	_delta_chat_kind_system = 3,

	NUMBER_OF_DELTA_CHAT_KINDS
};

/* CHAT_REFUSED's reasons; a number is never reused */
enum delta_chat_refusal
{
	_delta_chat_refused_none = 0,
	_delta_chat_refused_off = 1,
	/* team only on this host, or team chat in a game without teams */
	_delta_chat_refused_scope = 2,
	_delta_chat_refused_muted = 3,
	_delta_chat_refused_too_fast = 4,
	_delta_chat_refused_blocked = 5,
	_delta_chat_refused_bad_text = 6
};

/* a moderator's role (MOD_STATE): none, then each with more than the last */
enum delta_moderation_role
{
	_delta_moderation_role_none = 0,
	_delta_moderation_role_moderator = 1,
	_delta_moderation_role_admin = 2,
	_delta_moderation_role_owner = 3
};

/* a role's permissions (MOD_STATE's, and the dedicated server's table:
server/README.md) */
enum
{
	DELTA_MODERATION_VIEW = 0x001,
	DELTA_MODERATION_WARN = 0x002,
	DELTA_MODERATION_KICK = 0x004,
	/* a ban no longer than MOD_STATE's ban minutes */
	DELTA_MODERATION_BAN_TIMED = 0x008,
	/* a ban of any length, for ever too */
	DELTA_MODERATION_BAN = 0x010,
	DELTA_MODERATION_UNBAN = 0x020,
	/* the map: end the game, play the next */
	DELTA_MODERATION_MAP = 0x040,
	DELTA_MODERATION_SETTINGS = 0x080,
	DELTA_MODERATION_ROLES = 0x100
};

/* MOD_ACTION's actions; a number is never reused, and a host refuses one it
does not know */
enum delta_moderation_action
{
	_delta_moderation_action_warn = 1,
	_delta_moderation_action_kick = 2,
	_delta_moderation_action_ban = 3,
	_delta_moderation_action_end_game = 4,
	_delta_moderation_action_next_map = 5,
	/* chat: the target's lines not relayed (minutes, 0 until the game ends),
	and relayed again */
	_delta_moderation_action_mute = 6,
	_delta_moderation_action_unmute = 7,

	NUMBER_OF_DELTA_MODERATION_ACTIONS
};

/* MOD_NOTICE's kinds */
enum
{
	_delta_moderation_notice_warning = 1,
	_delta_moderation_notice_info = 2
};

/* TABLE_HAVE's serial for a machine that takes no tables (a local legacy
table in use, or no key to check one with) */
#define DELTA_WIRE_TABLE_NONE 0xFFFFFFFFu

/* the message types; a number is never reused, and a machine ignores types
it does not know */
enum delta_message_type
{
	/* client to host: who it is and what it speaks */
	_delta_message_hello = 1,
	/* host to client: the same, and what the two will use */
	_delta_message_welcome = 2,
	/* host to client: another major; nothing more is said (its number and
	empty payload are the same in every major) */
	_delta_message_legacy = 3,
	/* host to client: every machine's claims, and the room's capabilities */
	_delta_message_roster = 4,
	/* either way: leaving; the other side forgets the session */
	_delta_message_bye = 5,
	/* client to host, after WELCOME, if both agreed to profile: its player
	ID and profile revision */
	_delta_message_profile = 6,
	/* either way: a piece of the sender's signed legacy table, sent to a
	machine whose serial is older (delta.c checks the whole) */
	_delta_message_table = 7,
	/* either way: the serial of the legacy table the sender has now
	(DELTA_WIRE_TABLE_NONE: it takes none) */
	_delta_message_table_have = 8,
	/* moderation (both agreed to it): host to client, the session's nonce
	and the host's binding */
	_delta_message_mod_challenge = 9,
	/* client to host: its moderator key, and the nonce signed with it */
	_delta_message_mod_proof = 10,
	/* host to client: the role and permissions its key has */
	_delta_message_mod_state = 11,
	/* client to host: a signed action (kick, ban, ...) */
	_delta_message_mod_action = 12,
	/* host to client: what an action did */
	_delta_message_mod_result = 13,
	/* host to client: a warning or notice to show its player */
	_delta_message_mod_notice = 14,
	/* host to client: link this game to an account of the server's panel? */
	_delta_message_mod_bind = 15,
	/* client to host: the player's answer, signed */
	_delta_message_mod_bind_answer = 16,
	/* host to client, if both agreed to ce_maps: the game's map (its family,
	its file's name, size and hash), whenever it changes and with each
	roster */
	_delta_message_map = 17,
	/* chat (docs/delta.md, Chat): host to client, if both agreed to chat or
	server_messages: what the host relays, and whether this player is
	muted */
	_delta_message_chat_config = 18,
	/* client to host, if both agreed to chat: a line its player says */
	_delta_message_chat_say = 19,
	/* host to client: a line to show (a player's needs chat; the server's
	and the game's, server_messages) */
	_delta_message_chat_line = 20,
	/* host to client, if both agreed to chat: a CHAT_SAY not relayed */
	_delta_message_chat_refused = 21,
	/* host to client, if both agreed to bomb and bomb_traits: the game's
	player traits (for a Bomb game type), whenever they change and every few
	seconds. Grifball test builds 2 and 3 sent it as 18, chat's number */
	_delta_message_traits = 22,
	/* team changes (both agreed to team_change): client to host, its
	player asks to change teams */
	_delta_message_team_request = 23,
	/* host to client: what the host did with it */
	_delta_message_team_result = 24,
	/* (25 and 26 are kept for server badges, a draft) */
	/* object sync (both agreed to object_sync): host to client, units'
	shield and health as they changed, and a few of the rest in turn */
	_delta_message_unit_vitals = 27,
	/* host to client: the scenario names of the host's objects, whenever
	they change and every few seconds */
	_delta_message_object_names = 28
};

/* TRAITS's game kinds: what the traits are for */
enum
{
	/* none: no traits (the host's game is no Bomb game type) */
	DELTA_WIRE_TRAITS_NONE = 0,
	DELTA_WIRE_TRAITS_BOMB = 1
};

/* a player trait's shields */
enum
{
	/* the game type's (its variant's shields option) */
	DELTA_WIRE_SHIELDS_UNCHANGED = 0,
	DELTA_WIRE_SHIELDS_NONE = 1,
	DELTA_WIRE_SHIELDS_NORMAL = 2,
	/* charged past full, as the overshield powerup charges them, and
	charged again while not hit */
	DELTA_WIRE_SHIELDS_OVERSHIELD = 3,
	NUMBER_OF_DELTA_WIRE_SHIELDS
};

/* MAP's flags */
enum
{
	/* the size and hash are the map file's (a Halo PC map's); without it
	the map is the Xbox's own, named alone */
	DELTA_WIRE_MAP_HASHED = 0x01
};

struct delta_wire_header
{
	unsigned char major;
	unsigned char type;
	unsigned short length;
	/* the client's, chosen for each handshake (never 0); every message of
	the session carries it */
	delta_u32 session;
};

struct delta_wire_hello
{
	/* capability bits (1 << _delta_capability_...) */
	delta_u32 capabilities;
	/* the OpenCE network version it plays (HALO_PORT_NETWORK_VERSION) */
	unsigned short legacy_version;
	/* its machine in the game the host gave it (DELTA_WIRE_NO_MACHINE: not
	known yet) */
	unsigned char machine_index;
	/* its legacy table's serial (0: the built-in table) */
	delta_u32 legacy_table_serial;
	struct delta_platform_key key;
	char build[DELTA_WIRE_BUILD_SIZE + 1];
};

struct delta_wire_welcome
{
	delta_u32 capabilities;
	/* the bits both have: what this client and the host use */
	delta_u32 agreed;
	unsigned short legacy_version;
	/* the host's own machine (DELTA_WIRE_NO_MACHINE: a dedicated server's
	has no players) */
	unsigned char host_machine_index;
	delta_u32 legacy_table_serial;
	struct delta_platform_key key;
	char build[DELTA_WIRE_BUILD_SIZE + 1];
};

enum
{
	/* the machine speaks Delta Peer (else the rest is zeros) */
	DELTA_ROSTER_DELTA = 0x01,
	/* key holds its platform key (it agreed to platform) */
	DELTA_ROSTER_PLATFORM = 0x02,
	/* player_id and profile_revision hold its profile (it agreed to
	profile) */
	DELTA_ROSTER_PROFILE = 0x04
};

struct delta_wire_roster_entry
{
	unsigned char machine_index;
	unsigned char flags;
	/* what it agreed with the host */
	delta_u32 capabilities;
	struct delta_platform_key key;
	delta_u32 profile_revision;
	unsigned char player_id[DELTA_WIRE_PLAYER_ID_SIZE];
};

struct delta_wire_profile
{
	delta_u32 revision;
	unsigned char player_id[DELTA_WIRE_PLAYER_ID_SIZE];
};

struct delta_wire_table
{
	/* the table's serial */
	delta_u32 serial;
	/* the whole signed table's size (at most DELTA_LEGACY_SIGNED_SIZE) */
	delta_u32 total;
	/* where this piece goes (a multiple of DELTA_WIRE_TABLE_CHUNK) */
	delta_u32 offset;
	/* its bytes: DELTA_WIRE_TABLE_CHUNK, or what is left of the table */
	int length;
	/* (read: into the payload, nothing copied) */
	const unsigned char *data;
};

struct delta_wire_map
{
	/* the map's family (halo_map_families.h: 0 the Xbox's, 1 Custom
	Edition, 2 HaloMD, 3 Halo PC; a number this build does not know is
	carried as it is) */
	unsigned char family;
	/* DELTA_WIRE_MAP_* */
	unsigned char flags;
	/* the file's size in bytes, low and high words */
	delta_u32 size_low;
	delta_u32 size_high;
	unsigned char hash[DELTA_WIRE_MAP_HASH_SIZE];
	/* the file's name, without its family's suffix or ".map" */
	char name[DELTA_WIRE_MAP_NAME_SIZE + 1];
};

/* a set of player traits: percents (100 changes nothing) */
struct delta_wire_player_traits
{
	/* of the unit's vitality (10: a tenth: it dies of a tenth the damage) */
	unsigned short health;
	/* of the damage it deals */
	unsigned short damage;
	/* of the damage it resists (150: it takes two thirds) */
	unsigned short resistance;
	/* of its speed */
	unsigned short speed;
	/* DELTA_WIRE_SHIELDS_* */
	unsigned char shields;
	/* drawn in a bright color of its own (1), else its team's (0) */
	unsigned char highlight;
	/* of its jump's speed, of gravity on it in the air, and of its melee
	blow's reach (0: unchanged; only for players whose machine agreed to
	bomb_movement) */
	unsigned short jump;
	unsigned short gravity;
	unsigned short melee;
};

struct delta_wire_traits
{
	/* DELTA_WIRE_TRAITS_* */
	unsigned char kind;
	/* every player's, and the bomb carrier's on top of them */
	struct delta_wire_player_traits everyone;
	struct delta_wire_player_traits carrier;
};

struct delta_wire_roster
{
	/* the capabilities every machine in the game has now (0 while one has
	no Delta) */
	delta_u32 room_capabilities;
	/* the most players the room's machines take (delta_peer_room_limit) */
	unsigned char room_players;
	unsigned char count;
	struct delta_wire_roster_entry entries[DELTA_WIRE_MAXIMUM_ROSTER_ENTRIES];
};

struct delta_wire_mod_challenge
{
	unsigned char nonce[DELTA_WIRE_MODERATION_NONCE_SIZE];
	int binding_length;
	char binding[DELTA_WIRE_MODERATION_BINDING_SIZE + 1];
};

struct delta_wire_mod_proof
{
	unsigned char key[DELTA_WIRE_MODERATION_KEY_SIZE];
	unsigned char signature[DELTA_WIRE_MODERATION_SIGNATURE_SIZE];
};

struct delta_wire_mod_state
{
	/* enum delta_moderation_role */
	unsigned char role;
	/* DELTA_MODERATION_* */
	delta_u32 permissions;
	/* the longest ban DELTA_MODERATION_BAN_TIMED allows */
	delta_u32 ban_minutes;
};

struct delta_wire_mod_action
{
	/* strictly more than the session's last */
	delta_u32 sequence;
	/* enum delta_moderation_action */
	unsigned char action;
	/* the machine acted on (DELTA_WIRE_NO_MACHINE: none, as for end game) */
	unsigned char target;
	/* a ban's length (0: for ever) */
	unsigned short minutes;
	int reason_length;
	char reason[DELTA_WIRE_MODERATION_REASON_SIZE + 1];
	unsigned char signature[DELTA_WIRE_MODERATION_SIGNATURE_SIZE];
};

struct delta_wire_mod_result
{
	delta_u32 sequence;
	unsigned char ok;
	char text[DELTA_WIRE_MODERATION_TEXT_SIZE + 1];
};

struct delta_wire_mod_notice
{
	unsigned char kind;
	char text[DELTA_WIRE_MODERATION_TEXT_SIZE + 1];
};

struct delta_wire_mod_bind
{
	delta_u32 request;
	char account[DELTA_WIRE_MODERATION_NAME_SIZE + 1];
	char server[DELTA_WIRE_MODERATION_NAME_SIZE + 1];
};

struct delta_wire_mod_bind_answer
{
	delta_u32 request;
	unsigned char accepted;
	unsigned char key[DELTA_WIRE_MODERATION_KEY_SIZE];
	unsigned char signature[DELTA_WIRE_MODERATION_SIGNATURE_SIZE];
};

struct delta_wire_chat_config
{
	/* enum delta_chat_mode */
	unsigned char mode;
	/* DELTA_CHAT_CONFIG_* */
	unsigned char flags;
	/* a timed mute's seconds left (0: none, or until the game ends) */
	delta_u32 mute_seconds;
};

struct delta_wire_chat_say
{
	/* strictly more than the session's last */
	delta_u32 sequence;
	/* enum delta_chat_scope */
	unsigned char scope;
	/* its bytes as sent (UTF-8: delta_wire_chat_text_valid checks them) */
	int length;
	char text[DELTA_WIRE_CHAT_SAY_TEXT_SIZE + 1];
};

struct delta_wire_chat_line
{
	/* the host's count of lines */
	delta_u32 line;
	/* enum delta_chat_kind */
	unsigned char kind;
	/* enum delta_chat_scope */
	unsigned char scope;
	/* who said it (DELTA_WIRE_NO_MACHINE, and player 255: nobody, as the
	server's own) */
	unsigned char speaker_machine;
	unsigned char speaker_player;
	int name_length;
	char name[DELTA_WIRE_CHAT_NAME_SIZE + 1];
	int length;
	char text[DELTA_WIRE_CHAT_TEXT_SIZE + 1];
};

struct delta_wire_chat_refused
{
	/* the CHAT_SAY's */
	delta_u32 sequence;
	/* enum delta_chat_refusal */
	unsigned char reason;
	/* (too fast) the seconds the player's chat is paused; 0: none, or a host
	that does not say (one older than 0.8.0 reads it as reserved) */
	unsigned short paused_seconds;
};

struct delta_wire_team_request
{
	/* strictly more than the session's last */
	delta_u32 sequence;
	/* the player: its index in the game's player list (the host's) */
	unsigned char player;
	/* 0 red, 1 blue, DELTA_WIRE_TEAM_OTHER the other one */
	unsigned char team;
};

struct delta_wire_team_result
{
	/* the TEAM_REQUEST's */
	delta_u32 sequence;
	/* enum delta_team_result */
	unsigned char result;
	/* the player's team now (changed: the new one) */
	unsigned char team;
	/* (too soon) the seconds until the player may change again */
	unsigned short seconds;
};

/* a unit's shield and health (UNIT_VITALS) */
struct delta_wire_unit_vital
{
	/* the host's object (its datum index: a client's copy has the same) */
	delta_u32 object;
	/* DELTA_WIRE_VITALS_* */
	unsigned char flags;
	/* body and shield vitality, in 16384ths (the game's distributed unit
	states' scale: up to 4) */
	unsigned short body;
	unsigned short shield;
};

struct delta_wire_unit_vitals
{
	/* DELTA_WIRE_UNIT_VITALS_* (others a later build's: ignored) */
	unsigned char flags;
	int count;
	struct delta_wire_unit_vital entries[DELTA_WIRE_MAXIMUM_UNIT_VITALS];
};

/* OBJECT_NAMES's entry flags */
enum
{
	/* the host had an object of this name and has none now (its object is
	NONE): a client's own copy goes, after a tick with the vitals given */
	DELTA_WIRE_NAME_REMOVED = 0x01,
	DELTA_WIRE_NAME_FLAGS = 0x01
};

/* an object's scenario name (OBJECT_NAMES), or a name the host removed */
struct delta_wire_object_name
{
	/* (0xFFFFFFFF for a removed name) */
	delta_u32 object;
	/* the scenario's object name index */
	unsigned short name;
	/* DELTA_WIRE_NAME_* (others a later build's: ignored) */
	unsigned char flags;
	/* a removed name's unit as the host last had it (DELTA_WIRE_VITALS_*,
	and vitality in 16384ths; a full shield and body for one that was not a
	unit); 0 for the others */
	unsigned char vitals_flags;
	unsigned short body;
	unsigned short shield;
};

/* the host's named objects, total in all; this datagram's count of them
from first on */
struct delta_wire_object_names
{
	int total;
	int first;
	int count;
	struct delta_wire_object_name entries[DELTA_WIRE_MAXIMUM_OBJECT_NAMES_ENTRIES];
};

/* a datagram's header: 1 if it is Delta's (the magic) and its length is the
datagram's; the payload follows at data + DELTA_WIRE_HEADER_SIZE. A header of
another major is still read (to answer it with LEGACY) */
int delta_wire_read_header(const unsigned char *data, int size, struct delta_wire_header *header);
/* a header for a payload of length bytes into data (DELTA_WIRE_HEADER_SIZE) */
void delta_wire_write_header(unsigned char *data, int major, int type, int length, delta_u32 session);

/* a message's payload (the header's length): 1 if it holds the message.
Text that is not printable ASCII is replaced by '?', and a roster's count
is the entries it holds, at most DELTA_WIRE_MAXIMUM_ROSTER_ENTRIES */
int delta_wire_read_hello(const unsigned char *payload, int size, struct delta_wire_hello *hello);
int delta_wire_read_welcome(const unsigned char *payload, int size, struct delta_wire_welcome *welcome);
int delta_wire_read_roster(const unsigned char *payload, int size, struct delta_wire_roster *roster);
int delta_wire_read_profile(const unsigned char *payload, int size, struct delta_wire_profile *profile);
/* (a piece that is not where the serial's table has one: a size over the
cap, an offset off the grid or past the end, a length not the piece's) */
int delta_wire_read_table(const unsigned char *payload, int size, struct delta_wire_table *table);
int delta_wire_read_table_have(const unsigned char *payload, int size, delta_u32 *serial);
/* (a name that is empty or not a plain file name, longer than its field, or
of a Halo PC family without its hash is refused, never mended) */
int delta_wire_read_map(const unsigned char *payload, int size, struct delta_wire_map *map);

/* a whole datagram of the message into data (DELTA_WIRE_MAXIMUM_DATAGRAM
bytes); returns its size, 0 if it does not fit */
int delta_wire_write_hello(unsigned char *data, delta_u32 session, const struct delta_wire_hello *hello);
int delta_wire_write_welcome(unsigned char *data, delta_u32 session, const struct delta_wire_welcome *welcome);
int delta_wire_write_roster(unsigned char *data, delta_u32 session, const struct delta_wire_roster *roster);
int delta_wire_write_profile(unsigned char *data, delta_u32 session, const struct delta_wire_profile *profile);
int delta_wire_write_table(unsigned char *data, delta_u32 session, const struct delta_wire_table *table);
int delta_wire_write_table_have(unsigned char *data, delta_u32 session, delta_u32 serial);
/* (0 for a map delta_wire_read_map would refuse) */
int delta_wire_write_map(unsigned char *data, delta_u32 session, const struct delta_wire_map *map);
/* (a kind, shields or highlight this build does not know, or a percent
past its range, is refused) */
int delta_wire_read_traits(const unsigned char *payload, int size, struct delta_wire_traits *traits);
/* (0 for traits delta_wire_read_traits would refuse) */
int delta_wire_write_traits(unsigned char *data, delta_u32 session, const struct delta_wire_traits *traits);
/* whether traits are ones TRAITS carries (in their ranges, known values) */
int delta_wire_traits_valid(const struct delta_wire_traits *traits);
/* whether a map's name is one MAP carries: 1 to DELTA_WIRE_MAP_NAME_SIZE
characters, each a letter, digit, '_', '-', '.' or ' ', and not "." or
".." or holding ".." */
int delta_wire_map_name_valid(const char *name);
/* LEGACY and BYE: a header alone */
int delta_wire_write_empty(unsigned char *data, int major, int type, delta_u32 session);

/* moderation's messages (both sides must have agreed to moderation). An
action's reason must be printable ASCII (it is signed as sent: one that is
not is refused whole); other text that is not is read as '?'. A binding is
printable ASCII or refused */
int delta_wire_read_mod_challenge(const unsigned char *payload, int size, struct delta_wire_mod_challenge *challenge);
int delta_wire_read_mod_proof(const unsigned char *payload, int size, struct delta_wire_mod_proof *proof);
int delta_wire_read_mod_state(const unsigned char *payload, int size, struct delta_wire_mod_state *state);
int delta_wire_read_mod_action(const unsigned char *payload, int size, struct delta_wire_mod_action *action);
int delta_wire_read_mod_result(const unsigned char *payload, int size, struct delta_wire_mod_result *result);
int delta_wire_read_mod_notice(const unsigned char *payload, int size, struct delta_wire_mod_notice *notice);
int delta_wire_read_mod_bind(const unsigned char *payload, int size, struct delta_wire_mod_bind *bind);
int delta_wire_read_mod_bind_answer(const unsigned char *payload, int size, struct delta_wire_mod_bind_answer *answer);

/* (0 if a field does not fit or is not what the reader takes: an action's
reason not printable, a binding not printable) */
int delta_wire_write_mod_challenge(unsigned char *data, delta_u32 session,
	const struct delta_wire_mod_challenge *challenge);
int delta_wire_write_mod_proof(unsigned char *data, delta_u32 session, const struct delta_wire_mod_proof *proof);
int delta_wire_write_mod_state(unsigned char *data, delta_u32 session, const struct delta_wire_mod_state *state);
int delta_wire_write_mod_action(unsigned char *data, delta_u32 session, const struct delta_wire_mod_action *action);
int delta_wire_write_mod_result(unsigned char *data, delta_u32 session, const struct delta_wire_mod_result *result);
int delta_wire_write_mod_notice(unsigned char *data, delta_u32 session, const struct delta_wire_mod_notice *notice);
int delta_wire_write_mod_bind(unsigned char *data, delta_u32 session, const struct delta_wire_mod_bind *bind);
int delta_wire_write_mod_bind_answer(unsigned char *data, delta_u32 session,
	const struct delta_wire_mod_bind_answer *answer);

/* chat's messages. A text's bytes are read as they came (their length
checked, nothing else): delta_wire_chat_text_valid says whether they may be
shown. A name or text that does not fit its field, an unknown kind or
scope, or an empty line is refused */
int delta_wire_read_chat_config(const unsigned char *payload, int size, struct delta_wire_chat_config *config);
int delta_wire_read_chat_say(const unsigned char *payload, int size, struct delta_wire_chat_say *say);
int delta_wire_read_chat_line(const unsigned char *payload, int size, struct delta_wire_chat_line *line);
int delta_wire_read_chat_refused(const unsigned char *payload, int size, struct delta_wire_chat_refused *refused);
/* (0 for what the reader would refuse) */
int delta_wire_write_chat_config(unsigned char *data, delta_u32 session, const struct delta_wire_chat_config *config);
int delta_wire_write_chat_say(unsigned char *data, delta_u32 session, const struct delta_wire_chat_say *say);
int delta_wire_write_chat_line(unsigned char *data, delta_u32 session, const struct delta_wire_chat_line *line);
int delta_wire_write_chat_refused(unsigned char *data, delta_u32 session,
	const struct delta_wire_chat_refused *refused);
/* team changes' messages. A team other than 0, 1 and DELTA_WIRE_TEAM_OTHER,
or a result this build does not know, is refused */
int delta_wire_read_team_request(const unsigned char *payload, int size, struct delta_wire_team_request *request);
int delta_wire_read_team_result(const unsigned char *payload, int size, struct delta_wire_team_result *result);
/* (0 for what the reader would refuse) */
int delta_wire_write_team_request(unsigned char *data, delta_u32 session,
	const struct delta_wire_team_request *request);
int delta_wire_write_team_result(unsigned char *data, delta_u32 session, const struct delta_wire_team_result *result);
/* object sync's messages. A count past what the datagram holds, unknown
flags, or names past DELTA_WIRE_MAXIMUM_OBJECT_NAMES are refused */
int delta_wire_read_unit_vitals(const unsigned char *payload, int size, struct delta_wire_unit_vitals *vitals);
int delta_wire_read_object_names(const unsigned char *payload, int size, struct delta_wire_object_names *names);
/* (0 for what the reader would refuse) */
int delta_wire_write_unit_vitals(unsigned char *data, delta_u32 session, const struct delta_wire_unit_vitals *vitals);
int delta_wire_write_object_names(unsigned char *data, delta_u32 session, const struct delta_wire_object_names *names);

/* whether length bytes of text are a line to show: valid UTF-8 (shortest
forms, no surrogates, nothing past U+10FFFF), no control characters (C0,
DEL, C1), no bidirectional overrides or isolates (U+202A to U+202E, U+2066
to U+2069), no zero-width characters (U+200B to U+200D, U+2060, U+FEFF),
not blank, and at most maximum_characters characters (0: any). Its
characters, else -1 */
int delta_wire_chat_text_valid(const char *text, int length, int maximum_characters);

/* the bytes a moderator key signs, into message (at least
DELTA_WIRE_MODERATION_MESSAGE_SIZE): a proof, an action (its fields but the
signature), a bind answer; each starts with a label of its own, then the
session's nonce. Their length */
int delta_wire_moderation_proof_message(unsigned char *message, const unsigned char *nonce, const char *binding,
	int binding_length);
int delta_wire_moderation_action_message(unsigned char *message, const unsigned char *nonce,
	const struct delta_wire_mod_action *action);
int delta_wire_moderation_bind_message(unsigned char *message, const unsigned char *nonce, delta_u32 request,
	int accepted, const char *binding, int binding_length);

/* a platform key's 8 bytes, each way (a key of a newer version is read for
the fields this one knows) */
void delta_wire_read_key(const unsigned char *data, struct delta_platform_key *key);
void delta_wire_write_key(unsigned char *data, const struct delta_platform_key *key);

/* the platform policy's defaults for a platform (DELTA_PLATFORM_POLICY in
delta.h) as a key: platform, version, limits and memory class; an unknown
platform's row for one this build does not know */
void delta_platform_policy_default(int platform, struct delta_platform_key *key);
/* the platform policy's co-op column: whether a machine of the platform
plays network co-op (an unknown platform's row for one this build does not
know) */
int delta_platform_policy_coop(int platform);

/* A token bucket: up to burst messages at once, refilled rate a second.
1 if a message may be taken now (and takes it). Times in milliseconds,
wrapping */
struct delta_rate
{
	delta_u32 tokens_milli;
	delta_u32 time;
	int started;
};
int delta_rate_take(struct delta_rate *rate, delta_u32 now, int rate_per_second, int burst);

#endif
