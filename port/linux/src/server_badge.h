/*
SERVER_BADGE.H

Server badges (docs/server-badges.md): a certificate the badge key
(server_badge_key.h) signs for one dedicated server's key, its link key to
the site (Delta Control), with a tier (official, developer, community) and
an optional label. The list shows a badge only for a server that proved
the key when it announced its game; a joining game asks the server over
Delta Peer (BADGE_ASK) and checks the certificate and the server's
signature of its own nonce here, dropping the badge if either fails.

Plain C with no platform dependency: the game, the dedicated server and the
tests build it as it is. Every reader takes its bytes as hostile.
*/

#ifndef SERVER_BADGE_H
#define SERVER_BADGE_H

enum
{
	/* magic (4), format (1), tier (1), label length (1), reserved (1),
	issued (4), expires (4), server key (32) */
	SERVER_BADGE_HEADER_SIZE = 48,
	SERVER_BADGE_KEY_SIZE = 32,
	SERVER_BADGE_SIGNATURE_SIZE = 64,
	SERVER_BADGE_LABEL_SIZE = 31,
	SERVER_BADGE_NONCE_SIZE = 32,
	/* an invite as the list and the game spell it (hexadecimal) */
	SERVER_BADGE_INVITE_SIZE = 64,
	/* the hosts whose proof a game remembers (server_badge_joined_set) */
	SERVER_BADGE_JOINED_COUNT = 8,
	/* a whole certificate: 112 bytes and its label */
	SERVER_BADGE_MINIMUM_SIZE = SERVER_BADGE_HEADER_SIZE + SERVER_BADGE_SIGNATURE_SIZE,
	SERVER_BADGE_MAXIMUM_SIZE = SERVER_BADGE_MINIMUM_SIZE + SERVER_BADGE_LABEL_SIZE,
	SERVER_BADGE_FORMAT = 1,
	/* the most an announcement's or a proof's signed bytes are */
	SERVER_BADGE_MESSAGE_SIZE = 192
};

/* a certificate's tier; a number is never reused. One this build does not
know is read (the certificate is well formed) and shows no badge */
enum server_badge_tier
{
	_server_badge_tier_none = 0,
	_server_badge_tier_official = 1,
	_server_badge_tier_developer = 2,
	_server_badge_tier_community = 3,

	NUMBER_OF_SERVER_BADGE_TIERS
};

struct server_badge
{
	/* as the certificate says (a tier this build does not know included) */
	int tier;
	unsigned int issued;
	unsigned int expires;
	unsigned char server_key[SERVER_BADGE_KEY_SIZE];
	/* printable ASCII, empty for none */
	char label[SERVER_BADGE_LABEL_SIZE + 1];
};

/* a certificate's fields: 1 if it is a well-formed certificate of format 1
(magic, size, label, times), whatever its tier; its signature is not checked */
int server_badge_parse(const unsigned char *certificate, int size, struct server_badge *badge);
/* whether a certificate is signed by one of count keys (32 bytes each; a
key of all zeros is none) */
int server_badge_signed(const unsigned char *certificate, int size, const unsigned char (*keys)[32], int count);
/* a certificate as the game takes it: well formed, signed by a key of
server_badge_key.h, of a tier this build knows, and good at now (Unix
seconds; 0, a clock not known: not checked). 1, and its fields in badge */
int server_badge_check(const unsigned char *certificate, int size, unsigned long long now, struct server_badge *badge);
/* the same, saying why a certificate is not taken (the panel's badge state):
enum server_badge_verdict, its fields in badge from _server_badge_not_signed
on */
enum server_badge_verdict
{
	_server_badge_malformed = 0,
	/* not a badge key's of this build (or the build has none) */
	_server_badge_not_signed,
	_server_badge_unknown_tier,
	_server_badge_expired,
	_server_badge_not_yet,
	_server_badge_good
};
int server_badge_verdict(const unsigned char *certificate, int size, unsigned long long now, struct server_badge *badge);
/* whether this build has a badge key at all (server_badge_key.h not all
zeros): without one it shows no badge */
int server_badge_has_keys(void);

/* the bytes the server's key signs, into message (at least
SERVER_BADGE_MESSAGE_SIZE): its proof to a joining game, of the game's
nonce, the invite the game joined and the Delta Peer session between them
(so a proof made for one host's game is no proof for another's); its
announcement of a game, of the invite and the time (Unix seconds). Their
length (0: no invite, or one that does not fit). The invite is taken in
any case and signed in lowercase */
int server_badge_proof_message(unsigned char *message, const unsigned char *nonce, const char *invite,
	unsigned int session);
int server_badge_announce_message(unsigned char *message, const char *invite, unsigned long long time);
/* whether a signature is the certificate's server key's, of message (never
for a key of small order) */
int server_badge_signature_valid(const struct server_badge *badge, const unsigned char *message, int size,
	const unsigned char *signature);

/* a tier's name as the list spells it ("official"; "" for none or one this
build does not know), and a name's tier (_server_badge_tier_none for none) */
const char *server_badge_tier_name(int tier);
int server_badge_tier_number(const char *name);

/* a certificate from its file's text (lowercase or uppercase hexadecimal on
one line; lines starting with '#' and blank lines skipped): its size, 0 if
there is none */
int server_badge_from_text(const char *text, unsigned char *certificate, int size);

/* ---------- what a game made of the hosts it joined

The list's badge is the site's word; a game that joins the host checks it
itself (BADGE_ASK). The last SERVER_BADGE_JOINED_COUNT hosts' outcomes are
kept by invite: a host that proved no badge shows none, and one that
proved a badge shows the list's only while the list says the same tier,
label and server key. */

struct server_badge_joined
{
	char invite[SERVER_BADGE_INVITE_SIZE + 1];
	/* 1 proven (badge), -1 none proven, 0 an empty entry */
	int result;
	struct server_badge badge;
	unsigned int order;
};

struct server_badge_joined_set
{
	struct server_badge_joined entries[SERVER_BADGE_JOINED_COUNT];
	unsigned int next_order;
};

/* a joined host's outcome (proven NULL: it proved none), replacing what was
kept for its invite, else the oldest entry */
void server_badge_joined_note(struct server_badge_joined_set *set, const char *invite,
	const struct server_badge *proven);
/* whether a listed badge (tier, label, and the server key the list names;
NULL for a list that names none) may be shown: 1 for a host not joined, or
one that proved the same badge */
int server_badge_joined_allows(const struct server_badge_joined_set *set, const char *invite, int tier,
	const char *label, const unsigned char *listed_key);

#endif
