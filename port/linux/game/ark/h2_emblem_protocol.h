/*
H2_EMBLEM_PROTOCOL.H

A player's Halo 2 emblem as data, with no game in it, so that it is tested
and fuzzed on its own (port/vita/tests/emblem_net_test.c, net_fuzz_emblem.c):
the appearance's checks, the message between this project's builds, the
host's and a joiner's tables and the host's rate limit. emblem_net.c carries
it in the game's messages; h2_emblem.c draws it.

The appearance is Halo 2's (s_player_appearance; the checks function_153750,
the decompilation's src/unknown_153750.cpp): four indices into the globals'
18 profile colours - primary and secondary player colour, primary and
secondary emblem colour -, the player model, a foreground of 64, a background
of 32 and its flags (bit 0: the foreground's "a" layer off, X on Halo 2's Choose Emblem
screen, src/unknown_2c4e9c.cpp; bits 1 and 2 the map flips, 3 "show it in a
campaign", which this project's builds never set or take). Nothing else about
a player's look crosses the network: no bitmap, no text, no colour value.
*/

#ifndef HALO_H2_EMBLEM_PROTOCOL_H
#define HALO_H2_EMBLEM_PROTOCOL_H

#include <stdint.h>

#define H2_EMBLEM_FOREGROUNDS 64
#define H2_EMBLEM_BACKGROUNDS 32
#define H2_EMBLEM_COLORS 18
#define H2_EMBLEM_CHANGE_COLORS 4
/* (the player models Halo 2's Choose Player Model lists, c_choose_model_list:
Spartan, Covenant Elite. function_153750 lets -1..3 by; a list of 2 picks) */
#define H2_EMBLEM_MODELS 2
/* (Halo 2's: the top four bits never set; below that, what this project's
builds take: bit 3 dropped) */
#define H2_EMBLEM_FLAGS_INVALID 0xF0
#define H2_EMBLEM_FLAGS_KEPT 0x07
#define H2_EMBLEM_FLAG_FOREGROUND_A_OFF 0x01

#define EMBLEM_NET_VERSION 1
/* the game's machines and a machine's players (its controllers) */
#define EMBLEM_NET_MACHINES 128
#define EMBLEM_NET_PLAYERS 4
/* a joiner's emblems the host takes: this many at once, one more each
EMBLEM_NET_REFILL_MS (a picker scrolled in a lobby is sent when it is
closed; a machine's four players at once fit) */
#define EMBLEM_NET_BURST 8
#define EMBLEM_NET_REFILL_MS 1000

struct h2_emblem
{
	uint8_t colors[H2_EMBLEM_CHANGE_COLORS];
	uint8_t foreground;
	uint8_t background;
	uint8_t flags;
	uint8_t model;
};

/* both ways (14 bytes): a joiner's own (machine ignored by the host, which
knows who sent it), the host's for any player; present 0: that player has
none any more (host to joiner alone) */
struct emblem_net_message
{
	uint16_t version;
	uint8_t machine;
	uint8_t controller;
	uint8_t present;
	uint8_t foreground;
	uint8_t background;
	uint8_t flags;
	uint8_t colors[H2_EMBLEM_CHANGE_COLORS];
	uint8_t model;
	uint8_t reserved;
};

typedef char emblem_net_message_size_assert[sizeof(struct emblem_net_message) == 14 ? 1 : -1];

struct emblem_net_entry
{
	uint8_t known;
	struct h2_emblem emblem;
	/* (the host's: bumped by each change, a machine's sent up to it) */
	uint32_t serial;
};

struct emblem_net_table
{
	struct emblem_net_entry entries[EMBLEM_NET_MACHINES][EMBLEM_NET_PLAYERS];
	uint32_t serial;
};

struct emblem_net_rate
{
	uint32_t last_ms;
	int32_t tokens;
	uint8_t started;
};

/* Halo 2's new profile's appearance (player_profile_initialize,
src/player_profile_storage.cpp) */
void h2_emblem_default(struct h2_emblem *emblem);
/* Halo 2's checks (function_153750), for what came from a file or the wire:
every colour one of the 18, the model one of the 2, the foreground and
background in range, no unknown flag. 0: not an emblem to keep */
int h2_emblem_valid(struct h2_emblem const *emblem);
/* a player the network does not name (a bot): an appearance of Halo 2's
player_profile_make's ranges (16 colours, 64 foregrounds, 32 backgrounds)
from a seed every machine has alike */
void h2_emblem_from_seed(unsigned long seed, struct h2_emblem *emblem);

void emblem_net_encode(struct emblem_net_message *message, int machine, int controller, int present,
	struct h2_emblem const *emblem);
/* the message's fields checked. On 1: *machine, *controller, *present and,
when present, *emblem are safe to use. Reasons a message is dropped: another
version, a machine or player out of range, a present flag not 0 or 1, an
emblem h2_emblem_valid refuses (a removal's fields must be zero) */
int emblem_net_decode(struct emblem_net_message const *message, int *machine, int *controller, int *present,
	struct h2_emblem *emblem);

/* the table (zeros: nothing known). Set: FALSE when it is what the table has
(a joiner resending the same emblem costs no relay). Clear: FALSE when
nothing was there. A machine's whole row when it leaves */
void emblem_net_table_reset(struct emblem_net_table *table);
int emblem_net_table_set(struct emblem_net_table *table, int machine, int controller, struct h2_emblem const *emblem);
int emblem_net_table_clear(struct emblem_net_table *table, int machine, int controller);
int emblem_net_table_clear_machine(struct emblem_net_table *table, int machine);
/* NULL when unknown or out of range */
struct h2_emblem const *emblem_net_table_get(struct emblem_net_table const *table, int machine, int controller);

/* (the host) one message of a machine's at the time given: FALSE when it is
over its rate */
int emblem_net_rate_take(struct emblem_net_rate *rate, uint32_t now_ms);

#endif
