/*
STEAM_CLIENT_BRIDGE.C

The bridge's core (steam_client_bridge.h): its thread, its two backends
(the Steam client's, and steamcmd as the downloader where the client won't
download), and what the Workshop tab reads. The thread is the only one
calling the backends; the tab's calls only take the lock, copy and return,
so the frame never waits on Steam or steamcmd.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h): a console builds this to nothing */

#include "steam_client_bridge.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int config_boolean(const char *name);
unsigned long system_milliseconds(void);
void platform_log(const char *format, ...);

enum
{
	CLIENT_BIT = 1 << STEAM_BRIDGE_BACKEND_CLIENT,
	STEAMCMD_BIT = 1 << STEAM_BRIDGE_BACKEND_STEAMCMD,
};

struct watched_item
{
	char id[STEAM_BRIDGE_ID_SIZE];
	unsigned long long value;
	/* the tab's ask: 1 subscribe, 2 unsubscribe, 3 remove, 4 update, 5 cancel; taken by the thread */
	int want;
	/* when the tab last asked about it (system_milliseconds) */
	unsigned long seen;
	struct steam_bridge_item state;
};

/* (bridge's lock, apart from it: its initializer is not zeros, and
would put the whole of it in the data segment instead of bss) */
static pthread_mutex_t bridge_lock = PTHREAD_MUTEX_INITIALIZER;

static struct
{
	int backend_looked;
	int backend;
	/* the thread runs; it was told to stop; it is stopping; open again after */
	int running;
	int stop;
	int stopping;
	int reopen;
	int use_steamcmd;
	int status;
	/* CLIENT_BIT, STEAMCMD_BIT */
	int connected;
	int sign_in;
	unsigned long connect_time;
	int count;
	struct watched_item items[STEAM_BRIDGE_MAXIMUM_ITEMS];
	int install_wanted;
	int mcc_state;
} bridge;

/* the items this game subscribed to in the Steam client that weren't
subscribed before (only the worker reads or writes these): a cancel
unsubscribes only these, never a subscription the player made in Steam */
static unsigned long long subscribed_here[STEAM_BRIDGE_MAXIMUM_ITEMS];
static int subscribed_here_count;

/* ---------- private code */

static void subscribed_here_add(unsigned long long value)
{
	int index;

	for (index = 0; index < subscribed_here_count; index++)
	{
		if (subscribed_here[index] == value)
			return;
	}
	/* (full: the oldest forgotten, so at worst it stays subscribed) */
	if (subscribed_here_count == STEAM_BRIDGE_MAXIMUM_ITEMS)
	{
		memmove(subscribed_here, subscribed_here + 1, sizeof(subscribed_here[0]) * (STEAM_BRIDGE_MAXIMUM_ITEMS - 1));
		subscribed_here_count--;
	}
	subscribed_here[subscribed_here_count++] = value;
}

/* 1 if the game subscribed to it, and forgotten */
static int subscribed_here_take(unsigned long long value)
{
	int index;

	for (index = 0; index < subscribed_here_count; index++)
	{
		if (subscribed_here[index] == value)
		{
			subscribed_here[index] = subscribed_here[--subscribed_here_count];
			return 1;
		}
	}
	return 0;
}

static int id_value(const char *id, unsigned long long *value)
{
	unsigned long long result = 0;
	int length = 0;

	if (!id)
		return 0;
	for (; id[length]; length++)
	{
		if (id[length] < '0' || id[length] > '9' || length >= 20)
			return 0;
		if (result > (~0ULL - 9) / 10)
			return 0;
		result = result * 10 + (unsigned long long)(id[length] - '0');
	}
	if (!length || !result)
		return 0;
	*value = result;
	return 1;
}

/* (the lock held) */
static struct watched_item *find(const char *id)
{
	int index;

	for (index = 0; index < bridge.count; index++)
	{
		if (!strcmp(bridge.items[index].id, id))
			return &bridge.items[index];
	}
	return NULL;
}

/* an item nothing waits on (no ask pending, not queued or downloading),
asked about longest ago; NULL if every one is busy (the lock held) */
static struct watched_item *idle_oldest(void)
{
	struct watched_item *oldest = NULL;
	unsigned long now = system_milliseconds();
	int index;

	for (index = 0; index < bridge.count; index++)
	{
		struct watched_item *item = &bridge.items[index];

		if (item->want || item->state.state == STEAM_BRIDGE_ITEM_QUEUED ||
			item->state.state == STEAM_BRIDGE_ITEM_DOWNLOADING)
		{
			continue;
		}
		if (!oldest || now - item->seen > now - oldest->seen)
			oldest = item;
	}
	return oldest;
}

/* (the lock held) */
static struct watched_item *watch(const char *id)
{
	struct watched_item *item = find(id);
	unsigned long long value;

	if (item)
	{
		item->seen = system_milliseconds();
		return item;
	}
	if (!id_value(id, &value))
		return NULL;
	/* (full, as a long browse fills it: the idle item asked about longest
	ago makes room; the thread's turn writes back by id, so it only loses
	that item's answer, which the next ask gets again) */
	if (bridge.count < STEAM_BRIDGE_MAXIMUM_ITEMS)
		item = &bridge.items[bridge.count++];
	else if ((item = idle_oldest()) == NULL)
		return NULL;
	memset(item, 0, sizeof(*item));
	item->seen = system_milliseconds();
	snprintf(item->id, sizeof(item->id), "%s", id);
	item->value = value;
	return item;
}

/* the downloader: the built-in client, or steamcmd (debug.workshop_steamcmd,
or where the built-in client isn't built) */
static const struct steam_bridge_backend *steamcmd_functions(void)
{
	if (steam_link_available() && !config_boolean("debug.workshop_steamcmd"))
		return &steam_bridge_pipe_backend;
#ifndef _WIN32
	return &steam_bridge_cmd_backend;
#else
	return NULL;
#endif
}

/* the backends' word on an item: steamcmd's (it downloads) before the client's */
static void item_query(const struct steam_bridge_backend *client, const struct steam_bridge_backend *steamcmd,
	struct watched_item *item)
{
	struct steam_bridge_item from_client, from_steamcmd;
	int client_known = client && client->query(item->value, &from_client);
	int steamcmd_known = steamcmd && steamcmd->query(item->value, &from_steamcmd);

	memset(&item->state, 0, sizeof(item->state));
	if (steamcmd_known)
		item->state = from_steamcmd;
	else if (client_known)
		item->state = from_client;
	if (client_known)
		item->state.subscribed = from_client.subscribed;
}

/* one session: connect, turns until told to stop, disconnect */
static void session(int use_steamcmd)
{
	static struct watched_item turn[STEAM_BRIDGE_MAXIMUM_ITEMS];
	static int blocked_said;
	const struct steam_bridge_backend *client = &steam_bridge_api_backend;
	const struct steam_bridge_backend *steamcmd;
	int client_status, steamcmd_status, status, connected = 0, live;

	/* (a signed Mac: the client backend skipped before any library is looked
	for, the downloader in its place; subscribing opens the item's page) */
	if (steam_bridge_platform_client_blocked())
	{
		if (!blocked_said)
		{
			blocked_said = 1;
			platform_log("Steam bridge: the Steam app link is off in signed Mac builds; "
				"subscribing opens the item's page");
		}
		client = NULL;
		use_steamcmd = 1;
	}
	steamcmd = use_steamcmd ? steamcmd_functions() : NULL;
	client_status = client ? client->connect() : STEAM_BRIDGE_FAILED;
	steamcmd_status = steamcmd ? steamcmd->connect() : STEAM_BRIDGE_FAILED;

	if (client_status == STEAM_BRIDGE_READY)
		connected |= CLIENT_BIT;
	else
		client = NULL;
	if (steamcmd_status == STEAM_BRIDGE_READY || steamcmd_status == STEAM_BRIDGE_SIGN_IN)
		connected |= STEAMCMD_BIT;
	else
		steamcmd = NULL;
	status = connected ? STEAM_BRIDGE_READY : STEAM_BRIDGE_FAILED;
	pthread_mutex_lock(&bridge_lock);
	/* (a connect that took too long: the tab has fallen back already) */
	if (bridge.status == STEAM_BRIDGE_CONNECTING)
	{
		bridge.status = status;
		bridge.connected = connected;
		bridge.sign_in = steamcmd_status == STEAM_BRIDGE_SIGN_IN;
	}
	live = bridge.status == STEAM_BRIDGE_READY;
	pthread_mutex_unlock(&bridge_lock);
	platform_log("Workshop: Steam bridge %s (client %s, steamcmd %s)", live ? "ready" : "not available",
		client ? "connected" : "no", !steamcmd ? "no" : steamcmd_status == STEAM_BRIDGE_SIGN_IN ? "needs a sign-in" :
		"ready");
	while (live)
	{
		int count, index, install, stopping;

		pthread_mutex_lock(&bridge_lock);
		/* (told to stop: a last turn for the cancels asked just before, as
		a join's CANCEL closes the bridge right after asking) */
		stopping = bridge.stop;
		if (stopping)
			bridge.stopping = 1;
		count = bridge.count;
		memcpy(turn, bridge.items, sizeof(turn[0]) * (size_t)count);
		for (index = 0; index < count; index++)
		{
			if (!stopping || bridge.items[index].want == 5)
				bridge.items[index].want = 0;
			else
				turn[index].want = 0;
		}
		install = stopping ? 0 : bridge.install_wanted;
		if (!stopping)
			bridge.install_wanted = 0;
		pthread_mutex_unlock(&bridge_lock);
		for (index = 0; index < count; index++)
		{
			if (turn[index].want == 1)
			{
				if (client)
				{
					struct steam_bridge_item before;
					int was_subscribed = client->query(turn[index].value, &before) && before.subscribed;

					if (client->subscribe(turn[index].value) && !was_subscribed)
						subscribed_here_add(turn[index].value);
				}
				if (steamcmd)
					steamcmd->subscribe(turn[index].value);
			}
			/* (CANCEL: the client's subscription undone if this game made it,
			which stops Steam's download; the downloader's download stopped) */
			else if (turn[index].want == 5)
			{
				if (client && subscribed_here_take(turn[index].value))
				{
					platform_log("Workshop: item %s cancelled: unsubscribed in Steam", turn[index].id);
					client->unsubscribe(turn[index].value);
				}
				if (steamcmd && steamcmd->unsubscribe)
					steamcmd->unsubscribe(turn[index].value);
			}
			else if (turn[index].want == 2 && client)
				client->unsubscribe(turn[index].value);
			else if (turn[index].want == 3 || turn[index].want == 4)
			{
				/* (the backend that holds it: the downloader's copy first) */
				const struct steam_bridge_backend *holder = steamcmd && steamcmd->holds &&
					steamcmd->holds(turn[index].value) ? steamcmd : client;

				if (holder && turn[index].want == 3 && holder->remove)
					holder->remove(turn[index].value);
				else if (holder && turn[index].want == 4 && holder->update)
					holder->update(turn[index].value);
			}
		}
		if (stopping)
			break;
		if (install && steamcmd && steamcmd->install_mcc)
			steamcmd->install_mcc();
		if (client)
			client->tick();
		if (steamcmd)
			steamcmd->tick();
		for (index = 0; index < count; index++)
			item_query(client, steamcmd, &turn[index]);
		pthread_mutex_lock(&bridge_lock);
		for (index = 0; index < count; index++)
		{
			struct watched_item *item = find(turn[index].id);

			if (item)
				item->state = turn[index].state;
		}
		if (steamcmd)
		{
			bridge.mcc_state = steamcmd->mcc_state();
			bridge.sign_in = steamcmd_status == STEAM_BRIDGE_SIGN_IN || steamcmd->needs_sign_in();
		}
		pthread_mutex_unlock(&bridge_lock);
		steam_bridge_platform_sleep(STEAM_BRIDGE_POLL_INTERVAL);
	}
	if (client)
		client->disconnect();
	if (steamcmd)
		steamcmd->disconnect();
}

static void *worker(void *context)
{
	(void)context;
	for (;;)
	{
		int use_steamcmd, index;

		pthread_mutex_lock(&bridge_lock);
		use_steamcmd = bridge.use_steamcmd;
		pthread_mutex_unlock(&bridge_lock);
		session(use_steamcmd);
		pthread_mutex_lock(&bridge_lock);
		/* (the tab opened again while this one stopped: start over) */
		if (bridge.reopen)
		{
			bridge.reopen = 0;
			bridge.stop = 0;
			bridge.stopping = 0;
			bridge.status = STEAM_BRIDGE_CONNECTING;
			bridge.connect_time = system_milliseconds();
			pthread_mutex_unlock(&bridge_lock);
			continue;
		}
		bridge.running = 0;
		bridge.stopping = 0;
		bridge.connected = 0;
		if (bridge.status != STEAM_BRIDGE_FAILED)
			bridge.status = STEAM_BRIDGE_OFF;
		for (index = 0; index < bridge.count; index++)
			memset(&bridge.items[index].state, 0, sizeof(bridge.items[index].state));
		pthread_mutex_unlock(&bridge_lock);
		return NULL;
	}
}

/* ---------- public code */

int steam_bridge_enabled(void)
{
	return config_boolean("game.workshop_steam");
}

int steam_bridge_backend(void)
{
	if (!bridge.backend_looked)
	{
		char paths[1][STEAM_BRIDGE_PATH_SIZE];
#ifndef _WIN32
		char path[STEAM_BRIDGE_PATH_SIZE];
#endif

		bridge.backend_looked = 1;
		bridge.backend = STEAM_BRIDGE_BACKEND_NONE;
		/* (the built-in client first: it installs on every platform, signed
		in by the Steam app's QR; the Steam client only where it isn't built) */
		if (steam_link_available() && !config_boolean("debug.workshop_steamcmd"))
			bridge.backend = STEAM_BRIDGE_BACKEND_STEAMCMD;
		/* (a signed Mac: never the client's library, not even paths.steam_api) */
		else if (!steam_bridge_platform_client_blocked() && steam_bridge_api_library_paths(paths, 1, 0))
			bridge.backend = STEAM_BRIDGE_BACKEND_CLIENT;
#ifndef _WIN32
		else if (steam_bridge_platform_find_steamcmd(path, sizeof(path)))
			bridge.backend = STEAM_BRIDGE_BACKEND_STEAMCMD;
		/* (a game's steam_api library is looked for when the tab opens) */
		else if (!steam_bridge_platform_client_blocked() && steam_bridge_steam_present())
			bridge.backend = STEAM_BRIDGE_BACKEND_CLIENT;
#endif
	}
	return bridge.backend;
}

void steam_bridge_open(int use_steamcmd)
{
	pthread_t thread;

	if (!steam_bridge_enabled())
		return;
	pthread_mutex_lock(&bridge_lock);
	if (bridge.running)
	{
		/* (still running: told not to stop, or to start over once stopped) */
		if (bridge.stopping || bridge.use_steamcmd != use_steamcmd)
		{
			bridge.use_steamcmd = use_steamcmd;
			bridge.stop = 1;
			bridge.reopen = 1;
		}
		else
			bridge.stop = 0;
		pthread_mutex_unlock(&bridge_lock);
		return;
	}
	bridge.running = 1;
	bridge.stop = 0;
	bridge.stopping = 0;
	bridge.reopen = 0;
	bridge.use_steamcmd = use_steamcmd;
	bridge.connected = 0;
	bridge.sign_in = 0;
	bridge.status = STEAM_BRIDGE_CONNECTING;
	bridge.connect_time = system_milliseconds();
	pthread_mutex_unlock(&bridge_lock);
	if (pthread_create(&thread, NULL, worker, NULL) == 0)
	{
		pthread_detach(thread);
		return;
	}
	pthread_mutex_lock(&bridge_lock);
	bridge.running = 0;
	bridge.status = STEAM_BRIDGE_FAILED;
	pthread_mutex_unlock(&bridge_lock);
}

void steam_bridge_restart(int use_steamcmd)
{
	int running;

	pthread_mutex_lock(&bridge_lock);
	running = bridge.running;
	if (running)
	{
		/* (its session ends and a new one connects: the items asked for
		meanwhile wait, as while it first connects) */
		bridge.use_steamcmd = use_steamcmd;
		bridge.stop = 1;
		bridge.reopen = 1;
		bridge.sign_in = 0;
		bridge.status = STEAM_BRIDGE_CONNECTING;
		bridge.connect_time = system_milliseconds();
	}
	pthread_mutex_unlock(&bridge_lock);
	if (!running)
		steam_bridge_open(use_steamcmd);
}

void steam_bridge_close(void)
{
	pthread_mutex_lock(&bridge_lock);
	if (bridge.running)
	{
		bridge.stop = 1;
		bridge.reopen = 0;
	}
	/* (a failure is forgotten: the next open tries again) */
	else
		bridge.status = STEAM_BRIDGE_OFF;
	pthread_mutex_unlock(&bridge_lock);
}

int steam_bridge_status(void)
{
	int status;

	pthread_mutex_lock(&bridge_lock);
	if (bridge.status == STEAM_BRIDGE_CONNECTING &&
		system_milliseconds() - bridge.connect_time >= STEAM_BRIDGE_CONNECT_TIMEOUT)
	{
		/* (the thread may still be in SteamAPI_Init: it shuts down when it returns) */
		platform_log("Workshop: Steam bridge timed out");
		bridge.status = STEAM_BRIDGE_FAILED;
		bridge.stop = 1;
		bridge.reopen = 0;
	}
	status = bridge.status;
	pthread_mutex_unlock(&bridge_lock);
	return status;
}

int steam_bridge_connected(void)
{
	int connected;

	pthread_mutex_lock(&bridge_lock);
	connected = bridge.status == STEAM_BRIDGE_READY ? bridge.connected : 0;
	pthread_mutex_unlock(&bridge_lock);
	return connected;
}

int steam_bridge_offline(void)
{
	return steam_bridge_api_offline();
}

int steam_bridge_sign_in_needed(void)
{
	int needed;

	pthread_mutex_lock(&bridge_lock);
	needed = bridge.status == STEAM_BRIDGE_READY && bridge.sign_in;
	pthread_mutex_unlock(&bridge_lock);
	return needed;
}

int steam_bridge_item(const char *id, struct steam_bridge_item *item)
{
	struct watched_item *watched;
	int known = 0;

	memset(item, 0, sizeof(*item));
	pthread_mutex_lock(&bridge_lock);
	watched = watch(id);
	if (watched && bridge.status == STEAM_BRIDGE_READY)
	{
		*item = watched->state;
		known = item->state != STEAM_BRIDGE_ITEM_UNKNOWN;
	}
	pthread_mutex_unlock(&bridge_lock);
	return known;
}

static int ask(const char *id, int want, int needs)
{
	struct watched_item *item = NULL;
	int asked = 0, status, connected;

	pthread_mutex_lock(&bridge_lock);
	status = bridge.status;
	connected = bridge.connected;
	if (status == STEAM_BRIDGE_READY && (connected & needs) && (item = watch(id)) != NULL)
	{
		item->want = want;
		if (want == 1 && item->state.state != STEAM_BRIDGE_ITEM_INSTALLED &&
			item->state.state != STEAM_BRIDGE_ITEM_DOWNLOADING)
		{
			item->state.state = STEAM_BRIDGE_ITEM_QUEUED;
		}
		if (want == 2)
			item->state.subscribed = 0;
		if (want == 5)
		{
			memset(&item->state, 0, sizeof(item->state));
			item->state.state = STEAM_BRIDGE_ITEM_NONE;
		}
		asked = 1;
	}
	pthread_mutex_unlock(&bridge_lock);
	/* (why it wasn't asked, which the tab only calls Failed) */
	if (!asked)
	{
		platform_log("Workshop: item %s not asked of Steam: %s", id ? id : "", status != STEAM_BRIDGE_READY ?
			"the bridge isn't ready" : !(connected & needs) ? "no backend for it is connected" :
			"every watched item is busy");
	}
	return asked;
}

int steam_bridge_subscribe(const char *id)
{
	/* (steamcmd waiting on a sign-in downloads nothing: the client alone then) */
	return ask(id, 1, steam_bridge_sign_in_needed() ? CLIENT_BIT : CLIENT_BIT | STEAMCMD_BIT);
}

int steam_bridge_removable(const char *id)
{
	struct watched_item *item;
	unsigned long long value;
	int removable = 0;

	if (!id_value(id, &value))
		return 0;
	pthread_mutex_lock(&bridge_lock);
	item = find(id);
	if (bridge.status == STEAM_BRIDGE_READY)
	{
		/* (the downloader's own copy, or the client's subscription) */
		if ((bridge.connected & STEAMCMD_BIT) && steamcmd_functions() && steamcmd_functions()->holds &&
			steamcmd_functions()->holds(value))
		{
			removable = 1;
		}
		else if ((bridge.connected & CLIENT_BIT) && item && item->state.subscribed)
			removable = 1;
	}
	pthread_mutex_unlock(&bridge_lock);
	return removable;
}

int steam_bridge_remove(const char *id)
{
	return steam_bridge_removable(id) && ask(id, 3, CLIENT_BIT | STEAMCMD_BIT);
}

int steam_bridge_update(const char *id)
{
	return ask(id, 4, CLIENT_BIT | STEAMCMD_BIT);
}

int steam_bridge_cancel(const char *id)
{
	return ask(id, 5, CLIENT_BIT | STEAMCMD_BIT);
}

int steam_bridge_can_unsubscribe(void)
{
	return (steam_bridge_connected() & CLIENT_BIT) != 0;
}

int steam_bridge_unsubscribe(const char *id)
{
	return ask(id, 2, CLIENT_BIT);
}

int steam_bridge_sign_in(void)
{
	/* (the built-in client: the Guide's QR, steam_link_start) */
	if (steamcmd_functions() == &steam_bridge_pipe_backend)
	{
		steam_link_start();
		return 1;
	}
#ifndef _WIN32
	char steamcmd[STEAM_BRIDGE_PATH_SIZE];

	if (steam_bridge_platform_find_steamcmd(steamcmd, sizeof(steamcmd)) &&
		steam_bridge_platform_open_sign_in(steamcmd))
	{
		/* (the next open looks for the saved sign-in again) */
		steam_bridge_close();
		return 1;
	}
#endif
	return 0;
}

int steam_bridge_install_mcc(void)
{
	int asked = 0;

	pthread_mutex_lock(&bridge_lock);
	if (bridge.status == STEAM_BRIDGE_READY && (bridge.connected & STEAMCMD_BIT) && !bridge.sign_in)
	{
		bridge.install_wanted = 1;
		bridge.mcc_state = STEAM_BRIDGE_ITEM_QUEUED;
		asked = 1;
	}
	pthread_mutex_unlock(&bridge_lock);
	return asked;
}

int steam_bridge_mcc_state(void)
{
	int state;

	pthread_mutex_lock(&bridge_lock);
	state = bridge.mcc_state;
	pthread_mutex_unlock(&bridge_lock);
	return state;
}

#if defined(HALO_ANDROID)
/* ---------- no bridge here: the platform's functions as stand-ins that never work */

void *steam_bridge_platform_library_open(const char *path) { (void)path; return NULL; }
void *steam_bridge_platform_library_symbol(void *library, const char *name) { (void)library; (void)name; return NULL; }
void steam_bridge_platform_set_app_id(const char *app_id) { (void)app_id; }
void steam_bridge_platform_sleep(int milliseconds) { (void)milliseconds; }
int steam_bridge_platform_client_blocked(void) { return 0; }
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
#endif

#ifdef STEAM_BRIDGE_TESTS
/* the tests' fresh start (the backend looked for again) */
void steam_bridge_test_reset(void)
{
	pthread_mutex_lock(&bridge_lock);
	bridge.backend_looked = 0;
	bridge.status = STEAM_BRIDGE_OFF;
	bridge.count = 0;
	bridge.install_wanted = 0;
	bridge.mcc_state = 0;
	subscribed_here_count = 0;
	pthread_mutex_unlock(&bridge_lock);
}

int steam_bridge_test_running(void)
{
	int running;

	pthread_mutex_lock(&bridge_lock);
	running = bridge.running;
	pthread_mutex_unlock(&bridge_lock);
	return running;
}
#endif
#endif /* HALO_MCC */
