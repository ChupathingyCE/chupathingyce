/*
BROWSER.C

The game list (browser.h; configure.py
--game-browser): hosted system link games announced to the list server
(network.browser_url: stats.chupathingy.org, the community's) with their invites, and the
server's list for System Link to show.

Hosting: the game's server reports its game each frame
(browser_host_update). While p2p.c hosts it on the internet (it has an
invite), the game is public and has no password (its invite lets anyone in),
and network.list_hosted_games is on, the game is announced every
ANNOUNCE_INTERVAL, or a little after it changes (players join, the map
changes), and withdrawn when the reports stop or p2p.c stops hosting. The
server forgets a game it is not told about for a while, so a copy of the
game that quits without withdrawing drops off by itself.

Browsing: browser_get_games asks for the list when the last one is more
than LIST_INTERVAL old, and returns what it has meanwhile. Games of another
network version than this machine can join are kept too, for the screens to
mark (browser_game_version_state); never this machine's own. The list is
the main source; the public listings heard on the brokers (p2p_lobby.c,
mqtt.chupathingy.org among them) add the games it lacks (direct): OpenCE's
(legacy) while it answers, and every host's while it is down. So losing
the list costs no game the brokers carry, and losing the brokers OpenCE
uses costs only OpenCE's games.

The requests (posix_browser.c) block, so they are made on a thread of this
file's, which the first call starts; the game's threads only exchange state
with it under the lock.
*/

#ifdef HALO_GAME_BROWSER

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "browser_http.h"
#include "http_url_rules.h"
#include "p2p.h"
#include "p2p_internal.h"
#include "browser.h"
#include "game_report.h"
#include "delta_peer.h"
#include "server_badge.h"
#include "browser_ping.h"
#include "qrcodegen.h"
#include "delta_profile.h"
#ifndef HALO_CONSOLE_NO_GUIDE
#include "social.h"
#endif
#ifdef HALO_DELTA_PEER_IDENTITY
#include "identity.h"
#include "identity_key.h"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_filesystem.h>

/* an Xbox map pack's map's name, pack and tag (port/linux/game/map_packs.c) */
int map_pack_listing(char const *map, char *title, int title_size, char *pack, int pack_size, char *tag, int tag_size);

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
	/* the last list's games are kept through failures this long, then
	dropped (the games heard directly stay: browser_get_games) */
	LIST_STALE = 90000,
	THREAD_INTERVAL = 250,
	/* a copy of the game that quits waits this long at most for its
	listing's withdrawal (the list drops it by itself later otherwise) */
	EXIT_WITHDRAW_WAIT = 2500,
	EXIT_WITHDRAW_POLL = 50,
	/* (the list's answer: a line of 150 bytes or so a game, more with a
	full roster, for BROWSER_MAXIMUM_GAMES of them) */
	RESPONSE_SIZE = BROWSER_MAXIMUM_GAMES * 1024,
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
	/* Link Profile: the server asked whether its code was typed this often,
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
	/* Link a device: the site asked whether the other device answered this
	often (a device's requests count as a key's, 6 a minute) */
	APPROVE_POLL_INTERVAL = 10000,
	/* a joined game's report (browser_client_report): sent again this long
	after the server could not be reached, a few times */
	CLIENT_REPORT_INTERVAL = 8000,
	CLIENT_REPORT_ATTEMPTS = 4,
	/* the player's own profile asked of the site again this long after it
	could not be reached, a few times */
	PROFILE_CHECK_INTERVAL = 60000,
	PROFILE_CHECK_ATTEMPTS = 4,
	/* Delta Profile: the site takes a key's requests 6 a minute
	(list_server.py's GAME_REQUESTS_PER_KEY), so the profile is asked for
	this often at most (a request sooner waits), and asked again when a
	screen wants it and it is this old; the site's answer is under 8 KB
	(GAME_ANSWER) */
	GAME_PROFILE_INTERVAL = 10000,
	GAME_PROFILE_STALE = 60000,
	GAME_PROFILE_RESPONSE = 16384,
	/* other players' cards (POST /v1/game/players, 30 a minute an address):
	asked for this often at most, 16 at a time; after a failure, this long
	after; a player with none asked about again this long after */
	CARDS_INTERVAL = 5000,
	CARDS_RETRY = 60000,
	CARDS_PER_REQUEST = 16,
	CARD_NONE_AGAIN = 600000,
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
	/* what the announcement says of the host (Delta List): its platform
	key, whether it hosts with Delta, its machines by platform
	(delta_peer_game_host_summary: 0, not known yet) */
	int has_delta;
	struct delta_peer_host_summary delta;
};

/* another player's card, as the site gave it (browser_player_card) */
enum
{
	_card_wanted,
	_card_known,
	/* no profile to show */
	_card_none,
};

struct player_card
{
	char id[DELTA_PROFILE_ID_LENGTH + 1];
	int state;
	unsigned long time;
	struct delta_profile_card card;
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

	/* the browser thread's own (written under the lock, which the exit's
	withdrawal reads it under) */
	char listed_invite[BROWSER_INVITE_LENGTH + 1];
	/* the hosts this game joined and what they proved of their badges
	(server_badge.h): a listed badge is shown only if it matches */
	struct server_badge_joined_set badge_joined;
	unsigned long announce_time;
	struct hosted_game announced;
	/* whether the hosted game was offered to the list since it was last
	listed or withdrawn, and the answer's HTTP status (0: no answer), for
	browser_host_listing */
	int announce_tried;
	int announce_status;

	/* a finished game's carnage report, waiting to be sent (JSON, without
	the invite, which is the listing's) */
	char *report;
	/* a joined game's report, waiting to be sent (JSON, without the player
	key, which the browser thread adds), how often it was tried, and when */
	char *client_report;
	int client_report_attempts;
	unsigned long client_report_time;

	/* the local players' lines of a finished game, to confirm (the game's
	thread asks, the browser thread sends) */
	char claim_invite[BROWSER_INVITE_LENGTH + 1];
	unsigned short claim_names[MAXIMUM_CLAIM_NAMES][BROWSER_PLAYER_NAME_LENGTH];
	int claim_count;
	int claim_attempts;
	unsigned long claim_time;

	/* the profile page asked for (MY PROFILE: a sign-in link) */
	int profile_wanted;
	/* a restored key (a halo://key/ link) waiting for the player's yes */
	char pending_key[2 * PLAYER_KEY_SIZE + 1];

	/* Link Profile (the game's thread asks, the browser thread talks to the
	server): the serial counts the codes asked for and the panel's closings,
	so that an answer about a code no longer shown is dropped */
	int connect_state;
	int connect_serial;
	unsigned short connect_name[BROWSER_PLAYER_NAME_LENGTH];
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
	/* the code is Quick Connect's (the device key's), not the player key's */
	int connect_quick;

	/* the devices (docs/delta.md, Devices): this one signed out, another
	revoked (its ID), and another device's code approved (Link a device:
	its state, what the site said, and when it was last asked) */
	int device_sign_out_wanted;
	char device_revoke[DELTA_PROFILE_ID_LENGTH + 1];
	int approve_state;
	int approve_serial;
	/* the player's answer to the device shown: 1 approve, -1 deny */
	int approve_answer;
	char approve_code[16];
	char approve_handle[48];
	char approve_name[40];
	char approve_platform[24];
	char approve_message[96];
	unsigned long approve_time;

#ifndef HALO_CONSOLE_NO_GUIDE
	/* presence (social.h): whether the game says this copy is playing a
	Delta game, when it last said so, and the schedule (the browser
	thread's, under the lock) */
	int presence_playing;
	unsigned long presence_said;
	struct social_presence presence;
	int presence_logged;

	/* the Guide's friends (browser_social_*): where the game says the
	player is, and when it last said so; the schedule; the state (made the
	first time the game says) and its serial; the action asked for and
	what it came to; a join the site allowed */
	int social_in_game;
	int social_guide_open;
	unsigned long social_said;
	struct social_poll social_poll;
	struct social_state *social_state;
	int social_status;
	int social_serial;
	int social_action_wanted;
	struct browser_social_result social_result;
	char social_join[BROWSER_INVITE_LENGTH + 16];
	/* the doorbell (p2p_doorbell): this copy's (from the friends state), and
	the one internet play's signalling was last given (empty: none) */
	char doorbell_id[SOCIAL_DOORBELL_SIZE];
	char doorbell_given[SOCIAL_DOORBELL_SIZE];

	/* the party (browser_party_*): as the site last said and its serial;
	the action asked for (its words), what it came to, and the name a copy
	with no profile goes by; the leader's game to tell; a copy with no
	profile's own polling */
	struct social_party party;
	int party_serial;
	int party_action_wanted;
	char party_action[16];
	char party_value[96];
	char party_name[48];
	struct browser_social_result party_result;
	char party_go[BROWSER_INVITE_LENGTH + 1];
	struct social_poll party_poll;
#endif

	/* the profile this copy's games count for (browser_profile_linked): its
	handle while known, from Link Profile on this copy (kept in the save
	root) or the site's word on the player's own profile; the site asked
	once a run (wanted when a screen asks, tried a few times) */
	int profile_linked;
	char profile_handle[64];
	int profile_read;
	int profile_save_wanted;
	int profile_check_wanted;
	int profile_check_done;
	int profile_check_attempts;
	unsigned long profile_check_time;

	/* Delta Profile in the game (browser_profile_*): the game's thread
	asks, the browser thread asks the site (POST /v1/game/...) and keeps its
	answer here, in memory only; serial counts the answers kept */
	int game_profile_wanted;
	int game_profile_state;
	int game_profile_has;
	int game_profile_serial;
	unsigned long game_profile_time;
	unsigned long game_profile_asked;
	struct delta_profile game_profile;
	int game_unlink_wanted;
	/* the privacy settings to send (one request takes them all) */
	int game_privacy_count;
	char game_privacy_fields[6][16];
	int game_privacy_values[6];
	/* the friends' privacy to send (/v1/game/friends' "privacy"): who may
	send requests, who may join, who sees the status ("": as it is) */
	char game_friends_requests[24];
	char game_friends_join[24];
	char game_friends_presence[24];
	/* a clan request waiting (/v1/game/clan): its action and clan */
	char game_clan_action[12];
	int game_clan_id;
	/* (invite and kick, DELTA's: the member's handle) */
	char game_clan_handle[24];
	/* the profile was read again (its clan without the roster's handles
	and presence): the next friends poll asks for the whole state */
	int social_whole;
	int game_action;
	char game_message[128];
	/* the link kept in the save root forgotten (the copy was unlinked) */
	int profile_forget_wanted;
	/* other players' cards (by their shared player IDs) */
	int card_count;
	struct player_card cards[BROWSER_PLAYER_CARDS];
	unsigned long cards_time;
	int cards_failed;

	/* the estimates' anchors (browser_ping.c): asked for and timed once a
	run, the first time a screen of listed games is up with internet play
	on (the browser thread's; read under the lock) */
	int anchors_wanted;
	int anchors_started;
	int anchors_done;
	int anchor_count;
	struct browser_ping_anchor anchors[BROWSER_PING_ANCHORS];

	/* browsing */
	int list_wanted;
	unsigned long list_request_time;
	unsigned long list_time;
	int list_failed;
	unsigned long list_answered_time;
	int game_count;
	struct browser_game games[BROWSER_MAXIMUM_GAMES];
} browser;

/* ---------- text */

static int elapsed(unsigned long since, unsigned long interval)
{
	return !since || p2p_now() - since >= interval;
}

/* UTF-16 to UTF-8 (game_report.c's) */
static void utf8_from_name(const unsigned short *name, int length, char *text, int size)
{
	game_report_utf8(name, length, text, size);
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
static const char *local_platform_name(void);

static int player_key_loaded;
static unsigned char player_key_cached[PLAYER_KEY_SIZE];

static void player_key_path(char *path, int size)
{
	snprintf(path, (size_t)size, "%s/game_list_player.key", platform_save_root());
}

static int player_key(unsigned char *key)
{
	/* (one thread loads it, or makes it the first time, while another that
	wants it waits: the browser's thread and the game's both ask, and the
	second, finding the file just made and not yet written, had the key
	missing for the whole run; held across the file's reading, not
	browser_lock) */
	static pthread_mutex_t load_lock = PTHREAD_MUTEX_INITIALIZER;
	int loaded;
	char path[1024];

	pthread_mutex_lock(&load_lock);
	pthread_mutex_lock(&browser_lock);
	loaded = player_key_loaded;
	pthread_mutex_unlock(&browser_lock);
	if (!loaded)
	{
		unsigned char read_key[PLAYER_KEY_SIZE];

		/* (said once a run; a key that could not be read is tried again the
		next time one is wanted, not given up on until the game restarts) */
		static int logged;

		player_key_path(path, sizeof(path));
		loaded = posix_browser_private_key(path, read_key, PLAYER_KEY_SIZE) ? 1 : -1;
		if (loaded < 0 && !logged)
		{
			logged = 1;
			platform_log("Game list: no player key (%s): finished games are not confirmed", path);
		}
		pthread_mutex_lock(&browser_lock);
		/* (unless a restored key took its place meanwhile) */
		if (player_key_loaded)
			loaded = player_key_loaded;
		else if (loaded > 0)
		{
			memcpy(player_key_cached, read_key, PLAYER_KEY_SIZE);
			player_key_loaded = loaded;
		}
		pthread_mutex_unlock(&browser_lock);
		memset(read_key, 0, sizeof(read_key));
	}
	pthread_mutex_unlock(&load_lock);
	if (loaded < 0)
		return 0;
	pthread_mutex_lock(&browser_lock);
	memcpy(key, player_key_cached, PLAYER_KEY_SIZE);
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
	return http_url_safe_for_secret(url);
}

/* ---------- Delta's device identity (identity.c; docs/delta.md, "Device
identity")

Beside the player key, each copy has a device key of its own
(delta_device.key in the save root, 32 bytes made and read as the player
key is: its owner's alone, never a link), whose private half never leaves
this machine. The first time a build with it reaches the site with a player
key, it proves the key once more and enrolls the device; the site answers a
certificate for the device key (delta_device.cert beside it, public, with
the copy ID it was enrolled with), which this thread refreshes when the
site says, with the device key alone. Every signature names the site
(its host, the audience), so it is no use at another.

With a certificate, the requests that took the player key send an hour's
token instead (site_request). A token the site lost (a restart) is asked
for again and the request sent again with it; the key goes only to a site
that doesn't speak identity. A request the site answered otherwise is never
sent again. Another player key (a restored one, or one found at start that
isn't the copy's the certificate names) drops the certificate and the token
and makes a new device key, which enrolls with it. The player key file
stays: older sites and servers know only it. Keys, certificates and tokens
are never logged; debug.txt says only how enrollment went.

The browser thread's alone (no lock), but for identity_key_changed, which
the game's thread sets under browser_lock when it puts in another key. */

#ifdef HALO_DELTA_IDENTITY
enum
{
	/* (a certificate answer: two certificates in hex, and the account) */
	IDENTITY_RESPONSE = 2048,
};

static struct identity_state identity;
static int identity_read;
/* (under browser_lock) the certificate in use, for proofs in games
(browser_identity_card): its bytes, its issuer's, and what it says */
static int shown_valid;
static unsigned char shown_certificate[IDENTITY_DEVICE_MOST];
static int shown_certificate_size;
static unsigned char shown_issuer[IDENTITY_ISSUER_SIZE];
static struct identity_device shown_device;
/* the copy ID the certificate is for (the player key's when it enrolled) */
static unsigned char identity_copy[IDENTITY_ID_SIZE];
/* (under browser_lock) another player key was put in */
static int identity_key_changed;
/* the last thing debug.txt was told (an identity answer), said once */
static int identity_said = -1;

static unsigned long identity_clock(void)
{
	time_t now = time(NULL);

	return now > 0 ? (unsigned long)now : 0;
}

static void identity_path(const char *name, char *path, int size)
{
	snprintf(path, (size_t)size, "%s/%s", platform_save_root(), name);
}

/* the site's host, which every device signature names */
static int identity_site(char *audience, int size)
{
	return identity_audience(config_string("network.browser_url"), audience, size) > 0;
}

/* the device key: read, or made the first time; its public key and ID */
static int device_key(unsigned char *seed, unsigned char *public_key, unsigned char *device_id)
{
	char path[1024];

	identity_path("delta_device.key", path, sizeof(path));
	if (!posix_browser_device_key(path, seed, IDENTITY_SEED_SIZE))
		return 0;
	identity_public(seed, public_key);
	identity_device_id(public_key, device_id);
	return 1;
}

static int file_exists(const char *path)
{
	FILE *file = fopen(path, "rb");

	if (!file)
		return 0;
	fclose(file);
	return 1;
}

static int all_zero_bytes(const unsigned char *bytes, int size)
{
	int index;

	for (index = 0; index < size; index++)
	{
		if (bytes[index])
			return 0;
	}
	return 1;
}

/* the copy ID of the player key (the player ID of today), when it has one
(none is made here) */
static int current_copy(unsigned char *copy)
{
	unsigned char key[PLAYER_KEY_SIZE], data[28 + PLAYER_KEY_SIZE], digest[P2P_SHA256_SIZE];
	char path[1024];

	player_key_path(path, sizeof(path));
	if (!file_exists(path) || !player_key(key))
		return 0;
	memcpy(data, "halo-ce-universal player id\n", 28);
	memcpy(data + 28, key, PLAYER_KEY_SIZE);
	p2p_sha256(data, (int)sizeof(data), digest);
	memcpy(copy, digest, IDENTITY_ID_SIZE);
	identity_wipe(key, sizeof(key));
	identity_wipe(data, sizeof(data));
	return 1;
}

/* everything dropped for another copy: the certificate, the token and the
device key (a new one is made, and enrolls) */
static void identity_new_device(void)
{
	char path[1024];
	int new_key_made = identity.new_key_made;

	identity_wipe(&identity, sizeof(identity));
	identity.new_key_made = new_key_made;
	identity_wipe(identity_copy, sizeof(identity_copy));
	pthread_mutex_lock(&browser_lock);
	shown_valid = 0;
	pthread_mutex_unlock(&browser_lock);
	identity_path("delta_device.cert", path, sizeof(path));
	remove(path);
	identity_path("delta_device.key", path, sizeof(path));
	remove(path);
	/* (a new device is not signed out: it enrolls, or signs in by code) */
	identity_path("delta_device.out", path, sizeof(path));
	remove(path);
	identity_read = 1;
}

/* a certificate the site answered (or the file kept), taken when it checks;
a saved one past its time is kept to refresh, never to enroll again */
static int take_certificate(const unsigned char *certificate, int certificate_size, const unsigned char *issuer,
	int issuer_size, unsigned long refresh_after, int saved)
{
	unsigned char seed[IDENTITY_SEED_SIZE], public_key[IDENTITY_KEY_SIZE], id[IDENTITY_ID_SIZE];
	struct identity_device device;
	unsigned long now = identity_clock();
	int reason;

	if (!device_key(seed, public_key, id))
		return 0;
	identity_wipe(seed, sizeof(seed));
	reason = identity_accept(certificate, certificate_size, issuer, issuer_size, identity_root_keys,
		IDENTITY_ROOT_KEY_COUNT, public_key, now, &device);
	if (reason == IDENTITY_OK)
	{
		identity_have_certificate(&identity, &device, refresh_after, now);
		pthread_mutex_lock(&browser_lock);
		memcpy(shown_certificate, certificate, (size_t)certificate_size);
		shown_certificate_size = certificate_size;
		memcpy(shown_issuer, issuer, IDENTITY_ISSUER_SIZE);
		shown_device = device;
		shown_valid = issuer_size == IDENTITY_ISSUER_SIZE;
		pthread_mutex_unlock(&browser_lock);
		return 1;
	}
	/* (a saved one past its time: checked all the same, the issuer's
	signature, the chain with the root keys and this device's key, as of
	when it was issued; one that doesn't check is as none, and enrolls) */
	if (saved && (reason == IDENTITY_EXPIRED || reason == IDENTITY_NOT_YET) &&
		identity_parse_device(certificate, certificate_size, &device) &&
		identity_accept(certificate, certificate_size, issuer, issuer_size, identity_root_keys,
			IDENTITY_ROOT_KEY_COUNT, public_key, device.issued, &device) == IDENTITY_OK)
	{
		identity_have_certificate(&identity, &device, 0, now);
		identity.refresh_at = 0;
		return 1;
	}
	return 0;
}

/* delta_device.cert: the certificate's hex, the issuer's, then the copy
ID's, a line each */
static void read_certificate_file(void)
{
	char path[1024], line[2 * IDENTITY_DEVICE_MOST + 4], issuer_line[2 * IDENTITY_ISSUER_SIZE + 4];
	char copy_line[2 * IDENTITY_ID_SIZE + 4];
	unsigned char certificate[IDENTITY_DEVICE_MOST], issuer[IDENTITY_ISSUER_SIZE];
	unsigned char saved_copy[IDENTITY_ID_SIZE], copy[IDENTITY_ID_SIZE];
	FILE *file;
	int length, ok;

	if (identity_read)
		return;
	identity_read = 1;
	/* (signed out by its player: nothing as the account until it signs in
	again by code) */
	identity_path("delta_device.out", path, sizeof(path));
	if (file_exists(path))
	{
		identity.signed_out = 1;
		return;
	}
	identity_path("delta_device.cert", path, sizeof(path));
	file = fopen(path, "r");
	if (!file)
		return;
	line[0] = issuer_line[0] = copy_line[0] = 0;
	if (!fgets(line, sizeof(line), file) || !fgets(issuer_line, sizeof(issuer_line), file) ||
		!fgets(copy_line, sizeof(copy_line), file))
	{
		line[0] = 0;
	}
	fclose(file);
	line[strcspn(line, "\r\n")] = 0;
	issuer_line[strcspn(issuer_line, "\r\n")] = 0;
	copy_line[strcspn(copy_line, "\r\n")] = 0;
	length = (int)strlen(line);
	ok = !(length % 2) && length <= 2 * IDENTITY_DEVICE_MOST &&
		identity_unhex(line, length, certificate, length / 2) &&
		identity_unhex(issuer_line, (int)strlen(issuer_line), issuer, IDENTITY_ISSUER_SIZE) &&
		identity_unhex(copy_line, (int)strlen(copy_line), saved_copy, IDENTITY_ID_SIZE);
	/* (another player key since: a new device for it; a device signed in by
	code without its copy keeps zeros there, and no player key is its) */
	if (ok && !all_zero_bytes(saved_copy, IDENTITY_ID_SIZE) &&
		(!current_copy(copy) || memcmp(copy, saved_copy, IDENTITY_ID_SIZE)))
	{
		identity_new_device();
		platform_log("Delta identity: the player key changed; this device enrolls again with a new device key");
		return;
	}
	if (ok && take_certificate(certificate, length / 2, issuer, IDENTITY_ISSUER_SIZE, 0, 1))
	{
		memcpy(identity_copy, saved_copy, IDENTITY_ID_SIZE);
		return;
	}
	/* (one that doesn't check is enrolled again) */
	platform_log("Delta identity: the saved device certificate is not used; asked for again");
}

static void write_certificate_file(const unsigned char *certificate, int certificate_size, const unsigned char *issuer,
	int issuer_size)
{
	char path[1024], text[2 * (IDENTITY_DEVICE_MOST + IDENTITY_ISSUER_SIZE + IDENTITY_ID_SIZE) + 4];

	identity_path("delta_device.cert", path, sizeof(path));
	identity_hex(certificate, certificate_size, text);
	strcat(text, "\n");
	identity_hex(issuer, issuer_size, text + strlen(text));
	strcat(text, "\n");
	identity_hex(identity_copy, IDENTITY_ID_SIZE, text + strlen(text));
	strcat(text, "\n");
	if (!posix_browser_replace_key(path, (const unsigned char *)text, (int)strlen(text)))
		platform_log("Delta identity: could not save the device certificate");
}

/* a nonce from the site: its HTTP status (200 only with one), its answer
in response */
static int identity_nonce(char *nonce_hex, unsigned char *nonce, char *response, int size)
{
	char url[512], error[256];
	int status;

	server_url("/v1/identity/nonce", url, sizeof(url));
	response[0] = 0;
	status = posix_browser_request(url, NULL, NULL, response, size, error, sizeof(error));
	if (status == 200 && !identity_nonce_answer(response, nonce_hex, nonce))
		status = 502;
	return status;
}

static void identity_say(int said, const char *text)
{
	if (said != identity_said)
	{
		identity_said = said;
		platform_log("Delta identity: %s", text);
	}
}

/* an identity path's refusal (status, response) taken: the state, the
files and debug.txt */
static void identity_refused(int status, const char *response)
{
	char path[1024];
	int answer = identity_answered(&identity, status, status ? response : "", identity_clock());

	if (answer == IDENTITY_ANSWER_OFF)
	{
		FILE *file;

		/* (revoked or signed out on the site: as signed out here, this run
		and after, so the player key never goes in its place; signing in by
		code again ends it) */
		identity_path("delta_device.cert", path, sizeof(path));
		remove(path);
		pthread_mutex_lock(&browser_lock);
		shown_valid = 0;
		pthread_mutex_unlock(&browser_lock);
		identity_path("delta_device.out", path, sizeof(path));
		file = fopen(path, "w");
		if (file)
			fclose(file);
	}
	else if (answer == IDENTITY_ANSWER_NEW_KEY)
	{
		identity_new_device();
		identity.new_key_made = 1;
		identity.must_enroll = 1;
	}
	identity_say(answer, answer == IDENTITY_ANSWER_LATER ? "the site doesn't speak device identity; "
		"the player key is used (asked again in an hour)" : answer == IDENTITY_ANSWER_BUSY ?
		"the site is busy or doesn't issue device certificates yet; asked again later" :
		answer == IDENTITY_ANSWER_UNKNOWN_COPY ? "the site hasn't seen this copy yet; asked again after its "
		"next confirmed game" : answer == IDENTITY_ANSWER_DAY ? (identity.off ? "the site doesn't take this device; the player key is "
		"used this run" : "the site doesn't take this device yet; asked again tomorrow") :
		answer == IDENTITY_ANSWER_NEW_KEY ? "the device key was another copy's; a new one enrolls" :
		answer == IDENTITY_ANSWER_ENROLL ? "the site doesn't know this device; enrolling again" :
		answer == IDENTITY_ANSWER_OFF ? "this device was signed out or removed on the site; it signs in again by "
		"code" : "the site is not answering; asked again later");
}

/* a certificate answer (enroll or refresh) taken */
static void identity_certificate(int status, const char *response, int enrolling)
{
	unsigned char certificate[IDENTITY_DEVICE_MOST], issuer[IDENTITY_ISSUER_SIZE];
	char number[16];
	unsigned long refresh_after = 0;
	int certificate_size, issuer_size, index, length;

	if (status != 200)
	{
		identity_refused(status, response);
		return;
	}
	identity_answered(&identity, 200, response, identity_clock());
	/* (refresh_after: a duration, seconds from now) */
	length = identity_json_member(response, "refresh_after", number, sizeof(number));
	for (index = 0; index < length && number[index] >= '0' && number[index] <= '9'; index++)
		refresh_after = refresh_after * 10 + (unsigned long)(number[index] - '0');
	if (index != length)
		refresh_after = 0;
	if (!identity_certificate_answer(response, certificate, &certificate_size, issuer, &issuer_size) ||
		!take_certificate(certificate, certificate_size, issuer, issuer_size, refresh_after, 0))
	{
		identity_answered(&identity, 502, "", identity_clock());
		identity_say(-2, "the site's device certificate did not check; asked again later");
		return;
	}
	write_certificate_file(certificate, certificate_size, issuer, issuer_size);
	identity_say(IDENTITY_ANSWER_OK, enrolling ? "this device is enrolled" : "the device certificate is fresh");
}

/* another player key put in (browser_answer_key_link): a new device */
static void identity_check_key_change(void)
{
	int changed;

	pthread_mutex_lock(&browser_lock);
	changed = identity_key_changed;
	identity_key_changed = 0;
	pthread_mutex_unlock(&browser_lock);
	/* (a device signed in by code has no copy: another player key isn't its
	business) */
	if (changed && (!identity.has_certificate || !all_zero_bytes(identity_copy, IDENTITY_ID_SIZE)))
	{
		identity_new_device();
		identity.new_key_made = 0;
		platform_log("Delta identity: the player key changed; this device enrolls again with a new device key");
	}
}

/* enrollment or a refresh, when one is due (browser_thread) */
static void update_identity(void)
{
	unsigned char seed[IDENTITY_SEED_SIZE], public_key[IDENTITY_KEY_SIZE], device_id[IDENTITY_ID_SIZE];
	unsigned char nonce[IDENTITY_NONCE_SIZE], message[IDENTITY_MESSAGE_MOST], signature[IDENTITY_SIGNATURE_SIZE];
	unsigned char key[PLAYER_KEY_SIZE], copy[IDENTITY_ID_SIZE];
	char nonce_hex[2 * IDENTITY_NONCE_SIZE + 1], key_text[2 * PLAYER_KEY_SIZE + 1];
	char audience[IDENTITY_AUDIENCE_MOST + 1];
	char url[512], path[1024], body[768];
	static char response[IDENTITY_RESPONSE];
	char error[256];
	int step, status, size = -1;

	server_url("/v1/identity/nonce", url, sizeof(url));
	if (!safe_for_key(url) || !identity_site(audience, sizeof(audience)))
		return;
	identity_check_key_change();
	read_certificate_file();
	step = identity_due(&identity, 1, identity_clock());
	/* (a copy that never made a player key enrolls once it has one: no key
	is made for it here) */
	player_key_path(path, sizeof(path));
	if (step == IDENTITY_STEP_ENROLL && !file_exists(path))
	{
		/* (looked for again in a minute) */
		identity.wait_until = identity_clock() + IDENTITY_RETRY_FIRST;
		return;
	}
	if (step == IDENTITY_STEP_NONE)
		return;
	status = identity_nonce(nonce_hex, nonce, response, sizeof(response));
	if (status != 200)
	{
		identity_refused(status, response);
		return;
	}
	if (!device_key(seed, public_key, device_id))
	{
		identity_say(-3, "could not read or make this device's key");
		identity_answered(&identity, 0, "", identity_clock());
		return;
	}
	body[0] = 0;
	if (step == IDENTITY_STEP_ENROLL && current_copy(copy) && player_key(key))
	{
		/* (the copy ID, which the signature names, kept for the file) */
		size = identity_enroll_message(audience, nonce, copy, public_key, message);
		if (size > 0)
		{
			identity_sign(seed, message, size, signature);
			p2p_hex(key, PLAYER_KEY_SIZE, key_text);
			if (identity_enroll_body(body, sizeof(body), key_text, public_key, nonce_hex, signature,
				local_platform_name(), build_identity_version()) < 0)
			{
				body[0] = 0;
			}
			memcpy(identity_copy, copy, IDENTITY_ID_SIZE);
		}
		server_url("/v1/identity/legacy/enroll", url, sizeof(url));
		identity_wipe(key, sizeof(key));
		identity_wipe(key_text, sizeof(key_text));
	}
	else if (step == IDENTITY_STEP_REFRESH)
	{
		size = identity_refresh_message(audience, nonce, device_id, message);
		if (size > 0)
		{
			identity_sign(seed, message, size, signature);
			if (identity_signed_body(body, sizeof(body), device_id, nonce_hex, signature) < 0)
				body[0] = 0;
		}
		server_url("/v1/identity/refresh", url, sizeof(url));
	}
	identity_wipe(seed, sizeof(seed));
	identity_wipe(signature, sizeof(signature));
	if (!body[0])
	{
		identity_answered(&identity, 0, "", identity_clock());
		return;
	}
	status = posix_browser_request(url, body, "application/json", response, sizeof(response), error, sizeof(error));
	identity_wipe(body, sizeof(body));
	identity_certificate(status, status ? response : "", step == IDENTITY_STEP_ENROLL);
	identity_wipe(response, sizeof(response));
}

/* an hour's token for the site's requests, when one is due */
static void update_token(void)
{
	unsigned char seed[IDENTITY_SEED_SIZE], public_key[IDENTITY_KEY_SIZE], device_id[IDENTITY_ID_SIZE];
	unsigned char nonce[IDENTITY_NONCE_SIZE], message[IDENTITY_MESSAGE_MOST], signature[IDENTITY_SIGNATURE_SIZE];
	char nonce_hex[2 * IDENTITY_NONCE_SIZE + 1], token[IDENTITY_TOKEN_SIZE];
	char audience[IDENTITY_AUDIENCE_MOST + 1];
	char url[512], body[512], response[512], error[256];
	unsigned long lifetime;
	int status, size;

	if (!identity_site(audience, sizeof(audience)))
		return;
	identity_check_key_change();
	read_certificate_file();
	if (!identity_token_due(&identity, identity_clock()))
		return;
	status = identity_nonce(nonce_hex, nonce, response, sizeof(response));
	if (status != 200)
	{
		identity_refused(status, response);
		return;
	}
	if (!device_key(seed, public_key, device_id))
		return;
	size = identity_token_message(audience, nonce, device_id, message);
	if (size > 0)
		identity_sign(seed, message, size, signature);
	identity_wipe(seed, sizeof(seed));
	if (size <= 0 || identity_signed_body(body, sizeof(body), device_id, nonce_hex, signature) < 0)
		return;
	identity_wipe(signature, sizeof(signature));
	server_url("/v1/identity/token", url, sizeof(url));
	status = posix_browser_request(url, body, "application/json", response, sizeof(response), error, sizeof(error));
	if (status == 200 && identity_token_answer(response, token, sizeof(token), &lifetime))
		identity_set_token(&identity, token, lifetime, identity_clock());
	else if (status == 200)
		identity_answered(&identity, 502, "", identity_clock());
	else
		identity_refused(status, response);
	identity_wipe(token, sizeof(token));
	identity_wipe(response, sizeof(response));
}

int browser_identity_card(const unsigned char *account, unsigned char *certificate, int *certificate_size,
	unsigned char *issuer, struct identity_device *device)
{
	int ok;
	time_t now = time(NULL);

	pthread_mutex_lock(&browser_lock);
	/* (in date by this machine's clock: an expired one proves nothing until
	it refreshes) */
	ok = shown_valid && now > 0 && (unsigned long)now >= shown_device.issued &&
		(unsigned long)now < shown_device.expires && (shown_device.capabilities & IDENTITY_CAN_PLAY) &&
		(!account || !memcmp(account, shown_device.account, IDENTITY_ID_SIZE));
	if (ok)
	{
		memcpy(certificate, shown_certificate, (size_t)shown_certificate_size);
		*certificate_size = shown_certificate_size;
		memcpy(issuer, shown_issuer, IDENTITY_ISSUER_SIZE);
		if (device)
			*device = shown_device;
	}
	pthread_mutex_unlock(&browser_lock);
	return ok;
}

int browser_identity_sign(const unsigned char *message, int size, unsigned char *signature, unsigned char *public_key)
{
	unsigned char seed[IDENTITY_SEED_SIZE], key[IDENTITY_KEY_SIZE], id[IDENTITY_ID_SIZE];

	if (!device_key(seed, key, id))
		return 0;
	identity_sign(seed, message, size, signature);
	identity_wipe(seed, sizeof(seed));
	if (public_key)
		memcpy(public_key, key, IDENTITY_KEY_SIZE);
	return 1;
}

int browser_identity_moderator_sign(const unsigned char *message, int size, unsigned char *public_key,
	unsigned char *signature)
{
	struct identity_device device;
	unsigned char certificate[IDENTITY_DEVICE_MOST], issuer[IDENTITY_ISSUER_SIZE];
	int certificate_size;

	/* (a device whose certificate lets it moderate: the role is the
	account's, which the host knows from the device's proof) */
	if (!browser_identity_card(NULL, certificate, &certificate_size, issuer, &device) ||
		!(device.capabilities & IDENTITY_CAN_MODERATE))
	{
		return 0;
	}
	return browser_identity_sign(message, size, signature, public_key);
}
#endif

#ifdef HALO_DELTA_PEER_IDENTITY
/* ---------- verified names: the revocation list (docs/delta.md, Verified
names). Asked of the site every ten minutes (GET /v1/identity/revoked),
checked (its issuer by the root keys, the list by its issuer, a serial past
the one kept) and kept in delta_revoked.bin in the save root, so a host or
player that can't reach the site still knows the last one. Under
browser_lock. */
enum
{
	REVOKED_INTERVAL = 600,
	REVOKED_BYTES = IDENTITY_REVOKED_HEADER + 16 * IDENTITY_REVOKED_MOST + IDENTITY_SIGNATURE_SIZE,
};

static unsigned char revoked_bytes[REVOKED_BYTES];
static int revoked_size;
static struct identity_revoked revoked_list;
/* (each list taken: browser_identity_revoked_changes) */
static int revoked_changes;
/* (the browser thread's) */
static unsigned long revoked_asked;
static int revoked_loaded;

static void revoked_path(char *path, int size)
{
	snprintf(path, (size_t)size, "%s/delta_revoked.bin", platform_save_root());
}

/* a list and its issuer certificate checked: taken (1) when newer than the
one kept */
static int take_revoked(const unsigned char *list, int list_size, const unsigned char *issuer_data, int issuer_size,
	int save)
{
	struct identity_issuer issuer;
	struct identity_revoked parsed;
	time_t now = time(NULL);
	int taken = 0;

	int issuer_check;

	/* (an issuer past its dates still vouches for the list this copy kept,
	taken while it was good: an old list is better than none, and is said
	to be stale. One fetched is taken only from an issuer within its dates:
	a key past them is no longer looked after, and a list it signed could
	take revoked devices off) */
	issuer_check = list_size > REVOKED_BYTES ? IDENTITY_MALFORMED :
		identity_check_issuer(issuer_data, issuer_size, identity_root_keys, IDENTITY_ROOT_KEY_COUNT,
			now > 0 ? (unsigned long)now : 0, &issuer);
	if (!identity_revoked_issuer_usable(issuer_check, !save) ||
		identity_check_revoked(list, list_size, &issuer, &parsed) != IDENTITY_OK)
	{
		return 0;
	}
	if (issuer_check == IDENTITY_EXPIRED)
		platform_log("Delta identity: the revocation list's issuer has expired: the list is used, but stale");
	pthread_mutex_lock(&browser_lock);
	if (!revoked_size || parsed.serial > revoked_list.serial)
	{
		memcpy(revoked_bytes, list, (size_t)list_size);
		revoked_size = list_size;
		identity_parse_revoked(revoked_bytes, revoked_size, &revoked_list);
		revoked_changes++;
		taken = 1;
	}
	pthread_mutex_unlock(&browser_lock);
	if (taken && now > 0 && (unsigned long)now > parsed.next_update + IDENTITY_GRACE)
		platform_log("Delta identity: the revocation list is stale (its next update has passed): used until a newer one comes");
	if (taken && save)
	{
		static unsigned char file[REVOKED_BYTES + IDENTITY_ISSUER_SIZE];
		char path[1024];

		memcpy(file, issuer_data, IDENTITY_ISSUER_SIZE);
		memcpy(file + IDENTITY_ISSUER_SIZE, list, (size_t)list_size);
		revoked_path(path, sizeof(path));
		if (!posix_browser_replace_key(path, file, IDENTITY_ISSUER_SIZE + list_size))
			platform_log("Delta identity: could not save the revocation list");
	}
	return taken;
}

/* the list kept on disk, read once, whichever thread asks first (with or
without a site to ask: a LAN game still checks against the last list) */
static void load_saved_revoked(void)
{
	static unsigned char data[REVOKED_BYTES + IDENTITY_ISSUER_SIZE];
	char path[1024];
	FILE *file;
	size_t size;
	int first;

	pthread_mutex_lock(&browser_lock);
	first = !revoked_loaded;
	revoked_loaded = 1;
	pthread_mutex_unlock(&browser_lock);
	if (!first)
		return;
	revoked_path(path, sizeof(path));
	file = fopen(path, "rb");
	if (!file)
		return;
	size = fread(data, 1, sizeof(data), file);
	fclose(file);
	if (size > IDENTITY_ISSUER_SIZE)
		take_revoked(data + IDENTITY_ISSUER_SIZE, (int)size - IDENTITY_ISSUER_SIZE, data, IDENTITY_ISSUER_SIZE, 0);
}

/* (the browser thread) the list kept on disk, once; the site's every ten
minutes */
static void update_revoked(void)
{
	static unsigned char issuer[IDENTITY_ISSUER_SIZE];
	static unsigned char list[REVOKED_BYTES];
	char url[512], error[256], first[2 * IDENTITY_ISSUER_SIZE + 8];
	char *response, *text;
	time_t now = time(NULL);
	int status, length;
	const int response_size = 2 * (REVOKED_BYTES + IDENTITY_ISSUER_SIZE * 4) + 1024;

	load_saved_revoked();
	if (now <= 0 || (revoked_asked && (unsigned long)now < revoked_asked + REVOKED_INTERVAL))
		return;
	revoked_asked = (unsigned long)now;
	server_url("/v1/identity/revoked", url, sizeof(url));
	response = malloc((size_t)response_size);
	text = malloc(2 * REVOKED_BYTES + 2);
	if (!response || !text)
	{
		free(response);
		free(text);
		return;
	}
	status = posix_browser_request(url, NULL, NULL, response, response_size, error, sizeof(error));
	/* ("revoked": the list's hex; "issuers": the issuer certificates, the
	current first) */
	length = status == 200 ? identity_json_member(response, "revoked", text, 2 * REVOKED_BYTES + 2) : -1;
	if (length > 0 && length % 2 == 0 && length / 2 <= REVOKED_BYTES &&
		identity_unhex(text, length, list, length / 2) &&
		identity_json_member(response, "issuers", first, sizeof(first)) > 0)
	{
		char *start = strchr(first, '"');
		int issuer_length;

		/* ("[\"<hex>\", ...]": the first) */
		if (start)
		{
			start++;
			issuer_length = (int)strcspn(start, "\"");
			if (issuer_length == 2 * IDENTITY_ISSUER_SIZE &&
				identity_unhex(start, issuer_length, issuer, IDENTITY_ISSUER_SIZE))
			{
				take_revoked(list, length / 2, issuer, IDENTITY_ISSUER_SIZE, 1);
			}
		}
	}
	free(response);
	free(text);
}

int browser_identity_revoked_changes(void)
{
	int changes;

	load_saved_revoked();
	pthread_mutex_lock(&browser_lock);
	changes = revoked_changes;
	pthread_mutex_unlock(&browser_lock);
	return changes;
}

int browser_identity_revoked(const unsigned char *device_id)
{
	int revoked;

	load_saved_revoked();
	pthread_mutex_lock(&browser_lock);
	revoked = revoked_size && identity_is_revoked(&revoked_list, device_id);
	pthread_mutex_unlock(&browser_lock);
	return revoked;
}
#endif

/* (the player key's copies wiped as the seeds are) */
#ifdef HALO_DELTA_IDENTITY
#define identity_wipe_key identity_wipe
#else
#define identity_wipe_key(data, size) memset((data), 0, (size_t)(size))
#endif

/* a request that takes the player key (members: the body's other JSON
members, or ""): with this device's token (Authorization: Delta) when it
has one. A token the site lost is asked for again and the request sent
again with it; the key in the body only when there is no token, or the site
doesn't speak identity (a 400 without "token"). An answer the site may have
acted on is never sent again. Its HTTP status (0: no answer); *no_key when
the key can't go (no HTTPS game list, no player key). The browser thread's */
static int site_request(const char *path, const char *members, char *response, int size, char *error,
	int error_size, int *no_key)
{
	unsigned char key[PLAYER_KEY_SIZE];
	char key_text[2 * PLAYER_KEY_SIZE + 1];
	char url[512];
	char *body;
	size_t length = strlen(members) + 2 * PLAYER_KEY_SIZE + 32;
	int status;

	*no_key = 0;
	server_url(path, url, sizeof(url));
	if (!safe_for_key(url))
	{
		*no_key = 1;
		return 0;
	}
#ifdef HALO_DELTA_IDENTITY
	{
		char token[IDENTITY_TOKEN_SIZE], headers[IDENTITY_TOKEN_SIZE + 64];
		int attempt, sent;

		update_token();
		for (attempt = 0; attempt < 2 && identity_token(&identity, identity_clock(), token, sizeof(token)); attempt++)
		{
			body = malloc(length);
			if (!body)
			{
				identity_wipe(token, sizeof(token));
				return 0;
			}
			snprintf(body, length, "{%s}", members);
			snprintf(headers, sizeof(headers), "Authorization: Delta %s\r\n", token);
			status = posix_browser_send(url, body, strlen(body), "application/json", headers, response, size, error,
				error_size);
			identity_wipe(token, sizeof(token));
			identity_wipe(headers, sizeof(headers));
			free(body);
			sent = identity_token_sent(&identity, status, response, identity_clock());
			/* (the answer stands, or the second token failed too) */
			if (sent == IDENTITY_SENT_DONE || attempt)
				return status;
			/* (a token the site lost: a new one, and again with it once;
			none to be had: the answer, or for an "unknown" token, the key) */
			update_token();
			if (!identity_token(&identity, identity_clock(), token, sizeof(token)))
			{
				if (sent != IDENTITY_SENT_UNKNOWN_TOKEN)
					return status;
				break;
			}
			identity_wipe(token, sizeof(token));
		}
		/* (stage C: with a certificate, or signed out, the key goes only to a
		site that doesn't speak identity; else no answer now, asked again
		later) */
		if (!identity_key_allowed(&identity, identity_clock()))
		{
			*no_key = identity.signed_out;
			return 0;
		}
	}
#endif
	if (!player_key(key))
	{
		*no_key = 1;
		return 0;
	}
	body = malloc(length);
	if (!body)
	{
		identity_wipe_key(key, sizeof(key));
		return 0;
	}
	p2p_hex(key, PLAYER_KEY_SIZE, key_text);
	snprintf(body, length, "{\"key\": \"%s\"%s%s}", key_text, members[0] ? ", " : "", members);
	status = posix_browser_request(url, body, "application/json", response, size, error, error_size);
	identity_wipe_key(key, sizeof(key));
	identity_wipe_key(key_text, sizeof(key_text));
	identity_wipe_key(body, (int)strlen(body));
	free(body);
	return status;
}

static void send_claims(void)
{
	char invite[BROWSER_INVITE_LENGTH + 1];
	unsigned short names[MAXIMUM_CLAIM_NAMES][BROWSER_PLAYER_NAME_LENGTH];
	char members[512], name[64], response[256], error[256];
	int count, index, retry = 0, no_key = 0;

	pthread_mutex_lock(&browser_lock);
	count = browser.claim_count;
	if (count && !elapsed(browser.claim_time, browser.claim_attempts ? CLAIM_INTERVAL : CLAIM_DELAY))
		count = 0;
	memcpy(invite, browser.claim_invite, sizeof(invite));
	memcpy(names, browser.claim_names, sizeof(names));
	pthread_mutex_unlock(&browser_lock);
	if (!count)
		return;
	for (index = 0; index < count && !no_key; index++)
	{
		int status;

		utf8_from_name(names[index], BROWSER_PLAYER_NAME_LENGTH, name, sizeof(name));
		snprintf(members, sizeof(members), "\"invite\": \"%s\", \"platform\": \"%s\", \"name\": ", invite,
			local_platform_name());
		json_name(members + strlen(members), (int)(sizeof(members) - strlen(members) - 1), names[index],
			BROWSER_PLAYER_NAME_LENGTH);
		/* (the player key, or this device's token: site_request) */
		status = site_request("/v1/claim", members, response, sizeof(response), error, sizeof(error), &no_key);
		if (no_key)
			break;
		response[strcspn(response, "\r\n")] = 0;
#ifdef HALO_DELTA_IDENTITY
		/* (a copy the site hadn't seen is known now: it enrolls soon) */
		if (status == 200)
			identity_confirmed(&identity, identity_clock());
#endif
		if (status == 200 && !strncmp(response, "ok ", 3))
			platform_log("Game list: %s's line confirmed (player %s)", name, response + 3);
		else if (status == 200)
			platform_log("Game list: %s's line confirmed", name);
		else if (status == 404 || !status)
			retry = 1;
		else
			platform_log("Game list: %s's line was not confirmed (%s)", name, response);
	}
	pthread_mutex_lock(&browser_lock);
	if (no_key)
	{
		browser.claim_count = 0;
		pthread_mutex_unlock(&browser_lock);
		return;
	}
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
#ifdef HALO_DELTA_IDENTITY
	/* (this device's token in place of the key: site_request) */
	if (safe_for_key(url))
	{
		int no_key = 0;

		status = site_request("/v1/link", "", response, sizeof(response), error, sizeof(error), &no_key);
		key[0] = 0;
		if (no_key)
		{
			platform_log("Game list: the profile page needs a signed-in device or a player key");
			return;
		}
	}
	else
#endif
	{
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
	}
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

/* ---------- Link Profile (the browser thread) */

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

#ifdef HALO_DELTA_IDENTITY
/* ---------- sign-in by code (Quick Connect; docs/delta.md, Devices)

On a site that speaks device identity, Link Profile asks for a code with
this copy's device key (POST /v1/identity/device/start, signed) and works
the code out itself, so the player key never goes. The player types it in
at /connect, or on another signed-in device; the game hears so
(/v1/identity/device/poll), asks its player, and answers signed over the
profile it showed (/v1/identity/device/confirm), which gives it its
certificate. A site that doesn't speak identity gets the old Link Profile,
with the key, while the key may go (identity_key_allowed). */

/* the marker of a device its player signed out (no certificate, no key
sent, until it signs in again by code) */
static void signed_out_mark(int on)
{
	char path[1024];
	FILE *file;

	identity_path("delta_device.out", path, sizeof(path));
	if (!on)
	{
		remove(path);
		return;
	}
	file = fopen(path, "w");
	if (file)
		fclose(file);
}

/* a code for this device: 1 when done (shown, or failed with a message), 0
for the old Link Profile with the key */
static int quick_connect_start(int serial, const unsigned short *name)
{
	unsigned char seed[IDENTITY_SEED_SIZE], public_key[IDENTITY_KEY_SIZE], device_id[IDENTITY_ID_SIZE];
	unsigned char nonce[IDENTITY_NONCE_SIZE], message[IDENTITY_MESSAGE_MOST], signature[IDENTITY_SIGNATURE_SIZE];
	unsigned char code_nonce[IDENTITY_NONCE_SIZE];
	unsigned char qr[qrcodegen_BUFFER_LEN_FOR_VERSION(CONNECT_QR_VERSION)];
	unsigned char work[qrcodegen_BUFFER_LEN_FOR_VERSION(CONNECT_QR_VERSION)];
	char nonce_hex[2 * IDENTITY_NONCE_SIZE + 1], audience[IDENTITY_AUDIENCE_MOST + 1];
	char url[512], body[768], response[1024], error[256], page[640], player[64];
	char code[IDENTITY_CODE_TEXT], token[IDENTITY_POLL_TOKEN_SIZE];
	unsigned long interval = 0, lifetime = 0;
	int status = 0, size = 0, attempt, x, y, from_nonce = 0;

	if (!identity_site(audience, sizeof(audience)))
		return 0;
	identity_check_key_change();
	read_certificate_file();
	/* (a site that said it doesn't speak identity, lately) */
	if (identity.tokens_off_until && identity_clock() < identity.tokens_off_until)
		return 0;
	utf8_from_name(name, BROWSER_PLAYER_NAME_LENGTH, player, sizeof(player));
	for (attempt = 0; attempt < 2; attempt++)
	{
		status = identity_nonce(nonce_hex, nonce, response, sizeof(response));
		if (status != 200)
		{
			from_nonce = 1;
			break;
		}
		if (!device_key(seed, public_key, device_id))
		{
			connect_failed(serial, "Link Profile could not read or make this device's key.");
			return 1;
		}
		size = identity_start_message(audience, nonce, public_key, message);
		if (size > 0)
			identity_sign(seed, message, size, signature);
		identity_wipe(seed, sizeof(seed));
		if (size <= 0 || identity_start_body(body, sizeof(body), public_key, nonce_hex, signature,
			local_platform_name(), build_identity_version(), player) < 0)
		{
			connect_failed(serial, "Link Profile could not make its request.");
			return 1;
		}
		server_url("/v1/identity/device/start", url, sizeof(url));
		status = posix_browser_request(url, body, "application/json", response, sizeof(response), error, sizeof(error));
		/* (a key removed from its profile: a new one, once) */
		if (status == 403 && identity_json_true(response, "revoked") && !attempt)
		{
			identity_new_device();
			platform_log("Delta identity: this device was removed from its profile; a new device key signs in");
			continue;
		}
		break;
	}
	/* (only a site whose nonce is 404, one without device identity, gets
	the old Link Profile with the key, and only while the key may go) */
	if (from_nonce && status == 404)
	{
		identity_answered(&identity, 404, "", identity_clock());
		if (identity_key_allowed(&identity, identity_clock()))
			return 0;
	}
	if (status != 200 || !identity_start_answer(response, code_nonce, token, &interval, &lifetime))
	{
		platform_log("Game list: Link Profile: no code (%d)", status);
		connect_failed(serial, status == 429 ? "Too many codes. Try again later." :
			status == 503 ? "The site is busy. Try again in a minute." :
			status ? "The site gave no code." : error);
		return 1;
	}
	identity_device_code(public_key, code_nonce, code);
	server_url("/connect?code=", page, sizeof(page));
	strncat(page, code, sizeof(page) - strlen(page) - 1);
	if (qrcodegen_encodeText(page, work, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, CONNECT_QR_VERSION,
		qrcodegen_Mask_AUTO, true))
	{
		size = qrcodegen_getSize(qr);
	}
	else
		size = 0;
	pthread_mutex_lock(&browser_lock);
	if (browser.connect_serial == serial)
	{
		browser.connect_state = BROWSER_CONNECT_WAITING;
		browser.connect_quick = 1;
		snprintf(browser.connect_code, sizeof(browser.connect_code), "%s", code);
		snprintf(browser.connect_token, sizeof(browser.connect_token), "%s", token);
		browser.connect_time = p2p_now();
		browser.connect_lifetime = lifetime * 1000;
		browser.connect_poll_time = browser.connect_time;
		browser.connect_qr_size = size;
		for (y = 0; y < size; y++)
		{
			for (x = 0; x < size; x++)
				browser.connect_qr[y * size + x] = qrcodegen_getModule(qr, x, y) ? 1 : 0;
		}
	}
	pthread_mutex_unlock(&browser_lock);
	identity_wipe(token, sizeof(token));
	return 1;
}

/* a JSON member's text into out (a null or none: empty) */
static void member_text(const char *json, const char *name, char *out, int size)
{
	if (identity_json_member(json, name, out, size) <= 0 || !strcmp(out, "null"))
		out[0] = 0;
}

/* how the code is going, or the player's answer (signed over the profile
shown): the answer as connect_line takes the old one's ("confirm <handle>
[<previous>]", ...), status 0 for none */
static int quick_connect_ask(const char *token, int answer, const char *handle, char *line, int line_size)
{
	unsigned char seed[IDENTITY_SEED_SIZE], public_key[IDENTITY_KEY_SIZE], device_id[IDENTITY_ID_SIZE];
	unsigned char nonce[IDENTITY_NONCE_SIZE], message[IDENTITY_MESSAGE_MOST], signature[IDENTITY_SIGNATURE_SIZE];
	char nonce_hex[2 * IDENTITY_NONCE_SIZE + 1], audience[IDENTITY_AUDIENCE_MOST + 1];
	char url[512], body[512], error[256], state[16], who[64], previous[64];
	static char response[IDENTITY_RESPONSE];
	int status, size;

	line[0] = 0;
	if (!answer)
	{
		server_url("/v1/identity/device/poll", url, sizeof(url));
		snprintf(body, sizeof(body), "{\"poll_token\": \"%s\"}", token);
		status = posix_browser_request(url, body, "application/json", response, sizeof(response), error, sizeof(error));
		if (status != 200)
			return status == 429 ? 0 : status;
		member_text(response, "state", state, sizeof(state));
		member_text(response, "handle", who, sizeof(who));
		member_text(response, "previous", previous, sizeof(previous));
		if (!strcmp(state, "confirm") || !strcmp(state, "connected"))
			snprintf(line, (size_t)line_size, "%s %s%s%s", state, who, previous[0] ? " " : "", previous);
		else
			snprintf(line, (size_t)line_size, "%s", state[0] ? state : "expired");
		return 200;
	}
	if (!identity_site(audience, sizeof(audience)))
		return 0;
	status = identity_nonce(nonce_hex, nonce, response, sizeof(response));
	if (status != 200 || !device_key(seed, public_key, device_id))
		return 0;
	size = identity_confirm_message(audience, nonce, device_id, answer > 0, handle, message);
	if (size > 0)
		identity_sign(seed, message, size, signature);
	identity_wipe(seed, sizeof(seed));
	if (size <= 0 || identity_confirm_body(body, sizeof(body), token, device_id, nonce_hex, signature, answer > 0) < 0)
		return 0;
	server_url("/v1/identity/device/confirm", url, sizeof(url));
	status = posix_browser_request(url, body, "application/json", response, sizeof(response), error, sizeof(error));
	identity_wipe(body, sizeof(body));
	/* (a nonce or signature refused: asked again, as an answer not heard) */
	if (status == 401 || status == 429 || status == 503)
		return 0;
	if (status != 200)
	{
		snprintf(line, (size_t)line_size, "%s", "The site refused the sign-in.");
		return status;
	}
	member_text(response, "state", state, sizeof(state));
	if (strcmp(state, "connected"))
	{
		snprintf(line, (size_t)line_size, "%s", state[0] ? state : "expired");
		return 200;
	}
	/* the certificate: this device is the account's now (a device signed in
	by code with no copy keeps no copy ID: the player key isn't its) */
	{
		unsigned char certificate[IDENTITY_DEVICE_MOST];
		struct identity_device device;
		int certificate_size, issuer_size;
		unsigned char issuer[IDENTITY_ISSUER_SIZE];

		struct identity_state saved = identity;
		/* (nothing changes here until the new certificate checks: the
		chain, the issuer's signature and this device's key; checked on a
		copy of the state, put back after) */
		if (!identity_certificate_answer(response, certificate, &certificate_size, issuer, &issuer_size) ||
			!identity_parse_device(certificate, certificate_size, &device) ||
			!take_certificate(certificate, certificate_size, issuer, issuer_size, 0, 0))
		{
			identity = saved;
			identity_wipe(&saved, sizeof(saved));
			snprintf(line, (size_t)line_size, "%s", "The site's certificate did not check.");
			return 502;
		}
		identity = saved;
		identity_wipe(&saved, sizeof(saved));
		identity.off = 0;
		identity.signed_out = 0;
		identity.wait_until = 0;
		if (!(device.flags & IDENTITY_FLAG_LEGACY))
			identity_wipe(identity_copy, sizeof(identity_copy));
		identity_certificate(200, response, 0);
		signed_out_mark(0);
		identity_wipe(identity.token, sizeof(identity.token));
		member_text(response, "handle", who, sizeof(who));
		snprintf(line, (size_t)line_size, "connected %s", who[0] ? who : handle);
		platform_log("Delta identity: this device signed in by code");
	}
	return 200;
}

/* this device signed out by its player: told to the site (signed), then
its certificate dropped and the key kept; nothing goes as the account until
it signs in again. 1 when done */
static int device_sign_out(void)
{
	unsigned char seed[IDENTITY_SEED_SIZE], public_key[IDENTITY_KEY_SIZE], device_id[IDENTITY_ID_SIZE];
	unsigned char nonce[IDENTITY_NONCE_SIZE], message[IDENTITY_MESSAGE_MOST], signature[IDENTITY_SIGNATURE_SIZE];
	char nonce_hex[2 * IDENTITY_NONCE_SIZE + 1], audience[IDENTITY_AUDIENCE_MOST + 1];
	char url[512], body[512], response[512], error[256], path[1024];
	int status, size;

	if (!identity_site(audience, sizeof(audience)))
		return 0;
	read_certificate_file();
	status = identity_nonce(nonce_hex, nonce, response, sizeof(response));
	if (status != 200 || !device_key(seed, public_key, device_id))
		return 0;
	size = identity_signout_message(audience, nonce, device_id, message);
	if (size > 0)
		identity_sign(seed, message, size, signature);
	identity_wipe(seed, sizeof(seed));
	if (size <= 0 || identity_signed_body(body, sizeof(body), device_id, nonce_hex, signature) < 0)
		return 0;
	server_url("/v1/identity/device/signout", url, sizeof(url));
	status = posix_browser_request(url, body, "application/json", response, sizeof(response), error, sizeof(error));
	/* (404: the site doesn't know it; 403: off already) */
	if (status != 200 && status != 403 && status != 404)
		return 0;
	identity_path("delta_device.cert", path, sizeof(path));
	remove(path);
	identity_wipe(&identity.token, sizeof(identity.token));
	identity.has_certificate = 0;
	identity.signed_out = 1;
	signed_out_mark(1);
	/* (no more proofs in games from it: verified names) */
	pthread_mutex_lock(&browser_lock);
	shown_valid = 0;
	pthread_mutex_unlock(&browser_lock);
	platform_log("Delta identity: this device signed out");
	return 1;
}
#endif

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
	if (!safe_for_key(url))
	{
		connect_failed(serial, "Link Profile needs an HTTPS game list (network.browser_url).");
		return;
	}
#ifdef HALO_DELTA_IDENTITY
	/* (sign-in by code with the device key; the player key's way only on a
	site without it, while the key may go) */
	if (quick_connect_start(serial, name))
		return;
	if (!identity_key_allowed(&identity, identity_clock()))
	{
		connect_failed(serial, "The site isn't answering. Try again in a minute.");
		return;
	}
	pthread_mutex_lock(&browser_lock);
	if (browser.connect_serial == serial)
		browser.connect_quick = 0;
	pthread_mutex_unlock(&browser_lock);
#endif
	if (!player_key(key))
	{
		connect_failed(serial, "Link Profile could not read or make your player key.");
		return;
	}
	p2p_hex(key, PLAYER_KEY_SIZE, key_text);
	snprintf(body, sizeof(body), "{\"key\": \"%s\"", key_text);
	if (name[0])
	{
		strcat(body, ", \"name\": ");
		json_name(body + strlen(body), (int)(sizeof(body) - strlen(body) - 2), name, BROWSER_PLAYER_NAME_LENGTH);
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
		platform_log("Game list: Link Profile: no code (%s)", status ? response : error);
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

/* ---------- the profile this copy is linked to */

static void start_thread(void);

/* the file that keeps it: the player ID it was linked as, and the handle */
static void profile_file(char *path, int size)
{
	snprintf(path, (size_t)size, "%s/game_list_profile.txt", platform_save_root());
}

/* linked to handle (under the lock): known from now on, and kept (keep)
for the next runs (by the browser thread: check_profile) */
static void profile_linked_to(const char *handle, int keep)
{
	if (!handle[0])
		return;
	browser.profile_linked = 1;
	snprintf(browser.profile_handle, sizeof(browser.profile_handle), "%s", handle);
	if (keep)
		browser.profile_save_wanted = 1;
}

/* the kept link, read once (only the current player key's; the player key's
lock is taken without the browser's) */
static void read_profile_file(void)
{
	char path[512], line[160], id[2 * PLAYER_ID_SIZE + 1], handle[64];
	FILE *file;
	int read;

	pthread_mutex_lock(&browser_lock);
	read = !browser.profile_read;
	browser.profile_read = 1;
	pthread_mutex_unlock(&browser_lock);
	if (!read || !browser_player_id(id, sizeof(id)))
		return;
	profile_file(path, sizeof(path));
	file = fopen(path, "r");
	if (!file)
		return;
	if (fgets(line, sizeof(line), file) && !strncmp(line, id, strlen(id)) && line[strlen(id)] == ' ')
	{
		line[strcspn(line, "\r\n")] = 0;
		connect_handle(line + strlen(id) + 1, handle, sizeof(handle));
		pthread_mutex_lock(&browser_lock);
		if (!browser.profile_linked)
			profile_linked_to(handle, 0);
		pthread_mutex_unlock(&browser_lock);
	}
	fclose(file);
}

/* the browser thread's (the player key's lock is taken without the
browser's): a new link kept, and the site asked once a run whether the
player's own ID has a profile (one made from this copy's MY PROFILE link):
GET /v1/profiles/<ID>, its "handle" */
static void check_profile(void)
{
	char url[512], path[512], id[2 * PLAYER_ID_SIZE + 1], response[1024], error[256], handle[64];
	const char *found;
	int status, wanted, save;
	FILE *file;

	pthread_mutex_lock(&browser_lock);
	save = browser.profile_save_wanted;
	browser.profile_save_wanted = 0;
	snprintf(handle, sizeof(handle), "%s", browser.profile_handle);
	wanted = browser.profile_check_wanted && !browser.profile_check_done &&
		elapsed(browser.profile_check_time, PROFILE_CHECK_INTERVAL);
	if (wanted)
		browser.profile_check_time = p2p_now();
	pthread_mutex_unlock(&browser_lock);
	if (!(save || wanted) || !browser_player_id(id, sizeof(id)))
		return;
	profile_file(path, sizeof(path));
	if (save && (file = fopen(path, "w")) != NULL)
	{
		fprintf(file, "%s %s\n", id, handle);
		fclose(file);
	}
	if (!wanted)
		return;
	snprintf(path, sizeof(path), "/v1/profiles/%s", id);
	server_url(path, url, sizeof(url));
	status = posix_browser_request(url, NULL, NULL, response, sizeof(response), error, sizeof(error));
	handle[0] = 0;
	found = status == 200 ? strstr(response, "\"handle\": \"") : NULL;
	if (found)
	{
		int used = 0;

		for (found += strlen("\"handle\": \""); *found && *found != '"' && *found != '\\' &&
			used < (int)sizeof(handle) - 1; found++)
		{
			if ((unsigned char)*found >= 0x20)
				handle[used++] = *found;
		}
		handle[used] = 0;
	}
	pthread_mutex_lock(&browser_lock);
	if (handle[0] && !browser.profile_linked)
		profile_linked_to(handle, 0);
	/* (an answer: done for this run; none, a few tries) */
	if (status == 200 || status == 404 || ++browser.profile_check_attempts >= PROFILE_CHECK_ATTEMPTS)
		browser.profile_check_done = 1;
	pthread_mutex_unlock(&browser_lock);
}

int browser_profile_linked(char *handle, int size)
{
	int linked;

	pthread_once(&browser_once, start_thread);
	read_profile_file();
	pthread_mutex_lock(&browser_lock);
	browser.profile_check_wanted = 1;
	linked = browser.profile_linked;
	if (handle && size > 0)
		snprintf(handle, (size_t)size, "%s", linked ? browser.profile_handle : "");
	pthread_mutex_unlock(&browser_lock);
	return linked;
}

/* ---------- Delta Profile in the game (the browser thread)

The game asks the site about this copy's own profile with its player key,
as /v1/link and /v1/claim send it (over HTTPS alone: safe_for_key): the
profile it counts for, its copies, roles and privacy (POST
/v1/game/profile), this copy unlinked (/v1/game/unlink) and a privacy
setting changed (/v1/game/privacy). The site works out the player ID and
keeps no key. The answer is kept in memory only; debug.txt says whether it
loaded, never what it holds. */

/* a request with the player key and fields (JSON members, or ""): its HTTP
status (0: no answer); *no_key when the key can't go (no HTTPS game list,
no player key) */
static int game_request(const char *path, const char *fields, char *response, int size, char *error,
	int error_size, int *no_key)
{
	/* (or this device's token in its place: site_request) */
	return site_request(path, fields, response, size, error, error_size, no_key);
}

/* (under the lock) this copy is linked to no profile now: known, and the
link kept in the save root forgotten */
static void profile_unlinked(void)
{
	if (browser.profile_linked)
		browser.profile_forget_wanted = 1;
	browser.profile_linked = 0;
	browser.profile_handle[0] = 0;
}

static void game_action_done(int action, const char *message)
{
	browser.game_action = action;
	snprintf(browser.game_message, sizeof(browser.game_message), "%s", message);
}

#define NO_KEY_MESSAGE "No player key"
#define NOT_ANSWERING_MESSAGE "Site not answering"

static void update_game_profile(void)
{
	static char response[GAME_PROFILE_RESPONSE];
	static struct delta_profile read;
	char error[256], fields[256], path[512];
	char clan_action[sizeof(browser.game_clan_action)];
	char friends_requests[24], friends_join[24], friends_presence[24];
	char clan_handle[sizeof(browser.game_clan_handle)];
	char revoke[sizeof(browser.device_revoke)];
	int wanted, unlink, privacy_count, status, no_key, ok, clan_id, index, sign_out;

	pthread_mutex_lock(&browser_lock);
	wanted = browser.game_profile_wanted && elapsed(browser.game_profile_asked, GAME_PROFILE_INTERVAL);
	unlink = browser.game_unlink_wanted;
	browser.game_unlink_wanted = 0;
	privacy_count = browser.game_privacy_count;
	fields[0] = 0;
	for (index = 0; index < privacy_count; index++)
	{
		snprintf(fields + strlen(fields), sizeof(fields) - strlen(fields), "%s\"%s\": %s", index ? ", " : "",
			browser.game_privacy_fields[index], browser.game_privacy_values[index] ? "true" : "false");
	}
	browser.game_privacy_count = 0;
	memcpy(friends_requests, browser.game_friends_requests, sizeof(friends_requests));
	memcpy(friends_join, browser.game_friends_join, sizeof(friends_join));
	memcpy(friends_presence, browser.game_friends_presence, sizeof(friends_presence));
	browser.game_friends_requests[0] = browser.game_friends_join[0] = browser.game_friends_presence[0] = 0;
	memcpy(clan_action, browser.game_clan_action, sizeof(clan_action));
	memcpy(clan_handle, browser.game_clan_handle, sizeof(clan_handle));
	clan_id = browser.game_clan_id;
	browser.game_clan_action[0] = 0;
	sign_out = browser.device_sign_out_wanted;
	browser.device_sign_out_wanted = 0;
	memcpy(revoke, browser.device_revoke, sizeof(revoke));
	browser.device_revoke[0] = 0;
	if (browser.profile_forget_wanted)
	{
		browser.profile_forget_wanted = 0;
		profile_file(path, sizeof(path));
		remove(path);
	}
	pthread_mutex_unlock(&browser_lock);

	if (unlink)
	{
		status = game_request("/v1/game/unlink", "", response, sizeof(response), error, sizeof(error), &no_key);
		pthread_mutex_lock(&browser_lock);
		if (no_key)
			game_action_done(BROWSER_PROFILE_ACTION_FAILED, NO_KEY_MESSAGE);
		else if (status == 200 || status == 404)
		{
			/* (404: linked to none already) */
			game_action_done(BROWSER_PROFILE_ACTION_DONE, "Unlinked");
			profile_unlinked();
			memset(&browser.game_profile, 0, sizeof(browser.game_profile));
			browser.game_profile_serial++;
			browser.game_profile_wanted = 1;
			wanted = 0;
		}
		else if (status == 409)
			game_action_done(BROWSER_PROFILE_ACTION_FIRST_COPY, "");
		else
			game_action_done(BROWSER_PROFILE_ACTION_FAILED, NOT_ANSWERING_MESSAGE);
		pthread_mutex_unlock(&browser_lock);
		platform_log("Delta Profile: unlink: %s", status == 200 || status == 404 ? "done" : no_key ? "no player key" :
			status == 409 ? "the first copy" : "no answer");
	}
#ifdef HALO_DELTA_IDENTITY
	if (sign_out)
	{
		ok = device_sign_out();
		pthread_mutex_lock(&browser_lock);
		if (ok)
		{
			game_action_done(BROWSER_PROFILE_ACTION_DONE, "Signed out");
			profile_unlinked();
			memset(&browser.game_profile, 0, sizeof(browser.game_profile));
			browser.game_profile_has = 0;
			browser.game_profile_serial++;
		}
		else
			game_action_done(BROWSER_PROFILE_ACTION_FAILED, NOT_ANSWERING_MESSAGE);
		pthread_mutex_unlock(&browser_lock);
	}
	if (revoke[0])
	{
		snprintf(fields, sizeof(fields), "\"device_id\": \"%s\"", revoke);
		status = game_request("/v1/identity/device/revoke", fields, response, sizeof(response), error, sizeof(error),
			&no_key);
		pthread_mutex_lock(&browser_lock);
		if (status == 200)
		{
			game_action_done(BROWSER_PROFILE_ACTION_DONE, "Revoked");
			browser.game_profile_wanted = 1;
			wanted = 0;
		}
		else if (status == 404)
			game_action_done(BROWSER_PROFILE_ACTION_FAILED, "That device is gone already.");
		else if (status == 401 || status == 403)
			game_action_done(BROWSER_PROFILE_ACTION_FAILED, "Do that on the site.");
		else
			game_action_done(BROWSER_PROFILE_ACTION_FAILED, NOT_ANSWERING_MESSAGE);
		pthread_mutex_unlock(&browser_lock);
		platform_log("Delta Profile: revoke a device: %s", status == 200 ? "done" : status ? "refused" : "no answer");
	}
#else
	(void)sign_out;
#endif
	if (privacy_count)
	{
		status = game_request("/v1/game/privacy", fields, response, sizeof(response), error, sizeof(error), &no_key);
		pthread_mutex_lock(&browser_lock);
		if (no_key)
			game_action_done(BROWSER_PROFILE_ACTION_FAILED, NO_KEY_MESSAGE);
		else if (status == 200 && delta_profile_parse_privacy(response, &browser.game_profile))
		{
			game_action_done(BROWSER_PROFILE_ACTION_DONE, "Saved on the site.");
			browser.game_profile_serial++;
		}
		else if (status == 403)
			game_action_done(BROWSER_PROFILE_ACTION_FAILED, "Profile locked");
		else if (status == 503)
			game_action_done(BROWSER_PROFILE_ACTION_FAILED, "The site can't change that now.");
		else
			game_action_done(BROWSER_PROFILE_ACTION_FAILED, NOT_ANSWERING_MESSAGE);
		pthread_mutex_unlock(&browser_lock);
	}
	if (friends_requests[0] || friends_join[0] || friends_presence[0])
	{
		char friends_fields[160];

		if (social_friends_privacy_fields(friends_fields, sizeof(friends_fields), friends_requests, friends_join,
			friends_presence) < 0)
		{
			status = -1;
		}
		else
		{
			status = game_request("/v1/game/friends", friends_fields, response, sizeof(response), error, sizeof(error),
				&no_key);
		}
		pthread_mutex_lock(&browser_lock);
		if (status < 0)
			game_action_done(BROWSER_PROFILE_ACTION_FAILED, "Can't do that.");
		else if (no_key)
			game_action_done(BROWSER_PROFILE_ACTION_FAILED, NO_KEY_MESSAGE);
		else if (status == 200)
		{
			/* (its answer is the friends' state: the choice kept as made, and
			the profile asked for again) */
			if (friends_requests[0])
				snprintf(browser.game_profile.friend_requests, sizeof(browser.game_profile.friend_requests), "%s",
					friends_requests);
			if (friends_join[0])
				snprintf(browser.game_profile.who_can_join, sizeof(browser.game_profile.who_can_join), "%s", friends_join);
			if (friends_presence[0])
				snprintf(browser.game_profile.clan_presence, sizeof(browser.game_profile.clan_presence), "%s",
					friends_presence);
			game_action_done(BROWSER_PROFILE_ACTION_DONE, "Saved on the site.");
			browser.game_profile_serial++;
			social_poll_soon(&browser.social_poll);
		}
		else if (status == 403)
			game_action_done(BROWSER_PROFILE_ACTION_FAILED, "Profile locked");
		else
			game_action_done(BROWSER_PROFILE_ACTION_FAILED, NOT_ANSWERING_MESSAGE);
		pthread_mutex_unlock(&browser_lock);
		platform_log("Delta Profile: friends privacy %s", status == 200 ? "saved" : status > 0 ? "refused" : "not sent");
	}
	if (clan_action[0])
	{
		if (clan_handle[0])
			snprintf(fields, sizeof(fields), "\"action\": \"%s\", \"handle\": \"%s\"", clan_action, clan_handle);
		else
			snprintf(fields, sizeof(fields), "\"action\": \"%s\", \"clan\": %d", clan_action, clan_id);
		status = game_request("/v1/game/clan", fields, response, sizeof(response), error, sizeof(error), &no_key);
		pthread_mutex_lock(&browser_lock);
		if (no_key)
			game_action_done(BROWSER_PROFILE_ACTION_FAILED, NO_KEY_MESSAGE);
		else if (status == 200 && delta_profile_parse_clan(response, &browser.game_profile))
		{
			game_action_done(BROWSER_PROFILE_ACTION_DONE, !strcmp(clan_action, "accept") ? "Joined" :
				!strcmp(clan_action, "leave") ? "Left" : !strcmp(clan_action, "invite") ? "Invite sent" :
				!strcmp(clan_action, "kick") ? "Removed" : "Declined");
			browser.game_profile_serial++;
		}
		else if (status >= 400 && status < 500 && status != 429)
		{
			/* (the site's words: "Make another member leader first.") */
			const char *said = strstr(response, "\"error\"");
			char message[96] = "";

			if (said && (said = strchr(said + 7, '"')) != NULL)
			{
				size_t length = strcspn(said + 1, "\"\\");

				snprintf(message, sizeof(message), "%.*s", (int)(length < sizeof(message) - 1 ? length : sizeof(message) - 1),
					said + 1);
			}
			game_action_done(BROWSER_PROFILE_ACTION_FAILED, message[0] ? message : "Not allowed");
			browser.game_profile_wanted = 1;
		}
		else
			game_action_done(BROWSER_PROFILE_ACTION_FAILED, NOT_ANSWERING_MESSAGE);
		pthread_mutex_unlock(&browser_lock);
		platform_log("Delta Profile: clan %s: %s", clan_action, status == 200 ? "done" : no_key ? "no player key" :
			status ? "refused" : "no answer");
	}
	if (!wanted)
		return;

	status = game_request("/v1/game/profile", "", response, sizeof(response), error, sizeof(error), &no_key);
	ok = status == 200 && delta_profile_parse(response, &read);
#ifdef HALO_DELTA_IDENTITY
	/* (signed out: as linked to none, until it signs in again) */
	if (no_key && identity.signed_out)
	{
		memset(&read, 0, sizeof(read));
		ok = 1;
		no_key = 0;
	}
#endif
	pthread_mutex_lock(&browser_lock);
	browser.game_profile_asked = p2p_now();
	browser.game_profile_wanted = 0;
	if (no_key)
		browser.game_profile_state = BROWSER_PROFILE_NO_KEY;
	else if (ok)
	{
		browser.game_profile = read;
		browser.game_profile_has = 1;
		browser.game_profile_state = BROWSER_PROFILE_LOADED;
		browser.game_profile_time = p2p_now();
		browser.game_profile_serial++;
#ifndef HALO_CONSOLE_NO_GUIDE
		if (read.has_clan)
		{
			browser.social_whole = 1;
			social_poll_soon(&browser.social_poll);
		}
#endif
		/* (what browser_profile_linked says follows the site's word) */
		if (read.linked && !read.locked && (!browser.profile_linked || strcmp(browser.profile_handle, read.handle)))
			profile_linked_to(read.handle, 1);
		else if (!read.linked)
			profile_unlinked();
	}
	else
		browser.game_profile_state = BROWSER_PROFILE_FAILED;
	pthread_mutex_unlock(&browser_lock);
	if (ok)
		platform_log("Delta Profile: loaded");
	else
		platform_log("Delta Profile: not loaded (%s)", no_key ? "no HTTPS game list or player key" :
			status == 429 ? "too many requests" : status ? "an answer not understood" : "no answer");
	memset(response, 0, sizeof(response));
	memset(&read, 0, sizeof(read));
}

#ifdef HALO_DELTA_IDENTITY
/* ---------- Link a device (the browser thread): another device's code
approved with this one's token (POST /v1/identity/device/approve, then
/approved every APPROVE_POLL_INTERVAL until that device answers) */

/* a JSON string member anywhere in text (the first), into out */
static void any_member(const char *text, const char *name, char *out, int size)
{
	char pattern[48];
	const char *found;
	int used = 0;

	out[0] = 0;
	snprintf(pattern, sizeof(pattern), "\"%s\": \"", name);
	found = strstr(text, pattern);
	if (!found)
		return;
	for (found += strlen(pattern); *found && *found != '"' && *found != '\\' && used < size - 1; found++)
	{
		if ((unsigned char)*found >= 0x20 && (unsigned char)*found < 0x7F)
			out[used++] = *found;
	}
	out[used] = 0;
}

static void update_approve(void)
{
	static char response[2048];
	char code[16], fields[64], error[256], state_text[16];
	int serial, state, status, no_key = 0, due, answer;

	pthread_mutex_lock(&browser_lock);
	state = browser.approve_state;
	serial = browser.approve_serial;
	answer = browser.approve_answer;
	memcpy(code, browser.approve_code, sizeof(code));
	due = state == BROWSER_APPROVE_SENDING || state == BROWSER_APPROVE_ANSWERING ||
		(state == BROWSER_APPROVE_WAITING && elapsed(browser.approve_time, APPROVE_POLL_INTERVAL));
	if (due)
		browser.approve_time = p2p_now();
	pthread_mutex_unlock(&browser_lock);
	if (!due)
		return;
	response[0] = 0;
	if (state == BROWSER_APPROVE_SENDING || state == BROWSER_APPROVE_ANSWERING)
	{
		if (state == BROWSER_APPROVE_SENDING)
			snprintf(fields, sizeof(fields), "\"code\": \"%s\"", code);
		else
			snprintf(fields, sizeof(fields), "\"approve\": %s", answer > 0 ? "true" : "false");
		status = site_request("/v1/identity/device/approve", fields, response, sizeof(response), error,
			sizeof(error), &no_key);
	}
	else
		status = site_request("/v1/identity/device/approved", "", response, sizeof(response), error, sizeof(error),
			&no_key);
	pthread_mutex_lock(&browser_lock);
	if (browser.approve_serial == serial)
	{
		if (state == BROWSER_APPROVE_SENDING && status == 202)
		{
			/* (a device's code: shown first; another's goes straight on) */
			browser.approve_state = strstr(response, "\"shown\": true") ? BROWSER_APPROVE_SHOWN :
				BROWSER_APPROVE_WAITING;
			any_member(response, "handle", browser.approve_handle, sizeof(browser.approve_handle));
			any_member(response, "name", browser.approve_name, sizeof(browser.approve_name));
			any_member(response, "platform", browser.approve_platform, sizeof(browser.approve_platform));
			any_member(response, "warning", browser.approve_message, sizeof(browser.approve_message));
		}
		else if (state == BROWSER_APPROVE_ANSWERING && (status == 200 || status == 202))
		{
			browser.approve_state = answer > 0 ? BROWSER_APPROVE_WAITING : BROWSER_APPROVE_DENIED;
			browser.approve_time = p2p_now();
		}
		else if (state == BROWSER_APPROVE_WAITING && status == 200)
		{
			any_member(response, "state", state_text, sizeof(state_text));
			if (!strcmp(state_text, "connected"))
				browser.approve_state = BROWSER_APPROVE_DONE;
			else if (!strcmp(state_text, "declined"))
				browser.approve_state = BROWSER_APPROVE_DECLINED;
			else if (strcmp(state_text, "waiting"))
				browser.approve_state = BROWSER_APPROVE_EXPIRED;
			if (browser.approve_state == BROWSER_APPROVE_DONE)
				browser.game_profile_wanted = 1;
		}
		else if (status && status != 429)
		{
			browser.approve_state = BROWSER_APPROVE_FAILED;
			any_member(response, "error", browser.approve_message, sizeof(browser.approve_message));
			if (!browser.approve_message[0] || status == 401)
				snprintf(browser.approve_message, sizeof(browser.approve_message), "%s",
					status == 401 ? "Do that on the site." : "The site refused the code.");
		}
		else if (!status && (no_key || state != BROWSER_APPROVE_WAITING))
		{
			browser.approve_state = BROWSER_APPROVE_FAILED;
			snprintf(browser.approve_message, sizeof(browser.approve_message), "%s",
				no_key ? "Sign in on this device first." : "The site isn't answering.");
		}
	}
	pthread_mutex_unlock(&browser_lock);
	memset(response, 0, sizeof(response));
}
#endif

/* a card the site gave (delta_profile_parse_cards; under the lock) */
static void card_found(void *context, const char *id, const struct delta_profile_card *card)
{
	int index;

	(void)context;
	for (index = 0; index < browser.card_count; index++)
	{
		struct player_card *known = &browser.cards[index];

		if (strcmp(known->id, id))
			continue;
		known->state = card ? _card_known : _card_none;
		known->time = p2p_now();
		if (card)
			known->card = *card;
		else
			memset(&known->card, 0, sizeof(known->card));
	}
}

/* the cards of the players the game asked about (browser_player_card),
CARDS_PER_REQUEST at a time: their public profile cards, by the IDs they
chose to share in a game (Delta Peer's profile, network.share_profile).
No key goes; the IDs go to an HTTPS game list alone */
static void update_cards(void)
{
	static char response[GAME_PROFILE_RESPONSE];
	char url[512], body[CARDS_PER_REQUEST * (DELTA_PROFILE_ID_LENGTH + 4) + 32], error[256];
	int index, count = 0, status, read;

	pthread_mutex_lock(&browser_lock);
	if (elapsed(browser.cards_time, browser.cards_failed ? CARDS_RETRY : CARDS_INTERVAL))
	{
		strcpy(body, "{\"ids\": [");
		for (index = 0; index < browser.card_count && count < CARDS_PER_REQUEST; index++)
		{
			if (browser.cards[index].state != _card_wanted)
				continue;
			snprintf(body + strlen(body), sizeof(body) - strlen(body), "%s\"%s\"", count ? ", " : "",
				browser.cards[index].id);
			count++;
		}
		strcat(body, "]}");
	}
	if (count)
		browser.cards_time = p2p_now();
	pthread_mutex_unlock(&browser_lock);
	if (!count)
		return;
	server_url("/v1/game/players", url, sizeof(url));
	if (!safe_for_key(url))
		return;
	status = posix_browser_request(url, body, "application/json", response, sizeof(response), error, sizeof(error));
	pthread_mutex_lock(&browser_lock);
	read = status == 200 ? delta_profile_parse_cards(response, card_found, NULL) : -1;
	browser.cards_failed = read < 0;
	pthread_mutex_unlock(&browser_lock);
	if (read < 0)
		platform_log("Delta Profile: players' cards not loaded (%s)", status ? "an answer not understood" : "no answer");
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
		platform_log("Game list: Link Profile: the game is linked to %s", browser.connect_handle);
		profile_linked_to(browser.connect_handle, 1);
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
static void ask_connect(int serial, const char *token, int answer, int quick, const char *handle)
{
	char url[512], body[128], response[256], error[256];
	int status;

#ifdef HALO_DELTA_IDENTITY
	if (quick)
	{
		error[0] = 0;
		status = quick_connect_ask(token, answer, handle, response, sizeof(response));
		if (!status)
			snprintf(error, sizeof(error), "The site isn't answering.");
	}
	else
#else
	(void)quick;
	(void)handle;
#endif
	{
		server_url(answer ? "/v1/connect/confirm" : "/v1/connect/status", url, sizeof(url));
		snprintf(body, sizeof(body), answer ? "{\"token\": \"%s\", \"accept\": %s}" : "{\"token\": \"%s\"}", token,
			answer > 0 ? "true" : "false");
		status = posix_browser_request(url, body, "application/json", response, sizeof(response), error, sizeof(error));
		memset(body, 0, sizeof(body));
		response[strcspn(response, "\r\n")] = 0;
	}

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
	unsigned short name[BROWSER_PLAYER_NAME_LENGTH];
	char token[CONNECT_TOKEN_LENGTH + 1], handle[64];
	int state, serial, answer, ask, quick;

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
	memcpy(handle, browser.connect_handle, sizeof(handle));
	quick = browser.connect_quick;
	pthread_mutex_unlock(&browser_lock);
	if (state == BROWSER_CONNECT_STARTING)
		start_connect(serial, name);
	else if (ask)
		ask_connect(serial, token, state == BROWSER_CONNECT_CONFIRM ? answer : 0, quick, handle);
	memset(token, 0, sizeof(token));
}

/* ---------- hosting (the browser thread) */

/* the listing of the invite taken off the list */
static void withdraw_invite(const char *invite)
{
	char url[512], form[128], response[256], error[256];

	server_url("/v1/withdraw", url, sizeof(url));
	form[0] = 0;
	form_add(form, sizeof(form), "invite", invite);
	posix_browser_request(url, form, NULL, response, sizeof(response), error, sizeof(error));
}

static void withdraw(void)
{
	if (!browser.listed_invite[0])
		return;
	withdraw_invite(browser.listed_invite);
	platform_log("Game list: the game is no longer listed");
	pthread_mutex_lock(&browser_lock);
	browser.listed_invite[0] = 0;
	browser.announce_tried = 0;
	pthread_mutex_unlock(&browser_lock);
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

		utf8_from_name(roster[index].name, BROWSER_PLAYER_NAME_LENGTH, name, sizeof(name));
		used += snprintf(text + used, (size_t)(size - used), "%s%d:%s", index ? "|" : "", roster[index].team, name);
	}
}

/* updater.c's (server_platform.c's in the dedicated server) */
const char *updater_version(void);

/* (a listed game's machine kinds are Delta Peer's) */
typedef char browser_machine_kinds_check[BROWSER_MACHINE_KINDS == DELTA_PEER_MACHINE_KINDS ? 1 : -1];

/* the build's architecture, as the list takes it */
static const char *build_architecture(void)
{
#if defined(__aarch64__) || defined(__arm64__) || defined(HALO_ANDROID)
	return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
	return "x64";
/* (32-bit ARM ahead of _M_IX86, which halo_linux_prefix.h defines for the
XDK's headers on every 32-bit build) */
#elif defined(__arm__) || defined(_M_ARM)
	return "arm";
#elif defined(__i386__) || defined(_M_IX86)
	return "x86";
#else
	return "";
#endif
}

/* this machine's platform (Delta's registry name), as a player's copy
says it when it confirms its line or reports a game: the site's "Played
on" badges */
static const char *local_platform_name(void)
{
	struct delta_platform_key key;

	delta_peer_local_key(&key);
	return delta_peer_platform_name(key.platform);
}

/* the announcement's fields about the host (Delta List, docs/delta.md):
the Delta major it hosts with (0: the legacy protocol alone), its platform
key, architecture, build, capabilities and its game's machines by platform
(counts only). A list from before them ignores them */
/* (host_rules.c's: whether this host takes no new machines now) */
int host_rules_joins_locked_now(void);

static void host_fields(char *form, int size, const struct hosted_game *game)
{
	unsigned char key[8];
	char text[256], hex[2 * sizeof(key) + 1];
	int kind, used = 0;

	if (game->has_delta)
	{
		snprintf(text, sizeof(text), "%d", game->delta.delta ? DELTA_MAJOR : 0);
		form_add(form, size, "delta", text);
	}
	delta_wire_write_key(key, &game->delta.key);
	p2p_hex(key, (int)sizeof(key), hex);
	form_add(form, size, "platform_key", hex);
	form_add(form, size, "arch", build_architecture());
	form_add(form, size, "build", updater_version());
	/* (Delta Peer's, and whether this host answers Online Games' pings:
	p2p_probe.c, announced here alone) */
	snprintf(text, sizeof(text), "%08x", (unsigned int)game->delta.capabilities |
		(delta_capability_disabled(_delta_capability_ping) ? 0u : 1u << _delta_capability_ping) |
		(config_boolean("network.host_cheats") ? 1u << _delta_capability_host_cheats : 0u));
	form_add(form, size, "capabilities", text);
	/* (joins locked: sv_lock, a moderator's LOCK; the list shows it) */
	if (host_rules_joins_locked_now())
		form_add(form, size, "locked", "1");
	if (!game->has_delta || !game->delta.delta)
		return;
	text[0] = 0;
	for (kind = 0; kind < DELTA_PEER_MACHINE_KINDS && used < (int)sizeof(text) - 32; kind++)
	{
		if (game->delta.machines[kind])
		{
			used += snprintf(text + used, sizeof(text) - (size_t)used, "%s%s:%d", used ? "," : "",
				kind == DELTA_PEER_MACHINE_LEGACY ? "legacy" : delta_peer_platform_name(kind), game->delta.machines[kind]);
		}
	}
	if (text[0])
		form_add(form, size, "machines", text);
}

/* whether the game list is the community's site (network.browser_url's
default, or its first address): badges are only its word, so another list
gets none from this game and none of its badges are shown */
static int badge_list_trusted(void)
{
	static const char *const sites[] = { "https://stats.chupathingy.org", "https://halo.milenko.org" };
	const char *base = config_string("network.browser_url");
	size_t length = strlen(base);
	int index;

	while (length && base[length - 1] == '/')
		length--;
	for (index = 0; index < (int)(sizeof(sites) / sizeof(*sites)); index++)
	{
		if (length == strlen(sites[index]) && !SDL_strncasecmp(base, sites[index], length))
			return 1;
	}
	return 0;
}

#ifdef HALO_SERVER
/* (server/src/server_link.h's, Delta Control's link: plain types) */
int server_link_badge_certificate(unsigned char *certificate, int size);
int server_link_badge_sign(const unsigned char *message, int size, unsigned char *signature);

/* a dedicated server's badge, if it has a good one: its certificate and the
link key's signature of the invite and the time, which the list checks
(badge_cert, badge_time, badge_signature: docs/server-badges.md) */
static void badge_fields(char *form, int size, const char *invite)
{
	unsigned char certificate[SERVER_BADGE_MAXIMUM_SIZE], message[SERVER_BADGE_MESSAGE_SIZE], signature[64];
	char hex[2 * SERVER_BADGE_MAXIMUM_SIZE + 1], lower[BROWSER_INVITE_LENGTH + 1], text[24];
	struct server_badge badge;
	unsigned long long now = (unsigned long long)time(NULL);
	int certificate_size = server_link_badge_certificate(certificate, (int)sizeof(certificate));
	int index, length;

	if (!badge_list_trusted() || !certificate_size || !server_badge_check(certificate, certificate_size, now, &badge))
		return;
	/* (the list reads the invite in lowercase) */
	for (index = 0; invite[index] && index < BROWSER_INVITE_LENGTH; index++)
		lower[index] = (char)(invite[index] >= 'A' && invite[index] <= 'Z' ? invite[index] - 'A' + 'a' : invite[index]);
	lower[index] = 0;
	length = server_badge_announce_message(message, lower, now);
	if (!length || !server_link_badge_sign(message, length, signature))
		return;
	p2p_hex(certificate, certificate_size, hex);
	form_add(form, size, "badge_cert", hex);
	snprintf(text, sizeof(text), "%llu", now);
	form_add(form, size, "badge_time", text);
	p2p_hex(signature, (int)sizeof(signature), hex);
	form_add(form, size, "badge_signature", hex);
}
#endif

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
	/* (an Xbox map pack's map: its name, its pack's and the pack's tag, for
	players without the pack: port/linux/game/map_packs.c) */
	{
		char title[96], pack[96], tag[16];

		if (map_pack_listing(game->map, title, (int)sizeof(title), pack, (int)sizeof(pack), tag, (int)sizeof(tag)))
		{
			form_add(form, sizeof(form), "map_title", title);
			form_add(form, sizeof(form), "map_pack", pack);
			form_add(form, sizeof(form), "map_tag", tag);
		}
	}
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
	snprintf(text, sizeof(text), "%d", delta_legacy_announce());
	form_add(form, sizeof(form), "version", text);
	host_fields(form, sizeof(form), game);
	/* (a dedicated server's playlist, as its host names it: HALO_PLAYLIST_ID,
	the site's id of its verified map set, docs/playlist-maps.md) */
	if (browser_dedicated())
	{
		const char *playlist = getenv("HALO_PLAYLIST_ID");

		if (playlist && playlist[0] && strlen(playlist) <= 32 &&
			!playlist[strspn(playlist, "abcdefghijklmnopqrstuvwxyz0123456789_")])
		{
			form_add(form, sizeof(form), "playlist", playlist);
		}
	}
#ifdef HALO_SERVER
	badge_fields(form, sizeof(form), invite);
#endif
	/* (last: a full one may be cut short, and a list from before rosters
	takes no field of the name) */
	roster_text(game->roster, game->roster_count, roster, sizeof(roster));
	form_add(form, sizeof(form), "roster", roster);

	status = posix_browser_request(url, form, NULL, response, sizeof(response), error, sizeof(error));
	browser.announce_time = p2p_now();
	browser.announced = *game;
	pthread_mutex_lock(&browser_lock);
	browser.announce_tried = 1;
	browser.announce_status = status;
	pthread_mutex_unlock(&browser_lock);
	if (status == 200)
	{
		if (strcmp(browser.listed_invite, invite))
			platform_log("Game list: the game is listed on %s", config_string("network.browser_url"));
		pthread_mutex_lock(&browser_lock);
		snprintf(browser.listed_invite, sizeof(browser.listed_invite), "%s", invite);
		pthread_mutex_unlock(&browser_lock);
		p2p_hosting_invite_announced(invite);
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

	/* (only a public game without a password: its invite lets anyone in,
	so a private game's or a password game's is never listed) */
	hosting = reported && config_boolean("network.list_hosted_games") &&
		p2p_hosting_listable_invite(invite, sizeof(invite));
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
			if (status == 200 && !strncmp(response, "ok ", 3))
				platform_log("Game list: the game's carnage report is at %s/games/%s",
					config_string("network.browser_url"), response + 3);
			else if (status == 200)
				platform_log("Game list: the game's carnage report was sent");
			else
				platform_log("Game list: could not send the carnage report (%s)", status ? response : error);
			free(body);
		}
	}
	free(report);
}

/* a joined game's report, with this copy's player key (or this device's
token: site_request), which shows the server whose report it is (the
server keeps the player ID it works out from it, never the key, as for
/v1/claim) */
static void send_client_report(void)
{
	char response[256], error[256];
	char *report, *members;
	size_t size, length;
	int status, retry, no_key = 1;

	pthread_mutex_lock(&browser_lock);
	report = browser.client_report;
	if (report && browser.client_report_attempts &&
		!elapsed(browser.client_report_time, CLIENT_REPORT_INTERVAL))
	{
		report = NULL;
	}
	pthread_mutex_unlock(&browser_lock);
	if (!report)
		return;
	/* (the report's members after the platform: the report is the browser
	thread's to free or replace only under the lock, and the game's thread
	replaces it only with a new game's) */
	members = NULL;
	pthread_mutex_lock(&browser_lock);
	if (browser.client_report == report)
	{
		size = strlen(report) + 64;
		members = malloc(size);
		if (members)
		{
			snprintf(members, size, "\"platform\": \"%s\", %s", local_platform_name(), report + 1);
			length = strlen(members);
			while (length && (members[length - 1] == '\n' || members[length - 1] == '\r' ||
				members[length - 1] == ' '))
			{
				length--;
			}
			if (length && members[length - 1] == '}')
				length--;
			members[length] = 0;
		}
	}
	pthread_mutex_unlock(&browser_lock);
	status = 0;
	if (members)
	{
		status = site_request("/v1/client_report", members, response, sizeof(response), error, sizeof(error),
			&no_key);
		free(members);
		if (no_key)
			platform_log("Game list: the joined game's report needs an HTTPS game list and a player key");
	}
	if (no_key)
	{
		pthread_mutex_lock(&browser_lock);
		if (browser.client_report == report)
		{
			free(browser.client_report);
			browser.client_report = NULL;
		}
		pthread_mutex_unlock(&browser_lock);
		return;
	}
	response[strcspn(response, "\r\n")] = 0;
	/* (no answer, or the server busy: again in a while) */
	retry = !status || status == 429 || status >= 500;
#ifdef HALO_DELTA_IDENTITY
	if (status == 200)
		identity_confirmed(&identity, identity_clock());
#endif
	if (status == 200 && !strncmp(response, "ok ", 3))
		platform_log("Game list: the joined game's report was taken (%s)", response);
	else if (status == 200)
		platform_log("Game list: the game list did not answer the joined game's report as expected");
	else if (!retry)
		platform_log("Game list: the joined game's report was not taken (%s)", response);
	pthread_mutex_lock(&browser_lock);
	if (browser.client_report == report)
	{
		browser.client_report_attempts++;
		browser.client_report_time = p2p_now();
		if (!retry || browser.client_report_attempts >= CLIENT_REPORT_ATTEMPTS)
		{
			if (retry)
				platform_log("Game list: could not send the joined game's report (%s)", status ? response : error);
			free(browser.client_report);
			browser.client_report = NULL;
		}
	}
	pthread_mutex_unlock(&browser_lock);
}

/* ---------- browsing (the browser thread) */

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

				name_from_utf8(colon + 1, player->name, BROWSER_PLAYER_NAME_LENGTH);
				player->team = (short)atoi(entry);
			}
			game->roster_count++;
		}
		entry = next;
	}
}

/* the host's fields (?fields=17: platform, hosting, protocol, machines;
docs/delta.md, Delta List): what is not known is left 0 */
static void parse_host(char **fields, struct browser_game *game)
{
	char *entry;
	int platform = delta_peer_platform_number(fields[0]);

	if (platform >= 0)
	{
		game->has_platform = 1;
		game->host_platform = (unsigned char)platform;
	}
	game->hosting = !strcmp(fields[1], "official") ? BROWSER_HOSTING_OFFICIAL :
		!strcmp(fields[1], "dedicated") ? BROWSER_HOSTING_DEDICATED :
		!strcmp(fields[1], "player") ? BROWSER_HOSTING_PLAYER : BROWSER_HOSTING_UNKNOWN;
	game->protocol = !strcmp(fields[2], "delta") ? BROWSER_PROTOCOL_DELTA :
		!strcmp(fields[2], "opence") ? BROWSER_PROTOCOL_OPENCE : BROWSER_PROTOCOL_UNKNOWN;
	/* ("pc_linux:2,xbox:1,legacy:1") */
	for (entry = fields[3]; entry && *entry;)
	{
		char *next = strchr(entry, ',');
		char *colon;

		if (next)
			*next++ = 0;
		colon = strchr(entry, ':');
		if (colon)
		{
			int count, kind;

			*colon = 0;
			count = atoi(colon + 1);
			kind = !strcmp(entry, "legacy") ? BROWSER_MACHINE_KINDS - 1 : delta_peer_platform_number(entry);
			if (kind < 0)
				kind = _delta_platform_unknown;
			if (count > 0)
				game->machines[kind] = (unsigned char)(game->machines[kind] + count > 255 ? 255 : game->machines[kind] + count);
		}
		entry = next;
	}
}

/* a badge's server key from the list's 64 hexadecimal digits: 1, or 0 */
static int badge_key_from_hex(const char *text, unsigned char *key)
{
	int index;

	if (strlen(text) != 2 * SERVER_BADGE_KEY_SIZE)
		return 0;
	for (index = 0; index < 2 * SERVER_BADGE_KEY_SIZE; index++)
	{
		char character = text[index];
		int value = character >= '0' && character <= '9' ? character - '0' :
			character >= 'a' && character <= 'f' ? character - 'a' + 10 :
			character >= 'A' && character <= 'F' ? character - 'A' + 10 : -1;

		if (value < 0)
			return 0;
		key[index / 2] = (unsigned char)(index % 2 ? key[index / 2] | value : value << 4);
	}
	return 1;
}

/* one line of /v1/games.txt: invite name map engine players
maximum_players open version age score_limit teams roster, and (as asked:
?fields=22) region, platform, hosting, protocol, machines, gametype,
capabilities, an Xbox map pack's map's name, pack and tag, (?fields=23)
the playlist, (?fields=24) "opence_server" for an OpenCE dedicated
server's game, (?fields=25) 1 when the host's joins are locked,
(?fields=27) a server badge and its label, and (?fields=28) the server
key its certificate names (a list from before them sends 19, 22, 23, 24,
25 or 27) */
static int parse_game(char *line, struct browser_game *game)
{
	char *fields[28];
	int count = 0;
	char *cursor = line;

	while (count < 28)
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
	/* (then region, for the estimated ping, and the host's) */
	if (count >= 13 && browser_ping_region_valid(fields[12]))
		snprintf(game->region, sizeof(game->region), "%s", fields[12]);
	if (count >= 17)
	{
		fields[16][strcspn(fields[16], "\r\n")] = 0;
		parse_host(fields + 13, game);
	}
	/* (then the gametype: a co-op game's names its difficulty) */
	if (count >= 18)
	{
		static char const *const difficulties[] = { "easy", "normal", "heroic", "legend" };
		char const *gametype = fields[17];
		int index;

		fields[17][strcspn(fields[17], "\r\n")] = 0;
		if (!SDL_strncasecmp(gametype, "co-op ", 6))
		{
			for (index = 0; index < 4; index++)
			{
				if (!SDL_strncasecmp(gametype + 6, difficulties[index], strlen(difficulties[index])))
					game->difficulty = (unsigned char)(index + 1);
			}
		}
	}
	/* (then the host's announced capabilities, 8 hexadecimal digits: the ping
	bit says it answers pings) */
	if (count >= 19)
	{
		fields[18][strcspn(fields[18], "\r\n")] = 0;
		if (strlen(fields[18]) <= 8 && fields[18][strspn(fields[18], "0123456789abcdefABCDEF")] == 0)
			game->capabilities = strtoul(fields[18], NULL, 16);
	}
	/* (then an Xbox map pack's map's name and its pack's, printable ASCII:
	the menus draw them as they are) */
	if (count >= 21)
	{
		int field;

		for (field = 19; field <= 20; field++)
		{
			char *text = field == 19 ? game->map_title : game->map_pack;
			size_t size = field == 19 ? sizeof(game->map_title) : sizeof(game->map_pack);
			size_t index;

			fields[field][strcspn(fields[field], "\r\n")] = 0;
			for (index = 0; fields[field][index]; index++)
			{
				if ((unsigned char)fields[field][index] < 32 || (unsigned char)fields[field][index] > 126)
					break;
			}
			if (!fields[field][index])
				snprintf(text, size, "%s", fields[field]);
		}
	}
	/* (then the playlist's id, a-z, 0-9 and '_' alone: anything else is none) */
	if (count >= 23)
	{
		fields[22][strcspn(fields[22], "\r\n")] = 0;
		if (fields[22][0] && strlen(fields[22]) < sizeof(game->playlist) &&
			!fields[22][strspn(fields[22], "abcdefghijklmnopqrstuvwxyz0123456789_")])
		{
			snprintf(game->playlist, sizeof(game->playlist), "%s", fields[22]);
		}
	}
	/* (then an OpenCE dedicated server's label: the list knows it of a
	server that publishes only to OpenCE's masters, which no broker hears) */
	if (count >= 24)
	{
		fields[23][strcspn(fields[23], "\r\n")] = 0;
		game->opence_server = !strcmp(fields[23], "opence_server");
	}
	/* (then whether the host's joins are locked: sv_lock) */
	if (count >= 25)
	{
		fields[24][strcspn(fields[24], "\r\n")] = 0;
		game->joins_locked = !strcmp(fields[24], "1");
	}
	/* (then a dedicated server's badge, by name, and its label, printable
	ASCII: none in a build without badge keys) */
	if (count >= 27 && server_badge_has_keys() && badge_list_trusted())
	{
		size_t index;

		fields[25][strcspn(fields[25], "\r\n")] = 0;
		fields[26][strcspn(fields[26], "\r\n")] = 0;
		game->badge = (unsigned char)server_badge_tier_number(fields[25]);
		for (index = 0; game->badge && fields[26][index]; index++)
		{
			if ((unsigned char)fields[26][index] < 32 || (unsigned char)fields[26][index] > 126)
				break;
		}
		if (game->badge && !fields[26][index])
			snprintf(game->badge_label, sizeof(game->badge_label), "%s", fields[26]);
		else
			game->badge = 0;
		/* (and the key, 64 hexadecimal digits, to match a proof on joining) */
		if (game->badge && count >= 28)
		{
			fields[27][strcspn(fields[27], "\r\n")] = 0;
			game->has_badge_key = badge_key_from_hex(fields[27], game->badge_key);
		}
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

	/* (no room: tried again after the failure's wait, not at once with
	another request) */
	games = malloc(sizeof(*games) * BROWSER_MAXIMUM_GAMES);
	if (!games)
	{
		pthread_mutex_lock(&browser_lock);
		browser.list_time = p2p_now();
		browser.list_failed = 1;
		pthread_mutex_unlock(&browser_lock);
		return;
	}
	/* (with the host's fields: a list from before them sends what it has; and
	co-op games, which the list sends only when asked, for the lists' filter) */
	server_url("/v1/games.txt?fields=28&coop=1", url, sizeof(url));
	status = posix_browser_request(url, NULL, NULL, response, sizeof(response), error, sizeof(error));
	if (!p2p_hosting_invite(own, sizeof(own)))
		own[0] = 0;
	if (status == 200)
	{
		/* (split by hand: strtok's place is the whole process's, and the
		game's thread uses it too) */
		char *next;

		for (line = response; line && count < BROWSER_MAXIMUM_GAMES; line = next)
		{
			next = strchr(line, '\n');
			if (next)
				*next++ = 0;
			if (!line[0])
				continue;
			/* (a game of another number is kept, marked by the screens: a
			Delta host's may still be joined, by the game wire its
			advertisement says, network_game_client_advertised_game_compatible) */
			if (parse_game(line, &games[count]) && strcmp(games[count].invite, own))
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
		browser.list_answered_time = browser.list_time;
	}
	else if (elapsed(browser.list_answered_time, LIST_STALE))
		browser.game_count = 0;
	pthread_mutex_unlock(&browser_lock);
	free(games);
}

/* this machine's round trip to an anchor: a TCP connection's opening
(answered or refused, both a round trip), the lower of two; -1 if neither
came back in time. Nothing is sent */
static long time_anchor(const struct browser_ping_anchor *anchor)
{
	enum { ATTEMPTS = 2, TIMEOUT = 1500 };
	unsigned long address = posix_resolve_ipv4(anchor->host);
	long best = -1;
	int attempt;

	if (!address)
		return -1;
	for (attempt = 0; attempt < ATTEMPTS; attempt++)
	{
		struct sockaddr_in to;
		int socket = posix_socket(AF_INET, SOCK_STREAM, 0);
		unsigned long start;
		int write[1], write_count = 1, read_count = 0, error[1], error_count = 1;

		if (socket < 0)
			return best;
		posix_socket_set_nonblocking(socket, 1);
		memset(&to, 0, sizeof(to));
		to.sin_family = AF_INET;
		to.sin_port = (unsigned short)(anchor->port << 8 | anchor->port >> 8);
		to.sin_addr.s_addr = address;
		start = p2p_now();
		write[0] = socket;
		error[0] = socket;
		if (posix_socket_connect(socket, &to, sizeof(to)) == 0 ||
			(posix_socket_select(NULL, &read_count, write, &write_count, error, &error_count, 0, TIMEOUT * 1000L, 0) > 0 &&
			(write_count || error_count)))
		{
			long round_trip = (long)(unsigned int)(p2p_now() - start);

			if (best < 0 || round_trip < best)
				best = round_trip;
		}
		posix_socket_close(socket);
	}
	return best;
}

/* the anchors, once a run: asked for, then each timed (browser_ping.c), on
a thread of their own, so that the list is not held up meanwhile */
static void *anchors_thread(void *unused)
{
	static char response[4096];
	static struct browser_ping_anchor anchors[BROWSER_PING_ANCHORS];
	char url[512], error[256];
	int count, index;

	(void)unused;
	server_url("/v1/anchors.txt", url, sizeof(url));
	count = 0;
	if (posix_browser_request(url, NULL, NULL, response, sizeof(response), error, sizeof(error)) == 200)
		count = browser_ping_parse_anchors(response, anchors, BROWSER_PING_ANCHORS);
	for (index = 0; index < count; index++)
		anchors[index].ping = time_anchor(&anchors[index]);
	pthread_mutex_lock(&browser_lock);
	memcpy(browser.anchors, anchors, sizeof(*anchors) * (size_t)count);
	browser.anchor_count = count;
	browser.anchors_done = 1;
	pthread_mutex_unlock(&browser_lock);
	platform_log("Game list: %d anchors for estimated pings", count);
	return NULL;
}

static void update_anchors(void)
{
	pthread_t thread;
	int wanted;

	pthread_mutex_lock(&browser_lock);
	wanted = browser.anchors_wanted && !browser.anchors_started;
	browser.anchors_started |= wanted;
	pthread_mutex_unlock(&browser_lock);
	if (!wanted || !config_boolean("network.online"))
		return;
	if (pthread_create(&thread, NULL, anchors_thread, NULL) == 0)
		pthread_detach(thread);
}

#ifndef HALO_CONSOLE_NO_GUIDE
/* ---------- presence (the browser thread)

About once a minute while this copy plays a Delta game that has an invite,
it tells the site which game (POST /v1/presence {key, invite}), when
network.share_presence is on, this copy counts for a Delta profile and the
profile's "Show when I'm playing" is not off. The invite is the game's own:
the one this copy hosts with (p2p_hosting_invite), or the one it joined
(p2p_joined_invite). The site finds the listed game by it for the
profile's "playing" line, shows that game only to those the player allows,
and the invite itself only to those allowed to join. Nothing in the menus,
only this copy's own player key, nothing about other players. The site
keeps it in memory only, with no address. */

/* the game says it is playing this often (discord_game.c, once a second):
quiet longer, and it no longer is */
#define PRESENCE_SAID_TIMEOUT 5000

static void update_presence(void)
{
	struct social_presence_inputs inputs;
	char invite[BROWSER_INVITE_LENGTH + 1], fields[128], response[256], error[256];
	int status, no_key, answer, logged;

	/* (whether this copy is linked, without waiting for a screen to ask:
	the link kept in the save root, once a run, then the site's word on the
	player's own profile, asked once a run while playing) */
	read_profile_file();
	memset(&inputs, 0, sizeof(inputs));
	inputs.share = config_boolean("network.share_presence") != 0;
	/* (the game's invite: hosted, else joined; a password game hosted here
	has none to give) */
	invite[0] = 0;
	if (!p2p_hosting_invite(invite, sizeof(invite)) && !p2p_joined_invite(invite, sizeof(invite)))
		invite[0] = 0;
	inputs.has_invite = social_presence_invite_valid(invite);
	pthread_mutex_lock(&browser_lock);
	inputs.in_game = browser.presence_playing && !elapsed(browser.presence_said, PRESENCE_SAID_TIMEOUT);
	/* (playing says the game is a Delta one: discord_game.c) */
	inputs.on_delta = inputs.in_game;
	inputs.linked = browser.profile_linked || (browser.game_profile_has && browser.game_profile.linked);
	if (inputs.in_game && inputs.share && !inputs.linked)
		browser.profile_check_wanted = 1;
	inputs.hidden = browser.game_profile_has && browser.game_profile.has_privacy &&
		!browser.game_profile.show_presence;
	if (!social_presence_due(&browser.presence, &inputs, p2p_now()))
	{
		pthread_mutex_unlock(&browser_lock);
		return;
	}
	pthread_mutex_unlock(&browser_lock);
	response[0] = 0;
	if (social_presence_fields(fields, sizeof(fields), invite) < 0)
		fields[0] = 0;
	status = game_request("/v1/presence", fields, response, sizeof(response), error, sizeof(error), &no_key);
	pthread_mutex_lock(&browser_lock);
	answer = social_presence_answer(&browser.presence, no_key ? 0 : status, response, p2p_now());
	logged = browser.presence_logged;
	browser.presence_logged = answer;
	pthread_mutex_unlock(&browser_lock);
	/* (debug.txt says what the site said when it changes, never the key) */
	if (answer != logged)
	{
		platform_log("Delta presence: %s", answer == SOCIAL_PRESENCE_OK ? "the site shows you playing" :
			answer == SOCIAL_PRESENCE_OFF ? "the site keeps it hidden; asked again in half an hour" :
			answer == SOCIAL_PRESENCE_BUSY ? "the site is busy; asked again in ten minutes" :
			answer == SOCIAL_PRESENCE_REFUSED ? "the site does not take it; asked again in half an hour" :
			no_key ? "no player key or no HTTPS game list" : "the site is not answering");
	}
}
#endif

/* ---------- the Guide's friends (the browser thread)

While the game says where the player is (the Guide's, about once a second),
this copy is linked to a Delta profile and network.browser_url is set, the
site's friends state is asked for on social_poll_due's schedule (POST
/v1/social/state {key, since}: the cursor of the last state, so that an
unchanged one is just the cursor back). Too many (429), no answer and an
unlinked copy wait longer (social_poll_answer). An action asked for
(browser_social_act) goes first: a friends change (/v1/game/friends, whose
answer is the new state), a join (/v1/social/join: the halo://join link the
game then joins as any invite link) or an invite (/v1/social/invite, with
this copy's game's invite). Only this copy's key is sent; debug.txt says
how many, never who. */

#define SOCIAL_SAID_TIMEOUT 5000
#define SOCIAL_RESPONSE (256 * 1024)
#define SOCIAL_NOT_ANSWERING "The site isn't answering."

/* the /v1/game/friends actions, by BROWSER_SOCIAL_* ("" for the others) */
static const char *const social_action_names[NUMBER_OF_BROWSER_SOCIAL_ACTIONS] =
{
	"accept", "decline", "remove", "block", "request", "", "", "cancel", "unblock",
};

static int social_friends_action(int action)
{
	return action >= 0 && action < NUMBER_OF_BROWSER_SOCIAL_ACTIONS && social_action_names[action][0];
}

/* (under the lock) the state changed: the Guide copies it */
static void social_status_set(int status)
{
	if (browser.social_status != status)
	{
		browser.social_status = status;
		browser.social_serial++;
	}
}

/* (under the lock) the result of the action out */
static void social_done(int state, const char *message)
{
	browser.social_result.state = state;
	snprintf(browser.social_result.message, sizeof(browser.social_result.message), "%s", message ? message : "");
}

static void social_log(int status, const struct social_state *state)
{
	if (status == BROWSER_SOCIAL_LOADED)
	{
		platform_log("DELTA: friends loaded (%d of %d online, %d requests, %d invites)", social_online_count(state),
			state->friend_count, state->request_count, state->invite_count);
	}
	else if (status == BROWSER_SOCIAL_UNLINKED)
		platform_log("DELTA: the site says this copy is linked to no profile");
	else if (status == BROWSER_SOCIAL_FAILED)
		platform_log("DELTA: friends not loaded (the site is busy or not answering; asked again later)");
}

/* (under the lock) the party as the site says it now */
static void party_keep(const struct social_party *party)
{
	if (memcmp(&browser.party, party, sizeof(*party)))
	{
		browser.party = *party;
		browser.party_serial++;
	}
	browser.social_poll.party = party->has;
	browser.party_poll.party = party->has;
}

/* a state read (the poll's, or a friends action's): kept */
static void social_keep(const struct social_state *read)
{
	*browser.social_state = *read;
	browser.social_serial++;
	browser.social_status = BROWSER_SOCIAL_LOADED;
	party_keep(&read->party);
}

static void social_action(int action, const char *handle, char *response, struct social_state *read)
{
	char fields[192], error[256], text[96], invite[BROWSER_INVITE_LENGTH + 1];
	const char *path;
	int status, no_key, ok, parsed = 0, logged_status;

	fields[0] = 0;
	if (social_friends_action(action))
	{
		path = "/v1/game/friends";
		ok = social_action_fields(fields, sizeof(fields), social_action_names[action], handle) >= 0;
	}
	else if (action == BROWSER_SOCIAL_JOIN)
	{
		path = "/v1/social/join";
		ok = social_join_fields(fields, sizeof(fields), handle) >= 0;
	}
	else
	{
		/* (this copy's game: the one it hosts, else the one it joined) */
		path = "/v1/social/invite";
		if (!p2p_hosting_invite(invite, sizeof(invite)) && !p2p_joined_invite(invite, sizeof(invite)))
			invite[0] = 0;
		ok = social_invite_fields(fields, sizeof(fields), handle,
			social_presence_invite_valid(invite) ? invite : "") >= 0;
	}
	if (!ok)
	{
		pthread_mutex_lock(&browser_lock);
		social_done(BROWSER_SOCIAL_REFUSED, "Can't do that.");
		pthread_mutex_unlock(&browser_lock);
		return;
	}
	response[0] = 0;
	status = game_request(path, fields, response, SOCIAL_RESPONSE, error, sizeof(error), &no_key);
	if (status == 200 && social_friends_action(action))
		parsed = delta_profile_parse_social(response, read);
	pthread_mutex_lock(&browser_lock);
	logged_status = browser.social_status;
	social_poll_soon(&browser.social_poll);
	if (no_key || !status)
		social_done(BROWSER_SOCIAL_REFUSED, SOCIAL_NOT_ANSWERING);
	else if (action == BROWSER_SOCIAL_JOIN)
	{
		if (status == 200 && delta_profile_parse_join(response, browser.social_join, sizeof(browser.social_join)))
			social_done(BROWSER_SOCIAL_DONE, "");
		else if (status == 429)
			social_done(BROWSER_SOCIAL_REFUSED, "Too many tries. Wait a minute.");
		else
		{
			delta_profile_parse_text(response, "reason", text, sizeof(text));
			social_done(BROWSER_SOCIAL_REFUSED, social_join_refusal(text));
		}
	}
	else if (action == BROWSER_SOCIAL_INVITE)
	{
		if (status == 200)
			social_done(BROWSER_SOCIAL_DONE, "Invite sent");
		else
		{
			delta_profile_parse_text(response, "reason", text, sizeof(text));
			social_done(BROWSER_SOCIAL_REFUSED, social_invite_refusal(text, status));
		}
	}
	else if (parsed == 1)
	{
		social_keep(read);
		social_done(BROWSER_SOCIAL_DONE, "");
	}
	else
	{
		/* (the site's own words: "That request is gone.") */
		delta_profile_parse_text(response, "error", text, sizeof(text));
		social_done(BROWSER_SOCIAL_REFUSED, status == 429 ? "Too many tries. Wait a minute." :
			text[0] && status >= 400 && status < 500 ? text : "Can't do that right now.");
	}
	pthread_mutex_unlock(&browser_lock);
	platform_log("DELTA: %s %s", action == BROWSER_SOCIAL_JOIN ? "join" : action == BROWSER_SOCIAL_INVITE ? "invite" :
		social_action_names[action], status == 200 ? "done" : status ? "refused" : "no answer");
	if (parsed == 1 && logged_status != BROWSER_SOCIAL_LOADED)
		social_log(BROWSER_SOCIAL_LOADED, read);
}

/* ---------- the doorbell (docs/delta.md, "The doorbell")

While this copy is linked, the site gave it a doorbell and network.doorbell
is on, internet play's signalling listens for it on our own brokers, over
the connections it keeps to them anyway (p2p_doorbell). A ring makes the
next friends poll go out at once (social_poll_ring, within the site's
limit). */

/* the doorbell signalling should listen for now: given to it when it
changed (outside the lock: p2p takes its own) */
static void doorbell_update(int linked)
{
	char wanted[SOCIAL_DOORBELL_SIZE];

	pthread_mutex_lock(&browser_lock);
	snprintf(wanted, sizeof(wanted), "%s", linked && config_boolean("network.doorbell") ? browser.doorbell_id : "");
	if (!strcmp(wanted, browser.doorbell_given))
	{
		pthread_mutex_unlock(&browser_lock);
		return;
	}
	pthread_mutex_unlock(&browser_lock);
	if (!p2p_doorbell(wanted))
		return;
	pthread_mutex_lock(&browser_lock);
	memcpy(browser.doorbell_given, wanted, sizeof(wanted));
	pthread_mutex_unlock(&browser_lock);
}

static void update_social(void)
{
	static char *response;
	static struct social_state *read;
	char fields[128], error[256], cursor[SOCIAL_CURSOR_SIZE], handle[SOCIAL_HANDLE_SIZE];
	int linked, due, action, status, no_key, parsed, before, after;
	int rings = p2p_doorbell_rings();

	pthread_mutex_lock(&browser_lock);
	if (!browser.social_state || elapsed(browser.social_said, SOCIAL_SAID_TIMEOUT))
	{
		pthread_mutex_unlock(&browser_lock);
		return;
	}
	linked = browser.profile_linked || (browser.game_profile_has && browser.game_profile.linked);
	if (rings)
		social_poll_ring(&browser.social_poll);
	if (!linked)
	{
		browser.doorbell_id[0] = 0;
		/* (the site asked once a run whether this copy is linked) */
		browser.profile_check_wanted = 1;
		if (browser.social_status != BROWSER_SOCIAL_NONE)
		{
			memset(browser.social_state, 0, sizeof(*browser.social_state));
			social_status_set(BROWSER_SOCIAL_NONE);
		}
	}
	action = linked && browser.social_action_wanted ? browser.social_result.action : -1;
	snprintf(handle, sizeof(handle), "%s", browser.social_result.handle);
	if (browser.social_action_wanted && !linked)
		social_done(BROWSER_SOCIAL_REFUSED, "Sign in first.");
	browser.social_action_wanted = 0;
	due = action < 0 &&
		social_poll_due(&browser.social_poll, p2p_now(), browser.social_in_game && !browser.social_guide_open, linked);
	if (due && browser.social_status == BROWSER_SOCIAL_NONE)
		browser.social_status = BROWSER_SOCIAL_LOADING;
	snprintf(cursor, sizeof(cursor), "%s", browser.social_status == BROWSER_SOCIAL_LOADED && !browser.social_whole ?
		browser.social_state->cursor : "");
	if (due)
		browser.social_whole = 0;
	pthread_mutex_unlock(&browser_lock);
	doorbell_update(linked);
	if (action < 0 && !due)
		return;
	if (!response)
		response = malloc(SOCIAL_RESPONSE);
	if (!read)
		read = malloc(sizeof(*read));
	if (!response || !read)
		return;
	if (action >= 0)
	{
		social_action(action, handle, response, read);
		return;
	}

	if (social_state_fields(fields, sizeof(fields), cursor, SOCIAL_MAXIMUM_FRIENDS) < 0)
		social_state_fields(fields, sizeof(fields), "", SOCIAL_MAXIMUM_FRIENDS);
	response[0] = 0;
	status = game_request("/v1/social/state", fields, response, SOCIAL_RESPONSE, error, sizeof(error), &no_key);
	parsed = status == 200 ? delta_profile_parse_social(response, read) : 0;
	pthread_mutex_lock(&browser_lock);
	before = browser.social_status;
	social_poll_answer(&browser.social_poll, no_key ? 0 : status, parsed != 0);
	if (parsed)
		memcpy(browser.doorbell_id, read->doorbell, sizeof(browser.doorbell_id));
	else if (status == 404 && !no_key)
		browser.doorbell_id[0] = 0;
	if (parsed == 1)
	{
		social_keep(read);
		/* (the clan, asked with the friends: DELTA's CLAN roster; a site
		from before it sends none, and the profile's own stays) */
		if (browser.game_profile_has && delta_profile_parse_clan(response, &browser.game_profile))
			browser.game_profile_serial++;
	}
	else if (parsed == 2 && before == BROWSER_SOCIAL_LOADED)
	{
		/* (unchanged) */
	}
	else if (status == 404 && !no_key)
	{
		memset(browser.social_state, 0, sizeof(*browser.social_state));
		social_status_set(BROWSER_SOCIAL_UNLINKED);
	}
	else if (before == BROWSER_SOCIAL_LOADING || before == BROWSER_SOCIAL_LOADED)
	{
		/* (the state kept stays shown; the next poll asks for all of it) */
		social_status_set(BROWSER_SOCIAL_FAILED);
	}
	after = browser.social_status;
	pthread_mutex_unlock(&browser_lock);
	if (after != before || (parsed == 1 && before != BROWSER_SOCIAL_LOADED))
		social_log(after, read);
}

/* ---------- the Guide's party (the browser thread)

An action asked for (browser_party_act), then the leader's game to tell
(browser_party_go), go first as POST /v1/party; its answer is the party as
it is now. A copy linked to a profile has its party from the friends' state
as well (update_social); one with none asks /v1/party on the friends'
schedule while it is in a party. Only this copy's key, its game name (for a
copy with no profile) and its platform are sent; never an address. */

static void update_party(void)
{
	static char *response;
	static struct social_state *read;
	static int command_line_checked;
	char fields[320], error[256], action[16], value[96], name[48], text[96];
	int linked, wanted, going = 0, polling = 0, status, no_key, parsed;

	if (!command_line_checked)
	{
		char argument[256];
		int index;

		command_line_checked = 1;
		for (index = 1; posix_command_line_argument(index, argument, sizeof(argument)); index++)
			browser_party_link(argument);
	}
	pthread_mutex_lock(&browser_lock);
	if (!browser.social_state || elapsed(browser.social_said, SOCIAL_SAID_TIMEOUT))
	{
		pthread_mutex_unlock(&browser_lock);
		return;
	}
	linked = browser.profile_linked || (browser.game_profile_has && browser.game_profile.linked);
	wanted = browser.party_action_wanted;
	browser.party_action_wanted = 0;
	snprintf(action, sizeof(action), "%s", browser.party_action);
	snprintf(value, sizeof(value), "%s", browser.party_value);
	snprintf(name, sizeof(name), "%s", linked ? "" : browser.party_name);
	if (!wanted && browser.party_go[0])
	{
		going = 1;
		snprintf(action, sizeof(action), "go");
		snprintf(value, sizeof(value), "%s", browser.party_go);
		browser.party_go[0] = 0;
	}
	else if (!wanted && !linked && browser.party.has)
		polling = social_poll_due(&browser.party_poll, p2p_now(), browser.social_in_game && !browser.social_guide_open, 1);
	pthread_mutex_unlock(&browser_lock);
	if (!wanted && !going && !polling)
		return;
	if (!response)
		response = malloc(SOCIAL_RESPONSE);
	if (!read)
		read = malloc(sizeof(*read));
	if (!response || !read)
		return;
	if (polling)
		action[0] = value[0] = 0;
	if (social_party_fields(fields, sizeof(fields), action, value, name, local_platform_name()) < 0)
	{
		/* (nothing sent: said, so a refusal like this is never silent) */
		platform_log("DELTA: party %s not sent: the game can't send that", wanted ? action : "read");
		pthread_mutex_lock(&browser_lock);
		if (wanted)
		{
			browser.party_result.state = BROWSER_SOCIAL_REFUSED;
			snprintf(browser.party_result.message, sizeof(browser.party_result.message), "%s",
				!strcmp(action, "join") ? "That isn't a party code." : "Can't do that.");
		}
		pthread_mutex_unlock(&browser_lock);
		return;
	}
	response[0] = 0;
	status = game_request("/v1/party", fields, response, SOCIAL_RESPONSE, error, sizeof(error), &no_key);
	parsed = status == 200 && delta_profile_parse_party(response, read);
	text[0] = 0;
	if (status != 200)
		delta_profile_parse_text(response, "reason", text, sizeof(text));
	pthread_mutex_lock(&browser_lock);
	if (parsed)
		party_keep(&read->party);
	if (polling)
		social_poll_answer(&browser.party_poll, no_key ? 0 : status, parsed);
	if (linked && (wanted || going))
		social_poll_soon(&browser.social_poll);
	if (wanted)
	{
		browser.party_result.state = parsed ? BROWSER_SOCIAL_DONE : BROWSER_SOCIAL_REFUSED;
		snprintf(browser.party_result.message, sizeof(browser.party_result.message), "%s",
			parsed ? "" : no_key || !status ? SOCIAL_NOT_ANSWERING : social_party_action_refusal(action, text, status));
	}
	pthread_mutex_unlock(&browser_lock);
	if (wanted || going)
	{
		/* (the site's status and reason, never a handle or a key) */
		if (parsed)
			platform_log("DELTA: party %s done", action);
		else if (status)
			platform_log("DELTA: party %s refused (%d %s)", action, status, text[0] ? text : "no reason");
		else
			platform_log("DELTA: party %s no answer%s", action, no_key ? " (no player key)" : "");
	}
}

/* ---------- a report's clip (docs/delta.md, Reports)

A report a linked server took with REPORT_RESULT's "send the clip": the
clip's file goes to the site with the token the server made (the report it
forwards names it), /v1/game/clip/start (the copy's key or device token, as
the other /v1/game requests; 409 until the report reaches the site, asked
again every 5 seconds for 2 minutes), then the file itself to
/v1/game/clip/upload. One at a time; the browser thread does it. */

enum
{
	/* the site's cap (a bigger file is never read) */
	CLIP_MOST_BYTES = 48 * 1024 * 1024,
};

enum
{
	/* the site answers 409 until the report naming the token reaches it
	(the server's next poll): asked again this often, for this long */
	CLIP_RETRY_SECONDS = 5,
	CLIP_WAIT_SECONDS = 120,
};

static struct
{
	int waiting;
	int state;
	char path[1024];
	unsigned char token[16];
	/* when it was asked for, and when the site may be asked again */
	time_t asked;
	time_t next_try;
} clip_job;

static void clip_done(int state)
{
	pthread_mutex_lock(&browser_lock);
	clip_job.state = state;
	clip_job.waiting = 0;
	pthread_mutex_unlock(&browser_lock);
}

static void update_clip_upload(void)
{
	char path[1024], members[160], response[512], error[256], url[600], token[33], upload[80];
	unsigned char token_bytes[16];
	char *body, *found;
	long length;
	FILE *file;
	int no_key = 0, status;

	pthread_mutex_lock(&browser_lock);
	if (!clip_job.waiting || clip_job.state != BROWSER_CLIP_SENDING || time(NULL) < clip_job.next_try)
	{
		pthread_mutex_unlock(&browser_lock);
		return;
	}
	snprintf(path, sizeof(path), "%s", clip_job.path);
	memcpy(token_bytes, clip_job.token, sizeof(token_bytes));
	pthread_mutex_unlock(&browser_lock);
	p2p_hex(token_bytes, (int)sizeof(token_bytes), token);
	/* (its size first: the file is read only once the site takes it) */
	file = fopen(path, "rb");
	if (!file || fseek(file, 0, SEEK_END) || (length = ftell(file)) <= 0 || length > CLIP_MOST_BYTES)
	{
		if (file)
			fclose(file);
		clip_done(file ? BROWSER_CLIP_TOO_BIG : BROWSER_CLIP_FAILED);
		return;
	}
	fclose(file);
	snprintf(members, sizeof(members), "\"token\": \"%s\", \"size\": %ld", token, length);
	status = site_request("/v1/game/clip/start", members, response, sizeof(response), error, sizeof(error), &no_key);
	found = status == 200 ? strstr(response, "\"upload\"") : NULL;
	if (found)
		found = strchr(found + 8, '"');
	/* (409: no report names the token yet; it comes with the server's next
	poll. Asked again in a while, up to CLIP_WAIT_SECONDS) */
	if (status == 409)
	{
		time_t now = time(NULL);

		pthread_mutex_lock(&browser_lock);
		if (now - clip_job.asked < CLIP_WAIT_SECONDS)
		{
			clip_job.next_try = now + CLIP_RETRY_SECONDS;
			pthread_mutex_unlock(&browser_lock);
			return;
		}
		pthread_mutex_unlock(&browser_lock);
		platform_log("clips: the site never had the report (the server may not have sent it)");
		clip_done(BROWSER_CLIP_FAILED);
		return;
	}
	if (!found || sscanf(found + 1, "%70[A-Za-z0-9_-]", upload) != 1)
	{
		platform_log("clips: the site would not take the clip (HTTP %d)", status);
		clip_done(status == 413 ? BROWSER_CLIP_TOO_BIG : status == 429 ? BROWSER_CLIP_TOO_MANY : BROWSER_CLIP_FAILED);
		return;
	}
	file = fopen(path, "rb");
	body = file ? malloc((size_t)length) : NULL;
	if (!body || fread(body, 1, (size_t)length, file) != (size_t)length)
	{
		free(body);
		if (file)
			fclose(file);
		clip_done(BROWSER_CLIP_FAILED);
		return;
	}
	fclose(file);
	server_url("/v1/game/clip/upload?upload=", url, sizeof(url));
	snprintf(url + strlen(url), sizeof(url) - strlen(url), "%s", upload);
	status = posix_browser_send(url, body, (size_t)length, "video/mp4", NULL, response, sizeof(response), error,
		sizeof(error));
	free(body);
	platform_log("clips: a report's clip sent (%ld bytes): HTTP %d", length, status);
	clip_done(status == 200 ? BROWSER_CLIP_SENT : BROWSER_CLIP_FAILED);
}

int browser_clip_upload(const char *path, const unsigned char *token)
{
	int ok = 0;

	pthread_mutex_lock(&browser_lock);
	if (!clip_job.waiting && path && path[0] && strlen(path) < sizeof(clip_job.path))
	{
		snprintf(clip_job.path, sizeof(clip_job.path), "%s", path);
		memcpy(clip_job.token, token, sizeof(clip_job.token));
		clip_job.waiting = 1;
		clip_job.state = BROWSER_CLIP_SENDING;
		/* (the report rides the server's next poll: the first ask waits a
		moment for it) */
		clip_job.asked = time(NULL);
		clip_job.next_try = clip_job.asked + CLIP_RETRY_SECONDS;
		ok = 1;
	}
	pthread_mutex_unlock(&browser_lock);
	return ok;
}

int browser_clips_list(char paths[][512], char names[][48], int most)
{
	char folder[1024];
	char **found;
	long long times[16];
	int count = 0, total = 0, index;
	const char *root = platform_data_root();

	if (most > 16)
		most = 16;
	snprintf(folder, sizeof(folder), "%s%sclips", root ? root : "", root && root[0] ? "/" : "");
	found = SDL_GlobDirectory(folder, "*.mp4", 0, &total);
	/* (the newest first) */
	for (index = 0; found && index < total; index++)
	{
		SDL_PathInfo info;
		char path[512];
		int at;

		snprintf(path, sizeof(path), "%s/%s", folder, found[index]);
		if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE || info.size <= 0 ||
			info.size > CLIP_MOST_BYTES)
		{
			continue;
		}
		for (at = count; at > 0 && times[at - 1] < (long long)info.modify_time; at--)
			;
		if (at >= most)
			continue;
		if (count < most)
			count++;
		memmove(&times[at + 1], &times[at], (size_t)(count - 1 - at) * sizeof(times[0]));
		memmove(paths[at + 1], paths[at], (size_t)(count - 1 - at) * sizeof(paths[0]));
		memmove(names[at + 1], names[at], (size_t)(count - 1 - at) * sizeof(names[0]));
		times[at] = (long long)info.modify_time;
		snprintf(paths[at], 512, "%s", path);
		snprintf(names[at], 48, "%s", found[index]);
	}
	SDL_free(found);
	return count;
}

int browser_clip_state(void)
{
	int state;

	pthread_mutex_lock(&browser_lock);
	state = clip_job.state;
	pthread_mutex_unlock(&browser_lock);
	return state;
}

static void *browser_thread(void *unused)
{
	(void)unused;
	for (;;)
	{
		if (config_string("network.browser_url")[0])
		{
#ifdef HALO_DELTA_IDENTITY
			update_identity();
#endif
#ifdef HALO_DELTA_PEER_IDENTITY
			update_revoked();
#endif
			update_hosting();
			send_report();
			send_client_report();
			send_claims();
			open_profile();
			update_connect();
			check_profile();
			update_game_profile();
#ifdef HALO_DELTA_IDENTITY
			update_approve();
#endif
#ifndef HALO_CONSOLE_NO_GUIDE
			update_presence();
			update_social();
			update_party();
#endif
			update_cards();
			update_list();
			update_anchors();
			update_clip_upload();
		}
		Sleep(THREAD_INTERVAL);
	}
	return NULL;
}

/* the exit's withdrawal (withdraw_at_exit), on a thread of its own: the
invite, and whether the request is over (under the lock) */
static char exit_withdraw_invite[BROWSER_INVITE_LENGTH + 1];
static int exit_withdraw_done;

static void *exit_withdraw_thread(void *unused)
{
	(void)unused;
	withdraw_invite(exit_withdraw_invite);
	pthread_mutex_lock(&browser_lock);
	exit_withdraw_done = 1;
	pthread_mutex_unlock(&browser_lock);
	return NULL;
}

/* a copy of the game that quits while its game is listed takes it off the
list (without this the server drops it only once it stops hearing of it),
waiting no longer than EXIT_WITHDRAW_WAIT: a server that does not answer
does not hold up quitting */
static void withdraw_at_exit(void)
{
	pthread_t thread;
	int waited, done = 0;

	/* (the browser thread may be mid-request: the listing is withdrawn by
	whichever of the two gets there) */
	pthread_mutex_lock(&browser_lock);
	memcpy(exit_withdraw_invite, browser.listed_invite, sizeof(exit_withdraw_invite));
	pthread_mutex_unlock(&browser_lock);
	if (!exit_withdraw_invite[0])
		return;
	if (pthread_create(&thread, NULL, exit_withdraw_thread, NULL) != 0)
		return;
	pthread_detach(thread);
	for (waited = 0; waited < EXIT_WITHDRAW_WAIT && !done; waited += EXIT_WITHDRAW_POLL)
	{
		Sleep(EXIT_WITHDRAW_POLL);
		pthread_mutex_lock(&browser_lock);
		done = exit_withdraw_done;
		pthread_mutex_unlock(&browser_lock);
	}
	if (done)
		platform_log("Game list: the game is no longer listed");
	else
		platform_log("Game list: no answer from the list in time; it drops the game by itself");
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
	/* (on the game's thread, as Delta Peer's hooks) */
	game.has_delta = delta_peer_game_host_summary(&game.delta);

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

/* appends a JSON string of a name (game_report.c's) */
static int json_name(char *out, int size, const unsigned short *name, int length)
{
	return game_report_json_name(out, size, name, length);
}

int browser_json_name(char *out, int size, const unsigned short *name, int length)
{
	return json_name(out, size, name, length);
}

/* a line's tag: its player's address's hash with the invite (the host's
own report) */
static int report_line_tag(const struct browser_report_player *player, void *context, char *tag)
{
	unsigned long address = public_address(player->address);

	if (!address)
		return 0;
	address_tag((const char *)context, address, tag);
	return 1;
}

/* a report as JSON (game_report.c): the teams, the scores, the length and
the lines (each tagged with its player's address's hash when tag_invite is
given: the host's own report), then extra's members */
static char *report_json(int teams, int red_score, int blue_score, int duration_seconds,
	const struct browser_report_player *players, int count, const char *extra, const char *tag_invite)
{
	return game_report_json(NULL, teams, red_score, blue_score, duration_seconds, players, count, extra,
		tag_invite ? report_line_tag : NULL, (void *)tag_invite);
}

void browser_report_game(int teams, int red_score, int blue_score, int duration_seconds,
	const struct browser_report_player *players, int count, const char *extra)
{
	char invite[BROWSER_INVITE_LENGTH + 1];
	char *report;

	/* (a host can stay listed and keep its games' stats to itself) */
	if (!config_boolean("network.report_hosted_games"))
		return;
	report = report_json(teams, red_score, blue_score, duration_seconds, players, count, extra,
		p2p_hosting_invite(invite, sizeof(invite)) ? invite : NULL);
	if (!report)
		return;
	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	free(browser.report);
	browser.report = report;
	pthread_mutex_unlock(&browser_lock);
}

int browser_client_report(int teams, int red_score, int blue_score, int duration_seconds,
	const struct browser_report_player *players, int count, const char *extra)
{
	char invite[BROWSER_INVITE_LENGTH + 1];
	char *body, *report;
	size_t size;

	if (!config_string("network.browser_url")[0] || !config_boolean("network.report_joined_games") ||
		!p2p_joined_invite(invite, sizeof(invite)))
	{
		return 0;
	}
	report = report_json(teams, red_score, blue_score, duration_seconds, players, count, extra, NULL);
	if (!report)
		return 0;
	/* (the invite first: which game it is) */
	size = strlen(report) + BROWSER_INVITE_LENGTH + 32;
	body = malloc(size);
	if (body)
		snprintf(body, size, "{\"invite\": \"%s\", %s", invite, report + 1);
	free(report);
	if (!body)
		return 0;
	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	free(browser.client_report);
	browser.client_report = body;
	browser.client_report_attempts = 0;
	browser.client_report_time = p2p_now();
	pthread_mutex_unlock(&browser_lock);
	return 1;
}

/* the local players of a game that ended: their lines confirmed with the
player key, once the host has reported the game (if it is listed) */
void browser_claim_game(const unsigned short (*names)[BROWSER_PLAYER_NAME_LENGTH], int count)
{
	char invite[BROWSER_INVITE_LENGTH + 1];

	/* (a game it joined only with network.report_joined_games: a claim
	tells the site this copy played in it, as a report of it would) */
	if (count <= 0 || !config_string("network.browser_url")[0] ||
		((!config_boolean("network.report_hosted_games") || !p2p_hosting_invite(invite, sizeof(invite))) &&
			(!config_boolean("network.report_joined_games") || !p2p_joined_invite(invite, sizeof(invite)))))
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
		snprintf(browser.connect_message, sizeof(browser.connect_message), "No game list.");
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

/* a waiting key's hexadecimal digits as its bytes */
static void key_from_hex(const char *digits, unsigned char *key)
{
	int index;

	for (index = 0; index < PLAYER_KEY_SIZE; index++)
	{
		unsigned int byte;

		sscanf(digits + 2 * index, "%2x", &byte);
		key[index] = (unsigned char)byte;
	}
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
	key_from_hex(digits, key);
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
	int ok = 0;

	pthread_mutex_lock(&browser_lock);
	if (install && browser.pending_key[0])
	{
		key_from_hex(browser.pending_key, key);
		player_key_path(path, sizeof(path));
		ok = posix_browser_replace_key(path, key, PLAYER_KEY_SIZE);
		if (ok)
		{
			memcpy(player_key_cached, key, PLAYER_KEY_SIZE);
			player_key_loaded = 1;
			/* (another player: its link read and asked about again) */
			browser.profile_linked = 0;
			browser.profile_handle[0] = 0;
			browser.profile_read = 0;
			browser.profile_check_done = 0;
			browser.profile_check_attempts = 0;
			browser.profile_check_time = 0;
#ifdef HALO_DELTA_IDENTITY
			/* (another copy: a new device, on the browser thread) */
			identity_key_changed = 1;
#endif
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

/* the moderator key's seed: SHA-256 of a label and the player key, so the
key is no other use's (the site makes the same from the key it is sent) */
static int moderator_seed(unsigned char *seed)
{
	static const char label[] = "halo-ce-universal moderator key\n";
	unsigned char key[PLAYER_KEY_SIZE];
	unsigned char data[sizeof(label) - 1 + PLAYER_KEY_SIZE];

	if (browser_headless() || !player_key(key))
		return 0;
	memcpy(data, label, sizeof(label) - 1);
	memcpy(data + sizeof(label) - 1, key, PLAYER_KEY_SIZE);
	p2p_sha256(data, (int)sizeof(data), seed);
	memset(key, 0, sizeof(key));
	memset(data, 0, sizeof(data));
	return 1;
}

int browser_moderator_sign(const unsigned char *message, int size, unsigned char *public_key,
	unsigned char *signature)
{
	unsigned char seed[P2P_SEED_SIZE];

	if (!moderator_seed(seed))
		return 0;
	p2p_ed25519_public(seed, public_key, NULL);
	p2p_ed25519_sign(seed, public_key, message, size, signature);
	memset(seed, 0, sizeof(seed));
	return 1;
}

int browser_moderator_key(char *text, int size)
{
	unsigned char seed[P2P_SEED_SIZE];
	unsigned char public_key[P2P_KEY_SIZE];

	if (size <= 2 * P2P_KEY_SIZE || !moderator_seed(seed))
		return 0;
	p2p_ed25519_public(seed, public_key, NULL);
	memset(seed, 0, sizeof(seed));
	p2p_hex(public_key, P2P_KEY_SIZE, text);
	return 1;
}

/* a listed game's identifier (its invite's first bytes, made a locally
administered address as p2p.c makes one); 0 if the invite is not one */
static int invite_identifier(const char *invite, unsigned char *identifier)
{
	int index;

	for (index = 0; index < P2P_IDENTIFIER_SIZE; index++)
	{
		unsigned int byte;

		if (sscanf(invite + 2 * index, "%2x", &byte) != 1)
			return 0;
		identifier[index] = (unsigned char)byte;
	}
	identifier[0] = (unsigned char)((identifier[0] & 0xFC) | 0x02);
	return 1;
}

/* the public listings heard on the brokers (p2p_lobby.c) while they are
followed, read again each LISTINGS_INTERVAL (the game's thread's alone) */
enum { LISTINGS = 256, LISTINGS_INTERVAL = 1000 };
static struct p2p_listing listings[LISTINGS];
/* (each one's key hash in hexadecimal, as an invite starts: made once a
reading, not for every game every frame) */
static char listing_key_hashes[LISTINGS][2 * P2P_KEY_HASH_SIZE + 1];
static int listing_count;

static void read_listings(void)
{
	static unsigned long listings_time;
	static int listings_read;

	if (!listings_read || elapsed(listings_time, LISTINGS_INTERVAL))
	{
		int index;

		listing_count = p2p_lobby_browsing() ? p2p_lobby_games(listings, LISTINGS) : 0;
		for (index = 0; index < listing_count; index++)
			p2p_hex(listings[index].key_hash, P2P_KEY_HASH_SIZE, listing_key_hashes[index]);
		listings_time = p2p_now();
		listings_read = 1;
	}
}

/* strcmp without case (ASCII): the server and Android builds have no
SDL_strcasecmp */
static int text_compare_no_case(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
	{
		int x = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
		int y = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;

		if (x != y)
			return x - y;
	}
	return (unsigned char)*a - (unsigned char)*b;
}


/* whether a listing is a game the list has: the same invite token (the
invite's last 32 hexadecimal digits), or, for a host that lists a game once
for each network version (p2p_listing_older_copy), the same name, map,
game type and players */
static int listing_on_list(const struct p2p_listing *listing, const char *code, const struct browser_game *games,
	int count)
{
	int index, character;

	for (index = 0; index < count; index++)
	{
		const struct browser_game *game = &games[index];

		if (!text_compare_no_case(code + BROWSER_INVITE_LENGTH - 32, game->invite + BROWSER_INVITE_LENGTH - 32))
			return 1;
		if (game->engine != listing->engine_type || game->players != listing->player_count ||
			game->maximum_players != listing->maximum_player_count || text_compare_no_case(game->map, listing->map))
		{
			continue;
		}
		for (character = 0; character < BROWSER_NAME_LENGTH && listing->name[character]; character++)
		{
			unsigned short a = game->name[character], b = (unsigned char)listing->name[character];

			if ((a >= 'A' && a <= 'Z' ? a + 32 : a) != (b >= 'A' && b <= 'Z' ? b + 32 : b))
				break;
		}
		if ((character == BROWSER_NAME_LENGTH || !listing->name[character]) &&
			(character == BROWSER_NAME_LENGTH || !game->name[character]))
		{
			return 1;
		}
	}
	return 0;
}

/* a locked listing's code until its password opens it: its key hash (the
invite's first half, which names its host) and no token (zeros), so the
lists can show it, ping its host and match it to its listing again
(browser_unlock_game) */
static void locked_code(const struct p2p_listing *listing, char *code)
{
	p2p_hex(listing->key_hash, P2P_KEY_HASH_SIZE, code);
	memset(code + 2 * P2P_KEY_HASH_SIZE, '0', BROWSER_INVITE_LENGTH - 2 * P2P_KEY_HASH_SIZE);
	code[BROWSER_INVITE_LENGTH] = 0;
}

/* the games heard on the brokers that the list lacks, after its count
games (up to maximum_count): marked direct, and OpenCE (legacy) while the
list answers (a Chupathingy host announces its game to the list too; the
site marks a game it only hears there the same way). One with a password,
whose invite is sealed (p2p_listing_unlock), is listed locked, by its key
hash alone (locked_code), and joined once its password opens it
(browser_unlock_game); the list never has one (a password game's invite is
never sent to it). This machine's own is left out, as the list's is */
static int add_direct(struct browser_game *games, int count, int maximum_count, int list_answered)
{
	char own[BROWSER_INVITE_LENGTH + 1];
	int listed = count, index, character;

	if (!p2p_hosting_invite(own, sizeof(own)))
		own[0] = 0;
	for (index = 0; index < listing_count && count < maximum_count; index++)
	{
		const struct p2p_listing *listing = &listings[index];
		char sealed[BROWSER_INVITE_LENGTH + 1];
		const char *code = !SDL_strncasecmp(listing->invite, "halo://join/", 12) ? listing->invite + 12 : "";
		struct browser_game *game = &games[count];

		if (listing->locked)
		{
			locked_code(listing, sealed);
			code = sealed;
		}
		if (strlen(code) != BROWSER_INVITE_LENGTH ||
			!SDL_strncasecmp(code, own, 2 * P2P_KEY_HASH_SIZE) || listing_on_list(listing, code, games, listed))
		{
			continue;
		}
		memset(game, 0, sizeof(*game));
		snprintf(game->invite, sizeof(game->invite), "%s", code);
		game->locked = listing->locked;
		for (character = 0; character < BROWSER_NAME_LENGTH && listing->name[character]; character++)
			game->name[character] = (unsigned char)listing->name[character];
		snprintf(game->map, sizeof(game->map), "%s", listing->map);
		game->engine = listing->engine_type;
		game->players = listing->player_count;
		game->maximum_players = listing->maximum_player_count;
		game->open = listing->open;
		game->teams = listing->has_teams;
		game->version = listing->version;
		game->hosting = listing->dedicated ? BROWSER_HOSTING_DEDICATED : BROWSER_HOSTING_UNKNOWN;
		game->protocol = list_answered ? BROWSER_PROTOCOL_OPENCE : BROWSER_PROTOCOL_UNKNOWN;
		game->kind = listing->kind;
		game->direct = 1;
		game->opence_server = listing->opence_server;
		count++;
	}
	return count;
}

/* what the list does not say of its games and this machine knows: the
round trip to a host the tunnel reaches, and which games' public listings
have a password (a listing's key hash is its invite's first half) */
static void add_known(struct browser_game *games, int count)
{
	int index, other;

	for (index = 0; index < count; index++)
	{
		unsigned char identifier[P2P_IDENTIFIER_SIZE];

		/* the tunnel's (a game being joined or played), else a probe's (a host
		that answers pings), else an estimate */
		games[index].ping = (short)(invite_identifier(games[index].invite, identifier) ? p2p_peer_ping(identifier) : -1);
		games[index].ping_estimated = 0;
		if (games[index].ping < 0 && (games[index].capabilities & 1ul << _delta_capability_ping))
			games[index].ping = (short)p2p_probe_ping(games[index].invite, 1);
		if (games[index].ping < 0 && games[index].region[0])
		{
			long estimate;

			pthread_mutex_lock(&browser_lock);
			estimate = browser_ping_estimate(browser.anchors, browser.anchor_count, games[index].region);
			pthread_mutex_unlock(&browser_lock);
			if (estimate >= 0)
			{
				games[index].ping = (short)(estimate > 9999 ? 9999 : estimate);
				games[index].ping_estimated = 1;
			}
		}
		/* (a listing's own lock stays: add_direct's) */
		if (games[index].direct)
			continue;
		games[index].locked = 0;
		games[index].kind = 0;
		/* (the list's label, else a broker listing's; a Delta host is never
		OpenCE's) */
		if (games[index].protocol == BROWSER_PROTOCOL_DELTA)
			games[index].opence_server = 0;
		for (other = 0; other < listing_count; other++)
		{
			if (!SDL_strncasecmp(listing_key_hashes[other], games[index].invite, 2 * P2P_KEY_HASH_SIZE))
			{
				games[index].locked = listings[other].locked;
				games[index].kind = listings[other].kind;
				/* (a Delta host is never OpenCE's, whatever its listing) */
				if (games[index].protocol != BROWSER_PROTOCOL_DELTA && listings[other].opence_server)
					games[index].opence_server = 1;
				break;
			}
		}
	}
}

#endif

/* (every build, with the game browser or without: the menus' lobbies ask
it too, port/linux/game/menu_functions.c) */
#include "browser.h"

#include <string.h>

/* whether two names are the same, ASCII letters in either case (every
build has this: the server's and Android's link no SDL string functions) */
static int browser_ascii_equal(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
	{
		char x = *a >= 'A' && *a <= 'Z' ? (char)(*a - 'A' + 'a') : *a;
		char y = *b >= 'A' && *b <= 'Z' ? (char)(*b - 'A' + 'a') : *b;

		if (x != y)
			return 0;
	}
	return !*a && !*b;
}

int browser_map_is_campaign(const char *map)
{
	static const char *const levels[] = { "a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40" };
	const char *name = map ? map : "";
	size_t index;

	/* (the path's last part; a Halo PC or HaloMD map, <file>@ce or @md, is not a level) */
	for (index = 0; name[index]; index++)
	{
		if (name[index] == '@')
			return 0;
	}
	while (strpbrk(name, "\\/"))
		name = strpbrk(name, "\\/") + 1;
	for (index = 0; index < sizeof(levels) / sizeof(levels[0]); index++)
	{
		if (browser_ascii_equal(name, levels[index]))
			return 1;
	}
	return 0;
}

/* the lists' filter by kind of game (All, Multiplayer, Co-op), kept for the
session (every build: the consoles' Join Game screen has it too,
port/linux/game/menu_functions.c) */
static int list_filter;

int browser_list_filter(void)
{
	return list_filter;
}

void browser_list_filter_cycle(void)
{
	list_filter = (list_filter + 1) % BROWSER_LIST_FILTERS;
}

const char *browser_list_filter_name(void)
{
	static const char *const names[BROWSER_LIST_FILTERS] = { "ALL", "MULTIPLAYER", "CO-OP" };

	return names[list_filter];
}

int browser_list_filter_allows(const char *map)
{
	if (list_filter == BROWSER_LIST_FILTER_ALL)
		return 1;
	return browser_map_is_campaign(map) == (list_filter == BROWSER_LIST_FILTER_COOP);
}

int browser_version_state(int version)
{
	if (version < delta_legacy_minimum())
		return -1;
	return version > delta_legacy_maximum() ? 1 : 0;
}

#ifdef HALO_GAME_BROWSER

int browser_game_host_cheats(const struct browser_game *game)
{
	return (game->capabilities & 1ul << _delta_capability_host_cheats) != 0;
}

int browser_game_version_state(const struct browser_game *game)
{
	/* (a Delta host's other number may still be joined, as above) */
	if (game->protocol == BROWSER_PROTOCOL_DELTA && delta_peer_protocol() != _delta_peer_protocol_opence)
		return 0;
	return browser_version_state(game->version);
}

int browser_get_games(struct browser_game *games, int maximum_count)
{
	int count;

	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	browser.list_wanted = 1;
	count = browser.game_count < maximum_count ? browser.game_count : maximum_count;
	memcpy(games, browser.games, sizeof(*games) * (size_t)count);
	{
		int index;

		for (index = 0; index < count; index++)
		{
			if (games[index].badge && !server_badge_joined_allows(&browser.badge_joined, games[index].invite,
				games[index].badge, games[index].badge_label, games[index].has_badge_key ? games[index].badge_key : NULL))
			{
				games[index].badge = 0;
				games[index].badge_label[0] = 0;
			}
		}
	}
	pthread_mutex_unlock(&browser_lock);
	read_listings();
	count = add_direct(games, count, maximum_count, browser_list_status() == BROWSER_LIST_ANSWERED);
	add_known(games, count);
	return count;
}

int browser_unlock_game(const char *invite, const char *password, char *code, int size)
{
	int index;

	for (index = 0; index < listing_count; index++)
	{
		struct p2p_listing listing = listings[index];
		char sealed[BROWSER_INVITE_LENGTH + 1];

		if (!listing.locked)
			continue;
		locked_code(&listing, sealed);
		if (text_compare_no_case(sealed, invite))
			continue;
		if (!p2p_listing_unlock(&listing, password) || SDL_strncasecmp(listing.invite, "halo://join/", 12) ||
			strlen(listing.invite + 12) != BROWSER_INVITE_LENGTH)
		{
			return 0;
		}
		snprintf(code, (size_t)size, "%s", listing.invite + 12);
		memset(&listing, 0, sizeof(listing));
		return 1;
	}
	return -1;
}

int browser_host_listing(int *status)
{
	char invite[BROWSER_INVITE_LENGTH + 1];
	int reported, listed, tried, answer;

	*status = 0;
	if (!config_string("network.browser_url")[0])
		return BROWSER_HOST_NO_LIST;
	if (!config_boolean("network.list_hosted_games"))
		return BROWSER_HOST_OFF;
	pthread_mutex_lock(&browser_lock);
	reported = browser.host_reported && !elapsed(browser.host_report_time, HOST_TIMEOUT);
	listed = browser.listed_invite[0] != 0;
	tried = browser.announce_tried;
	answer = browser.announce_status;
	pthread_mutex_unlock(&browser_lock);
	if (!reported || !p2p_hosting_invite(invite, sizeof(invite)))
		return BROWSER_HOST_NOT_HOSTING;
	if (!p2p_hosting_listable_invite(invite, sizeof(invite)))
		return BROWSER_HOST_PRIVATE;
	if (listed)
		return BROWSER_HOST_LISTED;
	if (!tried)
		return BROWSER_HOST_WAITING;
	*status = answer;
	return BROWSER_HOST_FAILED;
}

const char *browser_badge_name(int badge)
{
	switch (badge)
	{
	case _server_badge_tier_official:
		return "Official server";
	case _server_badge_tier_developer:
		return "Developer server";
	case _server_badge_tier_community:
		return "Community server";
	default:
		return NULL;
	}
}

void browser_server_badge_proven(const char *invite, const struct server_badge *proven)
{
	if (!invite || strlen(invite) != BROWSER_INVITE_LENGTH)
		return;
	pthread_mutex_lock(&browser_lock);
	server_badge_joined_note(&browser.badge_joined, invite, proven);
	pthread_mutex_unlock(&browser_lock);
}

int browser_list_status(void)
{
	int status;

	if (!config_string("network.browser_url")[0])
		return BROWSER_LIST_NONE;
	pthread_mutex_lock(&browser_lock);
	status = browser.list_failed ? BROWSER_LIST_FAILED : !browser.list_time && !browser.game_count ?
		BROWSER_LIST_WAITING : BROWSER_LIST_ANSWERED;
	pthread_mutex_unlock(&browser_lock);
	return status;
}

void browser_follow_listings(int on)
{
	int online = config_boolean("network.online");

	p2p_lobby_browse(on && online);
	/* (no pings with internet play off: no probe, no anchor timed) */
	p2p_probe_browse(on && online);
	if (on && online)
	{
		pthread_mutex_lock(&browser_lock);
		browser.anchors_wanted = 1;
		pthread_mutex_unlock(&browser_lock);
	}
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

int browser_join(const char *invite, int open)
{
	/* ("halo://join/" and the invite's digits) */
	char link[16 + BROWSER_INVITE_LENGTH];

	snprintf(link, sizeof(link), "halo://join/%s", invite);
	platform_log("Game list: joining a listed game");
	return open ? p2p_join_listed_invite(link) : p2p_join_invite(link);
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

#ifndef HALO_CONSOLE_NO_GUIDE
void browser_presence_playing(int playing)
{
	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	browser.presence_playing = playing != 0;
	browser.presence_said = p2p_now();
	pthread_mutex_unlock(&browser_lock);
}

void browser_social_where(int in_game, int guide_open)
{
	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	if (!browser.social_state)
		browser.social_state = calloc(1, sizeof(*browser.social_state));
	/* (the Guide opening: its friends soon) */
	if (guide_open && !browser.social_guide_open)
		social_poll_soon(&browser.social_poll);
	browser.social_in_game = in_game != 0;
	browser.social_guide_open = guide_open != 0;
	browser.social_said = p2p_now();
	pthread_mutex_unlock(&browser_lock);
}

int browser_social_get(struct social_state *state, int *serial)
{
	int status;

	pthread_mutex_lock(&browser_lock);
	status = browser.social_status;
	if (browser.social_state && *serial != browser.social_serial)
	{
		*state = *browser.social_state;
		*serial = browser.social_serial;
	}
	pthread_mutex_unlock(&browser_lock);
	return status;
}

int browser_social_act(int action, const char *handle)
{
	int taken = 0;

	if (action < 0 || action >= NUMBER_OF_BROWSER_SOCIAL_ACTIONS || !social_handle_valid(handle))
		return 0;
	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	if (browser.social_result.state != BROWSER_SOCIAL_BUSY)
	{
		browser.social_result.serial++;
		browser.social_result.state = BROWSER_SOCIAL_BUSY;
		browser.social_result.action = action;
		snprintf(browser.social_result.handle, sizeof(browser.social_result.handle), "%s", handle);
		browser.social_result.message[0] = 0;
		browser.social_action_wanted = 1;
		browser.social_join[0] = 0;
		taken = 1;
	}
	pthread_mutex_unlock(&browser_lock);
	return taken;
}

void browser_social_result(struct browser_social_result *result)
{
	pthread_mutex_lock(&browser_lock);
	*result = browser.social_result;
	pthread_mutex_unlock(&browser_lock);
}

void browser_party_get(struct social_party *party, int *serial)
{
	pthread_mutex_lock(&browser_lock);
	if (*serial != browser.party_serial)
	{
		*party = browser.party;
		*serial = browser.party_serial;
	}
	pthread_mutex_unlock(&browser_lock);
}

int browser_party_act(const char *action, const char *value, const char *name)
{
	int taken = 0;

	if (!action || strlen(action) >= sizeof(browser.party_action) || (value && strlen(value) >= sizeof(browser.party_value)))
		return 0;
	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	if (browser.party_result.state != BROWSER_SOCIAL_BUSY)
	{
		browser.party_result.serial++;
		browser.party_result.state = BROWSER_SOCIAL_BUSY;
		browser.party_result.action = 0;
		browser.party_result.message[0] = 0;
		snprintf(browser.party_action, sizeof(browser.party_action), "%s", action);
		snprintf(browser.party_value, sizeof(browser.party_value), "%s", value ? value : "");
		if (name && name[0])
			snprintf(browser.party_name, sizeof(browser.party_name), "%s", name);
		browser.party_action_wanted = 1;
		taken = 1;
	}
	pthread_mutex_unlock(&browser_lock);
	return taken;
}

void browser_party_result(struct browser_social_result *result)
{
	pthread_mutex_lock(&browser_lock);
	*result = browser.party_result;
	pthread_mutex_unlock(&browser_lock);
}

void browser_party_go(const char *invite)
{
	if (!social_presence_invite_valid(invite))
		return;
	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	snprintf(browser.party_go, sizeof(browser.party_go), "%s", invite);
	pthread_mutex_unlock(&browser_lock);
}

/* a party's link waiting for the player's word (guide.c asks), under its
own lock: the link comes from the p2p thread too, which holds p2p_lock */
static pthread_mutex_t party_link_lock = PTHREAD_MUTEX_INITIALIZER;
static char party_link_code[SOCIAL_PARTY_CODE_SIZE];

int browser_party_link(const char *text)
{
	char code[SOCIAL_PARTY_CODE_SIZE];

	if (!text || SDL_strncasecmp(text, "halo://party/", 13))
		return 0;
	if (!social_party_code_clean(text, code))
	{
		platform_log("DELTA: that party link has no party code");
		return 1;
	}
	/* (never joined unseen: the Guide asks first, guide.c) */
	platform_log("DELTA: a party link, waiting for the player");
	pthread_mutex_lock(&party_link_lock);
	snprintf(party_link_code, sizeof(party_link_code), "%s", code);
	pthread_mutex_unlock(&party_link_lock);
	return 1;
}

int browser_party_take_link(char *code, int size)
{
	int taken;

	pthread_mutex_lock(&party_link_lock);
	taken = party_link_code[0] != 0;
	if (taken)
		snprintf(code, (size_t)size, "%s", party_link_code);
	party_link_code[0] = 0;
	pthread_mutex_unlock(&party_link_lock);
	return taken;
}

int browser_social_take_join(char *link, int size)
{
	int taken;

	pthread_mutex_lock(&browser_lock);
	taken = browser.social_join[0] != 0;
	snprintf(link, (size_t)size, "%s", browser.social_join);
	browser.social_join[0] = 0;
	pthread_mutex_unlock(&browser_lock);
	return taken;
}
#endif

/* ---------- Delta Profile in the game (public) */

void browser_profile_load(int force)
{
	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	if (!config_string("network.browser_url")[0])
		browser.game_profile_state = BROWSER_PROFILE_NO_LIST;
	else if (force || !browser.game_profile_has || browser.game_profile_state == BROWSER_PROFILE_NO_LIST ||
		elapsed(browser.game_profile_time, GAME_PROFILE_STALE))
	{
		browser.game_profile_wanted = 1;
		if (browser.game_profile_state != BROWSER_PROFILE_LOADED || !browser.game_profile_has)
			browser.game_profile_state = BROWSER_PROFILE_LOADING;
	}
	pthread_mutex_unlock(&browser_lock);
}

void browser_profile_get(struct browser_profile_status *status, struct delta_profile *profile)
{
	pthread_mutex_lock(&browser_lock);
	status->state = browser.game_profile_state;
	status->has_profile = browser.game_profile_has;
	status->loading = browser.game_profile_wanted;
	status->serial = browser.game_profile_serial;
	status->age_seconds = browser.game_profile_has ? (p2p_now() - browser.game_profile_time) / 1000 : 0;
	status->action = browser.game_action;
	memcpy(status->message, browser.game_message, sizeof(status->message));
	if (profile)
		*profile = browser.game_profile;
	pthread_mutex_unlock(&browser_lock);
}

void browser_profile_unlink(void)
{
	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	browser.game_unlink_wanted = 1;
	browser.game_action = BROWSER_PROFILE_ACTION_WORKING;
	browser.game_message[0] = 0;
	pthread_mutex_unlock(&browser_lock);
}

void browser_profile_privacy(const char *field, int value)
{
	int index;

	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);

	for (index = 0; index < browser.game_privacy_count && strcmp(browser.game_privacy_fields[index], field); index++)
		;
	if (index < 6)
	{
		snprintf(browser.game_privacy_fields[index], sizeof(browser.game_privacy_fields[index]), "%s", field);
		browser.game_privacy_values[index] = value;
		if (index == browser.game_privacy_count)
			browser.game_privacy_count++;
	}
	browser.game_action = BROWSER_PROFILE_ACTION_WORKING;
	browser.game_message[0] = 0;
	pthread_mutex_unlock(&browser_lock);
}

void browser_profile_friends_privacy(const char *field, const char *value)
{
	char *pending;

	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	pending = !strcmp(field, "friend_requests") ? browser.game_friends_requests :
		!strcmp(field, "who_can_join") ? browser.game_friends_join :
		!strcmp(field, "clan_presence") ? browser.game_friends_presence : NULL;
	if (pending)
	{
		snprintf(pending, 24, "%s", value ? value : "");
		browser.game_action = BROWSER_PROFILE_ACTION_WORKING;
		browser.game_message[0] = 0;
	}
	pthread_mutex_unlock(&browser_lock);
}

void browser_profile_clan(const char *action, int clan)
{
	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	snprintf(browser.game_clan_action, sizeof(browser.game_clan_action), "%s", action);
	browser.game_clan_id = clan;
	browser.game_clan_handle[0] = 0;
	browser.game_action = BROWSER_PROFILE_ACTION_WORKING;
	browser.game_message[0] = 0;
	pthread_mutex_unlock(&browser_lock);
}

#ifndef HALO_CONSOLE_NO_GUIDE
int browser_profile_clan_member(const char *action, const char *handle)
{
	if ((strcmp(action, "invite") && strcmp(action, "kick")) || !social_handle_valid(handle))
		return 0;
	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	snprintf(browser.game_clan_action, sizeof(browser.game_clan_action), "%s", action);
	snprintf(browser.game_clan_handle, sizeof(browser.game_clan_handle), "%s", handle);
	browser.game_clan_id = 0;
	browser.game_action = BROWSER_PROFILE_ACTION_WORKING;
	browser.game_message[0] = 0;
	pthread_mutex_unlock(&browser_lock);
	return 1;
}
#endif

void browser_profile_sign_out(void)
{
	pthread_mutex_lock(&browser_lock);
	browser.device_sign_out_wanted = 1;
	game_action_done(BROWSER_PROFILE_ACTION_WORKING, "");
	pthread_mutex_unlock(&browser_lock);
}

void browser_profile_revoke(const char *device_id)
{
	if (!device_id || strlen(device_id) != DELTA_PROFILE_ID_LENGTH ||
		strspn(device_id, "0123456789abcdef") != DELTA_PROFILE_ID_LENGTH)
	{
		return;
	}
	pthread_mutex_lock(&browser_lock);
	snprintf(browser.device_revoke, sizeof(browser.device_revoke), "%s", device_id);
	game_action_done(BROWSER_PROFILE_ACTION_WORKING, "");
	pthread_mutex_unlock(&browser_lock);
}

int browser_approve_start(const char *code)
{
#ifdef HALO_DELTA_IDENTITY
	char clean[IDENTITY_CODE_LENGTH + 1];

	if (!identity_approval_code(code, clean))
		return 0;
	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	browser.approve_serial++;
	browser.approve_state = BROWSER_APPROVE_SENDING;
	/* (sent without its dash, as /connect takes either) */
	snprintf(browser.approve_code, sizeof(browser.approve_code), "%s", clean);
	browser.approve_handle[0] = browser.approve_name[0] = browser.approve_platform[0] = 0;
	browser.approve_message[0] = 0;
	pthread_mutex_unlock(&browser_lock);
	return 1;
#else
	(void)code;
	return 0;
#endif
}

void browser_approve_answer(int approve)
{
	pthread_mutex_lock(&browser_lock);
	if (browser.approve_state == BROWSER_APPROVE_SHOWN)
	{
		browser.approve_answer = approve ? 1 : -1;
		browser.approve_state = BROWSER_APPROVE_ANSWERING;
		browser.approve_message[0] = 0;
	}
	pthread_mutex_unlock(&browser_lock);
}

void browser_approve_stop(void)
{
	pthread_mutex_lock(&browser_lock);
	browser.approve_serial++;
	browser.approve_state = BROWSER_APPROVE_NONE;
	pthread_mutex_unlock(&browser_lock);
}

void browser_approve_get(struct browser_approve *approve)
{
	pthread_mutex_lock(&browser_lock);
	approve->state = browser.approve_state;
	memcpy(approve->handle, browser.approve_handle, sizeof(approve->handle));
	memcpy(approve->name, browser.approve_name, sizeof(approve->name));
	memcpy(approve->platform, browser.approve_platform, sizeof(approve->platform));
	memcpy(approve->message, browser.approve_message, sizeof(approve->message));
	pthread_mutex_unlock(&browser_lock);
}

void browser_profile_action_seen(void)
{
	pthread_mutex_lock(&browser_lock);
	if (browser.game_action != BROWSER_PROFILE_ACTION_WORKING)
		browser.game_action = BROWSER_PROFILE_ACTION_NONE;
	pthread_mutex_unlock(&browser_lock);
}

int browser_player_card(const char *player_id, struct delta_profile_card *card)
{
	struct player_card *slot = NULL;
	int index, known = 0;

	if (strlen(player_id) != DELTA_PROFILE_ID_LENGTH || !config_string("network.browser_url")[0])
		return 0;
	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	for (index = 0; index < browser.card_count && !slot; index++)
	{
		if (!strcmp(browser.cards[index].id, player_id))
			slot = &browser.cards[index];
	}
	if (!slot)
	{
		/* (a new one, in place of the oldest when they are all taken) */
		if (browser.card_count < BROWSER_PLAYER_CARDS)
			slot = &browser.cards[browser.card_count++];
		else
		{
			slot = &browser.cards[0];
			for (index = 1; index < browser.card_count; index++)
			{
				if (browser.cards[index].time < slot->time)
					slot = &browser.cards[index];
			}
		}
		memset(slot, 0, sizeof(*slot));
		snprintf(slot->id, sizeof(slot->id), "%s", player_id);
		slot->state = _card_wanted;
		slot->time = p2p_now();
	}
	else if (slot->state == _card_none && elapsed(slot->time, CARD_NONE_AGAIN))
		slot->state = _card_wanted;
	if (slot->state == _card_known)
	{
		*card = slot->card;
		known = 1;
	}
	pthread_mutex_unlock(&browser_lock);
	return known;
}

/* any short text (a link) as a QR code: its size, 0 if it doesn't fit */
int browser_text_qr(const char *text, unsigned char *modules)
{
	unsigned char qr[qrcodegen_BUFFER_LEN_FOR_VERSION(CONNECT_QR_VERSION)];
	unsigned char work[qrcodegen_BUFFER_LEN_FOR_VERSION(CONNECT_QR_VERSION)];
	int size, x, y;

	if (!text || !text[0] || !qrcodegen_encodeText(text, work, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
		CONNECT_QR_VERSION, qrcodegen_Mask_AUTO, true))
	{
		return 0;
	}
	size = qrcodegen_getSize(qr);
	for (y = 0; y < size; y++)
	{
		for (x = 0; x < size; x++)
			modules[y * size + x] = qrcodegen_getModule(qr, x, y) ? 1 : 0;
	}
	return size;
}

/* a page of the site, as a player types it, and as a QR code */
int browser_page_qr(const char *path, char *page, int page_size, unsigned char *modules)
{
	unsigned char qr[qrcodegen_BUFFER_LEN_FOR_VERSION(CONNECT_QR_VERSION)];
	unsigned char work[qrcodegen_BUFFER_LEN_FOR_VERSION(CONNECT_QR_VERSION)];
	char url[640];
	int size, x, y;

	browser_server_name(page, page_size);
	if (!page[0])
		return 0;
	strncat(page, path, (size_t)page_size - strlen(page) - 1);
	server_url(path, url, sizeof(url));
	if (!qrcodegen_encodeText(url, work, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, CONNECT_QR_VERSION,
		qrcodegen_Mask_AUTO, true))
	{
		return 0;
	}
	size = qrcodegen_getSize(qr);
	for (y = 0; y < size; y++)
	{
		for (x = 0; x < size; x++)
			modules[y * size + x] = qrcodegen_getModule(qr, x, y) ? 1 : 0;
	}
	return size;
}

#endif
