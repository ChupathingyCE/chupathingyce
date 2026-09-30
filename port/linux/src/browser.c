/*
BROWSER.C

The game list (browser.h; configure.py
--game-browser): hosted system link games announced to the list server
(network.browser_url, server/list_server.py) with their invites, and the
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
#include "port_config.h"
#include "browser_http.h"
#include "p2p.h"
#include "p2p_internal.h"
#include "browser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void announce(const char *invite, const struct hosted_game *game)
{
	char url[512], form[1024], response[256], error[256], text[128];
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
		size = strlen(report) + 64;
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
static int parse_game(char *line, struct browser_game *game)
{
	char *fields[11];
	int count = 0;
	char *cursor = line;

	while (count < 11)
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
			if (parse_game(line, &games[count]) && games[count].version == HALO_PORT_NETWORK_VERSION &&
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
	short maximum_players, int open, short score_limit, int teams)
{
	struct hosted_game game;

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
	size_t size = 256 + (size_t)count * 320;
	char *report = malloc(size);
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
	for (index = 0; index < count && (size_t)used < size - 320; index++)
	{
		const struct browser_report_player *player = &players[index];

		used += snprintf(report + used, size - (size_t)used, "%s{\"name\": ", index ? ", " : "");
		used += json_name(report + used, (int)(size - (size_t)used), player->name, 12);
		used += snprintf(report + used, size - (size_t)used,
			", \"team\": %d, \"place\": %d, \"score\": %d, \"kills\": %d, \"assists\": %d, \"deaths\": %d, "
			"\"betrayals\": %d, \"suicides\": %d, \"shots_fired\": %d, \"shots_hit\": %d, \"multikills\": %d}",
			player->team, player->place, player->score, player->kills, player->assists, player->deaths,
			player->betrayals, player->suicides, player->shots_fired, player->shots_hit, player->multikills);
	}
	snprintf(report + used, size - (size_t)used, "]}");

	pthread_once(&browser_once, start_thread);
	pthread_mutex_lock(&browser_lock);
	free(browser.report);
	browser.report = report;
	pthread_mutex_unlock(&browser_lock);
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
	char link[64];

	snprintf(link, sizeof(link), "halo://join/%s", invite);
	platform_log("Game list: joining a listed game");
	return p2p_join_invite(link);
}

int browser_dedicated(void)
{
	const char *playlist = getenv("HALO_DEDICATED");

	return playlist && playlist[0];
}

#endif
