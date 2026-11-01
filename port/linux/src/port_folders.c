/*
PORT_FOLDERS.C

The game's own folders beside the maps and the saves (port_folders.h).
*/

#include "port_folders.h"

#include <stdio.h>
#include <string.h>

static const char *const folder_names[k_port_folder_count] = {
	"data",
	"logs",
	"cache",
	"private",
	"gametypes",
};

static int is_set(const char *value)
{
	return value && *value;
}

/* the path without its trailing separators (but a root's own) */
static int copy_trimmed(char *path, unsigned long size, const char *value)
{
	int length = snprintf(path, size, "%s", value);

	if (length <= 0 || (unsigned long)length >= size)
		return 0;
	while (length > 1 && (path[length - 1] == '/' || path[length - 1] == '\\') && path[length - 2] != ':')
		path[--length] = '\0';
	return 1;
}

const char *port_folder_name(enum port_folder folder)
{
	return (int)folder >= 0 && (int)folder < k_port_folder_count ? folder_names[folder] : "";
}

int port_folder_in_save_root(enum port_folder folder)
{
	return folder == _port_folder_cache || folder == _port_folder_private || folder == _port_folder_gametypes;
}

int port_folder_path(enum port_folder folder, const char *override, const char *data_root, const char *save_root,
	char *path, unsigned long size)
{
	const char *root = port_folder_in_save_root(folder) ? save_root : data_root;
	char trimmed[1024];
	int length;

	if (size)
		path[0] = '\0';
	if ((int)folder < 0 || (int)folder >= k_port_folder_count)
		return 0;
	if ((folder == _port_folder_logs || folder == _port_folder_cache) && is_set(override))
		return copy_trimmed(path, size, override);
	if (!is_set(root) || !copy_trimmed(trimmed, sizeof(trimmed), root))
		return 0;
	length = snprintf(path, size, "%s%s%s", trimmed,
		trimmed[strlen(trimmed) - 1] == '/' || trimmed[strlen(trimmed) - 1] == '\\' ? "" : "/", folder_names[folder]);
	if (length <= 0 || (unsigned long)length >= size)
	{
		if (size)
			path[0] = '\0';
		return 0;
	}
	return 1;
}

int port_folder_absolute(const char *path)
{
	if (!path)
		return 0;
	if (path[0] == '/' || path[0] == '\\')
		return 1;
	return ((path[0] >= 'a' && path[0] <= 'z') || (path[0] >= 'A' && path[0] <= 'Z')) && path[1] == ':';
}

/* whether a relative path has a part that is .. (and so could leave its
folder) */
static int leaves_folder(const char *value)
{
	const char *part = value;

	for (;;)
	{
		size_t length = strcspn(part, "/\\");

		if (length == 2 && part[0] == '.' && part[1] == '.')
			return 1;
		if (!part[length])
			return 0;
		part += length + 1;
	}
}

int port_folder_file(const char *folder, const char *value, char *path, unsigned long size)
{
	int length;

	if (size)
		path[0] = '\0';
	if (!is_set(value))
		return 0;
	if (port_folder_absolute(value))
		return copy_trimmed(path, size, value);
	if (!is_set(folder) || leaves_folder(value))
		return 0;
	length = snprintf(path, size, "%s/%s", folder, value);
	if (length <= 0 || (unsigned long)length >= size)
	{
		if (size)
			path[0] = '\0';
		return 0;
	}
	return 1;
}
