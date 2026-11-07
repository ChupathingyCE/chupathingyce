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
game's dedicated mode does on SDL's quit event. On Windows the same are
Ctrl+C, Ctrl+Break, the console window closed, and a service's Stop
(server_windows.c, which also installs the server as a service).

Before the game starts (a constructor, so nothing has been loaded yet), the
program checks what it was given: --version and --help answer and exit; a
run with neither HALO_DEDICATED (a playlist) nor HALO_PROBE (an invite) is
told how to start a server, unless its control panel is on (it then
starts for the panel's first-run setup, which chooses the playlist: one
included with the server until then); a data folder without maps, or a playlist that
cannot be read, is a clear error (exit status 1) instead of a server that
never hosts.
*/

#include "platform.h"
#include "sdl_platform.h"
#include "port_config.h"
#include "posix.h"
#include "browser.h"
#include "p2p.h"
#include "halo_port_limits.h"
#include "delta.h"
#include "server_control.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

/* (given for this file by the build, as for updater.c: tools/server_build.py) */
#ifndef HALO_VERSION
#define HALO_VERSION "dev"
#endif

/* SIGTERM or SIGINT arrived: the next pump stops the server */
static volatile sig_atomic_t stop_requested;

#ifdef _WIN32
/* server_windows.c's: plain types */
void server_windows_console_handlers(void (*stop)(void));
int server_windows_service_start(void (*stop)(void));
int server_windows_install_service(const char *name);
int server_windows_uninstall_service(const char *name);
#endif

/* ---------- start-up checks */

static void print_version(void)
{
	printf("%s, network version %d\n", build_identity(), delta_legacy_announce());
}

static void print_usage(FILE *stream)
{
	fprintf(stream,
		"Usage: HALO_DEDICATED=playlists/<name>.txt chupathingyce-server\n"
		"\n"
		"Hosts system link games from a playlist, listed on stats.chupathingy.org and in\n"
		"the in-game Server Browser. The data folder (HALO_DATA_ROOT, else the\n"
		"current folder or the program's) holds maps/ (ui.map and the multiplayer\n"
		"maps) and the playlist.\n"
		"\n"
		"Settings (environment variables):\n"
		"  HALO_DEDICATED                  the playlist, in the data folder (with the control\n"
		"                                  panel on, the panel's first-run setup chooses it)\n"
		"  HALO_DEDICATED_NAME             the server's name (15 characters at most)\n"
		"  HALO_DEDICATED_MINIMUM_PLAYERS  players a game waits for (1)\n"
		"  HALO_DEDICATED_MAXIMUM_PLAYERS  players a game takes (12)\n"
		"  HALO_DEDICATED_IDLE_LIMIT       minutes without a score that end a game (5; 0 never)\n"
		"  HALO_DEDICATED_PUBLIC           false: not listed in the in-game Server Browser\n"
		"  HALO_DEDICATED_COMMANDS         a file of commands in the data folder, run at start\n"
		"  HALO_DEDICATED_CONSOLE          true/false: commands on standard input (on in a terminal)\n"
		"  HALO_DEDICATED_CONTROL          the control panel's port or address (127.0.0.1:8080; off)\n"
		"  HALO_DATA_ROOT, HALO_SAVE_ROOT  the data folder, and where saves go\n"
		"\n"
		"HALO_PROBE=<invite> reads the game an invite leads to, prints it and exits.\n"
		"\n"
		"  --version  prints the version\n"
		"  --help     prints this\n"
#ifdef _WIN32
		"  --install-service [name]    makes a Windows service of this server, with the\n"
		"                              HALO_* settings set now (ChupathingyServer unless named)\n"
		"  --uninstall-service [name]  stops the service and takes it out\n"
#endif
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

/* the playlist a server started for the first-run setup hosts until its
owner chooses one (the setup's choice is kept, as sv_playlist_use's, while
HALO_DEDICATED is this): free_for_all.txt, else the first of the data
folder's playlists by name; 0 if there is none */
static int setup_playlist(char *playlist, unsigned long size)
{
	char folder[1024];
	char path[1024];
	char name[256];
	void *directory;

	playlist_path("playlists/free_for_all.txt", path, sizeof(path));
	if (readable(path))
	{
		snprintf(playlist, size, "playlists/free_for_all.txt");
		return 1;
	}
	playlist[0] = 0;
	playlist_path("playlists", folder, sizeof(folder));
	directory = posix_directory_open(folder);
	if (!directory)
		return 0;
	while (posix_directory_next(directory, name, sizeof(name)))
	{
		size_t length = strlen(name);
		char candidate[300];

		if (length < 5 || length > 200 || name[0] == '.' || strcmp(name + length - 4, ".txt"))
			continue;
		snprintf(candidate, sizeof(candidate), "playlists/%s", name);
		if (playlist[0] && strcmp(candidate, playlist) >= 0)
			continue;
		snprintf(path, sizeof(path), "%s/%s", folder, name);
		if (readable(path))
			snprintf(playlist, size, "%s", candidate);
	}
	posix_directory_close(directory);
	return playlist[0] != 0;
}

/* no playlist given, and the control panel on: the server starts for the
panel's first-run setup (1), else 0 */
static int setting_off(const char *value)
{
	static const char *const words[] = { "0", "false", "no", "off" };
	size_t word;

	for (word = 0; word < sizeof(words) / sizeof(words[0]); word++)
	{
		size_t index;

		for (index = 0; words[word][index] && (value[index] | 0x20) == words[word][index]; index++)
			;
		if (!words[word][index] && !value[index])
			return 1;
	}
	return 0;
}

static int start_for_setup(void)
{
	const char *control = server_control_setting();
	char playlist[300];

	if (!control[0] || setting_off(control))
	{
		return 0;
	}
	if (!setup_playlist(playlist, sizeof(playlist)))
	{
		fprintf(stderr, "\nNo playlist: HALO_DEDICATED is not set, and the data folder's playlists folder has none.\n"
			"Copy the server's playlists folder into the data folder.\n\n");
		return 0;
	}
#ifdef _WIN32
	{
		char variable[320];

		snprintf(variable, sizeof(variable), "HALO_DEDICATED=%s", playlist);
		_putenv(variable);
	}
#else
	setenv("HALO_DEDICATED", playlist, 1);
#endif
	server_control_setup_mode();
	printf("No playlist given (HALO_DEDICATED): the control panel's setup chooses one. Until then: %s\n", playlist);
	fflush(stdout);
	return 1;
}

static void stop_handler(int signal_number)
{
	(void)signal_number;
	stop_requested = 1;
}

#ifdef _WIN32
static void stop_now(void)
{
	stop_requested = 1;
}
#endif

/* after the Xbox address space is reserved (xbox_memory.c, priority 101),
before the game starts */
__attribute__((constructor(102)))
static void server_arguments(void)
{
#ifndef _WIN32
	struct sigaction action;
#else
	BOOL service = FALSE;
#endif
	char argument[256];
	int index;

	for (index = 1; posix_command_line_argument(index, argument, sizeof(argument)); index++)
	{
#ifdef _WIN32
		if (!strcmp(argument, "--install-service") || !strcmp(argument, "--uninstall-service"))
		{
			char name[128] = "";

			posix_command_line_argument(index + 1, name, sizeof(name));
			exit(!strcmp(argument, "--install-service") ? server_windows_install_service(name)
				: server_windows_uninstall_service(name));
		}
		if (!strcmp(argument, "--service"))
		{
			service = TRUE;
			continue;
		}
#endif
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
#ifdef _WIN32
	/* (the service manager answered first: it waits only so long) */
	if (service && !server_windows_service_start(stop_now))
		exit(2);
#endif
	/* (no sound to open: dsound_sdl.c's mixer runs on its own clock) */
#ifdef _WIN32
	if (!getenv("HALO_NO_AUDIO"))
		_putenv("HALO_NO_AUDIO=1");
#else
	setenv("HALO_NO_AUDIO", "1", 0);
#endif
	if (!browser_dedicated() && !browser_probe() && !start_for_setup())
	{
		print_version();
		fprintf(stderr, "\nNo playlist: set HALO_DEDICATED to a playlist in the data folder.\n\n");
		print_usage(stderr);
		exit(2);
	}
	/* which server this is, first in its log (build_identity.c) */
	build_identity_log();
	server_check_data();
	/* (the legacy table: a newer one fetched meanwhile) */
	delta_legacy_start();
#ifdef _WIN32
	/* Ctrl+C, Ctrl+Break and the window closed stop it at the next frame
	(Windows has no SIGPIPE: a closed connection's write fails) */
	server_windows_console_handlers(stop_now);
	(void)stop_handler;
#else
	/* a write to a connection the other end closed fails instead of ending
	the server (as sdl_platform.c); SIGTERM and SIGINT stop it at the next
	frame */
	signal(SIGPIPE, SIG_IGN);
	memset(&action, 0, sizeof(action));
	action.sa_handler = stop_handler;
	sigemptyset(&action.sa_mask);
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);
#endif
}

/* ---------- the platform layer's start (all done above) */

BOOL platform_sdl_initialize(void)
{
	return TRUE;
}

/* no disc image to offer: a server is given its maps */
BOOL platform_offer_game_data(char *destination, unsigned long size)
{
	(void)destination;
	(void)size;
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

/* (no invite to ask about: port/linux/game/invite_prompt.c) */
int platform_take_clipboard_invite(char *text, int size)
{
	if (size > 0)
		text[0] = 0;
	return 0;
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

/* (the server plays no movies: bink_playback.c) */
int platform_movie_skip_pressed(void)
{
	return 0;
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

/* whether people can join, for sv_status (server_commands.c): internet
play's port and UPnP, the NAT as the STUN servers saw it, and the game
list's listing. As JSON, an object's fields (no braces); else lines. Never
an address: only the port, and states */
int server_platform_reach(char *text, int size, int json)
{
	struct p2p_reach reach;
	int internet = 0;
	int listing, status = 0;
	const char *upnp, *nat, *listed;
	static const char *const listings[] = { "listed", "not_hosting", "private", "off", "no_list", "waiting", "failed" };

	if (size <= 0)
		return 0;
	text[0] = 0;
#ifdef HALO_GAME_BROWSER
	internet = p2p_hosting_reach(&reach);
	listing = browser_host_listing(&status);
#else
	memset(&reach, 0, sizeof(reach));
	listing = BROWSER_HOST_NO_LIST;
#endif
	upnp = !internet ? "off" : reach.upnp == P2P_UPNP_FORWARDED ? "forwarded" : !reach.upnp_allowed ? "off" :
		reach.upnp == P2P_UPNP_NO_ROUTER ? "no_router" : reach.upnp == P2P_UPNP_REFUSED ? "refused" : "not_asked";
	nat = !internet ? "off" : reach.strict ? "strict" : reach.stun_answers >= 2 || (reach.settled && reach.stun_answers) ?
		"open" : !reach.settled ? "checking" : "unknown";
	listed = listing >= 0 && listing < (int)(sizeof(listings) / sizeof(listings[0])) ? listings[listing] : "no_list";
	if (json)
	{
		snprintf(text, (size_t)size, "\"internet\": %s, \"port\": %u, \"port_fixed\": %s, \"upnp\": \"%s\", "
			"\"nat\": \"%s\", \"listing\": \"%s\", \"listing_status\": %d", internet ? "true" : "false",
			(unsigned)reach.port, reach.port_fixed ? "true" : "false", upnp, nat, listed, status);
		return 1;
	}
	if (!internet)
		snprintf(text, (size_t)size, "internet play: off (network.online)\n");
	else
	{
		snprintf(text, (size_t)size, "internet play: UDP %u%s; %s; %s\n", (unsigned)reach.port,
			reach.port_fixed ? "" : " (a new port each run)",
			!strcmp(upnp, "forwarded") ? "forwarded by the router (UPnP)" : !strcmp(upnp, "no_router") ?
			"UPnP found no router" : !strcmp(upnp, "refused") ? "the router refused UPnP" : !strcmp(upnp, "off") ?
			"UPnP off" : "UPnP not asked yet",
			!strcmp(nat, "open") ? "most players can reach it" : !strcmp(nat, "strict") ?
			"strict NAT: forward the port" : !strcmp(nat, "checking") ? "checking the network" :
			"the STUN servers did not answer");
	}
	{
		size_t used = strlen(text);
		static const char *const reasons[] = { "listed", "not hosting on the internet yet", "not public",
			"off (network.list_hosted_games)", "no game list (network.browser_url)", "waiting for the game list",
			"the game list did not take it" };

		snprintf(text + used, (size_t)size - used, "game list: %s", reasons[listing >= 0 && listing < 7 ? listing : 4]);
		used = strlen(text);
		if (listing == BROWSER_HOST_FAILED)
		{
			if (status)
				snprintf(text + used, (size_t)size - used, " (HTTP %d)", status);
			else
				snprintf(text + used, (size_t)size - used, " (no answer)");
		}
		used = strlen(text);
		snprintf(text + used, (size_t)size - used, "\n");
	}
	return 1;
}
