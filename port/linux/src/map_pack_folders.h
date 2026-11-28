/*
MAP_PACK_FOLDERS.H

The map packs' folders that "Make the folders" makes beside maps, maps_ce,
maps_pc and maps_md (map_pack_folders.c): maps_cex (Halo CE+ Xbox),
maps_spv3 (SPV3), maps_mcc (Halo MCC custom maps) and maps_os (OpenSauce
maps), each with a README.txt that says what goes there. The setup screen's step
(launcher.c) and the message boxes' (sdl_platform.c) both make them.
*/

#ifndef MAP_PACK_FOLDERS_H
#define MAP_PACK_FOLDERS_H

#define MAP_PACK_FOLDER_COUNT 4
#define MAP_PACK_FOLDERS_README "README.txt"

/* the index-th folder's name and its README.txt's text (NULL past the
last) */
const char *map_pack_folder_name(int index);
const char *map_pack_folder_readme(int index);

/* makes each folder in root, and its README.txt if it has none. A folder
there already, in any case, is kept as it is, its README.txt too; one
whose older name is there is not made (maps_cex beside maps_ceplus or
maps_ce+, maps_mcc beside GulchCE's mcc_maps, which the game reads only
when there is no maps_mcc). Returns how many folders are there now, made
or kept. */
int map_pack_folders_make(const char *root);

#endif
