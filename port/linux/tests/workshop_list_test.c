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
	CHECK(!strcmp(workshop_browser_state_name(WORKSHOP_ITEM_INSTALLING), "Installing"));
	workshop_browser_size_text(48211968, size, sizeof(size));
	CHECK(!strcmp(size, "46 MB"));
	workshop_browser_size_text(-1, size, sizeof(size));
	CHECK(!size[0]);
	if (failures)
		printf("%d failures\n", failures);
	return failures ? 1 : 0;
}
