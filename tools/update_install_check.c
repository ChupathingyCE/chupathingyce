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
an update that succeeds ends it by starting the new one.
*/

#define _GNU_SOURCE
/* (in place of the game's platform.h and its SDK headers: what updater.c
uses of it is here) */
#define __HALO_LINUX_PLATFORM_H

#include "build_identity.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
const char *platform_save_root(void);

#include "updater.c"

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
	(void)version;
	(void)where;
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
	return !strcmp(name, "debug.update_answer") ? "yes" : "";
}

int config_write_boolean(const char *name, int value)
{
	(void)name;
	(void)value;
	return 0;
}

int main(void)
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
	platform_log("started");
	updater_start();
	/* (as the game's frames do, until the check is answered) */
	deadline = SDL_GetTicks() + 60000;
	while (SDL_GetAtomicInt(&updater_state) == _updater_checking && SDL_GetTicks() < deadline)
		SDL_Delay(10);
	updater_poll(NULL);
	platform_log("done");
	SDL_Quit();
	return 0;
}
