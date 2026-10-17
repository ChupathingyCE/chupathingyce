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
	(void)path;
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
	else if (argc >= 3 && !strcmp(argv[1], "answer"))
	{
		config_new_defaults_answer(!strcmp(argv[2], "new"));
		return 0;
	}
	else if (argc < 2 || strcmp(argv[1], "get"))
	{
		fprintf(stderr, "usage: port_config_test get <setting>... | write <setting> <value> | pending | answer <new|keep>\n");
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
