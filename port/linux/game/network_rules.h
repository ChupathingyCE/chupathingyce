/*
NETWORK_RULES.H

The distributed netcode's rules that need no game to decide: what a client
does with the host's word on its own player standing on an elevator, which
datagrams are near enough in time to be taken, the bytes of the aim assist
policy message and of a player's input entry, which device a player aims
with, and what versions 33 to 38 changed (the game settings record's size by
the host's version, the gametype's further options, the host's shield effects
message, how near a static target's copy must be), and the layouts those
versions' facts make, which a signed legacy table extends for newer versions
(delta.h, "layouts"). network_distributed.c asks, the unit tests (port/linux/
tests/network_rules_test.c) check the answers byte for byte. Nothing here has
a dependency but the C library and delta.h's numbers; multi-byte values are
little-endian, as on every machine the game runs on.
*/

#ifndef __NETWORK_RULES_H
#define __NETWORK_RULES_H

#include <stddef.h>
#include <stdint.h>

struct delta_layout_fact;

/* ---------- constants */

/* how far (world units) the host's word on a client's own player may be
from where the client had it before the client moves it there */
#define NETWORK_RULES_LOCAL_CORRECTION_TOLERANCE 3.0f
/* how much of the height of that error a client discards while its player
stands on the same elevator as when it was recorded: the platform's travel
over a round trip */
#define NETWORK_RULES_ELEVATOR_PHASE_TOLERANCE 4.0f
/* the ticks a biped's elevator counter is set to on each tick that finds it
on an elevator (bipeds.c) */
#define NETWORK_RULES_ELEVATOR_CONTACT_TICKS 60

/* how far (ticks) the stamp of a datagram may be from the latest taken from
its sender, either way: 15 seconds of the game's 30 ticks a second */
#define NETWORK_RULES_TIME_WINDOW_TICKS 450

/* a distributed message's header: the message header word (size in bits
4 to 15, type in bits 2 and 3, flags in bits 0 and 1), the kind, the number
of entries and the sender's tick */
#define NETWORK_RULES_HEADER_SIZE 8
#define NETWORK_RULES_MESSAGE_TYPE_DATA 2

/* the host's policy on aiming assist for mouse and keyboard (message 90,
reliable, to a client): the header, then whether to block (0 or 1) as a
16-bit value and 16 bits that are 0 */
#define NETWORK_RULES_AIM_ASSIST_KIND 90
#define NETWORK_RULES_AIM_ASSIST_SIZE 12

/* a player's input entry in the host-bound message: its first 12 bytes, then
the 32-byte action and the buttons of four ticks (52 bytes in all) */
#define NETWORK_RULES_INPUT_HEAD_SIZE 12
#define NETWORK_RULES_INPUT_SIZE 52

/* ---------- prototypes/NETWORK_RULES.C */

/* whether a biped is in floor contact with its elevator: not in the air,
with a support surface, and its elevator counter just set (an elevator it
remembers after a jump or a step off is not contact) */
int network_rules_floor_contact(int airborne, int has_support_surface, int elevator_ticks);

/* (the host) whether a unit rides an elevator, for taking a client's word on
where it is: its biped has an elevator that is a machine, and either that is
moving (its device's position velocity is not 0) or the unit is in floor
contact with it. An elevator remembered alone (after a jump or a step off a
stopped one) is not riding. */
int network_rules_riding_elevator(int elevator_is_machine, int elevator_moving, int floor_contact);

/* the host's word on a client's own player against the client's record of
the same tick: error is the host's position less the record's (x, y, z).
When the record has an elevator (recorded_elevator, -1 for none) and the
player now stands on that same one (current_elevator), the height of the
error is dropped, if the error across is within the correction tolerance and
the height within the phase tolerance (both ends included). Returns whether
what is left is further than the correction tolerance, so that the player is
moved by it (error as changed here); otherwise nothing is corrected. */
int network_rules_elevator_correction(int32_t recorded_elevator, int32_t current_elevator, float *error);

/* a datagram's stamp against the latest taken from its sender (*latest, -1
for none): returns 0 to drop it; 1 to take it; 2 to take it as a message
over the sender's own stream that is further than the window from the
latest, which moves *latest to the stamp (and the kinds' newest seen from the
sender are forgotten). A datagram within the window moves *latest forward
only. A message over the stream is taken whatever its stamp. The distance
is measured unsigned. */
int network_rules_stamp_window(int32_t *latest, int32_t stamp, int from_stream, uint32_t window);

/* writes the aim assist policy message (NETWORK_RULES_AIM_ASSIST_SIZE bytes,
a reliable data message of one entry): block is 0 or 1 */
void network_rules_aim_assist_write(uint8_t *message, int block, int32_t game_time);

/* checks a message that came from the host: exactly NETWORK_RULES_AIM_ASSIST_SIZE
bytes (the header's size too), the data type, kind 90, one entry, block 0 or 1,
the 16 bits after it 0, and (reliable) not from a datagram. Returns whether to
take it, and in *block what it says. */
int network_rules_aim_assist_read(uint8_t const *message, int size, int from_datagram, int *block);

/* the first 12 bytes of a player's input entry: its player (byte 0), the
structure BSP the client has loaded (1), whether this player plays a
network co-op game without friendly fire between players (2: 1, else 0;
always 0 outside co-op), a byte of padding (3: 0), the client's tick (4) and
the latest host tick it has had (8) */
void network_rules_input_head_write(uint8_t *head, int player, int structure_bsp, int no_friendly_fire, int32_t tick,
	int32_t host_time);
/* ... and what a host reads: a value other than 0 in byte 2 is a player
who has declined friendly fire */
void network_rules_input_head_read(uint8_t const *head, int *player, int *structure_bsp, int *no_friendly_fire,
	int32_t *tick, int32_t *host_time);

/* whether the mouse is the device a player aims with, by when (milliseconds,
0 for never) the mouse, the right stick and a touch screen last aimed: the
mouse when the stick never aimed or the mouse moved at or after it, unless
touch aimed at or after both. A controller merely being connected changes
nothing. */
int network_rules_mouse_is_aim_device(uint64_t mouse_ms, uint64_t stick_ms, uint64_t touch_ms);

/* whether the host's policy blocks mouse magnetism now: only in a game in
progress, off the main menu's scene; on the host its own setting, on a client
what the host last said (blocked until it speaks), never otherwise */
int network_rules_aim_assist_blocks(int game_in_progress, int main_menu_active, int is_host, int is_client,
	int host_setting, int client_policy_blocked);

/* ---------- network versions 33 to 38 */

/* the versions that changed what a client does: 34 has shield-only scenery
and devices as static targets (every machine recharges them, a client
reports no damage one of them deals, and an event on a copy made during play
finds the nearest one); 35 has the gametype's further options, the game
settings record 16 bytes longer; 36 has the host's shield effects message */
#define NETWORK_RULES_STATIC_SHIELDS_VERSION 34
#define NETWORK_RULES_EXTENDED_OPTIONS_VERSION 35
#define NETWORK_RULES_SHIELD_EFFECTS_VERSION 36
/* (a host's version not known, as for a game joined by an invitation) */
#define NETWORK_RULES_VERSION_UNKNOWN 0xFFFF

/* the game settings record at the network's 128 machines and 128 players:
11 to 34, and 35 on (the gametype's options grow from 0x1C to 0x2C bytes, in
front of the record's last 4, its local data) */
#define NETWORK_RULES_SETTINGS_SIZE 13120
#define NETWORK_RULES_EXTENDED_OPTIONS_SIZE 16
#define NETWORK_RULES_SETTINGS_EXTENDED_SIZE (NETWORK_RULES_SETTINGS_SIZE + NETWORK_RULES_EXTENDED_OPTIONS_SIZE)

/* the further options' bytes (0x1C to 0x2B of the gametype's options) */
enum
{
	NETWORK_RULES_OPTION_MODE = 0,
	NETWORK_RULES_OPTION_HARDCORE,
	NETWORK_RULES_OPTION_FIRST_ZOMBIES,
	NETWORK_RULES_OPTION_ZOMBIE_SPEED,
	NETWORK_RULES_OPTION_SKULL_DAMAGE,
	NETWORK_RULES_OPTION_ZOMBIE_RESPAWN,
	NETWORK_RULES_OPTION_COUNTDOWN,
	NETWORK_RULES_OPTION_ZOMBIE_SHIELDS,
	NETWORK_RULES_OPTION_WEAPON_ORDER,
	NETWORK_RULES_OPTION_MELEE_DEMOTES,
	/* (then 6 reserved bytes, 0) */
};

/* the mode byte: OpenCE's game types played on the Slayer engine */
enum
{
	NETWORK_RULES_MODE_NONE = 0,
	NETWORK_RULES_MODE_SWAT,
	NETWORK_RULES_MODE_INFECTION,
	NETWORK_RULES_MODE_GUN_GAME,
	NETWORK_RULES_NUMBER_OF_MODES
};

/* the host's shield effects (message 91, reliable, to a client): the header,
the effects as a 16-bit value (0 stock to 6; 6 leaves each machine its own player's look) and 16 bits that are 0 */
#define NETWORK_RULES_SHIELD_EFFECTS_KIND 91
#define NETWORK_RULES_SHIELD_EFFECTS_SIZE 12
#define NETWORK_RULES_SHIELD_EFFECTS_MAXIMUM 6

/* how far (world units) past a static target's bounding sphere a host's
event on another copy of one reaches, before an area effect's cutoff */
#define NETWORK_RULES_STATIC_EVENT_REACH 2.0f
/* ... and a client's hit report on its own copy reaches past the host's
nearest one's (a host's matching) */
#define NETWORK_RULES_STATIC_REPORT_REACH 1.0f

/* the size of the game settings record a client takes from a host of the
version (the network's slots): 0 for a version not known, whose record
may be either */
int network_rules_settings_size(unsigned version);
/* whether a record whose pieces say total_size is the one a host of the
version sends: of a known version only its own size, of one not known
either size */
int network_rules_settings_size_taken(unsigned version, unsigned total_size);
/* a record of version 35 on (NETWORK_RULES_SETTINGS_EXTENDED_SIZE bytes, as
the wire has it) made the older layout in place: the 16 further option bytes
into extended, and the local data moved up after the first 0x1C */
void network_rules_settings_fold_options(uint8_t *record, uint8_t *extended);

/* the further options of a gametype that has none of its own (an older
host's, an older saved file's) */
void network_rules_extended_options_default(uint8_t *options);
/* the further options as every machine takes them: a mode past Gun Game
none, each flag 0 or 1, a ranged byte outside its range its default (not
held to the range), a weapon order past random strongest first, the
reserved bytes 0 */
void network_rules_extended_options_sanitize(uint8_t *options);
/* which of OpenCE's game types the (sanitized) options play on a variant:
the mode only on the Slayer engine (SWAT on any, Infection with teams alone,
Gun Game free for all alone), else NETWORK_RULES_MODE_NONE; hardcore (on any
engine) in *hardcore */
int network_rules_extended_options_mode(uint8_t const *options, int slayer, int teams, int *hardcore);
/* a game's kind byte in a listing or a LAN advertisement (version 37): the
low seven bits an OpenCE game type's number (0 none, 1 SWAT, 2 Infection,
3 Gun Game), bit 7 Hardcore. Its name for a list, in capitals, or NULL for
a game with no name of its own (a stock game). The mode counts only on the
Slayer engine (2 in the stock order, 1 CTF to 5 Race) and an unknown mode is
none; Hardcore on its own is "HARDCORE " and the engine's name (no name for
an engine outside 1 to 5). short_form: "HC SWAT" for "HARDCORE SWAT". The
name is the game types this build does not play, so a name is also the
reason to leave the game unjoined. Nothing here rejects a kind */
char const *network_rules_kind_name(int kind, int engine, int short_form);

/* writes the shield effects message (NETWORK_RULES_SHIELD_EFFECTS_SIZE bytes,
a reliable data message of one entry); effects past the last are stock */
void network_rules_shield_effects_write(uint8_t *message, int effects, int32_t game_time);
/* checks a message that came from the host as network_rules_aim_assist_read
does: the size, the data type, kind 91, one entry, effects 0 to maximum (the
host's layout's, NETWORK_RULES_SHIELD_EFFECTS_MAXIMUM built in), the 16 bits
after them 0, not from a datagram. Returns whether to take it, and in
*effects what it says. */
int network_rules_shield_effects_read(uint8_t const *message, int size, int from_datagram, int maximum,
	int *effects);

/* (a client) how far past a static target's bounding sphere the host's event
on another copy of one reaches: NETWORK_RULES_STATIC_EVENT_REACH, and an area
effect's cutoff radius (area: the damage is an area effect's, of a damage
effect definition) */
float network_rules_static_event_reach(int area, float cutoff_radius);
/* whether a copy at distance_squared from the point, of the bounding sphere
radius, is within the reach of it (both ends included) */
int network_rules_static_within(float distance_squared, float radius, float reach);

/* ---------- a host's side of network versions 34 and 35

A host announces one number (delta_legacy_announce), and an OpenCE machine
joins only a host of its own, so an OpenCE machine of the game is of that
number. A Delta machine says in its HELLO the newest version whose rules it
plays (DELTA_LEGACY_NEWEST; builds before 35 their own number, 32 or older),
and plays by the host's advertised number up to it. */

/* (a host) the size of the game settings record it sends a machine of its
game (13,120 or 13,136 bytes), of announce, the number it announces;
delta_version, the number the machine's HELLO said (0: none: an OpenCE
machine, or a Delta one whose HELLO has not come); waiting, whether a HELLO
may still come. 0: wait for it. A host of 34 or older sends every machine
the older record at once. */
int network_rules_host_settings_size(unsigned announce, unsigned delta_version, int waiting);
/* (a host) whether a machine's players report the damage a static target
deals (a tripmine's going off; network version 34): an OpenCE machine of a
host of 34 on does not, nor a Delta machine that takes 35's record from the
host (one that may be of 34 alone, joined by an invitation, is taken to
report: then the damage is lost, never dealt twice) */
int network_rules_host_static_damage_reported(unsigned announce, unsigned delta_version);
/* the bytes offset to offset + length of the record of 35's layout made
from the older one (record, NETWORK_RULES_SETTINGS_SIZE bytes as the wire
has it) and the 16 further option bytes (extended), into out: the older
record's bytes but its last 4, then the further ones, then those 4 (its
local data) */
void network_rules_settings_extended_copy(uint8_t const *record, uint8_t const *extended, int offset, int length,
	uint8_t *out);

/* ---------- layouts: a host's version's facts, built in and from the table

What this build reads of a host of a network version as data: the game
settings record's size, the gametype's options' bytes in it (the record is
its first 0x3320 bytes, the options, its 4 bytes of local data, and from a
table's facts any bytes after them), the shield effects message's largest
value, and the distributed message kinds such a host sends that a machine
that does not know them drops. Built in: what versions 11 to
DELTA_LEGACY_NEWEST (delta.h) are, and for a newer version the newest's. A
signed legacy table's facts (delta.h's struct delta_layout_fact) extend a
version past DELTA_LEGACY_NEWEST only, and only within these limits (the
buffers every build has room for); a fact's field past them is not taken. */

/* the gametype's options in the record: to 34, and from 35 */
#define NETWORK_RULES_OPTIONS_SIZE 0x1C
#define NETWORK_RULES_OPTIONS_EXTENDED_SIZE (NETWORK_RULES_OPTIONS_SIZE + NETWORK_RULES_EXTENDED_OPTIONS_SIZE)
/* the record's bytes before the options */
#define NETWORK_RULES_SETTINGS_HEAD_SIZE (NETWORK_RULES_SETTINGS_SIZE - 4 - NETWORK_RULES_OPTIONS_SIZE)
/* how many bytes a table may add past 35's record (the options and the
bytes after the local data together) */
#define NETWORK_RULES_LAYOUT_GROWTH_MAXIMUM 64
#define NETWORK_RULES_SETTINGS_MAXIMUM (NETWORK_RULES_SETTINGS_EXTENDED_SIZE + NETWORK_RULES_LAYOUT_GROWTH_MAXIMUM)
/* the most the shield effects message's largest value may be raised to (a
value this build has no look for is drawn as stock) */
#define NETWORK_RULES_SHIELD_EFFECTS_LIMIT 15

struct network_rules_layout
{
	/* the host's version (NETWORK_RULES_VERSION_UNKNOWN: not known) */
	unsigned version;
	/* the record's bytes (0 for a version not known: either built-in size)
	and the options' bytes in it (0: by the record's size) */
	int settings_size;
	int options_size;
	int shield_effects_maximum;
	/* the kinds a machine that does not know them drops, a bit a kind */
	uint8_t dropped[32];
	/* how many of the table's facts' fields were taken, and how many were
	past the limits and were not */
	int learned;
	int refused;
};

/* the built-in facts of a version */
void network_rules_layout_built_in(unsigned version, struct network_rules_layout *layout);
/* a table's facts (count of them, oldest first) applied to a layout from
network_rules_layout_built_in: each fact of a version past DELTA_LEGACY_NEWEST,
up to the layout's, in order; a size may only grow (the bytes after the local
data never shrink), to the limits; the shield effects' largest value only
rise, to NETWORK_RULES_SHIELD_EFFECTS_LIMIT; dropped kinds add */
void network_rules_layout_apply(struct network_rules_layout *layout, struct delta_layout_fact const *facts,
	int count);
/* where the table's facts come from (delta.c's delta_legacy_layouts, told
at start; none: the built-in facts alone, as on a build without the table) */
void network_rules_set_layout_source(int (*source)(struct delta_layout_fact *facts, int count));
/* a version's layout: built in, and the table's facts applied */
void network_rules_layout(unsigned version, struct network_rules_layout *layout);
/* the newest version whose layout this build reads: DELTA_LEGACY_NEWEST, or
the table's newest fact past it (a Delta client's HELLO says it) */
unsigned network_rules_layout_newest(void);
/* whether a layout says a host of it sends kind, a machine not knowing it
dropping it */
int network_rules_layout_drops(struct network_rules_layout const *layout, unsigned kind);

/* the options' bytes in a record of total_size from a host of the version
(of a version not known, by its size: 35's for the longer record) */
int network_rules_settings_options_size(unsigned version, unsigned total_size);
/* a record of a layout with options_size bytes of options (total_size bytes
as the wire has it, past NETWORK_RULES_SETTINGS_SIZE) made the older layout
in place: the 16 further option bytes 35 named into extended, the local data
moved up after the first 0x1C, the rest zeros (bytes this build has no use
for) */
void network_rules_settings_fold(uint8_t *record, int total_size, int options_size, uint8_t *extended);
/* (a host) the options' bytes of the record it sends a machine (as
network_rules_host_settings_size: a version's) */
int network_rules_host_settings_options(unsigned announce, unsigned delta_version);
/* the bytes offset to offset + length of the record of a layout with
options_size bytes of options made from the older one (record) and the 16
further option bytes 35 named (extended): the older record's bytes but its
last 4, the further ones, zeros for options this build has none of, the 4
bytes of local data, then zeros */
void network_rules_settings_layout_copy(uint8_t const *record, uint8_t const *extended, int options_size,
	int offset, int length, uint8_t *out);

#endif /* __NETWORK_RULES_H */
