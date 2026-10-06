/*
DELTA.C

The legacy number in use (docs/delta.md, "The legacy table as config"): the
OpenCE network version a host announces, and the range of hosts' versions a
client joins.

The floor is the numbers this build was made with (halo_port_limits.h).
Above it, a signed legacy table: a JSON document, signed with ChupathingyCE's
Ed25519 key (delta_key.h), with a row of numbers for each wire (DELTA_WIRE,
delta.h). This build reads its own wire's row alone, and takes it only if it
widens the built-in numbers (announces a newer number, joins a wider range).

Where tables come from, the newest serial winning (an equal or older one is
never taken, so an old copy cannot roll a machine back):
- the cache, delta_legacy.signed in the save root, read at start;
- Delta List (network.browser_url's /v1/delta/legacy and .sig), fetched in
the background at start and every few hours, and GitHub's copy
(ChupathingyCE/chupathingyce's delta-table branch) when the site cannot be
reached or gives no valid table;
- another machine (delta_legacy_offer: Delta Peer).
A table that does not verify is dropped, and logged; nothing is shown.

A local, unsigned file (network.legacy_table, HALO_LEGACY_TABLE) replaces
all of that, for tests and for admins: logged as a warning at start, never
passed on, and no table is fetched or taken meanwhile.

The signed table, as it is cached and passed between machines, is one text:
the signature's 128 hex digits, a line feed, then the document, the exact
bytes signed (at most DELTA_LEGACY_DOCUMENT_SIZE). The servers keep the two
apart: the document (legacy.json) and its signature (legacy.json.sig, the
128 hex digits and a line feed), as tools/delta_table.py writes them.

Everything from outside is hostile: its size is checked first, then the
signature, and only then is the document read, by a strict parser that
skips what it does not know.
*/

#include "platform.h"
#include "port_config.h"
#include "halo_port_limits.h"
#include "delta.h"
#include "delta_key.h"
#ifdef HALO_GAME_BROWSER
#include "browser_http.h"
#endif

#include "monocypher.h"
#include "monocypher-ed25519.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DELTA_LEGACY_FORMAT 1
#define DELTA_SIGNATURE_SIZE 64
#define DELTA_KEY_COUNT (sizeof(delta_public_keys) / sizeof(*delta_public_keys))
#define DELTA_CACHE_NAME "delta_legacy.signed"
#define DELTA_GITHUB_URL "https://raw.githubusercontent.com/ChupathingyCE/chupathingyce/delta-table/legacy.json"

/* (fetched this often, and this long after a failure, in milliseconds; a
test's build may give its own) */
#ifndef DELTA_REFRESH_INTERVAL
#define DELTA_REFRESH_INTERVAL (4 * 60 * 60 * 1000)
#endif
#ifndef DELTA_RETRY_INTERVAL
#define DELTA_RETRY_INTERVAL (30 * 60 * 1000)
#endif

enum
{
	/* (how deep the document's objects and arrays may nest) */
	JSON_DEPTH = 8,
	/* (the most disabled capabilities read) */
	MAXIMUM_DISABLED = 64,
	/* (an OpenCE network version is an unsigned 16-bit number) */
	MAXIMUM_VERSION = 65535,
};

/* the capability registry's names (delta.h), as the table spells them */
static const char *const delta_capability_names[] =
{
	"platform", "profile", "server_messages", "chat", "ce_maps", "md_maps", "coop", "ai_sync", "vote",
	"console_slots",
};
_Static_assert(sizeof(delta_capability_names) / sizeof(*delta_capability_names) == NUMBER_OF_DELTA_CAPABILITIES,
	"a name for each capability");

struct delta_table
{
	unsigned int serial;
	long long issued;
	/* (this build's wire's row, if the table has one) */
	int has_row;
	int announce, minimum, maximum;
	unsigned long disabled;
};

static pthread_mutex_t delta_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t delta_once = PTHREAD_ONCE_INIT;
/* (the fetching thread waits on it for its next time) */
static pthread_cond_t delta_wake = PTHREAD_COND_INITIALIZER;

static struct
{
	int announce, minimum, maximum;
	unsigned long disabled;
	/* the signed table in use (malloc'd), and its serial; none: 0 */
	unsigned int serial;
	char *signed_table;
	int signed_size;
	/* a local, unsigned table is in use */
	int override;
} delta;

/* ---------- the document (JSON, hostile) */

struct json
{
	const char *at;
	const char *end;
	int depth;
};

static void json_space(struct json *json)
{
	while (json->at < json->end &&
		(*json->at == ' ' || *json->at == '\t' || *json->at == '\n' || *json->at == '\r'))
	{
		json->at++;
	}
}

static int json_take(struct json *json, char c)
{
	json_space(json);
	if (json->at < json->end && *json->at == c)
	{
		json->at++;
		return 1;
	}
	return 0;
}

static int json_hex(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* a string: its text between the quotes, its escapes checked but not
decoded (escaped: whether it has any) */
static int json_string(struct json *json, const char **text, int *length, int *escaped)
{
	const char *start;

	*escaped = 0;
	if (!json_take(json, '"'))
		return 0;
	start = json->at;
	while (json->at < json->end)
	{
		unsigned char c = (unsigned char)*json->at;

		if (c == '"')
		{
			*text = start;
			*length = (int)(json->at - start);
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
					if (json->at + index >= json->end || !json_hex(json->at[index]))
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

static int json_is(const char *text, int length, const char *name)
{
	return (size_t)length == strlen(name) && !memcmp(text, name, (size_t)length);
}

/* a whole number from minimum to maximum (no sign, fraction or exponent) */
static int json_integer(struct json *json, long long minimum, long long maximum, long long *value)
{
	long long number = 0;
	const char *start;

	json_space(json);
	start = json->at;
	while (json->at < json->end && *json->at >= '0' && *json->at <= '9')
	{
		if (json->at - start >= 16)
			return 0;
		number = number * 10 + (*json->at - '0');
		json->at++;
	}
	if (json->at == start || (json->at - start > 1 && *start == '0'))
		return 0;
	if (json->at < json->end && (*json->at == '.' || *json->at == 'e' || *json->at == 'E'))
		return 0;
	if (number < minimum || number > maximum)
		return 0;
	*value = number;
	return 1;
}

static int json_literal(struct json *json, const char *literal)
{
	size_t length = strlen(literal);

	if ((size_t)(json->end - json->at) < length || memcmp(json->at, literal, length))
		return 0;
	json->at += length;
	return 1;
}

static int json_digits(struct json *json)
{
	const char *start = json->at;

	while (json->at < json->end && *json->at >= '0' && *json->at <= '9')
		json->at++;
	return json->at > start;
}

/* any value, checked and passed over */
static int json_skip(struct json *json)
{
	const char *text;
	int length, escaped;
	char c;

	json_space(json);
	if (json->at >= json->end)
		return 0;
	c = *json->at;
	if (c == '"')
		return json_string(json, &text, &length, &escaped);
	if (c == '{' || c == '[')
	{
		char close = c == '{' ? '}' : ']';
		int ok = 1;

		if (++json->depth > JSON_DEPTH)
			return 0;
		json->at++;
		if (!json_take(json, close))
		{
			do
			{
				if (c == '{' && (!json_string(json, &text, &length, &escaped) || !json_take(json, ':')))
					return 0;
				if (!json_skip(json))
					return 0;
			} while (json_take(json, ','));
			ok = json_take(json, close);
		}
		json->depth--;
		return ok;
	}
	if (c == 't')
		return json_literal(json, "true");
	if (c == 'f')
		return json_literal(json, "false");
	if (c == 'n')
		return json_literal(json, "null");
	if (c == '-')
		json->at++;
	if (json->at < json->end && *json->at == '0')
		json->at++;
	else if (!json_digits(json))
		return 0;
	if (json->at < json->end && *json->at == '.')
	{
		json->at++;
		if (!json_digits(json))
			return 0;
	}
	if (json->at < json->end && (*json->at == 'e' || *json->at == 'E'))
	{
		json->at++;
		if (json->at < json->end && (*json->at == '+' || *json->at == '-'))
			json->at++;
		if (!json_digits(json))
			return 0;
	}
	return 1;
}

/* a wire's row: {"announce": A, "minimum": Mi, "maximum": Ma} */
static int json_row(struct json *json, struct delta_table *table)
{
	int seen = 0;

	if (!json_take(json, '{'))
		return 0;
	if (json_take(json, '}'))
		return 0;
	do
	{
		const char *key;
		int length, escaped, bit = 0;
		int *field = NULL;
		long long value;

		if (!json_string(json, &key, &length, &escaped) || !json_take(json, ':'))
			return 0;
		if (!escaped && json_is(key, length, "announce"))
			field = &table->announce, bit = 1;
		else if (!escaped && json_is(key, length, "minimum"))
			field = &table->minimum, bit = 2;
		else if (!escaped && json_is(key, length, "maximum"))
			field = &table->maximum, bit = 4;
		if (!field)
		{
			if (!json_skip(json))
				return 0;
			continue;
		}
		if ((seen & bit) || !json_integer(json, 1, MAXIMUM_VERSION, &value))
			return 0;
		*field = (int)value;
		seen |= bit;
	} while (json_take(json, ','));
	return json_take(json, '}') && seen == 7;
}

/* "wires": {"<wire>": row, ...}: this build's row read, the others checked */
static int json_wires(struct json *json, struct delta_table *table)
{
	if (!json_take(json, '{'))
		return 0;
	if (json_take(json, '}'))
		return 1;
	json->depth++;
	do
	{
		const char *key;
		int length, escaped;

		if (!json_string(json, &key, &length, &escaped) || !json_take(json, ':'))
			return 0;
		if (!escaped && json_is(key, length, DELTA_WIRE))
		{
			/* (one row a wire: two would read differently elsewhere) */
			if (table->has_row || !json_row(json, table))
				return 0;
			table->has_row = 1;
		}
		else if (!json_skip(json))
			return 0;
	} while (json_take(json, ','));
	json->depth--;
	return json_take(json, '}');
}

/* "disabled_capabilities": ["<name>", ...]: the names this build knows */
static int json_disabled(struct json *json, struct delta_table *table)
{
	int count = 0;

	if (!json_take(json, '['))
		return 0;
	if (json_take(json, ']'))
		return 1;
	do
	{
		const char *name;
		int length, escaped, index;

		if (++count > MAXIMUM_DISABLED || !json_string(json, &name, &length, &escaped))
			return 0;
		for (index = 0; !escaped && index < NUMBER_OF_DELTA_CAPABILITIES; index++)
		{
			if (json_is(name, length, delta_capability_names[index]))
				table->disabled |= 1UL << index;
		}
	} while (json_take(json, ','));
	return json_take(json, ']');
}

/* the document into table; 0 (and why) if it is not one this build reads */
static int delta_table_parse(const char *document, size_t size, struct delta_table *table, const char **why)
{
	struct json json = { document, document + size, 0 };
	long long format = 0, value;
	int seen = 0;

	memset(table, 0, sizeof(*table));
	*why = "it is not a legacy table";
	if (size > DELTA_LEGACY_DOCUMENT_SIZE || !json_take(&json, '{') || json_take(&json, '}'))
		return 0;
	json.depth = 1;
	do
	{
		const char *key;
		int length, escaped, bit = 0, ok;

		if (!json_string(&json, &key, &length, &escaped) || !json_take(&json, ':'))
			return 0;
		if (escaped)
			ok = json_skip(&json);
		else if (json_is(key, length, "delta_legacy"))
			bit = 1, ok = json_integer(&json, 0, 1000000, &format);
		else if (json_is(key, length, "serial"))
			bit = 2, ok = json_integer(&json, 1, 4294967295LL, &value), table->serial = (unsigned int)value;
		else if (json_is(key, length, "issued"))
			bit = 4, ok = json_integer(&json, 0, 1LL << 53, &table->issued);
		else if (json_is(key, length, "wires"))
			bit = 8, ok = json_wires(&json, table);
		else if (json_is(key, length, "disabled_capabilities"))
			bit = 16, ok = json_disabled(&json, table);
		else
			ok = json_skip(&json);
		/* (a key twice would read differently elsewhere) */
		if (!ok || (seen & bit))
			return 0;
		seen |= bit;
	} while (json_take(&json, ','));
	if (!json_take(&json, '}'))
		return 0;
	json_space(&json);
	if (json.at != json.end || (seen & (1 | 2 | 8)) != (1 | 2 | 8))
		return 0;
	if (format != DELTA_LEGACY_FORMAT)
	{
		*why = "its format is newer than this build reads";
		return 0;
	}
	if (table->has_row && !(table->minimum <= table->announce && table->announce <= table->maximum))
	{
		*why = "its row for this build's wire is not a range";
		return 0;
	}
	return 1;
}

/* whether the table's row (if it has one) widens the built-in numbers: it
may announce a newer number and join a wider range, never less */
static int delta_table_widens(const struct delta_table *table)
{
	return !table->has_row || (table->announce >= HALO_PORT_NETWORK_VERSION &&
		table->minimum <= HALO_PORT_NETWORK_VERSION_MINIMUM && table->maximum >= HALO_PORT_NETWORK_VERSION_MAXIMUM);
}

/* ---------- the signature */

static int delta_key_set(const unsigned char *key)
{
	unsigned char any = 0;
	int index;

	for (index = 0; index < 32; index++)
		any |= key[index];
	return any != 0;
}

static int delta_has_key(void)
{
	size_t index;

	for (index = 0; index < DELTA_KEY_COUNT; index++)
	{
		if (delta_key_set(delta_public_keys[index]))
			return 1;
	}
	return 0;
}

static int delta_hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* the signature's 128 hex digits (and white space after them) into bytes */
static int delta_signature_parse(const char *text, size_t length, unsigned char *signature)
{
	size_t index;

	while (length > DELTA_SIGNATURE_SIZE * 2 &&
		(text[length - 1] == '\n' || text[length - 1] == '\r' || text[length - 1] == ' ' || text[length - 1] == '\t'))
	{
		length--;
	}
	if (length != DELTA_SIGNATURE_SIZE * 2)
		return 0;
	for (index = 0; index < DELTA_SIGNATURE_SIZE; index++)
	{
		int high = delta_hex_digit(text[index * 2]);
		int low = delta_hex_digit(text[index * 2 + 1]);

		if (high < 0 || low < 0)
			return 0;
		signature[index] = (unsigned char)(high << 4 | low);
	}
	return 1;
}

static int delta_signature_check(const char *document, size_t size, const unsigned char *signature)
{
	size_t index;

	for (index = 0; index < DELTA_KEY_COUNT; index++)
	{
		/* (Monocypher's check turns away an S past the group's order) */
		if (delta_key_set(delta_public_keys[index]) &&
			crypto_ed25519_check(signature, delta_public_keys[index], (const unsigned char *)document, size) == 0)
		{
			return 1;
		}
	}
	return 0;
}

/* ---------- the table in use */

enum delta_result
{
	_delta_taken,
	_delta_not_newer,
	_delta_invalid,
};

static void delta_use_built_in(void)
{
	delta.announce = HALO_PORT_NETWORK_VERSION;
	delta.minimum = HALO_PORT_NETWORK_VERSION_MINIMUM;
	delta.maximum = HALO_PORT_NETWORK_VERSION_MAXIMUM;
	delta.disabled = 0;
}

static void delta_cache_path(char *path, size_t size)
{
	snprintf(path, size, "%s/%s", platform_save_root(), DELTA_CACHE_NAME);
}

/* the signed table written to the cache: a new file beside it, then moved
over it */
static void delta_cache_write(const char *signed_table, int size)
{
	char path[1024], temporary[1100];
	FILE *file;
	int ok;

	delta_cache_path(path, sizeof(path));
	snprintf(temporary, sizeof(temporary), "%s.new", path);
	file = fopen(temporary, "wb");
	if (!file)
		return;
	ok = fwrite(signed_table, 1, (size_t)size, file) == (size_t)size;
	ok = fclose(file) == 0 && ok;
	/* (Windows does not rename over a file) */
	if (ok && rename(temporary, path) != 0)
	{
		remove(path);
		ok = rename(temporary, path) == 0;
	}
	if (!ok)
	{
		remove(temporary);
		platform_log("Delta: the legacy table could not be cached");
	}
}

/* a document and its signature (hex) from source: checked, and used if it
is newer than the one in use */
static enum delta_result delta_take(const char *document, size_t size, const char *signature_text,
	size_t signature_length, const char *source, int cache)
{
	unsigned char signature[DELTA_SIGNATURE_SIZE];
	struct delta_table table;
	const char *why;
	char *signed_table;
	int signed_size;

	if (size > DELTA_LEGACY_DOCUMENT_SIZE)
	{
		platform_log("Delta: dropped the legacy table from %s: it is too large", source);
		return _delta_invalid;
	}
	if (!delta_signature_parse(signature_text, signature_length, signature) ||
		!delta_signature_check(document, size, signature))
	{
		platform_log("Delta: dropped the legacy table from %s: its signature does not match", source);
		return _delta_invalid;
	}
	if (!delta_table_parse(document, size, &table, &why))
	{
		platform_log("Delta: dropped the legacy table from %s: %s", source, why);
		return _delta_invalid;
	}
	if (!delta_table_widens(&table))
	{
		platform_log("Delta: dropped the legacy table %u from %s: it narrows this build's numbers", table.serial,
			source);
		return _delta_invalid;
	}
	signed_size = DELTA_SIGNATURE_SIZE * 2 + 1 + (int)size;
	signed_table = malloc((size_t)signed_size);
	if (!signed_table)
		return _delta_invalid;
	memcpy(signed_table, signature_text, DELTA_SIGNATURE_SIZE * 2);
	signed_table[DELTA_SIGNATURE_SIZE * 2] = '\n';
	memcpy(signed_table + DELTA_SIGNATURE_SIZE * 2 + 1, document, size);

	pthread_mutex_lock(&delta_lock);
	if (delta.override || table.serial <= delta.serial)
	{
		pthread_mutex_unlock(&delta_lock);
		free(signed_table);
		return _delta_not_newer;
	}
	free(delta.signed_table);
	delta.signed_table = signed_table;
	delta.signed_size = signed_size;
	delta.serial = table.serial;
	delta_use_built_in();
	if (table.has_row)
	{
		delta.announce = table.announce;
		delta.minimum = table.minimum;
		delta.maximum = table.maximum;
	}
	delta.disabled = table.disabled;
	pthread_mutex_unlock(&delta_lock);

	platform_log("Delta: legacy table %u from %s: announcing %d, joining %d to %d%s", table.serial, source,
		table.has_row ? table.announce : HALO_PORT_NETWORK_VERSION,
		table.has_row ? table.minimum : HALO_PORT_NETWORK_VERSION_MINIMUM,
		table.has_row ? table.maximum : HALO_PORT_NETWORK_VERSION_MAXIMUM,
		table.has_row ? "" : " (no row for " DELTA_WIRE ": the built-in numbers)");
	if (cache)
		delta_cache_write(signed_table, signed_size);
	return _delta_taken;
}

/* a signed table (signature, line feed, document) from source */
static enum delta_result delta_take_signed(const char *signed_table, size_t size, const char *source, int cache)
{
	if (!signed_table || size < DELTA_SIGNATURE_SIZE * 2 + 1 || size > DELTA_LEGACY_SIGNED_SIZE ||
		signed_table[DELTA_SIGNATURE_SIZE * 2] != '\n')
	{
		platform_log("Delta: dropped the legacy table from %s: it is not a signed table", source);
		return _delta_invalid;
	}
	return delta_take(signed_table + DELTA_SIGNATURE_SIZE * 2 + 1, size - DELTA_SIGNATURE_SIZE * 2 - 1,
		signed_table, DELTA_SIGNATURE_SIZE * 2, source, cache);
}

/* network.legacy_table: an unsigned file, used instead of every signed one
(beside config.toml unless a full path) */
static int delta_load_override(void)
{
	const char *name = config_string("network.legacy_table");
	struct delta_table table;
	const char *why = "it cannot be read";
	char path[1024];
	char *file;
	size_t size = 0;
	int ok;

	if (!name[0])
		return 0;
	if (name[0] == '/' || name[0] == '\\' || (name[0] && name[1] == ':'))
		snprintf(path, sizeof(path), "%s", name);
	else
	{
		config_folder(path, sizeof(path));
		snprintf(path + strlen(path), sizeof(path) - strlen(path), "%s", name);
	}
	file = config_file_read(path, &size);
	ok = file && delta_table_parse(file, size, &table, &why);
	free(file);
	delta.override = 1;
	if (ok && table.has_row)
	{
		delta.announce = table.announce;
		delta.minimum = table.minimum;
		delta.maximum = table.maximum;
	}
	if (ok)
		delta.disabled = table.disabled;
	if (ok && table.has_row)
	{
		platform_log("Delta: WARNING: a local, unsigned legacy table is in use (network.legacy_table, %s): "
			"announcing %d, joining %d to %d; no signed table is fetched or passed on", path, delta.announce,
			delta.minimum, delta.maximum);
	}
	else
	{
		platform_log("Delta: WARNING: the local legacy table (network.legacy_table, %s) is not used: %s; "
			"the built-in numbers are, and no signed table is fetched or passed on", path,
			ok ? "it has no row for " DELTA_WIRE : why);
	}
	return 1;
}

static void delta_load(void)
{
	char path[1024];
	char *file;
	size_t size = 0;

	pthread_mutex_lock(&delta_lock);
	delta_use_built_in();
	pthread_mutex_unlock(&delta_lock);
	if (delta_load_override())
		return;
	delta_cache_path(path, sizeof(path));
	file = config_file_read(path, &size);
	if (file)
	{
		delta_take_signed(file, size, "the cache", 0);
		free(file);
	}
}

/* ---------- fetching (a thread of this file's) */

#ifdef HALO_GAME_BROWSER

/* the document at url and its signature (url.sig), from source; none there
(the server says it has no document: absent set, nothing logged) is
_delta_invalid too */
static enum delta_result delta_fetch(const char *url, const char *source, int *absent)
{
	/* (one byte past the cap shows a document cut short) */
	static char document[DELTA_LEGACY_DOCUMENT_SIZE + 2];
	char signature[256], signature_url[600], error[256];
	int status;

	*absent = 0;
	status = posix_browser_request(url, NULL, NULL, document, sizeof(document), error, sizeof(error));
	if (status == 404)
	{
		*absent = 1;
		return _delta_invalid;
	}
	if (status != 200)
	{
		platform_log("Delta: no legacy table from %s (%s)", source, status ? "the server refused" : error);
		return _delta_invalid;
	}
	snprintf(signature_url, sizeof(signature_url), "%s.sig", url);
	status = posix_browser_request(signature_url, NULL, NULL, signature, sizeof(signature), error, sizeof(error));
	if (status != 200)
	{
		platform_log("Delta: no legacy table's signature from %s (%s)", source,
			status ? "the server refused" : error);
		return _delta_invalid;
	}
	return delta_take(document, strlen(document), signature, strlen(signature), source, 1);
}

static void delta_refresh(int *failed)
{
	/* (none published anywhere: said once a run, not at every retry) */
	static int absence_said;
	const char *base = config_string("network.browser_url");
	char url[512];
	size_t length = strlen(base);
	int site_absent, github_absent;

	/* (with or without the final slash) */
	while (length && base[length - 1] == '/')
		length--;
	snprintf(url, sizeof(url), "%.*s/v1/delta/legacy", (int)length, base);
	*failed = 0;
	if (delta_fetch(url, "Delta List", &site_absent) != _delta_invalid ||
		delta_fetch(DELTA_GITHUB_URL, "GitHub", &github_absent) != _delta_invalid)
	{
		absence_said = 0;
		return;
	}
	*failed = 1;
	if (site_absent && github_absent)
	{
		if (!absence_said)
			platform_log("Delta: no legacy table is published (Delta List and GitHub have none); looked for "
				"again quietly");
		absence_said = 1;
		return;
	}
	if (site_absent)
		platform_log("Delta: no legacy table from Delta List (it has none)");
	if (github_absent)
		platform_log("Delta: no legacy table from GitHub (it has none)");
}

static void *delta_thread(void *unused)
{
	(void)unused;
	for (;;)
	{
		struct timespec deadline;
		int failed = 0, interval;

		if (!delta_legacy_override())
			delta_refresh(&failed);
		interval = failed ? DELTA_RETRY_INTERVAL : DELTA_REFRESH_INTERVAL;
		clock_gettime(CLOCK_REALTIME, &deadline);
		deadline.tv_sec += interval / 1000;
		deadline.tv_nsec += (long)(interval % 1000) * 1000000L;
		if (deadline.tv_nsec >= 1000000000L)
		{
			deadline.tv_sec++;
			deadline.tv_nsec -= 1000000000L;
		}
		pthread_mutex_lock(&delta_lock);
		/* (until the deadline: nothing signals it yet) */
		while (pthread_cond_timedwait(&delta_wake, &delta_lock, &deadline) == 0)
			;
		pthread_mutex_unlock(&delta_lock);
	}
	return NULL;
}

#endif

/* ---------- public code */

void delta_legacy_start(void)
{
	static int started;

	pthread_once(&delta_once, delta_load);
	if (started)
		return;
	started = 1;
#ifdef HALO_GAME_BROWSER
	/* (no key, nothing to check a table with; no list server, the player's
	choice to talk to none) */
	if (!delta_legacy_override() && delta_has_key() && config_string("network.browser_url")[0])
	{
		pthread_t thread;

		if (pthread_create(&thread, NULL, delta_thread, NULL) == 0)
			pthread_detach(thread);
		else
			platform_log("Delta: could not start the legacy table's thread");
	}
#endif
}

int delta_legacy_announce(void)
{
	int number;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	number = delta.announce;
	pthread_mutex_unlock(&delta_lock);
	return number;
}

int delta_legacy_minimum(void)
{
	int number;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	number = delta.minimum;
	pthread_mutex_unlock(&delta_lock);
	return number;
}

int delta_legacy_maximum(void)
{
	int number;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	number = delta.maximum;
	pthread_mutex_unlock(&delta_lock);
	return number;
}

unsigned int delta_legacy_serial(void)
{
	unsigned int serial;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	serial = delta.override ? 0 : delta.serial;
	pthread_mutex_unlock(&delta_lock);
	return serial;
}

int delta_legacy_signed(char *buffer, int size)
{
	int used = 0;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	if (!delta.override && delta.signed_table && delta.signed_size <= size)
	{
		memcpy(buffer, delta.signed_table, (size_t)delta.signed_size);
		used = delta.signed_size;
	}
	pthread_mutex_unlock(&delta_lock);
	return used;
}

int delta_legacy_offer(const char *signed_table, int size)
{
	pthread_once(&delta_once, delta_load);
	if (size < 0 || delta_legacy_override())
		return 0;
	return delta_take_signed(signed_table, (size_t)size, "another machine", 1) == _delta_taken;
}

int delta_capability_disabled(int capability)
{
	int disabled;

	if (capability < 0 || capability >= NUMBER_OF_DELTA_CAPABILITIES)
		return 0;
	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	disabled = (delta.disabled >> capability) & 1;
	pthread_mutex_unlock(&delta_lock);
	return disabled;
}

int delta_legacy_override(void)
{
	int override;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	override = delta.override;
	pthread_mutex_unlock(&delta_lock);
	return override;
}

int delta_legacy_relay(void)
{
	return !delta_legacy_override() && delta_has_key();
}
