/*
WORKSHOP_JSON_FUZZ.C

libFuzzer's entry for what a Steam Workshop item says of itself
(tools/test_host_fuzz.py builds it, target workshop_json): anyone can publish
one, and the game reads its files where Steam put them. The first byte picks
the reader (steam_workshop.c): 0, an item's manifest (its title, engine,
campaign flag and maps' json paths); 1, a map's json (its scenario file's
name, its title and images); 2, Steam's libraryfolders.vdf (its "path"
values). The rest is the text, in an allocation of exactly its size.
*/

#include "../src/steam_workshop.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* (the folder scanning steam_workshop.c also holds: not called here) */
#define NEVER(name) void name(void) { abort(); }
NEVER(posix_directory_close) NEVER(posix_directory_next) NEVER(posix_directory_open)
NEVER(posix_file_information) NEVER(posix_file_is_directory) NEVER(posix_find_entry_case_insensitive)
NEVER(posix_stat) NEVER(platform_data_root) NEVER(config_boolean) NEVER(config_string)
NEVER(mcc_manifest_load)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static struct steam_workshop_item_text item;
	static struct steam_workshop_map_text map;
	static char paths[STEAM_WORKSHOP_MAXIMUM_LIBRARIES][STEAM_WORKSHOP_PATH_SIZE];
	char *text;
	int length;

	if (size < 1 || size - 1 > STEAM_WORKSHOP_MAXIMUM_JSON + 1)
		return 0;
	length = (int)(size - 1);
	text = malloc(length ? (size_t)length : 1);
	if (!text)
		return 0;
	memcpy(text, data + 1, (size_t)length);
	switch (data[0] % 3)
	{
	case 0:
		if (steam_workshop_read_item(text, length, &item))
		{
			int index;

			for (index = 0; index < item.map_count; index++)
				if (!memchr(item.map_json[index], 0, sizeof(item.map_json[index])))
					abort();
		}
		break;
	case 1:
		if (steam_workshop_read_map(text, length, &map) && (!map.name[0] || strchr(map.name, '/') ||
			strchr(map.name, '\\') || !strcmp(map.name, "..")))
			abort();
		break;
	default:
		steam_workshop_read_libraries(text, length, paths, STEAM_WORKSHOP_MAXIMUM_LIBRARIES);
		break;
	}
	free(text);
	return 0;
}
