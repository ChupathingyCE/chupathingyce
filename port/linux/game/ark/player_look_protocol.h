/*
PLAYER_LOOK_PROTOCOL.H

A player's multiplayer look as data, with no game in it, so that it is tested
and fuzzed on its own (port/vita/tests/player_look_net_test.c,
net_fuzz_player_look.c): the look's checks, the message between this project's
builds, the host's and a joiner's tables. look_net.c carries it in the game's
messages; the host's rate is h2_emblem_protocol.h's.

A look says what a player wears, never who it is or what it does: one of five
models, a visor colour, a second armour colour and an index of the armour for
each part, in the order of the sender's Halo 3 files' catalog (a hash of its
names: another list, another game update, and the parts are not used). It is
only ever an index into a machine's own tables: no name, no path, no bitmap and
no colour value crosses the network, and a model a machine cannot show is a
sane stand-in of its own.
*/

#ifndef HALO_PLAYER_LOOK_PROTOCOL_H
#define HALO_PLAYER_LOOK_PROTOCOL_H

#include <stdint.h>

#include "h2_emblem_protocol.h"

enum
{
	PLAYER_LOOK_MODEL_H1_SPARTAN = 0,
	PLAYER_LOOK_MODEL_H2_SPARTAN,
	PLAYER_LOOK_MODEL_H3_SPARTAN,
	PLAYER_LOOK_MODEL_H2_ELITE,
	PLAYER_LOOK_MODEL_H3_ELITE,
	PLAYER_LOOK_MODELS,

	PLAYER_LOOK_PART_HELMET = 0,
	PLAYER_LOOK_PART_LEFT_SHOULDER,
	PLAYER_LOOK_PART_RIGHT_SHOULDER,
	PLAYER_LOOK_PART_CHEST,
	PLAYER_LOOK_PART_ARMS,
	PLAYER_LOOK_PART_LEGS,
	PLAYER_LOOK_PART_BODY,
	PLAYER_LOOK_PARTS,

	/* a part's index: 0 the model's own, else 1 + its place in the catalog's
	region (h3_spartan.h: H3_CATALOG_MAXIMUM_PERMUTATIONS at most) */
	PLAYER_LOOK_PART_INDICES = 81,
	/* (player_cosmetics.h: PLAYER_COSMETICS_VISORS) */
	PLAYER_LOOK_VISORS = 12,
	/* (h2_emblem_protocol.h: H2_EMBLEM_COLORS, Halo 2's eighteen) */
	PLAYER_LOOK_SECONDARIES = H2_EMBLEM_COLORS,

	PLAYER_LOOK_NET_VERSION = 1,
	PLAYER_LOOK_NET_HOST_MESSAGES = 256,
	PLAYER_LOOK_NET_MACHINES = EMBLEM_NET_MACHINES,
	PLAYER_LOOK_NET_PLAYERS = EMBLEM_NET_PLAYERS,
};

struct player_look
{
	uint8_t model;
	uint8_t visor;
	uint8_t secondary;
	uint8_t parts[PLAYER_LOOK_PARTS];
	/* the catalog its parts are of (0: it has none, all the models' own) */
	uint32_t catalog;
};

/* both ways (20 bytes). A joiner's own (machine ignored by the host, which
knows who sent it), the host's for any player. present 0, machine 0 and
everything else zero from a joiner: its hello, "this build takes looks"; from a
host, the same back. present 0 with a machine from a host: that player has none
any more */
struct player_look_net_message
{
	uint16_t version;
	uint8_t machine;
	uint8_t controller;
	uint8_t present;
	uint8_t model;
	uint8_t visor;
	uint8_t secondary;
	uint8_t parts[PLAYER_LOOK_PARTS];
	uint8_t reserved;
	/* (bytes, not a word: the message's own order on every machine) */
	uint8_t catalog[4];
};

typedef char player_look_net_message_size_assert[sizeof(struct player_look_net_message) == 20 ? 1 : -1];

struct player_look_net_entry
{
	uint8_t known;
	struct player_look look;
	uint32_t serial;
};

struct player_look_net_table
{
	struct player_look_net_entry entries[PLAYER_LOOK_NET_MACHINES][PLAYER_LOOK_NET_PLAYERS];
	uint32_t serial;
};

/* a joiner's count of what its host sends: a window of a second, at most
PLAYER_LOOK_NET_HOST_MESSAGES in it (a table's worth, twice over) */
struct player_look_net_host_rate
{
	uint32_t started;
	uint32_t window_ms;
	uint32_t count;
};

/* the stock Spartan, nothing changed */
void player_look_default(struct player_look *look);
/* every field in its range, and no part given for a model without armour
(Halo 3's alone have it). 0: not a look to keep */
int player_look_valid(struct player_look const *look);

void player_look_net_encode(struct player_look_net_message *message, int machine, int controller, int present,
	struct player_look const *look);
/* the message's fields checked. On 1: *machine, *controller, *present and,
when present, *look are safe to use. Dropped: another version, a machine or
player out of range, a present flag not 0 or 1, a look player_look_valid
refuses, a reserved byte set, a removal or hello with anything else in it */
int player_look_net_decode(struct player_look_net_message const *message, int *machine, int *controller, int *present,
	struct player_look *look);

/* FALSE: this message of the host's is one too many this second */
int player_look_net_host_rate_take(struct player_look_net_host_rate *rate, uint32_t now_ms);

void player_look_net_table_reset(struct player_look_net_table *table);
/* FALSE when it is what the table has: a joiner resending costs no relay */
int player_look_net_table_set(struct player_look_net_table *table, int machine, int controller, struct player_look const *look);
int player_look_net_table_clear(struct player_look_net_table *table, int machine, int controller);
int player_look_net_table_clear_machine(struct player_look_net_table *table, int machine);
/* NULL when unknown or out of range */
struct player_look const *player_look_net_table_get(struct player_look_net_table const *table, int machine, int controller);

/* what a machine shows of a look. catalog: the catalog this machine has for the
look's model (0: it has no Halo 3 files for it). A model it cannot show is the
same species it can, else the first; parts of another catalog are dropped. The
result is valid for this machine whatever came in */
void player_look_sanitize(struct player_look *look, uint32_t catalog, int has_halo2, int has_halo3_spartan, int has_halo3_elite,
	uint8_t const *part_counts);

#endif
