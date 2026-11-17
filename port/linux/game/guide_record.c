/*
GUIDE_RECORD.C

DELTA's SERVICE RECORD (guide_record.h): the first player's Halo profile's
own record beside their Delta profile's.

- The left pane is this device's record (port/linux/src/service_record.c,
  the profile folder's service_record.txt): every multiplayer game the
  profile finished on this device, as the game's statistics recorder
  added it (port/linux/game/game_stats.c). It is there with or without
  Delta.
- The right pane is the site's record, from Delta Profile's answer
  (browser_profile_get: confirmed games only, the host's and verified
  ones, as the site's own service record page counts them). Without a
  Delta profile it says so.
- Under them, the medals this profile earned most, each with its icon,
  name and count, as the site's medal case shows them (medal_icons.h, the
  site's medals and icons), and its one row: MEDALS ON HUD
  (display.medals), the medals on the HUD, off by default.

Drawn with the Chupathingy menus' look (ui_style_*), in the Guide's panel.
Nothing here is sent anywhere, and no other player is shown.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "guide.h"

#ifdef HALO_GUIDE

#include "interface/player_ui.h"
#include "saved games/player_profile.h"
#include "../src/browser.h"
#include "../src/delta_profile.h"
#include "../src/medal_icons.h"
#include "../src/medal_rules.h"
#include "../src/service_record.h"
#include "../src/ui_overlay.h"
#include "guide_record.h"

#include <stdio.h>
#include <string.h>

int config_boolean(char const *name);
int config_write_boolean(char const *name, int value);
unsigned long system_milliseconds(void);
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);

/* ---------- constants */

enum
{
	/* the record read again this often while shown (a game may have ended
	in the meantime) */
	RELOAD_INTERVAL = 5000,
	/* the site's answer looked at this often */
	SITE_INTERVAL = 500,

	PANE_GAP = 12,
	PANE_HEIGHT = 184,
	LINE_TOP = 40,
	LINE_HEIGHT = 11,
	MEDALS_SHOWN = 5,
	ROW_HEIGHT = 24,
	/* a medal's tile under the panes: its height, and its icon's size */
	TILE_HEIGHT = 40,
	TILE_ICON = 32,
};

/* the theme's (ui_style_color), and white the same in all */
#define COLOR_PANE_EDGE ui_style_color(UI_COLOR_ROW_EDGE)
#define COLOR_SETTING ui_style_color(UI_COLOR_SETTING_NAME)
#define COLOR_COUNT ui_style_color(UI_COLOR_TEXT_SOFT)
enum
{
	COLOR_VALUE = 0xFFFFFFFF,
};

/* ---------- globals */

static struct
{
	boolean loaded;
	unsigned long loaded_time;
	long profile;
	char name[48];
	struct service_record record;
	unsigned long site_time;
	struct browser_profile_status status;
	struct delta_profile site;
} guide_record = { FALSE, 0, NONE };

/* ---------- private code */

/* the first player's Halo profile: the one in use, else the one used last */
static long record_profile(void)
{
	long profile = player_ui_get_active_player_profile_index(0);

	return profile != NONE ? profile : player_ui_get_player1_last_used_profile_index();
}

static void record_load(void)
{
	struct player_profile profile;
	char folder[512], path[1024];
	short index;

	guide_record.loaded = TRUE;
	guide_record.loaded_time = system_milliseconds();
	guide_record.profile = record_profile();
	guide_record.name[0] = 0;
	service_record_clear(&guide_record.record);
	if (guide_record.profile == NONE)
		return;
	if (player_profile_get(guide_record.profile, &profile))
	{
		/* (the profile's name, as the menus' ASCII) */
		for (index = 0; index < (short)sizeof(guide_record.name) - 1 && index < MAXIMUM_PLAYER_PROFILE_NAME_LENGTH &&
			profile.player_name[index]; index++)
		{
			wchar_t character = profile.player_name[index];

			guide_record.name[index] = character >= 0x20 && character < 0x7F ? (char)character : '?';
		}
		guide_record.name[index] = 0;
	}
	if (!player_profile_get_enclosing_directory_path(guide_record.profile, folder))
		return;
	platform_translate_path(folder, path, sizeof(path));
	if (strlen(path) + 1 + strlen(SERVICE_RECORD_FILE_NAME) >= sizeof(path))
		return;
	if (path[0] && path[strlen(path) - 1] != '/' && path[strlen(path) - 1] != '\\')
		strcat(path, "/");
	strcat(path, SERVICE_RECORD_FILE_NAME);
	service_record_load(path, &guide_record.record);
}

static void record_site(void)
{
	guide_record.site_time = system_milliseconds();
	browser_profile_get(&guide_record.status, NULL);
	/* (the Guide opened on this section: nothing asked yet) */
	if (guide_record.status.state == BROWSER_PROFILE_NOT_ASKED)
		browser_profile_load(0);
	if (guide_record.status.has_profile)
		browser_profile_get(&guide_record.status, &guide_record.site);
	else
		csmemset(&guide_record.site, 0, sizeof(guide_record.site));
}

static void number_text(long value, char *text, int size)
{
	if (value >= 10000)
		snprintf(text, (size_t)size, "%ld,%03ld", value / 1000, value % 1000);
	else
		snprintf(text, (size_t)size, "%ld", value);
}

static void ratio_text(long kills, long deaths, char *text, int size)
{
	snprintf(text, (size_t)size, "%.2f", (double)kills / (double)(deaths > 0 ? deaths : 1));
}

static void time_text(long long seconds, char *text, int size)
{
	if (seconds <= 0)
		snprintf(text, (size_t)size, "-");
	else if (seconds < 3600)
		snprintf(text, (size_t)size, "%lldm", (seconds + 59) / 60);
	else
		snprintf(text, (size_t)size, "%lldh %02lldm", seconds / 3600, (seconds % 3600) / 60);
}

/* a medal's name from a key: the site's (medal_icons.h, the game's own and
the site's awards: "most_kills" is "Most Kills"), else the key's words */
static void medal_text(char const *key, char *text, int size)
{
	int medal = medal_icon_find(key);

	if (medal >= 0)
		snprintf(text, (size_t)size, "%s", medal_icon(medal)->name);
	else
		service_record_title(key, text, size);
}

/* a weapon's family ("sniper rifle") and a map's key as the menus name them */
static void weapon_text(char const *family, char *text, int size)
{
	if (family && family[0])
		service_record_title(family, text, size);
	else
		snprintf(text, (size_t)size, "-");
}

static void map_text(char const *map, char *text, int size)
{
	if (map && map[0])
		service_record_map_title(map, text, size);
	else
		snprintf(text, (size_t)size, "-");
}

static float line(float x, float right, float y, char const *label, char const *value)
{
	ui_style_text(UI_TEXT_LABEL, x, y, UI_ALIGN_LEFT, label);
	ui_overlay_text(UI_FONT_MENU_SMALL, 9.0f, right, y, UI_ALIGN_RIGHT, COLOR_VALUE, value);
	return y + LINE_HEIGHT;
}

static void pane(float x, float y, float width, char const *title, char const *caption)
{
	ui_style_glass(x, y, width, PANE_HEIGHT, 8, ui_style_color(UI_COLOR_PANE), COLOR_PANE_EDGE);
	ui_style_text(UI_TEXT_TITLE, x + 12, y + 8, UI_ALIGN_LEFT, title);
	ui_style_text(UI_TEXT_STATUS, x + 12, y + 25, UI_ALIGN_LEFT, caption);
}

/* the totals both panes show */
static float totals(float x, float right, float y, long games, long wins, long kills, long deaths, long assists,
	long medals, long best_spree, long long seconds)
{
	char value[32];

	number_text(games, value, sizeof(value));
	y = line(x, right, y, "GAMES", value);
	number_text(wins, value, sizeof(value));
	y = line(x, right, y, "WINS", value);
	number_text(kills, value, sizeof(value));
	y = line(x, right, y, "KILLS", value);
	number_text(deaths, value, sizeof(value));
	y = line(x, right, y, "DEATHS", value);
	ratio_text(kills, deaths, value, sizeof(value));
	y = line(x, right, y, "K/D", value);
	number_text(assists, value, sizeof(value));
	y = line(x, right, y, "ASSISTS", value);
	number_text(medals, value, sizeof(value));
	y = line(x, right, y, "MEDALS", value);
	if (best_spree > 0)
		number_text(best_spree, value, sizeof(value));
	else
		snprintf(value, sizeof(value), "-");
	y = line(x, right, y, "BEST SPREE", value);
	time_text(seconds, value, sizeof(value));
	return line(x, right, y, "TIME PLAYED", value);
}

static void render_local(float x, float y, float width)
{
	struct service_record const *record = &guide_record.record;
	char value[64];
	float right = x + width - 12, at = y + LINE_TOP;

	pane(x, y, width, guide_record.name[0] ? guide_record.name : "THIS PROFILE", "ON THIS DEVICE");
	if (guide_record.profile == NONE)
	{
		ui_style_text(UI_TEXT_LINE, x + 12, at, UI_ALIGN_LEFT, "Sign in to a Halo profile first.");
		return;
	}
	at = totals(x + 12, right, at, record->games, record->wins, record->kills, record->deaths, record->assists,
		service_record_medal_total(record), record->best_spree, record->seconds);
	map_text(service_record_favorite_map(record), value, sizeof(value));
	at = line(x + 12, right, at, "FAVORITE MAP", value);
	weapon_text(service_record_favorite_weapon(record), value, sizeof(value));
	line(x + 12, right, at, "FAVORITE WEAPON", value);
	if (!record->games)
		ui_style_text(UI_TEXT_STATUS, x + 12, y + PANE_HEIGHT - 16, UI_ALIGN_LEFT,
			"Finish a multiplayer game to start it.");
}

static void render_site(float x, float y, float width)
{
	struct delta_profile const *site = &guide_record.site;
	char title[64], value[64];
	float right = x + width - 12, at = y + LINE_TOP;
	char const *message = NULL;

	if (site->linked && site->handle[0])
		snprintf(title, sizeof(title), "@%s", site->handle);
	else
		snprintf(title, sizeof(title), "DELTA");
	pane(x, y, width, title, "ON DELTA");
	switch (guide_record.status.state)
	{
	case BROWSER_PROFILE_NO_LIST:
	case BROWSER_PROFILE_NO_KEY:
		message = "Delta isn't set up on this copy.";
		break;
	case BROWSER_PROFILE_FAILED:
		if (!guide_record.status.has_profile)
			message = "Can't reach Delta right now.";
		break;
	case BROWSER_PROFILE_LOADED:
		break;
	default:
		if (!guide_record.status.has_profile)
			message = "Loading\xE2\x80\xA6";
		break;
	}
	if (!message && guide_record.status.has_profile && !site->linked)
		message = "Not signed in. Sign in under FRIENDS.";
	if (message)
	{
		ui_style_text(UI_TEXT_LINE, x + 12, at, UI_ALIGN_LEFT, message);
		return;
	}
	at = totals(x + 12, right, at, site->record_games, site->record_wins, site->record_kills, site->record_deaths,
		site->record_assists, site->record_medals, site->record_best_spree, site->record_seconds);
	map_text(site->record_favorite_map, value, sizeof(value));
	at = line(x + 12, right, at, "FAVORITE MAP", value);
	if (site->record_top_medal_count > 0)
	{
		char name[48];

		medal_text(site->record_top_medals[0].key, name, sizeof(name));
		snprintf(value, sizeof(value), "%s x%d", name, site->record_top_medals[0].count);
	}
	else
		weapon_text(site->record_favorite_weapon, value, sizeof(value));
	line(x + 12, right, at, site->record_top_medal_count > 0 ? "TOP MEDAL" : "FAVORITE WEAPON", value);
	/* (the top medal's icon, before its name) */
	if (site->record_top_medal_count > 0 && ui_overlay_medals_available() &&
		medal_icon_find(site->record_top_medals[0].key) >= 0)
	{
		ui_overlay_medal(medal_icon_find(site->record_top_medals[0].key),
			right - ui_overlay_text_width(UI_FONT_MENU_SMALL, 9.0f, value) - 14, at - 1, 12, 0xFFFFFFFF);
	}
	if (site->hidden)
		ui_style_text(UI_TEXT_STATUS, x + 12, y + PANE_HEIGHT - 16, UI_ALIGN_LEFT, "Hidden from everyone else.");
}

/* the medals earned most on this device */
static void render_medals(float left, float right, float y)
{
	int medals[MEDALS_SHOWN];
	long counts[MEDALS_SHOWN];
	int count = service_record_top_medals(&guide_record.record, medals, counts, MEDALS_SHOWN);
	float x = left + ui_style_text(UI_TEXT_HEADING, left, y + TILE_HEIGHT * 0.5f - 5, UI_ALIGN_LEFT, "TOP MEDALS") + 10;
	float width;
	int index;

	if (!count)
	{
		ui_style_text(UI_TEXT_STATUS, x, y + TILE_HEIGHT * 0.5f - 4, UI_ALIGN_LEFT, "None yet");
		return;
	}
	width = (right - x - (count - 1) * 6) / count;
	if (width > 120)
		width = 120;
	for (index = 0; index < count; index++)
	{
		char text[64];
		int icon = medal_icon_find(medal_rules_key(medals[index]));

		ui_style_glass(x, y, width, TILE_HEIGHT, 6, ui_style_color(UI_COLOR_PANE), COLOR_PANE_EDGE);
		if (icon >= 0 && ui_overlay_medals_available())
		{
			/* its icon, then its name and count, as the site's medal case */
			float text_x = x + 6 + TILE_ICON + 5;

			ui_overlay_medal(icon, x + 5, y + (TILE_HEIGHT - TILE_ICON) * 0.5f, TILE_ICON, 0xFFFFFFFF);
			ui_overlay_text(UI_FONT_MENU_SMALL, 8.5f, text_x, y + 10, UI_ALIGN_LEFT, COLOR_VALUE,
				medal_icon(icon)->name);
			number_text(counts[index], text, sizeof(text));
			ui_overlay_text(UI_FONT_MENU_SMALL, 8.5f, text_x, y + 22, UI_ALIGN_LEFT, COLOR_COUNT, text);
		}
		else
		{
			snprintf(text, sizeof(text), "%s  %ld", medal_rules_name(medals[index]), counts[index]);
			ui_overlay_text(UI_FONT_MENU_SMALL, 8.5f, x + width * 0.5f, y + (TILE_HEIGHT - 8) * 0.5f,
				UI_ALIGN_CENTER, COLOR_VALUE, text);
		}
		x += width + 6;
	}
}

/* ---------- public code */

void guide_record_open(void)
{
	record_load();
	browser_profile_load(0);
	record_site();
}

short guide_record_rows(void)
{
	return 1;
}

void guide_record_activate(void)
{
	config_write_boolean("display.medals", !config_boolean("display.medals"));
}

char const *guide_record_prompt(void)
{
	return "CHANGE";
}

void guide_record_render(float left, float top, float right, float bottom, boolean selected)
{
	unsigned long now = system_milliseconds();
	float width = (right - left - PANE_GAP) * 0.5f;
	float row_y = bottom - ROW_HEIGHT;
	char const *value = config_boolean("display.medals") ? "ON" : "OFF";
	float value_right = right - 14;

	if (!guide_record.loaded || now - guide_record.loaded_time >= RELOAD_INTERVAL ||
		record_profile() != guide_record.profile)
	{
		record_load();
	}
	if (now - guide_record.site_time >= SITE_INTERVAL)
		record_site();
	render_local(left, top, width);
	render_site(left + width + PANE_GAP, top, width);
	render_medals(left, right, top + PANE_HEIGHT + 8);
	/* MEDALS ON HUD, as the Guide's settings are drawn */
	ui_style_row(left, row_y, right - left, ROW_HEIGHT, selected);
	ui_overlay_text(UI_FONT_MENU, 10.0f, left + 12, row_y + 7, UI_ALIGN_LEFT, COLOR_SETTING, "MEDALS ON HUD");
	ui_overlay_text(UI_FONT_MENU, 10.0f, value_right, row_y + 7, UI_ALIGN_RIGHT, COLOR_VALUE, "\xE2\x80\xBA");
	ui_overlay_text(UI_FONT_MENU, 10.0f, value_right - 12, row_y + 7, UI_ALIGN_RIGHT, COLOR_VALUE, value);
	ui_overlay_text(UI_FONT_MENU, 10.0f, value_right - 18 - ui_overlay_text_width(UI_FONT_MENU, 10.0f, value),
		row_y + 7, UI_ALIGN_RIGHT, COLOR_VALUE, "\xE2\x80\xB9");
}

#endif
