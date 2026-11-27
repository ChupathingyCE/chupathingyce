/*
MAP_PACK_FOLDERS.C

The map packs' folders "Make the folders" makes (map_pack_folders.h).
docs/maps.md says what goes in each; each README.txt says it in short.
*/

#include "map_pack_folders.h"
#include "posix.h"

#include <stdio.h>
#include <string.h>

#define DOCS "https://github.com/ChupathingyCE/chupathingyce/blob/main/docs/maps.md"

static const struct
{
	const char *folder;
	/* (other names the game reads it by: none made beside one of them) */
	const char *older[2];
	const char *readme;
} folders[MAP_PACK_FOLDER_COUNT] =
{
	{ "maps_cex", { "maps_ceplus", "maps_ce+" },
		"Put the Halo CE+ Xbox map pack here: everything in the pack's maps folder, maplist.txt too.\n"
		"Leave your Xbox maps in maps.\n"
		"More: " DOCS "#xbox-map-packs\n" },
	{ "maps_spv3", { NULL, NULL },
		"Put SPV3's maps here: everything in your SPV3 install's maps folder (the .yelo files, ui.yelo, "
		"bitmaps.map, sounds.map and loc.map).\n"
		"Its Lua scripts go in the lua folder, as the page below shows.\n"
		"More: " DOCS "#spv3\n" },
	{ "maps_mcc", { "mcc_maps", NULL },
		"Put Halo MCC custom maps here, or a Steam Workshop item's whole folder.\n"
		"More: " DOCS "#mcc-maps\n" },
	{ "maps_os", { NULL, NULL },
		"Put OpenSauce maps here: .yelo files, and .map files built with OpenSauce.\n"
		"They use the bitmaps.map, sounds.map and loc.map in maps_ce. SPV3 goes in maps_spv3.\n"
		"More: " DOCS "#opensauce-maps\n" },
};

const char *map_pack_folder_name(int index)
{
	return index >= 0 && index < MAP_PACK_FOLDER_COUNT ? folders[index].folder : NULL;
}

const char *map_pack_folder_readme(int index)
{
	return index >= 0 && index < MAP_PACK_FOLDER_COUNT ? folders[index].readme : NULL;
}

static int is_there(const char *root, const char *name, char *on_disk, posix_ulong size)
{
	return posix_find_entry_case_insensitive(root, name, on_disk, size) != 0;
}

int map_pack_folders_make(const char *root)
{
	int index, count = 0;

	for (index = 0; index < MAP_PACK_FOLDER_COUNT; index++)
	{
		char on_disk[256], path[1100], readme[1200];
		int older;
		FILE *file;

		if (!is_there(root, folders[index].folder, on_disk, sizeof(on_disk)))
		{
			for (older = 0; older < 2 && folders[index].older[older]; older++)
			{
				if (is_there(root, folders[index].older[older], on_disk, sizeof(on_disk)))
					break;
			}
			if (older < 2 && folders[index].older[older])
				continue;
			snprintf(on_disk, sizeof(on_disk), "%s", folders[index].folder);
			snprintf(path, sizeof(path), "%s/%s", root, on_disk);
			if (posix_make_directory(path) != 0)
				continue;
		}
		count++;
		/* (a README.txt there already is the player's, or ours: kept) */
		snprintf(path, sizeof(path), "%s/%s", root, on_disk);
		if (is_there(path, MAP_PACK_FOLDERS_README, readme, sizeof(readme)))
			continue;
		snprintf(readme, sizeof(readme), "%s/%s", path, MAP_PACK_FOLDERS_README);
		file = fopen(readme, "wb");
		if (file)
		{
			fputs(folders[index].readme, file);
			fclose(file);
		}
	}
	return count;
}
