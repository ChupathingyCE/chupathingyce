/*
LATENCY_METER.C

The ping meter of a network game, after Lord BirchWood's latency meter in
his Vita port: "Ping 45 ms" with four bars at the top right of the screen,
green below 80 ms, yellow below 150, red from there, while a network game
(multiplayer or co-op) is played. display.ping_meter turns it on; it is off
by default, as the Xbox had none.

What it shows is the scoreboard's Ping column (game_engine.c): a player's
round trip as the host measures it (network_distributed.c's
distributed_player_ping). Nothing is sent for it.

- A client shows its own player's, from the table of pings the host sends
  every two seconds. Before one comes (or with a host that sends none), it
  shows the round trip it times itself from the host's echo of its ticks
  (distributed_own_round_trip_ticks), else "Ping -- ms". When the host has
  sent nothing for two seconds, it shows "Connection problem" instead, before
  the game's own warning (network_client_manager.c, at five seconds).
- The host shows "Max ping": its slowest client's. Alone, it shows nothing.

Never drawn over the menus, the game's end, or a cutscene (but a connection
problem shows over one).
*/

#include "cseries.h"
#include "cutscene/cinematics.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/interface.h"
#include "interface/ui_widget.h"
#include "math/integer_math.h"
#include "memory/data.h"
#include "networking/network_client_manager.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"

#include "network_distributed.h"
#include "latency_meter.h"

/* ---------- constants */

enum
{
	/* the colors' bounds (milliseconds): green below the first, yellow
	below the second, red from it; four bars below the first, three below
	the second, two below the third, then one */
	LATENCY_GOOD_MILLISECONDS = 80,
	LATENCY_FAIR_MILLISECONDS = 150,
	LATENCY_POOR_MILLISECONDS = 250,
	LATENCY_BARS = 4,
	/* the host silent this long is a connection problem ... */
	LATENCY_SILENT_MILLISECONDS = 2000,
	/* ... but a frame of this machine's own this long starts its count over */
	LATENCY_STALL_MILLISECONDS = 500,
	/* the largest number shown */
	LATENCY_MAXIMUM_MILLISECONDS = 9999,
	/* the meter's place: its right edge from the window's, its bars' width
	and gap, and the gap before the text (all in window pixels) */
	LATENCY_RIGHT_MARGIN = 12,
	LATENCY_BAR_WIDTH = 4,
	LATENCY_BAR_GAP = 1,
	LATENCY_TEXT_GAP = 4,
};

/* draw_string's justifications (as ui_widget.c and hud_messaging.c have them) */
enum
{
	_text_justification_left,
	_text_justification_right,
	_text_justification_center,
};

/* its top: this share of the screen's height down, under the shields and
health at the top right */
#define LATENCY_TOP_FRACTION 0.15f

/* ---------- prototypes */

int config_boolean(char const *name);
unsigned long config_changes(void);
unsigned long system_milliseconds(void);

/* ---------- private code */

static boolean latency_meter_enabled(
	void)
{
	static unsigned long changes_seen;
	static boolean enabled;
	static boolean read;
	unsigned long changes = config_changes();

	if (!read || changes != changes_seen)
	{
		enabled = config_boolean("display.ping_meter") ? TRUE : FALSE;
		changes_seen = changes;
		read = TRUE;
	}
	return enabled;
}

/* the bars a round trip earns, and their color */
static short latency_bars(
	long milliseconds,
	real_argb_color *color)
{
	short bars;

	color->alpha = 1.0f;
	if (milliseconds < LATENCY_GOOD_MILLISECONDS)
	{
		bars = 4;
		color->red = 0.35f;
		color->green = 1.0f;
		color->blue = 0.35f;
	}
	else if (milliseconds < LATENCY_FAIR_MILLISECONDS)
	{
		bars = 3;
		color->red = 1.0f;
		color->green = 0.85f;
		color->blue = 0.25f;
	}
	else
	{
		bars = milliseconds < LATENCY_POOR_MILLISECONDS ? 2 : 1;
		color->red = 1.0f;
		color->green = 0.3f;
		color->blue = 0.3f;
	}
	return bars;
}

static pixel32 latency_pixel(
	real_argb_color const *color)
{
	return ((pixel32)(PIN(color->alpha, 0.0f, 1.0f) * 255.0f + 0.5f) << 24) |
		((pixel32)(PIN(color->red, 0.0f, 1.0f) * 255.0f + 0.5f) << 16) |
		((pixel32)(PIN(color->green, 0.0f, 1.0f) * 255.0f + 0.5f) << 8) |
		(pixel32)(PIN(color->blue, 0.0f, 1.0f) * 255.0f + 0.5f);
}

static boolean latency_meter_shown(
	void)
{
	short connection;

	/* (the setting first: off, nothing else is asked each frame) */
	if (!latency_meter_enabled())
		return FALSE;
	connection = game_connection();
	return (connection == _game_connection_network_client || connection == _game_connection_network_server) &&
		game_in_progress() && !main_menu_is_active() && !game_engine_showing_postgame();
}

/* (a client) its own player's ping, NONE when it has none */
static long latency_own_ping(
	void)
{
	short local_player_index;
	real round_trip_ticks;

	for (local_player_index = local_player_get_next(NONE);
		local_player_index != NONE;
		local_player_index = local_player_get_next(local_player_index))
	{
		long player_index = local_player_get_player_index(local_player_index);

		if (player_index != NONE)
		{
			long ping = distributed_player_ping((short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index));

			if (ping != NONE)
				return ping;
		}
	}
	round_trip_ticks = distributed_own_round_trip_ticks();
	if (round_trip_ticks > 0.0f)
		return (long)(round_trip_ticks * 1000.0f / TICKS_PER_SECOND + 0.5f);
	return NONE;
}

/* (a client) when this machine began listening without a break: its first
frame with the meter up, or its first after a frame of its own that took
longer than LATENCY_STALL_MILLISECONDS (a level loading, a BSP switching).
Its own wait is not the host's silence: what came meanwhile is read in the
frames after. Only the first of such frames in a row starts it over, so a
machine slow all the time still shows a host gone quiet */
static struct
{
	boolean listening;
	boolean stalled;
	unsigned long since;
	unsigned long last_frame;
} latency_listening;

/* (a client) how long the host has been silent: its connection's stream and
its game messages both quiet, and this machine listening all that time;
NONE when it isn't playing in a host's game */
static long latency_host_silent_milliseconds(
	void)
{
	unsigned long now = system_milliseconds();
	long silent = network_game_client_silent_milliseconds();
	long messages = distributed_host_silent_milliseconds();
	boolean stalled = latency_listening.listening && now - latency_listening.last_frame > LATENCY_STALL_MILLISECONDS;

	if (!latency_listening.listening || (stalled && !latency_listening.stalled))
		latency_listening.since = now;
	latency_listening.stalled = stalled;
	latency_listening.listening = TRUE;
	latency_listening.last_frame = now;
	if (silent != NONE && messages != NONE)
		silent = MIN(silent, messages);
	if (silent != NONE)
		silent = MIN(silent, (long)MIN(now - latency_listening.since, 3600000UL));
	return silent;
}

/* (the host) its slowest client's ping, NONE with no client's known */
static long latency_slowest_client_ping(
	void)
{
	long slowest = NONE;
	short player_index;

	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		/* (its own players' are 0, as on the scoreboard: not a client's) */
		long ping = distributed_player_machine(player_index) != NONE ? distributed_player_ping(player_index) : NONE;

		if (ping != NONE && ping > slowest)
			slowest = ping;
	}
	return slowest;
}

/* ---------- public code */

void latency_meter_draw(
	void)
{
	long font_index;
	struct font_header *font;
	short line_height;
	short bar_height;
	short top;
	short right;
	short bars = 0;
	short index;
	long ping;
	long silent;
	wchar_t text[32];
	real_argb_color color;
	rectangle2d bounds;

	if (!latency_meter_shown())
	{
		latency_listening.listening = FALSE;
		return;
	}
	silent = game_connection() == _game_connection_network_client ? latency_host_silent_milliseconds() : NONE;
	if (cinematic_in_progress() && silent < LATENCY_SILENT_MILLISECONDS)
		return;
	font_index = interface_get_tag_index(_interface_font_terminal);
	if (font_index == NONE)
		return;
	font = font_definition_get(font_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	if (line_height <= 0)
		return;
	if (game_connection() == _game_connection_network_server)
	{
		ping = latency_slowest_client_ping();
		if (ping == NONE)
			return;
		bars = latency_bars(ping, &color);
		usnprintf(text, NUMBEROF(text), L"Max ping %ld ms", MIN(ping, (long)LATENCY_MAXIMUM_MILLISECONDS));
	}
	else if (silent >= LATENCY_SILENT_MILLISECONDS)
	{
		/* (pulsing, with the bars dark) */
		color.alpha = system_milliseconds() % 1000 < 500 ? 1.0f : 0.45f;
		color.red = 1.0f;
		color.green = 0.3f;
		color.blue = 0.3f;
		usnprintf(text, NUMBEROF(text), L"Connection problem");
	}
	else
	{
		ping = latency_own_ping();
		if (ping == NONE)
		{
			color.alpha = 1.0f;
			color.red = color.green = color.blue = 0.6f;
			usnprintf(text, NUMBEROF(text), L"Ping -- ms");
		}
		else
		{
			bars = latency_bars(ping, &color);
			usnprintf(text, NUMBEROF(text), L"Ping %ld ms", MIN(ping, (long)LATENCY_MAXIMUM_MILLISECONDS));
		}
	}
	text[NUMBEROF(text) - 1] = 0;

	bounds = render.camera.window_bounds;
	offset_rectangle2d(&bounds, (short)-render.camera.viewport_bounds.x0, (short)-render.camera.viewport_bounds.y0);
	top = (short)(bounds.y0 + (bounds.y1 - bounds.y0) * LATENCY_TOP_FRACTION);
	right = (short)(bounds.x1 - LATENCY_RIGHT_MARGIN);
	/* the bars, rising left to right, as many lit as earned */
	bar_height = (short)MAX(line_height - 4, 4);
	for (index = 0; index < LATENCY_BARS; index++)
	{
		rectangle2d bar;
		real_argb_color bar_color = color;
		short height = (short)(bar_height * (index + 1) / LATENCY_BARS);

		if (index >= bars)
		{
			bar_color.red = bar_color.green = bar_color.blue = 0.25f;
			bar_color.alpha = 0.6f * color.alpha;
		}
		bar.x0 = (short)(right - (LATENCY_BARS - index) * (LATENCY_BAR_WIDTH + LATENCY_BAR_GAP) + LATENCY_BAR_GAP);
		bar.x1 = (short)(bar.x0 + LATENCY_BAR_WIDTH);
		bar.y1 = (short)(top + line_height - 2);
		bar.y0 = (short)(bar.y1 - height);
		draw_quad(&bar, latency_pixel(&bar_color));
	}
	/* the text, right-justified to their left */
	bounds.x1 = (short)(right - LATENCY_BARS * (LATENCY_BAR_WIDTH + LATENCY_BAR_GAP) - LATENCY_TEXT_GAP);
	bounds.y0 = top;
	bounds.y1 = (short)(top + line_height);
	draw_string_set_draw_mode(font_index, NONE, _text_justification_right, 0, &color);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, text);
}
