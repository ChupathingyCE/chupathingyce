/*
STEAM_BRIDGE_API.C

The bridge's client backend (steam_client_bridge.h): a steam_api library
the player already has (MCC's own steam_api64.dll on Windows; elsewhere one
an installed game ships), loaded at run time and called through its flat C
functions, which talk to the running Steam client. Every declaration here
is our own, written from Valve's public Steamworks documentation (the
steam_api, ISteamUGC and ISteamClient pages): no SDK header or
library is in our sources or builds.

- SteamAPI_InitFlat (newer libraries), else SteamAPI_Init or
  SteamAPI_InitSafe (older ones), with SteamAppId set to MCC's 976730 in
  the environment first. It fails without a running, signed-in Steam that
  owns MCC: Steam itself checks.
- ISteamUGC from the versioned accessor of newer libraries
  (SteamAPI_SteamUGC_v0NN), else from SteamClient()'s GetISteamUGC with the
  version name the library was built for.
- SubscribeItem + DownloadItem (at once), UnsubscribeItem, GetItemState
  and GetItemDownloadInfo; SteamAPI_RunCallbacks each turn;
  SteamAPI_Shutdown when the tab closes.
The list is not asked of the client: it is Steam's public Workshop page,
the same on every computer (workshop_browser.c). A missing function fails
the connect, and the tab falls back.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h): a console builds this to nothing */

#include "steam_client_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);

#define MCC_APP_ID "976730"

typedef unsigned long long steam_id64;
typedef void *steam_interface;

/* the flat functions (S_CALLTYPE is __cdecl: C's own) */
typedef int (*init_flat_function)(char *error_message);
typedef unsigned char (*bool_function)(void);
typedef void (*void_function)(void);
typedef steam_interface (*accessor_function)(void);
typedef int (*handle_function)(void);
typedef steam_interface (*client_interface_function)(steam_interface client, int user, int pipe, const char *version);
typedef steam_id64 (*subscribe_function)(steam_interface ugc, steam_id64 item);
typedef unsigned int (*item_state_function)(steam_interface ugc, steam_id64 item);
typedef unsigned char (*download_info_function)(steam_interface ugc, steam_id64 item, steam_id64 *downloaded,
	steam_id64 *total);
typedef unsigned char (*download_function)(steam_interface ugc, steam_id64 item, unsigned char high_priority);
typedef steam_interface (*client_user_function)(steam_interface client, int user, int pipe, const char *version);
typedef steam_interface (*client_utils_function)(steam_interface client, int pipe, const char *version);
typedef unsigned char (*logged_on_function)(steam_interface user);
typedef unsigned char (*call_completed_function)(steam_interface utils, steam_id64 call, unsigned char *failed);
typedef unsigned char (*call_result_function)(steam_interface utils, steam_id64 call, void *result, int size,
	int callback, unsigned char *failed);
typedef void (*pipe_function)(int pipe);
typedef unsigned char (*next_callback_function)(int pipe, void *message);
typedef unsigned char (*dispatch_result_function)(int pipe, steam_id64 call, void *result, int size, int callback,
	unsigned char *failed);

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
	/* RemoteStorageSubscribePublishedFileResult_t and its unsubscribe twin
	(k_iSteamRemoteStorageCallbacks + 13, + 15): EResult, then the item's
	id, at 8 with Windows' packing and at 4 with the others' */
	SUBSCRIBE_RESULT_CALLBACK = 1313,
	UNSUBSCRIBE_RESULT_CALLBACK = 1315,
#ifdef _WIN32
	SUBSCRIBE_RESULT_SIZE = 16,
#else
	SUBSCRIBE_RESULT_SIZE = 12,
#endif
	/* EResult's OK */
	RESULT_OK = 1,
	MAXIMUM_PENDING = 32,
	/* a call Steam never answers, given up after this many turns (about a minute) */
	PENDING_TURNS = 120,
	/* CallbackMsg_t, room for it */
	CALLBACK_MESSAGE_SIZE = 32,
};


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
	/* sign-on and call results (ISteamUser, ISteamUtils), or the manual
	dispatch of newer libraries */
	client_user_function get_user;
	logged_on_function logged_on;
	call_completed_function call_completed;
	call_result_function call_result;
	void_function dispatch_init;
	pipe_function dispatch_run_frame;
	next_callback_function dispatch_next;
	pipe_function dispatch_free_last;
	dispatch_result_function dispatch_result;
	handle_function get_pipe;
	int manual;
	int pipe;
	steam_interface ugc;
	steam_interface user;
	steam_interface utils;
	/* Steam isn't signed on (the last connect found so) */
	int offline;
	/* the async calls waiting on Steam's answer, and the items it refused */
	int pending_count;
	struct
	{
		steam_id64 call;
		steam_id64 item;
		int callback;
		int turns;
	} pending[MAXIMUM_PENDING];
	int refused_count;
	steam_id64 refused[MAXIMUM_PENDING];
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
	api.logged_on = (logged_on_function)symbol("SteamAPI_ISteamUser_BLoggedOn");
	api.call_completed = (call_completed_function)symbol("SteamAPI_ISteamUtils_IsAPICallCompleted");
	api.call_result = (call_result_function)symbol("SteamAPI_ISteamUtils_GetAPICallResult");
	api.dispatch_init = (void_function)symbol("SteamAPI_ManualDispatch_Init");
	api.dispatch_run_frame = (pipe_function)symbol("SteamAPI_ManualDispatch_RunFrame");
	api.dispatch_next = (next_callback_function)symbol("SteamAPI_ManualDispatch_GetNextCallback");
	api.dispatch_free_last = (pipe_function)symbol("SteamAPI_ManualDispatch_FreeLastCallback");
	api.dispatch_result = (dispatch_result_function)symbol("SteamAPI_ManualDispatch_GetAPICallResult");
	api.get_pipe = (handle_function)symbol("SteamAPI_GetHSteamPipe");
	/* (manual dispatch when the library has it whole: every answer is ours to take) */
	api.manual = api.dispatch_init && api.dispatch_run_frame && api.dispatch_next && api.dispatch_free_last &&
		api.dispatch_result && api.get_pipe;
	return (api.init_flat || api.init) && api.shutdown && api.subscribe && api.item_state && api.download_info &&
		api.logged_on;
}

/* ISteamUGC of the started library */
static int find_interfaces(void)
{
	accessor_function ugc = (accessor_function)versioned("SteamAPI_SteamUGC_v%03d", 40, 1);

	api.ugc = ugc ? ugc() : NULL;
	if (!api.ugc)
	{
		/* (older libraries: from ISteamClient, by version name) */
		accessor_function client = (accessor_function)symbol("SteamClient");
		handle_function user = (handle_function)symbol("SteamAPI_GetHSteamUser");
		handle_function pipe = (handle_function)symbol("SteamAPI_GetHSteamPipe");
		client_interface_function get_ugc = (client_interface_function)symbol("SteamAPI_ISteamClient_GetISteamUGC");
		steam_interface instance = client ? client() : NULL;
		char version[48];

		/* (the version the library was built for: its flat functions call by
		that version's layout. Only libraries whose exports say which are used:
		playtime tracking without DeleteItem is SDK 1.39's ISteamUGC 009) */
		int ugc_version = symbol("SteamAPI_ISteamUGC_StartPlaytimeTracking") &&
			!symbol("SteamAPI_ISteamUGC_DeleteItem") ? 9 : 0;

		if (instance && user && pipe && get_ugc && ugc_version)
		{
			snprintf(version, sizeof(version), "STEAMUGC_INTERFACE_VERSION%03d", ugc_version);
			api.ugc = get_ugc(instance, user(), pipe(), version);
		}
		else
			platform_log("Workshop: this steam_api library's Workshop version is not one the bridge knows");
	}
	{
		accessor_function user = (accessor_function)versioned("SteamAPI_SteamUser_v%03d", 40, 1);
		accessor_function utils = (accessor_function)versioned("SteamAPI_SteamUtils_v%03d", 30, 1);

		api.user = user ? user() : NULL;
		api.utils = utils ? utils() : NULL;
	}
	if (!api.user || !api.utils)
	{
		/* (older libraries: ISteamUser's BLoggedOn and ISteamUtils' call
		results sit early in them and keep their places in every version) */
		accessor_function client = (accessor_function)symbol("SteamClient");
		handle_function user = (handle_function)symbol("SteamAPI_GetHSteamUser");
		handle_function pipe = (handle_function)symbol("SteamAPI_GetHSteamPipe");
		client_user_function get_user = (client_user_function)symbol("SteamAPI_ISteamClient_GetISteamUser");
		client_utils_function get_utils = (client_utils_function)symbol("SteamAPI_ISteamClient_GetISteamUtils");
		steam_interface instance = client ? client() : NULL;
		char version[48];
		int number;

		for (number = 23; instance && user && pipe && get_user && !api.user && number >= 17; number--)
		{
			snprintf(version, sizeof(version), "SteamUser%03d", number);
			api.user = get_user(instance, user(), pipe(), version);
		}
		for (number = 10; instance && pipe && get_utils && !api.utils && number >= 5; number--)
		{
			snprintf(version, sizeof(version), "SteamUtils%03d", number);
			api.utils = get_utils(instance, pipe(), version);
		}
	}
	return api.ugc != NULL && api.user != NULL && (api.manual || (api.utils && api.call_completed && api.call_result));
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
	/* (manual dispatch is asked for before SteamAPI_Init, as its page says) */
	if (api.manual)
		api.dispatch_init();
	api.pending_count = api.refused_count = 0;
	api.offline = 0;
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
		api.ugc = NULL;
		platform_log("Workshop: Steam gave no Workshop interface");
		return STEAM_BRIDGE_FAILED;
	}
	api.pipe = api.get_pipe ? api.get_pipe() : 0;
	/* (a Steam that is running but offline answers nothing: Steam offline) */
	if (!api.logged_on(api.user))
	{
		api.offline = 1;
		api.shutdown();
		api.ugc = NULL;
		platform_log("Workshop: Steam is offline");
		return STEAM_BRIDGE_FAILED;
	}
	return STEAM_BRIDGE_READY;
}

static void api_disconnect(void)
{
	/* (the library stays loaded: the next open uses it again) */
	if (api.ugc)
	{
		api.ugc = NULL;
		api.shutdown();
	}
}

/* an async call's answer waited on (api_tick) */
static void pending_add(steam_id64 call, steam_id64 item, int callback)
{
	if (!call || api.pending_count >= MAXIMUM_PENDING)
		return;
	api.pending[api.pending_count].call = call;
	api.pending[api.pending_count].item = item;
	api.pending[api.pending_count].callback = callback;
	api.pending[api.pending_count].turns = 0;
	api.pending_count++;
}

static void refused_set(steam_id64 item, int refused)
{
	int index;

	for (index = 0; index < api.refused_count && api.refused[index] != item; index++)
		;
	if (refused && index == api.refused_count && api.refused_count < MAXIMUM_PENDING)
		api.refused[api.refused_count++] = item;
	else if (!refused && index < api.refused_count)
		api.refused[index] = api.refused[--api.refused_count];
}

static int refused(steam_id64 item)
{
	int index;

	for (index = 0; index < api.refused_count; index++)
	{
		if (api.refused[index] == item)
			return 1;
	}
	return 0;
}

static int api_subscribe(unsigned long long id)
{
	steam_id64 call;

	if (!api.ugc)
		return 0;
	/* (signed on still: an offline Steam takes the call and never answers) */
	if (!api.logged_on(api.user))
	{
		api.offline = 1;
		refused_set(id, 1);
		return 0;
	}
	refused_set(id, 0);
	call = api.subscribe(api.ugc, id);
	if (!call)
	{
		refused_set(id, 1);
		return 0;
	}
	pending_add(call, id, SUBSCRIBE_RESULT_CALLBACK);
	/* (now, not when MCC next starts) */
	if (api.download)
		api.download(api.ugc, id, 1);
	return 1;
}

static int api_unsubscribe(unsigned long long id)
{
	steam_id64 call;

	if (!api.ugc || !api.unsubscribe || !api.logged_on(api.user) || !(call = api.unsubscribe(api.ugc, id)))
		return 0;
	pending_add(call, id, UNSUBSCRIBE_RESULT_CALLBACK);
	return 1;
}

/* UPDATE: Steam asked to fetch it again, at once */
static int api_update(unsigned long long id)
{
	return api.ugc && api.download && api.logged_on(api.user) && api.download(api.ugc, id, 1);
}

static int api_query(unsigned long long id, struct steam_bridge_item *item)
{
	unsigned int flags;

	memset(item, 0, sizeof(*item));
	if (!api.ugc)
		return 0;
	/* (Steam said no to its subscribe: the call's own answer) */
	if (refused(id))
	{
		item->state = STEAM_BRIDGE_ITEM_FAILED;
		return 1;
	}
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

/* one waiting call's answer: 1 and its EResult once it came */
static int pending_answer(int index, int *result)
{
	unsigned char answer[SUBSCRIBE_RESULT_SIZE + 16], failed = 0;
	int got;

	memset(answer, 0, sizeof(answer));
	if (api.manual)
		got = api.dispatch_result(api.pipe, api.pending[index].call, answer, SUBSCRIBE_RESULT_SIZE,
			api.pending[index].callback, &failed);
	else
	{
		if (!api.call_completed(api.utils, api.pending[index].call, &failed))
			return 0;
		got = api.call_result(api.utils, api.pending[index].call, answer, SUBSCRIBE_RESULT_SIZE,
			api.pending[index].callback, &failed);
	}
	if (!got)
		return 0;
	memcpy(result, answer, sizeof(*result));
	if (failed)
		*result = 0;
	return 1;
}

static void api_tick(void)
{
	int index;

	if (!api.ugc)
		return;
	if (api.manual)
	{
		unsigned char message[CALLBACK_MESSAGE_SIZE];

		/* (every callback taken and let go: the calls' answers are asked for by handle) */
		api.dispatch_run_frame(api.pipe);
		while (api.dispatch_next(api.pipe, message))
			api.dispatch_free_last(api.pipe);
	}
	else if (api.run_callbacks)
		api.run_callbacks();
	for (index = 0; index < api.pending_count; )
	{
		int result = 0;

		if (pending_answer(index, &result) || ++api.pending[index].turns > PENDING_TURNS)
		{
			if (result != RESULT_OK)
			{
				platform_log("Workshop: Steam answered %d for item %llu", result, api.pending[index].item);
				if (api.pending[index].callback == SUBSCRIBE_RESULT_CALLBACK)
					refused_set(api.pending[index].item, 1);
			}
			api.pending[index] = api.pending[--api.pending_count];
		}
		else
			index++;
	}
}

int steam_bridge_api_offline(void)
{
	return api.offline;
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
	/* (REMOVE: the client's is its unsubscribe) */
	api_unsubscribe,
	api_update,
	NULL,
};
#endif /* HALO_MCC */
