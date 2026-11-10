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
tied to by its time and number. Medals no line took by the time the HUD is
drawn (medal_hud_flush) get a line of their own, the icons alone.

Hidden while a menu is over that player's view (the overlay draws over the
game's menus). With display.medals off nothing is kept and the kill feed is
retail's. The builds without the icons (HALO_MEDAL_ICONS,
halo_console_features.h: the original Xbox, the builds without the overlay)
show each medal as a line of the HUD's messages instead (game_stats.c).
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"

#ifdef HALO_MEDAL_ICONS

#include "game/game.h"
#include "interface/hud_messaging.h"
#include "interface/ui_widget.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "../src/medal_icons.h"
#include "../src/ui_overlay.h"
#include "medal_hud.h"

unsigned long system_milliseconds(void);
int config_boolean(char const *name);
char const *config_string(char const *name);

/* ---------- constants */

enum
{
	/* the medals one line holds at most (medal_rules.h's most for a kill
	is more, but a line has room for these) */
	MAXIMUM_PER_LINE = 6,
	/* the lines with medals kept for each player (more than a player's HUD
	shows) */
	MAXIMUM_LINES = 8,
	/* the local players (MAXIMUM_NUMBER_OF_LOCAL_PLAYERS's) */
	MAXIMUM_PLAYERS = 4,
	/* debug.medal_test's line comes again this often (milliseconds) */
	TEST_INTERVAL = 4000,
};

/* ---------- globals */

static struct
{
	/* the medals waiting for the kill's line, and the tick they came in */
	short pending[MAXIMUM_PER_LINE];
	short pending_count;
	long pending_time;
	/* the lines that have medals: a message's time and number, its medals */
	struct
	{
		long time;
		short number;
		short count;
		short icons[MAXIMUM_PER_LINE];
	} lines[MAXIMUM_LINES];
	short next_line;
} medal_hud[MAXIMUM_PLAYERS];

static unsigned long medal_hud_test_time;

/* ---------- private code */

static boolean medal_hud_on(
	void)
{
	return config_boolean("display.medals") && ui_overlay_medals_available();
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
	if (medal_hud[local_player_index].pending_time != now)
		medal_hud[local_player_index].pending_count = 0;
	medal_hud[local_player_index].pending_time = now;
	if (medal_hud[local_player_index].pending_count < MAXIMUM_PER_LINE)
		medal_hud[local_player_index].pending[medal_hud[local_player_index].pending_count++] = (short)icon;
}

void medal_hud_printed(
	short local_player_index,
	long time,
	short number)
{
	short slot, index;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_PLAYERS ||
		!medal_hud[local_player_index].pending_count || medal_hud[local_player_index].pending_time != time)
	{
		return;
	}
	slot = medal_hud[local_player_index].next_line;
	medal_hud[local_player_index].next_line = (short)((slot + 1) % MAXIMUM_LINES);
	medal_hud[local_player_index].lines[slot].time = time;
	medal_hud[local_player_index].lines[slot].number = number;
	medal_hud[local_player_index].lines[slot].count = medal_hud[local_player_index].pending_count;
	for (index = 0; index < medal_hud[local_player_index].pending_count; index++)
		medal_hud[local_player_index].lines[slot].icons[index] = medal_hud[local_player_index].pending[index];
	medal_hud[local_player_index].pending_count = 0;
}

void medal_hud_flush(
	short local_player_index)
{
	if (local_player_index < 0 || local_player_index >= MAXIMUM_PLAYERS)
		return;
	if (config_boolean("display.medals"))
		test_medals(local_player_index);
	/* (medals no line took: a line of their own) */
	if (medal_hud[local_player_index].pending_count)
		hud_print_message(local_player_index, L"");
	medal_hud[local_player_index].pending_count = 0;
}

short medal_hud_line_count(
	short local_player_index,
	long time,
	short number)
{
	short slot;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_PLAYERS || time == NONE)
		return 0;
	for (slot = 0; slot < MAXIMUM_LINES; slot++)
	{
		if (medal_hud[local_player_index].lines[slot].count && medal_hud[local_player_index].lines[slot].time == time &&
			medal_hud[local_player_index].lines[slot].number == number)
		{
			return medal_hud[local_player_index].lines[slot].count;
		}
	}
	return 0;
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
	short slot, index;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_PLAYERS || screen_width <= 0.0f ||
		screen_height <= 0.0f || alpha <= 0.0f || !medal_hud_on() ||
		ui_widgets_active_for_local_player(local_player_index))
	{
		return;
	}
	for (slot = 0; slot < MAXIMUM_LINES; slot++)
	{
		if (medal_hud[local_player_index].lines[slot].count && medal_hud[local_player_index].lines[slot].time == time &&
			medal_hud[local_player_index].lines[slot].number == number)
		{
			break;
		}
	}
	if (slot == MAXIMUM_LINES)
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
