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

/* a host's advertisement: its reserved bytes (size of them) filled from the
Delta major on (DELTA_ADVERTISED_WIRE_OFFSET): the major, the old slot's wire
(the oldest compatible wire from DELTA_WIRE_FIRST_ADVERTISED on, which a
client that compares the slot for an exact match knows) and the range bytes
(the host's own wire and its oldest). Zeros if size is too small or wire is
no ID. */
void delta_compat_advertise(unsigned char *reserved, int size, int major, const char *wire);

/* the range bytes alone: what they say, if they are right (the layout
version, the check byte, IDs, oldest not past the wire). 1 if so. */
int delta_compat_read_range(const unsigned char *reserved, int size, struct delta_wire_id *wire,
	struct delta_wire_id *oldest);

/* what a client of the wire (own) decides about a host's advertisement, of
the Delta major:
  1  join: the host's own wire is in the client's range (the client's oldest
     to its own), or the client's wire is in the host's, or, without range
     bytes that say so, the old slot's wire is the client's own;
  0  refuse: the host plays a wire this one does not (theirs is that wire,
     printable, ended; the host's own wire if it says it, else its slot's);
 -1  the advertisement does not say (no major or wire: the host is not
     Delta, is of another major, or is older than the wire's advertisement):
     the OpenCE numbers decide.
Whether the host or the client speaks Delta at all is the caller's. */
int delta_compat_decide(const unsigned char *reserved, int size, int major, const char *own, char *theirs,
	int theirs_size);

/* the wire a host plays, as its advertisement says: the range bytes' own
wire, else the old slot's. 1 if it says. */
int delta_compat_host_wire(const unsigned char *reserved, int size, int major, struct delta_wire_id *wire);

#endif
