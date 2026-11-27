/*
ARK_FILES.H

Where the player's own files for Project Ark are (ark_files.c), for the mode
(port/linux/game/ark_mode.c): never shipped, never downloaded, read in place.
*/

#ifndef ARK_FILES_H
#define ARK_FILES_H

/* the mode's id (game.pack_menus, --mode) */
#define ARK_FILES_MODE_ID "ark"

/* the data root's folder for them */
#define ARK_FILES_FOLDER "maps_ark"

/* the host folder holding the mode's maps (shared.map and the rest): the
data root's maps_ark if it has them, else their folder in MCC's install in a
Steam library. 1 and the path, or 0 with none here. Looked for once a run. */
int ark_files_folder(char *path, unsigned long size);

/* whether that folder is the data root's own (0: MCC's, or none) */
int ark_files_local(void);

#endif
