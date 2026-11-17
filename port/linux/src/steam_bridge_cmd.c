/*
STEAM_BRIDGE_CMD.C

The bridge's steamcmd backend (steam_client_bridge.h), for macOS (MCC has
no Mac build: its Windows files are fetched with steamcmd) and Linux
without the Steam client. The player's own steamcmd runs as a child, one
job at a time, with their saved sign-in only:

	steamcmd +@NoPromptForPassword 1 +@sSteamCmdForcePlatformType windows
		+login <account> +workshop_download_item 976730 <id> +quit

The account is the one steamcmd keeps in its config/config.vdf (or
game.steamcmd_account). steamcmd is never given a password, and its input
is empty, so it can't wait on one: a run that needs a sign-in ends, and
the tab offers SIGN IN, which opens a terminal running steamcmd for the
player. Items land in the game's own Steam library
(steam_bridge_set_download_folder, +force_install_dir: the data folder's
steam/, or paths.steam_downloads), <folder>/steamapps/workshop/content/
976730/<id>, which the folder watch and the map lists read in place
(steam_workshop.c).

MCC itself (steam_bridge_install_mcc): only its Halo CE multiplayer depot,
976734 (under halo1/, about 4.3 GB), instead of the whole game, moved from
steamcmd's depot folder into <folder>/steamapps/common/Halo The Master
Chief Collection, where the game looks for MCC's shared maps.

The text reading at the top has no files or processes: its tests run
anywhere (port/linux/tests/steam_bridge_test.c).

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h): a console builds this to nothing */

#include "steam_client_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MCC_APP_ID "976730"
#define MCC_FOLDER "steamapps/common/Halo The Master Chief Collection"

/* ---------- steamcmd's text */

static const char *find_text(const char *text, const char *word)
{
	/* (case-insensitive: steamcmd's wording moves between versions) */
	size_t length = strlen(word);

	for (; *text; text++)
	{
		size_t index;

		for (index = 0; index < length; index++)
		{
			char a = text[index], b = word[index];

			if (a >= 'A' && a <= 'Z')
				a = (char)(a + 32);
			if (b >= 'A' && b <= 'Z')
				b = (char)(b + 32);
			if (a != b)
				break;
		}
		if (index == length)
			return text;
	}
	return NULL;
}

/* digits after text into id; 1 if some */
static int read_id(const char *text, char *id, int size)
{
	int length = 0;

	while (*text == ' ')
		text++;
	while (text[length] >= '0' && text[length] <= '9' && length < size - 1)
	{
		id[length] = text[length];
		length++;
	}
	id[length] = 0;
	return length > 0;
}

/* the first "quoted" text after text */
static int read_quoted(const char *text, char *out, int size)
{
	const char *start = strchr(text, '"'), *end;

	if (!start || !(end = strchr(start + 1, '"')) || end - start - 1 >= size || end == start + 1)
		return 0;
	memcpy(out, start + 1, (size_t)(end - start - 1));
	out[end - start - 1] = 0;
	return 1;
}

void steamcmd_read_line(const char *line, struct steamcmd_line *out)
{
	static const char *const sign_in[] = { "cached credentials not found", "password:", "login failure",
		"invalid password", "steam guard", "two-factor", "account logon denied", "not been authenticated",
		"expired login", "please enter" };
	const char *found;
	int index;

	memset(out, 0, sizeof(*out));
	if ((found = find_text(line, "downloaded item ")) != NULL && find_text(line, "success"))
	{
		if (read_id(found + 16, out->id, sizeof(out->id)))
		{
			out->kind = STEAMCMD_LINE_ITEM_DONE;
			if ((found = find_text(found, " to ")) != NULL)
				read_quoted(found, out->path, sizeof(out->path));
		}
		return;
	}
	if ((found = find_text(line, "download item ")) != NULL && find_text(line, "failed"))
	{
		if (read_id(found + 14, out->id, sizeof(out->id)))
			out->kind = STEAMCMD_LINE_ITEM_FAILED;
		return;
	}
	if ((found = find_text(line, "depot download complete")) != NULL)
	{
		if (read_quoted(found, out->path, sizeof(out->path)))
			out->kind = STEAMCMD_LINE_DEPOT_DONE;
		return;
	}
	for (index = 0; index < (int)(sizeof(sign_in) / sizeof(sign_in[0])); index++)
	{
		if (find_text(line, sign_in[index]))
		{
			out->kind = STEAMCMD_LINE_SIGN_IN;
			return;
		}
	}
	if ((found = find_text(line, "progress: ")) != NULL)
	{
		out->kind = STEAMCMD_LINE_PROGRESS;
		out->percent = atoi(found + 10);
	}
	else if ((found = strchr(line, '%')) != NULL && found > line && found[-1] >= '0' && found[-1] <= '9')
	{
		const char *start = found - 1;

		while (start > line && ((start[-1] >= '0' && start[-1] <= '9') || start[-1] == '.'))
			start--;
		while (*start == '.')
			start++;
		out->kind = STEAMCMD_LINE_PROGRESS;
		out->percent = atoi(start);
	}
	else if (!strncmp(line, "ERROR", 5) || find_text(line, "no subscription"))
		out->kind = STEAMCMD_LINE_ERROR;
	if (out->kind == STEAMCMD_LINE_PROGRESS)
		out->percent = out->percent < 0 ? 0 : out->percent > 100 ? 100 : out->percent;
}

int steamcmd_account_valid(const char *name)
{
	int length;

	/* (Steam's account names: letters, digits and underscores) */
	for (length = 0; name[length]; length++)
	{
		char c = name[length];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') ||
			length >= 63)
		{
			return 0;
		}
	}
	return length >= 2;
}

int steamcmd_read_account(const char *text, int length, char *name, int size)
{
	char *copy = malloc((size_t)length + 1);
	const char *accounts, *brace;
	int found = 0;

	if (!copy)
		return 0;
	memcpy(copy, text, (size_t)length);
	copy[length] = 0;
	if ((accounts = find_text(copy, "\"Accounts\"")) != NULL && (brace = strchr(accounts + 10, '{')) != NULL)
	{
		const char *after = brace + 1;

		while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n')
			after++;
		found = *after == '"' && read_quoted(after, name, size) && steamcmd_account_valid(name);
	}
	free(copy);
	return found;
}

/* ---------- the backend (processes: not on Windows, which has the client) */

#ifndef _WIN32

const char *config_string(const char *name);
unsigned long system_milliseconds(void);
void platform_log(const char *format, ...);

enum
{
	MAXIMUM_QUEUE = 32,
	MAXIMUM_KNOWN = STEAM_BRIDGE_MAXIMUM_ITEMS,
	JOB_NONE = 0,
	JOB_ITEM,
	JOB_MCC,
};

static char download_folder[STEAM_BRIDGE_PATH_SIZE];

void steam_bridge_set_download_folder(const char *path)
{
	snprintf(download_folder, sizeof(download_folder), "%s", path ? path : "");
	steam_bridge_pipe_set_folder(path);
}

static struct
{
	char steamcmd[STEAM_BRIDGE_PATH_SIZE];
	char root[STEAM_BRIDGE_PATH_SIZE];
	char account[64];
	int sign_in;
	void *child;
	int job;
	char job_id[STEAM_BRIDGE_ID_SIZE];
	unsigned long job_start;
	int job_done;
	int queue_count;
	char queue[MAXIMUM_QUEUE][STEAM_BRIDGE_ID_SIZE];
	int mcc_wanted;
	int mcc_state;
	int depot_count;
	char depots[2][STEAM_BRIDGE_PATH_SIZE];
	int known_count;
	struct
	{
		unsigned long long id;
		struct steam_bridge_item item;
	} known[MAXIMUM_KNOWN];
} cmd;

static struct steam_bridge_item *known(const char *id_text)
{
	unsigned long long id = strtoull(id_text, NULL, 10);
	int index;

	for (index = 0; index < cmd.known_count; index++)
	{
		if (cmd.known[index].id == id)
			return &cmd.known[index].item;
	}
	if (cmd.known_count >= MAXIMUM_KNOWN || !id)
		return NULL;
	cmd.known[cmd.known_count].id = id;
	memset(&cmd.known[cmd.known_count].item, 0, sizeof(cmd.known[0].item));
	return &cmd.known[cmd.known_count++].item;
}

static int read_account(void)
{
	const char *setting = config_string("game.steamcmd_account");
	char path[STEAM_BRIDGE_PATH_SIZE + 32], *text;
	FILE *file;
	long length;
	int found = 0;

	if (setting && setting[0])
	{
		snprintf(cmd.account, sizeof(cmd.account), "%s", setting);
		return steamcmd_account_valid(cmd.account);
	}
	snprintf(path, sizeof(path), "%s/config/config.vdf", cmd.root);
	if (!(file = fopen(path, "rb")))
		return 0;
	text = malloc(256 * 1024);
	length = text ? (long)fread(text, 1, 256 * 1024, file) : 0;
	fclose(file);
	if (length > 0)
		found = steamcmd_read_account(text, (int)length, cmd.account, sizeof(cmd.account));
	free(text);
	return found;
}

static int cmd_connect(void)
{
	cmd.sign_in = 0;
	if (!steam_bridge_platform_find_steamcmd(cmd.steamcmd, sizeof(cmd.steamcmd)) ||
		!steam_bridge_platform_steamcmd_root(cmd.steamcmd, cmd.root, sizeof(cmd.root)))
	{
		return STEAM_BRIDGE_FAILED;
	}
	if (!read_account())
	{
		cmd.account[0] = 0;
		return STEAM_BRIDGE_SIGN_IN;
	}
	return STEAM_BRIDGE_READY;
}

static void job_start(void)
{
	const char *arguments[20];
	int count = 0;

	if (!cmd.account[0] || cmd.sign_in)
		return;
	arguments[count++] = cmd.steamcmd;
	arguments[count++] = "+@NoPromptForPassword";
	arguments[count++] = "1";
	arguments[count++] = "+@sSteamCmdForcePlatformType";
	arguments[count++] = "windows";
	if (download_folder[0])
	{
		arguments[count++] = "+force_install_dir";
		arguments[count++] = download_folder;
	}
	arguments[count++] = "+login";
	arguments[count++] = cmd.account;
	if (cmd.mcc_wanted)
	{
		cmd.mcc_wanted = 0;
		cmd.job = JOB_MCC;
		cmd.depot_count = 0;
		arguments[count++] = "+download_depot";
		arguments[count++] = MCC_APP_ID;
		arguments[count++] = "976734";
		cmd.mcc_state = STEAM_BRIDGE_ITEM_DOWNLOADING;
	}
	else if (cmd.queue_count > 0)
	{
		struct steam_bridge_item *item;

		snprintf(cmd.job_id, sizeof(cmd.job_id), "%s", cmd.queue[0]);
		memmove(cmd.queue[0], cmd.queue[1], sizeof(cmd.queue[0]) * (size_t)(--cmd.queue_count));
		cmd.job = JOB_ITEM;
		arguments[count++] = "+workshop_download_item";
		arguments[count++] = MCC_APP_ID;
		arguments[count++] = cmd.job_id;
		if ((item = known(cmd.job_id)) != NULL)
			item->state = STEAM_BRIDGE_ITEM_DOWNLOADING;
	}
	else
		return;
	arguments[count++] = "+quit";
	arguments[count] = NULL;
	cmd.job_done = 0;
	cmd.job_start = system_milliseconds();
	cmd.child = steam_bridge_platform_child_start(arguments);
	if (!cmd.child)
	{
		platform_log("Workshop: steamcmd didn't start");
		cmd.job_done = -1;
	}
}

/* the depots moved into MCC's folder in steamcmd's library */
static int mcc_place(void)
{
	int index;

	for (index = 0; index < cmd.depot_count; index++)
	{
		char root[STEAM_BRIDGE_PATH_SIZE], target[STEAM_BRIDGE_PATH_SIZE + 64];
		const char *library = strstr(cmd.depots[index], "/steamapps/");

		/* (the library the depot went to; steamcmd's root otherwise) */
		if (library)
			snprintf(root, sizeof(root), "%.*s", (int)(library - cmd.depots[index]), cmd.depots[index]);
		else
			snprintf(root, sizeof(root), "%s", download_folder[0] ? download_folder : cmd.root);
		snprintf(target, sizeof(target), "%s/" MCC_FOLDER, root);
		if (!steam_bridge_platform_merge_folder(cmd.depots[index], target))
			return 0;
	}
	return cmd.depot_count == 1;
}

static void job_end(int ok)
{
	if (cmd.child)
		steam_bridge_platform_child_end(cmd.child);
	cmd.child = NULL;
	if (cmd.job == JOB_ITEM)
	{
		struct steam_bridge_item *item = known(cmd.job_id);

		if (item && item->state != STEAM_BRIDGE_ITEM_INSTALLED)
			item->state = cmd.sign_in ? STEAM_BRIDGE_ITEM_NONE : STEAM_BRIDGE_ITEM_FAILED;
	}
	else if (cmd.job == JOB_MCC)
	{
		cmd.mcc_state = ok && !cmd.sign_in && mcc_place() ? STEAM_BRIDGE_ITEM_INSTALLED :
			cmd.sign_in ? STEAM_BRIDGE_ITEM_NONE : STEAM_BRIDGE_ITEM_FAILED;
		platform_log("Workshop: MCC's Halo CE files %s", cmd.mcc_state == STEAM_BRIDGE_ITEM_INSTALLED ?
			"installed" : "not installed");
	}
	cmd.job = JOB_NONE;
}

static void cmd_tick(void)
{
	char line[1024];
	int lines = 0, result = 0;

	if (!cmd.child)
	{
		if (cmd.job != JOB_NONE)
			job_end(0);
		job_start();
		return;
	}
	while (lines++ < 256 && (result = steam_bridge_platform_child_line(cmd.child, line, sizeof(line), 0)) > 0)
	{
		struct steamcmd_line read;
		struct steam_bridge_item *item;

		steamcmd_read_line(line, &read);
		switch (read.kind)
		{
		case STEAMCMD_LINE_ITEM_DONE:
			if ((item = known(read.id)) != NULL)
				item->state = STEAM_BRIDGE_ITEM_INSTALLED;
			platform_log("Workshop: steamcmd downloaded item %s", read.id);
			break;
		case STEAMCMD_LINE_ITEM_FAILED:
			if ((item = known(read.id)) != NULL)
				item->state = STEAM_BRIDGE_ITEM_FAILED;
			break;
		case STEAMCMD_LINE_DEPOT_DONE:
			if (cmd.depot_count < 2)
				snprintf(cmd.depots[cmd.depot_count++], sizeof(cmd.depots[0]), "%s", read.path);
			break;
		case STEAMCMD_LINE_SIGN_IN:
			cmd.sign_in = 1;
			break;
		case STEAMCMD_LINE_PROGRESS:
			if (cmd.job == JOB_ITEM && (item = known(cmd.job_id)) != NULL)
			{
				item->downloaded = (unsigned long long)read.percent;
				item->total = 100;
			}
			break;
		}
	}
	if (result < 0)
		job_end(1);
	else if (cmd.sign_in)
		job_end(0);
	else if (system_milliseconds() - cmd.job_start >= (unsigned long)(cmd.job == JOB_MCC ?
		STEAM_BRIDGE_INSTALL_TIMEOUT : STEAM_BRIDGE_DOWNLOAD_TIMEOUT))
	{
		platform_log("Workshop: steamcmd took too long");
		job_end(0);
	}
}

static void cmd_disconnect(void)
{
	/* (the job under way finishes, within its time; the queue is dropped) */
	cmd.queue_count = 0;
	cmd.mcc_wanted = 0;
	while (cmd.child)
	{
		cmd_tick();
		if (cmd.child)
			steam_bridge_platform_sleep(STEAM_BRIDGE_POLL_INTERVAL);
	}
	if (cmd.job != JOB_NONE)
		job_end(0);
}

static int cmd_subscribe(unsigned long long id)
{
	char text[STEAM_BRIDGE_ID_SIZE];
	struct steam_bridge_item *item;
	int index;

	snprintf(text, sizeof(text), "%llu", id);
	if (cmd.job == JOB_ITEM && !strcmp(cmd.job_id, text))
		return 1;
	for (index = 0; index < cmd.queue_count; index++)
	{
		if (!strcmp(cmd.queue[index], text))
			return 1;
	}
	if (cmd.queue_count >= MAXIMUM_QUEUE || !(item = known(text)))
		return 0;
	snprintf(cmd.queue[cmd.queue_count++], sizeof(cmd.queue[0]), "%s", text);
	item->state = STEAM_BRIDGE_ITEM_QUEUED;
	return 1;
}

static int cmd_query(unsigned long long id, struct steam_bridge_item *item)
{
	int index;

	for (index = 0; index < cmd.known_count; index++)
	{
		if (cmd.known[index].id == id && cmd.known[index].item.state != STEAM_BRIDGE_ITEM_UNKNOWN)
		{
			*item = cmd.known[index].item;
			return 1;
		}
	}
	/* (unknown: the folder watch tells) */
	return 0;
}

static int cmd_needs_sign_in(void)
{
	return cmd.sign_in;
}

static int cmd_install_mcc(void)
{
	if (cmd.job == JOB_MCC)
		return 1;
	cmd.mcc_wanted = 1;
	cmd.mcc_state = STEAM_BRIDGE_ITEM_QUEUED;
	return 1;
}

static int cmd_mcc_state(void)
{
	return cmd.mcc_state;
}

int steamcmd_signed_in(void)
{
	static unsigned long looked;
	static int signed_in, ever;
	unsigned long now = system_milliseconds();
	char steamcmd[STEAM_BRIDGE_PATH_SIZE], root[STEAM_BRIDGE_PATH_SIZE], account[64];
	const char *setting = config_string("game.steamcmd_account");

	if (ever && now - looked < 3000)
		return signed_in;
	ever = 1;
	looked = now;
	signed_in = 0;
	if (setting && setting[0])
		return signed_in = steamcmd_account_valid(setting);
	if (steam_bridge_platform_find_steamcmd(steamcmd, sizeof(steamcmd)) &&
		steam_bridge_platform_steamcmd_root(steamcmd, root, sizeof(root)))
	{
		char path[STEAM_BRIDGE_PATH_SIZE + 32], *text;
		FILE *file;
		long length;

		snprintf(path, sizeof(path), "%s/config/config.vdf", root);
		if ((file = fopen(path, "rb")) != NULL)
		{
			text = malloc(256 * 1024);
			length = text ? (long)fread(text, 1, 256 * 1024, file) : 0;
			fclose(file);
			signed_in = length > 0 && steamcmd_read_account(text, (int)length, account, sizeof(account));
			free(text);
		}
	}
	return signed_in;
}

/* the item's folder in the game's own library; 1 if it is there */
static int own_folder(unsigned long long id, char *path, int size)
{
	return download_folder[0] &&
		snprintf(path, (size_t)size, "%s/steamapps/workshop/content/" MCC_APP_ID "/%llu", download_folder, id) < size &&
		steam_bridge_platform_is_folder(path);
}

static int cmd_holds(unsigned long long id)
{
	char path[STEAM_BRIDGE_PATH_SIZE + 64];

	return own_folder(id, path, sizeof(path));
}

/* REMOVE: steamcmd's copy deleted (never anything outside the game's own library) */
static int cmd_remove(unsigned long long id)
{
	char path[STEAM_BRIDGE_PATH_SIZE + 64];
	int index;

	if (!own_folder(id, path, sizeof(path)))
		return 0;
	for (index = 0; index < cmd.known_count; index++)
	{
		if (cmd.known[index].id == id)
			memset(&cmd.known[index].item, 0, sizeof(cmd.known[index].item));
	}
	platform_log("Workshop: removing item %llu", id);
	return steam_bridge_platform_remove_folder(path);
}

/* UPDATE: downloaded again, over its copy */
static int cmd_update(unsigned long long id)
{
	return cmd_subscribe(id);
}

const struct steam_bridge_backend steam_bridge_cmd_backend =
{
	cmd_connect,
	cmd_disconnect,
	cmd_subscribe,
	NULL,
	cmd_query,
	cmd_tick,
	cmd_needs_sign_in,
	cmd_install_mcc,
	cmd_mcc_state,
	cmd_remove,
	cmd_update,
	cmd_holds,
};

#endif

#ifdef _WIN32
int steamcmd_signed_in(void)
{
	return 0;
}

/* (the built-in Steam download's folder: steamcmd isn't used here) */
void steam_bridge_set_download_folder(const char *path)
{
	steam_bridge_pipe_set_folder(path);
}
#endif
#endif /* HALO_MCC */
