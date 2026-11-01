/*
MAP_COMPRESSED.C

Halo PC maps compressed with Chimera's chimera-compress, expanded
(map_compressed.h). The header is the map's own, but for its version, which
has 0x861A0000 added; the rest of the file is Zstandard data (one frame from
chimera-compress, any number read here), which expands to the map's bytes
after its header. chimera-compress writes the expanded size in the header's
file size; a map that gives none (0, or no more than a header) is taken at
whatever size it expands to, as Chimera takes it.

Read and expanded a piece at a time, so that a map of hundreds of MB needs
no more memory than Zstandard's window and two buffers.

The format is Chimera's, by Snowy Mouse and its contributors (GPL-3.0,
github.com/SnowyMouse/chimera: compressor/main.cpp writes it,
src/chimera/map_loading/compression.cpp reads it). This reader is written
from them, in this game's style.

SPDX-License-Identifier: GPL-3.0-only
*/

#ifdef HALO_COMPRESSED_MAPS

#include "map_compressed.h"

#include "zstd.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	HEADER_SIGNATURE = 0x68656164,
	FOOTER_SIGNATURE = 0x666F6F74,
	HEADER_VERSION_OFFSET = 0x4,
	HEADER_FILE_SIZE_OFFSET = 0x8,
	FOOTER_OFFSET = 0x7FC,
	CUSTOM_EDITION_VERSION = 609,
	HALO_PC_VERSION = 7,
};

/* what chimera-compress adds to a compressed map's version */
#define COMPRESSED_VERSION_MARK 0x861A0000u

/* ---------- private code */

static unsigned int little_long(const unsigned char *bytes)
{
	return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8) | ((unsigned int)bytes[2] << 16) |
		((unsigned int)bytes[3] << 24);
}

static void set_little_long(unsigned char *bytes, unsigned int value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
}

static int refuse(char *error, int error_size, const char *format, ...)
{
	va_list arguments;

	if (error && error_size > 0)
	{
		va_start(arguments, format);
		vsnprintf(error, (size_t)error_size, format, arguments);
		va_end(arguments);
	}
	return 0;
}

static int stopped(const struct map_compressed_io *io)
{
	return io->stop && io->stop(io->context);
}

/* ---------- public code */

unsigned int map_compressed_version(const unsigned char *header)
{
	unsigned int version;

	if (!header || little_long(header) != HEADER_SIGNATURE)
		return 0;
	version = little_long(header + HEADER_VERSION_OFFSET);
	if ((version & COMPRESSED_VERSION_MARK) != COMPRESSED_VERSION_MARK)
		return 0;
	version &= ~COMPRESSED_VERSION_MARK;
	/* (Halo PC's demo, 6, has a header of its own, which this game does not
	read; chimera-compress refuses Xbox maps, 5) */
	return version == CUSTOM_EDITION_VERSION || version == HALO_PC_VERSION ? version : 0;
}

int map_compressed_expand(const struct map_compressed_io *io, unsigned int maximum_size, unsigned int *expanded_size,
	char *error, int error_size)
{
	unsigned char header[MAP_COMPRESSED_HEADER_SIZE];
	unsigned int version, stated_size, offset, total;
	size_t input_size = ZSTD_DStreamInSize();
	size_t output_size = ZSTD_DStreamOutSize();
	size_t last = 1;
	void *input = NULL;
	void *output = NULL;
	ZSTD_DStream *stream = NULL;
	int ok = 0;

	if (expanded_size)
		*expanded_size = 0;
	if (io->compressed_size < MAP_COMPRESSED_HEADER_SIZE || !io->read(io->context, 0, header, sizeof(header)))
		return refuse(error, error_size, "it is too short to be a map");
	version = map_compressed_version(header);
	if (!version || little_long(header + FOOTER_OFFSET) != FOOTER_SIGNATURE)
		return refuse(error, error_size, "it is not a compressed Custom Edition or Halo PC map");
	stated_size = little_long(header + HEADER_FILE_SIZE_OFFSET);
	if (stated_size <= MAP_COMPRESSED_HEADER_SIZE)
		stated_size = 0;
	if (stated_size > maximum_size)
		return refuse(error, error_size, "it expands to %u bytes, more than the %u a map may have", stated_size,
			maximum_size);
	set_little_long(header + HEADER_VERSION_OFFSET, version);
	if (!io->write(io->context, header, sizeof(header)))
		return refuse(error, error_size, "the expanded map could not be written");
	total = MAP_COMPRESSED_HEADER_SIZE;

	input = malloc(input_size);
	output = malloc(output_size);
	stream = ZSTD_createDStream();
	if (!input || !output || !stream || ZSTD_isError(ZSTD_initDStream(stream)))
	{
		refuse(error, error_size, "out of memory");
		goto done;
	}
	for (offset = MAP_COMPRESSED_HEADER_SIZE; offset < io->compressed_size;)
	{
		unsigned int piece = io->compressed_size - offset < input_size ? io->compressed_size - offset :
			(unsigned int)input_size;
		ZSTD_inBuffer in;

		if (stopped(io))
		{
			refuse(error, error_size, "stopped");
			goto done;
		}
		if (!io->read(io->context, offset, input, piece))
		{
			refuse(error, error_size, "it could not be read");
			goto done;
		}
		offset += piece;
		in.src = input;
		in.size = piece;
		in.pos = 0;
		/* (and on until the data is used, and no output is left over: a
		full output buffer may have more behind it) */
		for (;;)
		{
			ZSTD_outBuffer out;

			out.dst = output;
			out.size = output_size;
			out.pos = 0;
			last = ZSTD_decompressStream(stream, &out, &in);
			if (ZSTD_isError(last))
			{
				refuse(error, error_size, "its data is damaged (zstd: %s)", ZSTD_getErrorName(last));
				goto done;
			}
			if (out.pos > (size_t)(maximum_size - total))
			{
				refuse(error, error_size, "it expands to more than the %u bytes a map may have", maximum_size);
				goto done;
			}
			if (out.pos && !io->write(io->context, output, (unsigned int)out.pos))
			{
				refuse(error, error_size, "the expanded map could not be written");
				goto done;
			}
			total += (unsigned int)out.pos;
			if (in.pos >= in.size && out.pos < out.size)
				break;
		}
	}
	/* (0: the last frame ended, at the end of the file) */
	if (last != 0)
	{
		refuse(error, error_size, "its data is cut off");
		goto done;
	}
	if (total == MAP_COMPRESSED_HEADER_SIZE)
	{
		refuse(error, error_size, "it holds nothing past its header");
		goto done;
	}
	if (stated_size && total != stated_size)
	{
		refuse(error, error_size, "it expanded to %u bytes, its header says %u", total, stated_size);
		goto done;
	}
	if (expanded_size)
		*expanded_size = total;
	ok = 1;

done:
	ZSTD_freeDStream(stream);
	free(output);
	free(input);
	return ok;
}

#endif
