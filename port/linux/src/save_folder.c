/*
SAVE_FOLDER.C

The save root's usual place on Linux and Windows, and the one-time copy of
the saves from the folder the game shared with other builds (save_folder.h).
*/

#include "save_folder.h"
#include "posix.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define PATH_SIZE 1024

static int is_set(const char *value)
{
	return value && *value;
}

static char lower(char character)
{
	return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

/* whether name starts with start, but for case */
static int starts_with(const char *name, const char *start)
{
	while (*start)
	{
		if (lower(*name++) != lower(*start++))
			return 0;
	}
	return 1;
}

static int ends_with(const char *name, const char *ending)
{
	size_t name_length = strlen(name), ending_length = strlen(ending);

	return name_length >= ending_length && starts_with(name + name_length - ending_length, ending);
}

static int folder_path(enum save_folder_system system, const char *appdata, const char *data_home,
	const char *home, const char *windows_name, const char *linux_name, char *path, unsigned long size)
{
	int length;

	if (system == _save_folder_windows)
	{
		if (!is_set(appdata))
			return 0;
		length = snprintf(path, size, "%s/%s", appdata, windows_name);
	}
	else if (is_set(data_home))
		length = snprintf(path, size, "%s/%s", data_home, linux_name);
	else if (is_set(home))
		length = snprintf(path, size, "%s/.local/share/%s", home, linux_name);
	else
		return 0;
	return length > 0 && (unsigned long)length < size;
}

int save_folder_default(enum save_folder_system system, const char *appdata, const char *data_home,
	const char *home, char *path, unsigned long size)
{
	return folder_path(system, appdata, data_home, home, "ChupathingyCE", "chupathingyce", path, size);
}

int save_folder_shared(enum save_folder_system system, const char *appdata, const char *data_home,
	const char *home, char *path, unsigned long size)
{
	return folder_path(system, appdata, data_home, home, "halo", "halo-linux", path, size);
}

int save_folder_choose(enum save_folder_system system, const char *override, const char *appdata,
	const char *data_home, const char *home, char *root, unsigned long root_size, char *shared,
	unsigned long shared_size)
{
	if (shared_size)
		shared[0] = '\0';
	if (is_set(override))
	{
		int length = snprintf(root, root_size, "%s", override);

		return length > 0 && (unsigned long)length < root_size;
	}
	if (!save_folder_default(system, appdata, data_home, home, root, root_size))
		return 0;
	if (!save_folder_shared(system, appdata, data_home, home, shared, shared_size) && shared_size)
		shared[0] = '\0';
	return 1;
}

/* ---------- files and folders */

static int exists(const char *path, int *is_directory)
{
	struct posix_file_information information;

	if (posix_stat(path, &information) != 0)
		return 0;
	if (is_directory)
		*is_directory = (information.flags & _posix_file_is_directory) != 0;
	return 1;
}

static int directory_exists(const char *path)
{
	int is_directory;

	return exists(path, &is_directory) && is_directory;
}

static int join(char *path, unsigned long size, const char *folder, const char *name)
{
	int length = snprintf(path, size, "%s/%s", folder, name);

	return length > 0 && (unsigned long)length < size;
}

/* makes path and every missing folder along it; 1 when it is there */
static int make_folders(const char *path)
{
	char partial[PATH_SIZE];
	size_t index;

	snprintf(partial, sizeof(partial), "%s", path);
	for (index = 1; partial[index]; index++)
	{
		if ((partial[index] == '/' || partial[index] == '\\') && partial[index - 1] != ':')
		{
			char separator = partial[index];

			partial[index] = '\0';
			if (!directory_exists(partial))
				posix_make_directory(partial);
			partial[index] = separator;
		}
	}
	if (!directory_exists(partial))
		posix_make_directory(partial);
	return directory_exists(partial);
}

/* removes the folder and everything in it (the copy's own folder, left by
an interrupted copy); links are removed, never followed */
static void remove_tree(const char *path)
{
	void *directory;
	char name[256];
	char child[PATH_SIZE];

	if (posix_is_link(path))
	{
		if (unlink(path) != 0)
			rmdir(path);
		return;
	}
	directory = posix_directory_open(path);
	if (directory)
	{
		while (posix_directory_next(directory, name, sizeof(name)))
		{
			if (!join(child, sizeof(child), path, name))
				continue;
			if (directory_exists(child) && !posix_is_link(child))
				remove_tree(child);
			else if (unlink(child) != 0)
				rmdir(child);
		}
		posix_directory_close(directory);
	}
	rmdir(path);
}

/* copies one file; with by_rename, to a file beside it first, renamed into
place when whole. 1 on success */
static int copy_file(const char *from, const char *to, int by_rename)
{
	char partial[PATH_SIZE];
	char buffer[65536];
	const char *destination = to;
	int input, output, success = 1;

	if (by_rename)
	{
		if (snprintf(partial, sizeof(partial), "%s.copying", to) >= (int)sizeof(partial))
			return 0;
		destination = partial;
	}
	input = open(from, O_RDONLY | O_BINARY);
	if (input < 0)
		return 0;
	/* (the user's alone, as the originals are: the player key among them,
	which a copy readable by others would not be taken as) */
	output = open(destination, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0600);
	if (output < 0)
	{
		close(input);
		return 0;
	}
	for (;;)
	{
		int read_count = (int)read(input, buffer, sizeof(buffer));
		int written = 0;

		if (read_count == 0)
			break;
		if (read_count < 0)
		{
			success = 0;
			break;
		}
		while (written < read_count)
		{
			int count = (int)write(output, buffer + written, (size_t)(read_count - written));

			if (count <= 0)
			{
				success = 0;
				break;
			}
			written += count;
		}
		if (!success)
			break;
	}
	close(input);
	if (close(output) != 0)
		success = 0;
	if (by_rename)
	{
		if (success && rename(partial, to) != 0)
			success = 0;
		if (!success)
			unlink(partial);
	}
	return success;
}

/* whether a file in z/ is one the game rebuilds: the map cache, and its
temporary files */
static int rebuilt_file(const char *name)
{
	return (starts_with(name, "cache") && ends_with(name, ".map")) || starts_with(name, "tmp");
}

/* copies the folder's files and folders into to, which is made; with
merge, to may hold files already, which are kept, and each file is copied
beside its place first. top: from is the shared folder itself, whose z/
is copied without what the game rebuilds (rebuilt_file); in_z: from is that
z/. 1 on success */
static int copy_tree(const char *from, const char *to, int merge, int top, int in_z,
	unsigned long *files_copied)
{
	void *directory;
	char name[256];
	char source[PATH_SIZE];
	char destination[PATH_SIZE];
	int success = 1;

	if (!directory_exists(to) && posix_make_directory(to) != 0)
		return 0;
	directory = posix_directory_open(from);
	if (!directory)
		return 0;
	while (success && posix_directory_next(directory, name, sizeof(name)))
	{
		int is_directory;

		if (!join(source, sizeof(source), from, name) || !join(destination, sizeof(destination), to, name))
		{
			success = 0;
			break;
		}
		/* (links might lead anywhere, even back here: not followed) */
		if (posix_is_link(source) || !exists(source, &is_directory))
			continue;
		if (is_directory)
			success = copy_tree(source, destination, merge, 0, top && lower(name[0]) == 'z' && !name[1],
				files_copied);
		else if (in_z && rebuilt_file(name))
			continue;
		else if (merge && exists(destination, NULL))
			continue;
		else if (copy_file(source, destination, merge))
		{
			if (files_copied)
				(*files_copied)++;
		}
		else
			success = 0;
	}
	posix_directory_close(directory);
	return success;
}

/* whether the save root has a profile: a saved game in u/UDATA
(xbox_xapi.c, XCreateSaveGame) */
static int has_profiles(const char *save_root)
{
	char u[256], udata[256];
	char path[PATH_SIZE], udata_path[PATH_SIZE];
	void *directory;
	int found = 0;

	if (!posix_find_entry_case_insensitive(save_root, "u", u, sizeof(u)) ||
		!join(path, sizeof(path), save_root, u) ||
		!posix_find_entry_case_insensitive(path, "UDATA", udata, sizeof(udata)) ||
		!join(udata_path, sizeof(udata_path), path, udata))
	{
		return 0;
	}
	directory = posix_directory_open(udata_path);
	if (directory)
	{
		found = posix_directory_next(directory, u, sizeof(u));
		posix_directory_close(directory);
	}
	return found;
}

static int write_marker(const char *folder)
{
	static const char text[] =
		"Chupathingy looked once for saves to copy from the save folder it shared\n"
		"with other builds of the game before it had its own. Delete this file to\n"
		"look again.\n";
	char path[PATH_SIZE];
	int file;
	int success;

	if (!join(path, sizeof(path), folder, SAVE_FOLDER_MARKER))
		return 0;
	file = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
	if (file < 0)
		return 0;
	success = write(file, text, sizeof(text) - 1) == (int)(sizeof(text) - 1);
	return close(file) == 0 && success;
}

enum save_folder_copy_result save_folder_copy_once(const char *shared, const char *save_root,
	unsigned long *files_copied)
{
	char marker[PATH_SIZE];
	char copying[PATH_SIZE];

	if (files_copied)
		*files_copied = 0;
	if (!join(marker, sizeof(marker), save_root, SAVE_FOLDER_MARKER))
		return _save_folder_copy_failed;
	if (exists(marker, NULL))
		return _save_folder_copy_done_before;
	if (!is_set(shared) || !strcmp(shared, save_root) || !directory_exists(shared))
	{
		if (!make_folders(save_root) || !write_marker(save_root))
			return _save_folder_copy_failed;
		return _save_folder_copy_nothing_shared;
	}

	if (directory_exists(save_root))
	{
		if (has_profiles(save_root))
		{
			write_marker(save_root);
			return _save_folder_copy_has_profiles;
		}
		/* (what the save root holds is kept; each file is whole or not
		there, and the next start finishes the copy) */
		if (!copy_tree(shared, save_root, 1, 1, 0, files_copied) || !write_marker(save_root))
			return _save_folder_copy_failed;
		return _save_folder_copy_copied;
	}

	/* a new save root: filled beside its place, then renamed into place */
	if (snprintf(copying, sizeof(copying), "%s.copying", save_root) >= (int)sizeof(copying))
		return _save_folder_copy_failed;
	if (exists(copying, NULL))
		remove_tree(copying);
	if (!make_folders(copying) ||
		!copy_tree(shared, copying, 0, 1, 0, files_copied) ||
		!write_marker(copying) ||
		posix_rename_directory(copying, save_root) != 0)
	{
		remove_tree(copying);
		if (files_copied)
			*files_copied = 0;
		return _save_folder_copy_failed;
	}
	return _save_folder_copy_copied;
}
