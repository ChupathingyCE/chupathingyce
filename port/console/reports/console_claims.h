/*
CONSOLE_CLAIMS.H

A console's claim of its own line in each online game it played, hosted or
joined (HALO_CONSOLE_GAME_CLAIMS, which a port sets beside
HALO_CONSOLE_GAME_REPORTS), so the game counts for the Delta profile the
console is linked to. The site's POST /v1/console/claim (puma's
console_link.py has the formats).

Why: a console that hosts sends no carnage report, so its line in the game
its players reported is only a name. A joined game's report
(console_reports.c) can also go missing (no memory for TLS, one still on
its way). The claim is small and needs no TLS, so it covers both.

How, over the console list's plain HTTP (nothing secret travels):
1. GET /v1/claim_challenge: a one-use challenge.
2. POST /v1/console/claim: the line's name and numbers (team, score, kills,
   deaths), the game's invite (a joined game's, else empty), map, host
   name and length, how many seconds ago it ended by the console's own
   count (no clock needed), the role (host or joined), and an Ed25519
   signature over "chupathingy console claim v1\n" and each field, each
   with a line break (console_claims_message). Names go as their UTF-8 in
   hex, so the message is plain ASCII on any console.
The site finds the one game and the one line that fit. A 404 means the
game's reports aren't in yet: the console asks again a few times.

Who signs, as the reports choose (console_profile_id):
- an identity file (the port's player key): its player ID, signed by the
  moderator key the desktop builds derive from the player key;
- else the console's own link (port/console/link): its device key;
- else nothing at all is sent, and nothing is logged.

network.report_joined_games = false (config.toml) sends no claim either.

Memory: one claim at a time, its fields in a small static block (under
400 bytes). The request's buffers (about 2 KB) are on the port's
short-lived thread's stack, given back when it ends. Nothing big is
allocated, the main thread never waits, and a failure is one line in the
log. Bytes and text only, so the same on big- and little-endian consoles.

Plain C over the port's hooks; port/console/tests/console_claims_test.c
builds it on the host (tools/test_console_claims.py).
*/

#ifndef __CONSOLE_CLAIMS_H
#define __CONSOLE_CLAIMS_H

#define CONSOLE_CLAIMS_PATH "/v1/console/claim"
#define CONSOLE_CLAIMS_CHALLENGE_PATH "/v1/claim_challenge"
#define CONSOLE_CLAIMS_LABEL "chupathingy console claim v1\n"
/* the first try this long after the game ends (its players' reports reach
the site first), then again after a while for a game not found yet */
#define CONSOLE_CLAIMS_FIRST_WAIT 15000
#define CONSOLE_CLAIMS_RETRY_WAIT 20000
#define CONSOLE_CLAIMS_ATTEMPTS 4
/* the fields' sizes, with their terminators: an invite (64 hex digits), a
map's name, the host's name (16 characters) and the player's (11) as
UTF-8 in hex */
#define CONSOLE_CLAIMS_INVITE_SIZE 65
#define CONSOLE_CLAIMS_MAP_SIZE 65
#define CONSOLE_CLAIMS_HOST_SIZE 97
#define CONSOLE_CLAIMS_NAME_SIZE 73
/* the identity ("key:" and 64 hex digits, or "player:" and 32) */
#define CONSOLE_CLAIMS_IDENTITY_SIZE 72
/* the message and the body, and the site's answer, headers and all */
#define CONSOLE_CLAIMS_MESSAGE_SIZE 512
#define CONSOLE_CLAIMS_BODY_SIZE 768
#define CONSOLE_CLAIMS_REPLY_SIZE 512

/* one claim's fields */
struct console_claim
{
	/* 1 hosted, 0 joined */
	int hosted;
	char invite[CONSOLE_CLAIMS_INVITE_SIZE];
	char map[CONSOLE_CLAIMS_MAP_SIZE];
	char host_hex[CONSOLE_CLAIMS_HOST_SIZE];
	char name_hex[CONSOLE_CLAIMS_NAME_SIZE];
	int team, score, kills, deaths;
	int duration;
	/* when it ended (the port's milliseconds) */
	unsigned long ended;
};

struct console_claims_port
{
	/* the Delta platform name ("xbox") */
	const char *platform;
	/* one plain HTTP request to the console list's host: path, a JSON body
	(NULL for a GET); the answer's status and body (NUL terminated, cut to
	reply_size - 1). 1 if an answer came. Blocks: called on the thread
	start_thread started, never at the same time as another of its own */
	int (*request)(const char *path, const char *body, int *status, char *reply, int reply_size);
	/* runs function(context) on a thread of its own with a few KB of stack
	free, which ends when it returns: 1, or 0 if it could not */
	int (*start_thread)(void (*function)(void *context), void *context);
	void (*sleep)(unsigned long milliseconds);
	unsigned long (*milliseconds)(void);
	/* the identity file's player key: 1 and its 32 bytes (the caller wipes
	them), or 0 for none; NULL for no identity file on this console */
	int (*player_key)(unsigned char *key);
	/* one line for the log (never a key, a signature, a body or an address) */
	void (*write_log)(const char *text);
};

/* the port's hooks (kept, not copied) */
void console_claims_initialize(const struct console_claims_port *port);
/* the port's: calls console_claims_initialize when the first claim comes */
void console_claims_port_start(void);

/* game_stats.c's, once a game this console hosted or joined is over: the
claim of its first local player's line (name and host as UTF-16, the
game's), sent a while later on a thread of the port's. 1 if it is on its
way, 0 if not (claims off, no identity, one still under way, no thread) */
int console_claims_game_ended(int hosted, const char *invite, const char *map, const unsigned short *host_name,
	int host_length, const unsigned short *player_name, int name_length, int team, int score, int kills, int deaths,
	int duration_seconds);

/* ---------- the pure parts (console_claims_test.c) */

/* a name (UTF-16, up to length characters or a 0) as its UTF-8 in hex into
text (size bytes): its length, or 0 for an empty name or one that does not fit */
int console_claims_name_hex(const unsigned short *name, int length, char *text, int size);
/* a map's name as the claim gives it: the last part of its path, printable
ASCII without quotes or backslashes, into text (CONSOLE_CLAIMS_MAP_SIZE):
1, or 0 if it is not one */
int console_claims_map(const char *path, char *text);
/* the signed message: its length, or 0 if it does not fit */
int console_claims_message(const struct console_claim *claim, const char *identity, const char *platform,
	const char *challenge, int ago, char *text, int size);
/* the request's body (identity "key:..." gives "console_key", "player:..."
gives "player"): its length, or 0 if it does not fit */
int console_claims_body(const struct console_claim *claim, const char *identity, const char *platform,
	const char *challenge, int ago, const char *signature_hex, char *text, int size);
/* an identity file's player key: its identity ("player:" and its player ID)
and a message's signature by its moderator key (128 hex digits and a
terminator); the derived keys are wiped inside */
void console_claims_player_identity(const unsigned char *key, char *identity);
void console_claims_player_sign(const unsigned char *key, const void *message, int size, char *signature_hex);

#endif
