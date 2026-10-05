/*
SERVER_PLATFORM.C

The dedicated server's platform layer (server/README.md), in place of
sdl_platform.c: the same functions, for a program that has no window, no
display, no sound device and no player at the machine. The server build
(tools/server_build.py) links this instead of SDL.

What the game asks of a window it gets as from a game whose window is not
there yet: no video (d3d8_gl.c draws nothing), no input (server_input.c),
no clipboard, no message boxes (a message is logged). Stopping is SIGTERM's
or SIGINT's (a service stopped, Ctrl+C): the next frame's event pump ends
the program, which withdraws its game from the lists on the way out, as the
game's dedicated mode does on SDL's quit event.

Before the game starts (a constructor, so nothing has been loaded yet), the
program checks what it was given: --version and --help answer and exit; a
run with neither HALO_DEDICATED (a playlist) nor HALO_PROBE (an invite) is
told how to start a server; a data folder without maps, or a playlist that
cannot be read, is a clear error (exit status 1) instead of a server that
never hosts.
*/

#include "platform.h"
#include "sdl_platform.h"
#include "port_config.h"
#include "posix.h"
#include "browser.h"
#include "halo_port_limits.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* (given for this file by the build, as for updater.c: tools/server_build.py) */
#ifndef HALO_VERSION
#define HALO_VERSION "dev"
#endif

#if defined(__x86_64__)
#define SERVER_ARCHITECTURE "x64"
#elif defined(__aarch64__)
#define SERVER_ARCHITECTURE "arm64"
#elif defined(__i386__)
#define SERVER_ARCHITECTURE "x86"
#else
#define SERVER_ARCHITECTURE "unknown"
#endif

/* SIGTERM or SIGINT arrived: the next pump stops the server */
static volatile sig_atomic_t stop_requested;

/* ---------- start-up checks */

static void print_version(void)
{
	printf("ChupathingyCE Dedicated Server %s (linux-%s, network version %d)\n", HALO_VERSION,
		SERVER_ARCHITECTURE, (int)HALO_PORT_NETWORK_VERSION);
}

static void print_usage(FILE *stream)
{
	fprintf(stream,
		"Usage: HALO_DEDICATED=playlists/<name>.txt chupathingyce-server\n"
		"\n"
		"Hosts system link games from a playlist, listed on halo.milenko.org and in\n"
		"the in-game Server Browser. The data folder (HALO_DATA_ROOT, else the\n"
		"current folder or the program's) holds maps/ (ui.map and the multiplayer\n"
		"maps) and the playlist.\n"
		"\n"
		"Settings (environment variables):\n"
		"  HALO_DEDICATED                  the playlist, in the data folder\n"
		"  HALO_DEDICATED_NAME             the server's name (15 characters at most)\n"
		"  HALO_DEDICATED_MINIMUM_PLAYERS  players a game waits for (1)\n"
		"  HALO_DEDICATED_MAXIMUM_PLAYERS  players a game takes (12)\n"
		"  HALO_DEDICATED_IDLE_LIMIT       minutes without a score that end a game (5; 0 never)\n"
		"  HALO_DEDICATED_PUBLIC           false: not listed in the in-game Server Browser\n"
		"  HALO_DEDICATED_COMMANDS         a file of commands in the data folder, run at start\n"
		"  HALO_DEDICATED_CONSOLE          true/false: commands on standard input (on in a terminal)\n"
		"  HALO_DEDICATED_CONTROL          the control API's port or address (off; 127.0.0.1)\n"
		"  HALO_DATA_ROOT, HALO_SAVE_ROOT  the data folder, and where saves go\n"
		"\n"
		"HALO_PROBE=<invite> reads the game an invite leads to, prints it and exits.\n"
		"\n"
		"  --version  prints the version\n"
		"  --help     prints this\n"
		"\n"
		"More: server/README.md, https://github.com/ChupathingyCE/chupathingyce\n");
}

static BOOL readable(const char *path)
{
	FILE *file = fopen(path, "r");

	if (!file)
		return FALSE;
	fclose(file);
	return TRUE;
}

/* the playlist's path in the data folder (as dedicated.c reads it, d:\<name>) */
static void playlist_path(const char *playlist, char *path, unsigned long size)
{
	char xbox_path[512];
	char *cursor;

	snprintf(xbox_path, sizeof(xbox_path), "d:\\%s", playlist);
	for (cursor = xbox_path; *cursor; cursor++)
	{
		if (*cursor == '/')
			*cursor = '\\';
	}
	platform_translate_path(xbox_path, path, size);
}

/* the data folder and the playlist, before anything loads from them */
static void server_check_data(void)
{
	const char *root = platform_data_root();
	char path[1024];

	platform_translate_path("d:\\maps\\ui.map", path, sizeof(path));
	if (!readable(path))
	{
		fprintf(stderr,
			"chupathingyce-server: no maps: %s/maps/ui.map is missing.\n"
			"Put ui.map and the multiplayer maps in the data folder's maps folder, or point\n"
			"HALO_DATA_ROOT at the folder that holds maps/ (server/README.md).\n", root);
		platform_log("no maps: %s/maps/ui.map is missing", root);
		exit(EXIT_FAILURE);
	}
	if (browser_dedicated())
	{
		playlist_path(getenv("HALO_DEDICATED"), path, sizeof(path));
		if (!readable(path))
		{
			fprintf(stderr, "chupathingyce-server: cannot read the playlist %s (HALO_DEDICATED, in the data folder %s)\n",
				path, root);
			platform_log("cannot read the playlist %s", path);
			exit(EXIT_FAILURE);
		}
	}
}

static void stop_handler(int signal_number)
{
	(void)signal_number;
	stop_requested = 1;
}

/* after the Xbox address space is reserved (xbox_memory.c, priority 101),
before the game starts */
__attribute__((constructor(102)))
static void server_arguments(void)
{
	struct sigaction action;
	char argument[256];
	int index;

	for (index = 1; posix_command_line_argument(index, argument, sizeof(argument)); index++)
	{
		if (!strcmp(argument, "--version") || !strcmp(argument, "-v"))
		{
			print_version();
			exit(EXIT_SUCCESS);
		}
		if (!strcmp(argument, "--help") || !strcmp(argument, "-h"))
		{
			print_version();
			printf("\n");
			print_usage(stdout);
			exit(EXIT_SUCCESS);
		}
		fprintf(stderr, "chupathingyce-server: unknown argument %s\n\n", argument);
		print_usage(stderr);
		exit(2);
	}
	/* (no sound to open: dsound_sdl.c's mixer runs on its own clock) */
	setenv("HALO_NO_AUDIO", "1", 0);
	if (!browser_dedicated() && !browser_probe())
	{
		print_version();
		fprintf(stderr, "\nNo playlist: set HALO_DEDICATED to a playlist in the data folder.\n\n");
		print_usage(stderr);
		exit(2);
	}
	platform_log("ChupathingyCE Dedicated Server %s (linux-%s)", HALO_VERSION, SERVER_ARCHITECTURE);
	server_check_data();
	/* a write to a connection the other end closed fails instead of ending
	the server (as sdl_platform.c); SIGTERM and SIGINT stop it at the next
	frame */
	signal(SIGPIPE, SIG_IGN);
	memset(&action, 0, sizeof(action));
	action.sa_handler = stop_handler;
	sigemptyset(&action.sa_mask);
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);
}

/* ---------- the platform layer's start (all done above) */

BOOL platform_sdl_initialize(void)
{
	return TRUE;
}

/* no disc image to offer: a server is given its maps */
BOOL platform_offer_game_data(const char *destination)
{
	(void)destination;
	return FALSE;
}

/* as sdl_platform.c's (display.interpolation, on unless set off): the game's
main loop then runs unthrottled by the Xbox's 60 Hz display, which with no
display to wait for would spin; the director paces it (dedicated.c) */
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

/* ---------- no window */

BOOL platform_screen_mode(long *width, long *height)
{
	(void)width;
	(void)height;
	return FALSE;
}

/* (Video Setup's lists: a server has no display) */
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

BOOL platform_video_initialize(unsigned long width, unsigned long height)
{
	(void)width;
	(void)height;
	return FALSE;
}

void platform_display_apply(void)
{
}

void platform_video_drawable_size(int *width, int *height)
{
	*width = 640;
	*height = 480;
}

void platform_video_window_size(int *width, int *height)
{
	*width = 640;
	*height = 480;
}

void platform_video_swap(void)
{
}

void platform_mouse_capture(BOOL capture)
{
	(void)capture;
}

/* ---------- no player at the machine */

BOOL platform_next_keystroke(struct platform_keystroke *keystroke)
{
	(void)keystroke;
	return FALSE;
}

int platform_clipboard_get(char *text, int size)
{
	if (size > 0)
		text[0] = 0;
	return 0;
}

void platform_clipboard_set(const char *text)
{
	(void)text;
}

void platform_show_message(const char *title, const char *message)
{
	platform_log("%s: %s", title, message);
}

void platform_open_url(const char *url)
{
	(void)url;
}

void platform_request_quit(void)
{
	stop_requested = 1;
}

void platform_scoreboard_scroll(int open, long *notches, long *pages)
{
	(void)open;
	if (notches)
		*notches = 0;
	if (pages)
		*pages = 0;
}

void platform_menus_set_active(BOOL active)
{
	(void)active;
}

void platform_binding_capture_begin(void)
{
}

int platform_binding_capture_poll(int *input)
{
	(void)input;
	return 3;
}

void platform_ui_pointer_set_active(BOOL active)
{
	(void)active;
}

BOOL platform_ui_pointer_read(struct platform_ui_pointer *pointer)
{
	memset(pointer, 0, sizeof(*pointer));
	return FALSE;
}

void platform_input_read(struct platform_input_state *state, BOOL consume_motion)
{
	(void)consume_motion;
	memset(state, 0, sizeof(*state));
}

/* ---------- events */

/* every frame (server_input.c's XInputGetState): stops as asked */
void platform_pump_events(void)
{
	if (stop_requested)
	{
		platform_log("dedicated server: stopping");
		exit(EXIT_SUCCESS);
	}
}

/* ---------- the version (updater.c's, which the server leaves out: it
updates as its deployment does, server/README.md) */

void updater_start(void)
{
}

const char *updater_version(void)
{
	return HALO_VERSION;
}
