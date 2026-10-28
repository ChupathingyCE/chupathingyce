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

/* ---------- the player keys of the Halo profiles

Each Halo profile has its own player key (its Delta identity, what the
site links): a file in the profile's own folder, u/UDATA/<id>/ in the save
root (xbox_xapi.c, XCreateSaveGame). A profile without one is a guest: the
key is made when the profile signs in (Link Profile), and a guest sends no
key at all.

Builds before this one had one key per install, in the save root. It is
moved over once: the Halo profile player 1 uses (the active one, else the
last used one, z/lastprof.txt), or the install's only profile when player
1 has none, adopts a copy of it, and the marker says which. The others
start as guests. Until a profile can take it (no profile yet, or several
and none picked), the install key waits; and if the profile that adopted
it is deleted, the next profile player 1 uses adopts it again. The install
key itself is never changed or removed: older builds keep using it, and it
stays a backup. */

/* the key's file name, the install's (save root) and each profile's */
#define SAVE_FOLDER_PLAYER_KEY "game_list_player.key"
/* the file in the save root that says which profile adopted the install key */
#define SAVE_FOLDER_KEY_ADOPTED "player_key_adopted.txt"

/* profile_folder/game_list_player.key; 0 if it does not fit */
int save_folder_profile_key_path(const char *profile_folder, char *path, unsigned long size);

/* a profile's key copied into another folder (the profile renamed: it
moves to a new folder), the copy the user's alone, never replacing a key
there (and the marker following it, when it adopted the install key); 1
when it has no key or it was copied */
int save_folder_profile_key_copy(const char *save_root, const char *from_folder, const char *to_folder);

/* whether the profile has its own key */
int save_folder_profile_has_key(const char *profile_folder);

/* the install's only Halo profile (a folder of u/UDATA with a blam.sav),
into folder; 0 when it has none or more than one */
int save_folder_only_profile(const char *save_root, char *folder, unsigned long size);

enum save_folder_adopt_result
{
	/* the marker was there: adopted before */
	_save_folder_adopt_done_before,
	/* no install key: nothing to move */
	_save_folder_adopt_no_install_key,
	/* no profile to give it to yet; the next try may */
	_save_folder_adopt_waiting,
	/* the profile has a key of its own already: kept, and the install key
	waits for another */
	_save_folder_adopt_profile_has_key,
	/* the profile adopted the install key */
	_save_folder_adopt_adopted,
	/* the copy failed; nothing changed, and the next try tries again */
	_save_folder_adopt_failed,
};

/* once per save root (the marker, while the profile it names is there):
the install key copied into
profile_folder ("" or NULL: no profile, waits). The copy is the user's
alone, made beside its place and renamed in, so it is whole or not there. */
enum save_folder_adopt_result save_folder_adopt_install_key(const char *save_root, const char *profile_folder);

/* the Halo profile whose key is the player's: player1_folder (player 1's
profile; "" or NULL: none), else the install's only profile. The install
key is moved over first (save_folder_adopt_install_key). key_path gets the
profile's key file (there or not), or "" for no profile (a guest). 0 when
there is no profile. */
int save_folder_player_key(const char *save_root, const char *player1_folder, char *key_path, unsigned long size);

#endif
