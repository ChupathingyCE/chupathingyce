/*
SERVER_ADMIN.H

The dedicated server's files that its admins make (server_admin.c): the
playlists, game types and settings kept in the data folder's admin/
folder, and their commands (server_commands.c's table). The game's side:
built into the game with dedicated.c.
*/

#ifndef SERVER_ADMIN_H
#define SERVER_ADMIN_H

struct command_line;
struct command_output;
struct game_variant;
struct game_variant_options;
struct server_settings;

/* a game type, by its name in a playlist: a built-in's (slayer, ctf, ...),
else its file's (admin/gametypes/<name>.toml), built on its base. TRUE,
else FALSE and why in problem */
boolean server_gametype_resolve(char const *name, struct game_variant *variant, struct game_variant_options *options,
	char *problem, long problem_size);

/* the settings file (admin/settings.toml): TRUE and its settings (none
set if there is no file), else FALSE (it is there but cannot be read, or is
not a settings file: why logged) */
boolean server_admin_read_settings(struct server_settings *settings);

/* (server_commands.c's: whether the server has a map, as a command names
it, to play; why not in the output) */
boolean server_map_playable(char const *map, struct command_output *output);
/* (server_commands.c's: the text an API's request brought with its command,
NULL if none: a playlist's or game type's file to save) */
char const *server_commands_payload(void);

/* the commands (server/docs/admin.md) */
boolean server_admin_playlists(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_playlist(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_playlist_new(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_playlist_add(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_playlist_remove(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_playlist_move(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_playlist_delete(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_playlist_use(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_playlist_save(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_mapcycle_add(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_mapcycle_del(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_gametypes(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_gametype(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_gametype_new(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_gametype_set(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_gametype_delete(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_gametype_save(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_settings(struct command_line const *line, boolean json, struct command_output *output);
boolean server_admin_set(struct command_line const *line, boolean json, struct command_output *output);

#endif
