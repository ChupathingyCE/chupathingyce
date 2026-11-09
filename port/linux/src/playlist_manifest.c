/*
PLAYLIST_MANIFEST.C

A playlist's signed map set checked and read (playlist_manifest.h;
docs/playlist-maps.md). The signature first, over PLAYLIST_MANIFEST_CONTEXT
and the document's exact bytes, by one of Delta's keys (delta_key.h), the
key allowed the serial's epoch as for Delta's legacy table (delta.c); only
then is the document read, by a strict parser: a known field of the wrong
kind or out of its bounds, a pack named that the manifest does not have, or
a file twice refuses the whole manifest. Fields it does not know are passed
over, so later versions may add some.
*/

#include "playlist_manifest.h"
#include "delta_key.h"
#include "monocypher-ed25519.h"

#include <stdio.h>
#include <string.h>

/* ---------- constants */

#define PLAYLIST_KEY_COUNT (sizeof(delta_public_keys) / sizeof(*delta_public_keys))
#define PLAYLIST_EPOCH(serial) ((unsigned int)(serial) >> 24)

enum
{
	SIGNATURE_SIZE = 64,
	/* nesting a skipped value may have */
	MAXIMUM_DEPTH = 8,
};

/* ---------- the names */

static int is_lower_or_digit(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

int playlist_manifest_id_valid(const char *text)
{
	size_t length = strlen(text), index;

	if (length < 1 || length > PLAYLIST_MANIFEST_ID_SIZE - 1)
		return 0;
	for (index = 0; index < length; index++)
	{
		if (!is_lower_or_digit(text[index]) && text[index] != '_')
			return 0;
	}
	return 1;
}

static int pack_id_valid(const char *text)
{
	size_t length = strlen(text), index;

	if (length < 1 || length > PLAYLIST_MANIFEST_PACK_ID_SIZE - 1)
		return 0;
	for (index = 0; index < length; index++)
	{
		if (!is_lower_or_digit(text[index]) && text[index] != '_' && text[index] != '.' && text[index] != '-')
			return 0;
	}
	return 1;
}

int playlist_manifest_file_valid(const char *text)
{
	size_t length = strlen(text), index;

	if (length < 5 || length > PLAYLIST_MANIFEST_FILE_SIZE - 1 || text[0] == '.' || strstr(text, ".."))
		return 0;
	for (index = 0; index < length; index++)
	{
		if (!is_lower_or_digit(text[index]) && text[index] != '_' && text[index] != '-' && text[index] != '.')
			return 0;
	}
	return !strcmp(text + length - 4, ".map") || !strcmp(text + length - 4, ".txt");
}

int playlist_manifest_folder_valid(const char *text)
{
	size_t length = strlen(text), index;

	if (!strcmp(text, "maps") || !strcmp(text, "maps_ce"))
		return 1;
	if (strncmp(text, "maps_", 5) || length < 6 || length > 5 + 16)
		return 0;
	for (index = 5; index < length; index++)
	{
		if (!is_lower_or_digit(text[index]))
			return 0;
	}
	return 1;
}

int playlist_manifest_path_valid(const char *text)
{
	size_t length = strlen(text), index;
	int part_start = 1;

	if (length < 1 || length > PLAYLIST_MANIFEST_PATH_SIZE - 1)
		return 0;
	for (index = 0; index < length; index++)
	{
		char c = text[index];

		if (c == '/')
		{
			/* (no empty part) */
			if (part_start)
				return 0;
			part_start = 1;
			continue;
		}
		/* (no part starting with '.': nothing hidden, no "..") */
		if (part_start && c == '.')
			return 0;
		if (!is_lower_or_digit(c) && c != '_' && c != '-' && c != '.')
			return 0;
		part_start = 0;
	}
	return !part_start;
}

/* the game's name for a map: a file's name, and @ce or @<pack> */
static int map_name_valid(const char *text)
{
	size_t length = strlen(text), index;
	const char *at = strchr(text, '@');

	if (length < 1 || length > PLAYLIST_MANIFEST_MAP_SIZE - 1 || text[0] == '.' || strstr(text, ".."))
		return 0;
	for (index = 0; index < length; index++)
	{
		char c = text[index];

		if (text + index == at)
			continue;
		if (!is_lower_or_digit(c) && c != '_' && c != '-' && c != '.')
			return 0;
	}
	if (at)
	{
		if (at == text || strchr(at + 1, '@') || !at[1] || strlen(at + 1) > 16)
			return 0;
		for (index = 1; at[index]; index++)
		{
			if (!is_lower_or_digit(at[index]))
				return 0;
		}
	}
	return 1;
}

/* ---------- the JSON reader */

struct json
{
	const char *at;
	const char *end;
};

static void skip_space(struct json *json)
{
	while (json->at < json->end &&
		(*json->at == ' ' || *json->at == '\t' || *json->at == '\r' || *json->at == '\n'))
	{
		json->at++;
	}
}

static int take(struct json *json, char c)
{
	skip_space(json);
	if (json->at < json->end && *json->at == c)
	{
		json->at++;
		return 1;
	}
	return 0;
}

static int peek(struct json *json, char c)
{
	skip_space(json);
	return json->at < json->end && *json->at == c;
}

static int hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* a string into text (size bytes, NULL: skipped); 0 if it is not one, or
too long, or has a control character or a \u escape past ASCII. Bytes past
ASCII (UTF-8) are kept as they are */
static int json_string(struct json *json, char *text, size_t size)
{
	size_t length = 0;

	if (!take(json, '"'))
		return 0;
	while (json->at < json->end)
	{
		unsigned char c = (unsigned char)*json->at++;

		if (c == '"')
		{
			if (text)
				text[length] = 0;
			return 1;
		}
		if (c < 0x20)
			return 0;
		if (c == '\\')
		{
			if (json->at >= json->end)
				return 0;
			c = (unsigned char)*json->at++;
			switch (c)
			{
			case '"': case '\\': case '/': break;
			case 'n': c = '\n'; break;
			case 't': c = '\t'; break;
			case 'r': c = '\r'; break;
			case 'b': c = '\b'; break;
			case 'f': c = '\f'; break;
			case 'u':
			{
				int value = 0, index;

				if (json->end - json->at < 4)
					return 0;
				for (index = 0; index < 4; index++)
				{
					int digit = hex_digit(json->at[index]);

					if (digit < 0)
						return 0;
					value = value * 16 + digit;
				}
				json->at += 4;
				if (value < 0x20 || value > 0x7e)
					return 0;
				c = (unsigned char)value;
				break;
			}
			default: return 0;
			}
		}
		if (text)
		{
			if (length + 1 >= size)
				return 0;
			text[length] = (char)c;
		}
		length++;
	}
	return 0;
}

/* a whole number from minimum to maximum: no sign, fraction or exponent */
static int json_integer(struct json *json, long long minimum, long long maximum, long long *value)
{
	long long number = 0;
	int digits = 0;

	skip_space(json);
	while (json->at < json->end && *json->at >= '0' && *json->at <= '9')
	{
		if (number > (maximum - (*json->at - '0')) / 10)
			return 0;
		number = number * 10 + (*json->at++ - '0');
		digits++;
	}
	if (!digits || (digits > 1 && json->at[-digits] == '0'))
		return 0;
	if (json->at < json->end && (*json->at == '.' || *json->at == 'e' || *json->at == 'E'))
		return 0;
	if (number < minimum || number > maximum)
		return 0;
	*value = number;
	return 1;
}

static int json_literal(struct json *json, const char *word)
{
	size_t length = strlen(word);

	skip_space(json);
	if ((size_t)(json->end - json->at) < length || memcmp(json->at, word, length))
		return 0;
	json->at += length;
	return 1;
}

/* any value passed over (a field this reader does not know) */
static int json_skip(struct json *json, int depth)
{
	if (depth > MAXIMUM_DEPTH)
		return 0;
	skip_space(json);
	if (json->at >= json->end)
		return 0;
	switch (*json->at)
	{
	case '"': return json_string(json, NULL, 0);
	case 't': return json_literal(json, "true");
	case 'f': return json_literal(json, "false");
	case 'n': return json_literal(json, "null");
	case '{':
		json->at++;
		if (take(json, '}'))
			return 1;
		do
		{
			if (!json_string(json, NULL, 0) || !take(json, ':') || !json_skip(json, depth + 1))
				return 0;
		} while (take(json, ','));
		return take(json, '}');
	case '[':
		json->at++;
		if (take(json, ']'))
			return 1;
		do
		{
			if (!json_skip(json, depth + 1))
				return 0;
		} while (take(json, ','));
		return take(json, ']');
	default:
	{
		/* (a number, of any form) */
		const char *start = json->at;

		if (*json->at == '-')
			json->at++;
		while (json->at < json->end && ((*json->at >= '0' && *json->at <= '9') || *json->at == '.' ||
			*json->at == 'e' || *json->at == 'E' || *json->at == '+' || *json->at == '-'))
		{
			json->at++;
		}
		return json->at > start && (json->at[-1] >= '0' && json->at[-1] <= '9');
	}
	}
}

/* printable text for people: no control characters (UTF-8 kept) */
static int text_printable(const char *text, int empty_allowed)
{
	if (!text[0])
		return empty_allowed;
	for (; *text; text++)
	{
		if ((unsigned char)*text < 0x20 || *text == 0x7f)
			return 0;
	}
	return 1;
}

static int sha256_from_hex(const char *text, unsigned char *digest)
{
	int index;

	if (strlen(text) != PLAYLIST_MANIFEST_SHA256_SIZE * 2)
		return 0;
	for (index = 0; index < PLAYLIST_MANIFEST_SHA256_SIZE; index++)
	{
		int high = hex_digit(text[index * 2]), low = hex_digit(text[index * 2 + 1]);

		/* (lowercase alone, as the format says) */
		if (high < 0 || low < 0 || (text[index * 2] >= 'A' && text[index * 2] <= 'F') ||
			(text[index * 2 + 1] >= 'A' && text[index * 2 + 1] <= 'F'))
		{
			return 0;
		}
		digest[index] = (unsigned char)(high << 4 | low);
	}
	return 1;
}

/* ---------- the manifest's parts */

static int fail(char *why, int why_size, const char *text)
{
	snprintf(why, (size_t)why_size, "%s", text);
	return 0;
}

static int read_pack(struct json *json, struct playlist_pack *pack, char *why, int why_size)
{
	int has_name = 0;

	if (!take(json, '{'))
		return fail(why, why_size, "a pack is not an object");
	if (take(json, '}'))
		return fail(why, why_size, "a pack has no name");
	do
	{
		char key[32];
		int ok;

		if (!json_string(json, key, sizeof(key)) || !take(json, ':'))
			return fail(why, why_size, "a pack's field is not one");
		if (!strcmp(key, "name"))
		{
			ok = !has_name && json_string(json, pack->name, sizeof(pack->name)) && text_printable(pack->name, 0);
			has_name = 1;
		}
		else if (!strcmp(key, "author"))
			ok = json_string(json, pack->author, sizeof(pack->author)) && text_printable(pack->author, 1);
		else if (!strcmp(key, "credits"))
			ok = json_string(json, pack->credits, sizeof(pack->credits)) && text_printable(pack->credits, 1);
		else if (!strcmp(key, "permission"))
			ok = json_string(json, NULL, 0);
		else
			ok = json_skip(json, 2);
		if (!ok)
			return fail(why, why_size, "a pack's field is wrong");
	} while (take(json, ','));
	if (!take(json, '}'))
		return fail(why, why_size, "a pack is not closed");
	if (!has_name)
		return fail(why, why_size, "a pack has no name");
	return 1;
}

static int read_packs(struct json *json, struct playlist_manifest *manifest, char *why, int why_size)
{
	if (!take(json, '{'))
		return fail(why, why_size, "packs is not an object");
	if (take(json, '}'))
		return 1;
	do
	{
		struct playlist_pack *pack;
		int index;

		if (manifest->pack_count >= PLAYLIST_MANIFEST_PACKS)
			return fail(why, why_size, "too many packs");
		pack = &manifest->packs[manifest->pack_count];
		memset(pack, 0, sizeof(*pack));
		if (!json_string(json, pack->id, sizeof(pack->id)) || !pack_id_valid(pack->id) || !take(json, ':'))
			return fail(why, why_size, "a pack's id is not one");
		for (index = 0; index < manifest->pack_count; index++)
		{
			if (!strcmp(manifest->packs[index].id, pack->id))
				return fail(why, why_size, "a pack is there twice");
		}
		if (!read_pack(json, pack, why, why_size))
			return 0;
		manifest->pack_count++;
	} while (take(json, ','));
	return take(json, '}') || fail(why, why_size, "packs is not closed");
}

/* a file's entry; its pack's id into pack_id (resolved once packs are read) */
static int read_file(struct json *json, struct playlist_file *file, char *pack_id, char *why, int why_size)
{
	enum { HAS_FOLDER = 1, HAS_FILE = 2, HAS_SIZE = 4, HAS_SOURCE = 8, HAS_MAP = 16, HAS_TITLE = 32, HAS_PATH = 64,
		HAS_PACK = 128, HAS_SHA = 256 };
	int seen = 0;
	char source[16] = "", sha[80];

	memset(file, 0, sizeof(*file));
	file->pack = -1;
	pack_id[0] = 0;
	if (!take(json, '{'))
		return fail(why, why_size, "a file's entry is not an object");
	if (!take(json, '}'))
	{
		do
		{
			char key[32];
			int bit = 0, ok;

			if (!json_string(json, key, sizeof(key)) || !take(json, ':'))
				return fail(why, why_size, "a file's field is not one");
			if (!strcmp(key, "map"))
				bit = HAS_MAP, ok = json_string(json, file->map, sizeof(file->map)) && map_name_valid(file->map);
			else if (!strcmp(key, "title"))
				bit = HAS_TITLE, ok = json_string(json, file->title, sizeof(file->title)) && text_printable(file->title, 1);
			else if (!strcmp(key, "folder"))
				bit = HAS_FOLDER, ok = json_string(json, file->folder, sizeof(file->folder)) &&
					playlist_manifest_folder_valid(file->folder);
			else if (!strcmp(key, "file"))
				bit = HAS_FILE, ok = json_string(json, file->file, sizeof(file->file)) &&
					playlist_manifest_file_valid(file->file);
			else if (!strcmp(key, "path"))
				bit = HAS_PATH, ok = json_string(json, file->path, sizeof(file->path)) &&
					playlist_manifest_path_valid(file->path);
			else if (!strcmp(key, "pack"))
				bit = HAS_PACK, ok = json_string(json, pack_id, PLAYLIST_MANIFEST_PACK_ID_SIZE) && pack_id_valid(pack_id);
			else if (!strcmp(key, "source"))
				bit = HAS_SOURCE, ok = json_string(json, source, sizeof(source));
			else if (!strcmp(key, "sha256"))
				bit = HAS_SHA, ok = json_string(json, sha, sizeof(sha)) && sha256_from_hex(sha, file->sha256);
			else if (!strcmp(key, "size"))
			{
				long long value = 0;

				bit = HAS_SIZE;
				ok = json_integer(json, 1, PLAYLIST_MANIFEST_MAXIMUM_SIZE, &value);
				file->size = (unsigned int)value;
			}
			else
				ok = json_skip(json, 2);
			if (!ok || (seen & bit))
			{
				snprintf(why, (size_t)why_size, "a file's %s is wrong", key);
				return 0;
			}
			seen |= bit;
		} while (take(json, ','));
		if (!take(json, '}'))
			return fail(why, why_size, "a file's entry is not closed");
	}
	if ((seen & (HAS_FOLDER | HAS_FILE | HAS_SOURCE)) != (HAS_FOLDER | HAS_FILE | HAS_SOURCE))
		return fail(why, why_size, "a file has no folder, file or source");
	file->has_sha256 = (seen & HAS_SHA) != 0;
	if (!strcmp(source, "halonet"))
		file->source = PLAYLIST_SOURCE_HALONET;
	else if (!strcmp(source, "chupathingy"))
		file->source = PLAYLIST_SOURCE_CHUPATHINGY;
	else if (!strcmp(source, "own"))
		file->source = PLAYLIST_SOURCE_OWN;
	else
		return fail(why, why_size, "a file's source is not one this build knows");
	if (file->source != PLAYLIST_SOURCE_OWN && (seen & (HAS_SIZE | HAS_SHA)) != (HAS_SIZE | HAS_SHA))
		return fail(why, why_size, "a file to download has no size or SHA-256");
	if ((file->source == PLAYLIST_SOURCE_CHUPATHINGY) != ((seen & HAS_PATH) != 0))
		return fail(why, why_size, "a path is for Chupathingy's files alone, and each has one");
	if (file->source == PLAYLIST_SOURCE_OWN && !(seen & HAS_PACK))
		return fail(why, why_size, "a file of the player's own has no pack");
	/* (nothing downloaded into the Xbox's maps; the repositories' maps go in
	maps_ce, Chupathingy's in maps_ce or a pack's folder) */
	if (file->source != PLAYLIST_SOURCE_OWN && !strcmp(file->folder, "maps"))
		return fail(why, why_size, "nothing is downloaded into maps");
	if (file->source == PLAYLIST_SOURCE_HALONET && strcmp(file->folder, "maps_ce"))
		return fail(why, why_size, "the community repositories' maps go in maps_ce");
	return 1;
}

static int read_files(struct json *json, struct playlist_manifest *manifest, char pack_ids[][PLAYLIST_MANIFEST_PACK_ID_SIZE],
	char *why, int why_size)
{
	if (!take(json, '['))
		return fail(why, why_size, "files is not a list");
	if (take(json, ']'))
		return fail(why, why_size, "files is empty");
	do
	{
		int index;
		struct playlist_file *file;

		if (manifest->file_count >= PLAYLIST_MANIFEST_FILES)
			return fail(why, why_size, "too many files");
		file = &manifest->files[manifest->file_count];
		if (!read_file(json, file, pack_ids[manifest->file_count], why, why_size))
			return 0;
		for (index = 0; index < manifest->file_count; index++)
		{
			if (!strcmp(manifest->files[index].folder, file->folder) && !strcmp(manifest->files[index].file, file->file))
				return fail(why, why_size, "a file is there twice");
		}
		manifest->file_count++;
	} while (take(json, ','));
	return take(json, ']') || fail(why, why_size, "files is not closed");
}

/* ---------- public code */

int playlist_manifest_parse(const char *document, size_t size, struct playlist_manifest *manifest, char *why,
	int why_size)
{
	static char pack_ids[PLAYLIST_MANIFEST_FILES][PLAYLIST_MANIFEST_PACK_ID_SIZE];
	enum { HAS_FORMAT = 1, HAS_PLAYLIST = 2, HAS_NAME = 4, HAS_SERIAL = 8, HAS_ISSUED = 16, HAS_PACKS = 32,
		HAS_FILES = 64 };
	struct json json;
	int seen = 0, index;

	memset(manifest, 0, sizeof(*manifest));
	if (size > PLAYLIST_MANIFEST_DOCUMENT_SIZE)
		return fail(why, why_size, "it is too large");
	json.at = document;
	json.end = document + size;
	if (!take(&json, '{'))
		return fail(why, why_size, "it is not a JSON object");
	if (peek(&json, '}'))
		return fail(why, why_size, "it is not a playlist's map set");
	do
	{
		char key[32];
		int bit = 0, ok;
		long long value = 0;

		if (!json_string(&json, key, sizeof(key)) || !take(&json, ':'))
			return fail(why, why_size, "a field is not one");
		if (!strcmp(key, "playlist_maps"))
		{
			bit = HAS_FORMAT;
			ok = json_integer(&json, 0, 1000000, &value);
			if (ok && value != PLAYLIST_MANIFEST_FORMAT)
				return fail(why, why_size, "its format is not one this build reads");
		}
		else if (!strcmp(key, "playlist"))
			bit = HAS_PLAYLIST, ok = json_string(&json, manifest->playlist, sizeof(manifest->playlist)) &&
				playlist_manifest_id_valid(manifest->playlist);
		else if (!strcmp(key, "name"))
			bit = HAS_NAME, ok = json_string(&json, manifest->name, sizeof(manifest->name)) &&
				text_printable(manifest->name, 0);
		else if (!strcmp(key, "serial"))
		{
			bit = HAS_SERIAL;
			ok = json_integer(&json, 1, 0xFFFFFFFELL, &value);
			manifest->serial = (unsigned int)value;
		}
		else if (!strcmp(key, "issued"))
			bit = HAS_ISSUED, ok = json_integer(&json, 0, 1LL << 53, &manifest->issued);
		else if (!strcmp(key, "packs"))
		{
			bit = HAS_PACKS;
			if (seen & bit)
				ok = 0;
			else if (!read_packs(&json, manifest, why, why_size))
				return 0;
			else
				ok = 1;
		}
		else if (!strcmp(key, "files"))
		{
			bit = HAS_FILES;
			if (seen & bit)
				ok = 0;
			else if (!read_files(&json, manifest, pack_ids, why, why_size))
				return 0;
			else
				ok = 1;
		}
		else
			ok = json_skip(&json, 1);
		if (!ok || (seen & bit))
		{
			snprintf(why, (size_t)why_size, "its %s is wrong", key);
			return 0;
		}
		seen |= bit;
	} while (take(&json, ','));
	if (!take(&json, '}'))
		return fail(why, why_size, "it is not closed");
	skip_space(&json);
	if (json.at != json.end)
		return fail(why, why_size, "something follows it");
	if ((seen & (HAS_FORMAT | HAS_PLAYLIST | HAS_NAME | HAS_SERIAL | HAS_ISSUED | HAS_FILES)) !=
		(HAS_FORMAT | HAS_PLAYLIST | HAS_NAME | HAS_SERIAL | HAS_ISSUED | HAS_FILES))
	{
		return fail(why, why_size, "it lacks a field it must have");
	}
	/* (each file's pack, by its id) */
	for (index = 0; index < manifest->file_count; index++)
	{
		int pack;

		if (!pack_ids[index][0])
			continue;
		for (pack = 0; pack < manifest->pack_count && strcmp(manifest->packs[pack].id, pack_ids[index]); pack++)
			;
		if (pack >= manifest->pack_count)
			return fail(why, why_size, "a file names a pack it does not have");
		manifest->files[index].pack = (signed char)pack;
	}
	return 1;
}

int playlist_manifest_check(const char *document, size_t size, const char *signature, size_t signature_length,
	const char *playlist, struct playlist_manifest *manifest, char *why, int why_size)
{
	static unsigned char message[sizeof(PLAYLIST_MANIFEST_CONTEXT) - 1 + PLAYLIST_MANIFEST_DOCUMENT_SIZE];
	unsigned char bytes[SIGNATURE_SIZE];
	size_t context_size = sizeof(PLAYLIST_MANIFEST_CONTEXT) - 1;
	size_t index;
	int signer = -1;

	if (size > PLAYLIST_MANIFEST_DOCUMENT_SIZE)
		return fail(why, why_size, "it is too large");
	while (signature_length > SIGNATURE_SIZE * 2 && (signature[signature_length - 1] == '\n' ||
		signature[signature_length - 1] == '\r' || signature[signature_length - 1] == ' ' ||
		signature[signature_length - 1] == '\t'))
	{
		signature_length--;
	}
	if (signature_length != SIGNATURE_SIZE * 2)
		return fail(why, why_size, "its signature is not 128 hex digits");
	for (index = 0; index < SIGNATURE_SIZE; index++)
	{
		int high = hex_digit(signature[index * 2]), low = hex_digit(signature[index * 2 + 1]);

		if (high < 0 || low < 0)
			return fail(why, why_size, "its signature is not 128 hex digits");
		bytes[index] = (unsigned char)(high << 4 | low);
	}
	memcpy(message, PLAYLIST_MANIFEST_CONTEXT, context_size);
	memcpy(message + context_size, document, size);
	for (index = 0; index < PLAYLIST_KEY_COUNT && signer < 0; index++)
	{
		unsigned char any = 0;
		int byte;

		for (byte = 0; byte < 32; byte++)
			any |= delta_public_keys[index][byte];
		if (any && crypto_ed25519_check(bytes, delta_public_keys[index], message, context_size + size) == 0)
			signer = (int)index;
	}
	if (signer < 0)
		return fail(why, why_size, "its signature does not match");
	if (!playlist_manifest_parse(document, size, manifest, why, why_size))
		return 0;
	if (PLAYLIST_EPOCH(manifest->serial) > delta_key_last_epochs[signer])
		return fail(why, why_size, "its serial's epoch is past its key's");
	if (playlist && strcmp(playlist, manifest->playlist))
		return fail(why, why_size, "it is another playlist's");
	return 1;
}

/* ---------- SHA-256 (FIPS 180-4) */

static const unsigned int sha256_constants[64] =
{
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROTATE(value, count) (((value) >> (count)) | ((value) << (32 - (count))))

static void sha256_block(unsigned int *state, const unsigned char *block)
{
	unsigned int words[64], a, b, c, d, e, f, g, h;
	int index;

	for (index = 0; index < 16; index++)
	{
		words[index] = (unsigned int)block[index * 4] << 24 | (unsigned int)block[index * 4 + 1] << 16 |
			(unsigned int)block[index * 4 + 2] << 8 | block[index * 4 + 3];
	}
	for (index = 16; index < 64; index++)
	{
		unsigned int s0 = ROTATE(words[index - 15], 7) ^ ROTATE(words[index - 15], 18) ^ (words[index - 15] >> 3);
		unsigned int s1 = ROTATE(words[index - 2], 17) ^ ROTATE(words[index - 2], 19) ^ (words[index - 2] >> 10);

		words[index] = words[index - 16] + s0 + words[index - 7] + s1;
	}
	a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6], h = state[7];
	for (index = 0; index < 64; index++)
	{
		unsigned int s1 = ROTATE(e, 6) ^ ROTATE(e, 11) ^ ROTATE(e, 25);
		unsigned int choice = (e & f) ^ (~e & g);
		unsigned int first = h + s1 + choice + sha256_constants[index] + words[index];
		unsigned int s0 = ROTATE(a, 2) ^ ROTATE(a, 13) ^ ROTATE(a, 22);
		unsigned int majority = (a & b) ^ (a & c) ^ (b & c);
		unsigned int second = s0 + majority;

		h = g, g = f, f = e, e = d + first, d = c, c = b, b = a, a = first + second;
	}
	state[0] += a, state[1] += b, state[2] += c, state[3] += d;
	state[4] += e, state[5] += f, state[6] += g, state[7] += h;
}

void playlist_sha256_begin(struct playlist_sha256 *context)
{
	static const unsigned int initial[8] =
	{
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
	};

	memcpy(context->state, initial, sizeof(initial));
	context->length = 0;
	context->used = 0;
}

void playlist_sha256_add(struct playlist_sha256 *context, const void *data, size_t size)
{
	const unsigned char *bytes = (const unsigned char *)data;

	context->length += size;
	while (size)
	{
		size_t count = 64 - context->used;

		if (count > size)
			count = size;
		/* (whole blocks straight from the data) */
		if (!context->used && size >= 64)
		{
			sha256_block(context->state, bytes);
			bytes += 64;
			size -= 64;
			continue;
		}
		memcpy(context->block + context->used, bytes, count);
		context->used += (unsigned int)count;
		bytes += count;
		size -= count;
		if (context->used == 64)
		{
			sha256_block(context->state, context->block);
			context->used = 0;
		}
	}
}

void playlist_sha256_end(struct playlist_sha256 *context, unsigned char digest[PLAYLIST_MANIFEST_SHA256_SIZE])
{
	unsigned long long bits = context->length * 8;
	int index;

	context->block[context->used++] = 0x80;
	if (context->used > 56)
	{
		memset(context->block + context->used, 0, 64 - context->used);
		sha256_block(context->state, context->block);
		context->used = 0;
	}
	memset(context->block + context->used, 0, 56 - context->used);
	for (index = 0; index < 8; index++)
		context->block[63 - index] = (unsigned char)(bits >> (index * 8));
	sha256_block(context->state, context->block);
	for (index = 0; index < 32; index++)
		digest[index] = (unsigned char)(context->state[index / 4] >> (24 - (index % 4) * 8));
}
