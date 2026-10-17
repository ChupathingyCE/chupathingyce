/*
P2P_PROBE.H

Online Games' pings before joining (p2p_probe.c): a prober asks a listed
game's host through signalling (PROBE), the host answers with its addresses
(PROBE_ANSWER, signed), and the prober pings the host's tunnel socket with
small datagrams bound to the probe's id, which the host answers with the
same bytes. A probe is never a peer: no session, no slot, nothing the game
sees. docs/delta.md, "Pings before joining".

This unit is the part without sockets, clocks or keys: the formats, the
host's table of probes and the prober's, each given the time. p2p.c and
p2p_signal.c send and receive for it. Addresses and ports are in network
byte order, as p2p.h's.
*/

#ifndef __HALO_LINUX_P2P_PROBE_H
#define __HALO_LINUX_P2P_PROBE_H

enum
{
	/* the tunnel's is 0x69 (p2p.c) */
	P2P_PROBE_MAGIC = 0x6A,
	P2P_PROBE_ID_SIZE = 16,
	P2P_PROBE_HASH_SIZE = 16,
	P2P_PROBE_TOKEN_SIZE = 16,
	P2P_PROBE_KEY_SIZE = 32,
	P2P_PROBE_SIGNATURE_SIZE = 64,
	P2P_PROBE_MAXIMUM_ADDRESSES = 4,
	/* every probe datagram: magic, type, sequence, id, the prober's clock */
	P2P_PROBE_PACKET_SIZE = 1 + 1 + 2 + P2P_PROBE_ID_SIZE + 4,
	/* the signalling messages' version */
	P2P_PROBE_VERSION = 1,
	P2P_PROBE_MESSAGE_REQUEST = 'P',
	P2P_PROBE_MESSAGE_ANSWER = 'Q',
	/* the largest message: an answer with every address */
	P2P_PROBE_MAXIMUM_MESSAGE = 2 + P2P_PROBE_ID_SIZE + P2P_PROBE_KEY_SIZE + 1 + 6 * P2P_PROBE_MAXIMUM_ADDRESSES +
		P2P_PROBE_SIGNATURE_SIZE,

	/* the host's limits */
	P2P_PROBE_HOST_SLOTS = 16,
	P2P_PROBE_HOST_BURST = 8,
	P2P_PROBE_HOST_INTERVAL = 250,
	P2P_PROBE_HOST_LIFETIME = 10000,
	P2P_PROBE_HOST_PONGS = 8,
	P2P_PROBE_HOST_PUNCH_ROUNDS = 2,
	P2P_PROBE_HOST_PUNCH_INTERVAL = 250,
	P2P_PROBE_HOST_ANSWER_INTERVAL = 1000,

	/* the prober's */
	P2P_PROBE_TARGETS = 64,
	P2P_PROBE_IN_FLIGHT = 4,
	P2P_PROBE_REQUEST_INTERVAL = 1500,
	P2P_PROBE_REQUESTS = 3,
	P2P_PROBE_PING_INTERVAL = 200,
	P2P_PROBE_PONGS_WANTED = 3,
	P2P_PROBE_TIMEOUT = 5000,
	P2P_PROBE_REMEASURE = 60000,
	/* a target not asked about this long is forgotten */
	P2P_PROBE_FORGET = 30000,
};

enum
{
	P2P_PROBE_PING = 1,
	P2P_PROBE_PONG = 2,
	P2P_PROBE_PUNCH = 3,
};

struct p2p_probe_address
{
	unsigned long address;
	unsigned short port;
};

/* ---------- formats */

/* a probe datagram (P2P_PROBE_PACKET_SIZE bytes); returns its size */
int p2p_probe_put_packet(unsigned char *packet, int type, unsigned short sequence, const unsigned char *id,
	unsigned long clock);
/* the reverse: 1 if it is one (of a known type, exactly the size), else 0 */
int p2p_probe_get_packet(const unsigned char *packet, int size, int *type, unsigned short *sequence,
	unsigned char *id, unsigned long *clock);

/* PROBE: returns its size */
int p2p_probe_put_request(unsigned char *message, const unsigned char *id, const unsigned char *reply,
	const struct p2p_probe_address *addresses, int count);
/* 1 if message is a PROBE this version reads (longer ones are read for the
fields known), else 0 */
int p2p_probe_get_request(const unsigned char *message, int size, unsigned char *id, unsigned char *reply,
	struct p2p_probe_address *addresses, int *count);

/* PROBE_ANSWER without its signature: returns its size (the signature, of
p2p_probe_signed_text's text, goes after it) */
int p2p_probe_put_answer(unsigned char *message, const unsigned char *id, const unsigned char *key,
	const struct p2p_probe_address *addresses, int count);
/* the text a PROBE_ANSWER's signature is of: a label and the answer's first
size bytes; returns its size (text holds P2P_PROBE_MAXIMUM_MESSAGE + 32) */
int p2p_probe_signed_text(const unsigned char *message, int size, unsigned char *text);
/* 1 if message is a PROBE_ANSWER: its id, key and addresses, and where its
signature is (*signed_size: the bytes before it) */
int p2p_probe_get_answer(const unsigned char *message, int size, unsigned char *id, unsigned char *key,
	struct p2p_probe_address *addresses, int *count, int *signed_size);

/* ---------- the host */

struct p2p_probe_host_slot
{
	int used;
	unsigned char id[P2P_PROBE_ID_SIZE];
	struct p2p_probe_address addresses[P2P_PROBE_MAXIMUM_ADDRESSES];
	int count;
	unsigned long created;
	unsigned long answered;
	unsigned long punched;
	int punch_rounds;
	int pongs;
	int pinged;
};

struct p2p_probe_host
{
	struct p2p_probe_host_slot slots[P2P_PROBE_HOST_SLOTS];
	/* new probes allowed (P2P_PROBE_HOST_BURST at most, one more each
	P2P_PROBE_HOST_INTERVAL), as of when */
	int budget;
	unsigned long budget_time;
	int budget_started;
	/* counts, for the log (never an address) */
	unsigned long admitted, refused, pongs, dropped;
};

/* a PROBE heard: 1 if it is to be answered now (new and within the budget,
or the same id again and its answer is due), else 0 */
int p2p_probe_host_request(struct p2p_probe_host *host, unsigned long now, const unsigned char *id,
	const struct p2p_probe_address *addresses, int count);
/* a datagram with the magic heard: 1 if *answer (P2P_PROBE_PACKET_SIZE
bytes) is to go back to its source, else 0 */
int p2p_probe_host_packet(struct p2p_probe_host *host, unsigned long now, const unsigned char *packet, int size,
	unsigned char *answer);
/* the PUNCHes due now, into packets and to (at most maximum); returns
their count. Expired probes are let go */
int p2p_probe_host_due(struct p2p_probe_host *host, unsigned long now, unsigned char (*packets)[P2P_PROBE_PACKET_SIZE],
	struct p2p_probe_address *to, int maximum);

/* ---------- the prober */

enum
{
	P2P_PROBE_IDLE,
	/* PROBE sent, no answer yet */
	P2P_PROBE_ASKING,
	P2P_PROBE_PINGING,
	P2P_PROBE_MEASURED,
	P2P_PROBE_FAILED,
};

struct p2p_probe_target
{
	int used;
	unsigned char host_hash[P2P_PROBE_HASH_SIZE];
	unsigned char token[P2P_PROBE_TOKEN_SIZE];
	unsigned char id[P2P_PROBE_ID_SIZE];
	int state;
	unsigned long wanted_time;
	unsigned long started;
	unsigned long requested;
	int requests;
	unsigned long pinged;
	unsigned short sequence;
	struct p2p_probe_address addresses[P2P_PROBE_MAXIMUM_ADDRESSES];
	int count;
	int pongs;
	/* the lowest round trip of this measurement, and the last measurement's
	(-1: none) */
	long best;
	long ping;
	unsigned long measured;
};

struct p2p_probe_prober
{
	struct p2p_probe_target targets[P2P_PROBE_TARGETS];
	/* the reply topic's id, for the run */
	unsigned char reply[P2P_PROBE_ID_SIZE];
};

/* the target of a host hash and token, made if want (forgetting the one
asked about longest ago if the table is full): NULL if none */
struct p2p_probe_target *p2p_probe_target(struct p2p_probe_prober *prober, unsigned long now,
	const unsigned char *host_hash, const unsigned char *token, int want);
/* what the prober is to do now: a target to (re)send a PROBE for (its id
filled from random, 16 bytes, when it starts), returned with *action 1; or
the PINGs due, returned with *action 2 (packets and to, count in
*packet_count); NULL when nothing is due. Call until NULL. */
struct p2p_probe_target *p2p_probe_prober_next(struct p2p_probe_prober *prober, unsigned long now,
	const unsigned char *random, int *action, unsigned char (*packets)[P2P_PROBE_PACKET_SIZE],
	struct p2p_probe_address *to, int maximum, int *packet_count);
/* a PROBE_ANSWER whose signature and key were checked (p2p_probe.c's
caller), for the target with that id */
void p2p_probe_prober_answer(struct p2p_probe_prober *prober, unsigned long now, const unsigned char *id,
	const struct p2p_probe_address *addresses, int count);
/* a datagram with the magic from an address: 1 if it was a PONG of a
target's */
int p2p_probe_prober_packet(struct p2p_probe_prober *prober, unsigned long now, const unsigned char *packet,
	int size, unsigned long address, unsigned short port);

#endif
