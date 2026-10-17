/*
DISCORD_GAME.C

What the game is doing, for its activity on Discord (port/linux/src/
discord_activity.h, p2p_discord.c): the menus, the campaign (its level,
difficulty and whether others play it too) or a multiplayer game (its game
type, map, network game's name and players, a team game's score), when it
began, and the player's discord settings. Looked at once a second; the p2p
thread sends what changed.

Scores shown are the team games' that count in points: Capture the Flag,
Slayer and Race (Oddball and King of the Hill count time, or kills, as the
game type says: left out).

Called each frame from main.c, as game_stats_update is. Not on the
dedicated server.
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "interface/ui_widget.h"
#include "main/main.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "halo_map_families.h"
#include "halo_ui_map_list.h"
#include "../src/discord_activity.h"
#include "../src/p2p.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* the platform layer's */
char const *config_string(char const *name);
int config_boolean(char const *name);
const char *build_identity_user_agent(void);
int browser_dedicated(void);

enum
{
	LOOK_INTERVAL = 1000,
};

static struct
{
	unsigned long looked_time;
	boolean looked;
	struct discord_activity_game game;
	long last_tick;
} discord_game;

/* the game's 16-bit text, as UTF-8 */
static void utf8_from_wide(char *destination, int size, wchar_t const *source, int length)
{
	int used = 0, index;

	for (index = 0; index < length && source[index]; index++)
	{
		unsigned int character = (unsigned short)source[index];

		if (character < 0x80 && used + 1 < size)
			destination[used++] = (char)character;
		else if (character < 0x800 && used + 2 < size)
		{
			destination[used++] = (char)(0xC0 | character >> 6);
			destination[used++] = (char)(0x80 | (character & 0x3F));
		}
		else if (character >= 0x800 && (character < 0xD800 || character > 0xDFFF) && used + 3 < size)
		{
			destination[used++] = (char)(0xE0 | character >> 12);
			destination[used++] = (char)(0x80 | ((character >> 6) & 0x3F));
			destination[used++] = (char)(0x80 | (character & 0x3F));
		}
	}
	destination[used] = 0;
}

/* the platform as players know it: the user agent's "(macOS arm64)" */
static void platform_name(char *name, int size)
{
	char const *open = strchr(build_identity_user_agent(), '(');
	int used = 0;

	if (open)
	{
		for (open++; *open && *open != ' ' && *open != ')' && used < size - 1; open++)
			name[used++] = *open;
	}
	name[used] = 0;
}

static void multiplayer_map(struct discord_activity_game *game, struct network_game *network_game)
{
	char file[DISCORD_ACTIVITY_TEXT_SIZE];
	short family;

	if (network_game)
		map_family_from_wire_name(network_game->map.name, game->map, sizeof(game->map));
	else
		snprintf(game->map, sizeof(game->map), "%s", main_get_multiplayer_map_name());
	family = map_family_parse(game->map, file, sizeof(file));
	if (family != _map_family_xbox)
	{
		wchar_t name[DISCORD_ACTIVITY_TEXT_SIZE];

		ui_map_list_family_name(family, file, name, NUMBEROF(name));
		utf8_from_wide(game->map_name, sizeof(game->map_name), name, NUMBEROF(name));
	}
}

static void team_score(struct discord_activity_game *game)
{
	if (!game->teams || (game->mode != DISCORD_MODE_CTF && game->mode != DISCORD_MODE_SLAYER &&
		game->mode != DISCORD_MODE_RACE))
	{
		return;
	}
	snprintf(game->score, sizeof(game->score), "Red %ld - %ld Blue", game_engine_get_team_score(0),
		game_engine_get_team_score(1));
}

static void look(struct discord_activity_game *game)
{
	struct network_game *network_game = network_game_get_game();
	long tick;

	csmemset(game, 0, sizeof(*game));
	platform_name(game->platform, sizeof(game->platform));
	if (main_menu_is_active() || !global_scenario)
	{
		game->screen = DISCORD_SCREEN_MENUS;
		return;
	}
	if (network_game)
	{
		utf8_from_wide(game->server_name, sizeof(game->server_name), network_game->name, NUMBEROF(network_game->name));
		game->player_count = network_game->player_count;
		game->maximum_player_count = network_game->maximum_players;
	}
	if (global_scenario->type == _scenario_type_solo)
	{
		game->screen = DISCORD_SCREEN_CAMPAIGN;
		snprintf(game->map, sizeof(game->map), "%s", main_get_map_name());
		game->difficulty = game_difficulty_level_get();
		game->coop = network_game || game_is_cooperative();
		/* (a network game's name is a multiplayer one's) */
		game->server_name[0] = 0;
	}
	else
	{
		struct game_variant *variant = game_engine_running() ? game_engine_get_variant() : NULL;

		game->screen = DISCORD_SCREEN_MULTIPLAYER;
		multiplayer_map(game, network_game);
		if (variant)
		{
			long engine = variant->game_engine_index;

			game->mode = engine >= game_engine_ctf && engine <= game_engine_race ? (int)engine : DISCORD_MODE_NONE;
			game->teams = variant->universal_variant.teams ? 1 : 0;
			team_score(game);
		}
	}
	/* when it began: now less its time so far (a game joined under way too) */
	tick = game_time_initialized() ? game_time_get() : 0;
	if (discord_game.game.screen != game->screen || strcmp(discord_game.game.map, game->map) ||
		discord_game.game.mode != game->mode || tick < discord_game.last_tick || !discord_game.game.start_time)
	{
		game->start_time = (long long)time(NULL) * 1000 - (long long)tick * 1000 / TICKS_PER_SECOND;
	}
	else
		game->start_time = discord_game.game.start_time;
	discord_game.last_tick = tick;
}

void discord_game_update(void)
{
	struct discord_activity_options options;

	if (browser_dedicated())
		return;
	if (discord_game.looked && system_milliseconds() - discord_game.looked_time < LOOK_INTERVAL)
		return;
	/* (a map loading: as it was) */
	if (!main_menu_is_active() && !game_map_loaded())
		return;
	discord_game.looked = TRUE;
	discord_game.looked_time = system_milliseconds();
	{
		struct discord_activity_game game;

		look(&game);
		discord_game.game = game;
	}
	csmemset(&options, 0, sizeof(options));
	options.presence = discord_activity_presence(config_string("discord.presence"));
	options.show_server_name = config_boolean("discord.show_server_name") ? 1 : 0;
	options.show_join = config_boolean("discord.show_join") ? 1 : 0;
	p2p_discord_set_game(&discord_game.game, &options);
}

#endif
