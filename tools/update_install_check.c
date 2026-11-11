/*
UPDATE_INSTALL_CHECK.C

The desktop self-updater (port/linux/src/updater.c, posix_update.c), whole,
as a small program of its own for tools/test_update_install.py: built for
the updater's test (HALO_UPDATE_TEST, a test key, a version given by the
test), with stand-ins for the game's settings and log, it does what the game
does at its start and on its frames: looks for a newer release (at
HALO_UPDATE_TEST_SERVER, trusting HALO_UPDATE_TEST_CA), and answers yes. An
update it installs starts the new program, which looks again and finds
itself the latest.

Every line of its log is appended to the file HALO_UPDATE_TEST_LOG names,
after the version and the executable's file name: "<version> <name> <line>".
It exits once the check is answered (its update failed, or there is none);
an update that succeeds ends it by starting the new one, in its place (the
same process: its log says "pid <n>" as it starts). Started with APPIMAGE
set, it is an AppImage, as the AppImage runtime would start it.

HALO_UPDATE_TEST_ANSWER is the answer to the question (debug.update_answer:
"yes" when it is not set; "" asks, which under gamescope, with
GAMESCOPE_WAYLAND_DISPLAY set, only puts it on the version line). With
HALO_UPDATE_TEST_PAGE=1 the update is installed as the game's own pages
install it (the setup screen's UPDATE, Settings > Online > Updates'
INSTALL UPDATE: updater_install_begin), on its own thread, each line the
page would show logged as "page: <line>". A dialog or a window the updater
opens is logged as "dialog: <title>" or "window: <title>", the version
line's text as "watermark: <text>". With HALO_UPDATE_TEST_IN_GAME=1 a game
is in progress for two seconds once the install is ready (it waits), and
with HALO_UPDATE_TEST_CRASH_AT_EXIT=1 an exit handler kills the process
before the updater's last one runs (the watcher starts the new version).
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
/* (in place of the game's platform.h and its SDK headers: what updater.c
uses of it is here) */
#define __HALO_LINUX_PLATFORM_H

#include "build_identity.h"
#include "packaged_install.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
const char *platform_save_root(void);
int platform_packaged(void);

/* (the updater's dialogs and windows, logged: SDL's own, by these names in
updater.c, as SDL.h declares them) */
#define SDL_ShowSimpleMessageBox check_show_simple_message_box
#define SDL_ShowMessageBox check_show_message_box
#define SDL_CreateWindow check_create_window

#include "updater.c"

bool check_show_simple_message_box(SDL_MessageBoxFlags flags, const char *title, const char *message,
	SDL_Window *window)
{
	(void)flags;
	(void)window;
	platform_log("dialog: %s: %s", title, message);
	return false;
}

bool check_show_message_box(const SDL_MessageBoxData *data, int *button)
{
	platform_log("dialog: %s: %s", data->title, data->message);
	if (button)
		*button = -1;
	return false;
}

SDL_Window *check_create_window(const char *title, int w, int h, SDL_WindowFlags flags)
{
	(void)w;
	(void)h;
	(void)flags;
	platform_log("window: %s", title);
	return NULL;
}

static char check_executable_name[256] = "?";

void platform_log(const char *format, ...)
{
	const char *path = getenv("HALO_UPDATE_TEST_LOG");
	char line[1024];
	va_list arguments;
	FILE *file;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	fprintf(stderr, PLATFORM_LOG_PREFIX "%s\n", line);
	if (path && *path && (file = fopen(path, "a")) != NULL)
	{
		fprintf(file, "%s %s %s\n", HALO_VERSION, check_executable_name, line);
		fclose(file);
	}
}

const char *platform_save_root(void)
{
	return ".";
}

const char *build_identity_user_agent(void)
{
	return "chupathingyce-update-test/" HALO_VERSION;
}

void build_identity_watermark_update(const char *version, const char *where)
{
	/* (as build_identity.c puts it) */
	platform_log("watermark: %s - %s is out: update %s", HALO_VERSION, version, where);
}

void build_identity_watermark_note(const char *note)
{
	platform_log("watermark: %s - %s", HALO_VERSION, note);
}

/* (menu_functions.c's: with HALO_UPDATE_TEST_IN_GAME=1, a game is in
progress for two seconds after the install is ready, then the player leaves
it) */
int updater_restart_allowed(void)
{
	static Uint64 ready;
	const char *in_game = getenv("HALO_UPDATE_TEST_IN_GAME");

	if (!in_game || strcmp(in_game, "1"))
		return 1;
	if (updater_background.phase != _install_ready)
		return 0;
	if (!ready)
		ready = SDL_GetTicks();
	if (SDL_GetTicks() - ready < 2000)
		return 0;
	platform_log("left the game");
	return 1;
}

/* (HALO_UPDATE_TEST_CRASH_AT_EXIT=1: an exit handler of the game's that
kills the process, before the updater's last one can run) */
static void check_crash_at_exit(void)
{
	platform_log("crashing at exit");
	raise(SIGKILL);
}

int config_boolean(const char *name)
{
	return !strcmp(name, "update.auto");
}

double config_real(const char *name)
{
	(void)name;
	return 0.0;
}

const char *config_string(const char *name)
{
	/* (update.channel: the test's, HALO_UPDATE_TEST_CHANNEL; none, the
	release channel) */
	if (!strcmp(name, "update.channel"))
		return getenv("HALO_UPDATE_TEST_CHANNEL") ? getenv("HALO_UPDATE_TEST_CHANNEL") : "";
	if (!strcmp(name, "debug.update_answer"))
		return getenv("HALO_UPDATE_TEST_ANSWER") ? getenv("HALO_UPDATE_TEST_ANSWER") : "yes";
	return "";
}

/* (a packaged install's mark, as the game reads it: HALO_PACKAGED, or the
file "packaged" beside the executable) */
int platform_packaged(void)
{
	char folder[1024], path[1100];
	ssize_t length = readlink("/proc/self/exe", folder, sizeof(folder) - 1);
	SDL_PathInfo information;
	char *slash;

	folder[length > 0 ? length : 0] = 0;
	slash = strrchr(folder, '/');
	if (slash)
		*slash = 0;
	return packaged_install_marked(getenv("HALO_PACKAGED"),
		packaged_install_file_path(path, sizeof(path), folder) && SDL_GetPathInfo(path, &information) &&
		information.type == SDL_PATHTYPE_FILE);
}

int config_packaged_update_check(void)
{
	return 1;
}

int config_write_boolean(const char *name, int value)
{
	(void)name;
	(void)value;
	return 0;
}

int main(int argc, char **argv)
{
	char path[1024];
	ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
	Uint64 deadline;

	if (length > 0)
	{
		path[length] = 0;
		snprintf(check_executable_name, sizeof(check_executable_name), "%s",
			strrchr(path, '/') ? strrchr(path, '/') + 1 : path);
	}
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		platform_log("SDL_Init failed: %s", SDL_GetError());
		return 1;
	}
	if (getenv("HALO_UPDATE_TEST_CRASH_AT_EXIT") && !strcmp(getenv("HALO_UPDATE_TEST_CRASH_AT_EXIT"), "1"))
		atexit(check_crash_at_exit);
	platform_log("started");
	{
		/* (an update's new version is started with these too) */
		char arguments[512] = "";
		int index;

		for (index = 1; index < argc; index++)
			snprintf(arguments + strlen(arguments), sizeof(arguments) - strlen(arguments), " %s", argv[index]);
		platform_log("arguments:%s", arguments);
	}
	/* (the process: an update's new version is this one, in its place) */
	platform_log("pid %ld, parent %ld, group %ld", (long)getpid(), (long)getppid(), (long)getpgrp());
	/* (the launcher's restarts, SETUP's and a mode's: this same process
started again with the one argument given, HALO_UPDATE_TEST_RESTART_WITH,
and none of its own; once, as the new one has it) */
	if (getenv("HALO_UPDATE_TEST_RESTART_WITH") && argc < 2 && length > 0)
	{
		platform_log("restarting with %s", getenv("HALO_UPDATE_TEST_RESTART_WITH"));
		if (!update_start_in_place_with(path, getenv("HALO_UPDATE_TEST_RESTART_WITH")))
			platform_log("restart refused");
		SDL_Quit();
		exit(EXIT_SUCCESS);
	}
	/* (what an update's AppImage was started with: update_launch) */
	platform_log("library path: %s", getenv("LD_LIBRARY_PATH") ? getenv("LD_LIBRARY_PATH") : "(none)");
	updater_start();
	/* (as the game's frames do, until the check is answered) */
	deadline = SDL_GetTicks() + 60000;
	while (SDL_GetAtomicInt(&updater_state) == _updater_checking && SDL_GetTicks() < deadline)
		SDL_Delay(10);
	updater_poll(NULL);
	/* (a page's install: begun, and its lines shown each frame until the
	game restarts into the new version, or it fails) */
	if (getenv("HALO_UPDATE_TEST_PAGE") && !strcmp(getenv("HALO_UPDATE_TEST_PAGE"), "1"))
	{
		char line[256], shown[256] = "";
		int state;

		platform_log("page: %s", updater_install_begin() ? "installing" : "nothing to install");
		deadline = SDL_GetTicks() + 60000;
		do
		{
			updater_poll(NULL);
			state = updater_install_status(line, sizeof(line));
			if (strcmp(line, shown))
			{
				snprintf(shown, sizeof(shown), "%s", line);
				platform_log("page: %s", line);
			}
			SDL_Delay(5);
		} while ((state == UPDATER_INSTALL_DOWNLOADING || state == UPDATER_INSTALL_INSTALLING ||
			state == UPDATER_INSTALL_WAITING) &&
			SDL_GetTicks() < deadline);
	}
	platform_log("done");
	SDL_Quit();
	return 0;
}
