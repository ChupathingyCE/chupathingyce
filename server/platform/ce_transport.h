/*
CE_TRANSPORT.H

Halo Custom Edition 1.0.10's join wire, on the GameSpy listing's UDP port
(gamespy_listing.c), off unless the server's settings turn it on
(ce_joins, server/docs/settings.md). Written from our own captures of
Custom Edition 1.0.10's client and dedicated server and from reading the
retail binaries' behavior; see ce_transport.c for the wire.

It answers a Custom Edition client's connection (the transport's
handshake, its key agreement and enciphering, its reliable messages), names
the server's map to it and reads its join request, which the game takes or
refuses (server/src/server_ce.c).

Built with the host's ABI (server_control.h's rule): only int and char
cross here.
*/

#ifndef CE_TRANSPORT_H
#define CE_TRANSPORT_H

enum
{
	/* Custom Edition clients at once, at most (Custom Edition's own game
	holds 16 players) */
	CE_MAXIMUM_PEERS = 16,
	CE_CHALLENGE_SIZE = 32,
	/* the 1.0.10 client's version, in its connect message */
	CE_CLIENT_VERSION = 616999,
	/* a payload, at most */
	CE_PACKET_SIZE = 1400,
	CE_TEXT_SIZE = 80,
};

/* why a join is refused: the client's own message for each (machine
rejected's reason, as Custom Edition's server sends it) */
enum
{
	CE_REJECT_UNKNOWN = 0,
	CE_REJECT_PROTOCOL = 1,
	CE_REJECT_PASSWORD = 2,
	CE_REJECT_FULL = 3,
	CE_REJECT_CDKEY = 4,
	CE_REJECT_BANNED = 6,
	CE_REJECT_KICKED = 7,
};

enum
{
	CE_EVENTS = 64,
	/* a client asks to join (the game answers: ce_transport_accept or
	ce_transport_refuse), has loaded the map, has gone */
	CE_EVENT_JOIN = 1,
	CE_EVENT_LOADED,
	CE_EVENT_LEFT,
};

struct ce_event
{
	int peer;
	int type;
	char name[13];
	/* the client's IPv4 address, host byte order */
	unsigned int address;
};

/* what a client's join request carries */
struct ce_join_request
{
	/* the request's fixed protocol text ("message in a bot" from 1.0.10) */
	char protocol[17];
	/* ASCII, any other character as '?' */
	char password[10];
	char name[13];
	/* the client's CD key hash: 32 hex digits */
	char cdkey_hash[33];
	/* the client's answer to the server's challenge (not checked) */
	unsigned int answer;
};

/* ---------- the socket's side (the listing's thread) */

/* sends one packet to the address (a struct sockaddr_in) */
typedef void (*ce_send_function)(void *context, const void *address, const unsigned char *packet, int length);
/* a line for the server's log (one %s, a) */
typedef void (*ce_notice_function)(const char *format, const char *a);

void ce_transport_start(ce_send_function send, void *context, ce_notice_function notice);
/* the server's name, the map it plays, as Custom Edition names it
(gamespy_map_name), and its most players */
void ce_transport_set_game(const char *name, const char *map, int maximum_players);
/* 1: the game answers each join (ce_transport_accept or
ce_transport_refuse); 0: each is refused at once */
void ce_transport_set_accept(int accept);
/* a packet from address (a struct sockaddr_in): 1 when it is Custom
Edition's join wire (handled), else 0 (the listing's to answer) */
int ce_transport_packet(const unsigned char *packet, int length, const void *address, unsigned int now_milliseconds);
/* resends and timeouts */
void ce_transport_update(unsigned int now_milliseconds);
/* every connection told it's closed, and forgotten */
void ce_transport_stop(void);
int ce_transport_peer_count(void);

/* ---------- the game's side (any thread) */

/* the next event: 1, else 0 */
int ce_transport_next_event(struct ce_event *event);
/* the join taken: its first payload (game settings and begin game); 0
when the join is gone (refused for waiting too long, or the client left) */
int ce_transport_accept(int peer, const unsigned char *payload, int length);
void ce_transport_refuse(int peer, int reason);
/* 1 while a join waits for the game's answer */
int ce_transport_awaiting(int peer);
/* a payload to a client in the game: 1 when sent */
int ce_transport_send(int peer, const unsigned char *payload, int length, int reliable);
/* the client's actions so far (struct ce_client_update, ce_messages.h):
0 when it isn't in the game, else 1 + the updates it has sent */
struct ce_client_update;
int ce_transport_client_update(int peer, struct ce_client_update *update);
/* the client's next update, in the order sent (each the whole state, what
it left out filled in): 1, else 0 */
int ce_transport_next_update(int peer, struct ce_client_update *update);
/* how many of the client's updates wait to be played */
int ce_transport_pending_updates(int peer);
/* the client told it's closed (a kick or the game leaving it) */
void ce_transport_drop(int peer);
/* a client in the game sent away with a reason (CE_REJECT_*: its game
shows why), closed once it has the reason */
void ce_transport_kick(int peer, int reason);

/* ---------- the parts the tests use */

/* a challenge meets the transport's rule (its characters' parities) */
int ce_challenge_valid(const unsigned char challenge[CE_CHALLENGE_SIZE]);
/* the answer to a challenge; positions 0 and 13 are any character */
void ce_challenge_response(const unsigned char challenge[CE_CHALLENGE_SIZE], unsigned char response[CE_CHALLENGE_SIZE]);
/* a response matches the expected one (positions 0 and 13 not compared) */
int ce_response_matches(const unsigned char expected[CE_CHALLENGE_SIZE], const unsigned char response[CE_CHALLENGE_SIZE]);
/* base to the power exponent, modulo 65537 */
unsigned int ce_power(unsigned int base, unsigned int exponent);
/* the cipher's key from the agreed secret */
void ce_key_from_secret(unsigned int secret, unsigned int key[4]);
/* the CRC added to each payload */
unsigned int ce_crc32(const unsigned char *data, int length);
/* a payload with its CRC, enciphered (length + 4 bytes into out): its
length, or 0 */
int ce_seal(const unsigned int key[4], const unsigned char *payload, int length, unsigned char *out, int size);
/* the payload of a sealed packet (its CRC checked and dropped): its
length, or -1 */
int ce_open(const unsigned int key[4], const unsigned char *data, int length, unsigned char *out, int size);
/* the messages of a payload (its game messages, not its object updates):
each message's bytes (version first, type last) to visit; the count, or -1
when the payload is malformed */
int ce_payload_messages(const unsigned char *payload, int length,
	void (*visit)(void *context, const unsigned char *message, int message_length), void *context);
/* one message as a payload: its length, or 0 */
int ce_payload_from_message(const unsigned char *message, int message_length, unsigned char *out, int size);
/* the server's first message (the challenge and the map's name): its
length, or 0 */
int ce_message_new_client_challenge(const char *challenge, const char *map, unsigned char *out, int size);
/* the join refused, with a reason (CE_REJECT_*): its length */
int ce_message_machine_rejected(int reason, unsigned char *out, int size);
/* a join request's message (type 14): 1 and its fields, else 0 */
int ce_parse_join_request(const unsigned char *message, int length, struct ce_join_request *request);

#endif
