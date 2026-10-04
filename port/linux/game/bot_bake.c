/*
BOT_BAKE.C

Baking the bots' navigation (HALO_BAKE_NAVIGATION; bake-navigation.cmd on
Windows): the game run with no window, to work out the navigation of every
multiplayer map it finds and keep it with the game data, in navcache next to
maps. A map with baked navigation starts with bots at once, on every machine
the game data is copied to; without it the first game on the map with bots
works it out (seconds to a minute: bot_nav.c, bot_aas.c) and keeps it in
the save folder.

The maps are those of the maps folder that are multiplayer maps, whatever
their names (a campaign map has no navigation of this kind: its own AI's),
and on the builds with them Halo PC's maps: the Custom Edition maps
(<name>@ce) and HaloMD's (<name>@md), wherever the game finds them
(map_families.c).

It hosts each map in turn in a split screen lobby of one player and no
bots: the map loads, bots.c builds its navigation (bots_map_loaded), and
bot_nav.c and bot_aas.c, told they are baking, build it afresh and write it
to the game data's folder. Then the game is left for the next map, and when
the last is done the process ends: 0 when every map has its navigation, 1
when one could not be baked.

"new" bakes the maps that have no baked navigation of this build's kind,
"all" (or anything else) every map. A map has it when every one of its
structure BSPs does (a few Custom Edition maps have two, and a script that
changes to the second as the game starts): how many it has is written
beside its files when the last is baked (bot_nav.c, navm_baked_bsps), since
only the loaded map says.

Called from the main loop every frame (main.c).
*/

#include "cseries.h"
#include "main/main.h"
#include "cache/cache_files.h"
#include "interface/player_ui.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "networking/network_server_manager.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xtl.h>

#include "bots.h"
#include "bot_nav.h"
#include "halo_map_families.h"

/* the platform layer's */
void platform_log(char const *format, ...);
/* bot_port.c's */
unsigned long long bot_time_microseconds(void);

/* ---------- constants */

enum
{
	MAXIMUM_BAKED_MAPS = 128,
	MAXIMUM_BAKED_BSPS = 32, /* (of one map: more, and it is not baked) */
	/* a map file's header: 'head', the map's name and what it is */
	BAKE_HEADER_SIZE = 0x64,
	BAKE_HEADER_NAME_OFFSET = 0x20,
	BAKE_HEADER_TYPE_OFFSET = 0x60,
	BAKE_HEADER_VERSION_OFFSET = 0x4,
	BAKE_HEADER_MULTIPLAYER = 1,
	/* the Xbox's maps (Halo PC's are found by map_families.c) */
	BAKE_VERSION_XBOX = 5,
};

/* every baked game's random seed */
#define BAKE_RANDOM_SEED 0x62616b65

/* a lobby that has not started its game in this long is given up on */
#define BAKE_LOBBY_TIMEOUT_SECONDS 60.0
/* ... and a game that has not been left in this long ends the bake */
#define BAKE_LEAVE_TIMEOUT_SECONDS 30.0
/* (what a frame's time counts for at most: the frame a map loads in is as
long as its navigation takes) */
#define BAKE_MAXIMUM_FRAME_SECONDS 0.1f

/* ---------- structures */

enum
{
	_bake_map_waiting,
	_bake_map_current, /* (had its navigation already) */
	_bake_map_baked,
	_bake_map_failed,
	_bake_map_too_large, /* (no navigation for it at all: bot_nav.c) */
	NUMBER_OF_BAKE_MAP_STATES
};

struct bake_map
{
	/* (as the lobby has it: a Custom Edition map's ends in @ce, a HaloMD
	map's in @md) */
	char name[40];
	short state;
	real seconds;
};

static struct
{
	boolean checked;
	boolean enabled;
	boolean all;
	struct bake_map maps[MAXIMUM_BAKED_MAPS];
	short map_count;
	short map_index;

	/* hosting: the lobby is up */
	boolean hosting;
	/* its game has been asked to start */
	boolean started;
	/* the map's done with: the main menu, then the next */
	boolean leaving;
	/* (the lobby's first map is there) */
	boolean lobby_checked;
	/* (the lobby's one player) */
	boolean player_added;
	real menu_seconds;
	real lobby_seconds;
	real apply_seconds;
	long files_before;
	/* (the structure BSPs of the map being baked that are done: a map with a
	few has each in turn, whichever its script has it on when it starts) */
	unsigned long bsps_done;
	unsigned long long map_start;
	unsigned long long leave_start;
	unsigned long long start;
} bake;

/* ---------- private code */

/* a map to bake, by the name the lobby plays it under (each once, in the
names' order, whatever the folders': the same every time) */
static void bake_add_map(
	char const *name)
{
	short i, j;

	if (bake.map_count >= MAXIMUM_BAKED_MAPS || strlen(name) >= sizeof(bake.maps[0].name))
		return;
	for (i = 0; i < bake.map_count && strcmp(bake.maps[i].name, name) < 0; i++)
		;
	if (i < bake.map_count && !strcmp(bake.maps[i].name, name))
		return;
	for (j = bake.map_count; j > i; j--)
		bake.maps[j] = bake.maps[j - 1];
	memset(&bake.maps[i], 0, sizeof(bake.maps[i]));
	snprintf(bake.maps[i].name, sizeof(bake.maps[i].name), "%s", name);
	bake.maps[i].state = _bake_map_waiting;
	bake.map_count++;
}

/* the Xbox's multiplayer maps: those of the maps folder, by the name in
each one's header */
static void bake_find_xbox_maps(
	void)
{
	WIN32_FIND_DATAA data;
	char pattern[256];
	HANDLE find;

	snprintf(pattern, sizeof(pattern), "%s*.map", cache_files_map_directory());
	find = FindFirstFileA(pattern, &data);
	if (find == INVALID_HANDLE_VALUE)
		return;
	do
	{
		unsigned char header[BAKE_HEADER_SIZE];
		char path[512];
		char name[40];
		FILE *file;
		boolean multiplayer;

		if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		snprintf(path, sizeof(path), "%s%s", cache_files_map_directory(), data.cFileName);
		file = fopen(path, "rb");
		if (!file)
			continue;
		multiplayer = fread(header, sizeof(header), 1, file) == 1 && !memcmp(header, "daeh", 4) &&
			(long)(header[BAKE_HEADER_VERSION_OFFSET] | header[BAKE_HEADER_VERSION_OFFSET + 1] << 8 |
				header[BAKE_HEADER_VERSION_OFFSET + 2] << 16) == BAKE_VERSION_XBOX &&
			header[BAKE_HEADER_TYPE_OFFSET] == BAKE_HEADER_MULTIPLAYER && !header[BAKE_HEADER_TYPE_OFFSET + 1];
		fclose(file);
		if (!multiplayer || !header[BAKE_HEADER_NAME_OFFSET])
			continue;
		snprintf(name, sizeof(name), "%.31s", (char const *)header + BAKE_HEADER_NAME_OFFSET);
		bake_add_map(name);
	} while (FindNextFileA(find, &data));
	/* (as ui_map_list.c closes its own: the platform layer's find handles) */
	CloseHandle(find);
}

#ifdef HALO_CUSTOM_EDITION
/* one of Halo PC's multiplayer maps (map_family_list): played by its
file's name and its family's suffix */
static void bake_family_map_found(
	char const *file,
	void *context)
{
	char name[40];
	char const *suffix = map_family_suffix(*(short const *)context);

	if (strlen(file) + strlen(suffix) >= sizeof(name))
		return;
	snprintf(name, sizeof(name), "%s%s", file, suffix);
	bake_add_map(name);
}
#endif

static void bake_find_maps(
	void)
{
	bake_find_xbox_maps();
#ifdef HALO_CUSTOM_EDITION
	{
		short family;

		for (family = _map_family_custom_edition; family < NUMBER_OF_MAP_FAMILIES; family++)
			map_family_list(family, bake_family_map_found, &family);
	}
#endif
}

/* has a map its baked navigation, of this build's kind, for every one of
its structure BSPs? By the count its bake wrote down. A bake from before
that was written down: by its first BSP's files, and those of each BSP
after that there are files for (so a second BSP's stale files are seen,
and only one whose files were never written is not: "all" bakes it) */
static boolean bake_map_is_current(
	char const *name)
{
	long count = navm_baked_bsps(name);
	long bsp_index;

	if (count > 0)
	{
		for (bsp_index = 0; bsp_index < count; bsp_index++)
			if (!aas_baked_current(name, bsp_index))
				return FALSE;
		return TRUE;
	}
	if (!aas_baked_current(name, 0))
		return FALSE;
	for (bsp_index = 1; bsp_index < MAXIMUM_BAKED_BSPS && navm_baked_exists(name, bsp_index); bsp_index++)
		if (!aas_baked_current(name, bsp_index))
			return FALSE;
	return TRUE;
}

static void bake_read_settings(
	void)
{
	char const *setting = getenv("HALO_BAKE_NAVIGATION");
	short i;
	short waiting = 0;

	bake.checked = TRUE;
	if (!setting || !setting[0] || !strcmp(setting, "false") || !strcmp(setting, "0"))
		return;
	bake.enabled = TRUE;
	bake.all = strcmp(setting, "new") != 0;
	bake.start = bot_time_microseconds();
	bake_find_maps();
	for (i = 0; i < bake.map_count; i++)
	{
		if (!bake.all && bake_map_is_current(bake.maps[i].name))
			bake.maps[i].state = _bake_map_current;
		else
			waiting++;
	}
	/* (the lobbies have one player and no bots: the navigation is built
	for the map, bots.c's bots_map_loaded, whoever plays) */
	bots_set_count(0);
	bots_set_fill(0);
	navm_set_baking(TRUE);
	debug_no_drawing = TRUE;
	platform_log("bake: %d multiplayer map%s in %s, %d to bake", (int)bake.map_count, bake.map_count == 1 ? "" : "s",
		cache_files_map_directory(), (int)waiting);
}

/* all done: what was baked, and the process ends */
static void bake_finish(
	void)
{
	short counts[NUMBER_OF_BAKE_MAP_STATES] = { 0 };
	short i;

	for (i = 0; i < bake.map_count; i++)
		counts[bake.maps[i].state]++;
	for (i = 0; i < bake.map_count; i++)
		if (bake.maps[i].state == _bake_map_failed)
			platform_log("bake: %s has no baked navigation (the game works it out when the map is first played with bots)", bake.maps[i].name);
	if (!bake.map_count)
		platform_log("bake: no multiplayer maps in %s (the game data's maps folder)", cache_files_map_directory());
	platform_log("bake: %d baked, %d already baked, %d too large for navigation, %d failed, in %.1f s", (int)counts[_bake_map_baked],
		(int)counts[_bake_map_current], (int)counts[_bake_map_too_large], (int)(counts[_bake_map_failed] + counts[_bake_map_waiting]),
		(bot_time_microseconds() - bake.start) / 1.0e6);
	fflush(NULL);
	/* (as closing the window ends it; there is none) */
	exit(counts[_bake_map_failed] || counts[_bake_map_waiting] || !bake.map_count ? 1 : 0);
}

/* the map to bake now, or NULL when none is left */
static struct bake_map *bake_current_map(
	void)
{
	while (bake.map_index < bake.map_count && bake.maps[bake.map_index].state != _bake_map_waiting)
		bake.map_index++;
	return bake.map_index < bake.map_count ? &bake.maps[bake.map_index] : NULL;
}

/* this map's done with, baked or not: out of its game or its lobby */
static void bake_leave(
	struct bake_map *map,
	short state)
{
	map->state = state;
	map->seconds = (real)((bot_time_microseconds() - bake.map_start) / 1.0e6);
	if (state == _bake_map_baked)
		platform_log("bake: %s baked in %.1f s", map->name, map->seconds);
	bake.leaving = TRUE;
	bake.leave_start = bot_time_microseconds();
	bake.hosting = FALSE;
	bake.started = FALSE;
	bake.player_added = FALSE;
	bake.menu_seconds = 0.0f;
	network_game_abort();
}

/* (in either case) */
static boolean bake_names_differ(
	char const *name0,
	char const *name1)
{
	while (*name0 && *name1)
	{
		char character0 = *name0++;
		char character1 = *name1++;

		if (character0 >= 'A' && character0 <= 'Z')
			character0 += 'a' - 'A';
		if (character1 >= 'A' && character1 <= 'Z')
			character1 += 'a' - 'A';
		if (character0 != character1)
			return TRUE;
	}
	return *name0 || *name1;
}

/* the lobby's map is this one, its game slayer: TRUE once the settings the
server sent back say so */
static boolean bake_apply_map(
	struct bake_map *map,
	boolean set)
{
	struct network_game_server *server = global_network_game_server_get();
	struct network_game_client *client = global_network_game_client_get();
	struct network_game *settings;
	struct game_variant variant;
	char path[128];
	boolean applied = TRUE;

	if (!server || !client)
		return FALSE;
	settings = network_game_client_get_game(client);
	if (!settings)
		return FALSE;
	/* (a Halo PC map's name, <name>@ce or <name>@md, stays bare: cache_files_windows.c) */
	if (strchr(map->name, '@'))
		snprintf(path, sizeof(path), "%s", map->name);
	else
		snprintf(path, sizeof(path), "levels\\test\\%s\\%s", map->name, map->name);
	variant = *game_engine_get_variant_by_name(&variant, "slayer");
	if (bake_names_differ(settings->map.name, path))
	{
		applied = FALSE;
		if (set)
			network_game_server_change_map_name(server, path);
	}
	if (settings->variant.game_engine_index != variant.game_engine_index ||
		settings->variant.universal_variant.teams != variant.universal_variant.teams)
	{
		applied = FALSE;
		if (set)
		{
			player_ui_set_game_variant(&variant);
			network_game_server_change_game_variant(server, &variant);
		}
	}
	return applied;
}

/* ---------- public code */

boolean bot_bake_enabled(
	void)
{
	if (!bake.checked)
		bake_read_settings();
	return bake.enabled;
}

void bot_bake_update(
	boolean main_menu_loaded,
	real seconds)
{
	struct network_game_server *server;
	struct network_game_client *client;
	struct bake_map *map;

	if (!bot_bake_enabled())
		return;
	seconds = MIN(seconds, BAKE_MAXIMUM_FRAME_SECONDS);
	map = bake_current_map();
	server = global_network_game_server_get();
	client = global_network_game_client_get();

	/* out of the last map's game: the main menu first */
	if (bake.leaving)
	{
		if (!main_menu_loaded || server || client)
		{
			/* (never left: the rest are not baked, rather than a build
			that waits for ever) */
			if ((bot_time_microseconds() - bake.leave_start) / 1.0e6 > BAKE_LEAVE_TIMEOUT_SECONDS)
			{
				platform_log("bake: the game did not return to the main menu; giving up");
				bake_finish();
			}
			return;
		}
		bake.leaving = FALSE;
	}
	if (!map)
	{
		bake_finish();
		return;
	}

	/* the map is loaded and its game running: its navigation was built as
	it loaded (bots.c, bots_map_loaded), and written */
	if (bake.hosting && bake.started && client && !main_menu_loaded && game_in_progress() && game_engine_running())
	{
		/* (the BSP the navigation in hand is of: the one the map loaded
		with, or the one switched to below; not, if the map's script has
		changed it since, the one the game is on now) */
		long built = bots_navigation_bsp();

		if (aas_ready() && built >= 0 && built < MAXIMUM_BAKED_BSPS && navm_baked_files() >= bake.files_before + 2 &&
			aas_baked_current(map->name, built))
		{
			/* a map with other structure BSPs (one its script switches to
			as the game starts, in a few Custom Edition maps): each of them
			too, switched to here and worked out as the first was. Every
			one, or the map is not baked: one that cannot be switched to
			is a failure, not the end of the list. */
			struct scenario *scenario = global_scenario_get();
			long count = scenario ? scenario->structure_bsp_references.count : 1;
			long next;

			bake.bsps_done |= 1UL << built;
			for (next = 0; next < count && next < MAXIMUM_BAKED_BSPS && (bake.bsps_done & (1UL << next)); next++)
				;
			if (count > MAXIMUM_BAKED_BSPS)
			{
				platform_log("bake: %s has %ld structure BSPs, more than are baked (%d)", map->name, count, (int)MAXIMUM_BAKED_BSPS);
				bake_leave(map, _bake_map_failed);
			}
			else if (next >= count)
			{
				if (!navm_baked_bsps_write(map->name, count))
				{
					platform_log("bake: %s: its count of structure BSPs could not be written to the game data's navcache folder", map->name);
					bake_leave(map, _bake_map_failed);
				}
				else
					bake_leave(map, _bake_map_baked);
			}
			else if (next == global_structure_bsp_index || scenario_switch_structure_bsp((short)next))
			{
				/* (already on it: the map's script got there first, and the
				navigation in hand is still the last one's) */
				bake.files_before = navm_baked_files();
				bots_map_loaded();
				if (bots_navigation_bsp() != next)
				{
					platform_log("bake: %s: no navigation could be worked out for its structure BSP %ld", map->name, next);
					bake_leave(map, navm_map_too_large() ? _bake_map_too_large : _bake_map_failed);
				}
			}
			else
			{
				platform_log("bake: %s: its structure BSP %ld could not be switched to", map->name, next);
				bake_leave(map, _bake_map_failed);
			}
		}
		else if (!aas_ready() && navm_map_too_large())
		{
			/* (not a failure: there is nothing to bake, and the bots play
			the map without navigation) */
			platform_log("bake: %s is too large for the bots' navigation: left out", map->name);
			bake_leave(map, _bake_map_too_large);
		}
		else
		{
			platform_log("bake: %s: %s", map->name, aas_ready() ? "its navigation could not be written to the game data's navcache folder" :
				"no navigation could be worked out");
			bake_leave(map, _bake_map_failed);
		}
		return;
	}

	if (!main_menu_loaded)
		return;

	if (!bake.hosting)
	{
		/* (the main menu settling first) */
		bake.menu_seconds += seconds;
		if (bake.menu_seconds < 0.5f)
			return;
		/* a lobby opens with the playlist's first map, whatever it is to
		play: without that map the game has no lobby at all (it is the
		damaged disc message, which waits for a button), and nothing bakes */
		if (!bake.lobby_checked)
		{
			struct game_variant variant;
			char first[256];
			char const *name;
			short i;

			bake.lobby_checked = TRUE;
			game_engine_playlist_initialize();
			game_engine_get_current_stage(&variant, first);
			name = strrchr(first, '\\') ? strrchr(first, '\\') + 1 : first;
			for (i = 0; i < bake.map_count && bake_names_differ(bake.maps[i].name, name); i++)
				;
			if (i == bake.map_count)
			{
				platform_log("bake: %s%s.map is missing, the map every multiplayer lobby opens with: nothing can be baked",
					cache_files_map_directory(), name);
				bake_finish();
				return;
			}
		}
		bake.map_start = bot_time_microseconds();
		bake.files_before = navm_baked_files();
		bake.bsps_done = 0;
		main_set_multiplayer_map_name(map->name);
		player_ui_fast_setup_network_server();
		/* (nobody joins it) */
		network_game_accept_remote_connections(FALSE);
		/* the same game every time: a map's trees and rocks are of the
		shapes the game's random numbers pick, and the navigation goes
		around them, so the same seed bakes the same files */
		if (global_network_game_server_get())
			network_game_set_random_seed(BAKE_RANDOM_SEED);
		if (!global_network_game_server_get() || !global_network_game_client_get())
		{
			platform_log("bake: %s: could not host a game", map->name);
			map->state = _bake_map_failed;
			bake.menu_seconds = 0.0f;
			return;
		}
		bake.hosting = TRUE;
		bake.started = FALSE;
		bake.player_added = FALSE;
		bake.lobby_seconds = 0.0f;
		bake.apply_seconds = 0.0f;
		platform_log("bake: %s (%d of %d)", map->name, (int)bake.map_index + 1, (int)bake.map_count);
		return;
	}

	/* the game went down */
	if (!server || !client)
	{
		platform_log("bake: %s: the game went down", map->name);
		map->state = _bake_map_failed;
		bake.hosting = FALSE;
		bake.menu_seconds = 0.0f;
		return;
	}

	/* the lobby: the map and the game type, and once they are set and its
	player is in it (as pressing A in the lobby adds one), the game */
	bake.lobby_seconds += seconds;
	if ((bot_time_microseconds() - bake.map_start) / 1.0e6 > BAKE_LOBBY_TIMEOUT_SECONDS)
	{
		platform_log("bake: %s: its game did not start", map->name);
		bake_leave(map, _bake_map_failed);
		return;
	}
	if (!bake.started)
	{
		struct network_game *settings = network_game_client_get_game(client);

		/* (the settings come back from the server a moment after a change) */
		bake.apply_seconds -= seconds;
		if (bake.apply_seconds <= 0.0f)
		{
			bake.apply_seconds = 0.25f;
			bake_apply_map(map, TRUE);
		}
		if (!bake.player_added && bake.lobby_seconds >= 0.25f)
			bake.player_added = network_game_client_add_player(client, 0);
		if (bake.lobby_seconds >= 0.25f && settings && settings->player_count > 0 && bake_apply_map(map, FALSE))
		{
			bake.started = TRUE;
			bake.apply_seconds = 0.0f;
			network_game_client_request_immediate_start();
		}
	}
	else
	{
		/* asked to start and still in the lobby: asked again */
		bake.apply_seconds += seconds;
		if (bake.apply_seconds >= 5.0f)
			bake.started = FALSE;
	}
}
