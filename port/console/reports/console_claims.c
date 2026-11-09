/*
CONSOLE_CLAIMS.C

A console's claim of its own line in each online game it played
(console_claims.h): the pure parts first (the name, the message, the
body), then the console's (the claim handed over at the game's end, and
its thread).
*/

#include "console_claims.h"

#include "port/console/link/console_link.h"
#include "port/linux/src/game_report.h"

#include <stdio.h>
#include <string.h>

/* (port/linux/src/p2p_crypto.c's: Monocypher's Ed25519 and a SHA-256) */
void p2p_sha256(const void *data, int size, unsigned char *digest);
void p2p_ed25519_public(const unsigned char *seed, unsigned char *public_key, unsigned char *x25519_secret);
void p2p_ed25519_sign(const unsigned char *seed, const unsigned char *public_key, const void *message, int size,
	unsigned char *signature);

#define PLAYER_ID_LABEL "halo-ce-universal player id\n"
#define MODERATOR_LABEL "halo-ce-universal moderator key\n"
#define KEY_SIZE 32
#define PLAYER_ID_SIZE 16

static void wipe(void *data, unsigned long size)
{
	volatile unsigned char *bytes = (volatile unsigned char *)data;

	while (size--)
		*bytes++ = 0;
}

/* ---------- the pure parts */

int console_claims_name_hex(const unsigned short *name, int length, char *text, int size)
{
	char utf8[64];
	int index, count;

	game_report_utf8(name, length, utf8, (int)sizeof(utf8));
	count = (int)strlen(utf8);
	if (!count || 2 * count + 1 > size)
		return 0;
	for (index = 0; index < count; index++)
		snprintf(text + 2 * index, 3, "%02x", (unsigned char)utf8[index]);
	return 2 * count;
}

int console_claims_map(const char *path, char *text)
{
	const char *start = path, *cursor;
	int length = 0;

	if (!path)
		return 0;
	for (cursor = path; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			start = cursor + 1;
	}
	for (cursor = start; *cursor; cursor++)
	{
		unsigned char character = (unsigned char)*cursor;

		if (character <= ' ' || character > '~' || character == '"' || length >= CONSOLE_CLAIMS_MAP_SIZE - 1)
			return 0;
		text[length++] = (char)character;
	}
	text[length] = 0;
	return length > 0;
}

int console_claims_message(const struct console_claim *claim, const char *identity, const char *platform,
	const char *challenge, int ago, char *text, int size)
{
	int length = snprintf(text, (size_t)size, "%s%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%d\n%d\n%d\n%d\n%d\n%d\n",
		CONSOLE_CLAIMS_LABEL, identity, platform, challenge, claim->hosted ? "host" : "joined", claim->invite,
		claim->map, claim->host_hex, claim->name_hex, claim->team, claim->score, claim->kills, claim->deaths,
		claim->duration, ago);

	return length > 0 && length < size ? length : 0;
}

int console_claims_body(const struct console_claim *claim, const char *identity, const char *platform,
	const char *challenge, int ago, const char *signature_hex, char *text, int size)
{
	const char *member;
	int length;

	if (!strncmp(identity, "key:", 4))
		member = "console_key";
	else if (!strncmp(identity, "player:", 7))
		member = "player";
	else
		return 0;
	length = snprintf(text, (size_t)size,
		"{\"%s\": \"%s\", \"platform\": \"%s\", \"challenge\": \"%s\", \"role\": \"%s\", \"invite\": \"%s\", "
		"\"map\": \"%s\", \"host\": \"%s\", \"name\": \"%s\", \"team\": %d, \"score\": %d, \"kills\": %d, "
		"\"deaths\": %d, \"duration\": %d, \"ago\": %d, \"signature\": \"%s\"}",
		member, strchr(identity, ':') + 1, platform, challenge, claim->hosted ? "host" : "joined", claim->invite,
		claim->map, claim->host_hex, claim->name_hex, claim->team, claim->score, claim->kills, claim->deaths,
		claim->duration, ago, signature_hex);
	return length > 0 && length < size ? length : 0;
}

/* the SHA-256 of label and the 32-byte key */
static void key_digest(const char *label, const unsigned char *key, unsigned char *digest)
{
	unsigned char data[40 + KEY_SIZE];
	int length = (int)strlen(label);

	memcpy(data, label, (size_t)length);
	memcpy(data + length, key, KEY_SIZE);
	p2p_sha256(data, length + KEY_SIZE, digest);
	wipe(data, sizeof(data));
}

void console_claims_player_identity(const unsigned char *key, char *identity)
{
	unsigned char digest[32];

	key_digest(PLAYER_ID_LABEL, key, digest);
	memcpy(identity, "player:", 7);
	console_link_hex(digest, PLAYER_ID_SIZE, identity + 7);
	wipe(digest, sizeof(digest));
}

void console_claims_player_sign(const unsigned char *key, const void *message, int size, char *signature_hex)
{
	unsigned char seed[32], public_key[32], signature[64];

	key_digest(MODERATOR_LABEL, key, seed);
	p2p_ed25519_public(seed, public_key, NULL);
	p2p_ed25519_sign(seed, public_key, message, size, signature);
	console_link_hex(signature, 64, signature_hex);
	wipe(seed, sizeof(seed));
	wipe(signature, sizeof(signature));
}

#ifndef CONSOLE_CLAIMS_PURE

/* ---------- on the console */

/* the settings' (port/linux/src/port_config.c) */
int config_boolean(const char *name);

static const struct console_claims_port *port;
/* the claim under way (its thread clears claiming last), so one at a time:
the main thread fills it only while claiming is clear */
static struct console_claim pending;
static volatile int claiming;

static void log_line(const char *text)
{
	if (port && port->write_log)
		port->write_log(text);
}

/* this console's identity and the message's signature: 1, or 0 (none) */
static int sign(char *identity, char *message, int length_limit, const char *challenge, int ago, int *length,
	char *signature)
{
	unsigned char key[KEY_SIZE];

	if (port->player_key && port->player_key(key))
	{
		console_claims_player_identity(key, identity);
		*length = console_claims_message(&pending, identity, port->platform, challenge, ago, message, length_limit);
		if (*length)
			console_claims_player_sign(key, message, *length, signature);
		wipe(key, sizeof(key));
		return *length > 0;
	}
	memcpy(identity, "key:", 4);
	if (!console_link_linked_key(identity + 4))
		return 0;
	*length = console_claims_message(&pending, identity, port->platform, challenge, ago, message, length_limit);
	return *length && console_link_sign_linked(message, *length, signature);
}

/* one try: the site's status (0 for no answer), its answer's first line in reply */
static int claim_once(char *reply)
{
	char challenge[33], identity[CONSOLE_CLAIMS_IDENTITY_SIZE], signature[129];
	char message[CONSOLE_CLAIMS_MESSAGE_SIZE], body[CONSOLE_CLAIMS_BODY_SIZE];
	int status = 0, length = 0, ago;

	reply[0] = 0;
	if (!port->request(CONSOLE_CLAIMS_CHALLENGE_PATH, NULL, &status, reply, CONSOLE_CLAIMS_REPLY_SIZE))
		return 0;
	if (status != 200)
		return status;
	/* (an answer that is not a challenge: as no answer) */
	if (!console_link_parse_challenge(reply, challenge))
		return 0;
	ago = (int)((port->milliseconds() - pending.ended) / 1000);
	if (!sign(identity, message, (int)sizeof(message), challenge, ago, &length, signature))
	{
		/* (unlinked meanwhile: nothing to send, nor to try again) */
		snprintf(reply, CONSOLE_CLAIMS_REPLY_SIZE, "not linked");
		return 403;
	}
	length = console_claims_body(&pending, identity, port->platform, challenge, ago, signature, body, (int)sizeof(body));
	wipe(signature, sizeof(signature));
	reply[0] = 0;
	if (!length || !port->request(CONSOLE_CLAIMS_PATH, body, &status, reply, CONSOLE_CLAIMS_REPLY_SIZE))
		return 0;
	reply[strcspn(reply, "\r\n")] = 0;
	return status;
}

static void claim_thread(void *context)
{
	char reply[CONSOLE_CLAIMS_REPLY_SIZE], line[160];
	int attempt, status = 0;

	(void)context;
	for (attempt = 1; attempt <= CONSOLE_CLAIMS_ATTEMPTS; attempt++)
	{
		if (port->sleep)
			port->sleep(attempt == 1 ? CONSOLE_CLAIMS_FIRST_WAIT : CONSOLE_CLAIMS_RETRY_WAIT);
		status = claim_once(reply);
		/* (again for no answer, a busy site, or a game whose reports are not
		in yet) */
		if (status && status != 404 && status != 429 && status < 500)
			break;
	}
	if (status == 200)
		snprintf(line, sizeof(line), "Delta profile: the game was claimed for the linked profile (%.48s)", reply);
	else if (status == 404)
		/* (no report of it reached the site: a System Link game, or one whose
		players don't report) */
		snprintf(line, sizeof(line), "Delta profile: the site has no report of the game, so it was not claimed");
	else if (status)
		snprintf(line, sizeof(line), "Delta profile: the game was not claimed (%d %.64s)", status, reply);
	else
		snprintf(line, sizeof(line), "Delta profile: the game could not be claimed (no answer)");
	log_line(line);
	claiming = 0;
}

void console_claims_initialize(const struct console_claims_port *the_port)
{
	port = the_port;
}

int console_claims_game_ended(int hosted, const char *invite, const char *map, const unsigned short *host_name,
	int host_length, const unsigned short *player_name, int name_length, int team, int score, int kills, int deaths,
	int duration_seconds)
{
	unsigned char id[CONSOLE_LINK_ID_SIZE], key[KEY_SIZE];
	int identity;

	/* (the player's choice first: off sends nothing, nor asks anything) */
	if (!config_boolean("network.report_joined_games"))
		return 0;
	if (!port)
		console_claims_port_start();
	if (!port || claiming)
		return 0;
	/* (a console with no identity file and no link: silent) */
	identity = port->player_key && port->player_key(key);
	wipe(key, sizeof(key));
	if (!identity && !console_link_player_id(id))
		return 0;
	memset(&pending, 0, sizeof(pending));
	pending.hosted = hosted != 0;
	if (!hosted && invite && strlen(invite) == CONSOLE_CLAIMS_INVITE_SIZE - 1)
		memcpy(pending.invite, invite, CONSOLE_CLAIMS_INVITE_SIZE);
	if (!console_claims_map(map, pending.map) ||
		!console_claims_name_hex(host_name, host_length, pending.host_hex, CONSOLE_CLAIMS_HOST_SIZE) ||
		!console_claims_name_hex(player_name, name_length, pending.name_hex, CONSOLE_CLAIMS_NAME_SIZE))
	{
		return 0;
	}
	pending.team = team;
	pending.score = score;
	pending.kills = kills;
	pending.deaths = deaths;
	pending.duration = duration_seconds;
	pending.ended = port->milliseconds();
	claiming = 1;
	if (!port->start_thread(claim_thread, NULL))
	{
		log_line("Delta profile: no thread to claim the game");
		claiming = 0;
		return 0;
	}
	return 1;
}

#endif
