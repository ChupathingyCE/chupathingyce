/*
SDL_HEADLESS.C

The few SDL functions the platform units the server shares with the game
call (port_config.c, menu_files.c, browser.c, dsound_sdl.c, gl_functions.c),
for a server built without SDL (tools/server_build.py): files and folders
with the C library, and no audio device or OpenGL to open. Each behaves as
SDL 3's does for what those units ask of it. Built with the host's ABI, as
SDL is; only SDL's headers are needed, for the declarations.
*/

#include <SDL3/SDL.h>

#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

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

/* the folder, and the ones above it that it needs */
bool SDL_CreateDirectory(const char *path)
{
	char partial[PATH_MAX];
	size_t index;

	snprintf(partial, sizeof(partial), "%s", path);
	for (index = 1; partial[index]; index++)
	{
		if (partial[index] == '/')
		{
			partial[index] = 0;
			mkdir(partial, 0755);
			partial[index] = '/';
		}
	}
	if (mkdir(partial, 0755) != 0 && errno != EEXIST)
	{
		headless_set_error(strerror(errno));
		return false;
	}
	return true;
}

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

/* ---------- no devices */

SDL_FunctionPointer SDL_GL_GetProcAddress(const char *name)
{
	(void)name;
	headless_set_error("the server has no OpenGL");
	return NULL;
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
