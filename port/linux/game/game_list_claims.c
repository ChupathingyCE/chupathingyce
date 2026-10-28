/*
GAME_LIST_CLAIMS.C

The game list's confirmed players (configure.py --game-browser,
HALO_GAME_BROWSER; port/linux/src/browser.c): when a game this machine
plays in ends, on the host and on every machine that joined it, this
machine's players confirm their lines in the game's carnage report with its
player key. Called each frame from the main loop (main.c).

It also tells the game list which Halo profile is player 1's (the active
one, else the last used one), whose own player key is the one in use
(browser_set_player_profile; port/linux/src/save_folder.h).
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "interface/player_ui.h"
#include "tag_files/files.h"
#include "saved games/player_profile.h"
#include "saved games/saved_game_files.h"
#include "../src/browser.h"
#include "../src/save_folder.h"

#include <stdio.h>

/* xbox_files.c's: a game path (u:\UDATA\...) as the folder on disk */
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);
const char *platform_save_root(void);

enum
{
	MAXIMUM_CLAIMED_PLAYERS = 4,
	/* player 1's profile's folder looked at again this often (a renamed
	profile moves to a new folder under the same index) */
	PROFILE_CHECK_INTERVAL = 2000,
};

static boolean game_over_seen;

static struct
{
	boolean checked;
	unsigned long time;
	long active_index;
} player_profile;

/* a profile's game path (u:\UDATA\<id>\ ) as its folder on disk; ""
for any other (the default profiles are read only, in z:\saved) */
static void profile_folder_on_disk(
	char const *path,
	char *folder,
	unsigned long size)
{
	folder[0] = 0;
	if ((path[0] == 'u' || path[0] == 'U') && path[1] == ':' && path[2])
		platform_translate_path(path, folder, size);
}

/* player 1's Halo profile's folder on disk, or "" for none: the active
profile, else the one used last (z:\lastprof.txt, read here, as the
game wrote it: player_ui_remember_player1_profile) */
static void player1_profile_folder(
	char *folder,
	unsigned long size)
{
	char path[MAXIMUM_FILENAME_LENGTH + 1];
	long index = player_ui_get_active_player_profile_index(0);

	folder[0] = 0;
	csmemset(path, 0, sizeof(path));
	if (index != NONE)
	{
		if (TEST_FLAG(index, _saved_game_file_index_valid_bit) &&
			!TEST_FLAG(index, _saved_game_file_index_read_only_bit) &&
			saved_game_file_get_type(index) == _saved_game_file_type_player_profile &&
			player_profile_get_enclosing_directory_path(index, path))
		{
			path[MAXIMUM_FILENAME_LENGTH] = 0;
			profile_folder_on_disk(path, folder, size);
		}
	}
	else
	{
		char last_used[1024];
		FILE *file;

		platform_translate_path("z:\\lastprof.txt", last_used, sizeof(last_used));
		file = fopen(last_used, "rb");
		if (file)
		{
			size_t count = fread(path, 1, MAXIMUM_FILENAME_LENGTH, file);

			path[count] = 0;
			fclose(file);
			profile_folder_on_disk(path, folder, size);
		}
	}
}

/* the game list told player 1's profile when it changes (browser.c keeps
it only when it differs) */
static void update_player_profile(
	void)
{
	char folder[1024];
	long active;

	if (browser_headless())
		return;
	active = player_ui_get_active_player_profile_index(0);
	if (player_profile.checked && active == player_profile.active_index &&
		system_milliseconds() - player_profile.time < PROFILE_CHECK_INTERVAL)
	{
		return;
	}
	player1_profile_folder(folder, sizeof(folder));
	player_profile.active_index = active;
	player_profile.checked = TRUE;
	player_profile.time = system_milliseconds();
	browser_set_player_profile(folder);
}

/* a renamed profile's key, copied into its new folder with its saves
(saved_game_files.c, synchronize_metadata_display_name_with_profile_name,
before the old folder is deleted); TRUE when it has none, or it was
copied */
boolean game_list_profile_key_carry(
	char const *old_directory,
	char const *new_directory)
{
	char from[1024], to[1024];

	platform_translate_path(old_directory, from, sizeof(from));
	platform_translate_path(new_directory, to, sizeof(to));
	return save_folder_profile_key_copy(platform_save_root(), from, to) ? TRUE : FALSE;
}

void game_list_claims_update(
	void)
{
	boolean over;

	update_player_profile();
	/* a network game that has ended (scoring is over) */
	over = game_engine_running() && !game_engine_can_score() &&
		(global_network_game_server_get() || global_network_game_client_get());
	if (over && !game_over_seen)
	{
		unsigned short names[MAXIMUM_CLAIMED_PLAYERS][BROWSER_PLAYER_NAME_LENGTH];
		struct data_iterator iterator;
		struct player_datum *player;
		int count = 0;

		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL &&
			count < MAXIMUM_CLAIMED_PLAYERS)
		{
			if (player->local_player_index != NONE)
			{
				csmemcpy(names[count], player->name, sizeof(names[count]));
				count++;
			}
		}
		browser_claim_game((const unsigned short (*)[BROWSER_PLAYER_NAME_LENGTH])names, count);
	}
	game_over_seen = over;
}

#endif
