/*
UPDATE_MANIFEST.C

A release's list of its update files (update_manifest.h): read as hostile
JSON (its signature is checked first, but nothing here trusts it further than
it must), and the SHA-256 its files are checked against, here so that no
other library crosses the updater's line between the game's structure layouts
and the system's (update.h).
*/

#include "update_manifest.h"
#include "../game/content_sha256.h"

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

/* the list as read: its format, version and channel (empty when it has
none, as the lists before the channels), and the one file of kind for the
platform and configuration (matches: how many there are) */
struct manifest_list
{
	char format[MANIFEST_WORD_SIZE];
	char version[MANIFEST_WORD_SIZE];
	char channel[MANIFEST_WORD_SIZE];
	struct manifest_file found;
	int matches;
};

/* the list (text, length bytes) into list; 0 if it is not JSON of the list's
shape (platform NULL: no file looked for) */
static int manifest_read(const char *text, size_t length, const char *platform, const char *configuration,
	const char *kind, struct manifest_list *list)
{
	struct manifest_json json;
	struct manifest_file file;
	int has_format = 0, has_version = 0, has_channel = 0, has_files = 0;

	memset(list, 0, sizeof(*list));
	if (!text)
		return 0;
	json.at = text;
	json.end = text + length;
	json.depth = 0;
	if (!manifest_take(&json, '{'))
		return 0;
	if (!manifest_take(&json, '}'))
	{
		do
		{
			const char *key;
			size_t key_length;
			int escaped;

			if (!manifest_string(&json, &key, &key_length, &escaped) || !manifest_take(&json, ':'))
				return 0;
			if (manifest_is(key, key_length, "format"))
			{
				const char *value;
				size_t value_length;

				/* (not a word: it has a space) */
				if (has_format || !manifest_string(&json, &value, &value_length, &escaped))
					return 0;
				has_format = 1;
				if (!escaped && value_length < sizeof(list->format))
				{
					memcpy(list->format, value, value_length);
					list->format[value_length] = 0;
				}
			}
			else if (manifest_is(key, key_length, "version"))
			{
				if (has_version || !manifest_word_value(&json, list->version, sizeof(list->version)))
					return 0;
				has_version = 1;
			}
			else if (manifest_is(key, key_length, "channel"))
			{
				/* (a channel that is not a word reads as "?": no channel's) */
				if (has_channel || !manifest_word_value(&json, list->channel, sizeof(list->channel)))
					return 0;
				has_channel = 1;
				if (!list->channel[0])
					snprintf(list->channel, sizeof(list->channel), "?");
			}
			else if (manifest_is(key, key_length, "files"))
			{
				if (has_files || !manifest_take(&json, '['))
					return 0;
				has_files = 1;
				if (!manifest_take(&json, ']'))
				{
					do
					{
						if (!manifest_file(&json, &file))
							return 0;
						if (platform && !strcmp(file.platform, platform) &&
							!strcmp(file.configuration, configuration) && !strcmp(file.kind, kind))
						{
							list->found = file;
							list->matches++;
						}
					} while (manifest_take(&json, ','));
					if (!manifest_take(&json, ']'))
						return 0;
				}
			}
			else if (!manifest_skip(&json))
			{
				return 0;
			}
		} while (manifest_take(&json, ','));
		if (!manifest_take(&json, '}'))
			return 0;
	}
	manifest_space(&json);
	return json.at == json.end;
}

/* the list's file, found by manifest_read, checked and handed out: its tag
one tag_allowed accepts for the list's version */
static int manifest_hand_out(const struct manifest_list *list, const char *version, const char *platform,
	const char *configuration, const char *kind, int (*tag_allowed)(const char *tag, const char *version),
	char *tag, size_t tag_size, char *name, size_t name_size, unsigned long long *size, char *sha256,
	size_t sha256_size, char *main_name, size_t main_size, char *error, size_t error_size)
{
	const struct manifest_file *found = &list->found;

	if (!list->matches)
	{
		snprintf(error, error_size, "the list has no %s file for %s %s", kind, platform, configuration);
		return _update_manifest_missing;
	}
	if (list->matches > 1)
	{
		snprintf(error, error_size, "the list has %d %s files for %s %s", list->matches, kind, platform,
			configuration);
		return _update_manifest_refused;
	}
	if (!found->name[0] || !found->tag[0] || !found->has_size || !found->sha256[0] || !tag_allowed(found->tag, version))
	{
		snprintf(error, error_size, "the list's %s file for %s %s is not one", kind, platform, configuration);
		return _update_manifest_refused;
	}
	if (strlen(found->tag) >= tag_size || strlen(found->name) >= name_size || sha256_size < UPDATE_SHA256_TEXT_SIZE ||
		strlen(found->main_name) >= main_size)
	{
		snprintf(error, error_size, "the list's names are too long");
		return _update_manifest_refused;
	}
	snprintf(tag, tag_size, "%s", found->tag);
	snprintf(name, name_size, "%s", found->name);
	snprintf(sha256, sha256_size, "%s", found->sha256);
	snprintf(main_name, main_size, "%s", found->main_name);
	*size = found->size;
	return _update_manifest_found;
}

/* a release's file is on one of the release's own: v<version>, or
v<version>-<more> (its debug builds' v<version>-debug) */
static int manifest_release_tag(const char *tag, const char *version)
{
	size_t length = strlen(version);

	return tag[0] == 'v' && !strncmp(tag + 1, version, length) && (!tag[1 + length] || tag[1 + length] == '-');
}

/* a test build's file is on its version's testing pre-release:
v<base>-testing, or v<base>-testing<digits> (a second set), each with or
without -debug, <base> being the version without its label and serial
(v0.9.1a-testing for 0.9.1a-testing.202611101530). A nightly's is on the
nightly pre-release, "nightly" (or "nightly-debug"). The file's SHA-256 in
the signed list is what makes it the build: the tag is only where it is
downloaded from */
static int manifest_testing_tag(const char *tag, const char *version)
{
	const char *label = strstr(version, "-testing.");
	size_t base = label ? (size_t)(label - version) : 0;

	if (!label || tag[0] != 'v' || strncmp(tag + 1, version, base) || strncmp(tag + 1 + base, "-testing", 8))
		return 0;
	tag += 1 + base + 8;
	while (*tag >= '0' && *tag <= '9')
		tag++;
	return !*tag || !strcmp(tag, "-debug");
}

static int manifest_nightly_tag(const char *tag, const char *version)
{
	(void)version;
	return !strcmp(tag, "nightly") || !strcmp(tag, "nightly-debug");
}

int update_manifest_find(const char *text, size_t length, const char *version, const char *platform,
	const char *configuration, const char *kind, char *tag, size_t tag_size, char *name, size_t name_size,
	unsigned long long *size, char *sha256, size_t sha256_size, char *main_name, size_t main_size, char *error,
	size_t error_size)
{
	struct manifest_list list;

	if (!text)
	{
		snprintf(error, error_size, "the list is empty");
		return _update_manifest_refused;
	}
	if (!manifest_read(text, length, platform, configuration, kind, &list))
		goto broken;
	if (strcmp(list.format, UPDATE_MANIFEST_FORMAT))
	{
		snprintf(error, error_size, "the list is not one this build reads");
		return _update_manifest_refused;
	}
	/* (a list signed for another release is not this one's) */
	if (strcmp(list.version, version))
	{
		snprintf(error, error_size, "the list is version %s's, not %s's", list.version[0] ? list.version : "no",
			version);
		return _update_manifest_refused;
	}
	/* (a release's list: one from before the channels has none; a testing
	or nightly list, or a list of a test build's or a nightly's version, is
	never a release's) */
	if (!update_version_release(list.version))
	{
		snprintf(error, error_size, "the list's version %s is not a release's", list.version);
		return _update_manifest_refused;
	}
	if (list.channel[0] && strcmp(list.channel, "release"))
	{
		snprintf(error, error_size, "the list is the %s channel's, not a release's", list.channel);
		return _update_manifest_refused;
	}
	return manifest_hand_out(&list, version, platform, configuration, kind, manifest_release_tag, tag, tag_size, name,
		name_size, size, sha256, sha256_size, main_name, main_size, error, error_size);

broken:
	snprintf(error, error_size, "the list is not JSON this build can read");
	return _update_manifest_refused;
}

int update_manifest_about(const char *text, size_t length, const char *channel, char *version, size_t version_size,
	char *error, size_t error_size)
{
	struct manifest_list list;
	char label[MANIFEST_WORD_SIZE];

	if (version_size)
		version[0] = 0;
	if (!manifest_read(text, length, NULL, NULL, NULL, &list))
	{
		snprintf(error, error_size, "the list is not JSON this build can read");
		return _update_manifest_refused;
	}
	if (strcmp(list.format, UPDATE_MANIFEST_FORMAT))
	{
		snprintf(error, error_size, "the list is not one this build reads");
		return _update_manifest_refused;
	}
	/* (the feed's list is its channel's, and so is its version: a release's
	version never comes from a testing or nightly feed, whose key may sign
	only those: update_signature.h) */
	if (strcmp(list.channel, channel))
	{
		snprintf(error, error_size, "the list is the %s channel's, not the %s channel's",
			list.channel[0] ? list.channel : "release", channel);
		return _update_manifest_refused;
	}
	if (!update_version_prerelease(list.version, label, sizeof(label)) || strcmp(label, channel))
	{
		snprintf(error, error_size, "the %s list's version %s is not a %s build's", channel,
			list.version[0] ? list.version : "(none)", channel);
		return _update_manifest_refused;
	}
	if (strlen(list.version) >= version_size)
	{
		snprintf(error, error_size, "the list's version is too long");
		return _update_manifest_refused;
	}
	snprintf(version, version_size, "%s", list.version);
	return _update_manifest_found;
}

int update_manifest_find_channel(const char *text, size_t length, const char *channel, const char *version,
	const char *platform, const char *configuration, const char *kind, char *tag, size_t tag_size, char *name,
	size_t name_size, unsigned long long *size, char *sha256, size_t sha256_size, char *main_name, size_t main_size,
	char *error, size_t error_size)
{
	struct manifest_list list;
	char listed[MANIFEST_WORD_SIZE];
	int result = update_manifest_about(text, length, channel, listed, sizeof(listed), error, error_size);

	if (result != _update_manifest_found)
		return result;
	if (strcmp(listed, version))
	{
		snprintf(error, error_size, "the list is version %s's, not %s's", listed, version);
		return _update_manifest_refused;
	}
	if (!manifest_read(text, length, platform, configuration, kind, &list))
	{
		snprintf(error, error_size, "the list is not JSON this build can read");
		return _update_manifest_refused;
	}
	return manifest_hand_out(&list, version, platform, configuration, kind,
		!strcmp(channel, "nightly") ? manifest_nightly_tag : manifest_testing_tag, tag, tag_size, name, name_size, size,
		sha256, sha256_size, main_name, main_size, error, error_size);
}

/* ---------- versions */

/* a version: [v]<major>.<minor>.<patch>, its hotfix letters (0.9.0d), and
for a test build or a nightly -<label>.<serial> (0.9.1-testing.202611101530,
the time it was built); 0 if it is not one */
struct update_version
{
	unsigned long long numbers[3];
	char letters[16];
	char label[16];
	unsigned long long serial;
	int prerelease;
};

static int version_number(const char **text, unsigned long long *number, int most)
{
	int digits = 0;

	*number = 0;
	while (**text >= '0' && **text <= '9')
	{
		if (++digits > most)
			return 0;
		*number = *number * 10 + (unsigned long long)(**text - '0');
		(*text)++;
	}
	return digits > 0;
}

/* whether a serial is a time: YYYYMMDDHHMM, its year 2026 to 2099, and each
field in its range */
static int version_serial_valid(unsigned long long serial)
{
	unsigned long long year = serial / 100000000ULL, month = serial / 1000000ULL % 100, day = serial / 10000ULL % 100,
		hour = serial / 100ULL % 100, minute = serial % 100;

	return year >= 2026 && year <= 2099 && month >= 1 && month <= 12 && day >= 1 && day <= 31 && hour <= 23 &&
		minute <= 59;
}

static int version_parse(const char *text, struct update_version *version)
{
	size_t length;
	int part;

	memset(version, 0, sizeof(*version));
	if (!text)
		return 0;
	if (*text == 'v')
		text++;
	for (part = 0; part < 3; part++)
	{
		if (!version_number(&text, &version->numbers[part], 9))
			return 0;
		if (part < 2 && *text++ != '.')
			return 0;
	}
	for (length = 0; text[length] >= 'a' && text[length] <= 'z'; length++)
	{
		if (length + 1 >= sizeof(version->letters))
			return 0;
	}
	memcpy(version->letters, text, length);
	version->letters[length] = 0;
	text += length;
	if (!*text)
		return 1;
	if (*text++ != '-')
		return 0;
	for (length = 0; text[length] >= 'a' && text[length] <= 'z'; length++)
	{
		if (length + 1 >= sizeof(version->label))
			return 0;
	}
	if (!length || text[length] != '.')
		return 0;
	memcpy(version->label, text, length);
	version->label[length] = 0;
	/* (only the two channels' labels) */
	if (strcmp(version->label, "testing") && strcmp(version->label, "nightly"))
		return 0;
	text += length + 1;
	/* (the serial: exactly 12 digits, the UTC time it was built,
	YYYYMMDDHHMM, 2026 to 2099) */
	for (length = 0; text[length] >= '0' && text[length] <= '9'; length++)
	{
	}
	if (length != 12 || text[length] || !version_number(&text, &version->serial, 12) ||
		!version_serial_valid(version->serial))
	{
		return 0;
	}
	version->prerelease = 1;
	return 1;
}

int update_version_compare(const char *a, const char *b, int *comparable)
{
	struct update_version first, second;
	int part, order;

	*comparable = version_parse(a, &first) && version_parse(b, &second);
	if (!*comparable)
		return 0;
	for (part = 0; part < 3; part++)
	{
		if (first.numbers[part] != second.numbers[part])
			return first.numbers[part] > second.numbers[part] ? 1 : -1;
	}
	/* (the hotfix letters as before the channels: none first, then a, b) */
	order = strcmp(first.letters, second.letters);
	if (order)
		return order > 0 ? 1 : -1;
	/* (a release is newer than any test build or nightly of its version;
	those are in the order they were built, whatever their channel) */
	if (first.prerelease != second.prerelease)
		return first.prerelease ? -1 : 1;
	if (first.serial != second.serial)
		return first.serial > second.serial ? 1 : -1;
	return 0;
}

int update_version_newer(const char *version, const char *current)
{
	int comparable;

	return update_version_compare(version, current, &comparable) > 0 && comparable;
}

/* a release's made time (GitHub's created_at for it, its commit's date:
2026-11-26T02:15:07Z) as a serial, YYYYMMDDHHMM; 0 if it is not one */
static int version_made_serial(const char *made, unsigned long long *serial)
{
	static const char shape[] = "dddd-dd-ddTdd:dd";
	size_t index;

	*serial = 0;
	if (!made)
		return 0;
	for (index = 0; shape[index]; index++)
	{
		if (shape[index] == 'd' ? made[index] < '0' || made[index] > '9' : made[index] != shape[index])
			return 0;
		if (shape[index] == 'd')
			*serial = *serial * 10 + (unsigned long long)(made[index] - '0');
	}
	return version_serial_valid(*serial);
}

int update_release_newer(const char *release, const char *made, const char *current)
{
	struct update_version build;
	unsigned long long serial;

	if (!update_version_newer(release, current))
		return 0;
	/* (a release's build, or a version that is not a release's, or no made
	time: by the order alone, as before) */
	if (!update_version_release(release) || !version_parse(current, &build) || !build.prerelease ||
		!version_made_serial(made, &serial))
	{
		return 1;
	}
	/* (a test build or a nightly made after the release's commit was has
	it: the release is newer only when it was made after the build) */
	return serial > build.serial;
}

int update_version_release(const char *text)
{
	char label[16];

	/* (as before the channels: a version with a suffix of another shape,
	0.9.0-dev, is no prerelease of a channel, and the release path decides
	by its order) */
	return !update_version_prerelease(text, label, sizeof(label)) && !strstr(text ? text : "", "-testing") &&
		!strstr(text ? text : "", "-nightly");
}

int update_version_prerelease(const char *text, char *label, size_t label_size)
{
	struct update_version version;

	if (label_size)
		label[0] = 0;
	if (!version_parse(text, &version) || !version.prerelease || strlen(version.label) >= label_size)
		return 0;
	snprintf(label, label_size, "%s", version.label);
	return 1;
}

/* ---------- SHA-256 (the game's one: content_sha256.c) */

void update_sha256(const unsigned char *data, size_t size, char hex[UPDATE_SHA256_TEXT_SIZE])
{
	static const char digits[] = "0123456789abcdef";
	unsigned char digest[CONTENT_SHA256_SIZE];
	int index;

	content_sha256(data, size, digest);
	for (index = 0; index < 32; index++)
	{
		hex[index * 2] = digits[digest[index] >> 4];
		hex[index * 2 + 1] = digits[digest[index] & 15];
	}
	hex[64] = 0;
}
