/*
GAMETYPE_FILES.H

The player's game type files (<save root>/gametypes/<name>.toml, the
dedicated server's format: server/docs/playlists.md) and the game's saved
gametypes, kept as one (gametype_files.c). Built with the game list and
the dedicated server (HALO_GAME_BROWSER), whose parser they share.
*/

#ifndef __HALO_GAMETYPE_FILES_H
#define __HALO_GAMETYPE_FILES_H

struct game_variant;
struct game_variant_options;
struct server_gametype;

/* the file name a game type's name makes ("CTF Pro": ctf_pro), at most
size - 1 characters; never a built-in's name, which a server would play
in its place */
void gametype_files_name(char const *title, char *name, long size);

/* a game type saved as the player's: its file (the name it got in name,
name_size bytes, if name is not NULL) and the saved gametype of its name,
made if there is none, so that every gametype list has it. The saved
gametype's index, else NONE (why logged) */
long gametype_files_save(struct server_gametype const *gametype, char *name, long name_size);

/* the saved gametype (a custom one) whose name is title, else NONE */
long gametype_files_profile_find(char const *title);

/* playlist_profile.c's: a saved gametype written (its file written to
match), and one about to be deleted (its file deleted) */
void gametype_files_profile_saved(long profile_index, struct game_variant const *variant,
	struct game_variant_options const *options);
void gametype_files_profile_deleted(long profile_index);

/* the folder's files that no saved gametype has, or that were changed by
hand, made saved gametypes (as the menus' map loads): every gametype list
then has them */
void gametype_files_import(void);

#endif
