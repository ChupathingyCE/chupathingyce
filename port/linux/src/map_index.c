/*
MAP_INDEX.C

Chupathingy maps' signed index and mirror list checked and read
(map_index.h). The signature first, over the document's context line and
its exact bytes, by one of maps_key.h's keys, the key allowed the serial's
epoch as for Delta's legacy table; only then is the document read, by a
strict parser (json_reader.h), and it must not have expired.

The index is read whole once, when it is checked; a map is found, or a
hash looked for on the blacklist, by reading the checked document again,
so no list of maps is kept beside it.
*/

#include "map_index.h"
#include "maps_key.h"
#include "json_reader.h"
#include "monocypher-ed25519.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

#define MAP_INDEX_EPOCH(serial) ((unsigned int)(serial) >> 24)

enum
{
	SIGNATURE_SIZE = 64,
	/* a field's name, at most */
	KEY_SIZE = 32,
};

/* ---------- small checks */

static int fail(char *why, int why_size, const char *text)
{
	if (why && why_size > 0)
		snprintf(why, (size_t)why_size, "%s", text);
	return 0;
}

static int is_lower_or_digit(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

static char lower(char c)
{
	return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

/* a map's file name as the index has it: lowercase letters, digits, '_',
'-' and '.', not starting with '.' and holding no ".." */
static int name_valid(const char *text)
{
	size_t length = strlen(text), index;

	if (length < 1 || length > MAP_INDEX_NAME_SIZE - 1 || text[0] == '.' || strstr(text, ".."))
		return 0;
	for (index = 0; index < length; index++)
	{
		if (!is_lower_or_digit(text[index]) && text[index] != '_' && text[index] != '-' && text[index] != '.')
			return 0;
	}
	return 1;
}

/* printable text for people: no control characters, C1's in UTF-8 (U+0080
to U+009F, a terminal's escapes among them) included; other UTF-8 kept */
static int text_printable(const char *text)
{
	for (; *text; text++)
	{
		unsigned char c = (unsigned char)*text;

		if (c < 0x20 || c == 0x7f || (c == 0xc2 && (unsigned char)text[1] >= 0x80 && (unsigned char)text[1] <= 0x9f))
			return 0;
	}
	return 1;
}

/* a field's name and its ':': printable ASCII alone, as a name a field is
refused by is logged ("its <name> is wrong") */
static int json_key(struct json *json, char *key, size_t size)
{
	size_t index;

	if (!json_string(json, key, size) || !take(json, ':'))
		return 0;
	for (index = 0; key[index]; index++)
	{
		if ((unsigned char)key[index] <= 0x20 || (unsigned char)key[index] >= 0x7f)
			return 0;
	}
	return 1;
}

/* digits lowercase hex digits into bytes (digits / 2 of them) */
static int hex_bytes(const char *text, unsigned char *bytes, size_t digits)
{
	size_t index;

	if (strlen(text) != digits)
		return 0;
	for (index = 0; index < digits; index++)
	{
		if (!((text[index] >= '0' && text[index] <= '9') || (text[index] >= 'a' && text[index] <= 'f')))
			return 0;
	}
	for (index = 0; index < digits / 2; index++)
		bytes[index] = (unsigned char)(hex_digit(text[index * 2]) << 4 | hex_digit(text[index * 2 + 1]));
	return 1;
}

static unsigned int hex_number(const unsigned char *bytes)
{
	return (unsigned int)bytes[0] << 24 | (unsigned int)bytes[1] << 16 | (unsigned int)bytes[2] << 8 | bytes[3];
}

/* a mirror's address: https:// (or http:// to this machine, the tests'),
then a host and path of plain characters: no query, fragment, user, white
space or "..", and no '/' ending it */
static int base_url_valid(const char *text)
{
	size_t length = strlen(text), index;
	const char *rest;

	if (!strncmp(text, "https://", 8))
		rest = text + 8;
	else if ((!strncmp(text, "http://127.0.0.1", 16) || !strncmp(text, "http://localhost", 16)) &&
		(!text[16] || text[16] == ':' || text[16] == '/'))
	{
		/* (the host itself, not one it begins: http://localhost.example.org) */
		rest = text + 7;
	}
	else
		return 0;
	if (length > MAP_MIRROR_URL_SIZE - 1 || !*rest || *rest == '/' || text[length - 1] == '/' || strstr(text, ".."))
		return 0;
	for (index = 0; rest[index]; index++)
	{
		char c = rest[index];

		if (!is_lower_or_digit(c) && !(c >= 'A' && c <= 'Z') && !strchr("-._~:/%", c))
			return 0;
	}
	return 1;
}

/* ---------- the signature */

static int signature_check(const char *context, const char *document, size_t size, size_t maximum,
	const char *signature, size_t signature_length, int *signer, char *why, int why_size)
{
	unsigned char bytes[SIGNATURE_SIZE];
	size_t context_size = strlen(context);
	unsigned char *message;
	int index;
	int keys = 0;

	*signer = -1;
	if (size > maximum)
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
	message = (unsigned char *)malloc(context_size + size);
	if (!message)
		return fail(why, why_size, "no memory to check it");
	memcpy(message, context, context_size);
	memcpy(message + context_size, document, size);
	for (index = 0; index < MAPS_KEY_COUNT && *signer < 0; index++)
	{
		unsigned char any = 0;
		int byte;

		for (byte = 0; byte < 32; byte++)
			any |= maps_public_keys[index][byte];
		if (!any)
			continue;
		keys++;
		if (crypto_ed25519_check(bytes, maps_public_keys[index], message, context_size + size) == 0)
			*signer = index;
	}
	free(message);
	if (!keys)
		return fail(why, why_size, "this build has no key for it");
	if (*signer < 0)
		return fail(why, why_size, "its signature does not match");
	return 1;
}

/* ---------- the index's parts */

/* what a read of the index is for: every part checked, and a map or a
blacklist entry looked for */
struct index_reader
{
	/* (find: the name, lowercase, and version and kind; found: matches) */
	const char *name;
	unsigned int version;
	int kind;
	int matches;
	struct map_index_entry *entry;
	/* (blacklisted: the hashes, either NULL) */
	const unsigned char *sha256;
	const unsigned char *map_hash;
	int blocked;
	struct map_index_block *block;
};

/* one of the index's maps; *usable 0 for one of a kind or status this
build does not read */
static int read_map(struct json *json, struct index_reader *reader, int *usable, char *why, int why_size)
{
	enum
	{
		HAS_SHA256 = 1 << 0,
		HAS_NAME = 1 << 1,
		HAS_KIND = 1 << 2,
		HAS_SIZE = 1 << 3,
		HAS_MAP_HASH = 1 << 4,
		HAS_VERSION = 1 << 5,
		HAS_STATUS = 1 << 6,
		HAS_ALIASES = 1 << 7,
		HAS_CHECKSUM = 1 << 8,
	};
	struct map_index_entry entry;
	char aliases[MAP_INDEX_ALIASES][MAP_INDEX_NAME_SIZE];
	int alias_count = 0;
	int seen = 0, approved = 0, known_kind = 0;
	unsigned char version[4];

	memset(&entry, 0, sizeof(entry));
	*usable = 0;
	if (!take(json, '{'))
		return fail(why, why_size, "a map is not an object");
	if (take(json, '}'))
		return fail(why, why_size, "a map is empty");
	do
	{
		/* (text cleared: a value that is not a string is never compared as one) */
		char key[KEY_SIZE], text[MAP_INDEX_REASON_SIZE] = "";
		long long value = 0;
		int bit = 0, ok;

		if (!json_key(json, key, sizeof(key)))
			return fail(why, why_size, "a map's field is not one");
		if (!strcmp(key, "sha256"))
			bit = HAS_SHA256, ok = json_string(json, text, sizeof(text)) && hex_bytes(text, entry.sha256, 64);
		else if (!strcmp(key, "map_hash"))
			bit = HAS_MAP_HASH, ok = json_string(json, text, sizeof(text)) && hex_bytes(text, entry.map_hash, 64);
		else if (!strcmp(key, "name"))
			bit = HAS_NAME, ok = json_string(json, entry.name, sizeof(entry.name)) && name_valid(entry.name);
		else if (!strcmp(key, "kind"))
		{
			bit = HAS_KIND;
			ok = json_string(json, text, sizeof(text));
			known_kind = 1;
			if (!strcmp(text, "ce"))
				entry.kind = MAP_INDEX_KIND_CE;
			else if (!strcmp(text, "pc"))
				entry.kind = MAP_INDEX_KIND_PC;
			else if (!strcmp(text, "xbox"))
				entry.kind = MAP_INDEX_KIND_XBOX;
			else
				known_kind = 0;
		}
		else if (!strcmp(key, "size"))
		{
			bit = HAS_SIZE;
			ok = json_integer(json, 1, MAP_INDEX_MAXIMUM_SIZE, &value);
			entry.size = (unsigned int)value;
		}
		else if (!strcmp(key, "version"))
			bit = HAS_VERSION, ok = json_string(json, text, sizeof(text)) && hex_bytes(text, version, 8);
		else if (!strcmp(key, "header_checksum"))
		{
			unsigned char checksum[4];

			bit = HAS_CHECKSUM;
			ok = json_string(json, text, sizeof(text)) && hex_bytes(text, checksum, 8);
			entry.header_checksum = ok ? hex_number(checksum) : 0;
		}
		else if (!strcmp(key, "status"))
		{
			bit = HAS_STATUS;
			ok = json_string(json, text, sizeof(text));
			approved = ok && !strcmp(text, "approved");
		}
		else if (!strcmp(key, "aliases"))
		{
			bit = HAS_ALIASES;
			ok = take(json, '[');
			if (ok && !take(json, ']'))
			{
				do
				{
					ok = alias_count < MAP_INDEX_ALIASES &&
						json_string(json, aliases[alias_count], sizeof(aliases[alias_count])) &&
						name_valid(aliases[alias_count]);
					alias_count++;
				} while (ok && take(json, ','));
				ok = ok && take(json, ']');
			}
		}
		else
			ok = json_skip(json, 1);
		if (!ok || (seen & bit))
		{
			snprintf(why, (size_t)why_size, "a map's %s is wrong", key);
			return 0;
		}
		seen |= bit;
	} while (take(json, ','));
	if (!take(json, '}'))
		return fail(why, why_size, "a map is not closed");
	if ((seen & (HAS_SHA256 | HAS_NAME | HAS_KIND | HAS_SIZE | HAS_MAP_HASH | HAS_VERSION | HAS_STATUS)) !=
		(HAS_SHA256 | HAS_NAME | HAS_KIND | HAS_SIZE | HAS_MAP_HASH | HAS_VERSION | HAS_STATUS))
	{
		return fail(why, why_size, "a map lacks a field it must have");
	}
	/* (its version is its map hash's, as every host computes it) */
	entry.version = hex_number(entry.map_hash) ? hex_number(entry.map_hash) : 1;
	if (hex_number(version) != entry.version)
		return fail(why, why_size, "a map's version is not its map hash's");
	if (!known_kind || !approved)
		return 1;
	*usable = 1;
	/* (looked for: its name or an alias, its version or an older host's
	checksum, of the kind asked for) */
	if (reader && reader->name && entry.kind == reader->kind)
	{
		int named = !strcmp(entry.name, reader->name), index;

		for (index = 0; index < alias_count && !named; index++)
			named = !strcmp(aliases[index], reader->name);
		if (named && (!reader->version || reader->version == entry.version ||
			(seen & HAS_CHECKSUM && reader->version == entry.header_checksum)))
		{
			/* (the first kept; another file of that name and version, which
			only "any" (0) or a checksum two versions share can find, makes
			it a guess, and none is taken) */
			if (!reader->matches)
			{
				*reader->entry = entry;
				reader->matches = 1;
			}
			else if (memcmp(reader->entry->sha256, entry.sha256, MAP_INDEX_HASH_SIZE))
				reader->matches++;
		}
	}
	return 1;
}

static int read_block(struct json *json, struct index_reader *reader, char *why, int why_size)
{
	enum
	{
		HAS_HASH = 1 << 0,
		HAS_KIND = 1 << 1,
		HAS_REASON = 1 << 2,
	};
	struct map_index_block block;
	char hash_text[MAP_INDEX_REASON_SIZE] = "";
	int seen = 0, known_kind = 0;

	memset(&block, 0, sizeof(block));
	if (!take(json, '{'))
		return fail(why, why_size, "a blacklist entry is not an object");
	if (take(json, '}'))
		return fail(why, why_size, "a blacklist entry is empty");
	do
	{
		/* (text cleared: a value that is not a string is never compared as one) */
		char key[KEY_SIZE], text[MAP_INDEX_REASON_SIZE] = "";
		long long value;
		int bit = 0, ok;

		if (!json_key(json, key, sizeof(key)))
			return fail(why, why_size, "a blacklist entry's field is not one");
		if (!strcmp(key, "hash"))
			bit = HAS_HASH, ok = json_string(json, hash_text, sizeof(hash_text));
		else if (!strcmp(key, "kind"))
		{
			bit = HAS_KIND;
			ok = json_string(json, text, sizeof(text));
			known_kind = 1;
			if (!strcmp(text, "sha256"))
				block.kind = MAP_INDEX_BLOCK_SHA256;
			else if (!strcmp(text, "map_hash"))
				block.kind = MAP_INDEX_BLOCK_MAP_HASH;
			else
				known_kind = 0;
		}
		else if (!strcmp(key, "name"))
			ok = json_string(json, block.name, sizeof(block.name)) && text_printable(block.name);
		else if (!strcmp(key, "reason"))
			bit = HAS_REASON, ok = json_string(json, block.reason, sizeof(block.reason)) && text_printable(block.reason);
		else if (!strcmp(key, "since"))
			ok = json_integer(json, 0, 1LL << 53, &value);
		else
			ok = json_skip(json, 1);
		if (!ok || (seen & bit))
		{
			snprintf(why, (size_t)why_size, "a blacklist entry's %s is wrong", key);
			return 0;
		}
		seen |= bit;
	} while (take(json, ','));
	if (!take(json, '}'))
		return fail(why, why_size, "a blacklist entry is not closed");
	if ((seen & (HAS_HASH | HAS_KIND)) != (HAS_HASH | HAS_KIND))
		return fail(why, why_size, "a blacklist entry lacks a field it must have");
	if (!hex_bytes(hash_text, block.hash, 64))
		return fail(why, why_size, "a blacklist entry's hash is wrong");
	/* (a kind of hash this build does not know: one it has no file of) */
	if (!known_kind)
		return 1;
	if (reader && !reader->blocked)
	{
		const unsigned char *hash = block.kind == MAP_INDEX_BLOCK_SHA256 ? reader->sha256 : reader->map_hash;

		if (hash && !memcmp(hash, block.hash, MAP_INDEX_HASH_SIZE))
		{
			reader->blocked = 1;
			if (reader->block)
				*reader->block = block;
		}
	}
	return 1;
}

static int read_index(const char *document, size_t size, struct map_index_info *info, struct index_reader *reader,
	char *why, int why_size)
{
	enum
	{
		HAS_FORMAT = 1 << 0,
		HAS_SERIAL = 1 << 1,
		HAS_ISSUED = 1 << 2,
		HAS_EXPIRES = 1 << 3,
		HAS_REFRESH = 1 << 4,
		HAS_HALT = 1 << 5,
		HAS_HALT_REASON = 1 << 6,
		HAS_MAPS = 1 << 7,
		HAS_BLACKLIST = 1 << 8,
	};
	struct json json;
	int seen = 0;

	memset(info, 0, sizeof(*info));
	if (size > MAP_INDEX_DOCUMENT_SIZE)
		return fail(why, why_size, "it is too large");
	json.at = document;
	json.end = document + size;
	if (!take(&json, '{'))
		return fail(why, why_size, "it is not an object");
	if (peek(&json, '}'))
		return fail(why, why_size, "it is empty");
	do
	{
		char key[KEY_SIZE];
		long long value = 0;
		int bit = 0, ok;

		if (!json_key(&json, key, sizeof(key)))
			return fail(why, why_size, "a field is not one");
		if (!strcmp(key, "map_index"))
		{
			bit = HAS_FORMAT;
			ok = json_integer(&json, 0, 1000000, &value);
			if (ok && value != MAP_INDEX_FORMAT)
				return fail(why, why_size, "its format is not one this build reads");
		}
		else if (!strcmp(key, "serial"))
		{
			bit = HAS_SERIAL;
			ok = json_integer(&json, 1, 0xFFFFFFFELL, &value);
			info->serial = (unsigned int)value;
		}
		else if (!strcmp(key, "issued"))
			bit = HAS_ISSUED, ok = json_integer(&json, 0, 1LL << 53, &info->issued);
		else if (!strcmp(key, "expires"))
			bit = HAS_EXPIRES, ok = json_integer(&json, 0, 1LL << 53, &info->expires);
		else if (!strcmp(key, "refresh"))
		{
			bit = HAS_REFRESH;
			ok = json_integer(&json, MAP_INDEX_REFRESH_MINIMUM, MAP_INDEX_REFRESH_MAXIMUM, &value);
			info->refresh = (int)value;
		}
		else if (!strcmp(key, "halt"))
		{
			bit = HAS_HALT;
			if (json_literal(&json, "true"))
				ok = 1, info->halt = 1;
			else
				ok = json_literal(&json, "false");
		}
		else if (!strcmp(key, "halt_reason"))
			bit = HAS_HALT_REASON, ok = json_string(&json, info->halt_reason, sizeof(info->halt_reason)) &&
				text_printable(info->halt_reason);
		else if (!strcmp(key, "maps") || !strcmp(key, "blacklist"))
		{
			int maps = !strcmp(key, "maps");

			bit = maps ? HAS_MAPS : HAS_BLACKLIST;
			if (seen & bit)
				ok = 0;
			else if (!take(&json, '['))
				ok = 0;
			else
			{
				ok = 1;
				if (!take(&json, ']'))
				{
					do
					{
						int usable = 0;

						if (maps ? !read_map(&json, reader, &usable, why, why_size) :
							!read_block(&json, reader, why, why_size))
						{
							return 0;
						}
						if (maps)
							info->map_count += usable;
						else
							info->block_count++;
					} while (take(&json, ','));
					ok = take(&json, ']');
				}
			}
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
	if ((seen & (HAS_FORMAT | HAS_SERIAL | HAS_ISSUED | HAS_EXPIRES | HAS_MAPS | HAS_BLACKLIST)) !=
		(HAS_FORMAT | HAS_SERIAL | HAS_ISSUED | HAS_EXPIRES | HAS_MAPS | HAS_BLACKLIST))
	{
		return fail(why, why_size, "it lacks a field it must have");
	}
	if (info->expires <= info->issued)
		return fail(why, why_size, "it expires before it was issued");
	if (!(seen & HAS_REFRESH))
		info->refresh = 3600;
	return 1;
}

/* ---------- the mirror list's parts */

static int read_mirror(struct json *json, struct map_mirror *mirror, char *why, int why_size)
{
	enum
	{
		HAS_ID = 1 << 0,
		HAS_URL = 1 << 1,
		HAS_PRIORITY = 1 << 2,
		HAS_STATUS = 1 << 3,
		HAS_SHARD = 1 << 4,
	};
	int seen = 0;

	memset(mirror, 0, sizeof(*mirror));
	if (!take(json, '{'))
		return fail(why, why_size, "a mirror is not an object");
	if (take(json, '}'))
		return fail(why, why_size, "a mirror is empty");
	do
	{
		/* (text cleared: a value that is not a string is never compared as one) */
		char key[KEY_SIZE], text[MAP_INDEX_REASON_SIZE] = "";
		int bit = 0, ok;
		size_t index;

		if (!json_key(json, key, sizeof(key)))
			return fail(why, why_size, "a mirror's field is not one");
		if (!strcmp(key, "id"))
		{
			bit = HAS_ID;
			ok = json_string(json, mirror->id, sizeof(mirror->id)) && mirror->id[0];
			for (index = 0; ok && mirror->id[index]; index++)
				ok = is_lower_or_digit(mirror->id[index]) || mirror->id[index] == '-' || mirror->id[index] == '_';
		}
		else if (!strcmp(key, "base_url"))
			bit = HAS_URL, ok = json_string(json, mirror->base_url, sizeof(mirror->base_url)) &&
				base_url_valid(mirror->base_url);
		else if (!strcmp(key, "priority"))
			bit = HAS_PRIORITY, ok = json_integer(json, 0, 1000000000, &mirror->priority);
		else if (!strcmp(key, "status"))
		{
			bit = HAS_STATUS;
			ok = json_string(json, text, sizeof(text));
			mirror->status = !strcmp(text, "ok") ? MAP_MIRROR_OK : !strcmp(text, "stale") ? MAP_MIRROR_STALE :
				!strcmp(text, "bad") ? MAP_MIRROR_BAD : MAP_MIRROR_UNKNOWN;
		}
		else if (!strcmp(key, "shard"))
		{
			bit = HAS_SHARD;
			ok = take(json, '[');
			if (ok && !take(json, ']'))
			{
				do
				{
					char *shard;

					ok = mirror->shard_count < MAP_MIRROR_SHARDS;
					if (!ok)
						break;
					shard = mirror->shards[mirror->shard_count++];
					ok = json_string(json, shard, MAP_MIRROR_SHARD_SIZE) && shard[0];
					for (index = 0; ok && shard[index]; index++)
						ok = (shard[index] >= '0' && shard[index] <= '9') || (shard[index] >= 'a' && shard[index] <= 'f');
				} while (ok && take(json, ','));
				ok = ok && take(json, ']');
			}
		}
		else if (!strcmp(key, "region") || !strcmp(key, "operator"))
			ok = json_string(json, text, sizeof(text)) && text_printable(text);
		else
			ok = json_skip(json, 1);
		if (!ok || (seen & bit))
		{
			snprintf(why, (size_t)why_size, "a mirror's %s is wrong", key);
			return 0;
		}
		seen |= bit;
	} while (take(json, ','));
	if (!take(json, '}'))
		return fail(why, why_size, "a mirror is not closed");
	if ((seen & (HAS_ID | HAS_URL | HAS_PRIORITY | HAS_STATUS)) != (HAS_ID | HAS_URL | HAS_PRIORITY | HAS_STATUS))
		return fail(why, why_size, "a mirror lacks a field it must have");
	return 1;
}

/* ---------- public code */

int map_index_parse(const char *document, size_t size, struct map_index_info *info, char *why, int why_size)
{
	return read_index(document, size, info, NULL, why, why_size);
}

int map_index_check(const char *document, size_t size, const char *signature, size_t signature_length, long long now,
	struct map_index_info *info, char *why, int why_size)
{
	if (!map_index_verify(document, size, signature, signature_length, info, why, why_size))
		return 0;
	if (now > info->expires)
		return fail(why, why_size, "it has expired");
	return 1;
}

int map_index_verify(const char *document, size_t size, const char *signature, size_t signature_length,
	struct map_index_info *info, char *why, int why_size)
{
	int signer;

	memset(info, 0, sizeof(*info));
	if (!signature_check(MAP_INDEX_CONTEXT, document, size, MAP_INDEX_DOCUMENT_SIZE, signature, signature_length,
		&signer, why, why_size))
	{
		return 0;
	}
	if (!read_index(document, size, info, NULL, why, why_size))
		return 0;
	if (MAP_INDEX_EPOCH(info->serial) > maps_key_last_epochs[signer])
		return fail(why, why_size, "its serial's epoch is past its key's");
	return 1;
}

int map_index_find(const char *document, size_t size, const char *name, unsigned int version, int kind,
	struct map_index_entry *entry, char *why, int why_size)
{
	struct map_index_info info;
	struct index_reader reader;
	char asked[MAP_INDEX_NAME_SIZE];
	size_t index;

	if (!name || strlen(name) >= sizeof(asked))
		return fail(why, why_size, "the name is too long");
	for (index = 0; name[index]; index++)
		asked[index] = lower(name[index]);
	asked[index] = 0;
	memset(&reader, 0, sizeof(reader));
	reader.name = asked;
	reader.version = version;
	reader.kind = kind;
	reader.entry = entry;
	if (!read_index(document, size, &info, &reader, why, why_size))
		return 0;
	if (!reader.matches)
		return fail(why, why_size, "Chupathingy maps doesn't have it");
	if (reader.matches > 1)
		return fail(why, why_size, "Chupathingy maps has several versions of it, and the host didn't say which");
	return 1;
}

int map_index_blacklisted(const char *document, size_t size, const unsigned char *sha256,
	const unsigned char *map_hash, struct map_index_block *block)
{
	struct map_index_info info;
	struct index_reader reader;
	char why[8];

	if (!sha256 && !map_hash)
		return 0;
	memset(&reader, 0, sizeof(reader));
	reader.sha256 = sha256;
	reader.map_hash = map_hash;
	reader.block = block;
	return read_index(document, size, &info, &reader, why, (int)sizeof(why)) && reader.blocked;
}

int map_mirrors_parse(const char *document, size_t size, struct map_mirrors *mirrors, char *why, int why_size)
{
	enum
	{
		HAS_FORMAT = 1 << 0,
		HAS_SERIAL = 1 << 1,
		HAS_ISSUED = 1 << 2,
		HAS_EXPIRES = 1 << 3,
		HAS_MIRRORS = 1 << 4,
	};
	struct json json;
	int seen = 0;

	memset(mirrors, 0, sizeof(*mirrors));
	if (size > MAP_MIRRORS_DOCUMENT_SIZE)
		return fail(why, why_size, "it is too large");
	json.at = document;
	json.end = document + size;
	if (!take(&json, '{'))
		return fail(why, why_size, "it is not an object");
	if (peek(&json, '}'))
		return fail(why, why_size, "it is empty");
	do
	{
		char key[KEY_SIZE];
		long long value = 0;
		int bit = 0, ok;

		if (!json_key(&json, key, sizeof(key)))
			return fail(why, why_size, "a field is not one");
		if (!strcmp(key, "map_mirrors"))
		{
			bit = HAS_FORMAT;
			ok = json_integer(&json, 0, 1000000, &value);
			if (ok && value != MAP_INDEX_FORMAT)
				return fail(why, why_size, "its format is not one this build reads");
		}
		else if (!strcmp(key, "serial"))
		{
			bit = HAS_SERIAL;
			ok = json_integer(&json, 1, 0xFFFFFFFELL, &value);
			mirrors->serial = (unsigned int)value;
		}
		else if (!strcmp(key, "issued"))
			bit = HAS_ISSUED, ok = json_integer(&json, 0, 1LL << 53, &mirrors->issued);
		else if (!strcmp(key, "expires"))
			bit = HAS_EXPIRES, ok = json_integer(&json, 0, 1LL << 53, &mirrors->expires);
		else if (!strcmp(key, "mirrors"))
		{
			bit = HAS_MIRRORS;
			ok = !(seen & bit) && take(&json, '[');
			if (ok && !take(&json, ']'))
			{
				do
				{
					if (mirrors->count >= MAP_MIRRORS)
						return fail(why, why_size, "it lists too many mirrors");
					if (!read_mirror(&json, &mirrors->mirrors[mirrors->count], why, why_size))
						return 0;
					mirrors->count++;
				} while (take(&json, ','));
				ok = take(&json, ']');
			}
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
	if ((seen & (HAS_FORMAT | HAS_SERIAL | HAS_ISSUED | HAS_EXPIRES | HAS_MIRRORS)) !=
		(HAS_FORMAT | HAS_SERIAL | HAS_ISSUED | HAS_EXPIRES | HAS_MIRRORS))
	{
		return fail(why, why_size, "it lacks a field it must have");
	}
	if (mirrors->expires <= mirrors->issued)
		return fail(why, why_size, "it expires before it was issued");
	return 1;
}

int map_mirrors_check(const char *document, size_t size, const char *signature, size_t signature_length, long long now,
	struct map_mirrors *mirrors, char *why, int why_size)
{
	if (!map_mirrors_verify(document, size, signature, signature_length, mirrors, why, why_size))
		return 0;
	if (now > mirrors->expires)
		return fail(why, why_size, "it has expired");
	return 1;
}

int map_mirrors_verify(const char *document, size_t size, const char *signature, size_t signature_length,
	struct map_mirrors *mirrors, char *why, int why_size)
{
	int signer;

	memset(mirrors, 0, sizeof(*mirrors));
	if (!signature_check(MAP_MIRRORS_CONTEXT, document, size, MAP_MIRRORS_DOCUMENT_SIZE, signature, signature_length,
		&signer, why, why_size))
	{
		return 0;
	}
	if (!map_mirrors_parse(document, size, mirrors, why, why_size))
		return 0;
	if (MAP_INDEX_EPOCH(mirrors->serial) > maps_key_last_epochs[signer])
		return fail(why, why_size, "its serial's epoch is past its key's");
	return 1;
}

int map_mirrors_choose(const struct map_mirrors *mirrors, const unsigned char *sha256, int *order, int maximum)
{
	char text[MAP_INDEX_HASH_SIZE * 2 + 1];
	int chosen[MAP_MIRRORS];
	int count = 0, index;

	map_index_hash_text(sha256, text);
	for (index = 0; index < mirrors->count && index < MAP_MIRRORS; index++)
	{
		const struct map_mirror *mirror = &mirrors->mirrors[index];
		int holds = !mirror->shard_count, shard, at;

		if (mirror->status != MAP_MIRROR_OK)
			continue;
		for (shard = 0; shard < mirror->shard_count && !holds; shard++)
			holds = !strncmp(text, mirror->shards[shard], strlen(mirror->shards[shard]));
		if (!holds)
			continue;
		/* (in priority's order, the list's for the same priority) */
		for (at = count; at > 0 && mirrors->mirrors[chosen[at - 1]].priority > mirror->priority; at--)
			chosen[at] = chosen[at - 1];
		chosen[at] = index;
		count++;
	}
	if (count > maximum)
		count = maximum;
	for (index = 0; index < count; index++)
		order[index] = chosen[index];
	return count;
}

void map_mirror_file_url(const struct map_mirror *mirror, const unsigned char *sha256, char *url, size_t size)
{
	char text[MAP_INDEX_HASH_SIZE * 2 + 1];

	map_index_hash_text(sha256, text);
	snprintf(url, size, "%s/maps/%.2s/%s.map", mirror->base_url, text, text);
}

void map_index_hash_text(const unsigned char *hash, char *text)
{
	static const char digits[] = "0123456789abcdef";
	int index;

	for (index = 0; index < MAP_INDEX_HASH_SIZE; index++)
	{
		text[index * 2] = digits[hash[index] >> 4];
		text[index * 2 + 1] = digits[hash[index] & 15];
	}
	text[MAP_INDEX_HASH_SIZE * 2] = 0;
}
