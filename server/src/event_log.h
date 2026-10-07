/*
EVENT_LOG.H

The dedicated server's event log (server/docs/events.md): what happened in
each game, as the host decided it, gathered on the game's thread by
server/src/server_events.c, then kept, encoded, compressed and sent by the
host's side (server/platform/event_log.c, event_gzip.c, event_upload.c).

Plain types only across this boundary (int, short, float, char; no long,
which the 64-bit builds' copy of the game's sources spells int, and no
64-bit member, which -malign-double places differently): the
game's side is compiled with the Xbox's ABI (16-bit wchar_t, MSVC's 32-bit
long, rewritten as int for the 64-bit builds), the host's with its own.
Names cross as UTF-8.

One game is recorded at a time, on one thread (the game's): begin, then
players and events, then finish, which hands the encoded batch over.
*/

#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include <stddef.h>

/* the batch's "schema" and "version" (server/docs/events.md) */
#define EVENT_LOG_SCHEMA "chupathingyce-server-events"
#define EVENT_LOG_VERSION 1

enum
{
	/* a name, UTF-8, with its end (the game's 11 characters, 3 bytes each) */
	EVENT_LOG_NAME_SIZE = 40,
	/* a tag's path, or a short text (a reason) */
	EVENT_LOG_TAG_SIZE = 128,
	/* a hardware id, as a machine tells it (32 hexadecimal digits) */
	EVENT_LOG_HARDWARE_ID_SIZE = 33,
	/* the most of each a game keeps */
	EVENT_LOG_MAXIMUM_PLAYERS = 192,
	EVENT_LOG_MAXIMUM_TAGS = 512,
	/* a player's weapons and vehicles (tag folders) kept: more add up as
	"other" */
	EVENT_LOG_MAXIMUM_WEAPONS = 16,
	EVENT_LOG_MAXIMUM_VEHICLES = 8,
	/* events a game keeps (HALO_DEDICATED_EVENTS_LIMIT) */
	EVENT_LOG_DEFAULT_EVENTS = 20000,
	EVENT_LOG_MINIMUM_EVENTS = 64,
	EVENT_LOG_MAXIMUM_EVENTS = 200000,
	/* the game's ticks, a second */
	EVENT_LOG_TICKS_PER_SECOND = 30,
	/* samples (positions, network) are taken on whole seconds; when the
	log is full, every other second's are dropped, then every other of
	those, ... (server/docs/events.md, Limits) */
	EVENT_LOG_SAMPLE_TICKS = 30,
	EVENT_LOG_NONE = -1,
};

/* what an event is: its "e" in the batch (event_log.c's names) */
enum
{
	EVENT_LOG_KILL,
	EVENT_LOG_MULTIKILL,
	EVENT_LOG_SPREE,
	EVENT_LOG_SPREE_END,
	EVENT_LOG_GRENADE,
	EVENT_LOG_PICKUP,
	EVENT_LOG_VEHICLE_ENTER,
	EVENT_LOG_VEHICLE_EXIT,
	EVENT_LOG_FLAG_GRAB,
	EVENT_LOG_FLAG_CAPTURE,
	EVENT_LOG_FLAG_RETURN,
	EVENT_LOG_FLAG_DROP,
	EVENT_LOG_BALL_GRAB,
	EVENT_LOG_BALL_DROP,
	EVENT_LOG_LAP,
	EVENT_LOG_JOIN,
	EVENT_LOG_LEAVE,
	EVENT_LOG_TEAM,
	EVENT_LOG_NET,
	EVENT_LOG_POSITION,
	EVENT_LOG_HEALTH,
	EVENT_LOG_KICK,
	EVENT_LOG_BAN,
	EVENT_LOG_AUTO_KICK,
	EVENT_LOG_SUSPECT,
	EVENT_LOG_NUMBER_OF_TYPES
};

/* a kill's bits */
enum
{
	EVENT_LOG_KILL_HEADSHOT = 1 << 0,
	EVENT_LOG_KILL_MELEE = 1 << 1,
	EVENT_LOG_KILL_GRENADE = 1 << 2,
	EVENT_LOG_KILL_SPLATTER = 1 << 3,
	EVENT_LOG_KILL_FALL = 1 << 4,
	EVENT_LOG_KILL_SUICIDE = 1 << 5,
	EVENT_LOG_KILL_BETRAYAL = 1 << 6,
	EVENT_LOG_KILL_EXPLOSION = 1 << 7,
	/* no player's kill (the world's, or a killer who has left) */
	EVENT_LOG_KILL_WORLD = 1 << 8,
	/* the killer's and the victim's positions are known */
	EVENT_LOG_KILL_KILLER_POSITION = 1 << 9,
	EVENT_LOG_KILL_VICTIM_POSITION = 1 << 10,
};

/* a pickup's kind (value[0]) */
enum
{
	EVENT_LOG_PICKUP_WEAPON,
	EVENT_LOG_PICKUP_CAMOUFLAGE,
	EVENT_LOG_PICKUP_OVERSHIELD,
	EVENT_LOG_PICKUP_FULL_SPECTRUM_VISION,
	EVENT_LOG_NUMBER_OF_PICKUPS
};

/* a suspect flag's check (value[0]): flags, never actions */
enum
{
	EVENT_LOG_SUSPECT_ACCURACY,
	EVENT_LOG_SUSPECT_SPEED,
	EVENT_LOG_SUSPECT_DAMAGE,
	EVENT_LOG_SUSPECT_SPEED_HACK,
	EVENT_LOG_NUMBER_OF_SUSPECTS
};

/* how a game ended */
enum
{
	EVENT_LOG_END_UNKNOWN,
	/* its score or time limit, as the game ends it */
	EVENT_LOG_END_LIMIT,
	/* nobody scored for the idle limit (HALO_DEDICATED_IDLE_LIMIT) */
	EVENT_LOG_END_IDLE,
	/* everyone left */
	EVENT_LOG_END_EMPTY,
	/* a command (sv_end_game, sv_map, sv_mapcycle_next) */
	EVENT_LOG_END_COMMAND,
	/* the game stopped without its end (the server's game torn down) */
	EVENT_LOG_END_ABORTED,
	EVENT_LOG_NUMBER_OF_ENDS
};

/* one event. Which members mean what depends on its type
(server/docs/events.md has the batch's form of each): player and other
are slots of event_log_player; tag[] are event_log_tag's indices
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

/* a player as the host knows them, as they join the game */
struct event_log_player_identity
{
	char name[EVENT_LOG_NAME_SIZE];
	/* as the machine told it joining, "" if it told none: the batch has a
	hash of it, never the id itself */
	char hardware_id[EVENT_LOG_HARDWARE_ID_SIZE];
	/* network_player.icon_index: a ChupathingyCE client puts its platform
	there (0x4300 | platform) */
	int icon_index;
	int machine_index;
	int team;
	int color;
};

/* a player's totals at the end of the game (or when they left), as the
game counts them */
struct event_log_player_totals
{
	int score;
	int kills;
	int deaths;
	int assists;
	int betrayals;
	int suicides;
	int team;
	int flag_grabs;
	int flag_returns;
	int flag_scores;
	int ball_ticks;
	int ball_carrier_kills;
	int hill_ticks;
	int laps;
	int best_lap_ticks;
	int best_spree;
};

/* the game, as it starts */
struct event_log_game
{
	/* the map (its path, or name@ce / name@md) and game type, as the
	playlist or a command named them */
	char map[EVENT_LOG_TAG_SIZE];
	char variant[EVENT_LOG_NAME_SIZE];
	int engine;
	int teams;
	int score_limit;
	char server_name[EVENT_LOG_NAME_SIZE];
	char build[EVENT_LOG_NAME_SIZE];
	char architecture[16];
	int network_version;
	int maximum_players;
	/* the game's unix time at its start (unsigned: no 64-bit member, whose
	alignment the two ABIs disagree on) */
	unsigned int start_time;
};

/* the game, as it ends */
struct event_log_end
{
	int tick;
	int reason;
	int red_score;
	int blue_score;
	unsigned int end_time;
};

/* ---------- event_log.c: the game's log */

/* the most events a game keeps (from the next game) */
void event_log_set_capacity(int capacity);
int event_log_capacity(void);

/* a new game (what was kept of the last is forgotten); its id, 32
hexadecimal digits, made from random bytes (16 of them, given) */
void event_log_begin(struct event_log_game const *game, unsigned char const random_id[16]);
int event_log_recording(void);

/* a tag's path (or a short text) kept once, its index: EVENT_LOG_NONE for
none, or once the game's table is full */
int event_log_tag(char const *text);

/* a player who joined, their slot; EVENT_LOG_NONE once the game's slots
are full */
int event_log_player(int tick, struct event_log_player_identity const *identity);
void event_log_player_totals(int slot, struct event_log_player_totals const *totals);
/* the player left (or quit, kept on the scoreboard) at the tick */
void event_log_player_left(int slot, int tick, int quit);

/* what a player did with a weapon (a tag folder: weapons\assault rifle),
added to their totals */
void event_log_weapon(int slot, int tag, int shots, int hits, int kills, float damage);
/* damage one player dealt another (in the game's units: a body or a full
shield is 1), added to both totals; either may be EVENT_LOG_NONE */
void event_log_damage(int dealer, int taker, float amount);
/* ticks a player spent in a vehicle (a tag folder), added to their totals */
void event_log_vehicle_time(int slot, int tag, int ticks);

/* an event (copied); FALSE if it was dropped (the log's limits) */
int event_log_add(struct event_log_record const *record);

/* the game's end: the batch as JSON (malloc'd, NUL-terminated, its length
in *length), NULL if there is nothing to send (no game, or no player) or
no memory. The log is empty after. invite: the game's listed invite, ""
if none. */
char *event_log_finish(struct event_log_end const *end, char const *invite, size_t *length);

/* (tests) the events kept now, and those dropped */
int event_log_count(void);
int event_log_dropped(void);

/* ---------- event_gzip.c */

/* data compressed as a gzip member (RFC 1952: deflate's fixed codes, RFC
1951): malloc'd, its length in *compressed_length; NULL if no memory */
unsigned char *event_gzip(unsigned char const *data, size_t length, size_t *compressed_length);

/* ---------- event_upload.c: the host's side (not in the unit tests) */

/* whether games are recorded (HALO_DEDICATED_REPORT_EVENTS, or its default:
server/docs/events.md), and the most events one keeps */
int event_upload_enabled(void);
int event_upload_event_limit(void);
/* a finished game's batch (event_log_finish's), taken over: compressed,
written to the local files if asked (HALO_DEDICATED_EVENTS_FILES) and sent
to the game list on a thread of its own, again later if it could not be */
void event_upload_submit(char *json, size_t length);
/* random bytes (the game's id) */
void event_upload_random(unsigned char *bytes, int count);
/* the server's process: CPU time used since the last call (in thousandths
of a core over the wall time between the calls) and resident memory (KB);
-1 for what is not known */
void event_upload_system_sample(int *cpu_permille, int *resident_kb);
/* the unix time now */
unsigned int event_upload_time(void);
/* the game's listed invite (its digits), "" if none */
void event_upload_invite(char *text, int size);

#endif
