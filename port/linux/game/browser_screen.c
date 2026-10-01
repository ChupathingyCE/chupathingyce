/*
BROWSER_SCREEN.C

The in-game server browser (configure.py
--game-browser): every game on the game list (port/linux/src/browser.c),
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

#ifdef HALO_GAME_BROWSER

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
#include "../src/ui_overlay.h"

/* ---------- constants */

enum
{
	/* (event_manager.c's event types, which it keeps to itself) */
	BROWSER_EVENT_LEFT_STICK = 1,
	BROWSER_EVENT_BUTTON = 3,

	/* the System Link screen was up this recently: X opens the browser */
	LIST_SHOWN_WINDOW = 500,

	ROWS_PER_PAGE = 9,
	ROW_HEIGHT = 26,
	LIST_TOP = 112,
	STATUS_DURATION = 6000,
};

/* ui_widget.c owns the same private enum (virtual_keyboard.c keeps a copy) */
enum
{
	_ui_audio_feedback_none,
	_ui_audio_feedback_cursor,
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

/* the list's rows' icons, 11 by 11: a globe for the game list's games, a
network for the local network's */
#define ROW_ICON_SIZE 11

static char const *const globe_icon[ROW_ICON_SIZE] =
{
	"...#####...",
	".##.#.#.##.",
	".#..#.#..#.",
	"#...#.#...#",
	"###########",
	"#...#.#...#",
	"###########",
	"#...#.#...#",
	".#..#.#..#.",
	".##.#.#.##.",
	"...#####...",
};

static char const *const network_icon[ROW_ICON_SIZE] =
{
	"....###....",
	"....#.#....",
	"....###....",
	".....#.....",
	".....#.....",
	".#########.",
	".#.......#.",
	"###.....###",
	"#.#.....#.#",
	"###.....###",
	"...........",
};

/* the list's orders (LT and RT, or the shoulders, step through them) */
enum
{
	SORT_PLAYERS,
	SORT_NAME,
	SORT_MAP,
	SORT_TYPE,

	NUMBER_OF_SORTS
};

/* ---------- globals */

static struct
{
	boolean list_row_listed[9];
	long list_row_count;
	boolean active;
	unsigned long list_shown_time;
	short selected;
	short count;
	struct browser_game games[BROWSER_MAXIMUM_GAMES];
	char status[96];
	unsigned long status_time;
	short sort;
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

static long compare_names(
	unsigned short const *a,
	unsigned short const *b)
{
	long index;

	for (index = 0; index < BROWSER_NAME_LENGTH; index++)
	{
		unsigned short x = a[index] >= 'a' && a[index] <= 'z' ? (unsigned short)(a[index] - 32) : a[index];
		unsigned short y = b[index] >= 'a' && b[index] <= 'z' ? (unsigned short)(b[index] - 32) : b[index];

		if (x != y || !x)
			return (long)x - (long)y;
	}
	return 0;
}

static long compare_games(
	struct browser_game const *a,
	struct browser_game const *b)
{
	long order;

	/* (closed games last, whatever the order) */
	if (a->open != b->open)
		return a->open ? -1 : 1;
	switch (browser_screen.sort)
	{
	case SORT_NAME: order = compare_names(a->name, b->name); break;
	case SORT_MAP: order = strcmp(map_display_name(a->map), map_display_name(b->map)); break;
	case SORT_TYPE: order = (long)a->engine * 2 + a->teams - ((long)b->engine * 2 + b->teams); break;
	default: order = (long)b->players - (long)a->players; break;
	}
	return order ? order : compare_names(a->name, b->name);
}

/* the game list's games, in the screen's order; the selection stays on its
game */
static void fetch_games(
	void)
{
	char invite[BROWSER_INVITE_LENGTH + 1];
	short index, other;

	invite[0] = 0;
	if (browser_screen.selected >= 0 && browser_screen.selected < browser_screen.count)
		csstrncpy(invite, browser_screen.games[browser_screen.selected].invite, sizeof(invite) - 1);
	invite[sizeof(invite) - 1] = 0;
	browser_screen.count = (short)browser_get_games(browser_screen.games, BROWSER_MAXIMUM_GAMES);
	/* (insertion: a few dozen games) */
	for (index = 1; index < browser_screen.count; index++)
	{
		struct browser_game game = browser_screen.games[index];

		for (other = index; other > 0 && compare_games(&browser_screen.games[other - 1], &game) > 0; other--)
			browser_screen.games[other] = browser_screen.games[other - 1];
		browser_screen.games[other] = game;
	}
	for (index = 0; invite[0] && index < browser_screen.count; index++)
	{
		if (!strcmp(browser_screen.games[index].invite, invite))
			browser_screen.selected = index;
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

boolean network_game_client_browser_turn_page(long delta);
void network_game_client_browser_page(long *page, long *page_count);

/* left or right on the System Link screen: its list's page of listed games
(network_client_manager.c) */
static boolean browser_screen_turn_page(
	struct event_record const *event)
{
	long delta = 0;

	if (system_milliseconds() - browser_screen.list_shown_time > LIST_SHOWN_WINDOW)
		return FALSE;
	if (event->type == BROWSER_EVENT_BUTTON)
	{
		if (event->data.button.index == _gamepad_binary_button_dpad_left)
			delta = -1;
		else if (event->data.button.index == _gamepad_binary_button_dpad_right)
			delta = 1;
	}
	else if (event->type == BROWSER_EVENT_LEFT_STICK)
	{
		if (event->data.stick.x == SHORT_MIN)
			delta = -1;
		else if (event->data.stick.x == SHORT_MAX)
			delta = 1;
	}
	if (!delta)
		return FALSE;
	if (network_game_client_browser_turn_page(delta))
		ui_play_audio_feedback_sound(_ui_audio_feedback_cursor);
	return TRUE;
}

boolean browser_screen_open_from_event(
	struct event_record const *event)
{
	if (browser_screen_turn_page(event))
		return TRUE;
	if (event->type != BROWSER_EVENT_BUTTON ||
		event->data.button.index != _gamepad_analog_button_x ||
		system_milliseconds() - browser_screen.list_shown_time > LIST_SHOWN_WINDOW)
	{
		return FALSE;
	}
	browser_screen.active = TRUE;
	browser_screen.selected = 0;
	browser_screen.status[0] = 0;
	fetch_games();
	return TRUE;
}

void browser_screen_process(
	void)
{
	struct event_record event;
	short move = 0;

	fetch_games();
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
			case _gamepad_binary_button_start:
				browser_open_profile();
				set_status("Opening your profile in the web browser");
				break;
			case _gamepad_analog_button_x:
				fetch_games();
				set_status("Refreshed");
				break;
			case _gamepad_analog_button_y:
				set_status("Filters are next");
				break;
			case _gamepad_analog_button_left_trigger:
			case _gamepad_analog_button_white:
				browser_screen.sort = (short)((browser_screen.sort + NUMBER_OF_SORTS - 1) % NUMBER_OF_SORTS);
				fetch_games();
				break;
			case _gamepad_analog_button_right_trigger:
			case _gamepad_analog_button_black:
				browser_screen.sort = (short)((browser_screen.sort + 1) % NUMBER_OF_SORTS);
				fetch_games();
				break;
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
boolean ui_widget_list_row_bounds(short row, rectangle2d *bounds);

/* the System Link list's rows as it laid them out: which are the game list's */
void browser_screen_list_rows(
	boolean const *listed,
	long count)
{
	long row;

	browser_screen.list_row_count = MIN(count, NUMBEROF(browser_screen.list_row_listed));
	for (row = 0; row < browser_screen.list_row_count; row++)
		browser_screen.list_row_listed[row] = listed[row];
}

static void draw_row_icon(
	char const *const *icon,
	short x0,
	short y0,
	pixel32 color)
{
	short row, column;

	for (row = 0; row < ROW_ICON_SIZE; row++)
	{
		for (column = 0; column < ROW_ICON_SIZE; column++)
		{
			if (icon[row][column] == '#')
			{
				rectangle2d pixel;

				pixel.x0 = (short)(x0 + column);
				pixel.x1 = (short)(pixel.x0 + 1);
				pixel.y0 = (short)(y0 + row);
				pixel.y1 = (short)(pixel.y0 + 1);
				draw_quad(&pixel, color);
			}
		}
	}
}

/* each row's icon, in a column at the list's right, as the list's words */
static void render_row_icons(
	real_argb_color const *color)
{
	pixel32 pixel_color = ((pixel32)(long)(color->alpha * 255.0f) << 24) |
		((pixel32)(long)(color->red * 255.0f) << 16) | ((pixel32)(long)(color->green * 255.0f) << 8) |
		(pixel32)(long)(color->blue * 255.0f);
	short row;

	for (row = 0; row < browser_screen.list_row_count; row++)
	{
		rectangle2d bounds;

		if (!ui_widget_list_row_bounds(row, &bounds))
			continue;
		draw_row_icon(browser_screen.list_row_listed[row] ? globe_icon : network_icon,
			(short)(262 - ROW_ICON_SIZE), (short)((bounds.y0 + bounds.y1) / 2 - ROW_ICON_SIZE / 2), pixel_color);
	}
}

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
	/* (the button's bitmap has a margin around the button: a box 1.62 times
	the words' height shows the button as big as the footer's others) */
	icon_height = (short)(81 * (text.y1 - text.y0) / 50);
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

	render_row_icons(&color);

	/* the list's pages of listed games, under the list, when there are more
	than one */
	{
		long page, page_count;

		network_game_client_browser_page(&page, &page_count);
		if (page_count > 1)
		{
			wchar_t indicator[48];
			char ascii[48];

			snprintf(ascii, sizeof(ascii), "%s PAGE %ld OF %ld %s", page > 0 ? "<" : " ", page + 1, page_count,
				page + 1 < page_count ? ">" : " ");
			widen(indicator, NUMBEROF(indicator), ascii);
			bounds.x0 = 40;
			bounds.x1 = 290;
			bounds.y0 = (short)(text.y0 - 38);
			bounds.y1 = (short)(text.y0 - 16);
			draw_string_set_draw_mode(font_index, NONE, 2, 0, &color);
			rasterizer_draw_unicode_string(&bounds, &bounds, NULL, 0, indicator);
		}
	}
}

/* ---------- drawing: the Online Games screen (the overlay, ui_overlay.c) */

/* the screen's colors (0xRRGGBBAA), as the mockups */
enum
{
	COLOR_BACKGROUND_TOP = 0x0B1830FF,
	COLOR_BACKGROUND_BOTTOM = 0x03070FFF,
	COLOR_RULE = 0x2A62C8FF,
	COLOR_TITLE = 0x3D8BFFFF,
	COLOR_PANEL = 0x081530F0,
	COLOR_PANEL_EDGE = 0x2F6DD0FF,
	COLOR_HEAD = 0x7FB0FFFF,
	COLOR_ROW_SELECTED = 0x2052B0FF,
	COLOR_ROW_RULE = 0x16294AFF,
	COLOR_TEXT = 0xE6EEFCFF,
	COLOR_DIM = 0x8FA6C8FF,
	COLOR_LABEL = 0x4AA3FFFF,
	COLOR_CLOSED = 0xF08A4BFF,
	COLOR_PROMPT = 0x4AA3FFFF,
};

/* the list's rows, columns and panels, in the 640x480 layout */
enum
{
	LIST_X = 37, LIST_Y = 72, LIST_WIDTH = 566, LIST_HEAD = 20, LIST_ROW = 22, LIST_FOOT = 20,
	DETAIL_Y = 318, DETAIL_HEIGHT = 117,
	COLUMN_NAME = 67, COLUMN_MAP = 275, COLUMN_TYPE = 385, COLUMN_PLAYERS = 531, COLUMN_PING = 596,
};

/* the map pictures (the menus' mp_map_grafix, in their order:
ui_widget_game_data_input_functions.c) and the game types' (game_type_grafix) */
static char const *const map_picture_order[] =
{
	"beavercreek", "sidewinder", "damnation", "ratrace", "prisoner", "hangemhigh", "chillout",
	"carousel", "boardingaction", "bloodgulch", "wizard", "putput", "longest",
};
static short const engine_picture[] = { 5, 0, 2, 3, 1, 4 };

static char const *const sort_names[NUMBER_OF_SORTS] = { "PLAYERS", "NAME", "MAP", "TYPE" };

static void utf8_name(
	unsigned short const *name,
	char *text,
	long size)
{
	long used = 0, index;

	for (index = 0; index < BROWSER_NAME_LENGTH && name[index] && used < size - 4; index++)
	{
		unsigned int character = name[index];

		if (character < 0x80)
			text[used++] = (char)character;
		else if (character < 0x800)
		{
			text[used++] = (char)(0xC0 | (character >> 6));
			text[used++] = (char)(0x80 | (character & 0x3F));
		}
		else
		{
			text[used++] = (char)(0xE0 | (character >> 12));
			text[used++] = (char)(0x80 | ((character >> 6) & 0x3F));
			text[used++] = (char)(0x80 | (character & 0x3F));
		}
	}
	text[used] = 0;
}

static char const *type_name(
	struct browser_game const *game,
	char *text,
	long size)
{
	char const *engine = game->engine >= 0 && game->engine < NUMBEROF(engine_names) && engine_names[game->engine][0] ?
		engine_names[game->engine] : "Game";

	/* (Capture the Flag is played in teams alone) */
	snprintf(text, (size_t)size, "%s%s", game->teams && game->engine != 1 ? "Team " : "", engine);
	return text;
}

/* a game's picture from the game's own bitmaps, in a cut-out of the overlay */
static void draw_picture(
	char const *tag,
	short frame,
	short art_width,
	short art_height,
	short x0,
	short y0,
	short x1,
	short y1)
{
	long bitmap_index = tag_loaded('bitm', tag);
	struct bitmap_data *bitmap = bitmap_index != NONE ? bitmap_group_get_bitmap_from_sequence(bitmap_index, 0, frame) : NULL;
	rectangle2d bounds;

	ui_overlay_cutout(x0, y0, (float)(x1 - x0), (float)(y1 - y0));
	bounds.x0 = x0;
	bounds.y0 = y0;
	bounds.x1 = x1;
	bounds.y1 = y1;
	draw_quad(&bounds, 0xFF0A1A33);
	if (bitmap)
	{
		/* (the picture fills the bitmap's top left; the rest is margin) */
		rectangle2d art;

		art.x0 = 0;
		art.y0 = 0;
		art.x1 = (short)MIN(art_width, bitmap->width);
		art.y1 = (short)MIN(art_height, bitmap->height);
		draw_bitmap_in_rect(bitmap, &bounds, &art, NULL, 0xFFFFFFFF, NULL, FALSE);
	}
}

static float prompt(
	int button,
	char const *words,
	float x)
{
	x += ui_overlay_button(button, 15.0f, x, 455.0f, 0xFFFFFFFF) + 3.0f;
	return x + ui_overlay_text(UI_FONT_BOLD, 12.0f, x, 456.5f, UI_ALIGN_LEFT, COLOR_PROMPT, words) + 20.0f;
}

static float prompt_width(
	int button,
	char const *words)
{
	return ui_overlay_button_width(button, 15.0f) + 3.0f + ui_overlay_text_width(UI_FONT_BOLD, 12.0f, words) + 20.0f;
}

void browser_screen_render(
	void)
{
	short page_first, page_count, row;
	long players = 0, index;
	char text[160], name[64];
	float x, width, margin = (float)((halo_screen_width() - 640) / 2 + 2);
	struct browser_game const *selected = browser_screen.count ? &browser_screen.games[browser_screen.selected] : NULL;

	if (!ui_overlay_available())
		return;
	for (index = 0; index < browser_screen.count; index++)
		players += browser_screen.games[index].players;

	/* the screen, its widescreen margins too */
	ui_overlay_gradient(-margin, 0, 640 + 2 * margin, 480, 0, COLOR_BACKGROUND_TOP, COLOR_BACKGROUND_BOTTOM);
	ui_overlay_gradient(-margin, 0, 640 + 2 * margin, 62, 0, 0x0A1A36FF, 0x050C1AFF);
	ui_overlay_rect(-margin, 61.5f, 640 + 2 * margin, 1.0f, 0, COLOR_RULE);
	ui_overlay_text(UI_FONT_BOLD, 30.0f, 37, 17, UI_ALIGN_LEFT, COLOR_TITLE, "ONLINE GAMES");

	/* servers, players and the list it comes from, at the right */
	x = 603;
	x -= ui_overlay_text(UI_FONT_BOLD, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_TEXT, "HALO.MILENKO.ORG") + 4;
	x -= ui_overlay_text(UI_FONT_REGULAR, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_DIM, "MASTER") + 14;
	snprintf(text, sizeof(text), "%ld", players);
	x -= ui_overlay_text(UI_FONT_BOLD, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_TEXT, text) + 4;
	x -= ui_overlay_text(UI_FONT_REGULAR, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_DIM, "PLAYERS") + 14;
	snprintf(text, sizeof(text), "%d", browser_screen.count);
	x -= ui_overlay_text(UI_FONT_BOLD, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_TEXT, text) + 4;
	ui_overlay_text(UI_FONT_REGULAR, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_DIM, "SERVERS");

	/* the list */
	ui_overlay_rect(LIST_X, LIST_Y, LIST_WIDTH, LIST_HEAD + ROWS_PER_PAGE * LIST_ROW + LIST_FOOT, 6, COLOR_PANEL);
	ui_overlay_gradient(LIST_X, LIST_Y, LIST_WIDTH, LIST_HEAD, 6, 0x123266FF, 0x0C2347FF);
	ui_overlay_text(UI_FONT_BOLD, 8.0f, COLUMN_NAME, LIST_Y + 6, UI_ALIGN_LEFT, COLOR_HEAD, "Server");
	ui_overlay_text(UI_FONT_BOLD, 8.0f, COLUMN_MAP, LIST_Y + 6, UI_ALIGN_LEFT, COLOR_HEAD, "Map");
	ui_overlay_text(UI_FONT_BOLD, 8.0f, COLUMN_TYPE, LIST_Y + 6, UI_ALIGN_LEFT, COLOR_HEAD, "Type");
	ui_overlay_text(UI_FONT_BOLD, 8.0f, COLUMN_PLAYERS, LIST_Y + 6, UI_ALIGN_RIGHT,
		browser_screen.sort == SORT_PLAYERS ? 0xFFFFFFFF : COLOR_HEAD, "Players");
	ui_overlay_text(UI_FONT_BOLD, 8.0f, COLUMN_PING, LIST_Y + 6, UI_ALIGN_RIGHT, COLOR_HEAD, "Ping");

	page_first = (short)(browser_screen.selected - browser_screen.selected % ROWS_PER_PAGE);
	page_count = (short)MAX(1, (browser_screen.count + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE);
	if (!browser_screen.count)
	{
		ui_overlay_text(UI_FONT_REGULAR, 10.0f, 320, LIST_Y + LIST_HEAD + 80, UI_ALIGN_CENTER, COLOR_DIM,
			"No one is hosting right now. Host a game, and it shows here.");
	}
	for (row = 0; row < ROWS_PER_PAGE; row++)
	{
		float y = (float)(LIST_Y + LIST_HEAD + row * LIST_ROW);
		struct browser_game const *game;
		unsigned int color;

		if (row)
			ui_overlay_rect(LIST_X + 1, y, LIST_WIDTH - 2, 0.5f, 0, COLOR_ROW_RULE);
		if (page_first + row >= browser_screen.count)
			continue;
		game = &browser_screen.games[page_first + row];
		if (page_first + row == browser_screen.selected)
			ui_overlay_rect(LIST_X + 1, y, LIST_WIDTH - 2, LIST_ROW, 0, COLOR_ROW_SELECTED);
		color = game->open ? COLOR_TEXT : COLOR_DIM;
		utf8_name(game->name, name, sizeof(name));
		ui_overlay_text(UI_FONT_BOLD, 10.0f, COLUMN_NAME, y + 5, UI_ALIGN_LEFT, color, name);
		ui_overlay_text(UI_FONT_BOLD, 10.0f, COLUMN_MAP, y + 5, UI_ALIGN_LEFT, color, map_display_name(game->map));
		ui_overlay_text(UI_FONT_BOLD, 10.0f, COLUMN_TYPE, y + 5, UI_ALIGN_LEFT, color, type_name(game, text, sizeof(text)));
		snprintf(text, sizeof(text), "%d/%d", game->players, game->maximum_players);
		ui_overlay_text(UI_FONT_BOLD, 10.0f, COLUMN_PLAYERS, y + 5, UI_ALIGN_RIGHT, game->open ? color : COLOR_CLOSED, text);
		/* (no ping yet: the probe is to come) */
		ui_overlay_text(UI_FONT_REGULAR, 10.0f, COLUMN_PING, y + 5, UI_ALIGN_RIGHT, COLOR_DIM, "\xE2\x80\x93");
	}
	{
		float y = (float)(LIST_Y + LIST_HEAD + ROWS_PER_PAGE * LIST_ROW);

		ui_overlay_rect(LIST_X + 1, y, LIST_WIDTH - 2, 0.75f, 0, COLOR_PANEL_EDGE);
		snprintf(text, sizeof(text), "SORTED BY %s  \xC2\xB7  CLOSED GAMES LAST", sort_names[browser_screen.sort]);
		ui_overlay_text(UI_FONT_BOLD, 7.5f, LIST_X + 10, y + 6, UI_ALIGN_LEFT, COLOR_DIM, text);
		snprintf(text, sizeof(text), "\xE2\x80\xB9   PAGE %d OF %d   \xE2\x80\xBA", page_first / ROWS_PER_PAGE + 1, page_count);
		ui_overlay_text(UI_FONT_BOLD, 8.0f, LIST_X + LIST_WIDTH - 12, y + 5.5f, UI_ALIGN_RIGHT, COLOR_LABEL, text);
	}
	ui_overlay_outline(LIST_X, LIST_Y, LIST_WIDTH, LIST_HEAD + ROWS_PER_PAGE * LIST_ROW + LIST_FOOT, 6, 1.0f,
		COLOR_PANEL_EDGE);

	/* the selected game */
	ui_overlay_rect(LIST_X, DETAIL_Y, LIST_WIDTH, DETAIL_HEIGHT, 6, COLOR_PANEL);
	ui_overlay_outline(LIST_X, DETAIL_Y, LIST_WIDTH, DETAIL_HEIGHT, 6, 1.0f, COLOR_PANEL_EDGE);
	if (selected)
	{
		short map_frame = NUMBEROF(map_picture_order);
		char const *base = selected->map;
		char const *cursor;
		float y = DETAIL_Y + 34;

		for (cursor = selected->map; *cursor; cursor++)
		{
			if (*cursor == '\\' || *cursor == '/')
				base = cursor + 1;
		}
		for (index = 0; index < NUMBEROF(map_picture_order); index++)
		{
			if (!strcmp(base, map_picture_order[index]))
				map_frame = (short)index;
		}
		draw_picture("ui\\shell\\bitmaps\\mp_map_grafix", map_frame, 140, 116, 46, DETAIL_Y + 9, 171, DETAIL_Y + DETAIL_HEIGHT - 9);
		ui_overlay_outline(45, DETAIL_Y + 8, 127, DETAIL_HEIGHT - 16, 0, 1.0f, COLOR_ROW_RULE);
		draw_picture("ui\\shell\\bitmaps\\game_type_grafix",
			engine_picture[selected->engine >= 0 && selected->engine < NUMBEROF(engine_picture) ? selected->engine : 0],
			140, 114, 181, DETAIL_Y + 9, 256, DETAIL_Y + DETAIL_HEIGHT - 9);

		utf8_name(selected->name, name, sizeof(name));
		ui_overlay_text(UI_FONT_BOLD, 13.0f, 266, DETAIL_Y + 12, UI_ALIGN_LEFT, 0xFFFFFFFF, name);
#define DETAIL_LINE(label, value) \
		x = 266 + ui_overlay_text(UI_FONT_REGULAR, 10.0f, 266, y, UI_ALIGN_LEFT, COLOR_LABEL, label) + 4; \
		ui_overlay_text(UI_FONT_REGULAR, 10.0f, x, y, UI_ALIGN_LEFT, COLOR_TEXT, value); \
		y += 16;
		DETAIL_LINE("Status:", selected->open ? "Accepting Players" : "In Progress");
		DETAIL_LINE("Map:", map_display_name(selected->map));
		DETAIL_LINE("Rules:", type_name(selected, text, sizeof(text)));
		if (selected->score_limit)
		{
			snprintf(text, sizeof(text), "%d", selected->score_limit);
			DETAIL_LINE("Score Limit:", text);
		}
		snprintf(text, sizeof(text), "%d of %d", selected->players, selected->maximum_players);
		DETAIL_LINE("Players:", text);
#undef DETAIL_LINE

		/* who is in it (the host's roster, when it sends one) */
		ui_overlay_rect(444, DETAIL_Y + 10, 0.75f, DETAIL_HEIGHT - 20, 0, COLOR_ROW_RULE);
		ui_overlay_text(UI_FONT_BOLD, 8.5f, 453, DETAIL_Y + 10, UI_ALIGN_LEFT, COLOR_LABEL, "IN GAME");
		width = ui_overlay_text(UI_FONT_REGULAR, 8.5f, 453, DETAIL_Y + 27, UI_ALIGN_LEFT, COLOR_DIM,
			selected->players ? "The roster shows here" : "No one yet");
		(void)width;
	}

	/* the buttons */
	ui_overlay_rect(-margin, 444, 640 + 2 * margin, 0.75f, 0, COLOR_RULE);
	width = prompt_width(UI_BUTTON_A, "=JOIN") + prompt_width(UI_BUTTON_B, "=BACK") +
		prompt_width(UI_BUTTON_X, "=REFRESH") + prompt_width(UI_BUTTON_Y, "=FILTERS") +
		prompt_width(UI_BUTTON_LEFT_TRIGGER, "") + prompt_width(UI_BUTTON_RIGHT_TRIGGER, "=SORT") - 20 - 3;
	x = 320 - width / 2;
	x = prompt(UI_BUTTON_A, "=JOIN", x);
	x = prompt(UI_BUTTON_B, "=BACK", x);
	x = prompt(UI_BUTTON_X, "=REFRESH", x);
	x = prompt(UI_BUTTON_Y, "=FILTERS", x);
	x += ui_overlay_button(UI_BUTTON_LEFT_TRIGGER, 15.0f, x, 455.0f, 0xFFFFFFFF);
	prompt(UI_BUTTON_RIGHT_TRIGGER, "=SORT", x);

	if (browser_screen.status[0] && system_milliseconds() - browser_screen.status_time < STATUS_DURATION)
		ui_overlay_text(UI_FONT_BOLD, 9.0f, 603, 300, UI_ALIGN_RIGHT, COLOR_CLOSED, browser_screen.status);
}

#endif
