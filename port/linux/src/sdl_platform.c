/*
SDL_PLATFORM.C

The SDL3 window, OpenGL context and event loop behind the Linux build.

The window is created with the Direct3D device (d3d8_gl.c) on the game's
main thread, which is also the only thread that pumps events. Keyboard and
mouse state gathered here feeds the controller emulation in xinput_sdl.c
and the debug keyboard that the game's console reads.
*/

#include "platform.h"
#include "sdl_platform.h"
#include "gl.h"
/* (bgfx's switch, and the older drivers' path: XGPU_GL_LEGACY_AT_RUN_TIME) */
#include "xgpu.h"
#include "port_config.h"
#include "p2p.h"
#include "posix.h"
#include "xiso.h"
#include "pc_disc.h"
#include "ce_installer.h"
#include "touch_input.h"
#include "delta.h"
#include "map_repo.h"
#include "crash_report.h"
#include "launcher.h"
#include "vrr.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32) && !defined(HALO_ANDROID)
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#endif
#ifdef HALO_GAME_BROWSER
#include "browser.h"
#endif

static SDL_Window *platform_window;
static SDL_GLContext platform_gl_context;
static SDL_ThreadID platform_event_thread;
static BOOL platform_sdl_started = FALSE;

static struct platform_input_state input_state;
/* keys pressed since the last read, so a press and release between two
reads still counts as a press (input injected on Android, or a slow frame) */
static unsigned char keys_pressed[SDL_SCANCODE_COUNT];
/* likewise the mouse buttons pressed since the last read, so that a click
quicker than a frame still counts */
static unsigned char mouse_buttons_pressed[PLATFORM_MOUSE_BUTTON_COUNT];
#ifndef HALO_ANDROID
/* the menus' pointer (platform_ui_pointer_set_active), under input_lock */
static struct platform_ui_pointer ui_pointer;
/* the menus' pointer put in the middle as they open (platform_ui_pointer_set_active):
the move that warp sends isn't the player's, so it hovers nothing (it took the
focus off the item a screen opened on, like the level select's Workshop PLAY
level) */
static BOOL ui_pointer_warp_pending;
static float ui_pointer_warp_x, ui_pointer_warp_y;
static float ui_pointer_wheel;
#endif
static pthread_mutex_t input_lock = PTHREAD_MUTEX_INITIALIZER;

/* (under input_lock) each gamepad's buttons pressed and triggers pulled since
the controller's last reading of it (platform_gamepad_presses_take), as
mouse_buttons_pressed is the mouse's: a tap pressed and let go of between two
readings (a slow frame) still counts as a press. By the gamepad's joystick
ID (never 0); triggers' bit 0 is the left, bit 1 the right; triggers_down,
which are past PLATFORM_GAMEPAD_TRIGGER_PRESSED now, so that a pull counts
once. From the OpenH1 team's fix (g0atbutt, OpenH1 0473c477). */
#define GAMEPAD_PRESSES_COUNT 8
/* a tap older than this many event pumps (each port's reading pumps once,
so a frame pumps up to four times) is let go untaken: a gamepad nobody
reads (not yet assigned, or a second pad in split menus) never fires a tap
from long ago once it is read */
#define GAMEPAD_TAP_PUMPS 8
static unsigned long gamepad_pumps;
typedef char gamepad_presses_buttons_fit[SDL_GAMEPAD_BUTTON_COUNT <= 32 ? 1 : -1];
static struct gamepad_presses
{
	Uint32 id;
	Uint32 buttons;
	unsigned int triggers;
	unsigned int triggers_down;
	/* (the pump of its last tap: gamepad_pumps) */
	unsigned long pump;
} gamepad_presses[GAMEPAD_PRESSES_COUNT];

/* a gamepad's presses, given a place first if it has none and add is set */
static struct gamepad_presses *gamepad_presses_find(Uint32 id, BOOL add)
{
	struct gamepad_presses *empty = NULL;
	int index;

	if (!id)
		return NULL;
	for (index = 0; index < GAMEPAD_PRESSES_COUNT; index++)
	{
		if (gamepad_presses[index].id == id)
			return &gamepad_presses[index];
		if (!gamepad_presses[index].id && !empty)
			empty = &gamepad_presses[index];
	}
	if (!add || !empty)
		return NULL;
	memset(empty, 0, sizeof(*empty));
	empty->id = id;
	return empty;
}

static void gamepad_button_pressed(Uint32 id, int button)
{
	struct gamepad_presses *presses = gamepad_presses_find(id, TRUE);

	if (presses && button >= 0 && button < SDL_GAMEPAD_BUTTON_COUNT)
	{
		presses->buttons |= 1u << button;
		presses->pump = gamepad_pumps;
	}
}

static void gamepad_trigger_moved(Uint32 id, int axis, int value)
{
	struct gamepad_presses *presses;
	unsigned int bit;

	if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
		bit = 1;
	else if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
		bit = 2;
	else
		return;
	presses = gamepad_presses_find(id, value >= PLATFORM_GAMEPAD_TRIGGER_PRESSED);
	if (!presses)
		return;
	if (value < PLATFORM_GAMEPAD_TRIGGER_PRESSED)
		presses->triggers_down &= ~bit;
	else if (!(presses->triggers_down & bit))
	{
		presses->triggers_down |= bit;
		presses->triggers |= bit;
		presses->pump = gamepad_pumps;
	}
}

static void gamepad_presses_forget(Uint32 id)
{
	struct gamepad_presses *presses = gamepad_presses_find(id, FALSE);

	if (presses)
		memset(presses, 0, sizeof(*presses));
}

/* (the window's focus gone: what was pressed is dropped, as the keys are) */
static void gamepad_presses_clear(void)
{
	int index;

	for (index = 0; index < GAMEPAD_PRESSES_COUNT; index++)
	{
		gamepad_presses[index].buttons = 0;
		gamepad_presses[index].triggers = 0;
	}
}

static void gamepad_presses_take(Uint32 id, Uint32 *buttons, unsigned int *triggers)
{
	struct gamepad_presses *presses = gamepad_presses_find(id, FALSE);

	*buttons = 0;
	*triggers = 0;
	if (presses && gamepad_pumps - presses->pump > GAMEPAD_TAP_PUMPS)
	{
		presses->buttons = 0;
		presses->triggers = 0;
	}
	if (presses)
	{
		*buttons = presses->buttons;
		*triggers = presses->triggers;
		presses->buttons = 0;
		presses->triggers = 0;
	}
}

void platform_gamepad_presses_take(Uint32 id, Uint32 *buttons, unsigned int *triggers)
{
	pthread_mutex_lock(&input_lock);
	gamepad_presses_take(id, buttons, triggers);
	pthread_mutex_unlock(&input_lock);
}
/* rebinding (platform_binding_capture_begin), under input_lock: waiting for
an input, and the one taken; then the keyboard and mouse settle (their keys
and buttons held then are let go) before they drive anything again */
enum
{
	_binding_capture_idle,
	_binding_capture_waiting,
	_binding_capture_taken,
};
static int binding_capture;
static int binding_capture_result;
static int binding_captured_input;
static BOOL binding_settling;
static Uint64 binding_taken_ms;
/* when the menus last asked (a capture they stop asking about, their screen
gone, ends: else the keyboard stays held from the game) */
static Uint64 binding_polled_ms;
#define BINDING_ABANDONED_MS 500
static unsigned mouse_buttons_down;
/* keys (not a modifier alone, not a held key's repeats), mouse buttons and
touches pressed since platform_movie_skip_pressed last looked: what skips a
movie (bink_playback.c), the keys not reaching the game outside text and
the console (xinput_sdl.c), and the mouse not being the Xbox's */
static volatile int movie_skip_presses;
/* (an input taken that nothing asks for is let go after this) */
#define BINDING_UNCLAIMED_MS 2000
/* the multiplayer scoreboard is open (platform_scoreboard_scroll): the wheel
and Page Up/Down scroll it, and the wheel switches no weapon; how far they
have moved it since the game last asked (notches down, pages down). Open
until the game stops saying so for SCOREBOARD_OPEN_MS (a game that ends with
it open never says it closed). */
#define SCOREBOARD_OPEN_MS 250
static Uint64 scoreboard_open_until_ms;
static float scoreboard_wheel;
static long scoreboard_notches;
static long scoreboard_pages;

/* debug keyboard queue */
#define KEYSTROKE_QUEUE_SIZE 64
static struct platform_keystroke keystroke_queue[KEYSTROKE_QUEUE_SIZE];
static unsigned long keystroke_head, keystroke_count;

#ifndef HALO_ANDROID
/* updater.c's: the desktop self-updater */
void updater_start(void);
void updater_poll(SDL_Window *window);
#endif
/* (and the version, for the window's title) */
const char *updater_version(void);
/* (whether the game runs under gamescope, where no dialog is shown) */
int updater_gamescope(void);

/* the older folders of Halo PC maps (maps/ce, md_maps) moved into the new
ones (maps_ce, maps_md) once, as game.move_old_map_folders says: asked, in
the game's message box, when someone is there to answer; the answer kept.
Android, whose game data is the app's own, moves them without asking */
static void old_map_folders_offer(BOOL quiet)
{
	static const SDL_MessageBoxButtonData buttons[] = {
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Move them" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Leave them" },
	};
	const char *setting = config_string("game.move_old_map_folders");
	char moves[256];
	char text[1024];
	SDL_MessageBoxData question = { SDL_MESSAGEBOX_INFORMATION, NULL, "Chupathingy: map folders", text, 2,
		buttons, NULL };
	int answer = 0;

	if (!strcmp(setting, "no") || !platform_old_map_folders(moves, sizeof(moves)))
		return;
#ifndef HALO_ANDROID
	if (strcmp(setting, "yes"))
	{
		if (quiet)
		{
			platform_log("map folders: older folders of Halo PC maps (%s) are played from where they are; "
				"game.move_old_map_folders = \"yes\" moves them", moves);
			return;
		}
		snprintf(text, sizeof(text),
			"Halo PC maps now go in maps_ce (Custom Edition), maps_md (HaloMD) and maps_pc (Halo PC disc).\n\n"
			"Move your older folders there?\n\n%s\n\n"
			"Nothing is copied or deleted.",
			moves);
		if (!SDL_ShowMessageBox(&question, &answer))
			return;
		config_write("game.move_old_map_folders", answer ? "yes" : "no");
		if (!answer)
		{
			platform_log("map folders: the player left the older folders where they are (%s)", moves);
			return;
		}
	}
#else
	(void)quiet;
	(void)question;
	(void)answer;
	(void)text;
#endif
	platform_old_map_folders_move();
}

/* whether the game is run without anyone to watch it (a test run, a null
renderer, a headless game list run) */
BOOL platform_run_is_unwatched(void)
{
	BOOL unwatched = config_boolean("debug.hidden_window") || config_real("debug.exit_after") > 0.0 ||
		config_boolean("debug.null_renderer");

#ifdef HALO_GAME_BROWSER
	unwatched = unwatched || browser_headless();
#endif
	return unwatched;
}

/* whether the game is run without anyone to answer a question (an
unwatched run), or where a question cannot be shown (under gamescope, the
Steam Deck's Game Mode: updater_gamescope) */
BOOL platform_run_is_quiet(void)
{
	return platform_run_is_unwatched() || updater_gamescope();
}

/* the settings whose defaults this version changed, for a player whose
config.toml has them at the old ones (or following the default): asked once,
in the game's message box, before they change; the answer kept
(game.defaults_answered). "Keep mine" writes the old values into
config.toml; "Use the new settings" lets them follow the new defaults. A
new config.toml has the new defaults, and the dedicated server keeps its
own, so neither is asked. A run that cannot ask keeps the old values and
says so in the log. Android asks too, in the same box */
static void new_defaults_offer(BOOL quiet)
{
	static const SDL_MessageBoxButtonData buttons[] = {
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Use the new settings" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Keep mine" },
	};
	char changes[1024];
	char text[1536];
	SDL_MessageBoxData question = { SDL_MESSAGEBOX_INFORMATION, NULL, "Chupathingy: new settings", text, 2,
		buttons, NULL };
	int answer = 0;

	if (!config_new_defaults_pending(changes, sizeof(changes)))
		return;
	if (quiet)
	{
		platform_log("settings: this version changes some defaults (%s); they are asked about when the game is "
			"run where it can ask (not a test run, nor Steam Deck Game Mode), and until then your settings stay "
			"as they were", changes);
		return;
	}
	snprintf(text, sizeof(text),
		"This version changes some default settings:\n\n"
		"%s\n\n"
		"Use the new settings, or keep yours?",
		changes);
	if (!SDL_ShowMessageBox(&question, &answer))
	{
		platform_log("settings: the question about the new defaults could not be shown; your settings stay as "
			"they were");
		return;
	}
	platform_log("settings: the player chose %s", answer ? "the new defaults" : "to keep their settings");
	config_new_defaults_answer(answer);
}

#ifndef HALO_ANDROID
static BOOL data_has_xbox_maps(const char *destination);
#ifdef HALO_CONSOLE_SETUP_SCREENS
/* the port's start-up screens, for a console with no dialogs or file picker
(halo_console_features.h); platform_offer_game_data is the port's too */
void console_setup_start(void);
void console_setup_after_data(const char *data_root);
#endif

/* the maps folder checked to hold the Xbox maps (platform_maps_folder_check).
When it holds Halo PC's instead, or no ui.map, the player is told where each
kind goes and the game quits, rather than failing to load Halo PC's ui.map
as the Xbox's (game_load). Halo PC maps in maps/ itself, which only play
from maps_ce, maps_md or maps_pc, are warned of. Then the older folders of
Halo PC maps are offered a move into the new ones (game.move_old_map_folders) */
static void data_check_maps(void)
{
	struct platform_maps_folder maps;
	char message[2048];
	char where[1200];
	BOOL quiet = platform_run_is_quiet();

	new_defaults_offer(quiet);
	platform_maps_folder_check(&maps);
	snprintf(where, sizeof(where),
		"Put the Xbox maps from your Halo: Combat Evolved disc image in %s/maps. "
		"Custom Edition maps go in maps_ce, HaloMD maps in maps_md, Halo PC disc maps in maps_pc.",
		maps.root);
	switch (maps.state)
	{
	case _maps_folder_xbox:
		old_map_folders_offer(quiet);
		if (!maps.stray_pc_maps)
			return;
		snprintf(message, sizeof(message),
			"These Halo PC maps are in the maps folder: %s%s.\n\n%s",
			maps.stray_names, maps.stray_pc_maps > PLATFORM_MAPS_FOLDER_NAMED ? " and more" : "", where);
		platform_log("maps folder: %ld Halo PC maps in %s/maps (%s)", maps.stray_pc_maps, maps.root,
			maps.stray_names);
		if (!quiet)
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Chupathingy", message, NULL);
		return;
	case _maps_folder_halo_pc:
		snprintf(message, sizeof(message),
			"The maps folder holds Halo PC maps, not the Xbox maps: its ui.map is from %s.\n\n%s\n\n"
			"To copy the Xbox maps from your disc image, move this maps folder aside and start Chupathingy.",
			maps.ui_version == 609 ? "Halo PC (Custom Edition)" : "Halo PC", where);
		break;
	default:
		/* (a maps folder without ui.map and no Halo PC maps in it, as "make
		the folders" leaves it: the choices offered again; the launcher's Maps
		has them) */
#ifndef HALO_LAUNCHER
		if (!quiet && !maps.stray_pc_maps && platform_offer_game_data(maps.root, sizeof(maps.root)) &&
			data_has_xbox_maps(maps.root))
		{
			return;
		}
#endif
		snprintf(message, sizeof(message),
			"Halo's Xbox maps were not found: %s/maps has no ui.map.%s\n\n%s\n\n"
			"To copy the Xbox maps from your disc image, move the maps folder aside and start Chupathingy.",
			maps.root, maps.stray_pc_maps || maps.pc_maps_beside ?
				" Halo PC maps need the Xbox maps too." : "",
			where);
		break;
	}
#ifdef HALO_LAUNCHER
	/* (the launcher opens on Maps with the window, the problem shown there,
	rather than a message and a quit) */
	if (!quiet)
	{
		platform_log("maps folder: %s/maps is not the Xbox maps (ui.map version %ld): the launcher opens on Maps",
			maps.root, maps.ui_version);
		launcher_open_on_maps();
		return;
	}
#endif
	platform_log("maps folder: %s/maps is not the Xbox maps (ui.map version %ld): quitting", maps.root,
		maps.ui_version);
	platform_log("%s", message);
	if (!quiet)
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Chupathingy", message, NULL);
	exit(EXIT_FAILURE);
}
#endif

#if !defined(_WIN32) && !defined(HALO_ANDROID)
/* crash reports (posix_crash.c; Windows's are win32_crash.c's): the handler
for this run's crashes, then the reports earlier ones left, sent, deleted or
asked about as crash_reports.upload says */
static void crash_reports_start(void)
{
	static const SDL_MessageBoxButtonData buttons[] = {
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No" },
	};
	static const char text[] =
		"Chupathingy crashed the last time it ran.\n\n"
		"Send crash reports to the developers?\n\n"
		"A report holds the game's version, where it crashed and the end of debug.txt, with IP addresses "
		"removed.\n\n"
		"Yes sends this report and later ones. No never sends one.";
	SDL_MessageBoxData question = { SDL_MESSAGEBOX_WARNING, NULL, "Chupathingy crashed", text, 2, buttons, NULL };
	char folder[1024], log[1024], old_folder[1024], old_log[1024];
	const char *consent;
	int pending, answer = 0;

	if (!crash_reports_armed())
		return;
#ifdef HALO_GAME_BROWSER
	/* (a probe has none: its crashes are the site's to see) */
	if (browser_headless())
		return;
#endif
	snprintf(folder, sizeof(folder), "%s/crashes", platform_folder(_port_folder_logs));
	snprintf(log, sizeof(log), "%s/debug.txt", platform_folder(_port_folder_logs));
	/* (and those from before the logs folder, in the data root, still sent) */
	snprintf(old_folder, sizeof(old_folder), "%s/crashes", platform_data_root());
	snprintf(old_log, sizeof(old_log), "%s/debug.txt", platform_data_root());
	if (!posix_crash_install(folder, log, old_folder, old_log) || !(pending = posix_crash_pending()))
		return;
	consent = config_string("crash_reports.upload");
	if (!strcmp(consent, "no"))
	{
		posix_crash_discard();
		return;
	}
	if (strcmp(consent, "yes"))
	{
		/* (not for runs nobody is watching, nor under gamescope: the reports
		wait) */
		if (config_boolean("debug.hidden_window") || config_real("debug.exit_after") > 0.0 ||
			updater_gamescope() || !SDL_ShowMessageBox(&question, &answer))
		{
			return;
		}
		config_write("crash_reports.upload", answer ? "yes" : "no");
		if (!answer)
		{
			platform_log("crash report: the player declined; none will be sent");
			posix_crash_discard();
			return;
		}
	}
	platform_log("crash report: sending %d", pending);
	posix_crash_send();
}
#endif

#if !defined(_WIN32) && !defined(HALO_ANDROID)
/* SIGTERM and SIGINT quit as closing the window does, when the events are
next read (SDL's handler, which this one passes them to). A game stuck where
they are not read (#1149: a load that waited for ever) never would: the
signal also wakes a watchdog, which ends the process only if the game is
stuck: its events not read (platform_pump_events, quit_heartbeat) for
QUIT_WATCHDOG_SECONDS. Once a quit is under way (platform_exit_window_closed,
or any exit: its saves run then) it never steps in, so no save is cut in
half. It only writes to standard error, as the stuck thread may hold the
log's lock */
#ifndef QUIT_WATCHDOG_SECONDS
#define QUIT_WATCHDOG_SECONDS 10
#endif

static struct sigaction quit_previous_actions[2];
static int quit_pipe[2] = { -1, -1 };
/* (each read of the events counts one; a quit under way stands it down) */
static volatile unsigned long quit_heartbeat;
static volatile int quit_under_way;

static void quit_watchdog_stand_down(void)
{
	__atomic_store_n(&quit_under_way, 1, __ATOMIC_SEQ_CST);
}

static void quit_watchdog_beat(void)
{
	__atomic_add_fetch(&quit_heartbeat, 1, __ATOMIC_RELAXED);
}

static void quit_signal_handler(int signal_number, siginfo_t *information, void *context)
{
	struct sigaction const *previous = &quit_previous_actions[signal_number == SIGINT];
	int saved_errno = errno;

	if (quit_pipe[1] >= 0)
	{
		unsigned char byte = (unsigned char)signal_number;

		if (write(quit_pipe[1], &byte, 1) < 0)
			byte = 0;
	}
	if (previous->sa_flags & SA_SIGINFO)
	{
		if (previous->sa_sigaction)
			previous->sa_sigaction(signal_number, information, context);
	}
	else if (previous->sa_handler == SIG_DFL)
	{
		/* (nothing else would have ended it: as without this handler) */
		signal(signal_number, SIG_DFL);
		raise(signal_number);
	}
	else if (previous->sa_handler != SIG_IGN)
	{
		previous->sa_handler(signal_number);
	}
	errno = saved_errno;
}

static void *quit_watchdog(void *unused)
{
	unsigned char byte = 0;
	char message[128];
	struct timespec wait;
	int length;

	(void)unused;
	while (read(quit_pipe[0], &byte, 1) != 1)
	{
		if (errno != EINTR)
			return NULL;
	}
	/* (stuck: no read of the events for the whole time, and no quit under
	way; a game that goes on reading them quits by itself) */
	{
		unsigned long last = __atomic_load_n(&quit_heartbeat, __ATOMIC_RELAXED);
		int still = 0;

		while (still < QUIT_WATCHDOG_SECONDS)
		{
			unsigned long now;

			wait.tv_sec = 1;
			wait.tv_nsec = 0;
			while (nanosleep(&wait, &wait) != 0 && errno == EINTR)
				;
			if (__atomic_load_n(&quit_under_way, __ATOMIC_SEQ_CST))
				return NULL;
			now = __atomic_load_n(&quit_heartbeat, __ATOMIC_RELAXED);
			still = now == last ? still + 1 : 0;
			last = now;
		}
		if (__atomic_load_n(&quit_under_way, __ATOMIC_SEQ_CST))
			return NULL;
	}
	length = snprintf(message, sizeof(message),
		"chupathingyce: quit: signal %d, and the game has been stuck for %d seconds; stopping now\n",
		(int)byte, QUIT_WATCHDOG_SECONDS);
	if (length > 0 && write(STDERR_FILENO, message, (size_t)length) < 0)
		length = 0;
	_exit(128 + (int)byte);
	return NULL;
}

static void quit_watchdog_start(void)
{
	struct sigaction action;
	pthread_t thread;
	pthread_attr_t attributes;

	if (quit_pipe[0] >= 0 || pipe(quit_pipe) != 0)
		return;
	fcntl(quit_pipe[1], F_SETFL, fcntl(quit_pipe[1], F_GETFL) | O_NONBLOCK);
	fcntl(quit_pipe[0], F_SETFD, FD_CLOEXEC);
	fcntl(quit_pipe[1], F_SETFD, FD_CLOEXEC);
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&thread, &attributes, quit_watchdog, NULL) != 0)
	{
		pthread_attr_destroy(&attributes);
		return;
	}
	pthread_attr_destroy(&attributes);
	/* (the last registered runs first: before every save exit makes) */
	atexit(quit_watchdog_stand_down);
	memset(&action, 0, sizeof(action));
	action.sa_sigaction = quit_signal_handler;
	action.sa_flags = SA_SIGINFO | SA_RESTART;
	sigemptyset(&action.sa_mask);
	sigaction(SIGTERM, &action, &quit_previous_actions[0]);
	sigaction(SIGINT, &action, &quit_previous_actions[1]);
}
#endif

/* SDL's own messages in debug.txt: what its video driver did (a Wayland
connection the compositor closed, which ends the game as "window closed";
the driver it chose and why), and anything it warns about. Its video
category at info, where those are; the others at their defaults */
static void SDLCALL platform_sdl_log(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
	static const char *const priorities[] = { "", "trace", "verbose", "debug", "info", "warning", "error", "critical" };

	(void)userdata;
	(void)category;
	platform_log("SDL %s: %s", priority > 0 && priority < (int)(sizeof(priorities) / sizeof(priorities[0])) ?
		priorities[priority] : "message", message);
}

BOOL platform_sdl_initialize(void)
{
	if (platform_sdl_started)
		return TRUE;
	SDL_SetLogOutputFunction(platform_sdl_log, NULL);
	SDL_SetLogPriority(SDL_LOG_CATEGORY_VIDEO, SDL_LOG_PRIORITY_INFO);
#if !defined(_WIN32) && !defined(HALO_ANDROID)
	/* a write to a connection the other end closed fails instead of ending
	the game (the game's sockets and Discord's pass MSG_NOSIGNAL, but UPnP's
	miniupnpc does not, nor does a write to a closed pipe's standard error) */
	signal(SIGPIPE, SIG_IGN);
#endif
	/* a copy of the game started to open an invite link hands it to the
	one already running, and goes */
	if (p2p_hand_off_invite())
		exit(EXIT_SUCCESS);
	SDL_SetHint(SDL_HINT_APP_NAME, "ChupathingyCE");
#ifdef __APPLE__
	/* closing the window is the event's to decide (platform_pump_events:
	Command-W does not quit) */
	SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
#endif
#ifdef HALO_ANDROID
	/* landscape only; the back key arrives as a key event (xinput_sdl.c)
	instead of closing the activity */
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
	/* touching the screen must not aim or fire (the mouse drives the
	controller emulation in xinput_sdl.c) */
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif
#ifdef HALO_GAME_BROWSER
	/* the dedicated server and a probe play no sound and need no display
	(server/src) */
	if (browser_headless())
	{
		SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
	}
#endif
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS))
	{
		char reason[256];

		snprintf(reason, sizeof(reason), "%s", SDL_GetError());
		platform_log("SDL_Init failed: %s", reason);
#ifdef HALO_ANDROID
		/* (a phone always has its screen) */
		return FALSE;
#else
		/* (the window first: without a display there is no game, and the
		renderer would only crash on the window it never got; without sound
		or gamepads there still is one) */
		if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
		{
			char message[512];

			snprintf(reason, sizeof(reason), "%s", SDL_GetError());
			snprintf(message, sizeof(message),
				"Chupathingy couldn't open a window: %s.\n\n"
				"It needs a display to run on. Check that one is connected and turned on.", reason);
			platform_log("video: no window (%s): quitting", reason);
			fprintf(stderr, "Chupathingy: %s\n", message);
			if (!config_boolean("debug.hidden_window") && config_real("debug.exit_after") <= 0.0)
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Chupathingy", message, NULL);
			SDL_Quit();
			exit(EXIT_FAILURE);
		}
		if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
			platform_log("audio: could not start (%s): playing without sound", SDL_GetError());
		if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
			platform_log("gamepads: could not start (%s)", SDL_GetError());
#endif
	}
	platform_sdl_started = TRUE;
#if !defined(_WIN32) && !defined(HALO_ANDROID)
	/* (after SDL_Init, whose handlers it passes the signals on to) */
	quit_watchdog_start();
#endif
#ifdef HALO_CONSOLE_SETUP_SCREENS
	/* (the port's: halo_console_features.h) */
	console_setup_start();
#endif
	/* which build this is, first in its log (build_identity.c) */
	build_identity_log();
#ifndef HALO_ANDROID
	/* found (or offered to the player, platform_offer_game_data) before the
	game's window opens, and checked to be the Xbox maps */
	platform_data_root();
	data_check_maps();
#ifdef HALO_CONSOLE_SETUP_SCREENS
	console_setup_after_data(platform_data_root());
#endif
	/* (a new version looked for meanwhile, updater_poll asking about it) */
	updater_start();
#ifndef _WIN32
	crash_reports_start();
#endif
#else
	/* (the desktop's are moved from data_check_maps) */
	new_defaults_offer(platform_run_is_quiet());
	old_map_folders_offer(TRUE);
#endif
	/* (the legacy table: a newer one fetched meanwhile) */
	delta_legacy_start();
#if defined(HALO_GAME_BROWSER) && !defined(HALO_SERVER)
	/* (Chupathingy maps' index, for its blacklist before the first join:
	map_repo.h) */
	map_repo_game_refresh_soon();
#endif
	return TRUE;
}

/* the mouse pointer shown or hidden (Android has none to show) */
static void show_pointer(BOOL shown)
{
#ifndef HALO_ANDROID
	if (shown)
		SDL_ShowCursor();
	else
		SDL_HideCursor();
#else
	(void)shown;
#endif
}

#ifndef HALO_ANDROID

/* ---------- first start without game data (xbox_files.c)

The player is offered choices, never told to find a disc: Halo's maps from
an Xbox disc image (the default, and the only data the game needs), Halo
PC's maps from its CD image or Custom Edition's installer as optional
extras (into maps_pc and maps_ce), or the folders made empty for them to
fill themselves. A file picked is told apart by what it holds, not its
name: an Xbox disc image (xiso.c), Custom Edition's installer
(ce_installer.c), or a CD image (pc_disc.c) */

/* (sdl_platform.h's PLATFORM_IMPORT_*) */
enum
{
	_data_unknown = PLATFORM_IMPORT_UNKNOWN,
	_data_xbox_disc = PLATFORM_IMPORT_XBOX_DISC,
	_data_halo_pc_disc = PLATFORM_IMPORT_HALO_PC_DISC,
	_data_custom_edition_installer = PLATFORM_IMPORT_CE_INSTALLER,
};

struct data_extraction
{
	pthread_mutex_t lock;
	int kind;
	char image[1024];
	char destination[1024];
	char file[256];
	unsigned long long done;
	unsigned long long total;
	BOOL running;
	BOOL finished;
	BOOL succeeded;
	char error[512];
};

/* (one at a time: the first-run window's or the launcher's) */
static struct data_extraction extraction;
static pthread_mutex_t extraction_start_lock = PTHREAD_MUTEX_INITIALIZER;

static void data_extraction_progress(void *context, const char *file, unsigned long long done,
	unsigned long long total)
{
	struct data_extraction *extraction = context;

	pthread_mutex_lock(&extraction->lock);
	snprintf(extraction->file, sizeof(extraction->file), "%s", file);
	extraction->done = done;
	extraction->total = total;
	pthread_mutex_unlock(&extraction->lock);
}

static void *data_extraction_thread(void *context)
{
	struct data_extraction *extraction = context;
	BOOL succeeded;

	switch (extraction->kind)
	{
	case _data_halo_pc_disc:
		succeeded = pc_disc_extract_maps(extraction->image, extraction->destination, data_extraction_progress,
			extraction, extraction->error, sizeof(extraction->error)) != 0;
		break;
	case _data_custom_edition_installer:
		succeeded = ce_installer_extract_maps(extraction->image, extraction->destination,
			data_extraction_progress, extraction, extraction->error, sizeof(extraction->error)) != 0;
		break;
	default:
		succeeded = xiso_extract_maps(extraction->image, extraction->destination, data_extraction_progress,
			extraction, extraction->error, sizeof(extraction->error)) != 0;
		break;
	}
	pthread_mutex_lock(&extraction->lock);
	extraction->succeeded = succeeded;
	extraction->finished = TRUE;
	pthread_mutex_unlock(&extraction->lock);
	return NULL;
}

BOOL platform_import_start(int kind, const char *image, const char *destination, char *error, int error_size)
{
	static BOOL lock_made;
	pthread_t thread;

	pthread_mutex_lock(&extraction_start_lock);
	if (lock_made)
	{
		BOOL busy;

		pthread_mutex_lock(&extraction.lock);
		busy = extraction.running && !extraction.finished;
		pthread_mutex_unlock(&extraction.lock);
		if (busy)
		{
			pthread_mutex_unlock(&extraction_start_lock);
			snprintf(error, (size_t)error_size, "Maps are being copied already.");
			return FALSE;
		}
		pthread_mutex_destroy(&extraction.lock);
	}
	memset(&extraction, 0, sizeof(extraction));
	pthread_mutex_init(&extraction.lock, NULL);
	lock_made = TRUE;
	extraction.kind = kind;
	extraction.running = TRUE;
	snprintf(extraction.image, sizeof(extraction.image), "%s", image);
	snprintf(extraction.destination, sizeof(extraction.destination), "%s", destination);
	if (pthread_create(&thread, NULL, data_extraction_thread, &extraction) != 0)
	{
		extraction.running = FALSE;
		pthread_mutex_unlock(&extraction_start_lock);
		snprintf(error, (size_t)error_size, "Could not start the extraction.");
		return FALSE;
	}
	/* (waited for through extraction.finished; the Windows port's threads
	cannot be joined) */
	pthread_detach(thread);
	pthread_mutex_unlock(&extraction_start_lock);
	return TRUE;
}

BOOL platform_import_poll(char *file, int file_size, unsigned long long *done, unsigned long long *total,
	BOOL *succeeded, char *error, int error_size)
{
	BOOL finished;

	pthread_mutex_lock(&extraction.lock);
	finished = extraction.finished;
	if (file && file_size > 0)
		snprintf(file, (size_t)file_size, "%s", extraction.file);
	if (done)
		*done = extraction.done;
	if (total)
		*total = extraction.total;
	if (succeeded)
		*succeeded = extraction.succeeded;
	if (finished && error && error_size > 0)
		snprintf(error, (size_t)error_size, "%s", extraction.succeeded ? "" : extraction.error);
	pthread_mutex_unlock(&extraction.lock);
	return finished;
}

#if !defined(HALO_LAUNCHER) && !defined(HALO_CONSOLE_SETUP_SCREENS)
/* copies the maps, showing how far it has got; closing the window quits.
The bytes written in written (0 when the folder had them all) */
static BOOL data_extract(int kind, const char *image, const char *destination, unsigned long long *written,
	char *error, int error_size)
{
	const char *title = kind == _data_halo_pc_disc ? "Importing Halo PC maps..." :
		kind == _data_custom_edition_installer ? "Importing Custom Edition maps..." :
		"Extracting the maps folder...";
	SDL_Window *window;
	SDL_Renderer *renderer = NULL;
	BOOL finished = FALSE, succeeded = FALSE;
	unsigned long long done = 0, total = 0;

	if (!platform_import_start(kind, image, destination, error, error_size))
		return FALSE;
	window = SDL_CreateWindow("Chupathingy", 640, 150, 0);
	if (window)
	{
		renderer = SDL_CreateRenderer(window, NULL);
		if (renderer)
			SDL_SetRenderVSync(renderer, 1);
	}
	while (!finished)
	{
		SDL_Event event;
		char file[256];

		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
			{
				platform_log("extraction cancelled");
				exit(EXIT_SUCCESS);
			}
		}
		finished = platform_import_poll(file, sizeof(file), &done, &total, &succeeded, error, error_size);
		if (renderer)
		{
			char line[320];
			SDL_FRect bar = { 20.0f, 100.0f, 600.0f, 24.0f };
			float fraction = total ? (float)((double)done / (double)total) : 0.0f;

			SDL_SetRenderDrawColor(renderer, 12, 16, 20, 255);
			SDL_RenderClear(renderer);
			SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
			SDL_SetRenderScale(renderer, 2.0f, 2.0f);
			SDL_RenderDebugText(renderer, 10.0f, 10.0f, title);
			SDL_SetRenderScale(renderer, 1.0f, 1.0f);
			snprintf(line, sizeof(line), "%s  (%llu of %llu MB)", file, done >> 20, total >> 20);
			SDL_RenderDebugText(renderer, 20.0f, 70.0f, line);
			SDL_SetRenderDrawColor(renderer, 60, 66, 72, 255);
			SDL_RenderFillRect(renderer, &bar);
			bar.w *= fraction;
			SDL_SetRenderDrawColor(renderer, 90, 160, 90, 255);
			SDL_RenderFillRect(renderer, &bar);
			SDL_RenderPresent(renderer);
		}
		SDL_Delay(16);
	}
	if (renderer)
		SDL_DestroyRenderer(renderer);
	if (window)
		SDL_DestroyWindow(window);
	*written = done;
	return succeeded;
}

struct data_image_choice
{
	SDL_AtomicInt done;
	BOOL failed;
	char path[1024];
	char error[256];
};

static void SDLCALL data_image_chosen(void *userdata, const char * const *files, int filter)
{
	struct data_image_choice *choice = userdata;

	(void)filter;
	if (!files)
	{
		/* no dialog could be shown (SDL needs the desktop portal through
		libdbus, or zenity): not a cancelled choice */
		choice->failed = TRUE;
		snprintf(choice->error, sizeof(choice->error), "%s",
			*SDL_GetError() ? SDL_GetError() : "no desktop portal or zenity");
	}
	else if (files[0])
	{
		snprintf(choice->path, sizeof(choice->path), "%s", files[0]);
	}
	SDL_SetAtomicInt(&choice->done, 1);
}

/* the file the player picks (Halo PC's first with halo_pc); FALSE if they
pick none, with *failed set (and error written) when no dialog could be
shown */
static BOOL data_choose_image(char *path, int size, BOOL halo_pc, BOOL *failed, char *error, int error_size)
{
	static const SDL_DialogFileFilter xbox_filters[] =
	{
		{ "Xbox disc images", "iso;xiso" },
		{ "Halo PC CD images and the Custom Edition installer", "iso;exe" },
		{ "All files", "*" },
	};
	static const SDL_DialogFileFilter halo_pc_filters[] =
	{
		{ "Halo PC CD images and the Custom Edition installer", "iso;exe" },
		{ "Xbox disc images", "iso;xiso" },
		{ "All files", "*" },
	};
	static struct data_image_choice choice;

	memset(&choice, 0, sizeof(choice));
	SDL_ShowOpenFileDialog(data_image_chosen, &choice, NULL, halo_pc ? halo_pc_filters : xbox_filters, 3, NULL,
		false);
	/* the dialog answers through events (and on some systems another
	thread) */
	while (!SDL_GetAtomicInt(&choice.done))
	{
		SDL_PumpEvents();
		SDL_Delay(50);
	}
	*failed = choice.failed;
	if (choice.failed)
		snprintf(error, (size_t)error_size, "%s", choice.error);
	if (!choice.path[0])
		return FALSE;
	snprintf(path, (size_t)size, "%s", choice.path);
	return TRUE;
}

#endif

/* what a picked file holds (an Xbox image of a whole disc has a CD's file
system too, so it is looked for first) */
int platform_import_kind(const char *path)
{
	if (xiso_is_image(path))
		return _data_xbox_disc;
	if (ce_installer_is_installer(path))
		return _data_custom_edition_installer;
	if (pc_disc_is_image(path))
		return _data_halo_pc_disc;
	return _data_unknown;
}

/* whether the Xbox maps are in destination's maps folder */
static BOOL data_has_xbox_maps(const char *destination)
{
	char maps[1100];
	char on_disk[256];

	snprintf(maps, sizeof(maps), "%s/maps", destination);
	return posix_find_entry_case_insensitive(maps, "ui.map", on_disk, sizeof(on_disk)) != 0;
}

#if !defined(HALO_LAUNCHER) && !defined(HALO_CONSOLE_SETUP_SCREENS)
/* "make the folders": the maps folders made empty, what goes where shown,
and checked again when the player says so; TRUE once the Xbox maps are
there, FALSE to go back to the choices */
static BOOL data_make_folders(const char *destination)
{
	static const char *const folders[] = { "maps", "maps_ce", "maps_pc", "maps_md" };
	static const SDL_MessageBoxButtonData buttons[] =
	{
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Check again" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Back" },
	};
	char message[1400];
	size_t index;

	for (index = 0; index < sizeof(folders) / sizeof(folders[0]); index++)
	{
		char path[1100];

		snprintf(path, sizeof(path), "%s/%s", destination, folders[index]);
		posix_make_directory(path);
	}
	platform_log("made the maps folders in %s", destination);
	snprintf(message, sizeof(message),
		"The folders are in %s:\n\n"
		"maps: the Xbox maps, with ui.map. Required.\n"
		"maps_ce: Custom Edition maps, with bitmaps.map, sounds.map and loc.map.\n"
		"maps_pc: the maps from your Halo PC disc.\n"
		"maps_md: HaloMD maps.\n\n"
		"Put the files in, then choose Check again.",
		destination);
	for (;;)
	{
		SDL_MessageBoxData question = { SDL_MESSAGEBOX_INFORMATION, NULL, "Chupathingy", message, 2, buttons,
			NULL };
		int answer = 0;

		if (!SDL_ShowMessageBox(&question, &answer) || answer != 1)
			return FALSE;
		if (data_has_xbox_maps(destination))
		{
			platform_log("the Xbox maps are in %s/maps", destination);
			return TRUE;
		}
		snprintf(message, sizeof(message),
			"There is no ui.map in %s/maps yet.\n\n"
			"Copy the maps folder of your Xbox disc there, then choose Check again.",
			destination);
	}
}

BOOL platform_offer_game_data(char *destination, unsigned long size)
{
	enum
	{
		_choice_quit,
		_choice_xbox,
		_choice_halo_pc,
		_choice_folders,
	};
	static const SDL_MessageBoxButtonData buttons[] =
	{
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, _choice_xbox, "Xbox disc image" },
		{ 0, _choice_halo_pc, "Halo PC or Custom Edition" },
		{ 0, _choice_folders, "Make the folders" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, _choice_quit, "Quit" },
	};
	char message[1400];

	/* not for runs nobody is watching, nor under gamescope (the player adds
	the maps from the desktop) */
	if (config_boolean("debug.hidden_window") || config_real("debug.exit_after") > 0.0)
		return FALSE;
	if (updater_gamescope())
	{
		platform_log("no game data: start Chupathingy once from Desktop Mode to add the maps");
		return FALSE;
	}
	if (!SDL_Init(SDL_INIT_VIDEO))
		return FALSE;
	snprintf(message, sizeof(message),
		"Chupathingy needs the maps from your Halo: Combat Evolved Xbox disc. Add them from:\n\n"
		"Xbox disc image: a disc image (.iso), copied into %s/maps.\n\n"
		"Halo PC or Custom Edition (optional): a Halo PC CD image (.iso) or the "
		"Custom Edition installer (halocesetup_en_1.00.exe).\n\n"
		"Make the folders: empty map folders to fill yourself.",
		destination);
	for (;;)
	{
		SDL_MessageBoxData question = { SDL_MESSAGEBOX_INFORMATION, NULL, "Chupathingy", message, 4, buttons,
			NULL };
		char image[1024];
		char error[512];
		unsigned long long written = 0;
		int answer = _choice_quit;
		int kind;

		if (!SDL_ShowMessageBox(&question, &answer) || answer == _choice_quit)
		{
			platform_log("no game data: quitting");
			exit(EXIT_SUCCESS);
		}
		if (answer == _choice_folders)
		{
			if (data_make_folders(destination))
				return TRUE;
			continue;
		}
		/* no file picked: ask again; no file dialog at all: say why, or the
		same question comes back without end */
		{
			BOOL failed = FALSE;

			if (!data_choose_image(image, sizeof(image), answer == _choice_halo_pc, &failed, error,
				sizeof(error)))
			{
				if (failed)
				{
					char explanation[1400];

					platform_log("no file dialog could be shown: %s", error);
					snprintf(explanation, sizeof(explanation),
						"No file dialog could be shown (%s).\n\n"
						"Install zenity or the desktop portal, or choose Make the folders and copy "
						"the maps folder of your Xbox disc into %s/maps yourself.",
						error, destination);
					SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Chupathingy", explanation, NULL);
				}
				continue;
			}
		}
		kind = platform_import_kind(image);
		if (kind == _data_unknown)
		{
			platform_log("%s is no disc image or installer this reads", image);
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Chupathingy",
				"That file is not an Xbox disc image, a Halo PC CD image or the Custom Edition installer.",
				NULL);
			continue;
		}
		platform_log("importing maps from %s to %s", image, destination);
		if (!data_extract(kind, image, destination, &written, error, sizeof(error)))
		{
			platform_log("extraction failed: %s", error);
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Chupathingy", error, NULL);
			continue;
		}
		if (kind == _data_xbox_disc)
		{
			platform_log("extracted the maps folder");
			return TRUE;
		}
		{
			const char *folder = kind == _data_halo_pc_disc ? "maps_pc" : "maps_ce";
			char done[1400];

			platform_log("imported %llu MB of maps into %s/%s", written >> 20, destination, folder);
			snprintf(done, sizeof(done), "%s\n\nNext, add the Xbox maps: choose Xbox disc image.",
				written ? (kind == _data_halo_pc_disc ? "The Halo PC maps are in maps_pc." :
					"The Custom Edition maps are in maps_ce.") :
				(kind == _data_halo_pc_disc ? "maps_pc already has these maps." :
					"maps_ce already has these maps."));
			if (data_has_xbox_maps(destination))
			{
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Chupathingy",
					written ? "The maps are added." : "The folder already has these maps.", NULL);
				return TRUE;
			}
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Chupathingy", done, NULL);
		}
	}
}
#elif defined(HALO_LAUNCHER)
/* the launcher's Welcome (launcher.c), in the game's window */
BOOL platform_offer_game_data(char *destination, unsigned long size)
{
	/* (the window it opens finds the data root again while it is being
	found: asked once) */
	static BOOL offering;
	BOOL found;

	/* not for runs nobody is watching, nor under gamescope (the player adds
	the maps from the desktop) */
	if (offering || config_boolean("debug.hidden_window") || config_real("debug.exit_after") > 0.0)
		return FALSE;
	if (updater_gamescope())
	{
		platform_log("no game data: start Chupathingy once from Desktop Mode to add the maps");
		return FALSE;
	}
	offering = TRUE;
	found = launcher_welcome(destination, size);
	offering = FALSE;
	return found;
}
#endif
#endif

int halo_interpolation_enabled(void)
{
	static int enabled;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.interpolation");
	}
	return enabled;
}

#ifndef HALO_ANDROID
/* the display mode (display.mode, else display.fullscreen's: borderless or
the window) */
enum
{
	_display_mode_windowed = 0,
	_display_mode_borderless,
	_display_mode_fullscreen
};

static int platform_display_mode(void)
{
	const char *mode = config_string("display.mode");

	if (!SDL_strcasecmp(mode, "fullscreen"))
		return _display_mode_fullscreen;
	if (!SDL_strcasecmp(mode, "borderless"))
		return _display_mode_borderless;
	if (!SDL_strcasecmp(mode, "windowed"))
		return _display_mode_windowed;
	return config_boolean("display.fullscreen") ? _display_mode_borderless : _display_mode_windowed;
}

/* whether the window opens fullscreen (either kind), never when it is
hidden */
static BOOL platform_fullscreen_setting(void)
{
	return !config_boolean("debug.hidden_window") && platform_display_mode() != _display_mode_windowed;
}

/* a size as a setting has it, "<width>x<height>": whether it is one, and
the Xbox's 640x480 or more */
static BOOL platform_size_parse(const char *text, long *width, long *height)
{
	char *end;

	*width = strtol(text, &end, 10);
	*height = *end == 'x' || *end == 'X' ? strtol(end + 1, &end, 10) : 0;
	return !*end && *width >= 640 && *height >= 480;
}

/* display.resolution in pixels, or 0x0 for the display's own ("native"),
as for one the game cannot draw at */
static void platform_resolution_setting(long *width, long *height)
{
	if (!platform_size_parse(config_string("display.resolution"), width, height))
		*width = *height = 0;
}

/* the window's size (display.window_size), else the Xbox's 640x480 times
display.window_scale, as older versions set it */
static void platform_window_size_setting(long *width, long *height)
{
	long scale = config_integer("display.window_scale");

	if (!platform_size_parse(config_string("display.window_size"), width, height))
	{
		*width = 640 * (scale < 1 ? 1 : scale);
		*height = 480 * (scale < 1 ? 1 : scale);
	}
}

/* a display's own size in pixels (its desktop mode) */
static BOOL platform_display_size(SDL_DisplayID display, long *width, long *height)
{
	const SDL_DisplayMode *mode = display ? SDL_GetDesktopDisplayMode(display) : NULL;

	if (!mode)
		return FALSE;
	*width = (long)(mode->w * mode->pixel_density + 0.5f);
	*height = (long)(mode->h * mode->pixel_density + 0.5f);
	return TRUE;
}

/* whether fullscreen can set the display's refresh rate: on Windows, and
on an X server's own display; not on Wayland or XWayland (whose modes are
emulated, the display's own kept), nor under gamescope, nor on the Mac,
iOS or Android, whose displays stay at their own rate */
static BOOL platform_refresh_rates_settable(void)
{
#if defined(HALO_ANDROID) || defined(__APPLE__)
	return FALSE;
#else
	const char *driver = SDL_GetCurrentVideoDriver();
	const char *wayland = getenv("WAYLAND_DISPLAY");
	const char *gamescope = getenv("GAMESCOPE_WAYLAND_DISPLAY");

	if (!driver)
		return FALSE;
	if (!strcmp(driver, "windows"))
		return TRUE;
	return !strcmp(driver, "x11") && !(wayland && wayland[0]) && !(gamescope && gamescope[0]);
#endif
}

/* display.refresh_rate where fullscreen can set it, else 0: the display's
own. Based on work by erin.cena in OpenH1. */
static float platform_refresh_rate_setting(void)
{
	double rate = config_real("display.refresh_rate");

	return rate > 0.0 && rate < 1000.0 && platform_refresh_rates_settable() ? (float)rate : 0.0f;
}

#if defined(__APPLE__) && !defined(HALO_IOS)
/* port/macos/src/macos_video.c */
int macos_window_in_full_screen_space(void *window);
void macos_window_toggle_full_screen_space(void *window);
#endif

/* macOS: the window back out of a full-screen Space that macOS put it in
while SDL had it in exclusive fullscreen (display.mode = "fullscreen"),
before SDL changes its fullscreen. macOS's own full screen (the window's
toggleFullScreen:, which its full-screen shortcut and menu item call) moves
even the exclusive window to a Space, and SDL still takes it for exclusive:
its next change of kind or of fullscreen restyled the window as if it were
not in a Space, and AppKit threw ("NSWindowStyleMaskFullScreen cleared on a
window outside of a full screen transition"), which ended the game
(#1110). Taken out of the Space first, and the move waited for, the window
is an ordinary one again to SDL. Whether the window left a Space (the
change then has to make it fullscreen again, platform_display_apply), and
FALSE when it is still in one after 3 s: SDL's change is left for the next
apply rather than made into a throw. */
static BOOL platform_window_left_space;

static BOOL platform_window_space_settle(void)
{
#if defined(__APPLE__) && !defined(HALO_IOS)
	static BOOL logged_stuck;
	void *window;
	Uint64 deadline;

	if (!platform_window)
		return TRUE;
	window = SDL_GetPointerProperty(SDL_GetWindowProperties(platform_window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,
		NULL);
	if (!macos_window_in_full_screen_space(window) || !SDL_GetWindowFullscreenMode(platform_window))
		return TRUE;
	platform_log("display: the window is in a full-screen Space while exclusive fullscreen; leaving the Space first");
	macos_window_toggle_full_screen_space(window);
	/* (SDL hears of the move when it ends, and only then takes the window for
	windowed) */
	deadline = SDL_GetTicks() + 3000;
	while ((macos_window_in_full_screen_space(window) || (SDL_GetWindowFlags(platform_window) & SDL_WINDOW_FULLSCREEN)) &&
		SDL_GetTicks() < deadline)
	{
		SDL_PumpEvents();
		SDL_Delay(10);
	}
	if (macos_window_in_full_screen_space(window) || (SDL_GetWindowFlags(platform_window) & SDL_WINDOW_FULLSCREEN))
	{
		if (!logged_stuck)
			platform_log("display: the window did not leave its full-screen Space; the change waits for the next apply");
		logged_stuck = TRUE;
		return FALSE;
	}
	platform_window_left_space = TRUE;
#endif
	return TRUE;
}

/* the window's fullscreen kind (display.mode): borderless, a window over
the whole desktop (SDL's fullscreen without a mode), or fullscreen, the
display taken at display.resolution's mode (the one nearest it), else at its
desktop one, at display.refresh_rate's rate (the nearest the display has at
that size), else at the desktop's. F11 switches to the kind set. */
static void platform_fullscreen_kind_apply(void)
{
	static int applied = -1;
	static long applied_width, applied_height;
	static float applied_rate;
	int exclusive = platform_display_mode() == _display_mode_fullscreen ? 1 : 0;
	long width = 0, height = 0;
	float rate = 0.0f;
	SDL_DisplayID display;
	SDL_DisplayMode closest;
	const SDL_DisplayMode *mode = NULL;
	const SDL_DisplayMode *desktop;

	if (!platform_window)
		return;
	if (exclusive)
	{
		platform_resolution_setting(&width, &height);
		rate = platform_refresh_rate_setting();
	}
	if (exclusive == applied && width == applied_width && height == applied_height && rate == applied_rate)
		return;
	applied = exclusive;
	applied_width = width;
	applied_height = height;
	applied_rate = rate;
	display = SDL_GetDisplayForWindow(platform_window);
	if (exclusive && display)
	{
		desktop = SDL_GetDesktopDisplayMode(display);
		if (width && SDL_GetClosestFullscreenDisplayMode(display, (int)width, (int)height, rate, false, &closest))
			mode = &closest;
		else if (!width && rate > 0.0f && desktop &&
			SDL_GetClosestFullscreenDisplayMode(display, desktop->w, desktop->h, rate, false, &closest))
			mode = &closest;
		else
			mode = desktop;
		if (mode && rate > 0.0f)
		{
			platform_log("display: fullscreen at %dx%d, %.2f Hz (display.refresh_rate %.2f)", mode->w, mode->h,
				(double)mode->refresh_rate, (double)rate);
		}
	}
	if (!platform_window_space_settle())
	{
		/* (tried again at the next apply) */
		applied = -1;
		return;
	}
	SDL_SetWindowFullscreenMode(platform_window, mode);
}

/* whether the game last asked for the window to be fullscreen */
static BOOL platform_fullscreen_requested = FALSE;

#ifdef _WIN32
/* port/windows/src/win32_window.c */
void win32_window_borderless_styles(void *window_handle);
#endif

/* Windows: the borderless window an ordinary window with no frame, not
SDL's topmost popup, which some drivers take for exclusive fullscreen
(win32_window.c); before the context is made and before each frame shows,
since SDL sets its styles again on a change of focus or mode */
#ifdef _WIN32
static void platform_borderless_styles(void)
{
	if (platform_window && platform_fullscreen_requested && platform_display_mode() == _display_mode_borderless)
	{
		win32_window_borderless_styles(SDL_GetPointerProperty(SDL_GetWindowProperties(platform_window),
			SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL));
	}
}
#endif

/* (FALSE when macOS kept the window in a full-screen Space: nothing
changed, platform_window_space_settle) */
static BOOL platform_window_set_fullscreen(BOOL fullscreen)
{
	platform_fullscreen_requested = fullscreen;
	if (!platform_window_space_settle())
		return FALSE;
	SDL_SetWindowFullscreen(platform_window, fullscreen ? true : false);
	return TRUE;
}

/* whether the window is fullscreen. SDL sets its flag when the window
manager confirms the request, and some never do: gamescope (the Steam
Deck's Game Mode) makes the window the size of the display but leaves the
flag unset, so the game drew 640x480 and gamescope stretched it. A window
that was asked to be fullscreen and covers its display counts too. */
static BOOL platform_window_fullscreen(void)
{
	SDL_DisplayID display;
	const SDL_DisplayMode *mode;
	int width, height;

	if (SDL_GetWindowFlags(platform_window) & SDL_WINDOW_FULLSCREEN)
		return TRUE;
	if (!platform_fullscreen_requested)
		return FALSE;
	display = SDL_GetDisplayForWindow(platform_window);
	mode = display ? SDL_GetDesktopDisplayMode(display) : NULL;
	if (!mode || !SDL_GetWindowSizeInPixels(platform_window, &width, &height))
		return FALSE;
	return width >= (int)(mode->w * mode->pixel_density + 0.5f) &&
		height >= (int)(mode->h * mode->pixel_density + 0.5f);
}

/* the size in pixels the game draws its picture at (d3d8_gl.c): the
window's, the display's while fullscreen, or display.resolution's while
fullscreen (either kind) where the display has room for it; before the
window opens, what it will be. FALSE where display.resolution_scaling is
"original": the Xbox's 640x480, scaled to the window. */
BOOL platform_screen_mode(long *width, long *height)
{
	/* (the size last given, for a window that has none: minimized) */
	static long last_width, last_height;
	long resolution_width, resolution_height;
	BOOL fullscreen;

	if (!strcmp(config_string("display.resolution_scaling"), "original"))
		return FALSE;
	if (platform_window)
	{
		int pixel_width = 0, pixel_height = 0;

		SDL_GetWindowSizeInPixels(platform_window, &pixel_width, &pixel_height);
		if (pixel_width <= 0 || pixel_height <= 0)
		{
			*width = last_width;
			*height = last_height;
			return last_width > 0;
		}
		*width = pixel_width;
		*height = pixel_height;
		fullscreen = platform_window_fullscreen();
	}
	else
	{
		if (!platform_sdl_initialize())
			return FALSE;
		fullscreen = platform_fullscreen_setting();
		platform_window_size_setting(width, height);
		if (fullscreen && !platform_display_size(SDL_GetPrimaryDisplay(), width, height))
			return FALSE;
	}
	/* (fullscreen's display is at the resolution already, where it has that
	mode: platform_fullscreen_kind_apply; borderless's is scaled to) */
	platform_resolution_setting(&resolution_width, &resolution_height);
	if (fullscreen && resolution_width && resolution_width <= *width && resolution_height <= *height)
	{
		*width = resolution_width;
		*height = resolution_height;
	}
	last_width = *width;
	last_height = *height;
	return TRUE;
}

/* the size added to the list unless it has it already; the count */
static int platform_resolution_add(long *widths, long *heights, int count, int maximum, long width, long height)
{
	int index;

	for (index = 0; index < count; index++)
	{
		if (widths[index] == width && heights[index] == height)
			return count;
	}
	if (count < maximum)
	{
		widths[count] = width;
		heights[count] = height;
		count++;
	}
	return count;
}

int platform_display_resolutions(long *widths, long *heights, int maximum)
{
	SDL_DisplayID display;
	SDL_DisplayMode **modes;
	long display_width, display_height, width, height;
	int mode_count = 0, count = 0, index;

	if (maximum < 1 || !platform_sdl_initialize())
		return 0;
	display = platform_window ? SDL_GetDisplayForWindow(platform_window) : 0;
	if (!display)
		display = SDL_GetPrimaryDisplay();
	if (!platform_display_size(display, &display_width, &display_height))
		return 0;
	modes = SDL_GetFullscreenDisplayModes(display, &mode_count);
	for (index = 0; modes && index < mode_count; index++)
	{
		width = (long)(modes[index]->w * modes[index]->pixel_density + 0.5f);
		height = (long)(modes[index]->h * modes[index]->pixel_density + 0.5f);
		if (width >= 640 && height >= 480 && width <= display_width && height <= display_height &&
			(width != display_width || height != display_height))
		{
			count = platform_resolution_add(widths, heights, count, maximum, width, height);
		}
	}
	SDL_free(modes);
	/* (the one set, though this display has no such mode, so that Video
	Setup shows it) */
	platform_resolution_setting(&width, &height);
	if (width && (width != display_width || height != display_height))
		count = platform_resolution_add(widths, heights, count, maximum, width, height);
	/* largest first */
	for (index = 1; index < count; index++)
	{
		int place;

		width = widths[index];
		height = heights[index];
		for (place = index; place > 0 && (widths[place - 1] < width ||
			(widths[place - 1] == width && heights[place - 1] < height)); place--)
		{
			widths[place] = widths[place - 1];
			heights[place] = heights[place - 1];
		}
		widths[place] = width;
		heights[place] = height;
	}
	return count;
}

/* Video Setup's window sizes, by shape (4:3, 16:10, 16:9, 21:9), each from
the smallest */
static const short platform_window_sizes_offered[][2] =
{
	{ 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 960 }, { 1600, 1200 }, { 1920, 1440 }, { 2560, 1920 },
	{ 1280, 800 }, { 1440, 900 }, { 1680, 1050 }, { 1920, 1200 }, { 2560, 1600 },
	{ 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 },
	{ 2560, 1080 }, { 3440, 1440 }, { 3840, 1600 }, { 5120, 2160 },
};

int platform_window_sizes(long *widths, long *heights, int maximum)
{
	SDL_DisplayID display;
	SDL_Rect usable;
	long width, height;
	int count = 0, index;

	if (maximum < 1 || !platform_sdl_initialize())
		return 0;
	display = platform_window ? SDL_GetDisplayForWindow(platform_window) : 0;
	if (!display)
		display = SDL_GetPrimaryDisplay();
	if (!display || !SDL_GetDisplayUsableBounds(display, &usable))
		usable.w = usable.h = 0;
	for (index = 0; index < (int)(sizeof(platform_window_sizes_offered) / sizeof(*platform_window_sizes_offered));
		index++)
	{
		width = platform_window_sizes_offered[index][0];
		height = platform_window_sizes_offered[index][1];
		/* (the Xbox's own whatever the desktop's size) */
		if (!index || (width <= usable.w && height <= usable.h))
			count = platform_resolution_add(widths, heights, count, maximum, width, height);
	}
	/* (the one set, though it is none of them or too big for this desktop,
	so that Video Setup shows it) */
	platform_window_size_setting(&width, &height);
	return platform_resolution_add(widths, heights, count, maximum, width, height);
}

#else
int platform_display_resolutions(long *widths, long *heights, int maximum)
{
	(void)widths;
	(void)heights;
	(void)maximum;
	return 0;
}

int platform_window_sizes(long *widths, long *heights, int maximum)
{
	(void)widths;
	(void)heights;
	(void)maximum;
	return 0;
}

#endif
#ifndef HALO_ANDROID
/* the window's size (platform_window_size_setting), as the window was made
or last resized: platform_display_apply */
static long platform_window_width = -1, platform_window_height = -1;
/* the last configured mode; F11 can change the window independently */
static int platform_window_mode = _display_mode_windowed;
#endif

#ifndef HALO_ANDROID
/* the OpenGL version the renderer needs (platform_video_initialize): 4.5
draws as it was written, and 3.3 to 4.4 without what came after 3.3, as
macOS's 4.1 does (xgpu.h) */
#ifdef __APPLE__
#define PLATFORM_GL_NEEDED "4.1"
#else
#define PLATFORM_GL_NEEDED "3.3"
#endif

/* this computer's graphics can't run the renderer (no OpenGL context of the
version it needs, or one without the functions it calls): say so plainly
and quit, rather than go on without a display and crash at the first thing
drawn (crash reports from 0.8.0h; game issue #78). The version the driver
does have is asked from a context of no particular version. */
static void platform_gl_missing(const char *reason)
{
	char have[160] = "";
	char message[768];

	if (platform_gl_context)
	{
		SDL_GL_DestroyContext(platform_gl_context);
		platform_gl_context = NULL;
	}
	SDL_GL_ResetAttributes();
	{
		SDL_GLContext probe = SDL_GL_CreateContext(platform_window);

		if (probe)
		{
			/* (gl.h's type, with OpenGL's calling convention) */
			__typeof__(halo_glGetString) get_string;

			SDL_GL_MakeCurrent(platform_window, probe);
			get_string = (__typeof__(halo_glGetString))SDL_GL_GetProcAddress("glGetString");
			if (get_string)
			{
				const char *version = (const char *)get_string(GL_VERSION);
				const char *renderer = (const char *)get_string(GL_RENDERER);

				if (version)
					snprintf(have, sizeof(have), "%s%s%s%s", version, renderer ? " (" : "",
						renderer ? renderer : "", renderer ? ")" : "");
			}
			SDL_GL_MakeCurrent(platform_window, NULL);
			SDL_GL_DestroyContext(probe);
		}
	}
	platform_log("graphics: %s; OpenGL %s is needed%s%s: quitting", reason, PLATFORM_GL_NEEDED,
		have[0] ? ", the driver has OpenGL " : "", have);
	snprintf(message, sizeof(message),
		"Chupathingy can't run on this computer's graphics.\n\n"
		"It needs OpenGL %s%s%s.\n\n"
		"Updating the graphics driver can fix this. Some older graphics chips never get OpenGL %s, "
		"and Chupathingy can't run on those yet.",
		PLATFORM_GL_NEEDED,
		have[0] ? ", and the graphics driver here has OpenGL " : ", and the graphics driver here doesn't have it",
		have, PLATFORM_GL_NEEDED);
	if (!config_boolean("debug.hidden_window") && config_real("debug.exit_after") <= 0.0)
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Chupathingy", message, platform_window);
	exit(EXIT_FAILURE);
}
#endif

#ifdef HALO_BGFX
/* bgfx could not start (d3d8_bgfx.inl): the game draws with OpenGL */
static BOOL platform_bgfx_refused;
#endif
#ifdef HALO_VULKAN
/* the Vulkan backend could not start (d3d8_vk.inl): the game draws with OpenGL ES */
static BOOL platform_vulkan_refused;
/* the backend's own flag (port/android/host/host_vk_api.h) */
extern int host_renderer_vulkan;

/* A Vulkan start that did not get going draws with OpenGL ES for that start,
and once the window is up the player is asked whether to stay on OpenGL ES
for good (as the desktop's bgfx fallback asks): display.renderer changes only
on a yes; a no leaves it, so the next start tries Vulkan again and asks again
if it fails. The same for a driver archive (display.vk_driver) whose last
start never got as far as drawing (d3d8_vk.inl's start note): that start runs
on the phone's own driver and asks whether to keep it. A run nobody can
answer (platform_run_is_quiet) logs it only. */
static BOOL platform_vulkan_offer_pending, platform_vulkan_driver_offer_pending;
static char platform_vulkan_driver_skipped[160];

void platform_offer_opengl_es(void)
{
	platform_vulkan_offer_pending = TRUE;
}

void platform_offer_system_driver(const char *driver)
{
	snprintf(platform_vulkan_driver_skipped, sizeof(platform_vulkan_driver_skipped), "%s", driver);
	platform_vulkan_driver_offer_pending = TRUE;
}

static void platform_vulkan_offer(void)
{
	static const SDL_MessageBoxButtonData renderer_buttons[] = {
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Switch to OpenGL" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Not now" },
	};
	static const SDL_MessageBoxButtonData driver_buttons[] = {
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Use the phone's driver" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Not now" },
	};
	SDL_MessageBoxData question = { SDL_MESSAGEBOX_INFORMATION, NULL, "Chupathingy: renderer",
		"Vulkan didn't get going, so the game is using OpenGL ES for now.\n\n"
		"Switch to OpenGL ES for good? You can change it later in config.toml (display.renderer).",
		2, renderer_buttons, NULL };
	char driver_text[400];
	BOOL renderer = platform_vulkan_offer_pending, driver = platform_vulkan_driver_offer_pending;
	int answer = 0;

	platform_vulkan_offer_pending = platform_vulkan_driver_offer_pending = FALSE;
	if (!renderer && !driver)
		return;
	if (driver)
	{
		snprintf(driver_text, sizeof(driver_text),
			"The Vulkan driver %s didn't get as far as drawing the last time, so the game is using the phone's own "
			"driver for now.\n\nUse the phone's driver for good? You can change it later in config.toml (display.vk_driver).",
			platform_vulkan_driver_skipped);
		question.message = driver_text;
		question.buttons = driver_buttons;
	}
	if (platform_run_is_quiet())
	{
		platform_log("vulkan: %s for this start only; the setting stays as it is (no one to ask)",
			driver ? "the phone's driver" : "OpenGL ES");
		return;
	}
	if (!SDL_ShowMessageBox(&question, &answer))
		answer = 0;
	if (answer == 1)
	{
		config_write(driver ? "display.vk_driver" : "display.renderer", driver ? "" : "gl");
		platform_log("vulkan: the player switched to %s for good", driver ? "the phone's driver (display.vk_driver = \"\")" :
			"OpenGL ES (display.renderer = \"gl\")");
	}
	else
		platform_log("vulkan: the player kept their setting; %s is tried again next start",
			driver ? "the driver" : "Vulkan");
}
#endif
#ifdef HALO_BGFX

BOOL platform_bgfx_chosen(void)
{
	const char *renderer = config_string("display.renderer");

#ifdef _WIN32
	if (strcmp(renderer, "d3d11") == 0)
		return TRUE;
#endif
	return strcmp(renderer, "bgfx") == 0;
}
#endif

#if !defined(HALO_ANDROID) && !defined(_WIN32) && !defined(__APPLE__)
/* port: under gamescope (the Steam Deck's Game Mode), with vsync off, a
display.max_fps of the display's refresh rate or a whole fraction of it is
shown in step with the display (OpenGL). Compiled into the Linux and
FreeBSD builds; it does nothing unless gamescope runs the game. */
#define SWAP_INTERVAL_IN_STEP
#endif

#if !defined(__APPLE__) || defined(HALO_IOS)
/* the swap interval the OpenGL window was given (-1: none yet); above 0
with vsync off, display.max_fps in step with the display, and the clock
paces nothing (frame_interval_ns). platform_interval_wanted is the last
one asked for, given or refused, so that a refused one is asked for again
only when what is wanted changes. */
static int platform_interval = -1;
static int platform_interval_wanted = -1;
#endif

/* the display's refresh rate, 0 where it is not known; read again after a
display event (platform_refresh_rate_forget) or once a second, since under
gamescope it is known only after the window is shown */
#ifndef HALO_ANDROID
static float platform_refresh_rate_value;
static Uint64 platform_refresh_rate_read_at;
static BOOL platform_refresh_rate_known;

static void platform_refresh_rate_forget(void)
{
	platform_refresh_rate_known = FALSE;
}
#endif

static float platform_refresh_rate(void)
{
#ifndef HALO_ANDROID
	Uint64 now = SDL_GetTicks();

	if (!platform_refresh_rate_known || now - platform_refresh_rate_read_at >= 1000)
	{
		SDL_DisplayID display = platform_window ? SDL_GetDisplayForWindow(platform_window) : 0;
		const SDL_DisplayMode *mode = display ? SDL_GetCurrentDisplayMode(display) : NULL;

		platform_refresh_rate_value = mode && mode->refresh_rate > 0.0f ? mode->refresh_rate : 0.0f;
		platform_refresh_rate_read_at = now;
		platform_refresh_rate_known = TRUE;
	}
	return platform_refresh_rate_value;
#else
	return 0.0f;
#endif
}

/* ---------- display.vrr (vrr.h)

With VRR on, the frames are shown with vsync and held to 3 a second under
the refresh rate (frame_interval_ns), so a G-SYNC or FreeSync display shows
each as it comes. "auto" asks the system every VRR_ASK_MS; the choice and
its reason are logged as they change. Off (the default): as before. Based
on work by goatbutt and the OpenH1 team. */
static BOOL platform_vrr(void)
{
#ifndef HALO_ANDROID
	static unsigned long read_at = (unsigned long)-1;
	static Uint64 asked_at;
	static int on = -1, mode = VRR_MODE_OFF;
	static char reason[160];
	char why[160];
	int now_on;
	Uint64 now = SDL_GetTicks();

	if (read_at != config_changes())
	{
		read_at = config_changes();
		mode = vrr_mode(config_string("display.vrr"));
	}
	else if (on >= 0 && (mode != VRR_MODE_AUTO || now - asked_at < VRR_ASK_MS))
		return on ? TRUE : FALSE;
	asked_at = now;
	now_on = vrr_decide(mode, mode == VRR_MODE_AUTO ? vrr_system() : VRR_SYSTEM_NONE, why, sizeof(why));
	if (now_on != on || strcmp(why, reason))
	{
		/* (the first word logged only when it is something other than off) */
		if (on >= 0 || now_on || mode != VRR_MODE_OFF)
		{
			float rate = vrr_frame_rate(platform_refresh_rate(), config_integer("display.max_fps"));

			if (now_on && rate > 0.0f)
				platform_log("display: VRR on (%s): vsync on, at most %.0f frames a second at %.2f Hz", why,
					(double)rate, (double)platform_refresh_rate());
			else if (now_on)
				platform_log("display: VRR on (%s): vsync on, the display's refresh rate not known yet", why);
			else
				platform_log("display: VRR off (%s)", why);
		}
		snprintf(reason, sizeof(reason), "%s", why);
	}
	on = now_on;
	return on ? TRUE : FALSE;
#else
	return FALSE;
#endif
}

/* whether the frames are shown with vsync: display.vsync, or VRR on (the
bgfx renderer's reset flags read it too: d3d8_bgfx.inl) */
int platform_vsync_on(void)
{
	return config_boolean("display.vsync") || platform_vrr();
}

#if !defined(__APPLE__) || defined(HALO_IOS)
#ifdef SWAP_INTERVAL_IN_STEP
/* whether gamescope runs the game: GAMESCOPE_WAYLAND_DISPLAY, which it sets
for the programs it runs (the Steam Deck's Game Mode) */
static BOOL platform_under_gamescope(void)
{
	static int known = -1;

	if (known < 0)
	{
		const char *display = getenv("GAMESCOPE_WAYLAND_DISPLAY");

		known = display && display[0] ? 1 : 0;
	}
	return known ? TRUE : FALSE;
}
#endif

/* the refreshes each frame is shown for, 0 for none: display.vsync's 1.
Under gamescope with vsync off, a display.max_fps of the display's refresh
rate or a whole fraction of it (60 at 60 Hz, 30 or 45 at 90), within a
frame a second, so many: frames spaced by the clock at the display's own
rate drift against its refreshes, and now and then one is shown twice and
the next not at all (judder while the counter says 60: the Deck in Game
Mode with its Frame Rate Limit). An interval above 1 only with SDL's x11
driver (XWayland, gamescope's own): SDL's Wayland one takes any interval
above 1 as 1, so there a fraction keeps the clock's limit. Everywhere else
vsync off is as it was, the clock's limit, for the least latency. Based on
work by goatbutt and the OpenH1 team. */
static int platform_swap_interval(int vsync, long maximum)
{
#ifdef SWAP_INTERVAL_IN_STEP
	float refresh;
	int interval;
	int most;
	const char *driver;
	static int x11 = -1;
#endif

	if (vsync)
		return 1;
#ifdef SWAP_INTERVAL_IN_STEP
#ifdef HALO_BGFX
	if (xgpu_bgfx)
		return 0;
#endif
	if (maximum <= 0 || !platform_under_gamescope())
		return 0;
	refresh = platform_refresh_rate();
	if (refresh <= 0.0f)
		return 0;
	/* (the driver is the same for the whole run) */
	if (x11 < 0)
	{
		driver = SDL_GetCurrentVideoDriver();
		x11 = driver && !strcmp(driver, "x11") ? 1 : 0;
	}
	most = x11 ? 4 : 1;
	for (interval = 1; interval <= most; interval++)
	{
		float apart = refresh / (float)interval - (float)maximum;

		if (apart >= -1.0f && apart <= 1.0f)
			return interval;
	}
#else
	(void)maximum;
#endif
	return 0;
}

/* platform_swap_interval's, given to the OpenGL window when it changes
(force: whatever the window had, as when the window is made or Settings
are applied); one the driver refuses leaves the clock pacing the frames,
and is not asked for again until what is wanted changes */
static void platform_swap_interval_apply(BOOL force, int vsync, long maximum)
{
	int wanted = platform_swap_interval(vsync, maximum);
	int interval = wanted;

	if (!platform_gl_context || (!force && wanted == platform_interval_wanted))
		return;
	platform_interval_wanted = wanted;
	if (!SDL_GL_SetSwapInterval(interval))
	{
		platform_log("frame rate: a swap interval of %d refused (%s): the clock paces the frames", interval,
			SDL_GetError());
		interval = 0;
		SDL_GL_SetSwapInterval(0);
	}
	else if (interval > 0 && !vsync)
	{
		platform_log("frame rate: display.max_fps %ld shown in step with the display's %.2f Hz under gamescope, "
			"each frame for %d refresh%s", maximum, (double)platform_refresh_rate(), interval,
			interval == 1 ? "" : "es");
	}
	platform_interval = interval;
}
#endif

BOOL platform_video_initialize(unsigned long width, unsigned long height)
{
	int version;
	char title[64];

	if (platform_window)
		return TRUE;
	if (!platform_sdl_initialize())
		return FALSE;
#ifdef HALO_BGFX
	/* the bgfx renderer draws instead (d3d8_bgfx.inl): a window without an
	OpenGL context, whose Metal layer (macOS) or Vulkan surface bgfx draws
	into; unless it could not start before (platform_video_reopen_for_gl) */
	xgpu_bgfx = !platform_bgfx_refused && platform_bgfx_chosen();
#endif
#ifdef HALO_VULKAN
	/* the Vulkan backend draws instead (d3d8_vk.inl): a window without an
	OpenGL ES context (Android lets one API own a window), which it makes its
	surface on; unless it could not start before (platform_video_reopen_for_gl) */
	xgpu_vulkan = !platform_vulkan_refused && !strcmp(config_string("display.renderer"), "vulkan");
	host_renderer_vulkan = xgpu_vulkan;
#endif

#ifdef HALO_ANDROID
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#elif defined(HALO_CONSOLE_GLES2)
	/* ES 2, which the port fills out to the ES 3 the renderer uses
	(console_gles2_layer) */
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
	/* macOS stops at OpenGL 4.1, whose core contexts must be forward
	compatible; the renderer does without what it uses from later versions
	(d3d8_gl.c) */
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
#endif
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
#ifndef HALO_ANDROID
	/* an opaque window: SDL asks for an alpha channel by default, and some
	compositors (Windows' with some drivers, Wayland's) blend a window's
	alpha with what is behind it, which showed the desktop through the
	game */
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
#endif
	if (config_boolean("debug.gl_debug"))
	{
		int flags = 0;

#ifndef HALO_ANDROID
		/* (the Android guest's SDL has no getter: port/android) */
		SDL_GL_GetAttribute(SDL_GL_CONTEXT_FLAGS, &flags);
#endif
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, flags | SDL_GL_CONTEXT_DEBUG_FLAG);
	}
#if !defined(HALO_ANDROID) && !defined(_WIN32)
	/* Mesa's GL thread: the renderer makes thousands of GL calls a frame
	and never waits for their results, so handing them to a thread of
	their own takes a fifth of the main thread's time off it. It leaves an
	explicit mesa_glthread setting alone and other drivers ignore it. */
	setenv("mesa_glthread", "true", 0);
#endif

	/* "Chupathingy 0.5.0b" */
	snprintf(title, sizeof(title), "Chupathingy %s", updater_version());
#ifdef HALO_ANDROID
	{
		int scale = (int)config_integer("display.window_scale");

		if (scale < 1)
			scale = 1;
		platform_window = SDL_CreateWindow(title, (int)(width * scale), (int)(height * scale),
#ifdef HALO_VULKAN
			/* (a GL window's surface is stretched over the screen by the system,
			but a Vulkan swapchain is shown as it is: without high pixel density
			its images are the size of the window in points) */
			(xgpu_vulkan ? SDL_WINDOW_HIGH_PIXEL_DENSITY : SDL_WINDOW_OPENGL) |
#else
			SDL_WINDOW_OPENGL |
#endif
			SDL_WINDOW_FULLSCREEN);
	}
#else
	/* fullscreen (either kind) unless display.mode is the window, which F11
	switches to and from: display.window_size, whatever shape the fullscreen
	picture has. The game draws at the size platform_screen_mode gives
	(d3d8_gl.c). */
	(void)width;
	(void)height;
	platform_window_size_setting(&platform_window_width, &platform_window_height);
	platform_window = SDL_CreateWindow(title, (int)platform_window_width, (int)platform_window_height,
#if defined(HALO_BGFX) && defined(__APPLE__)
		(xgpu_bgfx ? SDL_WINDOW_METAL : SDL_WINDOW_OPENGL) |
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
#elif defined(HALO_BGFX)
		/* (bgfx makes the Vulkan surface itself) */
		(xgpu_bgfx ? 0 : SDL_WINDOW_OPENGL) |
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
#else
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
#endif
		(config_boolean("debug.hidden_window") ? SDL_WINDOW_HIDDEN : 0) |
		(platform_fullscreen_setting() ? SDL_WINDOW_FULLSCREEN : 0));
#endif
	if (!platform_window)
	{
		platform_log("SDL_CreateWindow failed: %s", SDL_GetError());
		return FALSE;
	}
	/* (the window's driver, for the renderer lines that follow: a Vulkan
	swap chain on Wayland is not one on X11) */
	platform_log("video: SDL %d.%d.%d, %s driver, %s window", SDL_VERSIONNUM_MAJOR(SDL_GetVersion()),
		SDL_VERSIONNUM_MINOR(SDL_GetVersion()), SDL_VERSIONNUM_MICRO(SDL_GetVersion()), SDL_GetCurrentVideoDriver(),
#ifdef HALO_BGFX
		xgpu_bgfx ? "a bgfx" :
#endif
		"an OpenGL");
#ifndef HALO_ANDROID
	platform_fullscreen_requested = platform_fullscreen_setting();
	platform_window_mode = platform_display_mode();
	platform_fullscreen_kind_apply();
#endif
#ifdef __APPLE__
	/* macOS opens a fullscreen window as an animated move to a Space of its
	own, and the first swap waits for it; the game draws its first frames
	before its event loop runs (rasterizer_preinitialize), so wait for the
	window here */
	SDL_SyncWindow(platform_window);
#endif
#ifdef HALO_VULKAN
	if (xgpu_vulkan)
	{
		platform_event_thread = SDL_GetCurrentThreadID();
		platform_log("renderer: vulkan (the window is made; the backend starts with the device)");
		return TRUE;
	}
#endif
#ifdef HALO_BGFX
	if (xgpu_bgfx)
	{
		platform_event_thread = SDL_GetCurrentThreadID();
		platform_log("renderer: bgfx");
		platform_mouse_capture(TRUE);
#ifdef HALO_LAUNCHER
		/* (the launcher starts bgfx itself to draw: xgpu_bgfx_launcher_frame) */
		launcher_at_start();
#endif
		return TRUE;
	}
#endif
#ifdef _WIN32
	platform_borderless_styles();
#endif
#ifdef XGPU_GL_LEGACY_AT_RUN_TIME
	/* (HALO_GL_LEGACY=1: the older drivers' path on any, to test it) */
	xgpu_gl_legacy = SDL_getenv("HALO_GL_LEGACY") && !strcmp(SDL_getenv("HALO_GL_LEGACY"), "1");
	platform_gl_context = xgpu_gl_legacy ? NULL : SDL_GL_CreateContext(platform_window);
	if (!platform_gl_context)
	{
		/* a driver without OpenGL 4.5 (an older integrated GPU's: game
		issue #78): a 3.3 context, which a 4.0 to 4.4 driver gives as its
		own newest, and the renderer's path without what came after 3.3 */
		if (!xgpu_gl_legacy)
			platform_log("graphics: no OpenGL 4.5 context (%s); trying 3.3", SDL_GetError());
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
		platform_gl_context = SDL_GL_CreateContext(platform_window);
		xgpu_gl_legacy = 1;
	}
#else
	platform_gl_context = SDL_GL_CreateContext(platform_window);
#endif
#ifdef HALO_ANDROID
	/* ES 3.2 where the driver has it, otherwise the renderer makes do with
	3.0 plus extensions */
	if (!platform_gl_context)
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		platform_gl_context = SDL_GL_CreateContext(platform_window);
	}
#endif
	if (!platform_gl_context)
	{
		platform_log("cannot create an OpenGL context: %s", SDL_GetError());
#ifndef HALO_ANDROID
		platform_gl_missing("no OpenGL context of the version needed");
#endif
		return FALSE;
	}
	SDL_GL_MakeCurrent(platform_window, platform_gl_context);
	if (!gl_functions_load())
	{
#ifndef HALO_ANDROID
		platform_gl_missing("OpenGL functions are missing");
#endif
		return FALSE;
	}
#ifdef XGPU_GL_LEGACY_AT_RUN_TIME
	if (xgpu_gl_legacy)
	{
		GLint major = 0, minor = 0;

		glGetIntegerv(GL_MAJOR_VERSION, &major);
		glGetIntegerv(GL_MINOR_VERSION, &minor);
		if (major < 3 || (major == 3 && minor < 3))
		{
			platform_gl_missing("no OpenGL 3.3 context");
			return FALSE;
		}
		/* (the shaders in the newest language the context has, of the
		ones macOS's 4.1 path is written for) */
		xgpu_gl_version_line = major > 4 || (major == 4 && minor >= 1) ? "#version 410 core\n" :
			major == 4 ? "#version 400 core\n" : "#version 330 core\n";
		xgpu_gl_sample_mask = major >= 4;
		platform_log("graphics: OpenGL %d.%d, without OpenGL 4.5: the renderer's older drivers' path (%.17s)",
			(int)major, (int)minor, xgpu_gl_version_line);
	}
#endif
#if defined(__APPLE__) && !defined(HALO_IOS)
	{
		/* port/macos/src/macos_video.c */
		void macos_set_swap_interval(int interval);

		version = SDL_GL_SetSwapInterval(0);
		macos_set_swap_interval(platform_vsync_on() ? 1 : 0);
	}
#else
	version = 0;
	platform_swap_interval_apply(TRUE, platform_vsync_on(), config_integer("display.max_fps"));
#endif
	(void)version;
	platform_event_thread = SDL_GetCurrentThreadID();
	platform_log("OpenGL %s on %s", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));
#ifndef HALO_ANDROID
	platform_mouse_capture(TRUE);
#endif
#ifdef HALO_LAUNCHER
	/* the launcher, before the game draws or loads anything, when it was
	asked for (--setup, a key held, the maps not right) */
	launcher_at_start();
#endif
	return TRUE;
}

#ifdef HALO_BGFX
/* bgfx did not start (d3d8_bgfx.inl, bg_initialize): the window again,
with an OpenGL context, for the OpenGL renderer, so that a computer whose
Vulkan (or Metal) is missing or broken still shows the game */
BOOL platform_video_reopen_for_gl(void)
{
	platform_log("renderer: bgfx did not start, so OpenGL");
	platform_bgfx_refused = TRUE;
	xgpu_bgfx = FALSE;
	if (platform_window)
	{
		SDL_DestroyWindow(platform_window);
		platform_window = NULL;
	}
	return platform_video_initialize(0, 0);
}
#endif
#ifdef HALO_VULKAN
/* the Vulkan backend did not start (d3d8_vk.inl, vk_initialize): the window
again, with an OpenGL ES context, for the OpenGL renderer */
BOOL platform_video_reopen_for_gl(void)
{
	platform_log("renderer: Vulkan did not start, so OpenGL ES");
	platform_vulkan_refused = TRUE;
	xgpu_vulkan = FALSE;
	host_renderer_vulkan = 0;
	platform_offer_opengl_es();
	if (platform_window)
	{
		SDL_DestroyWindow(platform_window);
		platform_window = NULL;
	}
	return platform_video_initialize(0, 0);
}
#endif

/* display.mode, display.resolution (fullscreen's display mode),
display.window_size (when it changes: the window can be resized) and
display.vsync, as Settings has written them; display.resolution_scaling
and borderless's resolution are taken up between frames
(halo_screen_commit) */
void platform_display_apply(void)
{
#ifndef HALO_ANDROID
	int mode = platform_display_mode();
	long width, height;

	if (!platform_window)
		return;
	platform_window_left_space = FALSE;
	platform_fullscreen_kind_apply();
	/* Saving unrelated settings must preserve the window mode chosen by F11. */
	if (mode != platform_window_mode)
	{
		/* (tried again at the next apply when it could not be made) */
		platform_window_mode = platform_window_set_fullscreen(platform_fullscreen_setting()) ? mode : -1;
	}
	else if (platform_window_left_space && platform_fullscreen_requested)
	{
		/* (macOS: a change of resolution only took the window out of its
		full-screen Space, platform_window_space_settle; fullscreen again) */
		platform_window_set_fullscreen(TRUE);
	}
	platform_window_size_setting(&width, &height);
	if (width != platform_window_width || height != platform_window_height)
	{
		platform_window_width = width;
		platform_window_height = height;
		SDL_SetWindowSize(platform_window, (int)width, (int)height);
	}
#else
	if (!platform_window)
		return;
#endif
#if defined(__APPLE__) && !defined(HALO_IOS)
	{
		/* (the context's own interval, as at start-up: SDL's stays off) */
		void macos_set_swap_interval(int interval);

		macos_set_swap_interval(platform_vsync_on() ? 1 : 0);
	}
#else
	platform_swap_interval_apply(TRUE, platform_vsync_on(), config_integer("display.max_fps"));
#endif
}

void platform_video_drawable_size(int *width, int *height)
{
	SDL_GetWindowSizeInPixels(platform_window, width, height);
}

#ifdef HALO_IOS
/* the part of the window the picture goes in, as fractions of it (x, y,
width, height from the top left): port/ios/src/ios_touch.m */
void host_present_area(float *area);

void platform_video_present_area(int *x, int *y, int *width, int *height)
{
	int drawable_width = 0, drawable_height = 0;
	float area[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

	platform_video_drawable_size(&drawable_width, &drawable_height);
	*x = *y = 0;
	*width = drawable_width;
	*height = drawable_height;
	host_present_area(area);
	if (area[2] > 0.0f && area[3] > 0.0f && area[0] >= 0.0f && area[1] >= 0.0f &&
		area[0] + area[2] <= 1.0f && area[1] + area[3] <= 1.0f)
	{
		*x = (int)(area[0] * drawable_width);
		*y = (int)(area[1] * drawable_height);
		*width = (int)(area[2] * drawable_width);
		*height = (int)(area[3] * drawable_height);
	}
	if (*width <= 0 || *height <= 0)
	{
		*x = *y = 0;
		*width = drawable_width;
		*height = drawable_height;
	}
}

/* (SDL's view makes it with the context: xgpu.h) */
unsigned int platform_window_framebuffer(void)
{
	return (unsigned int)SDL_GetNumberProperty(SDL_GetWindowProperties(platform_window),
		SDL_PROP_WINDOW_UIKIT_OPENGL_FRAMEBUFFER_NUMBER, 0);
}
#endif

/* with vsync off, the time between frames display.max_fps asks for (0:
twice the display's refresh rate), or 0 for no limit. A GPU never left idle
can hang (Intel's Raptor Lake graphics, whose reset then takes the desktop
with it), and a phone drawing as fast as it can heats up and drains its
battery; the limit gives either a rest every frame. */
static Uint64 frame_interval_ns(void)
{
	static int vsync;
	static long maximum;
	static unsigned long read_at = (unsigned long)-1;
	static int vrr_applied = -1;
	float rate;
	int vrr = platform_vrr();

	if (read_at != config_changes())
	{
		read_at = config_changes();
		vsync = config_boolean("display.vsync");
		maximum = config_integer("display.max_fps");
	}
	/* (VRR turned on or off, by auto's system: the window's vsync again) */
	if (vrr_applied >= 0 && vrr != vrr_applied && platform_window)
	{
#if defined(__APPLE__) && !defined(HALO_IOS)
		void macos_set_swap_interval(int interval);

		if (platform_gl_context)
			macos_set_swap_interval(vsync || vrr ? 1 : 0);
#else
		platform_swap_interval_apply(TRUE, vsync || vrr, maximum);
#endif
	}
	vrr_applied = vrr;
	if (vrr)
	{
		/* (VRR's ceiling, or display.max_fps under it; vsync alone where
		the refresh rate is not known) */
		rate = vrr_frame_rate(platform_refresh_rate(), maximum);
		return rate > 0.0f ? (Uint64)(1e9f / rate) : 0;
	}
#ifdef SWAP_INTERVAL_IN_STEP
	/* (in step with the display, or no longer: the refresh rate known only
	once the window is shown, as under gamescope, the window moved to
	another display, the display's mode or display.max_fps changed; read
	from the caches above, not asked of SDL each frame) */
	if (!vsync)
	{
		platform_swap_interval_apply(FALSE, vsync, maximum);
		if (platform_interval > 0)
			return 0;
	}
#endif
	if (vsync || maximum < 0)
		return 0;
	rate = (float)maximum;
	if (!maximum)
	{
		/* (the Android guest's SDL has no display modes to ask: 60) */
		float refresh = platform_refresh_rate();

		rate = 2.0f * (refresh > 0.0f ? refresh : 60.0f);
	}
	return (Uint64)(1e9f / rate);
}

#ifndef HALO_ANDROID
#define frame_clock_ns SDL_GetTicksNS
#define frame_delay_ns SDL_DelayPrecise
#else
/* (the Android guest's SDL has neither SDL_GetTicksNS nor
SDL_DelayPrecise: the system's clock and sleep) */
static Uint64 frame_clock_ns(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (Uint64)now.tv_sec * 1000000000u + (Uint64)now.tv_nsec;
}

static void frame_delay_ns(Uint64 nanoseconds)
{
	struct timespec wait;

	wait.tv_sec = (time_t)(nanoseconds / 1000000000u);
	wait.tv_nsec = (long)(nanoseconds % 1000000000u);
	nanosleep(&wait, NULL);
}
#endif

void platform_video_swap(void)
{
	static Uint64 next_frame;
	Uint64 interval, now;
	Uint64 swap_start = SDL_GetTicks(), swap_time;

#ifdef HALO_VULKAN
	/* (the question after a fallback, once the window is up) */
	platform_vulkan_offer();
#endif
#ifdef _WIN32
	platform_borderless_styles();
#endif
#ifdef HALO_BGFX
	/* (the device presents: d3d8_bgfx.inl; this paces the frames) */
	if (!xgpu_bgfx)
#endif
#ifdef HALO_VULKAN
	/* (the Vulkan backend presents, and its swapchain paces the frames) */
	if (!xgpu_vulkan)
#endif
	{
		/* the window's framebuffer bound for reading too, as the frame is
		shown: a capture hooked into the swap (OBS's game capture) copies
		from whatever is bound for reading, and the render target the frame
		was blitted from holds it upside down (row 0 the top) */
		if (platform_gl_context)
		{
#ifdef HALO_IOS
			glBindFramebuffer(GL_READ_FRAMEBUFFER, platform_window_framebuffer());
#else
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
#endif
		}
		SDL_GL_SwapWindow(platform_window);
	}
	/* (a frame the graphics driver held for seconds: the game froze, and
	online, a host stops hearing a machine after a few; the log says where) */
	swap_time = SDL_GetTicks() - swap_start;
	if (swap_time >= 2000)
		platform_log("display: showing a frame took %.1f s in the graphics driver", (double)swap_time / 1000.0);
	interval = frame_interval_ns();
	if (!interval)
		return;
	now = frame_clock_ns();
	if (next_frame > now)
	{
		frame_delay_ns(next_frame - now);
		now = next_frame;
	}
	/* (a frame more than an interval late starts the count again) */
	next_frame = now - next_frame > interval ? now + interval : next_frame + interval;
}

/* The quit event comes while a frame is presented (D3DDevice_Present pumps
the events), and the game exited from there with its OpenGL context still
current. Crash reports from 0.9.0c (game issue #829) then crashed in the
graphics driver (or in OBS's hook of it) as the process went down, after
"window closed". Nothing is drawn after this: the context is released and
deleted and the window destroyed before the exit. bgfx's window is left to
the process's end, as its device still draws into it. */
void platform_exit_window_closed(void)
{
#if !defined(_WIN32) && !defined(HALO_ANDROID)
	/* (a quit under way: the watchdog leaves it to finish) */
	quit_watchdog_stand_down();
#endif
	platform_log("window closed");
	if (platform_gl_context)
	{
		SDL_GL_MakeCurrent(platform_window, NULL);
		SDL_GL_DestroyContext(platform_gl_context);
		platform_gl_context = NULL;
	}
#ifdef HALO_BGFX
	if (!xgpu_bgfx)
#endif
#ifdef HALO_VULKAN
	if (!xgpu_vulkan)
#endif
	if (platform_window)
	{
		SDL_DestroyWindow(platform_window);
		platform_window = NULL;
	}
	exit(EXIT_SUCCESS);
}

SDL_Window *platform_sdl_window(void)
{
	return platform_window;
}

#if !defined(HALO_ANDROID) && !defined(_WIN32) && !defined(__APPLE__)
/* port: X11 without XInput2 (a 32-bit libSDL3 whose 32-bit libXi.so.6 is
missing, as with Arch Linux's lib32-sdl3, falls back to plain X11 or
XWayland) has no relative mouse: SDL_SetWindowRelativeMouseMode fails. The
mouse is then held with a hidden cursor, a grab of the window (which also
lets XWayland lock the pointer) and a warp back to the window's middle after
it moves. */
#define MOUSE_WARP_FALLBACK
static BOOL mouse_warp_capture;
static int mouse_warp_x;
static int mouse_warp_y;

static void mouse_warp_to_middle(void)
{
	int width = 0, height = 0;

	SDL_GetWindowSize(platform_window, &width, &height);
	if (width > 0 && height > 0)
	{
		mouse_warp_x = width / 2;
		mouse_warp_y = height / 2;
		SDL_WarpMouseInWindow(platform_window, (float)mouse_warp_x, (float)mouse_warp_y);
	}
}

static void mouse_warp_release(void)
{
	if (mouse_warp_capture)
	{
		mouse_warp_capture = FALSE;
		SDL_SetWindowMouseGrab(platform_window, false);
		SDL_ShowCursor();
	}
}
#endif

void platform_mouse_capture(BOOL capture)
{
	if (!platform_window)
		return;
#ifndef MOUSE_WARP_FALLBACK
	SDL_SetWindowRelativeMouseMode(platform_window, capture ? true : false);
#else
	if (!capture)
	{
		SDL_SetWindowRelativeMouseMode(platform_window, false);
		mouse_warp_release();
		return;
	}
	if (SDL_GetKeyboardFocus() == platform_window &&
		SDL_SetWindowRelativeMouseMode(platform_window, true))
	{
		mouse_warp_release();
		return;
	}
	/* (before the window has the focus SDL sets its relative mode without
	testing for XInput2, and if XInput2 is missing when the focus comes, the
	plain motion is dropped until the mode is cleared: clear it, hold the
	mouse here, and try the relative mode again when the focus comes) */
	SDL_SetWindowRelativeMouseMode(platform_window, false);
	mouse_warp_capture = TRUE;
	SDL_HideCursor();
	SDL_SetWindowMouseGrab(platform_window, true);
	mouse_warp_to_middle();
#endif
}

/* ---------- keyboard translation */

/* Windows virtual key code for an SDL scancode (the Xbox debug keyboard
reports virtual keys) */
static BYTE virtual_key_from_scancode(SDL_Scancode scancode)
{
	if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
		return (BYTE)('A' + (scancode - SDL_SCANCODE_A));
	if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
		return (BYTE)('1' + (scancode - SDL_SCANCODE_1));
	if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12)
		return (BYTE)(0x70 + (scancode - SDL_SCANCODE_F1));
	if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_9)
		return (BYTE)(0x61 + (scancode - SDL_SCANCODE_KP_1));
	switch (scancode)
	{
	case SDL_SCANCODE_0: return '0';
	case SDL_SCANCODE_KP_0: return 0x60;
	case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return 0x0d;
	case SDL_SCANCODE_ESCAPE: return 0x1b;
	case SDL_SCANCODE_BACKSPACE: return 0x08;
	case SDL_SCANCODE_TAB: return 0x09;
	case SDL_SCANCODE_SPACE: return 0x20;
	case SDL_SCANCODE_MINUS: return 0xbd;
	case SDL_SCANCODE_EQUALS: return 0xbb;
	case SDL_SCANCODE_LEFTBRACKET: return 0xdb;
	case SDL_SCANCODE_RIGHTBRACKET: return 0xdd;
	case SDL_SCANCODE_BACKSLASH: return 0xdc;
	case SDL_SCANCODE_SEMICOLON: return 0xba;
	case SDL_SCANCODE_APOSTROPHE: return 0xde;
	case SDL_SCANCODE_GRAVE: return 0xc0;
	case SDL_SCANCODE_COMMA: return 0xbc;
	case SDL_SCANCODE_PERIOD: return 0xbe;
	case SDL_SCANCODE_SLASH: return 0xbf;
	case SDL_SCANCODE_CAPSLOCK: return 0x14;
	case SDL_SCANCODE_PRINTSCREEN: return 0x2c;
	case SDL_SCANCODE_SCROLLLOCK: return 0x91;
	case SDL_SCANCODE_PAUSE: return 0x13;
	case SDL_SCANCODE_INSERT: return 0x2d;
	case SDL_SCANCODE_HOME: return 0x24;
	case SDL_SCANCODE_PAGEUP: return 0x21;
	case SDL_SCANCODE_DELETE: return 0x2e;
	case SDL_SCANCODE_END: return 0x23;
	case SDL_SCANCODE_PAGEDOWN: return 0x22;
	case SDL_SCANCODE_RIGHT: return 0x27;
	case SDL_SCANCODE_LEFT: return 0x25;
	case SDL_SCANCODE_DOWN: return 0x28;
	case SDL_SCANCODE_UP: return 0x26;
	case SDL_SCANCODE_NUMLOCKCLEAR: return 0x90;
	case SDL_SCANCODE_KP_DIVIDE: return 0x6f;
	case SDL_SCANCODE_KP_MULTIPLY: return 0x6a;
	case SDL_SCANCODE_KP_MINUS: return 0x6d;
	case SDL_SCANCODE_KP_PLUS: return 0x6b;
	case SDL_SCANCODE_KP_PERIOD: return 0x6e;
	case SDL_SCANCODE_LCTRL: return 0xa2;
	case SDL_SCANCODE_RCTRL: return 0xa3;
	case SDL_SCANCODE_LSHIFT: return 0xa0;
	case SDL_SCANCODE_RSHIFT: return 0xa1;
	case SDL_SCANCODE_LALT: return 0xa4;
	case SDL_SCANCODE_RALT: return 0xa5;
	default: return 0;
	}
}

static CHAR ascii_from_key(SDL_Keycode key, SDL_Keymod modifiers)
{
	BOOL shift = (modifiers & SDL_KMOD_SHIFT) != 0;
	static const char shifted_digits[] = ")!@#$%^&*(";

	if (key >= 'a' && key <= 'z')
		return (CHAR)((shift ^ ((modifiers & SDL_KMOD_CAPS) != 0)) ? key - 32 : key);
	if (key >= '0' && key <= '9')
		return (CHAR)(shift ? shifted_digits[key - '0'] : key);
	if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
		return '\r';
	if (key == SDLK_BACKSPACE)
		return '\b';
	if (key == SDLK_TAB)
		return '\t';
	if (key == SDLK_ESCAPE)
		return 0x1b;
	if (key >= 32 && key < 127)
	{
		if (!shift)
			return (CHAR)key;
		switch (key)
		{
		case '-': return '_';
		case '=': return '+';
		case '[': return '{';
		case ']': return '}';
		case '\\': return '|';
		case ';': return ':';
		case '\'': return '"';
		case ',': return '<';
		case '.': return '>';
		case '/': return '?';
		case '`': return '~';
		default: return (CHAR)key;
		}
	}
	return 0;
}

static void queue_keystroke(const SDL_KeyboardEvent *event)
{
	struct platform_keystroke *keystroke;
	BYTE flags = 0;

	if (keystroke_count == KEYSTROKE_QUEUE_SIZE)
	{
		keystroke_head = (keystroke_head + 1) % KEYSTROKE_QUEUE_SIZE;
		keystroke_count--;
	}
	keystroke = &keystroke_queue[(keystroke_head + keystroke_count) % KEYSTROKE_QUEUE_SIZE];
	if (event->mod & SDL_KMOD_CTRL) flags |= 0x01;
	if (event->mod & SDL_KMOD_SHIFT) flags |= 0x02;
	if (event->mod & SDL_KMOD_ALT) flags |= 0x04;
	if (event->mod & SDL_KMOD_CAPS) flags |= 0x08;
	if (event->mod & SDL_KMOD_NUM) flags |= 0x10;
	if (!event->down) flags |= 0x40;
	if (event->repeat) flags |= 0x80;
	keystroke->virtual_key = virtual_key_from_scancode(event->scancode);
	keystroke->ascii = event->down ? ascii_from_key(event->key, event->mod) : 0;
	keystroke->flags = flags;
	keystroke_count++;
}

BOOL platform_next_keystroke(struct platform_keystroke *keystroke)
{
	BOOL result = FALSE;

	pthread_mutex_lock(&input_lock);
	if (keystroke_count)
	{
		*keystroke = keystroke_queue[keystroke_head];
		keystroke_head = (keystroke_head + 1) % KEYSTROKE_QUEUE_SIZE;
		keystroke_count--;
		result = TRUE;
	}
	pthread_mutex_unlock(&input_lock);
	return result;
}

/* ---------- internet play's invite links (p2p.c) */

#ifdef HALO_ANDROID
/* SDL declares it for Android builds only, which the guest is not
(guest/runtime/guest_sdl.c passes it to the host) */
bool SDL_ShowAndroidToast(const char *message, int duration, int gravity, int xoffset, int yoffset);
#endif

/* whether the text has an invite link in it (its prefix, in any case) */
static BOOL platform_text_has_invite_link(const char *text)
{
	/* (halo://join/, or the site's join page: p2p.h) */
	return p2p_invite_link_digits(text) != NULL;
}

/* the clipboard's text (the menus' text fields' Ctrl+V), and text put on it
(the server settings' invite link); 0 if there is none. The main thread's */
int platform_clipboard_get(char *text, int size)
{
	char *clipboard = SDL_GetClipboardText();
	int got = clipboard && *clipboard;

	snprintf(text, (size_t)size, "%s", got ? clipboard : "");
	SDL_free(clipboard);
	return got;
}

void platform_clipboard_set(const char *text)
{
	SDL_SetClipboardText(text);
}

/* network.join_from_clipboard: "never" (the default), "ask" or "always"
(true and false, of older versions, are "always" and "never":
port_config.c) */
enum
{
	_clipboard_join_never,
	_clipboard_join_ask,
	_clipboard_join_always,
};

static int platform_clipboard_join_setting(void)
{
	const char *setting = config_string("network.join_from_clipboard");

	/* (in lowercase, true and false read as these: config_check_words) */
	if (!strcmp(setting, "always"))
		return _clipboard_join_always;
	if (!strcmp(setting, "ask"))
		return _clipboard_join_ask;
	return _clipboard_join_never;
}

/* an invite found on the clipboard, waiting for the player's answer (the
game's thread takes it: platform_take_clipboard_invite) */
static pthread_mutex_t platform_clipboard_invite_lock = PTHREAD_MUTEX_INITIALIZER;
static char platform_clipboard_invite[256];

int platform_take_clipboard_invite(char *text, int size)
{
	int taken;

	pthread_mutex_lock(&platform_clipboard_invite_lock);
	taken = platform_clipboard_invite[0] != 0;
	if (taken)
		snprintf(text, (size_t)size, "%s", platform_clipboard_invite);
	platform_clipboard_invite[0] = 0;
	pthread_mutex_unlock(&platform_clipboard_invite_lock);
	return taken;
}

/* puts a new invite on the clipboard (network.copy_to_clipboard), and joins
one found there when the game comes to the front, or asks first
(network.join_from_clipboard); both off by default, so the clipboard is the
player's own until a setting says otherwise */
static void platform_invite_clipboard(BOOL look)
{
	/* the last clipboard text looked at, so each invite is joined once */
	static char seen[256];
	const char *invite = p2p_take_clipboard_text();
	int setting;

#ifdef HALO_GAME_BROWSER
	/* (the dedicated server and a probe leave the clipboard alone) */
	if (browser_headless())
		return;
#endif
	if (invite)
	{
		char shared[256];

		/* (the game's own invite is never joined, copied or not; it goes as
		the site's join page, which chat programs make a link of) */
		if (!p2p_invite_share_link(invite, shared, sizeof(shared)))
			snprintf(shared, sizeof(shared), "%s", invite);
		snprintf(seen, sizeof(seen), "%s", shared);
		if (config_boolean("network.copy_to_clipboard"))
		{
			SDL_SetClipboardText(shared);
			platform_log("Internet play: the invite link is on the clipboard");
#ifdef HALO_ANDROID
			SDL_ShowAndroidToast("Invite link copied", 1, -1, 0, 0);
#endif
		}
	}
	setting = look ? platform_clipboard_join_setting() : _clipboard_join_never;
	if (setting != _clipboard_join_never)
	{
		char *text = SDL_GetClipboardText();

		if (text && strcmp(text, seen) && strlen(text) < sizeof(seen))
		{
			snprintf(seen, sizeof(seen), "%s", text);
			/* (a link, not a bare code: 64 hex digits alone are as often a
			checksum copied for something else) */
			if (platform_text_has_invite_link(text))
			{
#ifdef HALO_GAME_BROWSER
				/* (asked in the game's own style, over its menus:
				port/linux/game/invite_prompt.c) */
				if (setting == _clipboard_join_ask)
				{
					pthread_mutex_lock(&platform_clipboard_invite_lock);
					snprintf(platform_clipboard_invite, sizeof(platform_clipboard_invite), "%s", text);
					pthread_mutex_unlock(&platform_clipboard_invite_lock);
					platform_log("Internet play: an invite link is on the clipboard; asking whether to join it");
				}
				else
#endif
				if (p2p_join_invite(text))
				{
#ifdef HALO_ANDROID
					SDL_ShowAndroidToast("Joining the invite", 1, -1, 0, 0);
#endif
				}
			}
		}
		SDL_free(text);
	}
}

/* ---------- messages for the player */

/* a message waiting for the event pump to show it (on the window's thread,
between frames) */
static pthread_mutex_t platform_message_lock = PTHREAD_MUTEX_INITIALIZER;
static char platform_message_title[80];
static char platform_message_text[600];
static BOOL platform_message_pending;

/* shows the player a message in a box of its own (the network code's: a
host of another version), and logs it */
void platform_show_message(const char *title, const char *message)
{
	platform_log("%s: %s", title, message);
	/* (a run nobody watches: the log only) */
	if (config_boolean("debug.hidden_window") || config_boolean("debug.null_renderer"))
		return;
#ifdef HALO_GAME_BROWSER
	if (browser_headless())
		return;
#endif
	pthread_mutex_lock(&platform_message_lock);
	snprintf(platform_message_title, sizeof(platform_message_title), "%s", title);
	snprintf(platform_message_text, sizeof(platform_message_text), "%s", message);
	platform_message_pending = TRUE;
	pthread_mutex_unlock(&platform_message_lock);
}

#ifdef HALO_GAME_BROWSER
static pthread_mutex_t platform_url_lock = PTHREAD_MUTEX_INITIALIZER;
static char platform_url[700];

void platform_open_url(const char *url)
{
	pthread_mutex_lock(&platform_url_lock);
	snprintf(platform_url, sizeof(platform_url), "%s", url);
	pthread_mutex_unlock(&platform_url_lock);
}

/* (the main thread's: a page to open, a restored key to ask about) */
static void platform_game_list_requests(void)
{
	char url[sizeof(platform_url)];
	char new_id[64], old_id[64], message[512];

	pthread_mutex_lock(&platform_url_lock);
	snprintf(url, sizeof(url), "%s", platform_url);
	platform_url[0] = 0;
	pthread_mutex_unlock(&platform_url_lock);
	if (url[0] && !SDL_OpenURL(url))
		platform_log("Game list: could not open the web browser: %s", SDL_GetError());

	if (browser_take_key_link(new_id, old_id, sizeof(new_id)))
	{
		static const SDL_MessageBoxButtonData buttons[] =
		{
			{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Keep mine" },
			{ 0, 1, "Replace" },
		};
		SDL_MessageBoxData question;
		int answer = 0;

		if (!strcmp(new_id, old_id))
		{
			platform_log("Game list: that player key is already this copy's");
			browser_answer_key_link(0);
			return;
		}
		snprintf(message, sizeof(message),
			"A link asks to replace your player key.\n\n"
			"Current: player %.8s. New: player %.8s.\n\n"
			"Replace it only with your own key, from your profile on the game list.",
			old_id[0] ? old_id : "(none)", new_id);
		/* (no question under gamescope: the key stays) */
		if (updater_gamescope())
		{
			platform_log("Game list: a link asks to replace the player key; not asking under gamescope "
				"(Steam Deck Game Mode): open it from Desktop Mode");
			browser_answer_key_link(0);
			return;
		}
		memset(&question, 0, sizeof(question));
		question.flags = SDL_MESSAGEBOX_WARNING;
		question.window = platform_window;
		question.title = "Chupathingy: replace your player key?";
		question.message = message;
		question.numbuttons = 2;
		question.buttons = buttons;
		if (!SDL_ShowMessageBox(&question, &answer))
			answer = 0;
		browser_answer_key_link(answer == 1);
	}
}
#endif

static void platform_show_pending_message(void)
{
	char title[sizeof(platform_message_title)];
	char text[sizeof(platform_message_text)];
	BOOL pending;

	pthread_mutex_lock(&platform_message_lock);
	pending = platform_message_pending;
	platform_message_pending = FALSE;
	memcpy(title, platform_message_title, sizeof(title));
	memcpy(text, platform_message_text, sizeof(text));
	pthread_mutex_unlock(&platform_message_lock);
	/* (under gamescope, the log only: platform_show_message logged it) */
	if (!pending || updater_gamescope())
		return;
#ifdef HALO_ANDROID
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, title, text, NULL);
#else
	{
		/* (a box cannot show above a fullscreen game) */
		BOOL fullscreen = platform_window_fullscreen();

		if (fullscreen)
			platform_window_set_fullscreen(FALSE);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, title, text, platform_window);
		if (fullscreen)
			platform_window_set_fullscreen(TRUE);
	}
#endif
}

/* a Vulkan start that did not get going (d3d8_bgfx.inl's bg_start): that
start draws with OpenGL, and once the game's window is up the player is
asked whether to stay on OpenGL for good. display.renderer changes only on a
yes; a no leaves it, so the next start tries Vulkan again and asks again if
it fails. A run nobody can answer (platform_run_is_quiet) logs it only */
static BOOL platform_opengl_offer_pending;

void platform_offer_opengl(void)
{
	pthread_mutex_lock(&platform_message_lock);
	platform_opengl_offer_pending = TRUE;
	pthread_mutex_unlock(&platform_message_lock);
}

static void platform_opengl_offer(void)
{
	static const SDL_MessageBoxButtonData buttons[] = {
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Switch to OpenGL" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Not now" },
	};
	SDL_MessageBoxData question = { SDL_MESSAGEBOX_INFORMATION, NULL, "Chupathingy: renderer",
		"Vulkan didn't get going last time, so the game is using OpenGL for now.\n\n"
		"Switch to OpenGL for good? You can change it later on the RENDERER row of the display settings.",
		2, buttons, NULL };
	BOOL pending;
	int answer = 0;
	pthread_mutex_lock(&platform_message_lock);
	pending = platform_opengl_offer_pending;
	platform_opengl_offer_pending = FALSE;
	pthread_mutex_unlock(&platform_message_lock);
	if (!pending)
		return;
	if (platform_run_is_quiet())
	{
		platform_log("bgfx: OpenGL for this start only; display.renderer stays as it is (no one to ask)");
		return;
	}
#ifdef HALO_ANDROID
	if (!SDL_ShowMessageBox(&question, &answer))
		answer = 0;
#else
	{
		/* (a box cannot show above a fullscreen game) */
		BOOL fullscreen = platform_window_fullscreen();

		question.window = platform_window;
		if (fullscreen)
			platform_window_set_fullscreen(FALSE);
		if (!SDL_ShowMessageBox(&question, &answer))
			answer = 0;
		if (fullscreen)
			platform_window_set_fullscreen(TRUE);
	}
#endif
	if (answer == 1)
	{
		config_write("display.renderer", "gl");
		platform_log("bgfx: the player switched to OpenGL for good (display.renderer = \"gl\")");
	}
	else
		platform_log("bgfx: the player kept their renderer setting; Vulkan is tried again next start");
}

/* ---------- events */

/* quits as closing the window does, when the events are next read (the
menus' Quit: port/linux/game/menu_functions.c). Android's build has no
SDL_PushEvent (port/android/host_imports.list), so there the next read of
the events quits; the game's process ends, and Android starts it from its
launcher the next time */
#ifdef HALO_ANDROID
static BOOL android_quit_requested;
#endif
#ifdef __APPLE__
/* the menus asked to quit (platform_request_quit), not Command-Q */
static BOOL quit_requested;

/* the game's console (source/interface/terminal.c) */
void terminal_printf(const void *color, const char *format, ...);

/* a line in the game's console and the log */
static void platform_quit_notice(const char *text)
{
	platform_log("%s", text);
	terminal_printf(NULL, "%s", text);
}
#endif

void platform_request_quit(void)
{
#ifdef HALO_ANDROID
	android_quit_requested = TRUE;
#else
	SDL_Event event;

#ifdef __APPLE__
	quit_requested = TRUE;
#endif
	memset(&event, 0, sizeof(event));
	event.type = SDL_EVENT_QUIT;
	SDL_PushEvent(&event);
#endif
}

void platform_scoreboard_scroll(int open, long *notches, long *pages)
{
	Uint64 now = SDL_GetTicks();

	pthread_mutex_lock(&input_lock);
	if (!open || now >= scoreboard_open_until_ms)
	{
		scoreboard_wheel = 0.0f;
		scoreboard_notches = 0;
		scoreboard_pages = 0;
	}
	scoreboard_open_until_ms = open ? now + SCOREBOARD_OPEN_MS : 0;
	if (notches)
		*notches = scoreboard_notches;
	if (pages)
		*pages = scoreboard_pages;
	scoreboard_notches = 0;
	scoreboard_pages = 0;
	pthread_mutex_unlock(&input_lock);
}

#ifdef HALO_CONSOLE_PERFORMANCE_MODE
/* the port's: performance mode's passes left out, and its switch read
(halo_console_features.h) */
void console_performance_update(void);
#endif

void platform_pump_events(void)
{
	/* debug.exit_after (seconds) ends the game that long after the window
	opens, as closing it does (tools/pgo_train.py) */
	static Uint64 exit_ticks = (Uint64)-1;
	SDL_Event event;
	static BOOL looked_at_clipboard;
	BOOL look_at_clipboard = !looked_at_clipboard;
#ifdef MOUSE_WARP_FALLBACK
	BOOL mouse_needs_warp = FALSE;
#endif

#ifdef HALO_CONSOLE_PERFORMANCE_MODE
	console_performance_update();
#endif

#ifdef HALO_GAME_BROWSER
	/* the dedicated server has no window, but stops as asked (SIGTERM or
	SIGINT: SDL's quit event), as a service is stopped */
	if (!platform_window && browser_headless())
	{
		SDL_PumpEvents();
		if (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_QUIT, SDL_EVENT_QUIT) > 0)
		{
			platform_log("dedicated server: stopping");
			exit(EXIT_SUCCESS);
		}
		return;
	}
#endif
	if (!platform_window || SDL_GetCurrentThreadID() != platform_event_thread)
		return;
#if !defined(_WIN32) && !defined(HALO_ANDROID)
	/* (the game isn't stuck: quit_watchdog) */
	quit_watchdog_beat();
#endif
	if (exit_ticks == (Uint64)-1)
	{
		double seconds = config_real("debug.exit_after");

		exit_ticks = seconds > 0.0 ? SDL_GetTicks() + (Uint64)(seconds * 1000.0) : 0;
	}
	if (exit_ticks && SDL_GetTicks() >= exit_ticks)
	{
		platform_log("exiting after debug.exit_after");
		exit(EXIT_SUCCESS);
	}
#ifdef HALO_ANDROID
	if (android_quit_requested)
	{
		platform_log("quit from the menus");
		exit(EXIT_SUCCESS);
	}
#endif
	platform_show_pending_message();
	platform_opengl_offer();
#ifdef HALO_GAME_BROWSER
	platform_game_list_requests();
#endif
#ifndef HALO_ANDROID
	updater_poll(platform_window);
#endif
	pthread_mutex_lock(&input_lock);
	gamepad_pumps++;
	while (SDL_PollEvent(&event))
	{
#ifndef HALO_ANDROID
		/* (the refresh rate read again: frame_interval_ns) */
		if (event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
			event.type == SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED ||
			event.type == SDL_EVENT_DISPLAY_ADDED || event.type == SDL_EVENT_DISPLAY_REMOVED)
		{
			platform_refresh_rate_forget();
		}
#endif
		switch (event.type)
		{
#ifdef __APPLE__
		/* the window's close button quits at once; Command-W (the Window
		menu's Close, while W moves the player forward) does nothing */
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			if (SDL_GetModState() & SDL_KMOD_GUI)
			{
				platform_quit_notice("Press Command-Q twice to quit");
				break;
			}
			pthread_mutex_unlock(&input_lock);
			platform_exit_window_closed();
#endif
		case SDL_EVENT_QUIT:
#ifdef __APPLE__
			/* Command-Q quits on a second press within two seconds (Q is the
			flashlight); the menus' Quit, the Dock's and logging out quit at
			once */
			if (!quit_requested && (SDL_GetModState() & SDL_KMOD_GUI))
			{
				static Uint64 first_quit;
				Uint64 now = SDL_GetTicks();

				if (!first_quit || now - first_quit > 2000)
				{
					first_quit = now;
					platform_quit_notice("press Command-Q again to quit");
					break;
				}
			}
#endif
			pthread_mutex_unlock(&input_lock);
			platform_exit_window_closed();
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
			if (event.key.scancode < SDL_SCANCODE_COUNT)
			{
				input_state.keys[event.key.scancode] = event.key.down;
				if (event.key.down)
					keys_pressed[event.key.scancode] = 1;
			}
			if (event.key.down && !event.key.repeat &&
				(event.key.scancode < SDL_SCANCODE_LCTRL || event.key.scancode > SDL_SCANCODE_RGUI))
			{
				movie_skip_presses++;
			}
			if (binding_capture == _binding_capture_waiting && event.key.down && !event.key.repeat &&
				event.key.scancode != SDL_SCANCODE_F11 && event.key.scancode != SDL_SCANCODE_F12)
			{
				binding_capture = _binding_capture_taken;
				binding_taken_ms = SDL_GetTicks();
				binding_capture_result = event.key.scancode == SDL_SCANCODE_ESCAPE ? 3 :
					event.key.scancode == SDL_SCANCODE_DELETE ? 2 : 1;
				binding_captured_input = event.key.scancode;
				break;
			}
			queue_keystroke(&event.key);
			if (SDL_GetTicks() < scoreboard_open_until_ms && event.key.down &&
				(event.key.scancode == SDL_SCANCODE_PAGEUP || event.key.scancode == SDL_SCANCODE_PAGEDOWN))
			{
				scoreboard_pages += event.key.scancode == SDL_SCANCODE_PAGEDOWN ? 1 : -1;
			}
			/* F12 releases or recaptures the mouse */
			if (event.key.down && !event.key.repeat && event.key.scancode == SDL_SCANCODE_F12)
			{
				input_state.mouse_released = !input_state.mouse_released;
				platform_mouse_capture(!input_state.mouse_released && !input_state.ui_pointer);
				/* (the pointer shows while released, hidden again in play) */
				show_pointer(input_state.mouse_released || input_state.ui_pointer);
			}
#ifndef HALO_ANDROID
			/* F11 switches between fullscreen and the window (SDL keeps the
			window's size and place while fullscreen) */
			if (event.key.down && !event.key.repeat && event.key.scancode == SDL_SCANCODE_F11)
			{
				platform_window_set_fullscreen(!platform_window_fullscreen());
			}
#endif
			break;
		case SDL_EVENT_MOUSE_MOTION:
#ifndef HALO_ANDROID
			/* in the menus the mouse moves the pointer, not the view */
			if (input_state.ui_pointer)
			{
				BOOL warp = ui_pointer_warp_pending && event.motion.x > ui_pointer_warp_x - 1.0f &&
					event.motion.x < ui_pointer_warp_x + 1.0f && event.motion.y > ui_pointer_warp_y - 1.0f &&
					event.motion.y < ui_pointer_warp_y + 1.0f;

				ui_pointer_warp_pending = FALSE;
				ui_pointer.x = event.motion.x;
				ui_pointer.y = event.motion.y;
				if (!warp)
					ui_pointer.moved = TRUE;
				break;
			}
#endif
#ifdef MOUSE_WARP_FALLBACK
			/* port: the mouse held without XInput2: the move the warp back to
			the middle makes is not the player's */
			if (mouse_warp_capture)
			{
				if ((int)event.motion.x == mouse_warp_x && (int)event.motion.y == mouse_warp_y)
					break;
				mouse_needs_warp = TRUE;
			}
#endif
			input_state.mouse_dx += event.motion.xrel;
			input_state.mouse_dy += event.motion.yrel;
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			if (event.button.down)
				movie_skip_presses++;
			if (event.button.button < 32)
			{
				if (event.button.down)
					mouse_buttons_down |= 1u << event.button.button;
				else
					mouse_buttons_down &= ~(1u << event.button.button);
			}
			if (binding_capture == _binding_capture_waiting && event.button.down &&
				event.button.button < PLATFORM_MOUSE_BUTTON_COUNT)
			{
				binding_capture = _binding_capture_taken;
				binding_taken_ms = SDL_GetTicks();
				binding_capture_result = 1;
				binding_captured_input = INPUT_MOUSE + event.button.button;
				break;
			}
#ifndef HALO_ANDROID
			/* clicks in the menus go to the pointer; a button held down
			when the menu closes stays up until pressed again, so the click
			that resumes the game does not also fire */
			if (input_state.ui_pointer)
			{
				if (event.button.down && event.button.button == SDL_BUTTON_LEFT)
				{
					ui_pointer.left_clicks++;
					ui_pointer.click_x = event.button.x;
					ui_pointer.click_y = event.button.y;
				}
				else if (event.button.down && event.button.button == SDL_BUTTON_RIGHT)
				{
					ui_pointer.right_clicks++;
				}
				break;
			}
#endif
			if (event.button.button < PLATFORM_MOUSE_BUTTON_COUNT)
			{
				input_state.mouse_buttons[event.button.button] = event.button.down;
				if (event.button.down)
					mouse_buttons_pressed[event.button.button] = 1;
			}
#ifdef MOUSE_WARP_FALLBACK
			/* port: a click in play with the mouse held neither way (the
			relative mode did not take) holds it again */
			if (event.button.down && !input_state.mouse_released &&
				!input_state.ui_pointer && !mouse_warp_capture &&
				!SDL_GetWindowRelativeMouseMode(platform_window))
			{
				platform_mouse_capture(TRUE);
			}
#endif
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			if (binding_capture == _binding_capture_waiting && event.wheel.y != 0.0f)
			{
				binding_capture = _binding_capture_taken;
				binding_taken_ms = SDL_GetTicks();
				binding_capture_result = 1;
				binding_captured_input = event.wheel.y > 0.0f ? INPUT_WHEEL_UP : INPUT_WHEEL_DOWN;
				break;
			}
			if (SDL_GetTicks() < scoreboard_open_until_ms)
			{
				/* whole notches, up (away) scrolling up */
				scoreboard_wheel -= event.wheel.y;
				while (scoreboard_wheel >= 1.0f)
				{
					scoreboard_notches++;
					scoreboard_wheel -= 1.0f;
				}
				while (scoreboard_wheel <= -1.0f)
				{
					scoreboard_notches--;
					scoreboard_wheel += 1.0f;
				}
				break;
			}
#ifndef HALO_ANDROID
			if (input_state.ui_pointer)
			{
				/* whole notches: smooth-scrolling wheels send fractions */
				ui_pointer_wheel += event.wheel.y;
				while (ui_pointer_wheel >= 1.0f)
				{
					ui_pointer.wheel_steps++;
					ui_pointer_wheel -= 1.0f;
				}
				while (ui_pointer_wheel <= -1.0f)
				{
					ui_pointer.wheel_steps--;
					ui_pointer_wheel += 1.0f;
				}
				break;
			}
#endif
			input_state.mouse_wheel += event.wheel.y;
			break;
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			memset(input_state.keys, 0, sizeof(input_state.keys));
			memset(input_state.mouse_buttons, 0, sizeof(input_state.mouse_buttons));
			memset(mouse_buttons_pressed, 0, sizeof(mouse_buttons_pressed));
			gamepad_presses_clear();
			input_state.focused = FALSE;
#ifdef MOUSE_WARP_FALLBACK
			/* port: the held mouse let go while another window has the focus */
			mouse_warp_release();
#endif
#ifdef HALO_ANDROID
			touch_input_cancel();
#endif
			break;
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			input_state.focused = TRUE;
			look_at_clipboard = TRUE;
#ifndef HALO_ANDROID
			if (!input_state.mouse_released && !input_state.ui_pointer)
				platform_mouse_capture(TRUE);
#endif
			break;
#ifdef HALO_ANDROID
		case SDL_EVENT_FINGER_DOWN:
		case SDL_EVENT_FINGER_MOTION:
		case SDL_EVENT_FINGER_UP:
		case SDL_EVENT_FINGER_CANCELED:
			if (event.type == SDL_EVENT_FINGER_DOWN)
				movie_skip_presses++;
			touch_input_event(event.type, &event.tfinger);
			break;
#endif
		/* (a tap counted until the controller's next reading) */
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			gamepad_button_pressed(event.gbutton.which, event.gbutton.button);
			break;
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
			gamepad_trigger_moved(event.gaxis.which, event.gaxis.axis, event.gaxis.value);
			break;
		case SDL_EVENT_GAMEPAD_ADDED:
#if defined(HALO_ANDROID) && !defined(HALO_IOS)
			/* (the guest reaches SDL only through host_imports.list, which
			has no SDL_GetGamepadName) */
			SDL_OpenGamepad(event.gdevice.which);
#else
			{
				SDL_Gamepad *gamepad = SDL_OpenGamepad(event.gdevice.which);

				/* (which pads the game drives: under Steam Input, Steam's
				virtual ones, named for the controllers behind them) */
				if (gamepad)
				{
					const char *name = SDL_GetGamepadName(gamepad);

					platform_log("gamepad: %s", name ? name : "(unnamed)");
				}
			}
#endif
			break;
#if !defined(HALO_ANDROID) || defined(HALO_IOS)
		case SDL_EVENT_GAMEPAD_REMOVED:
			/* (SDL keeps a gamepad open until it is closed, even once the
			controller has gone: each one plugged in again was opened anew
			and the old one never let go. Android's guest has no
			SDL_CloseGamepad in host_imports.list) */
			{
				SDL_Gamepad *gamepad = SDL_GetGamepadFromID(event.gdevice.which);

				gamepad_presses_forget(event.gdevice.which);
				if (gamepad)
					SDL_CloseGamepad(gamepad);
			}
			break;
#endif
#ifdef __APPLE__
		case SDL_EVENT_DROP_FILE:
		case SDL_EVENT_DROP_TEXT:
			/* macOS hands an opened halo:// link (an invite) to the running
			application, which SDL reports as a dropped file; elsewhere it
			arrives on the command line (p2p_hand_off_invite) */
			if (event.drop.data && !strncmp(event.drop.data, "halo://", 7))
			{
				/* (a player key's link is a secret: never in the log) */
				if (!SDL_strncasecmp(event.drop.data, "halo://key/", 11))
					platform_log("Internet play: opened a player key link");
				else
				{
					char shown[128];

					platform_log("Internet play: opened %s",
						p2p_invite_log_text(event.drop.data, shown, sizeof(shown)));
				}
				p2p_link_opened(event.drop.data, 0);
			}
			break;
#endif
		default:
			break;
		}
	}
#ifdef MOUSE_WARP_FALLBACK
	if (mouse_warp_capture && mouse_needs_warp && !input_state.ui_pointer &&
		!input_state.mouse_released)
	{
		mouse_warp_to_middle();
	}
#endif
	pthread_mutex_unlock(&input_lock);
	looked_at_clipboard = TRUE;
	platform_invite_clipboard(look_at_clipboard);
}

void platform_menus_set_active(BOOL active)
{
	pthread_mutex_lock(&input_lock);
	input_state.menus = active;
	pthread_mutex_unlock(&input_lock);
}

void platform_binding_capture_begin(void)
{
	pthread_mutex_lock(&input_lock);
	binding_capture = _binding_capture_waiting;
	binding_settling = TRUE;
	binding_polled_ms = SDL_GetTicks();
	pthread_mutex_unlock(&input_lock);
}

int platform_binding_capture_poll(int *input)
{
	int result = 0;

	pthread_mutex_lock(&input_lock);
	binding_polled_ms = SDL_GetTicks();
	if (binding_capture == _binding_capture_taken)
	{
		result = binding_capture_result;
		*input = binding_captured_input;
		binding_capture = _binding_capture_idle;
	}
	pthread_mutex_unlock(&input_lock);
	return result;
}

#ifndef HALO_ANDROID
/* ---------- the menus' pointer */

/* While a menu is up the mouse is released, its pointer shows (centered when
the menu opens) and its motion, clicks and wheel go to the menus
(halo_ui_pointer_update, d3d8_gl.c) instead of the controller and the aim. */
void platform_ui_pointer_set_active(BOOL active)
{
	if (!platform_window || (active != FALSE) == (input_state.ui_pointer != FALSE))
		return;
	pthread_mutex_lock(&input_lock);
	input_state.ui_pointer = active;
	memset(&ui_pointer, 0, sizeof(ui_pointer));
	ui_pointer_wheel = 0.0f;
	input_state.mouse_dx = 0.0f;
	input_state.mouse_dy = 0.0f;
	input_state.mouse_wheel = 0.0f;
	memset(input_state.mouse_buttons, 0, sizeof(input_state.mouse_buttons));
	memset(mouse_buttons_pressed, 0, sizeof(mouse_buttons_pressed));
	pthread_mutex_unlock(&input_lock);
	platform_mouse_capture(!active && !input_state.mouse_released);
	if (active)
	{
		int width, height;

		SDL_GetWindowSize(platform_window, &width, &height);
		pthread_mutex_lock(&input_lock);
		ui_pointer_warp_pending = TRUE;
		ui_pointer_warp_x = width * 0.5f;
		ui_pointer_warp_y = height * 0.5f;
		pthread_mutex_unlock(&input_lock);
		SDL_WarpMouseInWindow(platform_window, width * 0.5f, height * 0.5f);
		show_pointer(TRUE);
		pthread_mutex_lock(&input_lock);
		ui_pointer.x = width * 0.5f;
		ui_pointer.y = height * 0.5f;
		pthread_mutex_unlock(&input_lock);
	}
	else
	{
		show_pointer(FALSE);
	}
}

/* debug.test_input's mouse (xinput_sdl.c): the event a mouse would send,
at a point of the window; button 0 moves, SDL_BUTTON_LEFT or _RIGHT clicks
(down and up), and wheel turns the wheel that many notches instead */
void platform_test_pointer_event(float x, float y, int button, int wheel)
{
	SDL_Event event;
	SDL_WindowID window = platform_window ? SDL_GetWindowID(platform_window) : 0;

	memset(&event, 0, sizeof(event));
	if (wheel)
	{
		event.type = SDL_EVENT_MOUSE_WHEEL;
		event.wheel.windowID = window;
		event.wheel.y = (float)wheel;
		event.wheel.integer_y = wheel;
		event.wheel.mouse_x = x;
		event.wheel.mouse_y = y;
		SDL_PushEvent(&event);
		return;
	}
	if (!button)
	{
		event.type = SDL_EVENT_MOUSE_MOTION;
		event.motion.windowID = window;
		event.motion.x = x;
		event.motion.y = y;
		SDL_PushEvent(&event);
		return;
	}
	event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	event.button.windowID = window;
	event.button.button = (Uint8)button;
	event.button.down = TRUE;
	event.button.clicks = 1;
	event.button.x = x;
	event.button.y = y;
	SDL_PushEvent(&event);
	event.type = SDL_EVENT_MOUSE_BUTTON_UP;
	event.button.down = FALSE;
	SDL_PushEvent(&event);
}

/* what the pointer did since the last call; FALSE when it is not active */
BOOL platform_ui_pointer_read(struct platform_ui_pointer *pointer)
{
	BOOL active;

	pthread_mutex_lock(&input_lock);
	active = input_state.ui_pointer;
	*pointer = ui_pointer;
	ui_pointer.moved = FALSE;
	ui_pointer.left_clicks = 0;
	ui_pointer.right_clicks = 0;
	ui_pointer.wheel_steps = 0;
	pthread_mutex_unlock(&input_lock);
	return active;
}

void platform_video_window_size(int *width, int *height)
{
	SDL_GetWindowSize(platform_window, width, height);
}

#else
/* ---------- the menus' pointer (the touchscreen)

While a menu is up, taps and drags go to the menus (touch_input.c,
halo_ui_pointer_update in d3d8_gl.c). */
void platform_ui_pointer_set_active(BOOL active)
{
	pthread_mutex_lock(&input_lock);
	if ((active != FALSE) != (input_state.ui_pointer != FALSE))
	{
		input_state.ui_pointer = active;
		touch_input_menu_set_active(active != FALSE);
	}
	pthread_mutex_unlock(&input_lock);
}

BOOL platform_ui_pointer_read(struct platform_ui_pointer *pointer)
{
	BOOL active;

	pthread_mutex_lock(&input_lock);
	active = input_state.ui_pointer;
	touch_input_menu_read(pointer);
	pthread_mutex_unlock(&input_lock);
	return active;
}

/* the window is its pixels on Android (no display scaling): touch_input.c
scales the fingers' 0..1 by the drawable's size */
void platform_video_window_size(int *width, int *height)
{
	platform_video_drawable_size(width, height);
}

#endif
void platform_input_read(struct platform_input_state *state, BOOL consume_motion)
{
	pthread_mutex_lock(&input_lock);
	*state = input_state;
	/* (rebinding: nothing reaches the controller until the input is taken
	and every key and button is up again) */
	if (binding_capture != _binding_capture_idle || binding_settling)
	{
		int scancode;
		BOOL held = mouse_buttons_down != 0;

		for (scancode = 0; scancode < SDL_SCANCODE_COUNT && !held; scancode++)
			held = input_state.keys[scancode] != 0;
		if (binding_capture == _binding_capture_taken && SDL_GetTicks() - binding_taken_ms > BINDING_UNCLAIMED_MS)
			binding_capture = _binding_capture_idle;
		if (binding_capture == _binding_capture_waiting && SDL_GetTicks() - binding_polled_ms > BINDING_ABANDONED_MS)
			binding_capture = _binding_capture_idle;
		if (binding_capture == _binding_capture_idle && !held)
			binding_settling = FALSE;
		memset(state->keys, 0, sizeof(state->keys));
		memset(state->mouse_buttons, 0, sizeof(state->mouse_buttons));
		memset(keys_pressed, 0, sizeof(keys_pressed));
		memset(mouse_buttons_pressed, 0, sizeof(mouse_buttons_pressed));
		state->mouse_wheel = 0.0f;
		/* (and the motion of the while, which would otherwise pile up for
		the aim) */
		state->mouse_dx = state->mouse_dy = 0.0f;
		if (consume_motion)
		{
			input_state.mouse_dx = input_state.mouse_dy = 0.0f;
			input_state.mouse_wheel = 0.0f;
		}
		pthread_mutex_unlock(&input_lock);
		return;
	}
	if (consume_motion)
	{
		int scancode;

		for (scancode = 0; scancode < SDL_SCANCODE_COUNT; scancode++)
		{
			state->keys[scancode] |= keys_pressed[scancode];
			keys_pressed[scancode] = 0;
		}
		for (scancode = 0; scancode < PLATFORM_MOUSE_BUTTON_COUNT; scancode++)
		{
			state->mouse_buttons[scancode] |= mouse_buttons_pressed[scancode];
			mouse_buttons_pressed[scancode] = 0;
		}
	}
	if (consume_motion)
	{
		input_state.mouse_dx = 0;
		input_state.mouse_dy = 0;
		input_state.mouse_wheel = 0;
	}
	pthread_mutex_unlock(&input_lock);
}

int platform_movie_skip_pressed(void)
{
	int pressed = movie_skip_presses != 0;

	movie_skip_presses = 0;
	return pressed;
}
