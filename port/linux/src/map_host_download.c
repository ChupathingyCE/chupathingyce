/*
MAP_HOST_DOWNLOAD.C

The rules of a map downloaded from a game's host (map_host_download.h).
*/

#include "map_host_download.h"
#include "map_download.h"
#include "map_quick_check.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------- private code */

static int same_word(const char *text, const char *word)
{
	for (; *text && *word; text++, word++)
	{
		int a = *text >= 'A' && *text <= 'Z' ? *text + ('a' - 'A') : *text;

		if (a != *word)
			return 0;
	}
	return !*text && !*word;
}

static int refuse(char *error, int error_size, const char *format, ...)
{
	va_list arguments;

	if (error && error_size > 0)
	{
		va_start(arguments, format);
		vsnprintf(error, (size_t)error_size, format, arguments);
		va_end(arguments);
	}
	return 0;
}

struct quarantined
{
	const struct map_host_files *files;
	const char *path;
};

static int quarantined_read(void *context, unsigned int offset, void *buffer, unsigned int size)
{
	const struct quarantined *file = (const struct quarantined *)context;

	return file->files->read(file->files->context, file->path, offset, buffer, size);
}

static void map_path(const char *file, char *path)
{
	snprintf(path, MAP_HOST_PATH_SIZE, "%s\\%s.map", MAP_HOST_MAPS_FOLDER, file);
}

/* ---------- public code */

int map_host_setting_from(const char *text)
{
	if (text && same_word(text, "always"))
		return MAP_HOST_DOWNLOADS_ALWAYS;
	if (text && same_word(text, "ask"))
		return MAP_HOST_DOWNLOADS_ASK;
	return MAP_HOST_DOWNLOADS_OFF;
}

const char *map_host_setting_word(int setting)
{
	return setting == MAP_HOST_DOWNLOADS_ALWAYS ? "always" : setting == MAP_HOST_DOWNLOADS_ASK ? "ask" : "off";
}

int map_host_step(int setting, int state, int source, int blocked)
{
	/* (the host last: after the repositories failed, never after the
	blacklist refused the map, never after the host itself) */
	if (state != MAP_DOWNLOAD_FAILED || blocked || source == MAP_DOWNLOAD_SOURCE_HOST)
		return MAP_HOST_STEP_NONE;
	if (setting == MAP_HOST_DOWNLOADS_ALWAYS)
		return MAP_HOST_STEP_START;
	if (setting == MAP_HOST_DOWNLOADS_ASK)
		return MAP_HOST_STEP_ASK;
	return MAP_HOST_STEP_NONE;
}

void map_host_quarantine_paths(const char *name, char *part, char *whole)
{
	snprintf(part, MAP_HOST_PATH_SIZE, "%s\\%s.map.part", MAP_HOST_QUARANTINE_FOLDER, name);
	snprintf(whole, MAP_HOST_PATH_SIZE, "%s\\%s.map", MAP_HOST_QUARANTINE_FOLDER, name);
}

int map_host_install(const struct map_host_files *files, const char *name, unsigned int version,
	const unsigned char *offered, unsigned int limit, char *installed, unsigned char *sha256, unsigned char *map_hash,
	int *blocked, char *error, int error_size)
{
	char part[MAP_HOST_PATH_SIZE], whole[MAP_HOST_PATH_SIZE], target[MAP_HOST_PATH_SIZE];
	char base[MAP_HOST_NAME_SIZE], reason[160];
	unsigned char header[MAP_ARCHIVE_HEADER_SIZE];
	unsigned int prefix = 0, hash_version, checksum;
	struct quarantined file;
	long long size;
	int hashed;
	size_t index;

	*blocked = 0;
	installed[0] = 0;
	if (error && error_size > 0)
		error[0] = 0;
	/* (the host's name must be one we keep before it names any file) */
	if (!map_quick_check_name(name) || strlen(name) >= MAP_HOST_NAME_SIZE)
		return refuse(error, error_size, "its name is not a map file's name we keep");
	map_host_quarantine_paths(name, part, whole);
	/* (a hashed name's map is named for its base inside, and kept by it,
	as the repositories' are) */
	hashed = map_archive_hashed_name(name, base, (int)sizeof(base), &prefix);
	if (!hashed)
		snprintf(base, sizeof(base), "%s", name);
	for (index = 0; base[index]; index++)
		base[index] = (char)(base[index] >= 'A' && base[index] <= 'Z' ? base[index] + ('a' - 'A') : base[index]);
	size = files->size(files->context, whole);
	if (size < 0)
	{
		refuse(error, error_size, "it isn't in the quarantine folder");
		goto refused;
	}
	file.files = files;
	file.path = whole;
	if ((unsigned long long)size > 0xffffffffull ||
		!map_quick_check(quarantined_read, &file, (unsigned int)size, hashed ? base : name, limit, error, error_size))
	{
		if (error && error_size > 0 && !error[0])
			refuse(error, error_size, "it is larger than any map");
		goto refused;
	}
	if (!files->hashes(files->context, whole, sha256, map_hash))
	{
		refuse(error, error_size, "it could not be read back");
		goto refused;
	}
	if (memcmp(map_hash, offered, MAP_ARCHIVE_HASH_SIZE))
	{
		refuse(error, error_size, "it is not the file the host offered");
		goto refused;
	}
	hash_version = map_archive_hash_version(map_hash);
	if (!files->read(files->context, whole, 0, header, sizeof(header)))
	{
		refuse(error, error_size, "it could not be read back");
		goto refused;
	}
	checksum = map_archive_header_checksum(header);
	if (version && !map_archive_version_matches(version, checksum, hash_version))
	{
		refuse(error, error_size, "its version is %08x, the host's %08x: another version of the map", hash_version,
			version);
		goto refused;
	}
	if (hashed && ((unsigned int)map_hash[0] << 24 | (unsigned int)map_hash[1] << 16 | (unsigned int)map_hash[2] << 8 |
		(unsigned int)map_hash[3]) != prefix)
	{
		refuse(error, error_size, "its hash doesn't begin as its name says");
		goto refused;
	}
	reason[0] = 0;
	if (files->blocked && files->blocked(files->context, sha256, map_hash, reason, (int)sizeof(reason)))
	{
		*blocked = 1;
		refuse(error, error_size, "it is on the blacklist: %s", reason);
		goto refused;
	}
	/* (into maps_ce, never over a file that is there: its own name's file,
	else beside another version of it) */
	snprintf(installed, MAP_HOST_NAME_SIZE, "%s", hashed ? base : name);
	map_path(installed, target);
	if (files->move(files->context, whole, target))
		return 1;
	if (hashed)
		snprintf(installed, MAP_HOST_NAME_SIZE, "%s", name);
	else
		snprintf(installed, MAP_HOST_NAME_SIZE, "%s_%02x%02x%02x%02x", name, map_hash[0], map_hash[1], map_hash[2],
			map_hash[3]);
	map_path(installed, target);
	if (files->move(files->context, whole, target))
		return 1;
	installed[0] = 0;
	refuse(error, error_size, "maps_ce/%s.map could not be put in place (is it there already?)", name);
refused:
	files->remove(files->context, whole);
	files->remove(files->context, part);
	return 0;
}
