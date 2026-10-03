/*
BROWSER.C

The game list (browser.h; configure.py
--game-browser): hosted system link games announced to the list server
(network.browser_url: halo.milenko.org, the community's) with their invites, and the
server's list for System Link to show.

Hosting: the game's server reports its game each frame
(browser_host_update). While p2p.c hosts it on the internet (it has an
invite) and network.list_hosted_games is on, the game is announced every
ANNOUNCE_INTERVAL, or a little after it changes (players join, the map
changes), and withdrawn when the reports stop or p2p.c stops hosting. The
server forgets a game it is not told about for a while, so a copy of the
game that quits without withdrawing drops off by itself.

Browsing: browser_get_games asks for the list when the last one is more
than LIST_INTERVAL old, and returns what it has meanwhile. Only games of
this machine's network version are kept (the others could not be joined),
and never this machine's own.

The requests (posix_browser.c) block, so they are made on a thread of this
file's, which the first call starts; the game's threads only exchange state
with it under the lock.
*/

#ifdef HALO_GAME_BROWSER

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "browser_http.h"
#include "p2p.h"
#include "p2p_internal.h"
#include "browser.h"
#include "qrcodegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL_stdinc.h>

enum
{
	ANNOUNCE_INTERVAL = 20000,
	/* (a change announced this soon after the last announcement at most) */
	CHANGE_INTERVAL = 3000,
	/* the game's server stopped reporting: it no longer hosts */
	HOST_TIMEOUT = 3000,
	LIST_INTERVAL = 5000,
	/* (asked again this long after a failure) */
	RETRY_INTERVAL = 15000,
	THREAD_INTERVAL = 250,
	RESPONSE_SIZE = 32768,
	/* the player key (game_list_player.key in the save root) */
	PLAYER_KEY_SIZE = 32,
	/* the public player ID: the first bytes of the key's hash */
	PLAYER_ID_SIZE = 16,
	/* a finished game's lines confirmed: the first try this long after the
	game ends (its host's report first), then again while the server has
	no report yet */
	CLAIM_DELAY = 4000,
	CLAIM_INTERVAL = 8000,
	CLAIM_ATTEMPTS = 8,
	MAXIMUM_CLAIM_NAMES = 4,
	/* Quick Connect: the server asked whether its code was typed this often,
	and a code given up on this long after it runs out (if the server could
	not say so) */
	CONNECT_POLL_INTERVAL = 3000,
	CONNECT_GRACE = 10000,
	/* (a confirm question's time, as the server keeps it; an answer not
	heard sent again a few times, the server taking five) */
	CONNECT_CONFIRM = 120000,
	CONNECT_ANSWER_TRIES = 4,
	CONNECT_TOKEN_LENGTH = 64,
	/* (the page with the code, as a QR code: version 10 at most) */
	CONNECT_QR_VERSION = 10,
};

struct hosted_game
{
	unsigned short name[BROWSER_NAME_LENGTH];
	char map[BROWSER_MAP_LENGTH];
	short engine;
	short players;
	short maximum_players;
	int open;
	short score_limit;
	int teams;
	int roster_count;
	struct browser_roster_player roster[BROWSER_HOSTED_ROSTER];
};

static pthread_mutex_t browser_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t browser_once = PTHREAD_ONCE_INIT;

static struct
{
	/* hosting (the game's threads write, the browser thread reads) */
	struct hosted_game hosted;
	unsigned long host_report_time;
	int host_reported;
	int host_changed;

	/* the browser thread's own */
	char listed_invite[BROWSER_INVITE_LENGTH + 1];
	unsigned long announce_time;
	struct hosted_game announced;

	/* a finished game's carnage report, waiting to be sent (JSON, without
	the invite, which is the listing's) */
	char *report;

	/* the local players' lines of a finished game, to confirm (the game's
	thread asks, the browser thread sends) */
	char claim_invite[BROWSER_INVITE_LENGTH + 1];
	unsigned short claim_names[MAXIMUM_CLAIM_NAMES][12];
	int claim_count;
	int claim_attempts;
	unsigned long claim_time;

	/* the profile page asked for (MY PROFILE: a sign-in link) */
	int profile_wanted;
	/* a restored key (a halo://key/ link) waiting for the player's yes */
	char pending_key[2 * PLAYER_KEY_SIZE + 1];

	/* Quick Connect (the game's thread asks, the browser thread talks to the
	server): the serial counts the codes asked for and the panel's closings,
	so that an answer about a code no longer shown is dropped */
	int connect_state;
	int connect_serial;
	unsigned short connect_name[12];
	char connect_code[16];
	char connect_token[CONNECT_TOKEN_LENGTH + 1];
	unsigned long connect_time;
	unsigned long connect_lifetime;
	unsigned long connect_poll_time;
	/* who typed the code (asked to confirm, then linked), and who the game
	was linked to before */
	char connect_handle[64];
	char connect_previous[64];
	unsigned long connect_confirm_time;
	/* the player's answer to the question: 1 yes, -1 no (0 none yet) */
	int connect_answer;
	int connect_answer_sent;
	int connect_answer_tries;
	char connect_message[96];
	int connect_qr_size;
	unsigned char connect_qr[BROWSER_CONNECT_QR_SIZE * BROWSER_CONNECT_QR_SIZE];

	/* browsing */
	int list_wanted;
	unsigned long list_request_time;
	unsigned long list_time;
	int list_failed;
	int game_count;
	struct browser_game games[BROWSER_MAXIMUM_GAMES];
} browser;

/* ---------- text */

static int elapsed(unsigned long since, unsigned long interval)
{
	return !since || p2p_now() - since >= interval;
}

/* UTF-16 to UTF-8 */
static void utf8_from_name(const unsigned short *name, int length, char *text, int size)
{
	int used = 0;
	int index;

	for (index = 0; index < length && name[index]; index++)
	{
		unsigned int character = name[index];
		char bytes[3];
		int count;

		/* (no surrogate pairs in the game's names) */
		if (character < 0x80)
		{
			bytes[0] = (char)character;
			count = 1;
		}
		else if (character < 0x800)
		{
			bytes[0] = (char)(0xC0 | (character >> 6));
			bytes[1] = (char)(0x80 | (character & 0x3F));
			count = 2;
		}
		else
		{
			bytes[0] = (char)(0xE0 | (character >> 12));
			bytes[1] = (char)(0x80 | ((character >> 6) & 0x3F));
			bytes[2] = (char)(0x80 | (character & 0x3F));
			count = 3;
		}
		if (used + count >= size)
			break;
		memcpy(text + used, bytes, (size_t)count);
		used += count;
	}
	text[used] = 0;
}

/* UTF-8 to UTF-16, into a name of the game's length */
static void name_from_utf8(const char *text, unsigned short *name, int length)
{
	const unsigned char *cursor = (const unsigned char *)text;
	int used = 0;

	while (*cursor && used < length - 1)
	{
		unsigned int character;

		if (*cursor < 0x80)
			character = *cursor++;
		else if ((*cursor & 0xE0) == 0xC0 && cursor[1])
		{
			character = ((cursor[0] & 0x1Fu) << 6) | (cursor[1] & 0x3Fu);
			cursor += 2;
		}
		else if ((*cursor & 0xF0) == 0xE0 && cursor[1] && cursor[2])
		{
			character = ((cursor[0] & 0x0Fu) << 12) | ((cursor[1] & 0x3Fu) << 6) | (cursor[2] & 0x3Fu);
			cursor += 3;
		}
		else
		{
			/* (a longer sequence, or a broken one: a question mark) */
			character = '?';
			cursor++;
			while ((*cursor & 0xC0) == 0x80)
				cursor++;
		}
		name[used++] = (unsigned short)character;
	}
	while (used < length)
		name[used++] = 0;
}

/* appends name=value, URL encoded */
static void form_add(char *form, int size, const char *name, const char *value)
{
	static const char digits[] = "0123456789ABCDEF";
	int used = (int)strlen(form);

	used += snprintf(form + used, (size_t)(size - used), "%s%s=", used ? "&" : "", name);
	for (; *value && used < size - 4; value++)
	{
		unsigned char character = (unsigned char)*value;

		if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == '-' || character == '_' || character == '.')
		{
			form[used++] = (char)character;
		}
		else
		{
			form[used++] = '%';
			form[used++] = digits[character >> 4];
			form[used++] = digits[character & 15];
		}
	}
	form[used] = 0;
}

static void server_url(const char *path, char *url, int size)
{
	const char *base = config_string("network.browser_url");
	size_t length = strlen(base);

	/* (with or without the final slash) */
	while (length && base[length - 1] == '/')
		length--;
	snprintf(url, (size_t)size, "%.*s%s", (int)length, base, path);
}

/* ---------- the player's identity

Each copy of the game has a player key: 32 random bytes made the first time
(posix_browser_private_key: readable by this user alone), in the save root.
The key is sent to the game list server alone, over HTTPS (whose
certificate is checked), to confirm the player's lines in finished games;
the server keeps no keys, only the player ID it works out from one again
each time: the first bytes of the key's SHA-256 (with a label, so that the
ID is no other use's hash of the key). The ID is public; the key is not.

To confirm a line the key alone does not do: the line must also be one the
game's host tagged with the address it had the player at (a hash of the
invite and that address: the address itself is never sent), and the
request must come from that address (the game list checks). So a key
confirms its own player's lines, in games they played. */

static int json_name(char *out, int size, const unsigned short *name, int length);

static int player_key_loaded;
static unsigned char player_key_cached[PLAYER_KEY_SIZE];

static void player_key_path(char *path, int size)
{
	snprintf(path, (size_t)size, "%s/game_list_player.key", platform_save_root());
}

static int player_key(unsigned char *key)
{
	int loaded;
	unsigned char *cached = player_key_cached;
	char path[1024];

	pthread_mutex_lock(&browser_lock);
	loaded = player_key_loaded;
	pthread_mutex_unlock(&browser_lock);
	if (!loaded)
	{
		player_key_path(path, sizeof(path));
		loaded = posix_browser_private_key(path, cached, PLAYER_KEY_SIZE) ? 1 : -1;
		if (loaded < 0)
			platform_log("Game list: no player key (%s): finished games are not confirmed", path);
		pthread_mutex_lock(&browser_lock);
		player_key_loaded = loaded;
		pthread_mutex_unlock(&browser_lock);
	}
	if (loaded < 0)
		return 0;
	pthread_mutex_lock(&browser_lock);
	memcpy(key, cached, PLAYER_KEY_SIZE);
	pthread_mutex_unlock(&browser_lock);
	return 1;
}

static void player_id_from_key(const unsigned char *key, char *text)
{
	static const char label[] = "halo-ce-universal player id\n";
	unsigned char data[sizeof(label) - 1 + PLAYER_KEY_SIZE];
	unsigned char digest[P2P_SHA256_SIZE];

	memcpy(data, label, sizeof(label) - 1);
	memcpy(data + sizeof(label) - 1, key, PLAYER_KEY_SIZE);
	p2p_sha256(data, (int)sizeof(data), digest);
	p2p_hex(digest, PLAYER_ID_SIZE, text);
}

/* a player's line tag: the hash of the invite and the address the host had
them at (dotted, as the server writes the address a request comes from) */
static void address_tag(const char *invite, unsigned long address, char *text)
{
	const unsigned char *bytes = (const unsigned char *)&address;
	char data[160];
	unsigned char digest[P2P_SHA256_SIZE];
	int size;

	size = snprintf(data, sizeof(data), "halo-ce-universal address\n%s\n%u.%u.%u.%u", invite,
		bytes[0], bytes[1], bytes[2], bytes[3]);
	p2p_sha256(data, size, digest);
	p2p_hex(digest, P2P_SHA256_SIZE, text);
}

/* the public address a player was at: an internet player's, as the tunnel
hears them; for the host's own machine, or one on its network (its game
address private), the host's own public address (the same network's) */
static unsigned long public_address(unsigned long game_address)
{
	unsigned long address = game_address ? p2p_peer_public_address(game_address) : 0;
	const unsigned char *bytes = (const unsigned char *)&game_address;
	int private_address = !game_address || bytes[0] == 10 || bytes[0] == 127 ||
		(bytes[0] == 172 && (bytes[1] & 0xF0) == 16) || (bytes[0] == 192 && bytes[1] == 168) ||
		(bytes[0] == 169 && bytes[1] == 254) || (bytes[0] == 100 && (bytes[1] & 0xC0) == 64);

	if (!address && private_address)
		address = p2p_public_address();
	else if (!address)
		address = game_address;
	return address;
}

/* the key goes to an HTTPS server, or one on this machine (a test) */
static int safe_for_key(const char *url)
{
	return !strncmp(url, "https://", 8) || !strncmp(url, "http://127.0.0.1", 16) ||
		!strncmp(url, "http://localhost", 16);
}

static void send_claims(void)
{
	char invite[BROWSER_INVITE_LENGTH + 1];
	unsigned short names[MAXIMUM_CLAIM_NAMES][12];
	unsigned char key[PLAYER_KEY_SIZE];
	char key_text[2 * PLAYER_KEY_SIZE + 1];
	char url[512], body[512], name[64], response[256], error[256];
	int count, index, retry = 0;

	pthread_mutex_lock(&browser_lock);
	count = browser.claim_count;
	if (count && !elapsed(browser.claim_time, browser.claim_attempts ? CLAIM_INTERVAL : CLAIM_DELAY))
		count = 0;
	memcpy(invite, browser.claim_invite, sizeof(invite));
	memcpy(names, browser.claim_names, sizeof(names));
	pthread_mutex_unlock(&browser_lock);
	if (!count)
		return;
	server_url("/v1/claim", url, sizeof(url));
	if (!safe_for_key(url) || !player_key(key))
	{
		pthread_mutex_lock(&browser_lock);
		browser.claim_count = 0;
		pthread_mutex_unlock(&browser_lock);
		return;
	}
	p2p_hex(key, PLAYER_KEY_SIZE, key_text);
	for (index = 0; index < count; index++)
	{
		int status;

		utf8_from_name(names[index], 12, name, sizeof(name));
		snprintf(body, sizeof(body), "{\"invite\": \"%s\", \"key\": \"%s\", \"name\": ", invite, key_text);
		json_name(body + strlen(body), (int)(sizeof(body) - strlen(body) - 2), names[index], 12);
		strcat(body, "}");
		status = posix_browser_request(url, body, "application/json", response, sizeof(response), error,
			sizeof(error));
		response[strcspn(response, "\r\n")] = 0;
		if (status == 200)
			platform_log("Game list: %s's line confirmed (player %s)", name, response + 3);
		else if (status == 404 || !status)
			retry = 1;
		else
			platform_log("Game list: %s's line was not confirmed (%s)", name, response);
	}
	memset(key, 0, sizeof(key));
	memset(key_text, 0, sizeof(key_text));
	memset(body, 0, sizeof(body));
	pthread_mutex_lock(&browser_lock);
	browser.claim_attempts++;
	browser.claim_time = p2p_now();
	if (!retry || browser.claim_attempts >= CLAIM_ATTEMPTS)
		browser.claim_count = 0;
	pthread_mutex_unlock(&browser_lock);
}

/* the profile page, signed in: the key goes to the server, which answers
with a code for a sign-in link good once, for a few minutes; the page opens
with the code (not the key) */
static void open_profile(void)
{
	unsigned char key[PLAYER_KEY_SIZE];
	char key_text[2 * PLAYER_KEY_SIZE + 1];
	char url[512], body[256], response[256], error[256], page[640];
	int wanted, status;

	pthread_mutex_lock(&browser_lock);
	wanted = browser.profile_wanted;
	browser.profile_wanted = 0;
	pthread_mutex_unlock(&browser_lock);
	if (!wanted)
		return;
	server_url("/v1/link", url, sizeof(url));
	if (!safe_for_key(url) || !player_key(key))
	{
		platform_log("Game list: the profile page needs an HTTPS game list and a player key");
		return;
	}
	p2p_hex(key, PLAYER_KEY_SIZE, key_text);
	snprintf(body, sizeof(body), "{\"key\": \"%s\"}", key_text);
	status = posix_browser_request(url, body, "application/json", response, sizeof(response), error, sizeof(error));
	memset(key, 0, sizeof(key));
	memset(key_text, 0, sizeof(key_text));
	memset(body, 0, sizeof(body));
	response[strcspn(response, "\r\n")] = 0;
	if (status != 200 || strncmp(response, "ok ", 3) || strspn(response + 3, "0123456789abcdef") != 64)
	{
		platform_log("Game list: could not open the profile page (%s)", status ? response : error);
		return;
	}
	server_url("/profile?link=", page, sizeof(page));
	strncat(page, response + 3, sizeof(page) - strlen(page) - 1);
	platform_open_url(page);
}

/* ---------- Quick Connect (the browser thread) */

static void connect_failed(int serial, const char *message)
{
	pthread_mutex_lock(&browser_lock);
	if (browser.connect_serial == serial)
	{
		browser.connect_state = BROWSER_CONNECT_FAILED;
		snprintf(browser.connect_message, sizeof(browser.connect_message), "%s", message);
	}
	pthread_mutex_unlock(&browser_lock);
}

/* a code for this copy's player key: the server answers "ok <code>
<seconds> <token>", the token what the game asks about the code with after
(the key goes once, as for the profile page's link; the profile's name
goes with it, for the page to say who it linked) */
static void start_connect(int serial, const unsigned short *name)
{
	unsigned char key[PLAYER_KEY_SIZE];
	unsigned char qr[qrcodegen_BUFFER_LEN_FOR_VERSION(CONNECT_QR_VERSION)];
	unsigned char work[qrcodegen_BUFFER_LEN_FOR_VERSION(CONNECT_QR_VERSION)];
	char key_text[2 * PLAYER_KEY_SIZE + 1];
	char url[512], body[320], response[256], error[256], page[640];
	char code[16], token[CONNECT_TOKEN_LENGTH + 1];
	int seconds = 0, status, size = 0, x, y;

	server_url("/v1/connect/start", url, sizeof(url));
	if (!safe_for_key(url) || !player_key(key))
	{
		connect_failed(serial, "Quick Connect needs an HTTPS game list and a player key.");
		return;
	}
	p2p_hex(key, PLAYER_KEY_SIZE, key_text);
	snprintf(body, sizeof(body), "{\"key\": \"%s\"", key_text);
	if (name[0])
	{
		strcat(body, ", \"name\": ");
		json_name(body + strlen(body), (int)(sizeof(body) - strlen(body) - 2), name, 12);
	}
	strcat(body, "}");
	status = posix_browser_request(url, body, "application/json", response, sizeof(response), error, sizeof(error));
	memset(key, 0, sizeof(key));
	memset(key_text, 0, sizeof(key_text));
	memset(body, 0, sizeof(body));
	response[strcspn(response, "\r\n")] = 0;
	if (status != 200 || sscanf(response, "ok %15s %d %64s", code, &seconds, token) != 3 || seconds <= 0 ||
		strspn(code, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") != strlen(code) ||
		strlen(token) != CONNECT_TOKEN_LENGTH || strspn(token, "0123456789abcdef") != CONNECT_TOKEN_LENGTH)
	{
		platform_log("Game list: no Quick Connect code (%s)", status ? response : error);
		connect_failed(serial, status && response[0] ? response : status ? "The game list gave no code." : error);
		memset(token, 0, sizeof(token));
		return;
	}
	/* (the page opens with the code filled in) */
	server_url("/connect?code=", page, sizeof(page));
	strncat(page, code, sizeof(page) - strlen(page) - 1);
	if (qrcodegen_encodeText(page, work, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, CONNECT_QR_VERSION,
		qrcodegen_Mask_AUTO, true))
	{
		size = qrcodegen_getSize(qr);
	}

	pthread_mutex_lock(&browser_lock);
	if (browser.connect_serial == serial)
	{
		browser.connect_state = BROWSER_CONNECT_WAITING;
		memcpy(browser.connect_code, code, sizeof(code));
		memcpy(browser.connect_token, token, sizeof(token));
		browser.connect_time = p2p_now();
		browser.connect_lifetime = (unsigned long)seconds * 1000;
		browser.connect_poll_time = browser.connect_time;
		browser.connect_qr_size = size;
		for (y = 0; y < size; y++)
		{
			for (x = 0; x < size; x++)
				browser.connect_qr[y * size + x] = qrcodegen_getModule(qr, x, y) ? 1 : 0;
		}
	}
	pthread_mutex_unlock(&browser_lock);
	memset(token, 0, sizeof(token));
}

/* a handle from the server's line, as text: up to a space, without control
characters, and no character cut in two */
static const char *connect_handle(const char *line, char *handle, int size)
{
	int used = 0;

	for (; *line && *line != ' ' && used < size - 1; line++)
	{
		if ((unsigned char)*line >= 0x20 && *line != 0x7F)
			handle[used++] = *line;
	}
	if (*line && *line != ' ')
	{
		while (used && ((unsigned char)handle[used - 1] & 0xC0) == 0x80)
			used--;
		if (used && ((unsigned char)handle[used - 1] & 0xC0) == 0xC0)
			used--;
		line += strcspn(line, " ");
	}
	handle[used] = 0;
	return *line == ' ' ? line + 1 : line;
}

/* the server's word on the code (under the lock): "pending" (not typed
yet), "confirm <handle> [<previous handle>]" (typed by someone signed in
as handle: the player is asked), "connected <handle>", "declined" or
"expired"; status 0 (no answer) changes nothing */
static void connect_line(int status, const char *response)
{
	browser.connect_poll_time = p2p_now();
	if (status == 200 && !strcmp(response, "pending"))
		return;
	if (status == 200 && !strncmp(response, "confirm ", 8))
	{
		char handle[sizeof(browser.connect_handle)];
		const char *rest = connect_handle(response + 8, handle, sizeof(handle));

		/* (the question's two minutes from when it is first asked) */
		if (browser.connect_state != BROWSER_CONNECT_CONFIRM || strcmp(handle, browser.connect_handle))
			browser.connect_confirm_time = p2p_now();
		memcpy(browser.connect_handle, handle, sizeof(handle));
		connect_handle(rest, browser.connect_previous, sizeof(browser.connect_previous));
		browser.connect_state = BROWSER_CONNECT_CONFIRM;
		return;
	}
	if (status == 200 && !strncmp(response, "connected ", 10))
	{
		connect_handle(response + 10, browser.connect_handle, sizeof(browser.connect_handle));
		browser.connect_state = BROWSER_CONNECT_CONNECTED;
		platform_log("Game list: Quick Connect linked the game to %s", browser.connect_handle);
	}
	else if (status == 200 && !strcmp(response, "declined"))
		browser.connect_state = BROWSER_CONNECT_DECLINED;
	else if (status == 200 && !strcmp(response, "expired"))
		browser.connect_state = BROWSER_CONNECT_EXPIRED;
	else if (status)
	{
		browser.connect_state = BROWSER_CONNECT_FAILED;
		snprintf(browser.connect_message, sizeof(browser.connect_message), "%s",
			response[0] ? response : "The game list did not answer as expected.");
	}
	else
		return;
	/* (the code is done with) */
	browser.connect_answer = 0;
	memset(browser.connect_token, 0, sizeof(browser.connect_token));
}

/* whether the code was typed (status), or the player's yes or no to who
typed it (confirm) */
static void ask_connect(int serial, const char *token, int answer)
{
	char url[512], body[128], response[256], error[256];
	int status;

	server_url(answer ? "/v1/connect/confirm" : "/v1/connect/status", url, sizeof(url));
	snprintf(body, sizeof(body), answer ? "{\"token\": \"%s\", \"accept\": %s}" : "{\"token\": \"%s\"}", token,
		answer > 0 ? "true" : "false");
	status = posix_browser_request(url, body, "application/json", response, sizeof(response), error, sizeof(error));
	memset(body, 0, sizeof(body));
	response[strcspn(response, "\r\n")] = 0;

	pthread_mutex_lock(&browser_lock);
	if (browser.connect_serial == serial)
	{
		/* (an answer sent: asked no more; one not heard: sent again, a few
		times) */
		if (answer && status)
			browser.connect_answer = 0;
		else if (answer && ++browser.connect_answer_tries >= CONNECT_ANSWER_TRIES)
		{
			browser.connect_answer = 0;
			memset(browser.connect_token, 0, sizeof(browser.connect_token));
			browser.connect_state = BROWSER_CONNECT_FAILED;
			snprintf(browser.connect_message, sizeof(browser.connect_message), "%s", error);
		}
		connect_line(status, response);
	}
	pthread_mutex_unlock(&browser_lock);
}

static void update_connect(void)
{
	unsigned short name[12];
	char token[CONNECT_TOKEN_LENGTH + 1];
	int state, serial, answer, ask;

	pthread_mutex_lock(&browser_lock);
	state = browser.connect_state;
	serial = browser.connect_serial;
	answer = browser.connect_answer;
	/* (the code ran out, or the question did, and the server could not say
	so) */
	if ((state == BROWSER_CONNECT_WAITING &&
			elapsed(browser.connect_time, browser.connect_lifetime + CONNECT_GRACE)) ||
		(state == BROWSER_CONNECT_CONFIRM && elapsed(browser.connect_confirm_time, CONNECT_CONFIRM + CONNECT_GRACE)))
	{
		state = browser.connect_state = BROWSER_CONNECT_EXPIRED;
		browser.connect_answer = 0;
		memset(browser.connect_token, 0, sizeof(browser.connect_token));
	}
	/* (the player's answer goes at once, the first time) */
	ask = (state == BROWSER_CONNECT_WAITING || state == BROWSER_CONNECT_CONFIRM) &&
		(elapsed(browser.connect_poll_time, CONNECT_POLL_INTERVAL) || (answer && !browser.connect_answer_sent));
	if (ask && answer)
		browser.connect_answer_sent = 1;
	memcpy(name, browser.connect_name, sizeof(name));
	memcpy(token, browser.connect_token, sizeof(token));
	pthread_mutex_unlock(&browser_lock);
	if (state == BROWSER_CONNECT_STARTING)
		start_connect(serial, name);
	else if (ask)
		ask_connect(serial, token, state == BROWSER_CONNECT_CONFIRM ? answer : 0);
	memset(token, 0, sizeof(token));
}

/* ---------- hosting (the browser thread) */

static void withdraw(void)
{
	char url[512], form[128], response[256], error[256];

	if (!browser.listed_invite[0])
		return;
	server_url("/v1/withdraw", url, sizeof(url));
	form[0] = 0;
	form_add(form, sizeof(form), "invite", browser.listed_invite);
	posix_browser_request(url, form, NULL, response, sizeof(response), error, sizeof(error));
	platform_log("Game list: the game is no longer listed");
	browser.listed_invite[0] = 0;
}

/* a roster as the list takes it: "team:name|team:name" (UTF-8; a name has
no "|", which the host's names leave out: network_server_message_handler.c) */
static void roster_text(const struct browser_roster_player *roster, int count, char *text, int size)
{
	int used = 0;
	int index;

	text[0] = 0;
	for (index = 0; index < count && used < size - 48; index++)
	{
		char name[64];

		utf8_from_name(roster[index].name, 12, name, sizeof(name));
		used += snprintf(text + used, (size_t)(size - used), "%s%d:%s", index ? "|" : "", roster[index].team, name);
	}
}

static void announce(const char *invite, const struct hosted_game *game)
{
	/* (the form: a roster of BROWSER_HOSTED_ROSTER names, URL encoded) */
	static char form[16384], roster[8192];
	char url[512], response[256], error[256], text[128];
	int status;

	server_url("/v1/announce", url, sizeof(url));
	form[0] = 0;
	form_add(form, sizeof(form), "invite", invite);
	utf8_from_name(game->name, BROWSER_NAME_LENGTH, text, sizeof(text));
	form_add(form, sizeof(form), "name", text);
	form_add(form, sizeof(form), "map", game->map);
	snprintf(text, sizeof(text), "%d", game->engine);
	form_add(form, sizeof(form), "engine", text);
	snprintf(text, sizeof(text), "%d", game->players);
	form_add(form, sizeof(form), "players", text);
	snprintf(text, sizeof(text), "%d", game->maximum_players);
	form_add(form, sizeof(form), "maximum_players", text);
	form_add(form, sizeof(form), "open", game->open ? "1" : "0");
	snprintf(text, sizeof(text), "%d", game->score_limit);
	form_add(form, sizeof(form), "score_limit", text);
	form_add(form, sizeof(form), "teams", game->teams ? "1" : "0");
	snprintf(text, sizeof(text), "%d", HALO_PORT_NETWORK_VERSION);
	form_add(form, sizeof(form), "version", text);
	/* (last: a full one may be cut short, and a list from before rosters
	takes no field of the name) */
	roster_text(game->roster, game->roster_count, roster, sizeof(roster));
	form_add(form, sizeof(form), "roster", roster);

	status = posix_browser_request(url, form, NULL, response, sizeof(response), error, sizeof(error));
	browser.announce_time = p2p_now();
	browser.announced = *game;
	if (status == 200)
	{
		if (strcmp(browser.listed_invite, invite))
			platform_log("Game list: the game is listed on %s", config_string("network.browser_url"));
		snprintf(browser.listed_invite, sizeof(browser.listed_invite), "%s", invite);
	}
	else
	{
		response[strcspn(response, "\r\n")] = 0;
		platform_log("Game list: could not list the game (%s)", status ? response : error);
	}
}

static void update_hosting(void)
{
	char invite[BROWSER_INVITE_LENGTH + 1];
	struct hosted_game game;
	int reported, changed, hosting;

	pthread_mutex_lock(&browser_lock);
	reported = browser.host_reported && !elapsed(browser.host_report_time, HOST_TIMEOUT);
	changed = browser.host_changed;
	game = browser.hosted;
	browser.host_changed = 0;
	pthread_mutex_unlock(&browser_lock);

	hosting = reported && config_boolean("network.list_hosted_games") &&
		p2p_hosting_invite(invite, sizeof(invite));
	if (!hosting)
	{
		withdraw();
		return;
	}
	/* (a new invite: the old one's listing withdrawn first) */
	if (browser.listed_invite[0] && strcmp(browser.listed_invite, invite))
		withdraw();
	if (!browser.listed_invite[0]
		? elapsed(browser.announce_time, browser.announce_time ? RETRY_INTERVAL : 0)
		: elapsed(browser.announce_time, ANNOUNCE_INTERVAL) ||
			((changed || memcmp(&game, &browser.announced, sizeof(game))) &&
				elapsed(browser.announce_time, CHANGE_INTERVAL)))
	{
		announce(invite, &game);
	}
}

static void send_report(void)
{
	char url[512], response[256], error[256];
	char *report, *body;
	size_t size;
	int status;

	pthread_mutex_lock(&browser_lock);
	report = browser.report;
	browser.report = NULL;
	pthread_mutex_unlock(&browser_lock);
	if (!report)
		return;
	/* (only a listed game: the server takes reports of those alone) */
	if (browser.listed_invite[0])
	{
		/* (the report, and the invite before it) */
		size = strlen(report) + BROWSER_INVITE_LENGTH + 32;
		body = malloc(size);
		if (body)
		{
			snprintf(body, size, "{\"invite\": \"%s\", %s", browser.listed_invite, report + 1);
			server_url("/v1/report", url, sizeof(url));
			status = posix_browser_request(url, body, "application/json", response, sizeof(response), error,
				sizeof(error));
			response[strcspn(response, "\r\n")] = 0;
			if (status == 200)
				platform_log("Game list: the game's carnage report is at %s/games/%s",
					config_string("network.browser_url"), response + 3);
			else
				platform_log("Game list: could not send the carnage report (%s)", status ? response : error);
			free(body);
		}
	}
	free(report);
}

/* ---------- browsing (the browser thread) */

/* one line of /v1/games.txt: invite name map engine players
maximum_players open version age score_limit teams */
/* a listed game's roster, from the list's "team:name|team:name" */
static void parse_roster(char *text, struct browser_game *game)
{
	char *entry = text;

	while (entry && *entry)
	{
		char *next = strchr(entry, '|');
		char *colon;

		if (next)
			*next++ = 0;
		colon = strchr(entry, ':');
		if (colon && colon[1])
		{
			*colon = 0;
			if (game->roster_count < BROWSER_LISTED_ROSTER)
			{
				struct browser_roster_player *player = &game->roster[game->roster_count];

				name_from_utf8(colon + 1, player->name, 12);
				player->team = (short)atoi(entry);
			}
			game->roster_count++;
		}
		entry = next;
	}
}

static int parse_game(char *line, struct browser_game *game)
{
	char *fields[12];
	int count = 0;
	char *cursor = line;

	while (count < 12)
	{
		fields[count++] = cursor;
		cursor = strchr(cursor, '\t');
		if (!cursor)
			break;
		*cursor++ = 0;
	}
	if (count < 8 || strlen(fields[0]) != BROWSER_INVITE_LENGTH)
		return 0;
	memset(game, 0, sizeof(*game));
	snprintf(game->invite, sizeof(game->invite), "%s", fields[0]);
	name_from_utf8(fields[1], game->name, BROWSER_NAME_LENGTH);
	snprintf(game->map, sizeof(game->map), "%s", fields[2]);
	game->engine = (short)atoi(fields[3]);
	game->players = (short)atoi(fields[4]);
	game->maximum_players = (short)atoi(fields[5]);
	game->open = (unsigned char)(atoi(fields[6]) != 0);
	game->version = (unsigned short)atoi(fields[7]);
	/* (a server from before these: none) */
	if (count >= 11)
	{
		game->score_limit = (short)atoi(fields[9]);
		game->teams = (unsigned char)(atoi(fields[10]) != 0);
	}
	/* (a list from before rosters: none) */
	if (count >= 12)
	{
		fields[11][strcspn(fields[11], "\r\n")] = 0;
		parse_roster(fields[11], game);
	}
	return 1;
}

static void update_list(void)
{
	static char response[RESPONSE_SIZE];
	char url[512], error[256];
	char own[BROWSER_INVITE_LENGTH + 1];
	struct browser_game *games;
	int count = 0;
	int status;
	char *line;
	int wanted;

	pthread_mutex_lock(&browser_lock);
	wanted = browser.list_wanted &&
		elapsed(browser.list_time, browser.list_failed ? RETRY_INTERVAL : LIST_INTERVAL);
	browser.list_wanted = 0;
	pthread_mutex_unlock(&browser_lock);
	if (!wanted || !config_string("network.browser_url")[0])
		return;

	server_url("/v1/games.txt", url, sizeof(url));
	status = posix_browser_request(url, NULL, NULL, response, sizeof(response), error, sizeof(error));
	games = malloc(sizeof(*games) * BROWSER_MAXIMUM_GAMES);
	if (!games)
		return;
	if (!p2p_hosting_invite(own, sizeof(own)))
		own[0] = 0;
	if (status == 200)
	{
		for (line = strtok(response, "\n"); line && count < BROWSER_MAXIMUM_GAMES; line = strtok(NULL, "\n"))
		{
			if (parse_game(line, &games[count]) && games[count].version >= HALO_PORT_NETWORK_VERSION_MINIMUM &&
				games[count].version <= HALO_PORT_NETWORK_VERSION_MAXIMUM &&
				strcmp(games[count].invite, own))
			{
				count++;
			}
		}
	}
	else
	{
		platform_log("Game list: could not get the list (%s)", status ? "the server refused" : error);
	}
	pthread_mutex_lock(&browser_lock);
	browser.list_time = p2p_now();
	browser.list_failed = status != 200;
	if (status == 200)
	{
		memcpy(browser.games, games, sizeof(*games) * (size_t)count);
		browser.game_count = count;
	}
	pthread_mutex_unlock(&browser_lock);
	free(games);
}

static void *browser_thread(void *unused)
{
	(void)unused;
	for (;;)
	{
		if (config_string("network.browser_url")[0])
		{
			update_hosting();
			send_report();
			send_claims();
			open_profile();
			update_connect();
			update_list();
		}
		Sleep(THREAD_INTERVAL);
	}
	return NULL;
}

/* a copy of the game that quits while its game is listed takes it off the
list (without this the server drops it only once it stops hearing of it) */
static void withdraw_at_exit(void)
{
	/* (the browser thread may be mid-request: the listing is withdrawn by
	whichever of the two gets there) */
	if (browser.listed_invite[0])
		withdraw();
}

static void start_thread(void)
{
	pthread_t thread;

	atexit(withdraw_at_exit);
	if (pthread_create(&thread, NULL, browser_thread, NULL) == 0)
		pthread_detach(thread);
	else
		platform_log("Game list: could not start its thread");
}

/* ---------- public code */

void browser_host_update(const unsigned short *name, const char *map, short engine, short players,
	short maximum_players, int open, short score_limit, int teams,
	const struct browser_roster_player *roster, int roster_count)
{
	/* (a roster of BROWSER_HOSTED_ROSTER: not on the stack, called on the
	game's thread only) */
	static struct hosted_game game;

	pthread_once(&browser_once, start_thread);
	memset(&game, 0, sizeof(game));
	memcpy(game.name, name, sizeof(game.name));
	snprintf(game.map, sizeof(game.map), "%s", map);
	game.engine = engine;
	game.players = players;
	game.maximum_players = maximum_players;
	game.open = open != 0;
	game.score_limit = score_limit;
	game.teams = teams != 0;
	if (roster_count > BROWSER_HOSTED_ROSTER)
		roster_count = BROWSER_HOSTED_ROSTER;
	if (roster && roster_count > 0)
	{
		memcpy(game.roster, roster, (size_t)roster_count * sizeof(*roster));
		game.roster_count = roster_count;
	}

	pthread_mutex_lock(&browser_lock);
	if (memcmp(&game, &browser.hosted, sizeof(game)))
	{
		browser.hosted = game;
		browser.host_changed = 1;
	}
	browser.host_reported = 1;
	browser.host_report_time = p2p_now();
	pthread_mutex_unlock(&browser_lock);
}

/* appends a JSON string of a name */
static int json_name(char *out, int size, const unsigned short *name, int length)
{
	char text[64];
	int used = 0;
	const char *cursor;

	utf8_from_name(name, length, text, sizeof(text));
	used += snprintf(out + used, (size_t)(size - used), "\"");
	for (cursor = text; *cursor && used < size - 8; cursor++)
	{
		unsigned char character = (unsigned char)*cursor;

		if (character == '"' || character == '\\')
			used += snprintf(out + used, (size_t)(size - used), "\\%c", character);
		else if (character < 0x20)
			used += snprintf(out + used, (size_t)(size - used), "\\u%04x", character);
		else
			out[used++] = (char)character;
	}
	used += snprintf(out + used, (size_t)(size - used), "\"");
	return used;
}

void browser_report_game(int teams, int red_score, int blue_score, int duration_seconds,
	const struct browser_report_player *players, int count)
{
	size_t size = 256 + (size_t)count * 576;
	char *report = malloc(size);
	char invite[BROWSER_INVITE_LENGTH + 1];
	int tagged;
	int used = 0;
	int index;

	if (!report || count <= 0)
	{
		free(report);
		return;
	}
	used += snprintf(report + used, size - (size_t)used,
		"{\"teams\": %d, \"duration\": %d, \"team_scores\": [%d, %d], \"players\": [",
		teams != 0, duration_seconds, teams ? red_score : 0, teams ? blue_score : 0);
	tagged = p2p_hosting_invite(invite, sizeof(invite));
	for (index = 0; index < count && (size_t)used < size - 576; index++)
	{
		const struct browser_report_player *player = &players[index];
		unsigned long address = tagged ? public_address(player->address) : 0;

		used += snprintf(report + used, size - (size_t)used, "%s{\"name\": ", index ? ", " : "");
		used += json_name(report + used, (int)(size - (size_t)used), player->name, 12);
		used += snprintf(report + used, size - (size_t)used,
			", \"team\": %d, \"place\": %d, \"score\": %d, \"kills\": %d, \"assists\": %d, \"deaths\": %d, "
			"\"betrayals\": %d, \"suicides\": %d, \"shots_fired\": %d, \"shots_hit\": %d, \"multikills\": %d, "
			"\"color\": %d, \"flag_grabs\": %d, \"flag_returns\": %d, \"flag_scores\": %d, \"ball_time\": %d, "
			"\"ball_carrier_kills\": %d, \"hill_time\": %d, \"laps\": %d}",
			player->team, player->place, player->score, player->kills, player->assists, player->deaths,
			player->betrayals, player->suicides, player->shots_fired, player->shots_hit, player->multikills,
			player->color, player->flag_grabs, player->flag_returns, player->flag_scores, player->ball_time,
			player->ball_carrier_kills, player->hill_time, player->laps);
		/* (the line's tag: who may confirm it) */
		if (address)
		{
			char tag[2 * P2P_SHA256_SIZE + 1];

			address_tag(invite, address, tag);
			used--;
			used += snprintf(report + used, size - (size_t)used, ", \"tag\": \"%s\"}", tag);
		}
	}
	snprintf(report + used, size - (size_t)used, "]}");

	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	free(browser.report);
	browser.report = report;
	pthread_mutex_unlock(&browser_lock);
}

/* the local players of a game that ended: their lines confirmed with the
player key, once the host has reported the game (if it is listed) */
void browser_claim_game(const unsigned short (*names)[12], int count)
{
	char invite[BROWSER_INVITE_LENGTH + 1];

	if (count <= 0 || !config_string("network.browser_url")[0] ||
		(!p2p_hosting_invite(invite, sizeof(invite)) && !p2p_joined_invite(invite, sizeof(invite))))
		return;
	if (count > MAXIMUM_CLAIM_NAMES)
		count = MAXIMUM_CLAIM_NAMES;
	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	memcpy(browser.claim_invite, invite, sizeof(invite));
	memcpy(browser.claim_names, names, (size_t)count * sizeof(names[0]));
	browser.claim_count = count;
	browser.claim_attempts = 0;
	browser.claim_time = p2p_now();
	pthread_mutex_unlock(&browser_lock);
}

void browser_open_profile(void)
{
	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	browser.profile_wanted = 1;
	pthread_mutex_unlock(&browser_lock);
}

void browser_connect_start(const unsigned short *name)
{
	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	browser.connect_serial++;
	browser.connect_state = BROWSER_CONNECT_STARTING;
	memset(browser.connect_name, 0, sizeof(browser.connect_name));
	if (name)
		memcpy(browser.connect_name, name, sizeof(browser.connect_name));
	browser.connect_code[0] = 0;
	browser.connect_handle[0] = 0;
	browser.connect_previous[0] = 0;
	browser.connect_answer = 0;
	browser.connect_qr_size = 0;
	memset(browser.connect_token, 0, sizeof(browser.connect_token));
	/* (the browser thread asks no server without one) */
	if (!config_string("network.browser_url")[0])
	{
		browser.connect_state = BROWSER_CONNECT_FAILED;
		snprintf(browser.connect_message, sizeof(browser.connect_message), "No game list (network.browser_url).");
	}
	pthread_mutex_unlock(&browser_lock);
}

void browser_connect_stop(void)
{
	pthread_mutex_lock(&browser_lock);
	browser.connect_serial++;
	browser.connect_state = BROWSER_CONNECT_OFF;
	browser.connect_answer = 0;
	memset(browser.connect_token, 0, sizeof(browser.connect_token));
	pthread_mutex_unlock(&browser_lock);
}

void browser_connect_answer(int accept)
{
	pthread_mutex_lock(&browser_lock);
	if (browser.connect_state == BROWSER_CONNECT_CONFIRM && !browser.connect_answer)
	{
		browser.connect_answer = accept ? 1 : -1;
		browser.connect_answer_sent = 0;
		browser.connect_answer_tries = 0;
	}
	pthread_mutex_unlock(&browser_lock);
}

void browser_server_name(char *text, int size)
{
	const char *base = config_string("network.browser_url");
	size_t length;

	/* (as a player types it: without the scheme or the final slash) */
	if (!strncmp(base, "https://", 8))
		base += 8;
	else if (!strncmp(base, "http://", 7))
		base += 7;
	length = strlen(base);
	while (length && base[length - 1] == '/')
		length--;
	snprintf(text, (size_t)size, "%.*s", (int)length, base);
}

void browser_connect_get(struct browser_connect *connect)
{
	int size;

	pthread_mutex_lock(&browser_lock);
	connect->state = browser.connect_state;
	memcpy(connect->code, browser.connect_code, sizeof(connect->code));
	connect->seconds = 0;
	if (connect->state == BROWSER_CONNECT_WAITING && !elapsed(browser.connect_time, browser.connect_lifetime))
		connect->seconds = (int)((browser.connect_lifetime - (p2p_now() - browser.connect_time) + 999) / 1000);
	else if (connect->state == BROWSER_CONNECT_CONFIRM && !elapsed(browser.connect_confirm_time, CONNECT_CONFIRM))
		connect->seconds = (int)((CONNECT_CONFIRM - (p2p_now() - browser.connect_confirm_time) + 999) / 1000);
	connect->answered = browser.connect_answer != 0;
	memcpy(connect->handle, browser.connect_handle, sizeof(connect->handle));
	memcpy(connect->previous, browser.connect_previous, sizeof(connect->previous));
	memcpy(connect->message, browser.connect_message, sizeof(connect->message));
	size = connect->state == BROWSER_CONNECT_WAITING ? browser.connect_qr_size : 0;
	connect->qr_size = size;
	memcpy(connect->qr, browser.connect_qr, (size_t)(size * size));
	pthread_mutex_unlock(&browser_lock);

	browser_server_name(connect->page, (int)sizeof(connect->page) - 8);
	strcat(connect->page, "/connect");
}

/* a restored key's link, halo://key/<64 hexadecimal digits> (the profile
page's Install in Game): kept until the player says yes
(browser_take_key_link, from the main thread); 1 if the text is one */
int browser_key_link(const char *text)
{
	static const char prefix[] = "halo://key/";
	const char *digits;

	if (SDL_strncasecmp(text, prefix, sizeof(prefix) - 1))
		return 0;
	digits = text + sizeof(prefix) - 1;
	if (strspn(digits, "0123456789abcdef") != 2 * PLAYER_KEY_SIZE ||
		(digits[2 * PLAYER_KEY_SIZE] && digits[2 * PLAYER_KEY_SIZE] != '/'))
	{
		platform_log("Game list: that key link is not a player key");
		return 1;
	}
	pthread_mutex_lock(&browser_lock);
	memcpy(browser.pending_key, digits, 2 * PLAYER_KEY_SIZE);
	browser.pending_key[2 * PLAYER_KEY_SIZE] = 0;
	pthread_mutex_unlock(&browser_lock);
	return 1;
}

/* a key link waiting (as on the command line, a copy started with one): its
key, and the player IDs of the key in use and of it */
int browser_take_key_link(char *new_id, char *old_id, int size)
{
	static int command_line_checked;
	unsigned char key[PLAYER_KEY_SIZE];
	char digits[2 * PLAYER_KEY_SIZE + 1];
	int index;

	if (!command_line_checked)
	{
		char argument[256];

		command_line_checked = 1;
		for (index = 1; posix_command_line_argument(index, argument, sizeof(argument)); index++)
			browser_key_link(argument);
	}
	pthread_mutex_lock(&browser_lock);
	memcpy(digits, browser.pending_key, sizeof(digits));
	pthread_mutex_unlock(&browser_lock);
	if (!digits[0] || size <= 2 * PLAYER_ID_SIZE)
		return 0;
	for (index = 0; index < PLAYER_KEY_SIZE; index++)
	{
		unsigned int byte;

		sscanf(digits + 2 * index, "%2x", &byte);
		key[index] = (unsigned char)byte;
	}
	player_id_from_key(key, new_id);
	if (!browser_player_id(old_id, size))
		old_id[0] = 0;
	memset(key, 0, sizeof(key));
	memset(digits, 0, sizeof(digits));
	return 1;
}

/* the waiting key put in place of this copy's (yes), or dropped (no) */
void browser_answer_key_link(int install)
{
	unsigned char key[PLAYER_KEY_SIZE];
	char path[1024];
	int index, ok = 0;

	pthread_mutex_lock(&browser_lock);
	if (install && browser.pending_key[0])
	{
		for (index = 0; index < PLAYER_KEY_SIZE; index++)
		{
			unsigned int byte;

			sscanf(browser.pending_key + 2 * index, "%2x", &byte);
			key[index] = (unsigned char)byte;
		}
		player_key_path(path, sizeof(path));
		ok = posix_browser_replace_key(path, key, PLAYER_KEY_SIZE);
		if (ok)
		{
			memcpy(player_key_cached, key, PLAYER_KEY_SIZE);
			player_key_loaded = 1;
		}
		memset(key, 0, sizeof(key));
	}
	memset(browser.pending_key, 0, sizeof(browser.pending_key));
	pthread_mutex_unlock(&browser_lock);
	if (install)
		platform_log(ok ? "Game list: the player key was restored" : "Game list: could not restore the player key");
}

/* this copy's public player ID (its line in finished games), as text */
int browser_player_id(char *text, int size)
{
	unsigned char key[PLAYER_KEY_SIZE];

	if (size <= 2 * PLAYER_ID_SIZE || !player_key(key))
		return 0;
	player_id_from_key(key, text);
	memset(key, 0, sizeof(key));
	return 1;
}

int browser_get_games(struct browser_game *games, int maximum_count)
{
	int count;

	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	browser.list_wanted = 1;
	count = browser.game_count < maximum_count ? browser.game_count : maximum_count;
	memcpy(games, browser.games, sizeof(*games) * (size_t)count);
	pthread_mutex_unlock(&browser_lock);
	return count;
}

int browser_game_peer(const char *invite, unsigned long *address)
{
	unsigned char identifier[P2P_IDENTIFIER_SIZE];
	int index;

	/* (the invite: the host's identifier, then the token) */
	for (index = 0; index < P2P_IDENTIFIER_SIZE; index++)
	{
		unsigned int byte;

		if (sscanf(invite + 2 * index, "%2x", &byte) != 1)
			return 0;
		identifier[index] = (unsigned char)byte;
	}
	return p2p_peer_address(identifier, address);
}

int browser_join(const char *invite)
{
	/* ("halo://join/" and the invite's digits) */
	char link[16 + BROWSER_INVITE_LENGTH];

	snprintf(link, sizeof(link), "halo://join/%s", invite);
	platform_log("Game list: joining a listed game");
	return p2p_join_invite(link);
}

int browser_dedicated(void)
{
	const char *playlist = getenv("HALO_DEDICATED");

	return playlist && playlist[0];
}

const char *browser_probe(void)
{
	const char *invite = getenv("HALO_PROBE");

	return invite && invite[0] ? invite : NULL;
}

int browser_headless(void)
{
	return browser_dedicated() || browser_probe() != NULL;
}

#endif
