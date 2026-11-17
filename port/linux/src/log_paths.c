/*
LOG_PATHS.C

A folder path as the log may show it (log_paths.h): the player's home
folder as "~" or "%USERPROFILE%", whole with debug.log_full_paths.
*/

#include "log_paths.h"

#ifndef LOG_PATHS_NO_HOME

#include "port_config.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);

enum
{
	MAXIMUM_HOMES = 4,
	HOME_SIZE = 1024,
	/* the shortest home folder taken out: a Windows one's placeholder is 13
	bytes, and 3 * length + 1 is enough for log_paths_redact */
	MINIMUM_WINDOWS_HOME = 5,
	MINIMUM_HOME = 2,
	/* log_paths_redact_line's copy of a line: platform_log's longest, and
	room for placeholders longer than their folders */
	LINE_SIZE = 2048 + 256,
};

struct home
{
	char path[HOME_SIZE];
	size_t length;
	/* a Windows path's (a drive letter or a share): any case, and
	"%USERPROFILE%" */
	int windows;
};

static pthread_once_t homes_once = PTHREAD_ONCE_INIT;
static struct home homes[MAXIMUM_HOMES];
static int home_count = 0;

static int separator(char character)
{
	return character == '/' || character == '\\';
}

/* a character of a folder's or file's name, as a home folder's end must not
be followed by one */
static int name_character(char character)
{
	return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
		(character >= '0' && character <= '9') || character == '_' || character == '-' ||
		(unsigned char)character >= 0x80;
}

static char lower(char character)
{
	return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

static int windows_path(const char *path)
{
	return (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':') ||
		(path[0] == '\\' && path[1] == '\\');
}

static int any_case(const struct home *home)
{
#if defined(_WIN32) || defined(__APPLE__)
	(void)home;
	return 1;
#else
	return home->windows;
#endif
}

static void add_home(const char *path)
{
	struct home *home;
	size_t length;
	int index;

	if (!path || home_count >= MAXIMUM_HOMES)
		return;
	length = strlen(path);
	/* (without its last separator: "C:\Users\bob\" is C:\Users\bob) */
	while (length > 1 && separator(path[length - 1]))
		length--;
	if (length >= HOME_SIZE || length < (windows_path(path) ? MINIMUM_WINDOWS_HOME : MINIMUM_HOME))
		return;
	for (index = 0; index < home_count; index++)
	{
		if (homes[index].length == length && !memcmp(homes[index].path, path, length))
			return;
	}
	home = &homes[home_count++];
	memcpy(home->path, path, length);
	home->path[length] = 0;
	home->length = length;
	home->windows = windows_path(path);
}

static void find_homes(void)
{
	const char *home = getenv("HOME");
	int index, other;

	add_home(home);
#ifndef _WIN32
	if (home && *home)
	{
		/* (where it really is: /var/home/bob for /home/bob on some systems,
		and what a path made with realpath says) */
		char *real = realpath(home, NULL);

		add_home(real);
		free(real);
	}
#else
	add_home(getenv("USERPROFILE"));
	{
		const char *drive = getenv("HOMEDRIVE"), *path = getenv("HOMEPATH");
		char joined[HOME_SIZE];

		if (drive && path && strlen(drive) + strlen(path) < sizeof(joined))
		{
			strcpy(joined, drive);
			strcat(joined, path);
			add_home(joined);
		}
	}
#endif
	/* (the longest first: a home inside another is taken whole) */
	for (index = 1; index < home_count; index++)
	{
		for (other = index; other > 0 && homes[other].length > homes[other - 1].length; other--)
		{
			struct home swap = homes[other];

			homes[other] = homes[other - 1];
			homes[other - 1] = swap;
		}
	}
}

static int home_matches(const struct home *home, const char *text, size_t length)
{
	size_t index;
	int fold = any_case(home);

	if (length < home->length)
		return 0;
	for (index = 0; index < home->length; index++)
	{
		char want = home->path[index], have = text[index];

		if (separator(want) && separator(have))
			continue;
		if (fold ? lower(want) != lower(have) : want != have)
			return 0;
	}
	/* (a whole folder name: what follows ends it, or is a sentence's '.') */
	if (index < length && name_character(text[index]))
		return 0;
	if (index < length && text[index] == '.' && index + 1 < length && name_character(text[index + 1]))
		return 0;
	return 1;
}

size_t log_paths_home_at(const char *text, size_t length, size_t position, const char **placeholder)
{
	int index;

	pthread_once(&homes_once, find_homes);
	if (position >= length)
		return 0;
	/* (where a path can start: not inside a name) */
	if (position && (name_character(text[position - 1]) || text[position - 1] == '.'))
		return 0;
	for (index = 0; index < home_count; index++)
	{
		/* (a home that starts at the root is not one under another folder,
		/Volumes/x/Users/bob; a Windows one may follow \\?\ or file:///) */
		if (position && separator(text[position - 1]) && !homes[index].windows)
			continue;
		if (home_matches(&homes[index], text + position, length - position))
		{
			*placeholder = homes[index].windows ? "%USERPROFILE%" : "~";
			return homes[index].length;
		}
	}
	return 0;
}

size_t log_paths_redact(const char *text, size_t length, char *out, size_t size)
{
	size_t used = 0, written = 0;

	if (!size)
		return 0;
	while (used < length)
	{
		const char *placeholder;
		size_t found = log_paths_home_at(text, length, used, &placeholder);

		if (found)
		{
			size_t placeholder_length = strlen(placeholder);

			if (written + placeholder_length >= size)
				break;
			memcpy(out + written, placeholder, placeholder_length);
			written += placeholder_length;
			used += found;
			continue;
		}
		if (written + 1 >= size)
			break;
		out[written++] = text[used++];
	}
	out[written] = 0;
	return written;
}

/* ---------- debug.log_full_paths */

static volatile int whole_state = -1;
static volatile int whole_said = 0;

int log_paths_whole(void)
{
	int whole = whole_state;

	if (whole >= 0)
		return whole;
	/* (before config.toml is read, the paths are taken out: config_boolean
	would read it, and reading it writes to the log) */
	if (!config_loaded_yet())
		return 0;
	whole = config_boolean("debug.log_full_paths") ? 1 : 0;
	whole_state = whole;
	/* said once, before the first path the log has whole: a log written so
	is not one to post */
	if (whole && !__sync_lock_test_and_set(&whole_said, 1))
	{
		platform_log("Log: debug.log_full_paths is on (or HALO_LOG_FULL_PATHS is set): folder paths are "
			"written whole, with this computer's account name; do not post this log");
	}
	return whole;
}

void log_paths_redact_line(char *line, size_t size)
{
	char redacted[LINE_SIZE];
	size_t length;

	if (!line || !size || log_paths_whole())
		return;
	{
		const char *end = memchr(line, 0, size);

		if (!end)
			return;
		length = (size_t)(end - line);
	}
	if (length < sizeof(redacted))
	{
		length = log_paths_redact(line, length, redacted, size < sizeof(redacted) ? size : sizeof(redacted));
		memcpy(line, redacted, length + 1);
	}
	else
	{
		/* (a line too long to copy: its account names overwritten in place) */
		log_paths_mask(line, length);
	}
}

/* ---------- binary data */

/* the account name's place in a home folder: after its last separator */
static size_t name_start(const struct home *home)
{
	size_t index = home->length;

	while (index > 0 && !separator(home->path[index - 1]))
		index--;
	return index;
}

/* whether the home folder is at data, its characters width bytes apart (1:
8-bit, 2: UTF-16 little-endian, the high bytes zero) */
static int home_in_data(const struct home *home, const unsigned char *data, size_t size, int width)
{
	size_t index;
	int fold = any_case(home);

	if (size / (size_t)width < home->length)
		return 0;
	for (index = 0; index < home->length; index++)
	{
		char want = home->path[index], have = (char)data[index * (size_t)width];

		if (width == 2 && data[index * 2 + 1])
			return 0;
		if (separator(want) && separator(have))
			continue;
		if (fold ? lower(want) != lower(have) : want != have)
			return 0;
	}
	return 1;
}

void log_paths_mask(void *data, size_t size)
{
	unsigned char *bytes = data;
	size_t position;
	int index, width;

	pthread_once(&homes_once, find_homes);
	if (!bytes)
		return;
	for (position = 0; position < size; position++)
	{
		for (index = 0; index < home_count; index++)
		{
			for (width = 1; width <= 2; width++)
			{
				if (home_in_data(&homes[index], bytes + position, size - position, width))
				{
					size_t character;

					for (character = name_start(&homes[index]); character < homes[index].length; character++)
						bytes[position + character * (size_t)width] = '_';
				}
			}
		}
	}
}

#else

/* (a console's: log_paths.h has its stand-ins) */
typedef int log_paths_console;

#endif
