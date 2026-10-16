/*
MAP_ART.C

The site's pictures of the stock multiplayer maps (map_art.h), for Online
Games' details: read from the save root's map_art folder if they were had
before, else downloaded from the site over HTTPS while online play is on,
checked to be a JPEG picture of a sensible size, and kept there. One
picture is loaded at a time, on a thread of its own, so the screen never
waits; a few decoded pictures are kept for the screen to draw. A download
that fails is asked for again after a while; a picture the site does not
have (404) is not asked for again this run. The log says nothing of them
(the site's address alone would be all it could say).
*/

#ifdef HALO_GAME_BROWSER

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "browser_http.h"
#include "build_identity.h"
#include "ui_font.h"
#include "map_art.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ART_URL "https://stats.chupathingy.org/art/"
#define CACHE_FOLDER "map_art"

enum
{
	/* the pictures kept decoded */
	KEPT = 8,
	PATH_SIZE = 48,
	/* (the site's are about 8 KB) */
	LARGEST_FILE = 256 * 1024,
	/* seconds before a failed download is asked for again */
	RETRY_SECONDS = 60,
};

/* the stock maps the site has pictures of: the Xbox's (art/maps), then
Halo PC's own; Halo PC's picture of every one of them is in art/maps/ce
(as discord_activity.c's stock_maps) */
static const struct
{
	const char *file;
	int on_xbox;
} stock_maps[] =
{
	{ "beavercreek", 1 }, { "sidewinder", 1 }, { "damnation", 1 }, { "ratrace", 1 }, { "prisoner", 1 },
	{ "hangemhigh", 1 }, { "chillout", 1 }, { "carousel", 1 }, { "boardingaction", 1 }, { "bloodgulch", 1 },
	{ "wizard", 1 }, { "putput", 1 }, { "longest", 1 },
	{ "icefields", 0 }, { "deathisland", 0 }, { "dangercanyon", 0 }, { "infinity", 0 }, { "timberland", 0 },
	{ "gephyrophobia", 0 },
};

struct entry
{
	char path[PATH_SIZE];
	int state;
	/* (loading: the thread's; else the game's thread's, under the lock) */
	int loading;
	unsigned char *pixels;
	int width, height, serial;
	/* the download failed (not a 404): when, to ask again after a while */
	time_t failed_time;
	unsigned long used;
};

static pthread_mutex_t art_lock = PTHREAD_MUTEX_INITIALIZER;
static struct
{
	struct entry entries[KEPT];
	int busy;
	int next_serial;
	unsigned long uses;
} art;

int map_art_site_path(int xbox_set, const char *file, char *path, int size)
{
	size_t index;

	for (index = 0; index < sizeof(stock_maps) / sizeof(stock_maps[0]); index++)
	{
		const char *a = stock_maps[index].file, *b = file;

		/* (any case: a map's file name as the game has it) */
		while (*a && *b && (*a | 0x20) == (*b | 0x20))
			a++, b++;
		if (*a || *b || (xbox_set && !stock_maps[index].on_xbox))
			continue;
		snprintf(path, (size_t)size, xbox_set ? "maps/%s" : "maps/ce/%s", stock_maps[index].file);
		return 1;
	}
	return 0;
}

/* the cache file of a site path ("maps/ce/icefields": map_art/maps_ce_icefields.jpg) */
static void cache_file(const char *path, char *file, int size, int folder_only)
{
	int used = snprintf(file, (size_t)size, "%s/%s", platform_save_root(), CACHE_FOLDER);
	const char *cursor;

	if (folder_only || used <= 0 || used >= size - 1)
		return;
	file[used++] = '/';
	for (cursor = path; *cursor && used < size - 5; cursor++)
		file[used++] = *cursor == '/' ? '_' : *cursor;
	snprintf(file + used, (size_t)(size - used), ".jpg");
}

/* a file's bytes (up to LARGEST_FILE), NULL if there is none */
static unsigned char *read_file(const char *file, int *size)
{
	FILE *stream = fopen(file, "rb");
	unsigned char *data;

	*size = 0;
	if (!stream)
		return NULL;
	data = malloc(LARGEST_FILE);
	if (data)
		*size = (int)fread(data, 1, LARGEST_FILE, stream);
	fclose(stream);
	if (data && *size <= 0)
	{
		free(data);
		data = NULL;
	}
	return data;
}

/* the site's picture downloaded: its bytes, NULL with *missing set when the
site has none (404) */
static unsigned char *download(const char *path, int *size, int *missing)
{
	char url[160], error[128];
	unsigned char *data;
	int handle, status = 0, length = 0, got;

	*size = 0;
	*missing = 0;
	snprintf(url, sizeof(url), ART_URL "%s.jpg", path);
	handle = posix_browser_download_open(url, build_identity_user_agent(), &status, &length, error, sizeof(error));
	if (!handle)
	{
		*missing = status == 404;
		return NULL;
	}
	data = malloc(LARGEST_FILE);
	while (data && *size < LARGEST_FILE && (got = posix_browser_download_read(handle, data + *size,
		LARGEST_FILE - *size)) > 0)
	{
		*size += got;
	}
	posix_browser_download_close(handle);
	if (data && (*size <= 0 || *size >= LARGEST_FILE || (length >= 0 && *size != length)))
	{
		free(data);
		data = NULL;
	}
	return data;
}

/* kept in the cache: written beside it, then renamed over it */
static void keep(const char *path, const unsigned char *data, int size)
{
	char file[512], temporary[520];
	FILE *stream;

	cache_file(path, file, sizeof(file), 1);
	posix_make_directory(file);
	cache_file(path, file, sizeof(file), 0);
	snprintf(temporary, sizeof(temporary), "%s.part", file);
	stream = fopen(temporary, "wb");
	if (!stream)
		return;
	if (fwrite(data, 1, (size_t)size, stream) != (size_t)size)
	{
		fclose(stream);
		remove(temporary);
		return;
	}
	fclose(stream);
	remove(file);
	if (rename(temporary, file) != 0)
		remove(temporary);
}

static void *load_thread(void *argument)
{
	struct entry *entry = argument;
	char path[PATH_SIZE], file[512];
	unsigned char *data, *pixels = NULL;
	int size = 0, width = 0, height = 0, missing = 0, downloaded = 0;

	pthread_mutex_lock(&art_lock);
	memcpy(path, entry->path, sizeof(path));
	pthread_mutex_unlock(&art_lock);

	cache_file(path, file, sizeof(file), 0);
	data = read_file(file, &size);
	if (data)
	{
		pixels = posix_ui_image_decode(data, size, &width, &height);
		free(data);
		data = NULL;
	}
	if (!pixels && config_boolean("network.online"))
	{
		data = download(path, &size, &missing);
		if (data)
			pixels = posix_ui_image_decode(data, size, &width, &height);
		downloaded = pixels != NULL;
	}
	if (downloaded)
		keep(path, data, size);
	free(data);

	pthread_mutex_lock(&art_lock);
	entry->loading = 0;
	art.busy = 0;
	if (pixels)
	{
		entry->pixels = pixels;
		entry->width = width;
		entry->height = height;
		entry->serial = ++art.next_serial;
		entry->state = MAP_ART_READY;
	}
	else
	{
		/* (none on the site: not asked again; else again after a while,
		or once online play is on) */
		entry->state = MAP_ART_NONE;
		entry->failed_time = missing ? 0 : time(NULL);
	}
	pthread_mutex_unlock(&art_lock);
	return NULL;
}

/* an entry for the path (under the lock): its own, or the one used least
long ago and not loading, emptied */
static struct entry *entry_for(const char *path)
{
	struct entry *found = NULL;
	int index;

	for (index = 0; index < KEPT; index++)
	{
		if (art.entries[index].path[0] && !strcmp(art.entries[index].path, path))
			return &art.entries[index];
	}
	for (index = 0; index < KEPT; index++)
	{
		struct entry *entry = &art.entries[index];

		if (!entry->loading && (!found || entry->used < found->used))
			found = entry;
	}
	if (!found)
		return NULL;
	if (found->pixels)
		posix_ui_image_free(found->pixels);
	memset(found, 0, sizeof(*found));
	snprintf(found->path, sizeof(found->path), "%s", path);
	found->state = MAP_ART_LOADING;
	return found;
}

int map_art_get(const char *path, const unsigned char **pixels, int *width, int *height, int *serial)
{
	struct entry *entry;
	int state;

	*pixels = NULL;
	*width = *height = *serial = 0;
	if (!path || !path[0] || strlen(path) >= PATH_SIZE)
		return MAP_ART_NONE;
	pthread_mutex_lock(&art_lock);
	entry = entry_for(path);
	if (!entry)
	{
		pthread_mutex_unlock(&art_lock);
		return MAP_ART_LOADING;
	}
	entry->used = ++art.uses;
	/* (a new one, or a failed download's time to ask again: loaded when no
	other is) */
	if (entry->state == MAP_ART_NONE && entry->failed_time && time(NULL) - entry->failed_time >= RETRY_SECONDS)
		entry->state = MAP_ART_LOADING;
	if (entry->state == MAP_ART_LOADING && !entry->loading && !art.busy)
	{
		pthread_t thread;

		entry->loading = 1;
		art.busy = 1;
		if (pthread_create(&thread, NULL, load_thread, entry) == 0)
			pthread_detach(thread);
		else
		{
			entry->loading = 0;
			art.busy = 0;
			entry->state = MAP_ART_NONE;
			entry->failed_time = time(NULL);
		}
	}
	state = entry->state;
	if (state == MAP_ART_READY)
	{
		*pixels = entry->pixels;
		*width = entry->width;
		*height = entry->height;
		*serial = entry->serial;
	}
	pthread_mutex_unlock(&art_lock);
	return state;
}

#endif
