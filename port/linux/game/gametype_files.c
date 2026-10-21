/*
GAMETYPE_FILES.C

The player's game type files and the game's saved gametypes, kept as one
(gametype_files.h). A file is the dedicated server's (server/docs/
playlists.md, Game type files): its base, a built-in game type, and the
settings it changes, read and written by the server's own code
(server/src/server_config.c) and played as the server plays it
(server/src/server_gametype_variant.c). So a game type made in the game's
editors plays the same on a dedicated server whose admin/gametypes it is
copied to, and one from a server plays the same hosted here.

The game's gametype lists are its saved gametypes (playlist_profile.c), so
each file has one of the same name: a saved gametype written (any of the
menus' editors) writes its file, and one deleted deletes it; the files that
have none, or were changed by hand, become saved gametypes as the menus'
map loads. A file wins over its saved gametype only when they differ in
what a file holds.
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "game/game_engine.h"
#include "saved games/playlist_profile.h"
#include "saved games/saved_game_files.h"
#include "text/unicode.h"

#include "../../../server/src/server_config.h"
#include "../../../server/src/server_gametype_variant.h"
#include "../src/gametype_folder.h"
#include "../src/port_folders.h"
#include "gametype_files.h"

#include <stdio.h>
#include <string.h>
#include <xtl.h>

const char *platform_folder(enum port_folder folder);
void platform_log(char const *format, ...);

/* ---------- constants */

enum
{
	/* the saved gametypes and files looked at, at most (the menus' lists') */
	MAXIMUM_GAMETYPES = 100,
	/* (a saved gametype's index: a built-in's, which is never written) */
	READ_ONLY_BIT = 0x40000000,
};

/* ---------- globals */

/* (a saved gametype written from a file: its file is not written back) */
static boolean gametype_files_importing;

/* ---------- private code */

static char const *folder(
	void)
{
	return platform_folder(_port_folder_gametypes);
}

/* a saved gametype's name as a file's (printable ASCII, 11 characters) */
static boolean profile_title(
	long profile_index,
	char *title)
{
	wchar_t name[MAX_GAMENAME];
	long index;

	title[0] = 0;
	if (profile_index == NONE || !playlist_profile_get_display_name(profile_index, name))
		return FALSE;
	for (index = 0; index < SERVER_GAMETYPE_TITLE_SIZE - 1 && name[index]; index++)
		title[index] = name[index] >= 0x20 && name[index] <= 0x7E ? (char)name[index] : '?';
	title[index] = 0;
	return title[0] != 0;
}

static boolean titles_match(
	char const *a,
	char const *b)
{
	for (; *a && *b; a++, b++)
	{
		char x = *a >= 'A' && *a <= 'Z' ? (char)(*a - 'A' + 'a') : *a;
		char y = *b >= 'A' && *b <= 'Z' ? (char)(*b - 'A' + 'a') : *b;

		if (x != y)
			return FALSE;
	}
	return !*a && !*b;
}

/* a game type's name in the game: its file's, else the file's name (as
much of it as fits) */
static void file_title(
	struct server_gametype const *gametype,
	char const *name,
	char *title)
{
	snprintf(title, SERVER_GAMETYPE_TITLE_SIZE, "%s", gametype->set[SERVER_GAMETYPE_NAME] ? gametype->title : name);
}

/* whether two game types are the same in what a file holds (their names
aside) */
static boolean gametypes_same(
	struct server_gametype const *a,
	struct server_gametype const *b)
{
	short field;

	for (field = SERVER_GAMETYPE_BASE; field < SERVER_GAMETYPE_FIELDS; field++)
	{
		if (!a->set[field] != !b->set[field] || (a->set[field] && a->values[field] != b->values[field]))
			return FALSE;
	}
	return TRUE;
}

/* a game type file's text written: TRUE */
static boolean file_write(
	struct server_gametype const *gametype,
	char const *name)
{
	static char text[SERVER_GAMETYPE_TEXT_SIZE];
	static struct server_gametype check;
	char error[SERVER_CONFIG_ERROR_SIZE];

	if (server_gametype_format(gametype, name, text, sizeof(text)) < 0 ||
		!server_gametype_parse(text, strlen(text), &check, error, sizeof(error)))
	{
		platform_log("game types: %s does not make a file the server reads", name);
		return FALSE;
	}
	if (!gametype_folder_write(folder(), name, text))
	{
		platform_log("game types: could not write %s/%s.toml", folder(), name);
		return FALSE;
	}
	platform_log("game types: saved %s.toml", name);
	return TRUE;
}

/* a game type's variant and PC options, its name its own */
static void gametype_build(
	struct server_gametype const *gametype,
	char const *title,
	struct game_variant *variant,
	struct game_variant_options *options)
{
	long index;

	server_gametype_variant_build(gametype, variant, options);
	for (index = 0; index < (long)NUMBEROF(variant->human_readable_game_description) - 1 && title[index]; index++)
		variant->human_readable_game_description[index] = (wchar_t)(unsigned char)title[index];
	for (; index < (long)NUMBEROF(variant->human_readable_game_description); index++)
		variant->human_readable_game_description[index] = 0;
}

/* a saved gametype made or written from a game type: its index, else NONE */
static long profile_write(
	struct server_gametype const *gametype,
	char const *title)
{
	struct game_variant variant;
	struct game_variant_options options;
	long profile_index = gametype_files_profile_find(title);

	gametype_build(gametype, title, &variant, &options);
	if (profile_index == NONE)
	{
		wchar_t name[SERVER_GAMETYPE_TITLE_SIZE];
		long index;

		for (index = 0; title[index] && index < SERVER_GAMETYPE_TITLE_SIZE - 1; index++)
			name[index] = (wchar_t)(unsigned char)title[index];
		name[index] = 0;
		profile_index = playlist_profile_new(0, name);
		if (profile_index == NONE)
		{
			platform_log("game types: could not make a saved gametype named %s (another saved game has the name?)",
				title);
			return NONE;
		}
	}
	gametype_files_importing = TRUE;
	playlist_profile_save_with_options(profile_index, &variant, &options);
	gametype_files_importing = FALSE;
	return profile_index;
}

/* ---------- public code */

void gametype_files_name(
	char const *title,
	char *name,
	long size)
{
	long length = 0;
	boolean separator = FALSE;

	if (size <= 0)
		return;
	for (; title && *title && length < size - 1 && length < GAMETYPE_FOLDER_NAME_SIZE - 1; title++)
	{
		char character = *title >= 'A' && *title <= 'Z' ? (char)(*title - 'A' + 'a') : *title;

		if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9'))
		{
			if (separator && length)
				name[length++] = '_';
			separator = FALSE;
			if (length < size - 1)
				name[length++] = character;
		}
		else
			separator = TRUE;
	}
	name[length] = 0;
	if (!length)
		snprintf(name, (size_t)size, "gametype");
	/* (a server plays a built-in of the name in a file's place) */
	if (server_builtin_find(name) >= 0 && strlen(name) + 7 < (size_t)size)
		strcat(name, "_custom");
}

long gametype_files_profile_find(
	char const *title)
{
	long indices[MAXIMUM_GAMETYPES];
	word count = MAXIMUM_GAMETYPES;
	word index;

	if (!title || !*title)
		return NONE;
	playlist_profiles_enumerate_available_to_local_player_index(0, &count, indices);
	for (index = 0; index < count; index++)
	{
		char name[SERVER_GAMETYPE_TITLE_SIZE];

		if (!((unsigned long)indices[index] & READ_ONLY_BIT) && profile_title(indices[index], name) &&
			titles_match(name, title))
		{
			return indices[index];
		}
	}
	return NONE;
}

long gametype_files_save(
	struct server_gametype const *gametype,
	char *name,
	long name_size)
{
	char file_name[GAMETYPE_FOLDER_NAME_SIZE];
	char title[SERVER_GAMETYPE_TITLE_SIZE];
	long profile_index;

	gametype_files_name(gametype->title, file_name, sizeof(file_name));
	if (name && name_size > 0)
		snprintf(name, (size_t)name_size, "%s", file_name);
	file_title(gametype, file_name, title);
	profile_index = profile_write(gametype, title);
	if (!file_write(gametype, file_name))
		return NONE;
	return profile_index;
}

void gametype_files_profile_saved(
	long profile_index,
	struct game_variant const *variant,
	struct game_variant_options const *options)
{
	struct server_gametype gametype;
	char title[SERVER_GAMETYPE_TITLE_SIZE];
	char name[GAMETYPE_FOLDER_NAME_SIZE];

	if (gametype_files_importing || ((unsigned long)profile_index & READ_ONLY_BIT) ||
		!profile_title(profile_index, title))
	{
		return;
	}
	server_gametype_variant_capture(variant, options, title, &gametype);
	gametype_files_name(title, name, sizeof(name));
	file_write(&gametype, name);
}

void gametype_files_profile_deleted(
	long profile_index)
{
	static char text[GAMETYPE_FOLDER_TEXT_SIZE];
	struct server_gametype gametype;
	char title[SERVER_GAMETYPE_TITLE_SIZE];
	char shown[SERVER_GAMETYPE_TITLE_SIZE];
	char name[GAMETYPE_FOLDER_NAME_SIZE];
	char error[SERVER_CONFIG_ERROR_SIZE];
	int length;

	if (((unsigned long)profile_index & READ_ONLY_BIT) || !profile_title(profile_index, title))
		return;
	gametype_files_name(title, name, sizeof(name));
	length = gametype_folder_read(folder(), name, text, sizeof(text));
	/* (only the file of this gametype: another name can make the same
	file's) */
	if (length < 0 || !server_gametype_parse(text, (size_t)length, &gametype, error, sizeof(error)))
		return;
	file_title(&gametype, name, shown);
	if (titles_match(shown, title) && gametype_folder_remove(folder(), name))
		platform_log("game types: deleted %s.toml", name);
}

void gametype_files_import(
	void)
{
	static char names[MAXIMUM_GAMETYPES][GAMETYPE_FOLDER_NAME_SIZE];
	static char text[GAMETYPE_FOLDER_TEXT_SIZE];
	int count = gametype_folder_list(folder(), names, MAXIMUM_GAMETYPES);
	int index;

	for (index = 0; index < count; index++)
	{
		struct server_gametype gametype;
		char error[SERVER_CONFIG_ERROR_SIZE];
		char title[SERVER_GAMETYPE_TITLE_SIZE];
		int length = gametype_folder_read(folder(), names[index], text, sizeof(text));
		long profile_index;

		if (length < 0 || !server_gametype_parse(text, (size_t)length, &gametype, error, sizeof(error)))
		{
			platform_log("game types: %s.toml is not read: %s", names[index], length < 0 ? "too large" : error);
			continue;
		}
		file_title(&gametype, names[index], title);
		profile_index = gametype_files_profile_find(title);
		if (profile_index != NONE)
		{
			struct game_variant variant;
			struct game_variant_options options;
			struct server_gametype saved, filed;

			/* (the same game: left as it is. Both as a file of the game
			they make holds them: a file may give a setting its base's
			value) */
			if (!playlist_profile_get(profile_index, &variant))
				continue;
			playlist_profile_get_options(profile_index, &options);
			server_gametype_variant_capture(&variant, &options, title, &saved);
			gametype_build(&gametype, title, &variant, &options);
			server_gametype_variant_capture(&variant, &options, title, &filed);
			if (gametypes_same(&saved, &filed))
				continue;
		}
		if (profile_write(&gametype, title) != NONE)
			platform_log("game types: %s.toml is the saved gametype %s", names[index], title);
	}
}

#endif
