/*
PORT_FOLDERS.H

The game's own folders beside the maps and the saves, one rule for every
platform (the desktops, Android, the dedicated server):

	<data root>/data/     what a player puts or edits that is not a map
	                      (data/fonts/: display.high_res_font)
	<data root>/logs/     debug.txt, halo.log (Windows), crashes/
	<save root>/cache/    what the game can make again (safe to delete)
	<save root>/private/  what is the player's alone, never shared
	<save root>/gametypes/ the player's game type files (the dedicated
	                      server's format: server/docs/playlists.md)

The data root holds maps/ (paths.data, HALO_DATA_ROOT); the save root, the
saves (paths.saves, HALO_SAVE_ROOT). logs/ and cache/ can be put elsewhere
(paths.logs, HALO_LOG_ROOT; paths.cache, HALO_CACHE_ROOT), for tests and
servers. A file that was in the root itself before (debug.txt, crashes/) is
left where it is and still read: only new files go in the new folders.

No SDL or game dependency: the tests (port/linux/tests/port_folders_test.c)
build it on their own. platform_folder (platform.h) is the game's.
*/

#ifndef __HALO_PORT_FOLDERS_H
#define __HALO_PORT_FOLDERS_H

enum port_folder
{
	_port_folder_data,
	_port_folder_logs,
	_port_folder_cache,
	_port_folder_private,
	_port_folder_gametypes,

	k_port_folder_count
};

/* the folder's name in its root: "data", "logs", "cache", "private" or
"gametypes" */
const char *port_folder_name(enum port_folder folder);

/* whether the folder is in the save root (cache, private, gametypes) rather
than the data root (data, logs) */
int port_folder_in_save_root(enum port_folder folder);

/* the folder's path: override as it is when set (logs and cache only; the
others' is ignored), else its root's path with its name. 0 when there is no
root, or the path does not fit. */
int port_folder_path(enum port_folder folder, const char *override, const char *data_root, const char *save_root,
	char *path, unsigned long size);

/* whether path is absolute: /..., \..., or a drive's (C:\..., C:/...) */
int port_folder_absolute(const char *path);

/* a file a setting names (display.high_res_font): an absolute path as it
is, a relative one in folder. A relative one that would leave folder (a ..
part) is refused. 0 when refused, empty, or too long. */
int port_folder_file(const char *folder, const char *value, char *path, unsigned long size);

#endif
