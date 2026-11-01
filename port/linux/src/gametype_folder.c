/*
GAMETYPE_FOLDER.C

The player's game type files (gametype_folder.h).
*/

#include "gametype_folder.h"
#include "posix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXTENSION ".toml"

/* a file's path in the folder: 0 if it does not fit or the name is not one */
static int file_path(const char *folder, const char *name, const char *suffix, char *path, size_t size)
{
	int length;

	if (!folder || !*folder || !gametype_folder_name_valid(name))
		return 0;
	length = snprintf(path, size, "%s/%s%s%s", folder, name, EXTENSION, suffix);
	return length > 0 && (size_t)length < size;
}

static int compare_names(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

int gametype_folder_name_valid(const char *name)
{
	size_t index;

	if (!name || !*name || strlen(name) >= GAMETYPE_FOLDER_NAME_SIZE)
		return 0;
	for (index = 0; name[index]; index++)
	{
		char character = name[index];
		int alphanumeric = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');

		if (!alphanumeric && (!index || (character != '_' && character != '-')))
			return 0;
	}
	return 1;
}

int gametype_folder_list(const char *folder, char names[][GAMETYPE_FOLDER_NAME_SIZE], int maximum)
{
	void *directory;
	char entry[256];
	int count = 0;

	if (!folder || !*folder || maximum <= 0)
		return 0;
	directory = posix_directory_open(folder);
	if (!directory)
		return 0;
	while (count < maximum && posix_directory_next(directory, entry, sizeof(entry)))
	{
		size_t length = strlen(entry);

		if (length <= strlen(EXTENSION) || strcmp(entry + length - strlen(EXTENSION), EXTENSION))
			continue;
		entry[length - strlen(EXTENSION)] = 0;
		if (!gametype_folder_name_valid(entry))
			continue;
		snprintf(names[count++], GAMETYPE_FOLDER_NAME_SIZE, "%s", entry);
	}
	posix_directory_close(directory);
	qsort(names, (size_t)count, GAMETYPE_FOLDER_NAME_SIZE, compare_names);
	return count;
}

int gametype_folder_read(const char *folder, const char *name, char *text, int size)
{
	char path[1024];
	FILE *file;
	size_t count;

	if (size <= 0)
		return -1;
	text[0] = 0;
	if (!file_path(folder, name, "", path, sizeof(path)))
		return -1;
	file = fopen(path, "rb");
	if (!file)
		return -1;
	count = fread(text, 1, (size_t)size, file);
	fclose(file);
	if (count >= (size_t)size)
	{
		text[0] = 0;
		return -1;
	}
	text[count] = 0;
	return (int)count;
}

int gametype_folder_write(const char *folder, const char *name, const char *text)
{
	char path[1024];
	char temporary[1024];
	size_t length = strlen(text);
	FILE *file;

	if (!file_path(folder, name, "", path, sizeof(path)) || !file_path(folder, name, ".new", temporary,
		sizeof(temporary)))
	{
		return 0;
	}
	posix_make_directory(folder);
	file = fopen(temporary, "wb");
	if (!file)
		return 0;
	if (fwrite(text, 1, length, file) != length || fflush(file))
	{
		fclose(file);
		remove(temporary);
		return 0;
	}
	if (fclose(file))
	{
		remove(temporary);
		return 0;
	}
	if (rename(temporary, path))
	{
#ifdef _WIN32
		/* (Windows' rename does not replace a file) */
		if (!remove(path) && !rename(temporary, path))
			return 1;
#endif
		remove(temporary);
		return 0;
	}
	return 1;
}

int gametype_folder_remove(const char *folder, const char *name)
{
	char path[1024];

	return file_path(folder, name, "", path, sizeof(path)) && !remove(path);
}
