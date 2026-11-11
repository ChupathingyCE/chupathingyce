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
the blacklist (it downloads nothing). The index is fetched for its
blacklist whatever network.map_downloads says: it is a public document and
the request carries nothing about the player. With "never" (and on the
dedicated server) the mirror list isn't asked for; set back to "ask" or
"always", the next refresh asks for it at once (map_repo_refresh_due). An empty
network.maps_url turns all of it off: nothing fetched, nothing kept read
back, nothing refused. Elsewhere nothing is asked and nothing refused.

What was taken is kept in the save root and read back at start
(map_repo_load), so the blacklist holds before the first fetch and while
the repository can't be reached. A fetch that fails is one line in the log
(debug.txt) and is tried again later (map_repo.c's wait); nothing is shown
and nothing else waits on it. The log has the repository's and mirrors'
addresses (never a player's) and why a document or a file was refused.
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
#ifdef _WIN32
#include <process.h>
#define process_id() ((long)_getpid())
#else
#include <unistd.h>
#define process_id() ((long)getpid())
#endif

#if defined(HALO_GAME_BROWSER)

/* ---------- constants */

enum
{
	CHUNK_SIZE = 64 * 1024,
	/* an index or mirror list's body, at most: the connection is shared
	with the game list, stats, updater and control link while it is open.
	A host that stops sending is already given up on after ten seconds of
	silence (each read's own limit, posix_browser.c TIMEOUT_MILLISECONDS),
	so this only bounds one that keeps sending slowly: ten minutes lets an
	index near its 8 MB cap through at about 14 KB/s */
	DOCUMENT_DEADLINE_MILLISECONDS = 10 * 60 * 1000,
};

/* ---------- globals */

static struct
{
	pthread_mutex_t lock;
	pthread_cond_t done;
	int refreshing;
	int loaded;
} map_repo_game = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0 };

/* ---------- the transport */

static int game_get(void *context, const char *url, char *buffer, int size, char *why, int why_size)
{
	(void)context;
	/* (not logged: the refresh logs its one line, naming the address) */
	return posix_browser_request_as(url, NULL, NULL, build_identity_user_agent(), buffer, size, why, why_size);
}

/* where the bytes of a streamed GET go: 1 when written */
typedef int (*stream_write)(void *file, const char *data, int count);

/* a GET of url streamed to write (file), at most maximum bytes: 1 with
the bytes received in *received and their SHA-256 in sha256 (NULL: not
wanted), else 0 with the HTTP status in *status (0 for none) and why in
why; a map's download tells hooks (NULL: none); deadline (0: the default, hours)
is the body's time in milliseconds */
static int stream_get(const char *url, stream_write write, void *file, unsigned int maximum, unsigned char *sha256,
	unsigned int *received, int *status, const struct map_repo_game_hooks *hooks, int deadline, char *why, int why_size)
{
	struct playlist_sha256 hash;
	char *buffer;
	int length = -1, download, ok = 1;

	*received = 0;
	download = posix_browser_download_open(url, build_identity_user_agent(), status, &length, why, why_size);
	if (!download)
		return 0;
	if (deadline > 0)
		posix_browser_download_deadline(download, deadline);
	if (length >= 0 && (unsigned int)length > maximum)
	{
		posix_browser_download_close(download);
		snprintf(why, (size_t)why_size, "it is %d bytes, more than the %u allowed", length, maximum);
		return 0;
	}
	buffer = (char *)malloc(CHUNK_SIZE);
	if (!buffer)
	{
		posix_browser_download_close(download);
		snprintf(why, (size_t)why_size, "out of memory");
		return 0;
	}
	playlist_sha256_begin(&hash);
	if (hooks && hooks->progress)
		hooks->progress(0, length > 0 ? (unsigned int)length : maximum);
	for (;;)
	{
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
			snprintf(why, (size_t)why_size, "it is larger than the %u bytes allowed", maximum);
			ok = 0;
			break;
		}
		if (!write(file, buffer, count))
		{
			snprintf(why, (size_t)why_size, "could not write it (is the disk full?)");
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
	if (sha256)
		playlist_sha256_end(&hash, sha256);
	return ok;
}

static int write_handle(void *file, const char *data, int count)
{
	DWORD written = 0;

	return WriteFile((HANDLE)file, data, (DWORD)count, &written, NULL) && written == (DWORD)count;
}

static int write_stdio(void *file, const char *data, int count)
{
	return fwrite(data, 1, (size_t)count, (FILE *)file) == (size_t)count;
}

static int game_get_file(void *context, const char *url, const char *path, unsigned int maximum, unsigned char *sha256,
	unsigned int *received, int *status, char *why, int why_size)
{
	const struct map_repo_game_hooks *hooks = (const struct map_repo_game_hooks *)context;
	HANDLE file;
	int ok;

	*received = 0;
	*status = 0;
	platform_log("chupathingy maps: GET %s", url);
	file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		snprintf(why, (size_t)why_size, "could not write the map's file");
		return 0;
	}
	ok = stream_get(url, write_handle, (void *)file, maximum, sha256, received, status, hooks, 0, why, why_size);
	CloseHandle(file);
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

static void kept_path(const char *name, char *path, size_t size)
{
	snprintf(path, size, "%s/%s", platform_save_root(), name);
}

static long game_load(void *context, const char *name, char *buffer, long size)
{
	char path[1024];
	FILE *file;
	size_t count;

	(void)context;
	kept_path(name, path, sizeof(path));
	file = fopen(path, "rb");
	if (!file)
		return -1;
	count = fread(buffer, 1, (size_t)size, file);
	/* (a file larger than any document kept: none of ours) */
	if (count == (size_t)size && fgetc(file) != EOF)
		count = 0;
	fclose(file);
	return count ? (long)count : -1;
}

/* the index or the mirror list streamed into the save root's file name
(never more than maximum bytes on disk or in memory), then read back */
static int game_get_document(void *context, const char *url, const char *name, char *buffer, long maximum, long *size,
	int *status, char *why, int why_size)
{
	char path[1024];
	unsigned int received = 0;
	FILE *file;
	size_t count;
	int ok;

	(void)context;
	*size = -1;
	*status = 0;
	buffer[0] = 0;
	/* (this process's own file: a second copy sharing the save root doesn't
	write into it too) */
	{
		char own[160];
		const char *dot = strrchr(name, '.');

		snprintf(own, sizeof(own), "%.*s.%ld%s", dot ? (int)(dot - name) : (int)strlen(name), name, process_id(),
			dot ? dot : "");
		kept_path(own, path, sizeof(path));
	}
	file = fopen(path, "wb");
	if (!file)
	{
		snprintf(why, (size_t)why_size, "could not write %s", name);
		return 0;
	}
	ok = stream_get(url, write_stdio, file, (unsigned int)maximum, NULL, &received, status, NULL, DOCUMENT_DEADLINE_MILLISECONDS, why,
		why_size);
	ok = fclose(file) == 0 && ok;
	if (ok && !(file = fopen(path, "rb")))
		ok = 0;
	if (ok)
	{
		count = fread(buffer, 1, (size_t)maximum + 1, file);
		fclose(file);
		ok = count == (size_t)received;
		if (ok)
		{
			buffer[count] = 0;
			*size = (long)count;
		}
	}
	if (!ok && !why[0])
		snprintf(why, (size_t)why_size, "could not read %s back", name);
	remove(path);
	return ok;
}

/* a new file beside it, then moved over it (as Delta's cache is written) */
static void game_save(void *context, const char *name, const char *data, long size)
{
	char path[1024], temporary[1100];
	FILE *file;
	int ok;

	(void)context;
	kept_path(name, path, sizeof(path));
	snprintf(temporary, sizeof(temporary), "%s.new", path);
	file = fopen(temporary, "wb");
	if (!file)
		return;
	ok = fwrite(data, 1, (size_t)size, file) == (size_t)size;
	ok = fclose(file) == 0 && ok;
	/* (Windows does not rename over a file) */
	if (ok && rename(temporary, path) != 0)
	{
		remove(path);
		ok = rename(temporary, path) == 0;
	}
	if (!ok)
	{
		remove(temporary);
		platform_log("chupathingy maps: could not keep %s", name);
	}
}

static const struct map_repo_transport map_repo_game_transport_value =
{
	NULL, game_get, game_get_document, game_get_file, game_remove, game_stop, game_load, game_save,
};

/* whether Chupathingy maps is asked anything: the build has its key, and
network.maps_url isn't empty */
static int game_enabled(void)
{
	char url[256];

	if (!map_repo_enabled())
		return 0;
	map_repo_game_url(url, (int)sizeof(url));
	return url[0] != 0;
}

/* what an earlier run kept, read back once */
static void load_once(void)
{
	char why[200] = "";
	int load;

	pthread_mutex_lock(&map_repo_game.lock);
	load = !map_repo_game.loaded;
	map_repo_game.loaded = 1;
	pthread_mutex_unlock(&map_repo_game.lock);
	if (!load)
		return;
	map_repo_load(&map_repo_game_transport_value, (long long)time(NULL), why, (int)sizeof(why));
	if (why[0])
		platform_log("chupathingy maps: %s", why);
}

/* the refresh's flags: the index alone when nothing is downloaded from it
(network.map_downloads "never", or the dedicated server, which downloads
nothing): the blacklist is all that's wanted */
static int refresh_flags(void)
{
#ifdef HALO_SERVER
	return MAP_REPO_REFRESH_INDEX_ONLY;
#else
	return map_download_setting() == MAP_DOWNLOADS_NEVER ? MAP_REPO_REFRESH_INDEX_ONLY : 0;
#endif
}

static void *refresh_thread(void *argument)
{
	(void)argument;
	map_repo_game_refresh_now();
	pthread_mutex_lock(&map_repo_game.lock);
	map_repo_game.refreshing = 0;
	pthread_cond_broadcast(&map_repo_game.done);
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

	/* (its default is MAP_REPO_DEFAULT_URL, port_config.c; an empty one is
	the player's own choice: none) */
	snprintf(url, (size_t)size, "%s", setting ? setting : "");
	length = strlen(url);
	while (length && url[length - 1] == '/')
		url[--length] = 0;
}

int map_repo_game_refresh_now(void)
{
#if defined(HALO_GAME_BROWSER)
	char url[256], why[200] = "";
	long long now = (long long)time(NULL);
	int held, flags;

	if (!game_enabled())
		return 0;
	load_once();
	map_repo_game_url(url, (int)sizeof(url));
	flags = refresh_flags();
	if (!map_repo_refresh_due(now, flags))
		return map_repo_refresh(&map_repo_game_transport_value, url, now, flags, why, (int)sizeof(why));
	held = map_repo_refresh(&map_repo_game_transport_value, url, now, flags, why, (int)sizeof(why));
	/* (one line, whatever happened: a repository that can't be reached is
	asked again later, map_repo.c's wait growing each time) */
	if (!held)
		platform_log("chupathingy maps: no new index from %s (%s); trying again later", url, why);
	else if (why[0])
		platform_log("chupathingy maps: index from %s checked; %s", url, why);
	else
		platform_log("chupathingy maps: index%s from %s checked", flags & MAP_REPO_REFRESH_INDEX_ONLY ? "" :
			" and mirror list", url);
	return held;
#else
	return 0;
#endif
}

void map_repo_game_refresh_soon(void)
{
#if defined(HALO_GAME_BROWSER)
	pthread_t thread;

	if (!game_enabled())
		return;
	load_once();
	if (!map_repo_refresh_due((long long)time(NULL), refresh_flags()))
		return;
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

void map_repo_game_refresh_wait(int milliseconds)
{
#if defined(HALO_GAME_BROWSER)
	struct timespec until;

	map_repo_game_refresh_soon();
	clock_gettime(CLOCK_REALTIME, &until);
	until.tv_sec += milliseconds / 1000;
	until.tv_nsec += (long)(milliseconds % 1000) * 1000000L;
	if (until.tv_nsec >= 1000000000L)
	{
		until.tv_sec++;
		until.tv_nsec -= 1000000000L;
	}
	pthread_mutex_lock(&map_repo_game.lock);
	while (map_repo_game.refreshing)
	{
		if (pthread_cond_timedwait(&map_repo_game.done, &map_repo_game.lock, &until) != 0)
			break;
	}
	pthread_mutex_unlock(&map_repo_game.lock);
#else
	(void)milliseconds;
#endif
}

/* whether the map file at path is on the blacklist: 1 with its entry's
reason in reason, else 0 (a fresher blacklist fetched for the next ask, if
this one is due) */
static int file_blocked(const char *path, char *reason, int size)
{
#if defined(HALO_GAME_BROWSER)
	unsigned char hash[MAP_ARCHIVE_HASH_SIZE], sha256[MAP_INDEX_HASH_SIZE];

	if (!game_enabled())
		return 0;
	map_repo_game_refresh_soon();
	/* (by its map hash or its SHA-256: the blacklist names a file either
	way, both read in one pass and kept for the run, map_file_hash.c) */
	return map_file_hash_both(path, hash, sha256) && map_repo_blocked(sha256, hash, (long long)time(NULL), reason, size);
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
