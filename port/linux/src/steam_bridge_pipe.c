/*
STEAM_BRIDGE_PIPE.C

The bridge's built-in downloader (steam_client_bridge.h): the game's own
Steam content client (steam_content/steam_content.h) in the downloader's
place, where steamcmd was, so the Workshop tab's GET, UPDATE, REMOVE,
progress and a join's missing map need no Steam client and no steamcmd.
The player links Steam once by scanning a QR code with the Steam app (the
Guide draws it: steam_link_*); the game never sees a password. steamcmd
stays as a fallback behind debug.workshop_steamcmd.

Items land in the game's own Steam library (steam_bridge_set_download_folder),
<folder>/steamapps/workshop/content/976730/<id>, as steamcmd's did.

The Steam name shown ("Linked to Steam: <name>") is for this screen only:
never logged, never sent to Delta, servers or reports.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h): a console builds this to nothing */

#include "steam_client_bridge.h"

#include <stdio.h>
#include <string.h>

#if defined(HALO_STEAM_CONTENT) && !defined(HALO_ANDROID) && !defined(HALO_WEB)

#include "steam_content/steam_content.h"
#ifdef HALO_GAME_BROWSER
#include "browser_http.h"
#endif
#include "qrcodegen.h"

#include <stdlib.h>
#ifdef _WIN32
#include <sys/utime.h> /* Windows keeps utime() here */
#else
#include <utime.h>
#endif

const char *platform_data_root(void);
void platform_log(const char *format, ...);
int config_boolean(const char *name);
long config_integer(const char *name);
const char *config_string(const char *name);

static char pipe_folder[STEAM_BRIDGE_PATH_SIZE];
static int pipe_ready;

unsigned long system_milliseconds(void);

/* ---------- debug.steam_link_fake: Steam's link and downloads played out
without Steam, for the Workshop's screenshot runs (never in a release
build). Its words: "linked" (else not linked at first), "link=offline" or
"link=expire" (else the scan goes through), "get=ok", "get=offline",
"get=notowned", "get=diskfull", "get=toolarge" or "get=failed" (else ok).
A fake GET counts up for eight seconds; an ok one moves
<folder>/steamapps/workshop/fake/976730/<id> in as the item, if it's there */
#ifndef HALO_RELEASE
static struct
{
	int checked, on, linked;
	unsigned long link_started;
	char link_outcome[16], get_outcome[16];
	int count;
	struct
	{
		unsigned long long id;
		unsigned long started;
		int state, done;
	} items[8];
} fake;

static void fake_word(const char *setting, const char *name, char *out, int size)
{
	const char *at = strstr(setting, name);
	int length = 0;

	out[0] = 0;
	if (!at)
		return;
	at += strlen(name);
	while (at[length] && at[length] != ',' && length < size - 1)
	{
		out[length] = at[length];
		length++;
	}
	out[length] = 0;
}

static int fake_on(void)
{
	if (!fake.checked)
	{
		const char *setting = config_string("debug.steam_link_fake");

		fake.checked = 1;
		fake.on = setting && setting[0];
		if (fake.on)
		{
			fake.linked = strstr(setting, "linked") && !strstr(setting, "unlinked");
			fake_word(setting, "link=", fake.link_outcome, sizeof(fake.link_outcome));
			fake_word(setting, "get=", fake.get_outcome, sizeof(fake.get_outcome));
			platform_log("Steam: debug.steam_link_fake: %s", setting);
		}
	}
	return fake.on;
}

static int fake_link_poll(char *qr_text, int size, char *error, int error_size)
{
	unsigned long elapsed = system_milliseconds() - fake.link_started;

	if (error && error_size > 0)
		error[0] = 0;
	if (qr_text && size > 0)
		snprintf(qr_text, (size_t)size, "https://s.team/q/1/1234567890123456789");
	if (!fake.link_started)
		return STEAM_LINK_IDLE;
	if (elapsed < 1500)
		return STEAM_LINK_STARTING;
	if (!strcmp(fake.link_outcome, "offline"))
	{
		if (error && error_size > 0)
			snprintf(error, (size_t)error_size, "Offline");
		return STEAM_LINK_FAILED;
	}
	if (elapsed < 5000 || (!strcmp(fake.link_outcome, "expire") && elapsed < 8000))
		return STEAM_LINK_WAITING;
	if (!strcmp(fake.link_outcome, "expire"))
	{
		if (error && error_size > 0)
			snprintf(error, (size_t)error_size, "Failed");
		return STEAM_LINK_FAILED;
	}
	if (elapsed < 7000)
		return STEAM_LINK_SCANNED;
	fake.linked = 1;
	return STEAM_LINK_DONE;
}

static int fake_find(unsigned long long id)
{
	int index;

	for (index = 0; index < fake.count; index++)
	{
		if (fake.items[index].id == id)
			return index;
	}
	return -1;
}

static int fake_get(unsigned long long id)
{
	int index = fake_find(id);

	if (index < 0)
	{
		if (fake.count >= (int)(sizeof(fake.items) / sizeof(fake.items[0])))
			return 0;
		index = fake.count++;
	}
	fake.items[index].id = id;
	fake.items[index].started = system_milliseconds();
	fake.items[index].done = 0;
	return 1;
}

static void fake_cancel(unsigned long long id)
{
	int index = fake_find(id);

	if (index >= 0)
		fake.items[index] = fake.items[--fake.count];
}

int steam_bridge_platform_merge_folder(const char *from, const char *to);

/* an item's fake progress: 1 if known (state as STEAM_CONTENT_ITEM_*) */
static int fake_progress(unsigned long long id, int *state, unsigned long long *done, unsigned long long *total,
	const char **error)
{
	int index = fake_find(id);
	unsigned long elapsed;
	const char *outcome = fake.get_outcome;

	*error = "";
	if (index < 0)
		return 0;
	elapsed = system_milliseconds() - fake.items[index].started;
	*total = 145ull << 20;
	*done = 0;
	if (elapsed < 1500)
	{
		*state = STEAM_CONTENT_ITEM_QUEUED;
		return 1;
	}
	if ((!strcmp(outcome, "notowned") && elapsed >= 2500) || (outcome[0] && strcmp(outcome, "ok") &&
		strcmp(outcome, "notowned") && elapsed >= 4500))
	{
		*state = STEAM_CONTENT_ITEM_FAILED;
		*error = !strcmp(outcome, "notowned") ? "Not owned" : !strcmp(outcome, "offline") ? "Offline" :
			!strcmp(outcome, "diskfull") ? "Disk full" : !strcmp(outcome, "toolarge") ? "Too large" : "Failed";
		return 1;
	}
	if (elapsed < 9500)
	{
		*state = STEAM_CONTENT_ITEM_DOWNLOADING;
		*done = *total * (elapsed - 1500) / 8000;
		return 1;
	}
	if (!fake.items[index].done)
	{
		char from[STEAM_BRIDGE_PATH_SIZE + 64], to[STEAM_BRIDGE_PATH_SIZE + 64];

		fake.items[index].done = 1;
		snprintf(from, sizeof(from), "%s/steamapps/workshop/fake/976730/%llu", pipe_folder, id);
		snprintf(to, sizeof(to), "%s/steamapps/workshop/content/976730/%llu", pipe_folder, id);
		/* (no folder for this item: the one called "any") */
		if (!steam_bridge_platform_is_folder(from))
			snprintf(from, sizeof(from), "%s/steamapps/workshop/fake/976730/any", pipe_folder);
		if (steam_bridge_platform_is_folder(from) && !steam_bridge_platform_is_folder(to))
			rename(from, to);
		/* (an update: the copy as new as Steam's now) */
		else
		{
			char info[STEAM_BRIDGE_PATH_SIZE + 96];

			snprintf(info, sizeof(info), "%s/ModInfo.json", to);
			utime(info, NULL);
		}
	}
	*state = STEAM_CONTENT_ITEM_INSTALLED;
	*done = *total;
	return 1;
}
#endif

void steam_bridge_pipe_set_folder(const char *path)
{
	snprintf(pipe_folder, sizeof(pipe_folder), "%s", path ? path : "");
}

static void pipe_init(void)
{
	struct steam_content_host host;
	const char *data;

	if (pipe_ready)
		return;
	memset(&host, 0, sizeof(host));
	data = platform_data_root();
	snprintf(host.data_folder, sizeof(host.data_folder), "%s", data ? data : "");
#ifdef HALO_GAME_BROWSER
	host.tls_lock = posix_browser_lock;
	host.tls_unlock = posix_browser_unlock;
	host.certificates = posix_browser_certificates;
#endif
	host.log = platform_log;
	host.file_only = config_boolean("debug.steam_link_file");
	/* (the size limits, in MB: 0 or less keeps the content client's defaults) */
	if (config_integer("game.workshop_item_limit_mb") > 0)
		host.item_limit = (unsigned long long)config_integer("game.workshop_item_limit_mb") << 20;
	if (config_integer("game.workshop_file_limit_mb") > 0)
		host.file_limit = (unsigned long long)config_integer("game.workshop_file_limit_mb") << 20;
	steam_content_init(&host);
	pipe_ready = 1;
}

static int pipe_linked(char *persona, int size)
{
#ifndef HALO_RELEASE
	if (fake_on())
	{
		if (persona && size > 0)
			snprintf(persona, (size_t)size, "%s", fake.linked ? "Tester" : "");
		return fake.linked;
	}
#endif
	return steam_content_linked(persona, size);
}

static int pipe_connect(void)
{
	pipe_init();
	return pipe_linked(NULL, 0) ? STEAM_BRIDGE_READY : STEAM_BRIDGE_SIGN_IN;
}

static void pipe_disconnect(void)
{
	/* (a download under way finishes: it is the player's, not the tab's) */
}

static int pipe_subscribe(unsigned long long id)
{
	if (!pipe_folder[0])
	{
		platform_log("Workshop: no folder to download item %llu into", id);
		return 0;
	}
#ifndef HALO_RELEASE
	if (fake_on())
		return fake_get(id);
#endif
	return steam_content_get_item(id, pipe_folder);
}

static void content_cancel(unsigned long long id)
{
#ifndef HALO_RELEASE
	if (fake_on())
	{
		fake_cancel(id);
		return;
	}
#endif
	steam_content_cancel(id);
}

static int content_progress(unsigned long long id, struct steam_content_progress *progress, const char **error)
{
	*error = "";
#ifndef HALO_RELEASE
	if (fake_on())
	{
		memset(progress, 0, sizeof(*progress));
		return fake_progress(id, &progress->state, &progress->done, &progress->total, error);
	}
#endif
	if (!steam_content_progress(id, progress))
		return 0;
	if (progress->state == STEAM_CONTENT_ITEM_FAILED)
		*error = steam_content_error_text(progress->error);
	return 1;
}

static int pipe_unsubscribe(unsigned long long id)
{
	content_cancel(id);
	return 1;
}

static int pipe_query(unsigned long long id, struct steam_bridge_item *item)
{
	struct steam_content_progress progress;
	const char *error;

	memset(item, 0, sizeof(*item));
	if (!content_progress(id, &progress, &error))
		return 0;
	switch (progress.state)
	{
	case STEAM_CONTENT_ITEM_QUEUED: item->state = STEAM_BRIDGE_ITEM_QUEUED; break;
	case STEAM_CONTENT_ITEM_DOWNLOADING: item->state = STEAM_BRIDGE_ITEM_DOWNLOADING; break;
	case STEAM_CONTENT_ITEM_INSTALLED: item->state = STEAM_BRIDGE_ITEM_INSTALLED; break;
	case STEAM_CONTENT_ITEM_FAILED: item->state = STEAM_BRIDGE_ITEM_FAILED; break;
	default: return 0;
	}
	item->downloaded = progress.done;
	item->total = progress.total;
	if (progress.state == STEAM_CONTENT_ITEM_FAILED)
		snprintf(item->error, sizeof(item->error), "%s", error);
	return 1;
}

static void pipe_tick(void)
{
}

static int pipe_needs_sign_in(void)
{
	return !pipe_linked(NULL, 0);
}

static int pipe_mcc_state(void)
{
	return STEAM_BRIDGE_ITEM_NONE;
}

/* the item's folder in the game's own library; 1 if it is there */
static int own_folder(unsigned long long id, char *path, int size)
{
	return pipe_folder[0] &&
		snprintf(path, (size_t)size, "%s/steamapps/workshop/content/976730/%llu", pipe_folder, id) < size &&
		steam_bridge_platform_is_folder(path);
}

static int pipe_holds(unsigned long long id)
{
	char path[STEAM_BRIDGE_PATH_SIZE + 64];

	return own_folder(id, path, sizeof(path));
}

/* REMOVE: the game's own copy deleted (never anything outside its own library) */
static int pipe_remove(unsigned long long id)
{
	char path[STEAM_BRIDGE_PATH_SIZE + 64];

	content_cancel(id);
	/* (a download put together halfway, too) */
	if (pipe_folder[0] && snprintf(path, sizeof(path), "%s/steamapps/workshop/downloads/976730/%llu", pipe_folder, id) <
		(int)sizeof(path) && steam_bridge_platform_is_folder(path))
	{
		steam_bridge_platform_remove_folder(path);
	}
	if (!own_folder(id, path, sizeof(path)))
		return 0;
	platform_log("Workshop: removing item %llu", id);
	return steam_bridge_platform_remove_folder(path);
}

/* UPDATE: fetched again over its copy (files already whole are kept) */
static int pipe_update(unsigned long long id)
{
	return pipe_subscribe(id);
}

const struct steam_bridge_backend steam_bridge_pipe_backend =
{
	pipe_connect,
	pipe_disconnect,
	pipe_subscribe,
	pipe_unsubscribe,
	pipe_query,
	pipe_tick,
	pipe_needs_sign_in,
	NULL,
	pipe_mcc_state,
	pipe_remove,
	pipe_update,
	pipe_holds,
};

int steam_link_available(void)
{
	return 1;
}

/* the game's own library, where the Workshop tab hasn't set it (a dedicated
server): paths.steam_downloads, else steam/ in the data folder */
static void folder_default(void)
{
	const char *setting = config_string("paths.steam_downloads"), *data = platform_data_root();

	if (pipe_folder[0])
		return;
	if (setting && setting[0])
		snprintf(pipe_folder, sizeof(pipe_folder), "%s", setting);
	else if (data && data[0])
		snprintf(pipe_folder, sizeof(pipe_folder), "%s/steam", data);
}

static int id_of(const char *text, unsigned long long *id)
{
	char *end;

	if (!text || !text[0] || strlen(text) > 20)
		return 0;
	*id = strtoull(text, &end, 10);
	return !*end && *id;
}

int steam_link_get(const char *id_text)
{
	unsigned long long id;

	pipe_init();
	folder_default();
	if (!id_of(id_text, &id) || !pipe_folder[0])
		return 0;
#ifndef HALO_RELEASE
	if (fake_on())
		return fake_get(id);
#endif
	return steam_content_get_item(id, pipe_folder);
}

/* ---------- CANCEL on a GET: stopped, and once the download has let go of
its files, what it had put together deleted (only a GET's: an update's
downloads folder holds the installed copy) */
static unsigned long long discard_ids[8];

void steam_link_cancel_get(const char *id_text)
{
	unsigned long long id;
	int slot;

	if (!id_of(id_text, &id))
		return;
	content_cancel(id);
	for (slot = 0; slot < (int)(sizeof(discard_ids) / sizeof(discard_ids[0])); slot++)
	{
		if (!discard_ids[slot] || discard_ids[slot] == id)
		{
			discard_ids[slot] = id;
			break;
		}
	}
}

void steam_link_discard_follow(void)
{
	int slot;

	folder_default();
	for (slot = 0; slot < (int)(sizeof(discard_ids) / sizeof(discard_ids[0])); slot++)
	{
		struct steam_content_progress progress;
		const char *why;
		char path[STEAM_BRIDGE_PATH_SIZE + 64];
		unsigned long long id = discard_ids[slot];

		if (!id || (content_progress(id, &progress, &why) && (progress.state == STEAM_CONTENT_ITEM_DOWNLOADING)))
			continue;
		discard_ids[slot] = 0;
		if (pipe_folder[0] && snprintf(path, sizeof(path), "%s/steamapps/workshop/downloads/976730/%llu", pipe_folder,
			id) < (int)sizeof(path) && steam_bridge_platform_is_folder(path))
		{
			platform_log("Workshop: item %llu's download stopped; its part deleted", id);
			steam_bridge_platform_remove_folder(path);
		}
	}
}

int steam_link_progress(const char *id_text, unsigned long long *done, unsigned long long *total, char *error,
	int error_size)
{
	struct steam_content_progress progress;
	unsigned long long id;

	if (error && error_size > 0)
		error[0] = 0;
	const char *why;

	if (!id_of(id_text, &id) || !content_progress(id, &progress, &why))
		return STEAM_BRIDGE_ITEM_UNKNOWN;
	*done = progress.done;
	*total = progress.total;
	switch (progress.state)
	{
	case STEAM_CONTENT_ITEM_QUEUED: return STEAM_BRIDGE_ITEM_QUEUED;
	case STEAM_CONTENT_ITEM_DOWNLOADING: return STEAM_BRIDGE_ITEM_DOWNLOADING;
	case STEAM_CONTENT_ITEM_INSTALLED: return STEAM_BRIDGE_ITEM_INSTALLED;
	case STEAM_CONTENT_ITEM_FAILED:
		if (error && error_size > 0)
			snprintf(error, (size_t)error_size, "%s", why);
		return STEAM_BRIDGE_ITEM_FAILED;
	default: return STEAM_BRIDGE_ITEM_NONE;
	}
}

int steam_link_qr_text(const char *text, char *out, int size)
{
	uint8_t qr[qrcodegen_BUFFER_LEN_MAX], work[qrcodegen_BUFFER_LEN_MAX];
	int modules, x, y, used = 0;

	if (!out || size <= 0)
		return 0;
	out[0] = 0;
	if (!qrcodegen_encodeText(text, work, qr, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
		qrcodegen_Mask_AUTO, true))
	{
		return 0;
	}
	modules = qrcodegen_getSize(qr);
	/* (two rows a line, light modules drawn: a dark terminal's background is
	the dark modules; a quiet edge of two modules around) */
	for (y = -2; y < modules + 2; y += 2)
	{
		for (x = -2; x < modules + 2; x++)
		{
			int top = !(y >= 0 && y < modules && x >= 0 && x < modules && qrcodegen_getModule(qr, x, y));
			int bottom = !(y + 1 >= 0 && y + 1 < modules && x >= 0 && x < modules && qrcodegen_getModule(qr, x, y + 1));
			const char *cell = top && bottom ? "\xE2\x96\x88" : top ? "\xE2\x96\x80" : bottom ? "\xE2\x96\x84" : " ";
			int length = (int)strlen(cell);

			if (used + length + 2 >= size)
				return 0;
			memcpy(out + used, cell, (size_t)length);
			used += length;
		}
		out[used++] = '\n';
	}
	out[used] = 0;
	return 1;
}

int steam_link_linked(char *persona, int size)
{
	pipe_init();
	return pipe_linked(persona, size);
}

void steam_link_start(void)
{
	pipe_init();
#ifndef HALO_RELEASE
	if (fake_on())
	{
		fake.link_started = system_milliseconds() | 1;
		return;
	}
#endif
	steam_content_sign_in_qr();
}

int steam_link_poll(char *qr_text, int size, char *error, int error_size)
{
	int code = 0, state;

#ifndef HALO_RELEASE
	if (fake_on())
		return fake_link_poll(qr_text, size, error, error_size);
#endif
	state = steam_content_sign_in_poll(qr_text, size, &code);

	if (error && error_size > 0)
		snprintf(error, (size_t)error_size, "%s", state == STEAM_CONTENT_QR_FAILED ? steam_content_error_text(code) : "");
	switch (state)
	{
	case STEAM_CONTENT_QR_STARTING: return STEAM_LINK_STARTING;
	case STEAM_CONTENT_QR_WAITING: return STEAM_LINK_WAITING;
	case STEAM_CONTENT_QR_SCANNED: return STEAM_LINK_SCANNED;
	case STEAM_CONTENT_QR_DONE: return STEAM_LINK_DONE;
	case STEAM_CONTENT_QR_FAILED: return STEAM_LINK_FAILED;
	default: return STEAM_LINK_IDLE;
	}
}

void steam_link_cancel(void)
{
#ifndef HALO_RELEASE
	if (fake_on())
	{
		fake.link_started = 0;
		return;
	}
#endif
	steam_content_sign_in_cancel();
}

static int armed;

void steam_link_set_armed(int value)
{
	armed = value;
}

int steam_link_armed(void)
{
	return armed;
}

const char *steam_link_store(void)
{
	pipe_init();
	return steam_content_store();
}

int steam_link_store_secure(void)
{
	pipe_init();
	return steam_content_store_secure();
}

void steam_link_unlink(void)
{
	pipe_init();
#ifndef HALO_RELEASE
	if (fake_on())
	{
		fake.linked = 0;
		fake.link_started = 0;
		return;
	}
#endif
	steam_content_sign_out();
}

#else

/* ---------- no built-in client here: steamcmd stays the downloader */

void steam_bridge_pipe_set_folder(const char *path)
{
	(void)path;
}

static int none_connect(void) { return STEAM_BRIDGE_FAILED; }
static void none_disconnect(void) {}
static int none_id(unsigned long long id) { (void)id; return 0; }
static int none_query(unsigned long long id, struct steam_bridge_item *item) { (void)id; (void)item; return 0; }
static void none_tick(void) {}
static int none_needs_sign_in(void) { return 0; }
static int none_mcc_state(void) { return 0; }

const struct steam_bridge_backend steam_bridge_pipe_backend =
{
	none_connect, none_disconnect, none_id, none_id, none_query, none_tick, none_needs_sign_in, NULL, none_mcc_state,
	none_id, none_id, none_id,
};

int steam_link_available(void) { return 0; }
int steam_link_get(const char *id_text) { (void)id_text; return 0; }
int steam_link_progress(const char *id_text, unsigned long long *done, unsigned long long *total, char *error,
	int error_size)
{
	(void)id_text; (void)done; (void)total;
	if (error && error_size > 0)
		error[0] = 0;
	return STEAM_BRIDGE_ITEM_UNKNOWN;
}
int steam_link_qr_text(const char *text, char *out, int size)
{
	(void)text;
	if (out && size > 0)
		out[0] = 0;
	return 0;
}
int steam_link_linked(char *persona, int size)
{
	if (persona && size > 0)
		persona[0] = 0;
	return 0;
}
void steam_link_start(void) {}
int steam_link_poll(char *qr_text, int size, char *error, int error_size)
{
	if (qr_text && size > 0)
		qr_text[0] = 0;
	if (error && error_size > 0)
		error[0] = 0;
	return STEAM_LINK_IDLE;
}
void steam_link_cancel(void) {}
void steam_link_cancel_get(const char *id_text) { (void)id_text; }
void steam_link_discard_follow(void) {}
const char *steam_link_store(void) { return ""; }
int steam_link_store_secure(void) { return 1; }
void steam_link_set_armed(int value) { (void)value; }
int steam_link_armed(void) { return 0; }
void steam_link_unlink(void) {}

#endif
#endif /* HALO_MCC */
