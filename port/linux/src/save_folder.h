/*
SAVE_FOLDER.H

The save root's usual place on Linux and Windows, and the one-time copy of
the saves from the folder the game shared with other builds before it had
its own (xbox_files.c, platform_save_root). No SDL or game dependency: the tests
(port/linux/tests/save_folder_test.c) build it on their own.
*/

#ifndef __HALO_SAVE_FOLDER_H
#define __HALO_SAVE_FOLDER_H

enum save_folder_system
{
	_save_folder_linux,
	_save_folder_windows,
};

/* the file in the save root that says the copy was done (or not needed) */
#define SAVE_FOLDER_MARKER "shared_saves_checked.txt"

enum save_folder_copy_result
{
	/* the marker was there: checked before */
	_save_folder_copy_done_before,
	/* no shared folder to copy from */
	_save_folder_copy_nothing_shared,
	/* the save root already has profiles of its own */
	_save_folder_copy_has_profiles,
	/* the shared folder's saves were copied */
	_save_folder_copy_copied,
	/* the copy failed; the save root is as it was, and the next start tries again */
	_save_folder_copy_failed,
};

/* the save root's usual place, given the environment (appdata is
%APPDATA%, data_home $XDG_DATA_HOME, home $HOME; NULL or empty when not
set): %APPDATA%/ChupathingyCE on Windows, $XDG_DATA_HOME/chupathingyce or
~/.local/share/chupathingyce on Linux. 0 when the environment gives none. */
int save_folder_default(enum save_folder_system system, const char *appdata, const char *data_home,
	const char *home, char *path, unsigned long size);

/* the folder the game shared with other builds before, the same way:
%APPDATA%/halo, $XDG_DATA_HOME/halo-linux or ~/.local/share/halo-linux */
int save_folder_shared(enum save_folder_system system, const char *appdata, const char *data_home,
	const char *home, char *path, unsigned long size);

/* the save root: override (paths.saves, or HALO_SAVE_ROOT) as it is when
set, else save_folder_default. shared gets the folder to copy the saves
from once (save_folder_shared, for save_folder_copy_once), or "" for an
override, which is never copied into. 0 when there is no save root. */
int save_folder_choose(enum save_folder_system system, const char *override, const char *appdata,
	const char *data_home, const char *home, char *root, unsigned long root_size, char *shared,
	unsigned long shared_size);

/* once per save root (the marker): copies the saves in the shared folder
into the save root, which is made if it is not there, unless the save root
has profiles of its own. Never changes the shared folder, never replaces a
file in the save root. A save root that is not there is filled in a folder
beside it first (save root + ".copying"), then renamed into place, so an
interrupted copy leaves nothing half done. The map cache (z/cache*.map),
which the game rebuilds, is not copied. files_copied, if not NULL, gets the
number of files copied. */
enum save_folder_copy_result save_folder_copy_once(const char *shared, const char *save_root,
	unsigned long *files_copied);

#endif
