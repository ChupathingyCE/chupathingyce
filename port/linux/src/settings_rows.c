/*
SETTINGS_ROWS.C

The settings rows shared by the setup screen's Settings and the Guide's
SETTINGS (settings_rows.h). Each row is a config.toml key: its values are
the ones port_config.c checks the file against (config_setting_word) or a
list of its own, and a change is written with config_write, a reset with
config_reset (the key follows the version's default again). The
Performance row is a preset: Potato sets the heavy display settings low,
Default puts them back.
*/

#include "halo_console_features.h"
#include "launcher.h"

#if defined(HALO_GUIDE) || defined(HALO_LAUNCHER)

#include "settings_rows.h"
#include "steam_client_bridge.h"
#include "workshop_decisions.h"
#include "port_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HALO_LAUNCHER
/* (sdl_platform.c's and dsound_sdl.c's: the desktop's) */
int platform_display_resolutions(long *widths, long *heights, int maximum);
int platform_window_sizes(long *widths, long *heights, int maximum);
int platform_audio_device_names(const char **names, int maximum);
void platform_display_apply(void);
#endif

enum
{
	/* on or off */
	_kind_toggle,
	/* the words config.toml takes (config_setting_word) */
	_kind_words,
	/* the row's own list */
	_kind_choices,
	/* "native", then the display's resolutions */
	_kind_resolutions,
	/* the window's sizes ("" is display.window_scale's) */
	_kind_window_sizes,
	/* "default", then the system's outputs */
	_kind_audio_devices,
	/* the Performance preset */
	_kind_preset,
	/* Steam's link (steam_link_*): the Steam name, or NOT LINKED; a change
	opens the Guide's Workshop on its link row (LINK's QR, UNLINK) */
	_kind_steam_link,
	/* a folder: shown, changed in config.toml ("" the data folder's own) */
	_kind_folder,
};

enum
{
	/* (a row in more than one group) */
	_in_display = 1 << SETTINGS_GROUP_DISPLAY,
	_in_audio = 1 << SETTINGS_GROUP_AUDIO,
	_in_network = 1 << SETTINGS_GROUP_NETWORK,
	_in_updates = 1 << SETTINGS_GROUP_UPDATES,
	_in_guide = 1 << SETTINGS_GROUP_GUIDE,
};

struct settings_row
{
	unsigned groups;
	const char *label;
	const char *key;
	int kind;
	/* _kind_choices': the values, NULL ended, and their names (NULL: the
	value in capitals) */
	const char *const *choices;
	const char *const *names;
	/* a change shows after a restart */
	int restart;
	/* a change shows at once on the window (platform_display_apply) */
	int window;
};

#ifdef HALO_BGFX
#ifdef __APPLE__
static const char *const renderer_choices[] = { "gl", "bgfx", NULL };
static const char *const renderer_names[] = { "OPENGL", "METAL", NULL };
#elif defined(_WIN32)
/* ("d3d11": bgfx on Direct3D 11, for testing; each falls back to
OpenGL) */
static const char *const renderer_choices[] = { "gl", "bgfx", "d3d11", NULL };
static const char *const renderer_names[] = { "OPENGL", "VULKAN", "DIRECT3D 11", NULL };
#else
static const char *const renderer_choices[] = { "gl", "bgfx", NULL };
static const char *const renderer_names[] = { "OPENGL", "VULKAN", NULL };
#endif
#endif
/* (Gearbox's menus, "gearbox" or the older "pc", are no longer one: such a
value is the Xbox's, menu_tags.c's menus_chosen) */
static const char *const menus_choices[] = { "chupathingy", "xbox", NULL };
/* display.title_font's (hud_hires.c, menu_files.c, ui_overlay.c) */
static const char *const title_font_choices[] = { "classic", "cyborg", "halo", NULL };
static const char *const channels_choices[] = { "auto", "stereo", "5.1", "7.1", NULL };
static const char *const hrtf_choices[] = { "on", "off", "auto", NULL };
static const char *const buffer_choices[] = { "256", "512", "1024", "2048", "4096", NULL };
/* controls.delta's: the F keys the game leaves free (F1 is Back, F2 Start,
F11 full screen, F12 the mouse) */
/* (F9 takes a screenshot and F10 a clip: capture.c) */
#ifdef __APPLE__
/* (a Mac's: F3 is Mission Control unless fn is held, so the backslash key
too, the default) */
static const char *const guide_key_choices[] = { "F3, Backslash", "Backslash", "F3", "F4", "F5", "F6", "F7", "F8", NULL };
#else
static const char *const guide_key_choices[] = { "F3", "F4", "F5", "F6", "F7", "F8", NULL };
#endif
/* display.theme's: the Guide's and the setup screen's colors (ui_style_theme) */
/* (in the order the row shows them; the words, not the order, are saved) */
static const char *const theme_choices[] = { "blue", "green", "orange", "live", "red", NULL };
/* delta.guide_background's: what the Guide draws behind its panel (auto:
the cellwall with Dashboard Green, the shade with the others) */
static const char *const background_choices[] = { "auto", "plain", "cellwall", NULL };
static const char *const background_names[] = { "AUTO", "PLAIN", "CELLWALL", NULL };
/* display.light_effects' (port/linux/game/lighting_config.c) */
static const char *const light_effects_choices[] = { "auto", "high", "low", "off", NULL };
static const char *const theme_names[] = { "HALO BLUE", "DASHBOARD GREEN", "VISOR ORANGE", "LIVE ORANGE", "CHUPATHINGY RED", NULL };

static const struct settings_row settings_rows[] =
{
#ifdef HALO_BGFX
	{ _in_display, "RENDERER", "display.renderer", _kind_choices, renderer_choices, renderer_names, 1, 0 },
#endif
	{ _in_display, "DISPLAY MODE", "display.mode", _kind_words, NULL, NULL, 0, 1 },
#ifdef HALO_LAUNCHER
	{ _in_display, "RESOLUTION", "display.resolution", _kind_resolutions, NULL, NULL, 0, 1 },
	{ _in_display, "WINDOW SIZE", "display.window_size", _kind_window_sizes, NULL, NULL, 0, 1 },
#endif
	{ _in_display, "V-SYNC", "display.vsync", _kind_toggle, NULL, NULL, 0, 1 },
	{ _in_display, "VARIABLE REFRESH", "display.vrr", _kind_words, NULL, NULL, 0, 1 },
	{ _in_display, "ANTI-ALIASING", "display.anti_aliasing", _kind_words, NULL, NULL, 0, 0 },
	{ _in_display, "MENU STYLE", "display.menus", _kind_choices, menus_choices, NULL, 0, 0 },
#ifdef HALO_TITLE_CHOICE
	{ _in_display, "TITLE FONT", "display.title_font", _kind_choices, title_font_choices, NULL, 0, 0 },
#endif
	{ _in_display, "PERFORMANCE", "display.particle_limit", _kind_preset, NULL, NULL, 0, 0 },
	{ _in_display, "LIGHT EFFECTS", "display.light_effects", _kind_choices, light_effects_choices, NULL, 0, 0 },

#ifdef HALO_LAUNCHER
	{ _in_audio, "OUTPUT", "audio.device", _kind_audio_devices, NULL, NULL, 1, 0 },
#endif
	{ _in_audio, "SPEAKERS", "audio.channels", _kind_choices, channels_choices, NULL, 1, 0 },
	{ _in_audio, "HEADPHONE 3D", "audio.hrtf", _kind_choices, hrtf_choices, NULL, 0, 0 },
	{ _in_audio, "REVERB", "audio.reverb", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_audio, "DOPPLER", "audio.doppler", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_audio, "BUFFER SIZE", "audio.buffer_frames", _kind_choices, buffer_choices, NULL, 1, 0 },
	{ _in_audio, "RESAMPLING", "audio.resampling", _kind_words, NULL, NULL, 0, 0 },

	{ _in_network, "UPNP PORT FORWARDING", "network.allow_upnp", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_network, "PROTOCOL", "network.protocol", _kind_words, NULL, NULL, 0, 0 },
	{ _in_network, "MAP DOWNLOADS", "network.map_downloads", _kind_words, NULL, NULL, 0, 0 },
	/* (the setup screen's help line for it is the disclaimer: launcher.c) */
	{ _in_network, "MAPS FROM HOSTS", "network.map_downloads_from_host", _kind_words, NULL, NULL, 0, 0 },
	{ _in_network, "LIST MY GAMES", "network.list_hosted_games", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_network, "HOSTED GAME STATS", "network.report_hosted_games", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_network, "JOINED GAME STATS", "network.report_joined_games", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_network, "DELTA STATS", "network.report_events", _kind_toggle, NULL, NULL, 0, 0 },
	/* (the Guide's own, then the one it shares with Network) */
	{ _in_guide, "OPEN KEY", "controls.delta", _kind_choices, guide_key_choices, NULL, 0, 0 },
	/* (the Workshop's: Steam's link, where items go, a join's missing map) */
	{ _in_network | _in_guide, "STEAM ACCOUNT", "game.workshop_steam", _kind_steam_link, NULL, NULL, 0, 0 },
	{ _in_guide, "WORKSHOP FOLDER", "paths.steam_downloads", _kind_folder, NULL, NULL, 0, 0 },
	/* (the Workshop's maps in the map lists: with the Workshop's own items
	always listed (workshop_decisions.h), only the maps in Steam's own MCC
	folders, and named so: settings_row_label) */
	{ _in_guide, "WORKSHOP MAPS", "game.workshop_maps", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_network | _in_guide, "MAPS WHEN JOINING", "network.workshop_downloads", _kind_words, NULL, NULL, 0, 0 },
	{ _in_guide, "MISSION ORDER", "game.mcc_campaigns", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_guide, "GUIDE BUTTON", "controls.delta_button", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_guide, "SHOW IN MENUS", "delta.menu_item", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_guide, "NOTIFICATIONS IN GAME", "delta.notify_in_game", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_guide, "FRIEND ONLINE ALERTS", "delta.notify_online", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_network | _in_guide, "SHOW WHEN I'M PLAYING", "network.share_presence", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_display | _in_guide, "SCOREBOARD ICONS", "display.scoreboard_icons", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_display | _in_guide, "THEME", "display.theme", _kind_choices, theme_choices, theme_names, 0, 0 },
	{ _in_guide, "BACKGROUND", "delta.guide_background", _kind_choices, background_choices, background_names, 0, 0 },
	{ _in_network, "DISCORD", "discord.presence", _kind_words, NULL, NULL, 0, 0 },
	{ _in_network, "CRASH REPORTS", "crash_reports.upload", _kind_words, NULL, NULL, 0, 0 },

	{ _in_updates, "CHECK FOR UPDATES", "update.auto", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_updates, "CHANNEL", "update.channel", _kind_words, NULL, NULL, 0, 0 },

};

#define NUMBER_OF_SETTINGS_ROWS ((int)(sizeof(settings_rows) / sizeof(settings_rows[0])))

/* the Potato preset: the display's heaviest work turned down */
static const struct
{
	const char *key;
	const char *value;
} potato[] =
{
	{ "display.particle_limit", "1024" },
	{ "display.particle_ticks", "true" },
	{ "display.dynamic_light_limit", "8" },
	{ "display.specular_light_limit", "2" },
	{ "display.shadow_resolution", "128" },
	{ "display.anisotropic_filtering", "0" },
	{ "display.anti_aliasing", "off" },
	{ "display.per_pixel_lighting", "false" },
	{ "display.light_effects", "off" },
	{ "display.resolution_scaling", "original" },
};

#define NUMBER_OF_POTATO_SETTINGS ((int)(sizeof(potato) / sizeof(potato[0])))

enum
{
	_preset_default,
	_preset_potato,
	_preset_custom,
};

/* ---------- words */

/* a value as a row shows it: in capitals, "_" a space */
static void shown(const char *value, char *text, int size)
{
	int index;

	for (index = 0; value[index] && index + 1 < size; index++)
		text[index] = value[index] == '_' ? ' ' : (char)toupper((unsigned char)value[index]);
	if (size > 0)
		text[index < size ? index : size - 1] = '\0';
}

static int same(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
	{
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return 0;
	}
	return *a == *b;
}

static void current(const char *key, char *text, int size)
{
	if (!config_text(key, text, (size_t)size))
		text[0] = '\0';
}

/* ---------- a row's values: the count, and the one at index (value and
name), from its kind */

enum
{
	MAXIMUM_VALUES = 48,
	VALUE_SIZE = 96,
};

struct values
{
	int count;
	char value[MAXIMUM_VALUES][VALUE_SIZE];
	char name[MAXIMUM_VALUES][VALUE_SIZE];
};

static void values_add(struct values *values, const char *value, const char *name)
{
	if (values->count >= MAXIMUM_VALUES)
		return;
	snprintf(values->value[values->count], VALUE_SIZE, "%s", value);
	if (name)
		snprintf(values->name[values->count], VALUE_SIZE, "%s", name);
	else
		shown(value, values->name[values->count], VALUE_SIZE);
	values->count++;
}

static void values_of(const struct settings_row *row, struct values *values)
{
	int index;

	values->count = 0;
	switch (row->kind)
	{
	case _kind_toggle:
		values_add(values, "false", "OFF");
		values_add(values, "true", "ON");
		break;
	case _kind_words:
	{
		const char *word;

		for (index = 0; (word = config_setting_word(row->key, index)) != NULL; index++)
		{
			/* (display.mode's empty word follows display.fullscreen: not
			one to pick) */
			if (word[0])
				values_add(values, word, NULL);
		}
		break;
	}
	case _kind_choices:
		for (index = 0; row->choices[index]; index++)
			values_add(values, row->choices[index], row->names ? row->names[index] : NULL);
		break;
#ifdef HALO_LAUNCHER
	case _kind_resolutions:
	case _kind_window_sizes:
	{
		long widths[MAXIMUM_VALUES], heights[MAXIMUM_VALUES];
		int count = row->kind == _kind_resolutions ?
			platform_display_resolutions(widths, heights, MAXIMUM_VALUES - 1) :
			platform_window_sizes(widths, heights, MAXIMUM_VALUES - 1);
		char size[32];

		if (row->kind == _kind_resolutions)
			values_add(values, "native", "NATIVE");
		for (index = 0; index < count; index++)
		{
			snprintf(size, sizeof(size), "%ldx%ld", widths[index], heights[index]);
			values_add(values, size, size);
		}
		break;
	}
	case _kind_audio_devices:
	{
		const char *names[MAXIMUM_VALUES];
		int count = platform_audio_device_names(names, MAXIMUM_VALUES - 1);

		values_add(values, "default", "SYSTEM DEFAULT");
		for (index = 0; index < count; index++)
			values_add(values, names[index], names[index]);
		break;
	}
#endif
	default:
		break;
	}
}

/* ---------- the preset */

static int preset_now(void)
{
	char value[VALUE_SIZE], fallback[VALUE_SIZE];
	int index, potato_all = 1, defaults_all = 1;

	for (index = 0; index < NUMBER_OF_POTATO_SETTINGS; index++)
	{
		if (config_setting_kind(potato[index].key) < 0)
			continue;
		current(potato[index].key, value, sizeof(value));
		if (!same(value, potato[index].value))
			potato_all = 0;
		if (!config_default(potato[index].key, fallback, sizeof(fallback)) || !same(value, fallback))
			defaults_all = 0;
	}
	return defaults_all ? _preset_default : potato_all ? _preset_potato : _preset_custom;
}

static void preset_set(int preset)
{
	int index;

	for (index = 0; index < NUMBER_OF_POTATO_SETTINGS; index++)
	{
		if (config_setting_kind(potato[index].key) < 0)
			continue;
		if (preset == _preset_potato)
			config_write(potato[index].key, potato[index].value);
		else
			config_reset(potato[index].key);
	}
}

/* ---------- public code */

static void (*steam_link_action)(void);
static int workshop_offered;

void settings_rows_set_steam_link_action(void (*action)(void))
{
	steam_link_action = action;
}

void settings_rows_set_workshop_offered(int offered)
{
	workshop_offered = offered;
}

const struct settings_row *settings_row_at(int group, int index)
{
	int row;

	for (row = 0; row < NUMBER_OF_SETTINGS_ROWS; row++)
	{
		if (!(settings_rows[row].groups & (1u << group)) || config_setting_kind(settings_rows[row].key) < 0)
			continue;
		/* (Steam's link: where the built-in Steam download is, and something opens it) */
		if ((settings_rows[row].kind == _kind_steam_link || settings_rows[row].kind == _kind_folder) &&
			(!steam_link_available() || !steam_link_action))
		{
			continue;
		}
		/* (Workshop maps: where the Guide has WORKSHOP, MCC on this computer) */
		if (!workshop_offered && !strcmp(settings_rows[row].key, "game.workshop_maps"))
			continue;
		if (!index--)
			return &settings_rows[row];
	}
	return NULL;
}

const char *settings_row_label(const struct settings_row *row)
{
	if (WORKSHOP_OWN_ITEMS_ALWAYS_LISTED && !strcmp(row->key, "game.workshop_maps"))
		return "STEAM'S MCC MAPS";
	return row->label;
}

int settings_row_arrows(const struct settings_row *row)
{
	/* (STEAM ACCOUNT and WORKSHOP FOLDER open something: no values to step through) */
	return row->kind != _kind_steam_link && row->kind != _kind_folder;
}

static void (*folder_action)(void);

void settings_rows_set_folder_action(void (*action)(void))
{
	folder_action = action;
}

const char *settings_row_key(const struct settings_row *row)
{
	return row->key;
}

int settings_row_restart(const struct settings_row *row)
{
	return row->restart;
}

void settings_row_value(const struct settings_row *row, char *text, int size)
{
	static const char *const preset_names[] = { "DEFAULT", "POTATO", "CUSTOM" };
	static struct values values;
	char value[VALUE_SIZE];
	int index;

	if (row->kind == _kind_preset)
	{
		snprintf(text, (size_t)size, "%s", preset_names[preset_now()]);
		return;
	}
	if (row->kind == _kind_steam_link)
	{
		char persona[64];

		char error[24];
		int link = steam_link_poll(NULL, 0, error, sizeof(error));

		/* (a word for where it stands, never an error's: the sign-in window
		says what went wrong) */
		if (link == STEAM_LINK_STARTING || link == STEAM_LINK_WAITING || link == STEAM_LINK_SCANNED)
			snprintf(text, (size_t)size, "LINKING");
		else if (!steam_link_linked(persona, sizeof(persona)))
			snprintf(text, (size_t)size, "NOT LINKED");
		else if (persona[0])
			snprintf(text, (size_t)size, "LINKED (%s)", persona);
		else
			snprintf(text, (size_t)size, "LINKED");
		return;
	}
	if (row->kind == _kind_folder)
	{
		const char *folder = config_string(row->key);

		snprintf(text, (size_t)size, "%s", folder && folder[0] ? folder : "DATA FOLDER");
		return;
	}
	current(row->key, value, sizeof(value));
	/* (empty: what the setting does without a value) */
	if (!value[0] && !strcmp(row->key, "display.mode"))
		snprintf(value, sizeof(value), "%s", config_boolean("display.fullscreen") ? "borderless" : "windowed");
	if (!value[0] && !strcmp(row->key, "display.window_size"))
	{
		long scale = config_integer("display.window_scale");

		scale = scale < 1 ? 1 : scale;
		snprintf(value, sizeof(value), "%ldx%ld", 640 * scale, 480 * scale);
	}
	values_of(row, &values);
	for (index = 0; index < values.count; index++)
	{
		if (same(values.value[index], value))
		{
			snprintf(text, (size_t)size, "%s", values.name[index]);
			return;
		}
	}
	/* (display.menus' retired "gearbox" and "pc" are the Xbox's) */
	if (!strcmp(row->key, "display.menus") && (same(value, "gearbox") || same(value, "pc")))
	{
		snprintf(text, (size_t)size, "XBOX");
		return;
	}
	shown(value, text, size);
}

void settings_row_change(const struct settings_row *row, int step)
{
	static struct values values;
	char value[VALUE_SIZE];
	int index, found = -1;

	if (row->kind == _kind_steam_link)
	{
		if (steam_link_action)
			steam_link_action();
		return;
	}
	if (row->kind == _kind_folder)
	{
		if (folder_action)
			folder_action();
		return;
	}
	if (row->kind == _kind_preset)
	{
		/* (Default and Potato: from Custom, a step goes to one of them) */
		int preset = preset_now();

		preset_set(preset == _preset_potato ? _preset_default : _preset_potato);
		return;
	}
	values_of(row, &values);
	if (!values.count)
		return;
	current(row->key, value, sizeof(value));
	if (!strcmp(row->key, "display.menus") && (same(value, "gearbox") || same(value, "pc")))
		snprintf(value, sizeof(value), "xbox");
	for (index = 0; index < values.count; index++)
	{
		if (same(values.value[index], value))
			found = index;
	}
	if (found < 0)
		found = step > 0 ? -1 : 0;
	index = ((found + step) % values.count + values.count) % values.count;
	config_write(row->key, values.value[index]);
#ifdef HALO_LAUNCHER
	if (row->window)
		platform_display_apply();
#endif
}

void settings_row_reset(const struct settings_row *row)
{
	if (row->kind == _kind_steam_link)
		return;
	if (row->kind == _kind_preset)
		preset_set(_preset_default);
	else
		config_reset(row->key);
#ifdef HALO_LAUNCHER
	if (row->window)
		platform_display_apply();
#endif
}

void settings_key_value(const char *key, char *text, int size)
{
	char value[VALUE_SIZE];

	current(key, value, sizeof(value));
	if (config_setting_kind(key) == CONFIG_KIND_BOOLEAN)
		snprintf(text, (size_t)size, "%s", config_boolean(key) ? "ON" : "OFF");
	else if (config_setting_kind(key) == CONFIG_KIND_STRING && !config_setting_word(key, 0))
		snprintf(text, (size_t)size, "%s", value);
	else
		shown(value, text, size);
}

int settings_key_change(const char *key, int step)
{
	char value[VALUE_SIZE];
	const char *word;
	int index, count, found = -1;

	switch (config_setting_kind(key))
	{
	case CONFIG_KIND_BOOLEAN:
		return config_write_boolean(key, !config_boolean(key));
	case CONFIG_KIND_INTEGER:
		snprintf(value, sizeof(value), "%ld", config_integer(key) + step);
		return config_write(key, value);
	case CONFIG_KIND_REAL:
	{
		char fallback[VALUE_SIZE];
		double base = config_default(key, fallback, sizeof(fallback)) ? strtod(fallback, NULL) : 0.0;
		double amount = base >= -2.0 && base <= 2.0 ? 0.05 : 1.0;
		double next = config_real(key) + amount * step;

		/* (no -0.00 from the steps adding up) */
		if (next > -amount * 0.5 && next < amount * 0.5)
			next = 0.0;
		snprintf(value, sizeof(value), "%.2f", next);
		return config_write(key, value);
	}
	case CONFIG_KIND_STRING:
		current(key, value, sizeof(value));
		for (count = 0; (word = config_setting_word(key, count)) != NULL; count++)
		{
			if (same(word, value))
				found = count;
		}
		if (!count)
			return 0;
		index = ((found + step) % count + count) % count;
		return config_write(key, config_setting_word(key, index));
	default:
		return 0;
	}
}

#endif
