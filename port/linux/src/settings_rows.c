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
int platform_display_native_size(long *width, long *height);
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

enum
{
	/* (a row's restart: 1 everywhere, or this) */
	_restart_in_game = 2,
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
	/* a change shows after a restart (_restart_in_game: only once the
	game is running, the Guide's; the setup screen comes before it) */
	int restart;
	/* a change shows at once on the window (platform_display_apply) */
	int window;
};

#ifdef HALO_BGFX
/* (OpenGL is the default; bgfx's are still being tested, and their names
say so, as config.toml's help does) */
#ifdef __APPLE__
static const char *const renderer_choices[] = { "gl", "bgfx", NULL };
static const char *const renderer_names[] = { "OPENGL (DEFAULT)", "METAL (BGFX, TESTING)", NULL };
#elif defined(_WIN32)
/* ("d3d11": bgfx on Direct3D 11, for testing; each falls back to
OpenGL) */
static const char *const renderer_choices[] = { "gl", "bgfx", "d3d11", NULL };
static const char *const renderer_names[] = { "OPENGL (DEFAULT)", "VULKAN (BGFX, TESTING)", "D3D11 (BGFX, TESTING)", NULL };
#else
static const char *const renderer_choices[] = { "gl", "bgfx", NULL };
static const char *const renderer_names[] = { "OPENGL (DEFAULT)", "VULKAN (BGFX, TESTING)", NULL };
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
/* display.look's (port_config.c's config_look_apply) */
static const char *const look_choices[] = { "chupathingy", "xbox", NULL };
/* display.light_effects' (port/linux/game/lighting_config.c) */
static const char *const light_effects_choices[] = { "auto", "high", "low", "off", NULL };
/* display.hdr's (d3d8_gl.c, xgpu_tone_map.h): the paper white and the UI's
white in nits (0 the display's, or the paper white's), the peak in nits (0
the display's) */
static const char *const hdr_white_choices[] = { "0", "80", "100", "120", "160", "200", "250", "300", "400", "500", NULL };
static const char *const hdr_white_names[] = { "AUTO", "80", "100", "120", "160", "200", "250", "300", "400", "500", NULL };
static const char *const hdr_ui_names[] = { "SAME", "80", "100", "120", "160", "200", "250", "300", "400", "500", NULL };
static const char *const hdr_peak_choices[] = { "0", "400", "600", "800", "1000", "1500", "2000", "4000", NULL };
static const char *const hdr_peak_names[] = { "AUTO", "400", "600", "800", "1000", "1500", "2000", "4000", NULL };
#ifdef HALO_HD_BUTTONS
/* the prompts' buttons (ui_overlay.c): input.button_style's, input.button_look's
and input.hud_buttons' */
static const char *const button_style_choices[] = { "auto", "duke", "xbox", "playstation", "switch", NULL };
static const char *const button_look_choices[] = { "gem", "flat", NULL };
static const char *const hud_buttons_choices[] = { "classic", "hd", NULL };
#endif
/* the reticle's size (display.reticle_scale), as Video Setup's View shows it */
static const char *const reticle_size_choices[] = { "0.5", "0.75", "1", "1.25", "1.5", "2", NULL };
static const char *const reticle_size_names[] = { "50%", "75%", "100%", "125%", "150%", "200%", NULL };
/* the crosshair's place (display.crosshair_position) */
static const char *const crosshair_position_choices[] = { "retail", "lowered", NULL };
static const char *const crosshair_position_names[] = { "XBOX", "LOWERED", NULL };
/* clips.seconds' (capture_clips.c keeps 5 to 120) */
static const char *const clip_length_choices[] = { "10", "15", "30", "60", "90", "120", NULL };
static const char *const clip_length_names[] = { "10 SECONDS", "15 SECONDS", "30 SECONDS", "60 SECONDS", "90 SECONDS",
	"120 SECONDS", NULL };
static const char *const theme_names[] = { "HALO BLUE", "DASHBOARD GREEN", "VISOR ORANGE", "LIVE ORANGE", "CHUPATHINGY RED", NULL };

static const struct settings_row settings_rows[] =
{
	{ _in_display, "DISPLAY MODE", "display.mode", _kind_words, NULL, NULL, 0, 1 },
#ifdef HALO_LAUNCHER
	{ _in_display, "RESOLUTION", "display.resolution", _kind_resolutions, NULL, NULL, 0, 1 },
	{ _in_display, "WINDOW SIZE", "display.window_size", _kind_window_sizes, NULL, NULL, 0, 1 },
#endif
	{ _in_display, "V-SYNC", "display.vsync", _kind_toggle, NULL, NULL, 0, 1 },
	{ _in_display, "VARIABLE REFRESH", "display.vrr", _kind_words, NULL, NULL, 0, 1 },
	{ _in_display, "ANTI-ALIASING", "display.anti_aliasing", _kind_words, NULL, NULL, 0, 0 },
	{ _in_display, "HDR", "display.hdr", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_display, "HDR DISPLAY", "display.hdr_output", _kind_toggle, NULL, NULL, 1, 0 },
	{ _in_display, "TONE MAPPING", "display.hdr_tone_mapping", _kind_words, NULL, NULL, 0, 0 },
	{ _in_display, "HDR BRIGHTNESS", "display.hdr_paper_white", _kind_choices, hdr_white_choices, hdr_white_names, 0, 0 },
	{ _in_display, "HDR PEAK", "display.hdr_peak", _kind_choices, hdr_peak_choices, hdr_peak_names, 0, 0 },
	{ _in_display, "HUD BRIGHTNESS", "display.hdr_ui_nits", _kind_choices, hdr_white_choices, hdr_ui_names, 0, 0 },
	/* (below the common rows: OpenGL, the default, is the one most keep) */
#ifdef HALO_BGFX
	{ _in_display, "RENDERER", "display.renderer", _kind_choices, renderer_choices, renderer_names, 1, 0 },
#endif
#if defined(__APPLE__) && !defined(HALO_IOS)
	{ _in_display, "METAL HUD", "display.metal_hud", _kind_toggle, NULL, NULL, 1, 0 },
#endif
	/* (the menus, text and buttons, as Video Setup's INTERFACE orders them;
	in the Guide too: the way back from the Xbox's menus, which have no
	screen of it, #1210) */
	{ _in_display | _in_guide, "MENU STYLE", "display.menus", _kind_choices, menus_choices, NULL, 0, 0 },
#ifdef HALO_TITLE_CHOICE
	{ _in_display, "TITLE FONT", "display.title_font", _kind_choices, title_font_choices, NULL, 0, 0 },
#endif
#ifdef HALO_HD_BUTTONS
	{ _in_display | _in_guide, "BUTTON STYLE", "input.button_style", _kind_choices, button_style_choices, NULL, 0, 0 },
	{ _in_display | _in_guide, "MENU BUTTONS", "input.button_look", _kind_choices, button_look_choices, NULL, 0, 0 },
	{ _in_display | _in_guide, "HUD BUTTONS", "input.hud_buttons", _kind_choices, hud_buttons_choices, NULL, 0, 0 },
#endif
	{ _in_display | _in_guide, "SCOREBOARD ICONS", "display.scoreboard_icons", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_display | _in_guide, "PING METER", "display.ping_meter", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_display | _in_guide, "LOOK", "display.look", _kind_choices, look_choices, NULL, 0, 0 },
	{ _in_display, "PERFORMANCE", "display.particle_limit", _kind_preset, NULL, NULL, 0, 0 },
	{ _in_display, "LIGHT EFFECTS", "display.light_effects", _kind_choices, light_effects_choices, NULL, 0, 0 },
	{ _in_display | _in_guide, "CAMO WEAPON FIX", "display.camo_weapon_fix", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_display | _in_guide, "PLASMA SPARKS", "display.plasma_sparks", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_display | _in_guide, "DEVELOPER FLAMES", "display.developer_flames", _kind_toggle, NULL, NULL, 0, 0 },
#ifdef HALO_PORT_RAGDOLLS
	{ _in_display | _in_guide, "RAGDOLLS", "display.ragdolls", _kind_toggle, NULL, NULL, 0, 0 },
#endif
	{ _in_display | _in_guide, "RETICLE SIZE", "display.reticle_scale", _kind_choices, reticle_size_choices,
	  reticle_size_names, 0, 0 },
	{ _in_display | _in_guide, "CROSSHAIR", "display.crosshair_position", _kind_choices, crosshair_position_choices,
	  crosshair_position_names, 0, 0 },
	{ _in_display | _in_guide, "WEAPON MODEL", "display.first_person_weapon", _kind_toggle, NULL, NULL, 0, 0 },

#ifdef HALO_LAUNCHER
	{ _in_audio, "OUTPUT", "audio.device", _kind_audio_devices, NULL, NULL, 1, 0 },
#endif
	{ _in_audio, "SPEAKERS", "audio.channels", _kind_choices, channels_choices, NULL, 1, 0 },
	{ _in_audio | _in_guide, "LOUDNESS", "audio.loudness", _kind_words, NULL, NULL, 0, 0 },
	{ _in_audio, "HEADPHONE 3D", "audio.hrtf", _kind_choices, hrtf_choices, NULL, 0, 0 },
	{ _in_audio, "REVERB", "audio.reverb", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_audio, "DOPPLER", "audio.doppler", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_audio | _in_guide, "HIT SOUNDS", "audio.hit_sounds", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_audio | _in_guide, "MODS", "game.mods", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_audio, "BUFFER SIZE", "audio.buffer_frames", _kind_choices, buffer_choices, NULL, 1, 0 },
	{ _in_audio, "RESAMPLING", "audio.resampling", _kind_words, NULL, NULL, 0, 0 },

	{ _in_network, "UPNP PORT FORWARDING", "network.allow_upnp", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_network, "PROTOCOL", "network.protocol", _kind_words, NULL, NULL, 0, 0 },
	{ _in_network, "MAP DOWNLOADS", "network.map_downloads", _kind_words, NULL, NULL, 0, 0 },
	/* (the setup screen's help line for it is the disclaimer: launcher.c) */
	{ _in_network, "MAPS FROM HOSTS", "network.map_downloads_from_host", _kind_words, NULL, NULL, 0, 0 },
	{ _in_network, "LIST MY GAMES", "network.list_hosted_games", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_network, "DIRECT IP HOST", "network.direct_ip_host", _kind_toggle, NULL, NULL, 0, 0 },
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
	/* (clips start once, with the game: capture_clips.c) */
	{ _in_guide, "CLIPS", "clips.enabled", _kind_toggle, NULL, NULL, _restart_in_game, 0 },
	{ _in_guide, "CLIP LENGTH", "clips.seconds", _kind_choices, clip_length_choices, clip_length_names,
	  _restart_in_game, 0 },
	{ _in_guide, "MISSION ORDER", "game.mcc_campaigns", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_guide, "FORGE", "game.forge", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_guide, "GUIDE BUTTON", "controls.delta_button", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_guide, "CONFIRM BUTTON", "delta.confirm_button", _kind_words, NULL, NULL, 0, 0 },
	{ _in_guide, "RUMBLE", "delta.rumble", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_guide, "SHOW IN MENUS", "delta.menu_item", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_guide, "NOTIFICATIONS IN GAME", "delta.notify_in_game", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_guide, "FRIEND ONLINE ALERTS", "delta.notify_online", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_network | _in_guide, "SHOW WHEN I'M PLAYING", "network.share_presence", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_display | _in_guide, "THEME", "display.theme", _kind_choices, theme_choices, theme_names, 0, 0 },
	{ _in_network, "JOIN SCREEN", "display.join_screen", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_guide, "BACKGROUND", "delta.guide_background", _kind_choices, background_choices, background_names, 0, 0 },
	{ _in_network, "DISCORD", "discord.presence", _kind_words, NULL, NULL, 0, 0 },
	{ _in_network, "CRASH REPORTS", "crash_reports.upload", _kind_words, NULL, NULL, 0, 0 },

	{ _in_updates, "CHECK FOR UPDATES", "update.auto", _kind_toggle, NULL, NULL, 0, 0 },
	{ _in_updates, "CHANNEL", "update.channel", _kind_words, NULL, NULL, 0, 0 },

};

#define NUMBER_OF_SETTINGS_ROWS ((int)(sizeof(settings_rows) / sizeof(settings_rows[0])))

/* the Potato preset: the display's heaviest work turned down. It is
config.toml's tuning.preset = "potato", whose knobs and values are
port_config.c's (config_tuning_knob, config_tuning_preset_value) */
#define POTATO_PRESET "potato"

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

		/* (Native with the display's own size, so a display larger than
		the sizes listed shows as larger) */
		if (row->kind == _kind_resolutions)
		{
			long native_width, native_height;

			if (platform_display_native_size(&native_width, &native_height))
				snprintf(size, sizeof(size), "NATIVE %ldx%ld", native_width, native_height);
			else
				snprintf(size, sizeof(size), "NATIVE");
			values_add(values, "native", size);
		}
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

/* the Potato preset's knob at index that this build has: its value, NULL
for one it leaves alone, and its setting in key */
static const char *potato_value(int index, const char **key)
{
	*key = config_tuning_knob(index);
	if (!*key || !**key || config_setting_kind(*key) < 0)
		return NULL;
	return config_tuning_preset_value(POTATO_PRESET, *key);
}

static int preset_now(void)
{
	char value[VALUE_SIZE], fallback[VALUE_SIZE];
	int index, potato_all = 1, defaults_all = 1;
	const char *key, *potato, *look;

	for (index = 0; config_tuning_knob(index); index++)
	{
		if (!(potato = potato_value(index, &key)))
			continue;
		current(key, value, sizeof(value));
		if (!same(value, potato))
			potato_all = 0;
		/* (a knob the look sets has the look's value as its default:
		display.look's Xbox shadows are not a preset of their own) */
		look = config_look_value(key);
		if (look)
			snprintf(fallback, sizeof(fallback), "%s", look);
		else if (!config_default(key, fallback, sizeof(fallback)))
			fallback[0] = '\0';
		if (!same(value, fallback))
			defaults_all = 0;
	}
	return defaults_all ? _preset_default : potato_all ? _preset_potato : _preset_custom;
}

/* Potato: tuning.preset written, and its knobs back to following it;
Default: the preset and its knobs back to their defaults */
static void preset_set(int preset)
{
	int index;
	const char *key;

	if (preset == _preset_potato)
		config_write("tuning.preset", POTATO_PRESET);
	else
		config_reset("tuning.preset");
	for (index = 0; config_tuning_knob(index); index++)
	{
		if (potato_value(index, &key))
			config_reset(key);
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

/* whether the game shows in a window (display.mode, else display.fullscreen) */
static int display_windowed(void)
{
	char mode[VALUE_SIZE];

	current("display.mode", mode, sizeof(mode));
	if (!mode[0])
		return !config_boolean("display.fullscreen");
	return same(mode, "windowed");
}

/* whether a row shows in this build now: its key this build's, and the
rows that depend on something only where it is */
static int row_shown(int row)
{
	if (config_setting_kind(settings_rows[row].key) < 0)
		return 0;
	/* (Steam's link: where the built-in Steam download is, and something opens it) */
	if ((settings_rows[row].kind == _kind_steam_link || settings_rows[row].kind == _kind_folder) &&
		(!steam_link_available() || !steam_link_action))
	{
		return 0;
	}
	/* (Workshop maps: where the Guide has WORKSHOP, MCC on this computer) */
	if (!workshop_offered && !strcmp(settings_rows[row].key, "game.workshop_maps"))
		return 0;
	/* (Resolution and Window Size in one place, as Video Setup has them:
	Window Size in a window, where Resolution does nothing, and
	Resolution otherwise) */
	if ((settings_rows[row].kind == _kind_window_sizes && !display_windowed()) ||
		(settings_rows[row].kind == _kind_resolutions && display_windowed()))
	{
		return 0;
	}
	return 1;
}

const struct settings_row *settings_row_at(int group, int index)
{
	int row;

	for (row = 0; row < NUMBER_OF_SETTINGS_ROWS; row++)
	{
		if (!(settings_rows[row].groups & (1u << group)) || !row_shown(row))
			continue;
		if (!index--)
			return &settings_rows[row];
	}
	return NULL;
}

const struct settings_row *settings_row_for_key(const char *key)
{
	int row;

	for (row = 0; row < NUMBER_OF_SETTINGS_ROWS; row++)
	{
		if (!strcmp(settings_rows[row].key, key) && row_shown(row))
			return &settings_rows[row];
	}
	return NULL;
}

/* ---------- the Guide's SETTINGS pages (settings_rows.h) */

static const char *const page_names[NUMBER_OF_SETTINGS_PAGES] =
{
	"DISPLAY", "SOUND", "GAME", "GUIDE", "ABOUT",
};

static const char *const display_lines[] =
{
	"#MENUS", "display.menus", "display.theme", "delta.guide_background",
	"#BUTTONS", "input.button_style", "input.button_look", "input.hud_buttons",
	"#HUD", "display.reticle_scale", "display.crosshair_position", "display.ping_meter", "display.scoreboard_icons",
	"#EFFECTS", "display.first_person_weapon", "display.camo_weapon_fix", "display.plasma_sparks", "display.ragdolls",
	"display.developer_flames",
	NULL
};
static const char *const sound_lines[] =
{
	"audio.hit_sounds",
	"#VOICE", "audio.voice_chat", "audio.voice_channel", "audio.voice_positional", "display.voice_speakers",
	"#VOLUME", "audio.voice_volume", "audio.voice_volume_party", "audio.voice_volume_team",
	"audio.voice_volume_proximity", "audio.voice_volume_everyone",
	NULL
};
static const char *const game_lines[] =
{
	"game.forge", "game.mods",
	"#CAPTURE", "clips.enabled", "clips.seconds",
	"#WORKSHOP", "game.workshop_steam", "paths.steam_downloads", "game.workshop_maps", "network.workshop_downloads",
	"game.mcc_campaigns",
	NULL
};
static const char *const guide_lines[] =
{
	"#OPENING", "controls.delta", "controls.delta_button", "delta.menu_item",
	"#CONTROLLER", "delta.confirm_button", "delta.rumble",
	"#NOTIFICATIONS", "delta.notify_in_game", "delta.notify_online",
	"#PRIVACY", "network.share_presence",
	"#DEVICES", "@devices", "@link",
	NULL
};
static const char *const about_lines[] =
{
	"#CHUPATHINGY", "=version", "=platform", "@setup", "@about",
	"#LOGS", "@logs0", "@logs1",
	NULL
};
static const char *const *const page_lines[NUMBER_OF_SETTINGS_PAGES] =
{
	display_lines, sound_lines, game_lines, guide_lines, about_lines,
};

const char *const *settings_page_names(void)
{
	return page_names;
}

const char *const *settings_page_lines(int page)
{
	return page >= 0 && page < NUMBER_OF_SETTINGS_PAGES ? page_lines[page] : NULL;
}

int settings_key_page(const char *key)
{
	int page;
	const char *const *line;

	for (page = 0; page < NUMBER_OF_SETTINGS_PAGES; page++)
	{
		for (line = page_lines[page]; *line; line++)
		{
			if (!strcmp(*line, key))
				return page;
		}
	}
	return -1;
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
	return row->restart == 1;
}

int settings_row_restart_in_game(const struct settings_row *row)
{
	return row->restart != 0;
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
