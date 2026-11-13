/*
PORT_CONFIG_TEST.C

Runs port/linux/src/port_config.c on its own, for tools/test_port_config.py:
the folder in PORT_CONFIG_TEST_FOLDER stands for the executable's, where
config.toml is read and written. SDL's few functions are stood in for with
the C library (the test's SDL3/SDL.h and platform.h declare them).

	port_config_test get <setting>...        prints "<setting>=<value>" each
	port_config_test write <setting> <value> config_write, then as get
	port_config_test pending                 prints "pending=<count>" and the
	                                         changes the player would be asked about
	port_config_test answer <new|keep>       config_new_defaults_answer
	port_config_test menus_retired           prints "menus_retired=<0|1>"
	                                         (config_menus_retired_pending)
	port_config_test menus_retired_seen      config_menus_retired_seen, then
	                                         as menus_retired
	port_config_test duplicates              a setting or environment variable
	                                         named twice ("duplicates=none")
*/

#include "platform.h"
#include "port_config.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	fputs("log: ", stdout);
	vprintf(format, arguments);
	fputc('\n', stdout);
	va_end(arguments);
}

const char *SDL_GetBasePath(void)
{
	static char base[1024];

	snprintf(base, sizeof(base), "%s/", getenv("PORT_CONFIG_TEST_FOLDER"));
	return base;
}

bool SDL_CreateDirectory(const char *path)
{
	/* (every missing folder along it, as SDL makes them: a packaged
	install's folder in the save root's place) */
	char partial[1024];
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
	mkdir(partial, 0755);
	return true;
}

void *SDL_LoadFile(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	char *data;
	long length;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)length + 1);
	if (fread(data, 1, (size_t)length, file) != (size_t)length)
		length = 0;
	fclose(file);
	*size = (size_t)length;
	return data;
}

SDL_IOStream *SDL_IOFromFile(const char *file, const char *mode)
{
	return (SDL_IOStream *)fopen(file, mode);
}

size_t SDL_WriteIO(SDL_IOStream *context, const void *data, size_t size)
{
	return fwrite(data, 1, size, (FILE *)context);
}

bool SDL_FlushIO(SDL_IOStream *context)
{
	return fflush((FILE *)context) == 0 && fsync(fileno((FILE *)context)) == 0;
}

bool SDL_CloseIO(SDL_IOStream *context)
{
	return fclose((FILE *)context) == 0;
}

bool SDL_RenamePath(const char *old_path, const char *new_path)
{
	return rename(old_path, new_path) == 0;
}

bool SDL_RemovePath(const char *path)
{
	return remove(path) == 0;
}

bool SDL_SaveFile(const char *path, const void *data, size_t size)
{
	FILE *file = fopen(path, "wb");
	int written;

	if (!file)
		return false;
	written = fwrite(data, 1, size, file) == size;
	return fclose(file) == 0 && written;
}

bool SDL_GetPathInfo(const char *path, SDL_PathInfo *info)
{
	struct stat status;

	if (stat(path, &status) != 0)
		return false;
	if (info)
	{
		info->type = S_ISDIR(status.st_mode) ? SDL_PATHTYPE_DIRECTORY : S_ISREG(status.st_mode) ? SDL_PATHTYPE_FILE :
			SDL_PATHTYPE_OTHER;
		info->size = status.st_size;
		info->modify_time = status.st_mtime;
	}
	return true;
}

int SDL_strncasecmp(const char *first, const char *second, size_t length)
{
	return strncasecmp(first, second, length);
}

void SDL_free(void *memory)
{
	free(memory);
}

int main(int argc, char **argv)
{
	int index = 2;
	char text[600];

	if (argc >= 4 && !strcmp(argv[1], "write"))
	{
		if (!config_write(argv[2], argv[3]))
			printf("write failed\n");
		index = 2;
		argc = 3;
	}
	else if (argc >= 2 && !strcmp(argv[1], "pending"))
	{
		char changes[1024];
		int count = config_new_defaults_pending(changes, sizeof(changes));

		printf("pending=%d\n", count);
		if (count)
			printf("changes=%s\n", changes);
		return 0;
	}
	else if (argc >= 2 && (!strcmp(argv[1], "menus_retired") || !strcmp(argv[1], "menus_retired_seen")))
	{
		if (!strcmp(argv[1], "menus_retired_seen"))
			config_menus_retired_seen();
		printf("menus_retired=%d\n", config_menus_retired_pending());
		config_text("display.menus", text, sizeof(text));
		printf("display.menus=%s\n", text);
		return 0;
	}
	else if (argc >= 3 && !strcmp(argv[1], "bench"))
	{
		/* config_changes called as the menus' glyph checks call it */
		unsigned long calls = strtoul(argv[2], NULL, 10), call, sum = 0;
		struct timespec begin, end;

		config_text("audio.effects_volume", text, sizeof(text));
		clock_gettime(CLOCK_MONOTONIC, &begin);
		for (call = 0; call < calls; call++)
			sum += config_changes();
		clock_gettime(CLOCK_MONOTONIC, &end);
		printf("bench_ns_per_call=%.2f sum=%lu\n", ((end.tv_sec - begin.tv_sec) * 1e9 + (end.tv_nsec - begin.tv_nsec)) / calls, sum);
		return 0;
	}
	else if (argc >= 3 && !strcmp(argv[1], "follow"))
	{
		/* config.toml edited while config_changes is called in a loop (as
		the draw code calls it): how long until it moves, in milliseconds */
		struct timespec begin, now;
		unsigned long first;
		double wait;
		FILE *file;

		config_text("audio.effects_volume", text, sizeof(text));
		first = config_changes();
		file = fopen(argv[2], "w");
		if (!file)
			return 2;
		fputs("[audio]\neffects_volume = 0.123\n", file);
		fclose(file);
		clock_gettime(CLOCK_MONOTONIC, &begin);
		do
		{
			usleep(1000);
			clock_gettime(CLOCK_MONOTONIC, &now);
			wait = (now.tv_sec - begin.tv_sec) * 1e3 + (now.tv_nsec - begin.tv_nsec) / 1e6;
		} while (config_changes() == first && wait < 5000.0);
		printf("follow_ms=%.0f moved=%d\n", wait, config_changes() != first);
		return 0;
	}
	else if (argc >= 2 && !strcmp(argv[1], "duplicates"))
	{
		char name[128];

		if (config_duplicate_setting(name, sizeof(name)))
			printf("duplicate=%s\n", name);
		else
			printf("duplicates=none\n");
		return 0;
	}
	else if (argc >= 3 && !strcmp(argv[1], "answer"))
	{
		config_new_defaults_answer(!strcmp(argv[2], "new"));
		return 0;
	}
	else if (argc < 2 || strcmp(argv[1], "get"))
	{
		fprintf(stderr, "usage: port_config_test get <setting>... | write <setting> <value> | pending | answer <new|keep> | menus_retired | menus_retired_seen\n");
		return 2;
	}
	for (; index < argc; index++)
	{
		if (config_text(argv[index], text, sizeof(text)))
			printf("%s=%s\n", argv[index], text);
		else
			printf("%s: no such setting\n", argv[index]);
	}
	return 0;
}
