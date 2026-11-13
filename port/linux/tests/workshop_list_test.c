/*
WORKSHOP_LIST_TEST.C

Tests of the WORKSHOP section's list document and words
(port/linux/src/workshop_list.c, workshop_browser.h). Built and run by
tools/test_workshop_list.py; exits nonzero on a failure.
*/

#define WORKSHOP_LIST_ONLY
#include "../src/workshop_browser.h"

#include <stdio.h>
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
		for (index = 0; index < 40; index++)
			strcat(text, "\xC3\xA9");
		strcat(text, "\"}]}");
		CHECK(read_text(text) == 1);
		CHECK(strlen(items[0].title) == 62);
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
		/* (a non-ASCII escape: the description passed over, the item kept) */
		CHECK(!items[0].description[0]);
		CHECK(!strcmp(items[1].id, "2886943774") && !strcmp(items[1].kind, "campaign") && items[1].size == 2265580503LL);
		{
			static const char plain[] = "{\"response\":{\"publishedfiledetails\":[{\"publishedfileid\":\"5\","
				"\"result\":1,\"consumer_app_id\":976730,\"title\":\"T\",\"description\":\"[h1]Hi[/h1]\\r\\nthere\","
				"\"tags\":[{\"tag\":\"Halo1\"}]}]}}";

			CHECK(workshop_browser_read_details(plain, (int)strlen(plain), items, 4) == 1);
			CHECK(!strcmp(items[0].description, "Hi there"));
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
	if (failures)
		printf("%d failures\n", failures);
	return failures ? 1 : 0;
}
