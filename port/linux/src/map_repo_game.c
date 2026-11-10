/*
MAP_REPO_GAME.C

Chupathingy maps in the game (map_repo.h): its transport (the game list's
HTTPS, as the community map repositories are asked, and the game's d:\
files), the refresh of the index on a thread of its own, and the blacklist
held against a map already here: a game on it isn't joined
(cache_files_map_present, source/cache/cache_files.c) or hosted, from the
menus or by the dedicated server (cache_files_map_host_refused). Playing it
alone or in split screen is never refused.

On the builds with the game browser (HALO_GAME_BROWSER): the game's, which
downloads (map_download.c), and the dedicated server's, which only checks
the blacklist (it downloads nothing, and fetches the index whatever
network.map_downloads says); elsewhere nothing is asked and nothing
refused. The log
(debug.txt) has each request's address (the repository's or a mirror's,
never a player's) and why a document or a file was refused.
*/

#include "platform.h"
#include "port_config.h"
#include "map_archive.h"
#include "map_download.h"
#include "map_file_hash.h"
#include "map_repo.h"
#include "playlist_manifest.h"
#if defined(HALO_GAME_BROWSER)
#include "browser_http.h"
#include "build_identity.h"
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(HALO_GAME_BROWSER)

/* ---------- constants */

enum
{
	CHUNK_SIZE = 64 * 1024,
};

/* ---------- globals */

static struct
{
	pthread_mutex_t lock;
	int refreshing;
} map_repo_game = { PTHREAD_MUTEX_INITIALIZER, 0 };

/* ---------- the transport */

static int game_get(void *context, const char *url, char *buffer, int size, char *why, int why_size)
{
	(void)context;
	platform_log("chupathingy maps: GET %s", url);
	return posix_browser_request_as(url, NULL, NULL, build_identity_user_agent(), buffer, size, why, why_size);
}

static int game_get_file(void *context, const char *url, const char *path, unsigned int maximum, unsigned char *sha256,
	unsigned int *received, int *status, char *why, int why_size)
{
	const struct map_repo_game_hooks *hooks = (const struct map_repo_game_hooks *)context;
	struct playlist_sha256 hash;
	char *buffer;
	HANDLE file;
	int length = -1, download, ok = 1;

	*received = 0;
	platform_log("chupathingy maps: GET %s", url);
	download = posix_browser_download_open(url, build_identity_user_agent(), status, &length, why, why_size);
	if (!download)
		return 0;
	if (length >= 0 && (unsigned int)length > maximum)
	{
		posix_browser_download_close(download);
		snprintf(why, (size_t)why_size, "the file is %d bytes, more than the index's", length);
		return 0;
	}
	buffer = (char *)malloc(CHUNK_SIZE);
	file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (!buffer || file == INVALID_HANDLE_VALUE)
	{
		posix_browser_download_close(download);
		free(buffer);
		if (file != INVALID_HANDLE_VALUE)
			CloseHandle(file);
		snprintf(why, (size_t)why_size, "could not write the map's file");
		return 0;
	}
	playlist_sha256_begin(&hash);
	if (hooks && hooks->progress)
		hooks->progress(0, length > 0 ? (unsigned int)length : maximum);
	for (;;)
	{
		DWORD written = 0;
		int count;

		if (hooks && hooks->stop && hooks->stop(NULL))
		{
			snprintf(why, (size_t)why_size, "cancelled");
			ok = 0;
			break;
		}
		count = posix_browser_download_read(download, buffer, CHUNK_SIZE);
		if (count == 0)
			break;
		if (count < 0)
		{
			snprintf(why, (size_t)why_size, "the connection failed after %u bytes", *received);
			ok = 0;
			break;
		}
		if ((unsigned int)count > maximum - *received)
		{
			snprintf(why, (size_t)why_size, "the file is larger than the index's %u bytes", maximum);
			ok = 0;
			break;
		}
		if (!WriteFile(file, buffer, (DWORD)count, &written, NULL) || written != (DWORD)count)
		{
			snprintf(why, (size_t)why_size, "could not write the map's file (is the disk full?)");
			ok = 0;
			break;
		}
		playlist_sha256_add(&hash, buffer, (size_t)count);
		*received += (unsigned int)count;
		if (hooks && hooks->progress)
			hooks->progress(*received, length > 0 ? (unsigned int)length : maximum);
	}
	posix_browser_download_close(download);
	free(buffer);
	CloseHandle(file);
	playlist_sha256_end(&hash, sha256);
	if (!ok)
		platform_log("chupathingy maps: %s", why);
	return ok;
}

static void game_remove(void *context, const char *path)
{
	(void)context;
	DeleteFileA(path);
}

static int game_stop(void *context)
{
	const struct map_repo_game_hooks *hooks = (const struct map_repo_game_hooks *)context;

	return hooks && hooks->stop && hooks->stop(NULL);
}

static const struct map_repo_transport map_repo_game_transport_value =
{
	NULL, game_get, game_get_file, game_remove, game_stop,
};

/* whether the index may be asked for: the game's not when the player said
never to download maps (network.map_downloads); the dedicated server's
always, as it downloads nothing and honors the blacklist for what it hosts */
static int refresh_allowed(void)
{
#ifdef HALO_SERVER
	return 1;
#else
	return map_download_setting() != MAP_DOWNLOADS_NEVER;
#endif
}

static void *refresh_thread(void *argument)
{
	(void)argument;
	map_repo_game_refresh_now();
	pthread_mutex_lock(&map_repo_game.lock);
	map_repo_game.refreshing = 0;
	pthread_mutex_unlock(&map_repo_game.lock);
	return NULL;
}

#endif

/* ---------- public code */

void map_repo_game_transport(struct map_repo_transport *transport, const struct map_repo_game_hooks *hooks)
{
	memset(transport, 0, sizeof(*transport));
#if defined(HALO_GAME_BROWSER)
	*transport = map_repo_game_transport_value;
	transport->context = (void *)hooks;
#else
	(void)hooks;
#endif
}

void map_repo_game_url(char *url, int size)
{
	const char *setting = config_string("network.maps_url");
	size_t length;

	snprintf(url, (size_t)size, "%s", setting && setting[0] ? setting : MAP_REPO_DEFAULT_URL);
	length = strlen(url);
	while (length && url[length - 1] == '/')
		url[--length] = 0;
}

int map_repo_game_refresh_now(void)
{
#if defined(HALO_GAME_BROWSER)
	char url[256], why[200] = "";
	long long now = (long long)time(NULL);
	int held;

	if (!map_repo_enabled() || !refresh_allowed())
		return 0;
	map_repo_game_url(url, (int)sizeof(url));
	if (!url[0])
		return 0;
	if (!map_repo_refresh_due(now))
		return map_repo_refresh(&map_repo_game_transport_value, url, now, 0, why, (int)sizeof(why));
	held = map_repo_refresh(&map_repo_game_transport_value, url, now, 0, why, (int)sizeof(why));
	if (why[0])
		platform_log("chupathingy maps: %s", why);
	else
		platform_log("chupathingy maps: index and mirror list checked");
	return held;
#else
	return 0;
#endif
}

void map_repo_game_refresh_soon(void)
{
#if defined(HALO_GAME_BROWSER)
	pthread_t thread;

	if (!map_repo_enabled() || !refresh_allowed() ||
		!map_repo_refresh_due((long long)time(NULL)))
	{
		return;
	}
	pthread_mutex_lock(&map_repo_game.lock);
	if (map_repo_game.refreshing)
	{
		pthread_mutex_unlock(&map_repo_game.lock);
		return;
	}
	map_repo_game.refreshing = 1;
	pthread_mutex_unlock(&map_repo_game.lock);
	if (pthread_create(&thread, NULL, refresh_thread, NULL) != 0)
	{
		pthread_mutex_lock(&map_repo_game.lock);
		map_repo_game.refreshing = 0;
		pthread_mutex_unlock(&map_repo_game.lock);
		return;
	}
	pthread_detach(thread);
#endif
}

/* whether the map file at path is on the blacklist: 1 with its entry's
reason in reason, else 0 (a fresher blacklist fetched for the next ask, if
this one is due) */
static int file_blocked(const char *path, char *reason, int size)
{
#if defined(HALO_GAME_BROWSER)
	unsigned char hash[MAP_ARCHIVE_HASH_SIZE];

	map_repo_game_refresh_soon();
	return map_repo_enabled() && map_file_hash_whole(path, hash) &&
		map_repo_blocked(NULL, hash, (long long)time(NULL), reason, size);
#else
	(void)path;
	(void)reason;
	(void)size;
	return 0;
#endif
}

int map_repo_file_refused(const char *path, const char *map_name, char *message, int size)
{
	char reason[MAP_INDEX_REASON_SIZE];

	if (!file_blocked(path, reason, (int)sizeof(reason)))
		return 0;
	platform_log("chupathingy maps: %s (%s) is on the blacklist: %s; not played in another's game", map_name, path,
		reason);
	snprintf(message, (size_t)size,
		"Your copy of %.63s is blocked by %s: %.120s. Chupathingy won't play it in network games. You can still "
		"play it by yourself or in split screen.", map_name, MAP_REPO_TITLE, reason);
	return 1;
}

int map_repo_host_refused(const char *path, const char *map_name, char *message, int size)
{
	char reason[MAP_INDEX_REASON_SIZE];

	if (!file_blocked(path, reason, (int)sizeof(reason)))
		return 0;
	platform_log("chupathingy maps: %s (%s) is on the blacklist: %s; not hosted", map_name, path, reason);
#ifdef HALO_SERVER
	snprintf(message, (size_t)size, "%.63s is blocked by %s: %.120s. This server won't host it.", map_name,
		MAP_REPO_TITLE, reason);
#else
	snprintf(message, (size_t)size,
		"%.63s is blocked by %s: %.120s. Chupathingy won't host network games on it. You can still play it by "
		"yourself or in split screen.", map_name, MAP_REPO_TITLE, reason);
#endif
	return 1;
}
