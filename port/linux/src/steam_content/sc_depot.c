/*
SC_DEPOT.C

A depot's content as the Steam content servers give it (sc_internal.h):

- Symmetric decryption: AES-256; the first 16 bytes, decrypted with ECB,
  are the IV for the rest, CBC with PKCS#7 padding.
- A chunk: decrypted, then "VZa" (LZMA: a 7-byte header, 5 bytes of
  properties, the data, and a footer of CRC-32, u32 size and "zv"), "VSZa"
  (Zstandard: an 8-byte header, the frame, and a 15-byte footer of CRC-32,
  u32 size, 4 more bytes and "zsv") or a zip ("PK"). The plain bytes' SHA-1
  is the chunk's id.
- A manifest: a zip with one file of sections, each a u32 magic and a u32
  length (the end marker is its magic alone): payload 0x71F617D0 (the
  files, ContentManifestPayload), metadata 0x1F4812BE (filenames_encrypted
  among it), signature 0x1B81B817, end 0x32C415AB. Encrypted filenames are
  base64 of the symmetric encryption.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h): a console builds this to nothing */

#include "sc_internal.h"
#include "sc_proto.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "LzmaDec.h"
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"
#include "zlib.h"
#include "zstd.h"

#define MAGIC_PAYLOAD 0x71F617D0u
#define MAGIC_METADATA 0x1F4812BEu
#define MAGIC_SIGNATURE 0x1B81B817u
#define MAGIC_END 0x32C415ABu

static uint32_t u32(const unsigned char *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint32_t u16(const unsigned char *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8);
}

long sc_symmetric_decrypt(const unsigned char key[32], const unsigned char *data, size_t length, unsigned char *out)
{
	mbedtls_aes_context aes;
	unsigned char iv[16];
	size_t plain;
	unsigned pad, index;
	int ok;

	if (length < 32 || (length - 16) % 16)
		return -1;
	mbedtls_aes_init(&aes);
	ok = mbedtls_aes_setkey_dec(&aes, key, 256) == 0 &&
		mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, data, iv) == 0 &&
		mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, length - 16, iv, data + 16, out) == 0;
	mbedtls_aes_free(&aes);
	if (!ok)
		return -1;
	plain = length - 16;
	pad = out[plain - 1];
	if (pad < 1 || pad > 16)
		return -1;
	for (index = 0; index < pad; index++)
	{
		if (out[plain - 1 - index] != pad)
			return -1;
	}
	return (long)(plain - pad);
}

/* ---------- zip */

static void *lzma_alloc(ISzAllocPtr allocator, size_t size)
{
	(void)allocator;
	return malloc(size);
}

static void lzma_free(ISzAllocPtr allocator, void *address)
{
	(void)allocator;
	free(address);
}

static const ISzAlloc lzma_allocator = { lzma_alloc, lzma_free };

static int inflate_raw(const unsigned char *data, size_t length, unsigned char *out, size_t out_length)
{
	z_stream stream;
	int result;

	memset(&stream, 0, sizeof(stream));
	if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
		return 0;
	stream.next_in = (Bytef *)data;
	stream.avail_in = (uInt)length;
	stream.next_out = out;
	stream.avail_out = (uInt)out_length;
	result = inflate(&stream, Z_FINISH);
	inflateEnd(&stream);
	return result == Z_STREAM_END && stream.total_out == out_length;
}

unsigned char *sc_unzip_first(const unsigned char *data, size_t length, size_t *out_length, size_t maximum)
{
	size_t at, end_record = (size_t)-1, central, local, start;
	uint32_t method, compressed, size;
	unsigned char *out;

	/* (the end of central directory record, from the end: a comment may follow it) */
	if (length < 22)
		return NULL;
	for (at = length - 22;; at--)
	{
		if (u32(data + at) == 0x06054b50u)
		{
			end_record = at;
			break;
		}
		if (!at || length - at > 22 + 65535)
			break;
	}
	if (end_record == (size_t)-1)
		return NULL;
	central = u32(data + end_record + 16);
	if (length < 46 || central > length - 46 || u32(data + central) != 0x02014b50u)
		return NULL;
	method = u16(data + central + 10);
	compressed = u32(data + central + 20);
	size = u32(data + central + 24);
	local = u32(data + central + 42);
	if (length < 30 || local > length - 30 || u32(data + local) != 0x04034b50u)
		return NULL;
	start = local + 30 + u16(data + local + 26) + u16(data + local + 28);
	if (start > length || compressed > length - start || size > maximum)
		return NULL;
	out = (unsigned char *)malloc(size ? size : 1);
	if (!out)
		return NULL;
	if (method == 0 && compressed == size)
		memcpy(out, data + start, size);
	else if (method != 8 || !inflate_raw(data + start, compressed, out, size))
	{
		free(out);
		return NULL;
	}
	if ((uint32_t)crc32(0, out, size) != u32(data + central + 16))
	{
		free(out);
		return NULL;
	}
	*out_length = size;
	return out;
}

/* ---------- chunks */

static int sha1_matches(const unsigned char *data, size_t length, const unsigned char sha[20])
{
	unsigned char digest[20];

	return mbedtls_sha1(data, length, digest) == 0 && !memcmp(digest, sha, 20);
}

int sc_chunk_unpack(const unsigned char *data, size_t length, unsigned char *out, size_t size, char *why, int why_size)
{
	if (length >= 22 && !memcmp(data, "VZa", 3))
	{
		SizeT out_size = size, in_size;
		ELzmaStatus status;
		SRes result;

		if (memcmp(data + length - 2, "zv", 2) || u32(data + length - 6) != size)
		{
			snprintf(why, (size_t)why_size, "a VZip chunk's footer is wrong");
			return 0;
		}
		in_size = length - 12 - 10;
		result = LzmaDecode(out, &out_size, data + 12, &in_size, data + 7, 5, LZMA_FINISH_ANY, &status,
			&lzma_allocator);
		if (result != SZ_OK || out_size != size)
		{
			snprintf(why, (size_t)why_size, "a VZip chunk did not unpack (%d)", (int)result);
			return 0;
		}
		if ((uint32_t)crc32(0, out, (uInt)size) != u32(data + length - 10))
		{
			snprintf(why, (size_t)why_size, "a VZip chunk's CRC is wrong");
			return 0;
		}
		return 1;
	}
	if (length >= 8 + 15 && !memcmp(data, "VSZa", 4))
	{
		size_t result;

		if (memcmp(data + length - 3, "zsv", 3) || u32(data + length - 11) != size)
		{
			snprintf(why, (size_t)why_size, "a VZstd chunk's footer is wrong");
			return 0;
		}
		result = ZSTD_decompress(out, size, data + 8, length - 8 - 15);
		if (ZSTD_isError(result) || result != size)
		{
			snprintf(why, (size_t)why_size, "a VZstd chunk did not unpack");
			return 0;
		}
		if ((uint32_t)crc32(0, out, (uInt)size) != u32(data + length - 15))
		{
			snprintf(why, (size_t)why_size, "a VZstd chunk's CRC is wrong");
			return 0;
		}
		return 1;
	}
	if (length >= 4 && data[0] == 'P' && data[1] == 'K')
	{
		size_t unzipped;
		unsigned char *inner = sc_unzip_first(data, length, &unzipped, size);

		if (!inner || unzipped != size)
		{
			free(inner);
			snprintf(why, (size_t)why_size, "a zipped chunk did not unpack");
			return 0;
		}
		memcpy(out, inner, size);
		free(inner);
		return 1;
	}
	snprintf(why, (size_t)why_size, "a chunk of an unknown kind");
	return 0;
}

int sc_chunk_decode(const unsigned char key[32], const unsigned char *data, size_t length,
	const struct sc_chunk *chunk, unsigned char *out, char *why, int why_size)
{
	unsigned char *plain;
	long plain_length;
	int ok;

	if (chunk->size > SC_MAXIMUM_CHUNK || length > SC_MAXIMUM_CHUNK)
	{
		snprintf(why, (size_t)why_size, "a chunk too large");
		return 0;
	}
	plain = (unsigned char *)malloc(length ? length : 1);
	if (!plain)
	{
		snprintf(why, (size_t)why_size, "out of memory");
		return 0;
	}
	plain_length = sc_symmetric_decrypt(key, data, length, plain);
	if (plain_length < 0)
	{
		free(plain);
		snprintf(why, (size_t)why_size, "a chunk did not decrypt");
		return 0;
	}
	ok = sc_chunk_unpack(plain, (size_t)plain_length, out, chunk->size, why, why_size);
	free(plain);
	if (ok && !sha1_matches(out, chunk->size, chunk->sha))
	{
		snprintf(why, (size_t)why_size, "a chunk's SHA-1 is wrong");
		ok = 0;
	}
	return ok;
}

/* ---------- the manifest */

/* a part Windows keeps for a device (CON, PRN, AUX, NUL, COM1-9, LPT1-9),
with any extension: "nul.txt" opens the device too */
static int device_name(const char *part, size_t length)
{
	static const char *const devices[] = { "CON", "PRN", "AUX", "NUL" };
	char base[5];
	size_t base_length = 0, index;

	while (base_length < length && part[base_length] != '.' && base_length < 5)
		base_length++;
	if (base_length < length && part[base_length] != '.')
		return 0;
	if (base_length != 3 && base_length != 4)
		return 0;
	for (index = 0; index < base_length; index++)
		base[index] = (char)toupper((unsigned char)part[index]);
	if (base_length == 3)
	{
		for (index = 0; index < sizeof(devices) / sizeof(devices[0]); index++)
		{
			if (!memcmp(base, devices[index], 3))
				return 1;
		}
		return 0;
	}
	return (!memcmp(base, "COM", 3) || !memcmp(base, "LPT", 3)) && base[3] >= '1' && base[3] <= '9';
}

int sc_name_safe(const char *name)
{
	const char *part = name;

	/* (no root, no drive or alternate data stream, one separator) */
	if (!name[0] || name[0] == '/' || strchr(name, ':') || strchr(name, '\\'))
		return 0;
	for (;;)
	{
		size_t length = strcspn(part, "/");
		size_t index;

		if (!length || (length == 1 && part[0] == '.') || (length == 2 && part[0] == '.' && part[1] == '.'))
			return 0;
		/* (Windows drops a part's last dots and spaces: "a." would be "a") */
		if (part[length - 1] == '.' || part[length - 1] == ' ')
			return 0;
		if (device_name(part, length))
			return 0;
		for (index = 0; index < length; index++)
		{
			if ((unsigned char)part[index] < 0x20 || part[index] == 0x7F || strchr("<>\"|?*", part[index]))
				return 0;
		}
		if (!part[length])
			return 1;
		part += length + 1;
	}
}

static int name_order(const void *a, const void *b)
{
	const char *left = *(const char *const *)a, *right = *(const char *const *)b;

	for (;; left++, right++)
	{
		int l = tolower((unsigned char)*left), r = tolower((unsigned char)*right);

		if (l != r || !l)
			return l - r;
	}
}

/* two files whose names differ only in case: one would overwrite the
other on a case-insensitive disk (Windows, macOS) */
static int names_unique(const struct sc_manifest *manifest)
{
	const char **names;
	int index, unique = 1;

	if (manifest->file_count < 2)
		return 1;
	names = (const char **)malloc((size_t)manifest->file_count * sizeof(*names));
	if (!names)
		return 0;
	for (index = 0; index < manifest->file_count; index++)
		names[index] = manifest->files[index].name;
	qsort(names, (size_t)manifest->file_count, sizeof(*names), name_order);
	for (index = 1; index < manifest->file_count && unique; index++)
		unique = name_order(&names[index - 1], &names[index]) != 0;
	free(names);
	return unique;
}

static int file_name(const unsigned char *bytes, size_t length, int encrypted, const unsigned char key[32],
	char *name)
{
	size_t index;

	if (encrypted)
	{
		unsigned char cipher[1024], plain[1024];
		size_t cipher_length = 0;
		long plain_length;

		if (mbedtls_base64_decode(cipher, sizeof(cipher), &cipher_length, bytes, length) != 0)
			return 0;
		plain_length = sc_symmetric_decrypt(key, cipher, cipher_length, plain);
		if (plain_length < 0)
			return 0;
		while (plain_length > 0 && plain[plain_length - 1] == 0)
			plain_length--;
		if (plain_length >= SC_MAXIMUM_NAME)
			return 0;
		memcpy(name, plain, (size_t)plain_length);
		name[plain_length] = 0;
	}
	else
	{
		if (length >= SC_MAXIMUM_NAME)
			return 0;
		memcpy(name, bytes, length);
		name[length] = 0;
	}
	for (index = 0; name[index]; index++)
	{
		if (name[index] == '\\')
			name[index] = '/';
	}
	return 1;
}

void sc_manifest_free(struct sc_manifest *manifest)
{
	free(manifest->files);
	free(manifest->chunks);
	memset(manifest, 0, sizeof(*manifest));
}

int sc_manifest_parse(const unsigned char *data, size_t length, const unsigned char key[32],
	struct sc_manifest *manifest, char *why, int why_size)
{
	const unsigned char *payload = NULL, *metadata = NULL;
	size_t payload_length = 0, metadata_length = 0, raw_length, at = 0;
	struct pb_reader reader;
	struct pb_field field;
	unsigned char *raw;
	int encrypted, files = 0, chunks = 0, result;

	memset(manifest, 0, sizeof(*manifest));
	raw = sc_unzip_first(data, length, &raw_length, 256u * 1024 * 1024);
	if (!raw)
	{
		snprintf(why, (size_t)why_size, "the manifest did not unzip");
		return 0;
	}
	while (at + 4 <= raw_length)
	{
		uint32_t magic = u32(raw + at), section;

		if (magic == MAGIC_END)
			break;
		if (at + 8 > raw_length)
			break;
		section = u32(raw + at + 4);
		at += 8;
		if (section > raw_length - at)
			break;
		if (magic == MAGIC_PAYLOAD)
		{
			payload = raw + at;
			payload_length = section;
		}
		else if (magic == MAGIC_METADATA)
		{
			metadata = raw + at;
			metadata_length = section;
		}
		at += section;
	}
	if (!payload || !metadata)
	{
		free(raw);
		snprintf(why, (size_t)why_size, "the manifest has no payload");
		return 0;
	}
	encrypted = pb_get_number(metadata, metadata_length, 4, 0) != 0;
	/* (counted first, then read) */
	pb_reader_init(&reader, payload, payload_length);
	while ((result = pb_next(&reader, &field)) == 1)
	{
		struct pb_reader inner;
		struct pb_field chunk;

		if (field.number != 1 || field.wire != PB_BYTES)
			continue;
		files++;
		pb_reader_init(&inner, field.data, field.length);
		while (pb_next(&inner, &chunk) == 1)
			chunks += chunk.number == 6;
	}
	if (result < 0 || files > 1000000 || chunks > 10000000)
	{
		free(raw);
		snprintf(why, (size_t)why_size, "the manifest's payload is broken");
		return 0;
	}
	manifest->files = (struct sc_file *)calloc((size_t)(files ? files : 1), sizeof(struct sc_file));
	manifest->chunks = (struct sc_chunk *)calloc((size_t)(chunks ? chunks : 1), sizeof(struct sc_chunk));
	if (!manifest->files || !manifest->chunks)
	{
		free(raw);
		sc_manifest_free(manifest);
		snprintf(why, (size_t)why_size, "out of memory");
		return 0;
	}
	pb_reader_init(&reader, payload, payload_length);
	while (pb_next(&reader, &field) == 1)
	{
		struct sc_file *file;
		struct pb_reader inner;
		struct pb_field part;
		int index;

		if (field.number != 1 || field.wire != PB_BYTES)
			continue;
		file = &manifest->files[manifest->file_count++];
		file->first_chunk = manifest->chunk_count;
		pb_reader_init(&inner, field.data, field.length);
		while (pb_next(&inner, &part) == 1)
		{
			if (part.number == 1 && part.wire == PB_BYTES)
			{
				if (!file_name(part.data, part.length, encrypted, key, file->name))
				{
					free(raw);
					sc_manifest_free(manifest);
					snprintf(why, (size_t)why_size, "a file name in the manifest did not decrypt");
					return 0;
				}
			}
			else if (part.number == 2)
				file->size = part.value;
			else if (part.number == 3)
				file->flags = (uint32_t)part.value;
			else if (part.number == 5 && part.wire == PB_BYTES && part.length == 20)
			{
				memcpy(file->sha, part.data, 20);
				file->has_sha = 1;
			}
			else if (part.number == 6 && part.wire == PB_BYTES)
			{
				struct sc_chunk *chunk = &manifest->chunks[manifest->chunk_count++];
				const unsigned char *sha;
				size_t sha_length;

				if (pb_get_bytes(part.data, part.length, 1, &sha, &sha_length) && sha_length == 20)
					memcpy(chunk->sha, sha, 20);
				chunk->offset = pb_get_number(part.data, part.length, 3, 0);
				chunk->size = (uint32_t)pb_get_number(part.data, part.length, 4, 0);
				chunk->compressed_size = (uint32_t)pb_get_number(part.data, part.length, 5, 0);
				file->chunk_count++;
			}
		}
		if (!sc_name_safe(file->name))
		{
			free(raw);
			sc_manifest_free(manifest);
			snprintf(why, (size_t)why_size, "a file name in the manifest is not safe");
			return 0;
		}
		/* (a folder has no content: chunks on one would have no file to go in) */
		if ((file->flags & SC_FILE_DIRECTORY) && file->chunk_count)
		{
			free(raw);
			sc_manifest_free(manifest);
			snprintf(why, (size_t)why_size, "a folder in the manifest has content");
			return 0;
		}
		/* (each chunk inside its file: no write past the size Steam gave) */
		for (index = file->first_chunk; index < file->first_chunk + file->chunk_count; index++)
		{
			const struct sc_chunk *chunk = &manifest->chunks[index];

			if (chunk->offset > file->size || chunk->size > file->size - chunk->offset)
			{
				free(raw);
				sc_manifest_free(manifest);
				snprintf(why, (size_t)why_size, "a chunk in the manifest lies outside its file");
				return 0;
			}
		}
		if (!(file->flags & SC_FILE_DIRECTORY))
			manifest->total += file->size;
	}
	free(raw);
	if (!names_unique(manifest))
	{
		sc_manifest_free(manifest);
		snprintf(why, (size_t)why_size, "two file names in the manifest differ only in case");
		return 0;
	}
	return 1;
}
#endif /* HALO_MCC */
