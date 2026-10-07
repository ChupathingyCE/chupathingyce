/*
CRASH_REPORT.C

A crash report's request body (crash_report.h): the report file's lines as
JSON, the log's last lines with their addresses taken out, the minidump in
base64. Plain C, for the platform layer and the system side alike.
*/

#include "build_identity.h"
#include "log_address.h"
#include "crash_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int crash_reports_armed(void)
{
	const char *channel = build_identity_channel();
	const char *any = getenv("HALO_CRASH_REPORTS_ANY_BUILD");

	return !strcmp(channel, "release") || !strcmp(channel, "nightly") || (any && any[0]);
}

/* ---------- text */

struct crash_text
{
	char *data;
	size_t length;
	size_t size;
	int failed;
};

static void text_append(struct crash_text *text, const char *data, size_t length)
{
	if (text->failed)
		return;
	if (text->length + length + 1 > text->size)
	{
		size_t size = (text->length + length + 1) * 2 + 1024;
		char *grown = realloc(text->data, size);

		if (!grown)
		{
			text->failed = 1;
			return;
		}
		text->data = grown;
		text->size = size;
	}
	memcpy(text->data + text->length, data, length);
	text->length += length;
	text->data[text->length] = 0;
}

static void text_string(struct crash_text *text, const char *string)
{
	text_append(text, string, strlen(string));
}

static int digit(char character)
{
	return character >= '0' && character <= '9';
}

static int hexadecimal(char character)
{
	return digit(character) || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');
}

static int word(char character)
{
	return digit(character) || (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
		character == '_';
}

/* ---------- addresses */

/* an IPv4 address at text (as addresses.py's site finds one: not part of a
longer word or number): its length, or 0 */
static size_t ipv4_at(const char *text, size_t length, unsigned char *bytes)
{
	size_t used = 0;
	int part;

	for (part = 0; part < 4; part++)
	{
		unsigned int value = 0;
		int digits = 0;

		if (part)
		{
			if (used >= length || text[used] != '.')
				return 0;
			used++;
		}
		while (used < length && digit(text[used]) && digits < 4)
		{
			value = value * 10 + (unsigned int)(text[used] - '0');
			digits++;
			used++;
		}
		if (!digits || digits > 3 || value > 255)
			return 0;
		bytes[part] = (unsigned char)value;
	}
	if (used < length && (word(text[used]) || (text[used] == '.' && used + 1 < length && digit(text[used + 1]))))
		return 0;
	return used;
}

/* an IPv6 address, the whole of text: 1 if it is one */
static int ipv6_parse(const char *text, size_t length, unsigned char *bytes)
{
	unsigned int head[8], tail[8];
	int heads = 0, tails = 0, gap = 0, index;
	size_t used = 0;

	if (length >= 2 && text[0] == ':' && text[1] == ':')
	{
		gap = 1;
		used = 2;
	}
	else if (length && text[0] == ':')
	{
		return 0;
	}
	while (used < length)
	{
		unsigned int value = 0;
		int digits = 0;

		while (used < length && hexadecimal(text[used]))
		{
			char character = text[used++];

			value = value * 16 + (unsigned int)(digit(character) ? character - '0' :
				(character | 0x20) - 'a' + 10);
			if (++digits > 4)
				return 0;
		}
		if (!digits || (gap ? tails : heads) >= 8)
			return 0;
		if (gap)
			tail[tails++] = value;
		else
			head[heads++] = value;
		if (used == length)
			break;
		/* (a colon; two once) */
		used++;
		if (used < length && text[used] == ':')
		{
			if (gap)
				return 0;
			gap = 1;
			used++;
		}
		else if (used == length)
		{
			return 0;
		}
	}
	if (gap ? heads + tails > 7 : heads != 8)
		return 0;
	memset(bytes, 0, 16);
	for (index = 0; index < heads; index++)
	{
		bytes[2 * index] = (unsigned char)(head[index] >> 8);
		bytes[2 * index + 1] = (unsigned char)head[index];
	}
	for (index = 0; index < tails; index++)
	{
		bytes[16 - 2 * (tails - index)] = (unsigned char)(tail[index] >> 8);
		bytes[17 - 2 * (tails - index)] = (unsigned char)tail[index];
	}
	return 1;
}

/* something IPv6-like at text (hexadecimal digits and two colons or more,
not part of a longer word): its length, or 0; public, if it is a public
address (one with an IPv4 address at its end, "::ffff:1.2.3.4", is taken
whole, by its IPv4 address) */
static size_t ipv6_at(const char *text, size_t length, int *public_address)
{
	unsigned char bytes[16];
	size_t used = 0;
	int colons = 0;

	while (used < length && (hexadecimal(text[used]) || text[used] == ':'))
		colons += text[used++] == ':';
	if (colons < 2)
		return 0;
	if (used < length && text[used] == '.')
	{
		size_t start = used;
		size_t ipv4;

		while (start && text[start - 1] != ':')
			start--;
		ipv4 = ipv4_at(text + start, length - start, bytes);
		if (!ipv4)
			return 0;
		*public_address = !log_address_local(bytes, 4);
		return start + ipv4;
	}
	if ((used < length && word(text[used])) || !ipv6_parse(text, used, bytes))
		return 0;
	*public_address = !log_address_local(bytes, 16);
	return used;
}

size_t crash_report_redact(const char *text, size_t length, char *out, size_t size)
{
	static const char replacement[] = "[address]";
	size_t used = 0, written = 0;

	if (!size)
		return 0;
	while (used < length)
	{
		char previous = used ? text[used - 1] : ' ';
		unsigned char bytes[4];
		size_t found = 0;
		int public_address = 0;

		if (!word(previous) && previous != '.' && previous != ':')
		{
			if ((found = ipv4_at(text + used, length - used, bytes)) != 0)
				public_address = !log_address_local(bytes, 4);
			else
				found = ipv6_at(text + used, length - used, &public_address);
		}
		if (found && public_address)
		{
			if (written + sizeof(replacement) - 1 >= size)
				break;
			memcpy(out + written, replacement, sizeof(replacement) - 1);
			written += sizeof(replacement) - 1;
			used += found;
			continue;
		}
		if (!found)
			found = 1;
		if (written + found >= size)
			break;
		memcpy(out + written, text + used, found);
		written += found;
		used += found;
	}
	out[written] = 0;
	return written;
}

/* ---------- the body */

/* text as a JSON string's contents: UTF-8 kept, other bytes '?' */
static void json_text(struct crash_text *body, const char *text, size_t length)
{
	static const char digits[] = "0123456789abcdef";
	size_t used = 0;

	while (used < length)
	{
		unsigned char character = (unsigned char)text[used];
		char escaped[8];

		if (character == '"' || character == '\\')
		{
			escaped[0] = '\\';
			escaped[1] = (char)character;
			text_append(body, escaped, 2);
			used++;
		}
		else if (character < 0x20 || character == 0x7F)
		{
			memcpy(escaped, "\\u00", 4);
			escaped[4] = digits[character >> 4];
			escaped[5] = digits[character & 15];
			text_append(body, escaped, 6);
			used++;
		}
		else if (character < 0x80)
		{
			text_append(body, text + used, 1);
			used++;
		}
		else
		{
			/* a whole UTF-8 sequence, or '?' */
			size_t count = character >= 0xF0 && character < 0xF5 ? 4 : character >= 0xE0 ? 3 :
				character >= 0xC2 && character < 0xE0 ? 2 : 0;
			size_t index;

			for (index = 1; count && index < count; index++)
			{
				if (used + index >= length || ((unsigned char)text[used + index] & 0xC0) != 0x80)
					count = 0;
			}
			if (count)
			{
				text_append(body, text + used, count);
				used += count;
			}
			else
			{
				text_append(body, "?", 1);
				used++;
			}
		}
	}
}

static void json_field(struct crash_text *body, const char *name, const char *value, size_t length)
{
	text_string(body, ", \"");
	text_string(body, name);
	text_string(body, "\": \"");
	json_text(body, value, length);
	text_string(body, "\"");
}

/* a frame as the site takes it: a module's name and an offset, with
nothing but [A-Za-z0-9_.+?-] */
static void json_frame(struct crash_text *body, const char *frame, size_t length)
{
	char clean[96];
	size_t index;

	if (length > sizeof(clean))
		length = sizeof(clean);
	for (index = 0; index < length; index++)
	{
		char character = frame[index];

		clean[index] = word(character) || character == '.' || character == '+' || character == '-' ||
			character == '?' ? character : '_';
	}
	text_string(body, "\"");
	text_append(body, clean, length);
	text_string(body, "\"");
}

static void base64(struct crash_text *body, const unsigned char *data, size_t size)
{
	static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t used;

	for (used = 0; used < size; used += 3)
	{
		unsigned int value = (unsigned int)data[used] << 16 |
			(used + 1 < size ? (unsigned int)data[used + 1] << 8 : 0) |
			(used + 2 < size ? data[used + 2] : 0);
		char quad[4];

		quad[0] = alphabet[value >> 18 & 63];
		quad[1] = alphabet[value >> 12 & 63];
		quad[2] = used + 1 < size ? alphabet[value >> 6 & 63] : '=';
		quad[3] = used + 2 < size ? alphabet[value & 63] : '=';
		text_append(body, quad, 4);
	}
}

/* the log's last lines: their start */
static size_t log_tail(const char *log, size_t size)
{
	size_t start = size, lines = 0;

	/* (a last line's newline ends it, and starts no line after it) */
	if (start && log[start - 1] == '\n')
		start--;
	while (start && size - start < CRASH_REPORT_MAXIMUM_LOG)
	{
		if (log[start - 1] == '\n' && ++lines >= CRASH_REPORT_LOG_LINES)
			break;
		start--;
	}
	/* (cut by size: from the next whole line) */
	if (start && log[start - 1] != '\n')
	{
		while (start < size && log[start - 1] != '\n')
			start++;
	}
	return start;
}

char *crash_report_body(const char *report, const char *log, size_t log_size, const void *dump, size_t dump_size,
	size_t *length)
{
	static const char *const keys[] = { "version", "channel", "commit", "platform", "architecture", "os", "exception" };
	struct crash_text body = { 0 };
	const char *line;
	int frames = 0;
	size_t key;
	char format[32];

	snprintf(format, sizeof(format), "{\"format\": %d", CRASH_REPORT_FORMAT);
	text_string(&body, format);
	/* the report file's fields, each once */
	for (key = 0; key < sizeof(keys) / sizeof(keys[0]); key++)
	{
		size_t name_length = strlen(keys[key]);

		for (line = report; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL)
		{
			size_t line_length = strcspn(line, "\r\n");

			if (line_length > name_length && !strncmp(line, keys[key], name_length) && line[name_length] == ' ')
			{
				json_field(&body, keys[key], line + name_length + 1, line_length - name_length - 1);
				break;
			}
		}
	}
	text_string(&body, ", \"frames\": [");
	for (line = report; line && *line && frames < CRASH_REPORT_FRAMES;
		line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL)
	{
		size_t line_length = strcspn(line, "\r\n");

		if (line_length > 6 && !strncmp(line, "frame ", 6))
		{
			if (frames++)
				text_string(&body, ", ");
			json_frame(&body, line + 6, line_length - 6);
		}
	}
	text_string(&body, "]");
	if (log && log_size)
	{
		size_t start = log_tail(log, log_size);
		size_t tail = log_size - start;
		char *redacted = malloc(3 * tail + 1);

		if (redacted)
		{
			size_t redacted_length = crash_report_redact(log + start, tail, redacted, 3 * tail + 1);

			json_field(&body, "log", redacted, redacted_length);
			free(redacted);
		}
	}
	if (dump && dump_size && dump_size <= CRASH_REPORT_MAXIMUM_DUMP)
	{
		text_string(&body, ", \"minidump\": \"");
		base64(&body, dump, dump_size);
		text_string(&body, "\"");
	}
	text_string(&body, "}");
	if (body.failed)
	{
		free(body.data);
		return NULL;
	}
	*length = body.length;
	return body.data;
}
