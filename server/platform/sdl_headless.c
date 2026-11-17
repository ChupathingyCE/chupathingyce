/*
SDL_HEADLESS.C

The few SDL functions the platform units the server shares with the game
call (port_config.c, menu_files.c, browser.c, dsound_sdl.c, gl_functions.c),
for a server built without SDL (tools/server_build.py): files and folders
with the C library, and no audio device or OpenGL to open. Each behaves as
SDL 3's does for what those units ask of it. Built with the host's ABI, as
SDL is; only SDL's headers are needed, for the declarations. On Windows
(the Windows server, tools/windows_build.py) the folders are the C
runtime's and a search is FindFirstFile's.
*/

#include <SDL3/SDL.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
/* (port/windows/include/posix: clock_gettime, readlink of /proc/self/exe) */
#include <unistd.h>
#define strncasecmp _strnicmp
#define mkdir(path, mode) _mkdir(path)
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif
#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif
#else
#include <glob.h>
#include <strings.h>
#include <unistd.h>
#endif

static char headless_error[128] = "";

static void headless_set_error(const char *text)
{
	snprintf(headless_error, sizeof(headless_error), "%s", text);
}

const char *SDL_GetError(void)
{
	return headless_error;
}

void SDL_free(void *memory)
{
	free(memory);
}

int SDL_strncasecmp(const char *first, const char *second, size_t length)
{
	return strncasecmp(first, second, length);
}

bool SDL_SetHint(const char *name, const char *value)
{
	(void)name;
	(void)value;
	return true;
}

/* ---------- files */

/* the whole file, with a NUL after it (as SDL's), or NULL */
void *SDL_LoadFile(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	char *data = NULL;
	size_t length = 0, capacity = 0;

	if (size)
		*size = 0;
	if (!file)
	{
		headless_set_error(strerror(errno));
		return NULL;
	}
	for (;;)
	{
		size_t got;

		if (length + 4096 + 1 > capacity)
		{
			char *grown;

			capacity = capacity ? capacity * 2 : 65536;
			grown = realloc(data, capacity);
			if (!grown)
			{
				free(data);
				fclose(file);
				headless_set_error("out of memory");
				return NULL;
			}
			data = grown;
		}
		got = fread(data + length, 1, capacity - length - 1, file);
		length += got;
		if (got == 0)
			break;
	}
	if (ferror(file))
	{
		free(data);
		fclose(file);
		headless_set_error("read error");
		return NULL;
	}
	fclose(file);
	data[length] = 0;
	if (size)
		*size = length;
	return data;
}

bool SDL_SaveFile(const char *path, const void *data, size_t size)
{
	FILE *file = fopen(path, "wb");
	bool written;

	if (!file)
	{
		headless_set_error(strerror(errno));
		return false;
	}
	written = fwrite(data, 1, size, file) == size;
	return fclose(file) == 0 && written;
}

/* a file written as a stream (port_config.c's config.toml: to a new file,
flushed to the disk, then renamed over the old one) */
SDL_IOStream *SDL_IOFromFile(const char *path, const char *mode)
{
	FILE *file = fopen(path, mode);

	if (!file)
		headless_set_error(strerror(errno));
	return (SDL_IOStream *)file;
}

size_t SDL_WriteIO(SDL_IOStream *stream, const void *data, size_t size)
{
	return fwrite(data, 1, size, (FILE *)stream);
}

/* (as SDL's: the C library's buffer, then the system's to the disk) */
bool SDL_FlushIO(SDL_IOStream *stream)
{
	FILE *file = (FILE *)stream;

	if (fflush(file) != 0)
		return false;
#ifdef _WIN32
	return _commit(_fileno(file)) == 0;
#else
	return fsync(fileno(file)) == 0;
#endif
}

bool SDL_CloseIO(SDL_IOStream *stream)
{
	return !stream || fclose((FILE *)stream) == 0;
}

/* (over a file that is there, as SDL's: MoveFileEx on Windows, here also
written through to the disk before it returns) */
bool SDL_RenamePath(const char *old_path, const char *new_path)
{
#ifdef _WIN32
	if (!MoveFileExA(old_path, new_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		headless_set_error("cannot rename the file");
		return false;
	}
#else
	if (rename(old_path, new_path) != 0)
	{
		headless_set_error(strerror(errno));
		return false;
	}
#endif
	return true;
}

bool SDL_RemovePath(const char *path)
{
	if (remove(path) != 0)
	{
		headless_set_error(strerror(errno));
		return false;
	}
	return true;
}

/* whether the path is there, and what it is */
bool SDL_GetPathInfo(const char *path, SDL_PathInfo *info)
{
	struct stat status;

	if (stat(path, &status) != 0)
	{
		headless_set_error(strerror(errno));
		return false;
	}
	if (info)
	{
		memset(info, 0, sizeof(*info));
		info->type = S_ISREG(status.st_mode) ? SDL_PATHTYPE_FILE :
			S_ISDIR(status.st_mode) ? SDL_PATHTYPE_DIRECTORY : SDL_PATHTYPE_OTHER;
		info->size = (Uint64)status.st_size;
		info->modify_time = (SDL_Time)status.st_mtime * 1000000000;
		info->create_time = info->modify_time;
		info->access_time = (SDL_Time)status.st_atime * 1000000000;
	}
	return true;
}

/* the executable's folder, with a / after it */
const char *SDL_GetBasePath(void)
{
	static char base[PATH_MAX + 1];

	if (!base[0])
	{
		ssize_t length = readlink("/proc/self/exe", base, sizeof(base) - 2);
		char *slash;

		if (length <= 0)
		{
			headless_set_error("cannot find the executable");
			return NULL;
		}
		base[length] = 0;
		slash = strrchr(base, '/');
		if (slash)
			slash[1] = 0;
	}
	return base;
}

#ifdef _WIN32
#define SEPARATOR_BACKSLASH 1
#else
#define SEPARATOR_BACKSLASH 0
#endif

/* the folder, and the ones above it that it needs */
bool SDL_CreateDirectory(const char *path)
{
	char partial[PATH_MAX];
	size_t index;

	snprintf(partial, sizeof(partial), "%s", path);
	for (index = 1; partial[index]; index++)
	{
		char separator = partial[index];

		/* (on Windows a backslash too, and not the one after a drive letter) */
		if (separator == '/' || (separator == '\\' && SEPARATOR_BACKSLASH))
		{
			if (index == 2 && partial[1] == ':')
				continue;
			partial[index] = 0;
			mkdir(partial, 0755);
			partial[index] = separator;
		}
	}
	if (mkdir(partial, 0755) != 0 && errno != EEXIST)
	{
		headless_set_error(strerror(errno));
		return false;
	}
	return true;
}

#ifdef _WIN32
struct found_names
{
	char **names;
	size_t used;
	size_t capacity;
	size_t bytes;
};

static void found_add(struct found_names *found, const char *name)
{
	if (found->used == found->capacity)
	{
		size_t capacity = found->capacity ? found->capacity * 2 : 32;
		char **grown = realloc(found->names, capacity * sizeof(char *));

		if (!grown)
			return;
		found->names = grown;
		found->capacity = capacity;
	}
	found->names[found->used] = _strdup(name);
	if (!found->names[found->used])
		return;
	found->bytes += sizeof(char *) + strlen(name) + 1;
	found->used++;
}

/* the names in path/folder that match pattern (no folder in it), each as
folder + name; folders only, or anything */
static void found_search(struct found_names *found, const char *path, const char *folder, const char *pattern,
	int folders_only)
{
	char full[PATH_MAX];
	WIN32_FIND_DATAA entry;
	HANDLE search;

	snprintf(full, sizeof(full), "%s/%s%s", path, folder, pattern);
	search = FindFirstFileA(full, &entry);
	if (search == INVALID_HANDLE_VALUE)
		return;
	do
	{
		char name[PATH_MAX];

		if (!strcmp(entry.cFileName, ".") || !strcmp(entry.cFileName, ".."))
			continue;
		if (folders_only && !(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;
		snprintf(name, sizeof(name), "%s%s", folder, entry.cFileName);
		found_add(found, name);
	}
	while (FindNextFileA(search, &entry));
	FindClose(search);
}

/* the names under path that match pattern (relative to path; * does not
cross a /), as one block SDL_free frees: the pointers, NULL, the names.
FindFirstFile matches one folder's names, so a pattern with folders in it
("*" + "/" + "*.xml", as menu_files.c asks) is matched a folder at a time */
char **SDL_GlobDirectory(const char *path, const char *pattern, SDL_GlobFlags flags, int *count)
{
	struct found_names found = { NULL, 0, 0, sizeof(char *) };
	struct found_names folders = { NULL, 0, 0, sizeof(char *) };
	char first[PATH_MAX];
	const char *slash = strchr(pattern, '/');
	size_t index;
	char **names;
	char *text;

	(void)flags;
	if (count)
		*count = 0;
	if (!slash)
		found_search(&found, path, "", pattern, 0);
	else
	{
		/* (one folder deep, as the units ask: "folder pattern/name pattern") */
		snprintf(first, sizeof(first), "%.*s", (int)(slash - pattern), pattern);
		found_search(&folders, path, "", first, 1);
		for (index = 0; index < folders.used; index++)
		{
			char folder[PATH_MAX];

			snprintf(folder, sizeof(folder), "%s/", folders.names[index]);
			found_search(&found, path, folder, slash + 1, 0);
			free(folders.names[index]);
		}
		free(folders.names);
	}
	if (!found.used)
	{
		free(found.names);
		return NULL;
	}
	names = malloc(found.bytes);
	if (names)
	{
		text = (char *)(names + found.used + 1);
		for (index = 0; index < found.used; index++)
		{
			names[index] = text;
			strcpy(text, found.names[index]);
			text += strlen(found.names[index]) + 1;
		}
		names[found.used] = NULL;
		if (count)
			*count = (int)found.used;
	}
	for (index = 0; index < found.used; index++)
		free(found.names[index]);
	free(found.names);
	return names;
}
#else
/* the names under path that match pattern (relative to path; * does not
cross a /), as one block SDL_free frees: the pointers, NULL, the names */
char **SDL_GlobDirectory(const char *path, const char *pattern, SDL_GlobFlags flags, int *count)
{
	char full[PATH_MAX];
	glob_t found;
	size_t prefix = strlen(path) + 1, bytes, index;
	char **names;
	char *text;

	(void)flags;
	if (count)
		*count = 0;
	snprintf(full, sizeof(full), "%s/%s", path, pattern);
	memset(&found, 0, sizeof(found));
	if (glob(full, 0, NULL, &found) != 0)
	{
		globfree(&found);
		return NULL;
	}
	bytes = (found.gl_pathc + 1) * sizeof(char *);
	for (index = 0; index < found.gl_pathc; index++)
		bytes += strlen(found.gl_pathv[index]) + 1;
	names = malloc(bytes);
	if (!names)
	{
		globfree(&found);
		return NULL;
	}
	text = (char *)(names + found.gl_pathc + 1);
	for (index = 0; index < found.gl_pathc; index++)
	{
		const char *name = found.gl_pathv[index];

		name += strlen(name) >= prefix ? prefix : strlen(name);
		names[index] = text;
		strcpy(text, name);
		text += strlen(name) + 1;
	}
	names[found.gl_pathc] = NULL;
	if (count)
		*count = (int)found.gl_pathc;
	globfree(&found);
	return names;
}
#endif

/* ---------- no devices */

SDL_FunctionPointer SDL_GL_GetProcAddress(const char *name)
{
	(void)name;
	headless_set_error("the server has no OpenGL");
	return NULL;
}

bool SDL_GL_ExtensionSupported(const char *extension)
{
	(void)extension;
	return false;
}

SDL_AudioDeviceID *SDL_GetAudioPlaybackDevices(int *count)
{
	if (count)
		*count = 0;
	headless_set_error("the server plays no sound");
	return NULL;
}

const char *SDL_GetAudioDeviceName(SDL_AudioDeviceID device)
{
	(void)device;
	return NULL;
}

void SDL_DestroyAudioStream(SDL_AudioStream *stream)
{
	(void)stream;
}

/* (milliseconds since the server started, as SDL counts them) */
Uint64 SDL_GetTicks(void)
{
	static struct timespec start;
	struct timespec now;

	if (!start.tv_sec && !start.tv_nsec)
		clock_gettime(CLOCK_MONOTONIC, &start);
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (Uint64)((long long)(now.tv_sec - start.tv_sec) * 1000 +
		(now.tv_nsec - start.tv_nsec) / 1000000);
}

SDL_AudioDeviceID SDL_GetAudioStreamDevice(SDL_AudioStream *stream)
{
	(void)stream;
	return 0;
}

bool SDL_GetAudioDeviceFormat(SDL_AudioDeviceID device, SDL_AudioSpec *specification, int *sample_frames)
{
	(void)device;
	(void)specification;
	(void)sample_frames;
	headless_set_error("the server plays no sound");
	return false;
}

SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID device, const SDL_AudioSpec *specification,
	SDL_AudioStreamCallback callback, void *userdata)
{
	(void)device;
	(void)specification;
	(void)callback;
	(void)userdata;
	headless_set_error("the server plays no sound");
	return NULL;
}

bool SDL_ResumeAudioStreamDevice(SDL_AudioStream *stream)
{
	(void)stream;
	return false;
}

bool SDL_PutAudioStreamData(SDL_AudioStream *stream, const void *buffer, int length)
{
	(void)stream;
	(void)buffer;
	(void)length;
	return false;
}
