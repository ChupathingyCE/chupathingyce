/*
GAMETYPE_FOLDER.H

The player's game type files: <save root>/gametypes/<name>.toml, in the
dedicated server's format (server/docs/playlists.md, Game type files), so
that one can be copied to a server's admin/gametypes and back. The files'
text only: the game reads and writes them with the server's parser
(server/src/server_config.c) and plays them as the server does
(server/src/server_gametype_variant.c, port/linux/game/gametype_files.c).

Each function takes the folder, so the tests (port/linux/tests/
gametype_folder_test.c) run them on a scratch folder; the game passes
platform_folder(_port_folder_gametypes). No SDL or game dependency.
*/

#ifndef __HALO_GAMETYPE_FOLDER_H
#define __HALO_GAMETYPE_FOLDER_H

enum
{
	/* a file's name without .toml, with its end: 1 to 31 of a-z, 0-9, _
	and -, the first a letter or digit (the server's game type names) */
	GAMETYPE_FOLDER_NAME_SIZE = 32,
	/* a file's text, at most (the server's SERVER_GAMETYPE_TEXT_SIZE) */
	GAMETYPE_FOLDER_TEXT_SIZE = 4096,
};

/* whether name is a game type file's name */
int gametype_folder_name_valid(const char *name);

/* the names of the folder's game type files (those with a valid name),
sorted, at most maximum: their count */
int gametype_folder_list(const char *folder, char names[][GAMETYPE_FOLDER_NAME_SIZE], int maximum);

/* a file's text into text (size bytes, ended): its length, or -1 if there
is no such file or it is larger than size - 1 bytes */
int gametype_folder_read(const char *folder, const char *name, char *text, int size);

/* a file written whole (a new file beside it, renamed over it), the folder
made if it is not there: 1, else 0 (nothing changed) */
int gametype_folder_write(const char *folder, const char *name, const char *text);

/* a file deleted: 1, else 0 (there was none) */
int gametype_folder_remove(const char *folder, const char *name);

#endif
