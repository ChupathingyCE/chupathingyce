/*
MEDAL_HUD.C

The medals on the HUD (display.medals, off by default: retail Halo shows
none), as icons in the kill feed: the site's own (medal_icons.h,
port/assets/medals), drawn by the overlay (ui_overlay_medal) on GL and bgfx
alike. Each medal sits at the end of the HUD message of the kill that
earned it ("You killed Sarge" and a Double Kill and a Killing Spree after
it), on its last line, at the text's height, and comes, fades and scrolls
with that line: it is drawn with it each frame, in its color
(hud_messaging.c), so it never stays on its own.

A kill's medals are worked out (game_stats.c) just before the game prints
the kill's line, in the same tick: they wait here (medal_hud_earned) for
the next line printed for that player (medal_hud_printed), which they are
tied to by its time and number (medal_lines.h). Medals no line of their
tick took get a line of their own, the icons alone, as the HUD is drawn
(medal_hud_flush) or as the next tick's medals come. A player quitting's
lines take none (medal_hud_hold).

Hidden while anything covers the kill feed: a menu over that player's view,
the scoreboard, the carnage report, this port's screens and DELTA (the
overlay draws over the game's own drawing). With display.medals off nothing
is kept and the kill feed is retail's. The builds without the icons
(HALO_MEDAL_ICONS, halo_console_features.h: every build without the game
list, the consoles' too, and the dedicated server) show each medal as a
line of the HUD's messages instead (game_stats.c).
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"

#ifdef HALO_MEDAL_ICONS

#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/hud_messaging.h"
#include "interface/ui_widget.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "../src/medal_icons.h"
#include "../src/medal_lines.h"
#include "../src/ui_overlay.h"
#include "medal_hud.h"

unsigned long system_milliseconds(void);
int config_boolean(char const *name);
char const *config_string(char const *name);
/* the screens drawn over the game (ui_widget.c's) */
boolean browser_screen_active(void);
boolean moderation_screen_active(void);
boolean invite_prompt_active(void);
#ifndef HALO_CONSOLE_NO_CONTROLS
boolean controls_screen_active(void);
#endif
#ifndef HALO_CONSOLE_NO_DELTA_PROFILE
boolean profile_screen_active(void);
#endif
#ifdef HALO_GUIDE
boolean guide_active(void);
#endif

/* ---------- constants */

enum
{
	/* the local players (MAXIMUM_NUMBER_OF_LOCAL_PLAYERS's) */
	MAXIMUM_PLAYERS = 4,
	/* debug.medal_test's line comes again this often (milliseconds) */
	TEST_INTERVAL = 4000,
};

/* ---------- globals */

static struct medal_lines medal_hud[MAXIMUM_PLAYERS];
/* a leftovers' line being printed (it takes them whatever their tick), and
lines printed that take none (a player quitting's) */
static boolean medal_hud_leftovers_printing;
static boolean medal_hud_holding;

static unsigned long medal_hud_test_time;

/* ---------- private code */

static boolean medal_hud_on(
	void)
{
	return config_boolean("display.medals") && ui_overlay_medals_available();
}

/* leftovers (medals no line of their tick took): a line of their own */
static void print_leftovers(
	short local_player_index)
{
	if (!medal_hud[local_player_index].pending_count)
		return;
	medal_hud_leftovers_printing = TRUE;
	hud_print_message(local_player_index, L"");
	medal_hud_leftovers_printing = FALSE;
	medal_hud[local_player_index].pending_count = 0;
}

/* whether something covers the player's kill feed: a menu over their view,
the scoreboard (BACK held: their messages hidden), the carnage report, this
port's screens or DELTA */
static boolean feed_covered(
	short local_player_index)
{
	if (ui_widgets_active_for_local_player(local_player_index) ||
		!game_engine_hud_draw_messages(local_player_get_player_index(local_player_index)) ||
		game_engine_showing_postgame() || game_engine_postgame_mode() != 0 || browser_screen_active() ||
		moderation_screen_active() || invite_prompt_active())
	{
		return TRUE;
	}
#ifndef HALO_CONSOLE_NO_CONTROLS
	if (controls_screen_active())
		return TRUE;
#endif
#ifndef HALO_CONSOLE_NO_DELTA_PROFILE
	if (profile_screen_active())
		return TRUE;
#endif
#ifdef HALO_GUIDE
	if (guide_active())
		return TRUE;
#endif
	return FALSE;
}

/* debug.medal_test ("double_kill killing_spree : You killed Sarge"): those
medals on the first player's HUD every few seconds, on a line of that text
(after the colon; none, the icons alone). For tests and screenshots; empty,
the default, nothing */
static void test_medals(
	short local_player_index)
{
	char const *value = config_string("debug.medal_test");
	unsigned long now = system_milliseconds();
	char key[48];
	wchar_t text[63];
	int length;
	short index;

	if (local_player_index != 0 || !value || !value[0] ||
		(medal_hud_test_time && now - medal_hud_test_time < TEST_INTERVAL))
	{
		return;
	}
	medal_hud_test_time = now;
	while (*value && *value != ':')
	{
		while (*value == ' ')
			value++;
		for (length = 0; value[length] && value[length] != ' ' && value[length] != ':'; length++)
			;
		if (length > 0 && length < (int)sizeof(key))
		{
			csmemcpy(key, value, (unsigned long)length);
			key[length] = 0;
			medal_hud_earned(local_player_index, key);
		}
		value += length;
	}
	if (*value != ':')
		return;
	for (value++; *value == ' '; value++)
		;
	for (index = 0; value[index] && index < (short)NUMBEROF(text) - 1; index++)
		text[index] = (wchar_t)(unsigned char)value[index];
	text[index] = 0;
	hud_print_message(local_player_index, text);
}

/* ---------- public code */

void medal_hud_earned(
	short local_player_index,
	char const *key)
{
	int icon = medal_icon_find(key);
	long now = game_time_get();

	if (local_player_index < 0 || local_player_index >= MAXIMUM_PLAYERS || icon < 0 || !medal_hud_on())
		return;
	/* (an earlier tick's that no line took, two ticks run between frames:
	their own line first) */
	if (medal_lines_leftover(&medal_hud[local_player_index], now))
		print_leftovers(local_player_index);
	medal_lines_earn(&medal_hud[local_player_index], icon, now);
}

void medal_hud_printed(
	short local_player_index,
	long time,
	short number)
{
	if (local_player_index < 0 || local_player_index >= MAXIMUM_PLAYERS || medal_hud_holding)
		return;
	medal_lines_printed(&medal_hud[local_player_index], time, number, medal_hud_leftovers_printing);
}

void medal_hud_hold(
	boolean hold)
{
	medal_hud_holding = hold;
}

void medal_hud_flush(
	short local_player_index)
{
	if (local_player_index < 0 || local_player_index >= MAXIMUM_PLAYERS)
		return;
	if (config_boolean("display.medals"))
		test_medals(local_player_index);
	/* (medals no line took: a line of their own) */
	print_leftovers(local_player_index);
}

short medal_hud_line_count(
	short local_player_index,
	long time,
	short number)
{
	int slot;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_PLAYERS || time == NONE)
		return 0;
	slot = medal_lines_find(&medal_hud[local_player_index], time, number);
	return slot >= 0 ? medal_hud[local_player_index].lines[slot].count : 0;
}

void medal_hud_draw_line(
	short local_player_index,
	long time,
	short number,
	real x,
	real top,
	real size,
	real gap,
	real alpha)
{
	struct rasterizer_globals_reserved04 const *screen = &rasterizer_globals.reserved04;
	real screen_width = (real)(screen->screen_bounds.x1 - screen->screen_bounds.x0);
	real screen_height = (real)(screen->screen_bounds.y1 - screen->screen_bounds.y0);
	real scale, layout_x, layout_y, layout_size, layout_step;
	unsigned int color_alpha;
	int slot;
	short index;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_PLAYERS || screen_width <= 0.0f ||
		screen_height <= 0.0f || alpha <= 0.0f || !medal_hud_on() || feed_covered(local_player_index))
	{
		return;
	}
	slot = medal_lines_find(&medal_hud[local_player_index], time, number);
	if (slot < 0)
		return;
	/* (the HUD's coordinates are its view's; the overlay's, the picture's
	480 lines, its middle the layout's) */
	scale = 480.0f / screen_height;
	layout_x = 320.0f + ((real)render.camera.viewport_bounds.x0 + x - screen_width * 0.5f) * scale;
	layout_y = ((real)render.camera.viewport_bounds.y0 + top) * scale;
	layout_size = size * scale;
	layout_step = (size + gap) * scale;
	color_alpha = (unsigned int)(PIN(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
	ui_overlay_layer(0);
	for (index = 0; index < medal_hud[local_player_index].lines[slot].count; index++)
	{
		ui_overlay_medal(medal_hud[local_player_index].lines[slot].icons[index], layout_x + index * layout_step,
			layout_y, layout_size, 0xFFFFFF00U | color_alpha);
	}
}

void medal_hud_clear(
	void)
{
	csmemset(medal_hud, 0, sizeof(medal_hud));
}

#endif
