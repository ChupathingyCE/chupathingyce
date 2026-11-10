/*
LOG_PATHS.H

The one way a folder path is written into the log (debug.txt, the terminal,
the dedicated server's journal) and into what the game sends to the site
(a log sent from the setup screen, a crash report): players post debug.txt
in public, and a path in the player's home folder holds the computer's
account name (/Users/<name>/Library/..., C:\Users\<name>\AppData\...,
/home/<name>/...). So the home folder is written as "~", or "%USERPROFILE%"
for a Windows one, wherever it appears in a line: the rest of the path stays,
which is all a log is read for.

The home folders are the environment's: HOME (and where it really is, past
any links) and, on Windows, USERPROFILE and HOMEDRIVE + HOMEPATH. A home
folder matches with either slash, and, for a Windows one (or on Windows and
macOS, whose file systems ignore case), in any case; only a whole folder
name matches (/home/bob is not the start of /home/bobby). A console has no
home folder: nothing changes there.

debug.log_full_paths (config.toml) writes the log's paths whole, for a
developer's own debugging; a log written that way is not one to post. What
goes to the site has the home folder taken out all the same.

Anything new that writes a log line goes through platform_log or the
game's error log (errors.c), which both use log_paths_redact_line: never a
log file of its own.
*/

#ifndef LOG_PATHS_H
#define LOG_PATHS_H

#include <stddef.h>
#include <string.h>

#if defined(HALO_CONSOLE) || defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE) || \
	defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE)
/* (a console has no home folder: nothing to take out, and nothing of
log_paths.c to build) */
#define LOG_PATHS_NO_HOME 1

static inline size_t log_paths_home_at(const char *text, size_t length, size_t position, const char **placeholder)
{
	(void)text;
	(void)length;
	(void)position;
	(void)placeholder;
	return 0;
}

static inline size_t log_paths_redact(const char *text, size_t length, char *out, size_t size)
{
	size_t written = length < size ? length : size ? size - 1 : 0;

	if (size)
	{
		memcpy(out, text, written);
		out[written] = 0;
	}
	return written;
}

static inline void log_paths_redact_line(char *line, size_t size)
{
	(void)line;
	(void)size;
}

static inline void log_paths_mask(void *data, size_t size)
{
	(void)data;
	(void)size;
}

static inline int log_paths_whole(void)
{
	return 0;
}

#else

/* the length of a home folder at text[position] (the whole text being
length bytes), 0 for none, with what is written for it in *placeholder */
size_t log_paths_home_at(const char *text, size_t length, size_t position, const char **placeholder);

/* text with every home folder in it written as its placeholder: its length,
written to out (size bytes, NUL terminated; 3 * length + 1 is enough) */
size_t log_paths_redact(const char *text, size_t length, char *out, size_t size);

/* a log line (NUL terminated, in a buffer of size bytes) as the log may
show it, in place: its home folders written as placeholders, unless
debug.log_full_paths is on (cut to fit, should a placeholder be longer) */
void log_paths_redact_line(char *line, size_t size);

/* binary data (a minidump) with the account name of every home folder in it,
8-bit or UTF-16, overwritten with '_' in place: its length is kept */
void log_paths_mask(void *data, size_t size);

/* whether the log writes paths whole (debug.log_full_paths) */
int log_paths_whole(void);

#endif

#endif
