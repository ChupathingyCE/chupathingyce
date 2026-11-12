/*
WORKSHOP_BROWSER.C

The Guide's WORKSHOP section's data (workshop_browser.h): the site's list,
asked for on a thread of its own, and the watcher of MCC's Workshop folders
in the player's Steam libraries. Nothing is downloaded here and nothing is
written to Steam's folders: Steam does both, asked through its page or,
with game.workshop_steam on, through the Steam bridge
(steam_client_bridge.h), which runs only while the section shows.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/
#ifndef HALO_MCC
#include "../include/halo_port_features.h"
#endif
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h, above): a console builds this to nothing */

#include "retry_backoff.h"
#include "workshop_browser.h"
#include "steam_workshop.h"
#include "steam_client_bridge.h"
#include "posix.h"
#include "ui_font.h"
#include "mcc_preview_image.h"
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
void platform_clipboard_set(const char *text);
int config_write_boolean(const char *name, int value);
void platform_log(const char *format, ...);
const char *platform_save_root(void);
const char *platform_data_root(void);

#define STEAM_APP "976730"
#define APP_MANIFEST "steamapps/appmanifest_" STEAM_APP ".acf"
#define CONTENT_FOLDER "steamapps/workshop/content/" STEAM_APP
#define DOWNLOADS_FOLDER "steamapps/workshop/downloads/" STEAM_APP
/* MCC's Halo CE files (from Steam, or steamcmd's depots: steam_bridge_cmd.c) */
#define MCC_HALO1_FOLDER "steamapps/common/Halo The Master Chief Collection/halo1"

enum
{
	MAXIMUM_LIBRARIES = 2 * STEAM_WORKSHOP_MAXIMUM_LIBRARIES,
	/* the libraries looked for again this often (Steam may add one) */
	LIBRARIES_INTERVAL = 15000,
	/* the item folders looked at this often while the section shows */
	WATCH_INTERVAL = 1000,
	/* a failed list asked for again no sooner than this */
	RETRY_INTERVAL = 30000,
	/* Steam's details: ids asked at once, and remembered this run */
	DETAILS_BATCH = 16,
	DETAILS_CACHE = 160,
	DETAILS_DOCUMENT = 1024 * 1024,
	/* the browse page: its largest, how long its list is kept, and how long
	Steam's "too many requests" (429) holds it off */
	BROWSE_PAGE_DOCUMENT = 2 * 1024 * 1024,
	BROWSE_PAGE_ITEMS = 32,
	BROWSE_KEEP = 10 * 60 * 1000,
	/* Steam's pages kept (each BROWSE_KEEP) */
	PAGE_CACHE = 6,
	BROWSE_BACKOFF = 10 * 60 * 1000,
};

/* Steam's public Workshop browse page: MCC's Halo CE items (tag Halo1),
trending first; only the item ids are taken from it (the details call
gives everything shown) */
#define BROWSE_PAGE_URL "https://steamcommunity.com/workshop/browse/?appid=976730&requiredtags%5B%5D=Halo1" \
	"&section=readytouseitems"
#define DETAILS_URL "https://api.steampowered.com/ISteamRemoteStorage/GetPublishedFileDetails/v1/"

/* an item's details from Steam's public, keyless GetPublishedFileDetails:
the item page's text where nothing else gives it (macOS, whose Steam won't
answer the client's queries; items found only on disk). This run's only */
struct details_entry
{
	char id[WORKSHOP_BROWSER_ID_SIZE];
	/* asked; answered (filled, or not a Halo CE item: nothing to show) */
	int answered;
	int filled;
	struct workshop_browser_item item;
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
	/* what the list shows: WORKSHOP_FILTER_*, WORKSHOP_SORT_*, the search's
	words; the page's address for them, and the one last fetched */
	int filter;
	int sort;
	char search[64];
	char web_url[512];
	char web_fetched[512];
	/* the list as shown: indices into items, -1 the find row */
	int view[WORKSHOP_BROWSER_MAXIMUM_ITEMS + 3];
	int view_count;
	struct workshop_browser_item find_row;
	/* what blocks GET (no Steam link), first when it does */
	struct workshop_browser_item link_row;
	/* Steam's browse page: which (1 the first), the list's end row, and the
	pages seen in the last ten minutes, by their address */
	int page;
	struct workshop_browser_item page_row;
	struct
	{
		char url[512];
		unsigned long time;
		int count;
		struct workshop_browser_item items[BROWSE_PAGE_ITEMS];
	} pages[PAGE_CACHE];
	int page_next_slot;
	/* the browse page: when its list came (0 never); asked again after a
	failure only as retry_backoff.h says (Steam's 429 and Retry-After
	honored), the thread's last status and Retry-After */
	unsigned long web_time;
	struct retry_backoff web_retry;
	int web_status;
	long web_retry_after;
	int web_answer;
	/* Steam's details the same way */
	struct retry_backoff details_retry;
	int details_status;
	long details_retry_after;
	/* Steam's details: the thread's, then the cache (the main thread's) */
	int details_fetching;
	int details_answered;
	int details_answer_count;
	char details_asked[DETAILS_BATCH][WORKSHOP_BROWSER_ID_SIZE];
	int details_asked_count;
	struct workshop_browser_item details_answer[DETAILS_BATCH];
	int details_count;
	struct details_entry details[DETAILS_CACHE];
	/* the section shows (workshop_browser_open until _close) */
	int tab_open;
	/* GET on the Halo CE files row: asked of steamcmd once the bridge is up */
	int install_pending;
	/* the libraries holding MCC: from the Steam client (its app manifest), or its Halo CE files at all */
	int client_mcc_count;
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
	workshop.client_mcc_count = 0;
	workshop.mcc_count = 0;
	/* (twice: the libraries with MCC, then the others, whose Workshop
	folders may still hold items from before MCC moved) */
	for (index = 0; index < count; index++)
	{
		char manifest[STEAM_WORKSHOP_PATH_SIZE], halo1[STEAM_WORKSHOP_PATH_SIZE];
		int client = join(manifest, sizeof(manifest), found[index], APP_MANIFEST) && is_file(manifest);

		if (client || (join(halo1, sizeof(halo1), found[index], MCC_HALO1_FOLDER) && is_directory(halo1)))
		{
			snprintf(workshop.libraries[workshop.library_count++], STEAM_WORKSHOP_PATH_SIZE, "%s", found[index]);
			workshop.client_mcc_count += client;
			workshop.mcc_count++;
			found[index][0] = 0;
		}
	}
	for (index = 0; index < count; index++)
	{
		if (found[index][0])
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
	snprintf(item->kind, sizeof(item->kind), "%s", text.has_campaign ? "campaign" : "multiplayer");
	item->tags = (text.has_campaign ? WORKSHOP_TAG_CAMPAIGN : 0) | (text.map_count > 0 ? WORKSHOP_TAG_MULTIPLAYER : 0);
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

/* ---------- the Steam bridge */

#define CLIENT_CONNECTED (1 << STEAM_BRIDGE_BACKEND_CLIENT)

/* the bridge offered here: the Steam client (Windows, and Linux with MCC
from Steam, under Proton: Steam downloads its items itself); steamcmd as
the downloader where the client won't (macOS: Steam.app subscribes to MCC
items but never downloads them; Linux without MCC from Steam), with the
client still tried for the list and the subscriptions */
static int bridge_backend(void)
{
#if defined(HALO_ANDROID) || defined(HALO_WEB)
	return STEAM_BRIDGE_BACKEND_NONE;
#else
	int backend = steam_bridge_backend();

	/* (MCC from the Steam client: the client subscribes, unless the game's
	own Steam download is here, which installs on every platform) */
	if (backend == STEAM_BRIDGE_BACKEND_STEAMCMD && workshop.client_mcc_count > 0 &&
		(!steam_link_available() || config_boolean("debug.workshop_steamcmd")))
		return STEAM_BRIDGE_BACKEND_CLIENT;
	return backend;
#endif
}

static int bridge_uses_steamcmd(void)
{
	return steam_bridge_backend() == STEAM_BRIDGE_BACKEND_STEAMCMD && bridge_backend() == STEAM_BRIDGE_BACKEND_STEAMCMD;
}

static int bridge_ready(void)
{
	return steam_bridge_enabled() && bridge_backend() != STEAM_BRIDGE_BACKEND_NONE &&
		steam_bridge_status() == STEAM_BRIDGE_READY;
}

/* ---------- Steam's link (the built-in downloader) */

/* the link row shows: the game's own Steam download is the downloader here */
static int link_offered(void)
{
	return steam_link_available() && !config_boolean("debug.workshop_steamcmd") &&
		bridge_backend() == STEAM_BRIDGE_BACKEND_STEAMCMD;
}

static void link_row_make(void)
{
	struct workshop_browser_item *item = &workshop.link_row;

	memset(item, 0, sizeof(*item));
	snprintf(item->id, sizeof(item->id), "link");
	snprintf(item->title, sizeof(item->title), "Link Steam");
	snprintf(item->kind, sizeof(item->kind), "Steam");
	snprintf(item->description, sizeof(item->description), "Downloads need your Steam account.\nLINK goes to "
		"Settings: scan the code there with the Steam app.\nThe game never sees your password.");
	item->size = -1;
	item->setup = WORKSHOP_ROW_LINK;
	item->step = WORKSHOP_STEP_TO_DO;
}

/* the list's end: Steam's page, and the way to the next */
static int page_row_offered(void)
{
	const char *setting = config_string("network.workshop_url");

	return workshop.filter != WORKSHOP_FILTER_INSTALLED && (!setting || !setting[0]) &&
		workshop.status == WORKSHOP_LIST_LOADED && (workshop.page > 1 || workshop.count >= 20);
}

static void page_row_make(void)
{
	struct workshop_browser_item *item = &workshop.page_row;

	memset(item, 0, sizeof(*item));
	snprintf(item->id, sizeof(item->id), "page");
	snprintf(item->title, sizeof(item->title), "More items");
	snprintf(item->kind, sizeof(item->kind), "Page %d", workshop.page > 0 ? workshop.page : 1);
	snprintf(item->description, sizeof(item->description), "Steam's list, a page at a time. NEXT PAGE shows the "
		"next; the pages seen are kept for ten minutes.");
	item->size = -1;
	item->setup = WORKSHOP_ROW_PAGE;
	item->step = -1;
}

int workshop_browser_page(void)
{
	return workshop.page > 0 ? workshop.page : 1;
}

int workshop_browser_page_next(void);
int workshop_browser_page_previous(void);

int workshop_browser_link_offered(void)
{
	return link_offered();
}

/* ---------- the setup rows */

#define STEAMCMD_BREW "brew install --cask steamcmd"
#define STEAMCMD_PAGE "https://developer.valvesoftware.com/wiki/SteamCMD"
#define MCC_STORE "https://store.steampowered.com/app/976730"

static int link_offered(void);

/* steamcmd (or the built-in Steam download) does the installing here (macOS; Linux without MCC from Steam; Windows without MCC's steam_api) */
static int setup_steamcmd(void)
{
	const char *debug = config_string("debug.workshop_setup");

	/* (debug.workshop_setup with "client": the Steam client's rows, for screenshots) */
	if (debug && strstr(debug, "client"))
		return 0;
	/* (the built-in Steam download, where it is the downloader: every platform) */
	if (link_offered())
		return 1;
#if defined(_WIN32)
	return 0;
#elif defined(__APPLE__)
	return 1;
#else
	return !workshop.client_mcc_count && !steam_bridge_steam_present();
#endif
}

/* (debug.workshop_setup: the rows as named, "row=step,...", for screenshots) */
static int setup_forced(int row, int step)
{
	static const char *const names[] = { "", "steam", "signin", "mcc", "files" };
	const char *setting = config_string("debug.workshop_setup"), *at;
	size_t length = strlen(names[row]);

	if (!setting || !setting[0])
		return step;
	for (at = setting; (at = strstr(at, names[row])) != NULL; at += length)
	{
		if (at[length] == '=' && (at == setting || at[-1] == ','))
		{
			int forced = at[length + 1] - '0';

			return forced >= WORKSHOP_STEP_READY && forced <= WORKSHOP_STEP_NOT_AVAILABLE ? forced : step;
		}
	}
	return step;
}

static void setup_rows_remove(void)
{
	int index = 0;

	while (index < workshop.count)
	{
		if (workshop.items[index].setup)
		{
			memmove(&workshop.items[index], &workshop.items[index + 1],
				sizeof(workshop.items[0]) * (size_t)(workshop.count - index - 1));
			workshop.count--;
		}
		else
			index++;
	}
}

static void setup_row(struct workshop_browser_item *item, int row, int step, const char *title, const char *steps)
{
	memset(item, 0, sizeof(*item));
	snprintf(item->id, sizeof(item->id), "setup%d", row);
	snprintf(item->title, sizeof(item->title), "%s", title);
	snprintf(item->kind, sizeof(item->kind), "setup");
	snprintf(item->description, sizeof(item->description), "%s", steps);
	item->size = -1;
	item->setup = row;
	item->step = setup_forced(row, step);
}

/* an installed Halo CE item (the account owns MCC: Steam fetched it) */
static int installed_any(void)
{
	int index;

	for (index = 0; index < workshop.count; index++)
	{
		if (!workshop.items[index].setup && workshop.items[index].state == WORKSHOP_ITEM_INSTALLED)
			return 1;
	}
	return 0;
}

/* the setup rows, at the top, while one of them isn't Ready */
static void setup_rows_update(void)
{
	struct workshop_browser_item rows[4];
	int count = 0, ready = 1, index, steamcmd = setup_steamcmd();
	int mcc_state = steam_bridge_mcc_state();
	int files = workshop.mcc_count > 0, steam;

	setup_rows_remove();
	if (steamcmd && link_offered())
	{
		/* (the built-in download: no setup rows in the list; Steam's link,
		what blocks GET, is the link row's, and lives in SETTINGS) */
		(void)files;
		(void)mcc_state;
		return;
	}
	else if (steamcmd)
	{
#ifdef __APPLE__
		steam = steam_bridge_backend() == STEAM_BRIDGE_BACKEND_STEAMCMD;
		setup_row(&rows[count++], WORKSHOP_SETUP_STEAM, steam ? WORKSHOP_STEP_READY : WORKSHOP_STEP_TO_DO, "steamcmd",
			"Open Terminal.\nRun: " STEAMCMD_BREW "\nCome back here.");
#else
		steam = steam_bridge_backend() == STEAM_BRIDGE_BACKEND_STEAMCMD;
		setup_row(&rows[count++], WORKSHOP_SETUP_STEAM, steam ? WORKSHOP_STEP_READY : WORKSHOP_STEP_TO_DO, "steamcmd",
			"Install steamcmd from your distribution, or from Valve's page.\nCome back here.");
#endif
		setup_row(&rows[count++], WORKSHOP_SETUP_SIGN_IN, !steam ? WORKSHOP_STEP_NOT_AVAILABLE :
			steamcmd_signed_in() ? WORKSHOP_STEP_READY : WORKSHOP_STEP_TO_DO, "Sign in",
			"Press SIGN IN.\nType your Steam name, password and code in the window.\nCome back here.\n"
			"The game never sees your password.");
		/* (owning MCC: shown by its files, or by an item steamcmd could fetch for it) */
		setup_row(&rows[count++], WORKSHOP_SETUP_MCC, files || installed_any() ? WORKSHOP_STEP_READY :
			WORKSHOP_STEP_TO_DO, "MCC", "Own Halo: The Master Chief Collection on Steam.");
		/* (MCC's shared Halo CE files: most items carry their own; only when one needed them) */
		if (steam_workshop_shared_missing() || mcc_state == STEAM_BRIDGE_ITEM_DOWNLOADING ||
			mcc_state == STEAM_BRIDGE_ITEM_QUEUED || workshop.install_pending || setup_forced(WORKSHOP_SETUP_FILES, -1) >= 0)
		{
			setup_row(&rows[count++], WORKSHOP_SETUP_FILES, files && mcc_state != STEAM_BRIDGE_ITEM_DOWNLOADING ?
				WORKSHOP_STEP_READY : mcc_state == STEAM_BRIDGE_ITEM_DOWNLOADING || mcc_state == STEAM_BRIDGE_ITEM_QUEUED ||
				workshop.install_pending ? WORKSHOP_STEP_WORKING : !steam ? WORKSHOP_STEP_NOT_AVAILABLE :
				WORKSHOP_STEP_TO_DO, "Halo CE files", "A map needs MCC's Halo CE files.\nPress GET. About 4.3 GB.");
		}
	}
	else
	{
		steam = steam_bridge_steam_present() && !steam_bridge_offline();
		setup_row(&rows[count++], WORKSHOP_SETUP_STEAM, steam ? WORKSHOP_STEP_READY : WORKSHOP_STEP_TO_DO,
			steam_bridge_offline() ? "Steam offline" : "Steam", steam_bridge_offline() ?
			"Open Steam and sign in.\nCome back here." : "Install Steam and sign in.\nCome back here.");
		setup_row(&rows[count++], WORKSHOP_SETUP_MCC, workshop.client_mcc_count ? WORKSHOP_STEP_READY :
			WORKSHOP_STEP_TO_DO, "MCC", "Get Halo: The Master Chief Collection on Steam.");
		if (steam_workshop_shared_missing() || setup_forced(WORKSHOP_SETUP_FILES, -1) >= 0)
			setup_row(&rows[count++], WORKSHOP_SETUP_FILES, files ? WORKSHOP_STEP_READY : WORKSHOP_STEP_TO_DO,
				"Halo CE files", "A map needs MCC's Halo CE files.\nIn Steam: MCC > Properties > DLC.\n"
				"Tick Halo: Combat Evolved Anniversary.");
	}
	for (index = 0; index < count; index++)
		ready &= rows[index].step == WORKSHOP_STEP_READY;
	if (ready || workshop.count + count > WORKSHOP_BROWSER_MAXIMUM_ITEMS)
		return;
	memmove(&workshop.items[count], &workshop.items[0], sizeof(workshop.items[0]) * (size_t)workshop.count);
	memcpy(&workshop.items[0], rows, sizeof(rows[0]) * (size_t)count);
	workshop.count += count;
}

const char *workshop_browser_setup_button(int index)
{
	const struct workshop_browser_item *item = workshop_browser_item(index);

	if (!item || !item->setup || item->setup == WORKSHOP_ROW_FIND || item->setup == WORKSHOP_ROW_LINK ||
		item->step == WORKSHOP_STEP_READY ||
		item->step == WORKSHOP_STEP_WORKING)
	{
		return NULL;
	}
	switch (item->setup)
	{
	case WORKSHOP_SETUP_STEAM:
#ifdef __APPLE__
		return setup_steamcmd() ? "COPY" : "OPEN STEAM";
#else
		return setup_steamcmd() ? "OPEN" : "OPEN STEAM";
#endif
	case WORKSHOP_SETUP_SIGN_IN: return item->step == WORKSHOP_STEP_TO_DO ? "SIGN IN" : NULL;
	case WORKSHOP_SETUP_MCC: return "STORE";
	default: return setup_steamcmd() ? (item->step == WORKSHOP_STEP_TO_DO ? "GET" : NULL) : "OPEN STEAM";
	}
}

int workshop_browser_setup_press(int index)
{
	const struct workshop_browser_item *item = workshop_browser_item(index);

	if (!workshop_browser_setup_button(index))
		return 0;
	switch (item->setup)
	{
	case WORKSHOP_SETUP_STEAM:
#ifdef __APPLE__
		if (setup_steamcmd())
		{
			platform_clipboard_set(STEAMCMD_BREW);
			return 1;
		}
#endif
		platform_open_url(setup_steamcmd() ? STEAMCMD_PAGE : "steam://open/main");
		return 1;
	case WORKSHOP_SETUP_SIGN_IN:
		return steam_bridge_sign_in();
	case WORKSHOP_SETUP_MCC:
		platform_open_url(MCC_STORE);
		return 1;
	default:
		if (!setup_steamcmd())
		{
			platform_open_url("steam://nav/games/details/976730");
			return 1;
		}
		/* (GET is the player's yes to steamcmd: the bridge on, MCC's files asked once it's up) */
		if (!steam_bridge_enabled())
			config_write_boolean("game.workshop_steam", 1);
		steam_bridge_open(1);
		workshop.install_pending = 1;
		return 1;
	}
}

static const char *failed_why(const char *id);

/* an item's state with the bridge's word on it: the folders win for
Installed (the maps come from them), the bridge tells the rest */
static int bridge_state(struct workshop_browser_item *item, int folder_state)
{
	struct steam_bridge_item known;

	item->steam_subscribed = 0;
	item->downloaded = item->total = 0;
	item->steam_error[0] = 0;
	/* (a GET or UPDATE the game's own download refused: its word stays
	until asked again) */
	if (failed_why(item->id))
	{
		snprintf(item->steam_error, sizeof(item->steam_error), "%s", failed_why(item->id));
		if (folder_state != WORKSHOP_ITEM_INSTALLED)
			item->subscribed = 0;
		return folder_state;
	}
	if (!bridge_ready() || !steam_bridge_item(item->id, &known))
		return folder_state;
	item->steam_subscribed = known.subscribed;
	if (known.state == STEAM_BRIDGE_ITEM_FAILED)
		snprintf(item->steam_error, sizeof(item->steam_error), "%s", known.error);
	switch (known.state)
	{
	case STEAM_BRIDGE_ITEM_DOWNLOADING:
		item->downloaded = known.downloaded;
		item->total = known.total;
		return folder_state == WORKSHOP_ITEM_INSTALLED ? folder_state : WORKSHOP_ITEM_DOWNLOADING;
	case STEAM_BRIDGE_ITEM_QUEUED:
		return folder_state == WORKSHOP_ITEM_NOT_INSTALLED ? WORKSHOP_ITEM_WAITING : folder_state;
	case STEAM_BRIDGE_ITEM_INSTALLED:
		/* (Steam's own word comes from appworkshop_976730.acf, which keeps
		items whose files have gone: the folders decide) */
		return folder_state;
	default:
		return folder_state;
	}
}

/* ---------- Steam's details */

#ifdef HALO_GAME_BROWSER
static void *details_thread(void *context)
{
	char *body = context, *response = malloc(DETAILS_DOCUMENT + 1);
	static struct workshop_browser_item found[DETAILS_BATCH];
	char error[128];
	int status = 0, count = -1;

	error[0] = 0;
	if (response)
	{
		status = posix_browser_request_as(DETAILS_URL, body, "application/x-www-form-urlencoded",
			build_identity_user_agent(), response, DETAILS_DOCUMENT + 1, error, sizeof(error));
		if (status == 200)
			count = workshop_browser_read_details(response, (int)strlen(response), found, DETAILS_BATCH);
	}
	if (count < 0)
		platform_log("Workshop: Steam's item details could not be read (HTTP %d)", status);
	pthread_mutex_lock(&workshop.lock);
	workshop.details_status = status;
	workshop.details_retry_after = retry_backoff_after(error);
	workshop.details_answer_count = count;
	if (count > 0)
		memcpy(workshop.details_answer, found, sizeof(found[0]) * (size_t)count);
	workshop.details_answered = 1;
	workshop.details_fetching = 0;
	pthread_mutex_unlock(&workshop.lock);
	free(response);
	free(body);
	return NULL;
}
#endif

static struct details_entry *details_find(const char *id)
{
	int index;

	for (index = 0; index < workshop.details_count; index++)
	{
		if (!strcmp(workshop.details[index].id, id))
			return &workshop.details[index];
	}
	return NULL;
}

/* what Steam said of an item, put on its page where the page lacks it */
static void details_apply(struct workshop_browser_item *item)
{
	struct details_entry *entry = details_find(item->id);

	if (!entry || !entry->filled)
		return;
	if (!item->description[0])
		snprintf(item->description, sizeof(item->description), "%s", entry->item.description);
	if (item->size < 0)
		item->size = entry->item.size;
	if (!item->updated)
		item->updated = entry->item.updated;
	if (item->local || !strcmp(item->kind, "item"))
		snprintf(item->kind, sizeof(item->kind), "%s", entry->item.kind);
	if (item->local && entry->item.title[0])
		snprintf(item->title, sizeof(item->title), "%s", entry->item.title);
	if (!item->preview_url[0])
		snprintf(item->preview_url, sizeof(item->preview_url), "%s", entry->item.preview_url);
	if (!item->tags)
		item->tags = entry->item.tags;
}

/* ---------- pictures */

enum
{
	PREVIEW_FILE_LARGEST = 8 * 1024 * 1024,
	/* a download's whole time */
	PREVIEW_DEADLINE = 20000,
};

static struct
{
	/* the thread's: asked for, and its answer */
	int busy;
	int done;
	char id[WORKSHOP_BROWSER_ID_SIZE];
	unsigned char *pixels;
	int width, height;
	/* the main thread's: what shows, and what was tried */
	char shown_id[WORKSHOP_BROWSER_ID_SIZE];
	unsigned char *shown;
	int shown_width, shown_height, serial;
	char tried[WORKSHOP_BROWSER_MAXIMUM_ITEMS][WORKSHOP_BROWSER_ID_SIZE];
	int tried_count;
} preview;

struct preview_job
{
	char id[WORKSHOP_BROWSER_ID_SIZE];
	/* (no address known: Steam's details asked for it first) */
	int ask_details;
	char url[200];
	char local[STEAM_WORKSHOP_PATH_SIZE];
	char cache[STEAM_WORKSHOP_PATH_SIZE];
};

static unsigned char *file_read(const char *path, int *size)
{
	FILE *file = fopen(path, "rb");
	unsigned char *data;
	long length;

	*size = 0;
	if (!file)
		return NULL;
	data = malloc(PREVIEW_FILE_LARGEST + 1);
	length = data ? (long)fread(data, 1, PREVIEW_FILE_LARGEST + 1, file) : 0;
	fclose(file);
	if (length <= 0 || length > PREVIEW_FILE_LARGEST)
	{
		free(data);
		return NULL;
	}
	*size = (int)length;
	return data;
}

/* (a picture's first bytes: PNG or JPEG, nothing else is decoded) */
static int picture_bytes(const unsigned char *data, int size)
{
	return size > 8 && ((data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') ||
		(data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF));
}

#ifdef HALO_GAME_BROWSER
static unsigned char *download(const char *url, int *size)
{
	unsigned char *data;
	int status = 0, length = -1, handle, got;
	char error[128];

	*size = 0;
	handle = posix_browser_download_open(url, build_identity_user_agent(), &status, &length, error, sizeof(error));
	if (!handle)
		return NULL;
	posix_browser_download_deadline(handle, PREVIEW_DEADLINE);
	data = length <= PREVIEW_FILE_LARGEST ? malloc(PREVIEW_FILE_LARGEST + 1) : NULL;
	while (data && (got = posix_browser_download_read(handle, data + *size, PREVIEW_FILE_LARGEST + 1 - *size)) > 0)
	{
		*size += got;
		if (*size > PREVIEW_FILE_LARGEST)
			break;
	}
	posix_browser_download_close(handle);
	if (!data || *size <= 0 || *size > PREVIEW_FILE_LARGEST || !picture_bytes(data, *size))
	{
		free(data);
		*size = 0;
		return NULL;
	}
	return data;
}
#endif

/* RGBA pixels made at most WORKSHOP_BROWSER_PREVIEW_WIDTH wide (box filter) */
static unsigned char *shrink(const unsigned char *pixels, int width, int height, int *out_width, int *out_height)
{
	int step = (width + WORKSHOP_BROWSER_PREVIEW_WIDTH - 1) / WORKSHOP_BROWSER_PREVIEW_WIDTH, x, y;
	unsigned char *out;

	if (step < 1)
		step = 1;
	*out_width = width / step;
	*out_height = height / step;
	if (*out_width < 1 || *out_height < 1 || !(out = malloc((size_t)*out_width * (size_t)*out_height * 4)))
		return NULL;
	for (y = 0; y < *out_height; y++)
	{
		for (x = 0; x < *out_width; x++)
		{
			unsigned sum[4] = { 0, 0, 0, 0 };
			int dx, dy, channel;

			for (dy = 0; dy < step; dy++)
			{
				const unsigned char *row = pixels + ((size_t)(y * step + dy) * (size_t)width + (size_t)(x * step)) * 4;

				for (dx = 0; dx < step * 4; dx++)
					sum[dx & 3] += row[dx];
			}
			for (channel = 0; channel < 4; channel++)
				out[((size_t)y * (size_t)*out_width + (size_t)x) * 4 + (size_t)channel] =
					(unsigned char)(sum[channel] / (unsigned)(step * step));
		}
	}
	return out;
}

static void *preview_thread(void *context)
{
	struct preview_job *job = context;
	unsigned char *data = NULL, *pixels = NULL, *small = NULL;
	int size = 0, width = 0, height = 0, small_width = 0, small_height = 0;

	/* (the item's own picture first, then the cache, then Steam's) */
	if (job->local[0])
		data = file_read(job->local, &size);
	if (!data && job->cache[0])
		data = file_read(job->cache, &size);
#ifdef HALO_GAME_BROWSER
	/* (an item the list hasn't shown: its picture's address from Steam's details) */
	if (!data && !job->url[0] && job->ask_details)
	{
		static struct workshop_browser_item found;
		char *answer = malloc(DETAILS_DOCUMENT + 1), form[96], error[128];

		snprintf(form, sizeof(form), "itemcount=1&publishedfileids%%5B0%%5D=%s", job->id);
		if (answer && posix_browser_request_as(DETAILS_URL, form, "application/x-www-form-urlencoded",
			build_identity_user_agent(), answer, DETAILS_DOCUMENT + 1, error, sizeof(error)) == 200 &&
			workshop_browser_read_details(answer, (int)strlen(answer), &found, 1) == 1 &&
			!strncmp(found.preview_url, "https://", 8))
		{
			snprintf(job->url, sizeof(job->url), "%s", found.preview_url);
		}
		free(answer);
	}
	if (!data && job->url[0] && (data = download(job->url, &size)) != NULL && job->cache[0])
	{
		FILE *file = fopen(job->cache, "wb");

		if (file)
		{
			fwrite(data, 1, (size_t)size, file);
			fclose(file);
		}
	}
#endif
	if (data && picture_bytes(data, size))
		pixels = posix_ui_image_decode(data, size, &width, &height);
	free(data);
	if (pixels)
	{
		small = shrink(pixels, width, height, &small_width, &small_height);
		posix_ui_image_free(pixels);
	}
	pthread_mutex_lock(&workshop.lock);
	snprintf(preview.id, sizeof(preview.id), "%s", job->id);
	preview.pixels = small;
	preview.width = small_width;
	preview.height = small_height;
	preview.done = 1;
	preview.busy = 0;
	pthread_mutex_unlock(&workshop.lock);
	free(job);
	return NULL;
}

/* an installed item's own picture: its first map's thumbnail */
static int local_preview(const char *id, char *path, int size)
{
	int index, count = steam_workshop_count();

	for (index = 0; index < count; index++)
	{
		const struct steam_workshop_map *map = steam_workshop_map(index);

		if (map && !strcmp(map->item_id, id) && map->preview[0])
			return snprintf(path, (size_t)size, "%s", map->preview) < size;
	}
	return 0;
}

const unsigned char *workshop_browser_preview(int index, int *width, int *height, int *serial)
{
#if defined(HALO_ANDROID) || defined(HALO_WEB)
	(void)index; (void)width; (void)height; (void)serial;
	return NULL;
#else
	const struct workshop_browser_item *item = workshop_browser_item(index);
	struct preview_job *job;
	pthread_t thread;
	int tried;

	if (!item || item->setup)
		return NULL;
	pthread_mutex_lock(&workshop.lock);
	if (preview.done)
	{
		preview.done = 0;
		free(preview.shown);
		preview.shown = preview.pixels;
		preview.pixels = NULL;
		preview.shown_width = preview.width;
		preview.shown_height = preview.height;
		snprintf(preview.shown_id, sizeof(preview.shown_id), "%s", preview.id);
		preview.serial++;
	}
	pthread_mutex_unlock(&workshop.lock);
	if (preview.shown && !strcmp(preview.shown_id, item->id))
	{
		*width = preview.shown_width;
		*height = preview.shown_height;
		*serial = 0x57000000 + preview.serial;
		return preview.shown;
	}
	/* (each item tried once a run; one at a time) */
	for (tried = 0; tried < preview.tried_count && strcmp(preview.tried[tried], item->id); tried++)
		;
	if (tried < preview.tried_count || preview.busy || !(job = calloc(1, sizeof(*job))))
		return NULL;
	if (preview.tried_count < WORKSHOP_BROWSER_MAXIMUM_ITEMS)
		snprintf(preview.tried[preview.tried_count++], WORKSHOP_BROWSER_ID_SIZE, "%s", item->id);
	else
		preview.tried_count = 0;
	snprintf(job->id, sizeof(job->id), "%s", item->id);
	if (item->state == WORKSHOP_ITEM_INSTALLED)
		local_preview(item->id, job->local, sizeof(job->local));
	if (!config_boolean("debug.workshop_no_steam"))
		snprintf(job->url, sizeof(job->url), "%s", item->preview_url);
	{
		const char *save = platform_save_root();
		char folder[STEAM_WORKSHOP_PATH_SIZE];

		if (save && snprintf(folder, sizeof(folder), "%s/cache", save) < (int)sizeof(folder))
		{
			posix_make_directory(folder);
			if (snprintf(folder, sizeof(folder), "%s/cache/workshop", save) < (int)sizeof(folder))
			{
				posix_make_directory(folder);
				snprintf(job->cache, sizeof(job->cache), "%s/%s-%lld.picture", folder, item->id, item->updated);
			}
		}
	}
	if (!job->local[0] && !job->url[0] && !job->cache[0])
	{
		free(job);
		return NULL;
	}
	preview.busy = 1;
	if (pthread_create(&thread, NULL, preview_thread, job) == 0)
		pthread_detach(thread);
	else
	{
		preview.busy = 0;
		free(job);
	}
	return NULL;
#endif
}

/* the picture of any Workshop item, by its id (a game's MCC map, mcc_maps\<id>\<map>, in Online
Games): its own installed picture, else the one kept in the cache, else Steam's (its details asked
for, then the picture, kept in the cache): one at a time, each id tried once a run. RGBA pixels as
workshop_browser_preview's; NULL until there is one */
const unsigned char *workshop_browser_picture_of(const char *id, int *width, int *height, int *serial)
{
#if defined(HALO_ANDROID) || defined(HALO_WEB)
	(void)id; (void)width; (void)height; (void)serial;
	return NULL;
#else
	struct preview_job *job;
	pthread_t thread;
	int tried;
	const char *save = platform_save_root();
	char folder[STEAM_WORKSHOP_PATH_SIZE], name[256];

	if (!workshop_browser_id_valid(id))
		return NULL;
	pthread_mutex_lock(&workshop.lock);
	if (preview.done)
	{
		preview.done = 0;
		free(preview.shown);
		preview.shown = preview.pixels;
		preview.pixels = NULL;
		preview.shown_width = preview.width;
		preview.shown_height = preview.height;
		snprintf(preview.shown_id, sizeof(preview.shown_id), "%s", preview.id);
		preview.serial++;
	}
	pthread_mutex_unlock(&workshop.lock);
	if (preview.shown && !strcmp(preview.shown_id, id))
	{
		*width = preview.shown_width;
		*height = preview.shown_height;
		*serial = 0x57000000 + preview.serial;
		return preview.shown;
	}
	for (tried = 0; tried < preview.tried_count && strcmp(preview.tried[tried], id); tried++)
		;
	if (tried < preview.tried_count || preview.busy || !(job = calloc(1, sizeof(*job))))
		return NULL;
	if (preview.tried_count < WORKSHOP_BROWSER_MAXIMUM_ITEMS)
		snprintf(preview.tried[preview.tried_count++], WORKSHOP_BROWSER_ID_SIZE, "%s", id);
	else
		preview.tried_count = 0;
	snprintf(job->id, sizeof(job->id), "%s", id);
	local_preview(id, job->local, sizeof(job->local));
	{
		struct details_entry *known = details_find(id);

		if (known && known->filled)
			snprintf(job->url, sizeof(job->url), "%s", known->item.preview_url);
	}
	job->ask_details = !job->url[0] && !config_boolean("debug.workshop_no_steam");
	if (save && snprintf(folder, sizeof(folder), "%s/cache/workshop", save) < (int)sizeof(folder))
	{
		void *directory;

		/* (any picture of the item's kept, else a new one under its id) */
		if ((directory = posix_directory_open(folder)) != NULL)
		{
			size_t length = strlen(id);

			while (!job->cache[0] && posix_directory_next(directory, name, sizeof(name)))
			{
				if (!strncmp(name, id, length) && name[length] == '-' && strstr(name, ".picture"))
					snprintf(job->cache, sizeof(job->cache), "%s/%s", folder, name);
			}
			posix_directory_close(directory);
		}
		if (!job->cache[0])
		{
			char cache_folder[STEAM_WORKSHOP_PATH_SIZE];

			if (snprintf(cache_folder, sizeof(cache_folder), "%s/cache", save) < (int)sizeof(cache_folder))
				posix_make_directory(cache_folder);
			posix_make_directory(folder);
			snprintf(job->cache, sizeof(job->cache), "%s/%s-0.picture", folder, id);
		}
	}
	preview.busy = 1;
	if (pthread_create(&thread, NULL, preview_thread, job) == 0)
		pthread_detach(thread);
	else
	{
		preview.busy = 0;
		free(job);
	}
	return NULL;
#endif
}

/* the answer taken in, and the next ids without details asked for */
static void details_step(void)
{
	int index, count = 0;

	pthread_mutex_lock(&workshop.lock);
	if (workshop.details_answered)
	{
		workshop.details_answered = 0;
		/* (failed: asked again only after a wait, 2 s doubling to a minute) */
		if (workshop.details_answer_count < 0)
			retry_backoff_failed(&workshop.details_retry, system_milliseconds(), workshop.details_status,
				workshop.details_retry_after);
		else
			retry_backoff_answered(&workshop.details_retry);
		for (index = 0; index < workshop.details_asked_count; index++)
		{
			struct details_entry *entry = details_find(workshop.details_asked[index]);
			int other;

			if (!entry)
				continue;
			/* (a failed request: asked again on the next open) */
			if (workshop.details_answer_count < 0)
			{
				entry->id[0] = 0;
				continue;
			}
			entry->answered = 1;
			for (other = 0; other < workshop.details_answer_count; other++)
			{
				if (!strcmp(workshop.details_answer[other].id, entry->id))
				{
					entry->item = workshop.details_answer[other];
					entry->filled = 1;
				}
			}
		}
		for (index = 0; index < workshop.count; index++)
			details_apply(&workshop.items[index]);
	}
	if (workshop.details_fetching)
	{
		pthread_mutex_unlock(&workshop.lock);
		return;
	}
	pthread_mutex_unlock(&workshop.lock);
#ifdef HALO_GAME_BROWSER
	{
		char *body;
		int length;
		size_t size;

		if (config_boolean("debug.workshop_no_steam") || !retry_backoff_ready(&workshop.details_retry,
			system_milliseconds()))
		{
			return;
		}
		for (index = 0; index < workshop.count && count < DETAILS_BATCH; index++)
		{
			struct workshop_browser_item *item = &workshop.items[index];
			struct details_entry *entry;

			if (item->setup || !workshop_browser_id_valid(item->id) || (item->description[0] && item->size >= 0))
				continue;
			if ((entry = details_find(item->id)) != NULL)
				continue;
			/* (the cache full: its first empty slot, else nothing more asked) */
			for (entry = NULL, length = 0; length < workshop.details_count && workshop.details[length].id[0]; length++)
				;
			if (length < workshop.details_count)
				entry = &workshop.details[length];
			else if (workshop.details_count < DETAILS_CACHE)
				entry = &workshop.details[workshop.details_count++];
			if (!entry)
				break;
			memset(entry, 0, sizeof(*entry));
			snprintf(entry->id, sizeof(entry->id), "%s", item->id);
			snprintf(workshop.details_asked[count++], WORKSHOP_BROWSER_ID_SIZE, "%s", item->id);
		}
		if (!count)
			return;
		size = 64 + (size_t)count * (WORKSHOP_BROWSER_ID_SIZE + 32);
		body = malloc(size);
		if (!body)
			return;
		length = snprintf(body, size, "itemcount=%d", count);
		for (index = 0; index < count; index++)
			length += snprintf(body + length, size - (size_t)length, "&publishedfileids%%5B%d%%5D=%s", index,
				workshop.details_asked[index]);
		pthread_mutex_lock(&workshop.lock);
		workshop.details_asked_count = count;
		workshop.details_fetching = 1;
		pthread_mutex_unlock(&workshop.lock);
		{
			pthread_t thread;

			if (pthread_create(&thread, NULL, details_thread, body) == 0)
				pthread_detach(thread);
			else
			{
				free(body);
				pthread_mutex_lock(&workshop.lock);
				workshop.details_fetching = 0;
				pthread_mutex_unlock(&workshop.lock);
			}
		}
	}
#else
	(void)count;
#endif
}

/* ---------- Steam's public browse page */

#ifdef HALO_GAME_BROWSER
static void *web_thread(void *context)
{
	char *url = context;
	static struct workshop_browser_item found[BROWSE_PAGE_ITEMS];
	static char ids[BROWSE_PAGE_ITEMS][WORKSHOP_BROWSER_ID_SIZE];
	char *response = malloc(BROWSE_PAGE_DOCUMENT + 1), *body = NULL, error[128];
	int status = 0, count = -1, id_count = 0, index, length;
	size_t size;

	error[0] = 0;
	if (response)
		status = posix_browser_request_as(url, NULL, NULL, build_identity_user_agent(), response,
			BROWSE_PAGE_DOCUMENT + 1, error, sizeof(error));
	if (status == 200)
		id_count = workshop_browser_read_browse_ids(response, (int)strlen(response), ids, BROWSE_PAGE_ITEMS);
	if (status == 200 && !id_count)
		platform_log("Workshop: Steam's browse page had no items (%d bytes)", response ? (int)strlen(response) : 0);
	/* (no item on a page without a search: not Steam's list after all, a
	busy page or another one; the list kept) */
	if (status == 200 && id_count == 0 && strstr(url, "searchtext="))
		count = 0;
	size = 64 + (size_t)id_count * (WORKSHOP_BROWSER_ID_SIZE + 32);
	if (id_count > 0 && (body = malloc(size)) != NULL)
	{
		length = snprintf(body, size, "itemcount=%d", id_count);
		for (index = 0; index < id_count; index++)
			length += snprintf(body + length, size - (size_t)length, "&publishedfileids%%5B%d%%5D=%s", index, ids[index]);
		status = posix_browser_request_as(DETAILS_URL, body, "application/x-www-form-urlencoded",
			build_identity_user_agent(), response, BROWSE_PAGE_DOCUMENT + 1, error, sizeof(error));
		if (status == 200)
			count = workshop_browser_read_details(response, (int)strlen(response), found, BROWSE_PAGE_ITEMS);
	}
	/* (Steam's "too many requests": -2, held off a while) */
	if (status == 429)
		count = -2;
	platform_log("Workshop: Steam's browse page: %d Halo CE items (HTTP %d)", count, status);
	pthread_mutex_lock(&workshop.lock);
	workshop.web_status = status;
	workshop.web_retry_after = retry_backoff_after(error);
	workshop.answer_count = count;
	if (count > 0)
		memcpy(workshop.answer, found, sizeof(found[0]) * (size_t)count);
	workshop.answered = 1;
	workshop.web_answer = 1;
	workshop.fetching = 0;
	pthread_mutex_unlock(&workshop.lock);
	free(body);
	free(response);
	free(url);
	return NULL;
}
#endif

/* the browse page's address for the sort and the search (Steam's own words:
trend, mostrecent, toprated, lastupdated; searchtext) */
static void web_url_make(void)
{
	static const char *const sorts[] = { "trend", "mostrecent", "toprated", "lastupdated", "trend" };
	int length = snprintf(workshop.web_url, sizeof(workshop.web_url), "%s&p=%d&browsesort=%s", BROWSE_PAGE_URL,
		workshop.page > 0 ? workshop.page : 1, sorts[workshop.sort]);
	const unsigned char *c;

	if (workshop.search[0] && length < (int)sizeof(workshop.web_url) - 16)
	{
		length += snprintf(workshop.web_url + length, sizeof(workshop.web_url) - (size_t)length, "&searchtext=");
		for (c = (const unsigned char *)workshop.search; *c && length < (int)sizeof(workshop.web_url) - 4; c++)
		{
			if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9'))
				workshop.web_url[length++] = (char)*c;
			else if (*c == ' ')
				workshop.web_url[length++] = '+';
			else
				length += snprintf(workshop.web_url + length, 4, "%%%02X", *c);
		}
		workshop.web_url[length] = 0;
	}
}

/* the list from Steam's public browse page: once, then kept ten minutes,
and not while Steam holds us off */
static void web_ask(void)
{
	unsigned long now = system_milliseconds();

#ifdef HALO_GAME_BROWSER
	pthread_t thread;

	web_url_make();
	/* (a page seen in the last ten minutes: taken as it was) */
	{
		int slot;

		for (slot = 0; slot < PAGE_CACHE; slot++)
		{
			if (workshop.pages[slot].time && now - workshop.pages[slot].time < BROWSE_KEEP &&
				!strcmp(workshop.pages[slot].url, workshop.web_url) && strcmp(workshop.web_url, workshop.web_fetched))
			{
				pthread_mutex_lock(&workshop.lock);
				if (!workshop.fetching)
				{
					workshop.answer_count = workshop.pages[slot].count;
					memcpy(workshop.answer, workshop.pages[slot].items,
						sizeof(workshop.answer[0]) * (size_t)workshop.pages[slot].count);
					workshop.answered = 1;
					workshop.web_answer = 1;
					snprintf(workshop.web_fetched, sizeof(workshop.web_fetched), "%s", workshop.web_url);
				}
				pthread_mutex_unlock(&workshop.lock);
				return;
			}
		}
	}
	if ((workshop.web_time && now - workshop.web_time < BROWSE_KEEP && !strcmp(workshop.web_url, workshop.web_fetched)) ||
		!retry_backoff_ready(&workshop.web_retry, now) ||
		config_boolean("debug.workshop_no_steam"))
	{
		if (workshop.status == WORKSHOP_LIST_LOADING || workshop.status == WORKSHOP_LIST_NOT_SET_UP)
			workshop.status = workshop.web_time ? WORKSHOP_LIST_LOADED : WORKSHOP_LIST_FAILED;
		return;
	}
	pthread_mutex_lock(&workshop.lock);
	if (workshop.fetching)
	{
		pthread_mutex_unlock(&workshop.lock);
		return;
	}
	workshop.fetching = 1;
	pthread_mutex_unlock(&workshop.lock);
	workshop.status = WORKSHOP_LIST_LOADING;
	snprintf(workshop.web_fetched, sizeof(workshop.web_fetched), "%s", workshop.web_url);
	{
		char *url = malloc(strlen(workshop.web_url) + 1);

		if (url)
		{
			strcpy(url, workshop.web_url);
			if (pthread_create(&thread, NULL, web_thread, url) == 0)
			{
				pthread_detach(thread);
				return;
			}
			free(url);
		}
	}
	pthread_mutex_lock(&workshop.lock);
	workshop.fetching = 0;
	pthread_mutex_unlock(&workshop.lock);
	workshop.status = WORKSHOP_LIST_FAILED;
#else
	(void)now;
	workshop.status = WORKSHOP_LIST_NOT_SET_UP;
#endif
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
	/* (no list of ours: Steam's public browse page, the same everywhere) */
	web_ask();
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
	if (workshop.web_answer)
	{
		workshop.web_answer = 0;
		if (workshop.answer_count >= 0)
		{
			workshop.web_time = system_milliseconds();
			retry_backoff_answered(&workshop.web_retry);
		}
		else
		{
			/* (Steam's 429 without a Retry-After: left alone ten minutes, as before) */
			long after = workshop.web_retry_after;

			if (workshop.answer_count == -2 && !after)
				after = BROWSE_BACKOFF / 1000;
			retry_backoff_failed(&workshop.web_retry, system_milliseconds(), workshop.web_status, after);
		}
		/* (a failed page keeps the list it had, if any) */
		if (workshop.answer_count < 0 && workshop.web_time)
		{
			workshop.status = WORKSHOP_LIST_LOADED;
			pthread_mutex_unlock(&workshop.lock);
			return;
		}
	}
	memcpy(before, workshop.items, sizeof(before[0]) * (size_t)before_count);
	/* (a page of Steam's kept for the next ten minutes) */
	if (workshop.answer_count >= 0 && workshop.answer_count <= BROWSE_PAGE_ITEMS && workshop.web_fetched[0])
	{
		int slot, found = -1;

		for (slot = 0; slot < PAGE_CACHE; slot++)
		{
			if (!strcmp(workshop.pages[slot].url, workshop.web_fetched))
				found = slot;
		}
		if (found < 0)
		{
			found = workshop.page_next_slot;
			workshop.page_next_slot = (workshop.page_next_slot + 1) % PAGE_CACHE;
			snprintf(workshop.pages[found].url, sizeof(workshop.pages[found].url), "%s", workshop.web_fetched);
			workshop.pages[found].time = system_milliseconds();
		}
		workshop.pages[found].count = workshop.answer_count;
		memcpy(workshop.pages[found].items, workshop.answer, sizeof(workshop.answer[0]) * (size_t)workshop.answer_count);
	}
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
	setup_rows_update();
	for (index = 0; index < workshop.count; index++)
		details_apply(&workshop.items[index]);
}

/* ---------- public code */

/* the game's own Steam library: steamcmd's downloads (paths.steam_downloads,
else steam/ in the data folder), read in place as any other library */
static void own_library(void)
{
	static int set;
	const char *setting = config_string("paths.steam_downloads"), *data = platform_data_root();
	char path[STEAM_WORKSHOP_PATH_SIZE];

	if (set)
		return;
	set = 1;
	if (setting && setting[0])
		snprintf(path, sizeof(path), "%s", setting);
	else if (!data || snprintf(path, sizeof(path), "%s/steam", data) >= (int)sizeof(path))
		return;
	steam_workshop_set_own_library(path);
	steam_bridge_set_download_folder(path);
}

int workshop_browser_mcc_present(void)
{
#if defined(HALO_ANDROID) || defined(HALO_WEB)
	return 0;
#else
	own_library();
	libraries_look(0);
	return workshop.mcc_count > 0;
#endif
}

int workshop_browser_offered(void)
{
#if defined(HALO_ANDROID) || defined(HALO_WEB)
	return 0;
#else
	/* (every desktop: the list works before setup, whose rows say what's needed) */
	return 1;
#endif
}

int workshop_browser_bridge_backend(void)
{
	return bridge_backend();
}

int workshop_browser_bridge_status(void)
{
	if (!steam_bridge_enabled() || bridge_backend() == STEAM_BRIDGE_BACKEND_NONE)
		return STEAM_BRIDGE_OFF;
	return steam_bridge_status();
}

void workshop_browser_own_library(void)
{
#if !defined(HALO_ANDROID) && !defined(HALO_WEB)
	own_library();
#endif
}

void workshop_browser_close(void)
{
	workshop.tab_open = 0;
	steam_bridge_close();
}

void workshop_browser_open(int force)
{
	int index;

	libraries_look(1);
	workshop.tab_open = 1;
	/* (only while the section shows: Steam shows MCC as running then) */
	if (steam_bridge_enabled() && bridge_backend() != STEAM_BRIDGE_BACKEND_NONE)
		steam_bridge_open(bridge_uses_steamcmd());
	for (index = 0; index < workshop.count; index++)
		workshop.items[index].state = item_state(workshop.items[index].id);
	local_items_look();
	setup_rows_update();
	for (index = 0; index < workshop.count; index++)
		details_apply(&workshop.items[index]);
	workshop.watch_time = system_milliseconds();
	if (force || !workshop.asked ||
		(workshop.status == WORKSHOP_LIST_FAILED && system_milliseconds() - workshop.asked_time >= RETRY_INTERVAL))
	{
		list_ask();
	}
}

static void pending_run(void);

int workshop_browser_poll(void)
{
	unsigned long now = system_milliseconds();
	int index, installed = 0;

	/* (the bridge's connect timed out here: steam_bridge_status) */
	workshop_browser_bridge_status();
	/* (GET on the Halo CE files row, once steamcmd is up) */
	if (workshop.install_pending)
	{
		int status = steam_bridge_status();

		if (status == STEAM_BRIDGE_READY)
		{
			steam_bridge_install_mcc();
			workshop.install_pending = 0;
		}
		else if (status != STEAM_BRIDGE_CONNECTING)
			workshop.install_pending = 0;
	}
	list_take();
	details_step();
	pending_run();
	if (now - workshop.watch_time < WATCH_INTERVAL)
		return 0;
	workshop.watch_time = now;
	libraries_look(0);
	for (index = 0; index < workshop.count; index++)
	{
		struct workshop_browser_item *item = &workshop.items[index];
		int state;

		if (item->setup)
			continue;
		state = bridge_state(item, item_state(item->id));
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
	setup_rows_remove();
	index = workshop.count;
	local_items_look();
	installed += workshop.count - index;
	for (; index < workshop.count; index++)
		details_apply(&workshop.items[index]);
	index = workshop.mcc_count;
	setup_rows_update();
	/* (MCC's Halo CE files arrived from steamcmd: the maps read again) */
	if (!index && workshop.mcc_count)
		installed++;
	return installed;
}

int workshop_browser_status(void)
{
	return workshop.status;
}

/* ---------- the list as shown */

static int contains_words(const char *text, const char *words)
{
	char word[64];
	const char *at = words;

	/* (every word somewhere, case aside) */
	while (*at)
	{
		int length = 0;
		const char *hay;
		int found = 0;

		while (*at == ' ')
			at++;
		while (*at && *at != ' ' && length < (int)sizeof(word) - 1)
			word[length++] = *at++;
		word[length] = 0;
		if (!length)
			break;
		for (hay = text; *hay && !found; hay++)
		{
			int index;

			for (index = 0; index < length && hay[index]; index++)
			{
				char a = hay[index], b = word[index];

				if ((a | 0x20) != (b | 0x20) || ((a | 0x20) < 'a' && a != b))
					break;
			}
			found = index == length;
		}
		if (!found)
			return 0;
	}
	return 1;
}

static int kept(const struct workshop_browser_item *item)
{
	switch (workshop.filter)
	{
	case WORKSHOP_FILTER_MULTIPLAYER: if (!(item->tags & WORKSHOP_TAG_MULTIPLAYER)) return 0; break;
	case WORKSHOP_FILTER_CAMPAIGN: if (!(item->tags & WORKSHOP_TAG_CAMPAIGN)) return 0; break;
	case WORKSHOP_FILTER_INSTALLED: if (item->state != WORKSHOP_ITEM_INSTALLED) return 0; break;
	default: break;
	}
	return !workshop.search[0] || contains_words(item->title, workshop.search) ||
		contains_words(item->description, workshop.search) || contains_words(item->kind, workshop.search);
}

static void find_row_make(void)
{
	struct workshop_browser_item *item = &workshop.find_row;

	memset(item, 0, sizeof(*item));
	snprintf(item->id, sizeof(item->id), "find");
	snprintf(item->title, sizeof(item->title), "%s", workshop.search[0] ? workshop.search : "Search");
	snprintf(item->kind, sizeof(item->kind), "%s - %s", workshop_browser_filter_name(workshop.filter),
		workshop_browser_sort_name(workshop.sort));
	item->size = -1;
	item->setup = WORKSHOP_ROW_FIND;
	item->step = -1;
}

/* whether title a comes after b, case aside */
static int title_after(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
	{
		char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;

		if (x != y)
			return x > y;
	}
	return *a != 0;
}

static void view_build(void)
{
	int index, other, count = 0;

	find_row_make();
	/* (no row for Steam's link: only GET and UPDATE need it, and say so) */
	for (index = 0; index < workshop.count; index++)
	{
		if (workshop.items[index].setup)
			workshop.view[count++] = index;
	}
	workshop.view[count++] = -1;
	for (index = 0; index < workshop.count && count < (int)(sizeof(workshop.view) / sizeof(workshop.view[0])); index++)
	{
		if (!workshop.items[index].setup && kept(&workshop.items[index]))
			workshop.view[count++] = index;
	}
	/* (A-Z: by title, in place; the others keep Steam's order) */
	if (workshop.sort == WORKSHOP_SORT_NAME)
	{
		for (index = 1; index < count; index++)
		{
			int value = workshop.view[index];

			if (value < 0 || workshop.items[value].setup)
				continue;
			for (other = index; other > 0 && workshop.view[other - 1] >= 0 &&
				!workshop.items[workshop.view[other - 1]].setup &&
				title_after(workshop.items[workshop.view[other - 1]].title, workshop.items[value].title); other--)
			{
				workshop.view[other] = workshop.view[other - 1];
			}
			workshop.view[other] = value;
		}
	}
	if (page_row_offered() && count < (int)(sizeof(workshop.view) / sizeof(workshop.view[0])))
	{
		page_row_make();
		workshop.view[count++] = -4;
	}
	workshop.view_count = count;
}

/* a shown row's index into items: -1 the find row, -3 the link row, -2 none */
static int real_index(int index)
{
	return index >= 0 && index < workshop.view_count ? workshop.view[index] : -2;
}

int workshop_browser_count(void)
{
	view_build();
	return workshop.view_count;
}

const struct workshop_browser_item *workshop_browser_item(int index)
{
	int real = real_index(index);

	if (real == -3)
		return &workshop.link_row;
	if (real == -4)
		return &workshop.page_row;
	return real == -1 ? &workshop.find_row : real >= 0 && real < workshop.count ? &workshop.items[real] : NULL;
}

int workshop_browser_filter(void)
{
	return workshop.filter;
}

int workshop_browser_sort(void)
{
	return workshop.sort;
}

const char *workshop_browser_search(void)
{
	return workshop.search;
}

int workshop_browser_filter_offered(int filter)
{
	int index;

	for (index = 0; filter != WORKSHOP_FILTER_ALL && index < workshop.count; index++)
	{
		const struct workshop_browser_item *item = &workshop.items[index];

		if (!item->setup && ((filter == WORKSHOP_FILTER_MULTIPLAYER && (item->tags & WORKSHOP_TAG_MULTIPLAYER)) ||
			(filter == WORKSHOP_FILTER_CAMPAIGN && (item->tags & WORKSHOP_TAG_CAMPAIGN)) ||
			(filter == WORKSHOP_FILTER_INSTALLED && item->state == WORKSHOP_ITEM_INSTALLED)))
		{
			return 1;
		}
	}
	return filter == WORKSHOP_FILTER_ALL;
}

/* (another page of Steam's: asked for, or taken from the pages kept) */
static void list_again(void);

int workshop_browser_busy_seconds(void)
{
	unsigned long left = retry_backoff_left(&workshop.web_retry, system_milliseconds());

	if (!left || !(workshop.web_retry.status == 429 || workshop.web_retry.status >= 500))
		return 0;
	return (int)((left + 999) / 1000);
}

int workshop_browser_retry(void)
{
	/* (the player's own try: any wait dropped, but a busy Steam's) */
	if (workshop_browser_busy_seconds())
		return 0;
	retry_backoff_answered(&workshop.web_retry);
	workshop.web_fetched[0] = 0;
	list_ask();
	return 1;
}

int workshop_browser_page_offered(int step)
{
	if (step > 0)
		return page_row_offered() && workshop.page < 100;
	return workshop_browser_page() > 1 && workshop.filter != WORKSHOP_FILTER_INSTALLED;
}

int workshop_browser_page_next(void)
{
	if (!page_row_offered() || workshop.page >= 100)
		return 0;
	workshop.page = workshop_browser_page() + 1;
	list_again();
	return 1;
}

int workshop_browser_page_previous(void)
{
	if (workshop_browser_page() <= 1)
		return 0;
	workshop.page = workshop_browser_page() - 1;
	list_again();
	return 1;
}

void workshop_browser_set_filter(int filter)
{
	workshop.filter = filter >= 0 && filter < WORKSHOP_FILTER_COUNT ? filter : WORKSHOP_FILTER_ALL;
	view_build();
}

/* (a change of order or words: Steam's page asked again, once; the network
list of ours, when set, is not Steam's and is only filtered) */
static void list_again(void)
{
	const char *setting = config_string("network.workshop_url");

	view_build();
	if (!setting || !setting[0])
		web_ask();
}

void workshop_browser_set_sort(int sort)
{
	workshop.page = 1;
	workshop.sort = sort >= 0 && sort < WORKSHOP_SORT_COUNT ? sort : WORKSHOP_SORT_POPULAR;
	list_again();
}

void workshop_browser_set_search(const char *text)
{
	workshop.page = 1;
	snprintf(workshop.search, sizeof(workshop.search), "%s", text ? text : "");
	list_again();
}

/* GETs pressed while the game's own Steam download was still connecting:
asked for once it is up (workshop_browser_poll) */
static char pending_gets[8][WORKSHOP_BROWSER_ID_SIZE];

/* GETs the game's own download refused, and its word for each: shown on the
item (TRY AGAIN, OPEN IN STEAM) until it is asked again */
static struct
{
	char id[WORKSHOP_BROWSER_ID_SIZE];
	char why[16];
} failed_gets[8];
static int failed_next;

static struct workshop_browser_item *item_by_id(const char *id)
{
	int index;

	for (index = 0; index < workshop.count; index++)
	{
		if (!workshop.items[index].setup && !strcmp(workshop.items[index].id, id))
			return &workshop.items[index];
	}
	return NULL;
}

static const char *failed_why(const char *id)
{
	int slot;

	for (slot = 0; slot < (int)(sizeof(failed_gets) / sizeof(failed_gets[0])); slot++)
	{
		if (failed_gets[slot].id[0] && !strcmp(failed_gets[slot].id, id))
			return failed_gets[slot].why;
	}
	return NULL;
}

static void failed_clear(const char *id)
{
	int slot;

	for (slot = 0; slot < (int)(sizeof(failed_gets) / sizeof(failed_gets[0])); slot++)
	{
		if (!strcmp(failed_gets[slot].id, id))
			failed_gets[slot].id[0] = 0;
	}
}

/* a GET that didn't go: the item back to Not installed with the word why */
static void get_failed(const char *id, const char *why)
{
	struct workshop_browser_item *item = item_by_id(id);
	int slot = failed_next;

	failed_clear(id);
	failed_next = (failed_next + 1) % (int)(sizeof(failed_gets) / sizeof(failed_gets[0]));
	snprintf(failed_gets[slot].id, sizeof(failed_gets[slot].id), "%s", id);
	snprintf(failed_gets[slot].why, sizeof(failed_gets[slot].why), "%s", why);
	platform_log("Workshop: item %s not taken by the Steam download: %s", id, why);
	if (item)
	{
		if (item->state != WORKSHOP_ITEM_INSTALLED)
		{
			item->subscribed = 0;
			item->state = WORKSHOP_ITEM_NOT_INSTALLED;
		}
		snprintf(item->steam_error, sizeof(item->steam_error), "%s", why);
	}
}

/* 1: waiting; 0: no room (the item says so) */
static int pending_add(const char *id)
{
	int slot, free_slot = -1;

	for (slot = 0; slot < (int)(sizeof(pending_gets) / sizeof(pending_gets[0])); slot++)
	{
		if (!strcmp(pending_gets[slot], id))
			return 1;
		if (!pending_gets[slot][0] && free_slot < 0)
			free_slot = slot;
	}
	if (free_slot < 0)
	{
		get_failed(id, "Too many at once");
		return 0;
	}
	snprintf(pending_gets[free_slot], sizeof(pending_gets[free_slot]), "%s", id);
	return 1;
}

static void pending_run(void)
{
	int slot;

	if (!bridge_ready())
		return;
	for (slot = 0; slot < (int)(sizeof(pending_gets) / sizeof(pending_gets[0])); slot++)
	{
		if (!pending_gets[slot][0])
			continue;
		if (!steam_bridge_subscribe(pending_gets[slot]))
			get_failed(pending_gets[slot], "Failed");
		pending_gets[slot][0] = 0;
	}
}

void workshop_browser_link_follow(void)
{
	/* (linked: the game's own download on, the link being the player's yes) */
	if (!link_offered() || !steam_link_linked(NULL, 0))
		return;
	if (!steam_bridge_enabled())
	{
		config_write_boolean("game.workshop_steam", 1);
		platform_log("Workshop: Steam linked, downloads in the game on");
	}
	if (workshop.tab_open && steam_bridge_status() != STEAM_BRIDGE_READY &&
		steam_bridge_status() != STEAM_BRIDGE_CONNECTING)
	{
		steam_bridge_open(bridge_uses_steamcmd());
	}
}

int workshop_browser_open_in_steam(int index)
{
	char url[96];

	index = real_index(index);
	if (index < 0 || index >= workshop.count || workshop.items[index].setup ||
		!workshop_browser_steam_url(workshop.items[index].id, url, sizeof(url)))
	{
		return 0;
	}
	platform_open_url(url);
	return 1;
}

int workshop_browser_subscribe(int index)
{
	char url[96];
	int shown = index;

	index = real_index(index);
	if (index < 0 || index >= workshop.count)
		return 0;
	if (workshop.items[index].setup)
		return workshop_browser_setup_press(shown);
	if (!workshop_browser_steam_url(workshop.items[index].id, url, sizeof(url)))
		return 0;
	/* (OPEN: an item Steam has, or is fetching: its page, nothing asked;
	the game's own download shows its progress instead) */
	if (workshop.items[index].state != WORKSHOP_ITEM_NOT_INSTALLED)
	{
		if (!link_offered() && !config_boolean("debug.workshop_no_steam"))
			platform_open_url(url);
		return 1;
	}
	/* (the game's own Steam download: linked, it downloads here once its
	connection is up; never Steam's page) */
	if (link_offered())
	{
		if (!steam_link_linked(NULL, 0))
			return 0;
		failed_clear(workshop.items[index].id);
		workshop.items[index].steam_error[0] = 0;
		if (!bridge_ready())
		{
			workshop_browser_link_follow();
			if (pending_add(workshop.items[index].id))
			{
				workshop.items[index].subscribed = 1;
				workshop.items[index].state = WORKSHOP_ITEM_WAITING;
			}
			return 1;
		}
		/* (refused: Failed on the item, TRY AGAIN; Steam's page only when
		the player picks OPEN IN STEAM) */
		if (!steam_bridge_subscribe(workshop.items[index].id))
		{
			get_failed(workshop.items[index].id, "Failed");
			return 1;
		}
		workshop.items[index].subscribed = 1;
		workshop.items[index].state = WORKSHOP_ITEM_WAITING;
		return 1;
	}
	/* (the bridge's: subscribed in Steam from here, no page) */
	if (bridge_ready() && steam_bridge_subscribe(workshop.items[index].id))
	{
		/* (steamcmd downloads, but only the Steam client subscribes: its page
		too when the bridge couldn't reach the client) */
		if (!(steam_bridge_connected() & CLIENT_CONNECTED) && steam_bridge_steam_present() &&
			!config_boolean("debug.workshop_no_steam"))
		{
			platform_open_url(url);
		}
		workshop.items[index].subscribed = 1;
		if (workshop.items[index].state == WORKSHOP_ITEM_NOT_INSTALLED)
			workshop.items[index].state = WORKSHOP_ITEM_WAITING;
		return 1;
	}
	/* (debug.workshop_no_steam: the page not opened, for tests) */
	if (!config_boolean("debug.workshop_no_steam"))
		platform_open_url(url);
	workshop.items[index].subscribed = 1;
	if (workshop.items[index].state == WORKSHOP_ITEM_NOT_INSTALLED)
		workshop.items[index].state = WORKSHOP_ITEM_WAITING;
	return 1;
}

int workshop_browser_unsubscribe(int index)
{
	index = real_index(index);
	if (index < 0 || index >= workshop.count || workshop.items[index].setup || !bridge_ready() ||
		!steam_bridge_unsubscribe(workshop.items[index].id))
	{
		return 0;
	}
	workshop.items[index].subscribed = 0;
	workshop.items[index].steam_subscribed = 0;
	return 1;
}

/* ---------- REMOVE, UPDATE (PLAY: the Guide's, guide_workshop.c) */

void mcc_maps_rescan(void);

/* REMOVE: the install backend's (the client unsubscribes; a downloader
deletes its own copy), only while the bridge is up */
int workshop_browser_removable(int index)
{
	const struct workshop_browser_item *item = workshop_browser_item(index);

	return item && !item->setup && bridge_ready() && steam_bridge_removable(item->id);
}

int workshop_browser_remove(int index)
{
	const struct workshop_browser_item *item = workshop_browser_item(index);

	if (!workshop_browser_removable(index) || !steam_bridge_remove(item->id))
		return 0;
	platform_log("Workshop: item %s asked to be removed", item->id);
	mcc_maps_rescan();
	workshop.watch_time = 0;
	return 1;
}

int workshop_browser_update_offered(int index)
{
	const struct workshop_browser_item *item = workshop_browser_item(index);
	char folder[STEAM_WORKSHOP_PATH_SIZE], path[STEAM_WORKSHOP_PATH_SIZE], found[256];
	struct posix_file_information information;
	int library;

	/* (Steam's update time after the installed copy's own, in any library) */
	if (!item || item->setup || item->state != WORKSHOP_ITEM_INSTALLED || item->updated <= 0)
		return 0;
	for (library = 0; library < workshop.library_count; library++)
	{
		if (snprintf(folder, sizeof(folder), "%s/" CONTENT_FOLDER "/%s", workshop.libraries[library], item->id) <
			(int)sizeof(folder) && posix_find_entry_case_insensitive(folder, "ModInfo.json", found, sizeof(found)) &&
			join(path, sizeof(path), folder, found) && posix_stat(path, &information) == 0)
		{
			return (long long)information.modification_seconds < item->updated;
		}
	}
	return 0;
}

int workshop_browser_update(int index)
{
	const struct workshop_browser_item *item = workshop_browser_item(index);

	if (!workshop_browser_update_offered(index))
		return 0;
	failed_clear(item->id);
	/* (not connected yet, or refused: Failed on the item with RETRY, never silence) */
	if (!bridge_ready())
	{
		workshop_browser_link_follow();
		get_failed(item->id, "Not connected");
		return 0;
	}
	if (!steam_bridge_update(item->id))
	{
		get_failed(item->id, "Update failed");
		return 0;
	}
	return 1;
}

/* ---------- a join's missing Workshop map */

static struct
{
	int state;
	int asked;
	char id[WORKSHOP_BROWSER_ID_SIZE];
	char why[64];
	unsigned long time;
	unsigned long started;
} joining;

enum { JOIN_LOOK_INTERVAL = 1000, JOIN_TIMEOUT = 45 * 60 * 1000 };

int workshop_join_setting(void)
{
	const char *setting = config_string("network.workshop_downloads");

	if (setting && !strcmp(setting, "always"))
		return WORKSHOP_JOIN_ALWAYS;
	if (setting && !strcmp(setting, "never"))
		return WORKSHOP_JOIN_NEVER;
	return WORKSHOP_JOIN_ASK;
}

int config_write(const char *name, const char *value);

void workshop_join_set_setting(int setting)
{
	config_write("network.workshop_downloads", setting == WORKSHOP_JOIN_ALWAYS ? "always" :
		setting == WORKSHOP_JOIN_NEVER ? "never" : "ask");
}

static void join_fail(const char *why)
{
	joining.state = WORKSHOP_JOIN_FAILED;
	snprintf(joining.why, sizeof(joining.why), "%s", why);
	platform_log("Workshop: the join's item %s not fetched: %s", joining.id, why);
}

/* the item checked with Steam before the player is asked: Steam's own
title and size, and that it is a Halo CE item of MCC's (the host's word
for it is never taken) */
static struct
{
	pthread_mutex_t lock;
	int state;
	char id[WORKSHOP_BROWSER_ID_SIZE];
	char title[WORKSHOP_BROWSER_TITLE_SIZE];
	long long size;
} checking = { PTHREAD_MUTEX_INITIALIZER, WORKSHOP_CHECK_NONE, "", "", 0 };

#ifdef HALO_GAME_BROWSER
static void *check_thread(void *context)
{
	char *id = context, body[96], error[128];
	static struct workshop_browser_item found;
	char *response = malloc(BROWSE_PAGE_DOCUMENT + 1);
	int status = 0, count = -1;

	snprintf(body, sizeof(body), "itemcount=1&publishedfileids%%5B0%%5D=%s", id);
	if (response)
		status = posix_browser_request_as(DETAILS_URL, body, "application/x-www-form-urlencoded",
			build_identity_user_agent(), response, BROWSE_PAGE_DOCUMENT + 1, error, sizeof(error));
	pthread_mutex_lock(&checking.lock);
	memset(&found, 0, sizeof(found));
	if (status == 200)
		count = workshop_browser_read_details(response, (int)strlen(response), &found, 1);
	if (!strcmp(checking.id, id))
	{
		/* (kept only when Steam says it is MCC's, tagged Halo1, and this id) */
		if (count == 1 && !strcmp(found.id, id))
		{
			checking.state = WORKSHOP_CHECK_OK;
			snprintf(checking.title, sizeof(checking.title), "%s", found.title);
			checking.size = found.size;
		}
		else
			checking.state = status == 200 && count >= 0 ? WORKSHOP_CHECK_REFUSED : WORKSHOP_CHECK_FAILED;
		platform_log("Workshop: the join's item %s checked with Steam: %s", id,
			checking.state == WORKSHOP_CHECK_OK ? "a Halo CE item" : checking.state == WORKSHOP_CHECK_REFUSED ?
			"not a Halo CE item of MCC's" : "no answer");
	}
	pthread_mutex_unlock(&checking.lock);
	free(response);
	free(id);
	return NULL;
}
#endif

int workshop_join_check(const char *id)
{
	pthread_mutex_lock(&checking.lock);
	memset(checking.title, 0, sizeof(checking.title));
	checking.size = 0;
	checking.state = WORKSHOP_CHECK_FAILED;
	snprintf(checking.id, sizeof(checking.id), "%s", workshop_browser_id_valid(id) ? id : "");
	pthread_mutex_unlock(&checking.lock);
	if (!workshop_browser_id_valid(id))
		return 0;
#ifdef HALO_GAME_BROWSER
	{
		pthread_t thread;
		char *copy = malloc(strlen(id) + 1);

		if (!copy)
			return 0;
		strcpy(copy, id);
		pthread_mutex_lock(&checking.lock);
		checking.state = WORKSHOP_CHECK_ASKING;
		pthread_mutex_unlock(&checking.lock);
		if (pthread_create(&thread, NULL, check_thread, copy) == 0)
			pthread_detach(thread);
		else
		{
			free(copy);
			pthread_mutex_lock(&checking.lock);
			checking.state = WORKSHOP_CHECK_FAILED;
			pthread_mutex_unlock(&checking.lock);
			return 0;
		}
	}
	return 1;
#else
	return 0;
#endif
}

int workshop_join_checked(char *title, int title_size, long long *size)
{
	int state;

	pthread_mutex_lock(&checking.lock);
	state = checking.state;
	if (title && title_size > 0)
		snprintf(title, (size_t)title_size, "%s", checking.title);
	if (size)
		*size = checking.size;
	pthread_mutex_unlock(&checking.lock);
	return state;
}

/* the item the player said yes to: only one Steam called a Halo CE item */
static int join_checked(const char *id)
{
	int ok;

	pthread_mutex_lock(&checking.lock);
	ok = checking.state == WORKSHOP_CHECK_OK && !strcmp(checking.id, id);
	pthread_mutex_unlock(&checking.lock);
	return ok;
}

int workshop_join_start(const char *id, char *why, int size)
{
	why[0] = 0;
	memset(&joining, 0, sizeof(joining));
	if (!workshop_browser_id_valid(id))
	{
		snprintf(why, (size_t)size, "Not a Workshop item.");
		return 0;
	}
	if (!join_checked(id))
	{
		snprintf(why, (size_t)size, "Steam didn't say it is a Halo CE item.");
		return 0;
	}
	snprintf(joining.id, sizeof(joining.id), "%s", id);
	own_library();
	libraries_look(1);
	joining.started = joining.time = system_milliseconds();
	if (item_state(id) == WORKSHOP_ITEM_INSTALLED)
	{
		joining.state = WORKSHOP_JOIN_INSTALLED;
		return 1;
	}
	/* (the step that is missing, as the setup rows name it) */
	if (setup_steamcmd() && link_offered())
	{
		/* (not linked: the Guide's QR, and the download once it's scanned) */
		if (!steam_link_linked(NULL, 0))
		{
			int link = steam_link_poll(NULL, 0, NULL, 0);

			if (link != STEAM_LINK_STARTING && link != STEAM_LINK_WAITING && link != STEAM_LINK_SCANNED)
				steam_link_start();
			joining.state = WORKSHOP_JOIN_LINKING;
			return 1;
		}
	}
	else if (setup_steamcmd())
	{
		if (steam_bridge_backend() != STEAM_BRIDGE_BACKEND_STEAMCMD)
		{
			snprintf(why, (size_t)size, "Install steamcmd first (Workshop tab).");
			return 0;
		}
		if (!steamcmd_signed_in())
		{
			snprintf(why, (size_t)size, "Sign in to steamcmd first (Workshop tab).");
			return 0;
		}
	}
	else if (bridge_backend() == STEAM_BRIDGE_BACKEND_NONE)
	{
		snprintf(why, (size_t)size, "Open Steam first.");
		return 0;
	}
	/* (the player's yes: the bridge on, as USE STEAM turns it on) */
	if (!steam_bridge_enabled())
		config_write_boolean("game.workshop_steam", 1);
	steam_bridge_open(bridge_uses_steamcmd());
	joining.state = WORKSHOP_JOIN_CONNECTING;
	return 1;
}

int workshop_join_poll(unsigned long long *done, unsigned long long *total, char *why, int size)
{
	unsigned long now = system_milliseconds();
	struct steam_bridge_item known;
	int status;

	*done = *total = 0;
	if (why && size > 0)
		snprintf(why, (size_t)size, "%s", joining.why);
	if (joining.state == WORKSHOP_JOIN_LINKING)
	{
		int link = steam_link_poll(NULL, 0, NULL, 0);

		if (steam_link_linked(NULL, 0) && link != STEAM_LINK_STARTING && link != STEAM_LINK_WAITING &&
			link != STEAM_LINK_SCANNED)
		{
			/* (scanned: the download goes on as if linked from the start) */
			workshop_browser_link_follow();
			steam_bridge_open(bridge_uses_steamcmd());
			joining.started = joining.time = now;
			joining.state = WORKSHOP_JOIN_CONNECTING;
		}
		else if (link == STEAM_LINK_IDLE || link == STEAM_LINK_FAILED)
			join_fail("Steam isn't linked.");
		return joining.state;
	}
	if (joining.state != WORKSHOP_JOIN_CONNECTING && joining.state != WORKSHOP_JOIN_DOWNLOADING)
		return joining.state;
	if (now - joining.started > JOIN_TIMEOUT)
	{
		join_fail("Steam took too long.");
		return joining.state;
	}
	status = steam_bridge_status();
	if (status == STEAM_BRIDGE_CONNECTING)
		return joining.state;
	if (status != STEAM_BRIDGE_READY)
	{
		join_fail("Steam closed.");
		return joining.state;
	}
	if (!joining.asked)
	{
		if (steam_bridge_sign_in_needed())
		{
			join_fail(link_offered() ? "Link Steam first." : "Sign in to steamcmd first.");
			return joining.state;
		}
		if (!steam_bridge_subscribe(joining.id))
		{
			join_fail("Steam didn't take it.");
			return joining.state;
		}
		joining.asked = 1;
		joining.state = WORKSHOP_JOIN_DOWNLOADING;
	}
	if (steam_bridge_item(joining.id, &known))
	{
		*done = known.downloaded;
		*total = known.total;
		if (known.state == STEAM_BRIDGE_ITEM_FAILED)
		{
			join_fail(steam_bridge_sign_in_needed() ? (link_offered() ? "Link Steam first." : "Sign in to steamcmd first.") :
				known.error[0] ? known.error : "Download failed.");
			return joining.state;
		}
	}
	if (now - joining.time >= JOIN_LOOK_INTERVAL)
	{
		joining.time = now;
		libraries_look(0);
		if (item_state(joining.id) == WORKSHOP_ITEM_INSTALLED)
		{
			char folder[STEAM_WORKSHOP_PATH_SIZE];

			joining.state = WORKSHOP_JOIN_INSTALLED;
			platform_log("Workshop: the join's item %s installed", joining.id);
			mcc_maps_rescan();
			/* (its ModInfo.json says Halo1, as Steam's tag did: only then are
			its maps read; anything else is no map of this game's) */
			if (!steam_workshop_package_folder(joining.id, folder, sizeof(folder)))
				join_fail("Not a Halo CE item.");
			if (!workshop.tab_open)
				steam_bridge_close();
		}
	}
	return joining.state;
}

void workshop_join_cancel(void)
{
	if (joining.state == WORKSHOP_JOIN_LINKING)
		steam_link_cancel();
	if (joining.state == WORKSHOP_JOIN_CONNECTING || joining.state == WORKSHOP_JOIN_DOWNLOADING)
		steam_bridge_close();
	joining.state = WORKSHOP_JOIN_IDLE;
}

/* ---------- the map lists' pictures */

int workshop_browser_cached_picture(const char *level, struct mcc_preview_image *image)
{
	const char *save = platform_save_root(), *start;
	char id[WORKSHOP_BROWSER_ID_SIZE], folder[STEAM_WORKSHOP_PATH_SIZE], name[256], path[STEAM_WORKSHOP_PATH_SIZE];
	unsigned char *data, *pixels;
	void *directory;
	int length = 0, size = 0, width = 0, height = 0, found = 0;
	unsigned index;

	memset(image, 0, sizeof(*image));
	if (!level || strncmp(level, "mcc_maps\\", 9) || !save)
		return 0;
	for (start = level + 9; start[length] >= '0' && start[length] <= '9' && length < (int)sizeof(id) - 1; length++)
		id[length] = start[length];
	id[length] = 0;
	if (!length || start[length] != '\\' ||
		snprintf(folder, sizeof(folder), "%s/cache/workshop", save) >= (int)sizeof(folder) ||
		!(directory = posix_directory_open(folder)))
	{
		return 0;
	}
	/* (<id>-<update time>.picture: any one of the item's) */
	while (!found && posix_directory_next(directory, name, sizeof(name)))
	{
		found = !strncmp(name, id, (size_t)length) && name[length] == '-' && strstr(name, ".picture") &&
			snprintf(path, sizeof(path), "%s/%s", folder, name) < (int)sizeof(path);
	}
	posix_directory_close(directory);
	if (!found || !(data = file_read(path, &size)))
		return 0;
	pixels = picture_bytes(data, size) ? posix_ui_image_decode(data, size, &width, &height) : NULL;
	free(data);
	if (!pixels)
		return 0;
	image->pixels = malloc((size_t)width * (size_t)height * sizeof(*image->pixels));
	if (image->pixels)
	{
		/* (RGBA bytes to the native ARGB words the map lists draw) */
		for (index = 0; index < (unsigned)(width * height); index++)
		{
			const unsigned char *pixel = pixels + (size_t)index * 4;

			image->pixels[index] = (uint32_t)pixel[3] << 24 | (uint32_t)pixel[0] << 16 | (uint32_t)pixel[1] << 8 | pixel[2];
		}
		image->width = (unsigned)width;
		image->height = (unsigned)height;
	}
	posix_ui_image_free(pixels);
	return image->pixels != NULL;
}
#endif /* HALO_MCC */
