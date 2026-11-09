/*
MAP_COMPRESSED_TEST.C

The compressed maps' reader (port/linux/src/map_compressed.c), run by
tools/test_compressed_maps.py on maps it compresses as Chimera's
chimera-compress does (tools/chimera_compress.py):

  map_compressed_test version <map>                      its expanded version, 0 if not compressed
  map_compressed_test expand <map> <out> [<maximum>]     ok <size> | refused: <why>
  map_compressed_test stop <map>                         refused: <why> (stopped at once)

No file is written but <out>, and none on a refusal.

SPDX-License-Identifier: GPL-3.0-only
*/

#include "map_compressed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct files
{
	FILE *input;
	FILE *output;
	int stop;
};

static int file_read(void *context, unsigned int offset, void *buffer, unsigned int size)
{
	struct files *files = (struct files *)context;

	return !fseek(files->input, (long)offset, SEEK_SET) && fread(buffer, 1, size, files->input) == size;
}

static int file_write(void *context, const void *buffer, unsigned int size)
{
	struct files *files = (struct files *)context;

	return !files->output || fwrite(buffer, 1, size, files->output) == size;
}

static int file_stop(void *context)
{
	return ((struct files *)context)->stop;
}

int main(int argc, char **argv)
{
	struct map_compressed_io io;
	struct files files;
	char error[160] = "";
	unsigned int expanded = 0;
	unsigned int maximum = 1024u * 1024u * 1024u;
	long size;
	int ok;

	if (argc < 3)
		return 2;
	memset(&files, 0, sizeof(files));
	files.input = fopen(argv[2], "rb");
	if (!files.input)
		return 2;
	if (!strcmp(argv[1], "version"))
	{
		unsigned char header[MAP_COMPRESSED_HEADER_SIZE];

		memset(header, 0, sizeof(header));
		if (fread(header, 1, sizeof(header), files.input) < 8)
			memset(header, 0, sizeof(header));
		printf("%u\n", map_compressed_version(header));
		fclose(files.input);
		return 0;
	}
	fseek(files.input, 0, SEEK_END);
	size = ftell(files.input);
	memset(&io, 0, sizeof(io));
	io.context = &files;
	io.compressed_size = (unsigned int)size;
	io.read = file_read;
	io.write = file_write;
	io.stop = file_stop;
	if (!strcmp(argv[1], "expand") && argc >= 4)
	{
		if (argc >= 5)
			maximum = (unsigned int)strtoul(argv[4], NULL, 0);
		files.output = fopen(argv[3], "wb");
		if (!files.output)
			return 2;
	}
	else if (!strcmp(argv[1], "stop"))
		files.stop = 1;
	else
		return 2;
	ok = map_compressed_expand(&io, maximum, &expanded, error, (int)sizeof(error));
	fclose(files.input);
	if (files.output)
	{
		fclose(files.output);
		if (!ok)
			remove(argv[3]);
	}
	if (ok)
		printf("ok %u\n", expanded);
	else
		printf("refused: %s\n", error);
	return 0;
}
