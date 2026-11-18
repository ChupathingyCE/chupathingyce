/*
DELTA_TABLE_FUZZ.C

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later

libFuzzer's entry for the signed legacy table's document (tools/
test_host_fuzz.py builds it): each input is a document, in an allocation of
its own size (so that a read past it is caught), read as delta.c reads one
whose signature has checked (delta_table_parse). delta.c is built here
whole, without the platform layer (its platform.h needs the Xbox SDK's
headers) or the game list's fetching: the stand-ins give it no settings, no
files and no network, so it starts on its built-in numbers.

What a table it reads must hold, or the fuzzer traps: its row a range, its
counts within their arrays, its floors network versions and none for the
unknown platform. A table read has its floors put in use, and the floor
rule is asked of every platform (and past both ends) at the numbers around
each floor: under the floor alone refuses.
*/

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);
const char *platform_save_root(void);

#include "../../../../port/linux/src/delta.c"

/* ---------- stand-ins for the rest of the game */

void platform_log(const char *format, ...)
{
	(void)format;
}

const char *platform_save_root(void)
{
	return ".";
}

const char *config_string(const char *name)
{
	(void)name;
	return "";
}

void config_folder(char *path, size_t size)
{
	if (size)
		path[0] = 0;
}

char *config_file_read(const char *path, size_t *size)
{
	(void)path;
	(void)size;
	return NULL;
}

void network_rules_set_layout_source(int (*source)(struct delta_layout_fact *facts, int count))
{
	(void)source;
}

/* ---------- the document */

static void delta_table_fuzz(const char *document, size_t size)
{
	struct delta_table table;
	const char *why = NULL;
	int platform;

	if (!delta_table_parse(document, size, &table, &why))
	{
		if (!why)
			__builtin_trap();
		return;
	}
	if (table.floors[0] || table.layout_count < 0 || table.layout_count > DELTA_LAYOUT_FACTS ||
		table.hosting_count < 0 || table.hosting_count > DELTA_HOSTING_MAXIMUM ||
		(table.has_row && !(table.minimum <= table.announce && table.announce <= table.maximum)))
	{
		__builtin_trap();
	}
	for (platform = 0; platform < NUMBER_OF_DELTA_PLATFORMS; platform++)
	{
		if (table.floors[platform] < 0 || table.floors[platform] > MAXIMUM_VERSION)
			__builtin_trap();
	}
	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	delta_use_floors(&table);
	pthread_mutex_unlock(&delta_lock);
	for (platform = -1; platform <= NUMBER_OF_DELTA_PLATFORMS; platform++)
	{
		int oldest = delta_platform_floor(platform);
		int known = platform > 0 && platform < NUMBER_OF_DELTA_PLATFORMS;

		if (known ? oldest != table.floors[platform] : oldest != 0)
			__builtin_trap();
		if (delta_platform_floor_refuses(platform, MAXIMUM_VERSION) ||
			delta_platform_floor_refuses(platform, oldest) ||
			(oldest && !delta_platform_floor_refuses(platform, oldest - 1)) ||
			(!oldest && delta_platform_floor_refuses(platform, 0)))
		{
			__builtin_trap();
		}
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *document = malloc(size ? size : 1);

	if (!document)
		return 0;
	memcpy(document, data, size);
	delta_table_fuzz(document, size);
	free(document);
	return 0;
}
