/*
DELTA_COMPAT.H

Wire compatibility ranges (docs/delta.md, "Wire compatibility ranges"): which
game wires play together, how a host says so in its advertisement, and how a
client decides from it. No dependency but the C library, so the unit tests
(tools/test_delta_compat.py) and the ports build it alone; plain C89.
*/

#ifndef HALO_DELTA_COMPAT_H
#define HALO_DELTA_COMPAT_H

/* a wire ID, "chupa-32a": its number and its letter. They order by number,
then letter. */
struct delta_wire_id
{
	int number;
	int letter;
};

/* the ID in text of length bytes (not ended): 1 if it is one, else 0 */
int delta_wire_parse(const char *text, int length, struct delta_wire_id *id);
/* -1, 0 or 1: the older, the same, the newer of a and b */
int delta_wire_compare(const struct delta_wire_id *a, const struct delta_wire_id *b);
/* the ID as text ("chupa-32a") into text, ended (size at least 16) */
void delta_wire_format(const struct delta_wire_id *id, char *text, int size);

/* the oldest wire the wire plays the game with (DELTA_WIRE_COMPATIBILITY in
delta.h): its own for a wire with no row. 1 if wire is a wire ID. */
int delta_compat_oldest(const char *wire, struct delta_wire_id *oldest);

/* a host's advertisement: its reserved bytes (size of them, at least
DELTA_ADVERTISED_RESERVED_SIZE) given the Delta tag at their tail
(DELTA_ADVERTISED_TAG_OFFSET): the major, the host's own wire and its oldest
compatible wire, and the check. The bytes before the tag (OpenCE's: the
version, the flags, the kind) are left alone. The tag's bytes zero if size is
too small or wire is no ID. */
void delta_compat_advertise(unsigned char *reserved, int size, int major, const char *wire);

/* ... and the old layout's major and slot (DELTA_ADVERTISED_WIRE_OFFSET),
for released clients, in the bytes between OpenCE's kind and the tag: the
major at byte 3 (the kind's byte, so only where OpenCE does not read it), then
the oldest compatible wire from DELTA_WIRE_FIRST_ADVERTISED on, which an
exact-match client knows, ended before the tag, which a released client
takes as the host's wire (the tag's check is not the old range bytes'). The
flag 0x04, which those clients also want, is the
caller's. Bytes 3 to 16 zero if size is too small or wire is no ID. */
void delta_compat_advertise_old(unsigned char *reserved, int size, int major, const char *wire);

/* the tag alone: what it says (the major, the host's wire and its oldest),
if it is right (the two tag bytes, the layout version, the check byte, IDs,
oldest not past the wire). 1 if so. */
int delta_compat_read_tag(const unsigned char *reserved, int size, int *major, struct delta_wire_id *wire,
	struct delta_wire_id *oldest);

/* the old layout's range bytes alone (DELTA_ADVERTISED_RANGE_OFFSET, hosts
before the tag): what they say, if they are right (the layout version, the
check byte, IDs, oldest not past the wire). 1 if so. */
int delta_compat_read_range(const unsigned char *reserved, int size, struct delta_wire_id *wire,
	struct delta_wire_id *oldest);

/* whether the host of an advertisement speaks Delta Peer, so that a client
says HELLO: a valid tag (of any major: another major answers LEGACY), or the
old layout's flag, which from DELTA_ADVERTISED_FLAG_TAKEN_VERSION on counts
only with a wire in the old slot (else it is OpenCE's dedicated flag) */
int delta_compat_speaks_delta(const unsigned char *reserved, int size);

/* what a client of the wire (own) decides about a host's advertisement, of
the Delta major, by its tag, else by the old layout:
  1  join: the host's own wire is in the client's range (the client's oldest
     to its own), or the client's wire is in the host's, or (the old layout),
     without range bytes that say so, the old slot's wire is the client's own;
  0  refuse: the host plays a wire this one does not (theirs is that wire,
     printable, ended; the host's own wire if it says it, else its slot's);
 -1  the advertisement does not say (no tag and no old layout, another
     major, a host older than the wire's advertisement): the OpenCE numbers
     decide.
Whether the client speaks Delta at all (network.protocol) is the caller's. */
int delta_compat_decide(const unsigned char *reserved, int size, int major, const char *own, char *theirs,
	int theirs_size);

/* the wire a host plays, as its advertisement says: the tag's, else the old
layout's range bytes' own wire, else its old slot's. 1 if it says. */
int delta_compat_host_wire(const unsigned char *reserved, int size, int major, struct delta_wire_id *wire);

/* An OpenCE dedicated server, told apart for a neutral label in the lists
(HOST_LABEL_OPENCE_SERVER, halo_join_reasons.h). A label only: it grants
nothing and joining does not read it. */
/* by its advertisement: it announces DELTA_ADVERTISED_FLAG_TAKEN_VERSION (39)
or later with the dedicated flag (0x04 at DELTA_ADVERTISED_FLAGS_OFFSET) and
does not speak Delta (delta_compat_speaks_delta: no tag, no old layout). Our
own dedicated servers carry the tag while they host with Delta, so they never
are (one with Delta off reads as OpenCE's here; its listing tells it apart).
1 if so. */
int delta_compat_opence_server(const unsigned char *reserved, int size);
/* by its public listing's flags (p2p_lobby.c): OpenCE's dedicated bit (32,
from its version 39) without ours (128), which every Chupathingy dedicated
server sets beside it. 1 if so. */
#define DELTA_COMPAT_LISTING_DEDICATED_OPENCE 32
#define DELTA_COMPAT_LISTING_DEDICATED 128
int delta_compat_opence_server_listing(int flags);

/* The join mark. A client that will say HELLO to its Delta host marks its
join request (the game's message_client_join_game_request, as it is) in the
last free characters of its machine name, past the name's end: a name of
count 16-bit characters ended within its first count - 3 (the mark is two
characters, then the field's last, 0). Only a Delta host is sent it, and a
host that reads it waits for the HELLO of a marked machine alone; OpenCE's
machines never mark. */
#define DELTA_COMPAT_JOIN_MARK_FIRST 0x0394
#define DELTA_COMPAT_JOIN_MARK_SECOND 0x0001
/* the mark into name (a name too long for it is left unmarked): 1 if
marked */
int delta_compat_join_mark(unsigned short *name, int count);
/* whether a received name has the mark; what follows the name's end is
cleared either way (a name not ended within count is left as it is) */
int delta_compat_join_marked(unsigned short *name, int count);

#endif
