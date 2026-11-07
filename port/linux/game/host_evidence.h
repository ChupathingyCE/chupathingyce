/*
HOST_EVIDENCE.H

What a host refused of each client machine in the game it hosts, counted
for the server's moderators (sv_evidence, the control panel's players):
on-foot predictions out of bounds, vehicle predictions out of bounds, hit
reports that failed validation, windows its clock ran fast in, and two kinds
kept for checks that feed it later (a hit refused for line of sight, a
client message that failed validation). Counts only: nothing here bans or
kicks, nothing is sent anywhere, and nothing outlives the game (a new game,
or a new machine at an index, starts from zero). No address is kept.

Every check that refuses something of a client calls host_evidence_note
once, with the sender's machine index and the kind. Nothing here has a
dependency but the C library; port/linux/tests/host_evidence_test.c tests
it (tools/test_host_evidence.py).
*/

#ifndef __HOST_EVIDENCE_H
#define __HOST_EVIDENCE_H

#include <stddef.h>

/* ---------- constants */

enum host_evidence_kind
{
	/* an on-foot prediction out of the movement bounds (network_distributed.c) */
	_host_evidence_movement,
	/* a vehicle prediction out of the vehicle bounds (network_objects.c) */
	_host_evidence_vehicle,
	/* a hit report that failed validation (network_damage.c), a line of
	sight's refusal aside */
	_host_evidence_hit_report,
	/* a window (two seconds) the machine's clock ran fast, its predictions
	refused (distributed_note_client_clock) */
	_host_evidence_clock,
	/* a hit report refused because the shooter could not see the target
	(network_damage.c, network.hit_line_of_sight) */
	_host_evidence_line_of_sight,
	/* a client message refused as malformed (size, count, values) */
	_host_evidence_malformed,
	NUMBER_OF_HOST_EVIDENCE_KINDS
};

enum
{
	/* the machines counted (halo_port_limits.h's most, on any build) */
	HOST_EVIDENCE_MACHINES = 128,
	/* a count stops here */
	HOST_EVIDENCE_MAXIMUM_COUNT = 999999,
	/* host_evidence_summary's text, at most, with its end */
	HOST_EVIDENCE_SUMMARY_SIZE = 160,
};

/* ---------- prototypes */

/* one refusal of a kind, of the machine at an index (anything out of range
is ignored) */
void host_evidence_note(long machine_index, enum host_evidence_kind kind);

/* a machine's counts back to zero (a new machine at its index); every
machine's (a new game) */
void host_evidence_reset_machine(long machine_index);
void host_evidence_reset(void);

/* a machine's count of a kind, and of every kind */
unsigned long host_evidence_count(long machine_index, enum host_evidence_kind kind);
unsigned long host_evidence_total(long machine_index);

/* a kind's name as the commands and the control API show it ("movement",
"vehicle", "hit_report", "clock", "line_of_sight", "malformed"), NULL for
none */
char const *host_evidence_kind_name(int kind);

/* a machine's counts as text, the kinds it has any of: "movement 3,
hit_report 1", or "none"; its length */
int host_evidence_summary(long machine_index, char *text, size_t size);

#endif
