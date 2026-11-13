/*
POSIX_STEAM_BRIDGE.C

The bridge's platform functions (steam_client_bridge.h) on Linux, macOS and
the BSDs, built with the C library's own headers as the other posix_*.c;
Windows has its own (port/windows/src/win32_steam_bridge.c), and Android
none (steam_client_bridge.c's stand-ins): a
library loaded at run time, the environment, and steamcmd as a child, its
sign-in in a terminal, and MCC's depots moved into place.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/
#include "../include/halo_port_features.h" /* HALO_MCC: on a console it is 0 before the test below */
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h): a console builds this to nothing */

#if !defined(_WIN32) && !defined(__ANDROID__)

#include "steam_client_bridge.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

const char *config_string(const char *name);
const char *platform_save_root(void);
void platform_log(const char *format, ...);

void *steam_bridge_platform_library_open(const char *path)
{
	return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

void *steam_bridge_platform_library_symbol(void *library, const char *name)
{
	return library ? dlsym(library, name) : NULL;
}

void steam_bridge_platform_set_app_id(const char *app_id)
{
	setenv("SteamAppId", app_id, 1);
	setenv("SteamGameId", app_id, 1);
}

void steam_bridge_platform_sleep(int milliseconds)
{
	struct timespec time = { milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L };

	while (nanosleep(&time, &time) != 0 && errno == EINTR)
		;
}

/* the player's own home, not HOME (a test run may move HOME): steamcmd
keeps its sign-in there */
static const char *real_home(void)
{
	struct passwd *entry = getpwuid(getuid());

	return entry && entry->pw_dir && entry->pw_dir[0] ? entry->pw_dir : getenv("HOME");
}

static int executable(const char *path)
{
	struct stat information;

	return stat(path, &information) == 0 && S_ISREG(information.st_mode) && access(path, X_OK) == 0;
}

static int is_directory(const char *path)
{
	struct stat information;

	return stat(path, &information) == 0 && S_ISDIR(information.st_mode);
}

int steam_bridge_platform_find_steamcmd(char *path, int size)
{
	static const char *const usual[] = { "/opt/homebrew/bin/steamcmd", "/usr/local/bin/steamcmd",
		"/usr/games/steamcmd", "/usr/bin/steamcmd" };
	static const char *const home_usual[] = { "steamcmd/steamcmd.sh", "Steam/steamcmd.sh",
		".steam/steamcmd/steamcmd.sh", "Library/Application Support/Steam/steamcmd.sh" };
	const char *setting = config_string("paths.steamcmd"), *search = getenv("PATH"), *home = real_home();
	int index;

	if (setting && setting[0])
		return snprintf(path, (size_t)size, "%s", setting) < size && executable(path);
	while (search && *search)
	{
		const char *end = strchr(search, ':');
		int length = end ? (int)(end - search) : (int)strlen(search);

		if (length > 0 && snprintf(path, (size_t)size, "%.*s/steamcmd", length, search) < size && executable(path))
			return 1;
		if (length > 0 && snprintf(path, (size_t)size, "%.*s/steamcmd.sh", length, search) < size && executable(path))
			return 1;
		search = end ? end + 1 : NULL;
	}
	for (index = 0; index < (int)(sizeof(usual) / sizeof(usual[0])); index++)
	{
		if (snprintf(path, (size_t)size, "%s", usual[index]) < size && executable(path))
			return 1;
	}
	for (index = 0; home && index < (int)(sizeof(home_usual) / sizeof(home_usual[0])); index++)
	{
		if (snprintf(path, (size_t)size, "%s/%s", home, home_usual[index]) < size && executable(path))
			return 1;
	}
	return 0;
}

int steam_bridge_platform_steamcmd_root(const char *steamcmd, char *root, int size)
{
	const char *home = real_home();
	const char *slash = strrchr(steamcmd, '/');
	int length;

#ifdef __APPLE__
	/* (steamcmd on macOS keeps everything here, wherever it is installed) */
	(void)slash;
	(void)length;
	return home && snprintf(root, (size_t)size, "%s/Library/Application Support/Steam", home) < size;
#else
	/* (steamcmd.sh keeps it beside itself; a packaged steamcmd in ~/.steam/steamcmd, else ~/Steam) */
	length = (int)strlen(steamcmd);
	if (slash && length > 11 && !strcmp(steamcmd + length - 11, "steamcmd.sh"))
		return snprintf(root, (size_t)size, "%.*s", (int)(slash - steamcmd), steamcmd) < size;
	if (!home)
		return 0;
	if (snprintf(root, (size_t)size, "%s/.steam/steamcmd", home) < size && is_directory(root))
		return 1;
	return snprintf(root, (size_t)size, "%s/Steam", home) < size;
#endif
}

/* ---------- the child */

struct child
{
	pid_t pid;
	int output;
	int ended;
	int length;
	char buffer[4096];
};

void *steam_bridge_platform_child_start(const char *const *arguments)
{
	struct child *child = calloc(1, sizeof(*child));
	int pipes[2];
	pid_t pid;

	if (!child || pipe(pipes) != 0)
	{
		free(child);
		return NULL;
	}
	pid = fork();
	if (pid < 0)
	{
		close(pipes[0]);
		close(pipes[1]);
		free(child);
		return NULL;
	}
	if (pid == 0)
	{
		int empty = open("/dev/null", O_RDONLY);
		const char *home = real_home();

		/* (its own group: steamcmd.sh's steamcmd ends with it) */
		setpgid(0, 0);
		/* (steamcmd's own home: where its saved sign-in is) */
		if (home)
			setenv("HOME", home, 1);
		if (empty >= 0)
			dup2(empty, 0);
		dup2(pipes[1], 1);
		dup2(pipes[1], 2);
		close(pipes[0]);
		close(pipes[1]);
		execv(arguments[0], (char *const *)arguments);
		_exit(127);
	}
	close(pipes[1]);
	fcntl(pipes[0], F_SETFL, fcntl(pipes[0], F_GETFL) | O_NONBLOCK);
	fcntl(pipes[0], F_SETFD, FD_CLOEXEC);
	child->pid = pid;
	child->output = pipes[0];
	return child;
}

int steam_bridge_platform_child_line(void *context, char *line, int size, int milliseconds)
{
	struct child *child = context;

	for (;;)
	{
		int index;
		struct pollfd wait;
		ssize_t got;

		/* (a line: \n or steamcmd's \r) */
		for (index = 0; index < child->length; index++)
		{
			if (child->buffer[index] == '\n' || child->buffer[index] == '\r')
			{
				int copied = index < size - 1 ? index : size - 1;

				memcpy(line, child->buffer, (size_t)copied);
				line[copied] = 0;
				memmove(child->buffer, child->buffer + index + 1, (size_t)(child->length - index - 1));
				child->length -= index + 1;
				return 1;
			}
		}
		if (child->length == (int)sizeof(child->buffer))
		{
			/* (a line too long: cut) */
			int copied = size - 1 < child->length ? size - 1 : child->length;

			memcpy(line, child->buffer, (size_t)copied);
			line[copied] = 0;
			child->length = 0;
			return 1;
		}
		if (child->ended)
		{
			if (child->length > 0)
			{
				int copied = size - 1 < child->length ? size - 1 : child->length;

				memcpy(line, child->buffer, (size_t)copied);
				line[copied] = 0;
				child->length = 0;
				return 1;
			}
			return -1;
		}
		wait.fd = child->output;
		wait.events = POLLIN;
		wait.revents = 0;
		if (poll(&wait, 1, milliseconds) <= 0)
			return 0;
		got = read(child->output, child->buffer + child->length, sizeof(child->buffer) - (size_t)child->length);
		if (got > 0)
			child->length += (int)got;
		else if (got == 0 || (errno != EAGAIN && errno != EINTR))
			child->ended = 1;
		milliseconds = 0;
	}
}

void steam_bridge_platform_child_end(void *context)
{
	struct child *child = context;
	int status;

	if (!child)
		return;
	if (waitpid(child->pid, &status, WNOHANG) == 0)
	{
		kill(-child->pid, SIGTERM);
		kill(child->pid, SIGTERM);
		waitpid(child->pid, &status, 0);
	}
	close(child->output);
	free(child);
}

/* ---------- the sign-in */

/* text in single quotes for sh */
static int quoted(const char *text, char *out, int size)
{
	int length = 0;

	if (size < 3)
		return 0;
	out[length++] = '\'';
	for (; *text; text++)
	{
		if (*text == '\'')
		{
			if (length + 4 >= size)
				return 0;
			memcpy(out + length, "'\\''", 4);
			length += 4;
		}
		else
		{
			if (length + 1 >= size)
				return 0;
			out[length++] = *text;
		}
	}
	if (length + 2 > size)
		return 0;
	out[length++] = '\'';
	out[length] = 0;
	return 1;
}

static void detached(const char *const *arguments)
{
	pid_t pid = fork();

	if (pid == 0)
	{
		/* (twice: the terminal belongs to init, not to the game) */
		if (fork() == 0)
		{
			setsid();
			execvp(arguments[0], (char *const *)arguments);
		}
		_exit(0);
	}
	if (pid > 0)
		waitpid(pid, NULL, 0);
}

int steam_bridge_platform_open_sign_in(const char *steamcmd)
{
	const char *save = platform_save_root();
	char script[STEAM_BRIDGE_PATH_SIZE], program[STEAM_BRIDGE_PATH_SIZE * 2];
	FILE *file;

	if (!save || !quoted(steamcmd, program, sizeof(program)) ||
		snprintf(script, sizeof(script), "%s/steamcmd-sign-in.command", save) >= (int)sizeof(script) ||
		!(file = fopen(script, "w")))
	{
		return 0;
	}
	/* (the player types their account, password and Steam Guard code into
	steamcmd itself; the game sees none of it) */
	fprintf(file, "#!/bin/sh\n"
		"echo 'Sign in to steamcmd. Chupathingy uses the sign-in steamcmd saves; it never sees your password.'\n"
		"printf 'Steam account name: '\n"
		"read name\n"
		"%s +login \"$name\" +quit\n"
		"echo 'Done. You can close this window and go back to the game.'\n", program);
	fclose(file);
	chmod(script, 0700);
#ifdef __APPLE__
	{
		const char *arguments[] = { "/usr/bin/open", "-a", "Terminal", script, NULL };

		detached(arguments);
		return 1;
	}
#else
	{
		static const char *const terminals[][3] = { { "x-terminal-emulator", "-e", NULL }, { "gnome-terminal", "--", NULL },
			{ "konsole", "-e", NULL }, { "xfce4-terminal", "-x", NULL }, { "xterm", "-e", NULL } };
		const char *search = getenv("PATH");
		int index;

		for (index = 0; index < (int)(sizeof(terminals) / sizeof(terminals[0])); index++)
		{
			const char *at = search;

			while (at && *at)
			{
				const char *end = strchr(at, ':');
				int length = end ? (int)(end - at) : (int)strlen(at);
				char found[STEAM_BRIDGE_PATH_SIZE];

				if (length > 0 && snprintf(found, sizeof(found), "%.*s/%s", length, at, terminals[index][0]) <
					(int)sizeof(found) && executable(found))
				{
					const char *arguments[] = { found, terminals[index][1], "/bin/sh", script, NULL };

					detached(arguments);
					return 1;
				}
				at = end ? end + 1 : NULL;
			}
		}
		platform_log("Workshop: no terminal to sign in to steamcmd with; run %s yourself", script);
		return 0;
	}
#endif
}

/* ---------- removing a download */

int steam_bridge_platform_is_folder(const char *path)
{
	struct stat information;

	return lstat(path, &information) == 0 && S_ISDIR(information.st_mode);
}

int steam_bridge_platform_remove_folder(const char *path)
{
	DIR *directory;
	struct dirent *entry;
	struct stat information;
	int result = 1;

	if (lstat(path, &information) != 0)
		return errno == ENOENT;
	if (!S_ISDIR(information.st_mode))
		return unlink(path) == 0;
	if (!(directory = opendir(path)))
		return 0;
	while (result && (entry = readdir(directory)) != NULL)
	{
		char child[STEAM_BRIDGE_PATH_SIZE + 256];

		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
		result = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) < (int)sizeof(child) &&
			steam_bridge_platform_remove_folder(child);
	}
	closedir(directory);
	return result && rmdir(path) == 0;
}

/* ---------- MCC's depots into place */

static int make_folders(const char *path)
{
	char partial[STEAM_BRIDGE_PATH_SIZE + 64];
	size_t index, length = strlen(path);

	if (length >= sizeof(partial))
		return 0;
	for (index = 1; index <= length; index++)
	{
		if (path[index] == '/' || path[index] == 0)
		{
			memcpy(partial, path, index);
			partial[index] = 0;
			if (mkdir(partial, 0755) != 0 && errno != EEXIST)
				return 0;
		}
	}
	return is_directory(path);
}

int steam_bridge_platform_merge_folder(const char *from, const char *to)
{
	DIR *directory;
	struct dirent *entry;
	int result = 1;

	if (!make_folders(to) || !(directory = opendir(from)))
		return 0;
	while (result && (entry = readdir(directory)) != NULL)
	{
		char source[STEAM_BRIDGE_PATH_SIZE + 256], target[STEAM_BRIDGE_PATH_SIZE + 256];
		struct stat information;

		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
		if (snprintf(source, sizeof(source), "%s/%s", from, entry->d_name) >= (int)sizeof(source) ||
			snprintf(target, sizeof(target), "%s/%s", to, entry->d_name) >= (int)sizeof(target) ||
			lstat(source, &information) != 0)
		{
			result = 0;
			break;
		}
		if (S_ISDIR(information.st_mode))
			result = steam_bridge_platform_merge_folder(source, target);
		else if (S_ISREG(information.st_mode))
			result = rename(source, target) == 0;
	}
	closedir(directory);
	return result;
}

#endif
#endif /* HALO_MCC */
