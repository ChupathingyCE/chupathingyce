/*
COMPETITIVE.C

Match rules for competitive play (docs/delta.md, Match rules), inspired by
Halo 1: NHE (the Neutral Host Edition) and the Xbox competitive community.
Each is off unless set, so a game plays as the retail game does:

- **No bloom** (network.no_bloom): the pistol, and the sniper rifle while
  unzoomed, fire straight, with no spread. Each machine fires its own
  players' shots (port/linux/NETCODE.md, Shooter's hits), so every machine
  of the game must play it alike: the host plays it only while every machine
  of its game agreed to Delta's competitive capability (the room's), and
  turns it off, saying so, while any machine without it (an OpenCE player,
  an older Chupathingy) is in the game. The shot still takes the random
  numbers the spread would have (and throws the direction away), so nothing
  else of the game draws different numbers for it.
- **Start countdown** (network.start_countdown seconds): at the game's
  start every player is held still (they may look around) and the host
  deals no damage, until the countdown ends. A client holds its own
  players, so it is the room's too, as no bloom is.
- **Item respawn** (network.item_respawn seconds): weapons and powerups
  respawn that often. Only the host places them, so it plays in any game.
- **Host spectates** (network.host_spectates): in a network game a player
  hosts, the host's own players watch (the follow camera) and never spawn:
  a neutral host on a LAN without a dedicated server. The host's alone.

The host works the rules out each frame and sends them to its Delta clients
(RULES, delta_peer_set_rules); a client plays its host's, and a client of a
host without them plays none. A dedicated server sets the host's rules from
its settings (competitive_host_set).

This machine's own display, which needs nothing of anyone:
display.auto_scoreboard (the scoreboard while dead, or always),
display.death_camera ("body": the death camera stays on the body, not on to
other players after 15 seconds) and display.simple_colors (in a game
without teams, every other player drawn red).
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/hud_messaging.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "tag_files/tag_files.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"
#include "units/units.h"
#include "../src/delta_wire.h"
#if defined(HALO_GAME_BROWSER) || defined(HALO_CONSOLE_DELTA)
#include "../src/delta_peer.h"
#define COMPETITIVE_DELTA
#endif
#include "halo_port_features.h"
#ifdef HALO_PORT_CHAT
#include "chat.h"
#endif

#include "competitive.h"

#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* a rule's state (the room's) must hold this long before a player is
	told of it: a joining machine's handshake turns the room's rules off for
	a moment */
	COMPETITIVE_NOTICE_DELAY = 3000,
	COMPETITIVE_MAXIMUM_COUNTDOWN = DELTA_WIRE_RULES_MAXIMUM_COUNTDOWN,
	COMPETITIVE_MAXIMUM_ITEM_RESPAWN = DELTA_WIRE_RULES_MAXIMUM_ITEM_RESPAWN,
	/* the rules every machine of the game must play alike */
	COMPETITIVE_ROOM_RULES = DELTA_RULES_NO_BLOOM | DELTA_RULES_COUNTDOWN,
};

enum
{
	_auto_scoreboard_off = 0,
	_auto_scoreboard_dead,
	_auto_scoreboard_always,
};

/* ---------- prototypes */

/* port_config.c */
int config_boolean(char const *name);
long config_integer(char const *name);
char const *config_string(char const *name);
unsigned long config_changes(void);
void platform_log(char const *format, ...);

/* ---------- globals */

static struct
{
	/* the host's rules as a dedicated server set them (else config.toml's) */
	boolean set;
	boolean no_bloom;
	short countdown;
	short item_respawn;
	boolean host_spectates;
	/* this machine's display settings, read when config.toml changes */
	unsigned long read_at;
	short auto_scoreboard;
	boolean death_camera_body;
	boolean simple_colors;
	/* the rules this machine's game plays by now (the host's own, or its
	host's), and whether it is the host's */
	struct delta_wire_rules rules;
	boolean hosting;
	/* what the player was last told, what the rules are now, and since when
	(COMPETITIVE_NOTICE_DELAY) */
	boolean told;
	struct delta_wire_rules told_rules;
	struct delta_wire_rules pending_rules;
	unsigned long pending_since;
	boolean in_game;
} competitive = { FALSE, FALSE, 0, 0, FALSE, (unsigned long)-1 };

/* ---------- private code */

static boolean same_word(char const *a, char const *b)
{
	for (;; a++, b++)
	{
		char x = *a >= 'A' && *a <= 'Z' ? (char)(*a - 'A' + 'a') : *a;
		char y = *b >= 'A' && *b <= 'Z' ? (char)(*b - 'A' + 'a') : *b;

		if (x != y)
			return FALSE;
		if (!x)
			return TRUE;
	}
}

static boolean same_rules(struct delta_wire_rules const *a, struct delta_wire_rules const *b)
{
	return a->flags == b->flags && a->requested == b->requested && a->countdown == b->countdown &&
		a->item_respawn == b->item_respawn;
}

static void display_read(void)
{
	char const *value;

	if (competitive.read_at == config_changes())
		return;
	competitive.read_at = config_changes();
	value = config_string("display.auto_scoreboard");
	competitive.auto_scoreboard = same_word(value, "always") ? _auto_scoreboard_always :
		same_word(value, "dead") ? _auto_scoreboard_dead : _auto_scoreboard_off;
	competitive.death_camera_body = same_word(config_string("display.death_camera"), "body");
	competitive.simple_colors = config_boolean("display.simple_colors") != 0;
}

/* the rules the host asks for (requested, with their seconds) */
static void host_requested(struct delta_wire_rules *rules)
{
	int no_bloom, countdown, item_respawn, host_spectates;

	competitive_host_get(&no_bloom, &countdown, &item_respawn, &host_spectates);
	csmemset(rules, 0, sizeof(*rules));
	if (no_bloom)
		rules->requested |= DELTA_RULES_NO_BLOOM;
	if (countdown > 0)
	{
		rules->requested |= DELTA_RULES_COUNTDOWN;
		rules->countdown = (unsigned char)countdown;
	}
	if (item_respawn > 0)
	{
		rules->requested |= DELTA_RULES_ITEM_RESPAWN;
		rules->item_respawn = (unsigned short)item_respawn;
	}
	if (host_spectates)
		rules->requested |= DELTA_RULES_HOST_SPECTATES;
}

/* whether every machine of the host's game plays the room's rules: a game
of this machine alone, or hosted over Delta Peer with every machine agreed
to competitive (none while any machine is without it, or Delta is off) */
static boolean host_room_agrees(void)
{
	if (!global_network_game_server_get() || network_game_is_splitscreen_local())
		return TRUE;
#ifdef COMPETITIVE_DELTA
	return delta_peer_game_hosting() && delta_peer_game_room_has(_delta_capability_competitive);
#else
	return FALSE;
#endif
}

/* the rules' words for the player: "no bloom, 5 second start" */
static void rules_words(unsigned char flags, struct delta_wire_rules const *rules, char *text, int size)
{
	int length = 0;

	text[0] = 0;
	if (flags & DELTA_RULES_NO_BLOOM)
		length += snprintf(text + length, (size_t)(size - length), "%sno bloom", length ? ", " : "");
	if ((flags & DELTA_RULES_COUNTDOWN) && length < size)
	{
		length += snprintf(text + length, (size_t)(size - length), "%s%d second start", length ? ", " : "",
			rules->countdown);
	}
	if ((flags & DELTA_RULES_ITEM_RESPAWN) && length < size)
	{
		length += snprintf(text + length, (size_t)(size - length), "%s%d second item respawns", length ? ", " : "",
			rules->item_respawn);
	}
	if ((flags & DELTA_RULES_HOST_SPECTATES) && length < size)
		snprintf(text + length, (size_t)(size - length), "%sthe host spectates", length ? ", " : "");
}

static void tell(char const *text)
{
	platform_log("competitive: %s", text);
#ifdef HALO_PORT_CHAT
	/* (a dedicated server has no player to tell) */
	if (local_player_count() > 0)
		chat_note(text);
#endif
}

/* the player told of the rules once they have held a moment: those played,
and those the host asked for that the game does not play (and why) */
static void notices_update(void)
{
	unsigned long now = system_milliseconds();
	unsigned char off;
	char words[128];
	char text[192];

	if (!same_rules(&competitive.rules, &competitive.pending_rules))
	{
		competitive.pending_rules = competitive.rules;
		competitive.pending_since = now;
		return;
	}
	if (now - competitive.pending_since < COMPETITIVE_NOTICE_DELAY ||
		(competitive.told && same_rules(&competitive.told_rules, &competitive.rules)))
	{
		return;
	}
	if (!competitive.told && !competitive.rules.requested)
	{
		competitive.told = TRUE;
		competitive.told_rules = competitive.rules;
		return;
	}
	rules_words(competitive.rules.flags, &competitive.rules, words, sizeof(words));
	if (words[0])
	{
		snprintf(text, sizeof(text), "Rules: %s", words);
		tell(text);
	}
	off = (unsigned char)(competitive.rules.requested & ~competitive.rules.flags);
	if (off)
	{
		rules_words(off, &competitive.rules, words, sizeof(words));
		snprintf(text, sizeof(text), "Off for this game: %s (not every player is on Chupathingy)", words);
		tell(text);
	}
	competitive.told = TRUE;
	competitive.told_rules = competitive.rules;
}

/* the weapon's tag is the one named ("weapons\\pistol\\pistol"), case aside */
static boolean weapon_named(long definition_index, char const *name)
{
	char const *tag_name = definition_index != NONE ? tag_get_name(definition_index) : NULL;

	return tag_name && same_word(tag_name, name);
}

/* ---------- public code */

void competitive_host_set(int no_bloom, int countdown, int item_respawn, int host_spectates)
{
	competitive.set = TRUE;
	competitive.no_bloom = no_bloom != 0;
	competitive.countdown = (short)PIN(countdown, 0, COMPETITIVE_MAXIMUM_COUNTDOWN);
	competitive.item_respawn = (short)PIN(item_respawn, 0, COMPETITIVE_MAXIMUM_ITEM_RESPAWN);
	competitive.host_spectates = host_spectates != 0;
}

void competitive_host_get(int *no_bloom, int *countdown, int *item_respawn, int *host_spectates)
{
	if (competitive.set)
	{
		*no_bloom = competitive.no_bloom;
		*countdown = competitive.countdown;
		*item_respawn = competitive.item_respawn;
		*host_spectates = competitive.host_spectates;
		return;
	}
	*no_bloom = config_boolean("network.no_bloom") != 0;
	*countdown = (int)PIN(config_integer("network.start_countdown"), 0, COMPETITIVE_MAXIMUM_COUNTDOWN);
	*item_respawn = (int)PIN(config_integer("network.item_respawn"), 0, COMPETITIVE_MAXIMUM_ITEM_RESPAWN);
	*host_spectates = config_boolean("network.host_spectates") != 0;
}

void competitive_update(void)
{
	boolean in_game = game_engine_running() && player_data != NULL;

	display_read();
	csmemset(&competitive.rules, 0, sizeof(competitive.rules));
	competitive.hosting = !network_game_distributed_client();
	if (competitive.hosting)
	{
		host_requested(&competitive.rules);
		competitive.rules.flags = competitive.rules.requested;
		if (!host_room_agrees())
			competitive.rules.flags &= (unsigned char)~COMPETITIVE_ROOM_RULES;
		/* (the host's players watch only in a network game it hosts) */
		if (!global_network_game_server_get() || network_game_is_splitscreen_local())
			competitive.rules.flags &= (unsigned char)~DELTA_RULES_HOST_SPECTATES;
#ifdef COMPETITIVE_DELTA
		if (global_network_game_server_get())
			delta_peer_game_set_rules(&competitive.rules);
#endif
	}
#ifdef COMPETITIVE_DELTA
	else
	{
		/* (a host without them, or not heard yet: none) */
		delta_peer_game_host_rules(&competitive.rules);
	}
#endif

	if (in_game != competitive.in_game)
	{
		competitive.in_game = in_game;
		competitive.told = FALSE;
		csmemset(&competitive.pending_rules, 0, sizeof(competitive.pending_rules));
		competitive.pending_since = system_milliseconds();
	}
	if (in_game)
		notices_update();
}

boolean competitive_no_bloom(long weapon_definition_index, boolean zoomed)
{
	if (!(competitive.rules.flags & DELTA_RULES_NO_BLOOM) || !game_engine_running())
		return FALSE;
	return weapon_named(weapon_definition_index, "weapons\\pistol\\pistol") ||
		(!zoomed && weapon_named(weapon_definition_index, "weapons\\sniper rifle\\sniper rifle"));
}

boolean competitive_countdown_frozen(void)
{
	return (competitive.rules.flags & DELTA_RULES_COUNTDOWN) && game_engine_running() &&
		game_time_get() < (long)competitive.rules.countdown * TICKS_PER_SECOND;
}

boolean competitive_damage_blocked(void)
{
	return competitive.hosting && competitive_countdown_frozen();
}

long competitive_item_respawn_ticks(void)
{
	if (!competitive.hosting || !(competitive.rules.flags & DELTA_RULES_ITEM_RESPAWN))
		return 0;
	return (long)competitive.rules.item_respawn * TICKS_PER_SECOND;
}

boolean competitive_player_spectates(long player_index)
{
	struct player_datum *player;

	if (!competitive.hosting || !(competitive.rules.flags & DELTA_RULES_HOST_SPECTATES) || !game_engine_running() ||
		player_index == NONE)
	{
		return FALSE;
	}
	player = player_try_and_get(player_index);
	return player && player->local_player_index != NONE;
}

boolean competitive_scoreboard_shown(short local_player_index)
{
	long player_index;
	struct player_datum *player;

	display_read();
	if (competitive.auto_scoreboard == _auto_scoreboard_off || !game_engine_running())
		return FALSE;
	if (competitive.auto_scoreboard == _auto_scoreboard_always)
		return TRUE;
	player_index = local_player_get_player_index(local_player_index);
	player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	return player && player->unit_index == NONE &&
		(player->statistics.deaths > 0 || competitive_player_spectates(player_index));
}

boolean competitive_death_camera_switches(void)
{
	display_read();
	return !competitive.death_camera_body;
}

real_rgb_color const *competitive_change_colors(long object_index, real_rgb_color const *colors)
{
	static real_rgb_color const red[NUMBER_OF_OBJECT_CHANGE_COLORS] =
	{
		{ 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },
	};
	struct unit_datum *unit;
	long viewer;

	if (!competitive.simple_colors || !game_engine_running() || game_engine_has_teams())
		return colors;
	unit = unit_try_and_get(object_index);
	if (!unit || unit->unit.player_index == NONE || unit->object.type != _object_type_biped)
		return colors;
	viewer = render.local_player_index != NONE ? local_player_get_player_index(render.local_player_index) : NONE;
	return unit->unit.player_index == viewer ? colors : red;
}

void competitive_draw(short local_player_index)
{
	long font_index = hud_get_font_index();
	long ticks_left;
	struct font_header *font;
	real_argb_color color = *global_real_argb_white;
	rectangle2d bounds;
	wchar_t text[48];
	short height;

	(void)local_player_index;
	if (!competitive_countdown_frozen() || font_index == NONE)
		return;
	ticks_left = (long)competitive.rules.countdown * TICKS_PER_SECOND - game_time_get();
	usnprintf(text, NUMBEROF(text) - 1, L"STARTING IN %ld", (ticks_left + TICKS_PER_SECOND - 1) / TICKS_PER_SECOND);
	text[NUMBEROF(text) - 1] = 0;
	/* one line, centered a little above the middle of the view */
	font = font_definition_get(font_index);
	height = (short)(font->ascending_height + font->descending_height);
	bounds = render.camera.window_bounds;
	bounds.y0 = (short)((bounds.y0 + bounds.y1) / 2 - 3 * height);
	bounds.y1 = (short)(bounds.y0 + height);
	draw_string_set_draw_mode(font_index, NONE, 2, 0, &color);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, text);
}
