/*
EVENT_LOG.H

Delta Stats' event log (docs/delta.md, "Delta Stats"): what happened in a
game this machine hosts, gathered on the game's thread by
port/linux/game/game_events.c, kept within fixed limits here
(event_log.c), and made into the game's batch (schema 1, JSON) at its end,
which event_upload.c compresses (event_gzip.c) and sends to the game list.

Plain types only across this boundary (int, short, float, char; no long,
which the 64-bit builds' copy of the game's sources spells int, and no
64-bit member, which -malign-double places differently): the game's side is
compiled with the Xbox's ABI, this side with the platform layer's. Names
cross as UTF-8.

One game is recorded at a time, on one thread (the game's): begin, then
players and events, then finish, which hands the encoded batch over. No
address of anyone is ever in it: players are their names and a keyed hash
of their machine's hardware ID.
*/

#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include <stddef.h>

/* the batch's "schema" (docs/delta.md: later versions only add fields) */
#define EVENT_LOG_SCHEMA 1

enum
{
	/* a name, UTF-8, with its end (the game's 11 characters, 3 bytes each) */
	EVENT_LOG_NAME_SIZE = 40,
	/* a tag's path, or a short text (a reason, a medal's key) */
	EVENT_LOG_TAG_SIZE = 96,
	/* a hardware ID as a machine tells it (32 hexadecimal digits) */
	EVENT_LOG_HARDWARE_ID_SIZE = 33,
	/* the most of each a game keeps */
	EVENT_LOG_MAXIMUM_PLAYERS = 64,
	EVENT_LOG_MAXIMUM_TAGS = 384,
	/* a player's weapons, vehicles and kinds of pickup kept: more add up as
	"other" */
	EVENT_LOG_MAXIMUM_WEAPONS = 24,
	EVENT_LOG_MAXIMUM_VEHICLES = 8,
	EVENT_LOG_MAXIMUM_PICKUPS = 16,
	/* events a game keeps (network.events_limit) */
	EVENT_LOG_DEFAULT_EVENTS = 40000,
	EVENT_LOG_MINIMUM_EVENTS = 64,
	EVENT_LOG_MAXIMUM_EVENTS = 200000,
	/* the game's ticks a second */
	EVENT_LOG_TICKS_PER_SECOND = 30,
	/* samples (positions, pings) are kept on whole seconds; when the log
	is full, every other second's are dropped, then every other of those,
	and so on (event_log.c) */
	EVENT_LOG_SAMPLE_TICKS = 30,
	EVENT_LOG_NONE = -1,
};

/* what an event is */
enum
{
	/* player: the killer (none: the world's), other: the victim; tag[0]
	the damage's tag, tag[1] the vehicle the killer rode; value[0] the
	damage's kind (EVENT_LOG_DAMAGE_*); bits EVENT_LOG_KILL_*; position the
	killer's, other_position the victim's */
	EVENT_LOG_KILL,
	/* player; tag[0] the medal's key (double_kill, killing_spree, ...) */
	EVENT_LOG_MEDAL,
	/* player; value[0] EVENT_LOG_OBJECTIVE_*, value[1] the team; position */
	EVENT_LOG_OBJECTIVE,
	/* player; tag[0] the item (a weapon's tag, or powerups\...); position */
	EVENT_LOG_PICKUP,
	/* a ride, at its end: player; tag[0] the vehicle; value[0] the seat
	(EVENT_LOG_SEAT_*), value[1] the tick it began */
	EVENT_LOG_RIDE,
	/* player; position */
	EVENT_LOG_SPAWN,
	/* (samples) player; position */
	EVENT_LOG_POSITION,
	/* (samples) player; value[0] milliseconds */
	EVENT_LOG_PING,
	/* the server's minute: value[0] the players, value[1] the frame's
	milliseconds (x100, average), value[2] the longest (x100), value[3]
	the process's resident memory (KB, -1 unknown); bits its CPU
	(thousandths of a core, -1 unknown, as int) */
	EVENT_LOG_HEALTH,
	/* player (none: someone not in the game); value[0]
	EVENT_LOG_MODERATION_*; tag[0] the reason, tag[1] by whom, tag[2] the
	name of someone not in the game */
	EVENT_LOG_MODERATION,
	/* an anti-cheat's flag (never an action): player; tag[0] its kind,
	tag[1] its detail; value[0] its severity (0-3) */
	EVENT_LOG_FLAG,
	EVENT_LOG_NUMBER_OF_TYPES
};

/* a kill's bits */
enum
{
	EVENT_LOG_KILL_HEADSHOT = 1 << 0,
	EVENT_LOG_KILL_BETRAYAL = 1 << 1,
	EVENT_LOG_KILL_SUICIDE = 1 << 2,
	/* the killer's and the victim's positions are known */
	EVENT_LOG_KILL_KILLER_POSITION = 1 << 3,
	EVENT_LOG_KILL_VICTIM_POSITION = 1 << 4,
	/* a stuck grenade */
	EVENT_LOG_KILL_STICK = 1 << 5,
	/* the killer was dead when it landed (a grenade, a rocket in flight) */
	EVENT_LOG_KILL_FROM_GRAVE = 1 << 7,
	/* the victim rode a vehicle */
	EVENT_LOG_KILL_VICTIM_RIDING = 1 << 6,
};

/* a kill's damage (the batch's "damage") */
enum
{
	EVENT_LOG_DAMAGE_OTHER,
	EVENT_LOG_DAMAGE_BULLET,
	EVENT_LOG_DAMAGE_PLASMA,
	EVENT_LOG_DAMAGE_MELEE,
	EVENT_LOG_DAMAGE_GRENADE,
	EVENT_LOG_DAMAGE_EXPLOSION,
	EVENT_LOG_DAMAGE_VEHICLE,
	EVENT_LOG_DAMAGE_FALL,
	EVENT_LOG_NUMBER_OF_DAMAGES
};

/* an objective's kind */
enum
{
	EVENT_LOG_OBJECTIVE_FLAG_GRAB,
	EVENT_LOG_OBJECTIVE_FLAG_RETURN,
	EVENT_LOG_OBJECTIVE_FLAG_SCORE,
	EVENT_LOG_OBJECTIVE_BALL_GRAB,
	EVENT_LOG_OBJECTIVE_BALL_DROP,
	EVENT_LOG_OBJECTIVE_HILL_ENTER,
	EVENT_LOG_OBJECTIVE_HILL_EXIT,
	EVENT_LOG_OBJECTIVE_RACE_LAP,
	EVENT_LOG_NUMBER_OF_OBJECTIVES
};

/* a seat */
enum
{
	EVENT_LOG_SEAT_DRIVER,
	EVENT_LOG_SEAT_GUNNER,
	EVENT_LOG_SEAT_PASSENGER,
	EVENT_LOG_NUMBER_OF_SEATS
};

/* how a player's time in the game ended */
enum
{
	EVENT_LOG_LEFT_END,
	EVENT_LOG_LEFT_QUIT,
	EVENT_LOG_LEFT_KICK,
	EVENT_LOG_LEFT_BAN,
	EVENT_LOG_LEFT_TIMEOUT,
	EVENT_LOG_NUMBER_OF_LEFTS
};

/* a moderator's action */
enum
{
	EVENT_LOG_MODERATION_KICK,
	EVENT_LOG_MODERATION_BAN,
	EVENT_LOG_MODERATION_UNBAN,
	EVENT_LOG_MODERATION_MUTE,
	EVENT_LOG_MODERATION_UNMUTE,
	EVENT_LOG_MODERATION_WARN,
	EVENT_LOG_MODERATION_OTHER,
	EVENT_LOG_NUMBER_OF_MODERATIONS
};

/* how a game ended */
enum
{
	EVENT_LOG_END_SCORE,
	EVENT_LOG_END_TIME,
	EVENT_LOG_END_ADMIN,
	EVENT_LOG_END_EMPTY,
	EVENT_LOG_END_ERROR,
	EVENT_LOG_END_OTHER,
	EVENT_LOG_NUMBER_OF_ENDS
};

/* a player's client (the batch's "client") */
enum
{
	EVENT_LOG_CLIENT_OTHER,
	EVENT_LOG_CLIENT_CHUPATHINGYCE,
	EVENT_LOG_NUMBER_OF_CLIENTS
};

/* one event. Which members mean what depends on its type (above): player
and other are slots of event_log_player; tag[] are event_log_tag's indices
(EVENT_LOG_NONE for none). */
struct event_log_record
{
	int tick;
	short type;
	short player;
	short other;
	short tag[3];
	int value[4];
	unsigned int bits;
	float position[3];
	float other_position[3];
};

/* a player as the host knows them, as they join */
struct event_log_player_identity
{
	char name[EVENT_LOG_NAME_SIZE];
	/* as the machine told it joining, "" if it told none: the batch has a
	keyed hash of it, never the ID itself */
	char hardware_id[EVENT_LOG_HARDWARE_ID_SIZE];
	/* EVENT_LOG_CLIENT_* */
	int client;
	/* "windows", "linux", "macos", "android", "xbox", "other", or "" */
	char platform[16];
	int team;
	int bot;
	/* the armor color (0 to 17, the game's), -1 unknown */
	int color;
};

/* a player's totals at the end of the game (or as they left), as the game
counts them */
struct event_log_player_totals
{
	int score;
	int place;
	int kills;
	int deaths;
	int assists;
	int betrayals;
	int suicides;
	int team;
	int flag_grabs;
	int flag_returns;
	int flag_scores;
	/* seconds */
	int ball_time;
	int ball_kills;
	int hill_time;
	int laps;
};

/* the game, as it starts */
struct event_log_game
{
	/* the map (its path, or name@ce / name@md) and game type, as the
	playlist or the menus named them */
	char map[EVENT_LOG_TAG_SIZE];
	char gametype[EVENT_LOG_NAME_SIZE];
	int engine;
	int teams;
	int score_limit;
	char server_name[EVENT_LOG_NAME_SIZE];
	char build[EVENT_LOG_NAME_SIZE];
	char platform[24];
	/* a dedicated server's playlist (its file's name: "slayer"), "" none */
	char playlist[EVENT_LOG_NAME_SIZE];
	/* the game's unix time at its start (unsigned: no 64-bit member) */
	unsigned int start_time;
};

/* the game, as it ends (or as it stands, for a part sent during it) */
struct event_log_end
{
	int tick;
	int reason;
	int team_scores[2];
	unsigned int end_time;
};

/* ---------- event_log.c: the game's log */

/* the most events a game keeps (from the next game) */
void event_log_set_capacity(int capacity);
int event_log_capacity(void);

/* a new game (what was kept of the last is forgotten); its ID, 32
hexadecimal digits, from random bytes (16 of them, given) */
void event_log_begin(struct event_log_game const *game, unsigned char const random_id[16]);
int event_log_recording(void);
/* the game's ID (32 hexadecimal digits), "" if none */
char const *event_log_game_id(void);

/* a tag's path (or a short text) kept once, its index: EVENT_LOG_NONE for
none, or once the game's table is full */
int event_log_tag(char const *text);

/* a player who joined, their slot; EVENT_LOG_NONE once the game's slots
are full */
int event_log_player(int tick, struct event_log_player_identity const *identity);
void event_log_player_totals(int slot, struct event_log_player_totals const *totals);
/* the player left (EVENT_LOG_LEFT_*) at the tick; joined again: a new
session from the tick */
void event_log_player_left(int slot, int tick, int how);
void event_log_player_rejoined(int slot, int tick);

/* shots a player fired with a weapon (its tag folder: weapons\assault
rifle; a grenade's: weapons\frag grenade), and those that hit a player
(hits count at most the shots, at the end) */
void event_log_shots(int slot, int weapon_tag, int shots);
void event_log_hit(int slot, int weapon_tag, float damage);
/* a grenade thrown (0 frag, 1 plasma) */
void event_log_grenade(int slot, int plasma);
/* damage one player dealt another (in the game's units: a body or a full
shield is 1), added to both totals; either may be EVENT_LOG_NONE */
void event_log_damage(int dealer, int taker, float amount);

/* an event (copied); FALSE if it was dropped (the log's limits). A kill's
weapon (for the player's weapons) is its damage tag's folder, which
event_log_add works out. */
int event_log_add(struct event_log_record const *record);

/* the game's batch as JSON (malloc'd, NUL-terminated, its length in
*length), NULL if there is nothing to send (no game, or no player) or no
memory. final: the game's end (the log is empty after); else a part of
the game so far (sent during a long game: the log goes on). invite: the
game's listed invite, "" if none. */
char *event_log_finish(struct event_log_end const *end, char const *invite, int final, size_t *length);
/* a batch event_log_finish gave, NULL or not, freed as it was allocated
(the C library's free: the game's units free through their own allocator,
debug_free, which takes neither another allocator's memory nor NULL) */
void event_log_free(char *json);

/* a moderator's action (EVENT_LOG_MODERATION_*) in the game under way, if
one is recorded (the game's thread only: the server's commands run there):
who, the player's name; by whom ("console", "admin:<name>", ...); why.
Delta Control's audit records the same action on its side. */
void event_log_moderation(int kind, char const *who, char const *by, char const *reason);

/* (tests) the events kept now, and those dropped */
int event_log_count(void);
int event_log_dropped(void);

/* ---------- event_gzip.c */

/* data compressed as a gzip member (RFC 1952; deflate's fixed codes, RFC
1951): malloc'd, its length in *compressed_length; NULL if no memory */
unsigned char *event_gzip(unsigned char const *data, size_t length, size_t *compressed_length);

/* ---------- event_upload.c: the platform's side */

/* whether this machine records the games it hosts (network.report_events)
and, if so, the most events one keeps and the seconds between position
samples */
int event_upload_enabled(void);
int event_upload_event_limit(void);
int event_upload_position_seconds(void);
/* a batch (event_log_finish's), taken over: compressed, written to
network.events_folder if set, and sent to the game list on a thread of its
own, again later if it could not be (the latest part of a game replaces
the one before it, waiting) */
void event_upload_submit(char *json, size_t length, char const *game_id);
/* a moderator's action from another thread (Delta Control's recorder),
kept until the game's thread takes it (event_upload_moderation_drain:
event_log_moderation for each); thread-safe */
void event_upload_moderation(int kind, char const *who, char const *by, char const *reason);
void event_upload_moderation_drain(void);
/* random bytes (the game's ID) */
void event_upload_random(unsigned char *bytes, int count);
/* the server's process: CPU used since the last call (thousandths of a
core) and resident memory (KB); -1 for what is not known */
void event_upload_system_sample(int *cpu_permille, int *resident_kb);
/* the unix time now */
unsigned int event_upload_time(void);
/* the hosted game's listed invite (its digits), "" if none */
void event_upload_invite(char *text, int size);

#endif
