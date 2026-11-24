/*
DIRECT_JOIN_PLATFORM.C

Joining a game by its address (port/linux/game/direct_join.c), the
platform's part: a host name looked up on a thread of its own (the game's
code starts none), and the addresses joined before, kept in the save
folder's direct_ip.txt on this machine alone, read and written by the save
root's own path (the game's files go by Xbox paths).
*/

#include "platform.h"
#include "posix.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- the command line's address */

int direct_command_line_address(char *text, int size)
{
	const char *environment = getenv("HALO_CONNECT");
	char argument[300];
	int index;

	if (!text || size <= 0)
		return 0;
	text[0] = 0;
	if (environment && *environment)
	{
		snprintf(text, (size_t)size, "%s", environment);
		return 1;
	}
	for (index = 1; posix_command_line_argument(index, argument, sizeof(argument)); index++)
	{
		if (!strcmp(argument, "--connect") && posix_command_line_argument(index + 1, text, (posix_ulong)size))
			return text[0] != 0;
		if (!strncmp(argument, "--connect=", 10) && argument[10])
		{
			snprintf(text, (size_t)size, "%s", argument + 10);
			return 1;
		}
	}
	return 0;
}

/* ---------- a host name looked up */

static pthread_mutex_t lookup_lock = PTHREAD_MUTEX_INITIALIZER;
static struct
{
	int running;
	/* 0 looking, 1 found, -1 not found */
	int result;
	/* bumped by each start: an older lookup's answer is dropped */
	unsigned int generation;
	char host[256];
	/* network byte order */
	posix_ulong address;
} lookup;

static void *lookup_thread(void *context)
{
	char host[256];
	unsigned int generation;
	posix_ulong address;

	(void)context;
	for (;;)
	{
		pthread_mutex_lock(&lookup_lock);
		snprintf(host, sizeof(host), "%s", lookup.host);
		generation = lookup.generation;
		pthread_mutex_unlock(&lookup_lock);
		address = posix_resolve_ipv4(host);
		pthread_mutex_lock(&lookup_lock);
		/* (a newer name asked for while this one was looked up: that one
		next, on this thread) */
		if (generation != lookup.generation)
		{
			pthread_mutex_unlock(&lookup_lock);
			continue;
		}
		lookup.address = address;
		lookup.result = address ? 1 : -1;
		lookup.running = 0;
		pthread_mutex_unlock(&lookup_lock);
		return NULL;
	}
}

int direct_lookup_start(const char *host)
{
	int started = 1;

	if (!host || !*host || strlen(host) >= sizeof(lookup.host))
		return 0;
	pthread_mutex_lock(&lookup_lock);
	lookup.generation++;
	lookup.result = 0;
	lookup.address = 0;
	snprintf(lookup.host, sizeof(lookup.host), "%s", host);
	if (!lookup.running)
	{
		pthread_t thread;

		lookup.running = 1;
		if (pthread_create(&thread, NULL, lookup_thread, NULL))
		{
			lookup.running = 0;
			started = 0;
		}
		else
			pthread_detach(thread);
	}
	pthread_mutex_unlock(&lookup_lock);
	return started;
}

int direct_lookup_result(unsigned char *address)
{
	int result;

	pthread_mutex_lock(&lookup_lock);
	result = lookup.result;
	if (result > 0)
		memcpy(address, &lookup.address, 4);
	pthread_mutex_unlock(&lookup_lock);
	return result;
}

void direct_lookup_cancel(void)
{
	pthread_mutex_lock(&lookup_lock);
	lookup.generation++;
	lookup.result = 0;
	pthread_mutex_unlock(&lookup_lock);
}

/* ---------- the addresses joined before */

#define DIRECT_SAVED_FILE "direct_ip.txt"

static int saved_path(char *path, size_t size, const char *suffix)
{
	int length = snprintf(path, size, "%s/%s%s", platform_save_root(), DIRECT_SAVED_FILE, suffix);

	return length > 0 && (size_t)length < size;
}

int direct_saved_read(char *text, int size)
{
	char path[1024];
	FILE *file;
	size_t length;

	if (!text || size <= 0)
		return 0;
	text[0] = 0;
	if (!saved_path(path, sizeof(path), "") || !(file = fopen(path, "rb")))
		return 0;
	length = fread(text, 1, (size_t)size - 1, file);
	fclose(file);
	text[length] = 0;
	return (int)length;
}

int direct_saved_write(const char *text)
{
	char path[1024], partial[1024];
	FILE *file;
	size_t length = strlen(text);
	int written;

	if (!saved_path(path, sizeof(path), "") || !saved_path(partial, sizeof(partial), ".part") ||
		!(file = fopen(partial, "wb")))
	{
		return 0;
	}
	written = fwrite(text, 1, length, file) == length;
	written = !fclose(file) && written;
	/* (Windows' rename never replaces a file: posix_replace_file) */
	if (!written || posix_replace_file(partial, path))
	{
		remove(partial);
		return 0;
	}
	return 1;
}
