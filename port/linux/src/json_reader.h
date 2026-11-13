/*
JSON_READER.H

A strict reader of the JSON the game's signed documents are written in (the
playlist map sets, playlist_manifest.c; Chupathingy maps' index and mirror
list, map_index.c): strings of printable ASCII escapes and UTF-8 kept as it
is, whole numbers with no sign, fraction or exponent, and any value passed
over (a field the reader does not know). No allocation, no dependency.
*/

#ifndef JSON_READER_H
#define JSON_READER_H

#include <stddef.h>
#include <string.h>

enum
{
	/* nesting a skipped value may have */
	JSON_READER_MAXIMUM_DEPTH = 8,
};

struct json
{
	const char *at;
	const char *end;
};

static inline void skip_space(struct json *json)
{
	while (json->at < json->end &&
		(*json->at == ' ' || *json->at == '\t' || *json->at == '\r' || *json->at == '\n'))
	{
		json->at++;
	}
}

static inline int take(struct json *json, char c)
{
	skip_space(json);
	if (json->at < json->end && *json->at == c)
	{
		json->at++;
		return 1;
	}
	return 0;
}

static inline int peek(struct json *json, char c)
{
	skip_space(json);
	return json->at < json->end && *json->at == c;
}

static inline int hex_digit(char c)
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
static inline int json_string(struct json *json, char *text, size_t size)
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
static inline int json_integer(struct json *json, long long minimum, long long maximum, long long *value)
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

static inline int json_literal(struct json *json, const char *word)
{
	size_t length = strlen(word);

	skip_space(json);
	if ((size_t)(json->end - json->at) < length || memcmp(json->at, word, length))
		return 0;
	json->at += length;
	return 1;
}

/* any value passed over (a field this reader does not know) */
static inline int json_skip(struct json *json, int depth)
{
	if (depth > JSON_READER_MAXIMUM_DEPTH)
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

#endif
