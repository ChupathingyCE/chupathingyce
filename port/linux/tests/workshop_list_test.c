/*
WORKSHOP_LIST_TEST.C

Tests of the WORKSHOP section's list document and words
(port/linux/src/workshop_list.c, workshop_browser.h). Built and run by
tools/test_workshop_list.py; exits nonzero on a failure.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#define WORKSHOP_LIST_ONLY
#include "../src/workshop_browser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

static struct workshop_browser_item items[WORKSHOP_BROWSER_MAXIMUM_ITEMS];

static int read_text(const char *text)
{
	return workshop_browser_read_list(text, (int)strlen(text), items, WORKSHOP_BROWSER_MAXIMUM_ITEMS);
}

int main(void)
{
	char url[96], size[24];

	/* a whole item, and one with only what is required */
	CHECK(read_text("{\"version\":1,\"items\":[{\"id\":\"2962107814\",\"title\":\"Blood Gulch\",\"author\":\"Someone\","
		"\"kind\":\"map\",\"size\":48211968,\"updated\":1730764800,\"description\":\"A remake.\",\"later\":[1,{}]},"
		"{\"title\":\"Two\",\"id\":\"5\"}]}") == 2);
	CHECK(!strcmp(items[0].id, "2962107814"));
	CHECK(!strcmp(items[0].title, "Blood Gulch"));
	CHECK(!strcmp(items[0].author, "Someone"));
	CHECK(items[0].size == 48211968 && items[0].updated == 1730764800);
	CHECK(!strcmp(items[0].description, "A remake."));
	CHECK(items[1].size == -1 && !items[1].author[0]);
	/* items without a sound id or a title are passed over; an id once */
	CHECK(read_text("{\"items\":[{\"id\":\"12a\",\"title\":\"x\"},{\"id\":\"7\"},{\"id\":7,\"title\":\"x\"},"
		"{\"id\":\"8\",\"title\":\"y\",\"author\":null,\"size\":-3},{\"id\":\"8\",\"title\":\"z\"}],\"version\":1}") == 1);
	CHECK(!strcmp(items[0].id, "8") && !strcmp(items[0].title, "y"));
	/* other versions, broken JSON and trailing bytes are refused whole */
	CHECK(read_text("{\"version\":2,\"items\":[]}") == -1);
	CHECK(read_text("{\"items\":[]}") == -1);
	CHECK(read_text("{\"version\":1}") == -1);
	CHECK(read_text("{\"version\":1,\"items\":[{\"id\":\"1\",\"title\":\"a\"}]") == -1);
	CHECK(read_text("{\"version\":1,\"items\":[]} x") == -1);
	CHECK(read_text("[]") == -1);
	CHECK(read_text("{\"version\":1,\"items\":[]}") == 0);
	/* a long title is cut, not inside a UTF-8 character */
	{
		char text[400];
		int index;

		strcpy(text, "{\"version\":1,\"items\":[{\"id\":\"1\",\"title\":\"");
		for (index = 0; index < 100; index++)
			strcat(text, "\xC3\xA9");
		strcat(text, "\"}]}");
		CHECK(read_text(text) == 1);
		CHECK(strlen(items[0].title) == 158);
	}
	CHECK(workshop_browser_steam_url("2962107814", url, sizeof(url)) &&
		!strcmp(url, "steam://url/CommunityFilePage/2962107814"));
	CHECK(!workshop_browser_steam_url("29621&x", url, sizeof(url)));
	CHECK(!workshop_browser_id_valid("") && !workshop_browser_id_valid("123456789012345678901"));
	CHECK(!strcmp(workshop_browser_kind_name("gametypes"), "Game types"));
	CHECK(!strcmp(workshop_browser_kind_name("odd"), "Item"));
	CHECK(!strcmp(workshop_browser_kind_name("campaign+multiplayer"), "Campaign + Multiplayer"));
	CHECK(!strcmp(workshop_browser_state_name(WORKSHOP_ITEM_INSTALLING), "Installing"));
	workshop_browser_size_text(48211968, size, sizeof(size));
	CHECK(!strcmp(size, "46 MB"));
	workshop_browser_size_text(-1, size, sizeof(size));
	CHECK(!size[0]);
	CHECK(!strcmp(workshop_browser_state_name(WORKSHOP_ITEM_DOWNLOADING), "Downloading"));

	/* Steam's own details (GetPublishedFileDetails): only MCC's Halo CE items
	(tag Halo1) are kept; MCC's Workshop mixes its games */
	{
		static const char details[] =
			"{\"response\":{\"result\":1,\"resultcount\":4,\"publishedfiledetails\":["
			"{\"publishedfileid\":\"2906110850\",\"result\":1,\"creator\":\"7656\",\"creator_app_id\":1695795,"
			"\"consumer_app_id\":976730,\"file_size\":\"0\",\"title\":\"Aerial\",\"description\":\"A [b]map[/b]\\r\\n"
			"[img]https://x/y.png[/img]above \\u00e9 clouds\",\"time_updated\":1700000000,"
			"\"tags\":[{\"tag\":\"Halo1\"},{\"tag\":\"Multiplayer\"}]},"
			"{\"publishedfileid\":\"3538769508\",\"result\":1,\"consumer_app_id\":976730,\"title\":\"Coldsnap\","
			"\"tags\":[{\"tag\":\"Halo3\"},{\"tag\":\"Multiplayer\"}]},"
			"{\"publishedfileid\":\"2886943774\",\"result\":1,\"consumer_app_id\":976730,\"file_size\":\"2265580503\","
			"\"title\":\"Combat Revolved\",\"tags\":[{\"tag\":\"Halo1\"},{\"tag\":\"Campaign\"}]},"
			"{\"publishedfileid\":\"1\",\"result\":1,\"consumer_app_id\":440,\"title\":\"Other game\","
			"\"tags\":[{\"tag\":\"Halo1\"}]},"
			"{\"publishedfileid\":\"2\",\"result\":9}]}}";

		CHECK(workshop_browser_read_details(details, (int)strlen(details), items, WORKSHOP_BROWSER_MAXIMUM_ITEMS) == 2);
		CHECK(!strcmp(items[0].id, "2906110850") && !strcmp(items[0].title, "Aerial"));
		CHECK(!strcmp(items[0].kind, "multiplayer") && items[0].tags == WORKSHOP_TAG_MULTIPLAYER && items[0].size == -1 && items[0].updated == 1700000000);
		/* (a non-ASCII escape: its character; a picture's address dropped) */
		CHECK(!strcmp(items[0].description, "A map\nabove \xC3\xA9 clouds"));
		CHECK(!strcmp(items[1].id, "2886943774") && !strcmp(items[1].kind, "campaign") && items[1].size == 2265580503LL);
		{
			static const char plain[] = "{\"response\":{\"publishedfiledetails\":[{\"publishedfileid\":\"5\","
				"\"result\":1,\"consumer_app_id\":976730,\"title\":\"T\",\"description\":\"[h1]Hi[/h1]\\r\\nthere\","
				"\"tags\":[{\"tag\":\"Halo1\"}]}]}}";

			CHECK(workshop_browser_read_details(plain, (int)strlen(plain), items, 4) == 1);
			CHECK(!strcmp(items[0].description, "Hi\nthere"));
		}
		{
			/* a long description (Mercury Rising's is 4.5 KB) reaches the
			page whole (read_details_full), its markup out and its lines
			kept; the item keeps its start. Past the cap, cut on a
			character's start, ending "..." */
			static char text[64 * 1024];
			static const char head[] = "{\"response\":{\"publishedfiledetails\":[{\"publishedfileid\":\"9\","
				"\"result\":1,\"consumer_app_id\":976730,\"title\":\"Long\",\"description\":\"";
			static const char tail[] = "\",\"tags\":[{\"tag\":\"Halo1\"}]}]}}";
			static const char start[] = "Co-op\nThe mod supports co-op up to 4 players. Its page \n  \nBubble Shield\n"
				"\nEmoji \xF0\x9F\x98\x80 and a lone \xEF\xBF\xBD. Pic  abc";
			char *full[4];
			size_t length, index;

			/* (600 bytes and more: the old 400-byte field cut "supports co") */
			snprintf(text, sizeof(text), "%s[h2]Co-op[/h2]\\nThe mod supports co-op up to 4 players. "
				"[URL=https://example.com/a]Its page[/url] [list]\\n  [*]Bubble Shield\\n[/list]\\n"
				"[previewyoutube=abc;full][/previewyoutube]Emoji \\ud83d\\ude00 and a lone \\ud83d. "
				"Pic [img]https://x/[url]y[/url].png[/img] ", head);
			length = strlen(text);
			for (index = 0; index < 600; index++)
				text[length++] = (char)('a' + index % 26);
			memcpy(text + length, tail, sizeof(tail));
			CHECK(workshop_browser_read_details_full(text, (int)strlen(text), items, 4, full) == 1);
			CHECK(full[0] && !full[1]);
			if (full[0])
			{
				length = strlen(full[0]);
				CHECK(length > 700);
				CHECK(!strncmp(full[0], start, sizeof(start) - 1));
				CHECK(full[0][length - 1] == 'z' - (26 - 600 % 26));
				CHECK(!strstr(full[0], "["));
				/* (the item's own: the same start, cut ending "...") */
				length = strlen(items[0].description);
				CHECK(length < WORKSHOP_BROWSER_DESCRIPTION_SIZE && !strcmp(items[0].description + length - 3, "..."));
				CHECK(!strncmp(items[0].description, full[0], length - 3));
				free(full[0]);
			}

			/* over the cap: 3-byte characters, so the cut falls inside one */
			length = strlen(head);
			memcpy(text, head, length);
			for (index = 0; index < 12000; index += 3)
			{
				memcpy(text + length, "\xE2\x82\xAC", 3);
				length += 3;
			}
			memcpy(text + length, tail, sizeof(tail));
			CHECK(workshop_browser_read_details_full(text, (int)strlen(text), items, 4, full) == 1);
			if (full[0])
			{
				length = strlen(full[0]);
				CHECK(length < WORKSHOP_BROWSER_FULL_DESCRIPTION_SIZE && length > WORKSHOP_BROWSER_FULL_DESCRIPTION_SIZE - 8);
				CHECK(!strcmp(full[0] + length - 3, "..."));
				CHECK((length - 3) % 3 == 0);
				CHECK(!memcmp(full[0] + length - 6, "\xE2\x82\xAC", 3));
				free(full[0]);
			}
			else
				CHECK(full[0] != NULL);

			/* a description longer than the reader's buffer (24 KB of a
			16 KB one): still read, cut, the item kept */
			length = strlen(head);
			memcpy(text, head, length);
			for (index = 0; index < 24000; index++)
				text[length++] = index % 50 == 49 ? ' ' : 'x';
			memcpy(text + length, tail, sizeof(tail));
			CHECK(workshop_browser_read_details_full(text, (int)strlen(text), items, 4, full) == 1);
			if (full[0])
			{
				length = strlen(full[0]);
				CHECK(length > WORKSHOP_BROWSER_FULL_DESCRIPTION_SIZE - 8 && length < WORKSHOP_BROWSER_FULL_DESCRIPTION_SIZE);
				CHECK(!strcmp(full[0] + length - 3, "..."));
				free(full[0]);
			}
			else
				CHECK(full[0] != NULL);
		}
		{
			/* a broken escape or a control character in one item's
			description: a space, and every item of the batch still read */
			static const char broken[] = "{\"response\":{\"publishedfiledetails\":["
				"{\"publishedfileid\":\"11\",\"result\":1,\"consumer_app_id\":976730,\"title\":\"A\","
				"\"description\":\"bad \\q escape, \\u12 short, raw\ttab\x01!\",\"tags\":[{\"tag\":\"Halo1\"}]},"
				"{\"publishedfileid\":\"12\",\"result\":1,\"consumer_app_id\":976730,\"title\":\"B\","
				"\"description\":\"fine\",\"tags\":[{\"tag\":\"Halo1\"}]}]}}";
			char *full[4];

			CHECK(workshop_browser_read_details_full(broken, (int)strlen(broken), items, 4, full) == 2);
			CHECK(!strcmp(items[0].description, "bad   escape,  12 short, raw tab !"));
			CHECK(full[0] && !strcmp(full[0], items[0].description) && full[1] && !strcmp(full[1], "fine"));
			free(full[0]);
			free(full[1]);
			/* (a document that breaks off: -1, nothing kept to free) */
			CHECK(workshop_browser_read_details_full(broken, 120, items, 4, full) == -1 && !full[0]);
		}
		{
			/* a campaign that brings multiplayer maps (Steam tags it both,
			as Cursed Halo Again is): both, in the MULTIPLAYER list by its
			tag and saying so in its row */
			static const char both[] = "{\"response\":{\"publishedfiledetails\":[{\"publishedfileid\":\"2962107814\","
				"\"result\":1,\"consumer_app_id\":976730,\"title\":\"Cursed Halo Again\","
				"\"tags\":[{\"tag\":\"Halo1\"},{\"tag\":\"Campaign\"},{\"tag\":\"Multiplayer\"}]},"
				"{\"publishedfileid\":\"7\",\"result\":1,\"consumer_app_id\":976730,\"title\":\"Missions\","
				"\"tags\":[{\"tag\":\"Campaign\"},{\"tag\":\"Halo1\"}]}]}}";

			CHECK(workshop_browser_read_details(both, (int)strlen(both), items, 4) == 2);
			CHECK(!strcmp(items[0].kind, "campaign+multiplayer"));
			CHECK(items[0].tags == (WORKSHOP_TAG_MULTIPLAYER | WORKSHOP_TAG_CAMPAIGN));
			CHECK(!strcmp(workshop_browser_kind_name(items[0].kind), "Campaign + Multiplayer"));
			CHECK(!strcmp(items[1].kind, "campaign") && items[1].tags == WORKSHOP_TAG_CAMPAIGN);
		}
		CHECK(workshop_browser_read_details("{\"nope\":1}", 10, items, 4) == -1);
	}
	{
		static const char page[] = "<a href=\"https://steamcommunity.com/sharedfiles/filedetails/?id=2906110850&searchtext=\">"
			"<a href=\"https://steamcommunity.com/sharedfiles/filedetails/?id=2906110850\"><img>"
			"filedetails/?id=<script>filedetails/?id=123456789012345678901 filedetails/?id=3496468566\"";
		char ids[8][WORKSHOP_BROWSER_ID_SIZE];

		CHECK(workshop_browser_read_browse_ids(page, (int)strlen(page), ids, 8) == 2);
		CHECK(!strcmp(ids[0], "2906110850") && !strcmp(ids[1], "3496468566"));
		CHECK(workshop_browser_read_browse_ids(page, (int)strlen(page), ids, 1) == 1);
	}
	{
		/* a page with its list as data: only the list's items, not one that
		a description links to, nor a collection's children */
		static const char page[] = "<a href=\"filedetails/?id=2894901917\">\\\"results\\\":["
			"{\\\"publishedfileid\\\":\\\"3839152667\\\",\\\"creator\\\":\\\"1\\\",\\\"short_description\\\":"
			"\\\"from filedetails/?id=2894901917\\\",\\\"children\\\":[{\\\"publishedfileid\\\":\\\"9\\\"}]},"
			"{\\\"publishedfileid\\\":\\\"3840175164\\\",\\\"creator\\\":\\\"2\\\"}]";
		char ids[8][WORKSHOP_BROWSER_ID_SIZE];

		CHECK(workshop_browser_read_browse_ids(page, (int)strlen(page), ids, 8) == 2);
		CHECK(!strcmp(ids[0], "3839152667") && !strcmp(ids[1], "3840175164"));
		CHECK(workshop_browser_read_browse_ids(page, (int)strlen(page), ids, 1) == 1);
	}
	{
		/* the browse page's data: each item's creator, and the creators'
		names (escaped as a script's string holds them) */
		static const char page[] =
			"<a href=\"filedetails/?id=99\">"
			"\\\"total_pages\\\":23,\\\"total_count\\\":665,\\\"results\\\":["
			"{\\\"publishedfileid\\\":\\\"3839152667\\\",\\\"creator\\\":\\\"76561198076314335\\\",\\\"title\\\":\\\"A\\\","
			"\\\"short_description\\\":\\\"see filedetails/?id=5 and \\\\\\\"publishedfileid\\\\\\\"\\\"},"
			"{\\\"publishedfileid\\\":\\\"3666362121\\\",\\\"creator\\\":\\\"76561199347445160\\\",\\\"children\\\":["
			"{\\\"publishedfileid\\\":\\\"1\\\",\\\"sortorder\\\":0}]},"
			"{\\\"publishedfileid\\\":\\\"3840065526\\\",\\\"creator\\\":\\\"7\\\"},"
			"{\\\"publishedfileid\\\":\\\"3839152667\\\",\\\"creator\\\":\\\"76561198076314335\\\"}],"
			"\\\"next_cursor\\\":\\\"\\\",\\\"creator_player_link_details\\\":["
			"{\\\"public_data\\\":{\\\"steamid\\\":\\\"76561198076314335\\\",\\\"persona_name\\\":\\\"x\\\\\\\"n\\\\\\\\x \\u00e9\\\\u00e8\\\"}},"
			"{\\\"public_data\\\":{\\\"steamid\\\":\\\"76561199347445160\\\",\\\"profile_url\\\":\\\"\\\"}},"
			"{\\\"public_data\\\":{\\\"steamid\\\":\\\"7\\\",\\\"persona_name\\\":\\\"\\ud83d\\ude00 Face\\\"}}]";
		struct workshop_browser_author authors[8];
		int pages = 0, total = 0;

		CHECK(workshop_browser_read_browse_authors(page, (int)strlen(page), authors, 8) == 3);
		CHECK(!strcmp(authors[0].id, "3839152667") && !strcmp(authors[0].creator, "76561198076314335"));
		CHECK(!strcmp(authors[0].name, "x\"n\\x \xC3\xA9\xC3\xA8"));
		CHECK(!strcmp(authors[1].id, "3666362121") && !authors[1].name[0]);
		CHECK(!strcmp(authors[2].id, "3840065526") && !strcmp(authors[2].name, "\xF0\x9F\x98\x80 Face"));
		CHECK(workshop_browser_read_browse_authors(page, (int)strlen(page), authors, 1) == 1);
		CHECK(workshop_browser_read_browse_authors("no data", 7, authors, 8) == 0);
		CHECK(workshop_browser_read_browse_pages(page, (int)strlen(page), &pages, &total) == 1 && pages == 23 &&
			total == 665);
		CHECK(workshop_browser_read_browse_pages("<html>", 6, &pages, &total) == 0 && pages == 23);
		/* (a page saying more pages than the browser goes to: its limit) */
		CHECK(workshop_browser_read_browse_pages("\\\"total_pages\\\":999999999,", 26, &pages, &total) == 1 &&
			pages == WORKSHOP_BROWSER_MAXIMUM_PAGES);
	}
	{
		/* a name too long for its field: cut, not inside a character */
		static const char page[] = "\\\"results\\\":[{\\\"publishedfileid\\\":\\\"5\\\",\\\"creator\\\":\\\"6\\\"}],"
			"\\\"creator_player_link_details\\\":[{\\\"steamid\\\":\\\"6\\\",\\\"persona_name\\\":\\\""
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\xC3\xA9\xC3\xA9\\\"}]";
		struct workshop_browser_author authors[2];

		CHECK(workshop_browser_read_browse_authors(page, (int)strlen(page), authors, 2) == 1);
		CHECK(strlen(authors[0].name) == 45 + 2 && (unsigned char)authors[0].name[46] == 0xA9);
	}
	{
		/* names that are not valid UTF-8: each bad byte read as '?' (a lead
		byte no character starts with, a lead without the bytes after it, an
		overlong form, a surrogate, past U+10FFFF, a byte after a lead that
		goes on none); \b and \f (the script's and the data's own) as spaces */
		static const char page[] = "\\\"results\\\":[{\\\"publishedfileid\\\":\\\"5\\\",\\\"creator\\\":\\\"6\\\"},"
			"{\\\"publishedfileid\\\":\\\"7\\\",\\\"creator\\\":\\\"8\\\"},"
			"{\\\"publishedfileid\\\":\\\"9\\\",\\\"creator\\\":\\\"10\\\"}],"
			"\\\"creator_player_link_details\\\":["
			"{\\\"steamid\\\":\\\"6\\\",\\\"persona_name\\\":\\\"a\xF8" "b\xC3\xC0\xAF" "c\xED\xA0\x80\xF4\x90\x80\x80\xC3(\\\"},"
			"{\\\"steamid\\\":\\\"8\\\",\\\"persona_name\\\":\\\"one\\btwo\\\\fthree\\\"},"
			"{\\\"steamid\\\":\\\"10\\\",\\\"persona_name\\\":\\\"\xE2\x82\xAC \xF0\x9F\x98\x80\\\"}]";
		/* (a, F8, b, C3 C0 AF, c, ED A0 80, F4 90 80 80, C3 "(") */
		static const char bad[] = { 'a', '?', 'b', '?', '?', '?', 'c', '?', '?', '?', '?', '?', '?', '?', '?', '(', 0 };
		struct workshop_browser_author authors[4];

		CHECK(workshop_browser_read_browse_authors(page, (int)strlen(page), authors, 4) == 3);
		CHECK(!strcmp(authors[0].name, bad));
		CHECK(!strcmp(authors[1].name, "one two three"));
		CHECK(!strcmp(authors[2].name, "\xE2\x82\xAC \xF0\x9F\x98\x80"));
	}
	{
		/* an id with a creator after the list's "]" is not an item of it,
		though a string in the list holds a "]" and an item has a nested
		list */
		static const char page[] = "\\\"results\\\":[{\\\"publishedfileid\\\":\\\"5\\\",\\\"creator\\\":\\\"6\\\","
			"\\\"title\\\":\\\"a ] b \\\\\\\"]\\\\\\\" c\\\",\\\"tags\\\":[{\\\"tag\\\":\\\"x\\\"}]}],"
			"\\\"related\\\":[{\\\"publishedfileid\\\":\\\"7\\\",\\\"creator\\\":\\\"8\\\"}]";
		struct workshop_browser_author authors[4];
		char ids[4][WORKSHOP_BROWSER_ID_SIZE];

		CHECK(workshop_browser_read_browse_authors(page, (int)strlen(page), authors, 4) == 1);
		CHECK(!strcmp(authors[0].id, "5"));
		CHECK(workshop_browser_read_browse_ids(page, (int)strlen(page), ids, 4) == 1 && !strcmp(ids[0], "5"));
		/* (a list with no "]", cut short: read to the page's end, as before) */
		CHECK(workshop_browser_read_browse_authors(page, (int)(strstr(page, "\\\"title") - page), authors, 4) == 1 &&
			!strcmp(authors[0].id, "5") && !strcmp(authors[0].creator, "6"));
	}
	{
		static const char profile[] = "<?xml version=\"1.0\"?><profile><steamID64>7656</steamID64>"
			"<steamID><![CDATA[Some \xC3\xA9 one]]></steamID><onlineState>offline</onlineState></profile>";
		char name[WORKSHOP_BROWSER_AUTHOR_SIZE], small[8];

		CHECK(workshop_browser_read_profile_name(profile, (int)strlen(profile), name, (int)sizeof(name)) == 1);
		CHECK(!strcmp(name, "Some \xC3\xA9 one"));
		/* (cut at a character's edge) */
		CHECK(workshop_browser_read_profile_name(profile, (int)strlen(profile), small, 7) == 1 && !strcmp(small, "Some "));
		CHECK(workshop_browser_read_profile_name("<steamID><![CDATA[]]></steamID>", 31, name, (int)sizeof(name)) == 0);
		CHECK(workshop_browser_read_profile_name("<response><error>", 17, name, (int)sizeof(name)) == 0);
	}
	{
		/* the details' creator */
		static const char details[] = "{\"response\":{\"publishedfiledetails\":[{\"publishedfileid\":\"5\","
			"\"creator\":\"76561198076314335\",\"result\":1,\"consumer_app_id\":976730,\"title\":\"T\","
			"\"tags\":[{\"tag\":\"Halo1\"}]}]}}";

		CHECK(workshop_browser_read_details(details, (int)strlen(details), items, 4) == 1);
		CHECK(!strcmp(items[0].creator, "76561198076314335"));
	}
	if (failures)
		printf("%d failures\n", failures);
	return failures ? 1 : 0;
}
