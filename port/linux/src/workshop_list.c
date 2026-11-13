/*
WORKSHOP_LIST.C

The WORKSHOP section's list document and words (workshop_browser.h): no
game, SDL or file system.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/
#ifndef HALO_MCC
#include "../include/halo_port_features.h"
#endif
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h, above): a console builds this to nothing */

#define WORKSHOP_LIST_ONLY
#include "workshop_browser.h"
#include "json_reader.h"

#include <stdio.h>
#include <string.h>

enum
{
	/* a string read whole before it is cut to its field */
	LONGEST_TEXT = 4096,
	ITEM_VERSION = 1,
};

/* text cut to size, not inside a UTF-8 character */
static void copy_cut(char *out, int size, const char *text)
{
	int length = (int)strlen(text);

	if (length >= size)
	{
		length = size - 1;
		while (length > 0 && (((unsigned char)text[length]) & 0xC0) == 0x80)
			length--;
	}
	memcpy(out, text, (size_t)length);
	out[length] = 0;
}

int workshop_browser_id_valid(const char *text)
{
	int length = 0;

	if (!text)
		return 0;
	for (; text[length]; length++)
	{
		if (text[length] < '0' || text[length] > '9')
			return 0;
	}
	return length >= 1 && length <= 20;
}

/* one item's object into item: 1 when it is sound JSON (kept or not: *kept) */
static int read_item(struct json *json, struct workshop_browser_item *item, int *kept)
{
	static char text[LONGEST_TEXT];
	char key[32];
	int has_title = 0;

	*kept = 0;
	memset(item, 0, sizeof(*item));
	item->size = -1;
	if (!take(json, '{'))
		return 0;
	if (!take(json, '}'))
	{
		do
		{
			long long number;

			if (!json_string(json, key, sizeof(key)) || !take(json, ':'))
				return 0;
			if (!strcmp(key, "id") || !strcmp(key, "title") || !strcmp(key, "author") || !strcmp(key, "kind") ||
				!strcmp(key, "description"))
			{
				/* (a value of another type: passed over; an id of another type
				leaves the item without one, so it is passed over too) */
				if (!peek(json, '"'))
				{
					if (!json_skip(json, 2))
						return 0;
					if (!strcmp(key, "id"))
						item->id[0] = 0;
					continue;
				}
				if (!json_string(json, text, sizeof(text)))
					return 0;
				if (!strcmp(key, "id"))
					copy_cut(item->id, sizeof(item->id), workshop_browser_id_valid(text) ? text : "");
				else if (!strcmp(key, "title"))
				{
					copy_cut(item->title, sizeof(item->title), text);
					has_title = item->title[0] != 0;
				}
				else if (!strcmp(key, "author"))
					copy_cut(item->author, sizeof(item->author), text);
				else if (!strcmp(key, "kind"))
					copy_cut(item->kind, sizeof(item->kind), text);
				else
					copy_cut(item->description, sizeof(item->description), text);
			}
			else if (!strcmp(key, "size") || !strcmp(key, "updated"))
			{
				if (!json_integer(json, 0, 1LL << 52, &number))
				{
					if (!json_skip(json, 2))
						return 0;
					continue;
				}
				if (!strcmp(key, "size"))
					item->size = number;
				else
					item->updated = number;
			}
			else if (!json_skip(json, 2))
				return 0;
		} while (take(json, ','));
		if (!take(json, '}'))
			return 0;
	}
	*kept = item->id[0] && has_title;
	return 1;
}

int workshop_browser_read_list(const char *text, int length, struct workshop_browser_item *items, int maximum)
{
	struct json json;
	char key[32];
	long long version = 0;
	int count = 0, has_items = 0;

	if (!text || length <= 0 || length > WORKSHOP_BROWSER_MAXIMUM_DOCUMENT)
		return -1;
	json.at = text;
	json.end = text + length;
	if (!take(&json, '{'))
		return -1;
	if (!take(&json, '}'))
	{
		do
		{
			if (!json_string(&json, key, sizeof(key)) || !take(&json, ':'))
				return -1;
			if (!strcmp(key, "version"))
			{
				if (!json_integer(&json, 0, 1000000, &version))
					return -1;
			}
			else if (!strcmp(key, "items"))
			{
				has_items = 1;
				count = 0;
				if (!take(&json, '['))
					return -1;
				if (!take(&json, ']'))
				{
					do
					{
						struct workshop_browser_item item;
						int kept, other;

						if (!read_item(&json, &item, &kept))
							return -1;
						/* (each id once: the first) */
						for (other = 0; kept && other < count && strcmp(items[other].id, item.id); other++)
							;
						if (kept && other == count && count < maximum)
							items[count++] = item;
					} while (take(&json, ','));
					if (!take(&json, ']'))
						return -1;
				}
			}
			else if (!json_skip(&json, 1))
				return -1;
		} while (take(&json, ','));
		if (!take(&json, '}'))
			return -1;
	}
	skip_space(&json);
	if (json.at != json.end || version != ITEM_VERSION || !has_items)
		return -1;
	return count;
}

/* ---------- Steam's own details (ISteamRemoteStorage/GetPublishedFileDetails) */

/* a description made plain: BBCode's [tags] out, line ends as spaces */
static void plain_description(char *out, int size, const char *text)
{
	int length = 0;

	while (*text && length < size - 1)
	{
		if (*text == '[')
		{
			const char *close = strchr(text, ']');

			/* (an image's or a link's address goes with its tags) */
			if (close && close - text < 16)
			{
				int image = !strncmp(text, "[img]", 5) || !strncmp(text, "[IMG]", 5);

				text = close + 1;
				if (image && (close = strstr(text, "[/")) != NULL)
					text = strchr(close, ']') ? strchr(close, ']') + 1 : close;
				continue;
			}
		}
		if (*text == '\r' || *text == '\n' || *text == '\t')
		{
			if (length > 0 && out[length - 1] != ' ')
				out[length++] = ' ';
			text++;
			continue;
		}
		out[length++] = *text++;
	}
	/* (not inside a UTF-8 character) */
	while (length > 0 && (((unsigned char)out[length - 1]) & 0xC0) == 0xC0)
		length--;
	out[length] = 0;
}

static int lenient_string(struct json *json, char *text, int size);

/* a tags array: its Halo1, Campaign and Multiplayer tags */
static int read_tags(struct json *json, int *halo1, int *campaign, int *multiplayer)
{
	char key[16], tag[64];

	if (!take(json, '['))
		return 0;
	if (take(json, ']'))
		return 1;
	do
	{
		if (!take(json, '{'))
			return 0;
		if (!take(json, '}'))
		{
			do
			{
				if (!json_string(json, key, sizeof(key)) || !take(json, ':'))
					return 0;
				if (!strcmp(key, "tag") && peek(json, '"'))
				{
					if (!lenient_string(json, tag, (int)sizeof(tag)))
						return 0;
					*halo1 |= !strcmp(tag, "Halo1");
					*campaign |= !strcmp(tag, "Campaign");
					*multiplayer |= !strcmp(tag, "Multiplayer");
				}
				else if (!json_skip(json, 3))
					return 0;
			} while (take(json, ','));
			if (!take(json, '}'))
				return 0;
		}
	} while (take(json, ','));
	return take(json, ']');
}

/* a string read into text, or (too long, or an escape json_reader.h doesn't
take: a non-ASCII \u) passed over whole and text left empty */
static int lenient_string(struct json *json, char *text, int size)
{
	const char *start = json->at;

	if (json_string(json, text, (size_t)size))
		return 1;
	json->at = start;
	skip_space(json);
	if (!take(json, '"'))
		return 0;
	while (json->at < json->end)
	{
		char c = *json->at++;

		if (c == '\\' && json->at < json->end)
			json->at++;
		else if (c == '"')
		{
			text[0] = 0;
			return 1;
		}
	}
	return 0;
}

/* one publishedfiledetails object; *kept when it is a Halo CE item of MCC's */
static int read_details_item(struct json *json, struct workshop_browser_item *item, int *kept)
{
	static char text[LONGEST_TEXT * 4];
	char key[32];
	long long number, result = 0, app = 0;
	int halo1 = 0, campaign = 0, multiplayer = 0;

	*kept = 0;
	memset(item, 0, sizeof(*item));
	item->size = -1;
	if (!take(json, '{'))
		return 0;
	if (!take(json, '}'))
	{
		do
		{
			if (!json_string(json, key, sizeof(key)) || !take(json, ':'))
				return 0;
			if (!strcmp(key, "tags"))
			{
				if (!read_tags(json, &halo1, &campaign, &multiplayer))
					return 0;
			}
			else if (peek(json, '"') && (!strcmp(key, "publishedfileid") || !strcmp(key, "title") ||
				!strcmp(key, "description") || !strcmp(key, "file_size") || !strcmp(key, "preview_url")))
			{
				if (!lenient_string(json, text, (int)sizeof(text)))
					return 0;
				if (!strcmp(key, "publishedfileid"))
					copy_cut(item->id, sizeof(item->id), workshop_browser_id_valid(text) ? text : "");
				else if (!strcmp(key, "title"))
					copy_cut(item->title, sizeof(item->title), text);
				else if (!strcmp(key, "description"))
					plain_description(item->description, sizeof(item->description), text);
				else if (!strcmp(key, "preview_url"))
				{
					/* (Steam's own picture host, https, a plain address: nothing else is fetched) */
					static const char host[] = "https://images.steamusercontent.com/";
					int plain = !strncmp(text, host, sizeof(host) - 1) && strlen(text) < sizeof(item->preview_url);
					const char *c;

					for (c = text; plain && *c; c++)
						plain = *c > ' ' && *c < 127 && *c != '"' && *c != '\\';
					if (plain)
						snprintf(item->preview_url, sizeof(item->preview_url), "%s", text);
				}
				else
				{
					long long size = 0;
					const char *digit = text;

					for (; *digit >= '0' && *digit <= '9' && size < (1LL << 52); digit++)
						size = size * 10 + (*digit - '0');
					/* (0: Steam doesn't say, as for MCC's items) */
					item->size = !*digit && size > 0 ? size : -1;
				}
			}
			else if (!strcmp(key, "result") || !strcmp(key, "consumer_app_id") || !strcmp(key, "time_updated"))
			{
				if (!json_integer(json, 0, 1LL << 52, &number))
				{
					if (!json_skip(json, 2))
						return 0;
					continue;
				}
				if (!strcmp(key, "result"))
					result = number;
				else if (!strcmp(key, "consumer_app_id"))
					app = number;
				else
					item->updated = number;
			}
			else if (!json_skip(json, 3))
				return 0;
		} while (take(json, ','));
		if (!take(json, '}'))
			return 0;
	}
	/* (an item tagged both, a campaign that brings multiplayer maps: both,
	so that its row says why it is in the MULTIPLAYER list) */
	snprintf(item->kind, sizeof(item->kind), "%s", campaign && multiplayer ? "campaign+multiplayer" :
		campaign ? "campaign" : multiplayer ? "multiplayer" : "item");
	item->tags = (multiplayer ? WORKSHOP_TAG_MULTIPLAYER : 0) | (campaign ? WORKSHOP_TAG_CAMPAIGN : 0);
	/* (MCC's Workshop mixes its games: only Halo CE's, tagged Halo1) */
	*kept = item->id[0] && item->title[0] && result == 1 && app == 976730 && halo1;
	return 1;
}

int workshop_browser_read_details(const char *text, int length, struct workshop_browser_item *items, int maximum)
{
	struct json json;
	char key[32];
	int count = 0, found = 0;

	if (!text || length <= 0 || length > 2 * 1024 * 1024)
		return -1;
	json.at = text;
	json.end = text + length;
	if (!take(&json, '{') || !json_string(&json, key, sizeof(key)) || strcmp(key, "response") || !take(&json, ':') ||
		!take(&json, '{'))
	{
		return -1;
	}
	if (!take(&json, '}'))
	{
		do
		{
			if (!json_string(&json, key, sizeof(key)) || !take(&json, ':'))
				return -1;
			if (!strcmp(key, "publishedfiledetails"))
			{
				found = 1;
				if (!take(&json, '['))
					return -1;
				if (!take(&json, ']'))
				{
					do
					{
						struct workshop_browser_item item;
						int kept;

						if (!read_details_item(&json, &item, &kept))
							return -1;
						if (kept && count < maximum)
							items[count++] = item;
					} while (take(&json, ','));
					if (!take(&json, ']'))
						return -1;
				}
			}
			else if (!json_skip(&json, 2))
				return -1;
		} while (take(&json, ','));
		if (!take(&json, '}'))
			return -1;
	}
	if (!take(&json, '}'))
		return -1;
	return found ? count : -1;
}

int workshop_browser_read_browse_ids(const char *text, int length, char (*ids)[WORKSHOP_BROWSER_ID_SIZE], int maximum)
{
	static const char marker[] = "filedetails/?id=";
	const char *at = text, *end = text + (length > 0 ? length : 0);
	int count = 0;

	while (count < maximum && at < end)
	{
		const char *found = NULL, *search;
		char id[WORKSHOP_BROWSER_ID_SIZE];
		int digits = 0, other;

		for (search = at; search + sizeof(marker) - 1 <= end; search++)
		{
			if (!memcmp(search, marker, sizeof(marker) - 1))
			{
				found = search;
				break;
			}
		}
		if (!found)
			break;
		at = found + sizeof(marker) - 1;
		/* (only the digits: anything else on the page is never used) */
		while (at < end && *at >= '0' && *at <= '9' && digits < 21)
			id[digits++] = *at++;
		if (digits < 1 || digits > 20)
			continue;
		id[digits] = 0;
		for (other = 0; other < count && strcmp(ids[other], id); other++)
			;
		if (other == count)
			memcpy(ids[count++], id, (size_t)digits + 1);
	}
	return count;
}

int workshop_browser_steam_url(const char *id, char *url, int size)
{
	int written;

	if (!workshop_browser_id_valid(id))
		return 0;
	written = snprintf(url, (size_t)size, "steam://url/CommunityFilePage/%s", id);
	return written > 0 && written < size;
}

const char *workshop_browser_kind_name(const char *kind)
{
	if (!kind || !kind[0] || !strcmp(kind, "map"))
		return "Multiplayer";
	if (!strcmp(kind, "maps") || !strcmp(kind, "multiplayer"))
		return "Multiplayer";
	if (!strcmp(kind, "campaign"))
		return "Campaign";
	if (!strcmp(kind, "campaign+multiplayer"))
		return "Campaign + Multiplayer";
	if (!strcmp(kind, "gametypes"))
		return "Game types";
	if (!strcmp(kind, "game"))
		return "Game";
	if (!strcmp(kind, "setup"))
		return "Setup";
	return "Item";
}

const char *workshop_browser_state_name(int state)
{
	switch (state)
	{
	case WORKSHOP_ITEM_WAITING: return "Waiting";
	case WORKSHOP_ITEM_INSTALLING: return "Installing";
	case WORKSHOP_ITEM_INSTALLED: return "Installed";
	case WORKSHOP_ITEM_DOWNLOADING: return "Downloading";
	default: return "Not installed";
	}
}

const char *workshop_browser_filter_name(int filter)
{
	static const char *const names[] = { "All", "Multiplayer", "Campaign", "Installed" };

	return filter >= 0 && filter < WORKSHOP_FILTER_COUNT ? names[filter] : "All";
}

const char *workshop_browser_sort_name(int sort)
{
	static const char *const names[] = { "Popular", "New", "Top rated", "Updated", "A-Z" };

	return sort >= 0 && sort < WORKSHOP_SORT_COUNT ? names[sort] : "Popular";
}

const char *workshop_browser_step_name(int step)
{
	switch (step)
	{
	case WORKSHOP_STEP_READY: return "Ready";
	case WORKSHOP_STEP_WORKING: return "Working";
	case WORKSHOP_STEP_NOT_AVAILABLE: return "Not available";
	default: return "To do";
	}
}

void workshop_browser_size_text(long long size, char *text, int size_of_text)
{
	if (size < 0)
		text[0] = 0;
	else if (size >= 1024LL * 1024 * 1024)
		snprintf(text, (size_t)size_of_text, "%.1f GB", (double)size / (1024.0 * 1024.0 * 1024.0));
	else if (size >= 1024LL * 1024)
		snprintf(text, (size_t)size_of_text, "%lld MB", (size + 512 * 1024) / (1024 * 1024));
	else
		snprintf(text, (size_t)size_of_text, "%lld KB", (size + 1023) / 1024);
}
#endif /* HALO_MCC */
