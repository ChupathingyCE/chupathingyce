/*
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later

DELTA_LAYOUT.H

Layout facts by network version, and a converter for the game settings
record between them, for a client that plays a range of OpenCE/legacy
network versions (docs/delta/versioning.md, "Layout facts by network version" and
"Playing a range"). layouts.json says the same facts as data.

The record is the game settings record at the network's 128 machines and
128 players. Its first 0x3320 bytes never move. Then come the gametype's
options (0x1C bytes to 34, 0x2C from 35, more by a signed table's facts or
another project's row), the 4 bytes of local data, and from 42 the bytes
after them (64: two MCC digests, zero on other maps).

Plain C99. No allocation, no OS calls; every buffer is the caller's. Bytes
are copied as they are, so it runs the same on big-endian machines.
*/

#ifndef DELTA_LAYOUT_H
#define DELTA_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

/* the network versions: the oldest a client of these facts joins (the newest
breaking one), the newest whose facts are built in, and how many past that
are presumed additive (played with the newest's facts) */
#define DELTA_LAYOUT_OLDEST 11
#define DELTA_LAYOUT_NEWEST 44
#define DELTA_LAYOUT_PRESUMED 1

/* the record's parts */
#define DELTA_LAYOUT_HEAD_SIZE 0x3320
#define DELTA_LAYOUT_OPTIONS_SIZE 0x1C
#define DELTA_LAYOUT_EXTENDED_SIZE 16
#define DELTA_LAYOUT_OPTIONS_EXTENDED_SIZE (DELTA_LAYOUT_OPTIONS_SIZE + DELTA_LAYOUT_EXTENDED_SIZE)
#define DELTA_LAYOUT_LOCAL_DATA_SIZE 4
#define DELTA_LAYOUT_SETTINGS_SIZE 13120 /* 11 to 34 */
#define DELTA_LAYOUT_SETTINGS_EXTENDED_SIZE 13136 /* 35 to 41 */
#define DELTA_LAYOUT_SETTINGS_EPOCH_SIZE 13200 /* 42 on */
/* the most a signed table's facts may add past 35's record */
#define DELTA_LAYOUT_GROWTH_MAXIMUM 128
#define DELTA_LAYOUT_SETTINGS_MAXIMUM (DELTA_LAYOUT_SETTINGS_EXTENDED_SIZE + DELTA_LAYOUT_GROWTH_MAXIMUM)
/* the most another project's row may make the options (Content Evolved's
0x58 at 44 fits) */
#define DELTA_LAYOUT_OPTIONS_MAXIMUM (DELTA_LAYOUT_OPTIONS_EXTENDED_SIZE + DELTA_LAYOUT_GROWTH_MAXIMUM)

/* the further options (from 35: the options' bytes 0x1C to 0x2B) */
enum
{
	DELTA_LAYOUT_OPTION_MODE = 0,
	DELTA_LAYOUT_OPTION_HARDCORE,
	DELTA_LAYOUT_OPTION_FIRST_ZOMBIES,
	DELTA_LAYOUT_OPTION_ZOMBIE_SPEED,
	DELTA_LAYOUT_OPTION_SKULL_DAMAGE,
	DELTA_LAYOUT_OPTION_ZOMBIE_RESPAWN,
	DELTA_LAYOUT_OPTION_COUNTDOWN,
	DELTA_LAYOUT_OPTION_ZOMBIE_SHIELDS,
	DELTA_LAYOUT_OPTION_WEAPON_ORDER,
	DELTA_LAYOUT_OPTION_MELEE_DEMOTES
	/* (then 6 reserved bytes, 0) */
};

/* the join request's fill (a table's "join_request_fill": "zeros", or
any other name) */
#define DELTA_LAYOUT_FILL_ZEROS 0
#define DELTA_LAYOUT_FILL_UNKNOWN 1

/* where a unit state's further part is */
#define DELTA_LAYOUT_PART_AFTER_UP 1
#define DELTA_LAYOUT_PART_LAST 2

/* a host's version's facts */
struct delta_layout
{
	unsigned version;
	/* the record's bytes, and the options' bytes in it */
	unsigned settings_size;
	unsigned options_size;
	/* the shield effects message's (91) largest value */
	unsigned shield_effects_maximum;
	/* the zero bytes a client's join request carries after its first 0x70,
	and where in them the host's version goes as a big-endian 16-bit word
	(-1: nowhere) */
	unsigned join_request_extra;
	int join_request_version_at;
	/* the bytes every distributed message's header has after its 8, and the
	kind of the host's checkpoint boundary when the first 4 are its epoch
	(0: none) */
	unsigned header_extra;
	unsigned epoch_kind;
	/* a unit state's further parts by bit: size (0 none) and place */
	uint8_t unit_part_size[8];
	uint8_t unit_part_at[8];
	/* riders leave a vehicle on the ground whose up's height is below this
	many thousandths (0: upside down alone) */
	unsigned rider_ejection_up;
	/* the distributed kinds a host of the version sends that a reader that
	doesn't play them drops, a bit a kind */
	uint8_t dropped[32];
};

/* a fact from a signed table's "layouts" for one version (0: not said;
the join request's length and version word have their own "said" flags,
as 0 is a value of them) */
struct delta_layout_fact
{
	unsigned version;
	unsigned settings_size;
	unsigned options_size;
	unsigned shield_effects_maximum;
	unsigned join_request_extra;
	int join_request_said;
	/* what fills those bytes: DELTA_LAYOUT_FILL_ZEROS, the only fill a
	reader writes; a fact of another fill is not taken */
	unsigned join_request_fill;
	unsigned join_request_version_at;
	int join_request_version_said;
	unsigned header_extra;
	unsigned epoch_kind;
	uint8_t unit_part_size[8];
	uint8_t unit_part_at[8];
	unsigned rider_ejection_up;
	uint8_t dropped[32];
};

/* the built-in facts of a version, 11 to DELTA_LAYOUT_NEWEST +
DELTA_LAYOUT_PRESUMED (45; 45 has 44's). Returns 0, the layout all zeros,
for any other version: one before 11 no reader of these facts joins, and
one past 45 has facts only from a signed table (delta_layout_of). */
int delta_layout_built_in(unsigned version, struct delta_layout *layout);

/* a table's facts (count of them, oldest first) applied to a version's
built-in layout: each fact of a version past DELTA_LAYOUT_NEWEST and no
newer than the layout's, in order, within the limits (a size only grows,
the record within DELTA_LAYOUT_SETTINGS_MAXIMUM; the shield effects' value
only rises, to 15; the join request to 1,024 bytes; the header's further
bytes to 8; an epoch kind from 93 with 4 header bytes; parts of bits 6 and
7 only, to 64 bytes; the riders' ejection to 999). Returns the number of
fields that were past a limit and not taken. Every field is bounded before
it is used, so no value (a wrapped 32-bit one too) moves the record past its
limits or the version word outside the request's bytes. */
int delta_layout_apply(struct delta_layout *layout, struct delta_layout_fact const *facts, int count);

/* a version's layout: built in, and a table's facts (count of them, oldest
first; may be none) applied. A version past 45 starts from 44's facts and
is played only up to one past the newest fact's version. Returns 0 for a
version outside that range or before 11; *refused (may be NULL) gets how
many fact fields were past a limit and not taken */
int delta_layout_of(unsigned version, struct delta_layout_fact const *facts, int count, struct delta_layout *layout,
	int *refused);

/* where the local data is, and how many bytes follow it */
unsigned delta_layout_local_data_offset(struct delta_layout const *layout);
unsigned delta_layout_trailing_size(struct delta_layout const *layout);
/* whether a host of the layout sends kind, for a reader to drop */
int delta_layout_drops(struct delta_layout const *layout, unsigned kind);

/* the further options of a gametype that has none (an older host's) */
void delta_layout_extended_default(uint8_t *extended);
/* the further options as every machine takes them: a mode past 3 none, a
flag 0 or 1, a ranged byte outside its range its default, the reserved
bytes 0 */
void delta_layout_extended_sanitize(uint8_t *extended);

/* the record of one layout made another's. from: from_size bytes with
from_options bytes of options; to: to_size bytes with to_options. The head
(0x3320 bytes) is copied; the options' first bytes as many as both have;
options the source has none of are the further options' defaults (0x1C to
0x2B) and zeros past them; the local data is copied; the bytes after it
as many as both have, zeros past them. extended (may be NULL) gets the
source's further options, sanitized, or the defaults for a source without
them, so that a reader folding to an older layout keeps the mode. Returns
0, or -1 if a size is not a record of its options (shorter than the head,
options and local data, or past the limits) or the two buffers overlap:
the conversion is never in place. */
int delta_layout_convert(uint8_t const *from, size_t from_size, unsigned from_options, uint8_t *to,
	size_t to_size, unsigned to_options, uint8_t *extended);

/* the same, by layouts: from a host's version's record to the one a reader
of another version uses */
int delta_layout_convert_versions(uint8_t const *from, struct delta_layout const *from_layout, uint8_t *to,
	struct delta_layout const *to_layout, uint8_t *extended);

#endif
