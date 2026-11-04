/*
CE_MESSAGES.H

Halo Custom Edition 1.0.10's payloads (ce_messages.c): a bit stream of game
messages, each either a data packet (byte fields, the Xbox game's kind) or
a message delta (Custom Edition's own: fields of fixed bit widths). Used
by ce_transport.c; written from our own captures of Custom Edition 1.0.10's
client and dedicated server and from reading the retail binaries' behavior.

Built with the host's ABI: plain types only.
*/

#ifndef CE_MESSAGES_H
#define CE_MESSAGES_H

enum
{
	CE_GAME_PLAYERS = 16,
	CE_UNIVERSAL_FIELDS = 24,
	CE_ENGINE_VARIANT_SIZE = 56,

	/* data packet types (the game's message group) */
	CE_MESSAGE_NEW_CLIENT_CHALLENGE = 4,
	CE_MESSAGE_MACHINE_REJECTED = 6,
	CE_MESSAGE_BEGIN_GAME = 10,
	CE_MESSAGE_JOIN_GAME_REQUEST = 14,
	CE_MESSAGE_MAP_IS_PRECACHED = 21,
	CE_MESSAGE_CLIENT_LOADED = 26,

	/* message delta types */
	CE_DELTA_OBJECT_DELETE = 0,
	CE_DELTA_BIPED_UPDATE = 4,
	CE_DELTA_OBJECT_UPDATE = 5,
	CE_DELTA_PLAYER_SPAWN = 8,
	CE_DELTA_UNIT_KILLED = 12,
	CE_DELTA_CLIENT_UPDATE = 13,
	CE_DELTA_CHAT = 15,
	CE_DELTA_SLAYER_SCORES = 16,
	CE_DELTA_PLAYER_STATISTICS = 21,
	CE_DELTA_LOCAL_PLAYER_UPDATE = 35,
	CE_DELTA_REMOTE_PLAYER_ACTIONS = 37,
	CE_DELTA_REMOTE_PLAYER_POSITION = 39,
	CE_DELTA_PLAYER_ADD = 7,
	CE_DELTA_BIPED_CREATE = 29,
	CE_DELTA_WEAPON_CREATE = 32,
	CE_DELTA_GAME_SETTINGS = 33,
	/* the client's word that it is there, about every 3 seconds */
	CE_DELTA_CLIENT_KEEP_ALIVE = 52,
	/* the server's, as often: up to 16 entries (an 8-bit and a 32-bit number) */
	CE_DELTA_SERVER_KEEP_ALIVE = 53,
};

struct ce_network_player
{
	/* empty: no player in the slot */
	char name[13];
	int color;
	int icon;
	int machine;
	int controller;
	int team;
	int list_index;
};

/* the network game, as the game settings delta carries it */
struct ce_game_settings
{
	char name[65];
	int map_version;
	char map[129];
	char variant_name[25];
	int engine;
	/* the universal variant's fields in the wire's order (ce_messages.c) */
	unsigned int universal[CE_UNIVERSAL_FIELDS];
	unsigned char engine_variant[CE_ENGINE_VARIANT_SIZE];
	int variant_flags;
	int minimum_players;
	int maximum_players;
	int difficulty;
	int player_count;
	struct ce_network_player players[CE_GAME_PLAYERS];
	unsigned int random_seed;
	int games_played;
	int local_data;
};

/* an object's creation: the fields every kind starts with */
struct ce_object_create
{
	/* the object's tag: its datum handle in the map (salt 0xe174 + index) */
	unsigned int definition;
	/* 9 bits */
	int object;
	/* 0xffff: none */
	int owner;
	/* 5 bits, 0: none */
	int player;
	/* 9 bits, 0: none */
	int parent;
	/* the two orientation vectors, each two 20-bit numbers */
	unsigned int forward[2];
	unsigned int up[2];
	float position[3];
};

struct ce_weapon_create
{
	struct ce_object_create object;
	unsigned int unknown_large;
	int unknown_small;
	/* each magazine's rounds left (not loaded) */
	int ammunition[2];
	/* the weapon's age (a battery's use), 6 bits of 1 */
	int age;
	/* each magazine's rounds loaded */
	int magazine[2];
};

struct ce_biped_create
{
	struct ce_object_create object;
	float change_colors[12];
	int unknown_boolean;
	int unknown_6bits_a;
	int unknown_small;
	int grenades[2];
	/* its vitality (ce_body_bits, ce_shield_bits) */
	int body;
	int shield;
	int unknown_boolean_b;
};

/* a biped's state as it changes (what its player's HUD shows): the
object; a baseline number (8 bits, Custom Edition's server goes up from 2)
that the client keeps; grenades, body vitality in 3 bits of 1 and shield
vitality in 6 bits of 3 (ce_body_bits, ce_shield_bits). A client takes
the body vitality always, the shield only with apply_shield (Custom
Edition's server leaves it to the client's own damage) */
struct ce_biped_update
{
	int object;
	int baseline;
	int apply_shield;
	int grenades[2];
	int body;
	int shield;
	int unknown_boolean;
};

/* a player's biped and weapons, by object index (0: none) */
struct ce_player_spawn
{
	int player;
	int biped;
	unsigned int unknown_large;
	int unknown_object;
	int unknown_medium;
	int weapons[4];
	int unknown_mediums[3];
};

/* a client's actions (its update delta), as a unit's control: the fields
an update leaves out keep their last values */
struct ce_client_update
{
	int update_id;
	int ticks;
	/* the unit's control flags (units.h's bits) */
	unsigned int control_flags;
	float yaw;
	float pitch;
	/* 2 bits a direction: forward/back, then left/right (ce_messages.c) */
	int throttle;
	int primary_trigger;
	int weapon;
	int grenade;
	int unknown_medium;
};

/* another player's actions, as a client animates them */
struct ce_remote_actions
{
	int player;
	int update_id;
	int ticks;
	unsigned int control_flags;
	/* the facing, as the two 20-bit numbers of a direction (ce_direction) */
	unsigned int facing[2];
	int throttle;
	int primary_trigger;
	int weapon;
	int grenade;
};

/* a player's numbers, as the scoreboard and carnage report show them */
struct ce_player_statistics
{
	int kills[4];
	int assists[4];
	int betrayals;
	int deaths;
	int suicides;
	/* the game type's own (Capture the Flag, King, Oddball, Race) */
	int game_specific[3];
	int multiplayer_special;
	int flag;
	float speed_multiplier;
};

/* a chat line's kind: everyone, team, vehicle, or the server's (a server
line names no player: CE_CHAT_NO_PLAYER) */
enum
{
	CE_CHAT_ALL = 0,
	CE_CHAT_TEAM = 1,
	CE_CHAT_VEHICLE = 2,
	CE_CHAT_SERVER = 3,
	CE_CHAT_NO_PLAYER = 0xff,
	/* a line's characters, at most */
	CE_CHAT_SIZE = 255,
};

struct ce_payload
{
	unsigned char *data;
	int size;
	int position;
	int overflow;
};

/* a payload into out (size bytes) */
void ce_payload_begin(struct ce_payload *payload, unsigned char *out, int size);
/* a data packet (its bytes: version first, type last) */
void ce_payload_put_message(struct ce_payload *payload, const unsigned char *message, int length);
/* the game settings delta, in full */
void ce_payload_put_game_settings(struct ce_payload *payload, const struct ce_game_settings *settings);
/* a player in the game: its slot in the game settings' players, its datum
handle and its number on the wire (5 bits) */
void ce_payload_put_player_add(struct ce_payload *payload, int list_index, unsigned int handle, int player);
void ce_payload_put_weapon_create(struct ce_payload *payload, const struct ce_weapon_create *weapon);
void ce_payload_put_biped_create(struct ce_payload *payload, const struct ce_biped_create *biped);
void ce_payload_put_player_spawn(struct ce_payload *payload, const struct ce_player_spawn *spawn);
/* an object's place: header (two 8-bit numbers, the object, a time in
milliseconds, a flag) and its position, still, with its orientation */
void ce_payload_put_object_update(struct ce_payload *payload, int object, unsigned int time, const float position[3],
	const unsigned int forward[2], const unsigned int up[2]);
void ce_payload_put_object_delete(struct ce_payload *payload, int object);
void ce_payload_put_biped_update(struct ce_payload *payload, const struct ce_biped_update *update);
/* a vitality as the biped messages carry it: body (0 to 1) in 3 bits,
shield (0 to 3, 1 full) in 6 */
int ce_body_bits(float vitality);
int ce_shield_bits(float vitality);
/* a client's own player: the last of its updates the server took (6 bits),
a sequence number (5 bits) and where its biped is (a client stops sending
updates once 64 go untaken) */
void ce_payload_put_local_player_update(struct ce_payload *payload, int sequence, int update_id,
	const float position[3]);
void ce_payload_put_remote_player_position(struct ce_payload *payload, int player, int update_id, int sequence,
	const float position[3]);
void ce_payload_put_remote_player_actions(struct ce_payload *payload, const struct ce_remote_actions *actions);
/* a unit killed (its object number), as Custom Edition's server tells a
death with no direction: the client plays the death itself and shows its
own player the respawn countdown (ticks) */
void ce_payload_put_unit_killed(struct ce_payload *payload, int object, int respawn_ticks);
/* a direction (unit vector) as the wire's two 20-bit numbers */
void ce_direction(const float vector[3], unsigned int out[2]);
/* the throttle's two numbers (forward, left) from its 4 bits */
void ce_throttle(int bits, float *forward, float *left);
/* the client updates in a payload, onto update (what they leave out stays):
how many were read */
int ce_payload_read_client_updates(const unsigned char *payload, int length, struct ce_client_update *update,
	void (*each)(void *context, const struct ce_client_update *update), void *context);
/* a chat line (kind CE_CHAT_*, the player who said it or
CE_CHAT_NO_PLAYER), printable ASCII */
void ce_payload_put_chat(struct ce_payload *payload, int kind, int player, const char *text);
/* Slayer's scores: each player's (by absolute index) and each team's */
void ce_payload_put_slayer_scores(struct ce_payload *payload, const int players[CE_GAME_PLAYERS],
	const int teams[CE_GAME_PLAYERS]);
/* a player's numbers (its wire number, 5 bits), in full */
void ce_payload_put_player_statistics(struct ce_payload *payload, int player,
	const struct ce_player_statistics *statistics);
/* the server's keep-alive delta: one entry */
void ce_payload_put_server_keep_alive(struct ce_payload *payload, int index, unsigned int value);
/* the payload's length in bytes (written in its first 11 bits), or 0 */
int ce_payload_end(struct ce_payload *payload);

/* a free-for-all Slayer game's settings, as Custom Edition's own server
sends them (name, map and players to fill in) */
void ce_game_settings_slayer(struct ce_game_settings *settings);

#endif
