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
The row may also carry "layouts" (delta.h): facts about newer OpenCE versions
that network_rules.c reads them by (delta_legacy_layouts), within its limits.

Where tables come from, the newest serial winning (an equal or older one is
never taken, so an old copy cannot roll a machine back):
- the cache, delta_legacy.signed in the save root, read at start;
- Delta List (network.browser_url's /v1/delta/legacy and .sig), fetched in
the background at start and every few hours, and GitHub's copy
(ChupathingyCE/chupathingyce's delta-table branch) when the site cannot be
reached or gives no valid table;
- another machine (delta_legacy_offer: Delta Peer).
A table that does not verify is dropped, and logged; nothing is shown.

A console with HTTPS (HALO_CONSOLE_HTTPS: port/console/https/console_https.h)
fetches the same two over its own TLS, the site by its pinned key and
GitHub by its pinned roots, when its port asks (delta_legacy_refresh, on a
thread of the port's: this file starts none there), and caches the table
beside config.toml.

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
#define DELTA_FETCHES
#elif defined(HALO_CONSOLE_HTTPS)
#include "console_https.h"
#define DELTA_FETCHES
#endif

/* (network_rules.c reads the table's layouts through delta_legacy_layouts,
told of it at start: port/linux/game/network_rules.h) */
void network_rules_set_layout_source(int (*source)(struct delta_layout_fact *facts, int count));

#include "monocypher.h"
#include "monocypher-ed25519.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DELTA_LEGACY_FORMAT 1
#define DELTA_SIGNATURE_SIZE 64
#define DELTA_KEY_COUNT (sizeof(delta_public_keys) / sizeof(*delta_public_keys))
/* a serial's epoch, its top byte (delta_key.h) */
#define DELTA_EPOCH(serial) ((unsigned int)(serial) >> 24)
#define DELTA_CACHE_NAME "delta_legacy.signed"
#define DELTA_GITHUB_URL "https://raw.githubusercontent.com/ChupathingyCE/chupathingyce/delta-table/legacy.json"

/* (fetched this often, and this long after a failure, in milliseconds; a
test's build may give its own) */
#ifndef DELTA_REFRESH_INTERVAL
#define DELTA_REFRESH_INTERVAL (4 * 60 * 60 * 1000)
#endif
#ifndef DELTA_SERVER_REFRESH_INTERVAL
#define DELTA_SERVER_REFRESH_INTERVAL (30 * 60 * 1000)
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
	/* (a distributed message's kind is a byte; 0 is none) */
	MAXIMUM_KIND = 255,
};

/* the capability registry's names (delta.h), as the table spells them */
static const char *const delta_capability_names[] =
{
	"platform", "profile", "server_messages", "chat", "ce_maps", "md_maps", "coop", "ai_sync", "vote",
	"console_slots", "moderation", "bomb", "ping", "bomb_movement", "bomb_traits", "team_change",
	"server_badge", "object_sync",
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
	/* ... and the OpenCE build the cross-play gate proved it with
	("build-145"; empty: not said) */
	char follows[DELTA_FOLLOWS_SIZE];
	/* ... and the versions it proves for hosting past announce ("hosting",
	oldest first; none: announce alone) */
	int hosting_count;
	int hosting[DELTA_HOSTING_MAXIMUM];
	unsigned long disabled;
	/* ... and the row's layouts, oldest first */
	int layout_count;
	struct delta_layout_fact layouts[DELTA_LAYOUT_FACTS];
};

static pthread_mutex_t delta_lock = PTHREAD_MUTEX_INITIALIZER;
/* (the cache's file: one writer at a time, the fetching thread's or the
game's, taken before delta_lock) */
static pthread_mutex_t delta_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t delta_once = PTHREAD_ONCE_INIT;
#ifdef HALO_GAME_BROWSER
/* (the fetching thread waits on it for its next time) */
static pthread_cond_t delta_wake = PTHREAD_COND_INITIALIZER;
#endif

static struct
{
	int announce, minimum, maximum;
	char follows[DELTA_FOLLOWS_SIZE];
	unsigned long disabled;
	int layout_count;
	struct delta_layout_fact layouts[DELTA_LAYOUT_FACTS];
	/* the signed table in use (malloc'd), and its serial; none: 0 */
	unsigned int serial;
	char *signed_table;
	int signed_size;
	/* a local, unsigned table is in use */
	int override;
	/* auto-announce: the newest version the table proves for hosting; the
	number announced now; whether a hosted game holds it; network.announce's
	pin (0: "latest"), and the last pin said to be refused (so said once) */
	int hosting_newest;
	int hosted;
	int held;
	int pin;
	int pin_refused;
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
					if (json->at + index >= json->end || delta_hex_digit(json->at[index]) < 0)
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

/* a row's "follows": an OpenCE build's tag, letters, digits, '.', '-' and
'_' (shown in the log and the server's status as it is) */
static int json_follows(struct json *json, struct delta_table *table)
{
	const char *text;
	int length, escaped, index;

	if (!json_string(json, &text, &length, &escaped) || escaped || length < 1 || length >= DELTA_FOLLOWS_SIZE)
		return 0;
	for (index = 0; index < length; index++)
	{
		char c = text[index];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
			c == '_'))
		{
			return 0;
		}
	}
	memcpy(table->follows, text, (size_t)length);
	table->follows[length] = 0;
	return 1;
}

/* a layout fact's "dropped_messages": [kind, ...], each 1 to 255 */
static int json_kinds(struct json *json, struct delta_layout_fact *fact)
{
	int count = 0;

	if (!json_take(json, '['))
		return 0;
	if (json_take(json, ']'))
		return 1;
	do
	{
		long long kind;

		if (++count > MAXIMUM_KIND || !json_integer(json, 1, MAXIMUM_KIND, &kind))
			return 0;
		fact->dropped[kind >> 3] |= (unsigned char)(1 << (kind & 7));
	} while (json_take(json, ','));
	return json_take(json, ']');
}

/* a layout fact: {"settings_size": S, "options_size": O,
"shield_effects_maximum": E, "dropped_messages": [K, ...]}, each optional,
others passed over (a later build's) */
static int json_fact(struct json *json, struct delta_layout_fact *fact)
{
	int seen = 0;

	if (!json_take(json, '{'))
		return 0;
	if (json_take(json, '}'))
		return 1;
	json->depth++;
	do
	{
		const char *key;
		int length, escaped, bit = 0, ok;
		long long value = 0;
		unsigned short *field = NULL;

		if (!json_string(json, &key, &length, &escaped) || !json_take(json, ':'))
			return 0;
		if (!escaped && json_is(key, length, "settings_size"))
			field = &fact->settings_size, bit = 1;
		else if (!escaped && json_is(key, length, "options_size"))
			field = &fact->options_size, bit = 2;
		else if (!escaped && json_is(key, length, "shield_effects_maximum"))
			field = &fact->shield_effects_maximum, bit = 4;
		if (field)
		{
			ok = json_integer(json, 0, MAXIMUM_VERSION, &value);
			if (ok)
				*field = (unsigned short)value;
		}
		else if (!escaped && json_is(key, length, "dropped_messages"))
			bit = 8, ok = json_kinds(json, fact);
		else
			ok = json_skip(json);
		/* (a key twice would read differently elsewhere) */
		if (!ok || (seen & bit))
			return 0;
		seen |= bit;
	} while (json_take(json, ','));
	json->depth--;
	return json_take(json, '}');
}

/* a row's "layouts": {"<version>": fact, ...}, at most DELTA_LAYOUT_FACTS,
each version once, kept oldest first */
static int json_layouts(struct json *json, struct delta_table *table)
{
	if (!json_take(json, '{'))
		return 0;
	if (json_take(json, '}'))
		return 1;
	json->depth++;
	do
	{
		struct delta_layout_fact fact;
		struct json number;
		const char *key;
		int length, escaped, index;
		long long version;

		if (!json_string(json, &key, &length, &escaped) || escaped || !json_take(json, ':'))
			return 0;
		/* (the key a version: its digits read as a number would be) */
		number.at = key;
		number.end = key + length;
		number.depth = 0;
		if (length < 1 || key[0] == ' ' || !json_integer(&number, 1, MAXIMUM_VERSION, &version) ||
			number.at != number.end)
		{
			return 0;
		}
		memset(&fact, 0, sizeof(fact));
		fact.version = (unsigned short)version;
		if (table->layout_count >= DELTA_LAYOUT_FACTS || !json_fact(json, &fact))
			return 0;
		for (index = table->layout_count; index > 0 && table->layouts[index - 1].version > fact.version; index--)
			table->layouts[index] = table->layouts[index - 1];
		if (index > 0 && table->layouts[index - 1].version == fact.version)
			return 0;
		table->layouts[index] = fact;
		table->layout_count++;
	} while (json_take(json, ','));
	json->depth--;
	return json_take(json, '}');
}

/* a row's "hosting": [V, ...], the versions the cross-play gate proved for
hosting (our host, their clients), oldest first, each once, at most
DELTA_HOSTING_MAXIMUM */
static int json_hosting(struct json *json, struct delta_table *table)
{
	if (!json_take(json, '['))
		return 0;
	if (json_take(json, ']'))
		return 1;
	do
	{
		long long value;

		if (table->hosting_count == DELTA_HOSTING_MAXIMUM || !json_integer(json, 1, MAXIMUM_VERSION, &value))
			return 0;
		if (table->hosting_count && value <= table->hosting[table->hosting_count - 1])
			return 0;
		table->hosting[table->hosting_count++] = (int)value;
	} while (json_take(json, ','));
	return json_take(json, ']');
}

/* a wire's row: {"announce": A, "minimum": Mi, "maximum": Ma}, and
optionally "follows": "build-N", "hosting" and "layouts" */
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
		else if (!escaped && json_is(key, length, "follows"))
		{
			/* (once) */
			if ((seen & 8) || !json_follows(json, table))
				return 0;
			seen |= 8;
			continue;
		}
		else if (!escaped && json_is(key, length, "layouts"))
		{
			if ((seen & 16) || !json_layouts(json, table))
				return 0;
			seen |= 16;
			continue;
		}
		else if (!escaped && json_is(key, length, "hosting"))
		{
			if ((seen & 32) || !json_hosting(json, table))
				return 0;
			seen |= 32;
			continue;
		}
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
	return json_take(json, '}') && (seen & 7) == 7;
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
		{
			/* (not 0xFFFFFFFF: on the wire that is a machine taking no tables) */
			bit = 2, ok = json_integer(&json, 1, 4294967294LL, &value);
			if (ok)
				table->serial = (unsigned int)value;
		}
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

/* the key that signed the document (its index in delta_public_keys), or -1 */
static int delta_signature_signer(const char *document, size_t size, const unsigned char *signature)
{
	size_t index;

	for (index = 0; index < DELTA_KEY_COUNT; index++)
	{
		/* (Monocypher's check turns away an S past the group's order) */
		if (delta_key_set(delta_public_keys[index]) &&
			crypto_ed25519_check(signature, delta_public_keys[index], (const unsigned char *)document, size) == 0)
		{
			return (int)index;
		}
	}
	return -1;
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
	delta.hosting_newest = HALO_PORT_NETWORK_VERSION;
	delta.minimum = HALO_PORT_NETWORK_VERSION_MINIMUM;
	delta.maximum = HALO_PORT_NETWORK_VERSION_MAXIMUM;
	delta.follows[0] = 0;
	delta.disabled = 0;
	delta.layout_count = 0;
}

/* the table's row's layouts into the ones in use (delta_lock held) */
static void delta_use_layouts(const struct delta_table *table)
{
	delta.layout_count = table->has_row ? table->layout_count : 0;
	memcpy(delta.layouts, table->layouts, sizeof(delta.layouts));
}

/* the newest version a table's row proves for hosting */
static int delta_row_hosting_newest(const struct delta_table *table)
{
	int newest = table->announce;

	if (table->hosting_count && table->hosting[table->hosting_count - 1] > newest)
		newest = table->hosting[table->hosting_count - 1];
	return newest;
}

/* the number to announce now (delta_lock held): network.announce's pin if
it is one this machine may announce (from the minimum to the newest the
table proves for hosting), else that newest; refused set to a pin that is
not */
static int delta_announce_target(int *refused)
{
	*refused = 0;
	if (!delta.pin)
		return delta.hosting_newest;
	if (delta.pin < delta.minimum || delta.pin > delta.hosting_newest)
	{
		*refused = delta.pin;
		return delta.hosting_newest;
	}
	return delta.pin;
}

/* network.announce: "latest" (or empty) 0, a number its number, anything
else 0 and said */
static int delta_read_pin(void)
{
	const char *text = config_string("network.announce");
	long value = 0;
	int index;

	if (!text[0] || !strcmp(text, "latest"))
		return 0;
	for (index = 0; text[index]; index++)
	{
		if (text[index] < '0' || text[index] > '9' || index >= 5)
		{
			platform_log("Delta: network.announce is \"%s\", not \"latest\" or a network version: following "
				"the legacy table", text);
			return 0;
		}
		value = value * 10 + (text[index] - '0');
	}
	if (value < 1 || value > MAXIMUM_VERSION)
	{
		platform_log("Delta: network.announce is %s, not a network version: following the legacy table", text);
		return 0;
	}
	return (int)value;
}

static void delta_cache_path(char *path, size_t size)
{
#if defined(HALO_CONSOLE_HTTPS) && !defined(HALO_GAME_BROWSER)
	/* (a console's: beside its config.toml) */
	config_folder(path, size);
	snprintf(path + strlen(path), size - strlen(path), "%s", DELTA_CACHE_NAME);
#else
	snprintf(path, size, "%s/%s", platform_save_root(), DELTA_CACHE_NAME);
#endif
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
	int signed_size, signer = -1, maximum;

	if (size > DELTA_LEGACY_DOCUMENT_SIZE)
	{
		platform_log("Delta: dropped the legacy table from %s: it is too large", source);
		return _delta_invalid;
	}
	if (!delta_signature_parse(signature_text, signature_length, signature) ||
		(signer = delta_signature_signer(document, size, signature)) < 0)
	{
		platform_log("Delta: dropped the legacy table from %s: its signature does not match", source);
		return _delta_invalid;
	}
	if (!delta_table_parse(document, size, &table, &why))
	{
		platform_log("Delta: dropped the legacy table from %s: %s", source, why);
		return _delta_invalid;
	}
	/* (only the keys whose last epoch reaches it sign a serial's epoch: the
	recovery key alone opens a new one, delta_key.h) */
	if (DELTA_EPOCH(table.serial) > delta_key_last_epochs[signer])
	{
		platform_log("Delta: dropped the legacy table %u from %s: its epoch (%u) is past its key's last (%u)",
			table.serial, source, DELTA_EPOCH(table.serial), (unsigned int)delta_key_last_epochs[signer]);
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
		/* (and the numbers past the one announced that are presumed
		additive, DELTA_LEGACY_PRESUMED, whatever the row says) */
		if (delta.maximum < delta.announce + DELTA_LEGACY_PRESUMED)
			delta.maximum = delta.announce + DELTA_LEGACY_PRESUMED > 65535 ? 65535 : delta.announce + DELTA_LEGACY_PRESUMED;
		delta.hosting_newest = delta_row_hosting_newest(&table);
		memcpy(delta.follows, table.follows, sizeof(delta.follows));
	}
	delta.disabled = table.disabled;
	delta_use_layouts(&table);
	maximum = delta.maximum;
	pthread_mutex_unlock(&delta_lock);

	platform_log("Delta: legacy table %u from %s: announcing %d, joining %d to %d%s%s%s; %d layout facts", table.serial,
		source, table.has_row ? delta_row_hosting_newest(&table) : HALO_PORT_NETWORK_VERSION,
		table.has_row ? table.minimum : HALO_PORT_NETWORK_VERSION_MINIMUM,
		maximum,
		table.has_row ? "" : " (no row for " DELTA_WIRE ": the built-in numbers)",
		table.has_row && table.follows[0] ? "; following OpenCE " : "", table.has_row ? table.follows : "",
		table.has_row ? table.layout_count : 0);
	/* (from the cache's own copy, and only while the table is still the
	one in use: another thread may take a newer one, and free this one,
	meanwhile) */
	if (cache)
	{
		char *copy = malloc((size_t)signed_size);
		int current;

		if (copy)
		{
			pthread_mutex_lock(&delta_cache_lock);
			pthread_mutex_lock(&delta_lock);
			current = delta.serial == table.serial && delta.signed_table == signed_table;
			if (current)
				memcpy(copy, signed_table, (size_t)signed_size);
			pthread_mutex_unlock(&delta_lock);
			if (current)
				delta_cache_write(copy, signed_size);
			pthread_mutex_unlock(&delta_cache_lock);
			free(copy);
		}
	}
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
		delta.hosting_newest = delta_row_hosting_newest(&table);
		delta.minimum = table.minimum;
		delta.maximum = table.maximum;
		memcpy(delta.follows, table.follows, sizeof(delta.follows));
	}
	if (ok)
	{
		delta.disabled = table.disabled;
		delta_use_layouts(&table);
	}
	if (ok && table.has_row)
	{
		platform_log("Delta: WARNING: a local, unsigned legacy table is in use (network.legacy_table, %s): "
			"announcing %d, joining %d to %d, %d layout facts; no signed table is fetched or passed on", path,
			delta.hosting_newest, delta.minimum, delta.maximum, delta.layout_count);
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

	int pin = delta_read_pin();
	int refused;

	pthread_mutex_lock(&delta_lock);
	delta_use_built_in();
	delta.pin = pin;
	pthread_mutex_unlock(&delta_lock);
	if (!delta_load_override())
	{
		delta_cache_path(path, sizeof(path));
		file = config_file_read(path, &size);
		if (file)
		{
			delta_take_signed(file, size, "the cache", 0);
			free(file);
		}
	}
	pthread_mutex_lock(&delta_lock);
	delta.hosted = delta_announce_target(&refused);
	delta.pin_refused = refused;
	pthread_mutex_unlock(&delta_lock);
	if (pin && !refused)
		platform_log("Delta: announcing OpenCE network version %d, pinned by network.announce", pin);
	else if (refused)
		platform_log("Delta: network.announce's %d is not one this machine announces (%d to %d): announcing %d, the "
			"newest the legacy table proves for hosting", refused, delta.minimum, delta.hosted, delta.hosted);
}

/* ---------- fetching (a thread of this file's; a console's port's) */

#ifdef DELTA_FETCHES

#ifdef HALO_GAME_BROWSER
#define delta_get posix_browser_request
#else
/* (a console's answers are read whole, headers and all, into the buffer
before the body is moved to its start: room for them) */
#define DELTA_HEADER_ROOM 2048

/* posix_browser_request's GET, over the console's HTTPS: https://host/path,
the hosts console_https.c knows alone; the status, or 0 and the error */
static int delta_get(const char *url, const char *form, const char *content_type, char *response,
	int response_size, char *error, int error_size)
{
	char host[64];
	const char *path;
	size_t length;
	int status = 0, result;

	(void)form;
	(void)content_type;
	if (strncmp(url, "https://", 8) || !(path = strchr(url + 8, '/')) ||
		(length = (size_t)(path - (url + 8))) >= sizeof(host))
	{
		snprintf(error, (size_t)error_size, "not an https:// address");
		return 0;
	}
	memcpy(host, url + 8, length);
	host[length] = 0;
	result = console_https_call(host, path, NULL, &status, response, response_size, 20000);
	if (result != CONSOLE_HTTPS_OK)
	{
		snprintf(error, (size_t)error_size, "%s", console_https_error_string(result));
		return 0;
	}
	return status;
}
#endif

#ifndef DELTA_HEADER_ROOM
#define DELTA_HEADER_ROOM 0
#endif

/* the document at url and its signature (url.sig), from source; none there
(the server says it has no document: absent set, nothing logged) is
_delta_invalid too */
static enum delta_result delta_fetch(const char *url, const char *source, int *absent)
{
	/* (one byte past the cap shows a document cut short; a console's is on
	the port's thread's stack, so that its memory is not kept between
	fetches) */
#ifdef HALO_GAME_BROWSER
	static
#endif
	char document[DELTA_LEGACY_DOCUMENT_SIZE + 2 + DELTA_HEADER_ROOM];
	char signature[256 + DELTA_HEADER_ROOM], signature_url[600], error[256];
	int status;

	*absent = 0;
	status = delta_get(url, NULL, NULL, document, sizeof(document), error, sizeof(error));
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
	status = delta_get(signature_url, NULL, NULL, signature, sizeof(signature), error, sizeof(error));
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
#ifdef HALO_GAME_BROWSER
	const char *base = config_string("network.browser_url");
#else
	/* (a console asks the site it pins, whatever network.browser_url says) */
	const char *base = "https://" CONSOLE_HTTPS_SITE;
#endif
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

#ifdef HALO_GAME_BROWSER
static void *delta_thread(void *unused)
{
	(void)unused;
	for (;;)
	{
		struct timespec deadline;
		int failed = 0, interval;

		if (!delta_legacy_override())
			delta_refresh(&failed);
		/* (a dedicated server sooner: its listing follows the table) */
		interval = failed ? DELTA_RETRY_INTERVAL : getenv("HALO_DEDICATED") && getenv("HALO_DEDICATED")[0] ?
			DELTA_SERVER_REFRESH_INTERVAL : DELTA_REFRESH_INTERVAL;
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
#else
int delta_legacy_refresh(void)
{
	int failed = 0;

	pthread_once(&delta_once, delta_load);
	/* (no key, nothing to check a table with; no list server, the player's
	choice to talk to none; no HTTPS yet) */
	if (delta_legacy_override() || !delta_has_key() || !config_string("network.browser_url")[0] ||
		!console_https_available())
		return 0;
	delta_refresh(&failed);
	return !failed;
}
#endif

#endif

/* ---------- public code */

void delta_legacy_start(void)
{
	static int started;

	pthread_once(&delta_once, delta_load);
	if (started)
		return;
	started = 1;
	network_rules_set_layout_source(delta_legacy_layouts);
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

/* the number announced, moved to the target when no hosted game holds it
(delta_lock held); what to say in *was (0: nothing changed) and *refused */
static int delta_settle(int *was, int *refused)
{
	int target = delta_announce_target(refused);

	*was = 0;
	if (*refused == delta.pin_refused)
		*refused = 0;
	else
		delta.pin_refused = *refused;
	if (!delta.held && delta.hosted != target)
	{
		*was = delta.hosted;
		delta.hosted = target;
	}
	return delta.hosted;
}

static void delta_settle_said(int number, int was, int refused)
{
	if (refused)
		platform_log("Delta: network.announce's %d is not one this machine announces now: announcing %d", refused,
			number);
	if (was)
		platform_log("Delta: now announcing OpenCE network version %d (was %d)%s", number, was,
			delta_legacy_pinned() ? ", pinned by network.announce" : ", the newest the legacy table proves for hosting");
}

int delta_legacy_announce(void)
{
	int number, was, refused;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	number = delta_settle(&was, &refused);
	pthread_mutex_unlock(&delta_lock);
	if (was || refused)
		delta_settle_said(number, was, refused);
	return number;
}

int delta_legacy_hosting_newest(void)
{
	int number;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	number = delta.hosting_newest;
	pthread_mutex_unlock(&delta_lock);
	return number;
}

void delta_legacy_hold(int hold)
{
	int number, was, refused;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	if (delta.held == !!hold)
	{
		pthread_mutex_unlock(&delta_lock);
		return;
	}
	delta.held = !!hold;
	number = delta_settle(&was, &refused);
	pthread_mutex_unlock(&delta_lock);
	if (was || refused)
		delta_settle_said(number, was, refused);
}

int delta_legacy_pinned(void)
{
	int pin, refused;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	pin = delta.pin;
	delta_announce_target(&refused);
	pthread_mutex_unlock(&delta_lock);
	return pin && !refused ? pin : 0;
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

void delta_legacy_following(char *text, int size)
{
	char follows[DELTA_FOLLOWS_SIZE];
	unsigned int serial;
	int announce, override;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	memcpy(follows, delta.follows, sizeof(follows));
	serial = delta.serial;
	announce = delta.hosting_newest;
	override = delta.override;
	pthread_mutex_unlock(&delta_lock);
	if (size <= 0)
		return;
	if (follows[0])
		snprintf(text, size, "Following OpenCE %s", follows);
	else
		snprintf(text, size, "Following OpenCE network version %d", announce);
	if (override)
		snprintf(text + strlen(text), (size_t)size - strlen(text), " (a local table)");
	else if (serial)
		snprintf(text + strlen(text), (size_t)size - strlen(text), " (table %u)", serial);
	else
		snprintf(text + strlen(text), (size_t)size - strlen(text), " (built in)");
}

int delta_legacy_layouts(struct delta_layout_fact *facts, int count)
{
	int used;

	pthread_once(&delta_once, delta_load);
	pthread_mutex_lock(&delta_lock);
	used = count < delta.layout_count ? count : delta.layout_count;
	if (used > 0)
		memcpy(facts, delta.layouts, sizeof(*facts) * (size_t)used);
	pthread_mutex_unlock(&delta_lock);
	return used > 0 ? used : 0;
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
