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
/* the server's keep-alive delta: one entry */
void ce_payload_put_server_keep_alive(struct ce_payload *payload, int index, unsigned int value);
/* the payload's length in bytes (written in its first 11 bits), or 0 */
int ce_payload_end(struct ce_payload *payload);

/* a free-for-all Slayer game's settings, as Custom Edition's own server
sends them (name, map and players to fill in) */
void ce_game_settings_slayer(struct ce_game_settings *settings);

#endif
