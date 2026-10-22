/*
NETWORK_RULES.H

The distributed netcode's rules that need no game to decide: what a client
does with the host's word on its own player standing on an elevator, which
datagrams are near enough in time to be taken, the bytes of the aim assist
policy message and of a player's input entry, and which device a player aims
with. network_distributed.c asks, the unit tests (port/linux/tests/
network_rules_test.c) check the answers byte for byte. Nothing here has a
dependency but the C library; multi-byte values are little-endian, as on
every machine the game runs on.
*/

#ifndef __NETWORK_RULES_H
#define __NETWORK_RULES_H

#include <stdint.h>

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

#endif /* __NETWORK_RULES_H */
