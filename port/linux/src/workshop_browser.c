/*
WORKSHOP_BROWSER.C

The Guide's WORKSHOP section's data (workshop_browser.h): the site's list,
asked for on a thread of its own, and the watcher of MCC's Workshop folders
in the player's Steam libraries. Nothing is downloaded here and nothing is
written to Steam's folders: Steam does both.
*/

#include "workshop_browser.h"
#include "steam_workshop.h"
#include "posix.h"
#ifdef HALO_GAME_BROWSER
#include "browser_http.h"
#include "build_identity.h"
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *config_string(const char *name);
int config_boolean(const char *name);
unsigned long system_milliseconds(void);
void platform_open_url(const char *url);
void platform_log(const char *format, ...);

#define STEAM_APP "976730"
#define APP_MANIFEST "steamapps/appmanifest_" STEAM_APP ".acf"
#define CONTENT_FOLDER "steamapps/workshop/content/" STEAM_APP
#define DOWNLOADS_FOLDER "steamapps/workshop/downloads/" STEAM_APP

enum
{
	MAXIMUM_LIBRARIES = 2 * STEAM_WORKSHOP_MAXIMUM_LIBRARIES,
	/* the libraries looked for again this often (Steam may add one) */
	LIBRARIES_INTERVAL = 15000,
	/* the item folders looked at this often while the section shows */
	WATCH_INTERVAL = 1000,
	/* a failed list asked for again no sooner than this */
	RETRY_INTERVAL = 30000,
};

static struct
{
	pthread_mutex_t lock;
	/* the thread's: asking, and its answer waiting to be taken */
	int fetching;
	int answered;
	int answer_count;
	struct workshop_browser_item answer[WORKSHOP_BROWSER_MAXIMUM_ITEMS];

	/* the main thread's */
	int status;
	int asked;
	unsigned long asked_time;
	int count;
	struct workshop_browser_item items[WORKSHOP_BROWSER_MAXIMUM_ITEMS];
	int libraries_looked;
	unsigned long libraries_time;
	int library_count;
	char libraries[MAXIMUM_LIBRARIES][STEAM_WORKSHOP_PATH_SIZE];
	/* the libraries holding MCC */
	int mcc_count;
	unsigned long watch_time;
} workshop = { PTHREAD_MUTEX_INITIALIZER };

/* ---------- files */

static int join(char *out, unsigned size, const char *folder, const char *name)
{
	int written = snprintf(out, size, "%s/%s", folder, name);

	return written > 0 && (unsigned)written < size;
}

static int is_directory(const char *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0 && (information.flags & _posix_file_is_directory);
}

static int is_file(const char *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0 && !(information.flags & _posix_file_is_directory);
}

/* the libraries again when they are old; MCC's ones first */
static void libraries_look(int force)
{
	unsigned long now = system_milliseconds();
	char (*found)[STEAM_WORKSHOP_PATH_SIZE];
	int count, index;

	if (workshop.libraries_looked && !force && now - workshop.libraries_time < LIBRARIES_INTERVAL)
		return;
	workshop.libraries_looked = 1;
	workshop.libraries_time = now;
	found = malloc(sizeof(*found) * MAXIMUM_LIBRARIES);
	if (!found)
		return;
	count = steam_workshop_libraries(found, MAXIMUM_LIBRARIES);
	workshop.library_count = 0;
	workshop.mcc_count = 0;
	/* (twice: the libraries with MCC, then the others, whose Workshop
	folders may still hold items from before MCC moved) */
	for (index = 0; index < count; index++)
	{
		char manifest[STEAM_WORKSHOP_PATH_SIZE];

		if (join(manifest, sizeof(manifest), found[index], APP_MANIFEST) && is_file(manifest))
		{
			snprintf(workshop.libraries[workshop.library_count++], STEAM_WORKSHOP_PATH_SIZE, "%s", found[index]);
			workshop.mcc_count++;
		}
	}
	for (index = 0; index < count; index++)
	{
		char manifest[STEAM_WORKSHOP_PATH_SIZE];

		if (!join(manifest, sizeof(manifest), found[index], APP_MANIFEST) || !is_file(manifest))
			snprintf(workshop.libraries[workshop.library_count++], STEAM_WORKSHOP_PATH_SIZE, "%s", found[index]);
	}
	free(found);
}

/* where an item's folders stand, in any library */
static int item_state(const char *id)
{
	int library, state = WORKSHOP_ITEM_NOT_INSTALLED;

	for (library = 0; library < workshop.library_count; library++)
	{
		char folder[STEAM_WORKSHOP_PATH_SIZE], item[STEAM_WORKSHOP_PATH_SIZE], found[256];

		if (join(folder, sizeof(folder), workshop.libraries[library], CONTENT_FOLDER) &&
			join(item, sizeof(item), folder, id) && is_directory(item))
		{
			/* (ModInfo.json is the last thing an item needs: Steam moves the
			whole folder in when it is done, so it is there or not) */
			if (posix_find_entry_case_insensitive(item, "ModInfo.json", found, sizeof(found)))
				return WORKSHOP_ITEM_INSTALLED;
			state = WORKSHOP_ITEM_INSTALLING;
		}
		if (join(folder, sizeof(folder), workshop.libraries[library], DOWNLOADS_FOLDER) &&
			join(item, sizeof(item), folder, id) && is_directory(item))
		{
			state = WORKSHOP_ITEM_INSTALLING;
		}
	}
	return state;
}

/* an installed Halo CE item that the list lacks: its title from its ModInfo.json */
static void local_item_add(const char *item_folder, const char *id)
{
	static struct steam_workshop_item_text text;
	struct workshop_browser_item *item;
	char path[STEAM_WORKSHOP_PATH_SIZE], found[256];
	char *document;
	FILE *file;
	long length;
	int halo1;

	if (workshop.count >= WORKSHOP_BROWSER_MAXIMUM_ITEMS ||
		!posix_find_entry_case_insensitive(item_folder, "ModInfo.json", found, sizeof(found)) ||
		!join(path, sizeof(path), item_folder, found) || !(file = fopen(path, "rb")))
	{
		return;
	}
	document = malloc(STEAM_WORKSHOP_MAXIMUM_JSON + 1);
	length = document ? (long)fread(document, 1, STEAM_WORKSHOP_MAXIMUM_JSON + 1, file) : 0;
	fclose(file);
	halo1 = length > 0 && length <= STEAM_WORKSHOP_MAXIMUM_JSON &&
		steam_workshop_read_item(document, (int)length, &text) && text.halo1;
	free(document);
	if (!halo1)
		return;
	item = &workshop.items[workshop.count++];
	memset(item, 0, sizeof(*item));
	snprintf(item->id, sizeof(item->id), "%s", id);
	snprintf(item->title, sizeof(item->title), "%s", text.title[0] ? text.title : id);
	snprintf(item->kind, sizeof(item->kind), "%s", text.has_campaign ? "campaign" : text.map_count > 1 ? "maps" :
		"map");
	item->size = -1;
	item->local = 1;
	item->state = WORKSHOP_ITEM_INSTALLED;
}

static int item_listed(const char *id)
{
	int index;

	for (index = 0; index < workshop.count; index++)
	{
		if (!strcmp(workshop.items[index].id, id))
			return 1;
	}
	return 0;
}

/* the installed items the list lacks, after its own */
static void local_items_look(void)
{
	int library;

	for (library = 0; library < workshop.library_count; library++)
	{
		char folder[STEAM_WORKSHOP_PATH_SIZE], item_folder[STEAM_WORKSHOP_PATH_SIZE], name[256];
		void *directory;

		if (!join(folder, sizeof(folder), workshop.libraries[library], CONTENT_FOLDER) ||
			!(directory = posix_directory_open(folder)))
		{
			continue;
		}
		while (workshop.count < WORKSHOP_BROWSER_MAXIMUM_ITEMS && posix_directory_next(directory, name, sizeof(name)))
		{
			if (workshop_browser_id_valid(name) && !item_listed(name) &&
				join(item_folder, sizeof(item_folder), folder, name) && is_directory(item_folder))
			{
				local_item_add(item_folder, name);
			}
		}
		posix_directory_close(directory);
	}
}

/* ---------- the list */

#ifdef HALO_GAME_BROWSER
static void *fetch_thread(void *context)
{
	char *url = context, *response = malloc(WORKSHOP_BROWSER_MAXIMUM_DOCUMENT + 1);
	static struct workshop_browser_item items[WORKSHOP_BROWSER_MAXIMUM_ITEMS];
	char error[128];
	int status = 0, count = -1;

	error[0] = 0;
	if (response)
	{
		status = posix_browser_request_as(url, NULL, NULL, build_identity_user_agent(), response,
			WORKSHOP_BROWSER_MAXIMUM_DOCUMENT + 1, error, sizeof(error));
		if (status == 200)
			count = workshop_browser_read_list(response, (int)strlen(response), items, WORKSHOP_BROWSER_MAXIMUM_ITEMS);
	}
	if (count < 0)
		platform_log("Workshop: the list could not be read (HTTP %d%s%s)", status, error[0] ? ": " : "", error);
	else
		platform_log("Workshop: %d items listed", count);
	pthread_mutex_lock(&workshop.lock);
	workshop.answer_count = count;
	if (count > 0)
		memcpy(workshop.answer, items, sizeof(items[0]) * (size_t)count);
	workshop.answered = 1;
	workshop.fetching = 0;
	pthread_mutex_unlock(&workshop.lock);
	free(response);
	free(url);
	return NULL;
}
#endif

static void list_ask(void)
{
	const char *setting = config_string("network.workshop_url");

	workshop.asked = 1;
	workshop.asked_time = system_milliseconds();
#ifdef HALO_GAME_BROWSER
	if (setting && (!strncmp(setting, "https://", 8) || !strncmp(setting, "http://", 7)))
	{
		pthread_t thread;
		char *url = malloc(strlen(setting) + 1);

		pthread_mutex_lock(&workshop.lock);
		if (workshop.fetching || !url)
		{
			pthread_mutex_unlock(&workshop.lock);
			free(url);
			return;
		}
		strcpy(url, setting);
		workshop.fetching = 1;
		pthread_mutex_unlock(&workshop.lock);
		workshop.status = WORKSHOP_LIST_LOADING;
		if (pthread_create(&thread, NULL, fetch_thread, url) == 0)
		{
			pthread_detach(thread);
			return;
		}
		pthread_mutex_lock(&workshop.lock);
		workshop.fetching = 0;
		pthread_mutex_unlock(&workshop.lock);
		free(url);
		workshop.status = WORKSHOP_LIST_FAILED;
		return;
	}
#else
	(void)setting;
#endif
	workshop.status = WORKSHOP_LIST_NOT_SET_UP;
}

/* the thread's answer, kept with what this run knows of each item */
static void list_take(void)
{
	static struct workshop_browser_item before[WORKSHOP_BROWSER_MAXIMUM_ITEMS];
	int before_count = workshop.count, index, other;

	pthread_mutex_lock(&workshop.lock);
	if (!workshop.answered)
	{
		pthread_mutex_unlock(&workshop.lock);
		return;
	}
	workshop.answered = 0;
	memcpy(before, workshop.items, sizeof(before[0]) * (size_t)before_count);
	if (workshop.answer_count >= 0)
	{
		workshop.count = workshop.answer_count;
		memcpy(workshop.items, workshop.answer, sizeof(workshop.items[0]) * (size_t)workshop.count);
		workshop.status = WORKSHOP_LIST_LOADED;
	}
	else
		workshop.status = WORKSHOP_LIST_FAILED;
	pthread_mutex_unlock(&workshop.lock);
	if (workshop.status != WORKSHOP_LIST_LOADED)
		return;
	for (index = 0; index < workshop.count; index++)
	{
		for (other = 0; other < before_count && strcmp(before[other].id, workshop.items[index].id); other++)
			;
		workshop.items[index].subscribed = other < before_count && before[other].subscribed;
		workshop.items[index].state = item_state(workshop.items[index].id);
	}
	local_items_look();
}

/* ---------- public code */

int workshop_browser_mcc_present(void)
{
#if defined(HALO_ANDROID) || defined(HALO_WEB)
	return 0;
#else
	libraries_look(0);
	return workshop.mcc_count > 0;
#endif
}

void workshop_browser_open(int force)
{
	int index;

	libraries_look(1);
	for (index = 0; index < workshop.count; index++)
		workshop.items[index].state = item_state(workshop.items[index].id);
	local_items_look();
	workshop.watch_time = system_milliseconds();
	if (force || !workshop.asked ||
		(workshop.status == WORKSHOP_LIST_FAILED && system_milliseconds() - workshop.asked_time >= RETRY_INTERVAL))
	{
		list_ask();
	}
}

int workshop_browser_poll(void)
{
	unsigned long now = system_milliseconds();
	int index, installed = 0;

	list_take();
	if (now - workshop.watch_time < WATCH_INTERVAL)
		return 0;
	workshop.watch_time = now;
	libraries_look(0);
	for (index = 0; index < workshop.count; index++)
	{
		struct workshop_browser_item *item = &workshop.items[index];
		int state = item_state(item->id);

		if (state == WORKSHOP_ITEM_NOT_INSTALLED && item->subscribed)
			state = WORKSHOP_ITEM_WAITING;
		if (state == WORKSHOP_ITEM_INSTALLED && item->state != WORKSHOP_ITEM_INSTALLED)
		{
			platform_log("Workshop: item %s installed", item->id);
			installed++;
		}
		item->state = state;
	}
	/* (an item subscribed in Steam itself shows up too) */
	index = workshop.count;
	local_items_look();
	installed += workshop.count - index;
	return installed;
}

int workshop_browser_status(void)
{
	return workshop.status;
}

int workshop_browser_count(void)
{
	return workshop.count;
}

const struct workshop_browser_item *workshop_browser_item(int index)
{
	return index >= 0 && index < workshop.count ? &workshop.items[index] : NULL;
}

int workshop_browser_subscribe(int index)
{
	char url[96];

	if (index < 0 || index >= workshop.count || !workshop_browser_steam_url(workshop.items[index].id, url, sizeof(url)))
		return 0;
	/* (debug.workshop_no_steam: the page not opened, for tests) */
	if (!config_boolean("debug.workshop_no_steam"))
		platform_open_url(url);
	workshop.items[index].subscribed = 1;
	if (workshop.items[index].state == WORKSHOP_ITEM_NOT_INSTALLED)
		workshop.items[index].state = WORKSHOP_ITEM_WAITING;
	return 1;
}
