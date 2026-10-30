/*
PORT_CONFIG.C

The native ports' settings (port_config.h), parsed with tomlc17
(port/third_party/tomlc17). Every setting is in the table below with its
type, default, the HALO_* environment variable that overrides it and the
comment written into a new file. The file is read once, on the first
question; unknown keys and values of the wrong type are reported in the log
and the defaults used instead. A new file has every setting commented out at
its default, so that a later version's default reaches it; a setting is
written as a value only when the player chooses one (config_write). Once
the file exists only those lines, the settings a newer version adds and an
older file's layout (config_version) are changed in it, so that the player's
edits and comments stay.

The original Xbox builds this file too (warthog-xbox's port/xbox/build.py, HALO_XBOX_CONSOLE):
its D:\config.toml beside default.xbe has the settings marked for it, read
and written with the C runtime's files in place of SDL's; it has no
environment, and a setting not marked for it reads as empty, 0 or false, as
it did before the console had the file.
*/

#include "platform.h"
#include "port_config.h"
#include "halo_port_limits.h"
#include "posix.h"
#if !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
#include "packaged_install.h"
#endif

#include <time.h>
#include "tomlc17.h"

#ifndef HALO_XBOX_CONSOLE
#include <SDL3/SDL.h>
#endif
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>

#ifdef HALO_SWITCH_CONSOLE
/* the guest's write is not on the card until the host commits it
(port/switch/host/host_main.c) */
void host_storage_commit(void);
#endif
#endif

#ifdef HALO_XBOX_CONSOLE
/* (the console's C runtime's) */
#define SDL_strncasecmp _strnicmp
#endif

/* ---------- the settings */

enum config_type
{
	_config_boolean,
	_config_integer,
	_config_real,
	_config_string,
};

/* how the setting's environment variable sets it */
enum config_environment
{
	/* the variable's text is the value ("0", "false", "no" and "off" are
	false for a boolean) */
	_environment_value,
	/* the variable being set makes it true */
	_environment_set_is_true,
	/* the variable being set makes it false */
	_environment_set_is_false,
};

/* the builds a setting means something in, and is written for */
enum
{
	_platform_desktop = 1,
	_platform_android = 2,
	_platform_all = _platform_desktop | _platform_android,
	/* (of the desktop builds, only Windows) */
	_platform_windows = 4,
	/* the original Xbox (port/xbox): not in _platform_all, each of its
	settings marked for it */
	_platform_xbox = 8,
	/* the consoles on the shared GL layer (HALO_CONSOLE_GL_LAYER: the Xbox
	360, the Wii U), as well as their desktop or Windows settings: the
	console settings they share with the original Xbox */
	_platform_console_gl = 16,
};

struct config_setting
{
	const char *name;
	enum config_type type;
	/* as it is written in the file */
	const char *default_value;
	/* NULL for a setting only the console has */
	const char *environment;
	enum config_environment environment_style;
	unsigned platforms;
	const char *comment;
};

/* macOS starts in a window, as its games do */
#ifdef __APPLE__
#define DEFAULT_FULLSCREEN "false"
#else
#define DEFAULT_FULLSCREEN "true"
#endif

/* macOS's audio cuts out with SDL's 512-frame buffer when the mixer is late
(a 10.6 ms budget at 48 kHz); 2048 frames (43 ms) rides that out. Android's
mixer runs on a phone's cores, often a slower one, so it has 1024 (21 ms).
Elsewhere 512 keeps the latency low. */
#ifdef __APPLE__
#define DEFAULT_AUDIO_BUFFER_FRAMES "2048"
#elif defined(HALO_ANDROID)
#define DEFAULT_AUDIO_BUFFER_FRAMES "1024"
#else
#define DEFAULT_AUDIO_BUFFER_FRAMES "512"
#endif

/* Headphone 3D (dsound_sdl.c's binaural rendering), on as the original Xbox
had it: its DirectSound filtered every 3D voice through its HRTFs, on TV
stereo too (DirectSoundUseFullHRTF). Surround output ignores it. A
console's build keeps it off: the original Xbox has its own hardware path,
and the others have not been tried with it. */
#ifdef HALO_CONSOLE
#define DEFAULT_AUDIO_HRTF "\"off\""
#else
#define DEFAULT_AUDIO_HRTF "\"on\""
#endif

/* The 3D view's anti-aliasing and the objects' shadow maps: the sharpest
picture the builds' computers draw comfortably, with the look kept
(multisampling smooths only the edges of what is drawn; a larger shadow map
only the steps along a shadow's edge, its blur widened to keep it as soft).
The 64-bit desktop builds take 2 samples a pixel (on an M4 at 1920x1080,
0.4 ms a frame more; 4 samples cost three times that, which a large display
multiplies) and 256 (about nothing); the 32-bit builds, for older
computers, and Android, whose picture is 480 lines, 256 and no
anti-aliasing. The server draws nothing: the Xbox's. */
#if defined(HALO_SERVER)
#define DEFAULT_ANTI_ALIASING "\"off\""
#define DEFAULT_SHADOW_RESOLUTION "128"
#elif defined(HALO_64BIT) && !defined(HALO_ANDROID)
#define DEFAULT_ANTI_ALIASING "\"msaa2x\""
#define DEFAULT_SHADOW_RESOLUTION "256"
#else
#define DEFAULT_ANTI_ALIASING "\"off\""
#define DEFAULT_SHADOW_RESOLUTION "256"
#endif

/* Internet play's port when hosting (p2p_host_port.h): Halo's own, 2302,
on the desktop and Android, so that a player can forward it on their router;
a joiner keeps a random one. The consoles and the dedicated server keep a
random port (0), as before. */
#if defined(HALO_SERVER) || defined(HALO_XBOX_CONSOLE) || defined(HALO_CONSOLE_GL_LAYER) || \
	defined(HALO_SWITCH_CONSOLE)
#define DEFAULT_TUNNEL_PORT "0"
#else
#define DEFAULT_TUNNEL_PORT "2302"
#endif

/* A game you host starts with only you by default (the older
debug.solo_game, from Mootjuh, was the same setting: config_renamed). The
dedicated server keeps waiting for a second player: it never needs this. */
#ifdef HALO_SERVER
#define DEFAULT_START_ALONE "false"
#else
#define DEFAULT_START_ALONE "true"
#endif

/* The particles, and the dynamic lights that light the level: the consoles
keep fewer (the Switch's particles, HALO_CONSOLE_PARTICLE_TICKS and
HALO_CONSOLE_LIGHT_LIMITS); the desktop has the Xbox's look, unlimited */
#ifdef HALO_SWITCH_CONSOLE
#define DEFAULT_PARTICLE_LIMIT "2048"
#else
#define DEFAULT_PARTICLE_LIMIT "4096"
#endif
#ifdef HALO_CONSOLE_PARTICLE_TICKS
#define DEFAULT_PARTICLE_TICKS "true"
#else
#define DEFAULT_PARTICLE_TICKS "false"
#endif
#ifdef HALO_CONSOLE_LIGHT_LIMITS
#define DEFAULT_DYNAMIC_LIGHT_LIMIT "8"
#define DEFAULT_SPECULAR_LIGHT_LIMIT "2"
#else
#define DEFAULT_DYNAMIC_LIGHT_LIMIT "0"
#define DEFAULT_SPECULAR_LIGHT_LIMIT "0"
#endif

/* the lines Android and the Switch draw (port/linux/src/d3d8_gl.c; the
Switch's deko3d renderer, port/switch/guest/d3d8_dk.c): the game's own 480 on
a phone, a handheld console's own 720 on the Switch, which has no "native" */
#ifdef HALO_SWITCH_CONSOLE
#define DEFAULT_RESOLUTION_LIMIT "\"720\""
#define RESOLUTION_LIMIT_HELP \
	"How many lines the game draws. The picture is scaled up to the screen.\n" \
	"\"480\" is the game's own. \"720\", \"900\", \"1080\" or any number between\n" \
	"also work. It never goes past what the console shows: 1280x720 in\n" \
	"handheld, 1920x1080 docked. Changes at once."
#else
#define DEFAULT_RESOLUTION_LIMIT "\"480\""
#define RESOLUTION_LIMIT_HELP \
	"How many lines the game draws. The picture is scaled up to the display.\n" \
	"\"480\" is the game's own. \"720\", \"900\", \"1080\" or any number between\n" \
	"also work. \"native\" uses the display's own. It never goes past what the\n" \
	"display has. More is sharper and uses more battery. Changes at once."
#endif

/* The settings, by section in the order the file has them (config_sections),
each section's everyday ones first. A setting's comment is its help text in
the file, and on the setup screen: short plain sentences, what it does and
what its values mean. */
static const struct config_setting config_settings[] =
{
	/* ---- display */
	{ "display.mode", _config_string, "\"\"", "HALO_DISPLAY_MODE", _environment_value, _platform_desktop,
		"How Chupathingy shows: \"fullscreen\", \"borderless\" or \"windowed\".\n"
		"Leave it empty to use display.fullscreen.\n"
		"F11 switches between a window and the full screen." },
	{ "display.fullscreen", _config_boolean, DEFAULT_FULLSCREEN, "HALO_FULLSCREEN", _environment_value, _platform_desktop,
		"Used when display.mode is empty. true starts borderless.\n"
		"false starts in a window. F11 switches." },
	{ "display.resolution", _config_string, "\"native\"", "HALO_RESOLUTION", _environment_value, _platform_desktop,
		"The resolution in fullscreen and borderless. \"native\" uses the\n"
		"display's own. Or give a size, like \"1920x1080\". The smallest is 640x480." },
	{ "display.window_size", _config_string, "\"\"", "HALO_WINDOW_SIZE", _environment_value, _platform_desktop,
		"The window's size, like \"1920x1080\". The smallest is 640x480.\n"
		"Leave it empty to use display.window_scale." },
	{ "display.window_scale", _config_integer, "2", "HALO_WINDOW_SCALE", _environment_value, _platform_desktop,
		"Used when display.window_size is empty. The window's size as a\n"
		"multiple of 640x480. 2 is 1280x960." },
	{ "display.resolution_scaling", _config_string, "\"native\"", "HALO_RESOLUTION_SCALING", _environment_value,
		_platform_desktop,
		"\"native\" draws at the window's resolution. \"original\" draws at\n"
		"640x480 and scales it up." },
#if defined(HALO_ANDROID) || defined(HALO_SWITCH_CONSOLE)
	{ "display.resolution_limit", _config_string, DEFAULT_RESOLUTION_LIMIT, "HALO_RESOLUTION_LIMIT", _environment_value,
		_platform_android, RESOLUTION_LIMIT_HELP },
#endif
	{ "display.screen_width", _config_integer, "0", "HALO_SCREEN_WIDTH", _environment_value, _platform_android,
		"The picture's width in columns. 0 matches the display. 640 is 4:3." },
	{ "display.vsync", _config_boolean, "true", "HALO_NO_VSYNC", _environment_set_is_false, _platform_all,
		"Wait for the display between frames (V-Sync)." },
	{ "display.max_fps", _config_integer, "0", "HALO_MAX_FPS", _environment_value, _platform_all,
		"The frame rate limit when V-Sync is off. 0 is twice the display's\n"
		"refresh rate. -1 is no limit. No limit can hang some Intel graphics,\n"
		"and it heats up a phone." },
	{ "display.interpolation", _config_boolean, "true", "HALO_INTERPOLATION", _environment_value, _platform_all,
		"Draw a frame for every display refresh. The game runs 30 ticks a\n"
		"second, and the frames between them are blended. false keeps the\n"
		"game's own 30 frames a second." },
	{ "display.anti_aliasing", _config_string, DEFAULT_ANTI_ALIASING, "HALO_ANTI_ALIASING", _environment_value,
		_platform_all,
		"Smooth jagged edges: \"off\", \"fxaa\", \"smaa\", \"ssaa2x\", \"msaa2x\",\n"
		"\"msaa4x\" or \"msaa8x\". Android has \"off\", \"fxaa\", \"msaa2x\" and\n"
		"\"msaa4x\". The default is \"msaa2x\" on 64-bit computers. It is \"off\"\n"
		"on 32-bit computers and Android." },
	{ "display.shadow_resolution", _config_integer, DEFAULT_SHADOW_RESOLUTION, "HALO_SHADOW_RESOLUTION",
		_environment_value, _platform_all,
		"Shadow size in pixels: 128, 256, 512 or 1024. The Xbox used 128." },
	{ "display.anisotropic_filtering", _config_integer, "0", "HALO_ANISOTROPIC_FILTERING", _environment_value,
		_platform_all,
		"Sharper textures seen at an angle: 0, 2, 4, 8 or 16.\n"
		"0 is off, as on the Xbox." },
	{ "display.field_of_view", _config_real, "70.0", "HALO_FIELD_OF_VIEW", _environment_value, _platform_all | _platform_xbox,
		"The first-person field of view in degrees, from 50 to 120.\n"
		"The Xbox used 70." },
	{ "display.viewmodel_field_of_view", _config_real, "0.0", "HALO_VIEWMODEL_FIELD_OF_VIEW", _environment_value,
		_platform_all | _platform_xbox,
		"The weapon's field of view, from 50 to 120.\n"
		"0 uses display.field_of_view." },
	{ "display.reticle_scales_with_fov", _config_boolean, "true", "HALO_RETICLE_SCALES_WITH_FOV", _environment_value,
		_platform_all | _platform_xbox,
		"Size the reticle to the field of view, so it matches the weapon's\n"
		"spread on screen. It makes no difference at 70." },
	{ "display.menus", _config_string, "\"chupathingy\"", "HALO_MENUS", _environment_value, _platform_all,
		"Which menus to use: \"chupathingy\", \"xbox\" or \"gearbox\".\n"
		"Put your own menus in a menus folder next to config.toml.\n"
		"Restart Chupathingy after a change." },
	{ "display.high_res_text", _config_boolean, "true", "HALO_HIGH_RES_TEXT", _environment_value, _platform_all,
		"Draw the text and titles of the menus and the HUD with sharp,\n"
		"high-res fonts." },
	{ "display.high_res_hud", _config_boolean, "true", "HALO_HIGH_RES_HUD", _environment_value, _platform_all,
		"Draw the HUD from the high-res assets." },
	{ "display.high_res_font", _config_string, "\"\"", "HALO_HIGH_RES_FONT", _environment_value, _platform_all,
		"A font file (.ttf or .otf) for the high-res text and Online Games.\n"
		"A name alone means a file in data/fonts next to the maps folder.\n"
		"Leave it empty for the built-in fonts." },
	{ "display.high_res_font_large", _config_string, "\"\"", "HALO_HIGH_RES_FONT_LARGE", _environment_value,
		_platform_all,
		"A font file for the large text only. It works like\n"
		"display.high_res_font. Leave it empty to use that one." },
	{ "display.skip_intro", _config_boolean, "false", "HALO_SKIP_INTRO", _environment_set_is_true, _platform_all,
		"Skip the intro movie and go straight to the main menu." },
	{ "display.player_names", _config_string, "\"all\"", "HALO_PLAYER_NAMES", _environment_value, _platform_all,
		"In multiplayer, whose names show above their heads: \"all\",\n"
		"\"allies\", \"enemies\" or \"none\"." },
	{ "display.player_name_scale", _config_real, "1.0", "HALO_PLAYER_NAME_SCALE", _environment_value, _platform_all,
		"The size of player names, from 0.25 to 4." },
	{ "display.match_timer", _config_boolean, "false", "HALO_MATCH_TIMER", _environment_value, _platform_all | _platform_xbox,
		"Show the time left at the top of the screen in a multiplayer game\n"
		"with a time limit." },
	{ "display.powerup_timers", _config_boolean, "false", "HALO_POWERUP_TIMERS", _environment_value, _platform_all | _platform_xbox,
		"Show the seconds left on your active camouflage, overshield and\n"
		"full spectrum vision." },
	{ "display.follow_camera", _config_string, "\"long_waits\"", "HALO_FOLLOW_CAMERA", _environment_value,
		_platform_all | _platform_xbox,
		"After you die, watch a living player's view. \"long_waits\" does it\n"
		"only during a long respawn wait. \"always\" does it every time, and\n"
		"\"off\" never. LB and RB, or Q and E, switch players." },
	{ "display.follow_camera_after", _config_real, "5.0", "HALO_FOLLOW_CAMERA_AFTER", _environment_value,
		_platform_all | _platform_xbox,
		"For \"long_waits\": a respawn wait longer than this many seconds is a\n"
		"long one. It counts from when the death camera is done." },
	{ "display.scoreboard_team_layout", _config_string, "\"teams\"", "HALO_SCOREBOARD_TEAM_LAYOUT", _environment_value,
		_platform_all | _platform_xbox,
		"A team game's scoreboard. \"teams\" gives each team a column.\n"
		"\"score\" lists everyone in order of score." },
	{ "display.scoreboard_background", _config_boolean, "true", "HALO_SCOREBOARD_BACKGROUND", _environment_value,
		_platform_all | _platform_xbox,
		"Draw a panel behind the multiplayer scoreboard." },
	{ "display.scoreboard_background_color", _config_string, "\"16, 16, 16, 150\"", "HALO_SCOREBOARD_BACKGROUND_COLOR",
		_environment_value, _platform_all | _platform_xbox,
		"The scoreboard panel's color, as \"red, green, blue, alpha\".\n"
		"Each is 0 to 255." },
	{ "display.scoreboard_platforms", _config_string, "\"auto\"", "HALO_SCOREBOARD_PLATFORMS", _environment_value,
		_platform_all,
		"Show each Chupathingy player's platform logo on the scoreboard.\n"
		"\"auto\" shows them only with the Chupathingy menus. \"on\" always\n"
		"shows them, and \"off\" never does." },
	{ "display.show_quit_players", _config_boolean, "true", "HALO_SHOW_QUIT_PLAYERS", _environment_value,
		_platform_all | _platform_xbox,
		"Keep players who quit on the multiplayer scoreboard." },
	{ "display.loading_screen", _config_boolean, "true", "HALO_LOADING_SCREEN", _environment_value,
		_platform_all,
		"Hold the loading screen up for a while when a level loads, the way\n"
		"the Xbox's disc did. The next two settings say how long. false loads\n"
		"every level as fast as it can." },
	{ "display.loading_screen_campaign_seconds", _config_real, "0.0", "HALO_LOADING_SCREEN_CAMPAIGN_SECONDS",
		_environment_value, _platform_all,
		"The shortest time the loading screen stays up for a campaign level\n"
		"on this machine, in seconds, up to 30. Split screen co-op counts.\n"
		"0 means no wait. A longer load shows for as long as it takes." },
	{ "display.loading_screen_multiplayer_seconds", _config_real, "0.0", "HALO_LOADING_SCREEN_MULTIPLAYER_SECONDS",
		_environment_value, _platform_all,
		"The same for every other game, up to 5: split screen multiplayer,\n"
		"system link, online and network co-op. The network waits while the\n"
		"loading screen is up." },
	{ "display.pause_change_teams", _config_boolean, "true", "HALO_PAUSE_CHANGE_TEAMS", _environment_value,
		_platform_all,
		"Show CHANGE TEAMS in the pause menu of a team game." },
#ifndef HALO_CONSOLE_NO_CONTROLS
	{ "display.pause_controls", _config_boolean, "true", "HALO_PAUSE_CONTROLS", _environment_value, _platform_all,
		"Show CONTROLS in the pause menu." },
#endif
	{ "display.delta_profile", _config_boolean, "true", "HALO_DELTA_PROFILE", _environment_value, _platform_all,
		"Show Delta Profile in the menus and Online Games." },
	{ "display.profile_names", _config_boolean, "true", "HALO_PROFILE_NAMES", _environment_value, _platform_all,
		"In Delta games, show players by their shared Delta profile name." },
	{ "display.server_messages", _config_boolean, "true", "HALO_SERVER_MESSAGES", _environment_value, _platform_all | _platform_xbox,
		"Show the banner for a Delta server's warnings and notices." },
	{ "display.server_message_text_color", _config_string, "\"\"", "HALO_SERVER_MESSAGE_TEXT_COLOR",
		_environment_value, _platform_all,
		"The banner's text color, as \"#RRGGBB\" or \"#RRGGBBAA\".\n"
		"Leave it empty for the default." },
	{ "display.server_message_border_color", _config_string, "\"\"", "HALO_SERVER_MESSAGE_BORDER_COLOR",
		_environment_value, _platform_all,
		"The banner's outline color, as \"#RRGGBB\" or \"#RRGGBBAA\".\n"
		"Leave it empty for the default." },
	{ "display.server_message_background_color", _config_string, "\"\"", "HALO_SERVER_MESSAGE_BACKGROUND_COLOR",
		_environment_value, _platform_all,
		"The banner's background color, as \"#RRGGBB\" or \"#RRGGBBAA\".\n"
		"Leave it empty for the default." },
	{ "display.version_watermark", _config_boolean, "false", "HALO_VERSION_WATERMARK", _environment_value,
		_platform_all | _platform_xbox,
		"Show the build's version in the bottom right corner while playing." },
	{ "display.direct_camera", _config_boolean, "true", "HALO_DIRECT_CAMERA", _environment_value, _platform_all | _platform_xbox,
		"In first person, turn the view in the same frame that the mouse,\n"
		"a stick or touch moves it." },
	{ "display.far_clip_distance", _config_real, "1024.0", "HALO_FAR_CLIP_DISTANCE", _environment_value, _platform_all,
		"How far the world is drawn, up to 10000. The Xbox used 1024." },
	{ "display.full_model_detail", _config_boolean, "false", "HALO_FULL_MODEL_DETAIL", _environment_value,
		_platform_all,
		"Draw every model at full detail at any distance." },
	{ "display.per_pixel_lighting", _config_boolean, "false", "HALO_PER_PIXEL_LIGHTING", _environment_value,
		_platform_all,
		"Light models for each pixel instead of each vertex." },
	{ "display.particle_limit", _config_integer, DEFAULT_PARTICLE_LIMIT, "HALO_PARTICLE_LIMIT", _environment_value,
		_platform_all,
		"The most particles alive at once: sparks, smoke, casings and debris.\n"
		"When it is full, the oldest goes to make room for a new one. The Xbox's\n"
		"limit was 1024. 0 uses the port's own 8192, which a 64-player firefight\n"
		"fills. Every particle costs CPU and GPU time. The default is 2048 on the\n"
		"Switch and 4096 elsewhere. Changes at once." },
	{ "display.particle_ticks", _config_boolean, DEFAULT_PARTICLE_TICKS, "HALO_PARTICLE_TICKS", _environment_value,
		_platform_all,
		"Move the particles that are not attached to an object once a 30 Hz\n"
		"tick, as on the Xbox, and draw them smoothly between ticks. That saves\n"
		"two in three of their collision tests at 60 frames a second, and more\n"
		"at 120. false moves them every frame, which is smoother at high\n"
		"refresh rates. The default is false on the desktop and true on the\n"
		"consoles. Takes effect at the next map." },
	{ "display.dynamic_light_limit", _config_integer, DEFAULT_DYNAMIC_LIGHT_LIMIT, "HALO_DYNAMIC_LIGHT_LIMIT",
		_environment_value, _platform_all,
		"The most dynamic lights that light the level at once, like muzzle\n"
		"flashes, plasma and explosions. The biggest on screen light the walls\n"
		"and floor. The rest light only the objects near them. 0 means no limit,\n"
		"as on the Xbox. Each light redraws the level it reaches twice, and in a\n"
		"big firefight that is most of the GPU's time. The default is 0 on the\n"
		"desktop and 8 on the consoles. Changes at once." },
	{ "display.specular_light_limit", _config_integer, DEFAULT_SPECULAR_LIGHT_LIMIT, "HALO_SPECULAR_LIGHT_LIMIT",
		_environment_value, _platform_all,
		"Of those lights, the most that also add their shine to metal and wet\n"
		"floors. The first-person weapon's go first, then the biggest on screen.\n"
		"The others still light the level, without the shine. 0 means no limit,\n"
		"as on the Xbox. Near lights' shine is the costliest pass in a firefight.\n"
		"The default is 0 on the desktop and 2 on the consoles. Changes at once." },
	{ "display.texture_decode", _config_string, "\"auto\"", "HALO_TEXTURE_DECODE", _environment_value,
		_platform_android,
		"Where compressed textures are unpacked: \"gpu\" or \"cpu\". \"cpu\"\n"
		"uses more memory. \"auto\" picks \"cpu\" on Adreno graphics and \"gpu\"\n"
		"on others." },
#ifdef HALO_SWITCH_CONSOLE
	/* the Switch's (port/switch): read by the deko3d renderer in the guest
	(port/switch/guest/d3d8_dk.c) and by the host from config.toml itself */
	{ "display.big_sprite_limit", _config_integer, "8", "HALO_BIG_SPRITE_LIMIT", _environment_value, _platform_android,
		"The most big sprites drawn in a frame. A big sprite covers more than\n"
		"half the view, so each is a layer over most of the screen. The Xbox's\n"
		"limit is 11. The Halo level's falls have 11 sheets of mist.\n"
		"Changes at once." },
	{ "display.dynamic_resolution", _config_boolean, "false", "HALO_DYNAMIC_RESOLUTION", _environment_value,
		_platform_android,
		"Draw one step lower when the GPU would keep the game under 55 frames a\n"
		"second for half a second: 1080 to 900, 900 to 720, 720 to 480, and\n"
		"never lower. Go back up once a frame fits in 90% of a 60 Hz frame for\n"
		"a second. Changes at once." },
#endif
#ifdef HALO_BGFX
	{ "display.renderer", _config_string, "\"gl\"", "HALO_RENDERER", _environment_value, _platform_desktop,
		"What draws the game. \"gl\" is OpenGL. \"bgfx\" is the bgfx test\n"
		"backend, which uses Metal on macOS. It is a work in progress.\n"
		"Read when Chupathingy starts." },
	{ "display.crt_filter", _config_boolean, "false", "HALO_CRT_FILTER", _environment_set_is_true, _platform_desktop,
		"Put a CRT look over the picture: scanlines, curvature and a phosphor\n"
		"mask. Only the bgfx renderer has it for now." },
#endif

	/* ---- audio */
	{ "audio.volume", _config_real, "1.0", "HALO_VOLUME", _environment_value, _platform_all,
		"The volume of everything, from 0.0 to 1.0." },
	{ "audio.music_volume", _config_real, "1.0", "HALO_MUSIC_VOLUME", _environment_value, _platform_all | _platform_xbox,
		"The music's volume, from 0.0 to 1.0." },
	{ "audio.effects_volume", _config_real, "1.0", "HALO_EFFECTS_VOLUME", _environment_value, _platform_all | _platform_xbox,
		"The volume of effects and speech, from 0.0 to 1.0." },
	{ "audio.enabled", _config_boolean, "true", "HALO_NO_AUDIO", _environment_set_is_false, _platform_all,
		"Play sound." },
	/* (audio.voice_device is kept for a voice output of its own, later:
	dsound_sdl.c's audio_device_named opens either) */
	{ "audio.device", _config_string, "\"default\"", "HALO_AUDIO_DEVICE", _environment_value, _platform_desktop,
		"The sound output, by its system name. Audio Setup lists them.\n"
		"\"default\" follows the system's output." },
	{ "audio.channels", _config_string, "\"auto\"", "HALO_AUDIO_CHANNELS", _environment_value, _platform_desktop,
		"\"stereo\", \"5.1\" or \"7.1\". Surround works like the Xbox's Dolby\n"
		"Digital. \"auto\" picks 5.1 or 7.1 when the sound device has 6 or 8\n"
		"channels, and stereo otherwise. Read when Chupathingy starts." },
	{ "audio.hrtf", _config_string, DEFAULT_AUDIO_HRTF, "HALO_AUDIO_HRTF", _environment_value, _platform_all,
		"Headphone 3D: 3D sound the way the Xbox made it, on headphones or TV\n"
		"stereo. \"on\" or \"off\". Turn it off if it sounds odd on your\n"
		"speakers. \"auto\" turns it on when the output's name says it is\n"
		"headphones. Stereo output only." },
	{ "audio.doppler", _config_boolean, "true", "HALO_AUDIO_DOPPLER", _environment_value, _platform_all,
		"Shift the pitch of moving sounds, like a Warthog or a projectile going\n"
		"past, as the Xbox did. false keeps every sound at its own pitch." },
	{ "audio.reverb", _config_boolean, "true", "HALO_REVERB", _environment_value, _platform_all,
		"Echo sounds to match the place you are in." },
	{ "audio.buffer_frames", _config_integer, DEFAULT_AUDIO_BUFFER_FRAMES, "HALO_AUDIO_BUFFER_FRAMES", _environment_value,
		_platform_all,
		"The audio buffer in sample frames, from 64 to 8192. Raise it if the\n"
		"sound cuts out. Lower it for less delay." },
	{ "audio.resampling", _config_string, "\"sinc\"", "HALO_AUDIO_RESAMPLING", _environment_value, _platform_all,
		"How sounds are converted to 48 kHz: \"sinc\" or \"linear\"." },

	/* ---- input */
	{ "input.mouse_sensitivity", _config_real, "1.0", "HALO_MOUSE_SENSITIVITY", _environment_value, _platform_all,
		"How fast the view turns with the mouse." },
	{ "input.mouse_vertical_sensitivity", _config_real, "0.0", "HALO_MOUSE_VERTICAL_SENSITIVITY", _environment_value,
		_platform_all,
		"How fast the view turns up and down with the mouse.\n"
		"0 uses input.mouse_sensitivity." },
	{ "input.invert_mouse", _config_boolean, "false", "HALO_MOUSE_INVERT", _environment_set_is_true, _platform_all,
		"Moving the mouse forward looks down." },
	{ "input.mouse_aim_assist", _config_boolean, "false", "HALO_MOUSE_AIM_ASSIST", _environment_value, _platform_all,
		"Give the mouse aim assist, as a controller has." },
	{ "input.keyboard_prompts", _config_boolean, "true", "HALO_KEYBOARD_PROMPTS", _environment_value, _platform_desktop,
		"Show keys in button prompts while you use the keyboard and mouse." },
	{ "input.touch_controls", _config_string, "\"on\"", "HALO_TOUCH_CONTROLS", _environment_value, _platform_android,
		"On-screen touch controls: \"on\" or \"off\". \"auto\" shows them only\n"
		"when no controller is connected." },
#ifdef HALO_SWITCH_CONSOLE
	/* gyro aiming (port/linux/src/xinput_sdl.c): the controller's gyroscope,
	each local player from their own */
	{ "input.gyro_aim", _config_string, "\"false\"", "HALO_GYRO_AIM", _environment_value, _platform_android,
		"Aim with the controller's gyroscope. Each local player uses their own:\n"
		"a Pro Controller, the console held in your hands, or one Joy-Con. With\n"
		"two Joy-Cons, the right one is used. \"true\" always aims with it, \"zoom\"\n"
		"only while zoomed in with a scope, and \"false\" never. Gamepads > Gyro\n"
		"Aim sets it too. Changes at once." },
	{ "input.gyro_sensitivity_horizontal", _config_real, "1.0", "HALO_GYRO_SENSITIVITY_HORIZONTAL",
		_environment_value, _platform_android,
		"How far the view turns left and right with the gyroscope, when\n"
		"input.gyro_aim is on. 1 is the sensor's own turn. From 0.25 to 10." },
	{ "input.gyro_sensitivity_vertical", _config_real, "1.0", "HALO_GYRO_SENSITIVITY_VERTICAL",
		_environment_value, _platform_android,
		"How far the view turns up and down with the gyroscope, when\n"
		"input.gyro_aim is on. 1 is the sensor's own turn. From 0.25 to 10." },
	{ "input.gyro_invert_horizontal", _config_boolean, "false", "HALO_GYRO_INVERT_HORIZONTAL",
		_environment_value, _platform_android,
		"Invert the gyroscope's left and right. With true, turning the\n"
		"controller right looks left. The stick's invert is separate." },
	{ "input.gyro_invert_vertical", _config_boolean, "false", "HALO_GYRO_INVERT_VERTICAL",
		_environment_value, _platform_android,
		"Invert the gyroscope's up and down. With true, tilting the controller\n"
		"up looks down. The stick's invert is separate." },
#endif

	/* ---- controls: the keyboard and mouse's own (port/linux/src/xinput_sdl.c) */
	{ "controls.move_forward", _config_string, "\"W\"", "HALO_KEY_MOVE_FORWARD", _environment_value, _platform_all,
		"Move forward." },
	{ "controls.move_backward", _config_string, "\"S\"", "HALO_KEY_MOVE_BACKWARD", _environment_value, _platform_all,
		"Move backward." },
	{ "controls.strafe_left", _config_string, "\"A\"", "HALO_KEY_STRAFE_LEFT", _environment_value, _platform_all,
		"Move left." },
	{ "controls.strafe_right", _config_string, "\"D\"", "HALO_KEY_STRAFE_RIGHT", _environment_value, _platform_all,
		"Move right." },
	{ "controls.jump", _config_string, "\"Space\"", "HALO_KEY_JUMP", _environment_value, _platform_all,
		"Jump. It also skips cutscenes." },
	{ "controls.crouch", _config_string, "\"Left Ctrl, C\"", "HALO_KEY_CROUCH", _environment_value, _platform_all,
		"Crouch." },
	{ "controls.fire", _config_string, "\"Mouse Left\"", "HALO_KEY_FIRE", _environment_value, _platform_all,
		"Fire." },
	{ "controls.throw_grenade", _config_string, "\"Mouse Right, G\"", "HALO_KEY_THROW_GRENADE", _environment_value,
		_platform_all,
		"Throw a grenade." },
	{ "controls.melee", _config_string, "\"F, Mouse 4\"", "HALO_KEY_MELEE", _environment_value, _platform_all,
		"Melee attack." },
	{ "controls.reload", _config_string, "\"R\"", "HALO_KEY_RELOAD", _environment_value, _platform_all,
		"Reload." },
	{ "controls.zoom", _config_string, "\"Z, Mouse Middle\"", "HALO_KEY_ZOOM", _environment_value, _platform_all,
		"Zoom." },
	{ "controls.switch_weapon", _config_string, "\"Wheel, 1\"", "HALO_KEY_SWITCH_WEAPON", _environment_value,
		_platform_all,
		"Switch weapons." },
	{ "controls.switch_grenade", _config_string, "\"X\"", "HALO_KEY_SWITCH_GRENADE", _environment_value, _platform_all,
		"Switch grenades." },
	{ "controls.action", _config_string, "\"E\"", "HALO_KEY_ACTION", _environment_value, _platform_all,
		"Action: pick up and swap weapons, get in and out of vehicles, and\n"
		"press switches." },
	{ "controls.flashlight", _config_string, "\"Q\"", "HALO_KEY_FLASHLIGHT", _environment_value, _platform_all,
		"Flashlight." },
	{ "controls.scoreboard", _config_string, "\"Tab\"", "HALO_KEY_SCOREBOARD", _environment_value, _platform_all,
		"Show the scores." },
	{ "controls.pause", _config_string, "\"Escape\"", "HALO_KEY_PAUSE", _environment_value, _platform_all,
		"Open the pause menu." },
	{ "controls.chat_all", _config_string, "\"T\"", "HALO_KEY_CHAT_ALL", _environment_value, _platform_all,
		"Chat to everyone." },
	{ "controls.chat_team", _config_string, "\"Y\"", "HALO_KEY_CHAT_TEAM", _environment_value, _platform_all,
		"Chat to your team." },
	{ "controls.chat_log", _config_string, "\"H\"", "HALO_KEY_CHAT_LOG", _environment_value, _platform_all,
		"Chat history and the player list. M mutes a player there." },
#ifndef HALO_CONSOLE_NO_CONTROLS
	{ "controls.controller_layout", _config_string, "\"\"", "HALO_CONTROLLER_LAYOUT", _environment_value,
		_platform_all,
		"The Custom controller layout from the Controls screen. It is a list of\n"
		"action and button pairs separated by commas, like \"jump=B,melee=RB\".\n"
		"Leave it empty for the default." },
#endif

	/* ---- chat */
	{ "chat.enabled", _config_boolean, "true", "HALO_CHAT", _environment_value, _platform_all | _platform_xbox,
		"Text chat with Chupathingy players. T talks to everyone, and Y to your\n"
		"team." },
	{ "chat.show", _config_string, "\"all\"", "HALO_CHAT_SHOW", _environment_value, _platform_all | _platform_xbox,
		"Whose chat lines you see: \"all\", \"team\" or \"off\"." },
	{ "chat.filter", _config_string, "\"mask\"", "HALO_CHAT_FILTER", _environment_value, _platform_all | _platform_xbox,
		"What happens to blocked words in chat. \"mask\" stars them out. \"hide\"\n"
		"hides the line. \"show\" shows it." },
	{ "chat.filter_file", _config_string, "\"chat_filter.txt\"", "HALO_CHAT_FILTER_FILE", _environment_value,
		_platform_all,
		"A file of your own blocked words in the save folder, one per line.\n"
		"Write \"whole <word>\" to block only the whole word. Lines that start\n"
		"with # are comments." },
	{ "chat.streamer_mode", _config_boolean, "false", "HALO_CHAT_STREAMER_MODE", _environment_value, _platform_all | _platform_xbox,
		"Show other players in chat by their team's name. Blocked words are\n"
		"always starred out." },
	{ "chat.quick_messages", _config_string,
		"\"Nice shot|Need backup|On my way|Thanks|Good game|Sorry\"", "HALO_CHAT_QUICK_MESSAGES",
		_environment_value, _platform_all | _platform_xbox,
		"Quick messages for the controller, separated by |. D-pad up picks one,\n"
		"and D-pad down sends it. Leave it empty for none." },

	/* ---- game */
	{ "game.start_with_one_player", _config_boolean, DEFAULT_START_ALONE, "HALO_START_WITH_ONE_PLAYER", _environment_value,
		_platform_all | _platform_xbox,
		"Let a game you host start with only you. Others can join while it\n"
		"is on. false waits for a second player, as on the Xbox." },
	{ "game.unlock_all_levels", _config_boolean, "false", "HALO_UNLOCK_ALL_LEVELS", _environment_value, _platform_all | _platform_xbox,
		"New profiles start with every campaign level unlocked." },
	{ "game.language", _config_string, "\"\"", "HALO_LANGUAGE", _environment_value, _platform_all,
		"The game's language: \"ja\", \"de\", \"fr\", \"es\" or \"it\". Leave it\n"
		"empty for English." },
	{ "game.console_log", _config_string, "\"important\"", "HALO_CONSOLE_LOG", _environment_value, _platform_all | _platform_xbox,
		"What the console shows on screen: \"important\", \"all\" or \"none\".\n"
		"debug.txt always has every line." },
	{ "game.pack_menus", _config_string, "\"\"", "HALO_PACK_MENUS", _environment_value, _platform_all | _platform_xbox,
		"Play with a map pack's own menus, named by the pack's folder. \"cex\"\n"
		"uses maps_cex/ui.map, the menus of Halo CE+. Leave it empty for the\n"
		"game's own menus. Restart Chupathingy after a change." },
	{ "game.downloaded_maps", _config_string, "\"\"", "HALO_DOWNLOADED_MAPS", _environment_value, _platform_all,
		"Maps treated as downloaded, separated by commas, like\n"
		"bloodgulch,hugeass@ce. \"*\" means all of them. Their scripts can't\n"
		"change your settings or other players' games." },
	{ "game.move_old_map_folders", _config_string, "\"ask\"", "HALO_MOVE_OLD_MAP_FOLDERS", _environment_value,
		_platform_all,
		"Move maps from the old maps/ce and md_maps folders to maps_ce, maps_md\n"
		"and maps_pc: \"ask\", \"yes\" or \"no\"." },
	{ "game.bomb_spawns", _config_string, "\"\"", "HALO_BOMB_SPAWNS", _environment_value, _platform_all,
		"Where the bomb goes in a Bomb or Grifball game you host. Use it on\n"
		"maps where the spot halfway between the goals plays badly. Each entry\n"
		"is \"<map> <x> <y> [<z>]\", and commas separate entries. Name the map\n"
		"as a playlist does, like bloodgulch or timberland@ce. The bomb rests\n"
		"on the first ground under the point. With a z, that search starts a\n"
		"little above it. A map not listed uses the game's own spot. On Blood\n"
		"Gulch that is halfway between its teleporters' exits. On other maps\n"
		"it is halfway between the goals." },
	{ "game.bomb_arm_time", _config_real, "1.0", "HALO_BOMB_ARM_TIME", _environment_value, _platform_all,
		"In a Bomb or Grifball game you host, the seconds the bomb takes to arm\n"
		"once it is planted in the enemy goal, up to 10. A defender who reaches\n"
		"it in time saves it, and it resets with no score. Otherwise it goes\n"
		"off and scores. 0 scores the moment it is planted." },
	{ "game.bomb_open_pitch_speed", _config_integer, "0", "HALO_BOMB_OPEN_PITCH_SPEED", _environment_value,
		_platform_all,
		"Everyone's speed in a Grifball: Open Pitch game you host, in percent,\n"
		"from 50 to 200. The carrier's extra 115% stops at 200. 0 uses the\n"
		"game type's own, 175." },
	{ "game.bomb_open_pitch_carrier_health", _config_integer, "0", "HALO_BOMB_OPEN_PITCH_CARRIER_HEALTH",
		_environment_value, _platform_all,
		"The bomb carrier's health in a Grifball: Open Pitch game you host, in\n"
		"percent of everyone's, from 10 to 400. 0 uses the game type's own,\n"
		"400. With 400 resistance too, the carrier lives through one melee\n"
		"hit, not two." },
	{ "game.bomb_open_pitch_carrier_resistance", _config_integer, "0",
		"HALO_BOMB_OPEN_PITCH_CARRIER_RESISTANCE", _environment_value, _platform_all,
		"The bomb carrier's damage resistance in a Grifball: Open Pitch game\n"
		"you host, in percent, from 25 to 400. At 150 they take two thirds of\n"
		"the damage. 0 uses the game type's own, 400." },
	{ "game.bomb_open_pitch_carrier_shields", _config_string, "\"none\"", "HALO_BOMB_OPEN_PITCH_CARRIER_SHIELDS",
		_environment_value, _platform_all,
		"The bomb carrier's shields in a Grifball: Open Pitch game you host:\n"
		"\"none\", \"normal\" or \"overshield\". Everyone else has none." },
	{ "game.bomb_open_pitch_fumble", _config_boolean, "true", "HALO_BOMB_OPEN_PITCH_FUMBLE", _environment_value,
		_platform_all,
		"In a Grifball: Open Pitch game you host, a melee hit that doesn't kill\n"
		"the bomb carrier knocks the bomb out of their hands." },
	{ "game.defaults_answered", _config_integer, "0", "HALO_DEFAULTS_ANSWERED", _environment_value, _platform_all,
		"Chupathingy sets this after it asks about changed defaults.\n"
		"Don't edit it." },

	/* ---- network */
	{ "network.online", _config_boolean, "true", "HALO_NET_ONLINE", _environment_value, _platform_all | _platform_xbox,
		"Host and join games over the internet with invite links. false keeps\n"
		"you to the local network." },
	{ "network.host_public", _config_boolean, "true", "HALO_NET_HOST_PUBLIC", _environment_value, _platform_all,
		"New internet games you host start as PUBLIC. false starts them as\n"
		"PRIVATE." },
	{ "network.coop_public", _config_boolean, "false", "HALO_NET_COOP_PUBLIC", _environment_value, _platform_all,
		"New online co-op games start as PUBLIC. false starts them as PRIVATE." },
	{ "network.tunnel_port", _config_integer, DEFAULT_TUNNEL_PORT, "HALO_NET_TUNNEL_PORT", _environment_value,
		_platform_all | _platform_xbox,
		"The UDP port for internet play. Forward it on your router if players\n"
		"can't join your games. 0 picks a new port each run." },
	{ "network.allow_upnp", _config_boolean, "true", "HALO_NET_ALLOW_UPNP", _environment_value, _platform_all,
		"Ask your router to forward the internet play port (UPnP)." },
	{ "network.map_downloads", _config_string, "\"ask\"", "HALO_NET_MAP_DOWNLOADS", _environment_value,
		_platform_all,
		"Download a missing Custom Edition map from the community repositories:\n"
		"\"ask\", \"always\" or \"never\"." },
	{ "network.playlist_downloads", _config_string, "\"ask\"", "HALO_NET_PLAYLIST_DOWNLOADS", _environment_value,
		_platform_all,
		"When you join a playlist's game, offer its missing maps from the\n"
		"playlist's verified map set: \"ask\", \"always\" or \"never\"." },
	{ "network.join_from_clipboard", _config_string, "\"never\"", "HALO_NET_JOIN_FROM_CLIPBOARD", _environment_value,
		_platform_all,
		"What to do with an invite link on the clipboard when Chupathingy\n"
		"comes to the front: \"ask\", \"always\" to join it, or \"never\"." },
	{ "network.copy_to_clipboard", _config_boolean, "false", "HALO_NET_COPY_TO_CLIPBOARD", _environment_value,
		_platform_all,
		"Put a game's invite link on the clipboard when you start hosting it.\n"
		"With false, the log has the link, and the Gearbox menus' Server\n"
		"Settings copy it." },
	{ "network.public_lobby", _config_boolean, "true", "HALO_NET_PUBLIC_LOBBY", _environment_value, _platform_all,
		"List public games in the Server Browser, and show others' games there." },
	{ "network.ping_games", _config_boolean, "true", "HALO_NET_PING_GAMES", _environment_value, _platform_all,
		"Ping the hosts listed in Online Games. false shows estimated pings\n"
		"only." },
	{ "network.chat", _config_string, "\"on\"", "HALO_NET_CHAT", _environment_value, _platform_all,
		"Text chat in games you host: \"on\", \"team\" or \"off\"." },
	{ "network.chat_filter", _config_boolean, "true", "HALO_NET_CHAT_FILTER", _environment_value, _platform_all,
		"In games you host, block chat lines that have blocked words." },
	{ "network.chat_legacy", _config_boolean, "true", "HALO_NET_CHAT_LEGACY", _environment_value, _platform_all,
		"In games you host, let OpenCE players chat too while network.chat is\n"
		"on. Their own text chat is bridged in." },
	{ "network.team_change", _config_string, "\"balanced\"", "HALO_NET_TEAM_CHANGE", _environment_value,
		_platform_all,
		"Team changes in team games you host: \"balanced\", \"allow\" or \"off\"." },
	{ "network.team_change_margin", _config_integer, "1", "HALO_NET_TEAM_CHANGE_MARGIN", _environment_value,
		_platform_all,
		"For \"balanced\": how many more players one team may have, from 0 to\n"
		"16." },
	{ "network.block_mouse_aim_assist", _config_boolean, "false", "HALO_NET_BLOCK_MOUSE_AIM_ASSIST", _environment_value,
		_platform_all,
		"Turn off mouse aim assist for everyone in games you host." },
	{ "network.coop_friendly_fire", _config_string, "\"on\"", "HALO_NET_COOP_FRIENDLY_FIRE", _environment_value,
		_platform_all,
		"Friendly fire in online co-op: \"off\", \"on\", \"shields_only\" or\n"
		"\"explosives_only\"." },
	{ "network.coop_ally_friendly_fire", _config_boolean, "true", "HALO_NET_COOP_ALLY_FRIENDLY_FIRE",
		_environment_value, _platform_all,
		"In online co-op you host, players' hits hurt their AI allies, like\n"
		"Marines." },
	{ "network.coop_player_collisions", _config_boolean, "true", "HALO_NET_COOP_PLAYER_COLLISIONS", _environment_value,
		_platform_all,
		"Players bump into each other in online co-op." },
	{ "network.coop_enemies_mode", _config_string, "\"per_player\"", "HALO_NET_COOP_ENEMIES_MODE", _environment_value,
		_platform_all,
		"Extra enemies in online co-op: \"none\", \"per_player\" or \"multiplier\".\n"
		"\"per_player\" uses network.coop_enemies. \"multiplier\" uses\n"
		"network.coop_enemies_multiplier." },
	{ "network.coop_enemies", _config_integer, "50", "HALO_NET_COOP_ENEMIES", _environment_value, _platform_all,
		"Extra enemies for each co-op player after the first, in percent, from\n"
		"25 to 200." },
	{ "network.coop_enemies_multiplier", _config_integer, "2", "HALO_NET_COOP_ENEMIES_MULTIPLIER", _environment_value,
		_platform_all,
		"How many times larger each co-op enemy squad is, from 2 to 32." },
	{ "network.share_profile", _config_boolean, "false", "HALO_NET_SHARE_PROFILE", _environment_value,
		_platform_all,
		"Show your Chupathingy player ID to other Chupathingy players over\n"
		"Delta." },
#ifdef HALO_GAME_BROWSER
	{ "network.browser_url", _config_string, "\"https://stats.chupathingy.org\"", "HALO_NET_BROWSER", _environment_value,
		_platform_all,
		"The game list server. Leave it empty for none." },
	{ "network.list_hosted_games", _config_boolean, "true", "HALO_NET_LIST_GAMES", _environment_value, _platform_all,
		"List the public games you host without a password on the game list,\n"
		"and send their carnage reports." },
	{ "network.report_hosted_games", _config_boolean, "true", "HALO_NET_REPORT_HOSTED", _environment_value,
		_platform_all,
		"Send the stats of games you host to the game list." },
	{ "network.report_joined_games", _config_boolean, "true", "HALO_NET_REPORT_GAMES", _environment_value,
		_platform_all,
		"Send the scores of internet games you join to the game list." },
	{ "network.report_events", _config_boolean, "true", "HALO_NET_REPORT_EVENTS", _environment_value, _platform_all,
		"Delta Stats: record the games you host for the site's match pages." },
	{ "network.events_token", _config_string, "\"\"", "HALO_EVENTS_TOKEN", _environment_value, _platform_all,
		"A dedicated server's Delta Stats token, from the game list's operator." },
	{ "network.events_positions", _config_integer, "2", "HALO_EVENTS_POSITIONS", _environment_value, _platform_all,
		"Delta Stats: seconds between position samples, from 1 to 60. 0 for\n"
		"none." },
	{ "network.events_limit", _config_integer, "40000", "HALO_EVENTS_LIMIT", _environment_value, _platform_all,
		"Delta Stats: the most events kept for a game, from 64 to 200000." },
	{ "network.events_part_minutes", _config_integer, "30", "HALO_EVENTS_PART_MINUTES", _environment_value,
		_platform_all,
		"Delta Stats: send a long game in parts, one every this many minutes.\n"
		"0 sends it only at the end." },
	{ "network.events_folder", _config_string, "\"\"", "HALO_EVENTS_FOLDER", _environment_value, _platform_all,
		"Delta Stats: a folder to keep a copy of each batch sent. Leave it\n"
		"empty for none." },
#endif
	{ "network.protocol", _config_string, "\"auto\"", "HALO_NET_PROTOCOL", _environment_value, _platform_all,
		"How to talk to other Chupathingy machines. \"auto\" uses Delta where it\n"
		"can and OpenCE otherwise. \"opence\" uses OpenCE only. \"delta\" is the\n"
		"same as \"auto\" for now." },
	{ "network.platform_limits", _config_string, "\"on\"", "HALO_NET_PLATFORM_LIMITS", _environment_value,
		_platform_all,
		"Delta hosts keep games to this platform's player limit: \"on\" or\n"
		"\"off\"." },
	{ "network.host_platform_limits", _config_boolean, "true", "HALO_NET_HOST_PLATFORM_LIMITS", _environment_value,
		_platform_all,
		"Games you host keep to their Delta players' platform limits. false is\n"
		"for testing." },
	{ "network.address", _config_string, "\"\"", "HALO_NET_ADDRESS", _environment_value, _platform_all,
		"This machine's IPv4 address for system link. Leave it empty to pick\n"
		"one." },
	{ "network.broadcast", _config_string, "\"\"", "HALO_NET_BROADCAST", _environment_value, _platform_all,
		"IPv4 addresses for system link announcements, separated by commas.\n"
		"Use it for a VPN. Leave it empty for the local network." },
	{ "network.brokers_file", _config_string, "\"brokers.txt\"",
		"HALO_NET_BROKERS_FILE", _environment_value, _platform_all | _platform_xbox,
		"The file of MQTT brokers that connect invites, one host:port per line,\n"
		"up to 4. Updates replace brokers.txt, so keep your own list under\n"
		"another name." },
	{ "network.stun_servers", _config_string, "\"stun.l.google.com:19302,stun.cloudflare.com:3478\"",
		"HALO_NET_STUN", _environment_value, _platform_all | _platform_xbox,
		"STUN servers that find this machine's internet address, as host:port,\n"
		"separated by commas." },
	{ "network.announce", _config_string, "\"latest\"", "HALO_NET_ANNOUNCE", _environment_value, _platform_all,
		"The OpenCE network version the games you host announce. \"latest\"\n"
		"follows the newest the signed legacy table proves, so OpenCE's newest\n"
		"players see your games. A number pins it, like 32 for a server meant\n"
		"for console players on older builds." },
	{ "network.legacy_table", _config_string, "\"\"", "HALO_LEGACY_TABLE", _environment_value, _platform_all,
		"For testing only: an unsigned legacy table file that sets the OpenCE\n"
		"network versions (docs/delta.md). Leave it empty for none." },

	/* ---- launcher: the setup screen (launcher.c) */
	{ "launcher.show_at_start", _config_string, "\"when_needed\"", "HALO_LAUNCHER_SHOW", _environment_value,
		_platform_desktop,
		"When to show the setup screen before the game: \"when_needed\" or\n"
		"\"always\". \"when_needed\" shows it on the first start, when maps are\n"
		"missing, with --setup, or when you hold Shift or a controller's Back\n"
		"as Chupathingy starts." },
	{ "launcher.hold_to_open", _config_boolean, "true", "HALO_LAUNCHER_HOLD", _environment_value, _platform_desktop,
		"Holding Shift or a controller's Back (View) button while Chupathingy\n"
		"starts opens the setup screen." },
	{ "launcher.mode_choice", _config_string, "\"ask\"", "HALO_LAUNCHER_MODE_CHOICE", _environment_value,
		_platform_desktop,
		"What to do when there is more than one way to play, like a map pack\n"
		"with its own menus. \"ask\" lets you pick on the setup screen each\n"
		"time. \"default\" starts in launcher.default_mode. The --play and\n"
		"--mode <id> options skip the question." },
	{ "launcher.default_mode", _config_string, "\"vanilla\"", "HALO_LAUNCHER_DEFAULT_MODE", _environment_value,
		_platform_desktop,
		"The way to play when launcher.mode_choice is \"default\": \"vanilla\",\n"
		"or a pack's id like \"cex\" for Halo CE+." },

#ifndef HALO_CONSOLE_NO_UPDATER
	/* ---- update */
	{ "update.auto", _config_boolean, "true", "HALO_UPDATE_AUTO", _environment_value, _platform_all,
		"Check for a new version when Chupathingy starts." },
#endif

#ifndef HALO_CONSOLE_NO_DISCORD
	/* ---- discord (the project's Discord application, "Chupathingy") */
	{ "discord.presence", _config_string, "\"full\"", "HALO_DISCORD_PRESENCE", _environment_value,
		_platform_desktop,
		"What Discord shows: \"full\", \"minimal\" or \"off\"." },
	{ "discord.show_server_name", _config_boolean, "true", "HALO_DISCORD_SHOW_SERVER_NAME", _environment_value,
		_platform_desktop,
		"Show the network game's name on Discord." },
	{ "discord.show_join", _config_boolean, "true", "HALO_DISCORD_SHOW_JOIN", _environment_value,
		_platform_desktop,
		"Show a Join game button on Discord while you host a public game." },
	{ "discord.application_id", _config_string, "\"1560397272166957136\"", "HALO_DISCORD_APPLICATION",
		_environment_value, _platform_desktop,
		"The Discord application Chupathingy shows as. Leave it empty for none." },
#endif

	/* ---- crash_reports */
	{ "crash_reports.upload", _config_string, "\"ask\"", "HALO_CRASH_REPORTS", _environment_value, _platform_desktop,
		"Send crash reports to the developers: \"ask\", \"yes\" or \"no\"." },

	/* ---- paths */
	{ "paths.data", _config_string, "\"\"", "HALO_DATA_ROOT", _environment_value, _platform_desktop,
		"The folder that holds the maps folder. Leave it empty for the working\n"
		"folder and its assets folder. Use single quotes for Windows paths, like\n"
		"'C:\\Games\\Halo'." },
	{ "paths.saves", _config_string, "\"\"", "HALO_SAVE_ROOT", _environment_value, _platform_desktop,
		"Where saved games and profiles go. Leave it empty for the usual place." },
	{ "paths.logs", _config_string, "\"\"", "HALO_LOG_ROOT", _environment_value, _platform_desktop,
		"Where logs and crash reports go. Leave it empty for logs/ next to\n"
		"maps." },
	{ "paths.cache", _config_string, "\"\"", "HALO_CACHE_ROOT", _environment_value, _platform_desktop,
		"Where Chupathingy keeps its cache. Leave it empty for cache/ in the\n"
		"save folder." },

#ifdef HALO_SWITCH_CONSOLE
	/* ---- performance (the Switch host's: port/switch/host/host_main.c) */
	{ "performance.cpu_clock", _config_string, "\"1785\"", "HALO_CPU_CLOCK", _environment_value, _platform_android,
		"The CPU's clock. \"1785\" is 1.7 GHz, the most Nintendo's own games\n"
		"use. \"1020\" is 1 GHz, the console's handheld rate: it runs cooler and\n"
		"the battery lasts longer. \"stock\" leaves it as the console has it.\n"
		"Changes at once." },
#endif

	/* ---- debug: the Advanced part of the file, for testing and support */
	{ "debug.log_addresses", _config_boolean, "false", "HALO_LOG_ADDRESSES", _environment_set_is_true, _platform_all,
		"Write full internet addresses in debug.txt and the console. Don't\n"
		"post a debug.txt written with this on." },
	{ "debug.telnet_console", _config_boolean, "false", "HALO_TELNET_CONSOLE", _environment_set_is_true, _platform_all,
		"Open a console on 127.0.0.1, at debug.telnet_console_port. It has no\n"
		"password." },
	{ "debug.telnet_console_port", _config_integer, "2323", "HALO_TELNET_CONSOLE_PORT", _environment_value,
		_platform_all,
		"The telnet console's port." },
	{ "debug.network_test", _config_string, "\"\"", "HALO_NETWORK_TEST", _environment_value, _platform_all,
		"Automated system link tests: \"host:<map>\" or \"join\". Leave it empty\n"
		"for none." },
	{ "debug.network_test_start", _config_real, "15.0", "HALO_NETWORK_TEST_START", _environment_value, _platform_all,
		"Seconds after hosting before an automated test game starts." },
	{ "debug.network_test_kill", _config_real, "0.0", "HALO_NETWORK_TEST_KILL", _environment_value,
		_platform_all,
		"An automated test host kills its last player every this many seconds.\n"
		"0 never does." },
	{ "debug.network_test_team_change", _config_real, "0.0", "HALO_NETWORK_TEST_TEAM_CHANGE", _environment_value,
		_platform_all,
		"A test player changes teams every this many seconds. 0 never does." },
	{ "debug.network_test_pause", _config_real, "0.0", "HALO_NETWORK_TEST_PAUSE", _environment_value, _platform_all,
		"Seconds into a test game to open the pause menu. 0 never does." },
	{ "debug.network_test_moderate", _config_string, "\"\"", "HALO_NETWORK_TEST_MODERATE", _environment_value,
		_platform_all,
		"A test host's Moderation action on its first other player, like\n"
		"\"Kick\" or \"Ban forever\". Leave it empty for none." },
	{ "debug.network_test_moderate_time", _config_real, "20.0", "HALO_NETWORK_TEST_MODERATE_TIME", _environment_value,
		_platform_all,
		"Seconds into a test game before the host takes its Moderation action." },
	{ "debug.network_test_local_players", _config_integer, "1", "HALO_NETWORK_TEST_LOCAL_PLAYERS", _environment_value,
		_platform_all,
		"A test machine's players, from 1 to 4." },
	{ "debug.network_test_score", _config_integer, "0", "HALO_NETWORK_TEST_SCORE", _environment_value, _platform_all,
		"The score a test game plays to. 0 uses the game type's own." },
	{ "debug.network_test_shoot", _config_real, "0.0", "HALO_NETWORK_TEST_SHOOT", _environment_value, _platform_all,
		"Each test player hits the next one every this many seconds. 0 never\n"
		"does." },
	{ "debug.network_test_vehicle", _config_real, "0.0", "HALO_NETWORK_TEST_VEHICLE", _environment_value, _platform_all,
		"Seconds into a test game to seat the last player in a vehicle. 0 never\n"
		"does." },
	{ "debug.network_test_pickup", _config_real, "0.0", "HALO_NETWORK_TEST_PICKUP", _environment_value, _platform_all,
		"Seconds into a test game to stand the last player on a weapon. 0 never\n"
		"does." },
	{ "debug.network_test_pickup_weapon", _config_string, "\"\"", "HALO_NETWORK_TEST_PICKUP_WEAPON", _environment_value,
		_platform_all,
		"The weapon for debug.network_test_pickup, by part of its tag name.\n"
		"Leave it empty for any." },
	{ "debug.network_test_bomb", _config_real, "0.0", "HALO_NETWORK_TEST_BOMB", _environment_value, _platform_all,
		"For an automated test Bomb game (host:<map>:grifball). After this many\n"
		"seconds, and again each time that long after, the last player is stood\n"
		"on the bomb. Three seconds later they are put in the goal their team\n"
		"attacks. 0 never does." },
	{ "debug.network_test_bomb_in_goal", _config_boolean, "false", "HALO_NETWORK_TEST_BOMB_IN_GOAL",
		_environment_value, _platform_all,
		"With debug.network_test_bomb: the host first lays the bomb in the goal\n"
		"the last player's team attacks, then stands the player on it. That is\n"
		"like a bomb thrown down in the goal and picked up again." },
	{ "debug.network_test_bomb_throw", _config_boolean, "false", "HALO_NETWORK_TEST_BOMB_THROW",
		_environment_value, _platform_all,
		"With debug.network_test_bomb: the joining machine's player holds the\n"
		"trigger for the first four seconds of each try. They pick the bomb up\n"
		"with it held, throw it down and pick it up again." },
	{ "debug.network_test_bomb_keep", _config_boolean, "false", "HALO_NETWORK_TEST_BOMB_KEEP", _environment_value,
		_platform_all,
		"With debug.network_test_bomb: the carrier is not moved into the goal\n"
		"and keeps the bomb. debug.network_test_melee's hits then strike a\n"
		"carrier." },
	{ "debug.network_test_bomb_save", _config_boolean, "false", "HALO_NETWORK_TEST_BOMB_SAVE", _environment_value,
		_platform_all,
		"With debug.network_test_bomb: as the planted bomb arms, the host stands\n"
		"a player of the goal's team on it, which saves it. See\n"
		"game.bomb_arm_time." },
	{ "debug.network_test_fall", _config_real, "0.0", "HALO_NETWORK_TEST_FALL", _environment_value, _platform_all,
		"In an automated test game, the last player is lifted 10 world units\n"
		"every this many seconds, for a fall that kills. 0 never does." },
	{ "debug.network_test_melee", _config_real, "0.0", "HALO_NETWORK_TEST_MELEE", _environment_value, _platform_all,
		"Every this many seconds, an automated test host brings its last player\n"
		"in front of its first, and one strikes the other with melee. The host's\n"
		"player and the joining machine's take turns. 0 never does." },
	{ "debug.network_test_password", _config_string, "\"\"", "HALO_NETWORK_TEST_PASSWORD", _environment_value,
		_platform_all,
		"With debug.network_test: the host's game password, like Server Setup's\n"
		"PASSWORD. A joining machine types it when its invite asks for one.\n"
		"Leave it empty for none." },
	{ "debug.network_test_invite_file", _config_string, "\"\"", "HALO_NETWORK_TEST_INVITE_FILE", _environment_value,
		_platform_all,
		"With debug.network_test's host: a file the host writes its invite link\n"
		"to once it has one, for a joining machine's command line. Leave it\n"
		"empty for none." },
	{ "debug.network_latency", _config_real, "0.0", "HALO_NETWORK_LATENCY", _environment_value, _platform_all,
		"Milliseconds to delay everything received. 0 for none." },
	{ "debug.network_loss", _config_real, "0.0", "HALO_NETWORK_LOSS", _environment_value, _platform_all,
		"Percent of received datagrams to drop. 0 for none." },
	{ "debug.network_corrupt", _config_real, "0.0", "HALO_NETWORK_CORRUPT", _environment_value, _platform_all,
		"Percent of received datagrams to damage. 0 for none." },
	{ "debug.network_corrupt_stream", _config_real, "0.0", "HALO_NETWORK_CORRUPT_STREAM", _environment_value,
		_platform_all,
		"Percent of stream reads to damage. 0 for none." },
	{ "debug.network_corrupt_after", _config_real, "0.0", "HALO_NETWORK_CORRUPT_AFTER", _environment_value,
		_platform_all,
		"Seconds after the start before anything is damaged." },
	{ "debug.touch_targets", _config_boolean, "false", "HALO_TOUCH_TARGETS", _environment_set_is_true, _platform_all,
		"Outline the menus' tap targets and log each tap." },
	{ "debug.launcher_test", _config_string, "\"\"", "HALO_LAUNCHER_TEST", _environment_value, _platform_desktop,
		"Scripted input for the setup screen, for tests: steps like\n"
		"\"down,a,shot:maps,quit\" (launcher.c). Leave it empty for none." },
	{ "debug.dump_url", _config_string, "\"\"", "HALO_DUMP_URL", _environment_value, _platform_desktop,
		"Where Send my logs sends them, for tests. Leave it empty for the log\n"
		"dump site." },
	{ "debug.test_input", _config_string, "\"\"", "HALO_TEST_INPUT", _environment_value, _platform_all,
		"Scripted input on controller 1: \"bot:<seed>\" or \"look:<seed>\". Leave\n"
		"it empty for none." },
	{ "debug.determinism", _config_string, "\"\"", "HALO_DETERMINISM", _environment_value, _platform_all,
		"The input replay check (tools/test_determinism.py). \"record:<file>\"\n"
		"writes each tick's player actions to the file. \"replay:<file>\" plays\n"
		"them back in place of the controllers. Leave it empty for none." },
	{ "debug.determinism_hashes", _config_string, "\"\"", "HALO_DETERMINISM_HASHES", _environment_value,
		_platform_all,
		"With debug.determinism: a file that gets a line of hashes of the game's\n"
		"state after each tick. Leave it empty for none." },
	{ "debug.determinism_ticks", _config_integer, "0", "HALO_DETERMINISM_TICKS", _environment_value, _platform_all,
		"With debug.determinism: quit after this many game ticks. 0 never does." },
	{ "debug.determinism_seed", _config_integer, "1", "HALO_DETERMINISM_SEED", _environment_value, _platform_all,
		"With debug.determinism: the game's random seed. A replay uses its\n"
		"recording's." },
	{ "debug.test_gamepads", _config_integer, "0", "HALO_TEST_GAMEPADS", _environment_value, _platform_desktop,
		"Virtual gamepads for split screen tests, up to 4. 0 for none.\n"
		"debug.test_input's \"menu:\" script presses their buttons and sticks as\n"
		"pad<n>:<button>. pad<n>:unplug and pad<n>:plug take one out and put\n"
		"it back." },
#ifndef HALO_CONSOLE_NO_UPDATER
	{ "debug.update_answer", _config_string, "\"\"", "HALO_UPDATE_ANSWER", _environment_value, _platform_desktop,
		"The answer to the update question, for tests: \"yes\", \"no\" or\n"
		"\"never\". Leave it empty to ask." },
#endif
	{ "debug.chat_test", _config_string, "\"\"", "HALO_CHAT_TEST", _environment_value, _platform_all,
		"Scripted chat for tests, as steps separated by ;. Each step is\n"
		"\"<seconds>:<all|team|server|mute|unmute|localmute|log|close|type>[:<text>]\".\n"
		"Leave it empty for none." },
#ifndef HALO_CONSOLE_NO_AUDIO_PROBE
	{ "debug.audio_probe", _config_string, "\"\"", "HALO_AUDIO_PROBE", _environment_value, _platform_all | _platform_xbox,
		"A probe file to run (port/linux/game/audio_probe.c), named the way\n"
		"Halo names its files. d:\\ is the game's folder, which on the desktop\n"
		"is the data folder. Chupathingy loads the probe's map, plays its sounds\n"
		"at its spots, records what it plays next to the file, and quits. Compare\n"
		"the recordings with tools/audio_probe_compare.py. Leave it empty for\n"
		"none." },
	{ "debug.audio_capture_seconds", _config_real, "0.0", "HALO_AUDIO_CAPTURE_SECONDS", _environment_value,
		_platform_all | _platform_xbox,
		"Record this many seconds of the sound of each game played: solo,\n"
		"system link or online. A log of the player's position and every sound\n"
		"started goes with it, to d:\\audio_probe\\capture-<map>-<n>-*. That\n"
		"folder must exist (port/linux/game/audio_probe.c). 0 for none." },
#endif
	{ "debug.exit_after", _config_real, "0.0", "HALO_EXIT_AFTER", _environment_value, _platform_all,
		"Quit this many seconds after the window opens. 0 never does." },
	{ "debug.hidden_window", _config_boolean, "false", "HALO_HIDDEN_WINDOW", _environment_set_is_true, _platform_desktop,
		"Keep the window hidden, and never fullscreen." },
	{ "debug.null_renderer", _config_boolean, "false", "HALO_NULL_RENDERER", _environment_set_is_true, _platform_all,
		"Run without a window, and draw nothing." },
	{ "debug.gl_debug", _config_boolean, "false", "HALO_GL_DEBUG", _environment_set_is_true, _platform_all,
		"Report OpenGL errors in the log." },
	{ "debug.menu_open", _config_string, "\"\"", "HALO_MENU_OPEN", _environment_value, _platform_all,
		"Start on this menu screen instead of the main menu. Leave it empty for\n"
		"the main menu." },
	{ "debug.gpu_flush_draws", _config_integer, "-1", "HALO_GPU_FLUSH_DRAWS", _environment_value, _platform_desktop,
		"Flush the GPU every this many draws. -1 means every 3 on Intel with\n"
		"Mesa. 0 never does." },
	{ "debug.gpu_stats", _config_boolean, "false", "HALO_GPU_STATS", _environment_set_is_true, _platform_all,
		"Log the renderer's draw counts once a second." },
	{ "debug.gpu_trace_frame", _config_integer, "-1", "HALO_GPU_TRACE", _environment_value, _platform_all,
		"Log every draw of this frame. -1 for none." },
	{ "debug.gpu_trace_constants", _config_boolean, "false", "HALO_GPU_TRACE_CONSTANTS", _environment_set_is_true, _platform_all,
		"With debug.gpu_trace_frame, also log the vertex shader constants." },
	{ "debug.gpu_skip_vertex_shaders", _config_string, "\"\"", "HALO_GPU_SKIP_VS", _environment_value, _platform_all,
		"Ids of vertex shaders not to draw with, separated by commas." },
	{ "debug.gpu_dump_shaders", _config_string, "\"\"", "HALO_GPU_DUMP_SHADERS", _environment_value, _platform_all,
		"A folder to write the generated GLSL to. Leave it empty for none." },
	{ "debug.gpu_debug_expression", _config_string, "\"\"", "HALO_GPU_DEBUG_EXPR", _environment_value, _platform_all,
		"A GLSL expression every pixel shader shows instead of its result." },
	{ "debug.gpu_debug_texture0", _config_boolean, "false", "HALO_GPU_DEBUG_T0", _environment_set_is_true, _platform_all,
		"Pixel shaders show their first texture." },
	{ "debug.gpu_debug_flat", _config_boolean, "false", "HALO_GPU_DEBUG_FLAT", _environment_set_is_true, _platform_all,
		"Pixel shaders show their vertex color." },
	{ "debug.screenshot_directory", _config_string, "\"\"", "HALO_SCREENSHOT_DIR", _environment_value, _platform_all,
		"A folder to save frames to, with debug.screenshot_every. Leave it empty\n"
		"for none." },
	{ "debug.screenshot_every", _config_integer, "0", "HALO_SCREENSHOT_EVERY", _environment_value, _platform_all,
		"Save every this many frames to debug.screenshot_directory. 0 for none." },
	{ "debug.screenshot_window", _config_boolean, "false", "HALO_SCREENSHOT_WINDOW", _environment_set_is_true,
		_platform_all,
		"With debug.screenshot_every, save the window as shown instead of the\n"
		"game's frame." },
	{ "debug.texture_dump_directory", _config_string, "\"\"", "HALO_TEXTURE_DUMP", _environment_value, _platform_all,
		"A folder to write every texture to as it is uploaded. Leave it empty\n"
		"for none." },
#ifndef HALO_CONSOLE_NO_DISCORD
	{ "debug.discord_log", _config_boolean, "false", "HALO_DISCORD_LOG", _environment_set_is_true,
		_platform_desktop,
		"Log each activity sent to Discord." },
#endif
	{ "debug.texture_log", _config_boolean, "false", "HALO_TEXTURE_LOG", _environment_set_is_true, _platform_all,
		"Log texture uploads." },
	{ "debug.texture_no_cache", _config_boolean, "false", "HALO_TEXTURE_NO_CACHE", _environment_set_is_true, _platform_all,
		"Upload textures again every time they are used." },
	{ "debug.sample_seconds", _config_real, "0.0", "HALO_SAMPLE", _environment_value, _platform_android,
		"Log where every game thread is this often, in seconds. 0 never does." },
#ifdef HALO_SWITCH_CONSOLE
	/* the Switch's: read by the deko3d renderer in the guest
	(port/switch/guest/d3d8_dk.c, nv2a_psh_dk.c) and by the host from
	config.toml itself (port/switch/host/host_main.c, host_dk.c) */
	{ "debug.log_files", _config_boolean, "true", "HALO_LOG_FILES", _environment_value, _platform_android,
		"Write the logs to files: debug.txt next to the data, and halo.log.\n"
		"false writes neither, which saves the card a write for each line. Then\n"
		"nothing is kept to say what went wrong. halo.log follows the change at\n"
		"the next start." },
	{ "debug.resolution_uncapped", _config_boolean, "false", "HALO_RESOLUTION_UNCAPPED", _environment_value,
		_platform_android,
		"Draw at display.resolution_limit's lines even past what the console\n"
		"shows, scaled down to fit. A Switch Lite (720p) can draw 1080p this way,\n"
		"to test a docked console's GPU load. Changes at once." },
	{ "debug.mic_probe", _config_boolean, "false", "HALO_MIC_PROBE", _environment_set_is_true, _platform_android,
		"For two minutes from the start, log what the console's microphone\n"
		"hears: its inputs, the format it gives and the sound's level each\n"
		"second (port/switch/host/host_mic_probe.c). Takes effect at the next\n"
		"start." },
	{ "debug.gpu_trace", _config_boolean, "false", "HALO_GPU_TRACE_FAULTS", _environment_set_is_true, _platform_android,
		"Keep a record of the GPU's last commands. After a GPU fault, write it\n"
		"and the console's account of the fault to halo.log\n"
		"(port/switch/host/host_dk.c). It costs the GPU some time. Takes effect\n"
		"at the next start." },
	{ "debug.gpu_fault_test", _config_boolean, "false", "HALO_GPU_FAULT_TEST", _environment_set_is_true,
		_platform_android,
		"Make the GPU fault on purpose at the 1200th frame, to see\n"
		"debug.gpu_trace's record. Takes effect at the next start." },
	{ "debug.gpu_profile", _config_boolean, "false", "HALO_GPU_PROFILE", _environment_set_is_true, _platform_android,
		"Every 300 frames, log the GPU's time for each part of the frame, like\n"
		"the sky, the objects and the fog, and what a frame asks of it. Takes\n"
		"effect at the next start." },
	{ "debug.gpu_capture", _config_boolean, "false", "HALO_GPU_CAPTURE", _environment_set_is_true, _platform_android,
		"With debug.gpu_profile: time one frame in every 300, draw by draw, and\n"
		"log the GPU's time by shader, target and kind of command. Takes effect\n"
		"at the next start." },
	{ "debug.gpu_experiment", _config_string, "\"\"", "HALO_GPU_EXPERIMENT", _environment_value, _platform_android,
		"Measure what a change could save, as a list separated by commas.\n"
		"\"no_compression\" draws render targets without hardware compression.\n"
		"\"no_target_barriers\" skips the wait before a render target change,\n"
		"and may draw wrong. \"lod_bias\" samples textures three levels smaller,\n"
		"so they blur. Leave it empty for none. Takes effect at the next start." },
	{ "debug.gpu_shader_ranges", _config_boolean, "true", "HALO_GPU_SHADER_RANGES", _environment_value,
		_platform_android,
		"Leave out the pixel shader clamps their values can't need\n"
		"(port/switch/guest/nv2a_psh_dk.c). false writes them all, as before,\n"
		"to compare. Takes effect at the next start." },
	{ "debug.gpu_texture_clip", _config_string, "\"on\"", "HALO_GPU_TEXTURE_CLIP", _environment_value,
		_platform_android,
		"Draw shadows cast on the level only over their map: \"on\" or \"off\".\n"
		"\"wide\" clips them at limits nothing reaches, to measure the clip's\n"
		"own effect. Takes effect at the next start." },
	{ "debug.original_zoom", _config_boolean, "false", "HALO_ORIGINAL_ZOOM", _environment_value, _platform_android,
		"Draw the zoom's blur the way the game does, in passes over the whole\n"
		"screen. false draws it on a small copy and blends that over once, which\n"
		"takes about 3.7 ms less of the GPU's frame at 720p. Takes effect at\n"
		"the next start." },
	{ "debug.gpu_blend_discard", _config_boolean, "true", "HALO_GPU_BLEND_DISCARD", _environment_value,
		_platform_android,
		"Skip a blended draw's pixels that would leave the screen as it was,\n"
		"like the empty parts of particles and glows. false blends them all,\n"
		"as before, to compare. Takes effect at the next start." },
	{ "debug.render_thread", _config_boolean, "true", "HALO_RENDER_THREAD", _environment_value, _platform_android,
		"Turn the game's drawing commands into the GPU's on a thread of their\n"
		"own, on another core, while the game goes on with its next frame.\n"
		"false runs them on the game's thread, as before. Takes effect at the\n"
		"next start." },
	{ "debug.gpu_draw_scissor", _config_boolean, "true", "HALO_GPU_DRAW_SCISSOR", _environment_value,
		_platform_android,
		"Draw an object's shadow and a dynamic light's pass only inside the\n"
		"part of the screen they can change. false draws their whole\n"
		"triangles, as before, to compare. Takes effect at the next start." },
	{ "debug.original_blur", _config_boolean, "false", "HALO_ORIGINAL_BLUR", _environment_value, _platform_android,
		"Draw a blur screen effect, as in cutscenes, the way the game does: in\n"
		"passes over the whole screen. false draws its passes on a small copy.\n"
		"Takes effect at the next start." },
	{ "debug.gpu_glsl_folder", _config_string, "\"\"", "HALO_GPU_GLSL_FOLDER", _environment_value, _platform_android,
		"A folder to write each shader the game draws with to, as GLSL named by\n"
		"its hash. Those are the names debug.gpu_capture logs. The folder must\n"
		"exist. Leave it empty for none." },
	{ "debug.profiler", _config_boolean, "false", "HALO_PROFILER", _environment_set_is_true, _platform_android,
		"Profile Chupathingy. It samples where its threads are debug.profile_hz\n"
		"times a second, and writes the counts to the profile folder every 20\n"
		"seconds, for tools/switch_profile.py. Takes effect at the next start." },
	{ "debug.profile_hz", _config_integer, "500", "HALO_PROFILE_HZ", _environment_value, _platform_android,
		"The profiler's samples a second, when debug.profiler is on." },
	{ "debug.texture_verify_compressed", _config_boolean, "false", "HALO_TEXTURE_VERIFY_COMPRESSED",
		_environment_set_is_true, _platform_android,
		"Read each DXT-compressed texture back after it is written, and log\n"
		"where it differs from the game's bytes (port/switch/host/host_dk.c).\n"
		"It is slow: it waits for the GPU each time. Takes effect at the next\n"
		"start." },
#endif

	/* ---- console: the original Xbox's own (port/xbox/src/xbox_config.c),
	once empty files beside default.xbe (D:\memory_64.txt and the rest), and
	the consoles' on the shared GL layer */
	{ "console.bypass_security", _config_boolean, "false", NULL, _environment_value, _platform_xbox,
		"Start the network in its open mode, XNet without its security.\n"
		"Internet play, the game list and play with the PC builds need it.\n"
		"false keeps System Link's security, with Xboxes only.\n"
		"tools/xbox_package.py writes true unless you pass --secure. Read when\n"
		"Chupathingy starts." },
	{ "console.memory_class", _config_string, "\"auto\"", NULL, _environment_value, _platform_xbox,
		"The console's memory class. \"auto\" uses the console's own. A console\n"
		"with 128 MB or more, like the common upgrade or a devkit, gets extras:\n"
		"larger texture and sound caches, render buffers and network queues.\n"
		"\"64\" uses a stock Xbox's sizes on any console, to test the 64 MB\n"
		"baseline or when an extra misbehaves. The game itself stays the same." },
	{ "console.delta", _config_string, "\"auto\"", NULL, _environment_value, _platform_xbox,
		"Delta Peer. When you join a game whose host offers it, tell the host\n"
		"this is an Xbox: its platform and memory, nothing else. The stats site,\n"
		"the scoreboard and the server then show Xbox. \"auto\" is on. \"off\"\n"
		"uses the legacy protocol alone. The game plays the same either way." },
	{ "console.game_list_server", _config_string, "\"\"", NULL, _environment_value, _platform_xbox,
		"Where to get the game list when warthog.milenko.org does not resolve:\n"
		"\"host\" or \"a.b.c.d\", with \":port\" if needed. Leave it empty for\n"
		"none." },
	{ "console.join", _config_string, "\"\"", NULL, _environment_value, _platform_xbox,
		"For testing joins without the game list. System Link joins a host's\n"
		"LAN address, like \"192.168.1.20\", directly. Y in ONLINE GAMES joins\n"
		"an invite, like \"halo://join/...\". Leave it empty for none." },
	{ "console.trace", _config_boolean, "false", NULL, _environment_value, _platform_xbox,
		"Detailed traces in debug.txt, for reporting a join that fails: the\n"
		"main loop each second, every hop of an internet join and the games\n"
		"advertised. Other hosts show only as tags, never by their addresses." },
	{ "console.log_flush", _config_string, "\"buffered\"", NULL, _environment_value, _platform_xbox,
		"When debug.txt is written. \"buffered\" keeps lines in memory and\n"
		"writes them together: once a second, when 8 KB are waiting, and at\n"
		"once for an assert, an exit or a crash. \"every_line\" writes each line\n"
		"as it comes. That is slow, with a file opened for each, but it keeps\n"
		"even the last line before the console stops, for a crash hunt." },
	{ "console.perf_log", _config_boolean, "false", NULL, _environment_value, _platform_xbox | _platform_console_gl,
		"For finding what makes a scene slow. Once a second, write a line to\n"
		"debug.txt with the frame time, average and worst, and the time each\n"
		"part of the frame took: game ticks, the render's parts, sound, network,\n"
		"present and the wait for the screen. The line also has the draw calls\n"
		"and objects drawn. Zooming in or out adds a line. Read when\n"
		"Chupathingy starts." },
	{ "console.perf_gpu_sync", _config_boolean, "false", NULL, _environment_value, _platform_xbox,
		"With console.perf_log: wait for the graphics chip to finish at each\n"
		"timed part of the render. Each part's time is then what the chip spent\n"
		"drawing it, not only the processor's share. It slows the game down.\n"
		"Read when Chupathingy starts." },
	{ "console.visibility_test_runs", _config_boolean, "true", NULL, _environment_value, _platform_console_gl,
		"Draw the lens flares' visibility tests as one run. After the first,\n"
		"each test keeps the first one's state and is only a query and a draw\n"
		"call. The picture is the same. false draws each test in full, to\n"
		"compare. Read when Chupathingy starts." },
	{ "console.widget_pool_log", _config_boolean, "false", NULL, _environment_value,
		_platform_xbox | _platform_console_gl,
		"For sizing the menus' memory: write a line to debug.txt each time the\n"
		"menus reach a new high in widget memory, by 1 KB or more, with the\n"
		"pool's size. Read once the menus start." },
	{ "console.debug_geometry", _config_boolean, "false", NULL, _environment_value, _platform_xbox,
		"For development: the debug build's debug geometry, the lines and\n"
		"triangles that debug commands draw. Its buffers take 1440 KB. It is\n"
		"off because the retail game had none, and a stock 64 MB console may\n"
		"not have the room." },
	{ "console.simulate_64mb", _config_boolean, "false", NULL, _environment_value, _platform_xbox,
		"No longer used. A devkit gets 64 MB from the XBE's own flag now, set\n"
		"with tools/xbox_package.py --devkit-64mb." },
	{ "console.large_caches", _config_boolean, "false", NULL, _environment_value, _platform_xbox,
		"No longer used. A console with 128 MB or more gets the larger caches\n"
		"on its own now (console.memory_class)." },
};

#define NUMBER_OF_CONFIG_SETTINGS (sizeof(config_settings) / sizeof(config_settings[0]))

/* The first line of the block before the Advanced part of a new file
(config_default_text). A setting added to the section before it goes above
it (config_add_missing). */
#define CONFIG_ADVANCED_MARK "# ---- Advanced"

/* The file's sections, in the order a new file has them, with the short
note written under each one's [header]. The advanced ones come last, after
the Advanced block. A section not listed here goes before them. */
static const struct
{
	const char *name;
	int advanced;
	const char *intro;
} config_sections[] =
{
	{ "display", 0,
		"The picture and what shows on screen. Video Setup in the menus\n"
		"changes many of these. The first few are the ones most people change." },
	{ "audio", 0,
		"Volumes, the sound output, surround and Headphone 3D. Audio Setup in\n"
		"the menus changes most of these." },
	{ "input", 0,
		"Mouse, touch and gyro aiming. The keys are in [controls]." },
	{ "controls", 0,
		"Keyboard and mouse controls. Settings > Controls Setup changes them.\n"
		"Each control takes up to two inputs, separated by a comma, like\n"
		"\"F, Mouse 4\". Keys use their US keyboard names, like \"W\", \"Space\",\n"
		"\"Left Ctrl\" or \"F1\". The mouse has \"Mouse Left\", \"Mouse Right\",\n"
		"\"Mouse Middle\", \"Mouse 4\", \"Mouse 5\", \"Wheel\", \"Wheel Up\" and\n"
		"\"Wheel Down\". Leave a control empty for none." },
	{ "chat", 0,
		"The text chat you see and send. The chat rules for games you host are\n"
		"in [network]." },
	{ "game", 0,
		"How games play. The bomb settings are for Bomb and Grifball games you\n"
		"host." },
	{ "network", 0,
		"Online play, hosting, co-op and the game list. Network Setup and\n"
		"Server Setup in the menus change many of these. Most people never\n"
		"need the ones near the end." },
	{ "launcher", 0,
		"The setup screen that can open before the game." },
	{ "update", 0,
		"Checking for new versions." },
	{ "discord", 0,
		"What Discord shows while you play." },
	{ "crash_reports", 0,
		"Crash reports." },
	{ "paths", 0,
		"Where Chupathingy finds and keeps its files. Leave these empty unless\n"
		"you moved something." },
	{ "performance", 0,
		"The console's processor speed." },
	{ "debug", 1,
		"Logs, test scripts and renderer checks." },
	{ "console", 1,
		"The consoles' own settings: the network's security, memory, logs and\n"
		"tests." },
};

#define NUMBER_OF_CONFIG_SECTIONS (sizeof(config_sections) / sizeof(config_sections[0]))

#ifdef HALO_ANDROID
#define CONFIG_PLATFORM _platform_android
#elif defined(HALO_XBOX_CONSOLE)
#define CONFIG_PLATFORM _platform_xbox
#elif defined(HALO_CONSOLE_GL_LAYER) && defined(_WIN32)
#define CONFIG_PLATFORM (_platform_desktop | _platform_windows | _platform_console_gl)
#elif defined(HALO_CONSOLE_GL_LAYER)
#define CONFIG_PLATFORM (_platform_desktop | _platform_console_gl)
#elif defined(_WIN32)
#define CONFIG_PLATFORM (_platform_desktop | _platform_windows)
#else
#define CONFIG_PLATFORM _platform_desktop
#endif

struct config_value
{
	int boolean;
	long integer;
	double real;
	char *string;
};

static struct config_value config_values[NUMBER_OF_CONFIG_SETTINGS];
/* whether the file or the environment gives the setting a value */
static int config_given[NUMBER_OF_CONFIG_SETTINGS];
/* the settings an existing file would see change with this version's
defaults, until the player is asked (config_find_new_defaults): 1 for one
that follows the default (commented out, or not there), 2 for one that an
older version wrote as a value beside the commented one; and the old
default each had, as written */
static int config_pending[NUMBER_OF_CONFIG_SETTINGS];
static const char *config_pending_old[NUMBER_OF_CONFIG_SETTINGS];
static int config_loaded = 0;
#ifndef HALO_CONSOLE_NO_UPDATER
/* update.auto as the file or the environment gave it, in a packaged
install, where update.auto itself is off (config_load) */
static int packaged_update_check = 1;
#endif
static pthread_mutex_t config_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------- the file */

int platform_packaged(void)
{
#if defined(HALO_ANDROID) || defined(HALO_XBOX_CONSOLE)
	return 0;
#else
	static int packaged = -1;

	if (packaged < 0)
	{
		char path[1100];
		SDL_PathInfo information;

		packaged = packaged_install_marked(getenv("HALO_PACKAGED"),
			packaged_install_file_path(path, sizeof(path), SDL_GetBasePath()) &&
			SDL_GetPathInfo(path, &information) && information.type == SDL_PATHTYPE_FILE);
	}
	return packaged;
#endif
}

#if defined(__linux__) && !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
/* whether folder has maps/ or config.toml, in any case */
static int folder_has_setup(const char *folder)
{
	char found[256];

	return posix_find_entry_case_insensitive(folder, "maps", found, sizeof(found)) ||
		posix_find_entry_case_insensitive(folder, "config.toml", found, sizeof(found));
}
#endif

int platform_app_folder(char *path, unsigned long size)
{
#if defined(__APPLE__) && !defined(HALO_XBOX_CONSOLE)
	const char *base = SDL_GetBasePath();
	const char *home = getenv("HOME");

	if (!base || !strstr(base, ".app/Contents/") || !home || !*home)
		return 0;
	snprintf(path, (size_t)size, "%s/Library/Application Support/ChupathingyCE", home);
	SDL_CreateDirectory(path);
	return 1;
#elif defined(__linux__) && !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
	/* an AppImage (which sets APPIMAGE) runs from its own file, mounted
	read-only, and a packaged install (packaged_install.h) from a folder
	its player cannot write: their data and settings go in the save root's
	usual place (save_folder.c), or a test's HALO_SAVE_ROOT. So do those of
	a game in any folder it cannot write, unless the folder has its own
	maps/ or config.toml (a setup made there by hand, kept as it is) */
	const char *appimage = getenv("APPIMAGE");
	const char *save_root = getenv("HALO_SAVE_ROOT");
	const char *data_home = getenv("XDG_DATA_HOME");
	const char *home = getenv("HOME");
	const char *base = SDL_GetBasePath();
	int writable = !base || !*base || access(base, W_OK) == 0;

	if (!packaged_install_files_in_home(appimage && *appimage, platform_packaged(), writable,
			writable || folder_has_setup(base)))
	{
		return 0;
	}
	if (save_root && *save_root)
		snprintf(path, (size_t)size, "%s", save_root);
	else if (data_home && *data_home)
		snprintf(path, (size_t)size, "%s/chupathingyce", data_home);
	else if (home && *home)
		snprintf(path, (size_t)size, "%s/.local/share/chupathingyce", home);
	else
		return 0;
	SDL_CreateDirectory(path);
	return 1;
#else
	(void)path;
	(void)size;
	return 0;
#endif
}

static void config_path(char *path, size_t size)
{
#if !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
	char folder[1024];
#endif
#ifdef __APPLE__
	const char *save_root = getenv("HALO_SAVE_ROOT");

	/* (a copy given a save root of its own, a test's, keeps its settings
	there too, not in the player's) */
	if (save_root && *save_root)
	{
		snprintf(path, size, "%s/config.toml", save_root);
		return;
	}
#endif
#if !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
	/* (the application's folder: not the application, which is signed, or
	the AppImage, which cannot be written) */
	if (platform_app_folder(folder, sizeof(folder)))
	{
		snprintf(path, size, "%s/config.toml", folder);
		return;
	}
#endif
#if defined(HALO_XBOX_CONSOLE)
	/* beside default.xbe */
	snprintf(path, size, "d:\\config.toml");
#elif defined(HALO_ANDROID)
	/* the data folder, which the app names (port/android/host/host_main.c) */
	const char *root = getenv("HALO_DATA_ROOT");

	snprintf(path, size, "%s/config.toml", root && *root ? root : ".");
#else
	/* the executable's folder, with its separator */
	const char *base = SDL_GetBasePath();

	snprintf(path, size, "%sconfig.toml", base ? base : "");
#endif
}

/* the whole file, NUL terminated, or NULL; free() it */
static char *config_read_file(const char *path, size_t *size)
{
#if defined(HALO_ANDROID) || defined(HALO_XBOX_CONSOLE)
	FILE *file = fopen(path, "rb");
	char *text = NULL;
	long length;

	if (!file)
		return NULL;
	if (fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) >= 0 && fseek(file, 0, SEEK_SET) == 0)
	{
		text = malloc((size_t)length + 1);
		if (text && fread(text, 1, (size_t)length, file) == (size_t)length)
		{
			text[length] = 0;
			*size = (size_t)length;
		}
		else
		{
			free(text);
			text = NULL;
		}
	}
	fclose(file);
	return text;
#else
	/* SDL's, for UTF-8 paths on Windows */
	void *data = SDL_LoadFile(path, size);
	char *text;

	if (!data)
		return NULL;
	text = malloc(*size + 1);
	if (text)
	{
		memcpy(text, data, *size);
		text[*size] = 0;
	}
	SDL_free(data);
	return text;
#endif
}

static int config_write_file(const char *path, const char *text)
{
#if defined(HALO_ANDROID) || defined(HALO_XBOX_CONSOLE)
	FILE *file = fopen(path, "wb");
	int written;

	if (!file)
		return 0;
	written = fwrite(text, 1, strlen(text), file) == strlen(text);
#ifdef HALO_SWITCH_CONSOLE
	/* leaving the game is _exit, which does not flush the card; without a
	commit here a setting is kept for this run and gone on the next */
	if (written)
		fsync(fileno(file));
	written = fclose(file) == 0 && written;
	if (written)
		host_storage_commit();
	return written;
#else
	return fclose(file) == 0 && written;
#endif
#else
	return SDL_SaveFile(path, text, strlen(text));
#endif
}

/* the file's layout: 2 writes a setting at its default commented out (1,
before config_version, wrote every value, which kept a default a later
version changed) */
#define CONFIG_VERSION 2
/* the defaults the game has asked about (game.defaults_answered): 1, the
ones of v0.8.0; 2, network.tunnel_port's 2302 (v0.8.1b); 3, a game you host
starting alone (game.start_with_one_player); 4, Headphone 3D on. A file
answered for an earlier generation is asked about the later ones only */
#define CONFIG_DEFAULTS_GENERATION 4
#define CONFIG_VERSION_LINE "config_version = 2\n"
#define CONFIG_VERSION_LINES "# Chupathingy uses this line to update the file. Leave it as it is.\n" CONFIG_VERSION_LINE
#define CONFIG_VERSION_TEXT "\n" CONFIG_VERSION_LINES

struct config_text
{
	char *buffer;
	size_t length, capacity;
};

static void config_append(struct config_text *text, const char *string)
{
	size_t length = strlen(string);

	if (text->length + length + 1 > text->capacity)
	{
		size_t capacity = (text->capacity ? text->capacity : 4096) * 2 + length;
		char *buffer = realloc(text->buffer, capacity);

		if (!buffer)
			return;
		text->buffer = buffer;
		text->capacity = capacity;
	}
	memcpy(text->buffer + text->length, string, length + 1);
	text->length += length;
}

/* the first length characters of text, as a string of their own */
static char *config_copy(const char *text, size_t length)
{
	char *copy = malloc(length + 1);

	if (copy)
	{
		memcpy(copy, text, length);
		copy[length] = 0;
	}
	return copy;
}

/* the line's key, if it is "key = ..." (after spaces), in key */
static int config_line_key(const char *line, const char *end, const char *key)
{
	size_t length = strlen(key);

	while (line < end && (*line == ' ' || *line == '\t'))
		line++;
	if ((size_t)(end - line) <= length || strncmp(line, key, length) != 0)
		return 0;
	line += length;
	while (line < end && (*line == ' ' || *line == '\t'))
		line++;
	return line < end && *line == '=';
}

/* the section the line opens, if it is "[section]" (after spaces) */
static int config_line_section(const char *line, const char *end, char *section, size_t size)
{
	const char *close;

	while (line < end && (*line == ' ' || *line == '\t'))
		line++;
	if (line >= end || *line != '[')
		return 0;
	close = memchr(line, ']', (size_t)(end - line));
	if (!close || (size_t)(close - line - 1) >= size)
		return 0;
	memcpy(section, line + 1, (size_t)(close - line - 1));
	section[close - line - 1] = 0;
	return 1;
}

/* the line's key, if it is a setting written at its default, "# key = ..."
(config_append_setting) */
static int config_line_commented_key(const char *line, const char *end, const char *key)
{
	while (line < end && (*line == ' ' || *line == '\t'))
		line++;
	return line < end && *line == '#' && config_line_key(line + 1, end, key);
}

/* one setting as the file holds it: its comment, and its key at the
default, commented out, so that it follows the default of whichever version
reads the file until the player chooses a value (config_write) */
static void config_append_setting(struct config_text *text, const struct config_setting *setting)
{
	const char *dot = strchr(setting->name, '.');
	const char *line;
	char buffer[256];

	config_append(text, "\n");
	for (line = setting->comment; *line;)
	{
		size_t length = strcspn(line, "\n");

		snprintf(buffer, sizeof(buffer), "# %.*s\n", (int)length, line);
		config_append(text, buffer);
		line += length;
		if (*line)
			line++;
	}
#if !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
	/* (Android apps and the console have no environment to set) */
	switch (setting->environment_style)
	{
	case _environment_value:
		snprintf(buffer, sizeof(buffer), "# (one run: %s=<value>)\n", setting->environment);
		break;
	case _environment_set_is_true:
		snprintf(buffer, sizeof(buffer), "# (one run: %s=1 turns it on)\n", setting->environment);
		break;
	case _environment_set_is_false:
		snprintf(buffer, sizeof(buffer), "# (one run: %s=1 turns it off)\n", setting->environment);
		break;
	}
	config_append(text, buffer);
#endif
	snprintf(buffer, sizeof(buffer), "# %s = %s\n", dot + 1, setting->default_value);
	config_append(text, buffer);
}

/* the top of a new file: what it is, how to change a setting by hand and
how to go back to the defaults */
#if defined(HALO_XBOX_CONSOLE)
#define CONFIG_FILE_HEADER \
	"# Chupathingy settings\n" \
	"#\n" \
	"# This file holds Chupathingy's settings. It sits next to default.xbe.\n" \
	"# The menus change many of them, so you rarely need to edit it.\n" \
	"#\n" \
	"# Each setting has a short note above it. A line that starts with # does\n" \
	"# nothing. A setting you haven't changed is shown commented out at its\n" \
	"# default, and it follows the default of each new version.\n" \
	"#\n" \
	"# To change a setting by hand:\n" \
	"#   1. Remove the \"# \" at the start of its line.\n" \
	"#   2. Change the value. Text goes in quotes. true, false and numbers don't.\n" \
	"#   3. Save the file and restart Chupathingy.\n" \
	"#\n" \
	"# To undo a change, put the \"# \" back. To go back to all the defaults,\n" \
	"# delete this file. Chupathingy writes a new one the next time it starts.\n" \
	"#\n" \
	"# The sections after the Advanced note at the end are for testing and\n" \
	"# support.\n"
#elif defined(HALO_ANDROID)
#define CONFIG_FILE_HEADER \
	"# Chupathingy settings\n" \
	"#\n" \
	"# This file holds Chupathingy's settings. The menus change many of them,\n" \
	"# so you rarely need to edit it.\n" \
	"#\n" \
	"# Each setting has a short note above it. A line that starts with # does\n" \
	"# nothing. A setting you haven't changed is shown commented out at its\n" \
	"# default, and it follows the default of each new version.\n" \
	"#\n" \
	"# To change a setting by hand:\n" \
	"#   1. Remove the \"# \" at the start of its line.\n" \
	"#   2. Change the value. Text goes in quotes. true, false and numbers don't.\n" \
	"#   3. Save the file and restart Chupathingy.\n" \
	"#\n" \
	"# To undo a change, put the \"# \" back. To go back to all the defaults,\n" \
	"# delete this file. Chupathingy writes a new one the next time it starts.\n" \
	"#\n" \
	"# The sections after the Advanced note at the end are for testing and\n" \
	"# support.\n"
#else
#define CONFIG_FILE_HEADER \
	"# Chupathingy settings\n" \
	"#\n" \
	"# This file holds Chupathingy's settings. The setup screen and the menus\n" \
	"# change most of them, so you rarely need to edit it.\n" \
	"#\n" \
	"# Each setting has a short note above it. A line that starts with # does\n" \
	"# nothing. A setting you haven't changed is shown commented out at its\n" \
	"# default, and it follows the default of each new version.\n" \
	"#\n" \
	"# To change a setting by hand:\n" \
	"#   1. Remove the \"# \" at the start of its line.\n" \
	"#   2. Change the value. Text goes in quotes. true, false and numbers don't.\n" \
	"#   3. Save the file and restart Chupathingy.\n" \
	"#\n" \
	"# To undo a change, put the \"# \" back. To go back to all the defaults,\n" \
	"# delete this file. Chupathingy writes a new one the next time it starts.\n" \
	"#\n" \
	"# Each setting's note also names an environment variable. Set it to\n" \
	"# change the setting for one run. It wins over this file.\n" \
	"#\n" \
	"# The sections after the Advanced note at the end are for testing and\n" \
	"# support.\n"
#endif

/* the block before the Advanced part (CONFIG_ADVANCED_MARK first) */
#define CONFIG_ADVANCED_TEXT \
	"\n" \
	CONFIG_ADVANCED_MARK " ----\n" \
	"#\n" \
	"# Everything below is for testing and support. You don't need it to\n" \
	"# play. Change these only when a developer or a bug report asks you to.\n"

/* whether the section is one of this build's: it has a setting for it */
static int config_section_used(const char *section)
{
	size_t index, length = strlen(section);

	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		if ((config_settings[index].platforms & CONFIG_PLATFORM) &&
			!strncmp(config_settings[index].name, section, length) && config_settings[index].name[length] == '.')
		{
			return 1;
		}
	}
	return 0;
}

/* a section of a new file: its header, its note, and its settings in the
table's order */
static void config_append_section(struct config_text *text, const char *section, const char *intro)
{
	size_t index, length = strlen(section);
	char buffer[256];
	const char *line;

	snprintf(buffer, sizeof(buffer), "\n[%s]\n", section);
	config_append(text, buffer);
	for (line = intro ? intro : ""; *line;)
	{
		size_t line_length = strcspn(line, "\n");

		snprintf(buffer, sizeof(buffer), "# %.*s\n", (int)line_length, line);
		config_append(text, buffer);
		line += line_length;
		if (*line)
			line++;
	}
	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const struct config_setting *setting = &config_settings[index];

		if ((setting->platforms & CONFIG_PLATFORM) && !strncmp(setting->name, section, length) &&
			setting->name[length] == '.')
		{
			config_append_setting(text, setting);
		}
	}
}

/* whether the section is in config_sections */
static int config_section_listed(const char *section, size_t length)
{
	size_t index;

	for (index = 0; index < NUMBER_OF_CONFIG_SECTIONS; index++)
	{
		if (strlen(config_sections[index].name) == length && !strncmp(config_sections[index].name, section, length))
			return 1;
	}
	return 0;
}

/* the file with every setting of this build at its default: the sections in
config_sections' order, the advanced ones last after their block, so that
each section is written once */
static char *config_default_text(void)
{
	struct config_text text = { NULL, 0, 0 };
	size_t index;
	int advanced;

	config_append(&text, CONFIG_FILE_HEADER CONFIG_VERSION_TEXT);
	for (advanced = 0; advanced <= 1; advanced++)
	{
		int block_written = 0;

		/* (a section not listed: before the advanced ones) */
		if (advanced)
		{
			char section[64] = "";

			for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
			{
				const struct config_setting *setting = &config_settings[index];
				const char *dot = strchr(setting->name, '.');
				size_t length = dot ? (size_t)(dot - setting->name) : 0;

				if (!dot || !(setting->platforms & CONFIG_PLATFORM) || length >= sizeof(section) ||
					config_section_listed(setting->name, length))
				{
					continue;
				}
				/* (each such section once: written at its first setting) */
				{
					size_t earlier;
					int seen = 0;

					for (earlier = 0; earlier < index && !seen; earlier++)
					{
						seen = (config_settings[earlier].platforms & CONFIG_PLATFORM) &&
							!strncmp(config_settings[earlier].name, setting->name, length + 1);
					}
					if (seen)
						continue;
				}
				snprintf(section, sizeof(section), "%.*s", (int)length, setting->name);
				config_append_section(&text, section, NULL);
			}
		}
		for (index = 0; index < NUMBER_OF_CONFIG_SECTIONS; index++)
		{
			if (config_sections[index].advanced != advanced || !config_section_used(config_sections[index].name))
				continue;
			if (advanced && !block_written)
			{
				config_append(&text, CONFIG_ADVANCED_TEXT);
				block_written = 1;
			}
			config_append_section(&text, config_sections[index].name, config_sections[index].intro);
		}
	}
	return text.buffer;
}

/* whether text has the setting's line in its section: as a value, or (with
commented) also commented out at its default */
static int config_text_has_key(const char *text, const char *name, int commented)
{
	const char *dot = strchr(name, '.');
	char section[64], current[64] = "";
	const char *line;

	snprintf(section, sizeof(section), "%.*s", (int)(dot - name), name);
	for (line = text; *line;)
	{
		const char *end = line + strcspn(line, "\n");

		if (!config_line_section(line, end, current, sizeof(current)) && !strcmp(current, section) &&
			(config_line_key(line, end, dot + 1) || (commented && config_line_commented_key(line, end, dot + 1))))
		{
			return 1;
		}
		line = *end ? end + 1 : end;
	}
	return 0;
}

/* the settings of this build that text (the file, parsed as table) lacks,
neither a value nor commented out at a default, added to it in their
sections (commented out), keeping the rest as it is: a newer version's
settings appear in an older file. Returns the new text, or NULL if nothing
was missing */
static char *config_add_missing(const char *text, toml_datum_t table)
{
	char *result = NULL;
	size_t index;

	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const struct config_setting *setting = &config_settings[index];
		const char *dot = strchr(setting->name, '.');
		const char *current = result ? result : text;
		struct config_text block = { NULL, 0, 0 };
		struct config_text updated = { NULL, 0, 0 };
		char header[40];
		const char *line;
		const char *insert = NULL;

		if (!(setting->platforms & CONFIG_PLATFORM) || !dot || toml_seek(table, setting->name).type != TOML_UNKNOWN ||
			config_pending[index] || config_text_has_key(current, setting->name, 1))
			continue;
		snprintf(header, sizeof(header), "[%.*s]", (int)(dot - setting->name), setting->name);
		/* the end of the section's last line that is not blank */
		for (line = current; *line; )
		{
			const char *start = line;
			size_t length = strcspn(line, "\n");

			while (*start == ' ' || *start == '\t')
				start++;
			/* (the Advanced block before the next section is not this one's) */
			if (insert && (*start == '[' || !strncmp(start, CONFIG_ADVANCED_MARK, strlen(CONFIG_ADVANCED_MARK))))
				break;
			if (!insert && !strncmp(start, header, strlen(header)))
				insert = line + length;
			else if (insert && start < line + length && *start != '\r')
				insert = line + length;
			line += length;
			if (*line)
				line++;
		}
		if (insert)
		{
			if (*insert)
				insert++;
			config_append_setting(&block, setting);
		}
		else
		{
			/* no such section: a new one at the end */
			insert = current + strlen(current);
			config_append(&block, current[0] && insert[-1] != '\n' ? "\n\n" : "\n");
			config_append(&block, header);
			config_append(&block, "\n");
			config_append_setting(&block, setting);
		}
		if (!block.buffer)
			continue;
		{
			char *before = config_copy(current, (size_t)(insert - current));

			if (before)
				config_append(&updated, before);
			free(before);
		}
		if (insert > current && insert[-1] != '\n')
			config_append(&updated, "\n");
		config_append(&updated, block.buffer);
		config_append(&updated, insert);
		free(block.buffer);
		if (updated.buffer)
		{
			free(result);
			result = updated.buffer;
			platform_log("settings: added %s (new in this version) at its default", setting->name);
		}
	}
	return result;
}

/* ---------- values */

static int config_text_is_false(const char *text)
{
	char lower[8];
	size_t index;

	for (index = 0; index + 1 < sizeof(lower) && text[index]; index++)
		lower[index] = (char)tolower((unsigned char)text[index]);
	lower[index] = 0;
	return !strcmp(lower, "0") || !strcmp(lower, "false") || !strcmp(lower, "no") || !strcmp(lower, "off");
}

#ifndef HALO_XBOX_CONSOLE
/* whether an environment variable that only has to be set (a
_environment_set_is_*) is: one empty or false is not */
static int config_environment_set(const char *text)
{
	return text[0] && !config_text_is_false(text);
}
#endif

static void config_set_from_text(struct config_value *value, enum config_type type, const char *text)
{
	switch (type)
	{
	case _config_boolean:
		value->boolean = !config_text_is_false(text);
		break;
	case _config_integer:
		value->integer = strtol(text, NULL, 10);
		break;
	case _config_real:
		value->real = strtod(text, NULL);
		break;
	case _config_string:
		/* (the old string is kept, not freed: config_string's callers hold
		its pointer, and Settings writes few, seldom) */
		value->string = strdup(text);
		break;
	}
}

static int config_was_boolean(const char *name);

/* the value in the file, if it is there and of the setting's type */
static void config_set_from_file(struct config_value *value, const struct config_setting *setting,
	toml_datum_t table)
{
	toml_datum_t datum = toml_seek(table, setting->name);
	int wrong_type = 0;

	if (datum.type == TOML_UNKNOWN)
		return;
	switch (setting->type)
	{
	case _config_boolean:
		if (datum.type == TOML_BOOLEAN)
			value->boolean = datum.u.boolean;
		else
			wrong_type = 1;
		break;
	case _config_integer:
		if (datum.type == TOML_INT64)
			value->integer = (long)datum.u.int64;
		else
			wrong_type = 1;
		break;
	case _config_real:
		if (datum.type == TOML_FP64)
			value->real = datum.u.fp64;
		else if (datum.type == TOML_INT64)
			value->real = (double)datum.u.int64;
		else
			wrong_type = 1;
		break;
	case _config_string:
		if (datum.type == TOML_STRING)
		{
			free(value->string);
			value->string = strdup(datum.u.s);
		}
		/* (true or false, as an older version had it: config_old_words) */
		else if (datum.type == TOML_BOOLEAN && config_was_boolean(setting->name))
		{
			free(value->string);
			value->string = strdup(datum.u.boolean ? "true" : "false");
		}
		else
		{
			wrong_type = 1;
		}
		break;
	}
	if (wrong_type)
	{
		static const char *const expected[] = { "true or false", "a whole number", "a number", "a quoted string" };

		platform_log("config.toml line %d: %s should be %s; using %s", datum.lineno, setting->name,
			expected[setting->type], setting->default_value);
	}
}

/* how many times a setting has been written (config_write): readers that
keep a setting watch this, to read it again */
static volatile unsigned long config_change_count;

/* the settings the game asked for last, by where their names are: most reads
pass the same string literal each time (the scoreboard and the HUD ask each
frame), and a search of the table compares the name with the 200 others
first. A slot is only a guess, taken when the name at the index it gives is
the one asked for, so two threads that fill a slot at once, or a name in a
buffer that later holds another, only cost a search. */
enum
{
	CONFIG_INDEX_GUESSES = 64
};
static long config_index_guesses[CONFIG_INDEX_GUESSES];

static long config_setting_index(const char *name)
{
	size_t index;
	long *guess = &config_index_guesses[((size_t)name >> 2) % CONFIG_INDEX_GUESSES];
	long guessed = *guess;

	/* (the slot holds the index plus one: 0 is no guess) */
	if (guessed > 0 && guessed <= (long)NUMBER_OF_CONFIG_SETTINGS && !strcmp(config_settings[guessed - 1].name, name))
		return guessed - 1;
	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		if (!strcmp(config_settings[index].name, name))
		{
			*guess = (long)index + 1;
			return (long)index;
		}
	}
	return -1;
}

/* a key in the file that is no setting, likely misspelt or in the wrong
section: said in the log, with the section of a setting of that name */
static void config_report_unknown_key(int line, const char *name)
{
	const char *key = strrchr(name, '.');
	const char *found = NULL;
	size_t index, count = 0;

	key = key ? key + 1 : name;
	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const char *dot = strchr(config_settings[index].name, '.');

		if (dot && !strcmp(dot + 1, key))
		{
			found = config_settings[index].name;
			count++;
		}
	}
	/* (only a name one setting has: the section is then sure) */
	if (count == 1)
	{
		platform_log("config.toml line %d: unknown setting %s, ignored (%s goes under [%.*s])", line, name, key,
			(int)(strchr(found, '.') - found), found);
	}
	else
	{
		platform_log("config.toml line %d: unknown setting %s, ignored", line, name);
	}
}

/* keys in the file that are no setting, likely misspelt */
static void config_report_unknown_keys(toml_datum_t table)
{
	int section_index;

	for (section_index = 0; section_index < table.u.tab.size; section_index++)
	{
		toml_datum_t section = table.u.tab.value[section_index];
		int key_index;

		if (!strcmp(table.u.tab.key[section_index], "config_version"))
			continue;
		if (section.type != TOML_TABLE)
		{
			config_report_unknown_key(section.lineno, table.u.tab.key[section_index]);
			continue;
		}
		for (key_index = 0; key_index < section.u.tab.size; key_index++)
		{
			char name[128];

			snprintf(name, sizeof(name), "%s.%s", table.u.tab.key[section_index], section.u.tab.key[key_index]);
			if (config_setting_index(name) < 0)
				config_report_unknown_key(section.u.tab.value[key_index].lineno, name);
		}
	}
}

/* ---------- older files */

/* the settings whose default an earlier version had otherwise, with that
default as it was written */
static const struct
{
	const char *name;
	const char *default_value;
	/* whether a file of the current layout holding it is moved to the
	current default too (the value was never a choice a player made) */
	int follow;
	/* for an existing file that has the setting at the old default (or not
	set): the change in words, to ask about it (config_find_new_defaults) */
	const char *change;
	/* the generation of defaults it was asked about in (CONFIG_DEFAULTS_GENERATION;
	0 for 1) */
	int generation;
} config_old_defaults[] =
{
	/* upstream's, which ChupathingyCE kept until v0.5.2b */
	{ "display.menus", "\"pc\"", 0, NULL, 0 },
	/* the Xbox's menus, the default until v0.8.0 (a file that names them
	keeps them) */
	{ "display.menus", "\"xbox\"", 0,
		"The Chupathingy menus", 0 },
	/* the Xbox's, until the per-platform defaults of v0.8.0 (a file that
	names them keeps them) */
	{ "display.anti_aliasing", "\"off\"", 0, "Smoother edges (2x multisampling)", 0 },
	{ "display.shadow_resolution", "128", 0, "Sharper shadows (256)", 0 },
	/* asking to join a copied invite, the default until v0.8.0c: a file
	that names "ask" keeps it, one that left it commented out follows
	"never" without a question */
	{ "network.join_from_clipboard", "\"ask\"", 0, NULL, 0 },
	/* the community site's first address, the default until stats.chupathingy.org
	(the same server, so a file that holds it follows without a question) */
	{ "network.browser_url", "\"https://halo.milenko.org\"", 1, NULL, 0 },
#ifdef HALO_ANDROID
	{ "audio.buffer_frames", "512", 0, "A larger audio buffer (1024)", 0 },
#endif
#ifndef HALO_CONSOLE_NO_DISCORD
	/* OpenCE's Discord application, which ChupathingyCE kept until v0.7.5c
	(Discord showed "OpenCE" as being played) */
	{ "discord.application_id", "\"1553978809840050229\"", 1, NULL, 0 },
#endif
	/* a random port each run, until v0.8.1b gave hosting Halo's 2302 (a file
	that names 0 keeps it; the consoles and the server keep 0) */
	{ "network.tunnel_port", "0", 0, "A fixed port for hosting (UDP 2302), to forward on your router", 2 },
	/* a game you host waited for a second player, until it started alone by
	default (a file that names false keeps waiting) */
	{ "game.start_with_one_player", "false", 0, "Start a game you host alone, without waiting for a second player", 3 },
#ifndef HALO_CONSOLE
	/* the stereo pan, the default until Headphone 3D matched the Xbox (a
	file that names "off" keeps it) */
	{ "audio.hrtf", "\"off\"", 0, "Headphone 3D on, as the original Xbox did", 4 },
#endif
};

#define NUMBER_OF_CONFIG_OLD_DEFAULTS (sizeof(config_old_defaults) / sizeof(config_old_defaults[0]))

/* the settings an earlier version had under another name, whose value the
player chose goes on under the new one (config_forward_renamed), and the
retired settings a newer one does the job of: an older file that has one
still loads, and the old name's lines are taken out of it */
static const struct
{
	const char *old_name;
	const char *name;
	/* the old setting's default, as written: a value at it was no choice and
	is only taken out (NULL: every value goes on) */
	const char *old_default;
	/* whether the old value goes on even over a value under the new name:
	the old setting turned the new one on too, as debug.solo_game did */
	int combine;
} config_renamed[] =
{
	/* the loading screen's one least time, until v0.8.0b: a campaign
	level's now (a multiplayer game's starts at its own default) */
	{ "display.loading_screen_min_seconds", "display.loading_screen_campaign_seconds", NULL, 0 },
	/* Mootjuh's first name for starting a hosted game alone, retired:
	either one being true started it alone (its environment variable,
	HALO_SOLO_GAME, still does: config_environment_aliases) */
	{ "debug.solo_game", "game.start_with_one_player", "false", 1 },
};

#define NUMBER_OF_CONFIG_RENAMED (sizeof(config_renamed) / sizeof(config_renamed[0]))

#ifndef HALO_XBOX_CONSOLE
/* the environment variables of retired settings (config_renamed) that still
work: one set (not empty, "0", "false", "no" or "off") turns the boolean
setting that took its place on, over the file and its own variable, as the
two settings together did */
static const struct
{
	const char *environment;
	const char *name;
} config_environment_aliases[] =
{
	{ "HALO_SOLO_GAME", "game.start_with_one_player" },
};

#define NUMBER_OF_CONFIG_ENVIRONMENT_ALIASES (sizeof(config_environment_aliases) / sizeof(config_environment_aliases[0]))
#endif

/* whether the file's value is the default written as text */
static int config_datum_is(toml_datum_t datum, enum config_type type, const char *text)
{
	struct config_value value = { 0, 0, 0.0, NULL };
	size_t length = strlen(text);

	switch (type)
	{
	case _config_boolean:
		config_set_from_text(&value, type, text);
		return datum.type == TOML_BOOLEAN && !datum.u.boolean == !value.boolean;
	case _config_integer:
		config_set_from_text(&value, type, text);
		return datum.type == TOML_INT64 && datum.u.int64 == value.integer;
	case _config_real:
		config_set_from_text(&value, type, text);
		return (datum.type == TOML_FP64 && datum.u.fp64 == value.real) ||
			(datum.type == TOML_INT64 && (double)datum.u.int64 == value.real);
	case _config_string:
		/* (a TOML basic string without escapes) */
		return datum.type == TOML_STRING && length >= 2 && strlen(datum.u.s) == length - 2 &&
			!strncmp(datum.u.s, text + 1, length - 2);
	}
	return 0;
}

/* a setting's line the player changed but left commented out, "# key =
value" with a value other than the default written there: it does nothing,
which the log says (the file as the game writes it, every line at its
default, says nothing). A setting given a value elsewhere in the file is
left alone */
static void config_report_commented(const char *text, toml_datum_t table)
{
	char section[64] = "";
	const char *line;
	int line_number = 1;

	for (line = text; *line; line_number++)
	{
		const char *end = line + strcspn(line, "\n");
		const char *next = *end ? end + 1 : end;
		size_t index;

		for (index = 0; !config_line_section(line, end, section, sizeof(section)) &&
			index < NUMBER_OF_CONFIG_SETTINGS; index++)
		{
			const struct config_setting *setting = &config_settings[index];
			const char *dot = strchr(setting->name, '.');
			const char *value;
			char *probe;
			size_t probe_size;
			toml_result_t parsed;
			int same;

			if (!(setting->platforms & CONFIG_PLATFORM) || !dot ||
				strncmp(section, setting->name, (size_t)(dot - setting->name)) ||
				section[dot - setting->name] != 0 || !config_line_commented_key(line, end, dot + 1))
			{
				continue;
			}
			if (toml_seek(table, setting->name).type != TOML_UNKNOWN || config_pending[index])
				break;
			/* (the value read as TOML: 1.0 is 1.00) */
			value = (const char *)memchr(line, '=', (size_t)(end - line)) + 1;
			probe_size = (size_t)(end - value) + 8;
			probe = malloc(probe_size);
			if (!probe)
				break;
			snprintf(probe, probe_size, "v = %.*s\n", (int)(end - value), value);
			parsed = toml_parse(probe, (int)strlen(probe));
			same = parsed.ok && config_datum_is(toml_seek(parsed.toptab, "v"), setting->type, setting->default_value);
			toml_free(parsed);
			free(probe);
			if (!same)
			{
				platform_log("config.toml line %d: %s is commented out, so it does nothing; remove the '# ' to use it",
					line_number, setting->name);
			}
			break;
		}
		line = next;
	}
}

/* a file from before config_version (text, parsed as table) wrote every
setting at the default of the version that wrote it, so that a default
changed since never reached it: its lines holding this version's default
are commented out at it, as a new file has them, and config_version
written. Any other value stays, being the player's, even an older
version's default (the player may have chosen it: display.menus = "gearbox"),
which the log says. Returns the new text, or NULL if the file has its
version */
static char *config_update_layout(const char *text, toml_datum_t table)
{
	toml_datum_t version = toml_seek(table, "config_version");
	int lines[NUMBER_OF_CONFIG_SETTINGS];
	struct config_text out = { NULL, 0, 0 };
	int line_number = 1, defaults = 0, version_written;
	const char *line;
	size_t index;

	if (version.type == TOML_INT64 && version.u.int64 >= CONFIG_VERSION)
		return NULL;
	/* (an older version's line is changed where it is) */
	version_written = version.type != TOML_UNKNOWN;
	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const struct config_setting *setting = &config_settings[index];
		toml_datum_t datum = toml_seek(table, setting->name);
		size_t old;

		lines[index] = 0;
		if (datum.type == TOML_UNKNOWN)
			continue;
		if (config_datum_is(datum, setting->type, setting->default_value))
		{
			lines[index] = datum.lineno;
			continue;
		}
		for (old = 0; old < NUMBER_OF_CONFIG_OLD_DEFAULTS; old++)
		{
			if (!strcmp(config_old_defaults[old].name, setting->name) &&
				config_datum_is(datum, setting->type, config_old_defaults[old].default_value))
			{
				platform_log("settings: %s = %s is kept (an older version's default; this version's is %s, which "
					"commenting the line out follows)", setting->name,
					config_old_defaults[old].default_value, setting->default_value);
			}
		}
	}
	for (line = text; *line; line_number++)
	{
		const char *end = line + strcspn(line, "\n");
		const char *next = *end ? end + 1 : end;
		char section[64];
		long setting_index = -1;

		for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
		{
			/* (only the line "key = value" in the section: not a dotted key
			or an inline table, which hold more) */
			if (lines[index] == line_number &&
				config_line_key(line, end, strchr(config_settings[index].name, '.') + 1))
			{
				setting_index = (long)index;
			}
		}
		if (version.type != TOML_UNKNOWN && version.lineno == line_number &&
			config_line_key(line, end, "config_version"))
		{
			config_append(&out, CONFIG_VERSION_LINE);
		}
		else if (setting_index >= 0)
		{
			const struct config_setting *setting = &config_settings[setting_index];
			char buffer[256];

			snprintf(buffer, sizeof(buffer), "# %s = %s\n", strchr(setting->name, '.') + 1, setting->default_value);
			config_append(&out, buffer);
			defaults++;
		}
		else
		{
			char *copy;

			/* (the version goes before the first section, as TOML has it,
			after the blank line that is there) */
			if (!version_written && config_line_section(line, end, section, sizeof(section)))
			{
				config_append(&out, CONFIG_VERSION_LINES "\n");
				version_written = 1;
			}
			copy = config_copy(line, (size_t)(next - line));
			if (copy)
				config_append(&out, copy);
			free(copy);
		}
		line = next;
	}
	if (!version_written)
	{
		if (out.length && out.buffer[out.length - 1] != '\n')
			config_append(&out, "\n");
		config_append(&out, CONFIG_VERSION_TEXT);
	}
	platform_log("settings: config.toml updated (config_version %d): %d lines at this version's default commented "
		"out, to follow it; every other value is kept", CONFIG_VERSION, defaults);
	return out.buffer;
}

/* whether the value is the setting's default, or one an earlier version had */
static int config_datum_is_a_default(toml_datum_t datum, const struct config_setting *setting)
{
	size_t old;

	if (config_datum_is(datum, setting->type, setting->default_value))
		return 1;
	for (old = 0; old < NUMBER_OF_CONFIG_OLD_DEFAULTS; old++)
	{
		if (!strcmp(config_old_defaults[old].name, setting->name) &&
			config_datum_is(datum, setting->type, config_old_defaults[old].default_value))
		{
			return 1;
		}
	}
	return 0;
}

/* whether text has the setting commented out at a default in its section,
"# key = ..." */
static int config_text_has_commented_key(const char *text, const char *name)
{
	const char *dot = strchr(name, '.');
	char section[64], current[64] = "";
	const char *line;

	snprintf(section, sizeof(section), "%.*s", (int)(dot - name), name);
	for (line = text; *line;)
	{
		const char *end = line + strcspn(line, "\n");

		if (!config_line_section(line, end, current, sizeof(current)) && !strcmp(current, section) &&
			config_line_commented_key(line, end, dot + 1))
		{
			return 1;
		}
		line = *end ? end + 1 : end;
	}
	return 0;
}

/* whether the line (from its '#') is a setting commented out, "# key = ..." */
static int config_line_is_commented_setting(const char *line)
{
	const char *start;

	line++;
	while (*line == ' ' || *line == '\t')
		line++;
	start = line;
	while ((*line >= 'a' && *line <= 'z') || (*line >= '0' && *line <= '9') || *line == '_')
		line++;
	if (line == start)
		return 0;
	while (*line == ' ' || *line == '\t')
		line++;
	return *line == '=';
}

/* a version from before config_version, run on a file of this layout, sees
the settings commented out at their default as missing, and adds each as a
value at its own default (with its comment, after a blank line) at the end
of its section: the file then holds the setting twice, "# key = default"
from this layout and "key = value" from the older version, and the value
pins the older version's default. Such a value that is a default, this
version's or an older one, is taken out with its comment, so that the
setting follows this version's default again; a value that is no default
stays, being the player's. With asked, only the settings asked about (and
values at this version's default): one at an older default that the player
kept in an earlier question stays. Returns the new text, or NULL if there
was none */
static char *config_drop_added_defaults(const char *text, toml_datum_t table, const int *asked)
{
	toml_datum_t version = toml_seek(table, "config_version");
	int lines[NUMBER_OF_CONFIG_SETTINGS];
	struct config_text out = { NULL, 0, 0 };
	int line_number = 1, dropped = 0, any = 0;
	const char *line;
	size_t index;

	if (version.type != TOML_INT64 || version.u.int64 < 2)
		return NULL;
	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const struct config_setting *setting = &config_settings[index];
		toml_datum_t datum = toml_seek(table, setting->name);

		lines[index] = 0;
		if (datum.type != TOML_UNKNOWN && config_datum_is_a_default(datum, setting) &&
			(!asked || asked[index] || config_datum_is(datum, setting->type, setting->default_value)) &&
			config_text_has_commented_key(text, setting->name))
		{
			lines[index] = datum.lineno;
			any = 1;
		}
	}
	if (!any)
		return NULL;
	for (line = text; *line; line_number++)
	{
		const char *end = line + strcspn(line, "\n");
		const char *next = *end ? end + 1 : end;
		int drop = 0;

		for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS && !drop; index++)
		{
			drop = lines[index] == line_number &&
				config_line_key(line, end, strchr(config_settings[index].name, '.') + 1);
		}
		if (drop)
		{
			/* (its comment above it, and the blank line before that) */
			while (out.length)
			{
				size_t start = out.length - 1;
				const char *first;

				while (start > 0 && out.buffer[start - 1] != '\n')
					start--;
				for (first = out.buffer + start; *first == ' ' || *first == '\t'; first++)
				{
				}
				/* (not another setting's line, "# key = value") */
				if (*first != '#' || config_line_is_commented_setting(first))
					break;
				out.length = start;
				out.buffer[start] = 0;
			}
			if (out.length >= 2 && out.buffer[out.length - 1] == '\n' && out.buffer[out.length - 2] == '\n')
				out.buffer[--out.length] = 0;
			dropped++;
		}
		else
		{
			char *copy = config_copy(line, (size_t)(next - line));

			if (copy)
				config_append(&out, copy);
			free(copy);
		}
		line = next;
	}
	platform_log("settings: config.toml held %d settings twice, commented out and as a default an older version "
		"added; those values are taken out, to follow this version's defaults", dropped);
	return out.buffer;
}

/* a default's text without its quotes, as config_write takes it */
static void config_unquote(const char *text, char *out, size_t size)
{
	size_t length = strlen(text);

	if (length >= 2 && text[0] == '"')
	{
		text++;
		length -= 2;
	}
	if (length >= size)
		length = size - 1;
	memcpy(out, text, length);
	out[length] = 0;
}

#ifndef HALO_SERVER
/* whether the setting is commented out in text ("# key = ...") at the value
(a default as written): 1 if so, 0 if at another, -1 if it is not there */
static int config_commented_value_is(const char *text, const struct config_setting *setting, const char *value_text)
{
	const char *dot = strchr(setting->name, '.');
	char section[64], current[64] = "";
	const char *line;

	snprintf(section, sizeof(section), "%.*s", (int)(dot - setting->name), setting->name);
	for (line = text; *line;)
	{
		const char *end = line + strcspn(line, "\n");

		if (!config_line_section(line, end, current, sizeof(current)) && !strcmp(current, section) &&
			config_line_commented_key(line, end, dot + 1))
		{
			const char *value = (const char *)memchr(line, '=', (size_t)(end - line)) + 1;
			size_t probe_size = (size_t)(end - value) + 8;
			char *probe = malloc(probe_size);
			toml_result_t parsed;
			int same;

			if (!probe)
				return 0;
			snprintf(probe, probe_size, "v = %.*s\n", (int)(end - value), value);
			parsed = toml_parse(probe, (int)strlen(probe));
			same = parsed.ok && config_datum_is(toml_seek(parsed.toptab, "v"), setting->type, value_text);
			toml_free(parsed);
			free(probe);
			return same;
		}
		line = *end ? end + 1 : end;
	}
	return -1;
}

#endif

/* An existing file (text, parsed as table) whose settings this version's
defaults would change: those at an older default that follow the default (at
the old one, commented out, or not there at all, the file being from before
the setting), and those an older version wrote as a value beside the
commented one (config_drop_added_defaults). They are listed in
config_pending, to be asked about once (config_new_defaults_pending); the
file is left as it is, and the settings keep their old values, until the
player answers. A file that was answered (game.defaults_answered) has none.
The dedicated server keeps its own defaults and is never asked. */
static void config_find_new_defaults(const char *text, toml_datum_t table)
{
#ifndef HALO_SERVER
	toml_datum_t version = toml_seek(table, "config_version");
	toml_datum_t answered = toml_seek(table, "game.defaults_answered");
	size_t old;

	long answered_generation = answered.type == TOML_INT64 ? (long)answered.u.int64 : 0;

	if (answered_generation >= CONFIG_DEFAULTS_GENERATION)
		return;
	for (old = 0; old < NUMBER_OF_CONFIG_OLD_DEFAULTS; old++)
	{
		long index = config_old_defaults[old].change ? config_setting_index(config_old_defaults[old].name) : -1;
		int generation = config_old_defaults[old].generation ? config_old_defaults[old].generation : 1;
		const struct config_setting *setting;
		toml_datum_t datum;

		/* (asked about already, in an earlier generation's question) */
		if (index < 0 || generation <= answered_generation)
			continue;
		setting = &config_settings[index];
		if (!(setting->platforms & CONFIG_PLATFORM) || !strcmp(config_old_defaults[old].default_value, setting->default_value))
			continue;
		datum = toml_seek(table, setting->name);
		if (datum.type != TOML_UNKNOWN)
		{
			if (version.type == TOML_INT64 && version.u.int64 >= 2 &&
				config_datum_is(datum, setting->type, config_old_defaults[old].default_value) &&
				config_text_has_commented_key(text, setting->name))
			{
				config_pending[index] = 2;
			}
		}
		else if (config_commented_value_is(text, setting, config_old_defaults[old].default_value) != 0)
		{
			config_pending[index] = 1;
		}
		if (config_pending[index])
			config_pending_old[index] = config_old_defaults[old].default_value;
	}
#else
	(void)text;
	(void)table;
#endif
}

/* whether the line is blank */
static int config_line_blank(const char *line, const char *end)
{
	while (line < end && (*line == ' ' || *line == '\t' || *line == '\r'))
		line++;
	return line == end;
}

/* whether the line is a comment */
static int config_line_comment(const char *line, const char *end)
{
	while (line < end && (*line == ' ' || *line == '\t'))
		line++;
	return line < end && *line == '#';
}

/* a file that holds, for a setting whose old default is followed, that
default as a value: the setting's lines are taken out of it (the value, and
a copy commented out at that default with its help text of that day, each
with the comment above it and one blank line), and config_add_missing then
writes it again as this version has it, commented out at the current
default, so that it follows. Returns the new text, or NULL if there was
nothing to change */
static char *config_follow_old_defaults(const char *text, toml_datum_t table)
{
	char *result = NULL;
	size_t old;

	for (old = 0; old < NUMBER_OF_CONFIG_OLD_DEFAULTS; old++)
	{
		const char *source = result ? result : text;
		const struct config_setting *setting;
		struct config_text out = { NULL, 0, 0 };
		toml_datum_t datum;
		const char *dot, *line;
		const char **starts;
		size_t count = 0, capacity = 0, line_index, found = 0;
		unsigned char *drop;
		char section[64], current[64] = "";
		long index = config_old_defaults[old].follow ? config_setting_index(config_old_defaults[old].name) : -1;

		if (index < 0)
			continue;
		setting = &config_settings[index];
		datum = toml_seek(table, setting->name);
		if (!config_datum_is(datum, setting->type, config_old_defaults[old].default_value) ||
			config_datum_is(datum, setting->type, setting->default_value))
			continue;
		dot = strchr(setting->name, '.');
		snprintf(section, sizeof(section), "%.*s", (int)(dot - setting->name), setting->name);
		for (line = source; *line; line += strcspn(line, "\n") + (line[strcspn(line, "\n")] ? 1 : 0))
			capacity++;
		starts = malloc((capacity + 1) * sizeof(*starts));
		drop = calloc(capacity + 1, 1);
		if (!starts || !drop)
		{
			free(starts);
			free(drop);
			continue;
		}
		for (line = source; *line; line += strcspn(line, "\n") + (line[strcspn(line, "\n")] ? 1 : 0))
			starts[count++] = line;
		starts[count] = line;
		for (line_index = 0; line_index < count; line_index++)
		{
			const char *start = starts[line_index];
			const char *end = start + strcspn(start, "\n");

			if (config_line_section(start, end, current, sizeof(current)))
				continue;
			if (!strcmp(current, section) &&
				(config_line_key(start, end, dot + 1) || config_line_commented_key(start, end, dot + 1)))
			{
				size_t back = line_index;

				drop[line_index] = 1;
				found++;
				while (back > 0 && config_line_comment(starts[back - 1], starts[back - 1] + strcspn(starts[back - 1], "\n")))
					drop[--back] = 1;
				if (back > 0 && config_line_blank(starts[back - 1], starts[back - 1] + strcspn(starts[back - 1], "\n")))
					drop[back - 1] = 1;
			}
		}
		if (found)
		{
			for (line_index = 0; line_index < count; line_index++)
			{
				if (!drop[line_index])
				{
					char *copy = config_copy(starts[line_index], (size_t)(starts[line_index + 1] - starts[line_index]));

					if (copy)
						config_append(&out, copy);
					free(copy);
				}
			}
			if (out.buffer)
			{
				free(result);
				result = out.buffer;
				platform_log("settings: %s = %s was an older version's default; it follows this version's (%s) now",
					setting->name, config_old_defaults[old].default_value, setting->default_value);
			}
		}
		free(starts);
		free(drop);
	}
	return result;
}

/* text with the setting's line (line_text, "key = value\n") in its section:
in place of its value, or of its line commented out at the default when it
has no value (the help text above it kept), or else at the end of its
section, or in a new section at the end. Returns the new text, or NULL */
static char *config_text_with_line(const char *text, const char *name, const char *line_text)
{
	const char *dot = strchr(name, '.');
	char wanted[80], key[64], current[64] = "";
	struct config_text out = { 0 };
	const char *line;
	int written = 0, in_section = 0, has_value;

	if (!dot)
		return NULL;
	snprintf(wanted, sizeof(wanted), "%.*s", (int)(dot - name), name);
	snprintf(key, sizeof(key), "%s", dot + 1);
	/* (the commented-out line is the one replaced only when there is no
	value) */
	has_value = config_text_has_key(text, name, 0);
	for (line = text; *line;)
	{
		const char *end = line + strcspn(line, "\n");
		const char *next = *end ? end + 1 : end;

		if (config_line_section(line, end, current, sizeof(current)))
		{
			/* (leaving the section without the key: it goes at its end) */
			if (in_section && !written)
			{
				config_append(&out, line_text);
				written = 1;
			}
			in_section = !strcmp(current, wanted);
		}
		else if (in_section && !written &&
			(has_value ? config_line_key(line, end, key) : config_line_commented_key(line, end, key)))
		{
			config_append(&out, line_text);
			written = 1;
			line = next;
			continue;
		}
		{
			char *copy = config_copy(line, (size_t)(next - line));

			if (copy)
			{
				config_append(&out, copy);
				free(copy);
			}
		}
		line = next;
	}
	if (!written)
	{
		if (out.length && out.buffer[out.length - 1] != '\n')
			config_append(&out, "\n");
		if (!in_section)
		{
			/* (appended in pieces: a section name of any length fits) */
			config_append(&out, "\n[");
			config_append(&out, wanted);
			config_append(&out, "]\n");
		}
		config_append(&out, line_text);
	}
	return out.buffer;
}

/* a file that holds a setting under its older name (config_renamed): that
name's lines are taken out of it (its value, and its line commented out at
the default with its help text of that day, each with the comment above it
and one blank line), and a value the player chose there goes in under the
new name where the old one was, with the new setting's help text, as
config_write writes a choice; a value already given under the new name is
the one kept (unless the old one combines with it). A new name in another
section, or one that has a value, takes the value on its own line, as
config_write has it. A value at the old setting's default, and a setting
not chosen (only commented out), are only taken out: config_add_missing
writes the new one at its default. Returns the new text, or NULL if there
was nothing to change */
static char *config_forward_renamed(const char *text, toml_datum_t table)
{
	char *result = NULL;
	size_t renamed;

	for (renamed = 0; renamed < NUMBER_OF_CONFIG_RENAMED; renamed++)
	{
		const char *source = result ? result : text;
		const char *old_name = config_renamed[renamed].old_name;
		const char *old_dot = strchr(old_name, '.');
		long index = config_setting_index(config_renamed[renamed].name);
		const struct config_setting *setting;
		struct config_text out = { NULL, 0, 0 };
		struct config_text block = { NULL, 0, 0 };
		const char *line;
		const char **starts;
		unsigned char *drop;
		size_t count = 0, capacity = 0, line_index, value_line = (size_t)-1, found = 0;
		char section[64], current[64] = "", value[256] = "";
		int forward, given, in_place;
		toml_datum_t old_datum = toml_seek(table, old_name);

		if (index < 0 || !old_dot)
			continue;
		setting = &config_settings[index];
		given = toml_seek(table, setting->name).type != TOML_UNKNOWN;
		forward = old_datum.type != TOML_UNKNOWN && (!given || config_renamed[renamed].combine) &&
			(!config_renamed[renamed].old_default ||
			!config_datum_is(old_datum, setting->type, config_renamed[renamed].old_default));
		snprintf(section, sizeof(section), "%.*s", (int)(old_dot - old_name), old_name);
		/* (the new name's lines where the old one's were: only in the same
		section, and with no value of its own there) */
		in_place = !given && !strncmp(setting->name, old_name, (size_t)(old_dot - old_name) + 1);
		for (line = source; *line; line += strcspn(line, "\n") + (line[strcspn(line, "\n")] ? 1 : 0))
			capacity++;
		starts = malloc((capacity + 1) * sizeof(*starts));
		drop = calloc(capacity + 1, 1);
		if (!starts || !drop)
		{
			free(starts);
			free(drop);
			continue;
		}
		for (line = source; *line; line += strcspn(line, "\n") + (line[strcspn(line, "\n")] ? 1 : 0))
			starts[count++] = line;
		starts[count] = line;
		for (line_index = 0; line_index < count; line_index++)
		{
			const char *start = starts[line_index];
			const char *end = start + strcspn(start, "\n");
			int is_value;

			if (config_line_section(start, end, current, sizeof(current)))
				continue;
			is_value = config_line_key(start, end, old_dot + 1);
			if (strcmp(current, section) || (!is_value && !config_line_commented_key(start, end, old_dot + 1)))
				continue;
			{
				size_t back = line_index;

				drop[line_index] = 1;
				found++;
				while (back > 0 && config_line_comment(starts[back - 1], starts[back - 1] + strcspn(starts[back - 1], "\n")))
					drop[--back] = 1;
				if (back > 0 && config_line_blank(starts[back - 1], starts[back - 1] + strcspn(starts[back - 1], "\n")))
					drop[back - 1] = 1;
			}
			/* (the value as the player wrote it) */
			if (is_value && value_line == (size_t)-1)
			{
				const char *text_start = (const char *)memchr(start, '=', (size_t)(end - start)) + 1;
				const char *text_end = end;

				while (text_start < text_end && (*text_start == ' ' || *text_start == '\t'))
					text_start++;
				while (text_end > text_start && (text_end[-1] == ' ' || text_end[-1] == '\t' || text_end[-1] == '\r'))
					text_end--;
				if ((size_t)(text_end - text_start) < sizeof(value))
				{
					memcpy(value, text_start, (size_t)(text_end - text_start));
					value[text_end - text_start] = 0;
					value_line = line_index;
				}
			}
		}
		if (forward && value_line != (size_t)-1 && in_place)
		{
			char buffer[320];
			size_t last;

			/* the new setting's lines, its key taken out of its comment */
			config_append_setting(&block, setting);
			if (block.buffer)
			{
				for (last = block.length - 1; last > 0 && block.buffer[last - 1] != '\n'; last--)
					;
				block.buffer[last] = 0;
				block.length = last;
				snprintf(buffer, sizeof(buffer), "%s = %s\n", strchr(setting->name, '.') + 1, value);
				config_append(&block, buffer);
			}
		}
		if (found)
		{
			for (line_index = 0; line_index < count; line_index++)
			{
				if (line_index == value_line && block.buffer)
					config_append(&out, block.buffer);
				if (!drop[line_index])
				{
					char *copy = config_copy(starts[line_index], (size_t)(starts[line_index + 1] - starts[line_index]));

					if (copy)
						config_append(&out, copy);
					free(copy);
				}
			}
			if (out.buffer && forward && value_line != (size_t)-1 && !in_place)
			{
				char line_text[320];
				char *moved;

				snprintf(line_text, sizeof(line_text), "%s = %s\n", strchr(setting->name, '.') + 1, value);
				moved = config_text_with_line(out.buffer, setting->name, line_text);
				if (moved)
				{
					free(out.buffer);
					out.buffer = moved;
				}
			}
			if (out.buffer)
			{
				free(result);
				result = out.buffer;
				if (forward && value_line != (size_t)-1)
					platform_log("settings: %s = %s goes on as %s", old_name, value, setting->name);
				else
					platform_log("settings: %s (an older version's) taken out of config.toml", old_name);
			}
		}
		free(block.buffer);
		free(starts);
		free(drop);
	}
	return result;
}

/* ---------- words */

/* the settings that take one of a few words: one in any case is taken (as
written here), and another is said once in the log */
static const struct
{
	const char *name;
	/* NULL ended; "" where empty is one */
	const char *words[8];
	/* what the game does with another, for the log (NULL: what reads the
	setting says it) */
	const char *otherwise;
} config_words[] =
{
	{ "display.mode", { "", "fullscreen", "borderless", "windowed", NULL }, "display.fullscreen's" },
	{ "display.resolution_scaling", { "native", "original", NULL }, "\"native\"" },
	{ "display.anti_aliasing", { "off", "fxaa", "smaa", "ssaa2x", "msaa2x", "msaa4x", "msaa8x", NULL }, NULL },
	{ "display.menus", { "xbox", "gearbox", "pc", "chupathingy", NULL }, NULL },
	{ "display.player_names", { "all", "allies", "allys", "enemies", "none", NULL }, "\"all\"" },
	{ "display.scoreboard_team_layout", { "teams", "score", NULL }, "\"teams\"" },
	{ "display.scoreboard_platforms", { "auto", "on", "off", NULL }, "\"auto\"" },
	{ "display.follow_camera", { "long_waits", "always", "off", NULL }, "\"long_waits\"" },
	{ "display.texture_decode", { "auto", "gpu", "cpu", NULL }, "\"auto\"" },
	{ "audio.resampling", { "sinc", "linear", NULL }, "\"sinc\"" },
	{ "input.touch_controls", { "on", "auto", "off", NULL }, "\"on\"" },
	{ "game.console_log", { "important", "all", "none", NULL }, "\"important\"" },
	{ "game.move_old_map_folders", { "ask", "yes", "no", NULL }, "\"ask\"" },
	{ "network.protocol", { "auto", "opence", "delta", NULL }, NULL },
	{ "network.map_downloads", { "ask", "always", "never", NULL }, "\"ask\"" },
	{ "network.playlist_downloads", { "ask", "always", "never", NULL }, "\"ask\"" },
	{ "network.join_from_clipboard", { "ask", "always", "never", NULL }, "\"never\"" },
	{ "network.team_change", { "balanced", "allow", "off", NULL }, "\"balanced\"" },
#ifndef HALO_CONSOLE_NO_DISCORD
	{ "discord.presence", { "full", "minimal", "off", NULL }, "\"full\"" },
#endif
	{ "network.platform_limits", { "on", "off", NULL }, "\"on\"" },
	{ "console.delta", { "auto", "on", "off", NULL }, "\"auto\"" },
	{ "network.coop_friendly_fire", { "off", "on", "shields_only", "explosives_only", NULL }, NULL },
	{ "network.coop_enemies_mode", { "none", "per_player", "multiplier", NULL }, "\"none\"" },
	{ "crash_reports.upload", { "ask", "yes", "no", NULL }, "\"ask\"" },
	{ "launcher.show_at_start", { "when_needed", "always", NULL }, "\"when_needed\"" },
	{ "launcher.mode_choice", { "ask", "default", NULL }, "\"ask\"" },
};

#define NUMBER_OF_CONFIG_WORDS (sizeof(config_words) / sizeof(config_words[0]))

/* an older version's value of a word setting, read as the word that took
its place (a setting that was true or false: the file's true and false are
taken as these words' "true" and "false": config_set_from_file) */
static const struct
{
	const char *name;
	const char *old_word;
	const char *word;
} config_old_words[] =
{
	{ "network.join_from_clipboard", "true", "always" },
	{ "network.join_from_clipboard", "false", "never" },
};

#define NUMBER_OF_CONFIG_OLD_WORDS (sizeof(config_old_words) / sizeof(config_old_words[0]))

/* whether the setting once was true or false, which the file may still say */
static int config_was_boolean(const char *name)
{
	size_t index;

	for (index = 0; index < NUMBER_OF_CONFIG_OLD_WORDS; index++)
	{
		if (!strcmp(config_old_words[index].name, name))
			return 1;
	}
	return 0;
}

/* the word settings' values as their readers compare them (lowercase:
"Borderless" is "borderless"), and the log told of one that is none of
its words */
static void config_check_words(void)
{
	size_t word_index;

	for (word_index = 0; word_index < NUMBER_OF_CONFIG_WORDS; word_index++)
	{
		long index = config_setting_index(config_words[word_index].name);
		const char *const *words = config_words[word_index].words;
		struct config_value *value;
		char list[160] = "";
		size_t word;

		if (index < 0 || !(config_settings[index].platforms & CONFIG_PLATFORM))
			continue;
		value = &config_values[index];
		for (word = 0; word < NUMBER_OF_CONFIG_OLD_WORDS; word++)
		{
			if (!strcmp(config_old_words[word].name, config_words[word_index].name) &&
				!SDL_strncasecmp(value->string, config_old_words[word].old_word, strlen(config_old_words[word].old_word) + 1))
			{
				free(value->string);
				value->string = strdup(config_old_words[word].word);
				break;
			}
		}
		for (word = 0; words[word] && SDL_strncasecmp(value->string, words[word], strlen(words[word]) + 1); word++)
		{
		}
		if (words[word])
		{
			if (strcmp(value->string, words[word]))
			{
				free(value->string);
				value->string = strdup(words[word]);
			}
			continue;
		}
		if (!config_words[word_index].otherwise)
			continue;
		/* ("a", "b" or "c", and "or empty" for "") */
		for (word = 0; words[word]; word++)
		{
			size_t length = strlen(list);

			if (!words[word][0])
				continue;
			snprintf(list + length, sizeof(list) - length, "%s\"%s\"",
				!length ? "" : !words[word + 1] && words[0][0] ? " or " : ", ", words[word]);
		}
		if (!words[0][0])
			snprintf(list + strlen(list), sizeof(list) - strlen(list), " or empty");
		platform_log("settings: %s = \"%s\" is not %s; using %s", config_words[word_index].name, value->string, list,
			config_words[word_index].otherwise);
	}
}

/* the most lines of a file that cannot be read that are skipped to read
the rest */
#define CONFIG_MAXIMUM_SKIPPED_LINES 16

/* the line a parse error is on ("(line 12) ..."), or 0 */
static int config_error_line(const char *message)
{
	const char *at = strstr(message, "(line ");
	int line = 0;

	if (!at || sscanf(at, "(line %d)", &line) != 1)
		return 0;
	return line;
}

/* the line of text that cannot be read, said in the log with what likely
makes it so, and emptied (its number kept for the lines after it): returns
whether it was there */
static int config_skip_line(char *text, int line_number, const char *message)
{
	char *line = text, *end, *value, *value_end;
	const char *reason = strchr(message, ')');
	char key[64] = "";
	char hint[160] = "";
	int number;

	for (number = 1; number < line_number && *line; number++)
		line += strcspn(line, "\n") + (line[strcspn(line, "\n")] ? 1 : 0);
	if (number != line_number || !*line)
		return 0;
	end = line + strcspn(line, "\n");
	reason = reason ? reason + 1 : message;
	while (*reason == ' ')
		reason++;
	/* "key = value": the value as written, to the end of the line or a
	comment */
	value = memchr(line, '=', (size_t)(end - line));
	if (value)
	{
		char *key_start = line, *key_end = value;

		while (key_start < key_end && (*key_start == ' ' || *key_start == '\t'))
			key_start++;
		while (key_end > key_start && (key_end[-1] == ' ' || key_end[-1] == '\t'))
			key_end--;
		snprintf(key, sizeof(key), "%.*s", (int)(key_end - key_start), key_start);
		value++;
		while (value < end && (*value == ' ' || *value == '\t'))
			value++;
		value_end = value + strcspn(value, "#\n");
		if (value_end > end)
			value_end = end;
		while (value_end > value && (value_end[-1] == ' ' || value_end[-1] == '\t' || value_end[-1] == '\r'))
			value_end--;
		if ((value_end - value == 4 && !SDL_strncasecmp(value, "true", 4)) ||
			(value_end - value == 5 && !SDL_strncasecmp(value, "false", 5)))
		{
			snprintf(hint, sizeof(hint), ": true and false are lowercase, %s = %.*s", key, (int)(value_end - value),
				value_end - value == 4 ? "true" : "false");
		}
		else if (value < value_end && isalpha((unsigned char)*value))
		{
			snprintf(hint, sizeof(hint), ": text needs quotes, %s = \"%.*s\"", key, (int)(value_end - value), value);
		}
	}
	platform_log("config.toml line %d cannot be read (%s), so it is skipped%s", line_number, reason, hint);
	memset(line, ' ', (size_t)(end - line));
	return 1;
}

#if !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
/* a config.toml the game never reads, likely edited in its place: said in
the log, with the one it reads (path) */
static void config_report_unread(const char *path)
{
	char other[1100];
#ifdef __APPLE__
	/* beside the application, or in it (the game's is in Application
	Support) */
	const char *base = SDL_GetBasePath();
	const char *bundle = base ? strstr(base, ".app/Contents/") : NULL;

	if (bundle)
	{
		const char *folder_end = bundle;

		while (folder_end > base && folder_end[-1] != '/')
			folder_end--;
		snprintf(other, sizeof(other), "%sconfig.toml", base);
		if (SDL_GetPathInfo(other, NULL))
			platform_log("settings: %s is not read; the game's settings are %s", other, path);
		snprintf(other, sizeof(other), "%.*sconfig.toml", (int)(folder_end - base), base);
		if (SDL_GetPathInfo(other, NULL))
			platform_log("settings: %s is not read; the game's settings are %s", other, path);
	}
#elif !defined(_WIN32)
	/* in the folder the game was started from, when that is not the
	executable's (the game's is beside the executable) */
	char folder[1024];

	if (getcwd(folder, sizeof(folder)))
	{
		snprintf(other, sizeof(other), "%s/config.toml", folder);
		if (strcmp(other, path) && SDL_GetPathInfo(other, NULL))
		{
			SDL_PathInfo mine, theirs;

			/* (the same file by another name is the one read) */
			if (!SDL_GetPathInfo(path, &mine) || !SDL_GetPathInfo(other, &theirs) ||
				mine.size != theirs.size || mine.modify_time != theirs.modify_time)
			{
				platform_log("settings: %s is not read; the game's settings are %s", other, path);
			}
		}
	}
#endif
	/* the file saved with .txt after it, by an editor that adds it (and
	Windows Explorer hides it) */
	snprintf(other, sizeof(other), "%s.txt", path);
	if (SDL_GetPathInfo(other, NULL))
	{
#ifdef _WIN32
		platform_log("settings: %s is not read: Windows saved it as config.toml.txt; rename it to config.toml", other);
#else
		platform_log("settings: %s is not read; rename it to config.toml", other);
#endif
	}
}
#endif

static void config_load(void)
{
	char path[1024];
	size_t size = 0;
	char *text;
	size_t index;

	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const char *default_value = config_settings[index].default_value;

		if (config_settings[index].type == _config_string)
		{
			/* written as a TOML basic string without escapes */
			size_t length = strlen(default_value);

			config_values[index].string = length >= 2 ? config_copy(default_value + 1, length - 2) : strdup("");
		}
		else
		{
			config_set_from_text(&config_values[index], config_settings[index].type, default_value);
		}
	}

	config_path(path, sizeof(path));
#if !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
	config_report_unread(path);
#endif
	text = config_read_file(path, &size);
	if (text)
	{
		toml_result_t result = toml_parse(text, (int)size);
		int skipped = 0;

		/* (a line that cannot be read is skipped, in memory, so that the
		rest is) */
		while (!result.ok && skipped < CONFIG_MAXIMUM_SKIPPED_LINES &&
			config_skip_line(text, config_error_line(result.errmsg), result.errmsg))
		{
			toml_free(result);
			result = toml_parse(text, (int)strlen(text));
			skipped++;
		}
		if (result.ok)
		{
			char *updated = config_update_layout(text, result.toptab);
			char *completed;

			if (updated)
			{
				/* (read as it is now, the defaults it gave up taking effect) */
				toml_result_t updated_result = toml_parse(updated, (int)strlen(updated));

				if (updated_result.ok)
				{
					toml_free(result);
					free(text);
					result = updated_result;
					text = updated;
				}
				else
				{
					toml_free(updated_result);
					free(updated);
					updated = NULL;
				}
			}
			{
				/* (a value that was an older version's default, followed) */
				char *followed = config_follow_old_defaults(text, result.toptab);
				toml_result_t followed_result;

				if (followed)
				{
					followed_result = toml_parse(followed, (int)strlen(followed));
					if (followed_result.ok)
					{
						toml_free(result);
						free(text);
						result = followed_result;
						text = followed;
						updated = text;
					}
					else
					{
						toml_free(followed_result);
						free(followed);
					}
				}
			}
			{
				/* (a value under a setting's older name, forwarded) */
				char *forwarded = config_forward_renamed(text, result.toptab);
				toml_result_t forwarded_result;

				if (forwarded)
				{
					forwarded_result = toml_parse(forwarded, (int)strlen(forwarded));
					if (forwarded_result.ok)
					{
						toml_free(result);
						free(text);
						result = forwarded_result;
						text = forwarded;
						updated = text;
					}
					else
					{
						toml_free(forwarded_result);
						free(forwarded);
					}
				}
			}
			config_find_new_defaults(text, result.toptab);
			for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
			{
				config_set_from_file(&config_values[index], &config_settings[index], result.toptab);
				config_given[index] = toml_seek(result.toptab, config_settings[index].name).type != TOML_UNKNOWN;
			}
			/* (until the player is asked, the values stay what they were: the
			new defaults take effect only when they say so) */
			for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
			{
				if (config_pending[index])
				{
					char old[64];

					config_unquote(config_pending_old[index], old, sizeof(old));
					config_set_from_text(&config_values[index], config_settings[index].type, old);
				}
			}
			config_report_unknown_keys(result.toptab);
			platform_log("settings: %s", path);
			completed = skipped ? NULL : config_add_missing(text, result.toptab);
			config_report_commented(completed ? completed : text, result.toptab);
			/* (a file with lines skipped is left as the player wrote it, to
			be fixed) */
			if (skipped)
			{
				platform_log("settings: %d line%s of config.toml skipped; every other setting in it is used", skipped,
					skipped == 1 ? "" : "s");
			}
			else if ((completed || updated) && !config_write_file(path, completed ? completed : text))
			{
				platform_log("settings: cannot write %s", path);
			}
			free(completed);
		}
		else
		{
			platform_log("config.toml cannot be read (%s): every setting is at its default until that is fixed",
				result.errmsg);
		}
		toml_free(result);
		free(text);
	}
	else
	{
		char *defaults = config_default_text();

		if (defaults && config_write_file(path, defaults))
			platform_log("settings: wrote the defaults to %s", path);
		else
			platform_log("settings: cannot write %s; using the defaults", path);
		free(defaults);
	}

	/* (the console has no environment) */
#ifndef HALO_XBOX_CONSOLE
	for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
	{
		const struct config_setting *setting = &config_settings[index];
		const char *environment = setting->environment ? getenv(setting->environment) : NULL;

		/* (one set to "", "0", "false", "no" or "off" is as if it were not:
		HALO_LOG_ADDRESSES=0 never turns a setting on) */
		if (!environment || (setting->environment_style != _environment_value && !config_environment_set(environment)))
			continue;
		config_given[index] = 1;
		switch (setting->environment_style)
		{
		case _environment_value:
			config_set_from_text(&config_values[index], setting->type, environment);
			break;
		case _environment_set_is_true:
			config_values[index].boolean = 1;
			break;
		case _environment_set_is_false:
			config_values[index].boolean = 0;
			break;
		}
	}
	for (index = 0; index < NUMBER_OF_CONFIG_ENVIRONMENT_ALIASES; index++)
	{
		const char *environment = getenv(config_environment_aliases[index].environment);
		long setting = config_setting_index(config_environment_aliases[index].name);

		if (!environment || !config_environment_set(environment) || setting < 0 ||
			config_settings[setting].type != _config_boolean)
		{
			continue;
		}
		config_values[setting].boolean = 1;
		config_given[setting] = 1;
	}
#endif
#ifndef HALO_CONSOLE_NO_UPDATER
	/* (a packaged install's package manager updates it: update.auto is off
	and stays off, packaged_install.h; whether the game still looks for a
	new version to say so is the file's or the environment's choice) */
	if (platform_packaged())
	{
		long update = config_setting_index("update.auto");

		if (update >= 0)
		{
			packaged_update_check = config_values[update].boolean;
			config_values[update].boolean = 0;
			config_given[update] = 1;
			platform_log("settings: a packaged install: update.auto is off (the package manager updates the game)");
		}
	}
#endif
	config_check_words();
#if !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
	{
		long mode = config_setting_index("display.mode");
		long fullscreen = config_setting_index("display.fullscreen");

		/* (said once: which of the two the window follows) */
		const char *value = config_values[mode].string;

		if ((!strcmp(value, "fullscreen") || !strcmp(value, "borderless") || !strcmp(value, "windowed")) &&
			config_given[fullscreen])
		{
			platform_log("settings: display.mode = \"%s\" is used; display.fullscreen is ignored",
				config_values[mode].string);
		}
	}
#endif
}

static const struct config_value *config_value(const char *name, enum config_type type)
{
	static const struct config_value none = { 0, 0, 0.0, "" };
	long index;

	pthread_mutex_lock(&config_lock);
	if (!config_loaded)
	{
		config_load();
		config_loaded = 1;
	}
	pthread_mutex_unlock(&config_lock);
	index = config_setting_index(name);
	if (index < 0 || config_settings[index].type != type)
	{
		/* (said once a name: code asking every frame for a setting this build
		doesn't have once filled the log, and the disk, with the line) */
		static char said[32][64];
		static long said_count;
		long i;
		int known = 0;

		pthread_mutex_lock(&config_lock);
		for (i = 0; i < said_count && !known; i++)
			known = strncmp(said[i], name, sizeof(said[i]) - 1) == 0;
		if (!known && said_count < (long)(sizeof(said) / sizeof(said[0])))
		{
			strncpy(said[said_count], name, sizeof(said[said_count]) - 1);
			said_count++;
		}
		/* (past the first 32 names, none: a flood of different names is no
		better than one name's) */
		else if (!known)
		{
			known = 1;
		}
		pthread_mutex_unlock(&config_lock);
		if (!known)
			platform_log("settings: no %s setting %s", type == _config_string ? "string" : "such", name);
		return &none;
	}
#ifdef HALO_XBOX_CONSOLE
	/* (the shared code's settings the console has none of: empty, 0 or
	false, as before it had config.toml; the first few said in the log) */
	if (!(config_settings[index].platforms & CONFIG_PLATFORM))
	{
		/* (each setting said once, not at each read: the scoreboard asks
		every frame) */
		static unsigned char reported[1024];

		if (index < (long)sizeof(reported) && !reported[index])
		{
			reported[index] = 1;
			platform_log("settings: %s is not one of the console's; read as empty", name);
		}
		return &none;
	}
#endif
	return &config_values[index];
}

/* sets a setting for this run only, from its value as text, as its
environment variable does: config.toml is not changed (the console's old
switch files, port/xbox/src/xbox_config.c) */
int config_set(const char *name, const char *value)
{
	long index = config_setting_index(name);

	if (index < 0)
		return 0;
	/* (the file read first, so that it does not overwrite this) */
	config_value(name, config_settings[index].type);
	pthread_mutex_lock(&config_lock);
	config_set_from_text(&config_values[index], config_settings[index].type, value);
	config_given[index] = 1;
	config_change_count++;
	pthread_mutex_unlock(&config_lock);
	return 1;
}

/* ---------- writing a setting */

/* sets a setting, for now and in config.toml, from its value as text
("true", "60", "1.5", "all"): its line there is changed (or added, or the
line at its default taken out of its comment, now that the player chose
it), the rest of the file kept as it is */
int config_write(const char *name, const char *value)
{
	const char *dot = strchr(name, '.');
	long index = config_setting_index(name);
	char section[64], key[64], line_text[600], path[1024];
	size_t size = 0;
	char *text, *changed;
	int succeeded;

	if (index < 0 || !dot || (size_t)(dot - name) >= sizeof(section) || strlen(value) > 256)
		return 0;
	/* (the file read first, as the other settings are) */
	config_value(name, config_settings[index].type);
#ifndef HALO_CONSOLE_NO_UPDATER
	if (platform_packaged() && !strcmp(name, "update.auto"))
	{
		platform_log("settings: update.auto stays off in a packaged install");
		return 0;
	}
#endif
	pthread_mutex_lock(&config_lock);
	config_set_from_text(&config_values[index], config_settings[index].type, value);
	snprintf(section, sizeof(section), "%.*s", (int)(dot - name), name);
	snprintf(key, sizeof(key), "%s", dot + 1);
	switch (config_settings[index].type)
	{
	case _config_boolean:
		snprintf(line_text, sizeof(line_text), "%s = %s\n", key, config_values[index].boolean ? "true" : "false");
		break;
	case _config_integer:
		snprintf(line_text, sizeof(line_text), "%s = %ld\n", key, config_values[index].integer);
		break;
	case _config_real:
		/* (with its point: TOML reads 1 as an integer) */
		snprintf(line_text, sizeof(line_text), "%s = %.15g", key, config_values[index].real);
		if (!strpbrk(line_text + strlen(key) + 3, ".en"))
			strcat(line_text, ".0");
		strcat(line_text, "\n");
		break;
	case _config_string:
	{
		char *end = line_text + snprintf(line_text, sizeof(line_text), "%s = \"", key);
		const char *character;

		for (character = config_values[index].string; *character; character++)
		{
			if (*character == '"' || *character == '\\')
				*end++ = '\\';
			*end++ = *character;
		}
		strcpy(end, "\"\n");
		break;
	}
	}
	config_path(path, sizeof(path));
	text = config_read_file(path, &size);
	changed = config_text_with_line(text ? text : "", name, line_text);
	succeeded = changed && config_write_file(path, changed);
	config_change_count++;
	pthread_mutex_unlock(&config_lock);
	free(changed);
	free(text);
	return succeeded;
}

/* the file's lines "# key = <an older default>" for a setting, at the
current default again, so that the file reads as this version writes it
(NULL if there is none) */
static char *config_refresh_commented(const char *text, const struct config_setting *setting)
{
	const char *dot = strchr(setting->name, '.');
	struct config_text out = { NULL, 0, 0 };
	char section[64], current[64] = "", buffer[256];
	const char *line;
	int changed = 0;

	snprintf(section, sizeof(section), "%.*s", (int)(dot - setting->name), setting->name);
	for (line = text; *line;)
	{
		const char *end = line + strcspn(line, "\n");
		const char *next = *end ? end + 1 : end;

		config_line_section(line, end, current, sizeof(current));
		if (!strcmp(current, section) && config_line_commented_key(line, end, dot + 1))
		{
			snprintf(buffer, sizeof(buffer), "# %s = %s\n", dot + 1, setting->default_value);
			config_append(&out, buffer);
			changed = 1;
		}
		else
		{
			char *copy = config_copy(line, (size_t)(next - line));

			if (copy)
				config_append(&out, copy);
			free(copy);
		}
		line = next;
	}
	if (!changed)
	{
		free(out.buffer);
		return NULL;
	}
	return out.buffer;
}

int config_new_defaults_pending(char *text, size_t size)
{
	size_t old;
	int count = 0;

	if (size)
		text[0] = 0;
	/* (the file read first) */
	config_value("game.defaults_answered", _config_integer);
	pthread_mutex_lock(&config_lock);
	for (old = 0; old < NUMBER_OF_CONFIG_OLD_DEFAULTS; old++)
	{
		long index = config_old_defaults[old].change ? config_setting_index(config_old_defaults[old].name) : -1;
		size_t used = strlen(text);

		if (index >= 0 && config_pending[index] && config_pending_old[index] == config_old_defaults[old].default_value)
		{
			snprintf(text + used, size - used, "%s%s", count ? "\n" : "", config_old_defaults[old].change);
			count++;
		}
	}
	pthread_mutex_unlock(&config_lock);
	return count;
}

void config_new_defaults_answer(int use_new)
{
	int pending[NUMBER_OF_CONFIG_SETTINGS];
	const char *old_value[NUMBER_OF_CONFIG_SETTINGS];
	size_t index;
	char path[1024];

	config_value("game.defaults_answered", _config_integer);
	pthread_mutex_lock(&config_lock);
	memcpy(pending, config_pending, sizeof(pending));
	memcpy(old_value, config_pending_old, sizeof(old_value));
	memset(config_pending, 0, sizeof(config_pending));
	pthread_mutex_unlock(&config_lock);
	if (use_new)
	{
		size_t size = 0;
		char *text;

		config_path(path, sizeof(path));
		text = config_read_file(path, &size);
		if (text)
		{
			toml_result_t result = toml_parse(text, (int)size);
			char *changed = NULL;

			/* (the values an older version added beside the commented ones,
			which pinned the old defaults, taken out; the commented lines
			at the old defaults written at the new ones) */
			if (result.ok)
				changed = config_drop_added_defaults(text, result.toptab, pending);
			toml_free(result);
			for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
			{
				if (pending[index])
				{
					char *refreshed = config_refresh_commented(changed ? changed : text, &config_settings[index]);

					if (refreshed)
					{
						free(changed);
						changed = refreshed;
					}
				}
			}
			if (changed && !config_write_file(path, changed))
				platform_log("settings: cannot write %s", path);
			free(changed);
			free(text);
		}
		pthread_mutex_lock(&config_lock);
		for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
		{
			if (pending[index])
			{
				char value[64];

				config_unquote(config_settings[index].default_value, value, sizeof(value));
				config_set_from_text(&config_values[index], config_settings[index].type, value);
				platform_log("settings: %s follows this version's default (%s) now", config_settings[index].name,
					config_settings[index].default_value);
			}
		}
		config_change_count++;
		pthread_mutex_unlock(&config_lock);
	}
	else
	{
		for (index = 0; index < NUMBER_OF_CONFIG_SETTINGS; index++)
		{
			if (pending[index])
			{
				char value[64];

				config_unquote(old_value[index], value, sizeof(value));
				/* (a value an older version wrote is there already) */
				if (pending[index] == 1)
					config_write(config_settings[index].name, value);
				platform_log("settings: %s = %s is kept", config_settings[index].name, old_value[index]);
			}
		}
	}
	{
		char generation[16];

		snprintf(generation, sizeof(generation), "%d", CONFIG_DEFAULTS_GENERATION);
		config_write("game.defaults_answered", generation);
	}
}

int config_write_boolean(const char *name, int value)
{
	long index = config_setting_index(name);

	return index >= 0 && config_settings[index].type == _config_boolean && config_write(name, value ? "true" : "false");
}

/* a setting's value as text ("true", "60", "1.5", "all"): 0 if there is no
such setting */
int config_text(const char *name, char *text, size_t size)
{
	long index = config_setting_index(name);
	const struct config_value *value;

	if (index < 0)
		return 0;
	value = config_value(name, config_settings[index].type);
	switch (config_settings[index].type)
	{
	case _config_boolean:
		snprintf(text, size, "%s", value->boolean ? "true" : "false");
		break;
	case _config_integer:
		snprintf(text, size, "%ld", value->integer);
		break;
	case _config_real:
		snprintf(text, size, "%.15g", value->real);
		break;
	case _config_string:
		snprintf(text, size, "%s", value->string ? value->string : "");
		break;
	}
	return 1;
}

void config_folder(char *path, size_t size)
{
	char file[1024];
	char *separator;

	config_path(file, sizeof(file));
	separator = strrchr(file, '/');
#ifndef HALO_ANDROID
	if (!separator || (strrchr(file, '\\') && strrchr(file, '\\') > separator))
		separator = strrchr(file, '\\');
#endif
	if (separator)
		separator[1] = 0;
	else
		file[0] = 0;
	snprintf(path, size, "%s", file);
}

/* ---------- public code */

char *config_file_read(const char *path, size_t *size)
{
	return config_read_file(path, size);
}

#ifndef HALO_PORT_CONSOLE_LIMITS
/* the settings config.toml changes while the game runs, edited by hand
(Settings writes them through config_write): the volumes, which their
readers read again when config_changes moves (dsound_sdl.c,
sound_manager.c). The rest still take effect from the next start */
static const char *const config_live_settings[] =
{
	"audio.volume",
	"audio.music_volume",
	"audio.effects_volume",
};

enum
{
	/* config.toml's time looked at no more often than this (seconds; the
	C library's clock, which the dedicated server, without SDL, has too) */
	CONFIG_FOLLOW_INTERVAL_SECONDS = 1
};

/* config.toml changed on disk since it was last looked at: its live
settings read again from it (one given by the environment kept), and
config_changes moved if any is different */
static void config_follow_file(void)
{
	static time_t looked_at;
	static posix_ulong modified, modified_nanoseconds, modified_size;
	static int looked;
	static char path[1024];
	time_t now = time(NULL);
	struct posix_file_information info;
	size_t size = 0;
	char *text;
	toml_result_t result;
	size_t live;
	int changed = 0;

	if (!config_loaded || (looked && now - looked_at < CONFIG_FOLLOW_INTERVAL_SECONDS))
		return;
	looked_at = now;
	if (!path[0])
		config_path(path, sizeof(path));
	if (posix_stat(path, &info) != 0)
		return;
	/* (the first look keeps the file as it was read; the size too, for a
	file system whose times are whole seconds) */
	if (!looked || (info.modification_seconds == modified &&
		info.modification_nanoseconds == modified_nanoseconds && info.size_low == modified_size))
	{
		looked = 1;
		modified = info.modification_seconds;
		modified_nanoseconds = info.modification_nanoseconds;
		modified_size = info.size_low;
		return;
	}
	modified = info.modification_seconds;
	modified_nanoseconds = info.modification_nanoseconds;
	modified_size = info.size_low;
	text = config_read_file(path, &size);
	if (!text)
		return;
	result = toml_parse(text, (int)size);
	if (result.ok)
	{
		pthread_mutex_lock(&config_lock);
		for (live = 0; live < sizeof(config_live_settings) / sizeof(config_live_settings[0]); live++)
		{
			long index = config_setting_index(config_live_settings[live]);
			const struct config_setting *setting;
			struct config_value value;

			if (index < 0)
				continue;
			setting = &config_settings[index];
			if (getenv(setting->environment))
				continue;
			memset(&value, 0, sizeof(value));
			config_set_from_text(&value, setting->type, setting->default_value);
			config_set_from_file(&value, setting, result.toptab);
			if (value.real != config_values[index].real)
			{
				config_values[index].real = value.real;
				platform_log("settings: config.toml changed: %s = %g", setting->name, value.real);
				changed = 1;
			}
		}
		if (changed)
			config_change_count++;
		pthread_mutex_unlock(&config_lock);
	}
	toml_free(result);
	free(text);
}
#else
/* (not on a console: its config.toml is edited off the console, and a look
at the disk every second is a hitch the console can do without) */
static void config_follow_file(void)
{
}
#endif

unsigned long config_changes(void)
{
	config_follow_file();
	return config_change_count;
}

int config_chosen(const char *name)
{
	long index = config_setting_index(name);
	int chosen;

	if (index < 0)
		return 0;
	/* (the file read first) */
	config_value(name, config_settings[index].type);
	pthread_mutex_lock(&config_lock);
	chosen = config_given[index] && !config_pending[index];
	pthread_mutex_unlock(&config_lock);
	return chosen;
}

int config_default(const char *name, char *text, size_t size)
{
	long index = config_setting_index(name);
	const char *value;
	size_t length;

	if (index < 0)
		return 0;
	value = config_settings[index].default_value;
#ifndef HALO_CONSOLE_NO_UPDATER
	/* (Defaults on the settings screen: a packaged install's is off) */
	if (platform_packaged() && !strcmp(name, "update.auto"))
		value = "false";
#endif
	length = strlen(value);
	/* (a string's without its quotes: the defaults have no escapes) */
	if (config_settings[index].type == _config_string && length >= 2 && value[0] == '"')
	{
		value++;
		length -= 2;
	}
	snprintf(text, size, "%.*s", (int)length, value);
	return 1;
}

#ifndef HALO_CONSOLE_NO_UPDATER
int config_packaged_update_check(void)
{
	config_value("update.auto", _config_boolean);
	return packaged_update_check;
}
#endif

int config_boolean(const char *name)
{
	return config_value(name, _config_boolean)->boolean;
}

long config_integer(const char *name)
{
	return config_value(name, _config_integer)->integer;
}

double config_real(const char *name)
{
	return config_value(name, _config_real)->real;
}

const char *config_string(const char *name)
{
	const char *string = config_value(name, _config_string)->string;

	return string ? string : "";
}
