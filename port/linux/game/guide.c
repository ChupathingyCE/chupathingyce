/*
GUIDE.C

The Guide (guide.h): one panel over whatever is on screen, the same in the
Xbox's, Gearbox's and the Chupathingy menus and over a game in progress.
Its look is the Chupathingy menus' (the Xbox's blues, the plasma under the
menus' items, glow on the selected row and the edges, the prompt bar), its
data social.c's.

The panel: the player's card (their name and what they're doing), then
the sections, switched with LB and RB (Q and E, or a click): FRIENDS,
PARTY, PLAYERS HERE, RECENT and SETTINGS.
- PLAYERS HERE lists the game's players: their Delta Tag when they share
  their profile (else their game name), their platform's logo when the game
  is on Delta, and a local Mute (the chat log's, chat.c). No address is ever
  shown.
- SETTINGS has the Guide's own settings.
- FRIENDS, PARTY and RECENT come with the site's friends: until then, a
  player without a Delta profile is offered SIGN IN (Delta Profile's Link
  Profile).

It takes the first player's input while open (guide_input.h): the game
gets a controller at rest, and their player stands still
(guide_takes_player). Network games keep running; a game on this machine
alone pauses as it does under the pause menu. B closes it, back to what was
under it as it was.

It opens with controls.guide (F3), the first player's Guide button
(controls.guide_button, where SDL reads it), Back held in the menus with
guide.hold_back (off by default), or the first player's pause menu's GUIDE
(menu_tags.c, guide.pause_menu); the same key or button, or B, closes it.

Toasts (guide_toast) show low in the middle of the screen for a few
seconds: in the menus, and in a game only with guide.notify_in_game. None
are sent yet.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "guide.h"

#ifdef HALO_GUIDE

#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "interface/event_manager.h"
#include "interface/interface.h"
#include "interface/ui_widget.h"
#include "bitmaps/bitmap_group.h"
#include "cache/texture_cache.h"
#include "networking/network_game_globals.h"
#include "saved games/player_profile.h"
#include "interface/player_ui.h"
#include "main/main.h"
#include "../src/browser.h"
#include "../src/delta_profile.h"
#include "../src/discord_activity.h"
#include "../src/guide_input.h"
#include "../src/social.h"
#include "../src/ui_overlay.h"
#include "halo_ui_pointer.h"
#include "halo_profile_screen.h"
#include "chat.h"

#include <stdio.h>
#include <string.h>

int config_boolean(char const *name);
char const *config_string(char const *name);
int config_write(char const *name, char const *value);
int config_write_boolean(char const *name, int value);
void platform_log(char const *format, ...);
/* delta_peer_game.c's */
int delta_peer_game_on_delta(void);
int delta_peer_player_platform(int player_index);
int delta_peer_player_profile(int player_index, unsigned char *player_id);
/* discord_game.c's */
boolean discord_game_get(struct discord_activity_game *game);
/* virtual_keyboard.c's */
boolean virtual_keyboard_active(void);
/* ui_widget.c's: the game paused as a menu pauses it */
void ui_widgets_port_pause_game(boolean pause);

/* ---------- constants */

/* the panel's place, in the menus' 640x480 */
enum
{
	PANEL_X = 48, PANEL_Y = 38, PANEL_WIDTH = 544, PANEL_HEIGHT = 398,
	PANEL_RIGHT = PANEL_X + PANEL_WIDTH,
	INSET = 16,
	CONTENT_LEFT = PANEL_X + INSET,
	CONTENT_RIGHT = PANEL_RIGHT - INSET,
	HEADER_Y = PANEL_Y + 12,
	RULE_Y = PANEL_Y + 54,
	TABS_Y = PANEL_Y + 62,
	TABS_HEIGHT = 22,
	BODY_Y = PANEL_Y + 96,
	PROMPTS_Y = PANEL_Y + PANEL_HEIGHT - 30,
	BODY_BOTTOM = PROMPTS_Y - 8,
	LIST_WIDTH = 262,
	DETAIL_LEFT = CONTENT_LEFT + LIST_WIDTH + 12,
	ROW_HEIGHT = 30,
	ROW_GAP = 4,
	SETTING_HEIGHT = 26,

	/* the toast's place */
	TOAST_WIDTH = 220, TOAST_HEIGHT = 34,
	TOAST_X = 320 - TOAST_WIDTH / 2, TOAST_Y = 404,
	TOAST_TIME = 5000,
	TOAST_FADE = 400,

	/* a d-pad held: again after this long, then this often */
	REPEAT_DELAY = 400,
	REPEAT_INTERVAL = 120,

	/* (the plasma's map from ui.map: looked for this many frames, then the
	overlay's own noise) */
	PLASMA_TRIES = 240,

	MAXIMUM_TARGETS = 48,
};

/* the Chupathingy menus' blues (the mockups': review/guide-mockup) */
enum
{
	COLOR_SHADE = 0x02081AA8,
	COLOR_PANEL = 0x0A2652E0,
	COLOR_PANEL_EDGE = 0x2F7FD6FF,
	COLOR_GLOW = 0x2F8FFFFF,
	COLOR_RULE = 0x50A0FF5A,
	COLOR_TITLE = 0x3D9BFFFF,
	COLOR_NAME = 0xFFFFFFFF,
	COLOR_STATUS = 0x8FC4FFFF,
	COLOR_LABEL = 0x6FA6DDFF,
	COLOR_TAB = 0x7FB6EEFF,
	COLOR_TAB_ON = 0x2F7FD68C,
	COLOR_SELECTED_EDGE = 0x9FD4FFFF,
	COLOR_ROW = 0x0A2246E6,
	COLOR_ROW_EDGE = 0x50A0FF40,
	COLOR_ROW_ON = 0x2F7FD670,
	COLOR_DETAIL = 0x020C1EA0,
	COLOR_VALUE = 0xFFFFFFFF,
	COLOR_SETTING = 0x9FD0FFFF,
	COLOR_PROMPT = 0x3D9BFFFF,
	COLOR_PROMPT_GLOW = 0x3D8BFF80,
	COLOR_DOT_GAME = 0x4FB3FFFF,
	COLOR_DOT_ONLINE = 0x7FE39AFF,
	COLOR_DOT_AWAY = 0xE0A53AFF,
	COLOR_DOT_OFFLINE = 0x4A5A70FF,
	COLOR_MUTED = 0xFF9F9FFF,
};

/* the Guide's own settings, as SETTINGS lists them */
enum
{
	_setting_open_key,
	_setting_guide_button,
	_setting_notify_in_game,
	_setting_share_presence,
	NUMBER_OF_SETTINGS
};

static char const *const setting_labels[NUMBER_OF_SETTINGS] =
{
	"OPEN KEY", "GUIDE BUTTON", "NOTIFICATIONS IN GAME", "SHOW WHEN I'M PLAYING",
};

static char const *const setting_keys[NUMBER_OF_SETTINGS] =
{
	"controls.guide", "controls.guide_button", "guide.notify_in_game", "network.share_presence",
};

/* controls.guide's choices: the F keys the game leaves free (F1 is Back,
F2 Start, F11 full screen, F12 the mouse) */
static char const *const open_keys[] =
{
	"F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10",
};

/* what a click or tap lands on */
enum
{
	_target_tab,
	_target_row,
	_target_action,
	_target_setting,
	_target_sign_in,
	_target_close,
};

struct guide_target
{
	float x0, y0, x1, y1;
	short kind;
	short index;
};

/* ---------- globals */

static struct
{
	boolean open;
	short section;
	/* the row chosen in each section */
	short row[NUMBER_OF_SOCIAL_SECTIONS];
	/* the first row shown of a long list */
	short first_row;
	unsigned int held;
	unsigned int repeat_held;
	unsigned long repeat_time;
	unsigned long opened_time;
	/* it paused the game (a game on this machine alone) */
	boolean paused_game;

	/* PLAYERS HERE: the rows, and each row's player (their machine and game
	name, for the mute) */
	struct social_list players;
	struct social_list players_fresh;
	short machines[SOCIAL_MAXIMUM_ROWS];
	wchar_t names[SOCIAL_MAXIMUM_ROWS][12];

	/* the card */
	char name[SOCIAL_TAG_SIZE];
	char status[SOCIAL_STATUS_SIZE];
	unsigned int color;
	boolean linked;

	/* the toast, and when it came */
	char toast_title[96];
	char toast_line[96];
	unsigned long toast_time;
	boolean toast_shown;
	boolean debug_toast_done;
	boolean debug_open_done;

	int plasma_tries;

	struct guide_target targets[MAXIMUM_TARGETS];
	short target_count;
} guide;

/* ---------- private code */

static void target_add(float x0, float y0, float x1, float y1, short kind, short index)
{
	if (guide.target_count >= MAXIMUM_TARGETS)
		return;
	guide.targets[guide.target_count].x0 = x0;
	guide.targets[guide.target_count].y0 = y0;
	guide.targets[guide.target_count].x1 = x1;
	guide.targets[guide.target_count].y1 = y1;
	guide.targets[guide.target_count].kind = kind;
	guide.targets[guide.target_count].index = index;
	guide.target_count++;
}

/* the game's UTF-16 as UTF-8 */
static void utf8_from_wide(char *text, int size, wchar_t const *wide, int length)
{
	int used = 0, index;

	for (index = 0; index < length && wide[index] && used < size - 1; index++)
	{
		unsigned int character = (unsigned short)wide[index];

		if (character < 0x80)
			text[used++] = (char)character;
		else if (character < 0x800)
		{
			if (used + 2 > size - 1)
				break;
			text[used++] = (char)(0xC0 | (character >> 6));
			text[used++] = (char)(0x80 | (character & 0x3F));
		}
		else
		{
			if (used + 3 > size - 1)
				break;
			text[used++] = (char)(0xE0 | (character >> 12));
			text[used++] = (char)(0x80 | ((character >> 6) & 0x3F));
			text[used++] = (char)(0x80 | (character & 0x3F));
		}
	}
	text[used] = 0;
}

static unsigned int fade(unsigned int color, float share)
{
	float alpha = (float)(color & 0xFF) * share;

	return (color & 0xFFFFFF00) | (unsigned int)(alpha < 0.0f ? 0.0f : alpha > 255.0f ? 255.0f : alpha);
}

/* a game alone on this machine: the Guide pauses it as the pause menu does */
static boolean pauses_game(void)
{
	return !main_menu_is_active() && game_in_progress() && game_connection() == _game_connection_local;
}

static boolean in_game(void)
{
	return !main_menu_is_active() && game_in_progress();
}

/* whether SIGN IN goes anywhere: Delta Profile's Link Profile */
static short sign_in_section(void)
{
	short section, count;

	if (!profile_screen_available())
		return NONE;
	count = profile_screen_section_count();
	for (section = 0; section < count; section++)
	{
		if (!strcmp(profile_screen_section_name(section), "LINK PROFILE"))
			return section;
	}
	return NONE;
}

/* ---------- the card */

static void card_update(void)
{
	struct discord_activity_game game;
	char handle[64];

	guide.linked = browser_profile_linked(handle, sizeof(handle)) != 0;
	guide.name[0] = 0;
	if (guide.linked && handle[0])
		social_copy_text(guide.name, sizeof(guide.name), handle);
	guide.color = 0x2F6DD0FF;
	if (player_ui_get_active_player_profile_index(0) != NONE)
	{
		struct player_profile profile;
		real_rgb_color color;

		player_ui_get_active_player_profile(0, &profile);
		if (!guide.name[0])
			utf8_from_wide(guide.name, sizeof(guide.name), profile.player_name, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH);
		color = player_profile_get_rgb_color(profile.primary_color_index);
		guide.color = ((unsigned int)(PIN(color.red, 0.0f, 1.0f) * 255.0f) << 24) |
			((unsigned int)(PIN(color.green, 0.0f, 1.0f) * 255.0f) << 16) |
			((unsigned int)(PIN(color.blue, 0.0f, 1.0f) * 255.0f) << 8) | 0xFF;
	}
	/* (no profile chosen, in a game: the first player's name there) */
	if (!guide.name[0] && in_game())
	{
		struct data_iterator iterator;
		struct player_datum *player;

		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (player->local_player_index == 0)
			{
				utf8_from_wide(guide.name, sizeof(guide.name), player->name, NUMBEROF(player->name));
				break;
			}
		}
	}
	social_own_status(discord_game_get(&game) ? &game : NULL, guide.status, sizeof(guide.status));
}

/* ---------- PLAYERS HERE: the game's players (none outside a network game) */

static void players_update(void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	boolean delta = delta_peer_game_on_delta() != 0;
	boolean teams = game_engine_running() && game_engine_has_teams();
	short count = 0;

	social_list_clear(&guide.players_fresh);
	if (!network_game_get_game() || main_menu_is_active() || !game_in_progress())
	{
		social_list_commit(&guide.players, &guide.players_fresh);
		return;
	}
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL && count < SOCIAL_MAXIMUM_ROWS)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct social_row row;
		wchar_t shown[48];
		boolean you = player->local_player_index != NONE;

		if (player->quit_out_of_game)
			continue;
		csmemset(&row, 0, sizeof(row));
		/* (a shared Delta profile's name, else the game name) */
		if (profile_names_player(absolute, shown, NUMBEROF(shown)))
		{
			utf8_from_wide(row.tag, sizeof(row.tag), shown, NUMBEROF(shown));
			row.verified = TRUE;
		}
		else
			utf8_from_wide(row.tag, sizeof(row.tag), player->name, NUMBEROF(player->name));
		row.platform = delta ? delta_peer_player_platform((int)absolute) : 0;
		if (row.platform < 0)
			row.platform = 0;
		row.state = SOCIAL_STATE_IN_GAME;
		row.key = (int)absolute;
		row.muted = !you && chat_local_muted(player->network_player_data.machine_index, player->name);
		row.actions = you ? 0 : SOCIAL_ACTION_MUTE;
		if (you)
			snprintf(row.status, sizeof(row.status), "You");
		else if (row.muted)
			snprintf(row.status, sizeof(row.status), "Muted");
		else if (teams)
			snprintf(row.status, sizeof(row.status), "%s", player->team_index == 0 ? "Red Team" : "Blue Team");
		if (social_list_add(&guide.players_fresh, &row) >= 0)
		{
			guide.machines[guide.players_fresh.count - 1] = player->network_player_data.machine_index;
			csmemcpy(guide.names[guide.players_fresh.count - 1], player->name, sizeof(guide.names[0]));
			count++;
		}
	}
	social_list_commit(&guide.players, &guide.players_fresh);
	if (guide.row[SOCIAL_SECTION_PLAYERS_HERE] >= guide.players.count)
		guide.row[SOCIAL_SECTION_PLAYERS_HERE] = (short)MAX(0, guide.players.count - 1);
}

static void player_mute_toggle(short row)
{
	struct social_row const *player;

	if (row < 0 || row >= guide.players.count)
		return;
	player = &guide.players.rows[row];
	if (!(player->actions & SOCIAL_ACTION_MUTE))
		return;
	chat_local_mute(guide.machines[row], guide.names[row], !player->muted);
	players_update();
}

/* ---------- SETTINGS */

static boolean setting_on(short setting)
{
	return config_boolean(setting_keys[setting]) != 0;
}

static void setting_value(short setting, char *text, int size)
{
	if (setting == _setting_open_key)
	{
		char const *key = config_string(setting_keys[setting]);

		snprintf(text, (size_t)size, "%s", key[0] ? key : "F3");
	}
	else
		snprintf(text, (size_t)size, "%s", setting_on(setting) ? "ON" : "OFF");
}

static void setting_change(short setting, short direction)
{
	if (setting == _setting_open_key)
	{
		char const *key = config_string(setting_keys[setting]);
		short count = (short)NUMBEROF(open_keys), index, found = 0;

		for (index = 0; index < count; index++)
		{
			char const *a = key, *b = open_keys[index];

			while (*a && (*a | 0x20) == (*b | 0x20))
				a++, b++;
			if (!*a && !*b)
				found = index;
		}
		found = (short)((found + (direction < 0 ? count - 1 : 1)) % count);
		config_write(setting_keys[setting], open_keys[found]);
	}
	else if (setting >= 0 && setting < NUMBER_OF_SETTINGS)
		config_write_boolean(setting_keys[setting], !setting_on(setting));
}

/* ---------- the sections' rows */

static short section_rows(short section)
{
	switch (section)
	{
	case SOCIAL_SECTION_PLAYERS_HERE:
		return (short)guide.players.count;
	case SOCIAL_SECTION_SETTINGS:
		return NUMBER_OF_SETTINGS;
	default:
		/* (SIGN IN, without a Delta profile) */
		return !guide.linked && sign_in_section() != NONE ? 1 : 0;
	}
}

static void section_go(short section)
{
	guide.section = (short)((section + NUMBER_OF_SOCIAL_SECTIONS) % NUMBER_OF_SOCIAL_SECTIONS);
	guide.first_row = 0;
}

/* A on the chosen row */
static void activate(void)
{
	short row = guide.row[guide.section];

	switch (guide.section)
	{
	case SOCIAL_SECTION_PLAYERS_HERE:
		player_mute_toggle(row);
		break;
	case SOCIAL_SECTION_SETTINGS:
		setting_change(row, 1);
		break;
	default:
		if (!guide.linked && sign_in_section() != NONE)
		{
			short section = sign_in_section();

			/* (Delta Profile's Link Profile, its dialog over what was under
			the Guide) */
			guide_close();
			profile_screen_section_action(section);
		}
		break;
	}
}

static void move(short step)
{
	short rows = section_rows(guide.section);
	short *row = &guide.row[guide.section];

	if (rows <= 0)
		return;
	*row = (short)PIN(*row + step, 0, rows - 1);
}

/* what was just pressed (and a d-pad held, again and again) */
static unsigned int presses(void)
{
	unsigned int held = platform_guide_held();
	unsigned int pressed = held & ~guide.held;
	unsigned int directions = held & (GUIDE_INPUT_UP | GUIDE_INPUT_DOWN | GUIDE_INPUT_LEFT | GUIDE_INPUT_RIGHT);
	unsigned long now = system_milliseconds();

	if (directions && directions == guide.repeat_held)
	{
		if (now - guide.repeat_time >= REPEAT_DELAY)
		{
			pressed |= directions;
			guide.repeat_time = now - REPEAT_DELAY + REPEAT_INTERVAL;
		}
	}
	else
	{
		guide.repeat_held = directions;
		guide.repeat_time = now;
	}
	guide.held = held;
	return pressed;
}

/* ---------- drawing */

/* the player's dot */
static unsigned int dot_color(int state)
{
	switch (state)
	{
	case SOCIAL_STATE_IN_GAME: return COLOR_DOT_GAME;
	case SOCIAL_STATE_ONLINE: return COLOR_DOT_ONLINE;
	case SOCIAL_STATE_AWAY: return COLOR_DOT_AWAY;
	default: return COLOR_DOT_OFFLINE;
	}
}

/* the ui.map's plasma map, read once it is in the texture cache (none:
the overlay's own noise) */
static void plasma_look(void)
{
	long tag;

	if (guide.plasma_tries >= PLASMA_TRIES || (!main_menu_is_active() && !game_map_loaded()))
		return;
	guide.plasma_tries++;
	tag = interface_get_tag_index(_interface_bitmap_iface_map3);
	if (tag != NONE)
	{
		struct bitmap_group *group = bitmap_group_get(tag);

		if (group && group->bitmaps.count > 0)
		{
			struct bitmap_data *bitmap = TAG_BLOCK_GET_ELEMENT(&group->bitmaps, 0, struct bitmap_data);
			void *hardware_format = _texture_cache_bitmap_get_hardware_format(bitmap, FALSE, TRUE);

			if (hardware_format && ui_overlay_plasma_from_texture(hardware_format))
			{
				guide.plasma_tries = PLASMA_TRIES;
				return;
			}
		}
	}
	if (guide.plasma_tries == PLASMA_TRIES)
		platform_log("Guide: no plasma map in ui.map; drawn from the Guide's own");
}

static void render_card(void)
{
	ui_style_card(CONTENT_LEFT, CONTENT_RIGHT, HEADER_Y, guide.name, guide.status, guide.color, "GUIDE");
}

static void render_tabs(void)
{
	char const *names[NUMBER_OF_SOCIAL_SECTIONS];
	float starts[NUMBER_OF_SOCIAL_SECTIONS], ends[NUMBER_OF_SOCIAL_SECTIONS];
	short section;

	for (section = 0; section < NUMBER_OF_SOCIAL_SECTIONS; section++)
		names[section] = social_section_name(section);
	ui_style_tabs(CONTENT_LEFT, CONTENT_RIGHT, TABS_Y, TABS_HEIGHT, names, NUMBER_OF_SOCIAL_SECTIONS, guide.section,
		"Q", "E", starts, ends);
	for (section = 0; section < NUMBER_OF_SOCIAL_SECTIONS; section++)
		target_add(starts[section], TABS_Y, ends[section], TABS_Y + TABS_HEIGHT, _target_tab, section);
}

/* a player's row: their dot, tag, status line and platform's logo */
static void render_row(struct social_row const *row, float x, float y, float width, boolean on, short index)
{
	ui_style_row(x, y, width, ROW_HEIGHT, on);
	ui_overlay_rect(x + 9, y + ROW_HEIGHT * 0.5f - 3, 6, 6, 3, dot_color(row->state));
	if (row->status[0])
	{
		ui_overlay_text(UI_FONT_MENU, 10.5f, x + 22, y + 3, UI_ALIGN_LEFT, COLOR_NAME, row->tag);
		ui_overlay_text(UI_FONT_REGULAR, 8.0f, x + 22, y + 17, UI_ALIGN_LEFT,
			row->muted ? COLOR_MUTED : COLOR_STATUS, row->status);
	}
	else
		ui_overlay_text(UI_FONT_MENU, 10.5f, x + 22, y + (ROW_HEIGHT - 10.5f) * 0.5f, UI_ALIGN_LEFT, COLOR_NAME, row->tag);
	if (row->platform > 0)
		ui_overlay_platform(row->platform, 11.0f, x + width - 14, y + (ROW_HEIGHT - 11.0f) * 0.5f, COLOR_LABEL, 0);
	target_add(x, y, x + width, y + ROW_HEIGHT, _target_row, index);
}

/* a button of the detail pane (an action) */
static void render_action(char const *label, float x, float y, float width, boolean on, unsigned int text,
	short index)
{
	ui_style_row(x, y, width, 24, on);
	ui_overlay_text(UI_FONT_MENU, 10.0f, x + 10, y + 7, UI_ALIGN_LEFT, text, label);
	target_add(x, y, x + width, y + 24, _target_action, index);
}

static void render_players(void)
{
	short count = (short)guide.players.count, visible, index;
	short chosen = guide.row[SOCIAL_SECTION_PLAYERS_HERE];
	char heading[48];
	float y = BODY_Y + 16;

	if (!count)
		return;
	snprintf(heading, sizeof(heading), "PLAYERS (%d)", count);
	ui_overlay_text(UI_FONT_MENU, 8.5f, CONTENT_LEFT, BODY_Y, UI_ALIGN_LEFT, COLOR_LABEL, heading);
	visible = (short)((BODY_BOTTOM - y + ROW_GAP) / (ROW_HEIGHT + ROW_GAP));
	if (chosen < guide.first_row)
		guide.first_row = chosen;
	else if (chosen >= guide.first_row + visible)
		guide.first_row = (short)(chosen - visible + 1);
	for (index = guide.first_row; index < count && index < guide.first_row + visible; index++)
	{
		render_row(&guide.players.rows[index], CONTENT_LEFT, y, LIST_WIDTH, index == chosen, index);
		y += ROW_HEIGHT + ROW_GAP;
	}
	/* the chosen player, and what can be done */
	if (chosen >= 0 && chosen < count)
	{
		struct social_row const *row = &guide.players.rows[chosen];
		float x = DETAIL_LEFT, width = CONTENT_RIGHT - DETAIL_LEFT;

		ui_style_surface(x, BODY_Y, width, BODY_BOTTOM - BODY_Y, 8, COLOR_DETAIL, COLOR_ROW_EDGE, FALSE);
		ui_overlay_text(UI_FONT_MENU, 13.0f, x + 12, BODY_Y + 12, UI_ALIGN_LEFT, COLOR_NAME, row->tag);
		if (row->platform > 0)
			ui_overlay_platform(row->platform, 13.0f, x + width - 18, BODY_Y + 12, COLOR_LABEL, 0);
		if (row->status[0])
			ui_overlay_text(UI_FONT_REGULAR, 9.5f, x + 12, BODY_Y + 32, UI_ALIGN_LEFT,
				row->muted ? COLOR_MUTED : COLOR_STATUS, row->status);
		if (row->actions & SOCIAL_ACTION_MUTE)
			render_action(row->muted ? "UNMUTE" : "MUTE", x + 12, BODY_Y + 54, width - 24, TRUE, COLOR_VALUE, 0);
	}
}

static void render_settings(void)
{
	short setting;
	float y = BODY_Y;

	for (setting = 0; setting < NUMBER_OF_SETTINGS; setting++)
	{
		boolean on = setting == guide.row[SOCIAL_SECTION_SETTINGS];
		char value[32];
		float width = CONTENT_RIGHT - CONTENT_LEFT, value_right = CONTENT_RIGHT - 14;

		setting_value(setting, value, sizeof(value));
		ui_style_row(CONTENT_LEFT, y, width, SETTING_HEIGHT, on);
		ui_overlay_text(UI_FONT_MENU, 10.0f, CONTENT_LEFT + 12, y + 8, UI_ALIGN_LEFT, COLOR_SETTING,
			setting_labels[setting]);
		/* (the value between its arrows, as the menus' settings have it) */
		ui_overlay_text(UI_FONT_MENU, 10.0f, value_right, y + 8, UI_ALIGN_RIGHT, COLOR_VALUE, "\xE2\x80\xBA");
		ui_overlay_text(UI_FONT_MENU, 10.0f, value_right - 12, y + 8, UI_ALIGN_RIGHT, COLOR_VALUE, value);
		ui_overlay_text(UI_FONT_MENU, 10.0f, value_right - 18 - ui_overlay_text_width(UI_FONT_MENU, 10.0f, value),
			y + 8, UI_ALIGN_RIGHT, COLOR_VALUE, "\xE2\x80\xB9");
		target_add(CONTENT_LEFT, y, CONTENT_RIGHT, y + SETTING_HEIGHT, _target_setting, setting);
		y += SETTING_HEIGHT + ROW_GAP;
	}
}

/* FRIENDS, PARTY and RECENT: SIGN IN without a Delta profile; nothing yet
with one (the site's friends come next) */
static void render_coming(void)
{
	float center = (CONTENT_LEFT + CONTENT_RIGHT) * 0.5f, width = 140;
	float y = (BODY_Y + BODY_BOTTOM) * 0.5f - 24;

	if (guide.linked || sign_in_section() == NONE)
		return;
	ui_overlay_text(UI_FONT_REGULAR, 11.0f, center, y, UI_ALIGN_CENTER, COLOR_STATUS, "Sign in to Delta to see friends");
	ui_style_row(center - width * 0.5f, y + 22, width, 26, TRUE);
	ui_overlay_text_glow(UI_FONT_MENU, 11.0f, center, y + 29, UI_ALIGN_CENTER, 0xFFFFFFFF, COLOR_PROMPT_GLOW,
		"SIGN IN");
	target_add(center - width * 0.5f, y + 22, center + width * 0.5f, y + 48, _target_sign_in, 0);
}

static void render_prompts(void)
{
	struct ui_style_prompt prompts[2];
	float starts[2], ends[2];
	char const *word = NULL;
	short count = 0, index;

	switch (guide.section)
	{
	case SOCIAL_SECTION_PLAYERS_HERE:
		if (guide.row[guide.section] < guide.players.count &&
			(guide.players.rows[guide.row[guide.section]].actions & SOCIAL_ACTION_MUTE))
		{
			word = guide.players.rows[guide.row[guide.section]].muted ? "UNMUTE" : "MUTE";
		}
		break;
	case SOCIAL_SECTION_SETTINGS:
		word = "CHANGE";
		break;
	default:
		if (section_rows(guide.section))
			word = "SIGN IN";
		break;
	}
	if (word)
	{
		prompts[count].button = UI_BUTTON_A;
		prompts[count].key = "ENTER";
		prompts[count++].word = word;
	}
	prompts[count].button = UI_BUTTON_B;
	prompts[count].key = "ESC";
	prompts[count++].word = "CLOSE";
	ui_style_prompts(CONTENT_RIGHT, PROMPTS_Y, 15.0f, prompts, count, starts, ends);
	for (index = 0; index < count; index++)
	{
		target_add(starts[index] - 4, PROMPTS_Y - 4, ends[index] + 4, PROMPTS_Y + 19,
			prompts[index].button == UI_BUTTON_B ? _target_close : _target_action, 0);
	}
}

static void render_toast(void)
{
	unsigned long age = system_milliseconds() - guide.toast_time;
	float share;

	if (!guide.toast_shown)
		return;
	if (age >= TOAST_TIME)
	{
		guide.toast_shown = FALSE;
		return;
	}
	/* (quiet in a game unless asked for; never over the loading screen) */
	if (in_game() && !config_boolean("guide.notify_in_game"))
		return;
	share = age < TOAST_FADE ? (float)age / TOAST_FADE :
		age > TOAST_TIME - TOAST_FADE ? (float)(TOAST_TIME - age) / TOAST_FADE : 1.0f;
	ui_overlay_layer(4);
	ui_style_clock((unsigned int)system_milliseconds());
	ui_style_surface(TOAST_X, TOAST_Y, TOAST_WIDTH, TOAST_HEIGHT, 8, fade(COLOR_PANEL, share),
		fade(COLOR_PANEL_EDGE, share), FALSE);
	ui_overlay_rect(TOAST_X + 8, TOAST_Y + 7, 20, 20, 4, fade(guide.color ? guide.color : 0x2F6DD0FF, share));
	ui_overlay_text(UI_FONT_MENU, 10.0f, TOAST_X + 36, TOAST_Y + 5, UI_ALIGN_LEFT, fade(COLOR_NAME, share),
		guide.toast_title);
	ui_overlay_text(UI_FONT_REGULAR, 8.0f, TOAST_X + 36, TOAST_Y + 19, UI_ALIGN_LEFT, fade(COLOR_STATUS, share),
		guide.toast_line);
	ui_overlay_layer(0);
}

/* ---------- public code */

boolean guide_active(void)
{
	return guide.open;
}

boolean guide_takes_player(void)
{
	return guide.open;
}

void guide_open(void)
{
	if (guide.open || !ui_overlay_available())
		return;
	guide.open = TRUE;
	guide.opened_time = system_milliseconds();
	/* (what is held as it opens is not pressed in it) */
	guide.held = 0xFFFFFFFF;
	guide.repeat_held = 0;
	guide.target_count = 0;
	platform_guide_capture(TRUE);
	event_manager_flush();
	card_update();
	players_update();
	browser_profile_load(FALSE);
	guide.paused_game = pauses_game();
	if (guide.paused_game)
		ui_widgets_port_pause_game(TRUE);
	platform_log("Guide: opened");
}

void guide_close(void)
{
	if (!guide.open)
		return;
	guide.open = FALSE;
	guide.target_count = 0;
	platform_guide_capture(FALSE);
	event_manager_flush();
	if (guide.paused_game)
		ui_widgets_port_pause_game(FALSE);
	guide.paused_game = FALSE;
}

void guide_toast(char const *title, char const *line)
{
	social_copy_text(guide.toast_title, sizeof(guide.toast_title), title);
	social_copy_text(guide.toast_line, sizeof(guide.toast_line), line);
	guide.toast_time = system_milliseconds();
	guide.toast_shown = TRUE;
}

void guide_update(boolean menu_open)
{
	/* (debug.guide_open and debug.guide_toast: for screenshots and tests,
	once a run) */
	if (!guide.debug_open_done && config_boolean("debug.guide_open") && (main_menu_is_active() || game_map_loaded()))
	{
		guide.debug_open_done = TRUE;
		guide_open();
	}
	if (!guide.debug_toast_done && config_string("debug.guide_toast")[0] && main_menu_is_active() &&
		menu_open)
	{
		guide.debug_toast_done = TRUE;
		guide_toast(config_string("debug.guide_toast"), "Online \xC2\xB7 In the menus");
	}
	/* (F3, the Guide button, Back held in the menus: open or close; not
	over the virtual keyboard, which takes typing) */
	if (platform_guide_toggles())
	{
		if (guide.open)
			guide_close();
		else if (!virtual_keyboard_active())
			guide_open();
	}
	if (guide.open)
	{
		/* (a game that ended under it lets go of its pause) */
		if (guide.paused_game && !pauses_game())
		{
			ui_widgets_port_pause_game(FALSE);
			guide.paused_game = FALSE;
		}
		card_update();
		players_update();
	}
}

void guide_process(void)
{
	unsigned int pressed;

	if (!guide.open)
		return;
	pressed = presses();
	if (pressed & GUIDE_INPUT_B)
	{
		guide_close();
		return;
	}
	if (pressed & GUIDE_INPUT_LEFT_BUMPER)
		section_go((short)(guide.section - 1));
	if (pressed & GUIDE_INPUT_RIGHT_BUMPER)
		section_go((short)(guide.section + 1));
	if (pressed & GUIDE_INPUT_UP)
		move(-1);
	if (pressed & GUIDE_INPUT_DOWN)
		move(1);
	if (guide.section == SOCIAL_SECTION_SETTINGS && (pressed & (GUIDE_INPUT_LEFT | GUIDE_INPUT_RIGHT)))
		setting_change(guide.row[guide.section], (pressed & GUIDE_INPUT_LEFT) ? -1 : 1);
	if (pressed & GUIDE_INPUT_A)
		activate();
	else if ((pressed & GUIDE_INPUT_X) && guide.section == SOCIAL_SECTION_PLAYERS_HERE)
		player_mute_toggle(guide.row[guide.section]);
}

void guide_pointer(struct halo_ui_pointer const *pointer)
{
	short index;

	if (!guide.open)
		return;
	if (pointer->right_clicks)
	{
		guide_close();
		return;
	}
	if (!pointer->left_clicks)
		return;
	for (index = 0; index < guide.target_count; index++)
	{
		struct guide_target const *target = &guide.targets[index];

		if (pointer->click_x < target->x0 || pointer->click_x >= target->x1 || pointer->click_y < target->y0 ||
			pointer->click_y >= target->y1)
		{
			continue;
		}
		switch (target->kind)
		{
		case _target_tab:
			section_go(target->index);
			break;
		case _target_row:
			/* (a tap chooses the row; on the one chosen, it is its A) */
			if (guide.row[guide.section] == target->index && pointer->touch)
				activate();
			guide.row[guide.section] = target->index;
			break;
		case _target_setting:
			guide.row[guide.section] = target->index;
			setting_change(target->index, 1);
			break;
		case _target_action:
		case _target_sign_in:
			activate();
			break;
		case _target_close:
			guide_close();
			break;
		}
		return;
	}
	/* (a click outside the panel closes it) */
	if (pointer->click_x < PANEL_X || pointer->click_x >= PANEL_RIGHT || pointer->click_y < PANEL_Y ||
		pointer->click_y >= PANEL_Y + PANEL_HEIGHT)
	{
		guide_close();
	}
}

void guide_render(void)
{
	if (!ui_overlay_available())
		return;
	if (!guide.open)
	{
		render_toast();
		return;
	}
	plasma_look();
	guide.target_count = 0;
	ui_overlay_layer(4);
	ui_overlay_rect(-200, 0, 1040, 480, 0, COLOR_SHADE);
	ui_style_clock((unsigned int)system_milliseconds());
	ui_style_panel(PANEL_X, PANEL_Y, PANEL_WIDTH, PANEL_HEIGHT, 1.0f);
	render_card();
	render_tabs();
	switch (guide.section)
	{
	case SOCIAL_SECTION_PLAYERS_HERE:
		render_players();
		break;
	case SOCIAL_SECTION_SETTINGS:
		render_settings();
		break;
	default:
		render_coming();
		break;
	}
	render_prompts();
	ui_overlay_layer(0);
}

#endif
