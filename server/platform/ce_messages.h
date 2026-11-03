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
	CE_DELTA_PLAYER_SPAWN = 8,
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
	int ammunition[2];
	int unknown_6bits;
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
	int unknown_3bits;
	int unknown_6bits_b;
	int unknown_boolean_b;
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
/* a player in the game: its index (5 bits) and its datum handle */
void ce_payload_put_player_add(struct ce_payload *payload, int player, unsigned int handle);
void ce_payload_put_weapon_create(struct ce_payload *payload, const struct ce_weapon_create *weapon);
void ce_payload_put_biped_create(struct ce_payload *payload, const struct ce_biped_create *biped);
void ce_payload_put_player_spawn(struct ce_payload *payload, const struct ce_player_spawn *spawn);
/* the server's keep-alive delta: one entry */
void ce_payload_put_server_keep_alive(struct ce_payload *payload, int index, unsigned int value);
/* the payload's length in bytes (written in its first 11 bits), or 0 */
int ce_payload_end(struct ce_payload *payload);

/* a free-for-all Slayer game's settings, as Custom Edition's own server
sends them (name, map and players to fill in) */
void ce_game_settings_slayer(struct ce_game_settings *settings);

#endif
