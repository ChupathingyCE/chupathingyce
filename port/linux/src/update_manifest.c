/*
UPDATE_MANIFEST.C

A release's list of its update files (update_manifest.h): read as hostile
JSON (its signature is checked first, but nothing here trusts it further than
it must), and the SHA-256 its files are checked against, here so that no
other library crosses the updater's line between the game's structure layouts
and the system's (update.h).
*/

#include "update_manifest.h"

#include <stdio.h>
#include <string.h>

#define UPDATE_MANIFEST_FORMAT "chupathingyce-updates 1"
/* (deeper than the list's own nesting, for what a later list adds) */
#define MANIFEST_JSON_DEPTH 16
#define MANIFEST_WORD_SIZE 128

struct manifest_json
{
	const char *at;
	const char *end;
	int depth;
};

/* one of the list's files, as read (a field that is missing or not one of
the shapes below stays empty) */
struct manifest_file
{
	char platform[MANIFEST_WORD_SIZE];
	char configuration[MANIFEST_WORD_SIZE];
	char kind[MANIFEST_WORD_SIZE];
	char tag[MANIFEST_WORD_SIZE];
	char name[MANIFEST_WORD_SIZE];
	char main_name[MANIFEST_WORD_SIZE];
	char sha256[UPDATE_SHA256_TEXT_SIZE];
	unsigned long long size;
	int has_size;
};

/* ---------- the JSON */

static void manifest_space(struct manifest_json *json)
{
	while (json->at < json->end &&
		(*json->at == ' ' || *json->at == '\t' || *json->at == '\n' || *json->at == '\r'))
	{
		json->at++;
	}
}

static int manifest_take(struct manifest_json *json, char c)
{
	manifest_space(json);
	if (json->at < json->end && *json->at == c)
	{
		json->at++;
		return 1;
	}
	return 0;
}

static int manifest_hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* a string: its text between the quotes, its escapes checked but not
decoded (escaped: whether it has any) */
static int manifest_string(struct manifest_json *json, const char **text, size_t *length, int *escaped)
{
	const char *start;

	*escaped = 0;
	if (!manifest_take(json, '"'))
		return 0;
	start = json->at;
	while (json->at < json->end)
	{
		unsigned char c = (unsigned char)*json->at;

		if (c == '"')
		{
			*text = start;
			*length = (size_t)(json->at - start);
			json->at++;
			return 1;
		}
		if (c < 0x20)
			return 0;
		if (c == '\\')
		{
			*escaped = 1;
			if (++json->at >= json->end)
				return 0;
			c = (unsigned char)*json->at;
			if (c == 'u')
			{
				int index;

				for (index = 1; index <= 4; index++)
				{
					if (json->at + index >= json->end || manifest_hex_digit(json->at[index]) < 0)
						return 0;
				}
				json->at += 4;
			}
			else if (!strchr("\"\\/bfnrt", c))
				return 0;
		}
		json->at++;
	}
	return 0;
}

static int manifest_is(const char *text, size_t length, const char *name)
{
	return length == strlen(name) && !memcmp(text, name, length);
}

static int manifest_digits(struct manifest_json *json)
{
	const char *start = json->at;

	while (json->at < json->end && *json->at >= '0' && *json->at <= '9')
		json->at++;
	return json->at > start;
}

static int manifest_literal(struct manifest_json *json, const char *literal)
{
	size_t length = strlen(literal);

	if ((size_t)(json->end - json->at) < length || memcmp(json->at, literal, length))
		return 0;
	json->at += length;
	return 1;
}

/* any value, checked and passed over */
static int manifest_skip(struct manifest_json *json)
{
	const char *text;
	size_t length;
	int escaped;
	char c;

	manifest_space(json);
	if (json->at >= json->end)
		return 0;
	c = *json->at;
	if (c == '"')
		return manifest_string(json, &text, &length, &escaped);
	if (c == '{' || c == '[')
	{
		char close = c == '{' ? '}' : ']';
		int ok = 1;

		if (++json->depth > MANIFEST_JSON_DEPTH)
			return 0;
		json->at++;
		if (!manifest_take(json, close))
		{
			do
			{
				if (c == '{' && (!manifest_string(json, &text, &length, &escaped) || !manifest_take(json, ':')))
					return 0;
				if (!manifest_skip(json))
					return 0;
			} while (manifest_take(json, ','));
			ok = manifest_take(json, close);
		}
		json->depth--;
		return ok;
	}
	if (c == 't')
		return manifest_literal(json, "true");
	if (c == 'f')
		return manifest_literal(json, "false");
	if (c == 'n')
		return manifest_literal(json, "null");
	if (c == '-')
		json->at++;
	if (json->at < json->end && *json->at == '0')
		json->at++;
	else if (!manifest_digits(json))
		return 0;
	if (json->at < json->end && *json->at == '.')
	{
		json->at++;
		if (!manifest_digits(json))
			return 0;
	}
	if (json->at < json->end && (*json->at == 'e' || *json->at == 'E'))
	{
		json->at++;
		if (json->at < json->end && (*json->at == '+' || *json->at == '-'))
			json->at++;
		if (!manifest_digits(json))
			return 0;
	}
	return 1;
}

/* (a name, version, tag or the like: letters, digits and . _ + -, as the
release workflow names them; nothing that could leave the update's folder
or change a download's address) */
static int manifest_word(const char *text, size_t length)
{
	size_t index;

	if (!length || length >= MANIFEST_WORD_SIZE || *text == '.')
		return 0;
	for (index = 0; index < length; index++)
	{
		char c = text[index];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
			c == '+' || c == '-'))
		{
			return 0;
		}
	}
	return 1;
}

/* a string value into value, when it is a word (above); any other string
leaves it empty, and anything but a string is not the list's */
static int manifest_word_value(struct manifest_json *json, char *value, size_t value_size)
{
	const char *text;
	size_t length;
	int escaped;

	*value = 0;
	if (!manifest_string(json, &text, &length, &escaped))
		return 0;
	if (!escaped && manifest_word(text, length) && length < value_size)
	{
		memcpy(value, text, length);
		value[length] = 0;
	}
	return 1;
}

/* a file's size: a whole number (no sign, fraction or exponent) */
static int manifest_size(struct manifest_json *json, struct manifest_file *file)
{
	const char *start;
	unsigned long long number = 0;

	manifest_space(json);
	start = json->at;
	while (json->at < json->end && *json->at >= '0' && *json->at <= '9')
	{
		if (json->at - start >= 15)
			return 0;
		number = number * 10 + (unsigned long long)(*json->at - '0');
		json->at++;
	}
	if (json->at == start || (json->at - start > 1 && *start == '0'))
		return 0;
	if (json->at < json->end && (*json->at == '.' || *json->at == 'e' || *json->at == 'E'))
		return 0;
	file->size = number;
	file->has_size = 1;
	return 1;
}

/* a SHA-256: 64 hex digits, kept in lowercase */
static int manifest_sha256(struct manifest_json *json, struct manifest_file *file)
{
	const char *text;
	size_t length, index;
	int escaped;

	file->sha256[0] = 0;
	if (!manifest_string(json, &text, &length, &escaped))
		return 0;
	if (escaped || length != UPDATE_SHA256_TEXT_SIZE - 1)
		return 1;
	for (index = 0; index < length; index++)
	{
		if (manifest_hex_digit(text[index]) < 0)
			return 1;
	}
	for (index = 0; index < length; index++)
		file->sha256[index] = (char)(text[index] >= 'A' && text[index] <= 'F' ? text[index] - 'A' + 'a' : text[index]);
	file->sha256[length] = 0;
	return 1;
}

/* one of "files": an object, its fields into file */
static int manifest_file(struct manifest_json *json, struct manifest_file *file)
{
	memset(file, 0, sizeof(*file));
	if (!manifest_take(json, '{'))
		return 0;
	if (manifest_take(json, '}'))
		return 1;
	do
	{
		const char *key;
		size_t length;
		int escaped, ok;

		if (!manifest_string(json, &key, &length, &escaped) || !manifest_take(json, ':'))
			return 0;
		if (manifest_is(key, length, "platform"))
			ok = manifest_word_value(json, file->platform, sizeof(file->platform));
		else if (manifest_is(key, length, "configuration"))
			ok = manifest_word_value(json, file->configuration, sizeof(file->configuration));
		else if (manifest_is(key, length, "kind"))
			ok = manifest_word_value(json, file->kind, sizeof(file->kind));
		else if (manifest_is(key, length, "tag"))
			ok = manifest_word_value(json, file->tag, sizeof(file->tag));
		else if (manifest_is(key, length, "name"))
			ok = manifest_word_value(json, file->name, sizeof(file->name));
		else if (manifest_is(key, length, "main"))
			ok = manifest_word_value(json, file->main_name, sizeof(file->main_name));
		else if (manifest_is(key, length, "size"))
			ok = manifest_size(json, file);
		else if (manifest_is(key, length, "sha256"))
			ok = manifest_sha256(json, file);
		else
			ok = manifest_skip(json);
		if (!ok)
			return 0;
	} while (manifest_take(json, ','));
	return manifest_take(json, '}');
}

/* ---------- the list */

int update_manifest_find(const char *text, size_t length, const char *version, const char *platform,
	const char *configuration, const char *kind, char *tag, size_t tag_size, char *name, size_t name_size,
	unsigned long long *size, char *sha256, size_t sha256_size, char *main_name, size_t main_size, char *error,
	size_t error_size)
{
	struct manifest_json json;
	struct manifest_file file, found;
	char format[MANIFEST_WORD_SIZE] = "", listed_version[MANIFEST_WORD_SIZE] = "";
	int has_format = 0, has_version = 0, has_files = 0, matches = 0;

	if (!text)
	{
		snprintf(error, error_size, "the list is empty");
		return _update_manifest_refused;
	}
	json.at = text;
	json.end = text + length;
	json.depth = 0;
	memset(&found, 0, sizeof(found));
	if (!manifest_take(&json, '{'))
		goto broken;
	if (!manifest_take(&json, '}'))
	{
		do
		{
			const char *key;
			size_t key_length;
			int escaped;

			if (!manifest_string(&json, &key, &key_length, &escaped) || !manifest_take(&json, ':'))
				goto broken;
			if (manifest_is(key, key_length, "format"))
			{
				const char *value;
				size_t value_length;

				/* (not a word: it has a space) */
				if (has_format || !manifest_string(&json, &value, &value_length, &escaped))
					goto broken;
				has_format = 1;
				if (!escaped && value_length < sizeof(format))
				{
					memcpy(format, value, value_length);
					format[value_length] = 0;
				}
			}
			else if (manifest_is(key, key_length, "version"))
			{
				if (has_version || !manifest_word_value(&json, listed_version, sizeof(listed_version)))
					goto broken;
				has_version = 1;
			}
			else if (manifest_is(key, key_length, "files"))
			{
				if (has_files || !manifest_take(&json, '['))
					goto broken;
				has_files = 1;
				if (!manifest_take(&json, ']'))
				{
					do
					{
						if (!manifest_file(&json, &file))
							goto broken;
						if (!strcmp(file.platform, platform) && !strcmp(file.configuration, configuration) &&
							!strcmp(file.kind, kind))
						{
							found = file;
							matches++;
						}
					} while (manifest_take(&json, ','));
					if (!manifest_take(&json, ']'))
						goto broken;
				}
			}
			else if (!manifest_skip(&json))
			{
				goto broken;
			}
		} while (manifest_take(&json, ','));
		if (!manifest_take(&json, '}'))
			goto broken;
	}
	manifest_space(&json);
	if (json.at != json.end)
		goto broken;
	if (strcmp(format, UPDATE_MANIFEST_FORMAT))
	{
		snprintf(error, error_size, "the list is not one this build reads");
		return _update_manifest_refused;
	}
	/* (a list signed for another release is not this one's) */
	if (strcmp(listed_version, version))
	{
		snprintf(error, error_size, "the list is version %s's, not %s's", listed_version[0] ? listed_version : "no",
			version);
		return _update_manifest_refused;
	}
	if (!matches)
	{
		snprintf(error, error_size, "the list has no %s file for %s %s", kind, platform, configuration);
		return _update_manifest_missing;
	}
	if (matches > 1)
	{
		snprintf(error, error_size, "the list has %d %s files for %s %s", matches, kind, platform, configuration);
		return _update_manifest_refused;
	}
	/* (its file in full: the release it is on is one of this version's) */
	if (!found.name[0] || !found.tag[0] || !found.has_size || !found.sha256[0] || found.tag[0] != 'v' ||
		strncmp(found.tag + 1, version, strlen(version)) ||
		(found.tag[1 + strlen(version)] && found.tag[1 + strlen(version)] != '-'))
	{
		snprintf(error, error_size, "the list's %s file for %s %s is not one", kind, platform, configuration);
		return _update_manifest_refused;
	}
	if (strlen(found.tag) >= tag_size || strlen(found.name) >= name_size || sha256_size < UPDATE_SHA256_TEXT_SIZE ||
		strlen(found.main_name) >= main_size)
	{
		snprintf(error, error_size, "the list's names are too long");
		return _update_manifest_refused;
	}
	snprintf(tag, tag_size, "%s", found.tag);
	snprintf(name, name_size, "%s", found.name);
	snprintf(sha256, sha256_size, "%s", found.sha256);
	snprintf(main_name, main_size, "%s", found.main_name);
	*size = found.size;
	return _update_manifest_found;

broken:
	snprintf(error, error_size, "the list is not JSON this build can read");
	return _update_manifest_refused;
}

/* ---------- SHA-256 (FIPS 180-4) */

static const unsigned long sha256_constants[64] =
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

/* (32 bits, whatever a long's size) */
#define SHA256_ROTATE(x, n) ((((x) >> (n)) | ((x) << (32 - (n)))) & 0xffffffffUL)

static void sha256_block(unsigned long state[8], const unsigned char *block)
{
	unsigned long w[64], a, b, c, d, e, f, g, h;
	int index;

	for (index = 0; index < 16; index++)
	{
		w[index] = (unsigned long)block[index * 4] << 24 | (unsigned long)block[index * 4 + 1] << 16 |
			(unsigned long)block[index * 4 + 2] << 8 | (unsigned long)block[index * 4 + 3];
	}
	for (index = 16; index < 64; index++)
	{
		unsigned long s0 = SHA256_ROTATE(w[index - 15], 7) ^ SHA256_ROTATE(w[index - 15], 18) ^ (w[index - 15] >> 3);
		unsigned long s1 = SHA256_ROTATE(w[index - 2], 17) ^ SHA256_ROTATE(w[index - 2], 19) ^ (w[index - 2] >> 10);

		w[index] = (w[index - 16] + s0 + w[index - 7] + s1) & 0xffffffffUL;
	}
	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];
	e = state[4];
	f = state[5];
	g = state[6];
	h = state[7];
	for (index = 0; index < 64; index++)
	{
		unsigned long s1 = SHA256_ROTATE(e, 6) ^ SHA256_ROTATE(e, 11) ^ SHA256_ROTATE(e, 25);
		unsigned long choice = (e & f) ^ (~e & g);
		unsigned long t1 = (h + s1 + choice + sha256_constants[index] + w[index]) & 0xffffffffUL;
		unsigned long s0 = SHA256_ROTATE(a, 2) ^ SHA256_ROTATE(a, 13) ^ SHA256_ROTATE(a, 22);
		unsigned long majority = (a & b) ^ (a & c) ^ (b & c);
		unsigned long t2 = (s0 + majority) & 0xffffffffUL;

		h = g;
		g = f;
		f = e;
		e = (d + t1) & 0xffffffffUL;
		d = c;
		c = b;
		b = a;
		a = (t1 + t2) & 0xffffffffUL;
	}
	state[0] = (state[0] + a) & 0xffffffffUL;
	state[1] = (state[1] + b) & 0xffffffffUL;
	state[2] = (state[2] + c) & 0xffffffffUL;
	state[3] = (state[3] + d) & 0xffffffffUL;
	state[4] = (state[4] + e) & 0xffffffffUL;
	state[5] = (state[5] + f) & 0xffffffffUL;
	state[6] = (state[6] + g) & 0xffffffffUL;
	state[7] = (state[7] + h) & 0xffffffffUL;
}

void update_sha256(const unsigned char *data, size_t size, char hex[UPDATE_SHA256_TEXT_SIZE])
{
	static const char digits[] = "0123456789abcdef";
	unsigned long state[8] =
	{
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
	};
	unsigned char tail[128];
	unsigned long long bits = (unsigned long long)size * 8;
	size_t whole = size - size % 64, rest = size % 64, tail_size, index;

	for (index = 0; index < whole; index += 64)
		sha256_block(state, data + index);
	/* (the rest, a 1 bit, zeros and the length in bits, in one block or two) */
	memset(tail, 0, sizeof(tail));
	if (rest)
		memcpy(tail, data + whole, rest);
	tail[rest] = 0x80;
	tail_size = rest < 56 ? 64 : 128;
	for (index = 0; index < 8; index++)
		tail[tail_size - 1 - index] = (unsigned char)(bits >> (index * 8));
	sha256_block(state, tail);
	if (tail_size == 128)
		sha256_block(state, tail + 64);
	for (index = 0; index < 32; index++)
	{
		unsigned char byte = (unsigned char)(state[index / 4] >> (24 - (index % 4) * 8));

		hex[index * 2] = digits[byte >> 4];
		hex[index * 2 + 1] = digits[byte & 15];
	}
	hex[64] = 0;
}
