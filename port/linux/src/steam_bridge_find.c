/*
STEAM_BRIDGE_FIND.C

Where the bridge's client backend finds a steam_api library
(steam_client_bridge.h), always one already on the player's computer:

- paths.steam_api, when set.
- Windows (64-bit): the steam_api64.dll inside MCC's install, in any of
  the player's Steam libraries (steam_workshop.c). Only the folders a game
  keeps its programs in are looked through (Engine/.../Win64,
  MCC/Binaries/Win64), not MCC's maps.
- macOS, Linux and the BSDs: MCC's library is a Windows one, and Steam
  itself ships none (only its client library, whose interfaces are not
  documented), so the steam_api library some other installed game ships,
  found in steamapps/common within a bounded walk. One built for another
  architecture (an x86_64-only one under arm64) won't load, and the next
  is tried (steam_bridge_api.c).

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h): a console builds this to nothing */

#include "steam_client_bridge.h"
#include "steam_workshop.h"
#include "posix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *config_string(const char *name);

#if defined(_WIN32)
#define LIBRARY_NAME "steam_api64.dll"
#elif defined(__APPLE__)
#define LIBRARY_NAME "libsteam_api.dylib"
#else
#define LIBRARY_NAME "libsteam_api.so"
#endif

enum
{
	/* the folders a walk looks into at most */
	WALK_BUDGET = 6000,
};

static int is_file(const char *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0 && !(information.flags & _posix_file_is_directory);
}

static int is_directory(const char *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0 && (information.flags & _posix_file_is_directory);
}

static int same_name(const char *a, const char *b, int prefix)
{
	for (; *a && *b; a++, b++)
	{
		char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
		char y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;

		if (x != y)
			return 0;
	}
	return !*b && (prefix || !*a);
}

/* (Windows: the folders on the way to a game's programs) */
static int program_folder(const char *name)
{
#ifdef _WIN32
	static const char *const names[] = { "engine", "mcc", "binaries", "thirdparty", "steamworks", "win64" };
	int index;

	for (index = 0; index < (int)(sizeof(names) / sizeof(names[0])); index++)
	{
		if (same_name(name, names[index], 0))
			return 1;
	}
	/* (Unreal's Steamworks/Steamv1NN) */
	return same_name(name, "steamv", 1);
#else
	/* (anywhere but a game's data: links and dot folders passed over) */
	return name[0] != '.' && !same_name(name, "__macosx", 0);
#endif
}

struct walk
{
	char (*paths)[STEAM_BRIDGE_PATH_SIZE];
	int maximum;
	int count;
	int budget;
};

static void walk(struct walk *state, const char *folder, int depth)
{
	char found[256], child[STEAM_BRIDGE_PATH_SIZE], name[256];
	void *directory;

	if (state->count >= state->maximum || state->budget-- <= 0)
		return;
	if (posix_find_entry_case_insensitive(folder, LIBRARY_NAME, found, sizeof(found)) &&
		snprintf(child, sizeof(child), "%s/%s", folder, found) < (int)sizeof(child) && is_file(child))
	{
		snprintf(state->paths[state->count++], STEAM_BRIDGE_PATH_SIZE, "%s", child);
		return;
	}
	if (depth <= 0 || !(directory = posix_directory_open(folder)))
		return;
	while (state->count < state->maximum && state->budget > 0 && posix_directory_next(directory, name, sizeof(name)))
	{
		if (program_folder(name) && snprintf(child, sizeof(child), "%s/%s", folder, name) < (int)sizeof(child) &&
			!posix_is_link(child) && is_directory(child))
		{
			walk(state, child, depth - 1);
		}
	}
	posix_directory_close(directory);
}

#if !defined(_WIN32) || defined(_WIN64)
/* the Steam libraries walked: MCC's install on Windows, the games elsewhere */
static int walk_libraries(char (*paths)[STEAM_BRIDGE_PATH_SIZE], int maximum)
{
	char (*libraries)[STEAM_WORKSHOP_PATH_SIZE] = malloc(sizeof(*libraries) * STEAM_WORKSHOP_MAXIMUM_LIBRARIES);
	int count = libraries ? steam_workshop_libraries(libraries, STEAM_WORKSHOP_MAXIMUM_LIBRARIES) : 0, index;
	struct walk state;

	state.paths = paths;
	state.maximum = maximum;
	state.count = 0;
	state.budget = WALK_BUDGET;
	for (index = 0; index < count && state.count < maximum; index++)
	{
		char folder[STEAM_WORKSHOP_PATH_SIZE + 64];

#ifdef _WIN32
		snprintf(folder, sizeof(folder), "%s/steamapps/common/Halo The Master Chief Collection", libraries[index]);
		walk(&state, folder, 6);
#else
		snprintf(folder, sizeof(folder), "%s/steamapps/common", libraries[index]);
		walk(&state, folder, 9);
#endif
	}
	free(libraries);
	return state.count;
}
#endif

int steam_bridge_api_library_paths(char (*paths)[STEAM_BRIDGE_PATH_SIZE], int maximum, int search)
{
	const char *setting = config_string("paths.steam_api");

	if (maximum <= 0)
		return 0;
	if (setting && setting[0])
		return snprintf(paths[0], STEAM_BRIDGE_PATH_SIZE, "%s", setting) < STEAM_BRIDGE_PATH_SIZE && is_file(paths[0]);
#if defined(_WIN32) && !defined(_WIN64)
	/* (MCC's library is 64-bit: a 32-bit game can't load it) */
	(void)search;
	return 0;
#elif defined(_WIN32)
	/* (Windows: MCC's own, a short walk, even from the game's thread) */
	(void)search;
	return walk_libraries(paths, maximum);
#else
	return search ? walk_libraries(paths, maximum) : 0;
#endif
}

int steam_bridge_steam_present(void)
{
	char (*libraries)[STEAM_WORKSHOP_PATH_SIZE] = malloc(sizeof(*libraries) * STEAM_WORKSHOP_MAXIMUM_LIBRARIES);
	int count = libraries ? steam_workshop_libraries(libraries, STEAM_WORKSHOP_MAXIMUM_LIBRARIES) : 0;

	free(libraries);
	return count > 0;
}
#endif /* HALO_MCC */
