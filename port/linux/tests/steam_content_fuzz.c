/*
STEAM_CONTENT_FUZZ.C

libFuzzer's entry for what the Steam content client (port/linux/src/
steam_content) takes from the network, all of it untrusted (built by
tools/test_steam_content.py). The first byte picks the reader: 0, a
protobuf message walked three levels deep; 1, a manifest (zip, sections,
encrypted names) with the test key; 2, a decrypted chunk's unpacking
(LZMA, Zstandard, zip) into the size the next two bytes give; 3, a whole
chunk (decrypt, unpack, SHA-1); 4, a zip's first file. Each input in an
allocation of exactly its size.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/

#include "../src/steam_content/sc_internal.h"
#include "../src/steam_content/sc_proto.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char key[32] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
	23, 24, 25, 26, 27, 28, 29, 30, 31 };

unsigned long steam_content_fuzz_walked;

static void walk(const unsigned char *data, size_t size, int depth)
{
	struct pb_reader reader;
	struct pb_field field;
	char text[64];

	pb_reader_init(&reader, data, size);
	while (pb_next(&reader, &field) == 1)
	{
		steam_content_fuzz_walked += field.value & 1;
		if (field.wire == PB_BYTES && depth < 3)
			walk(field.data, field.length, depth + 1);
	}
	pb_get_string(data, size, 1, text, sizeof(text));
	steam_content_fuzz_walked += pb_get_number(data, size, 2, 0) & 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *input, size_t size)
{
	unsigned char *data;
	char why[128];

	if (!size)
		return 0;
	data = (unsigned char *)malloc(size - 1 ? size - 1 : 1);
	if (!data)
		return 0;
	memcpy(data, input + 1, size - 1);
	switch (input[0] % 5)
	{
	case 0:
		walk(data, size - 1, 0);
		break;
	case 1:
	{
		struct sc_manifest manifest;

		if (sc_manifest_parse(data, size - 1, key, &manifest, why, sizeof(why)))
			sc_manifest_free(&manifest);
		break;
	}
	case 2:
	case 3:
		if (size >= 3)
		{
			size_t out_size = (size_t)input[1] | ((size_t)input[2] << 8);
			unsigned char *out = (unsigned char *)malloc(out_size ? out_size : 1);

			if (out && input[0] % 5 == 2)
				sc_chunk_unpack(data + 2, size - 3, out, out_size, why, sizeof(why));
			else if (out)
			{
				struct sc_chunk chunk;

				memset(&chunk, 0, sizeof(chunk));
				chunk.size = (uint32_t)out_size;
				sc_chunk_decode(key, data + 2, size - 3, &chunk, out, why, sizeof(why));
			}
			free(out);
		}
		break;
	default:
	{
		size_t length;
		unsigned char *out = sc_unzip_first(data, size - 1, &length, 1 << 20);

		free(out);
		break;
	}
	}
	free(data);
	return 0;
}
