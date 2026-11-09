/*
CONSOLE_PINS.C

The console pin document checked and read (console_pins.h): its size, its
Ed25519 signature by a console pin key (console_pins_key.h, Monocypher), the
signing key's last epoch, then the document by a strict parser that reads
the site's pins and passes over what it does not know.
*/

#include "console_pins.h"
#include "console_pins_key.h"

#include "monocypher.h"
#include "monocypher-ed25519.h"

#include <string.h>

#define KEY_COUNT ((int)(sizeof(console_pins_keys) / sizeof(*console_pins_keys)))
/* how deep a value passed over may nest */
#define SKIP_DEPTH 4

typedef char console_pins_epochs_assert[sizeof(console_pins_last_epochs) ==
	sizeof(console_pins_keys) / sizeof(*console_pins_keys) ? 1 : -1];

static int key_set(const unsigned char *key)
{
	unsigned char any = 0;
	int index;

	for (index = 0; index < 32; index++)
		any |= key[index];
	return any != 0;
}

int console_pins_have_key(void)
{
	int index;

	for (index = 0; index < KEY_COUNT; index++)
	{
		if (key_set(console_pins_keys[index]))
			return 1;
	}
	return 0;
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

static int hex_bytes(const char *text, int length, unsigned char *bytes)
{
	int index;

	for (index = 0; index < length; index++)
	{
		int high = hex_digit(text[index * 2]), low = hex_digit(text[index * 2 + 1]);

		if (high < 0 || low < 0)
			return 0;
		bytes[index] = (unsigned char)(high << 4 | low);
	}
	return 1;
}

/* ---------- the document (JSON, hostile) */

struct json
{
	const char *at;
	const char *end;
};

static void space(struct json *json)
{
	while (json->at < json->end && (*json->at == ' ' || *json->at == '\t' || *json->at == '\r' || *json->at == '\n'))
		json->at++;
}

static int take(struct json *json, char c)
{
	space(json);
	if (json->at < json->end && *json->at == c)
	{
		json->at++;
		return 1;
	}
	return 0;
}

/* a string without escapes (none of the document's has one): its text and
length, pointing into the document */
static int string(struct json *json, const char **text, int *length)
{
	const char *start;

	if (!take(json, '"'))
		return 0;
	start = json->at;
	while (json->at < json->end && *json->at != '"')
	{
		if (*json->at == '\\' || (unsigned char)*json->at < 0x20)
			return 0;
		json->at++;
	}
	if (json->at >= json->end)
		return 0;
	*text = start;
	*length = (int)(json->at - start);
	json->at++;
	return 1;
}

/* a whole number from 0 to 2^32 - 1 */
static int number(struct json *json, unsigned long *value)
{
	int digits = 0;

	space(json);
	*value = 0;
	while (json->at < json->end && *json->at >= '0' && *json->at <= '9')
	{
		if (digits++ >= 10 || *value > (0xffffffffUL - (unsigned long)(*json->at - '0')) / 10)
			return 0;
		*value = *value * 10 + (unsigned long)(*json->at++ - '0');
	}
	return digits > 0 && (json->at >= json->end || (*json->at != '.' && *json->at != 'e' && *json->at != 'E'));
}

static int skip(struct json *json, int depth);

static int skip_members(struct json *json, char close, int depth)
{
	const char *name;
	int length;

	if (take(json, close))
		return 1;
	do
	{
		if (close == '}' && (!string(json, &name, &length) || !take(json, ':')))
			return 0;
		if (!skip(json, depth + 1))
			return 0;
	} while (take(json, ','));
	return take(json, close);
}

/* any value, passed over */
static int skip(struct json *json, int depth)
{
	const char *text;
	int length;
	unsigned long value;

	if (depth > SKIP_DEPTH)
		return 0;
	space(json);
	if (json->at >= json->end)
		return 0;
	switch (*json->at)
	{
	case '"':
		return string(json, &text, &length);
	case '{':
		json->at++;
		return skip_members(json, '}', depth);
	case '[':
		json->at++;
		return skip_members(json, ']', depth);
	case 't':
		if (json->end - json->at >= 4 && !memcmp(json->at, "true", 4))
			return (json->at += 4), 1;
		return 0;
	case 'f':
		if (json->end - json->at >= 5 && !memcmp(json->at, "false", 5))
			return (json->at += 5), 1;
		return 0;
	case 'n':
		if (json->end - json->at >= 4 && !memcmp(json->at, "null", 4))
			return (json->at += 4), 1;
		return 0;
	default:
		return number(json, &value);
	}
}

static int is(const char *text, int length, const char *name)
{
	return (int)strlen(name) == length && !memcmp(text, name, (size_t)length);
}

/* one pin, 64 hex digits, into pins */
static int pin(struct json *json, struct console_pins *pins)
{
	const char *text;
	int length;

	if (!string(json, &text, &length) || length != CONSOLE_PINS_PIN_SIZE * 2 || pins->count >= CONSOLE_PINS_MAXIMUM)
		return 0;
	return hex_bytes(text, CONSOLE_PINS_PIN_SIZE, pins->pins[pins->count++]);
}

/* the site's entry: {"current": PIN, "next": PIN} (others passed over) */
static int site_entry(struct json *json, struct console_pins *pins)
{
	const char *name;
	int length, current = 0;

	if (!take(json, '{'))
		return 0;
	if (!take(json, '}'))
	{
		do
		{
			if (!string(json, &name, &length) || !take(json, ':'))
				return 0;
			if (is(name, length, "current") || is(name, length, "next"))
			{
				if (!pin(json, pins))
					return 0;
				current |= is(name, length, "current");
			}
			else if (!skip(json, 2))
				return 0;
		} while (take(json, ','));
		if (!take(json, '}'))
			return 0;
	}
	return current;
}

static int hosts(struct json *json, const char *host, struct console_pins *pins, int *found)
{
	const char *name;
	int length;

	if (!take(json, '{'))
		return 0;
	if (take(json, '}'))
		return 1;
	do
	{
		if (!string(json, &name, &length) || !take(json, ':'))
			return 0;
		if (is(name, length, host) && !*found)
		{
			if (!site_entry(json, pins))
				return 0;
			*found = 1;
		}
		else if (!skip(json, 1))
			return 0;
	} while (take(json, ','));
	return take(json, '}');
}

static int parse(const char *host, const char *document, unsigned long size, struct console_pins *pins,
	unsigned long *epoch, const char **why)
{
	struct json json;
	const char *name;
	int length, found = 0, have_format = 0, have_serial = 0, have_epoch = 0;
	unsigned long value;

	json.at = document;
	json.end = document + size;
	memset(pins, 0, sizeof(*pins));
	*why = "it is not a pin document";
	if (!take(&json, '{'))
		return 0;
	do
	{
		if (!string(&json, &name, &length) || !take(&json, ':'))
			return 0;
		if (is(name, length, "console_pins"))
		{
			if (!number(&json, &value))
				return 0;
			if (value != CONSOLE_PINS_FORMAT)
			{
				*why = "its format is not this build's";
				return 0;
			}
			have_format = 1;
		}
		else if (is(name, length, "serial"))
		{
			if (!number(&json, &pins->serial) || !pins->serial)
				return 0;
			have_serial = 1;
		}
		else if (is(name, length, "epoch"))
		{
			if (!number(&json, epoch))
				return 0;
			have_epoch = 1;
		}
		else if (is(name, length, "hosts"))
		{
			if (!hosts(&json, host, pins, &found))
				return 0;
		}
		else if (!skip(&json, 1))
			return 0;
	} while (take(&json, ','));
	if (!take(&json, '}'))
		return 0;
	space(&json);
	if (json.at != json.end || !have_format || !have_serial || !have_epoch)
		return 0;
	/* (the epoch said, and the serial's top byte, agree) */
	if (*epoch != pins->serial >> 24)
	{
		*why = "its epoch is not its serial's";
		return 0;
	}
	if (!found || !pins->count)
	{
		*why = "it has no pins for the site";
		return 0;
	}
	return 1;
}

/* ---------- public code */

int console_pins_check(const char *host, const char *document, unsigned long size, const char *signature_text,
	unsigned long signature_length, struct console_pins *pins, const char **why)
{
	unsigned char signature[CONSOLE_PINS_SIGNATURE_SIZE];
	unsigned long epoch = 0;
	int signer = -1, index;

	memset(pins, 0, sizeof(*pins));
	if (!console_pins_have_key())
	{
		*why = "this build has no pin key";
		return 0;
	}
	if (size > CONSOLE_PINS_DOCUMENT_SIZE)
	{
		*why = "it is too large";
		return 0;
	}
	while (signature_length > CONSOLE_PINS_SIGNATURE_SIZE * 2 && (signature_text[signature_length - 1] == '\n' ||
		signature_text[signature_length - 1] == '\r' || signature_text[signature_length - 1] == ' ' ||
		signature_text[signature_length - 1] == '\t'))
		signature_length--;
	if (signature_length != CONSOLE_PINS_SIGNATURE_SIZE * 2 ||
		!hex_bytes(signature_text, CONSOLE_PINS_SIGNATURE_SIZE, signature))
	{
		*why = "its signature is not 128 hex digits";
		return 0;
	}
	for (index = 0; index < KEY_COUNT && signer < 0; index++)
	{
		/* (Monocypher's check turns away an S past the group's order) */
		if (key_set(console_pins_keys[index]) &&
			crypto_ed25519_check(signature, console_pins_keys[index], (const unsigned char *)document, size) == 0)
			signer = index;
	}
	if (signer < 0)
	{
		*why = "its signature does not match";
		return 0;
	}
	if (!parse(host, document, size, pins, &epoch, why))
	{
		memset(pins, 0, sizeof(*pins));
		return 0;
	}
	if (epoch > console_pins_last_epochs[signer])
	{
		*why = "its epoch is past its key's last";
		memset(pins, 0, sizeof(*pins));
		return 0;
	}
	return 1;
}

int console_pins_check_signed(const char *host, const char *signed_form, unsigned long size,
	struct console_pins *pins, const char **why)
{
	memset(pins, 0, sizeof(*pins));
	if (size < CONSOLE_PINS_SIGNATURE_SIZE * 2 + 1 || size > CONSOLE_PINS_SIGNED_SIZE ||
		signed_form[CONSOLE_PINS_SIGNATURE_SIZE * 2] != '\n')
	{
		*why = "it is not a signed pin document";
		return 0;
	}
	return console_pins_check(host, signed_form + CONSOLE_PINS_SIGNATURE_SIZE * 2 + 1,
		size - CONSOLE_PINS_SIGNATURE_SIZE * 2 - 1, signed_form, CONSOLE_PINS_SIGNATURE_SIZE * 2, pins, why);
}
