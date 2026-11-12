/*
WORKSHOP_LIST.C

The WORKSHOP section's list document and words (workshop_browser.h): no
game, SDL or file system.
*/

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
		return "Map";
	if (!strcmp(kind, "maps"))
		return "Maps";
	if (!strcmp(kind, "campaign"))
		return "Campaign";
	if (!strcmp(kind, "gametypes"))
		return "Game types";
	if (!strcmp(kind, "game"))
		return "Game";
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
