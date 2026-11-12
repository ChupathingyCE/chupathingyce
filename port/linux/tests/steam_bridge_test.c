/*
STEAM_BRIDGE_TEST.C

Tests of the Steam bridge (port/linux/src/steam_client_bridge.h) with a
fake steam_api library and fake platform: every failure falls back (status
FAILED, nothing asked of Steam), the app id goes only into the environment,
Steam is shut down when the tab closes, subscribe/state/progress map to the
tab's words, each call's own answer is waited on (Steam refusing a
subscribe shows; manual dispatch asked for before SteamAPI_Init), a Steam
that isn't signed on is offline and asked nothing, and steamcmd's text is
read right. Built and run by tools/test_steam_bridge.py.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/

#include "../src/steam_client_bridge.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void steam_bridge_test_reset(void);
int steam_bridge_test_running(void);

static int failures;

#define CHECK(condition) do { if (!(condition)) { failures++; \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

/* ---------- the game's functions */

static int setting_enabled = 1;

int config_boolean(const char *name)
{
	return !strcmp(name, "game.workshop_steam") ? setting_enabled : 0;
}

const char *config_string(const char *name)
{
	(void)name;
	return "";
}

unsigned long system_milliseconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long)now.tv_sec * 1000UL + (unsigned long)(now.tv_nsec / 1000000L);
}

void platform_log(const char *format, ...)
{
	(void)format;
}

/* ---------- the fake library */

static struct
{
	int present;
	/* the symbols left out */
	const char *missing[4];
	int init_result;
	int init_delay;
	int init_calls;
	int shutdown_calls;
	int subscribe_calls;
	int download_calls;
	int unsubscribe_calls;
	unsigned long long last_item;
	unsigned int flags;
	unsigned long long downloaded, total;
	char app_id[16];
	/* signed on; the subscribe call's answer (EResult); manual dispatch offered,
	and whether it was asked for before SteamAPI_Init */
	int logged_on;
	int subscribe_result;
	int manual;
	int manual_before_init;
	int manual_inits;
	int next_callbacks;
} fake;

static int fake_ugc_object, fake_utils_object;

static unsigned char fake_init(void)
{
	const char *app = getenv("SteamAppId");

	fake.manual_before_init = fake.manual_inits > 0;
	fake.init_calls++;
	snprintf(fake.app_id, sizeof(fake.app_id), "%s", app ? app : "");
	if (fake.init_delay)
		usleep((useconds_t)fake.init_delay * 1000);
	return (unsigned char)fake.init_result;
}

static void fake_shutdown(void) { fake.shutdown_calls++; }
static void fake_run_callbacks(void) { }
static unsigned char fake_running(void) { return 1; }
static void *fake_ugc(void) { return &fake_ugc_object; }
static void *fake_utils(void) { return &fake_utils_object; }

static unsigned long long fake_subscribe(void *ugc, unsigned long long item)
{
	(void)ugc;
	fake.subscribe_calls++;
	fake.last_item = item;
	fake.flags = 1 | 32;
	return 7;
}

static unsigned long long fake_unsubscribe(void *ugc, unsigned long long item)
{
	(void)ugc;
	(void)item;
	fake.unsubscribe_calls++;
	fake.flags = 0;
	return 8;
}

static unsigned int fake_item_state(void *ugc, unsigned long long item)
{
	(void)ugc;
	return item == fake.last_item ? fake.flags : 0;
}

static unsigned char fake_download_info(void *ugc, unsigned long long item, unsigned long long *downloaded,
	unsigned long long *total)
{
	(void)ugc;
	(void)item;
	*downloaded = fake.downloaded;
	*total = fake.total;
	return 1;
}

static unsigned char fake_download(void *ugc, unsigned long long item, unsigned char high)
{
	(void)ugc;
	(void)item;
	(void)high;
	fake.download_calls++;
	return 1;
}


static int fake_user_object;
static void *fake_user(void) { return &fake_user_object; }
static unsigned char fake_logged_on(void *user) { (void)user; return (unsigned char)fake.logged_on; }

/* the subscribe's answer: RemoteStorageSubscribePublishedFileResult_t (EResult first) */
static int call_answer(unsigned long long call, void *result, int size, int callback)
{
	if (call != 7 || (callback != 1313 && callback != 1315) || size < 12)
		return 0;
	memset(result, 0, (size_t)size);
	memcpy(result, &fake.subscribe_result, 4);
	return 1;
}

static unsigned char fake_completed(void *utils, unsigned long long call, unsigned char *failed)
{
	(void)utils;
	*failed = 0;
	return call == 7;
}

static unsigned char fake_utils_result(void *utils, unsigned long long call, void *result, int size, int callback,
	unsigned char *failed)
{
	(void)utils;
	*failed = 0;
	return (unsigned char)call_answer(call, result, size, callback);
}

static void fake_dispatch_init(void) { fake.manual_inits++; }
static void fake_run_frame(int pipe) { (void)pipe; }
static unsigned char fake_next(int pipe, void *message) { (void)pipe; (void)message; return 0; }
static void fake_free_last(int pipe) { (void)pipe; }
static int fake_pipe(void) { return 1; }

static unsigned char fake_dispatch_result(int pipe, unsigned long long call, void *result, int size, int callback,
	unsigned char *failed)
{
	(void)pipe;
	*failed = 0;
	return (unsigned char)call_answer(call, result, size, callback);
}

static int is_missing(const char *name)
{
	int index;

	for (index = 0; index < 4; index++)
	{
		if (fake.missing[index] && !strcmp(fake.missing[index], name))
			return 1;
	}
	return 0;
}

void *steam_bridge_platform_library_open(const char *path)
{
	return fake.present && !strcmp(path, "fake_steam_api") ? &fake : NULL;
}

void *steam_bridge_platform_library_symbol(void *library, const char *name)
{
	static const struct { const char *name; void *function; } table[] =
	{
		{ "SteamAPI_Init", (void *)fake_init },
		{ "SteamAPI_Shutdown", (void *)fake_shutdown },
		{ "SteamAPI_RunCallbacks", (void *)fake_run_callbacks },
		{ "SteamAPI_IsSteamRunning", (void *)fake_running },
		{ "SteamAPI_SteamUGC_v020", (void *)fake_ugc },
		{ "SteamAPI_SteamUtils_v010", (void *)fake_utils },
		{ "SteamAPI_ISteamUGC_SubscribeItem", (void *)fake_subscribe },
		{ "SteamAPI_ISteamUGC_UnsubscribeItem", (void *)fake_unsubscribe },
		{ "SteamAPI_ISteamUGC_GetItemState", (void *)fake_item_state },
		{ "SteamAPI_ISteamUGC_GetItemDownloadInfo", (void *)fake_download_info },
		{ "SteamAPI_ISteamUGC_DownloadItem", (void *)fake_download },
		{ "SteamAPI_ISteamUtils_IsAPICallCompleted", (void *)fake_completed },
		{ "SteamAPI_ISteamUtils_GetAPICallResult", (void *)fake_utils_result },
		{ "SteamAPI_SteamUser_v021", (void *)fake_user },
		{ "SteamAPI_ISteamUser_BLoggedOn", (void *)fake_logged_on },
		{ "SteamAPI_GetHSteamPipe", (void *)fake_pipe },
	};
	static const struct { const char *name; void *function; } manual[] =
	{
		{ "SteamAPI_ManualDispatch_Init", (void *)fake_dispatch_init },
		{ "SteamAPI_ManualDispatch_RunFrame", (void *)fake_run_frame },
		{ "SteamAPI_ManualDispatch_GetNextCallback", (void *)fake_next },
		{ "SteamAPI_ManualDispatch_FreeLastCallback", (void *)fake_free_last },
		{ "SteamAPI_ManualDispatch_GetAPICallResult", (void *)fake_dispatch_result },
	};
	int index;

	if (library != &fake || is_missing(name))
		return NULL;
	for (index = 0; index < (int)(sizeof(table) / sizeof(table[0])); index++)
	{
		if (!strcmp(table[index].name, name))
			return table[index].function;
	}
	for (index = 0; fake.manual && index < (int)(sizeof(manual) / sizeof(manual[0])); index++)
	{
		if (!strcmp(manual[index].name, name))
			return manual[index].function;
	}
	return NULL;
}

void steam_bridge_platform_set_app_id(const char *app_id)
{
	setenv("SteamAppId", app_id, 1);
}

void steam_bridge_platform_sleep(int milliseconds)
{
	usleep((useconds_t)milliseconds * 1000);
}

int steam_bridge_api_library_paths(char (*paths)[STEAM_BRIDGE_PATH_SIZE], int maximum, int search)
{
	(void)search;
	if (!fake.present || maximum < 1)
		return 0;
	snprintf(paths[0], STEAM_BRIDGE_PATH_SIZE, "fake_steam_api");
	return 1;
}

int steam_bridge_steam_present(void) { return 0; }

/* (no steamcmd in these tests: the client backend alone) */
int steam_bridge_platform_find_steamcmd(char *path, int size) { (void)path; (void)size; return 0; }
int steam_bridge_platform_steamcmd_root(const char *steamcmd, char *root, int size)
{
	(void)steamcmd; (void)root; (void)size; return 0;
}
void *steam_bridge_platform_child_start(const char *const *arguments) { (void)arguments; return NULL; }
int steam_bridge_platform_child_line(void *child, char *line, int size, int milliseconds)
{
	(void)child; (void)line; (void)size; (void)milliseconds; return -1;
}
void steam_bridge_platform_child_end(void *child) { (void)child; }
int steam_bridge_platform_open_sign_in(const char *steamcmd) { (void)steamcmd; return 0; }
int steam_bridge_platform_merge_folder(const char *from, const char *to) { (void)from; (void)to; return 0; }
int steam_bridge_platform_remove_folder(const char *path) { (void)path; return 0; }
int steam_bridge_platform_is_folder(const char *path) { (void)path; return 0; }

/* ---------- helpers */

static void reset(void)
{
	int waited = 0;

	steam_bridge_close();
	while (steam_bridge_test_running() && waited++ < 400)
		usleep(10000);
	steam_bridge_test_reset();
	memset(&fake, 0, sizeof(fake));
	fake.present = 1;
	fake.init_result = 1;
	fake.logged_on = 1;
	fake.subscribe_result = 1;
	setting_enabled = 1;
	unsetenv("SteamAppId");
}

/* the status once it isn't CONNECTING (or the timeout's FAILED) */
static int settled(void)
{
	int status, waited = 0;

	while ((status = steam_bridge_status()) == STEAM_BRIDGE_CONNECTING && waited++ < 1500)
		usleep(10000);
	return status;
}

static int item_when(const char *id, int state, struct steam_bridge_item *item)
{
	int waited = 0;

	while (waited++ < 300)
	{
		steam_bridge_item(id, item);
		if (item->state == state)
			return 1;
		usleep(10000);
	}
	return 0;
}

/* ---------- tests */

static void test_fallbacks(void)
{
	struct steam_bridge_item item;

	/* off: nothing starts */
	reset();
	setting_enabled = 0;
	steam_bridge_open(0);
	CHECK(steam_bridge_status() == STEAM_BRIDGE_OFF);
	CHECK(!steam_bridge_test_running());
	CHECK(!steam_bridge_subscribe("123"));

	/* no library */
	reset();
	fake.present = 0;
	steam_bridge_open(0);
	CHECK(settled() == STEAM_BRIDGE_FAILED);
	CHECK(!steam_bridge_subscribe("123"));
	CHECK(fake.init_calls == 0);

	/* a function the subscribe needs is missing */
	reset();
	fake.missing[0] = "SteamAPI_ISteamUGC_GetItemState";
	steam_bridge_open(0);
	CHECK(settled() == STEAM_BRIDGE_FAILED);
	CHECK(fake.init_calls == 0);

	/* Steam won't start (not running, not signed in, MCC not owned) */
	reset();
	fake.init_result = 0;
	steam_bridge_open(0);
	CHECK(settled() == STEAM_BRIDGE_FAILED);
	CHECK(fake.init_calls == 1);
	CHECK(!steam_bridge_item("123", &item) && item.state == STEAM_BRIDGE_ITEM_UNKNOWN);

	/* no ISteamUGC */
	reset();
	fake.missing[0] = "SteamAPI_SteamUGC_v020";
	steam_bridge_open(0);
	CHECK(settled() == STEAM_BRIDGE_FAILED);
	CHECK(fake.shutdown_calls == 1);

	/* a hung SteamAPI_Init: the tab falls back at the timeout; Steam shut down when it returns */
	reset();
	fake.init_delay = STEAM_BRIDGE_CONNECT_TIMEOUT + 1500;
	steam_bridge_open(0);
	CHECK(settled() == STEAM_BRIDGE_FAILED);
	CHECK(!steam_bridge_subscribe("123"));
	{
		int waited = 0;

		while (steam_bridge_test_running() && waited++ < 400)
			usleep(10000);
	}
	CHECK(!steam_bridge_test_running());
	CHECK(fake.shutdown_calls == 1);
	CHECK(steam_bridge_status() == STEAM_BRIDGE_FAILED);
	/* (closing forgets the failure: the next open tries again) */
	steam_bridge_close();
	CHECK(steam_bridge_status() == STEAM_BRIDGE_OFF);
}

/* Steam running but offline: no Workshop call made, "Steam offline" */
static void test_offline(void)
{
	reset();
	fake.logged_on = 0;
	steam_bridge_open(0);
	CHECK(settled() == STEAM_BRIDGE_FAILED);
	CHECK(steam_bridge_offline());
	CHECK(fake.shutdown_calls == 1);
	CHECK(!steam_bridge_subscribe("123"));
	CHECK(fake.subscribe_calls == 0);
}

/* the subscribe's own answer: Steam refusing it shows as failed */
static void test_subscribe_answers(int manual)
{
	struct steam_bridge_item item;
	int waited = 0;

	reset();
	fake.manual = manual;
	fake.subscribe_result = 2;
	steam_bridge_open(0);
	CHECK(settled() == STEAM_BRIDGE_READY);
	CHECK(!steam_bridge_offline());
	/* (manual dispatch asked for before SteamAPI_Init, when the library has it) */
	CHECK(manual ? fake.manual_before_init : !fake.manual_inits);
	CHECK(steam_bridge_subscribe("2906110850"));
	CHECK(item_when("2906110850", STEAM_BRIDGE_ITEM_FAILED, &item));
	/* (signed off since: the call isn't made) */
	fake.logged_on = 0;
	CHECK(steam_bridge_subscribe("3496468566"));
	CHECK(item_when("3496468566", STEAM_BRIDGE_ITEM_FAILED, &item));
	CHECK(fake.subscribe_calls == 1);
	/* (an answer of OK: subscribed) */
	fake.logged_on = 1;
	fake.subscribe_result = 1;
	CHECK(steam_bridge_subscribe("2906110850"));
	while (fake.subscribe_calls < 2 && waited++ < 300)
		usleep(10000);
	CHECK(item_when("2906110850", STEAM_BRIDGE_ITEM_QUEUED, &item) && item.subscribed);
	reset();
}

static void test_session(void)
{
	struct steam_bridge_item item;
	int waited = 0;

	reset();
	unlink("steam_appid.txt");
	steam_bridge_open(0);
	CHECK(settled() == STEAM_BRIDGE_READY);
	CHECK(!strcmp(fake.app_id, "976730"));
	CHECK(access("steam_appid.txt", F_OK) != 0);
	CHECK(!steam_bridge_subscribe("12a"));
	CHECK(!steam_bridge_subscribe("0"));
	CHECK(steam_bridge_subscribe("2886943774"));
	while (!fake.subscribe_calls && waited++ < 300)
		usleep(10000);
	waited = 0;
	CHECK(item_when("2886943774", STEAM_BRIDGE_ITEM_QUEUED, &item) && item.subscribed);
	CHECK(fake.subscribe_calls == 1 && fake.download_calls == 1 && fake.last_item == 2886943774ULL);
	fake.downloaded = 25;
	fake.total = 100;
	fake.flags = 1 | 16;
	CHECK(item_when("2886943774", STEAM_BRIDGE_ITEM_DOWNLOADING, &item));
	CHECK(item.downloaded == 25 && item.total == 100);
	fake.flags = 1 | 4 | 8;
	CHECK(item_when("2886943774", STEAM_BRIDGE_ITEM_QUEUED, &item));
	fake.flags = 1 | 4;
	CHECK(item_when("2886943774", STEAM_BRIDGE_ITEM_INSTALLED, &item));
	/* (UPDATE and REMOVE through the backend that holds it: the client's
	DownloadItem again, and its unsubscribe) */
	CHECK(steam_bridge_update("2886943774"));
	while (fake.download_calls < 2 && waited++ < 300)
		usleep(10000);
	CHECK(fake.download_calls == 2);
	waited = 0;
	CHECK(steam_bridge_removable("2886943774"));
	CHECK(!steam_bridge_removable("123"));
	CHECK(steam_bridge_can_unsubscribe());
	CHECK(steam_bridge_remove("2886943774"));
	while (!fake.unsubscribe_calls && waited++ < 300)
		usleep(10000);
	CHECK(fake.unsubscribe_calls == 1);
	waited = 0;
	CHECK(steam_bridge_unsubscribe("2886943774"));
	while (fake.unsubscribe_calls < 2 && waited++ < 300)
		usleep(10000);
	waited = 0;
	CHECK(item_when("2886943774", STEAM_BRIDGE_ITEM_NONE, &item));
	CHECK(fake.unsubscribe_calls == 2 && !item.subscribed);
	/* steamcmd's calls without steamcmd */
	CHECK(!steam_bridge_install_mcc());
	CHECK(!steam_bridge_sign_in_needed());
	/* the tab closes: Steam shut down, the thread gone */
	steam_bridge_close();
	while (steam_bridge_test_running() && waited++ < 400)
		usleep(10000);
	CHECK(!steam_bridge_test_running());
	CHECK(fake.shutdown_calls == 1);
	CHECK(steam_bridge_status() == STEAM_BRIDGE_OFF);
	CHECK(!steam_bridge_subscribe("2886943774"));
}

static void test_steamcmd_text(void)
{
	struct steamcmd_line line;
	char name[64];
	const char *config = "\"InstallConfigStore\"\n{\n\t\"Software\"\n\t{\n\t\t\"Valve\"\n\t\t{\n\t\t\t\"Steam\"\n"
		"\t\t\t{\n\t\t\t\t\"Accounts\"\n\t\t\t\t{\n\t\t\t\t\t\"player_one\"\n\t\t\t\t\t{\n"
		"\t\t\t\t\t\t\"SteamID\"\t\t\"1\"\n\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n";

	steamcmd_read_line("Success. Downloaded item 2886943774 to \"/Users/x/Library/Application Support/Steam/"
		"steamapps/workshop/content/976730/2886943774\" (2265580503 bytes)", &line);
	CHECK(line.kind == STEAMCMD_LINE_ITEM_DONE && !strcmp(line.id, "2886943774"));
	CHECK(strstr(line.path, "content/976730/2886943774") != NULL);
	steamcmd_read_line("ERROR! Download item 2886943774 failed (Failure).", &line);
	CHECK(line.kind == STEAMCMD_LINE_ITEM_FAILED && !strcmp(line.id, "2886943774"));
	steamcmd_read_line("Depot download complete : \"/x/steamapps/content/app_976730/depot_976733\" (12 files)", &line);
	CHECK(line.kind == STEAMCMD_LINE_DEPOT_DONE && !strcmp(line.path, "/x/steamapps/content/app_976730/depot_976733"));
	steamcmd_read_line("Cached credentials not found.", &line);
	CHECK(line.kind == STEAMCMD_LINE_SIGN_IN);
	steamcmd_read_line("FAILED (Invalid Password)", &line);
	CHECK(line.kind == STEAMCMD_LINE_SIGN_IN);
	steamcmd_read_line("Steam Guard code:", &line);
	CHECK(line.kind == STEAMCMD_LINE_SIGN_IN);
	steamcmd_read_line(" Update state (0x61) downloading, progress: 42.17 (1 / 2)", &line);
	CHECK(line.kind == STEAMCMD_LINE_PROGRESS && line.percent == 42);
	steamcmd_read_line("  7.5% done", &line);
	CHECK(line.kind == STEAMCMD_LINE_PROGRESS && line.percent == 7);
	steamcmd_read_line("Loading Steam API...OK", &line);
	CHECK(line.kind == STEAMCMD_LINE_NOTHING);
	CHECK(steamcmd_read_account(config, (int)strlen(config), name, sizeof(name)) && !strcmp(name, "player_one"));
	CHECK(!steamcmd_read_account("\"Accounts\"\n{\n}\n", 15, name, sizeof(name)));
	CHECK(steamcmd_account_valid("player_one"));
	CHECK(!steamcmd_account_valid("a b"));
	CHECK(!steamcmd_account_valid("-x"));
	CHECK(!steamcmd_account_valid("+quit"));
}

int main(void)
{
	test_steamcmd_text();
	test_fallbacks();
	test_session();
	test_offline();
	test_subscribe_answers(0);
	test_subscribe_answers(1);
	if (failures)
		fprintf(stderr, "%d failed\n", failures);
	else
		printf("steam bridge: all passed\n");
	return failures != 0;
}
