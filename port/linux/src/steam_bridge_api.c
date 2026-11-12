/*
STEAM_BRIDGE_API.C

The bridge's client backend (steam_client_bridge.h): a steam_api library
the player already has (MCC's own steam_api64.dll on Windows; elsewhere one
an installed game ships), loaded at run time and called through its flat C
functions, which talk to the running Steam client. Every declaration here
is our own, written from Valve's public Steamworks documentation (the
steam_api, ISteamUGC, ISteamUtils and ISteamClient pages): no SDK header or
library is in our sources or builds.

- SteamAPI_InitFlat (newer libraries), else SteamAPI_Init or
  SteamAPI_InitSafe (older ones), with SteamAppId set to MCC's 976730 in
  the environment first. It fails without a running, signed-in Steam that
  owns MCC: Steam itself checks.
- ISteamUGC and ISteamUtils from the versioned accessors of newer
  libraries (SteamAPI_SteamUGC_v0NN, SteamAPI_SteamUtils_v0NN), else from
  SteamClient()'s GetISteamUGC / GetISteamUtils with their version names.
- SubscribeItem + DownloadItem (at once), UnsubscribeItem, GetItemState
  and GetItemDownloadInfo; SteamAPI_RunCallbacks each turn;
  SteamAPI_Shutdown when the tab closes.
- Browse: CreateQueryAllUGCRequest for MCC (made with its Workshop tools'
  app, 1695795), tag Halo1, most subscribed first or a text search; its
  answer polled through ISteamUtils' IsAPICallCompleted and
  GetAPICallResult (SteamUGCQueryCompleted_t), each item read with
  GetQueryUGCResult (steam_bridge_read_details).
A missing function the subscribe needs fails the connect, and the tab
falls back; a missing browse function only leaves browsing out. MCC's
Workshop holds every MCC game's items: a list is never asked for without
the Halo1 tag, and items on disk count only with a ModInfo.json whose
Engine is Halo1 (workshop_browser.c).
*/

#include "steam_client_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);

#define MCC_APP_ID "976730"

enum
{
	MCC_APP = 976730,
	/* the app MCC's Workshop items are made with (its Workshop tools) */
	MCC_CREATOR_APP = 1695795,
};

typedef unsigned long long steam_id64;
typedef void *steam_interface;

/* the flat functions (S_CALLTYPE is __cdecl: C's own) */
typedef int (*init_flat_function)(char *error_message);
typedef unsigned char (*bool_function)(void);
typedef void (*void_function)(void);
typedef steam_interface (*accessor_function)(void);
typedef int (*handle_function)(void);
typedef steam_interface (*client_interface_function)(steam_interface client, int user, int pipe, const char *version);
typedef steam_interface (*client_utils_function)(steam_interface client, int pipe, const char *version);
typedef steam_id64 (*subscribe_function)(steam_interface ugc, steam_id64 item);
typedef unsigned int (*item_state_function)(steam_interface ugc, steam_id64 item);
typedef unsigned char (*download_info_function)(steam_interface ugc, steam_id64 item, steam_id64 *downloaded,
	steam_id64 *total);
typedef unsigned char (*download_function)(steam_interface ugc, steam_id64 item, unsigned char high_priority);
typedef steam_id64 (*create_query_function)(steam_interface ugc, int query, int matching, unsigned int creator,
	unsigned int consumer, unsigned int page);
typedef unsigned char (*query_text_function)(steam_interface ugc, steam_id64 query, const char *text);
typedef steam_id64 (*send_query_function)(steam_interface ugc, steam_id64 query);
typedef unsigned char (*query_result_function)(steam_interface ugc, steam_id64 query, unsigned int index,
	void *details);
typedef unsigned char (*release_query_function)(steam_interface ugc, steam_id64 query);
typedef unsigned char (*call_completed_function)(steam_interface utils, steam_id64 call, unsigned char *failed);
typedef unsigned char (*call_result_function)(steam_interface utils, steam_id64 call, void *result, int size,
	int callback, unsigned char *failed);

/* EItemState (ISteamUGC::GetItemState) */
enum
{
	ITEM_SUBSCRIBED = 1,
	ITEM_INSTALLED = 4,
	ITEM_NEEDS_UPDATE = 8,
	ITEM_DOWNLOADING = 16,
	ITEM_DOWNLOAD_PENDING = 32,
};

enum
{
	/* ESteamAPIInitResult's OK; SteamErrMsg is 1024 bytes */
	INIT_OK = 0,
	INIT_MESSAGE_SIZE = 1024,
	/* EUGCQuery: RankedByTextSearch, RankedByTotalUniqueSubscriptions; EUGCMatchingUGCType_Items */
	QUERY_TEXT_SEARCH = 11,
	QUERY_MOST_SUBSCRIBED = 12,
	MATCHING_ITEMS = 0,
	/* SteamUGCQueryCompleted_t's callback id (k_iSteamUGCCallbacks + 1), and
	its size: with m_rgchNextCursor[256] (libraries with the cursor
	query), and before */
	QUERY_COMPLETED_CALLBACK = 3401,
	QUERY_COMPLETED_SIZE = 280,
	QUERY_COMPLETED_OLD_SIZE = 24,
	/* EResult's OK */
	RESULT_OK = 1,
	/* room for any version of SteamUGCDetails_t (about 9.8 KB today) */
	DETAILS_BUFFER = 16 * 1024,
	/* the browse's whole time with Steam */
	BROWSE_TIMEOUT = 15000,
};

unsigned long system_milliseconds(void);

static struct
{
	void *library;
	init_flat_function init_flat;
	bool_function init;
	void_function shutdown;
	void_function run_callbacks;
	bool_function is_steam_running;
	subscribe_function subscribe;
	subscribe_function unsubscribe;
	item_state_function item_state;
	download_info_function download_info;
	download_function download;
	create_query_function create_query;
	query_text_function required_tag;
	query_text_function search_text;
	send_query_function send_query;
	query_result_function query_result;
	release_query_function release_query;
	call_completed_function call_completed;
	call_result_function call_result;
	int completed_size;
	steam_interface ugc;
	steam_interface utils;
	/* the browse under way */
	steam_id64 query;
	steam_id64 call;
	unsigned long browse_time;
} api;

static void *symbol(const char *name)
{
	return steam_bridge_platform_library_symbol(api.library, name);
}

/* the newest of name_format's versions the library has */
static void *versioned(const char *name_format, int newest, int oldest)
{
	char name[64];
	void *found = NULL;

	for (; newest >= oldest && !found; newest--)
	{
		snprintf(name, sizeof(name), name_format, newest);
		found = symbol(name);
	}
	return found;
}

/* the library's functions; 0 if one the subscribe can't do without is missing */
static int bind_functions(void)
{
	api.init_flat = (init_flat_function)symbol("SteamAPI_InitFlat");
	api.init = (bool_function)symbol("SteamAPI_Init");
	if (!api.init)
		api.init = (bool_function)symbol("SteamAPI_InitSafe");
	api.shutdown = (void_function)symbol("SteamAPI_Shutdown");
	api.run_callbacks = (void_function)symbol("SteamAPI_RunCallbacks");
	api.is_steam_running = (bool_function)symbol("SteamAPI_IsSteamRunning");
	api.subscribe = (subscribe_function)symbol("SteamAPI_ISteamUGC_SubscribeItem");
	api.unsubscribe = (subscribe_function)symbol("SteamAPI_ISteamUGC_UnsubscribeItem");
	api.item_state = (item_state_function)symbol("SteamAPI_ISteamUGC_GetItemState");
	api.download_info = (download_info_function)symbol("SteamAPI_ISteamUGC_GetItemDownloadInfo");
	api.download = (download_function)symbol("SteamAPI_ISteamUGC_DownloadItem");
	api.create_query = (create_query_function)symbol("SteamAPI_ISteamUGC_CreateQueryAllUGCRequestPage");
	api.completed_size = QUERY_COMPLETED_SIZE;
	if (!api.create_query)
	{
		api.create_query = (create_query_function)symbol("SteamAPI_ISteamUGC_CreateQueryAllUGCRequest");
		api.completed_size = QUERY_COMPLETED_OLD_SIZE;
	}
	api.required_tag = (query_text_function)symbol("SteamAPI_ISteamUGC_AddRequiredTag");
	api.search_text = (query_text_function)symbol("SteamAPI_ISteamUGC_SetSearchText");
	api.send_query = (send_query_function)symbol("SteamAPI_ISteamUGC_SendQueryUGCRequest");
	api.query_result = (query_result_function)symbol("SteamAPI_ISteamUGC_GetQueryUGCResult");
	api.release_query = (release_query_function)symbol("SteamAPI_ISteamUGC_ReleaseQueryUGCRequest");
	api.call_completed = (call_completed_function)symbol("SteamAPI_ISteamUtils_IsAPICallCompleted");
	api.call_result = (call_result_function)symbol("SteamAPI_ISteamUtils_GetAPICallResult");
	return (api.init_flat || api.init) && api.shutdown && api.subscribe && api.item_state && api.download_info;
}

/* ISteamUGC and ISteamUtils of the started library */
static int find_interfaces(void)
{
	accessor_function ugc = (accessor_function)versioned("SteamAPI_SteamUGC_v%03d", 40, 1);
	accessor_function utils = (accessor_function)versioned("SteamAPI_SteamUtils_v%03d", 30, 1);

	api.ugc = ugc ? ugc() : NULL;
	api.utils = utils ? utils() : NULL;
	if (!api.ugc || !api.utils)
	{
		/* (older libraries: from ISteamClient, by version name) */
		accessor_function client = (accessor_function)symbol("SteamClient");
		handle_function user = (handle_function)symbol("SteamAPI_GetHSteamUser");
		handle_function pipe = (handle_function)symbol("SteamAPI_GetHSteamPipe");
		client_interface_function get_ugc = (client_interface_function)symbol("SteamAPI_ISteamClient_GetISteamUGC");
		client_utils_function get_utils = (client_utils_function)symbol("SteamAPI_ISteamClient_GetISteamUtils");
		steam_interface instance = client ? client() : NULL;
		char version[48];
		int number;

		/* (the version the library was built for: its flat functions call by
		that version's layout. Only libraries whose exports say which are used:
		playtime tracking without DeleteItem is SDK 1.39's ISteamUGC 009) */
		int ugc_version = symbol("SteamAPI_ISteamUGC_StartPlaytimeTracking") &&
			!symbol("SteamAPI_ISteamUGC_DeleteItem") ? 9 : 0;

		if (instance && user && pipe && get_ugc && ugc_version)
		{
			snprintf(version, sizeof(version), "STEAMUGC_INTERFACE_VERSION%03d", ugc_version);
			api.ugc = get_ugc(instance, user(), pipe(), version);
			/* (ISteamUtils' call results sit early and keep their places) */
			for (number = 9; get_utils && !api.utils && number >= 5; number--)
			{
				snprintf(version, sizeof(version), "SteamUtils%03d", number);
				api.utils = get_utils(instance, pipe(), version);
			}
		}
		else if (!api.ugc)
			platform_log("Workshop: this steam_api library's Workshop version is not one the bridge knows");
	}
	return api.ugc != NULL;
}

static int api_connect(void)
{
	int started;

	if (!api.library)
	{
		char (*paths)[STEAM_BRIDGE_PATH_SIZE] = malloc(sizeof(*paths) * STEAM_BRIDGE_MAXIMUM_LIBRARIES);
		int count = paths ? steam_bridge_api_library_paths(paths, STEAM_BRIDGE_MAXIMUM_LIBRARIES, 1) : 0, index;

		for (index = 0; index < count && !api.library; index++)
			api.library = steam_bridge_platform_library_open(paths[index]);
		free(paths);
		if (!api.library)
		{
			platform_log("Workshop: no steam_api library to load (%d found)", count);
			return STEAM_BRIDGE_FAILED;
		}
	}
	if (!bind_functions())
	{
		platform_log("Workshop: the steam_api library lacks a function the bridge needs");
		return STEAM_BRIDGE_FAILED;
	}
	if (api.is_steam_running && !api.is_steam_running())
	{
		platform_log("Workshop: Steam isn't running");
		return STEAM_BRIDGE_FAILED;
	}
	/* (never steam_appid.txt: the environment only, this process's) */
	steam_bridge_platform_set_app_id(MCC_APP_ID);
	if (api.init_flat)
	{
		char message[INIT_MESSAGE_SIZE];
		int result;

		message[0] = 0;
		result = api.init_flat(message);
		message[sizeof(message) - 1] = 0;
		started = result == INIT_OK;
		if (!started)
			platform_log("Workshop: Steam didn't start (%d%s%s)", result, message[0] ? ": " : "", message);
	}
	else
	{
		started = api.init() != 0;
		if (!started)
			platform_log("Workshop: Steam didn't start (not signed in, or MCC not owned)");
	}
	if (!started)
		return STEAM_BRIDGE_FAILED;
	if (!find_interfaces())
	{
		api.shutdown();
		api.utils = NULL;
		platform_log("Workshop: Steam gave no Workshop interface");
		return STEAM_BRIDGE_FAILED;
	}
	api.query = api.call = 0;
	return STEAM_BRIDGE_READY;
}

static void browse_end(void)
{
	if (api.query && api.release_query && api.ugc)
		api.release_query(api.ugc, api.query);
	api.query = api.call = 0;
}

static void api_disconnect(void)
{
	/* (the library stays loaded: the next open uses it again) */
	browse_end();
	if (api.ugc)
	{
		api.ugc = NULL;
		api.utils = NULL;
		api.shutdown();
	}
}

static int api_subscribe(unsigned long long id)
{
	if (!api.ugc || !api.subscribe(api.ugc, id))
		return 0;
	/* (now, not when MCC next starts) */
	if (api.download)
		api.download(api.ugc, id, 1);
	return 1;
}

static int api_unsubscribe(unsigned long long id)
{
	return api.ugc && api.unsubscribe && api.unsubscribe(api.ugc, id) != 0;
}

static int api_query(unsigned long long id, struct steam_bridge_item *item)
{
	unsigned int flags;

	memset(item, 0, sizeof(*item));
	if (!api.ugc)
		return 0;
	flags = api.item_state(api.ugc, id);
	item->subscribed = (flags & ITEM_SUBSCRIBED) != 0;
	if (flags & ITEM_DOWNLOADING)
	{
		steam_id64 downloaded = 0, total = 0;

		item->state = STEAM_BRIDGE_ITEM_DOWNLOADING;
		if (api.download_info(api.ugc, id, &downloaded, &total))
		{
			item->downloaded = downloaded;
			item->total = total;
		}
	}
	else if (flags & ITEM_DOWNLOAD_PENDING)
		item->state = STEAM_BRIDGE_ITEM_QUEUED;
	else if ((flags & ITEM_INSTALLED) && !(flags & ITEM_NEEDS_UPDATE))
		item->state = STEAM_BRIDGE_ITEM_INSTALLED;
	else if (flags & ITEM_SUBSCRIBED)
		item->state = STEAM_BRIDGE_ITEM_QUEUED;
	else
		item->state = STEAM_BRIDGE_ITEM_NONE;
	return 1;
}

static void api_tick(void)
{
	if (api.ugc && api.run_callbacks)
		api.run_callbacks();
}

static int api_browse(const char *search)
{
	browse_end();
	/* (MCC's Workshop mixes all its games: never a list without the Halo1 tag) */
	if (!api.ugc || !api.utils || !api.create_query || !api.required_tag || !api.send_query || !api.query_result ||
		!api.call_completed || !api.call_result)
	{
		return 0;
	}
	api.query = api.create_query(api.ugc, search[0] ? QUERY_TEXT_SEARCH : QUERY_MOST_SUBSCRIBED, MATCHING_ITEMS,
		MCC_CREATOR_APP, MCC_APP, 1);
	/* (0 and ~0 are k_UGCQueryHandleInvalid's) */
	if (!api.query || api.query == ~0ULL)
	{
		api.query = 0;
		return 0;
	}
	if (!api.required_tag(api.ugc, api.query, "Halo1"))
	{
		browse_end();
		return 0;
	}
	if (search[0] && api.search_text)
		api.search_text(api.ugc, api.query, search);
	api.call = api.send_query(api.ugc, api.query);
	if (!api.call)
	{
		browse_end();
		return 0;
	}
	api.browse_time = system_milliseconds();
	return 1;
}

static int api_listings(struct steam_bridge_listing *listings, int maximum)
{
	unsigned char completed[QUERY_COMPLETED_SIZE], failed = 0, *details;
	unsigned int returned, index;
	int result, count = 0;

	if (!api.call)
		return STEAM_BRIDGE_LISTINGS_FAILED;
	if (!api.call_completed(api.utils, api.call, &failed))
	{
		if (system_milliseconds() - api.browse_time < BROWSE_TIMEOUT)
			return STEAM_BRIDGE_LISTINGS_PENDING;
		platform_log("Workshop: Steam's list took too long");
		browse_end();
		return STEAM_BRIDGE_LISTINGS_FAILED;
	}
	memset(completed, 0, sizeof(completed));
	if (failed || !api.call_result(api.utils, api.call, completed, api.completed_size, QUERY_COMPLETED_CALLBACK,
		&failed) || failed)
	{
		browse_end();
		return STEAM_BRIDGE_LISTINGS_FAILED;
	}
	/* (handle, then EResult at 8 and the count returned at 12: the same packed either way) */
	memcpy(&result, completed + 8, sizeof(result));
	memcpy(&returned, completed + 12, sizeof(returned));
	details = result == RESULT_OK ? malloc(DETAILS_BUFFER) : NULL;
	for (index = 0; details && index < returned && count < maximum; index++)
	{
		memset(details, 0, DETAILS_BUFFER);
		if (api.query_result(api.ugc, api.query, index, details) &&
			steam_bridge_read_details(details, &listings[count]))
		{
			count++;
		}
	}
	free(details);
	browse_end();
	if (result != RESULT_OK)
		platform_log("Workshop: Steam's list failed (%d)", result);
	return result == RESULT_OK ? count : STEAM_BRIDGE_LISTINGS_FAILED;
}

const struct steam_bridge_backend steam_bridge_api_backend =
{
	api_connect,
	api_disconnect,
	api_subscribe,
	api_unsubscribe,
	api_query,
	api_tick,
	NULL,
	NULL,
	NULL,
	api_browse,
	api_listings,
};

/* ---------- SteamUGCDetails_t's head */

enum
{
	DETAILS_ID = 0,
	DETAILS_RESULT = 8,
	DETAILS_TITLE = 24,
	DETAILS_TITLE_SIZE = 129,
	DETAILS_DESCRIPTION = DETAILS_TITLE + DETAILS_TITLE_SIZE,
	DETAILS_DESCRIPTION_SIZE = 8000,
};

/* a fixed field's text, cut to size (always ended) */
static void field_text(const unsigned char *field, int field_size, char *out, int size)
{
	int length = 0;

	while (length < field_size && field[length] && length < size - 1)
	{
		/* (control characters as spaces: the list draws one line) */
		out[length] = field[length] < ' ' ? ' ' : (char)field[length];
		length++;
	}
	out[length] = 0;
}

int steam_bridge_read_details(const unsigned char *details, struct steam_bridge_listing *listing)
{
	unsigned long long id;
	int result;

	memcpy(&id, details + DETAILS_ID, sizeof(id));
	memcpy(&result, details + DETAILS_RESULT, sizeof(result));
	memset(listing, 0, sizeof(*listing));
	if (!id || result != RESULT_OK)
		return 0;
	snprintf(listing->id, sizeof(listing->id), "%llu", id);
	field_text(details + DETAILS_TITLE, DETAILS_TITLE_SIZE, listing->title, sizeof(listing->title));
	field_text(details + DETAILS_DESCRIPTION, DETAILS_DESCRIPTION_SIZE, listing->description,
		sizeof(listing->description));
	if (!listing->title[0])
		snprintf(listing->title, sizeof(listing->title), "%s", listing->id);
	return 1;
}
