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
#include <stdlib.h>
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
	/* (each call's own: the Workshop reads answers on several threads at once) */
	char text[LONGEST_TEXT];
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

/* a name's tag of Steam's BBCode at text ("[b]", "[/url]", "[url=...]"):
its length, or 0 when it is not one (a "[H1]" of a title's is, so is
"[h1]": Steam's headings) */
static int bbcode_tag(const char *text)
{
	static const char *const names[] = {"b", "i", "u", "s", "strike", "spoiler", "noparse", "hr", "h1", "h2", "h3",
		"url", "img", "previewyoutube", "list", "olist", "*", "quote", "code", "table", "tr", "th", "td"};
	const char *at = text + 1, *end;
	int index;

	if (*at == '/')
		at++;
	for (index = 0; index < (int)(sizeof(names) / sizeof(names[0])); index++)
	{
		size_t length = strlen(names[index]), letter;

		for (letter = 0; letter < length; letter++)
		{
			char c = at[letter];

			if ((c >= 'A' && c <= 'Z' ? c + 32 : c) != names[index][letter])
				break;
		}
		if (letter < length || (at[length] != ']' && at[length] != '=' && at[length] != ' '))
			continue;
		/* (its end within 512 bytes, a [url=...]'s address and all: never a
		scan of the whole text for each "[") */
		for (end = at + length; *end && *end != ']' && end - text < 512; end++)
			;
		return *end == ']' ? (int)(end - text) + 1 : 0;
	}
	return 0;
}

/* whether text starts with name, any case (0: it does, as strncmp says) */
static int names_match(const char *text, const char *name)
{
	for (; *name; text++, name++)
	{
		if ((*text >= 'A' && *text <= 'Z' ? *text + 32 : *text) != *name)
			return 1;
	}
	return 0;
}

/* a description made plain: Steam's BBCode tags out ([url=...]'s words
kept; a picture's or a video's address dropped with its tags), its line
breaks kept (the Guide's page shows its paragraphs). Longer than size: cut
whole characters, ending "..."; so too when more (the text came cut) */
static void plain_description(char *out, int size, const char *text, int more)
{
	/* (a picture's or a video's tag found unclosed once: not looked for
	again, the text read once whatever its tags) */
	int unclosed[2] = {0, 0};
	int length = 0;

	if (size < 4)
	{
		if (size > 0)
			out[0] = 0;
		return;
	}
	while (*text)
	{
		int tag;

		if (*text == '[' && (tag = bbcode_tag(text)) > 0)
		{
			char lower[16];
			int index;

			for (index = 0; index < 15 && text[1 + index] && text[1 + index] != ']' && text[1 + index] != '=' &&
				text[1 + index] != ' '; index++)
				lower[index] = (char)(text[1 + index] >= 'A' && text[1 + index] <= 'Z' ? text[1 + index] + 32 :
					text[1 + index]);
			lower[index] = 0;
			/* (a list's item or a rule starts a line) */
			if ((!strcmp(lower, "*") || !strcmp(lower, "hr")) && length > 0 && out[length - 1] != '\n' &&
				length < size - 1)
				out[length++] = '\n';
			text += tag;
			/* (nothing to read in a picture or a video: up to its closing tag) */
			if ((!strcmp(lower, "img") && !unclosed[0]) || (!strcmp(lower, "previewyoutube") && !unclosed[1]))
			{
				const char *close;

				for (close = text; *close; close++)
				{
					if (close[0] == '[' && close[1] == '/' && (tag = bbcode_tag(close)) > 0 &&
						(int)strlen(lower) + 3 == tag && !names_match(close + 2, lower))
					{
						text = close + tag;
						break;
					}
				}
				if (!*close)
					unclosed[lower[0] == 'p'] = 1;
			}
			continue;
		}
		if (*text == '\r')
		{
			text++;
			continue;
		}
		if (length >= size - 1)
		{
			more = 1;
			break;
		}
		out[length++] = *text == '\t' ? ' ' : *text;
		text++;
	}
	if (more)
	{
		/* (room for the dots, then back to a character's start) */
		int written = length;

		if (length > size - 4)
			length = size - 4;
		while (length > 0 && length < written && (((unsigned char)out[length]) & 0xC0) == 0x80)
			length--;
		while (length > 0 && (out[length - 1] == ' ' || out[length - 1] == '\n'))
			length--;
		memcpy(out + length, "...", 3);
		length += 3;
	}
	else
	{
		/* (a last character cut short: out) */
		int start = length;

		while (start > 0 && length - start < 4 && (((unsigned char)out[start - 1]) & 0xC0) == 0x80)
			start--;
		if (start > 0 && ((unsigned char)out[start - 1]) >= 0xC0)
		{
			unsigned char lead = (unsigned char)out[start - 1];
			int need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;

			if (length - (start - 1) < need)
				length = start - 1;
		}
	}
	out[length] = 0;
}

/* a code point as UTF-8 at out: its length (0: no room) */
static int code_point_utf8(char *out, int room, unsigned long code)
{
	if (code < 0x80 && room >= 1)
	{
		out[0] = (char)code;
		return 1;
	}
	if (code < 0x800 && room >= 2)
	{
		out[0] = (char)(0xC0 | (code >> 6));
		out[1] = (char)(0x80 | (code & 0x3F));
		return 2;
	}
	if (code >= 0x800 && code < 0x10000 && room >= 3)
	{
		out[0] = (char)(0xE0 | (code >> 12));
		out[1] = (char)(0x80 | ((code >> 6) & 0x3F));
		out[2] = (char)(0x80 | (code & 0x3F));
		return 3;
	}
	if (code >= 0x10000 && room >= 4)
	{
		out[0] = (char)(0xF0 | (code >> 18));
		out[1] = (char)(0x80 | ((code >> 12) & 0x3F));
		out[2] = (char)(0x80 | ((code >> 6) & 0x3F));
		out[3] = (char)(0x80 | (code & 0x3F));
		return 4;
	}
	return 0;
}

/* four hex digits at at: their value, or -1 */
static long hex_four(const char *at, const char *end)
{
	long value = 0;
	int index;

	if (end - at < 4)
		return -1;
	for (index = 0; index < 4; index++)
	{
		int digit = hex_digit(at[index]);

		if (digit < 0)
			return -1;
		value = value * 16 + digit;
	}
	return value;
}

/* a long string (a description) read into text, as much as fits, whole
characters (*more: it went on); any \u escape, a pair of them too, as its
character (a lone half: U+FFFD); a broken escape or a control character as
a space. 0 only when it isn't a JSON string or doesn't end */
static int long_string(struct json *json, char *text, int size, int *more)
{
	int length = 0;

	*more = 0;
	if (!take(json, '"'))
		return 0;
	while (json->at < json->end)
	{
		unsigned char c = (unsigned char)*json->at++;
		char piece[4];
		int count = 1;

		if (c == '"')
		{
			text[length] = 0;
			return 1;
		}
		/* (a control character, never in sound JSON: a space, the rest of
		the document still read) */
		piece[0] = c < 0x20 ? ' ' : (char)c;
		if (c == '\\')
		{
			if (json->at >= json->end)
				return 0;
			c = (unsigned char)*json->at++;
			switch (c)
			{
			case '"': case '\\': case '/': piece[0] = (char)c; break;
			case 'n': piece[0] = '\n'; break;
			case 't': piece[0] = '\t'; break;
			case 'r': piece[0] = '\r'; break;
			case 'b': case 'f': piece[0] = ' '; break;
			case 'u':
			{
				long code = hex_four(json->at, json->end), low;

				/* (not four hex digits: a space, what follows read as it is) */
				if (code < 0)
				{
					piece[0] = ' ';
					break;
				}
				json->at += 4;
				if (code >= 0xD800 && code < 0xDC00 && json->end - json->at >= 6 && json->at[0] == '\\' &&
					json->at[1] == 'u' && (low = hex_four(json->at + 2, json->end)) >= 0xDC00 && low < 0xE000)
				{
					json->at += 6;
					code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
				}
				else if (code >= 0xD800 && code < 0xE000)
					code = 0xFFFD;
				/* (a control character: a space, as a tab is) */
				if (code < 0x20 && code != '\n' && code != '\r')
					code = ' ';
				count = code_point_utf8(piece, 4, (unsigned long)code);
				break;
			}
			/* (an escape JSON hasn't: a space) */
			default: piece[0] = ' '; break;
			}
		}
		else if (c >= 0x80)
		{
			/* (a raw UTF-8 character: kept whole, or not at all) */
			int need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1, index;

			for (index = 1; index < need && json->at < json->end && ((unsigned char)*json->at & 0xC0) == 0x80;
				index++)
				piece[index] = *json->at++;
			count = index;
		}
		if (*more || length + count > size - 1)
		{
			*more = 1;
			continue;
		}
		memcpy(text + length, piece, (size_t)count);
		length += count;
	}
	return 0;
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
static int read_details_fields(struct json *json, struct workshop_browser_item *item, int *kept, char **full)
{
	/* (each call's own: the Workshop reads answers on several threads at once) */
	char text[LONGEST_TEXT * 4];
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
				!strcmp(key, "description") || !strcmp(key, "file_size") || !strcmp(key, "preview_url") ||
				!strcmp(key, "creator")))
			{
				int more = 0;

				if (!strcmp(key, "description") ? !long_string(json, text, (int)sizeof(text), &more) :
					!lenient_string(json, text, (int)sizeof(text)))
					return 0;
				if (!strcmp(key, "publishedfileid"))
					copy_cut(item->id, sizeof(item->id), workshop_browser_id_valid(text) ? text : "");
				else if (!strcmp(key, "creator"))
					copy_cut(item->creator, sizeof(item->creator), workshop_browser_id_valid(text) ? text : "");
				else if (!strcmp(key, "title"))
					copy_cut(item->title, sizeof(item->title), text);
				else if (!strcmp(key, "description"))
				{
					plain_description(item->description, (int)sizeof(item->description), text, more);
					/* (the whole of it, for the item's page, when asked for) */
					if (full)
					{
						char *whole = malloc(WORKSHOP_BROWSER_FULL_DESCRIPTION_SIZE), *smaller;

						free(*full);
						*full = NULL;
						if (whole)
						{
							plain_description(whole, WORKSHOP_BROWSER_FULL_DESCRIPTION_SIZE, text, more);
							smaller = realloc(whole, strlen(whole) + 1);
							*full = smaller ? smaller : whole;
						}
					}
				}
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

/* one publishedfiledetails object; *kept when it is a Halo CE item of MCC's,
its whole plain description in *full (full: NULL, not asked for; the
caller's to free; NULL when there is none) */
static int read_details_item(struct json *json, struct workshop_browser_item *item, int *kept, char **full)
{
	int sound;

	if (full)
		*full = NULL;
	sound = read_details_fields(json, item, kept, full);
	if (full && (!sound || !*kept))
	{
		free(*full);
		*full = NULL;
	}
	return sound;
}

static int read_details_document(const char *text, int length, struct workshop_browser_item *items, int maximum,
	char **full)
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
						char *whole = NULL;
						int kept;

						if (!read_details_item(&json, &item, &kept, full ? &whole : NULL))
							return -1;
						if (kept && count < maximum)
						{
							if (full)
								full[count] = whole;
							items[count++] = item;
						}
						else
							free(whole);
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

int workshop_browser_read_details_full(const char *text, int length, struct workshop_browser_item *items,
	int maximum, char **full)
{
	int count, index;

	if (full)
	{
		for (index = 0; index < maximum; index++)
			full[index] = NULL;
	}
	count = read_details_document(text, length, items, maximum, full);
	/* (a document that broke off: nothing of it kept) */
	if (count < 0 && full)
	{
		for (index = 0; index < maximum; index++)
		{
			free(full[index]);
			full[index] = NULL;
		}
	}
	return count;
}

int workshop_browser_read_details(const char *text, int length, struct workshop_browser_item *items, int maximum)
{
	return workshop_browser_read_details_full(text, length, items, maximum, NULL);
}

/* the item ids of the page's links to items, each once, in the page's order
(the page's older form, without its list as data) */
static int browse_link_ids(const char *text, int length, char (*ids)[WORKSHOP_BROWSER_ID_SIZE], int maximum)
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

/* ---------- the browse page's data: authors and pages */

/* where needle first is in [at, end), or NULL */
static const char *find_in(const char *at, const char *end, const char *needle)
{
	size_t length = strlen(needle);

	for (; at && at < end && (size_t)(end - at) >= length; at++)
	{
		if (*at == *needle && !memcmp(at, needle, length))
			return at;
	}
	return NULL;
}

/* the digits at at into out (1 to size - 1 of them): how many, 0 for none or too many */
static int digits_at(const char *at, const char *end, char *out, int size)
{
	int count = 0;

	while (at + count < end && at[count] >= '0' && at[count] <= '9')
	{
		if (count >= size - 1)
			return 0;
		out[count] = at[count];
		count++;
	}
	out[count] = 0;
	return count;
}

static int hex4(const char *at, const char *end, unsigned *value)
{
	int index;

	*value = 0;
	if (end - at < 4)
		return 0;
	for (index = 0; index < 4; index++)
	{
		char c = (char)(at[index] | 0x20);

		*value <<= 4;
		if (at[index] >= '0' && at[index] <= '9')
			*value |= (unsigned)(at[index] - '0');
		else if (c >= 'a' && c <= 'f')
			*value |= (unsigned)(c - 'a' + 10);
		else
			return 0;
	}
	return 1;
}

/* a character's UTF-8 put at out + *length, whole or (no room: *full, and
nothing more written after it) not at all */
static void utf8_put(char *out, int *length, int size, int *full, unsigned code)
{
	char bytes[4];
	int count;

	if (code < 0x20 || *full)
		return;
	if (code < 0x80)
	{
		bytes[0] = (char)code;
		count = 1;
	}
	else if (code < 0x800)
	{
		bytes[0] = (char)(0xC0 | (code >> 6));
		bytes[1] = (char)(0x80 | (code & 0x3F));
		count = 2;
	}
	else if (code < 0x10000)
	{
		bytes[0] = (char)(0xE0 | (code >> 12));
		bytes[1] = (char)(0x80 | ((code >> 6) & 0x3F));
		bytes[2] = (char)(0x80 | (code & 0x3F));
		count = 3;
	}
	else
	{
		bytes[0] = (char)(0xF0 | (code >> 18));
		bytes[1] = (char)(0x80 | ((code >> 12) & 0x3F));
		bytes[2] = (char)(0x80 | ((code >> 6) & 0x3F));
		bytes[3] = (char)(0x80 | (code & 0x3F));
		count = 4;
	}
	if (*length + count > size - 1)
	{
		*full = 1;
		return;
	}
	memcpy(out + *length, bytes, (size_t)count);
	*length += count;
}

/* a \u escape's character (a surrogate pair's two, one character) at at,
just past its "u": how many bytes it took, 0 when it isn't one */
static int unicode_escape(const char *at, const char *end, const char *pair, unsigned *code)
{
	unsigned low;
	size_t pair_length = strlen(pair);

	if (!hex4(at, end, code))
		return 0;
	if (*code < 0xD800 || *code > 0xDFFF)
		return 4;
	/* (the high half, then the low half after the same escape) */
	if (*code <= 0xDBFF && (size_t)(end - at) >= 4 + pair_length + 4 && !memcmp(at + 4, pair, pair_length) &&
		hex4(at + 4 + pair_length, end, &low) && low >= 0xDC00 && low <= 0xDFFF)
	{
		*code = 0x10000 + ((*code - 0xD800) << 10) + (low - 0xDC00);
		return 4 + (int)pair_length + 4;
	}
	*code = '?';
	return 4;
}

/* the length of the UTF-8 character at at (before end): 1 to 4, or 0 where
it is not one (a byte that starts none, a byte after it that does not go on
one, an overlong form, a surrogate, past U+10FFFF) */
static int utf8_length(const char *at, const char *end)
{
	unsigned char lead = (unsigned char)at[0], second;
	int count, index;

	if (lead < 0x80)
		return 1;
	count = lead >= 0xC2 && lead <= 0xDF ? 2 : lead >= 0xE0 && lead <= 0xEF ? 3 : lead >= 0xF0 && lead <= 0xF4 ? 4 : 0;
	if (!count || end - at < count)
		return 0;
	for (index = 1; index < count; index++)
	{
		if (((unsigned char)at[index] & 0xC0) != 0x80)
			return 0;
	}
	second = (unsigned char)at[1];
	if ((lead == 0xE0 && second < 0xA0) || (lead == 0xED && second > 0x9F) || (lead == 0xF0 && second < 0x90) ||
		(lead == 0xF4 && second > 0x8F))
	{
		return 0;
	}
	return count;
}

/* a string of the page's data, which sits in a script's string: its quotes
\", its own escapes doubled (\\\" a quote, \\\\ a backslash, \\u a
character) and the script's \u too. Read from at (past its opening \") into
out, cut whole characters to size; 1 when it ended where it should. A byte
that is not valid UTF-8 is read as '?', as a lone surrogate's \u is */
static int page_string(const char *at, const char *end, char *out, int size)
{
	int length = 0, full = 0;

	while (at < end)
	{
		unsigned code;
		int taken;

		if (*at != '\\')
		{
			unsigned char lead = (unsigned char)*at;
			int count = utf8_length(at, end);

			if (*at == '"')
				return 0;
			if (!count)
			{
				utf8_put(out, &length, size, &full, '?');
				at++;
				continue;
			}
			if (lead >= 0x20 && !full && length + count <= size - 1)
			{
				memcpy(out + length, at, (size_t)count);
				length += count;
			}
			else if (lead >= 0x20)
				full = 1;
			at += count;
			continue;
		}
		if (end - at < 2)
			return 0;
		switch (at[1])
		{
		case '"':
			out[length] = 0;
			return 1;
		case 'u':
			if (!(taken = unicode_escape(at + 2, end, "\\u", &code)))
				return 0;
			utf8_put(out, &length, size, &full, code);
			at += 2 + taken;
			continue;
		case '/':
			utf8_put(out, &length, size, &full, '/');
			at += 2;
			continue;
		case 'n': case 'r': case 't': case 'b': case 'f':
			utf8_put(out, &length, size, &full, ' ');
			at += 2;
			continue;
		case '\\':
			break;
		default:
			return 0;
		}
		/* (the data's own escape, doubled) */
		if (end - at < 3)
			return 0;
		if (at[2] == '\\' && end - at >= 4 && (at[3] == '"' || at[3] == '\\'))
		{
			utf8_put(out, &length, size, &full, (unsigned char)at[3]);
			at += 4;
		}
		else if (at[2] == 'u' && (taken = unicode_escape(at + 3, end, "\\\\u", &code)) != 0)
		{
			utf8_put(out, &length, size, &full, code);
			at += 3 + taken;
		}
		else if (at[2] == '/' || at[2] == 'n' || at[2] == 'r' || at[2] == 't' || at[2] == 'b' || at[2] == 'f')
		{
			utf8_put(out, &length, size, &full, at[2] == '/' ? '/' : ' ');
			at += 3;
		}
		else
			return 0;
	}
	return 0;
}

/* the end of the page's array whose first item is at (past its "["): its
closing "]", found as the data's brackets nest, the brackets inside its
strings left out (the page's data sits in a script's string: a data string
is between \" and \", and a quote inside one is \\\"); end if it has none */
static const char *page_array_end(const char *at, const char *end)
{
	int depth = 1, in_string = 0, escaped = 0;

	while (at < end)
	{
		char character = *at;

		/* (the script's escapes: \" a quote, \\ a backslash, \uXXXX and
		the others a character that is no bracket) */
		if (character == '\\')
		{
			if (end - at < 2)
				return end;
			character = at[1] == '"' || at[1] == '\\' ? at[1] : 'x';
			at += at[1] == 'u' ? (end - at >= 6 ? 6 : end - at) : 2;
		}
		else
			at++;
		if (in_string)
		{
			if (escaped)
				escaped = 0;
			else if (character == '\\')
				escaped = 1;
			else if (character == '"')
				in_string = 0;
			continue;
		}
		if (character == '"')
			in_string = 1;
		else if (character == '[')
			depth++;
		else if (character == ']' && !--depth)
			return at - 1;
	}
	return end;
}

int workshop_browser_read_browse_authors(const char *text, int length, struct workshop_browser_author *authors,
	int maximum)
{
	static const char item_key[] = "\\\"publishedfileid\\\":\\\"", creator_key[] = "\\\",\\\"creator\\\":\\\"";
	static const char player_key[] = "\\\"steamid\\\":\\\"", name_key[] = "\\\"persona_name\\\":\\\"";
	const char *end, *at, *results, *results_end, *players;
	int count = 0, index;

	if (!text || length <= 0 || maximum <= 0)
		return 0;
	end = text + length;
	/* (the list's items, each with its creator right after its id: nothing
	else on the page, such as an item named in a description, is taken) */
	if (!(results = find_in(text, end, "\\\"results\\\":[")))
		return 0;
	/* (nor an id and creator after the list: up to its "]") */
	results_end = page_array_end(results + sizeof("\\\"results\\\":[") - 1, end);
	for (at = results; count < maximum && (at = find_in(at, results_end, item_key)) != NULL;)
	{
		struct workshop_browser_author *author = &authors[count];
		int digits;

		at += sizeof(item_key) - 1;
		if (!(digits = digits_at(at, results_end, author->id, (int)sizeof(author->id))))
			continue;
		at += digits;
		if ((size_t)(results_end - at) < sizeof(creator_key) - 1 || memcmp(at, creator_key, sizeof(creator_key) - 1))
			continue;
		at += sizeof(creator_key) - 1;
		if (!digits_at(at, results_end, author->creator, (int)sizeof(author->creator)))
			continue;
		author->name[0] = 0;
		for (index = 0; index < count && strcmp(authors[index].id, author->id); index++)
			;
		if (index == count)
			count++;
	}
	/* (the creators' names, each beside its id) */
	players = find_in(results, end, "\\\"creator_player_link_details\\\":[");
	for (at = players; at && (at = find_in(at, end, player_key)) != NULL;)
	{
		char steam_id[WORKSHOP_BROWSER_ID_SIZE], name[WORKSHOP_BROWSER_AUTHOR_SIZE];
		const char *next, *name_at;
		int digits;

		at += sizeof(player_key) - 1;
		if (!(digits = digits_at(at, end, steam_id, (int)sizeof(steam_id))))
			continue;
		at += digits;
		next = find_in(at, end, player_key);
		name_at = find_in(at, next ? next : end, name_key);
		if (!name_at || !page_string(name_at + sizeof(name_key) - 1, end, name, (int)sizeof(name)) || !name[0])
			continue;
		for (index = 0; index < count; index++)
		{
			if (!strcmp(authors[index].creator, steam_id) && !authors[index].name[0])
				copy_cut(authors[index].name, sizeof(authors[index].name), name);
		}
	}
	return count;
}

int workshop_browser_read_browse_ids(const char *text, int length, char (*ids)[WORKSHOP_BROWSER_ID_SIZE], int maximum)
{
	struct workshop_browser_author listed[64];
	int count, index;

	if (!text || length <= 0 || maximum <= 0)
		return 0;
	/* (the page's own list, where it has one: its items alone, in its order,
	never an item that a description links to) */
	count = workshop_browser_read_browse_authors(text, length, listed,
		maximum < (int)(sizeof(listed) / sizeof(listed[0])) ? maximum : (int)(sizeof(listed) / sizeof(listed[0])));
	if (count <= 0)
		return browse_link_ids(text, length, ids, maximum);
	for (index = 0; index < count; index++)
		memcpy(ids[index], listed[index].id, sizeof(ids[index]));
	return count;
}

int workshop_browser_read_browse_pages(const char *text, int length, int *pages, int *total)
{
	static const char pages_key[] = "\\\"total_pages\\\":", total_key[] = "\\\"total_count\\\":";
	const char *end, *at;
	char digits[12];
	int found_pages, found_total = -1;

	if (!text || length <= 0)
		return 0;
	end = text + length;
	if (!(at = find_in(text, end, pages_key)) || !digits_at(at + sizeof(pages_key) - 1, end, digits, 10))
		return 0;
	found_pages = atoi(digits);
	/* (no more pages than the browser goes to, whatever the page says) */
	if (found_pages > WORKSHOP_BROWSER_MAXIMUM_PAGES)
		found_pages = WORKSHOP_BROWSER_MAXIMUM_PAGES;
	if ((at = find_in(text, end, total_key)) != NULL && digits_at(at + sizeof(total_key) - 1, end, digits, 10))
		found_total = atoi(digits);
	*pages = found_pages;
	if (found_total >= 0)
		*total = found_total;
	return 1;
}

int workshop_browser_read_profile_name(const char *text, int length, char *name, int size)
{
	static const char open[] = "<steamID><![CDATA[", close[] = "]]></steamID>";
	const char *end, *at, *stop;
	int out = 0;

	if (!text || length <= 0 || size <= 0)
		return 0;
	name[0] = 0;
	end = text + length;
	if (!(at = find_in(text, end, open)))
		return 0;
	at += sizeof(open) - 1;
	if (!(stop = find_in(at, end, close)))
		return 0;
	/* (control characters left out; cut whole characters) */
	for (; at < stop && out < size - 1; at++)
	{
		if ((unsigned char)*at >= 0x20 && *at != 0x7F)
			name[out++] = *at;
	}
	/* (cut inside a character: the whole character left out) */
	if (at < stop && (((unsigned char)*at) & 0xC0) == 0x80)
	{
		while (out > 0 && (((unsigned char)name[out - 1]) & 0xC0) == 0x80)
			out--;
		if (out > 0)
			out--;
	}
	name[out] = 0;
	return out > 0;
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
