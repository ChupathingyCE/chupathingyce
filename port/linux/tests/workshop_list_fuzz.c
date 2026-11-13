/*
WORKSHOP_LIST_FUZZ.C

libFuzzer's entry for the Workshop's readers of what Steam sends
(port/linux/src/workshop_list.c), built by tools/test_workshop_list.py. The
first byte picks the reader: 0, the list document; 1, Steam's
GetPublishedFileDetails answer; 2, the public browse page's item ids; 3, an
item id's check and its Steam page address. Each input as the game reads
it: NUL-ended text in an allocation of exactly that size.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#define WORKSHOP_LIST_ONLY
#include "../src/workshop_browser.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { FUZZ_ITEMS = 8 };

int LLVMFuzzerTestOneInput(const uint8_t *input, size_t size)
{
	static struct workshop_browser_item items[FUZZ_ITEMS];
	static char ids[FUZZ_ITEMS][WORKSHOP_BROWSER_ID_SIZE];
	char url[256], *text;

	if (!size)
		return 0;
	text = (char *)malloc(size);
	if (!text)
		return 0;
	memcpy(text, input + 1, size - 1);
	text[size - 1] = 0;
	switch (input[0] % 4)
	{
	case 0:
		workshop_browser_read_list(text, (int)(size - 1), items, FUZZ_ITEMS);
		break;
	case 1:
		workshop_browser_read_details(text, (int)(size - 1), items, FUZZ_ITEMS);
		break;
	case 2:
		workshop_browser_read_browse_ids(text, (int)(size - 1), ids, FUZZ_ITEMS);
		break;
	default:
		if (workshop_browser_id_valid(text))
			workshop_browser_steam_url(text, url, (int)sizeof(url));
		break;
	}
	free(text);
	return 0;
}
