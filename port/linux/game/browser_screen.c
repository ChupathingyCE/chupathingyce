/*
BROWSER_SCREEN.C

The in-game server browser of the new networking (configure.py
--new-networking): every game on the game list (port/linux/src/browser.c),
on a screen of its own over the menus, as the game's virtual keyboard is
(interface/virtual_keyboard.c): drawn and driven by code, not a widget of
the user interface's tags.

X on the System Link screen opens it (ui_widget.c; the list screen marks
when it is up, ui_widget_game_data_input_functions.c). Up and down pick a
game, left and right turn the page, A joins it through its invite, as a web
page's Join or an invite link would, and B goes back. Once the invite's host
answers, its game shows in the System Link list through the tunnel, to be
picked there as any.
*/

#ifdef HALO_NEW_NETWORKING

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "cutscene/cinematics.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "interface/interface.h"
#include "rasterizer/rasterizer.h"
#include "text/draw_string.h"
#include "bitmaps/bitmap_group.h"
#include "interface/ui_widget.h"
#include "tag_files/tag_groups.h"
#include "../src/browser.h"

/* ---------- constants */

enum
{
	/* (event_manager.c's event types, which it keeps to itself) */
	BROWSER_EVENT_LEFT_STICK = 1,
	BROWSER_EVENT_BUTTON = 3,

	/* the System Link screen was up this recently: X opens the browser */
	LIST_SHOWN_WINDOW = 500,

	ROWS_PER_PAGE = 10,
	ROW_HEIGHT = 26,
	LIST_TOP = 112,
	STATUS_DURATION = 6000,
};

/* the game's engines, short (as players say them) to fit the column */
static char const *const engine_names[] =
{
	"", "CTF", "Slayer", "Oddball", "King", "Race",
};

/* the multiplayer maps' names in the menus */
static char const *const map_names[][2] =
{
	{ "beavercreek", "Battle Creek" }, { "bloodgulch", "Blood Gulch" }, { "boardingaction", "Boarding Action" },
	{ "carousel", "Derelict" }, { "chillout", "Chill Out" }, { "damnation", "Damnation" },
	{ "hangemhigh", "Hang 'Em High" }, { "longest", "Longest" }, { "prisoner", "Prisoner" },
	{ "putput", "Chiron TL-34" }, { "ratrace", "Rat Race" }, { "sidewinder", "Sidewinder" }, { "wizard", "Wizard" },
};

/* ---------- globals */

static struct
{
	boolean active;
	unsigned long list_shown_time;
	short selected;
	short count;
	struct browser_game games[BROWSER_MAXIMUM_GAMES];
	char status[96];
	unsigned long status_time;
} browser_screen;

/* ---------- private code */

static void set_status(
	char const *text)
{
	csstrncpy(browser_screen.status, text, sizeof(browser_screen.status) - 1);
	browser_screen.status[sizeof(browser_screen.status) - 1] = 0;
	browser_screen.status_time = system_milliseconds();
}

static char const *map_display_name(
	char const *path)
{
	char const *base = path;
	char const *cursor;
	long index;

	for (cursor = path; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			base = cursor + 1;
	}
	for (index = 0; index < NUMBEROF(map_names); index++)
	{
		if (!csstrcmp(base, map_names[index][0]))
			return map_names[index][1];
	}
	return base;
}

/* ASCII into the game's wide strings */
static void widen(
	wchar_t *out,
	long size,
	char const *text)
{
	long index;

	for (index = 0; index < size - 1 && text[index]; index++)
		out[index] = (wchar_t)(unsigned char)text[index];
	out[index] = 0;
}

static void draw_text(
	short x0,
	short y0,
	short x1,
	short y1,
	short justification,
	real_argb_color const *color,
	wchar_t const *text)
{
	rectangle2d bounds;

	bounds.x0 = x0;
	bounds.y0 = y0;
	bounds.x1 = x1;
	bounds.y1 = y1;
	draw_string_set_draw_mode(interface_get_tag_index(_interface_font_terminal), NONE, justification, 0, color);
	rasterizer_draw_unicode_string(&bounds, &bounds, NULL, 0, text);
}

static void draw_ascii(
	short x0,
	short y0,
	short x1,
	short y1,
	short justification,
	real_argb_color const *color,
	char const *text)
{
	wchar_t wide[128];

	widen(wide, NUMBEROF(wide), text);
	draw_text(x0, y0, x1, y1, justification, color, wide);
}

static void join_selected(
	void)
{
	struct browser_game const *game;

	if (browser_screen.selected < 0 || browser_screen.selected >= browser_screen.count)
		return;
	game = &browser_screen.games[browser_screen.selected];
	if (!game->open)
	{
		set_status("That game is not accepting players.");
		return;
	}
	if (browser_join(game->invite))
	{
		/* (its game shows in System Link once its host answers) */
		browser_screen.active = FALSE;
	}
	else
	{
		set_status("Internet play is off (network.online in config.toml).");
	}
}

/* ---------- public code */

void browser_screen_list_shown(
	void)
{
	browser_screen.list_shown_time = system_milliseconds();
}

boolean browser_screen_active(
	void)
{
	return browser_screen.active;
}

boolean browser_screen_open_from_event(
	struct event_record const *event)
{
	if (event->type != BROWSER_EVENT_BUTTON ||
		event->data.button.index != _gamepad_analog_button_x ||
		system_milliseconds() - browser_screen.list_shown_time > LIST_SHOWN_WINDOW)
	{
		return FALSE;
	}
	browser_screen.active = TRUE;
	browser_screen.selected = 0;
	browser_screen.status[0] = 0;
	browser_screen.count = (short)browser_get_games(browser_screen.games, BROWSER_MAXIMUM_GAMES);
	return TRUE;
}

void browser_screen_process(
	void)
{
	struct event_record event;
	short move = 0;

	browser_screen.count = (short)browser_get_games(browser_screen.games, BROWSER_MAXIMUM_GAMES);
	while (browser_screen.active && get_next_event(&event, NONE))
	{
		if (event.type == BROWSER_EVENT_LEFT_STICK)
		{
			if (event.data.stick.y == SHORT_MAX)
				move = -1;
			else if (event.data.stick.y == SHORT_MIN)
				move = 1;
			else if (event.data.stick.x == SHORT_MIN)
				move = -ROWS_PER_PAGE;
			else if (event.data.stick.x == SHORT_MAX)
				move = ROWS_PER_PAGE;
		}
		else if (event.type == BROWSER_EVENT_BUTTON)
		{
			switch (event.data.button.index)
			{
			case _gamepad_binary_button_dpad_up: move = -1; break;
			case _gamepad_binary_button_dpad_down: move = 1; break;
			case _gamepad_binary_button_dpad_left: move = -ROWS_PER_PAGE; break;
			case _gamepad_binary_button_dpad_right: move = ROWS_PER_PAGE; break;
			case _gamepad_analog_button_a: join_selected(); break;
			case _gamepad_analog_button_b:
			case _gamepad_binary_button_back:
				browser_screen.active = FALSE;
				break;
			default: break;
			}
		}
		if (move)
		{
			browser_screen.selected = (short)PIN(browser_screen.selected + move, 0,
				MAX(0, browser_screen.count - 1));
			move = 0;
		}
	}
	if (browser_screen.selected >= browser_screen.count)
		browser_screen.selected = (short)MAX(0, browser_screen.count - 1);
	/* (the widgets behind take nothing while the browser is up) */
	event_manager_flush();
}

/* the System Link screen's footer has no word of the browser (it is the
user interface's tags): its free corner says so */
boolean ui_widget_text_style(char const *name, long *font_index, real_argb_color *color, rectangle2d *bounds);

/* the System Link screen's footer has no word of the browser (it is the
user interface's tags): an X button's prompt joins its others, left of
"[Y]= CREATE GAME" (its words' font, color and line as they are drawn; the X
button's icon from the shell's button bitmaps, as the Y button's) */
void browser_screen_render_hint(
	void)
{
	static wchar_t const words[] = L"=ALL GAMES";
	long font_index;
	real_argb_color color;
	rectangle2d text;
	rectangle2d measured;
	rectangle2d cursor;
	rectangle2d bounds;
	long bitmap_index;
	struct bitmap_data *bitmap;
	short icon_width, icon_height, words_width, right, middle;

	if (browser_screen.active || system_milliseconds() - browser_screen.list_shown_time > LIST_SHOWN_WINDOW)
		return;
	/* (not drawn yet this time: the next frame) */
	if (!ui_widget_text_style(
		"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\server_list\\create_game_button",
		&font_index, &color, &text))
	{
		return;
	}
	bitmap_index = tag_loaded('bitm', "ui\\shell\\bitmaps\\x_butn");
	bitmap = bitmap_index != NONE ? bitmap_group_get_bitmap_from_sequence(bitmap_index, 0, 0) : NULL;
	if (!bitmap)
		return;
	/* (the button's bitmap has a margin around the button: a box 1.8 times
	the words' height shows the button as big as the footer's others) */
	icon_height = (short)(9 * (text.y1 - text.y0) / 5);
	icon_width = bitmap->height ? (short)(icon_height * bitmap->width / bitmap->height) : icon_height;

	draw_string_set_draw_mode(font_index, NONE, 0, 0, &color);
	bounds.x0 = 0;
	bounds.y0 = 0;
	bounds.x1 = 640;
	bounds.y1 = 480;
	/* (it writes the cursor's bounds too: it takes no NULL there) */
	draw_unicode_string_compute_bounds(&bounds, words, &measured, &cursor);
	/* (a little slack: the measure falls a few short of the drawing) */
	words_width = (short)(measured.x1 - measured.x0 + 6);

	/* the Y button shows about 6 left of its words' bounds; this prompt
	ends 20 before it, as far as the footer's prompts are apart */
	right = (short)(text.x0 - 6 - 20);
	middle = (short)((text.y0 + text.y1) / 2 + 4);

	bounds.x1 = right;
	bounds.x0 = (short)(right - words_width);
	bounds.y0 = text.y0;
	bounds.y1 = text.y1;
	rasterizer_draw_unicode_string(&bounds, &bounds, NULL, 0, words);

	/* (its margin under the words' start: the button against the "=") */
	bounds.x1 = (short)(right - words_width + icon_width / 4);
	bounds.x0 = (short)(bounds.x1 - icon_width);
	bounds.y0 = (short)(middle - icon_height / 2);
	bounds.y1 = (short)(bounds.y0 + icon_height);
	draw_bitmap_in_rect(bitmap, &bounds, NULL, NULL, (pixel32)((long)(color.alpha * 255.0f) << 24) | 0x00FFFFFF,
		NULL, FALSE);
}

void browser_screen_render(
	void)
{
	real_argb_color title_color = { 1.0f, 0.55f, 0.75f, 1.0f };
	real_argb_color text_color = { 1.0f, 0.88f, 0.9f, 0.95f };
	real_argb_color dim_color = { 1.0f, 0.55f, 0.62f, 0.72f };
	real_argb_color open_color = { 1.0f, 0.45f, 0.85f, 0.5f };
	real_argb_color closed_color = { 1.0f, 0.85f, 0.5f, 0.45f };
	rectangle2d bounds;
	short page_first;
	short row;
	char text[128];

	/* the screen, the widescreen margins too */
	bounds.x0 = (short)(-(halo_screen_width() - 640) / 2);
	bounds.x1 = (short)(640 + (halo_screen_width() - 640) / 2);
	bounds.y0 = 0;
	bounds.y1 = 480;
	/* (nearly opaque: the menu behind is only a hint) */
	draw_quad(&bounds, 0xF4060C18);

	draw_ascii(48, 40, 592, 72, 0, &title_color, "INTERNET GAMES");
	snprintf(text, sizeof(text), "%d game%s on %s", browser_screen.count, browser_screen.count == 1 ? "" : "s",
		"the game list");
	draw_ascii(48, 72, 592, 92, 0, &dim_color, text);

	/* the column heads */
	draw_ascii(48, LIST_TOP - 22, 240, LIST_TOP - 4, 0, &dim_color, "GAME");
	draw_ascii(240, LIST_TOP - 22, 360, LIST_TOP - 4, 0, &dim_color, "MAP");
	draw_ascii(360, LIST_TOP - 22, 500, LIST_TOP - 4, 0, &dim_color, "TYPE");
	draw_ascii(500, LIST_TOP - 22, 592, LIST_TOP - 4, 1, &dim_color, "PLAYERS");

	if (!browser_screen.count)
	{
		draw_ascii(48, LIST_TOP + 20, 592, LIST_TOP + 44, 0, &text_color,
			"No games are hosted on the list right now.");
	}
	page_first = (short)(browser_screen.selected - browser_screen.selected % ROWS_PER_PAGE);
	for (row = 0; row < ROWS_PER_PAGE && page_first + row < browser_screen.count; row++)
	{
		struct browser_game const *game = &browser_screen.games[page_first + row];
		short y = (short)(LIST_TOP + row * ROW_HEIGHT);
		wchar_t name[BROWSER_NAME_LENGTH + 1];
		long index;

		if (page_first + row == browser_screen.selected)
		{
			rectangle2d highlight;

			highlight.x0 = 40;
			highlight.x1 = 600;
			highlight.y0 = (short)(y - 2);
			highlight.y1 = (short)(y + ROW_HEIGHT - 4);
			draw_quad(&highlight, 0x803D8BFF);
		}
		for (index = 0; index < BROWSER_NAME_LENGTH; index++)
			name[index] = (wchar_t)game->name[index];
		name[BROWSER_NAME_LENGTH] = 0;
		draw_text(48, y, 236, (short)(y + ROW_HEIGHT), 0, &text_color, name);
		draw_ascii(240, y, 356, (short)(y + ROW_HEIGHT), 0, &text_color, map_display_name(game->map));
		snprintf(text, sizeof(text), "%s%s", game->teams ? "Team " : "",
			game->engine >= 0 && game->engine < NUMBEROF(engine_names) ? engine_names[game->engine] : "Game");
		draw_ascii(360, y, 496, (short)(y + ROW_HEIGHT), 0, &text_color, text);
		snprintf(text, sizeof(text), "%d/%d", game->players, game->maximum_players);
		draw_ascii(500, y, 592, (short)(y + ROW_HEIGHT), 1, game->open ? &open_color : &closed_color, text);
	}

	if (browser_screen.count > ROWS_PER_PAGE)
	{
		snprintf(text, sizeof(text), "Page %d of %d", browser_screen.selected / ROWS_PER_PAGE + 1,
			(browser_screen.count + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE);
		draw_ascii(48, 392, 592, 412, 1, &dim_color, text);
	}
	if (browser_screen.status[0] && system_milliseconds() - browser_screen.status_time < STATUS_DURATION)
		draw_ascii(48, 392, 592, 412, 0, &closed_color, browser_screen.status);
	draw_ascii(48, 424, 592, 448, 2, &dim_color,
		"A = Join     B = Back     Left/Right = Page");
}

#endif
